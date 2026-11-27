#pragma once
#include "GameDx12AdaptiveShadowPlan.h"

namespace game_shadow12
{
// One independent frame-slot arena. A page is a depth Texture2DArray of
// a single resolution. The owner supplies actual whole-resource costs.
struct ShadowPageSpec { uint32_t resolution, slices; uint64_t bytes; };
struct ShadowPage { uint64_t id; ShadowPageSpec spec; };
struct ShadowPlacement { uint64_t light, page; uint32_t first, faces, resolution; };
struct ShadowArena
{
    uint64_t session = 0, epoch = 0, policyRevision = 0, frame = 0, fence = 0;
    uint64_t nextPage = 1;
    uint32_t slot = 0;
    std::vector<ShadowPage> pages;
    std::vector<ShadowPlacement> placed;
};
struct ShadowPagePlan
{
    ShadowArena next;
    uint64_t bytes = 0;
    std::vector<uint64_t> create, retire, denied;
};
inline bool SamePageSpec(const ShadowPageSpec& a, const ShadowPageSpec& b) noexcept
{
    return a.resolution == b.resolution && a.slices == b.slices && a.bytes == b.bytes;
}
inline bool ValidPageSpec(const ShadowPageSpec& s) noexcept
{
    return s.resolution && !(s.resolution & (s.resolution - 1)) && s.slices >= 6 && s.bytes;
}
// Provisional plan only: owner waits for the slot, applies retire/create,
// records, then publishes next only after the reserved fence is signaled.
// On allocation/submit failure reconcile physical objects; on unknown GPU
// completion quarantine the arena. No GPU depth contents are cached here.
inline bool BuildShadowPagePlan(const AdaptivePlan& logical, std::span<const ShadowPageSpec> specs,
    uint64_t pageBudget, uint32_t slot, uint64_t completedFence, uint64_t submitFence,
    const ShadowArena* previous, ShadowPagePlan& out, uint32_t pageCapacity = UINT32_MAX)
{
    constexpr auto max = (std::numeric_limits<uint64_t>::max)();
    if (!logical.session || !logical.epoch || !logical.policyRevision || !logical.frame ||
        completedFence == max || submitFence == max || submitFence <= completedFence || specs.empty()) return false;
    std::unordered_map<uint32_t, ShadowPageSpec> byResolution;
    for (const auto& s : specs)
        if (!ValidPageSpec(s) || !byResolution.emplace(s.resolution,s).second) return false;
    ShadowPagePlan plan;
    auto& next = plan.next;
    next.session = logical.session; next.epoch = logical.epoch;
    next.policyRevision = logical.policyRevision; next.frame = logical.frame;
    next.slot = slot; next.fence = submitFence;
    struct WorkPage { ShadowPage page; std::vector<uint8_t> used; };
    std::vector<WorkPage> pages, old;
    std::unordered_map<uint64_t, size_t> active, oldIndex;
    std::unordered_map<uint64_t, ShadowPlacement> prior;
    bool compatible = false;
    if (previous)
    {
        if (previous->slot != slot || !previous->fence || previous->fence > completedFence ||
            !previous->nextPage) return false;
        compatible = previous->session == logical.session && previous->epoch == logical.epoch &&
            previous->policyRevision == logical.policyRevision;
        if (compatible && previous->frame >= logical.frame) return false;
        next.nextPage = previous->nextPage;
        for (const auto& p : previous->pages)
        {
            if (!p.id || p.id >= next.nextPage || !ValidPageSpec(p.spec) ||
                !oldIndex.emplace(p.id,old.size()).second) return false;
            old.push_back({p,std::vector<uint8_t>(p.spec.slices)});
        }
        for (const auto& p : previous->placed)
        {
            const auto it = oldIndex.find(p.page);
            if (!p.light || (p.faces != 1 && p.faces != 6) || it == oldIndex.end() ||
                !prior.emplace(p.light,p).second) return false;
            auto& w = old[it->second];
            if (p.resolution != w.page.spec.resolution || p.first > w.used.size() ||
                p.faces > w.used.size() - p.first) return false;
            for (uint32_t i = 0; i < p.faces; ++i)
                if (w.used[p.first+i]++) return false;
        }
        std::sort(old.begin(),old.end(),[](const auto& a,const auto& b){return a.page.id < b.page.id;});
        oldIndex.clear();
        for (size_t i=0;i<old.size();++i) oldIndex.emplace(old[i].page.id,i);
    }
    std::vector<AdaptiveAssignment> requests;
    std::unordered_set<uint64_t> seen, assigned;
    for (const auto& a : logical.lights)
    {
        if (!a.id || !std::isfinite(a.priority) || !seen.emplace(a.id).second || (a.faces != 1 && a.faces != 6) ||
            (!a.resolution && a.bytes) || (a.resolution && (!a.bytes || !byResolution.count(a.resolution)))) return false;
        if (a.resolution) requests.push_back(a);
    }
    // Физический план сохраняет приоритет с учётом hysteresis.
    // ID решает только ничью между источниками с одинаковым приоритетом.
    std::sort(requests.begin(),requests.end(),[](const auto& a,const auto& b){
        return a.priority != b.priority ? a.priority > b.priority : a.id < b.id;
    });
    const auto activate = [&](ShadowPage p) -> bool {
        if (pages.size() >= pageCapacity || p.spec.bytes > pageBudget-plan.bytes) return false;
        active.emplace(p.id,pages.size());
        pages.push_back({p,std::vector<uint8_t>(p.spec.slices)});
        plan.bytes += p.spec.bytes;
        return true;
    };
    const auto place = [&](WorkPage& w, const AdaptiveAssignment& a, uint32_t first) {
        for (uint32_t i=0;i<a.faces;++i) w.used[first+i]=1;
        next.placed.push_back({a.id,w.page.id,first,a.faces,a.resolution});
        assigned.emplace(a.id);
    };
    for (const auto& a : requests)
    {
        const auto& spec = byResolution.at(a.resolution);
        if (compatible)
        {
            const auto it = prior.find(a.id);
            if (it != prior.end())
            {
                const auto& p = it->second;
                const auto& oldPage = old[oldIndex.at(p.page)].page;
                if (p.faces == a.faces && SamePageSpec(oldPage.spec, spec) &&
                    (active.count(p.page) || activate(oldPage)))
                {
                    auto& page = pages[active.at(p.page)];
                    if (std::none_of(page.used.begin() + p.first, page.used.begin() + p.first + p.faces,
                            [](uint8_t used) { return used != 0; }))
                    {
                        place(page, a, p.first);
                        continue;
                    }
                }
            }
        }
        for (auto& w : pages)
        {
            if (w.page.spec.resolution != a.resolution) continue;
            uint32_t run = 0;
            for (uint32_t i=0;i<w.page.spec.slices;++i)
            {
                run = w.used[i] ? 0 : run+1;
                if (run == a.faces) { place(w,a,i+1-a.faces); break; }
            }
            if (assigned.count(a.id)) break;
        }
        if (assigned.count(a.id)) continue;
        if (compatible)
            for (const auto& w : old)
                if (!active.count(w.page.id) && SamePageSpec(w.page.spec,spec) && activate(w.page))
                { place(pages.back(),a,0); break; }
        if (assigned.count(a.id)) continue;
        if (pages.size() < pageCapacity && spec.bytes <= pageBudget-plan.bytes)
        {
            if (next.nextPage == max) return false;
            const uint64_t id = next.nextPage++;
            if (!activate({id,spec})) return false;
            plan.create.push_back(id);
            place(pages.back(),a,0);
        }
        else plan.denied.push_back(a.id);
    }
    for (const auto& w : pages) next.pages.push_back(w.page);
    for (const auto& w : old) if (!active.count(w.page.id)) plan.retire.push_back(w.page.id);
    out = std::move(plan);
    return true;
}
}
