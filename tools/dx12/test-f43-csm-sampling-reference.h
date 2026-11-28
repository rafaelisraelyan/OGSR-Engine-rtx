#pragma once
// Independent numerical oracle for the small WARP sampling fixture.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace csm_reference
{
constexpr int Size = 8;
constexpr float Bias = 0.03125f;
constexpr uint32_t ProbeCount = 8;
constexpr float SunDepth(uint32_t slice) { return 0.125f + 0.25f * slice; }
constexpr float LocalDepth(uint32_t layer) { return 0.03125f * (layer + 1); }
constexpr uint32_t PhysicalLocalLayer(uint32_t logical) { return logical%8<2 ? logical^1u : logical; }

// Lower five rows are uniform; the upper three rows of slice zero
// contain a vertical step and a checkerboard.
constexpr float Texel(uint32_t slice, int x, int y)
{
    if (x < 0 || y < 0 || x >= Size || y >= Size)
        return 1.0f;
    if (slice == 0 && y < 3)
        return (y == 0 ? x >= 4 : ((x + y) % 2 != 0)) ? 0.625f : 0.375f;
    return SunDepth(slice);
}
struct Probe { double u, v, z; };
constexpr std::array<Probe, ProbeCount> Probes{{
    {0.46875, 0.0625, 0.5}, {0.53125, 0.0625, 0.640625},
    {0.46875, 0.265625, 0.5}, {0.53125, 0.3125, 0.5},
    {0.03125, 0.1875, 0.5}, {0.96875, 0.1875, 0.5},
    {0.4375, 0.4375, 0.5}, {0.5625, 0.6875, 0.5}
}};
inline double Compare(int x, int y, double z)
{
    return z <= Texel(0, x, y) ? 1.0 : 0.0;
}
// Integrate point comparisons with triangular weights in normalized UV.
// Analytical fixture-plane dz/du = -2 * nx / xScale = -4.
inline double Linear(double u, double v, double z, double gradient = 0)
{
    double sum = 0;
    for (int y = -1; y <= Size; ++y)
        for (int x = -1; x <= Size; ++x)
        {
            const double cx = (x + .5) / Size, cy = (y + .5) / Size;
            const double wx = (std::max)(0.0, 1.0 - std::abs(u - cx) * Size);
            const double wy = (std::max)(0.0, 1.0 - std::abs(v - cy) * Size);
            if (wx && wy) sum += wx * wy * Compare(x,y,z + gradient * (cx-u));
        }
    return sum;
}
inline double Box(uint32_t slice, double u, double v, double z, int radius)
{
    const int x0 = static_cast<int>(std::floor(u * Size));
    const int y0 = static_cast<int>(std::floor(v * Size));
    double sum = 0;
    for (int y = y0-radius; y <= y0+radius; ++y)
        for (int x = x0-radius; x <= x0+radius; ++x)
            sum += z <= Texel(slice,x,y) ? 1.0 : 0.0;
    const int width = radius*2+1;
    return sum / (width*width);
}
// Независимый эталон: сумма N*N билинейных сравнений, а не свёрнутые веса GPU.
inline double Continuous(uint32_t slice, double u, double v, double z, int radius, double gx = 0, double gy = 0)
{
    double sum = 0;
    for (int oy = -radius; oy <= radius; ++oy)
        for (int ox = -radius; ox <= radius; ++ox)
        {
            const double sampleU = u + double(ox) / Size, sampleV = v + double(oy) / Size;
            const double px = sampleU * Size - .5, py = sampleV * Size - .5;
            const int bx = static_cast<int>(std::floor(px)), by = static_cast<int>(std::floor(py));
            const double fx = px - bx, fy = py - by;
            for (int y = 0; y != 2; ++y) for (int x = 0; x != 2; ++x)
            {
                const double cx = (bx + x + .5) / Size, cy = (by + y + .5) / Size;
                sum += (x ? fx : 1-fx) * (y ? fy : 1-fy) *
                    (z + gx*(cx-u) + gy*(cy-v) <= Texel(slice,bx+x,by+y) ? 1.0 : 0.0);
            }
        }
    const int width = radius*2+1;
    return sum / (width*width);
}
// Эталон локальной карты: повтор краевого текселя и корректировка его
// реального центра. Суммируем отдельные билинейные выборки.
inline double LocalContinuous(double u, double v, double receiver, double stored, int radius, double gx, double gy)
{
    double sum = 0;
    for (int oy=-radius; oy<=radius; ++oy) for (int ox=-radius; ox<=radius; ++ox)
    {
        const double px=u*Size+ox-.5, py=v*Size+oy-.5;
        const int bx=(int)std::floor(px), by=(int)std::floor(py);
        const double fx=px-bx, fy=py-by;
        for (int y=0;y<2;++y) for (int x=0;x<2;++x)
        {
            const int tx=(std::clamp)(bx+x,0,Size-1), ty=(std::clamp)(by+y,0,Size-1);
            const double z=receiver+gx*((tx+.5)/Size-u)+gy*((ty+.5)/Size-v);
            sum+=(x?fx:1-fx)*(y?fy:1-fy)*(z<=stored ? 1.0 : 0.0);
        }
    }
    const int width=radius*2+1;
    return sum/(width*width);
}
inline double Filter(uint32_t mode, Probe p, bool slope)
{
    const double z = p.z - Bias;
    const int x0 = static_cast<int>(std::floor(p.u * Size));
    const int y0 = static_cast<int>(std::floor(p.v * Size));
    if (mode == 1) return Compare(x0,y0,z);
    if (mode >= 9 && mode <= 11) return Continuous(0,p.u,p.v,z,mode-8,slope ? -4.0 : 0.0);
    if (mode == 5 || mode == 6 || mode == 8)
        return Box(0,p.u,p.v,z,mode == 5 ? 1 : mode == 6 ? 2 : 3);
    const double base = Linear(p.u,p.v,z,slope && (mode == 4 || mode == 7) ? -4.0 : 0.0);
    if (mode == 3 && base > 0 && base < 1)
        return (base + Linear(p.u+.75/Size,p.v+.5/Size,z) +
            Linear(p.u-.75/Size,p.v-.5/Size,z)) / 3.0;
    return base;
}
enum Failure : uint32_t { Primary=1, Overlap=2, Inactive=4, Local=8, Pcf=16, Projection=32 };
struct Expected
{
    std::array<double,4> rgba;
    uint32_t category;
    int exactIndexChannel = -1;
};
inline Expected Pixel(uint32_t x,uint32_t y,uint32_t sun,uint32_t local,uint32_t filter)
{
    if (y == 5)
    {
        if (x >= sun && x < sun+local)
            return {{1.0/255,(x-sun+1)/255.0,.5,1},Projection,1};
        return {{3.0/255,(local+1)/255.0,0,1},Projection,1};
    }
    if (y == 4)
    {
        if (x >= ProbeCount) return {{0,0,0,1},Pcf};
        return {{Filter(filter,Probes[x],false),Filter(filter,Probes[x],true),x == 0 ? .25 : x == 1 ? .75 : 0,1},Pcf};
    }
    if (x >= sun && x < sun+local) return {{LocalDepth(PhysicalLocalLayer(x-sun)),1,0,1},Local};
    if (x >= sun) return {{0,0,1,1},Inactive,1};
    // Wide 7x7 reaches patterned rows and white border even in the
    // primary/overlap probes. Compute the original fixture projection.
    const double u = .25 + .0625 * (x + (y == 2 ? .875 : .5));
    const auto visibility = [&](uint32_t slice) {
        return filter >= 9 && filter <= 11 ? Continuous(slice,u,.6875,.5-Bias,filter-8) :
            filter == 8 ? Box(slice,u,.6875,.5-Bias,3) :
            (SunDepth(slice) >= .5-Bias ? 1.0 : 0.0);
    };
    const double visible = visibility(x);
    if (y == 2)
    {
        const bool next = x+1 < sun;
        const double neighbor = next ? visibility(x+1) : 0.0;
        // Exact midpoint of smoothstep interval [0.75,1].
        const double blended = next ? (visible+neighbor)*.5 : (visible+1)*.5;
        return {{blended,next?(x+2)/255.0:0,next?.5:0,next?0:.5},Overlap,1};
    }
    if (y == 3) return {{visible,.5,0,1},Projection};
    return {{SunDepth(x),(x+1)/255.0,visible,1},Primary,1};
}
}
