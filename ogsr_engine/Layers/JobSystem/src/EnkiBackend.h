// Internal backend adapter around enkiTS. Not installed, not part of the public
// API. This is the only layer that names enkiTS types.
#ifndef GR_ENKI_BACKEND_H
#define GR_ENKI_BACKEND_H

#include "gr/jobs/JobConfig.h"

#include <TaskScheduler.h>

#include <cstdint>
#include <thread>

namespace gr {
namespace jobs {

class InternalJobTask;

// Owns the enkiTS scheduler and translates GR operations into enkiTS calls.
//
// Initialize/Shutdown — только поток-владелец, вне callback. Submit/Wait
// допускаются также внутри GR-задачи. Полный контракт — в JobSystem.h.
class EnkiBackend
{
public:
    EnkiBackend();
    ~EnkiBackend();

    EnkiBackend(const EnkiBackend&) = delete;
    EnkiBackend& operator=(const EnkiBackend&) = delete;

    // Creates the worker threads. `backgroundWorkers` is the number of worker
    // threads to create; 0 means sequential mode (no worker threads).
    // Returns false and leaves the backend uninitialised on invalid input.
    bool Initialize(uint32_t backgroundWorkers, bool enableStats);

    // Waits for all outstanding work, stops the threads and releases the
    // scheduler. Safe to call when uninitialised.
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    // false означает отказ ДО публикации. После публикации исключения
    // недопустимы: enkiTS не предоставляет откат частичной отправки.
    bool Submit(InternalJobTask* task) noexcept;

    // Blocks until the task is complete, running pending work meanwhile.
    void Wait(enki::ITaskSet* task) noexcept;

    // Blocks until every task in the scheduler has completed.
    void WaitAll();

    // Number of threads that can execute tasks: background workers + the
    // initiating thread.
    uint32_t GetTotalParticipantCount() const;

    // Configured background worker thread count.
    uint32_t GetBackgroundWorkerCount() const { return m_backgroundWorkers; }

    bool AreStatsEnabled() const { return m_enableStats; }

    enki::TaskScheduler& GetScheduler() { return m_scheduler; }

private:
    enki::TaskScheduler m_scheduler;
    uint32_t            m_backgroundWorkers = 0;
    bool                m_enableStats = true;
    bool                m_initialized = false;
    std::thread::id     m_ownerThread;
};

} // namespace jobs
} // namespace gr

#endif // GR_ENKI_BACKEND_H
