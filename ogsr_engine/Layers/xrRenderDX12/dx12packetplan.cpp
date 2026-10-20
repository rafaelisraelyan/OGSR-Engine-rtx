#include "dx12packetplan.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace renderprep12
{
namespace
{
struct PacketKey12
{
    Resource12 pso;
    Resource12 material;
    Resource12 visual;

    bool operator==(const PacketKey12&) const = default;
};

PacketKey12 KeyOf(const DrawPacket12& packet)
{
    return PacketKey12{packet.object.pso, packet.object.material, packet.object.visual};
}

bool CheckedRangeEnd(uint32_t begin, uint32_t count, uint32_t limit, uint32_t& end)
{
    const uint64_t sum = static_cast<uint64_t>(begin) + static_cast<uint64_t>(count);
    if (sum > limit || sum > std::numeric_limits<uint32_t>::max())
        return false;
    end = static_cast<uint32_t>(sum);
    return true;
}

bool ValidateCategoryRanges(const OrderedFrame12& frame)
{
    const uint64_t packetCount = frame.frame.packets.size();
    if (packetCount > std::numeric_limits<uint32_t>::max())
        return false;

    uint32_t cursor = 0;
    for (const PacketRange12& range : frame.categories)
    {
        uint32_t end = 0;
        if (!CheckedRangeEnd(range.begin, range.count,
                             static_cast<uint32_t>(packetCount), end))
            return false;
        if (range.begin != cursor)
            return false;
        cursor = end;
    }
    return cursor == packetCount;
}

DrawCategory12 CategoryAt(const OrderedFrame12& frame, uint32_t index)
{
    for (uint32_t i = 0; i < frame.categories.size(); ++i)
    {
        const PacketRange12& range = frame.categories[i];
        const uint64_t end = static_cast<uint64_t>(range.begin) + range.count;
        if (index >= range.begin && static_cast<uint64_t>(index) < end)
            return static_cast<DrawCategory12>(i);
    }
    throw std::logic_error("packet category range does not cover index");
}

bool SameCategoryAndKey(const OrderedFrame12& frame,
                        const PacketBatch12& batch)
{
    const PacketKey12 key = KeyOf(frame.frame.packets[batch.firstPacket]);
    const uint64_t end = static_cast<uint64_t>(batch.firstPacket) + batch.packetCount;
    for (uint64_t i = batch.firstPacket; i < end; ++i)
    {
        const DrawPacket12& packet = frame.frame.packets[static_cast<size_t>(i)];
        if (CategoryAt(frame, static_cast<uint32_t>(i)) != batch.category)
            return false;
        if (!(KeyOf(packet) == key))
            return false;
    }
    return true;
}
} // namespace

FrameBatchPlan12 BuildPacketBatchPlan(
    const OrderedFrame12& frame,
    uint32_t maxLists,
    uint32_t maxPacketsPerBatch)
{
    if (maxLists == 0 || maxLists > 8)
        throw std::invalid_argument("maxLists must be in 1..8");
    if (!ValidateCategoryRanges(frame))
        throw std::invalid_argument("invalid packet category ranges");

    const size_t packetCount = frame.frame.packets.size();
    if (packetCount > std::numeric_limits<uint32_t>::max())
        throw std::length_error("packet count exceeds uint32");

    FrameBatchPlan12 plan;
    plan.packetCount = static_cast<uint32_t>(packetCount);
    if (packetCount == 0)
        return plan;

    const uint32_t total = static_cast<uint32_t>(packetCount);
    const uint32_t listCount = (std::min)(maxLists, total);
    plan.lists.reserve(listCount);

    for (uint32_t listIndex = 0; listIndex < listCount; ++listIndex)
    {
        const uint32_t first = static_cast<uint32_t>(
            static_cast<uint64_t>(total) * listIndex / listCount);
        const uint32_t last = static_cast<uint32_t>(
            static_cast<uint64_t>(total) * (listIndex + 1) / listCount);
        if (first >= last)
            throw std::logic_error("empty packet list range");

        const uint32_t firstBatch = static_cast<uint32_t>(plan.batches.size());
        uint32_t batchStart = first;
        uint32_t batchCount = 1;
        DrawCategory12 category = CategoryAt(frame, first);
        PacketKey12 key = KeyOf(frame.frame.packets[first]);

        for (uint32_t index = first + 1; index < last; ++index)
        {
            const DrawCategory12 newCategory = CategoryAt(frame, index);
            const PacketKey12 newKey = KeyOf(frame.frame.packets[index]);
            const bool sizeLimit = maxPacketsPerBatch != 0 &&
                                   batchCount >= maxPacketsPerBatch;
            if (newCategory != category || !(newKey == key) || sizeLimit)
            {
                plan.batches.push_back(
                    PacketBatch12{batchStart, batchCount, category});
                batchStart = index;
                batchCount = 1;
                category = newCategory;
                key = newKey;
            }
            else
            {
                ++batchCount;
            }
        }

        plan.batches.push_back(PacketBatch12{batchStart, batchCount, category});
        plan.lists.push_back(PacketList12{
            firstBatch,
            static_cast<uint32_t>(plan.batches.size()) - firstBatch});
    }

    return plan;
}

BatchPlanValidation12 ValidatePacketBatchPlan(
    const OrderedFrame12& frame,
    const FrameBatchPlan12& plan)
{
    if (!ValidateCategoryRanges(frame))
        return {false, "invalid packet category ranges"};

    const size_t packetCount = frame.frame.packets.size();
    if (packetCount > std::numeric_limits<uint32_t>::max())
        return {false, "packet count exceeds uint32"};
    if (plan.packetCount != packetCount)
        return {false, "plan packetCount mismatch"};
    if (packetCount == 0)
    {
        if (!plan.lists.empty() || !plan.batches.empty())
            return {false, "empty frame produced non-empty plan"};
        return {true, "empty plan"};
    }
    if (plan.lists.empty() || plan.batches.empty())
        return {false, "non-empty frame produced empty plan"};
    if (plan.lists.size() > 8)
        return {false, "plan has more than 8 lists"};

    uint32_t expectedBatch = 0;
    for (const PacketList12& list : plan.lists)
    {
        if (list.batchCount == 0)
            return {false, "empty list"};
        if (list.firstBatch != expectedBatch)
            return {false, "non-contiguous list batch range"};
        const uint64_t end = static_cast<uint64_t>(list.firstBatch) + list.batchCount;
        if (end > plan.batches.size())
            return {false, "list batch range overflow"};
        expectedBatch = static_cast<uint32_t>(end);
    }
    if (expectedBatch != plan.batches.size())
        return {false, "lists do not cover all batches"};

    uint32_t expectedPacket = 0;
    uint32_t previousCategory = 0;
    for (uint32_t batchIndex = 0; batchIndex < plan.batches.size(); ++batchIndex)
    {
        const PacketBatch12& batch = plan.batches[batchIndex];
        if (batch.packetCount == 0)
            return {false, "empty batch"};
        if (batch.firstPacket != expectedPacket)
            return {false, "non-contiguous batch packet range"};
        const uint64_t end =
            static_cast<uint64_t>(batch.firstPacket) + batch.packetCount;
        if (end > packetCount)
            return {false, "batch packet range overflow"};
        if (static_cast<uint32_t>(batch.category) < previousCategory)
            return {false, "category order regression"};
        previousCategory = static_cast<uint32_t>(batch.category);
        if (!SameCategoryAndKey(frame, batch))
            return {false, "batch mixes category or resource key"};
        expectedPacket = static_cast<uint32_t>(end);
    }
    if (expectedPacket != packetCount)
        return {false, "batches do not cover all packets"};
    return {true, "ok"};
}

bool Equivalent(const FrameBatchPlan12& a, const FrameBatchPlan12& b)
{
    return a == b;
}

} // namespace renderprep12
