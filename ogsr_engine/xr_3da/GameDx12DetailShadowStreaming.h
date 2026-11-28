#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace gameaux12
{
struct DetailSlotRect { int32_t xmin = 0, xmax = -1, zmin = 0, zmax = -1; };

// Возрастающий обход объединения. Каждый слот учитывается в рабочем
// бюджете один раз. Биты указывают запрашивающие его каскады.
template<class Visit>
bool VisitDetailShadowSlots(std::span<const DetailSlotRect> areas, uint32_t width, Visit visit)
{
    struct Cursor { int32_t x, z; bool valid; };
    std::array<Cursor, 4> cursors{};
    if (areas.size() > cursors.size())
        return false;
    for (size_t i = 0; i < areas.size(); ++i)
    {
        const auto& area = areas[i];
        cursors[i] = {area.xmin, area.zmin, area.xmin <= area.xmax && area.zmin <= area.zmax};
    }
    const auto slot = [width](const Cursor& cursor)
    {
        return static_cast<uint32_t>(cursor.z) * width + static_cast<uint32_t>(cursor.x);
    };
    for (;;)
    {
        uint32_t next = UINT32_MAX;
        for (const auto& cursor : cursors)
            if (cursor.valid)
                next = (std::min)(next, slot(cursor));
        if (next == UINT32_MAX)
            return true;
        uint32_t mask = 0;
        for (size_t i = 0; i < areas.size(); ++i)
        {
            auto& cursor = cursors[i];
            const auto& area = areas[i];
            if (!cursor.valid || slot(cursor) != next)
                continue;
            mask |= 1u << i;
            if (++cursor.x > area.xmax)
            {
                cursor.x = area.xmin;
                cursor.valid = ++cursor.z <= area.zmax;
            }
        }
        if (!visit(next, mask))
            return false;
    }
}

// Peak покрывает одновременно резидентные и временные данные размещения.
// Владелец декодера определяет область учёта transient.
struct DetailStreamingBudget { size_t residentBytes, transientBytes, peakBytes, decodeSlots; };
struct DetailStreamingResult
{
    size_t pending = 0, denied = 0, retained = 0, attempts = 0;
    uint32_t deniedFrom = UINT32_MAX;
};

enum class DetailShadowCoverageReason : uint8_t
{
    Complete, Pending, Denied, InvalidInput, WorkingBudget, OutputBudget, HostMemory
};

inline const char* DetailShadowCoverageLabel(DetailShadowCoverageReason reason) noexcept
{
    switch (reason)
    {
    case DetailShadowCoverageReason::Complete: return "complete";
    case DetailShadowCoverageReason::Pending: return "pending";
    case DetailShadowCoverageReason::Denied: return "budget_denied";
    case DetailShadowCoverageReason::WorkingBudget: return "working_budget";
    case DetailShadowCoverageReason::OutputBudget: return "output_budget";
    case DetailShadowCoverageReason::HostMemory: return "host_memory";
    default: return "invalid_input";
    }
}

struct DetailShadowCascadeEvidence
{
    size_t requestedSlots = 0, pendingSlots = 0, deniedSlots = 0;
    DetailShadowCoverageReason reason = DetailShadowCoverageReason::InvalidInput;
};

template<class Cache>
uint32_t MeasureDetailShadowCoverage(std::span<const uint32_t> requested, std::span<const uint32_t> masks,
    const Cache& cache, uint32_t deniedFrom, std::span<DetailShadowCascadeEvidence> coverage)
{
    for (size_t i = 0; i < requested.size(); ++i)
    {
        const bool present = cache.contains(requested[i]);
        for (size_t cascade = 0; cascade < coverage.size(); ++cascade)
        {
            if (!(masks[i] & (1u << cascade)))
                continue;
            auto& evidence = coverage[cascade];
            ++evidence.requestedSlots;
            if (!present && requested[i] >= deniedFrom)
                ++evidence.deniedSlots;
            else if (!present)
                ++evidence.pendingSlots;
        }
    }
    uint32_t complete = 0;
    for (size_t cascade = 0; cascade < coverage.size(); ++cascade)
    {
        auto& evidence = coverage[cascade];
        evidence.reason = evidence.deniedSlots ? DetailShadowCoverageReason::Denied :
            evidence.pendingSlots ? DetailShadowCoverageReason::Pending : DetailShadowCoverageReason::Complete;
        if (evidence.reason == DetailShadowCoverageReason::Complete)
            complete |= 1u << cascade;
    }
    return complete;
}

// Обязательные резидентные слоты защищены от вытеснения при декодировании
// соседних областей. При первом точном отказе общий запрос уже невыполним
// в этом бюджете. Оставшийся суффикс считается отклонённым по политике
// admission, а доступные слоты продолжают подтверждать свои области.
template<class Cache, class Cost, class Decode, class Retain>
DetailStreamingResult UpdateDetailShadowCache(std::span<const uint32_t> requested,
    const DetailStreamingBudget& budget, Cache& cache, size_t& cachedBytes,
    Cost cost, Decode decode, Retain retain)
{
    DetailStreamingResult result;
    const auto required = [&](uint32_t id)
    {
        return std::binary_search(requested.begin(), requested.end(), id);
    };
    for (auto entry = cache.begin(); entry != cache.end();)
    {
        if (required(entry->first) || retain(entry->first))
        {
            ++entry;
            continue;
        }
        cachedBytes -= cost(entry->second);
        entry = cache.erase(entry);
    }
    const size_t residentLimit = (std::min)(budget.residentBytes, budget.peakBytes);
    const auto evictRetained = [&](size_t limit, size_t needed)
    {
        for (auto it = cache.begin(); it != cache.end() &&
            (cachedBytes > limit || needed > limit - cachedBytes);)
        {
            if (required(it->first))
            {
                ++it;
                continue;
            }
            cachedBytes -= cost(it->second);
            it = cache.erase(it);
        }
    };
    evictRetained(residentLimit, 0);
    if (cachedBytes > residentLimit)
    {
        cache.clear();
        cachedBytes = 0;
    }
    if (budget.transientBytes > budget.peakBytes)
        result.deniedFrom = 0;
    const size_t decodeLimit = (std::min)(residentLimit, budget.peakBytes -
        (std::min)(budget.transientBytes, budget.peakBytes));

    for (const uint32_t id : requested)
    {
        if (cache.contains(id))
            continue;
        if (id >= result.deniedFrom)
        {
            ++result.denied;
            continue;
        }
        if (result.attempts == budget.decodeSlots)
        {
            ++result.pending;
            continue;
        }
        evictRetained(decodeLimit, 0);
        if (cachedBytes > decodeLimit)
        {
            result.deniedFrom = id;
            ++result.denied;
            continue;
        }
        ++result.attempts;
        auto slot = decode(id);
        const size_t bytes = cost(slot);
        if (bytes <= budget.transientBytes && bytes <= residentLimit)
        {
            // После decode временный резерв освобождается. Сам placement
            // переходит в resident storage без второй копии.
            evictRetained(residentLimit, bytes);
            if (bytes <= residentLimit - cachedBytes)
            {
                cache.emplace(id, std::move(slot));
                cachedBytes += bytes;
                continue;
            }
        }
        result.deniedFrom = id;
        ++result.denied;
    }
    for (const auto& entry : cache)
        if (!required(entry.first))
            ++result.retained;
    return result;
}
}
