#pragma once

// F4.0-B3-B2B: CPU-only policy seam for the persistent DX12 consumer.
//
// No D3D12 types are used here. The standalone renderer and the negative
// tests share these decisions so that the tests exercise the same completion,
// submission, sampling and viewport rules as the GPU path.

#include <cstdint>
#include <cstring>

namespace b3b2b
{
enum class FrameState : uint8_t
{
    Idle,
    Recording,
    Closed,
    Submitted,
    Completed,
    Failed,
    CompletionUnknown
};

inline bool MaySubmit(FrameState state) noexcept
{
    return state == FrameState::Recording;
}

inline bool MayReset(FrameState state, bool completionConfirmed) noexcept
{
    if (!completionConfirmed)
        return false;
    return state == FrameState::Closed || state == FrameState::Completed ||
           state == FrameState::Idle;
}

inline bool MustRetain(FrameState state) noexcept
{
    return state == FrameState::Submitted ||
           state == FrameState::CompletionUnknown;
}

inline FrameState AfterSubmit() noexcept { return FrameState::Submitted; }

inline FrameState AfterConfirmedCompletion() noexcept
{
    return FrameState::Completed;
}

inline FrameState AfterFailedClose() noexcept { return FrameState::Failed; }

struct FenceEvidence
{
    bool signalSucceeded = false;
    bool armSucceeded = false;
    bool waitSignaled = false;
    uint64_t completed = 0;
    uint64_t expected = 0;
    long deviceRemovedReason = 0;
    bool confirmed = false;
    const char* reason = "Uninitialized";
};

inline bool IsDeviceRemovedReason(long reason) noexcept
{
    switch (static_cast<unsigned long>(reason))
    {
    case 0x887A0005ul:
    case 0x887A0006ul:
    case 0x887A0007ul:
    case 0x887A0020ul:
        return true;
    default:
        return false;
    }
}

enum class FenceFailureDisposition : uint8_t
{
    UnknownCompletion,
    DeviceRemoved
};

inline FenceFailureDisposition ClassifyFenceFailure(long reason) noexcept
{
    return IsDeviceRemovedReason(reason)
               ? FenceFailureDisposition::DeviceRemoved
               : FenceFailureDisposition::UnknownCompletion;
}

inline FenceEvidence EvaluateFence(bool signalSucceeded, bool armSucceeded,
                                   bool waitSignaled, uint64_t completed,
                                   uint64_t expected,
                                   long deviceRemovedReason) noexcept
{
    FenceEvidence evidence;
    evidence.signalSucceeded = signalSucceeded;
    evidence.armSucceeded = armSucceeded;
    evidence.waitSignaled = waitSignaled;
    evidence.completed = completed;
    evidence.expected = expected;
    evidence.deviceRemovedReason = deviceRemovedReason;

    if (!signalSucceeded)
    {
        evidence.reason = "SignalFailed";
        return evidence;
    }
    if (!armSucceeded)
    {
        evidence.reason = "SetEventOnCompletionFailed";
        return evidence;
    }
    if (!waitSignaled)
    {
        evidence.reason = "WaitFailedOrTimeout";
        return evidence;
    }
    if (completed == 0xFFFFFFFFFFFFFFFFull)
    {
        evidence.reason = "DeviceRemovedMarker";
        return evidence;
    }
    // Strict HRESULT contract: only S_OK proves that the device is healthy.
    // S_FALSE, E_FAIL, unknown failures and every DXGI removal code must
    // leave work in WorkSubmittedUnknown/DeviceRemoved, never WorkCompleted.
    if (deviceRemovedReason != 0)
    {
        evidence.reason = "DeviceRemovedReason";
        return evidence;
    }
    if (completed < expected)
    {
        evidence.reason = "SpuriousWake";
        return evidence;
    }
    evidence.confirmed = true;
    evidence.reason = "Completed";
    return evidence;
}

inline bool NeedsTargetRecreate(uint32_t currentWidth, uint32_t currentHeight,
                                uint32_t newWidth, uint32_t newHeight,
                                bool completionConfirmed) noexcept
{
    if (newWidth == 0 || newHeight == 0)
        return false;
    if (currentWidth == newWidth && currentHeight == newHeight)
        return false;
    return completionConfirmed;
}

inline bool LevelNamesEqual(const char* left, const char* right) noexcept
{
    if (left == nullptr || right == nullptr)
        return false;
    while (*left && *right)
    {
        unsigned char a = static_cast<unsigned char>(*left);
        unsigned char b = static_cast<unsigned char>(*right);
        if (a >= 'A' && a <= 'Z')
            a = static_cast<unsigned char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<unsigned char>(b - 'A' + 'a');
        if (a != b)
            return false;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

struct SamplePlan
{
    bool sample = false;
    bool first = false;
    bool last = false;
    bool intermediate = false;
};

inline SamplePlan PlanSample(uint64_t acceptedIndex, uint64_t sampleEvery,
                             bool forceLast) noexcept
{
    SamplePlan plan;
    plan.first = acceptedIndex == 1;
    plan.last = forceLast;
    const uint64_t every = sampleEvery == 0 ? 1 : sampleEvery;
    plan.intermediate = acceptedIndex % every == 0;
    plan.sample = plan.first || plan.last || plan.intermediate;
    return plan;
}

// Readback/evidence failure policy. A completed GPU submit without a valid
// readback or without successfully written evidence must not be reported as a
// pixel-verified frame.
inline const char* SampleEvidenceStatus(bool gpuCompleted, bool readbackOk,
                                        bool evidenceIoOk,
                                        bool gpuPixels) noexcept
{
    if (!gpuCompleted)
        return "FAIL";
    if (!readbackOk || !evidenceIoOk)
        return "UNVERIFIED";
    return gpuPixels ? "VISIBLE_DRAW_VERIFIED" : "NO_PIXEL_COVERAGE";
}

// Acceptance is deliberately separate from GPU execution and from D3D12
// validation. Evidence I/O failure makes the result UNVERIFIED, not a fake
// GPU failure; a real GPU/render failure remains FAIL.
inline const char* EvaluateAcceptance(bool gpuOk, const char* validationStatus,
                                      bool evidenceOk,
                                      bool visibilityOk) noexcept
{
    if (!gpuOk || validationStatus == nullptr ||
        std::strcmp(validationStatus, "FAIL") == 0)
        return "FAIL";
    if (std::strcmp(validationStatus, "PASS") != 0 || !evidenceOk ||
        !visibilityOk)
        return "UNVERIFIED";
    return "PASS";
}

inline int AcceptanceExitCode(const char* status) noexcept
{
    if (status != nullptr && std::strcmp(status, "PASS") == 0)
        return 0;
    if (status != nullptr && std::strcmp(status, "UNVERIFIED") == 0)
        return 3;
    return 1;
}

inline const char* FrameStateName(FrameState state) noexcept
{
    switch (state)
    {
    case FrameState::Idle: return "Idle";
    case FrameState::Recording: return "Recording";
    case FrameState::Closed: return "Closed";
    case FrameState::Submitted: return "Submitted";
    case FrameState::Completed: return "Completed";
    case FrameState::Failed: return "Failed";
    case FrameState::CompletionUnknown: return "CompletionUnknown";
    }
    return "Unknown";
}

} // namespace b3b2b
