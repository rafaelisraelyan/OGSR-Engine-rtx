#pragma once

// Minimal scope-exit guard used by the J2A VFS reader. It is intentionally
// independent of engine types so the same lifetime mechanism can be exercised
// by a standalone CPU test.

#include <utility>

namespace dx12scope
{
template <typename Function>
class ScopeGuard
{
public:
    explicit ScopeGuard(Function&& function)
        : m_function(std::forward<Function>(function))
    {
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

    ScopeGuard(ScopeGuard&& other) noexcept
        : m_function(std::move(other.m_function)), m_active(other.m_active)
    {
        other.m_active = false;
    }

    ScopeGuard& operator=(ScopeGuard&&) = delete;

    ~ScopeGuard()
    {
        if (m_active)
            m_function();
    }

    void Dismiss() noexcept { m_active = false; }

private:
    Function m_function;
    bool m_active = true;
};

template <typename Function>
ScopeGuard<Function> MakeScopeGuard(Function&& function)
{
    return ScopeGuard<Function>(std::forward<Function>(function));
}
} // namespace dx12scope
