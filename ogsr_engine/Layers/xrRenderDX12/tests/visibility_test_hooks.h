#pragma once
#ifndef GR_JOBS_TESTING
#error visibility_test_hooks requires GR_JOBS_TESTING
#endif
#include <atomic>
#include <stdexcept>

namespace renderprep12::testing {
// Только отдельная тестовая сборка. Переключать фазу можно после drain.
enum class Phase { None, Counts, Scatter, Keys, Ordering, OrderedScatter };
inline std::atomic<Phase> failPhase{Phase::None};
inline std::atomic<unsigned> active{0}, entered{0}, exited{0}, failures{0};
struct CallbackScope {
    CallbackScope() { ++active; ++entered; }
    ~CallbackScope() { ++exited; --active; }
};
inline void FailAfterWrite(Phase phase, bool parallel, size_t block) {
    if (!parallel || block!=0) return;
    auto expected=phase;
    if (failPhase.compare_exchange_strong(expected,Phase::None)) {
        ++failures;
        throw std::runtime_error("injected late visibility failure");
    }
}
}
