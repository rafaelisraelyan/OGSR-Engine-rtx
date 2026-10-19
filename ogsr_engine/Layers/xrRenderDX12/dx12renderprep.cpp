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
#include <unordered_set>
namespace renderprep12 {
using namespace DirectX;
using Clock = std::chrono::steady_clock;
static double Ms(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now()-a).count(); }
static XMFLOAT4X4 Transpose(const XMFLOAT4X4& m) {
    XMFLOAT4X4 out; XMStoreFloat4x4(&out, XMMatrixTranspose(XMLoadFloat4x4(&m))); return out;
}
RenderSceneSnapshot12::RenderSceneSnapshot12(const XMFLOAT4X4& camera,
    std::span<const Object12> objects, uint64_t generation)
    : m_camera(camera), m_shaderCamera(Transpose(camera)), m_objects(objects.begin(),objects.end()), m_generation(generation) {
    if (objects.size()>UINT32_MAX) throw std::length_error("snapshot too large");
    std::unordered_set<uint64_t> ids;
    ids.reserve(objects.size());
    for (const auto& o:objects)
        if (!o.id || !ids.insert(o.id).second) throw std::invalid_argument("invalid object ID");
}
static bool Finite(const XMFLOAT4X4& m) {
    for (const auto& row : m.m) for (float x : row) if (!std::isfinite(x)) return false;
    return true;
}
static bool Finite3(const XMFLOAT3& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
// TwoSum сохраняет знак ошибки сложения. /fp:precise запрещает reassociation.
// Произведение двух входных float точно представимо в double.
static double AddOutward(double a, double b, bool upper) {
    const double sum=a+b, tail=sum-a;
    const double error=(a-(sum-tail))+(b-tail);
    if (upper ? error>0 : error<0)
        return std::nextafter(sum,upper?INFINITY:-INFINITY);
    return sum;
}
static float FloatOutward(double value, bool upper) {
    if (!std::isfinite(value) || value>FLT_MAX || value<-FLT_MAX)
        throw std::invalid_argument("bounds overflow");
    float result=static_cast<float>(value);
    if (upper ? double(result)<value : double(result)>value)
        result=std::nextafter(result,upper?INFINITY:-INFINITY);
    return result;
}
static void AffineInterval(const float* lo, const float* hi, const XMFLOAT4X4& matrix,
    double* outLo, double* outHi) {
    for (int j=0;j<3;++j) {
        outLo[j]=outHi[j]=matrix.m[3][j];
        for (int i=0;i<3;++i) {
            double a=double(lo[i])*matrix.m[i][j], b=double(hi[i])*matrix.m[i][j];
            if (a>b) std::swap(a,b);
            outLo[j]=AddOutward(outLo[j],a,false);
            outHi[j]=AddOutward(outHi[j],b,true);
        }
    }
}
static bool ValidResource(Resource12 r) {
    return std::find(SyntheticResources12.begin(),SyntheticResources12.end(),r)!=SyntheticResources12.end();
}
static DrawPacket12 Kernel(const Object12& o, const XMFLOAT4X4& camera) {
    if (!o.vertexCount || o.firstVertex > 72 || o.vertexCount > 72-o.firstVertex ||
        !Finite(o.world) || o.world._14 != 0 || o.world._24 != 0 || o.world._34 != 0 || o.world._44 != 1)
        throw std::invalid_argument("invalid render object");
    if ((o.flags & ~(Hud|Invisible|Transparent)) || !std::isfinite(o.opacity) || o.opacity<0 || o.opacity>1 ||
        (o.opacity<1 && !(o.flags&Transparent)) || !Finite3(o.sphere.center) ||
        !std::isfinite(o.sphere.radius) || o.sphere.radius<0)
        throw std::invalid_argument("invalid flags, opacity or sphere");
    if (!ValidResource(o.visual) || !ValidResource(o.material) || !ValidResource(o.pso))
        throw std::invalid_argument("stale or unknown synthetic resource");
    const float lo[] = {o.minimum.x,o.minimum.y,o.minimum.z}, hi[] = {o.maximum.x,o.maximum.y,o.maximum.z};
    for (int a=0;a<3;++a) if (!std::isfinite(lo[a]) || !std::isfinite(hi[a]) || lo[a]>hi[a])
        throw std::invalid_argument("invalid bounds");
    DrawPacket12 p{}; p.object=o;
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
        outside[0]&=clip.x < -clip.w; outside[1]&=clip.x > clip.w;
        outside[2]&=clip.y < -clip.w; outside[3]&=clip.y > clip.w;
        outside[4]&=clip.z < 0; outside[5]&=clip.z > clip.w;
    }
    double boundLo[3],boundHi[3];
    AffineInterval(lo,hi,o.world,boundLo,boundHi);
    p.worldMinimum={FloatOutward(boundLo[0],false),FloatOutward(boundLo[1],false),FloatOutward(boundLo[2],false)};
    p.worldMaximum={FloatOutward(boundHi[0],true),FloatOutward(boundHi[1],true),FloatOutward(boundHi[2],true)};
    p.visible=true;
    for (bool b:outside) if (b) p.visible=false;
    if (o.flags&Hud) p.visible=true;
    if ((o.flags&Invisible) || o.opacity==0 || !o.visibility.mask) p.visible=false;
    const float center[]={o.sphere.center.x,o.sphere.center.y,o.sphere.center.z};
    double centerLo[3],centerHi[3];
    AffineInterval(center,center,o.world,centerLo,centerHi);
    float storedCenter[3]; double centerError=0;
    for (int i=0;i<3;++i) {
        storedCenter[i]=FloatOutward(centerLo[i],false);
        const double error=std::max(AddOutward(double(storedCenter[i]),-centerLo[i],true),
                                    AddOutward(centerHi[i],-double(storedCenter[i]),true));
        // L1 — безопасная верхняя граница ошибки положения центра.
        centerError=AddOutward(centerError,error,true);
    }
    p.worldSphere.center={storedCenter[0],storedCenter[1],storedCenter[2]};
    // Верхняя граница спектральной нормы: корректна и для shear/отражения.
    double norm1=0, normInf=0;
    for (int i=0;i<3;++i) {
        double row=0,col=0;
        for (int j=0;j<3;++j) {
            row=AddOutward(row,std::abs(double(o.world.m[i][j])),true);
            col=AddOutward(col,std::abs(double(o.world.m[j][i])),true);
        }
        norm1=std::max(norm1,col); normInf=std::max(normInf,row);
    }
    double radius=0;
    if (o.sphere.radius>0 && norm1>0 && normInf>0) {
        const double product=std::nextafter(norm1*normInf,INFINITY);
        const double norm=std::nextafter(std::sqrt(product),INFINITY);
        radius=std::nextafter(double(o.sphere.radius)*norm,INFINITY);
    }
    radius=AddOutward(radius,centerError,true);
    if (radius>FLT_MAX || !Finite3(p.worldSphere.center)) throw std::invalid_argument("sphere overflow");
    p.worldSphere.radius=static_cast<float>(radius);
    // Округление вверх не позволяет float уменьшить консервативную границу.
    if (double(p.worldSphere.radius)<radius) p.worldSphere.radius=std::nextafter(p.worldSphere.radius,FLT_MAX);
    return p;
}
bool Equivalent(const PreparedFrame12& a, const PreparedFrame12& b) {
    if (a.packets.size()!=b.packets.size() || a.generation!=b.generation || !Finite(a.shaderCamera) || !Finite(b.shaderCamera) ||
        std::memcmp(&a.shaderCamera,&b.shaderCamera,sizeof(a.shaderCamera))) return false;
    for (size_t i=0;i<a.packets.size();++i) {
        const auto& x=a.packets[i]; const auto& y=b.packets[i];
        if (x.object.visual!=y.object.visual || x.object.material!=y.object.material || x.object.pso!=y.object.pso ||
            x.object.flags!=y.object.flags || x.object.opacity!=y.object.opacity || x.object.visibility!=y.object.visibility ||
            !Finite3(x.worldMinimum)||!Finite3(y.worldMinimum)||!Finite3(x.worldMaximum)||!Finite3(y.worldMaximum)||
            !Finite3(x.worldSphere.center)||!Finite3(y.worldSphere.center)||!std::isfinite(x.worldSphere.radius)||!std::isfinite(y.worldSphere.radius)||
            std::memcmp(&x.object.sphere.center,&y.object.sphere.center,sizeof(XMFLOAT3)) || x.object.sphere.radius!=y.object.sphere.radius ||
            std::memcmp(&x.worldSphere.center,&y.worldSphere.center,sizeof(XMFLOAT3)) || x.worldSphere.radius!=y.worldSphere.radius ||
            x.object.id!=y.object.id || x.object.firstVertex!=y.object.firstVertex || x.object.vertexCount!=y.object.vertexCount ||
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
    if (!liveGeneration || snapshot.Generation()!=liveGeneration || !Finite(snapshot.Camera()))
        throw std::invalid_argument("invalid snapshot or stale generation");
    if (mode!=Mode::Serial && mode!=Mode::Parallel && mode!=Mode::Verify) throw std::invalid_argument("invalid mode");
    if (mode!=Mode::Serial && (!jobs || !jobs->IsInitialized()))
        throw std::runtime_error("render preparation requires running JobSystem");
    const auto run=[&](bool parallel) {
        const auto start=Clock::now();
        PreparedFrame12 result{};
        std::vector<DrawPacket12> scratch(snapshot.Objects().size());
        if (parallel) {
            // result и snapshot живут дольше группы, в том числе при исключении.
            auto group=jobs->CreateGroup();
            const auto submit=Clock::now();
            group.ParallelFor(static_cast<uint32_t>(snapshot.Objects().size()),snapshot.Objects().size()>2?64:1,[&](uint32_t begin,uint32_t end,uint32_t) {
                for (auto i=begin;i<end;++i) scratch[i]=Kernel(snapshot.Objects()[i],snapshot.Camera());
            });
            time.submit=Ms(submit); const auto wait=Clock::now(); group.Wait(); time.wait=Ms(wait);
        } else for (size_t i=0;i<snapshot.Objects().size();++i) scratch[i]=Kernel(snapshot.Objects()[i],snapshot.Camera());
        const double prepare=Ms(start);
        const auto merge=Clock::now();
        // Порядок исходного списка — порядок draw, без worker-dependent push_back.
        // Последовательная публикация: порядок исходного snapshot, без сортировки прозрачности.
        result.packets=std::move(scratch);
        result.generation=snapshot.Generation(); result.shaderCamera=snapshot.ShaderCamera();
        const double mergeMs=Ms(merge);
        time.merge+=mergeMs;
        if (parallel) { time.parallel=Ms(start); time.parallelPrepare=prepare; time.parallelMerge=mergeMs; }
        else { time.serial=Ms(start); time.serialPrepare=prepare; time.serialMerge=mergeMs; }
        return result;
    };
    if (mode==Mode::Serial) return run(false);
    if (mode==Mode::Parallel) return run(true);
    const auto serial=run(false); const auto parallel=run(true);
    if (!Equivalent(serial,parallel)) throw std::runtime_error("render preparation mismatch");
    return parallel;
}
}
