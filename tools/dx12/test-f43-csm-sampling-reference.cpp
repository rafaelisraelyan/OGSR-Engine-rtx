#include "test-f43-csm-sampling-reference.h"
#include <cstdio>
#include <cstdlib>

namespace ref = csm_reference;

void Require(bool ok)
{
    if (!ok)
    {
        std::fputs("FAIL: CSM CPU sampling reference\n", stderr);
        std::exit(1);
    }
}

// Four-tap implementation independent of the oracle's integration loop.
double FourTaps(double u, double v, double z, double gradient)
{
    const double px = u * 8 - .5, py = v * 8 - .5;
    const int ix = static_cast<int>(std::floor(px)), iy = static_cast<int>(std::floor(py));
    const double fx = px - ix, fy = py - iy;
    double sum = 0;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
        {
            const double cx = (ix + x + .5) / 8;
            const bool visible = z + gradient * (cx - u) <= ref::Texel(0, ix + x, iy + y);
            sum += (x ? fx : 1 - fx) * (y ? fy : 1 - fy) * visible;
        }
    return sum;
}

int main()
{
    unsigned checks = 0, slopes = 0;
    for (int u = 0; u <= 64; ++u)
        for (int v = 0; v <= 64; ++v)
            for (double z : {.125, .375, .5, .625, .875})
                for (double g : {0., -4.})
                {
                    const double a = ref::Linear(u / 64., v / 64., z, g);
                    const double b = FourTaps(u / 64., v / 64., z, g);
                    Require(std::abs(a - b) < 1e-12);
                    ++checks;
                }
    for (unsigned n = 1; n <= 4; ++n)
        for (unsigned l : {0u, 8u, 16u, 24u})
            for (unsigned f = 1; f <= 7; ++f)
                for (unsigned y = 1; y <= 5; ++y)
                    for (unsigned x = 0; x < (std::max)(n + l + 1, 8u); ++x)
                        for (double a : ref::Pixel(x, y, n, l, f).rgba)
                        {
                            Require(std::isfinite(a) && a >= 0 && a <= 1);
                            ++checks;
                        }
    for (unsigned x = 0; x < ref::ProbeCount; ++x)
    {
        const double a = ref::Filter(4, ref::Probes[x], false);
        const double b = ref::Filter(4, ref::Probes[x], true);
        if (a != b) ++slopes;
        std::printf("probe %u flat=%f slope=%f\n", x, a, b);
    }
    Require(slopes > 0);
    for (unsigned layer = 0; layer < 24; ++layer)
    {
        const auto local = ref::Pixel(4 + layer, 1, 4, 24, 4);
        const auto projection = ref::Pixel(4 + layer, 5, 4, 24, 4);
        Require(local.rgba[0] == ref::LocalDepth(layer) && local.rgba[1] == 1 && local.rgba[2] == 0);
        Require(projection.rgba[0] == 1.0/255 && projection.rgba[1] == (layer+1)/255.0);
        ++checks;
    }
    Require(ref::Pixel(28, 5, 4, 24, 4).rgba[0] == 3.0/255);
    Require(ref::Pixel(1, 2, 4, 8, 4).rgba[0] == .5);
    Require(ref::Pixel(0, 2, 1, 0, 4).rgba[0] == .5);
    std::printf("PASS: %u numerical checks; %u gradient-sensitive probes\n", checks, slopes);
}
