#pragma once

#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "dx12camera.h"
#include "dx12mesh.h"
#include "dx12quarantine.h"

using Microsoft::WRL::ComPtr;

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

// F2.1-B.1: минимальный DX12 bootstrap для отдельного smoke-исполняемого файла.
// Debug Layer, DRED и явная синхронизация включены: smoke-сборка существует
// именно для того, чтобы validation errors были видны до переноса игрового кадра.
class CHW12
{
public:
    CHW12() = default;
    ~CHW12();

    CHW12(const CHW12&) = delete;
    CHW12& operator=(const CHW12&) = delete;

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
    bool RenderFrame();
    bool Resize(uint32_t width, uint32_t height);

    // Идемпотентный shutdown. Безопасен после частично неуспешного Initialize
    // и при повторных вызовах (деструктор + явный вызов).
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }
    bool IsDeviceRemoved() const { return m_deviceRemoved; }
    bool IsOccluded() const { return m_occluded; }

    // F3.1: управление режимами тестирования.
    void SetTestMode(bool enabled) { m_testMode = enabled; }
    void SetFixedPose(bool enabled) { m_fixedPose = enabled; }
    void UpdateCamera(float timeSeconds) { m_camera.SetOrbit(timeSeconds); }
    bool SetCameraLookAt(DirectX::XMVECTOR eye, DirectX::XMVECTOR target, DirectX::XMVECTOR up)
    {
        return m_camera.SetLookAt(eye, target, up);
    }
    bool SetCameraAspect(float aspect) { return m_camera.SetAspect(aspect); }
    void GetCameraFixedPose(DirectX::XMVECTOR& eye, DirectX::XMVECTOR& target, DirectX::XMVECTOR& up) const
    {
        m_camera.GetFixedPose(eye, target, up);
    }

    // F3.1: результат автоматического depth-теста.
    struct DepthTestResult
    {
        bool passed = false;
        char message[512] = {};
    };

    // F3.1: автоматический depth-test с инкапсуляцией readback.
    // Синхронизирует GPU через fence, читает readback с учётом
    // Offset/RowPitch, проверяет три контрольных пикселя.
    // Возвращает структуру с результатом и диагностикой.
    DepthTestResult VerifyDepthTest();

