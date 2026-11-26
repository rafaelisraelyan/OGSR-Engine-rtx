#pragma once

#include "GameDx12ShadowAddress.h"

#include <unordered_set>

namespace game_shadow12
{
inline constexpr uint32_t ReservedCsmProjections = 4;

struct ShadowPacketRange
{
    uint32_t first = 0, count = 0;
};

struct ScheduledShadowView
{
    uint64_t light = 0, page = 0;
    uint32_t projection = 0, slice = 0, resolution = 0, face = 0;
    ShadowPacketRange packets{};
};

// Диапазоны ссылаются на неизменяемый снимок draw packets владельца кадра.
inline bool PlanShadowViews(const ShadowAddressPlan& addresses, std::span<const ShadowPacketRange> packets,
    size_t packetCount, std::vector<ScheduledShadowView>& out)
{
    const auto count = addresses.views.size();
    if (count > UINT32_MAX - ReservedCsmProjections || packets.size() != count ||
        !std::is_sorted(addresses.pageIds.begin(), addresses.pageIds.end()) ||
        std::adjacent_find(addresses.pageIds.begin(), addresses.pageIds.end()) != addresses.pageIds.end())
        return false;

    std::vector<ScheduledShadowView> result;
    result.reserve(count);
    std::unordered_set<uint64_t> slices;
    slices.reserve(count);
    uint64_t previousLight = 0;
    for (const auto& group : addresses.groups)
    {
        if (group.light <= previousLight || group.firstProjection != result.size() ||
            (group.faces != 1 && group.faces != 6) || group.faces > count - result.size())
            return false;
        previousLight = group.light;
        const auto& base = addresses.views[group.firstProjection];
        for (uint32_t face = 0; face < group.faces; ++face)
        {
            const auto index = group.firstProjection + face;
            const auto& view = addresses.views[index];
            const auto range = packets[index];
            if (view.page >= addresses.pageIds.size() || !addresses.pageIds[view.page] ||
                view.page != base.page || view.slice < base.slice || view.slice - base.slice != face ||
                !view.resolution || (view.resolution & (view.resolution - 1)) || view.resolution != base.resolution ||
                view.group != (group.faces | (face << 8)) ||
                range.first > packetCount || range.count > packetCount - range.first ||
                !slices.insert((static_cast<uint64_t>(view.page) << 32) | view.slice).second)
                return false;

            result.push_back({group.light, addresses.pageIds[view.page], ReservedCsmProjections + index,
                view.slice, view.resolution, face, range});
        }
    }
    if (result.size() != count)
        return false;
    out = std::move(result);
    return true;
}
}
