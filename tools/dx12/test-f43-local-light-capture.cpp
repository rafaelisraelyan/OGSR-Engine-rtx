#include "GameDx12Lighting.h"
#include <cassert>
#include <cstdio>
#include <random>
#include <utility>

using namespace gamelight12;
int main()
{
    FrameSnapshot snapshot;
    assert(FinalizeLocalLightOrder(snapshot) && snapshot.localCount==0);
    assert(FindLocalLightSlot(snapshot,1)==-1);
    std::mt19937 rng(431);
    for (uint64_t i=1;i<=8192;++i)
    {
        LocalLight light;
        light.id=2*i;
        light.selectionScore=double(rng()%1000);
        light.range=float(i);
        light.castsShadow=i%2!=0;
        snapshot.localLights.push_back(light);
    }
    std::shuffle(snapshot.localLights.begin(),snapshot.localLights.end(),rng);
    assert(FinalizeLocalLightOrder(snapshot) && snapshot.localCount==8192);
    for (uint32_t i=0;i<snapshot.localCount;++i)
    {
        const auto& light=snapshot.localLights[i];
        assert(light.id==2ull*(i+1) && light.range==float(i+1));
        assert(light.castsShadow==(i%2==0));
        assert(FindLocalLightSlot(snapshot,light.id)==static_cast<int32_t>(i));
        assert(FindLocalLightSlot(snapshot,light.id-1)==-1);
    }
    assert(FindLocalLightSlot(snapshot,UINT64_MAX)==-1);
    const auto ordered=snapshot;
    snapshot.localLights.erase(snapshot.localLights.begin()+20);
    assert(FinalizeLocalLightOrder(snapshot) && snapshot.localCount==8191);
    assert(FindLocalLightSlot(snapshot,42)==-1 && FindLocalLightSlot(snapshot,44)==20);
    assert(FindLocalLightSlot(ordered,44)==21);
    snapshot.localLights.push_back(snapshot.localLights.back());
    assert(!FinalizeLocalLightOrder(snapshot));
    snapshot=ordered;
    snapshot.localLights[100].id=0;
    assert(!FinalizeLocalLightOrder(snapshot));
    snapshot=ordered;
    snapshot.localLights.clear();
    snapshot.localLights.push_back(ordered.localLights.back());
    assert(FinalizeLocalLightOrder(snapshot) && snapshot.localCount==1);
    assert(FindLocalLightSlot(snapshot,16384)==0 && FindLocalLightSlot(snapshot,2)==-1);
    std::puts("PASS: 8192 captured records, ID ordering, high indices, visibility change, diagnostic replacement and invalid IDs");
}
