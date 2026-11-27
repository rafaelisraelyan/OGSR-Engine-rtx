#include "../../ogsr_engine/xr_3da/GameDx12ShadowContentCache.h"
#include <cassert>
#include <utility>
using namespace game_shadow12;

static ShadowContentView View(uint64_t resource = 1, uint32_t slice = 0)
{
    ShadowContentView v;
    v.resource = resource; v.resourceGeneration = 1; v.session = 1; v.epoch = 1;
    v.slice = slice; v.resolution = 512; v.light = 1; v.lightGeneration = 1;
    v.known = true; v.lightState = {5}; v.raster = {6};
    ShadowContentCaster c;
    c.identity = 7; c.generation = 1; c.known = true;
    c.geometry = {8}; c.material = {9}; c.transform = {10}; c.raster = {11};
    v.casters.push_back(c);
    return v;
}
int main()
{
    ShadowContentCache a, b;
    auto v = View();
    assert(a.Prepare({v}, 1)); assert(a.Reasons()[0] & ShadowContentNew);
    assert(a.CommitSubmitted(1));
    assert(a.Prepare({v}, 2)); assert(a.Stats().hits == 1 && !a.Stats().dirty);
    assert(a.CommitSubmitted(2));
    assert(b.Prepare({v}, 3)); assert(b.Reasons()[0] & ShadowContentNew);
    assert(b.CommitSubmitted(3));
    auto changed = v; changed.casters.push_back(changed.casters[0]);
    changed.casters.back().identity++;
    assert(a.Prepare({changed}, 4)); assert(a.Reasons()[0] & ShadowContentCasters);
    assert(a.CancelUnsubmitted());
    assert(a.Prepare({v}, 5)); assert(a.Stats().hits == 1); assert(a.CommitSubmitted(5));
    assert(a.Prepare({changed}, 6)); assert(a.CommitSubmitted(6));
    assert(a.Prepare({v}, 7)); assert(a.Reasons()[0] & ShadowContentCasters);
    assert(a.CommitSubmitted(7));
    changed = v; changed.light++; assert(a.Prepare({changed}, 8));
    assert(a.Reasons()[0] & ShadowContentLight); assert(a.CommitSubmitted(8));
    assert(b.Prepare({v}, 9)); assert(b.Stats().hits == 1); assert(b.CommitSubmitted(9));
    changed = v; changed.resourceGeneration++; assert(b.Prepare({changed}, 10));
    assert(b.Reasons()[0] & ShadowContentResource); assert(b.CancelUnsubmitted());
    changed = v; changed.matrixBits[0]++; assert(b.Prepare({changed}, 11));
    assert(b.Reasons()[0] & ShadowContentMatrix); assert(b.CancelUnsubmitted());
    changed = v; changed.casters[0].transform[0]++; assert(b.Prepare({changed}, 12));
    assert(b.Reasons()[0] & ShadowContentCasters); assert(b.CancelUnsubmitted());
    changed = v; changed.known = false; assert(b.Prepare({changed}, 13));
    assert(b.Reasons()[0] & ShadowContentUnknown); assert(b.CommitSubmitted(13));
    assert(b.Prepare({changed}, 14)); assert(b.Reasons()[0] & ShadowContentUnknown);
    assert(b.CancelUnsubmitted());
    changed = v; changed.casters[0].known = false; assert(b.Prepare({changed}, 141));
    assert(b.Reasons()[0] & ShadowContentUnknown); assert(b.CancelUnsubmitted());
    assert(!b.Prepare({v, v}, 15));
    assert(b.Prepare({}, 16)); assert(b.CommitSubmitted(16));
    assert(b.Prepare({v}, 17)); assert(b.Reasons()[0] & ShadowContentNew);
    assert(!b.CommitSubmitted(18)); assert(!b.Prepare({v}, 19));
    ShadowContentCache two;
    auto second = View(1, 1); second.light = 2;
    assert(two.Prepare({v, second}, 20)); assert(two.CommitSubmitted(20));
    assert(two.Prepare({v, second}, 21)); assert(two.Stats().hits == 2 && two.Stats().dirty == 0);
    assert(two.CommitSubmitted(21));
    std::swap(v.slice, second.slice);
    assert(two.Prepare({v, second}, 22));
    assert((two.Reasons()[0] & ShadowContentLight) && (two.Reasons()[1] & ShadowContentLight));
    assert(two.CancelUnsubmitted());
    return 0;
}
