#include "GameDx12AdaptiveShadowBudget.h"
#include "GameDx12ShadowPagePlan.h"

#include <array>
#include <cassert>
#include <cstdio>

int main()
{
    using namespace game_shadow12;
    constexpr auto roomy = PlanAdaptiveSlotBudget(1000, 700, 100, 200, 30);
    static_assert(roomy.finalBytes == 370 && roomy.newBytes == 270);
    constexpr auto shrink = PlanAdaptiveSlotBudget(650, 700, 100, 200, 0);
    static_assert(shrink.finalBytes == 50 && shrink.newBytes == 0);
    constexpr auto inFlight = PlanAdaptiveSlotBudget(500, 700, 100, 200, 0);
    static_assert(inFlight.finalBytes == 0 && inFlight.newBytes == 0);
    constexpr auto uncertain = PlanAdaptiveSlotBudget(1000, 150, 100, 200, 0);
    static_assert(uncertain.finalBytes == 850 && uncertain.newBytes == 850);
    constexpr auto limited = PlanAdaptiveSlotBudget(1000, 950, 100, 100, 200);
    static_assert(limited.finalBytes == 0 && limited.newBytes == 0);
    constexpr auto wide = PlanAdaptiveSlotBudget(UINT64_MAX, UINT64_MAX - 1, 400, 500, 0);
    static_assert(wide.finalBytes == 401 && wide.newBytes == 1);

    constexpr auto capped = PlanAdaptiveSlotBudget(1000, 500, 100, 200, 100, 400);
    static_assert(capped.finalBytes == 200 && capped.newBytes == 100);
    constexpr auto initial = PlanAdaptiveSlotBudget(1000, 0, 0, 0, 0, 400);
    static_assert(initial.finalBytes == 200);
    constexpr auto noPeak = PlanAdaptiveSlotBudget(2000, 1000, 200, 200, 0, 400);
    static_assert(noPeak.finalBytes == 200 && noPeak.newBytes == 0);

    const std::array<ShadowPageSpec, 2> tiers{{{64,6,60},{128,6,120}}};
    const AdaptivePlan logical{1,1,1,1,1000,0,{{11,6,64,64,60},{22,6,128,128,120},{33,1,64,64,10}}};
    ShadowPagePlan plan;
    assert(BuildShadowPagePlan(logical, tiers, 1000, 0, 0, 1, nullptr, plan, 1));
    assert(plan.next.pages.size() == 1 && plan.next.placed.size() == 1 && plan.denied.size() == 2);
    const std::array<uint64_t, 4> ids{11,22,33,44};
    ShadowAddressPlan addresses;
    assert(BuildShadowAddresses(std::span<const ShadowPage>(plan.next.pages),
        std::span<const ShadowPlacement>(plan.next.placed), addresses));
    std::vector<uint32_t> mapped;
    assert(ReconcileAdaptiveAddresses(ids, addresses, mapped));
    assert((mapped == std::vector<uint32_t>{1,0,0,0}));
    addresses.groups.front().faces = 1;
    assert(!ReconcileAdaptiveAddresses(ids, addresses, mapped));
    assert((mapped == std::vector<uint32_t>{1,0,0,0}));
    assert(ReconcileAdaptiveAddresses(ids, {}, mapped));
    assert((mapped == std::vector<uint32_t>{0,0,0,0}));
    const std::array<AdaptiveTier, 2> quality{{{64,10},{128,20}}};
    const std::array<AdaptiveCandidate, 1> previous{{{1,20,64,6}}};
    AdaptivePlan prior;
    assert(BuildAdaptivePlan(quality, previous, 300, 2, 1, 1, 1, nullptr, prior));
    ShadowPagePlan old;
    assert(BuildShadowPagePlan(prior, tiers, 300, 0, 0, 1, nullptr, old));
    std::array<AdaptiveCandidate, 4> competition{{{1,20,64,6},{2,15,64,6},{90,90,128,1},{99,100,128,1}}};
    AdaptivePlan ranked;
    assert(BuildAdaptivePlan(quality, competition, 300, 2, 1, 2, 1, &prior, ranked));
    for (auto budgets : {std::pair{120ull, UINT32_MAX}, std::pair{300ull, 1u}})
    {
        assert(BuildShadowPagePlan(ranked, tiers, budgets.first, 0, 1, 2, &old.next, plan, budgets.second));
        assert(plan.next.placed.size() == 2 && plan.next.placed[0].light == 99 && plan.next.placed[1].light == 90);
        assert(plan.denied.size() == 2 && plan.retire.size() == 1);
    }
    std::reverse(competition.begin(), competition.end());
    competition[0].importance = 22;
    competition[1].importance = 21;
    assert(BuildAdaptivePlan(quality, competition, 300, 2, 1, 2, 1, &prior, ranked));
    assert(BuildShadowPagePlan(ranked, tiers, 300, 0, 1, 2, &old.next, plan, 1));
    assert(plan.next.placed.size() == 1 && plan.next.placed[0].light == 1 &&
        plan.next.placed[0].faces == 6 && plan.next.placed[0].page == old.next.placed[0].page &&
        plan.next.placed[0].first == old.next.placed[0].first && plan.create.empty());
    std::puts("PASS: physical pressure preserves importance across light types, retention hysteresis and atomic point groups");
    std::puts("PASS: two-slot budget, no double charge, replacement peak, descriptor pressure and physical-denial reconciliation");
}
