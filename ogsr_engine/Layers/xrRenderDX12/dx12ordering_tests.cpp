#include "dx12renderprep.h"
#include "dx12draworder_internal.h"
#include "dx12log.h"
#include "gr/jobs/JobSystem.h"
#include <algorithm>
#include <cmath>
#include <limits>
#ifdef GR_JOBS_TESTING
#include "tests/visibility_test_hooks.h"
#endif

namespace renderprep12 {
namespace {
using namespace DirectX;
XMFLOAT4X4 Identity() { XMFLOAT4X4 m; XMStoreFloat4x4(&m,XMMatrixIdentity()); return m; }
XMFLOAT4X4 Projection() { auto p=Identity(); p._11=p._22=0.001f; p._33=0.01f; return p; }
Object12 Object(uint64_t id,uint32_t flags,float z) {
    Object12 o{}; o.id=id; o.flags=flags; o.vertexCount=36;
    o.world=Identity(); o.world._43=z;
    o.minimum={-0.5f,-0.5f,-0.5f}; o.maximum={0.5f,0.5f,0.5f}; o.sphere={{0,0,0},1};
    o.visibility={uint32_t(id%11),uint32_t(id%7),~0u,true};
    return o;
}
std::vector<Object12> SmallScene() {
    std::vector<Object12> o{Object(90,Hud|Transparent,10),Object(80,Transparent,10),Object(70,0,20),
        Object(60,Transparent,30),Object(50,0,20),Object(40,0,20),Object(30,0,20),
        Object(20,Transparent,30),Object(10,Hud,20),Object(5,0,20)};
    o[2].pso=SyntheticResources12[1]; o[4].material=SyntheticResources12[1]; o[5].visual=SyntheticResources12[1];
    return o;
}
std::vector<Object12> LargeScene(uint32_t count) {
    std::vector<Object12> out; out.reserve(count);
    for (uint32_t i=0;i<count;++i) {
        auto o=Object(100000+count-i,(i%9==0?Hud:0)|(i%3==0?Transparent:0)|(i%19==0?Invisible:0),float(i%97)+1);
        XMStoreFloat4x4(&o.world,XMMatrixScaling(i%2?-1.f:1.f,1.2f,0.8f)*XMMatrixRotationY(float(i%13)*0.1f)*XMMatrixTranslation(float(int(i%101)-50),float(int(i%53)-26),float(i%97)+1));
        o.pso=SyntheticResources12[i%4]; o.material=SyntheticResources12[(i/4)%4]; o.visual=SyntheticResources12[(i/16)%4];
        out.push_back(o);
    }
    return out;
}
// Независимый ожидаемый порядок задан вручную, без production comparator.
OrderedFrame12 Expected(const RenderSceneSnapshot12& s, std::initializer_list<uint64_t> ids,
    std::array<PacketRange12,3> ranges) {
    VisibilityTimings12 t;
    const auto visible=PrepareVisible(s,42,Mode::Serial,nullptr,t);
    OrderedFrame12 e{}; e.frame.shaderCamera=s.ShaderCamera(); e.frame.generation=42; e.categories=ranges;
    for (auto id:ids) {
        const auto it=std::find_if(visible.packets.begin(),visible.packets.end(),[&](const auto& p) { return p.object.id==id; });
        if (it==visible.packets.end()) throw std::logic_error("missing oracle object");
        e.frame.packets.push_back(*it);
    }
    return e;
}
}
bool RunOrderingTests() {
    uint64_t checks=0,failures=0;
    const auto check=[&](bool b) { ++checks; if (!b) ++failures; };
    const auto rejects=[&](auto&& f) { bool threw=false; try { f(); } catch(const std::exception&) { threw=true; } check(threw); };
    try {
        check(detail::PacketCount(UINT32_MAX)==UINT32_MAX);
        rejects([&] { detail::PacketCount(size_t(UINT32_MAX)+1); });
        rejects([&] { detail::Depth(std::numeric_limits<double>::quiet_NaN()); });
        rejects([&] { detail::Depth(INFINITY); }); rejects([&] { detail::Depth(-INFINITY); });
        check(!std::signbit(detail::Depth(-0.0)) && detail::Depth(+0.0)==0);
        // Поколения — часть каждого ключа, даже если текущая синтетическая таблица
        // не содержит одновременно два поколения одного id.
        for (unsigned field=0;field<3;++field) {
            detail::OrderKey a{},b{}; a.object=99; b.object=1;
            if (field==0) b.pso.generation=2;
            if (field==1) b.material.generation=2;
            if (field==2) b.geometry.generation=2;
            check(detail::Less(a,b) && !detail::Less(b,a));
        }
        detail::OrderKey a{},b{}; a.object=b.object=7; a.source=1; b.source=2;
        check(detail::Less(a,b) && !detail::Less(b,a));
        a.category=b.category=DrawCategory12::Transparent;
        a.depth=detail::Depth(-0.0); b.depth=detail::Depth(0.0);
        check(detail::Less(a,b) && !detail::Less(b,a));
        std::vector<detail::OrderKey> keys(12);
        for (unsigned i=0;i<keys.size();++i) {
            keys[i].category=static_cast<DrawCategory12>(i%3); keys[i].object=12-i;
            keys[i].source=i; keys[i].depth=i%2; keys[i].material.generation=1+i%2;
        }
        for (const auto& x:keys) for (const auto& y:keys) {
            check(!detail::Less(x,x));
            check(!(detail::Less(x,y)&&detail::Less(y,x)));
            for (const auto& z:keys) if (detail::Less(x,y)&&detail::Less(y,z)) check(detail::Less(x,z));
        }
        const auto objects=SmallScene();
        auto view=Identity(), projection=Projection();
        const RenderSceneSnapshot12 snapshot(view,projection,objects,42);
        view._43=100; projection._33=100; // После capture источник камеры можно менять.
        check(snapshot.View()._43==0 && snapshot.Camera()._33==0.01f);
        const auto expected=Expected(snapshot,{5,30,40,50,70,20,60,80,90,10},{{{0,5},{5,3},{8,2}}});
        OrderingTimings12 timing;
        rejects([&] { PrepareOrdered(RenderSceneSnapshot12(snapshot.Camera(),objects,42),42,Mode::Serial,nullptr,timing); });
        for (float bad:{std::numeric_limits<float>::quiet_NaN(),INFINITY,-INFINITY}) {
            auto invalid=Identity(); invalid._33=bad;
            rejects([&] { RenderSceneSnapshot12(invalid,Projection(),objects,42); });
            rejects([&] { RenderSceneSnapshot12(Identity(),invalid,objects,42); });
        }
        auto overflow=Identity(); overflow._11=std::numeric_limits<float>::max();
        rejects([&] { RenderSceneSnapshot12(overflow,overflow,objects,42); });
        for (uint32_t workers:{1u,2u,4u,8u}) {
            gr::jobs::JobSystem jobs; gr::jobs::JobConfig config;
            config.backgroundWorkers=workers; config.automaticWorkers=false; check(jobs.Initialize(config));
            for (Mode mode:{Mode::Serial,Mode::Parallel,Mode::Verify}) {
                check(Equivalent(expected,PrepareOrdered(snapshot,42,mode,&jobs,timing)));
                auto reverse=Identity(); reverse._11=-1; reverse._33=-1; reverse._43=100;
                const RenderSceneSnapshot12 back(reverse,Projection(),objects,42);
                check(Equivalent(Expected(back,{5,30,40,50,70,80,20,60,90,10},expected.categories),PrepareOrdered(back,42,mode,&jobs,timing)));
                auto side=Identity(); side._11=side._33=0; side._13=1; side._31=-1; side._43=50;
                const RenderSceneSnapshot12 turned(side,Projection(),objects,42);
                check(Equivalent(Expected(turned,{5,30,40,50,70,20,60,80,90,10},expected.categories),PrepareOrdered(turned,42,mode,&jobs,timing)));
                // Глубина центра bounds, а не translation world: отражённые смещённые bounds.
                std::vector<Object12> shifted{Object(1,Transparent,50),Object(2,Transparent,35)};
                shifted[0].minimum.z=20; shifted[0].maximum.z=22; shifted[0].world._33=-1;
                shifted[0].sphere={{0,0,21},2};
                const RenderSceneSnapshot12 centers(Identity(),Projection(),shifted,42);
                check(Equivalent(Expected(centers,{2,1},{{{0,0},{0,2},{2,0}}}),PrepareOrdered(centers,42,mode,&jobs,timing)));
                shifted={Object(2,Transparent,-0.0f),Object(1,Transparent,0.0f)};
                const RenderSceneSnapshot12 zeros(Identity(),Projection(),shifted,42);
                check(Equivalent(Expected(zeros,{1,2},{{{0,0},{0,2},{2,0}}}),PrepareOrdered(zeros,42,mode,&jobs,timing)));
                const RenderSceneSnapshot12 empty(Identity(),Projection(),{},42);
                check(Equivalent(Expected(empty,{},{}),PrepareOrdered(empty,42,mode,&jobs,timing)));
                for (unsigned category=0;category<3;++category) {
                    std::vector<Object12> subset;
                    for (const auto& o:objects) if ((o.flags&Hud?2u:o.flags&Transparent?1u:0u)==category) subset.push_back(o);
                    const RenderSceneSnapshot12 only(Identity(),Projection(),subset,42);
                    auto reference=expected;
                    std::erase_if(reference.frame.packets,[&](const auto& p) { return (p.object.flags&Hud?2u:p.object.flags&Transparent?1u:0u)!=category; });
                    uint32_t offset=0;
                    for (unsigned c=0;c<3;++c) { reference.categories[c]={offset,c==category?uint32_t(subset.size()):0}; offset+=reference.categories[c].count; }
                    check(Equivalent(reference,PrepareOrdered(only,42,mode,&jobs,timing)));
                }
                auto hidden=objects; for (auto& o:hidden) o.flags|=Invisible;
                const RenderSceneSnapshot12 none(Identity(),Projection(),hidden,42);
                check(Equivalent(Expected(none,{},{}),PrepareOrdered(none,42,mode,&jobs,timing)));
            }
            auto published=expected;
            for (unsigned resource=0;resource<3;++resource) {
                auto stale=objects;
                if (resource==0) ++stale[0].pso.generation;
                if (resource==1) ++stale[0].material.generation;
                if (resource==2) ++stale[0].visual.generation;
                const RenderSceneSnapshot12 invalid(Identity(),Projection(),stale,42);
                for (Mode mode:{Mode::Serial,Mode::Parallel,Mode::Verify}) {
                    rejects([&] { published=PrepareOrdered(invalid,42,mode,&jobs,timing); });
                    check(Equivalent(expected,published));
                }
            }
            rejects([&] { published=PrepareOrdered(snapshot,43,Mode::Parallel,&jobs,timing); });
            rejects([&] { published=PrepareOrdered(snapshot,42,Mode::Parallel,nullptr,timing); });
            rejects([&] { published=PrepareOrdered(snapshot,42,static_cast<Mode>(9),&jobs,timing); });
            check(Equivalent(expected,published));
#ifdef GR_JOBS_TESTING
            const auto faultObjects=LargeScene(1024);
            const RenderSceneSnapshot12 faultSnapshot(Identity(),Projection(),faultObjects,42);
            const auto faultReference=PrepareOrdered(faultSnapshot,42,Mode::Serial,nullptr,timing);
            published=faultReference;
            for (Mode mode:{Mode::Parallel,Mode::Verify}) for (auto phase:{testing::Phase::Counts,testing::Phase::Scatter,testing::Phase::Keys,testing::Phase::Ordering,testing::Phase::OrderedScatter}) {
                const auto before=jobs.GetStats(); const auto failBefore=testing::failures.load();
                testing::failPhase=phase;
                rejects([&] { published=PrepareOrdered(faultSnapshot,42,mode,&jobs,timing); });
                check(testing::failPhase==testing::Phase::None && testing::failures==failBefore+1);
                check(testing::active==0 && testing::entered==testing::exited);
                const auto after=jobs.GetStats();
                check(after.tasksSubmitted==after.tasksCompleted && after.unobservedFailures==before.unobservedFailures);
                check(after.tasksSubmitted-before.tasksSubmitted==(phase==testing::Phase::Counts?3u:phase==testing::Phase::Scatter?4u:5u));
                check(Equivalent(faultReference,published));
                published=PrepareOrdered(faultSnapshot,42,mode,&jobs,timing);
                check(Equivalent(faultReference,published));
            }
            published=expected;
#endif
            for (uint32_t count:{256u,1024u,4096u,16384u}) {
                const auto large=LargeScene(count);
                const RenderSceneSnapshot12 scene(Identity(),Projection(),large,42);
                OrderingPhases12 sums[2]{};
                for (unsigned sample=0;sample<120;++sample) {
                    OrderingTimings12 st,pt; OrderedFrame12 serial,parallel;
                    if (sample%2) { parallel=PrepareOrdered(scene,42,Mode::Parallel,&jobs,pt); serial=PrepareOrdered(scene,42,Mode::Serial,nullptr,st); }
                    else { serial=PrepareOrdered(scene,42,Mode::Serial,nullptr,st); parallel=PrepareOrdered(scene,42,Mode::Parallel,&jobs,pt); }
                    check(Equivalent(serial,parallel));
                    check(serial.categories[2].begin+serial.categories[2].count==serial.frame.packets.size());
                    if (sample>=20) {
                        const OrderingPhases12 t[]={st.serial,pt.parallel};
                        for (unsigned i=0;i<2;++i) { sums[i].visibility+=t[i].visibility; sums[i].keyGeneration+=t[i].keyGeneration; sums[i].ordering+=t[i].ordering; sums[i].total+=t[i].total; }
                    }
                }
                check(Equivalent(PrepareOrdered(scene,42,Mode::Serial,nullptr,timing),PrepareOrdered(scene,42,Mode::Verify,&jobs,timing)));
                for (unsigned i=0;i<2;++i) {
                    const auto& t=sums[i];
                    dx12log::Write("[ordering-bench] objects=%u workers=%u mode=%s samples=100 visibility=%.6f keyGeneration=%.6f ordering=%.6f total=%.6f ms",
                        count,workers,i?"parallel":"serial",t.visibility/100,t.keyGeneration/100,t.ordering/100,t.total/100);
                }
            }
            jobs.Shutdown(); rejects([&] { published=PrepareOrdered(snapshot,42,Mode::Parallel,&jobs,timing); });
            check(Equivalent(expected,published)); check(jobs.Initialize(config));
            check(Equivalent(expected,PrepareOrdered(snapshot,42,Mode::Verify,&jobs,timing))); jobs.Shutdown();
        }
    } catch(const std::exception& e) { ++failures; dx12log::Write("[ordering-test] exception: %s",e.what()); }
    dx12log::Write("[ordering-test] %s checks=%llu failures=%llu",failures?"FAIL":"PASS",(unsigned long long)checks,(unsigned long long)failures);
    return failures==0;
}
}
