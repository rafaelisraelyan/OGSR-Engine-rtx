#ifndef GR_JOBS_TYPES_H
#define GR_JOBS_TYPES_H

#include <cstdint>
#include <functional>

namespace gr {
namespace jobs {

// Callback that processes a half-open element range [begin, end).
//
// `threadIndex` identifies the worker executing the range. Use it to select
// per-thread output buckets; never use it to change control flow, because the
// same range may be executed by different workers on different runs.
using RangeCallback = std::function<void(uint32_t begin, uint32_t end, uint32_t threadIndex)>;

// Self-contained callback taking no arguments.
using JobCallback = std::function<void()>;

} // namespace jobs
} // namespace gr

#endif // GR_JOBS_TYPES_H
