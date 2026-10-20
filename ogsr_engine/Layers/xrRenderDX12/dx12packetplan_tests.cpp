#include "dx12packetplan.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

namespace renderprep12
{
namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

Object12 MakeObject(uint32_t id, uint32_t keyIndex)
{
    Object12 object{};
    object.id = id;
    object.firstVertex = (id % 2u) * 36u;
    object.vertexCount = 36;
    object.pso = SyntheticResources12[keyIndex % SyntheticResources12.size()];
    object.material = SyntheticResources12[(keyIndex + 1u) % SyntheticResources12.size()];
    object.visual = SyntheticResources12[(keyIndex + 2u) % SyntheticResources12.size()];
    return object;
}

OrderedFrame12 MakeFrame(uint32_t packetCount, uint32_t opaque,
                         uint32_t transparent, uint32_t hud,
                         uint32_t keyRun = 1)
{
    if (opaque + transparent > packetCount ||
        hud != packetCount - opaque - transparent)
        throw std::invalid_argument("invalid test category counts");
    const uint32_t hudCount = hud;

    OrderedFrame12 frame{};
    frame.frame.packets.resize(packetCount);
    for (uint32_t i = 0; i < packetCount; ++i)
    {
        DrawPacket12 packet{};
        packet.object = MakeObject(i + 1, keyRun ? i / keyRun : 0);
        packet.visible = true;
        frame.frame.packets[i] = packet;
    }
    frame.frame.generation = 1;
    frame.categories[0] = PacketRange12{0, opaque};
    frame.categories[1] = PacketRange12{opaque, transparent};
    frame.categories[2] = PacketRange12{opaque + transparent, hudCount};
    return frame;
}

void CheckPlan(const OrderedFrame12& frame, const FrameBatchPlan12& plan,
               uint32_t maxLists, uint32_t maxPacketsPerBatch)
{
    const BatchPlanValidation12 validation = ValidatePacketBatchPlan(frame, plan);
    Check(validation.ok, validation.reason);
    Check(plan.packetCount == frame.frame.packets.size(), "packetCount");
    Check(plan.lists.size() == (std::min)(maxLists,
          static_cast<uint32_t>(frame.frame.packets.size())), "list count");

    std::vector<uint32_t> flattened;
    flattened.reserve(frame.frame.packets.size());
    for (const PacketList12& list : plan.lists)
    {
        Check(list.batchCount != 0, "empty list");
        for (uint32_t b = 0; b < list.batchCount; ++b)
        {
            const PacketBatch12& batch = plan.batches[list.firstBatch + b];
            Check(batch.packetCount != 0, "empty batch");
            if (maxPacketsPerBatch != 0)
                Check(batch.packetCount <= maxPacketsPerBatch, "batch size limit");
            for (uint32_t p = 0; p < batch.packetCount; ++p)
                flattened.push_back(batch.firstPacket + p);
        }
    }
    Check(flattened.size() == frame.frame.packets.size(), "coverage size");
    for (uint32_t i = 0; i < flattened.size(); ++i)
        Check(flattened[i] == i, "coverage/order");
}

void TestBuildMatrix()
{
    const uint32_t sizes[] = {0, 1, 2, 7, 8, 9, 17, 256, 4097};
    const uint32_t lists[] = {1, 2, 4, 8};
    const uint32_t batchLimits[] = {0, 1, 2, 64, 1024};
    for (uint32_t n : sizes)
    {
        const uint32_t categorySets[][3] = {
            {n, 0, 0},
            {0, n, 0},
            {0, 0, n},
            {n / 2, n - n / 2, 0},
            {n / 3, n / 3, n - 2 * (n / 3)},
            {n / 2, 0, n - n / 2}
        };
        for (const auto& categories : categorySets)
        {
            const OrderedFrame12 frame = MakeFrame(
                n, categories[0], categories[1], categories[2],
                (n > 17) ? 4u : 1u);
            for (uint32_t maxLists : lists)
            {
                for (uint32_t limit : batchLimits)
                {
                    const FrameBatchPlan12 plan =
                        BuildPacketBatchPlan(frame, maxLists, limit);
                    CheckPlan(frame, plan, maxLists, limit);
                    const FrameBatchPlan12 repeated =
                        BuildPacketBatchPlan(frame, maxLists, limit);
                    Check(Equivalent(plan, repeated), "determinism");
                }
            }
        }
    }
}

void TestDeterminism()
{
    const OrderedFrame12 frame = MakeFrame(4097, 1500, 1597, 1000, 7);
    for (uint32_t i = 0; i < 100; ++i)
    {
        const FrameBatchPlan12 plan = BuildPacketBatchPlan(frame, 8, 64);
        const FrameBatchPlan12 again = BuildPacketBatchPlan(frame, 8, 64);
        Check(Equivalent(plan, again), "100-repeat determinism");
        Check(ValidatePacketBatchPlan(frame, plan).ok, "determinism validation");
    }
}

void TestResourceKeyTransitions()
{
    const OrderedFrame12 frame = MakeFrame(32, 32, 0, 0, 2);
    const FrameBatchPlan12 plan = BuildPacketBatchPlan(frame, 8, 0);
    Check(ValidatePacketBatchPlan(frame, plan).ok, "resource key validation");
    Check(plan.batches.size() > 1, "resource key transitions must split batches");
    for (const PacketBatch12& batch : plan.batches)
        Check(batch.packetCount == 2, "resource key batch width");
    bool sawMultipleBatchesPerList = false;
    for (const PacketList12& list : plan.lists)
        if (list.batchCount > 1)
            sawMultipleBatchesPerList = true;
    Check(sawMultipleBatchesPerList, "multiple batches per list");
}

void TestCorruptedPlans()
{
    const OrderedFrame12 frame = MakeFrame(8, 8, 0, 0, 8);
    const FrameBatchPlan12 base = BuildPacketBatchPlan(frame, 2, 2);
    Check(ValidatePacketBatchPlan(frame, base).ok, "base plan");

    auto expectInvalid = [&](const FrameBatchPlan12& plan, const char* what) {
        const BatchPlanValidation12 result = ValidatePacketBatchPlan(frame, plan);
        Check(!result.ok, what);
    };

    FrameBatchPlan12 gap = base;
    gap.batches[1].firstPacket += 1;
    expectInvalid(gap, "gap must be rejected");

    FrameBatchPlan12 overlap = base;
    overlap.batches[1].firstPacket = base.batches[0].firstPacket + 1;
    expectInvalid(overlap, "overlap must be rejected");

    FrameBatchPlan12 invalidIndex = base;
    invalidIndex.batches.back().firstPacket = 9;
    expectInvalid(invalidIndex, "invalid packet index must be rejected");

    FrameBatchPlan12 wrongCount = base;
    wrongCount.packetCount += 1;
    expectInvalid(wrongCount, "packetCount mismatch must be rejected");

    FrameBatchPlan12 wrongCategory = base;
    wrongCategory.batches[0].category = DrawCategory12::Transparent;
    expectInvalid(wrongCategory, "wrong category must be rejected");

    FrameBatchPlan12 listGap = base;
    listGap.lists[1].firstBatch += 1;
    expectInvalid(listGap, "list gap must be rejected");

    FrameBatchPlan12 emptyList = base;
    emptyList.lists[0].batchCount = 0;
    expectInvalid(emptyList, "empty list must be rejected");

    FrameBatchPlan12 emptyBatch = base;
    emptyBatch.batches[0].packetCount = 0;
    expectInvalid(emptyBatch, "empty batch must be rejected");

    FrameBatchPlan12 listOverflow = base;
    listOverflow.lists[1].batchCount = 100;
    expectInvalid(listOverflow, "list range overflow must be rejected");

    FrameBatchPlan12 missingBatch = base;
    missingBatch.batches.pop_back();
    expectInvalid(missingBatch, "missing batch coverage must be rejected");
}

void TestInvalidInputs()
{
    const OrderedFrame12 frame = MakeFrame(8, 4, 4, 0, 1);
    bool rejected = false;
    try { BuildPacketBatchPlan(frame, 0, 0); }
    catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "maxLists=0 rejected");

