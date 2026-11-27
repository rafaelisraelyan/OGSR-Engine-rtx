#include "../../ogsr_engine/xr_3da/GameDx12ClusteredLighting.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
using namespace gamecluster12;
// Проверки и их побочные действия выполняются также при NDEBUG.
#undef assert
#define assert(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); std::abort(); } } while (false)

static Config Camera()
{
    Config c; c.width=127; c.height=73; c.tileWidth=16; c.tileHeight=13; c.slices=12;
    c.nearDepth=0.25f; c.farDepth=100.f;
    // LH perspective: x clip = x, y clip = 1.5*y, w clip = z.
    c.viewProjection[0]=1; c.viewProjection[5]=1.5f; c.viewProjection[10]=1;
    c.viewProjection[11]=1; c.viewProjection[14]=-c.nearDepth;
    return c;
}
static bool Contains(const Result& r, uint32_t cluster, uint32_t light)
{
    const Entry e=r.grid.at(cluster);
    return std::binary_search(r.indices.begin()+e.offset,r.indices.begin()+e.offset+e.count,light);
}
// Oracle независим от построения границ: вычисляет пиксель и глубину точки,
// затем сравнивает список с точным сферическим LocalFalloff.
static void Oracle(const Config& c, const std::vector<Light>& lights, const Result& r,
                   double x,double y,double z)
{
    if (z<c.nearDepth || z>c.farDepth) return;
    const double sx=(x/z*0.5+0.5)*c.width, sy=(-1.5*y/z*0.5+0.5)*c.height;
    if (sx<0 || sx>=c.width || sy<0 || sy>=c.height) return;
    const uint32_t tx=uint32_t(sx)/c.tileWidth, ty=uint32_t(sy)/c.tileHeight;
    const uint32_t tz=std::min(uint32_t(std::max(0.0,std::floor(std::log(z/c.nearDepth)*c.slices/
        std::log(double(c.farDepth)/c.nearDepth)))),c.slices-1);
    const uint32_t cluster=(tz*r.tilesY+ty)*r.tilesX+tx;
    for (uint32_t i=0;i<lights.size();++i)
    {
        const auto& l=lights[i]; const double dx=x-l.x,dy=y-l.y,dz=z-l.z;
        if (dx*dx+dy*dy+dz*dz < double(l.radius)*l.radius)
            assert(Contains(r,cluster,i));
    }
}
int main()
{
    Config c=Camera();
    std::vector<Light> lights={{0,0,0,2},{0,0,0.3f,0.1f},{0,0,10,3},
        {1000,1000,10,1},{-8,0,8,1},{1,1,100,3},{0,0,-4,1},{0,0,5,0}};
    std::mt19937 rng(0x4321); std::uniform_real_distribution<float> pos(-30,30), depth(-3,110), radius(0.01f,8);
    for (unsigned i=0;i<200;++i) lights.push_back({pos(rng),pos(rng),depth(rng),radius(rng)});
    Result r; assert(Build(c,lights,r)); assert(r.tilesX==8 && r.tilesY==6);
    for (const Entry e:r.grid)
    {
        assert(uint64_t(e.offset)+e.count<=r.indices.size());
        for (uint32_t i=1;i<e.count;++i) assert(r.indices[e.offset+i-1]<r.indices[e.offset+i]);
    }
    std::uniform_real_distribution<double> unit(0,1);
    for (unsigned i=0;i<100000;++i)
    {
        double z=c.nearDepth*std::pow(double(c.farDepth)/c.nearDepth,unit(rng));
        double x=(2*unit(rng)-1)*z, y=(2*unit(rng)-1)*z/1.5;
        Oracle(c,lights,r,x,y,z);
    }
    for (unsigned s=0;s<=c.slices;++s)
        for (unsigned x=0;x<c.width;x+=c.tileWidth)
            for (double delta:{-1.e-6,0.,1.e-6})
            {
                const double z=c.nearDepth*std::pow(double(c.farDepth)/c.nearDepth,double(s)/c.slices);
                Oracle(c,lights,r,((double(x)+delta)/c.width*2-1)*z,0,z);
            }
    Result repeat; assert(Build(c,lights,repeat)); assert(r.indices==repeat.indices);
    for (size_t i=0;i<r.grid.size();++i) assert(r.grid[i].offset==repeat.grid[i].offset && r.grid[i].count==repeat.grid[i].count);
    // Мутации конфигурации и данных обязаны переключать вызывающего на полный цикл.
    Config invalid=c; invalid.tileWidth=0; assert(!Build(invalid,lights,repeat) && repeat.grid.empty());
    invalid=c; invalid.farDepth=invalid.nearDepth; assert(!Build(invalid,lights,repeat));
    invalid=c; invalid.viewProjection[11]=0; assert(!Build(invalid,lights,repeat));
    invalid=c; invalid.maxBytes=8; assert(!Build(invalid,lights,repeat));
    auto bad=lights; bad[0].radius=-1; assert(!Build(c,bad,repeat));
    bad=lights; bad[0].x=std::numeric_limits<float>::quiet_NaN(); assert(!Build(c,bad,repeat));
    assert(Build(c,{},repeat)); assert(repeat.indices.empty());
    // Большие координаты: float clip.x может отличаться от double на много тайлов.
    Config large=Camera(); large.tileWidth=1;
    large.viewProjection[0]=1; large.viewProjection[8]=1; large.viewProjection[12]=-100000000.f;
    std::vector<Light> largeLights={{100000000.f,0,11,0.1f}};
    assert(Build(large,largeLights,repeat));
    const float gpuClipX = (largeLights[0].x + 11.f) - 100000000.f;
    const uint32_t gpuTile = uint32_t((gpuClipX / 11.f * .5f + .5f) * large.width);
    const uint32_t gpuZ = uint32_t(std::log2(11.f / large.nearDepth) * large.slices /
        std::log2(large.farDepth / large.nearDepth));
    assert(Contains(repeat, (gpuZ * repeat.tilesY + 2) * repeat.tilesX + gpuTile, 0));
    // clip.w сам по себе не гарантирует, что farDepth отсекает геометрию.
    Config unusual=Camera(); unusual.viewProjection[10]=0; unusual.viewProjection[14]=0;
    std::vector<Light> beyond={{0,0,200,1}};
    assert(Build(unusual,beyond,repeat));
    assert(repeat.indices.empty() && beyond.front().z > unusual.farDepth); // Shader использует полный цикл.
    auto mutation = r;
    auto relevant = std::find_if(mutation.grid.begin(), mutation.grid.end(), [](Entry e) { return e.count > 0; });
    assert(relevant != mutation.grid.end());
    const auto relevantIndex = uint32_t(relevant - mutation.grid.begin());
    const auto light = mutation.indices[relevant->offset];
    assert(Contains(mutation, relevantIndex, light));
    relevant->count = 0;
    assert(!Contains(mutation, relevantIndex, light));
    assert(r.indices.size() < r.grid.size() * lights.size());
    std::puts("clustered lighting CPU oracle: PASS");
}
