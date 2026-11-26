#include "GameDx12LocalShadowAllocator.h"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
using namespace game_shadow12;
static unsigned checks;
static void Require(bool ok)
{
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL at check %u\n", checks); std::exit(1); }
}
static void Validate(std::span<const LocalCandidate> in, uint32_t banks, const LocalAllocation& plan)
{
    uint32_t mask = 0, assigned = 0;
    std::vector<uint8_t> lights(in.size());
    Require(plan.eligibleCandidates.size() == in.size());
    Require(std::count(plan.eligibleCandidates.begin(), plan.eligibleCandidates.end(), 1) == plan.eligible);
    for (uint32_t s = 0; s < MaxLocalSlots; ++s)
    {
        const uint32_t i = plan.candidate[s];
        if (i == NoCandidate) { Require(plan.next.owners[s] == 0); continue; }
        Require(s < banks * SlotsPerBank && i < in.size());
        Require(in[i].eligible && in[i].id && in[i].spot == SpotSlot(s));
        Require(plan.eligibleCandidates[i] != 0);
        Require(plan.next.owners[s] == in[i].id && !lights[i]);
        lights[i] = 1;
        const uint32_t base = SlotBase(s), n = SpotSlot(s) ? 1 : PointFaces;
        Require(ValidBase(base, in[i].spot, banks) && base + n <= banks * LayersPerBank);
        for (uint32_t f = 0; f < n; ++f)
        {
            Require(!(mask & (1u << (base + f))));
            mask |= 1u << (base + f);
            Require(View{MaxSunCascades + base + f}.Kind() ==
                (in[i].spot ? ViewKind::SpotLight : ViewKind::PointLightFace));
        }
        ++assigned;
    }
    Require(assigned == plan.assigned && plan.eligible == assigned + plan.misses);
}
int main()
{
    std::array<LocalCandidate, 16> in{};
    for (uint32_t i = 0; i < in.size(); ++i) in[i] = {i+1, 1., i < 8, false, true};
    for (uint32_t banks = 1; banks <= 3; ++banks)
    {
        auto first = AllocateLocalShadows(in,banks,1,1,{});
        Validate(in,banks,first);
        Require(first.assigned == banks*3 && first.misses == 16-banks*3);
        auto reversed = in;
        std::reverse(reversed.begin(),reversed.end());
        auto second = AllocateLocalShadows(reversed,banks,1,1,first.next);
        Validate(reversed,banks,second);
        Require(first.next.owners == second.next.owners && second.retained == first.assigned);
    }
    std::array<LocalCandidate,3> points{{{1,1.,false,true,true},{2,1.1,false,true,true},{3,.5,false,false,true}}};
    LocalAllocationState previous{1,1,1,{0,0,1}};
    auto held = AllocateLocalShadows(points,1,1,1,previous);
    Require(held.next.owners[2] == 1 && held.retained == 1);
    points[1].score = 1.5;
    Require(AllocateLocalShadows(points,1,1,1,held.next).next.owners[2] == 2);
    points[1].score = 1.1;
    Require(AllocateLocalShadows(points,1,2,1,previous).next.owners[2] == 2);
    Require(AllocateLocalShadows(points,1,1,2,previous).next.owners[2] == 2);
    points[1].id = 1;
    Require(AllocateLocalShadows(points,1,1,1,previous).next.owners[2] == 3);
    points[2].score = std::numeric_limits<double>::quiet_NaN();
    Require(AllocateLocalShadows(points,1,1,1,previous).assigned == 0);
    Require(AllocateLocalShadows(in,0,1,1,{}).assigned == 0);
    Require(AllocateLocalShadows(in,4,1,1,{}).assigned == 0);
    std::vector<LocalCandidate> many(4096);
    for (uint32_t i = 0; i < many.size(); ++i)
        many[i] = {i+1, double(i+1), i%3 != 0, false, true};
    for (uint32_t banks = 1; banks <= 3; ++banks)
    {
        const auto plan = AllocateLocalShadows(many,banks,1,1,{});
        Validate(many,banks,plan);
        Require(plan.eligible == many.size() && plan.assigned == banks*3);
        for (uint32_t s=0; s<banks*SlotsPerBank; ++s) Require(plan.candidate[s] > 31);
        auto reversed=many; std::reverse(reversed.begin(),reversed.end());
        const auto again=AllocateLocalShadows(reversed,banks,1,1,plan.next);
        Validate(reversed,banks,again);
        Require(plan.next.owners==again.next.owners && again.retained==plan.assigned);
    }
    many.back().id=many.front().id;
    const auto ambiguous=AllocateLocalShadows(many,3,1,1,{});
    Validate(many,3,ambiguous);
    Require(ambiguous.eligible==many.size()-2 && !ambiguous.eligibleCandidates.front() && !ambiguous.eligibleCandidates.back());
    std::mt19937 rng(4711);
    LocalAllocationState state{};
    for (uint32_t frame = 0; frame < 10000; ++frame)
    {
        const uint32_t banks = 1 + rng()%3;
        const uint64_t session = 1 + frame/100;
        for (uint32_t i = 0; i < in.size(); ++i)
            in[i] = {i+1, double(rng()%1000)/100., (rng()%2)!=0, (rng()%3)==0, (rng()%4)!=0};
        const bool stable = (rng()%2)!=0;
        const auto plan = AllocateLocalShadows(in,banks,session,1,state,stable);
        Validate(in,banks,plan);
        const auto again = AllocateLocalShadows(in,banks,session,1,state,stable);
        Require(plan.next.owners == again.next.owners && plan.candidate == again.candidate);
        auto shuffled = in;
        std::shuffle(shuffled.begin(),shuffled.end(),rng);
        const auto shuffledPlan = AllocateLocalShadows(shuffled,banks,session,1,state,stable);
        Require(plan.next.owners == shuffledPlan.next.owners);
        if (state.session == session)
            for (uint32_t s = 0; s < banks*SlotsPerBank; ++s)
                if (state.owners[s])
                    for (uint32_t t = 0; t < banks*SlotsPerBank; ++t)
                        if (SpotSlot(s) == SpotSlot(t) && plan.next.owners[t] == state.owners[s])
                            Require(s == t);
        state = plan.next;
    }
    std::printf("PASS: %u assertions, 10000 deterministic allocation frames, 4096 candidates\n",checks);
}
