#include "GameDx12AdaptiveShadowPlan.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <random>

using namespace game_shadow12;
int main()
{
    const std::array<AdaptiveTier, 3> tiers{{{128,65536},{256,262144},{512,1048576}}};
    const std::array<AdaptiveCandidate, 3> lights{{{1,10,512,6},{2,2,512,1},{3,1,256,1}}};
    AdaptivePlan p, q;
    assert(BuildAdaptivePlan(tiers,lights,8*65536,1,1,1,1,nullptr,p));
    assert(p.usedBytes == 8*65536 && p.lights[0].resolution == 128 && p.lights[2].resolution == 128);
    assert(BuildAdaptivePlan(tiers,lights,5*65536,1,1,2,1,&p,q));
    assert(q.lights[0].resolution == 0 && q.lights[0].bytes == 0 && q.lights[1].resolution != 0);
    assert(BuildAdaptivePlan(tiers,lights,0,1,1,3,1,&q,q) && q.usedBytes == 0);
    const std::array<AdaptiveCandidate,1> one{{{5,1,256,1}}};
    assert(BuildAdaptivePlan(tiers,one,1048576,1,1,1,1,nullptr,p));
    auto changed=one; changed[0].requestedResolution=400;
    assert(BuildAdaptivePlan(tiers,changed,1048576,1,1,2,1,&p,q) && q.lights[0].resolution == 256);
    assert(BuildAdaptivePlan(tiers,changed,1048576,1,2,2,1,&p,q) && q.lights[0].resolution == 512);
    changed[0].requestedResolution=512;
    assert(BuildAdaptivePlan(tiers,changed,1048576,1,1,2,1,&p,q) && q.lights[0].resolution == 512);
    changed[0].requestedResolution=400; changed[0].faces=6;
    assert(BuildAdaptivePlan(tiers,changed,6*1048576,1,1,2,1,&p,q) && q.lights[0].resolution == 512);
    changed=one; changed[0].requestedResolution=190;
    assert(BuildAdaptivePlan(tiers,changed,1048576,1,1,2,1,&p,q) && q.lights[0].resolution == 128);
    changed[0].requestedResolution=195;
    assert(BuildAdaptivePlan(tiers,changed,1048576,1,1,2,1,&p,q) && q.lights[0].resolution == 256);
    const auto saved=q.frame;
    auto duplicate=lights; duplicate[1].id=1;
    assert(!BuildAdaptivePlan(tiers,duplicate,1048576,1,1,3,1,&p,q) && q.frame==saved);
    auto invalid=tiers; invalid[1].bytesPerFace=UINT64_MAX;
    assert(!BuildAdaptivePlan(invalid,lights,UINT64_MAX,1,1,3,1,nullptr,q));
    std::mt19937 rng(431);
    std::vector<AdaptiveCandidate> many;
    for (uint64_t id=1;id<=2048;++id)
        many.push_back({id,double(rng()%1000),double(64+rng()%1000),id%3==0?6u:1u});
    for (uint64_t frame=1;frame<=20;++frame)
    {
        const uint64_t budget=frame*16000000;
        assert(BuildAdaptivePlan(tiers,many,budget,1,1,frame,1,nullptr,p));
        auto reversed=many; std::reverse(reversed.begin(),reversed.end());
        assert(BuildAdaptivePlan(tiers,reversed,budget,1,1,frame,1,nullptr,q));
        assert(p.lights.size()==many.size() && p.usedBytes<=budget && p.usedBytes==q.usedBytes);
        uint64_t sum=0;
        for (size_t i=0;i<many.size();++i)
        {
            const auto& a=p.lights[i]; const auto& b=q.lights[many.size()-1-i];
            assert(a.id==b.id && a.resolution==b.resolution && a.bytes==b.bytes && a.faces==many[i].faces);
            if (a.resolution)
            {
                auto t=std::find_if(tiers.begin(),tiers.end(),[&](auto t){return t.resolution==a.resolution;});
                assert(t!=tiers.end() && a.bytes==t->bytesPerFace*a.faces && a.resolution<=a.requestedTier);
            }
            else assert(!a.bytes);
            sum+=a.bytes;
        }
        assert(sum==p.usedBytes);
    }
    assert(BuildAdaptivePlan(tiers,many,UINT64_MAX,1,1,21,1,nullptr,p));
    for (const auto& a:p.lights) assert(a.resolution==a.requestedTier && a.resolution!=0);
    std::puts("PASS: adaptive shadow budgets, atomic point groups, hysteresis, identity, 2048 lights, deterministic input permutation");
}
