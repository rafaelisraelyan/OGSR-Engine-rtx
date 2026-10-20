#pragma once

#include <cstdint>

// F3.0: фактическое состояние GPU на момент решения об освобождении
// ресурсов. Каждое значение опирается на проверяемый факт, а не на
// предположение о скорости завершения процесса.
enum class GpuLifetime : uint8_t
{
    // GPU не получал команд: к памяти ресурсов не обращается никто.
    // Освобождение безопасно.
    NoWorkSubmitted,

    // Fence completion подтверждён: GPU завершил всю работу, читавшую
    // ресурсы. Освобождение безопасно.
    WorkCompleted,

    // GetDeviceRemovedReason() вернул код, отличный от S_OK: устройство
    // потеряно, fence находится в состоянии UINT64_MAX, команд не
    // выполняется. Освобождение безопасно.
    // Это констатация состояния объекта устройства, не утверждение о железе.
    DeviceRemoved,

    // Состояние неизвестно: GPU может читать ресурсы прямо сейчас.
    // Освобождение небезопасно, требуется карантин.
    Unknown
};

// F3.5.5-H1: чистая классификация lifetime, отделённая от D3D12 device.
// Главный инвариант: WorkCompleted возможен только при подтверждённом
// drain. Неуспешное/неизвестное завершение никогда не становится
// WorkCompleted.
constexpr GpuLifetime ClassifyGpuLifetime(bool workSubmitted,
                                          bool synchronizationAvailable,
                                          bool drainSucceeded,
                                          bool deviceRemovedConfirmed)
{
    if (!workSubmitted)
        return GpuLifetime::NoWorkSubmitted;
    if (!synchronizationAvailable)
        return GpuLifetime::Unknown;
    if (drainSucceeded)
        return GpuLifetime::WorkCompleted;
    if (deviceRemovedConfirmed)
        return GpuLifetime::DeviceRemoved;
    return GpuLifetime::Unknown;
}
