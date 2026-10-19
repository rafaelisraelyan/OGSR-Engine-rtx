#include "dx12renderprep.h"
#include "dx12camera.h"
#include "dx12log.h"
#include "gr/jobs/JobSystem.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#ifdef GR_JOBS_TESTING
#include "../JobSystem/tests/JobTestHooks.h"
#include "tests/visibility_test_hooks.h"
#endif

namespace renderprep12 {
namespace {
using namespace DirectX;
using Clock = std::chrono::steady_clock;
double Elapsed(Clock::time_point t) { return std::chrono::duration<double,std::milli>(Clock::now()-t).count(); }

// Только представитель данных: не выполняет R4 traversal и не разрешает GPU handles.
std::vector<Object12> Scene(uint32_t count) {
    std::vector<Object12> objects(count);
    uint32_t seed=0x13579bdu;
    const auto random=[&]() { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return float(seed&65535)/65535.f; };
    for (uint32_t i=0;i<count;++i) {
        auto& o=objects[i];
        o.id=0x100000000ull+i; o.firstVertex=(i%2)*36; o.vertexCount=36;
        o.minimum={-1,-2,-0.5f}; o.maximum={1,2,0.5f};
        o.sphere={{0,0,0},2.3f};
        const float sx=(i%3?-1.f:1.f)*(0.1f+3*random()), sy=0.2f+2*random(), sz=0.3f+4*random();
        const float rx=random()*6, ry=random()*6, rz=random()*6;
        const float tx=(random()-0.5f)*200, ty=(random()-0.5f)*100, tz=random()*200;
        XMMATRIX local=XMMatrixScaling(sx,sy,sz);
        if (i%7==0) { XMFLOAT4X4 shear; XMStoreFloat4x4(&shear,XMMatrixIdentity()); shear._12=0.7f; local*=XMLoadFloat4x4(&shear); }
        XMStoreFloat4x4(&o.world,local*XMMatrixRotationRollPitchYaw(rx,ry,rz)*XMMatrixTranslation(tx,ty,tz));
        o.visibility={i%9,i%17,~0u,i%2==0};
        o.flags=(i%11==0?Hud:0)|(i%13==0?Invisible:0)|(i%5==0?Transparent:0);
        o.opacity=(o.flags&Transparent)?0.4f:1.f;
        o.visual=SyntheticResources12[i%4]; o.material=SyntheticResources12[(i+1)%4]; o.pso=SyntheticResources12[(i+2)%4];
    }
    return objects;
}

// Независимый double oracle: center/extents * abs(linear), не восемь SIMD-углов kernel.
bool BoundsOracle(const DrawPacket12& p) {
    const auto& o=p.object;
    const double center[]={(double(o.minimum.x)+o.maximum.x)/2,(double(o.minimum.y)+o.maximum.y)/2,(double(o.minimum.z)+o.maximum.z)/2};
    const double extent[]={(double(o.maximum.x)-o.minimum.x)/2,(double(o.maximum.y)-o.minimum.y)/2,(double(o.maximum.z)-o.minimum.z)/2};
    const float low[]={p.worldMinimum.x,p.worldMinimum.y,p.worldMinimum.z}, high[]={p.worldMaximum.x,p.worldMaximum.y,p.worldMaximum.z};
    for (int j=0;j<3;++j) {
        double c=o.world.m[3][j], e=0;
        for (int i=0;i<3;++i) { c+=center[i]*o.world.m[i][j]; e+=extent[i]*std::abs(double(o.world.m[i][j])); }
        if (std::abs(low[j]-(c-e))>0.0001 || std::abs(high[j]-(c+e))>0.0001) return false;
    }
    // Точки сферы вдоль осей и диагоналей обязаны остаться внутри консервативного радиуса.
    for (int a=-1;a<=1;++a) for(int b=-1;b<=1;++b) for(int c=-1;c<=1;++c) {
        if (!a&&!b&&!c) continue;
        const double norm=std::sqrt(double(a*a+b*b+c*c));
        const double local[]={o.sphere.center.x+a*o.sphere.radius/norm,o.sphere.center.y+b*o.sphere.radius/norm,o.sphere.center.z+c*o.sphere.radius/norm};
        const double sphereCenter[]={p.worldSphere.center.x,p.worldSphere.center.y,p.worldSphere.center.z};
        double distance2=0;
        for(int j=0;j<3;++j) {
            double v=o.world.m[3][j]; for(int i=0;i<3;++i) v+=local[i]*o.world.m[i][j];
            distance2+=(v-sphereCenter[j])*(v-sphereCenter[j]);
        }
        if (std::sqrt(distance2)>double(p.worldSphere.radius)+0.0001) return false;
    }
    return true;
}
}

bool RunRepresentativeTests() {
    uint64_t checks=0,failures=0;
    const auto check=[&](bool b) { ++checks; if(!b) ++failures; };
    const auto rejects=[&](auto f) { bool threw=false; try { f(); } catch(const std::exception&) { threw=true; } check(threw); };
    try {
        SmokeCamera12 camera; camera.SetAspect(16.f/9); camera.SetOrbit(1);
        auto source=Scene(256);
        const RenderSceneSnapshot12 captured(camera.CaptureViewProjection(),source,42);
        source.clear(); source.shrink_to_fit(); // Источник может исчезнуть после capture.
        Timings12 t;
        const auto reference=Prepare(captured,42,Mode::Serial,nullptr,t);
        check(reference.packets.size()==256);
        // Точный контрпример потери консервативности при float-округлении.
        auto point=Scene(1);
        XMStoreFloat4x4(&point[0].world,XMMatrixTranslation(16777216.f,0,0));
        point[0].minimum=point[0].maximum=point[0].sphere.center={0.5f,0,0};
        point[0].sphere.radius=0;
        const auto rounded=Prepare(RenderSceneSnapshot12(camera.CaptureViewProjection(),point,42),42,Mode::Serial,nullptr,t);
        check(double(rounded.packets[0].worldMaximum.x)>=16777216.5);
        check(double(rounded.packets[0].worldSphere.radius)>=std::abs(double(rounded.packets[0].worldSphere.center.x)-16777216.5));
        // Строгое включение без допуска: rotation, negative scale, shear и cancellation.
        for (unsigned shape=0;shape<4;++shape) {
            auto exact=point;
            auto& o=exact[0];
            o.minimum={-0.5f,-0.25f,-1}; o.maximum={0.5f,0.25f,1};
            XMStoreFloat4x4(&o.world,XMMatrixTranslation(16777216.f,-16777216.f,0));
            if (shape==0) { o.world._11=0; o.world._12=1; o.world._21=-1; o.world._22=0; }
            if (shape==1) { o.world._11=-2; o.world._22=3; o.world._33=0.5f; }
            if (shape==2) { o.world._12=0.5f; o.world._21=-0.25f; }
            if (shape==3) { o.world._11=16777216.f; o.world._21=-16777216.f; }
            const auto frame=Prepare(RenderSceneSnapshot12(camera.CaptureViewProjection(),exact,42),42,Mode::Serial,nullptr,t);
            const auto& p=frame.packets[0];
            const float lo[]={o.minimum.x,o.minimum.y,o.minimum.z},hi[]={o.maximum.x,o.maximum.y,o.maximum.z};
            const float lower[]={p.worldMinimum.x,p.worldMinimum.y,p.worldMinimum.z},upper[]={p.worldMaximum.x,p.worldMaximum.y,p.worldMaximum.z};
            for (int corner=0;corner<8;++corner) for(int j=0;j<3;++j) {
                double v=o.world.m[3][j];
                for(int i=0;i<3;++i) v+=double((corner&(1<<i))?hi[i]:lo[i])*o.world.m[i][j];
                check(double(lower[j])<=v && double(upper[j])>=v);
            }
        }
        rejects([&] { Prepare(captured,43,Mode::Serial,nullptr,t); });
        rejects([&] { Prepare(captured,42,Mode::Parallel,nullptr,t); });
        const RenderSceneSnapshot12 empty(camera.CaptureViewProjection(),{},42);
        check(Prepare(empty,42,Mode::Serial,nullptr,t).packets.empty());
        for (uint32_t workers : {1u,2u,4u,8u}) {
            gr::jobs::JobSystem jobs; gr::jobs::JobConfig config;
            config.backgroundWorkers=workers; config.automaticWorkers=false; check(jobs.Initialize(config));
            auto published=reference;
            for (unsigned fault=0;fault<10;++fault) {
                auto bad=Scene(256); auto& o=bad.back();
                switch(fault) {
                case 0: ++o.visual.generation; break;
                case 1: o.material.id=99; break;
                case 2: ++o.pso.generation; break;
                case 3: o.minimum.x=o.maximum.x+1; break;
                case 4: o.maximum.z=std::numeric_limits<float>::quiet_NaN(); break;
                case 5: o.sphere.radius=-1; break;
                case 6: o.flags=0x80000000u; break;
                case 7: o.flags=0; o.opacity=0.5f; break;
                case 8: o.world._11=std::numeric_limits<float>::infinity(); break;
                case 9: o.world._14=1; break;
                }
                const RenderSceneSnapshot12 invalid(camera.CaptureViewProjection(),bad,42);
                rejects([&] { Prepare(invalid,42,Mode::Serial,nullptr,t); });
                rejects([&] { published=Prepare(invalid,42,Mode::Parallel,&jobs,t); });
                check(Equivalent(reference,published));
            }
            check(Equivalent(reference,Prepare(captured,42,Mode::Verify,&jobs,t)));
            for(uint32_t count : {256u,1024u,4096u,16384u}) {
                auto objects=Scene(count);
                for (float time : {0.f,2.f,100.f}) {
                    camera.SetOrbit(time);
                    const RenderSceneSnapshot12 snapshot(camera.CaptureViewProjection(),objects,42);
                    const auto result=Prepare(snapshot,42,Mode::Verify,&jobs,t);
                    check(result.packets.size()==count);
                    for (uint32_t i=0;i<count;++i) {
                        const auto& p=result.packets[i];
                        check(p.object.id==objects[i].id && p.object.visibility==objects[i].visibility &&
                            p.object.visual==objects[i].visual && p.object.flags==objects[i].flags);
                        check(BoundsOracle(p));
                        if(objects[i].flags&Invisible) check(!p.visible);
                        else if(objects[i].flags&Hud) check(p.visible);
                    }
                }
                // Snapshot и Prepare измеряются отдельно; порядок путей чередуется.
                double capture=0, serial=0, parallel=0, submit=0, wait=0, serialMerge=0, parallelMerge=0;
                double serialTotal=0,parallelTotal=0;
                for(unsigned frame=0;frame<120;++frame) {
                    camera.SetOrbit(float(frame)/60);
                    const auto begin=Clock::now();
                    const RenderSceneSnapshot12 snapshot(camera.CaptureViewProjection(),objects,42);
                    const double captureMs=Elapsed(begin);
                    Timings12 s,p; PreparedFrame12 a,b;
                    if(frame%2) { b=Prepare(snapshot,42,Mode::Parallel,&jobs,p); a=Prepare(snapshot,42,Mode::Serial,nullptr,s); }
                    else { a=Prepare(snapshot,42,Mode::Serial,nullptr,s); b=Prepare(snapshot,42,Mode::Parallel,&jobs,p); }
                    check(Equivalent(a,b));
                    if(frame>=20) {
                        capture+=captureMs; serial+=s.serialPrepare; parallel+=p.parallelPrepare;
                        submit+=p.submit; wait+=p.wait; serialMerge+=s.serialMerge; parallelMerge+=p.parallelMerge;
                        serialTotal+=s.serial; parallelTotal+=p.parallel;
                    }
                }
                dx12log::Write("[snapshot-bench] objects=%u workers=%u samples=100 snapshot=%.6f serialPrepare=%.6f parallelPrepare=%.6f submit=%.6f wait=%.6f serialMerge=%.6f parallelMerge=%.6f serialTotal=%.6f parallelTotal=%.6f ms",
                    count,workers,capture/100,serial/100,parallel/100,submit/100,wait/100,serialMerge/100,parallelMerge/100,serialTotal/100,parallelTotal/100);
            }
            jobs.Shutdown();
            rejects([&] { Prepare(captured,42,Mode::Parallel,&jobs,t); });
            check(jobs.Initialize(config));
            check(Equivalent(reference,Prepare(captured,42,Mode::Verify,&jobs,t)));
            jobs.Shutdown();
        }
    } catch(const std::exception& e) { ++failures; dx12log::Write("[snapshot-test] exception: %s",e.what()); }
    dx12log::Write("[snapshot-test] %s checks=%llu failures=%llu",failures?"FAIL":"PASS",(unsigned long long)checks,(unsigned long long)failures);
    return failures==0;
}
}

