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
    // F3.5.5A: источник Draw. Synthetic — прежний benchmark path; Packets —
    // OrderedFrame12 → один owner command list. Packet path несовместим с
    // multi-list recording.
    enum class DrawSource : uint8_t { Synthetic = 0, Packets = 1 };
    bool SetDrawSource(DrawSource source)
    {
        if (m_initialized) return false;
        if (source == DrawSource::Packets && m_multiList) return false;
        m_drawSource = source;
        return true;
    }
    // TEST ONLY: детерминированно портит сцену до snapshot, чтобы проверить
    // validation packet-path без реального traversal/registry.
    enum class PacketTestFault : uint8_t
    {
        None = 0,
        HideAll,
        HideSecond,
        CullSecond,
        Reverse,
        StaleVisual,
        UnsupportedPso,
        UnknownObject,
        InvalidRange,
        OverflowRange,
        Transparent,
        Hud,
        WorldTransform
    };
    void SetPacketTestFault(PacketTestFault fault)
    {
        if (!m_initialized) m_packetTestFault = fault;
    }
    // Режим выбирается до Initialize; legacy остаётся default.
    bool SetMultiList(bool enabled, bool parallel=false)
    {
        if (m_initialized) return false;
        if (enabled && m_drawSource == DrawSource::Packets) return false;
        m_multiList=enabled;
        m_parallelRecording=enabled && parallel;
        return true;
    }
    // F3.5.4A: число draw lists и общее число draw calls. Оба параметра
    // фиксируются до Initialize: от них зависит число пар allocator/list
    // в FrameResource.
    bool SetDrawListCount(uint32_t count);
    void SetDrawCount(uint32_t count) { if (!m_initialized) m_drawCount = count ? count : 1; }
    // GPU-Based Validation применяется при создании device; менять позже
    // бессмысленно.
    void SetGpuValidation(bool enabled) { if (!m_initialized) m_gpuValidationRequested = enabled; }
    // Debug Layer создаётся до device. Отключение обязательно для
    // performance-прогонов: слой меняет и CPU-, и GPU-поведение.
    void SetDebugLayer(bool enabled) { if (!m_initialized) m_debugLayerRequested = enabled; }
    // Только smoke-тест: 0=allocator Reset, 1=list Reset, 2=Record, 3=Close,
    // 4=отказ второго Submit, 5=исключение callback (последние два только parallel).
    void ArmRecordingFailure(unsigned stage, unsigned list) { m_recordFaultStage=stage; m_recordFaultList=list; }
    uint64_t RecordingSubmissions() const { return m_recordFrames; }

    // F3.5.3B TEST ONLY: одноразовый отказ подготовки кадра. Нужен, чтобы
    // проверить восстановление после m_prepFailed: Shutdown → Initialize
    // обязан сбросить флаг, иначе RenderFrame навсегда возвращает false.
    void ArmPreparationFailure() { m_prepFaultArmed = true; }


    // F3.3.0a TEST ONLY — WM_TIMER fault injection.
    //
    // ArmTimerRenderFailure() включает одноразовый отказ, который виден ТОЛЬКО
    // WM_TIMER-пути: RenderFrameFromTimer() в этом случае возвращает false,
    // не обращаясь к device, fence, ресурсам или JobSystem, после чего
    // вызывающий выполняет обычную обработку сбоя. RenderFrame() главного
    // цикла этот флаг не читает, поэтому production-путь не меняется, пока
    // не вызван arm API. Нужен, чтобы проверить обработку сбоя в WM_TIMER
    // без реального device removal/TDR.
    void ArmTimerRenderFailure() { m_timerFaultArmed = true; }
    bool RenderFrameFromTimer();

    // Заимствование: JobSystem должен пережить CHW12.
    void ConfigurePreparation(renderprep12::Mode mode, gr::jobs::JobSystem* jobs) { m_prepMode=mode; m_jobs=jobs; }
    bool PreparationFailed() const { return m_prepFailed; }
    bool Resize(uint32_t width, uint32_t height);

    // Идемпотентный shutdown. Безопасен после частично неуспешного Initialize
    // и при повторных вызовах (деструктор + явный вызов).
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }
    bool IsDeviceRemoved() const { return m_deviceRemoved; }
    bool IsOccluded() const { return m_occluded; }

    // F3.5.4B TEST ONLY: per-frame срезы для внешней статистики. Массив
    // заполняется в RenderFrame без I/O и без логирования; smoke пишет его
    // в CSV только после Shutdown, вне измеряемого участка.
    struct FrameTimingSample
    {
        uint64_t frame = 0;
        double recordingMs = 0;
        double schedulingMs = 0;
        double waitMs = 0;
        double submissionMs = 0;
        double gpuMs = -1;       // -1: timestamp ещё не прочитан
        double intervalMs = -1;  // -1: Present не завершился
    };
    static constexpr uint32_t MAX_TIMING_SAMPLES = 512;
    const FrameTimingSample* GetFrameTimings(uint32_t& count) const
    {
        count = m_timingSampleCount;
        return m_timingSamples;
    }
    uint32_t TimingSamplesDropped() const { return m_timingSamplesDropped; }

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
    // F3.5.5A TEST ONLY: empty visible frame. Проверяет, что весь кадр равен
    // clear color, Draw=0, а центральный depth-регион равен 1.0 после clear.
    void SetBackgroundTest(bool enabled) { if (!m_initialized) m_backgroundTest = enabled; }
    DepthTestResult VerifyBackgroundOnly();

