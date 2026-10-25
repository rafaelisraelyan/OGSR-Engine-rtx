// F4.0-B3-A: GPU lifetime, command-list and validation decision seam.
//
// This header holds the policy that the standalone offscreen test obeys and
// that the CPU safety tests exercise. It contains no D3D12 calls, so the tests
// can prove the decisions without creating a device or provoking a real GPU
// fault (TDR/hang).
//
// Terminal policy: the standalone test process must never release GPU-backed
// objects while completion is unconfirmed. The guard below turns an ordinary
// return or an exception unwinding in that state into a process exit without
// C++ destruction. This policy belongs to the standalone test only and must
// not be copied into the game executable.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace b3a
{
// Exit code used when an unconfirmed GPU owner would otherwise be destroyed.
constexpr int kTerminalGpuExitCode = 90;

// State of the GPU work that belongs to one owner.
enum class GpuWorkState : uint8_t
{
    NoWorkSubmitted,
    WorkSubmittedUnknown,
    WorkCompleted,
    DeviceRemoved
};

// State of the single command list/allocator pair.
enum class ListState : uint8_t
{
    Recording,
    Closed,
    CloseFailed,
    ResetReady
};

// Result of a bounded fence wait, independent of the D3D12 vocabulary.
enum class FenceOutcome : uint8_t
{
    Completed,
    Timeout,
    Failed
};

// A failed Close is terminal for the current command session: the list is
// neither recording nor closed, so it may not be submitted, closed again or
// reset. The D3D12 runtime does not define a recovery from this state.
inline bool MaySubmit(ListState state) noexcept
{
    return state == ListState::Recording;
}

// An allocator/list reset is only legal once completion is confirmed and the
// list reached a defined post-close state. A list whose Close failed carries
// no submitted work but is still not resettable.
inline bool MayResetList(ListState list, GpuWorkState work) noexcept
{
    if (work == GpuWorkState::WorkSubmittedUnknown ||
        work == GpuWorkState::DeviceRemoved)
        return false;
    return list == ListState::ResetReady || list == ListState::Closed;
}

inline bool CompletionConfirmed(GpuWorkState work) noexcept
{
    return work == GpuWorkState::WorkCompleted;
}

// A resource used by submitted commands may not be released while completion
// is unconfirmed. DeviceRemoved keeps the same terminal-retention policy.
inline bool MustRetainUntilCompletion(GpuWorkState work) noexcept
{
    return work == GpuWorkState::WorkSubmittedUnknown ||
           work == GpuWorkState::DeviceRemoved;
}

// ExecuteCommandLists is asynchronous: immediately after the call the
// completion state is unknown, even if no error was reported.
inline GpuWorkState StateAfterExecute() noexcept
{
    return GpuWorkState::WorkSubmittedUnknown;
}

// Device-removal reasons reported by ID3D12Device::GetDeviceRemovedReason().
// S_OK or any unrelated code is not evidence of completion: a non-completed
// fence wait with such a code stays WorkSubmittedUnknown.
inline bool IsDeviceRemovalReason(long reason) noexcept
{
    switch (static_cast<unsigned long>(reason))
    {
    case 0x887A0005ul: // DXGI_ERROR_DEVICE_REMOVED
    case 0x887A0006ul: // DXGI_ERROR_DEVICE_HUNG
    case 0x887A0007ul: // DXGI_ERROR_DEVICE_RESET
    case 0x887A0020ul: // DXGI_ERROR_DRIVER_INTERNAL_ERROR
        return true;
    default:
        return false;
    }
}

inline GpuWorkState StateAfterFenceWait(FenceOutcome outcome,
                                       long deviceRemovedReason) noexcept
{
    if (outcome == FenceOutcome::Completed)
        return GpuWorkState::WorkCompleted;
    return IsDeviceRemovalReason(deviceRemovedReason)
               ? GpuWorkState::DeviceRemoved
               : GpuWorkState::WorkSubmittedUnknown;
}

inline ListState ListStateAfterClose() noexcept
{
    return ListState::Closed;
}

inline ListState ListStateAfterFailedClose() noexcept
{
    return ListState::CloseFailed;
}

inline ListState ListStateAfterReset() noexcept
{
    return ListState::Recording;
}

inline ListState ListStateAfterConfirmedSubmit() noexcept
{
    return ListState::ResetReady;
}

// Debug-layer validation evidence gathered after GPU completion.
struct ValidationEvidence
{
    bool debugLayerAvailable = false;
    bool queueAvailable = false;
    bool enumerationComplete = false;
    bool completionConfirmed = false;
    bool gbvActive = false;
    uint64_t errors = 0;
    uint64_t corruptions = 0;
    uint64_t warnings = 0;
    uint64_t discarded = 0;
    uint64_t hiddenByFilter = 0;
};

// Verdict policy:
//   FAIL       - reproducible ERROR/CORRUPTION messages.
//   UNVERIFIED - queue missing, enumeration incomplete, work not confirmed,
//                dropped messages, or no evidence that GBV is active.
//   PASS       - complete clean enumeration on a live debug layer with
//                confirmed GPU completion and GBV evidence.
inline const char* EvaluateValidation(const ValidationEvidence& evidence) noexcept
{
    if (!evidence.debugLayerAvailable || !evidence.queueAvailable ||
        !evidence.completionConfirmed || !evidence.enumerationComplete)
        return "UNVERIFIED";
    if (evidence.errors != 0 || evidence.corruptions != 0)
        return "FAIL";
    if (evidence.discarded != 0 || evidence.hiddenByFilter != 0 ||
        !evidence.gbvActive)
        return "UNVERIFIED";
    return "PASS";
}

inline bool ValidationPasses(const char* status) noexcept
{
    return status != nullptr && status[0] == 'P' && status[1] == 'A' &&
           status[2] == 'S' && status[3] == 'S' && status[4] == '\0';
}

// Terminates the standalone process immediately: no stack unwinding, no static
// destruction, no atexit handlers. std::_Exit is used deliberately; diagnostics
// must be flushed by the caller before reaching this point.
[[noreturn]] inline void TerminalExit(int exitCode) noexcept
{
    std::_Exit(exitCode);
}

// Declared after the GPU owner. Its noexcept destructor inspects the live work
// state: if completion is still unconfirmed it runs the terminal action, which
// must not return. A normal return or an exception unwinding with confirmed
// completion therefore keeps ordinary RAII cleanup.
template <typename TerminalAction>
class UnknownCompletionGuard
{
public:
    UnknownCompletionGuard(const GpuWorkState& state,
                           TerminalAction action) noexcept
        : state_(state), action_(action)
    {
    }

    UnknownCompletionGuard(const UnknownCompletionGuard&) = delete;
    UnknownCompletionGuard& operator=(const UnknownCompletionGuard&) = delete;

    ~UnknownCompletionGuard() noexcept
    {
        if (!MustRetainUntilCompletion(state_))
            return;
        try
        {
            action_();
        }
        catch (...)
        {
        }
        // The terminal action must not return. If it does (or throws), exit
        // without unwinding instead of releasing the owner.
        TerminalExit(kTerminalGpuExitCode);
    }

private:
    const GpuWorkState& state_;
    TerminalAction action_;
};

// Move-only Win32 HANDLE owner: closes at most once, on every exit path.
class UniqueHandle
{
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE value) noexcept : value_(value) {}
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : value_(other.release()) {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            value_ = other.release();
        }
        return *this;
    }

    ~UniqueHandle() { reset(); }

    HANDLE get() const noexcept { return value_; }

    explicit operator bool() const noexcept
    {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }

    HANDLE release() noexcept
    {
        const HANDLE value = value_;
        value_ = nullptr;
        return value;
    }

    void reset(HANDLE value = nullptr) noexcept
    {
        // Self-reset must not close the handle and then keep a stale value.
        if (value == value_ && value_ != nullptr &&
            value_ != INVALID_HANDLE_VALUE)
            return;
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE)
            CloseHandle(value_);
        value_ = value;
    }

