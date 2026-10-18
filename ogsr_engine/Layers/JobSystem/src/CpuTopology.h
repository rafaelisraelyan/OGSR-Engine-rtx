#ifndef GR_CPU_TOPOLOGY_H
#define GR_CPU_TOPOLOGY_H

#include <cstdint>

namespace gr {
namespace jobs {

// CPU topology of the current process, as reported by the OS.
struct CpuTopologyInfo
{
    // Logical processors available to this process.
    uint32_t logicalProcessors = 0;

    // Physical cores. Counted from RelationProcessorCore only, never derived as
    // logicalProcessors / 2, because SMT and hybrid topologies make that wrong.
    uint32_t physicalCores = 0;

    // True when there are more logical processors than physical cores.
    bool smt = false;

    // True when the topology query succeeded. When false, physicalCores is 0 and
    // callers must fall back to a conservative estimate.
    bool detected = false;
};

// Queries the OS for the CPU topology of the current process.
CpuTopologyInfo DetectCpuTopology();

// std::thread::hardware_concurrency(), or 0 when the value is unknown.
uint32_t GetHardwareConcurrency();

// Applies the documented automatic background worker policy:
//     workers = clamp(physicalCores / 2, 1, maxWorkers)
// with a conservative fallback when topology detection failed.
uint32_t ComputeAutoBackgroundWorkers(const CpuTopologyInfo& topology, uint32_t maxWorkers);

} // namespace jobs
} // namespace gr

#endif // GR_CPU_TOPOLOGY_H