    rejected = false;
    try { BuildPacketBatchPlan(frame, 9, 0); }
    catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "maxLists>8 rejected");

    OrderedFrame12 gap = frame;
    gap.categories[1].begin += 1;
    rejected = false;
    try { BuildPacketBatchPlan(gap, 8, 0); }
    catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "category gap rejected");

    OrderedFrame12 overflow = frame;
    overflow.categories[2].begin = std::numeric_limits<uint32_t>::max();
    overflow.categories[2].count = 2;
    rejected = false;
    try { BuildPacketBatchPlan(overflow, 8, 0); }
    catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "category overflow rejected");

    FrameBatchPlan12 badPlan;
    badPlan.packetCount = std::numeric_limits<uint32_t>::max();
    Check(!ValidatePacketBatchPlan(frame, badPlan).ok, "plan overflow rejected");
}

void TestImmutability()
{
    const OrderedFrame12 frame = MakeFrame(64, 32, 16, 16, 3);
    std::vector<uint64_t> ids;
    std::vector<Resource12> pso;
    for (const DrawPacket12& packet : frame.frame.packets)
    {
        ids.push_back(packet.object.id);
        pso.push_back(packet.object.pso);
    }
    const PacketRange12 categories[3] = {
        frame.categories[0], frame.categories[1], frame.categories[2]};
    for (uint32_t lists : {1u, 4u, 8u})
        (void)BuildPacketBatchPlan(frame, lists, 7);

    Check(frame.frame.packets.size() == ids.size(), "immutability size");
    for (size_t i = 0; i < ids.size(); ++i)
    {
        Check(frame.frame.packets[i].object.id == ids[i], "immutability id");
        Check(frame.frame.packets[i].object.pso == pso[i], "immutability pso");
    }
    for (uint32_t i = 0; i < 3; ++i)
    {
        Check(frame.categories[i].begin == categories[i].begin, "immutability category begin");
        Check(frame.categories[i].count == categories[i].count, "immutability category count");
    }
}

