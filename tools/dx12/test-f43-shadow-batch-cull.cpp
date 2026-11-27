#include "../../ogsr_engine/xr_3da/GameDx12ShadowCullMask.h"
#include <cassert>
#include <limits>
#include <vector>

using namespace game_shadow12;

static bool Visible(const ShadowCullPlane* planes, uint32_t mask, float x, float y, float z, float radius)
{
    for (unsigned i = 0; i < 6; ++i)
        if (mask & (1u << i))
        {
            const auto& p = planes[i];
            const float distance = p.nx * x + p.ny * y + p.nz * z + p.d;
            if (distance < -radius) return false;
        }
    return true;
}

int main()
{
    const float lo[3] = {-2, -2, -2}, hi[3] = {2, 2, 2};
    ShadowCullPlane planes[6]{};
    for (int nx = -2; nx <= 2; ++nx)
        for (int ny = -2; ny <= 2; ++ny)
            for (int nz = -2; nz <= 2; ++nz)
                for (int d = -8; d <= 8; ++d)
                {
                    for (auto& p : planes) p = {float(nx), float(ny), float(nz), float(d)};
                    const uint32_t reduced = ReduceShadowPlaneMask(planes, lo, hi, 63, true);
                    for (int x = -2; x <= 2; ++x)
                        for (int y = -2; y <= 2; ++y)
                            for (int z = -2; z <= 2; ++z)
                                for (int r = 0; r <= 2; ++r)
                                    assert(Visible(planes, 63, float(x), float(y), float(z), float(r)) ==
                                        Visible(planes, reduced, float(x), float(y), float(z), float(r)));
                    assert(ReduceShadowPlaneMask(planes, lo, hi, 63, false) == 63);
                }
    for (auto& p : planes) p = {0, 0, 0, 1};
    assert(ReduceShadowPlaneMask(planes, lo, hi, 63, true) == 0);
    planes[0].d = (std::numeric_limits<float>::infinity)();
    assert(ReduceShadowPlaneMask(planes, lo, hi, 63, true) == 1);
    planes[0].d = 0;
    assert(ReduceShadowPlaneMask(planes, lo, hi, 63, true) == 1);
    const ShadowCullPlane varied[6] = {{1, 0, 0, 3}, {-1, 0, 0, 0},
        {0, 1, 0, 3}, {0, -1, 0, 1}, {0, 0, 1, 3}, {0, 0, -1, 2}};
    const auto reduced = ReduceShadowPlaneMask(varied, lo, hi, 63, true);
    std::vector<int> reference, candidate;
    int id = 0;
    for (int x = -2; x <= 2; ++x)
        for (int y = -2; y <= 2; ++y)
            for (int z = -2; z <= 2; ++z, ++id)
            {
                if (Visible(varied, 63, float(x), float(y), float(z), 0)) reference.push_back(id);
                if (Visible(varied, reduced, float(x), float(y), float(z), 0)) candidate.push_back(id);
            }
    assert(reference == candidate);
}