private:
    HANDLE value_ = nullptr;
};

// Fixed-capacity, statically allocated retention store. The standalone GPU
// test no longer parks owners here: unconfirmed completion goes straight to the
// terminal process exit, because a static store is destroyed at static
// destruction and would release GPU objects anyway. The type is kept as the
// negative control for the subprocess lifetime tests.
template <typename Owner, size_t Capacity>
class RetentionSlots
{
public:
    bool TryPark(Owner&& owner) noexcept
    {
        for (size_t i = 0; i < Capacity; ++i)
        {
            if (occupied_[i])
                continue;
            owners_[i] = std::move(owner);
            occupied_[i] = true;
            parked_ = i;
            return true;
        }
        return false;
    }

    size_t Occupied() const noexcept
    {
        size_t count = 0;
        for (size_t i = 0; i < Capacity; ++i)
            count += occupied_[i] ? 1u : 0u;
        return count;
    }

    bool IsOccupied(size_t index) const noexcept
    {
        return index < Capacity && occupied_[index];
    }

    Owner& At(size_t index) noexcept { return owners_[index]; }

    size_t LastParkedIndex() const noexcept { return parked_; }

private:
    Owner owners_[Capacity]{};
    bool occupied_[Capacity]{};
    size_t parked_ = Capacity;
};

} // namespace b3a
