#ifndef GR_JOBS_CONFIG_H
#define GR_JOBS_CONFIG_H

#include <cstdint>

namespace gr {
namespace jobs {

// Priority of a job or a parallel range. Mapped onto enkiTS task priorities
// internally; the mapping is an implementation detail.
enum class JobPriority : uint8_t {
    High = 0,
    Normal = 1,
    Low = 2,
};

// Configuration passed to JobSystem::Initialize().
//
// The initiating (calling) thread is never counted as a background worker: it
// always participates in execution and is reported separately.
struct JobConfig {
    // Background worker threads to create.
    //   0  -> automatic policy (see below)
    //   1+ -> exactly this many background worker threads
    //
    // Use 1 to force a single worker, or 0 combined with the automatic policy
    // being undesirable. There is no way to create a system with zero
    // participants: the initiating thread is always present.
    //
    // To run fully sequentially (no background workers at all) pass
    // backgroundWorkers = 0 and set automaticWorkers = false.
    uint32_t backgroundWorkers = 0;

    // When true and backgroundWorkers == 0, derive the worker count from the
    // detected CPU topology instead of using the sequential mode.
    bool automaticWorkers = true;

    // Default minimum number of elements per ParallelFor partition (grain size).
    // Must be >= 1. Can be overridden per call.
    uint32_t defaultGrainSize = 1;

    // Enable the lock-free statistics counters. Disable for the lowest
    // possible overhead in hot paths.
    bool enableStats = true;

    // Ceiling applied to the automatic policy, so the job system cannot starve
    // the rest of the engine on many-core machines.
    uint32_t maxAutoBackgroundWorkers = 8;
};

} // namespace jobs
} // namespace gr

#endif // GR_JOBS_CONFIG_H
