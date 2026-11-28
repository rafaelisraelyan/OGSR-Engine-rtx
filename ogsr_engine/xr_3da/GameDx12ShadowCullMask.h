#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace game_shadow12
{
// Плоскость совместима с выражением nx*x + ny*y + nz*z + d в SphereInside.
struct ShadowCullPlane { float nx = 0, ny = 0, nz = 0, d = 0; };

inline bool ShadowSphereInside(const ShadowCullPlane planes[6], const float center[3], float radius) noexcept
{
    for (uint32_t index = 0; index < 6; ++index)
    {
        const auto& plane = planes[index];
        const float distance = plane.nx * center[0] + plane.ny * center[1] +
                               plane.nz * center[2] + plane.d;
        if (distance < -radius)
            return false;
    }
    return true;
}

// boundsVerified означает: все центры потомков конечны и лежат внутри bounds,
// радиусы конечны и неотрицательны. Иначе вызывающий обязан передать false.
// Каждый снятый бит гарантирует float distance >= 0, а значит sphere test
// не может отклонить сферу по этой плоскости. double вычисляет оценку минимума;
// 16 epsilon покрывают три умножения и три сложения float, включая FMA,
// а добавка denorm_min покрывает абсолютную ошибку субнормальных результатов.
// При риске overflow, неподтверждённых или неконечных данных бит сохраняется.
// Предполагается стандартное округление IEEE-754, без fast-math.
#ifdef _MSC_VER
#pragma float_control(precise, on, push)
#endif
inline uint32_t ReduceShadowPlaneMask(const ShadowCullPlane planes[6], const float minimum[3],
    const float maximum[3], uint32_t mask, bool boundsVerified) noexcept
{
    if (!boundsVerified)
        return mask;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || minimum[axis] > maximum[axis])
            return mask;
    for (unsigned i = 0; i < 6; ++i)
    {
        const uint32_t bit = 1u << i;
        if (!(mask & bit)) continue;
        const auto& p = planes[i];
        const float coeff[3] = {p.nx, p.ny, p.nz};
        if (!std::isfinite(p.d) || !std::isfinite(p.nx) || !std::isfinite(p.ny) || !std::isfinite(p.nz))
            continue;
        double lower = p.d, magnitude = std::abs(static_cast<double>(p.d));
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const double c = coeff[axis];
            lower += c * (c >= 0 ? minimum[axis] : maximum[axis]);
            magnitude += std::abs(c) * (std::max)(std::abs(static_cast<double>(minimum[axis])),
                std::abs(static_cast<double>(maximum[axis])));
        }
        if (magnitude >= static_cast<double>((std::numeric_limits<float>::max)()) / 2)
            continue;
        const double error = magnitude * (16.0 * std::numeric_limits<float>::epsilon()) +
            16.0 * std::numeric_limits<float>::denorm_min();
        if (lower > error)
            mask &= ~bit;
    }
    return mask;
}
#ifdef _MSC_VER
#pragma float_control(pop)
#endif
}
