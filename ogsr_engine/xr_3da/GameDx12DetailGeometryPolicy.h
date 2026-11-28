#pragma once

#include <algorithm>
#include <cmath>

namespace gameaux12
{
// Геометрическое состояние placement вычисляется одинаково для color и
// shadow из одного eye и настроек кадра. Frustum camera сюда не входит:
// внекамерный caster остаётся кандидатом для теневой проекции.
enum class DetailGeometryCull { None, Invalid, Near, Distance, Lod };
struct DetailGeometryState
{
    float scale = 0.f;
    DetailGeometryCull cull = DetailGeometryCull::Invalid;
    bool Eligible() const noexcept { return cull == DetailGeometryCull::None; }
};

inline DetailGeometryState EvaluateDetailGeometry(float baseScale, float distanceSquared,
    float nearSquared, float fadeSquared, float modelRadius, float ssaDiscard,
    bool noScaleOnFade) noexcept
{
    if (!std::isfinite(baseScale) || baseScale <= 0.f ||
        !std::isfinite(distanceSquared) || distanceSquared < 0.f ||
        !std::isfinite(nearSquared) || nearSquared < 0.f ||
        !std::isfinite(fadeSquared) || fadeSquared <= 1.f ||
        !std::isfinite(modelRadius) || modelRadius < 0.f ||
        !std::isfinite(ssaDiscard) || ssaDiscard < 0.f)
        return {};
    if (distanceSquared < nearSquared)
        return {0.f, DetailGeometryCull::Near};
    if (distanceSquared >= fadeSquared)
        return {0.f, DetailGeometryCull::Distance};
    const float fade = (std::clamp)((fadeSquared - distanceSquared) / (fadeSquared - 1.f), 0.f, 1.f);
    const float scale = noScaleOnFade ? baseScale : baseScale * fade;
    const float projectedRadius = modelRadius * scale;
    const float ssa = noScaleOnFade ? scale : projectedRadius * projectedRadius /
        (std::max)(distanceSquared, 1.f);
    if (!std::isfinite(scale) || !std::isfinite(ssa)) return {};
    if (ssa < ssaDiscard) return {scale, DetailGeometryCull::Lod};
    return {scale, DetailGeometryCull::None};
}
}
