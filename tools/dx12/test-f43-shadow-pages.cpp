#include "GameDx12ShadowPagePlan.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <random>
using namespace game_shadow12;
int main()
{
    const std::array<AdaptiveTier,2> tiers{{{128,65536},{256,262144}}};
    const std::array<ShadowPageSpec,2> specs{{{128,12,786432},{256,12,3145728}}};
    std::vector<AdaptiveCandidate> lights{{1,5,128,6},{2,4,128,1},{3,3,256,6}};
    AdaptivePlan logical;
    ShadowPagePlan p,q;
    assert(BuildAdaptivePlan(tiers,lights,UINT64_MAX,1,1,1,1,nullptr,logical));
    assert(BuildShadowPagePlan(logical,specs,3932160,0,0,1,nullptr,p));
    assert(p.next.pages.size()==2 && p.next.placed.size()==3 && p.create.size()==2 && p.bytes==3932160);
    assert(!BuildShadowPagePlan(logical,specs,3932160,0,0,2,&p.next,q));
    assert(!BuildShadowPagePlan(logical,specs,3932160,1,1,2,&p.next,q));
    assert(!BuildShadowPagePlan(logical,specs,3932160,0,UINT64_MAX,UINT64_MAX,&p.next,q));
    logical.frame=2;
    assert(BuildShadowPagePlan(logical,specs,3932160,0,1,2,&p.next,q));
    assert(q.create.empty() && q.retire.empty() && q.next.placed.size()==3);
    for (size_t i=0;i<p.next.placed.size();++i)
        assert(p.next.placed[i].page==q.next.placed[i].page && p.next.placed[i].first==q.next.placed[i].first);
    assert(BuildShadowPagePlan(logical,specs,786432,0,1,2,&p.next,q));
    assert(q.retire.size()==1 && q.denied.size()==1 && q.denied[0]==3 && q.bytes==786432);
    logical.epoch=2;
    assert(BuildShadowPagePlan(logical,specs,3932160,0,1,2,&p.next,q));
    assert(q.create.size()==2 && q.retire.size()==2 && q.next.pages[0].id>=p.next.nextPage);
    logical.epoch=1;
    auto corrupt=p.next; corrupt.placed.push_back(corrupt.placed.front()); corrupt.placed.back().light=999;
    assert(!BuildShadowPagePlan(logical,specs,3932160,0,1,2,&corrupt,q));
    std::mt19937 rng(432);
    unsigned groups=0;
    p={};
    for (uint64_t frame=1;frame<=100;++frame)
    {
        lights.clear();
        for (uint64_t id=1;id<=512;++id)
            if (rng()%4) lights.push_back({id,double(rng()%100),rng()%2?128.0:256.0,id%3?1u:6u});
        assert(BuildAdaptivePlan(tiers,lights,UINT64_MAX,2,1,frame,1,nullptr,logical));
        const uint64_t budget=(rng()%64)*786432ull;
        const auto* old=frame==1?nullptr:&p.next;
        assert(BuildShadowPagePlan(logical,specs,budget,0,frame-1,frame,old,q));
        auto reverse=logical; std::reverse(reverse.lights.begin(),reverse.lights.end());
        ShadowPagePlan r;
        assert(BuildShadowPagePlan(reverse,specs,budget,0,frame-1,frame,old,r));
        assert(q.bytes<=budget && q.bytes==r.bytes && q.create==r.create && q.retire==r.retire && q.denied==r.denied);
        assert(q.next.placed.size()+q.denied.size()==lights.size() && q.next.placed.size()==r.next.placed.size());
        std::unordered_map<uint64_t,std::vector<uint8_t>> used;
        uint64_t bytes=0;
        for(const auto& page:q.next.pages) { assert(used.emplace(page.id,std::vector<uint8_t>(page.spec.slices)).second); bytes+=page.spec.bytes; }
        assert(bytes==q.bytes);
        for(size_t i=0;i<q.next.placed.size();++i)
        {
            const auto& a=q.next.placed[i]; const auto& b=r.next.placed[i];
            assert(a.light==b.light && a.page==b.page && a.first==b.first && a.faces==b.faces && a.resolution==b.resolution);
            auto& range=used.at(a.page);
            assert(a.first+a.faces<=range.size() && a.faces==(a.light%3?1u:6u));
            for(uint32_t j=0;j<a.faces;++j) assert(range[a.first+j]++==0);
            ++groups;
        }
        p=std::move(q);
    }
    std::printf("PASS: 100 frames, %u placed groups, fence, epoch, budget, stable placement and deterministic input order\n",groups);
}