private:
    bool m_multiList=false, m_parallelRecording=false, m_recordFailed=false;
    DrawSource m_drawSource = DrawSource::Synthetic;
    PacketTestFault m_packetTestFault = PacketTestFault::None;
    // F3.5.4A: L — число draw lists, N — всего draw calls за кадр.
    uint32_t m_drawListCount = 2;
    uint32_t m_drawCount = 2;
    uint32_t m_visiblePackets = 0;
    bool m_gpuValidationRequested = true;
    bool m_debugLayerRequested = true;
    // F3.5.4B: кольцевой буфер не нужен — объём ограничен прогоном.
    FrameTimingSample m_timingSamples[MAX_TIMING_SAMPLES] = {};
    uint32_t m_timingSampleCount = 0;
    uint32_t m_timingSamplesDropped = 0;
    // Интервал между кадрами (wall clock). Present(1,0) ограничен vsync,
    // поэтому это диагностика, а не FPS игры.
    LARGE_INTEGER m_lastPresentTick = {};
    double m_frameIntervalMs = 0;
    uint64_t m_frameIntervals = 0;
    unsigned m_recordFaultStage=~0u, m_recordFaultList=~0u;
    uint64_t m_recordFrames=0, m_recordSamples=0;
    uint64_t m_timestampFrequency=0, m_gpuSamples=0;
    double m_gpuMs=0;
    double m_recordMs=0, m_queueSubmitMs=0, m_recordScheduleMs=0, m_recordWaitMs=0;
    bool InjectRecordingFailure(unsigned stage, unsigned list);
    // F3.5.5A: проверка packet binding до первого Reset/Execute. Возвращает
    // nullptr при успехе или статическую причину отказа.
    const char* ValidatePacketFrame(const renderprep12::OrderedFrame12& frame) const;
    renderprep12::Mode m_prepMode=renderprep12::Mode::Serial;
    gr::jobs::JobSystem* m_jobs=nullptr;
    bool m_preparing=false, m_prepFailed=false;
    bool m_timerFaultArmed=false; // F3.3.0a TEST ONLY, см. ArmTimerRenderFailure()
    bool m_prepFaultArmed=false;  // F3.5.3B TEST ONLY, см. ArmPreparationFailure()
    uint64_t m_sceneGeneration=1, m_prepFrames=0, m_prepSamples=0;
    double m_snapshotMs=0, m_serialMs=0, m_parallelMs=0, m_submitMs=0, m_waitMs=0;
    // F3.3.0a: merge (финальная сборка PreparedFrame) и ожидание GPU fence
    // измеряются отдельно от CPU JobGroup::Wait — это разные синхронизации,
    // и гейт F3.3.0 требует видеть их раздельно. gpuFence усредняется по
    // числу ожиданий, а не по числу кадров: первый кадр слота не ждёт fence.
    double m_mergeMs=0, m_gpuFenceMs=0;
    uint64_t m_gpuFenceWaits=0;
    static constexpr uint32_t FRAME_COUNT = 3;
    // F3.5.4A: жёсткая верхняя граница draw lists на слот кадра. Каждая
    // list владеет собственным allocator и переиспользуется только после
    // ожидания fence своего слота; общего пула между слотами нет.
    static constexpr uint32_t MAX_DRAW_LISTS = kMaxDrawLists;
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

    struct RecordingResource {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
    };
    struct FrameResource
    {
        ComPtr<ID3D12CommandAllocator> commandAllocator;
        ComPtr<ID3D12GraphicsCommandList> commandList;
        ComPtr<ID3D12QueryHeap> timestamps;
        ComPtr<ID3D12Resource> timestampReadback;
        bool timestampPending=false, timestampSample=false;
        // F3.5.4B: индекс среза, в который CollectGpuTiming запишет GPU-время.
        int32_t timestampSampleIndex=-1;
        RecordingResource drawRecording[MAX_DRAW_LISTS]; // Draw lists слота
        RecordingResource ownerEnd;                      // End-список кадра
        uint32_t drawListCount = 0;                      // фактическое L кадра
        ComPtr<ID3D12Resource> renderTarget;
        ComPtr<ID3D12Resource> constantBuffer;
        void* mapped = nullptr;
        uint64_t fenceValue = 0;
    };

    void CollectGpuTiming(FrameResource& frame);
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
    UINT64 m_readbackAllocatedBytes = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_copyableFootprint = {};
    // F3.5.5A TEST ONLY: 4x4 depth-регион в хвосте того же readback buffer.
    bool m_backgroundTest = false;
    UINT64 m_backgroundDepthOffset = 0;
    UINT64 m_backgroundDepthBytes = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_backgroundDepthFootprint = {};

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
