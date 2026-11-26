#pragma once

#include <cstdint>

namespace game_shadow12
{
constexpr uint32_t MaxSunCascades = 4;
constexpr uint32_t SpotLayers = 2;
constexpr uint32_t PointFaces = 6;
constexpr uint32_t LayersPerBank = SpotLayers + PointFaces;
constexpr uint32_t MaxLocalBanks = 3;
constexpr uint32_t SlotsPerBank = SpotLayers + 1;
constexpr uint32_t MaxLocalSlots = MaxLocalBanks * SlotsPerBank;
constexpr uint32_t LocalLayers = MaxLocalBanks * LayersPerBank;
constexpr uint32_t MaxViews = MaxSunCascades + LocalLayers;
static_assert(MaxViews < 32);

constexpr uint32_t SlotBase(uint32_t slot) noexcept
{
    return (slot / SlotsPerBank) * LayersPerBank + slot % SlotsPerBank;
}
constexpr bool SpotSlot(uint32_t slot) noexcept { return slot % SlotsPerBank < SpotLayers; }
constexpr bool ValidBanks(uint32_t banks) noexcept { return banks >= 1 && banks <= MaxLocalBanks; }
constexpr bool ValidBase(uint32_t base, bool spot, uint32_t banks) noexcept
{
    return ValidBanks(banks) && base < banks * LayersPerBank &&
        (spot ? base % LayersPerBank < SpotLayers : base % LayersPerBank == SpotLayers);
}

enum class ViewKind : uint8_t { SunCascade, SpotLight, PointLightFace };
struct View
{
    uint32_t projection;
    constexpr bool IsSun() const noexcept { return projection < MaxSunCascades; }
    constexpr uint32_t Slice() const noexcept { return IsSun() ? projection : projection - MaxSunCascades; }
    constexpr ViewKind Kind() const noexcept
    {
        return IsSun() ? ViewKind::SunCascade : Slice() % LayersPerBank < SpotLayers ? ViewKind::SpotLight : ViewKind::PointLightFace;
    }
};
}
