#include "dx12renderprep.h"
#include "gr/jobs/JobSystem.h"
#include "gr/jobs/JobGroup.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace renderprep12 {
using namespace DirectX;
using Clock = std::chrono::steady_clock;
static double Ms(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now()-a).count(); }
static XMFLOAT4X4 Transpose(const XMFLOAT4X4& m) {
    XMFLOAT4X4 out; XMStoreFloat4x4(&out, XMMatrixTranspose(XMLoadFloat4x4(&m))); return out;
}
RenderSceneSnapshot12::RenderSceneSnapshot12(const XMFLOAT4X4& camera,
    const std::array<Object12,2>& objects, uint64_t generation)
    : m_camera(camera), m_shaderCamera(Transpose(camera)), m_objects(objects), m_generation(generation) {}
static bool Finite(const XMFLOAT4X4& m) {
    for (const auto& row : m.m) for (float x : row) if (!std::isfinite(x)) return false;
    return true;
}
static DrawPacket12 Kernel(const Object12& o, const XMFLOAT4X4& camera) {
    if (!o.vertexCount || o.firstVertex > 72 || o.vertexCount > 72-o.firstVertex ||
        !Finite(o.world) || o.world._14 != 0 || o.world._24 != 0 || o.world._34 != 0 || o.world._44 != 1)
        throw std::invalid_argument("invalid render object");
    const float lo[] = {o.minimum.x,o.minimum.y,o.minimum.z}, hi[] = {o.maximum.x,o.maximum.y,o.maximum.z};
    for (int a=0;a<3;++a) if (!std::isfinite(lo[a]) || !std::isfinite(hi[a]) || lo[a]>hi[a])
        throw std::invalid_argument("invalid bounds");
    DrawPacket12 p{}; p.object=o;
    XMFLOAT3 minimum{FLT_MAX,FLT_MAX,FLT_MAX}, maximum{-FLT_MAX,-FLT_MAX,-FLT_MAX};
    bool outside[6] = {true,true,true,true,true,true};
    // Консервативный clip test восьми углов; совпадение путей не заменяет oracle.
    for (int c=0;c<8;++c) {
        const auto v = XMVectorSet((c&1)?hi[0]:lo[0],(c&2)?hi[1]:lo[1],(c&4)?hi[2]:lo[2],1);
        const auto world = XMVector4Transform(v,XMLoadFloat4x4(&o.world));
        XMFLOAT4 w, clip; XMStoreFloat4(&w,world);
        XMStoreFloat4(&clip,XMVector4Transform(world,XMLoadFloat4x4(&camera)));
        if (!std::isfinite(w.x)||!std::isfinite(w.y)||!std::isfinite(w.z)||
            !std::isfinite(clip.x)||!std::isfinite(clip.y)||!std::isfinite(clip.z)||!std::isfinite(clip.w))
            throw std::invalid_argument("nonfinite transformed bounds");
        minimum.x=std::min(minimum.x,w.x); minimum.y=std::min(minimum.y,w.y); minimum.z=std::min(minimum.z,w.z);
        maximum.x=std::max(maximum.x,w.x); maximum.y=std::max(maximum.y,w.y); maximum.z=std::max(maximum.z,w.z);
        outside[0]&=clip.x < -clip.w; outside[1]&=clip.x > clip.w;
        outside[2]&=clip.y < -clip.w; outside[3]&=clip.y > clip.w;
        outside[4]&=clip.z < 0; outside[5]&=clip.z > clip.w;
    }
    p.worldMinimum=minimum; p.worldMaximum=maximum; p.visible=true;
    for (bool b:outside) if (b) p.visible=false;
    return p;
}
bool Equivalent(const PreparedFrame12& a, const PreparedFrame12& b) {
    if (a.generation!=b.generation || !Finite(a.shaderCamera) || !Finite(b.shaderCamera) ||
        std::memcmp(&a.shaderCamera,&b.shaderCamera,sizeof(a.shaderCamera))) return false;
    for (size_t i=0;i<2;++i) {
        const auto& x=a.packets[i]; const auto& y=b.packets[i];
        if (x.object.id!=y.object.id || x.object.firstVertex!=y.object.firstVertex || x.object.vertexCount!=y.object.vertexCount ||
            x.visible!=y.visible || !Finite(x.object.world) || !Finite(y.object.world) ||
            std::memcmp(&x.object.world,&y.object.world,sizeof(x.object.world)) ||
            std::memcmp(&x.object.minimum,&y.object.minimum,sizeof(XMFLOAT3)) ||
            std::memcmp(&x.object.maximum,&y.object.maximum,sizeof(XMFLOAT3)) ||
            std::memcmp(&x.worldMinimum,&y.worldMinimum,sizeof(XMFLOAT3)) ||
            std::memcmp(&x.worldMaximum,&y.worldMaximum,sizeof(XMFLOAT3))) return false;
    }
    return true;
}
PreparedFrame12 Prepare(const RenderSceneSnapshot12& snapshot, uint64_t liveGeneration,
    Mode mode, gr::jobs::JobSystem* jobs, Timings12& time) {
    time={};
    if (!liveGeneration || snapshot.Generation()!=liveGeneration || !Finite(snapshot.Camera()) ||
        snapshot.Objects()[0].id==snapshot.Objects()[1].id)
        throw std::invalid_argument("invalid snapshot or stale generation");
    if (mode!=Mode::Serial && (!jobs || !jobs->IsInitialized()))
        throw std::runtime_error("render preparation requires running JobSystem");
    const auto run=[&](bool parallel) {
        const auto start=Clock::now();
        PreparedFrame12 result{};
        if (parallel) {
            // result и snapshot живут дольше группы, в том числе при исключении.
            auto group=jobs->CreateGroup();
            const auto submit=Clock::now();
            group.ParallelFor(2,1,[&](uint32_t begin,uint32_t end,uint32_t) {
                for (auto i=begin;i<end;++i) result.packets[i]=Kernel(snapshot.Objects()[i],snapshot.Camera());
            });
            time.submit=Ms(submit); const auto wait=Clock::now(); group.Wait(); time.wait=Ms(wait);
        } else for (size_t i=0;i<2;++i) result.packets[i]=Kernel(snapshot.Objects()[i],snapshot.Camera());
        const auto merge=Clock::now();
        // Порядок исходного списка — порядок draw, без worker-dependent push_back.
        result.generation=snapshot.Generation(); result.shaderCamera=snapshot.ShaderCamera();
        time.merge+=Ms(merge);
        if (parallel) time.parallel=Ms(start); else time.serial=Ms(start);
        return result;
    };
    if (mode==Mode::Serial) return run(false);
    if (mode==Mode::Parallel) return run(true);
    const auto serial=run(false); const auto parallel=run(true);
    if (!Equivalent(serial,parallel)) throw std::runtime_error("render preparation mismatch");
    return parallel;
}
}
