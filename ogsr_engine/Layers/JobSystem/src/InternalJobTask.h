// Internal implementation detail of the GR Job System. Not installed, not part
// of the public API, and deliberately free of any enkiTS type in the public
// headers. Safe to change without affecting consumers.
#ifndef GR_INTERNAL_JOB_TASK_H
#define GR_INTERNAL_JOB_TASK_H

#include "gr/jobs/JobConfig.h"
#include "gr/jobs/JobTypes.h"

#include <TaskScheduler.h>

#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>

namespace gr {
namespace jobs {

// Maps a GR priority onto an enkiTS task priority.
inline enki::TaskPriority ToEnkiPriority(JobPriority priority)
{
#if ENKITS_TASK_PRIORITIES_NUM > 2
    switch (priority) {
    case JobPriority::High:   return enki::TASK_PRIORITY_HIGH;
    case JobPriority::Normal: return enki::TASK_PRIORITY_MED;
    case JobPriority::Low:    return enki::TASK_PRIORITY_LOW;
    }
    return enki::TASK_PRIORITY_MED;
#else
    // enkiTS was compiled with two priority bands. Normal shares the high band,
    // which is also enkiTS's own default for a task.
    return (priority == JobPriority::Low) ? enki::TASK_PRIORITY_LOW
                                          : enki::TASK_PRIORITY_HIGH;
#endif
}

// Captures the first exception thrown by a job so that it can be rethrown on
// the thread that waits.
//
// This is mandatory, not defensive: enkiTS decrements a task's running count
// only after ExecuteRange returns (TaskScheduler::SplitAndAddTask). An
// exception escaping ExecuteRange would therefore leak the count, the task
// would never report complete, and WaitforTask would block forever.
class JobErrorSlot
{
public:
    // Stores the exception unless one was already captured. Never throws.
    void Capture(std::exception_ptr error) noexcept
    {
        if (!error) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_first) {
            m_first = error;
        }
        ++m_failures;
    }

    // True when at least one job captured an exception.
    bool HasError() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_first != nullptr;
    }

    uint32_t FailureCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_failures;
    }

    // Returns the first captured exception, or nullptr when no job failed.
    std::exception_ptr FirstException() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_first;
    }

    // Rethrows the first captured exception. Does nothing when no job failed.
    // Must be called on the thread that performed the wait.
    void RethrowFirst() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_first) {
            std::rethrow_exception(m_first);
        }
    }

private:
    mutable std::mutex m_mutex;
    std::exception_ptr m_first;
    uint32_t           m_failures = 0;
};

// One GR job or one GR ParallelFor range, expressed as an enkiTS task set.
class InternalJobTask : public enki::ITaskSet
{
public:
    typedef std::function<void(enki::TaskSetPartition range, uint32_t threadIndex)> Function;

    InternalJobTask(uint32_t setSize, uint32_t minRange, Function function,
                   std::shared_ptr<JobErrorSlot> errors, JobPriority priority)
        : m_function(std::move(function))
        , m_errors(std::move(errors))
    {
        m_SetSize = setSize;
        m_MinRange = (minRange > 0) ? minRange : 1;
        m_Priority = ToEnkiPriority(priority);
    }

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadIndex) noexcept override
    {
        ++CallbackDepth();
        // An exception must never leave this function; see JobErrorSlot.
        try {
            m_function(range, threadIndex);
        } catch (...) {
            if (m_errors) {
                m_errors->Capture(std::current_exception());
            } else {
                // No error slot means the caller explicitly accepted that a
                // failure is unobservable. Swallowing here is still required to
                // keep enkiTS's completion accounting intact.
            }
        }
        --CallbackDepth();
    }

    static bool IsExecutingCallback() noexcept { return CallbackDepth() != 0; }

    const std::shared_ptr<JobErrorSlot>& Errors() const { return m_errors; }

    // Adapts a GR RangeCallback to the enkiTS function shape.
    static Function MakeRangeFunction(const RangeCallback& callback)
    {
        return [callback](enki::TaskSetPartition range, uint32_t threadIndex) {
            callback(range.start, range.end, threadIndex);
        };
    }

    static Function MakeJobFunction(const JobCallback& callback)
    {
        return [callback](enki::TaskSetPartition, uint32_t) {
            callback();
        };
    }

private:
    static uint32_t& CallbackDepth() noexcept
    {
        static thread_local uint32_t depth = 0;
        return depth;
    }

    Function m_function;
    std::shared_ptr<JobErrorSlot> m_errors;
};

} // namespace jobs
} // namespace gr

#endif // GR_INTERNAL_JOB_TASK_H
