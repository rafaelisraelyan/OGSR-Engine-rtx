#pragma once

#include <cstdint>
#include <limits>

namespace audit
{
// ID3D12Fence::GetCompletedValue returns UINT64_MAX when the device has been
// removed. The value must be classified before any >= comparison, otherwise a
// lost device looks like a completed fence.
enum class FenceCompletion
{
    Pending,
    Completed,
    DeviceRemoved,
};

inline FenceCompletion ClassifyFenceCompletion(uint64_t completed, uint64_t expected)
{
    if (completed == std::numeric_limits<uint64_t>::max())
        return FenceCompletion::DeviceRemoved;
    if (completed >= expected)
        return FenceCompletion::Completed;
    return FenceCompletion::Pending;
}

enum class ValidationVerdict
{
    NotRequested,
    Pass,
    Fail,
    Inconclusive,
};

struct ValidationStatus
{
    bool requested = false;
    bool infrastructureOk = true;
    bool performed = false;
    ValidationVerdict verdict = ValidationVerdict::NotRequested;
};

// An explicit validation request must never be reported as a PASS when the
// debug layer / GPU-based validation infrastructure is missing.
inline ValidationStatus EvaluateValidationStatus(
    bool debugRequested,
    bool debugEnabled,
    bool infoQueueAvailable,
    bool gpuRequested,
    bool gpuEnabled,
    uint64_t errorCount)
{
    ValidationStatus status{};
    status.requested = debugRequested || gpuRequested;
    const bool diagnosticsReady = debugEnabled && infoQueueAvailable;
    status.infrastructureOk =
        (!debugRequested || diagnosticsReady) &&
        (!gpuRequested || (gpuEnabled && diagnosticsReady));
    status.performed = diagnosticsReady && (!gpuRequested || gpuEnabled);

    if (!status.requested)
    {
        status.verdict = ValidationVerdict::NotRequested;
        return status;
    }
    if (!status.infrastructureOk)
    {
        status.verdict = ValidationVerdict::Inconclusive;
        return status;
    }
    if (errorCount != 0)
    {
        status.verdict = ValidationVerdict::Fail;
        return status;
    }

    status.verdict = ValidationVerdict::Pass;
    return status;
}
} // namespace audit
