#pragma once
#include "GameDx12ShadowViews.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>

namespace game_shadow12
{
// CPU-only, owner-thread allocation of complete spot/cubemap slots.
// This remembers ownership, never claims the old GPU depth contents are valid.
constexpr uint32_t MaxLocalCandidates = 16;
constexpr uint32_t NoCandidate = UINT32_MAX;
struct LocalCandidate
{
    uint64_t id = 0;
    double score = 0;
    bool spot = false;
    bool near = false;
    bool eligible = false;
};
struct LocalAllocationState
{
    uint64_t session = 0, epoch = 0;
    uint32_t banks = 0;
    std::array<uint64_t, MaxLocalSlots> owners{};
};
struct LocalAllocation
{
    LocalAllocationState next{};
    std::array<uint32_t, MaxLocalSlots> candidate{};
    uint32_t eligible = 0, retained = 0, assigned = 0, released = 0, misses = 0;
    uint32_t eligibleMask = 0;
};

inline LocalAllocation AllocateLocalShadows(std::span<const LocalCandidate> input,
    uint32_t banks, uint64_t session, uint64_t epoch, const LocalAllocationState& previous,
    bool stablePriority = true) noexcept
{
    LocalAllocation out;
    out.candidate.fill(NoCandidate);
    if (!ValidBanks(banks) || input.size() > MaxLocalCandidates)
        return out;
    out.next.session = session;
    out.next.epoch = epoch;
    out.next.banks = banks;
    const bool sameLevel = previous.session == session && previous.epoch == epoch && ValidBanks(previous.banks);
    const uint32_t oldSlots = sameLevel ? previous.banks * SlotsPerBank : 0;
    const uint32_t slots = banks * SlotsPerBank;
    std::array<bool, MaxLocalCandidates> eligible{}, selected{};
    std::array<double, MaxLocalCandidates> scores{};
    for (uint32_t i = 0; i < input.size(); ++i)
    {
        const auto& c = input[i];
        if (!c.eligible || !c.id || !std::isfinite(c.score) || c.score < 0)
            continue;
        // Reject ambiguous identities rather than bind a map to the wrong light.
        uint32_t copies = 0;
        for (const auto& other : input)
            copies += other.eligible && other.id == c.id;
        if (copies != 1)
            continue;
        eligible[i] = true;
        out.eligibleMask |= 1u << i;
        ++out.eligible;
        bool held = false;
        for (uint32_t s = 0; s < oldSlots; ++s)
            held |= SpotSlot(s) == c.spot && previous.owners[s] == c.id;
        scores[i] = (std::min)(c.score, (std::numeric_limits<double>::max)() / 4) *
            (held ? 1.25 : 1.0) * (stablePriority && c.near ? 1.25 : 1.0);
    }
    // Select winners by type first, before assigning physical slots.
    std::array<uint32_t, MaxLocalSlots> ranked{};
    ranked.fill(NoCandidate);
    for (uint32_t s = 0; s < slots; ++s)
    {
        uint32_t best = NoCandidate;
        for (uint32_t i = 0; i < input.size(); ++i)
        {
            if (!eligible[i] || selected[i] || input[i].spot != SpotSlot(s))
                continue;
            const bool better = best == NoCandidate ||
                (!stablePriority && input[i].near != input[best].near ? input[i].near :
                    scores[i] > scores[best] || (scores[i] == scores[best] && input[i].id < input[best].id));
            if (better) best = i;
        }
        if (best != NoCandidate)
        {
            selected[best] = true;
            ranked[s] = best;
        }
    }
    // A surviving owner keeps the identical physical base, even after ranking changes.
    for (uint32_t s = 0; s < (std::min)(slots, oldSlots); ++s)
        for (uint32_t r = 0; r < slots; ++r)
            if (ranked[r] != NoCandidate && SpotSlot(r) == SpotSlot(s) &&
                input[ranked[r]].id == previous.owners[s])
            {
                out.candidate[s] = ranked[r];
                ranked[r] = NoCandidate;
                ++out.retained;
                break;
            }
    for (uint32_t s = 0; s < slots; ++s)
        if (out.candidate[s] == NoCandidate)
            for (uint32_t r = 0; r < slots; ++r)
                if (ranked[r] != NoCandidate && SpotSlot(r) == SpotSlot(s))
                {
                    out.candidate[s] = ranked[r];
                    ranked[r] = NoCandidate;
                    break;
                }
    for (uint32_t s = 0; s < slots; ++s)
        if (out.candidate[s] != NoCandidate)
        {
            out.next.owners[s] = input[out.candidate[s]].id;
            ++out.assigned;
        }
    for (uint32_t s = 0; s < oldSlots; ++s)
        if (previous.owners[s] && previous.owners[s] != out.next.owners[s])
            ++out.released;
    out.misses = out.eligible - out.assigned;
    return out;
}
}