namespace renderprep12 {
namespace {
// Независимый геометрический oracle для двух известных frustum.
// Ortho: пересечение с [-1,1]² x [0,1]. Perspective: сечения [-z,z]²,
// z в [1,2]; достаточно самого широкого доступного сечения. Плоскости
// из camera matrix не извлекаются; production support/interval helpers не вызываются.
bool FrustumOracle(const DrawPacket12& p, bool perspective) {
    const auto& o=p.object;
    if ((o.flags&Invisible) || o.opacity==0 || !o.visibility.mask) return false;
    if (o.flags&Hud) return true;
    const double zlo=(std::max)(double(p.worldMinimum.z),perspective?1.0:0.0);
    const double zhi=(std::min)(double(p.worldMaximum.z),perspective?2.0:1.0);
    if (zlo>zhi) return false;
    const double extent=perspective?zhi:1.0;
    return double(p.worldMinimum.x)<=extent && double(p.worldMaximum.x)>=-extent &&
        double(p.worldMinimum.y)<=extent && double(p.worldMaximum.y)>=-extent;
}
DirectX::XMFLOAT4X4 OracleCamera(bool perspective) {
    DirectX::XMFLOAT4X4 m;
    DirectX::XMStoreFloat4x4(&m,DirectX::XMMatrixIdentity());
    if (perspective) { m._33=2; m._34=1; m._43=-2; m._44=0; }
    return m;
}
}
bool RunVisibilityTests() {
    uint64_t checks=0,failures=0;
    const auto check=[&](bool value) { ++checks; if (!value) ++failures; };
    const auto rejects=[&](auto&& action) { bool threw=false; try { action(); } catch(const std::exception&) { threw=true; } check(threw); };
    try {
        for (uint32_t workers:{1u,2u,4u,8u}) {
            gr::jobs::JobSystem jobs; gr::jobs::JobConfig config;
            config.backgroundWorkers=workers; config.automaticWorkers=false; check(jobs.Initialize(config));
            VisibilityTimings12 timing;
            for (bool perspective:{false,true}) {
                const auto camera=OracleCamera(perspective);
                // Границы блоков, пустой/полностью видимый/полностью скрытый список.
                for (uint32_t count:{0u,1u,127u,128u,129u,255u,256u,257u}) {
                    for (unsigned pattern=0;pattern<3;++pattern) {
                        auto objects=Scene(count);
                        for (uint32_t i=0;i<count;++i) {
                            auto& o=objects[i];
                            DirectX::XMStoreFloat4x4(&o.world,DirectX::XMMatrixIdentity());
                            o.minimum={-0.1f,-0.1f,1}; o.maximum={0.1f,0.1f,1};
                            o.flags=pattern==1 || (pattern==2 && i%3==0)?Invisible:0; o.opacity=1;
                            // Порядок ID намеренно обратный: compaction не должна сортировать.
                            o.id=count-i;
                        }
                        const RenderSceneSnapshot12 snapshot(camera,objects,42);
                        Timings12 oldTime; auto expected=Prepare(snapshot,42,Mode::Serial,nullptr,oldTime);
                        std::erase_if(expected.packets,[&](auto& p) { p.visible=FrustumOracle(p,perspective); return !p.visible; });
                        for (Mode mode:{Mode::Serial,Mode::Parallel,Mode::Verify})
                            check(Equivalent(expected,PrepareVisible(snapshot,42,mode,&jobs,timing)));
                    }
                }
                std::vector<Object12> cases;
                const auto add=[&](DirectX::XMFLOAT3 lo,DirectX::XMFLOAT3 hi,uint32_t flags=0) {
                    auto o=Scene(1).front(); o.id=cases.size()+1; o.minimum=lo; o.maximum=hi;
                    o.flags=flags; o.opacity=1;
                    DirectX::XMStoreFloat4x4(&o.world,DirectX::XMMatrixIdentity()); cases.push_back(o);
                };
                const float nearZ=perspective?1.f:0.f,farZ=perspective?2.f:1.f;
                for (float z:{std::nextafter(nearZ,-INFINITY),nearZ,std::nextafter(nearZ,INFINITY),
                              std::nextafter(farZ,-INFINITY),farZ,std::nextafter(farZ,INFINITY)}) add({0,0,z},{0,0,z});
                for (float edge:{-1.f,1.f}) for (float x:{std::nextafter(edge,-INFINITY),edge,std::nextafter(edge,INFINITY)}) {
                    add({x,0,1},{x,0,1}); add({0,x,1},{0,x,1});
                }
                add({-10,-10,-10},{10,10,10}); // Frustum целиком внутри AABB; ни один угол не внутри frustum.
                add({-0.1f,-0.1f,-1},{0.1f,0.1f,1.5f}); // Охват камеры и near plane.
                add({-0.1f,-0.1f,-2},{0.1f,0.1f,-1});
                add({-2,-0.1f,0.5f},{2,0.1f,1.5f});
                add({100,100,100},{101,101,101},Hud);
                add({100,100,100},{101,101,101},Hud|Invisible);
                add({0,0,1},{0,0,1},Transparent); cases.back().opacity=0;
                add({0,0,1},{0,0,1},Hud); cases.back().visibility.mask=0;
                // Детерминированные AABB на бинарной сетке, oracle с точной геометрией.
                uint32_t seed=7931;
                const auto sample=[&]() { seed=1664525u*seed+1013904223u; return float((seed>>16)%65)/8-4; };
                for (unsigned i=0;i<1000;++i) {
                    const float x=sample(),y=sample(),z=sample();
                    add({x,y,z},{x+0.5f,y+0.25f,z+0.5f});
                }
                const RenderSceneSnapshot12 snapshot(camera,cases,42);
                Timings12 oldTime; const auto unfiltered=Prepare(snapshot,42,Mode::Serial,nullptr,oldTime);
                const auto expected=PrepareVisible(snapshot,42,Mode::Serial,nullptr,timing);
                for (Mode mode:{Mode::Serial,Mode::Parallel,Mode::Verify}) {
                    const auto actual=PrepareVisible(snapshot,42,mode,&jobs,timing);
                    check(Equivalent(expected,actual));
                    auto metadata=unfiltered;
                    std::erase_if(metadata.packets,[&](auto& p) {
                        p.visible=true;
                        return !std::any_of(actual.packets.begin(),actual.packets.end(),[&](const auto& q) { return q.object.id==p.object.id; });
                    });
                    check(Equivalent(metadata,actual)); // Все поля и порядок исходного snapshot.
                    for (size_t i=0;i<unfiltered.packets.size();++i) {
                        const auto& p=unfiltered.packets[i];
                        const bool oracle=FrustumOracle(p,perspective);
                        const bool present=std::any_of(actual.packets.begin(),actual.packets.end(),[&](const auto& q) { return q.object.id==p.object.id; });
                        // Для произвольных AABB допускается консервативное лишнее включение
                        // у рёбер frustum, но ни одного false negative относительно точного oracle.
                        check(!oracle || present);
                        if (i<26) check(oracle==present);
                    }
                }
                auto published=expected;
#ifdef GR_JOBS_TESTING
                // Отдельная ASan-сборка: реальные ветки отказа регистрации и Submit.
                for (Mode mode:{Mode::Parallel,Mode::Verify}) {
                    for (auto phase:{testing::Phase::Counts,testing::Phase::Scatter}) {
                        const auto before=jobs.GetStats();
                        const auto failuresBefore=testing::failures.load();
                        check(testing::active==0 && testing::entered==testing::exited);
                        testing::failPhase=phase;
                        rejects([&] { published=PrepareVisible(snapshot,42,mode,&jobs,timing); });
                        check(testing::failPhase==testing::Phase::None && testing::failures==failuresBefore+1);
                        check(testing::active==0 && testing::entered==testing::exited);
                        const auto after=jobs.GetStats();
                        check(after.tasksSubmitted==after.tasksCompleted &&
                            after.tasksSubmitted-before.tasksSubmitted==(phase==testing::Phase::Counts?3u:4u));
                        check(after.jobFailures==before.jobFailures+1 && after.unobservedFailures==before.unobservedFailures);
                        check(Equivalent(published,expected));
                        // После исключения та же система и snapshot вновь пригодны к работе.
                        published=PrepareVisible(snapshot,42,mode,&jobs,timing);
                        check(Equivalent(published,expected));
                        check(testing::active==0 && testing::entered==testing::exited);
                    }
                    gr::jobs::testing::failRegistration=true;
                    rejects([&] { published=PrepareVisible(snapshot,42,mode,&jobs,timing); });
                    check(!gr::jobs::testing::failRegistration && Equivalent(published,expected));
                    gr::jobs::testing::failSubmit=true;
                    rejects([&] { published=PrepareVisible(snapshot,42,mode,&jobs,timing); });
                    check(!gr::jobs::testing::failSubmit && Equivalent(published,expected));
                }
#endif
                for (unsigned fault=0;fault<5;++fault) {
                    auto bad=cases;
                    auto& o=bad.back();
                    if (fault==0) ++o.visual.generation;
                    if (fault==1) o.minimum.x=o.maximum.x+1;
                    if (fault==2) o.flags=0x80000000;
                    if (fault==3) o.world._11=std::numeric_limits<float>::quiet_NaN();
                    if (fault==4) o.opacity=-1;
                    const RenderSceneSnapshot12 invalid(camera,bad,42);
                    for (Mode mode:{Mode::Serial,Mode::Parallel,Mode::Verify}) {
                        rejects([&] { published=PrepareVisible(invalid,42,mode,&jobs,timing); });
                        check(Equivalent(published,expected));
                    }
                }
                rejects([&] { published=PrepareVisible(snapshot,43,Mode::Parallel,&jobs,timing); });
                rejects([&] { published=PrepareVisible(snapshot,42,Mode::Parallel,nullptr,timing); });
                rejects([&] { published=PrepareVisible(snapshot,42,static_cast<Mode>(99),&jobs,timing); });
                check(Equivalent(published,expected));
                jobs.Shutdown();
                rejects([&] { published=PrepareVisible(snapshot,42,Mode::Verify,&jobs,timing); });
                check(Equivalent(published,expected));
                check(jobs.Initialize(config));
                check(Equivalent(expected,PrepareVisible(snapshot,42,Mode::Verify,&jobs,timing)));
            }
            for (uint32_t count:{256u,1024u,4096u,16384u}) {
                const auto objects=Scene(count);
                SmokeCamera12 camera; camera.SetAspect(16.f/9); camera.SetOrbit(1);
                const RenderSceneSnapshot12 snapshot(camera.CaptureViewProjection(),objects,42);
                VisibilityPhases12 sums[2]{};
                size_t visible=0;
                for (unsigned sample=0;sample<120;++sample) {
                    VisibilityTimings12 s,p; PreparedFrame12 a,b;
                    if (sample%2) { b=PrepareVisible(snapshot,42,Mode::Parallel,&jobs,p); a=PrepareVisible(snapshot,42,Mode::Serial,nullptr,s); }
                    else { a=PrepareVisible(snapshot,42,Mode::Serial,nullptr,s); b=PrepareVisible(snapshot,42,Mode::Parallel,&jobs,p); }
                    check(Equivalent(a,b)); visible=a.packets.size();
                    uint64_t previous=0;
                    for (const auto& packet:a.packets) { check(packet.visible && packet.object.id>previous); previous=packet.object.id; }
                    if (sample>=20) {
                        const VisibilityPhases12 phases[]={s.serial,p.parallel};
                        for (unsigned i=0;i<2;++i) {
                            sums[i].preparation+=phases[i].preparation; sums[i].visibility+=phases[i].visibility;
                            sums[i].blockCounts+=phases[i].blockCounts; sums[i].prefix+=phases[i].prefix;
                            sums[i].scatter+=phases[i].scatter; sums[i].total+=phases[i].total;
                        }
                    }
                }
                check(Equivalent(PrepareVisible(snapshot,42,Mode::Serial,nullptr,timing),PrepareVisible(snapshot,42,Mode::Verify,&jobs,timing)));
                for (unsigned i=0;i<2;++i) {
                    const auto& t=sums[i];
                    dx12log::Write("[visibility-bench] objects=%u visible=%llu workers=%u mode=%s samples=100 preparation=%.6f visibility=%.6f counts=%.6f prefix=%.6f scatter=%.6f total=%.6f ms",
                        count,(unsigned long long)visible,workers,i?"parallel":"serial",t.preparation/100,t.visibility/100,t.blockCounts/100,t.prefix/100,t.scatter/100,t.total/100);
                }
            }
            jobs.Shutdown();
        }
    } catch(const std::exception& e) { ++failures; dx12log::Write("[visibility-test] exception: %s",e.what()); }
    dx12log::Write("[visibility-test] %s checks=%llu failures=%llu",failures?"FAIL":"PASS",(unsigned long long)checks,(unsigned long long)failures);
    return failures==0;
}
}
