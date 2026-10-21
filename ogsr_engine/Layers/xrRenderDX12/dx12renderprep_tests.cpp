#include "dx12renderprep.h"
#include "dx12camera.h"
#include "dx12log.h"
#include "gr/jobs/JobSystem.h"
#include <cstring>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>
namespace renderprep12 {
bool RunTests() {
    static_assert(!std::is_copy_assignable_v<RenderSceneSnapshot12>);
    unsigned checks=0, failures=0;
    const auto check=[&](bool b) { ++checks; if(!b) ++failures; };
    const auto rejects=[&](auto f) { bool threw=false; try { f(); } catch(const std::exception&) { threw=true; } check(threw); };
    try {
        DirectX::XMFLOAT4X4 identity;
        DirectX::XMStoreFloat4x4(&identity,DirectX::XMMatrixIdentity());
        std::array<Object12,2> objects{};
        for (unsigned i=0;i<2;++i) objects[i]={i+1,i*36,36,0,0,0,identity,{-0.5f,-0.5f,0.1f},{0.5f,0.5f,0.9f}};
        Timings12 timing;
        const RenderSceneSnapshot12 snapshot(identity,objects,7);
        objects[0].id=99; // Snapshot не ссылается на изменяемый источник.
        check(snapshot.Objects()[0].id==1);
        std::copy(snapshot.Objects().begin(),snapshot.Objects().end(),objects.begin());
        const auto expected=Prepare(snapshot,7,Mode::Serial,nullptr,timing);
        check(expected.packets[0].visible && expected.packets[1].visible);
        check(expected.packets[0].object.firstVertex==0 && expected.packets[1].object.firstVertex==36);
        check(expected.packets[0].worldMinimum.x==-0.5f);
        rejects([&] { Prepare(snapshot,8,Mode::Serial,nullptr,timing); });
        rejects([&] { Prepare(snapshot,7,Mode::Parallel,nullptr,timing); });
        auto duplicate=objects; duplicate[1].id=duplicate[0].id;
        rejects([&] { Prepare(RenderSceneSnapshot12(identity,duplicate,7),7,Mode::Serial,nullptr,timing); });
        auto nan=identity; nan._11=std::numeric_limits<float>::quiet_NaN();
        rejects([&] { Prepare(RenderSceneSnapshot12(nan,objects,7),7,Mode::Serial,nullptr,timing); });
        auto invalid=objects; invalid[0].minimum.x=std::numeric_limits<float>::infinity();
        rejects([&] { Prepare(RenderSceneSnapshot12(identity,invalid,7),7,Mode::Serial,nullptr,timing); });
        invalid=objects; invalid[0].firstVertex=UINT32_MAX;
        rejects([&] { Prepare(RenderSceneSnapshot12(identity,invalid,7),7,Mode::Serial,nullptr,timing); });
        auto scaled=objects; scaled[0].world._11=2; scaled[0].world._22=3; scaled[0].world._33=4;
        const auto transformed=Prepare(RenderSceneSnapshot12(identity,scaled,7),7,Mode::Serial,nullptr,timing);
        check(transformed.packets[0].worldMinimum.x==-1 && transformed.packets[0].worldMaximum.y==1.5f);
        check(transformed.packets[0].visible); // Пересечение near/far не равно полному отсечению.
        auto reversed=objects; std::swap(reversed[0],reversed[1]);
        const auto ordered=Prepare(RenderSceneSnapshot12(identity,reversed,7),7,Mode::Serial,nullptr,timing);
        check(ordered.packets[0].object.id==2 && ordered.packets[1].object.id==1);
        // Независимый oracle для identity clip: D3D z находится в [0,1].
        for (int plane=0;plane<6;++plane) {
            auto shifted=objects;
            auto& m=shifted[0].world;
            if (plane==0) m._41=-3; if (plane==1) m._41=3;
            if (plane==2) m._42=-3; if (plane==3) m._42=3;
            if (plane==4) m._43=-3; if (plane==5) m._43=3;
            const auto out=Prepare(RenderSceneSnapshot12(identity,shifted,7),7,Mode::Serial,nullptr,timing);
            check(!out.packets[0].visible && out.packets[1].visible);
            check(out.packets.size()==2 && out.packets[0].object.vertexCount==36);
        }
        for (unsigned workers : {0u,1u,2u,4u,8u}) {
            gr::jobs::JobSystem jobs; gr::jobs::JobConfig config;
            config.backgroundWorkers=workers; config.automaticWorkers=false;
            check(jobs.Initialize(config));
            for (float aspect : {1.0f,16.0f/9.0f,9.0f/16.0f}) for (float t : {0.0f,2.0f,100.0f}) {
                SmokeCamera12 camera; check(camera.SetAspect(aspect)); camera.SetOrbit(t);
                const RenderSceneSnapshot12 frame(camera.CaptureViewProjection(),objects,7);
                DirectX::XMFLOAT4X4 old; camera.BuildViewProjection(old);
                check(std::memcmp(&old,&frame.ShaderCamera(),sizeof(old))==0);
                const auto a=Prepare(frame,7,Mode::Serial,nullptr,timing);
                for (unsigned repeat=0;repeat<8;++repeat) {
                    const auto b=Prepare(frame,7,Mode::Verify,&jobs,timing);
                    check(Equivalent(a,b));
                }
            }
            // Ошибка реального callback после частичной записи: старый выход сохраняется.
            auto bad=objects; bad[1].vertexCount=73;
            auto published=expected;
            rejects([&] { published=Prepare(RenderSceneSnapshot12(identity,bad,7),7,Mode::Parallel,&jobs,timing); });
            check(Equivalent(published,expected));
            check(Equivalent(Prepare(snapshot,7,Mode::Parallel,&jobs,timing),expected));
            jobs.Shutdown();
            rejects([&] { Prepare(snapshot,7,Mode::Parallel,&jobs,timing); });
            check(jobs.Initialize(config));
            check(Equivalent(Prepare(snapshot,7,Mode::Verify,&jobs,timing),expected));
            jobs.Shutdown();
        }
    } catch(const std::exception& e) { ++failures; dx12log::Write("[prep-selftest] exception: %s",e.what()); }
    dx12log::Write("[prep-selftest] %s checks=%u failures=%u",failures?"FAIL":"PASS",checks,failures);
    return failures==0;
}
}
