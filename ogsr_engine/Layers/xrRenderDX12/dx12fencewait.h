#pragma once

#include <cstdint>

// F3.5.5-H1: платформенно-независимая логика ожидания fence.
//
// Helper не знает ни о D3D12, ни о Win32: адаптер в CHW12.cpp передаёт
// три операции (прочитать completed value, зарегистрировать событие,
// дождаться события). Это позволяет воспроизвести stale/spurious wake
// управляемым stub'ом без реального GPU и без искусственного TDR.
namespace dx12fence12
{
inline constexpr uint64_t kRemovedMarker = 0xFFFFFFFFFFFFFFFFull;

// Результат WaitForSingleObject-подобного ожидания.
enum class WaitStatus : uint8_t
{
    Signaled,
    Timeout,
    Failed
};

// Полный исход ожидания. Timeout, WaitFailed, ArmFailed и SpuriousWake
// намеренно различаются: это разные диагнозы, и только Timeout означает
// «CPU перестал ждать», а не «GPU завершил работу».
enum class Outcome : uint8_t
{
    Completed,
    Timeout,
    WaitFailed,
    ArmFailed,
    SpuriousWake,
    DeviceRemoved
};

struct Result
{
    Outcome outcome = Outcome::WaitFailed;
    uint64_t completedBefore = 0;
    uint64_t completedAfter = 0;
};

inline const char* ToString(Outcome outcome)
{
    switch (outcome)
    {
    case Outcome::Completed:     return "Completed";
    case Outcome::Timeout:       return "Timeout";
    case Outcome::WaitFailed:    return "WaitFailed";
    case Outcome::ArmFailed:     return "ArmFailed";
    case Outcome::SpuriousWake:  return "SpuriousWake";
    case Outcome::DeviceRemoved: return "DeviceRemoved";
    }
    return "Unknown";
}

// TFence должен предоставить:
//   uint64_t GetCompletedValue();
//   bool SetEventOnCompletion(uint64_t value);
//   WaitStatus Wait(uint32_t timeoutMs);
//
// Инвариант: успех возможен только при completed >= requested. Событие
// может быть signaled устаревшей регистрацией предыдущего ожидания, поэтому
// одного WAIT_OBJECT_0 недостаточно.
template <typename TFence>
Result WaitForValue(TFence& fence, uint64_t value, uint32_t timeoutMs)
{
    Result result;
    result.completedBefore = fence.GetCompletedValue();
    result.completedAfter = result.completedBefore;

    if (result.completedBefore == kRemovedMarker)
    {
        result.outcome = Outcome::DeviceRemoved;
        return result;
    }

    if (result.completedBefore >= value)
    {
        result.outcome = Outcome::Completed;
        return result;
    }

    if (!fence.SetEventOnCompletion(value))
    {
        result.outcome = Outcome::ArmFailed;
        return result;
    }

    const WaitStatus wait = fence.Wait(timeoutMs);
    if (wait == WaitStatus::Timeout)
    {
        result.outcome = Outcome::Timeout;
        return result;
    }
    if (wait != WaitStatus::Signaled)
    {
        result.outcome = Outcome::WaitFailed;
        return result;
    }

    // Путь после пробуждения. UINT64_MAX — маркер removed; подтверждение
    // device removal выполняет CHW12 через GetDeviceRemovedReason().
    // Значение меньше запрошенного означает stale/spurious wake и не
    // является успехом.
    result.completedAfter = fence.GetCompletedValue();
    if (result.completedAfter == kRemovedMarker)
    {
        result.outcome = Outcome::DeviceRemoved;
        return result;
    }
    if (result.completedAfter < value)
    {
        result.outcome = Outcome::SpuriousWake;
        return result;
    }

    result.outcome = Outcome::Completed;
    return result;
}

// CPU-only self-test, реализация в dx12fencewait_tests.cpp.
bool RunFenceWaitTests();

} // namespace dx12fence12
