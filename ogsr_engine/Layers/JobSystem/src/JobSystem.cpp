#include "gr/jobs/JobSystem.h"
#include "gr/jobs/JobGroup.h"

#include "InternalJobTask.h"
#include "JobDiagnostics.h"
#include "JobSystemState.h"

#include <atomic>
#include <exception>
#include <stdexcept>

namespace gr {
namespace jobs {

// ---------------------------------------------------------------------------
// Unobserved-failure diagnostics
//
// Process-wide, not per-system: this is a safety signal, and a discarded job
// failure must be visible no matter which system it belonged to.
// ---------------------------------------------------------------------------
namespace {

std::atomic<UnobservedJobErrorHook> g_unobservedHook(nullptr);
std::atomic<uint64_t>              g_unobservedFailures(0);
thread_local bool                 g_insideErrorHook = false;

} // namespace

void ReportUnobservedJobFailure(const char* context, std::exception_ptr error) noexcept
{
    g_unobservedFailures.fetch_add(1, std::memory_order_relaxed);

    UnobservedJobErrorHook hook = g_unobservedHook.load(std::memory_order_acquire);
    if (hook && !g_insideErrorHook) {
        // The hook runs on the thread that destroyed the group. It must not
        // throw: ReportUnobservedJobFailure is noexcept.
        g_insideErrorHook = true;
        try {
            // Сообщение не требует выделения памяти. Время жизни what()
            // ограничено вызовом hook; рекурсивная диагностика лишь считает.
            if (error) {
                try {
                    std::rethrow_exception(error);
                } catch (const std::exception& e) {
                    hook(context, e.what());
                } catch (...) {
                    hook(context, "<non-standard exception>");
                }
            } else {
                hook(context, "");
            }
        } catch (...) {
            // A throwing hook is a programming error; absorb it so the
            // noexcept contract holds. The counter above still records the
            // failure.
        }
        g_insideErrorHook = false;
    }
}

void SetUnobservedJobErrorHook(UnobservedJobErrorHook hook)
{
    g_unobservedHook.store(hook, std::memory_order_release);
}

uint64_t GetUnobservedJobErrorCount()
{
    return g_unobservedFailures.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// JobSystem
// ---------------------------------------------------------------------------
JobSystem::JobSystem()
    : m_state(new JobSystemState())
{
}

JobSystem::~JobSystem()
{
    Shutdown();
    // m_state released here. Any JobGroup still holding a shared_ptr keeps the
    // state alive, and that state's backend has already drained every task.
}

bool JobSystem::Initialize(const JobConfig& config)
{
    if (InternalJobTask::IsExecutingCallback()) {
        return false;
    }
    if (!m_state) {
        return false;
    }
    if (m_state->backend.IsInitialized()) {
        // Initialising an already-running system would discard the current
        // configuration and any outstanding work. Call Shutdown() first.
        return false;
    }

    if (config.defaultGrainSize == 0 || config.maxAutoBackgroundWorkers == 0) {
        return false;
    }

    // Старые группы сохраняют закрытое состояние и не могут отправлять
    // задачи в новый пул. Присваиваем m_state только после успешного запуска.
    std::shared_ptr<JobSystemState> next = m_state;
    if (next->retired) {
        next = std::make_shared<JobSystemState>();
    }
    JobSystemState& state = *next;

    uint32_t backgroundWorkers = config.backgroundWorkers;
    if (backgroundWorkers == 0) {
        if (config.automaticWorkers) {
            backgroundWorkers = ComputeAutoBackgroundWorkers(state.topology,
                                                             config.maxAutoBackgroundWorkers);
        }
        // else: sequential mode, no background workers at all.
    }

    state.config = config;

    if (!state.backend.Initialize(backgroundWorkers, config.enableStats)) {
        return false;
    }

    state.stats.backgroundWorkers = state.backend.GetBackgroundWorkerCount();
    state.stats.totalParticipants = state.backend.GetTotalParticipantCount();
    m_state = std::move(next);
    return true;
}

void JobSystem::Shutdown()
{
    if (!m_state) {
        return;
    }
    JobSystemState& state = *m_state;
    if (!state.backend.IsInitialized()) {
        return;
    }

    // WaitforAllAndShutdown drains every submitted task — including tasks owned
    // by JobGroups that are still alive — before the worker threads stop. That
    // is what makes destroying a group after Shutdown() safe.
    state.backend.Shutdown();
    state.retired = true;

    state.stats.backgroundWorkers = 0;
    state.stats.totalParticipants = 0;
}

bool JobSystem::IsInitialized() const
{
    return m_state && m_state->backend.IsInitialized();
}

JobGroup JobSystem::CreateGroup()
{
    if (!m_state || !m_state->backend.IsInitialized()) {
        return JobGroup();  // invalid group; operations on it are no-ops
    }
    m_state->addGroupsCreated(1);
    return JobGroup(m_state);
}

void JobSystem::ParallelFor(uint32_t count, const RangeCallback& range, JobPriority priority)
{
    ParallelFor(count, m_state ? m_state->config.defaultGrainSize : 1u, range, priority);
}

void JobSystem::ParallelFor(uint32_t count, uint32_t grainSize, const RangeCallback& range,
                            JobPriority priority)
{
    if (count == 0 || !range || !m_state) {
        return;
    }
    JobSystemState& state = *m_state;
    if (!state.backend.IsInitialized()) {
        return;
    }

    // The error slot is shared by every partition of this call, so a failure in
    // any partition is reported to the caller after the call completes.
    std::shared_ptr<JobErrorSlot> errors(new JobErrorSlot());
    std::unique_ptr<InternalJobTask> task(
        new InternalJobTask(count, grainSize, InternalJobTask::MakeRangeFunction(range),
                            errors, priority));

    if (!state.backend.Submit(task.get())) {
        throw std::runtime_error("GR Job System: task submission rejected");
    }
    state.addSubmitted(1);

    // Synchronous: wait before returning so that callbacks may capture by
    // reference to the caller's stack.
    state.backend.Wait(task.get());

    if (task->GetIsComplete()) {
        state.addCompleted(1);
    }

    const uint32_t failures = errors->FailureCount();
    if (failures > 0) {
        state.addJobFailures(failures);
    }
    state.addParallelFor(1);

    // Rethrow after the counters are updated and before the task is destroyed.
    errors->RethrowFirst();
}

uint32_t JobSystem::GetBackgroundWorkerCount() const
{
    return m_state ? m_state->backend.GetBackgroundWorkerCount() : 0u;
}

uint32_t JobSystem::GetTotalParticipantCount() const
{
    return m_state ? m_state->backend.GetTotalParticipantCount() : 0u;
}

uint32_t JobSystem::GetLogicalProcessorCount() const
{
    return m_state ? m_state->topology.logicalProcessors : 0u;
}

uint32_t JobSystem::GetPhysicalCoreCount() const
{
    return m_state ? m_state->topology.physicalCores : 0u;
}

JobStats JobSystem::GetStats() const
{
    JobStats snapshot;
    if (!m_state) {
        return snapshot;
    }
    const JobSystemState& state = *m_state;

    snapshot.backgroundWorkers = state.stats.backgroundWorkers;
    snapshot.totalParticipants = state.stats.totalParticipants;
    snapshot.logicalProcessors = state.stats.logicalProcessors;
    snapshot.physicalCores = state.stats.physicalCores;
    snapshot.unobservedFailures = g_unobservedFailures.load(std::memory_order_relaxed);

    if (state.config.enableStats) {
        snapshot.tasksSubmitted = state.tasksSubmitted.load(std::memory_order_relaxed);
        snapshot.tasksCompleted = state.tasksCompleted.load(std::memory_order_relaxed);
        snapshot.parallelForCalls = state.parallelForCalls.load(std::memory_order_relaxed);
        snapshot.groupsCreated = state.groupsCreated.load(std::memory_order_relaxed);
        snapshot.groupsCompleted = state.groupsCompleted.load(std::memory_order_relaxed);
        snapshot.jobFailures = state.jobFailures.load(std::memory_order_relaxed);
    }
    return snapshot;
}

uint64_t JobSystem::GetUnobservedFailureCount() const
{
    return g_unobservedFailures.load(std::memory_order_relaxed);
}

} // namespace jobs
} // namespace gr
