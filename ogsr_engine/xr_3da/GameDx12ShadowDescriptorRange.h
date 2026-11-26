#pragma once

#include <cstdint>

namespace game_shadow12
{
struct ShadowDescriptorRange
{
    uint32_t first = 0;
    uint32_t count = 0;
};

// Слоты растут навстречу друг другу. Каждый может занять весь свободный
// хвост heap, если второй слот его не использует.
constexpr bool PlaceShadowDescriptors(uint32_t prefix, uint32_t capacity,
    uint32_t slot, uint32_t count, uint32_t otherCount, ShadowDescriptorRange& out) noexcept
{
    if (slot > 1 || prefix > capacity || !count || otherCount > capacity - prefix ||
        count > capacity - prefix - otherCount)
        return false;
    out = {slot == 0 ? prefix : capacity - count, count};
    return true;
}
}
