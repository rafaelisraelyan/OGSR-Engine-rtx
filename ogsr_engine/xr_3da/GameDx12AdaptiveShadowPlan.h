#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace game_shadow12
{
// Logical planning only. Costs are supplied by the resource owner; a tier
// cost is not a DXGI reservation and does not assign a physical array slice.
struct AdaptiveTier
{
    uint32_t resolution;
    uint64_t bytesPerFace;
};
struct AdaptiveCandidate
{
    uint64_t id;
    double importance;
    double requestedResolution;
    uint32_t faces; // 1 spot, or complete six-face point group
};
struct AdaptiveAssignment
{
    uint64_t id;
    uint32_t faces;
    uint32_t requestedTier;
    uint32_t resolution; // 0: explicitly denied by budget
    uint64_t bytes;
    double priority = 0.0;
};
struct AdaptivePlan
{
    uint64_t session = 0, epoch = 0, frame = 0, policyRevision = 0;
    uint64_t budget = 0, usedBytes = 0;
    std::vector<AdaptiveAssignment> lights;
};
inline bool ValidAdaptiveTiers(std::span<const AdaptiveTier> tiers) noexcept
{
    uint32_t prevSize = 0;
    uint64_t prevBytes = 0;
    for (const auto& t : tiers)
    {
        if (!t.resolution || (t.resolution & (t.resolution - 1)) != 0 ||
            t.resolution <= prevSize || t.bytesPerFace <= prevBytes ||
            t.bytesPerFace > (std::numeric_limits<uint64_t>::max)() / 6)
            return false;
        prevSize = t.resolution; prevBytes = t.bytesPerFace;
    }
    return !tiers.empty();
}
inline size_t NearestAdaptiveTier(std::span<const AdaptiveTier> tiers, double target) noexcept
{
    size_t result = 0;
    for (size_t i = 1; i < tiers.size(); ++i)
        if (std::abs(target - tiers[i].resolution) < std::abs(target - tiers[result].resolution))
            result = i;
    return result;
}
// Admit minimum-resolution complete groups by importance first, then improve
// quality in priority order. All denied lights remain visible to the caller.
// Prior assignments affect resolution hysteresis and ranking, not GPU residency.
inline bool BuildAdaptivePlan(std::span<const AdaptiveTier> tiers,
    std::span<const AdaptiveCandidate> candidates, uint64_t budget,
    uint64_t session, uint64_t epoch, uint64_t frame, uint64_t policyRevision,
    const AdaptivePlan* previous, AdaptivePlan& out)
{
    if (!session || !epoch || !frame || !policyRevision || !ValidAdaptiveTiers(tiers)) return false;
    AdaptivePlan plan{session, epoch, frame, policyRevision, budget, 0, {}};
    struct Work { size_t index, target; double score; };
    std::vector<Work> work;
    std::unordered_map<uint64_t, AdaptiveAssignment> prior;
    std::unordered_set<uint64_t> seen;
    if (previous && previous->session == session && previous->epoch == epoch &&
        previous->policyRevision == policyRevision && previous->frame < frame)
    {
        for (const auto& a : previous->lights)
            if (a.resolution && (a.faces == 1 || a.faces == 6) &&
                !prior.emplace(a.id, a).second) return false;
    }
    plan.lights.reserve(candidates.size());
    work.reserve(candidates.size());
    seen.reserve(candidates.size());
    for (const auto& c : candidates)
    {
        if (!c.id || (c.faces != 1 && c.faces != 6) || !std::isfinite(c.importance) ||
            c.importance < 0 || !std::isfinite(c.requestedResolution) || c.requestedResolution <= 0 ||
            !seen.emplace(c.id).second) return false;
        const double target = std::clamp(c.requestedResolution,
            double(tiers.front().resolution), double(tiers.back().resolution));
        size_t wanted = NearestAdaptiveTier(tiers, target);
        const auto old = prior.find(c.id);
        double score = c.importance;
        if (old != prior.end() && old->second.faces == c.faces)
        {
            const auto oldTier = std::find_if(tiers.begin(), tiers.end(), [&](const auto& t) {
                return t.resolution == old->second.resolution;
            });
            if (oldTier != tiers.end())
            {
                const size_t oldIndex = static_cast<size_t>(oldTier - tiers.begin());
                if ((wanted < oldIndex && target > oldTier->resolution * .75) ||
                    (wanted > oldIndex && target < tiers[wanted].resolution)) wanted = oldIndex;
                const double maxScore = (std::numeric_limits<double>::max)();
                score = score >= maxScore / 1.25 ? maxScore : score * 1.25;
            }
        }
        work.push_back({plan.lights.size(), wanted, score});
        plan.lights.push_back({c.id, c.faces, tiers[wanted].resolution, 0, 0, score});
    }
    std::sort(work.begin(), work.end(), [&](const Work& a, const Work& b) {
        if (a.score != b.score) return a.score > b.score;
        return plan.lights[a.index].id < plan.lights[b.index].id;
    });
    for (const auto& w : work)
    {
        auto& a = plan.lights[w.index];
        const uint64_t cost = tiers.front().bytesPerFace * a.faces;
        if (cost > budget - plan.usedBytes) continue;
        a.resolution = tiers.front().resolution; a.bytes = cost; plan.usedBytes += cost;
    }
    for (const auto& w : work)
    {
        auto& a = plan.lights[w.index];
        if (!a.resolution) continue;
        for (size_t i = w.target; i > 0; --i)
        {
            const uint64_t cost = tiers[i].bytesPerFace * a.faces;
            if (cost - a.bytes > budget - plan.usedBytes) continue;
            plan.usedBytes += cost - a.bytes;
            a.resolution = tiers[i].resolution; a.bytes = cost; break;
        }
    }
    out = std::move(plan);
    return true;
}
}
