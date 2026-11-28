#include "../GameDx12DetailShadowStreaming.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <tuple>
#include <unordered_map>
#include <vector>

int main()
{
    using namespace gameaux12;
    std::unordered_map<uint32_t, size_t> cache;
    size_t bytes = 0, decodes = 0, observedPeak = 0;
    const auto cost = [](size_t size) { return size; };
    const auto decode = [&](uint32_t)
    {
        ++decodes;
        observedPeak = (std::max)(observedPeak, bytes + 4);
        return size_t{1};
    };
    const auto discard = [](uint32_t) { return false; };
    const std::array<uint32_t, 10> ids{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    for (int frame = 0; frame < 4; ++frame)
    {
        const auto progress = UpdateDetailShadowCache(ids, {10, 4, 14, 256}, cache, bytes, cost, decode, discard);
        assert(!progress.pending && !progress.denied && bytes == 10 && cache.size() == 10);
    }
    assert(decodes == 10 && observedPeak <= 14);

    cache.clear();
    bytes = observedPeak = 0;
    const auto nine = UpdateDetailShadowCache(std::span(ids).first(9), {10, 4, 12, 256},
        cache, bytes, cost, decode, discard);
    assert(!nine.pending && !nine.denied && bytes == 9 && observedPeak <= 12);
    const auto denied = UpdateDetailShadowCache(ids, {10, 4, 12, 256}, cache, bytes, cost, decode, discard);
    assert(denied.denied == 1 && !denied.pending && denied.deniedFrom == 9 && !denied.attempts && bytes == 9);
    const auto restored = UpdateDetailShadowCache(ids, {10, 4, 14, 256}, cache, bytes, cost, decode, discard);
    assert(!restored.pending && !restored.denied && restored.attempts == 1 && bytes == 10);
    const auto shrunk = UpdateDetailShadowCache(ids, {2, 4, 6, 256}, cache, bytes, cost, decode, discard);
    assert(shrunk.denied == 8 && !shrunk.pending && bytes == 2);

    std::vector<uint32_t> requested, masks;
    const auto gather = [&](std::span<const DetailSlotRect> areas, uint32_t width, size_t budget)
    {
        requested.clear();
        masks.clear();
        return VisitDetailShadowSlots(areas, width, [&](uint32_t id, uint32_t mask)
        {
            if (requested.size() == budget)
                return false;
            requested.push_back(id);
            masks.push_back(mask);
            return true;
        });
    };
    const std::array identical{DetailSlotRect{1, 2, 0, 1}, DetailSlotRect{1, 2, 0, 1}};
    assert(gather(identical, 4, 4));
    assert((requested == std::vector<uint32_t>{1, 2, 5, 6}) &&
        (masks == std::vector<uint32_t>{3, 3, 3, 3}));
    const std::array overlap{DetailSlotRect{0, 2, 0, 0}, DetailSlotRect{2, 3, 0, 0}};
    assert(gather(overlap, 4, 4));
    assert((requested == std::vector<uint32_t>{0, 1, 2, 3}) &&
        (masks == std::vector<uint32_t>{1, 1, 3, 2}));
    assert(!gather(overlap, 4, 3));
    const std::array disjoint{DetailSlotRect{1, 2, 0, 0}, DetailSlotRect{1, 2, 2, 2}};
    assert(gather(disjoint, 4, 4));
    assert((requested == std::vector<uint32_t>{1, 2, 9, 10}) &&
        (masks == std::vector<uint32_t>{1, 1, 2, 2}));

    cache.clear();
    bytes = decodes = 0;
    const std::array initial{DetailSlotRect{0, 99, 0, 0}, DetailSlotRect{80, 299, 0, 0}};
    const std::array shifted{DetailSlotRect{1, 100, 0, 0}, DetailSlotRect{80, 559, 0, 0}};
    assert(gather(initial, 600, 600));
    const auto process = [&](auto retain)
    {
        std::array<DetailShadowCascadeEvidence, 2> evidence{};
        const auto update = UpdateDetailShadowCache(requested, {580, 4, 584, 256},
            cache, bytes, cost, decode, retain);
        const auto complete = MeasureDetailShadowCoverage(requested, masks, cache, update.deniedFrom, evidence);
        return std::tuple{update, complete, evidence};
    };
    auto [first, firstMask, firstEvidence] = process(discard);
    assert(first.pending == 44 && firstMask == 1 && firstEvidence[1].pendingSlots == 44);
    auto [full, fullMask, fullEvidence] = process(discard);
    assert(!full.pending && fullMask == 3 && bytes == 300);

    assert(gather(shifted, 600, 600));
    const auto keepMargin = [](uint32_t id) { return id == 0; };
    auto [moved, movedMask, movedEvidence] = process(keepMargin);
    assert(moved.pending == 4 && moved.retained == 1 && movedMask == 1 &&
        movedEvidence[0].pendingSlots == 0 && movedEvidence[1].pendingSlots == 4);
    auto [recovered, recoveredMask, recoveredEvidence] = process(keepMargin);
    assert(!recovered.pending && recoveredMask == 3 && bytes == 560 && cache.contains(0) && cache.contains(559));
    auto [evicted, evictedMask, evictedEvidence] = process(discard);
    assert(evictedMask == 3 && evicted.attempts == 0 && !cache.contains(0));

    // Смена поколения сбрасывает резидентность, включая зону пересечения.
    cache.clear();
    bytes = 0;
    auto [changed, changedMask, changedEvidence] = process(keepMargin);
    assert(changed.pending == 303 && changedMask == 1 && !cache.contains(559));
    std::puts("PASS: detail streaming, peak admission, unique coverage and two-area transition");
}
