#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <utility>

namespace gameaux12
{
// requested отсортирован и не содержит повторов; владелец вызывает на одном потоке.
template<class Cache, class Cost, class Decode>
size_t UpdateDetailShadowCache(std::span<const uint32_t> requested, size_t cacheBudget,
    size_t decodeBudget, size_t transientBytes, Cache& cache, size_t& cachedBytes,
    Cost cost, Decode decode)
{
    std::erase_if(cache, [&](const auto& entry)
    {
        if (std::binary_search(requested.begin(), requested.end(), entry.first)) return false;
        cachedBytes -= cost(entry.second);
        return true;
    });
    if (cachedBytes > cacheBudget) { cache.clear(); cachedBytes = 0; }
    size_t attempts = 0, pending = 0;
    for (const uint32_t index : requested)
    {
        if (cache.contains(index)) continue;
        if (attempts >= decodeBudget || cachedBytes > cacheBudget ||
            transientBytes > cacheBudget - cachedBytes) { ++pending; continue; }
        ++attempts; // Бюджет ограничивает работу даже при неудачной вставке.
        auto slot = decode(index);
        const size_t bytes = cost(slot);
        if (bytes > cacheBudget - cachedBytes) { ++pending; continue; }
        cache.emplace(index, std::move(slot));
        cachedBytes += bytes;
    }
    return pending;
}
}
