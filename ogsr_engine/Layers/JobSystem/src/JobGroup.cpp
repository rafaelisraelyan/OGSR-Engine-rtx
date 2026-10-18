#include "gr/jobs/JobGroup.h"

#include "InternalJobTask.h"
#include "JobDiagnostics.h"
#include "JobSystemState.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>
#include <vector>

#ifdef GR_JOBS_TESTING
#include "../tests/JobTestHooks.h"
#endif

namespace gr {
namespace jobs {

// ---------------------------------------------------------------------------
// JobGroup::Impl
// ---------------------------------------------------------------------------
struct JobGroup::Impl
{
    Impl()
        : tasks()
        , errors()
    {
    }

    // Jobs owned by this group. The vector is drained by Wait()/DrainNoThrow(),
    // which makes the group reusable. The pointees never move: only the
    // unique_ptrs are moved if the vector reallocates.
#ifdef GR_JOBS_TESTING
    std::vector<std::unique_ptr<InternalJobTask>,
                testing::RegistrationAllocator<std::unique_ptr<InternalJobTask>>> tasks;
#else
    std::vector<std::unique_ptr<InternalJobTask>> tasks;
#endif

    void SubmitTask(JobSystemState& state, std::unique_ptr<InternalJobTask> task)
    {
        InternalJobTask* const published = task.get();
        // Сначала владение: при bad_alloc задача ещё не видна планировщику.
        // Перемещение unique_ptr при росте вектора не перемещает саму задачу.
        tasks.push_back(std::move(task));
        if (!state.backend.Submit(published)) {
            // Отказ гарантированно предшествует публикации и callback.
            tasks.pop_back();
            throw std::runtime_error("GR Job System: task submission rejected");
        }
        // После принятия — только невыбрасывающий счётчик; владельцем уже
        // является группа. Не обращаемся к элементу вектора после callback.
        state.addSubmitted(1);
    }

    // Shared by every job in the group, so a failure in any of them is reported
    // once to the thread that waits.
    std::shared_ptr<JobErrorSlot> errors;
};

// ---------------------------------------------------------------------------
// JobGroup
// ---------------------------------------------------------------------------
JobGroup::JobGroup()
    : m_state()
    , m_impl(new Impl())
{
}

JobGroup::JobGroup(const std::shared_ptr<JobSystemState>& state)
    : m_state(state)
    , m_impl(new Impl())
{
}

// Never throws: a throwing destructor would call std::terminate. Pending work is
// drained so a group can never destroy a job that is still running.
JobGroup::~JobGroup()
{
    DrainNoThrow();
}

JobGroup::JobGroup(JobGroup&& other) noexcept
    : m_state(std::move(other.m_state))
    , m_impl(std::move(other.m_impl))
{
    other.m_state.reset();
}

JobGroup& JobGroup::operator=(JobGroup&& other) noexcept
{
    if (this != &other) {
        // Drain our own work before adopting another group's. DrainNoThrow, not
        // Wait(): this function is noexcept and must not terminate the process.
        DrainNoThrow();
        m_state = std::move(other.m_state);
        m_impl = std::move(other.m_impl);
        other.m_state.reset();
    }
    return *this;
}

bool JobGroup::IsValid() const
{
    return (m_impl != nullptr) && (m_state != nullptr);
}

void JobGroup::Submit(const JobCallback& job, JobPriority priority)
{
    if (!job || !m_impl || !m_state) {
        return;
    }
    JobSystemState& state = *m_state;
    if (!state.backend.IsInitialized()) {
        // Submitting after Shutdown() is a programming error; it is a no-op
        // rather than a crash.
        return;
    }

    if (!m_impl->errors) {
        m_impl->errors = std::make_shared<JobErrorSlot>();
    }

    std::unique_ptr<InternalJobTask> task(
        new InternalJobTask(1, 1, InternalJobTask::MakeJobFunction(job), m_impl->errors, priority));

    m_impl->SubmitTask(state, std::move(task));
}

void JobGroup::ParallelFor(uint32_t count, const RangeCallback& range, JobPriority priority)
{
    if (!m_state) {
        return;
    }
    ParallelFor(count, m_state->config.defaultGrainSize, range, priority);
}

void JobGroup::ParallelFor(uint32_t count, uint32_t grainSize, const RangeCallback& range,
                           JobPriority priority)
{
    if (count == 0 || !range || !m_impl || !m_state) {
        return;
    }
    JobSystemState& state = *m_state;
    if (!state.backend.IsInitialized()) {
        return;
    }

    if (!m_impl->errors) {
        m_impl->errors = std::make_shared<JobErrorSlot>();
    }

    std::unique_ptr<InternalJobTask> task(
        new InternalJobTask(count, grainSize, InternalJobTask::MakeRangeFunction(range),
                            m_impl->errors, priority));

    m_impl->SubmitTask(state, std::move(task));
}

std::exception_ptr JobGroup::DrainImpl()
{
    if (!m_impl || !m_state) {
        return std::exception_ptr();
    }
    JobSystemState& state = *m_state;

    {
        // Waiting on each job in turn is correct and deadlock-free: the first
        // wait drains the shared pipe (running jobs from the whole system while
        // it waits), and each later wait returns immediately because its job is
        // already done.
        const size_t jobCount = m_impl->tasks.size();
        for (size_t i = 0; i < jobCount; ++i) {
            InternalJobTask* task = m_impl->tasks[i].get();
            if (state.backend.IsInitialized()) {
                state.backend.Wait(task);
            }
            if (task->GetIsComplete()) {
                state.addCompleted(1);
            }
        }
    }
    // When the backend is not initialised the system has already been shut
    // down, and JobSystem::Shutdown() drains every submitted task before it
    // returns. There is nothing left to wait for, but the task objects still
    // have to be released below.

    const uint32_t failures = m_impl->errors ? m_impl->errors->FailureCount() : 0;
    if (failures > 0) {
        state.addJobFailures(failures);
    }

    // Keep the captured exception across the drain: the group is released and
    // marked reusable before the failure is reported.
    std::exception_ptr captured = m_impl->errors ? m_impl->errors->FirstException()
                                                 : std::exception_ptr();

    m_impl->tasks.clear();
    m_impl->errors.reset();
    state.addGroupsCompleted(1);

    return captured;
}

void JobGroup::Wait()
{
    std::exception_ptr captured = DrainImpl();
    if (captured) {
        // Rethrow on the caller's thread, after the group has been drained and
        // the counters updated, so a failure never leaves inconsistent state.
        std::rethrow_exception(captured);
    }
}

void JobGroup::DrainNoThrow() noexcept
{
    std::exception_ptr captured;
    try {
        captured = DrainImpl();
    } catch (...) {
        // DrainImpl should not throw, but a noexcept destructor must survive
        // anything the backend does.
        captured = std::current_exception();
    }

    if (captured) {
        ReportUnobservedJobFailure("JobGroup destroyed without Wait()", captured);
    }
}

bool JobGroup::IsComplete() const
{
    if (!m_impl || !m_state) {
        return true;
    }
    for (size_t i = 0; i < m_impl->tasks.size(); ++i) {
        if (!m_impl->tasks[i]->GetIsComplete()) {
            return false;
        }
    }
    return true;
}

uint32_t JobGroup::GetPendingCount() const
{
    if (!m_impl || !m_state) {
        return 0;
    }
    uint32_t pending = 0;
    for (size_t i = 0; i < m_impl->tasks.size(); ++i) {
        if (!m_impl->tasks[i]->GetIsComplete()) {
            ++pending;
        }
    }
    return pending;
}

} // namespace jobs
} // namespace gr
