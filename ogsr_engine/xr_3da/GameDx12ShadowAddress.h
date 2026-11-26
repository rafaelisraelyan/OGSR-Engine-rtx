#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace game_shadow12
{
// 16-byte GPU ABI, appended to the four matrix columns. Page is a frame-local
// descriptor ordinal, never a persistent resource ID. Group packs face count
// in the low byte and face index in the next byte. Zero resolution is invalid.
struct ShadowViewAddress
{
    uint32_t page = 0, slice = 0, resolution = 0, group = 0;
};
static_assert(sizeof(ShadowViewAddress) == 16);
constexpr ShadowViewAddress MakeShadowAddress(uint32_t page,uint32_t slice,uint32_t resolution,
    uint32_t faces,uint32_t face) noexcept
{
    if (!resolution || (resolution & (resolution-1)) || (faces!=1 && faces!=6) || face>=faces)
        return {};
    return {page,slice,resolution,faces | (face<<8)};
}
constexpr ShadowViewAddress LegacyShadowAddress(uint32_t slice,uint32_t resolution) noexcept
{
    const uint32_t offset=slice%8;
    return MakeShadowAddress(0,slice,resolution,offset<2?1u:6u,offset<2?0u:offset-2);
}
struct ShadowAddressGroup { uint64_t light; uint32_t firstProjection, faces; };
struct ShadowAddressPlan
{
    std::vector<uint64_t> pageIds;
    std::vector<ShadowAddressGroup> groups;
    std::vector<ShadowViewAddress> views;
};
// Accepts ShadowArena's page/placement records. Physical pages and light IDs
// must be unique; every placed group is complete and non-overlapping. Output
// is deterministic independently of allocator traversal. Caller validates
// arena epoch/slot/fence and installs descriptors in exactly pageIds order.
// The four sun projection IDs are reserved outside this local view stream.
template<class Page,class Placement>
bool BuildShadowAddresses(std::span<const Page> pages,std::span<const Placement> placed,ShadowAddressPlan& out)
{
    constexpr uint32_t max=(std::numeric_limits<uint32_t>::max)();
    if (pages.size()>max || placed.size()>max-4u) return false;
    ShadowAddressPlan plan;
    std::vector<const Page*> sortedPages;
    sortedPages.reserve(pages.size());
    for (const auto& p:pages) sortedPages.push_back(&p);
    std::sort(sortedPages.begin(),sortedPages.end(),[](auto a,auto b){return a->id<b->id;});
    std::unordered_map<uint64_t,uint32_t> index;
    index.reserve(pages.size());
    for (auto p:sortedPages)
    {
        if (!p->id || !p->spec.resolution || (p->spec.resolution&(p->spec.resolution-1)) ||
            !p->spec.slices || !p->spec.bytes || !index.emplace(p->id,static_cast<uint32_t>(plan.pageIds.size())).second)
            return false;
        plan.pageIds.push_back(p->id);
    }
    std::vector<const Placement*> sorted;
    sorted.reserve(placed.size());
    uint64_t count=0;
    for (const auto& p:placed)
    {
        const auto it=index.find(p.page);
        if (!p.light || it==index.end() || (p.faces!=1 && p.faces!=6)) return false;
        const auto& spec=sortedPages[it->second]->spec;
        if (p.resolution!=spec.resolution || p.first>spec.slices || p.faces>spec.slices-p.first ||
            count+p.faces>max-4u) return false;
        count+=p.faces;
        sorted.push_back(&p);
    }
    std::sort(sorted.begin(),sorted.end(),[](auto a,auto b){
        return a->page!=b->page?a->page<b->page:a->first<b->first;
    });
    for (size_t i=1;i<sorted.size();++i)
        if (sorted[i-1]->page==sorted[i]->page && sorted[i]->first<sorted[i-1]->first+sorted[i-1]->faces)
            return false;
    std::sort(sorted.begin(),sorted.end(),[](auto a,auto b){return a->light<b->light;});
    plan.groups.reserve(sorted.size());
    plan.views.reserve(static_cast<size_t>(count));
    for (auto p:sorted)
    {
        if (!plan.groups.empty() && plan.groups.back().light==p->light) return false;
        plan.groups.push_back({p->light,static_cast<uint32_t>(plan.views.size()),p->faces});
        const uint32_t page=index.at(p->page);
        for (uint32_t face=0;face<p->faces;++face)
            plan.views.push_back(MakeShadowAddress(page,p->first+face,p->resolution,p->faces,face));
    }
    out=std::move(plan);
    return true;
}
}
