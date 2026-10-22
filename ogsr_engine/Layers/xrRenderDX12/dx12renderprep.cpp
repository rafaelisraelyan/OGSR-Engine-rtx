#include "dx12renderprep.h"
#include "dx12draworder_internal.h"
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
#ifdef GR_JOBS_TESTING
#include "tests/visibility_test_hooks.h"
#endif
namespace renderprep12 {
using namespace DirectX;
using Clock = std::chrono::steady_clock;
static double Ms(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now()-a).count(); }
static XMFLOAT4X4 Transpose(const XMFLOAT4X4& m) {
    XMFLOAT4X4 out; XMStoreFloat4x4(&out, XMMatrixTranspose(XMLoadFloat4x4(&m))); return out;
}
static bool Finite(const XMFLOAT4X4& m) {
    for (const auto& row : m.m) for (float x : row) if (!std::isfinite(x)) return false;
    return true;
}
static XMFLOAT4X4 ViewProjection(const XMFLOAT4X4& view, const XMFLOAT4X4& projection) {
    if (!Finite(view) || !Finite(projection) || view._14!=0 || view._24!=0 || view._34!=0 || view._44!=1)
        throw std::invalid_argument("invalid ordering camera");
    XMFLOAT4X4 result;
    XMStoreFloat4x4(&result,XMMatrixMultiply(XMLoadFloat4x4(&view),XMLoadFloat4x4(&projection)));
    if (!Finite(result)) throw std::overflow_error("view projection overflow");
    return result;
}
RenderSceneSnapshot12::RenderSceneSnapshot12(const XMFLOAT4X4& camera,
    std::span<const Object12> objects, uint64_t generation)
    : RenderSceneSnapshot12(camera,XMFLOAT4X4{},objects,generation,false) {}
RenderSceneSnapshot12::RenderSceneSnapshot12(const XMFLOAT4X4& view, const XMFLOAT4X4& projection,
    std::span<const Object12> objects, uint64_t generation)
    : RenderSceneSnapshot12(ViewProjection(view,projection),view,objects,generation,true) {}
RenderSceneSnapshot12::RenderSceneSnapshot12(const XMFLOAT4X4& camera, const XMFLOAT4X4& view,
    std::span<const Object12> objects, uint64_t generation, bool hasView)
    : m_camera(camera), m_shaderCamera(Transpose(camera)), m_view(view), m_hasView(hasView),
      m_objects(objects.begin(),objects.end()), m_generation(generation) {
    if (objects.size()>UINT32_MAX) throw std::length_error("snapshot too large");
    std::unordered_set<uint64_t> ids;
    ids.reserve(objects.size());
    for (const auto& o:objects)
        if (!o.id || !ids.insert(o.id).second) throw std::invalid_argument("invalid object ID");
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
static bool ValidResource(Resource12 r, std::span<const Resource12> extraResources) {
    return std::find(SyntheticResources12.begin(),SyntheticResources12.end(),r)!=SyntheticResources12.end() ||
        std::find(extraResources.begin(),extraResources.end(),r)!=extraResources.end();
}
static DrawPacket12 Kernel(const Object12& o, const XMFLOAT4X4& camera,
    std::span<const Resource12> extraResources, bool diagnosticClip=true) {
    if (o.indexCount != 0)
    {
        // Indexed geometry range is validated against the render-owned mesh
        // registry during owner resolution; here only reject arithmetic
        // overflow so Prepare can build bounds for the object.
        if (o.firstIndex > UINT32_MAX - o.indexCount)
            throw std::invalid_argument("invalid indexed range");
    }
    else if (!o.vertexCount || o.firstVertex > 72 || o.vertexCount > 72-o.firstVertex)
    {
        throw std::invalid_argument("invalid render object");
    }
    if (!Finite(o.world) || o.world._14 != 0 || o.world._24 != 0 ||
        o.world._34 != 0 || o.world._44 != 1)
        throw std::invalid_argument("invalid render object");
    if ((o.flags & ~(Hud|Invisible|Transparent)) || !std::isfinite(o.opacity) || o.opacity<0 || o.opacity>1 ||
        (o.opacity<1 && !(o.flags&Transparent)) || !Finite3(o.sphere.center) ||
        !std::isfinite(o.sphere.radius) || o.sphere.radius<0)
        throw std::invalid_argument("invalid flags, opacity or sphere");
    if (!ValidResource(o.visual, extraResources) ||
        !ValidResource(o.material, extraResources) ||
        !ValidResource(o.pso, extraResources))
        throw std::invalid_argument("stale or unknown synthetic resource");
    const float lo[] = {o.minimum.x,o.minimum.y,o.minimum.z}, hi[] = {o.maximum.x,o.maximum.y,o.maximum.z};
    for (int a=0;a<3;++a) if (!std::isfinite(lo[a]) || !std::isfinite(hi[a]) || lo[a]>hi[a])
        throw std::invalid_argument("invalid bounds");
    DrawPacket12 p{}; p.object=o;
    bool outside[6] = {true,true,true,true,true,true};
    // Консервативный clip test восьми углов; совпадение путей не заменяет oracle.
    for (int c=0;diagnosticClip && c<8;++c) {
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
            x.object.firstIndex!=y.object.firstIndex || x.object.indexCount!=y.object.indexCount || x.object.baseVertex!=y.object.baseVertex ||
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
                for (auto i=begin;i<end;++i) scratch[i]=Kernel(
                    snapshot.Objects()[i],snapshot.Camera(),
                    std::span<const Resource12>());
            });
            time.submit=Ms(submit); const auto wait=Clock::now(); group.Wait(); time.wait=Ms(wait);
        } else for (size_t i=0;i<snapshot.Objects().size();++i)
            scratch[i]=Kernel(snapshot.Objects()[i],snapshot.Camera(),
                              std::span<const Resource12>());
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

