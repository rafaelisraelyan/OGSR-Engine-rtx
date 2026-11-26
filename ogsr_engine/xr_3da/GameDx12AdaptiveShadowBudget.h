#pragma once

#include "GameDx12ShadowAddress.h"

namespace game_shadow12
{
struct AdaptiveSlotBudget
{
    uint64_t finalBytes = 0;
    uint64_t newBytes = 0;
};

constexpr AdaptiveSlotBudget PlanAdaptiveSlotBudget(uint64_t budget, uint64_t usage, uint64_t current,
    uint64_t other, uint64_t incrementalReserve) noexcept
{
    // CurrentUsage уже включает резидентные страницы. Кредит относится только
    // к текущему слоту и доступен после успешной публикации замены.
    const uint64_t credited = (std::min)(current, usage - (std::min)(other, usage));
    const uint64_t nonReplaceableUsage = usage - credited;
    const uint64_t finalHeadroom = budget > nonReplaceableUsage ? budget - nonReplaceableUsage : 0;
    const uint64_t peakHeadroom = budget > usage ? budget - usage : 0;
    return {finalHeadroom - (std::min)(finalHeadroom, incrementalReserve),
        peakHeadroom - (std::min)(peakHeadroom, incrementalReserve)};
}

// Возвращает только физически опубликованные группы, сохраняя ID и порядок
// t14. Неунифицированные или частично размещённые point lights не принимаются.
inline bool ReconcileAdaptiveAddresses(std::span<const uint64_t> lightIds,
    const ShadowAddressPlan& addresses, std::vector<uint32_t>& firstPlusOne)
{
    if (!std::is_sorted(lightIds.begin(), lightIds.end()) ||
        std::adjacent_find(lightIds.begin(), lightIds.end()) != lightIds.end())
        return false;
    std::vector<uint32_t> result(lightIds.size());
    size_t nextProjection = 0, nextLight = 0;
    for (const auto& group : addresses.groups)
    {
        while (nextLight < lightIds.size() && lightIds[nextLight] < group.light)
            ++nextLight;
        if (nextLight == lightIds.size() || lightIds[nextLight] != group.light ||
            group.firstProjection != nextProjection || group.firstProjection == UINT32_MAX ||
            (group.faces != 1 && group.faces != 6) ||
            group.faces > addresses.views.size() - nextProjection)
            return false;
        result[nextLight++] = group.firstProjection + 1;
        nextProjection += group.faces;
    }
    if (nextProjection != addresses.views.size())
        return false;
    firstPlusOne = std::move(result);
    return true;
}
}
