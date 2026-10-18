#include "EnkiBackend.h"
#include "InternalJobTask.h"

#ifdef GR_JOBS_TESTING
#include "../tests/JobTestHooks.h"
#endif

#include <cassert>
#include <stdexcept>

namespace gr {
namespace jobs {

EnkiBackend::EnkiBackend()
    : m_scheduler()
    , m_backgroundWorkers(0)
    , m_enableStats(true)
    , m_initialized(false)
{
}

EnkiBackend::~EnkiBackend()
{
    // Never leave worker threads running. WaitforAllAndShutdown is safe to call
    // on an uninitialised scheduler.
    Shutdown();
}

bool EnkiBackend::Initialize(uint32_t backgroundWorkers, bool enableStats)
{
    if (m_initialized) {
        // Re-initialisation is supported by enkiTS (Initialize stops existing
        // threads first), but a second concurrent initialisation from the same
        // owner thread is almost certainly a programming error. Refuse it so the
        // caller notices instead of silently leaking a configuration.
        return false;
    }

    if (backgroundWorkers > 256) {
        return false;
    }

    m_backgroundWorkers = backgroundWorkers;
    m_enableStats = enableStats;

    // enkiTS counts the thread that calls Initialize as thread 0 and creates
    // numThreadsTotal - 1 background threads. Passing backgroundWorkers + 1
    // therefore yields exactly `backgroundWorkers` workers plus the initiating
    // thread, and backgroundWorkers == 0 yields a sequential single-thread system
    // (enkiTS handles a total of 1 thread as a special case).
    m_scheduler.Initialize(backgroundWorkers + 1);
    m_ownerThread = std::this_thread::get_id();
    m_initialized = true;
    return true;
}

void EnkiBackend::Shutdown()
{
    if (!m_initialized) {
        return;
    }
    if (InternalJobTask::IsExecutingCallback() ||
        std::this_thread::get_id() != m_ownerThread) {
        // Остановка из задачи ждала бы саму себя; чужой поток не является
        // зарегистрированным участником scheduler. Отказ до смены состояния.
        throw std::logic_error("GR Job System: Shutdown requires the owner outside callbacks");
    }

    // WaitforAllAndShutdown drains all outstanding work and then stops the
    // threads. It sets m_bWaitforAllCalled and m_bShutdownRequested, both of
    // which StartThreads() resets, so Initialize() works correctly afterwards.
    m_scheduler.WaitforAllAndShutdown();
    m_initialized = false;
    // Report no workers once the system is stopped, matching GetTotalParticipantCount().
    m_backgroundWorkers = 0;
}

bool EnkiBackend::Submit(InternalJobTask* task) noexcept
{
    if (!m_initialized || task == nullptr || !task->GetIsComplete()) {
        return false;
    }
#ifdef GR_JOBS_TESTING
    if (testing::RejectSubmit()) {
        return false;
    }
#endif
    // В закреплённой ревизии этот путь не выделяет память: очередь уже
    // создана Initialize(). Переполнение исполняет часть задачи синхронно.
    // InternalJobTask перехватывает исключения callback. После входа сюда
    // нельзя сообщать об отказе и освобождать задачу как неопубликованную.
    m_scheduler.AddTaskSetToPipe(task);
    return true;
}

void EnkiBackend::Wait(enki::ITaskSet* task) noexcept
{
    assert(task != nullptr);
    if (!m_initialized || task == nullptr) {
        return;
    }

    // TASK_PRIORITY_NUM - 1 lets the waiting thread run tasks of every priority
    // while it waits, so a group's jobs are not starved by the wait itself.
    m_scheduler.WaitforTask(task, enki::TaskPriority(enki::TASK_PRIORITY_NUM - 1));
}

void EnkiBackend::WaitAll()
{
    if (!m_initialized) {
        return;
    }
    m_scheduler.WaitforAll();
}

uint32_t EnkiBackend::GetTotalParticipantCount() const
{
    if (!m_initialized) {
        return 0;
    }
    return m_backgroundWorkers + 1;
}

} // namespace jobs
} // namespace gr