void TestSingleVersusMultiList()
{
    const OrderedFrame12 frame = MakeFrame(4097, 1400, 1497, 1200, 5);
    const FrameBatchPlan12 single = BuildPacketBatchPlan(frame, 1, 0);
    const FrameBatchPlan12 multi = BuildPacketBatchPlan(frame, 8, 0);
    Check(ValidatePacketBatchPlan(frame, single).ok, "single plan validation");
    Check(ValidatePacketBatchPlan(frame, multi).ok, "multi plan validation");
    Check(single.lists.size() == 1, "single list count");
    Check(multi.lists.size() == 8, "multi list count");

    std::vector<uint32_t> singleOrder;
    std::vector<uint32_t> multiOrder;
    for (const PacketList12& list : single.lists)
        for (uint32_t b = 0; b < list.batchCount; ++b)
        {
            const PacketBatch12& batch = single.batches[list.firstBatch + b];
            for (uint32_t p = 0; p < batch.packetCount; ++p)
                singleOrder.push_back(batch.firstPacket + p);
        }
    for (const PacketList12& list : multi.lists)
        for (uint32_t b = 0; b < list.batchCount; ++b)
        {
            const PacketBatch12& batch = multi.batches[list.firstBatch + b];
            for (uint32_t p = 0; p < batch.packetCount; ++p)
                multiOrder.push_back(batch.firstPacket + p);
        }
    Check(singleOrder == multiOrder, "single/multi packet sequence");
}

void TestEmptyPlan()
{
    const OrderedFrame12 frame = MakeFrame(0, 0, 0, 0, 1);
    for (uint32_t lists : {1u, 2u, 4u, 8u})
    {
        const FrameBatchPlan12 plan = BuildPacketBatchPlan(frame, lists, 0);
        Check(plan.packetCount == 0, "empty packetCount");
        Check(plan.lists.empty() && plan.batches.empty(), "empty plan vectors");
        Check(ValidatePacketBatchPlan(frame, plan).ok, "empty plan validation");
    }
}
} // namespace

bool RunPacketBatchPlanTests()
{
    try
    {
        TestBuildMatrix();
        TestDeterminism();
        TestResourceKeyTransitions();
        TestInvalidInputs();
        TestCorruptedPlans();
        TestImmutability();
        TestSingleVersusMultiList();
        TestEmptyPlan();
        std::printf("[packetplan] PASS: deterministic CPU batch planner\n");
        return true;
    }
    catch (const std::exception& error)
    {
        std::printf("[packetplan] FAIL: %s\n", error.what());
        return false;
    }
    catch (...)
    {
        std::printf("[packetplan] FAIL: unknown exception\n");
        return false;
    }
}

} // namespace renderprep12
