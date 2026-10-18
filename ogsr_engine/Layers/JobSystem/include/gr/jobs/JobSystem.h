#ifndef GR_JOBS_SYSTEM_H
#define GR_JOBS_SYSTEM_H

#include "gr/jobs/JobConfig.h"
#include "gr/jobs/JobStats.h"
#include "gr/jobs/JobTypes.h"

#include <cstdint>
#include <memory>

namespace gr {
namespace jobs {

class JobGroup;

// Opaque shared state. Declared here so a JobGroup can hold a shared_ptr to it
// and therefore outlive the JobSystem that created it. Defined in a private
// header; consumers never see it.
struct JobSystemState;

// ---------------------------------------------------------------------------
// Unobserved job failures
// ---------------------------------------------------------------------------
// A job failure is normally rethrown by an explicit Wait(). When a group is
// destroyed without one, the destructor must not throw — a throwing destructor
// calls std::terminate — so the failure is reported through this hook and an
// always-maintained counter instead.
//
// This is what prevents silent data loss: a discarded failure is never invisible.
using UnobservedJobErrorHook = void (*)(const char* context, const char* what);

// Атомарно заменяет диагностический hook; nullptr оставляет только счётчик.
// Hook вызывается после завершения и освобождения задач группы. Его исключения
// перехватываются; рекурсивный вызов на том же потоке лишь увеличивает счётчик.
// Строки действительны только во время вызова. Hook не должен менять lifecycle
// из callback задачи. Смена hook не дожидается уже начавшихся вызовов: код
// старого hook должен оставаться загруженным до их завершения.
void SetUnobservedJobErrorHook(UnobservedJobErrorHook hook);

// Number of job failures discarded because their group was destroyed without an
// explicit Wait(). Always maintained, including when JobConfig::enableStats is
// false. Never reset.
uint64_t GetUnobservedJobErrorCount();

// GR Job System — a small, graph-API-independent task layer over enkiTS.
//
// LIFETIME
//   Initialize() ... Shutdown(). Shutdown() may be called when uninitialized.
//   Initialize() may be called again after Shutdown(). The destructor calls
//   Shutdown().
//
//   A JobSystem and the groups it created share ownership of the state, so a
//   group may outlive its system: the group keeps the state (and the
//   already-drained backend) alive. The only requirement is that no NEW work is
//   submitted through a group whose system is gone — see JobGroup.
//
// ПОТОКИ И LIFECYCLE
//   Initialize, Shutdown и уничтожение системы — только на потоке-владельце
//   вне callback задач. Initialize из callback возвращает false; Shutdown
//   из callback или чужого потока выбрасывает logic_error до смены состояния.
//   Уничтожение системы внутри её задачи недопустимо (noexcept-деструктор).
//   CreateGroup, ParallelFor и операции групп — на потоке инициализации
//   либо внутри GR-задачи. Произвольные внешние потоки не поддерживаются.
//   Операции lifecycle нельзя выполнять одновременно с внешними вызовами API.
//   После Shutdown старые группы остаются закрытыми даже после Initialize:
//   новый запуск получает отдельное состояние и новые локальные счётчики.
//   Процессный счётчик необработанных ошибок не сбрасывается.
//
// CPU AFFINITY
//   None is applied, by design (F3.2A.1 decision). OS Default scheduling means
//   the OS may place any worker on any logical processor; it does NOT reserve
//   physical cores for the job system. Thread priorities are never modified.
//
// EXCEPTIONS
//   A throwing job never terminates the process and never escapes into enkiTS:
//   enkiTS only decrements a task's running count after ExecuteRange returns,
//   so an escaping exception would leak the count and deadlock WaitforTask.
//   Every callback is therefore wrapped and the first exception is captured.
//
//   An explicit Wait() rethrows it on the caller's thread. If the group is
//   destroyed without Wait(), the destructor cannot throw, so the failure goes
//   to SetUnobservedJobErrorHook and GetUnobservedJobErrorCount instead.
class JobSystem {
public:
    JobSystem();
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Creates the worker threads and starts the scheduler.
    //
    // backgroundWorkers == 0 with automaticWorkers == true selects the
    // documented automatic policy:
    //     P = physical core count (GetLogicalProcessorInformationEx)
    //     backgroundWorkers = clamp(P / 2, 1, maxAutoBackgroundWorkers)
    // This is a HEURISTIC, not a proven optimum: it has not been measured
    // against real engine load. Physical cores are used rather than logical
    // processors to avoid SMT sibling contention, and half of them are left to
    // the rest of the engine. OS Default scheduling still decides where each
    // worker actually runs.
    //
    // backgroundWorkers == 0 with automaticWorkers == false runs sequentially:
    // no background threads are created and all work executes on the initiating
    // thread. This is the debugging and fallback mode.
    //
    // Returns false without changing state if the configuration is invalid.
    // При повторном Initialize работающая система сохраняется без изменений.
    bool Initialize(const JobConfig& config = JobConfig());

    // Waits for all outstanding work — including work owned by any still-alive
    // JobGroup — stops the worker threads and releases the scheduler's threads.
    // Safe to call when uninitialized. Initialize() may be called again
    // afterwards.
    //
    // Because this drains every submitted task, destroying a JobGroup after
    // Shutdown() is safe even without an explicit Wait().
    //
    // Jobs must not be submitted after Shutdown() is called; doing so is a
    // programming error and is a no-op.
    void Shutdown();

    // True once Initialize() has succeeded and Shutdown() has not been called.
    bool IsInitialized() const;

    // Creates an empty group bound to this system.
    // Returns an invalid group (IsValid() == false) when the system is not
    // initialised; every operation on an invalid group is a no-op.
    JobGroup CreateGroup();

    // Synchronous parallel range over [0, count).
    //
    // Partitions the range, runs it on the worker pool and waits for completion
    // before returning. Because it waits, callbacks may safely capture by
    // reference to the caller's stack.
    //
    // count == 0 is valid and does nothing. grainSize is clamped to >= 1.
    // Rethrows the first captured job exception, if any.
    void ParallelFor(uint32_t count, const RangeCallback& range,
                     JobPriority priority = JobPriority::Normal);

    void ParallelFor(uint32_t count, uint32_t grainSize, const RangeCallback& range,
                     JobPriority priority = JobPriority::Normal);

    // Configured background worker threads (0 in sequential mode).
    uint32_t GetBackgroundWorkerCount() const;

    // backgroundWorkers + 1: the initiating thread is always a participant.
    uint32_t GetTotalParticipantCount() const;

    // Logical processors available to this process.
    uint32_t GetLogicalProcessorCount() const;

    // Physical cores detected (0 when detection was unavailable).
    uint32_t GetPhysicalCoreCount() const;

    // Snapshot of the diagnostic counters.
    JobStats GetStats() const;

    // Процессный счётчик необработанных ошибок (общий для всех поколений).
    uint64_t GetUnobservedFailureCount() const;

private:
    friend class JobGroup;

    std::shared_ptr<JobSystemState> m_state;
};

} // namespace jobs
} // namespace gr

#endif // GR_JOBS_SYSTEM_H
