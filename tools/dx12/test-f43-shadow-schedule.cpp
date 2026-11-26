#include "GameDx12ShadowSchedule.h"
#include "GameDx12ShadowPagePlan.h"

#include <array>
#include <cassert>
#include <cstdio>

int main()
{
    using namespace game_shadow12;
    const std::array<ShadowPage, 2> pages{{{9,{128,24,1}},{5,{256,24,1}}}};
    std::vector<ShadowPlacement> placements;
    for (uint32_t light = 0; light < 8; ++light)
        placements.push_back({8 - light, light & 1 ? 5u : 9u, (light / 2) * 6, 6, light & 1 ? 256u : 128u});
    ShadowAddressPlan addresses;
    assert(BuildShadowAddresses(std::span<const ShadowPage>(pages), std::span<const ShadowPlacement>(placements), addresses));
    std::vector<ShadowPacketRange> packets(48);
    for (uint32_t i = 0; i < packets.size(); ++i)
        packets[i] = {i * 2, 2};
    std::vector<ScheduledShadowView> schedule;
    assert(PlanShadowViews(addresses, packets, 96, schedule) && schedule.size() == 48);
    for (uint32_t i = 0; i < schedule.size(); ++i)
    {
        const auto& view = schedule[i];
        assert(view.projection == i + 4 && view.light == 1 + i / 6 && view.face == i % 6);
        assert(view.page == addresses.pageIds[addresses.views[i].page] && view.slice == addresses.views[i].slice);
        assert(view.packets.first == i * 2 && view.packets.count == 2);
    }
    const auto first = schedule.front().projection;
    const auto rejects = [&](const ShadowAddressPlan& broken, size_t count)
    {
        assert(!PlanShadowViews(broken, packets, count, schedule));
        assert(schedule.size() == 48 && schedule.front().projection == first);
    };
    auto bad = addresses;
    bad.views[2].page ^= 1;
    rejects(bad, 96);
    bad = addresses;
    bad.groups.front().faces = 5;
    rejects(bad, 96);
    bad = addresses;
    bad.views[1].slice = bad.views[0].slice;
    rejects(bad, 96);
    rejects(addresses, 95);
    std::puts("PASS: 48 views, reserved CSM IDs, deterministic packet mapping, atomic point groups and transactional failure");
}
