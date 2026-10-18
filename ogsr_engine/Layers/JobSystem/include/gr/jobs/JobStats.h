#ifndef GR_JOBS_STATS_H
#define GR_JOBS_STATS_H

#include <cstdint>

namespace gr {
namespace jobs {

// Diagnostic counters for a JobSystem.
//
// All counters are lock-free atomics and are safe to read while the system is
// running. They are updated only when JobConfig::enableStats is true, except
// unobservedFailures, which is always maintained.
struct JobStats {
    // Configured background worker threads (0 in sequential mode).
    uint32_t backgroundWorkers = 0;

    // backgroundWorkers + 1 (the initiating thread).
    uint32_t totalParticipants = 0;

    // Logical processors available to this process.
    uint32_t logicalProcessors = 0;

    // Physical cores detected. 0 when topology detection was unavailable.
    uint32_t physicalCores = 0;

    // Jobs handed to the backend.
    uint64_t tasksSubmitted = 0;

    // Jobs that returned normally or via a captured exception.
    uint64_t tasksCompleted = 0;

    // ParallelFor invocations (system-level synchronous calls).
    uint64_t parallelForCalls = 0;

    // Groups created.
    uint64_t groupsCreated = 0;

    // Groups whose Wait() returned.
    uint64_t groupsCompleted = 0;

    // Jobs whose callback threw. The exception is captured and rethrown on the
    // waiting thread; it never escapes a worker thread.
    uint64_t jobFailures = 0;

    // Job failures that were DISCARDED because the owning group was destroyed
    // without an explicit Wait(). The destructor cannot throw, so these are
    // reported through SetUnobservedJobErrorHook instead. Always maintained,
    // even when enableStats is false: this is the evidence that no failure was
    // silently lost.
    uint64_t unobservedFailures = 0;
};

} // namespace jobs
} // namespace gr

#endif // GR_JOBS_STATS_H
