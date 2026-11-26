#include "GameDx12ShadowAddress.h"
#include <cassert>
#include <cstdio>
#include <random>
using namespace game_shadow12;
struct Spec { uint32_t resolution,slices; uint64_t bytes; };
struct Page { uint64_t id; Spec spec; };
struct Placement { uint64_t light,page; uint32_t first,faces,resolution; };
int main()
{
    std::vector<Page> pages;
    std::vector<Placement> placements;
    for (uint32_t i=0;i<256;++i)
    {
        const uint32_t resolution=128u<<(i%4);
        pages.push_back({1000+i,{resolution,8,uint64_t(resolution)*resolution*32}});
        placements.push_back({10000+i,1000+i,2,6,resolution});
        placements.push_back({1+i,1000+i,0,1,resolution});
        placements.push_back({500+i,1000+i,1,1,resolution});
    }
    ShadowAddressPlan plan;
    const auto build=[&](ShadowAddressPlan& out){return BuildShadowAddresses(std::span<const Page>(pages),std::span<const Placement>(placements),out);};
    assert(build(plan) && plan.pageIds.size()==256 && plan.groups.size()==768 && plan.views.size()==2048);
    for (uint32_t i=0;i<plan.groups.size();++i)
    {
        const auto& g=plan.groups[i];
        const auto p=std::find_if(placements.begin(),placements.end(),[&](const auto& v){return v.light==g.light;});
        assert(p!=placements.end() && g.faces==p->faces);
        for (uint32_t f=0;f<g.faces;++f)
        {
            const auto& a=plan.views[g.firstProjection+f];
            assert(plan.pageIds[a.page]==p->page && a.slice==p->first+f && a.resolution==p->resolution && a.group==(g.faces|(f<<8)));
        }
    }
    std::mt19937 rng(431);
    for (int i=0;i<100;++i)
    {
        std::shuffle(pages.begin(),pages.end(),rng);
        std::shuffle(placements.begin(),placements.end(),rng);
        ShadowAddressPlan next;
        assert(build(next) && next.pageIds==plan.pageIds && next.groups.size()==plan.groups.size());
        for (size_t j=0;j<plan.groups.size();++j)
        {
            const auto a=next.groups[j],b=plan.groups[j];
            assert(a.light==b.light && a.firstProjection==b.firstProjection && a.faces==b.faces);
        }
        for (size_t j=0;j<plan.views.size();++j)
        {
            const auto a=next.views[j],b=plan.views[j];
            assert(a.page==b.page && a.slice==b.slice && a.resolution==b.resolution && a.group==b.group);
        }
    }
    auto good=placements;
    placements.push_back(placements.front()); placements.back().light=99999;
    assert(!build(plan) && plan.views.size()==2048);
    placements=good; placements[0].light=placements[1].light; assert(!build(plan));
    placements=good; placements[0].first=8; assert(!build(plan));
    placements=good; placements[0].resolution=1; assert(!build(plan));
    placements=good; placements[0].page=0; assert(!build(plan));
    placements=good; placements[0].faces=2; assert(!build(plan));
    placements=good; pages.push_back(pages.front()); assert(!build(plan));
    for (uint32_t i=0;i<24;++i)
    {
        auto a=LegacyShadowAddress(i,1024);
        assert(a.page==0 && a.slice==i && a.resolution==1024 && a.group==(i%8<2?1u:6u|((i%8-2)<<8)));
    }
    assert(!MakeShadowAddress(0,0,127,1,0).resolution);
    assert(!MakeShadowAddress(0,0,128,6,6).resolution);
    std::puts("PASS: 256 pages, 768 complete groups, 2048 views, 100 shuffled layouts and invalid placement rejection");
}
