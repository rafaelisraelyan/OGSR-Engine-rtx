#ifndef GR_JOB_DIAGNOSTICS_H
#define GR_JOB_DIAGNOSTICS_H

#include <exception>

namespace gr {
namespace jobs {

// Reports a job failure that no explicit Wait() observed, because the owning
// group was destroyed without one.
//
// The group destructor cannot throw (a throwing destructor calls
// std::terminate), so this is the documented, safe route for a discarded
// failure. Invokes the hook installed by SetUnobservedJobErrorHook, increments
// the always-on counter, and never throws.
void ReportUnobservedJobFailure(const char* context, std::exception_ptr error) noexcept;

} // namespace jobs
} // namespace gr

#endif // GR_JOB_DIAGNOSTICS_H
