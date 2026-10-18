#ifndef GR_JOBS_GROUP_H
#define GR_JOBS_GROUP_H

#include "gr/jobs/JobSystem.h"
#include "gr/jobs/JobTypes.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>

namespace gr {
namespace jobs {

// RangeCallback and JobCallback are declared in gr/jobs/JobTypes.h.

// A set of jobs that can be waited on as a unit.
//
// LIFETIME CONTRACT
//   The callback, and every object it captures by reference, must remain valid
//   until Wait() (or DrainNoThrow()) has returned. Callbacks may capture by
//   value freely.
//
//   A group holds a shared reference to the system's state, so it may outlive
//   the JobSystem that created it. Submitting new work after the system is gone
//   is a no-op; draining an already-drained group is safe.
//
// ПОТОКИ И ВЛАДЕНИЕ
//   Одна группа принадлежит одному потоку, зарегистрированному в scheduler
//   (потоку Initialize либо GR-задаче). Операции одной группы не перекрываются:
//   нельзя вызывать Submit/Wait/move/деструктор реентерабельно из её callback.
//   Вложенные задачи используют отдельную группу, а не ожидающую их родительскую.
//   После Shutdown отправка через старую группу — no-op, включая новый запуск
//   той же системы. IsValid означает привязку к состоянию, а не работающий пул.
//   Ошибка выделения памяти при регистрации выбрасывается до публикации.
//   Отказ backend до публикации выбрасывает runtime_error; задача не выполняется.
//   Wait и деструктор удерживают адреса всех принятых задач до их завершения.
//
// DESTRUCTION — the important part
//   ~JobGroup() is noexcept and never throws. It drains any pending work, so a
//   group can never destroy a job that is still running.
//
//   If a job in the group threw and the user did NOT call Wait() explicitly,
//   the destructor cannot rethrow: a throwing destructor calls std::terminate.
//   Instead the failure is reported through SetUnobservedJobErrorHook and
//   GetUnobservedJobErrorCount, and counted in JobStats::unobservedFailures.
//   The failure is never silently dropped.
//
//   Call Wait() explicitly when the caller needs to react to a job failure.
class JobGroup {
public:
    // Creates an empty group that is not bound to any system.
    // IsValid() returns false and every operation on it is a no-op.
    JobGroup();

    // Drains pending work. Never throws.
    ~JobGroup();

    JobGroup(JobGroup&& other) noexcept;
    JobGroup& operator=(JobGroup&& other) noexcept;
    JobGroup(const JobGroup&) = delete;
    JobGroup& operator=(const JobGroup&) = delete;

    // True when this group is bound to a system created by JobSystem::CreateGroup().
    bool IsValid() const;

    // Adds a self-contained job to the group.
    // The job starts executing as soon as a worker is available; it is not
    // required that the caller waits immediately.
    // No-op on an invalid group or when the system is no longer initialised.
    void Submit(const JobCallback& job, JobPriority priority = JobPriority::Normal);

    // Adds a parallel range [0, count) to the group, using the system default
    // grain size. See the class comment for the lifetime contract.
    void ParallelFor(uint32_t count, const RangeCallback& range,
                     JobPriority priority = JobPriority::Normal);

    // Adds a parallel range [0, count) with an explicit grain size.
    // grainSize is clamped to at least 1.
    void ParallelFor(uint32_t count, uint32_t grainSize, const RangeCallback& range,
                     JobPriority priority = JobPriority::Normal);

    // Blocks until every job in the group has completed. While waiting, the
    // calling thread executes pending jobs, so Wait() never deadlocks on work
    // submitted by this group or by others.
    //
    // If any job threw, the first captured exception is rethrown here, after
    // all jobs in the group have finished and after the group has been drained
    // and marked reusable. Exceptions never escape a worker thread.
    //
    // After Wait() returns the group is empty and may be reused.
    void Wait();

    // Drains the group exactly like Wait(), but never throws.
    //
    // Called by the destructor and by move assignment, where throwing would
    // terminate the process. An unobserved job failure is reported through
    // SetUnobservedJobErrorHook and counted, never dropped.
    void DrainNoThrow() noexcept;

    // True when every job in the group has completed.
    // Note: this is a point-in-time observation; jobs may still be submitted.
    bool IsComplete() const;

    // Number of jobs currently pending or running.
    uint32_t GetPendingCount() const;

private:
    friend class JobSystem;

    explicit JobGroup(const std::shared_ptr<JobSystemState>& state);

    // Shared by Wait() and DrainNoThrow(): waits for every job, updates the
    // counters, releases the tasks and marks the group reusable. Returns the
    // first captured exception, if any. Not noexcept: DrainNoThrow() contains it.
    std::exception_ptr DrainImpl();

    struct Impl;

    // Shared with the owning JobSystem, so a group can outlive it. Null when the
    // group is invalid.
    std::shared_ptr<JobSystemState> m_state;

    std::unique_ptr<Impl> m_impl;
};

} // namespace jobs
} // namespace gr

#endif // GR_JOBS_GROUP_H
