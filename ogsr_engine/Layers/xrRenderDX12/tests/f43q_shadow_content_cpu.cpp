#include "../../../xr_3da/GameDx12ShadowContentCache.h"

#include <cstdlib>
#include <iostream>
#include <vector>

using namespace game_shadow12;

static void Check(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

static ShadowContentView View()
{
    ShadowContentView view;
    view.resource = 1; view.resourceGeneration = 2; view.session = 3; view.epoch = 4;
    view.resolution = 256; view.light = 5; view.lightGeneration = 6;
    view.sceneGeneration = 7; view.known = true;
    view.staticCasters = {10, 11};
    ShadowContentCaster caster;
    caster.identity = 8; caster.generation = 9; caster.known = true;
    caster.geometry = {1}; caster.material = {2}; caster.transform = {3, 4};
    view.casters.push_back(caster);
    return view;
}

static uint32_t Prepare(ShadowContentCache& cache, ShadowContentView view, uint64_t fence)
{
    Check(cache.Prepare({std::move(view)}, fence), "Prepare failed");
    return cache.Reasons().at(0);
}

static void Submit(ShadowContentCache& cache, uint64_t fence)
{
    // This CPU test models a successful submission; it does not execute GPU commands.
    Check(cache.Expects(fence) && cache.CommitSubmitted(fence), "Commit failed");
}

int main()
{
    ShadowContentCache slots[2];
    auto original = View();
    for (auto& slot : slots)
    {
        Check(Prepare(slot, original, 1) & ShadowContentNew, "new slot must be dirty");
        Submit(slot, 1);
        Check(Prepare(slot, original, 2) == ShadowContentHit, "unchanged content must hit");
        Submit(slot, 2);
    }
    auto moved = original;
    moved.casters[0].transform[1]++;
    Check(Prepare(slots[0], moved, 3) & ShadowContentCasters, "pose must dirty");
    Check(slots[0].CancelUnsubmitted(), "abort failed");
    Check(Prepare(slots[0], original, 4) == ShadowContentHit, "abort must preserve submitted content");
    Submit(slots[0], 4);
    Check(Prepare(slots[0], moved, 5) & ShadowContentCasters, "moved pose must dirty after abort");
    Submit(slots[0], 5);
    Check(Prepare(slots[0], moved, 6) == ShadowContentHit, "submitted pose must hit");
    Submit(slots[0], 6);
    auto removed = original;
    removed.casters.clear();
    Check(Prepare(slots[0], removed, 7) & ShadowContentCasters, "removed caster must dirty");
    Submit(slots[0], 7);
    auto staticChanged = removed;
    staticChanged.staticCasters.pop_back();
    Check(Prepare(slots[0], staticChanged, 8) & ShadowContentCasters, "static membership must dirty");
    Submit(slots[0], 8);
    auto matrixChanged = staticChanged;
    matrixChanged.matrixBits[0]++;
    Check(Prepare(slots[0], matrixChanged, 9) & ShadowContentMatrix, "projection must dirty");
    Submit(slots[0], 9);
    Check(Prepare(slots[1], original, 3) == ShadowContentHit, "other slot must be independent");
    Submit(slots[1], 3);
    auto reassigned = original;
    reassigned.light++;
    Check(Prepare(slots[1], reassigned, 4) & ShadowContentLight, "page reassignment must dirty");
    Submit(slots[1], 4);
    auto unknown = reassigned;
    unknown.casters[0].known = false;
    Check(Prepare(slots[1], unknown, 5) & ShadowContentUnknown, "unknown pose must be dirty");
    Submit(slots[1], 5);
    Check(Prepare(slots[1], unknown, 6) & ShadowContentUnknown, "unknown cannot become a hit");
    Check(!slots[1].CommitSubmitted(7), "incorrect fence must fail");
    Check(!slots[1].Prepare({original}, 8), "uncertain submission must quarantine");
    ShadowContentCache duplicate;
    Check(!duplicate.Prepare({original, original}, 1), "duplicate physical slice must fail");
    std::cout << "f43q shadow content CPU: PASS\n";
}
