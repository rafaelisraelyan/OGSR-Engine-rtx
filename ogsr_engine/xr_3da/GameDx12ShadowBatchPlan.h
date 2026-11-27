#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace game_shadow12
{
struct ShadowBatchRange
{
    uint32_t first = 0, last = 0; // Полуинтервал исходных проекций.
};

// Нулевой вес тоже требует записи переходов и очистки, поэтому учитывается как 1.
// Границы детерминированы, порядок проекций и покрытие без пропусков сохраняются.
inline bool PlanShadowBatches(std::span<const uint32_t> draws, uint32_t maxBatches,
    std::vector<ShadowBatchRange>& out)
{
    if (!maxBatches || draws.size() > (std::numeric_limits<uint32_t>::max)())
        return false;
    std::vector<ShadowBatchRange> result;
    const auto count = static_cast<uint32_t>(draws.size());
    if (!count)
    {
        out = std::move(result);
        return true;
    }
    const auto batches = (std::min)(count, maxBatches);
    result.reserve(batches);
    uint64_t remaining = 0;
    for (auto value : draws)
        remaining += (std::max)(value, 1u);
    uint32_t first = 0;
    for (uint32_t batch = 0; batch < batches; ++batch)
    {
        const auto left = batches - batch;
        const uint32_t limit = count - (left - 1);
        const uint64_t target = remaining / left + (remaining % left >= (left + 1ull) / 2 ? 1 : 0);
        uint32_t last = first;
        uint64_t weight = 0;
        // Выбираем ближайшую границу к среднему оставшемуся весу.
        while (last < limit)
        {
            const uint64_t next = (std::max)(draws[last], 1u);
            if (last != first && weight >= target)
                break;
            if (last != first && weight + next > target &&
                target - weight <= weight + next - target)
                break;
            weight += next;
            ++last;
        }
        result.push_back({first, last});
        remaining -= weight;
        first = last;
    }
    out = std::move(result);
    return true;
}
}
