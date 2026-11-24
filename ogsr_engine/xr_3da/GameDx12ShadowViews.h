#pragma once

#include <cstdint>

namespace game_shadow12
{
constexpr uint32_t MaxSunCascades = 4;
constexpr uint32_t SpotLayers = 2;
constexpr uint32_t PointFaces = 6;
constexpr uint32_t LocalLayers = SpotLayers + PointFaces;
constexpr uint32_t MaxViews = MaxSunCascades + LocalLayers;
static_assert(MaxViews <= 32);

enum class ViewKind : uint8_t { SunCascade, SpotLight, PointLightFace };

struct View
{
    uint32_t projection;

    constexpr bool IsSun() const noexcept { return projection < MaxSunCascades; }
    constexpr uint32_t Slice() const noexcept { return IsSun() ? projection : projection - MaxSunCascades; }
    constexpr ViewKind Kind() const noexcept
    {
        return IsSun() ? ViewKind::SunCascade : Slice() < SpotLayers ? ViewKind::SpotLight : ViewKind::PointLightFace;
    }
};
}
