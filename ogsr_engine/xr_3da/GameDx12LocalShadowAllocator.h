#pragma once
#include "GameDx12ShadowViews.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <unordered_map>
#include <vector>

namespace game_shadow12
{
// Dynamic candidate admission; the physical bank layout is still legacy.
// This remembers ownership, never claims the old GPU depth contents are valid.
constexpr uint32_t NoCandidate = UINT32_MAX;
struct LocalCandidate
{
    uint64_t id = 0;
    double score = 0;
    bool spot = false;
    bool nearCamera = false;
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
    std::vector<uint8_t> eligibleCandidates;
};

inline LocalAllocation AllocateLocalShadows(std::span<const LocalCandidate> input,
    uint32_t banks, uint64_t session, uint64_t epoch, const LocalAllocationState& previous,
    bool stablePriority = true)
{
    LocalAllocation out;
    out.candidate.fill(NoCandidate);
    if (!ValidBanks(banks) || input.size() > UINT32_MAX)
        return out;
    out.next.session = session;
    out.next.epoch = epoch;
    out.next.banks = banks;
    const bool sameLevel = previous.session == session && previous.epoch == epoch && ValidBanks(previous.banks);
    const uint32_t oldSlots = sameLevel ? previous.banks * SlotsPerBank : 0;
    const uint32_t slots = banks * SlotsPerBank;
    out.eligibleCandidates.resize(input.size());
    std::vector<uint8_t> selected(input.size());
    std::vector<double> scores(input.size());
    // Count duplicate eligible identities once in O(n), including invalid
    // scores, preserving the previous rejection semantics.
    std::unordered_map<uint64_t, uint8_t> identities;
    identities.reserve(input.size());
    for (const auto& c : input)
        if (c.eligible && c.id)
        {
            const auto [it, inserted] = identities.try_emplace(c.id, 1);
            if (!inserted) it->second = 2;
        }
    for (uint32_t i = 0; i < input.size(); ++i)
    {
        const auto& c = input[i];
        if (!c.eligible || !c.id || !std::isfinite(c.score) || c.score < 0 || identities.at(c.id) != 1)
            continue;
        out.eligibleCandidates[i] = 1;
        ++out.eligible;
        bool held = false;
        for (uint32_t s = 0; s < oldSlots; ++s)
            held |= SpotSlot(s) == c.spot && previous.owners[s] == c.id;
        scores[i] = (std::min)(c.score, (std::numeric_limits<double>::max)() / 4) *
            (held ? 1.25 : 1.0) * (stablePriority && c.nearCamera ? 1.25 : 1.0);
    }
    // Select winners by type first, before assigning physical slots.
    std::array<uint32_t, MaxLocalSlots> ranked{};
    ranked.fill(NoCandidate);
    for (uint32_t s = 0; s < slots; ++s)
    {
        uint32_t best = NoCandidate;
        for (uint32_t i = 0; i < input.size(); ++i)
        {
            if (!out.eligibleCandidates[i] || selected[i] || input[i].spot != SpotSlot(s))
                continue;
            const bool better = best == NoCandidate ||
                (!stablePriority && input[i].nearCamera != input[best].nearCamera ? input[i].nearCamera :
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
