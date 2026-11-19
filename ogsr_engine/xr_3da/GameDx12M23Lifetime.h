#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <wrl/client.h>

// F4.0-J2B-M23-R1: test-only COM ownership/refcount instrumentation.
//
// The normal feature-on build aliases M23Ptr12 to Microsoft::WRL::ComPtr and
// pays nothing. A dedicated test build defines OGSR_M23_LIFETIME_PROBE and
// counts every actual ComPtr Reset (which calls Release) and every Detach
// (which transfers ownership without Release), plus resource creation and
// captured owner objects. The corrective test asserts that a forced-unknown
// draw leaves no tracked pointer to be released by the destructor.
#ifdef OGSR_M23_LIFETIME_PROBE

#include <wrl/client.h>

namespace m23probe12
{
inline std::atomic<uint64_t>& CreatedCounter() noexcept
{
    static std::atomic<uint64_t> value{0};
    return value;
}
inline std::atomic<uint64_t>& CapturedCounter() noexcept
{
    static std::atomic<uint64_t> value{0};
    return value;
}
inline std::atomic<uint64_t>& ReleasedCounter() noexcept
{
    static std::atomic<uint64_t> value{0};
    return value;
}
inline std::atomic<uint64_t>& DetachedCounter() noexcept
{
    static std::atomic<uint64_t> value{0};
    return value;
}

inline void MarkCreated() noexcept { ++CreatedCounter(); }
inline void OnCapture() noexcept { ++CapturedCounter(); }
inline void OnRelease() noexcept { ++ReleasedCounter(); }
inline void OnDetach() noexcept { ++DetachedCounter(); }

inline int64_t LiveCount() noexcept
{
    const int64_t created =
        static_cast<int64_t>(CreatedCounter().load());
    const int64_t captured =
        static_cast<int64_t>(CapturedCounter().load());
    const int64_t released =
        static_cast<int64_t>(ReleasedCounter().load());
    const int64_t detached =
        static_cast<int64_t>(DetachedCounter().load());
    return created + captured - released - detached;
}
} // namespace m23probe12

template <typename T>
class M23ProbePtr12
{
public:
    M23ProbePtr12() = default;
    ~M23ProbePtr12() { Reset(); }

    M23ProbePtr12(const M23ProbePtr12&) = delete;
    M23ProbePtr12& operator=(const M23ProbePtr12&) = delete;

    M23ProbePtr12(M23ProbePtr12&& other) noexcept
        : m_ptr(other.m_ptr.Detach())
    {
    }
    M23ProbePtr12& operator=(M23ProbePtr12&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_ptr.Attach(other.m_ptr.Detach());
        }
        return *this;
    }
    M23ProbePtr12& operator=(T* other) noexcept
    {
        if (m_ptr.Get() != other)
        {
            Reset();
            // ComPtr::operator=(T*) takes its own reference; Attach would
            // steal a reference owned by the bridge and underflow later.
            m_ptr = other;
            if (other != nullptr)
                m23probe12::OnCapture();
        }
        return *this;
    }

    T* Get() const noexcept { return m_ptr.Get(); }
    T** GetAddressOf() noexcept { return m_ptr.GetAddressOf(); }
    T* const* GetAddressOf() const noexcept { return m_ptr.GetAddressOf(); }
    T* operator->() const noexcept { return m_ptr.Get(); }
    explicit operator bool() const noexcept { return m_ptr.Get() != nullptr; }
    bool operator!=(std::nullptr_t) const noexcept
    {
        return m_ptr.Get() != nullptr;
    }
    bool operator==(std::nullptr_t) const noexcept
    {
        return m_ptr.Get() == nullptr;
    }

    void Reset() noexcept
    {
        if (m_ptr.Get() != nullptr)
        {
            m23probe12::OnRelease();
            m_ptr.Reset();
        }
    }
    T* Detach() noexcept
    {
        if (m_ptr.Get() == nullptr)
            return nullptr;
        m23probe12::OnDetach();
        return m_ptr.Detach();
    }

private:
    Microsoft::WRL::ComPtr<T> m_ptr;
};

template <typename T>
using M23Ptr12 = M23ProbePtr12<T>;

#else // OGSR_M23_LIFETIME_PROBE

namespace m23probe12
{
inline void MarkCreated() noexcept {}
inline void OnCapture() noexcept {}
inline void OnRelease() noexcept {}
inline void OnDetach() noexcept {}
} // namespace m23probe12

template <typename T>
using M23Ptr12 = Microsoft::WRL::ComPtr<T>;

#endif // OGSR_M23_LIFETIME_PROBE