private:
    static constexpr uint32_t FRAME_COUNT = 3;
    static constexpr DXGI_FORMAT BACKBUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DWORD GPU_WAIT_TIMEOUT_MS = 5000;

    // Стадии инициализации. Каждая следующая возможна только при успехе
    // предыдущей, поэтому shutdown знает, какие объекты существуют.
    enum class Stage : uint8_t
    {
        None = 0,
        DebugLayer,
        Device,
        SwapChain,
        RenderTargetViews,
        FrameResources,
        Fence,
        DepthBuffer,
        Ready
    };

    struct FrameResource
    {
        ComPtr<ID3D12CommandAllocator> commandAllocator;
        ComPtr<ID3D12GraphicsCommandList> commandList;
        ComPtr<ID3D12Resource> renderTarget;
        ComPtr<ID3D12Resource> constantBuffer;
        void* mapped = nullptr;
        uint64_t fenceValue = 0;
    };

    bool CreateDevice();
    bool CreateSwapChain(HWND hwnd, uint32_t width, uint32_t height);
    bool CreateRenderTargetViews();
    bool CreateFrameResources();
    bool CreateSynchronization();
    bool EnableDebugLayer();
    bool CreateDepthBuffer();
    bool CreateReadback();

    // F3.0: определение фактического состояния GPU для решения о lifetime
    // ресурсов. Решение опирается на факты: были ли отправлены команды,
    // доступны ли объекты синхронизации, подтверждено ли завершение fence,
    // что возвращает GetDeviceRemovedReason().
    GpuLifetime DetermineGpuLifetime();

    // Подтверждённая потеря D3D12-устройства по фактическому HRESULT.
    // GetDeviceRemovedReason() возвращает S_OK, если устройство НЕ удалено.
    // m_deviceRemoved для этого не используется: он диагностический.
    bool IsDeviceRemovedConfirmed();

    // Передача владения GPU-ресурсами карантину через ComPtr::Detach().
    // Приватный метод: имеет законный доступ к m_frameResources, чей тип
    // FrameResource является приватным вложенным типом этого класса.
    void QuarantineGpuResources();

    // F3.1: геометрия — два куба с мировыми координатами в вершинах.
    // Отдельный компонент, а не часть CHW12, чтобы владение геометрией
    // и pipeline не смешивалось с владением device/queue/swapchain/fence.
    // Значение-член, а не указатель: ручное new/delete здесь ничего
    // не даёт, деструктор вызовет Release().
    SmokeMesh12 m_mesh;

    // F3.1: камера. Значение-член, как m_mesh: без COM-объектов, только
    // математика. Тестируема на CPU без GPU и переносима в IX-Ray-адаптер.
    SmokeCamera12 m_camera;

    // UINT64_MAX от GetCompletedValue означает device removal. Возвращает
    // false, если ожидание нельзя считать успешным, и логирует причину.
    bool CheckFenceForRemoval(uint64_t completed, uint64_t value, const wchar_t* stage);

    // Ждёт конкретного значения fence. false только при аппаратной ошибке
    // (нет fence/event или провал SetEventOnCompletion); timeout возвращает
    // отдельно, чтобы вызывающий код мог отличить зависание GPU от сбоя API.
    bool WaitForFenceValue(uint64_t value, bool& timedOut);

    // Полный дренаж GPU. Возвращает false, если очередь не смогла поставить
    // сигнал или ожидание не завершилось: в этом случае состояние GPU
    // неизвестно и освобождать ресурсы раньше времени нельзя.
    bool WaitForGpu();

    void MoveToNextFrame();
    void DrainInfoQueue();
    void ReportGpuFailure(const wchar_t* stage, HRESULT hr);

    // Device
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<IDXGISwapChain4> m_swapChain;
    ComPtr<ID3D12InfoQueue> m_infoQueue;

    // Frame resources
    FrameResource m_frameResources[FRAME_COUNT];
    uint32_t m_frameIndex = 0;

    // Descriptors
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    uint32_t m_rtvDescriptorSize = 0;

    // F3.1: depth buffer и DSV heap. Один общий depth buffer на все три
    // слота кадра: CPU в него не пишет никогда, а внутри одной очереди
    // команды выполняются строго в порядке отправки (§7 ТЗ). Барьеров на
    // depth нет: ресурс остаётся в DEPTH_WRITE весь кадр.
    ComPtr<ID3D12Resource> m_depthBuffer;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_dsv = {};
    uint32_t m_dsvDescriptorSize = 0;

    // F3.1: readback buffer для автоматического depth-теста (только тестовый
    // режим). Создаётся и уничтожается при resize вместе с backbuffer,
    // потому что RowPitch и число строк зависят от размера backbuffer.
    ComPtr<ID3D12Resource> m_readbackBuffer;
    void* mappedReadback = nullptr;
    UINT64 m_readbackTotalBytes = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_copyableFootprint = {};

    // Synchronization
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    uint64_t m_fenceValue = 0;

    // State
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    Stage m_stage = Stage::None;
    bool m_initialized = false;
    bool m_deviceRemoved = false;
    bool m_occluded = false;
    bool m_debugLayerEnabled = false;

    // GPU получал команды: до первой отправки WaitForGpu бессмысленен,
    // а SetEventOnCompletion на fence без signal завис бы навсегда.
    bool m_gpuWorkSubmitted = false;

    // Идемпотентность Shutdown. Отдельный флаг, а не m_stage: см. Shutdown().
    // НЕ является терминальным флагом: после штатного Shutdown повторный
    // Initialize разрешён. См. m_terminalGpuFailure.
    bool m_shutdownComplete = false;

    // F3.0: терминальное состояние. Устанавливается, когда GPU-ресурсы
    // переданы карантину из-за неизвестного состояния GPU. В отличие от
    // m_shutdownComplete, не сбрасывается никогда и блокирует повторный
    // Initialize: аллокаторы и командные листы могут ещё читаться GPU.
    bool m_terminalGpuFailure = false;

    // F3.1: режимы тестирования. m_testMode включает readback buffer и
    // автоматический depth-тест. m_fixedPose отключает орбитальную камеру
    // для детерминированных прогонов.
    bool m_testMode = false;
    bool m_fixedPose = false;
};
