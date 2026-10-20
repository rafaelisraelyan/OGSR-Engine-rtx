#pragma once

// F3.5.5B: deterministic CPU-only batch planner for ordered draw packets.
// The plan owns only indices/ranges; OrderedFrame12 remains the owner of the
// packets. This header has no D3D12, GPU registry or JobSystem dependency.

#include "dx12renderprep.h"

#include <cstdint>
#include <vector>

namespace renderprep12
{

struct PacketBatch12
{
    uint32_t firstPacket = 0;
    uint32_t packetCount = 0;
    DrawCategory12 category = DrawCategory12::Opaque;

    bool operator==(const PacketBatch12&) const = default;
};

struct PacketList12
{
    uint32_t firstBatch = 0;
    uint32_t batchCount = 0;

    bool operator==(const PacketList12&) const = default;
};

struct FrameBatchPlan12
{
    std::vector<PacketBatch12> batches;
    std::vector<PacketList12> lists;
    uint32_t packetCount = 0;

    bool operator==(const FrameBatchPlan12&) const = default;
};

struct BatchPlanValidation12
{
    bool ok = false;
    const char* reason = "unvalidated";
};

// Throws std::invalid_argument for invalid configuration or category ranges,
// std::length_error for packet-count overflow. Allocation failure propagates;
// no partial plan is returned.
FrameBatchPlan12 BuildPacketBatchPlan(
    const OrderedFrame12& frame,
    uint32_t maxLists,
    uint32_t maxPacketsPerBatch);

// Checks coverage, gaps/overlap, list/batch bounds, category order and stable
// resource keys inside each batch. Never mutates the frame.
BatchPlanValidation12 ValidatePacketBatchPlan(
    const OrderedFrame12& frame,
    const FrameBatchPlan12& plan);

bool Equivalent(const FrameBatchPlan12& a, const FrameBatchPlan12& b);

// CPU-only self-tests; returns false on the first failed invariant.
bool RunPacketBatchPlanTests();

} // namespace renderprep12
