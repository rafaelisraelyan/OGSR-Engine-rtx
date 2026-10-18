// Private shared state of a JobSystem.
//
// Declared in the public header as an opaque type, defined here. Both the
// JobSystem and every JobGroup it created hold a shared_ptr to this object, so
// a group can safely outlive the JobSystem that produced it: the group keeps
// the backend alive and the backend has already drained every task by the time
// JobSystem::Shutdown returns.
//
// Defined in a private header rather than nested inside JobSystem because a
// friend declaration grants access to private members but does NOT make an
// incomplete nested type usable in another translation unit.
#ifndef GR_JOB_SYSTEM_STATE_H
#define GR_JOB_SYSTEM_STATE_H

#include "gr/jobs/JobConfig.h"
#include "gr/jobs/JobStats.h"

#include "CpuTopology.h"
#include "EnkiBackend.h"

#include <atomic>

namespace gr {
namespace jobs {

struct JobSystemState
{
    JobSystemState()
        : backend()
        , config()
        , topology(DetectCpuTopology())
        , stats()
        , tasksSubmitted(0)
        , tasksCompleted(0)
        , parallelForCalls(0)
        , groupsCreated(0)
        , groupsCompleted(0)
        , jobFailures(0)
    {
        stats.logicalProcessors = topology.logicalProcessors;
        stats.physicalCores = topology.physicalCores;
    }

    EnkiBackend     backend;
    // Закрытое поколение никогда не запускается вновь: старые группы
    // удерживают только уже остановленный планировщик.
    bool           retired = false;
    JobConfig       config;
    CpuTopologyInfo topology;
    JobStats        stats;

    std::atomic<uint64_t> tasksSubmitted;
    std::atomic<uint64_t> tasksCompleted;
    std::atomic<uint64_t> parallelForCalls;
    std::atomic<uint64_t> groupsCreated;
    std::atomic<uint64_t> groupsCompleted;
    std::atomic<uint64_t> jobFailures;

    void addSubmitted(uint64_t n)
    {
        if (config.enableStats) tasksSubmitted.fetch_add(n, std::memory_order_relaxed);
    }

    void addCompleted(uint64_t n)
    {
        if (config.enableStats) tasksCompleted.fetch_add(n, std::memory_order_relaxed);
    }

    void addParallelFor(uint64_t n)
    {
        if (config.enableStats) parallelForCalls.fetch_add(n, std::memory_order_relaxed);
    }

    void addGroupsCreated(uint64_t n)
    {
        if (config.enableStats) groupsCreated.fetch_add(n, std::memory_order_relaxed);
    }

    void addGroupsCompleted(uint64_t n)
    {
        if (config.enableStats) groupsCompleted.fetch_add(n, std::memory_order_relaxed);
    }

    void addJobFailures(uint64_t n)
    {
        if (config.enableStats) jobFailures.fetch_add(n, std::memory_order_relaxed);
    }
};

} // namespace jobs
} // namespace gr

#endif // GR_JOB_SYSTEM_STATE_H