namespace renderprep12 {
namespace {
// Row-vector camera: -w <= x,y <= w, 0 <= z <= w (D3D12).
// Верхняя оценка support AABB: удаляем только строго вне одной плоскости.
// Не нормализуем плоскости, не делим на w; касание и охват камеры сохраняются.
bool VisibleWorld(const DrawPacket12& packet, const DirectX::XMFLOAT4X4& camera) {
    const auto& o=packet.object;
    if ((o.flags&Invisible) || o.opacity==0 || !o.visibility.mask) return false;
    if (o.flags&Hud) return true;
    const float lo[]={packet.worldMinimum.x,packet.worldMinimum.y,packet.worldMinimum.z};
    const float hi[]={packet.worldMaximum.x,packet.worldMaximum.y,packet.worldMaximum.z};
    for (unsigned plane=0;plane<6;++plane) {
        double lower[4], upperPlane[4];
        for (unsigned i=0;i<4;++i) {
            if (plane==4) lower[i]=upperPlane[i]=camera.m[i][2];
            else {
                const unsigned axis=plane<4?plane/2:2;
                const double sign=(plane%2)?-1.0:1.0;
                // Интервал сохраняет малое слагаемое при разных порядках.
                lower[i]=AddOutward(camera.m[i][3],sign*double(camera.m[i][axis]),false);
                upperPlane[i]=AddOutward(camera.m[i][3],sign*double(camera.m[i][axis]),true);
            }
        }
        double upper=upperPlane[3];
        for (unsigned i=0;i<3;++i) {
            const double product=std::nextafter(std::max({double(lo[i])*lower[i],double(lo[i])*upperPlane[i],
                double(hi[i])*lower[i],double(hi[i])*upperPlane[i]}),INFINITY);
            upper=AddOutward(upper,product,true);
        }
        if (upper<0) return false;
    }
    return true;
}
}
PreparedFrame12 PrepareVisible(const RenderSceneSnapshot12& snapshot, uint64_t liveGeneration,
    Mode mode, gr::jobs::JobSystem* jobs, VisibilityTimings12& timings,
    std::span<const Resource12> extraResources) {
    timings={};
    if (!liveGeneration || snapshot.Generation()!=liveGeneration || !Finite(snapshot.Camera()))
        throw std::invalid_argument("invalid visibility snapshot");
    if (mode!=Mode::Serial && mode!=Mode::Parallel && mode!=Mode::Verify)
        throw std::invalid_argument("invalid visibility mode");
    if (mode!=Mode::Serial && (!jobs || !jobs->IsInitialized()))
        throw std::runtime_error("visibility requires running JobSystem");
    const size_t count=snapshot.Objects().size();
    if (count>UINT32_MAX) throw std::length_error("visibility count overflow");
    constexpr size_t blockSize=128;
    const size_t blocks=count/blockSize+(count%blockSize!=0);
    const auto run=[&](bool parallel, VisibilityPhases12& time) {
        const auto start=Clock::now();
        std::vector<DrawPacket12> scratch(count);
        std::vector<size_t> counts(blocks), offsets(blocks+1);
        // Группа каждой стадии уничтожается до массивов и захваченных callback.
        const auto ranges=[&](size_t n, auto&& callback) {
            if (!n) return;
            if (parallel) {
                auto group=jobs->CreateGroup();
                if (!group.IsValid()) throw std::runtime_error("invalid visibility group");
                group.ParallelFor(static_cast<uint32_t>(n),1,[&](uint32_t begin,uint32_t end,uint32_t) {
#ifdef GR_JOBS_TESTING
                    testing::CallbackScope callbackScope;
#endif
                    for (size_t i=begin;i<end;++i) callback(i);
                });
                group.Wait();
            } else for (size_t i=0;i<n;++i) callback(i);
        };
        auto phase=Clock::now();
        ranges(blocks,[&](size_t b) {
            const size_t end=std::min(count,(b+1)*blockSize);
            for (size_t i=b*blockSize;i<end;++i) scratch[i]=Kernel(
                snapshot.Objects()[i],snapshot.Camera(),extraResources,false);
        });
        time.preparation=Ms(phase);
        phase=Clock::now();
        ranges(blocks,[&](size_t b) {
            const size_t end=std::min(count,(b+1)*blockSize);
            for (size_t i=b*blockSize;i<end;++i) scratch[i].visible=VisibleWorld(scratch[i],snapshot.Camera());
        });
        time.visibility=Ms(phase);
        phase=Clock::now();
        ranges(blocks,[&](size_t b) {
            size_t n=0;
            const size_t end=std::min(count,(b+1)*blockSize);
            for (size_t i=b*blockSize;i<end;++i) n+=scratch[i].visible?1:0;
            counts[b]=n;
#ifdef GR_JOBS_TESTING
            testing::FailAfterWrite(testing::Phase::Counts,parallel,b);
#endif
        });
        time.blockCounts=Ms(phase);
        phase=Clock::now();
        for (size_t b=0;b<blocks;++b) {
            if (counts[b]>count-offsets[b]) throw std::overflow_error("visibility prefix overflow");
            offsets[b+1]=offsets[b]+counts[b];
        }
        time.prefix=Ms(phase);
        PreparedFrame12 result{};
        if (offsets.back()>result.packets.max_size()) throw std::length_error("visibility output overflow");
        result.packets.resize(offsets.back());
        phase=Clock::now();
        ranges(blocks,[&](size_t b) {
            size_t destination=offsets[b];
            const size_t end=std::min(count,(b+1)*blockSize);
            for (size_t i=b*blockSize;i<end;++i) if (scratch[i].visible) {
                if (destination>=offsets[b+1]) throw std::logic_error("visibility scatter overflow");
                result.packets[destination++]=scratch[i];
#ifdef GR_JOBS_TESTING
                // Исключение после реальной записи в ещё не опубликованный кадр.
                testing::FailAfterWrite(testing::Phase::Scatter,parallel,b);
#endif
            }
            if (destination!=offsets[b+1]) throw std::logic_error("visibility count mismatch");
        });
        time.scatter=Ms(phase);
        result.shaderCamera=snapshot.ShaderCamera(); result.generation=snapshot.Generation();
        time.total=Ms(start);
        return result;
    };
    if (mode==Mode::Serial) return run(false,timings.serial);
    if (mode==Mode::Parallel) return run(true,timings.parallel);
    const auto serial=run(false,timings.serial);
    auto parallel=run(true,timings.parallel);
    if (!Equivalent(serial,parallel)) throw std::runtime_error("visibility mismatch");
    return parallel;
}
}

