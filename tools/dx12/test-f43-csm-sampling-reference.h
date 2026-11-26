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
inline double Filter(uint32_t mode, Probe p, bool slope)
{
    const double z = p.z - Bias;
    const int x0 = static_cast<int>(std::floor(p.u * Size));
    const int y0 = static_cast<int>(std::floor(p.v * Size));
    if (mode == 1) return Compare(x0,y0,z);
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
        return {{Filter(filter,Probes[x],false),Filter(filter,Probes[x],true),0,1},Pcf};
    }
    if (x >= sun && x < sun+local) return {{LocalDepth(x-sun),1,0,1},Local};
    if (x >= sun) return {{0,0,1,1},Inactive,1};
    // Wide 7x7 reaches patterned rows and white border even in the
    // primary/overlap probes. Compute the original fixture projection.
    const double u = .25 + .0625 * (x + (y == 2 ? .875 : .5));
    const auto visibility = [&](uint32_t slice) {
        return filter == 8 ? Box(slice,u,.6875,.5-Bias,3) :
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
