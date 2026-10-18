#include "CpuTopology.h"

#include <algorithm>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gr {
namespace jobs {

#if defined(_WIN32)

// Counts physical cores from RelationProcessorCore records.
//
// The iteration is bounded by each record's own Size field, not by
// sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX): the header can be larger than
// a real record, which silently drops the final record and under-reports the
// core count. This was a real defect found during F3.2A.1.
static CpuTopologyInfo DetectTopologyWindows()
{
    CpuTopologyInfo info;

    DWORD length = 0;
    const BOOL first = GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
    if (first || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return info;
    }

    std::vector<uint8_t> buffer(length);
    DWORD written = length;
    const BOOL ok = GetLogicalProcessorInformationEx(
        RelationProcessorCore,
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
        &written);
    if (!ok) {
        return info;
    }

    size_t offset = 0;
    while (offset + 8 <= written) {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* record =
            reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (record->Size == 0 || offset + record->Size > written) {
            break;  // malformed or truncated buffer
        }

        if (record->Relationship == RelationProcessorCore) {
            ++info.physicalCores;

            // Every set bit in the group masks is one logical processor of this core.
            for (WORD g = 0; g < record->Processor.GroupCount; ++g) {
                KAFFINITY mask = record->Processor.GroupMask[g].Mask;
                while (mask) {
                    if (mask & 1) {
                        ++info.logicalProcessors;
                    }
                    mask >>= 1;
                }
            }
        }

        offset += record->Size;
    }

    info.detected = (info.physicalCores > 0);
    info.smt = (info.logicalProcessors > info.physicalCores);
    return info;
}

#endif  // _WIN32

CpuTopologyInfo DetectCpuTopology()
{
#if defined(_WIN32)
    return DetectTopologyWindows();
#else
    // No topology query is implemented for this platform yet. Report the logical
    // processor count only, with detected == false so callers use the fallback.
    CpuTopologyInfo info;
    const uint32_t concurrency = GetHardwareConcurrency();
    if (concurrency > 0) {
        info.logicalProcessors = concurrency;
    }
    return info;
#endif
}

uint32_t GetHardwareConcurrency()
{
    const unsigned int concurrency = std::thread::hardware_concurrency();
    return (concurrency > 0) ? static_cast<uint32_t>(concurrency) : 0u;
}

uint32_t ComputeAutoBackgroundWorkers(const CpuTopologyInfo& topology, uint32_t maxWorkers)
{
    if (maxWorkers == 0) {
        return 1;
    }

    // Preferred: physical cores, halved.
    uint32_t workers = 0;
    if (topology.detected && topology.physicalCores > 0) {
        workers = topology.physicalCores / 2;
    } else {
        // Fallback: assume SMT-2 and take a quarter of the logical processors, so
        // an undetected topology still leaves most of the machine free.
        uint32_t logical = topology.logicalProcessors;
        if (logical == 0) {
            logical = GetHardwareConcurrency();
        }
        workers = logical / 4;
    }

    return std::max(1u, std::min(workers, maxWorkers));
}

} // namespace jobs
} // namespace gr