namespace renderprep12 {
bool Equivalent(const OrderedFrame12& a, const OrderedFrame12& b) {
    return a.categories==b.categories && Equivalent(a.frame,b.frame);
}
OrderedFrame12 PrepareOrdered(const RenderSceneSnapshot12& snapshot, uint64_t generation,
    Mode mode, gr::jobs::JobSystem* jobs, OrderingTimings12& timings,
    std::span<const Resource12> extraResources) {
    timings={};
    if (!snapshot.HasView()) throw std::invalid_argument("ordering requires immutable view");
    if (mode!=Mode::Serial && mode!=Mode::Parallel && mode!=Mode::Verify)
        throw std::invalid_argument("invalid ordering mode");
    const auto run=[&](bool parallel, OrderingPhases12& time) {
        const auto start=Clock::now();
        VisibilityTimings12 visibility;
        auto visible=PrepareVisible(snapshot,generation,
            parallel?Mode::Parallel:Mode::Serial,jobs,visibility,extraResources);
        time.visibility=Ms(start);
        const auto keyStart=Clock::now();
        const uint32_t count=detail::PacketCount(visible.packets.size());
        std::vector<detail::OrderKey> keys(count);
        // Compaction сохраняет исходный порядок; линейно восстанавливаем ordinal
        // без hash map и без зависимости от порядка worker completion.
        size_t source=0;
        for (uint32_t i=0;i<count;++i) {
            while (source<snapshot.Objects().size() && snapshot.Objects()[source].id!=visible.packets[i].object.id) ++source;
            if (source==snapshot.Objects().size()) throw std::logic_error("ordering source mismatch");
            keys[i].source=detail::PacketCount(source++); keys[i].packet=i;
        }
        const auto key=[&](uint32_t i) {
            const auto& p=visible.packets[i]; const auto& o=p.object; auto& k=keys[i];
            k.category=(o.flags&Hud)?DrawCategory12::Hud:(o.flags&Transparent)?DrawCategory12::Transparent:DrawCategory12::Opaque;
            k.object=o.id; k.pso=o.pso; k.material=o.material; k.geometry=o.visual;
            if (k.category==DrawCategory12::Transparent) {
                // Центр мировой AABB; double не переполняется при сложении float bounds.
                const double x=(double(p.worldMinimum.x)+p.worldMaximum.x)*0.5;
                const double y=(double(p.worldMinimum.y)+p.worldMaximum.y)*0.5;
                const double z=(double(p.worldMinimum.z)+p.worldMaximum.z)*0.5;
                const auto& v=snapshot.View();
                k.depth=detail::Depth(x*v._13+y*v._23+z*v._33+v._43);
            }
#ifdef GR_JOBS_TESTING
            testing::FailAfterWrite(testing::Phase::Keys,parallel,i);
#endif
        };
        if (parallel && count) {
            auto group=jobs->CreateGroup();
            if (!group.IsValid()) throw std::runtime_error("invalid ordering group");
            group.ParallelFor(count,128,[&](uint32_t begin,uint32_t end,uint32_t) {
#ifdef GR_JOBS_TESTING
                testing::CallbackScope scope;
#endif
                for (uint32_t i=begin;i<end;++i) key(i);
            });
            group.Wait();
        } else for (uint32_t i=0;i<count;++i) key(i);
        time.keyGeneration=Ms(keyStart);
        const auto orderingStart=Clock::now();
        std::sort(keys.begin(),keys.end(),detail::Less);
#ifdef GR_JOBS_TESTING
        testing::FailAfterWrite(testing::Phase::Ordering,parallel,0);
#endif
        OrderedFrame12 result{};
        result.frame.shaderCamera=visible.shaderCamera; result.frame.generation=visible.generation;
        result.frame.packets.resize(count);
        for (uint32_t i=0;i<count;++i) {
            const auto& k=keys[i];
            result.frame.packets[i]=visible.packets[k.packet];
            auto& range=result.categories[static_cast<size_t>(k.category)];
            range.count=detail::PacketCount(size_t(range.count)+1);
#ifdef GR_JOBS_TESTING
            testing::FailAfterWrite(testing::Phase::OrderedScatter,parallel,i);
#endif
        }
        size_t offset=0;
        for (auto& range:result.categories) {
            range.begin=detail::PacketCount(offset);
            offset+=range.count;
        }
        if (offset!=count) throw std::logic_error("ordering range mismatch");
        time.ordering=Ms(orderingStart); time.total=Ms(start);
        return result;
    };
    if (mode==Mode::Serial) return run(false,timings.serial);
    if (mode==Mode::Parallel) return run(true,timings.parallel);
    auto serial=run(false,timings.serial); auto parallel=run(true,timings.parallel);
    if (!Equivalent(serial,parallel)) throw std::runtime_error("ordering mismatch");
    return parallel;
}
}
