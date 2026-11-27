#pragma once
#include <cmath>
#include <cstdint>
#include <string_view>

namespace game_shadow12
{
// Значения существующих solar modes используются диагностикой и WARP.
inline constexpr uint32_t FirstContinuousSunMode = 9;

inline uint32_t SunFilterMode(std::string_view name) noexcept
{
    constexpr std::string_view names[] = {"legacyauto", "point", "bilinear", "legacy",
        "receiverplane", "pcf3", "pcf5", "fr3", "pcf7", "subtexel3", "subtexel5", "subtexel7"};
    for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (name == names[i])
            return i;
    return FirstContinuousSunMode;
}

inline uint32_t LocalFilterRadius(std::string_view name) noexcept
{
    if (name == "legacy")
        return 0;
    if (name == "subtexel5")
        return 2;
    if (name == "subtexel7")
        return 3;
    return 1;
}

// Четыре текселя: радиус 7x7 и последний тексель билинейной опоры.
// Расширение выполняется в плоскости проекции; угловая добавка 0.04 не
// гарантирует место при малых картах или широком конусе spot.
inline constexpr float LocalFilterGuardTexels = 4.0f;
inline constexpr float PointFaceBlendTexels = 2.0f;

inline float PointFaceBlendStart(uint32_t resolution) noexcept
{
    return (resolution - 2.0f * (LocalFilterGuardTexels + PointFaceBlendTexels)) /
        (resolution - 2.0f * LocalFilterGuardTexels);
}

inline bool GuardedLocalFov(float nominalFov, uint32_t minimumResolution, float& result,
    bool blendPointFaces = false) noexcept
{
    constexpr float pi = 3.14159265358979323846f;
    const float guard = LocalFilterGuardTexels + (blendPointFaces ? PointFaceBlendTexels : 0.0f);
    if (!std::isfinite(nominalFov) || nominalFov <= 0.0f || nominalFov >= pi ||
        minimumResolution <= 2 * guard)
        return false;
    const float tangent = std::tan(nominalFov * 0.5f) /
        (1.0f - 2.0f * guard / static_cast<float>(minimumResolution));
    result = 2.0f * std::atan(tangent);
    return std::isfinite(result) && result > 0.0f && result < pi;
}
}
