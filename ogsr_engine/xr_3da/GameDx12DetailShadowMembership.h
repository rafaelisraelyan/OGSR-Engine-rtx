#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gameaux12
{
// Индекс placement назначается при детерминированном декодировании слота,
// до камерных fade/SSA. Владелец обеспечивает уникальность пары в поколении.
struct DetailShadowIdentity
{
    uint32_t slot = 0, ordinal = 0;
    bool operator==(const DetailShadowIdentity&) const = default;
};

struct DetailShadowBound
{
    DetailShadowIdentity identity;
    std::array<float, 3> center{};
    float radius = 0.f;
};

// Внутренняя полуплоскость: dot(normal, point) + d >= 0. Нормализация
// необязательна. Неизвестная граница не используется для отбрасывания.
struct DetailShadowPlane
{
    std::array<float, 3> normal{};
    float d = 0.f;
};

// Снимок и его матрицы принадлежат вызывающему коду и не изменяются в query.
// Возвращаемые индексы действительны только для переданного снимка. Выделение
// памяти может бросить исключение: вызывающий код обязан сообщить backpressure,
// а не считать усечённый результат полным. Неполный источник отмечается отдельно.
inline std::vector<size_t> DetailShadowMembership(std::span<const DetailShadowBound> bounds,
    std::span<const DetailShadowPlane> planes)
{
    std::vector<size_t> result;
    result.reserve(bounds.size());
    for (size_t i = 0; i < bounds.size(); ++i)
    {
        const auto& b = bounds[i];
        bool keep = true;
        if (std::isfinite(b.center[0]) && std::isfinite(b.center[1]) &&
            std::isfinite(b.center[2]) && std::isfinite(b.radius) && b.radius >= 0.f)
        {
            for (const auto& p : planes)
            {
                const double nx = p.normal[0], ny = p.normal[1], nz = p.normal[2], d = p.d;
                if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz) || !std::isfinite(d))
                    continue;
                const double length = std::hypot(nx, ny, nz);
                if (!(length > 0.) || !std::isfinite(length)) continue;
                const double distance = nx * b.center[0] + ny * b.center[1] + nz * b.center[2] + d;
                if (distance < -static_cast<double>(b.radius) * length) { keep = false; break; }
            }
        }
        if (keep) result.push_back(i);
    }
    std::sort(result.begin(), result.end(), [&](size_t a, size_t b)
    {
        const auto& x = bounds[a].identity; const auto& y = bounds[b].identity;
        if (x.slot != y.slot) return x.slot < y.slot;
        if (x.ordinal != y.ordinal) return x.ordinal < y.ordinal;
        return a < b;
    });
    return result;
}
} // namespace gameaux12
