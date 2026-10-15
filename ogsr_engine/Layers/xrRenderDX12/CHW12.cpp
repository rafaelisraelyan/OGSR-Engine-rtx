#include "stdafx.h"
#include "CHW12.h"
#include "dx12log.h"

#include <cstdlib>
#include <dxgidebug.h>

static const float kClearColor[4] = {0.07f, 0.16f, 0.32f, 1.0f};

// DXGI_DEBUG_ALL из dxgidebug.h — extern-объявление, требующее dxguid.lib.
// Для smoke-целей достаточно локального значения: линковка остаётся минимальной.
static const GUID kDxgiDebugAll = {
    0xe48ae283, 0xda80, 0x490b, {0x87, 0xe6, 0x43, 0xe9, 0xa9, 0xcf, 0xda, 0x08}};

// GPU-Based Validation можно отключить define'ом при сборке: она заметно
// замедляет кадры, и на слабых адаптерах smoke-тест без неё информативнее.
#ifndef OGSR_DX12_DISABLE_GBV
#define OGSR_DX12_ENABLE_GBV 1
#else
#define OGSR_DX12_ENABLE_GBV 0
#endif

CHW12::~CHW12()
{
    Shutdown();
}

// Debug Layer + DRED. Вызывается до создания устройства: позднее включение
// валидации не применяется к уже созданному device.
// Провал НЕ является фатальным: smoke должен запускаться и без validation,
// иначе отлаживать стало бы нечем на машинах без Graphics Tools.
bool CHW12::EnableDebugLayer()
{
    ComPtr<ID3D12Debug> debug;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    {
        dx12log::Write("[DX12] D3D12GetDebugInterface failed: debug layer unavailable, continuing");
        return false;
    }

    debug->EnableDebugLayer();
    m_debugLayerEnabled = true;

#if OGSR_DX12_ENABLE_GBV
    ComPtr<ID3D12Debug1> debug1;
    if (SUCCEEDED(debug.As(&debug1)))
        debug1->SetEnableGPUBasedValidation(TRUE);
#endif

    // DRED: авто-breadcrumbs и page-fault вывод выживают device removal,
    // без них причина падения остаётся неизвестной. Недоступность интерфейса
    // не должна мешать запуску.
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))))
    {
        dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dx12log::Write("[DX12] DRED forced on (auto-breadcrumbs + page fault)");
    }
    else
    {
        dx12log::Write("[DX12] DRED settings unavailable: device removal diagnostics will be limited");
    }

    dx12log::Write("[DX12] Debug layer enabled, GPU-based validation=%d", OGSR_DX12_ENABLE_GBV);
    m_stage = Stage::DebugLayer;
    return true;
}

bool CHW12::Initialize(HWND hwnd, uint32_t width, uint32_t height)
{
    if (m_initialized)
    {
        dx12log::Write("[DX12] Initialize called twice, refusing");
        return false;
    }

    // Новая попытка инициализации (в том числе после неуспешной, когда
    // Shutdown уже приводил объект к исходному состоянию): идемпотентность
    // Shutdown снимается, иначе cleanup после повторной попыки стал бы no-op.
    m_shutdownComplete = false;

    m_width = width;
    m_height = height;

    EnableDebugLayer();

    if (!CreateDevice())
    {
        dx12log::Write("[DX12] Initialize failed at CreateDevice, hr path logged above");
        return false;
    }
    m_stage = Stage::Device;

    if (!CreateSwapChain(hwnd, width, height))
    {
        dx12log::Write("[DX12] Initialize failed at CreateSwapChain");
        return false;
    }
    m_stage = Stage::SwapChain;

    if (!CreateRenderTargetViews())
    {
        dx12log::Write("[DX12] Initialize failed at CreateRenderTargetViews");
        return false;
    }
    m_stage = Stage::RenderTargetViews;

    if (!CreateFrameResources())
    {
        dx12log::Write("[DX12] Initialize failed at CreateFrameResources");
        return false;
    }
    m_stage = Stage::FrameResources;

    if (!CreateSynchronization())
    {
        dx12log::Write("[DX12] Initialize failed at CreateSynchronization");
        return false;
    }

    m_stage = Stage::Ready;
    m_initialized = true;
    dx12log::Write("[DX12] Initialize complete: %ux%u, %u frames in flight",
                   width, height, FRAME_COUNT);
    return true;
}

bool CHW12::CreateDevice()
{
    ComPtr<IDXGIFactory6> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr))
    {
        dx12log::Write("[DX12] CreateDXGIFactory1 failed, hr=0x%08X", static_cast<unsigned>(hr));
        return false;
    }

    // HRESULT проверяется, программный адаптер (WARP) отсекается по флагу,
    // иначе smoke-тест тихо уходит на CPU-рендер.
    ComPtr<IDXGIAdapter1> adapter;
    bool adapterFound = false;
    for (UINT i = 0; ; ++i)
    {
        ComPtr<IDXGIAdapter1> candidate;
        hr = factory->EnumAdapterByGpuPreference(
            i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(hr))
        {
            dx12log::Write("[DX12] EnumAdapterByGpuPreference(%u) failed, hr=0x%08X",
                           i, static_cast<unsigned>(hr));
            return false;
        }

        DXGI_ADAPTER_DESC1 desc = {};
        if (FAILED(candidate->GetDesc1(&desc)))
            continue;
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        adapter = candidate;
        adapterFound = true;
        break;
    }

    if (!adapterFound)
    {
        dx12log::Write("[DX12] No hardware adapter found (WARP excluded)");
        return false;
    }

    DXGI_ADAPTER_DESC1 desc1 = {};
    if (FAILED(adapter->GetDesc1(&desc1)))
        return false;

    dx12log::WriteWide(L"[DX12] GPU: %s (vendor: %04X, device: %04X)",
                       desc1.Description, desc1.VendorId, desc1.DeviceId);

    hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
    if (FAILED(hr))
    {
        dx12log::Write("[DX12] D3D12CreateDevice failed, hr=0x%08X", static_cast<unsigned>(hr));
        return false;
    }

    dx12log::Write("[DX12] Device created, feature level: 11.0");

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

    hr = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
    if (FAILED(hr))
    {
        dx12log::Write("[DX12] CreateCommandQueue failed, hr=0x%08X", static_cast<unsigned>(hr));
        return false;
    }

    // Info queue: validation-сообщения D3D12 без него остаются только в
    // Output отладчика и теряются при запуске без него.
    if (m_debugLayerEnabled)
        m_device.As(&m_infoQueue);

    dx12log::Write("[DX12] Command queue created");
    return true;
}

bool CHW12::CreateSwapChain(HWND hwnd, uint32_t width, uint32_t height)
{
    ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr))
        return false;

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = BACKBUFFER_FORMAT;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = FRAME_COUNT;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags = 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    hr = factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &swapChain1);
    if (FAILED(hr))
    {
        dx12log::Write("[DX12] CreateSwapChainForHwnd failed, hr=0x%08X", static_cast<unsigned>(hr));
        return false;
    }

    hr = swapChain1.As(&m_swapChain);
    if (FAILED(hr))
        return false;

    // Alt+Enter переводит swapchain в exclusive fullscreen, который этот
    // bootstrap не умеет обрабатывать: подавляем переключатель.
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    dx12log::Write("[DX12] Swapchain created: %ux%u, %u backbuffers", width, height, FRAME_COUNT);
    return true;
}

bool CHW12::CreateRenderTargetViews()
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = FRAME_COUNT;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    HRESULT hr = m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_rtvHeap));
    if (FAILED(hr))
    {
        dx12log::Write("[DX12] CreateDescriptorHeap failed, hr=0x%08X", static_cast<unsigned>(hr));
        return false;
    }

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < FRAME_COUNT; ++i)
    {
        hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_frameResources[i].renderTarget));
        if (FAILED(hr))
        {
            dx12log::Write("[DX12] GetBuffer(%u) failed, hr=0x%08X", i, static_cast<unsigned>(hr));
            return false;
        }

        m_device->CreateRenderTargetView(
            m_frameResources[i].renderTarget.Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_rtvDescriptorSize;
    }

    dx12log::Write("[DX12] RTV descriptors created");
    return true;
}

// Аллокатор и командный лист создаются один раз на слот кадра и
// переиспользуются: аллокатор нельзя сбрасывать, пока GPU не закончил с ним.
bool CHW12::CreateFrameResources()
{
    for (uint32_t i = 0; i < FRAME_COUNT; ++i)
    {
        HRESULT hr = m_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_frameResources[i].commandAllocator));
        if (FAILED(hr))
        {
            dx12log::Write("[DX12] CreateCommandAllocator(%u) failed, hr=0x%08X",
                           i, static_cast<unsigned>(hr));
            return false;
        }

        hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         m_frameResources[i].commandAllocator.Get(), nullptr,
                                         IID_PPV_ARGS(&m_frameResources[i].commandList));
        if (FAILED(hr))
        {
            dx12log::Write("[DX12] CreateCommandList(%u) failed, hr=0x%08X",
                           i, static_cast<unsigned>(hr));
            return false;
        }

        // Создание возвращает лист в открытом состоянии; закрываем, чтобы
        // Reset() в RenderFrame() был первым обращением к нему.
        hr = m_frameResources[i].commandList->Close();
        if (FAILED(hr))
            return false;
    }

    dx12log::Write("[DX12] Command allocators and lists created");
    return true;
}

bool CHW12::CreateSynchronization()
{
    HRESULT hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr))
    {
        dx12log::Write("[DX12] CreateFence failed, hr=0x%08X", static_cast<unsigned>(hr));
        return false;
    }

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent)
    {
        dx12log::Write("[DX12] CreateEvent failed, GetLastError=%lu", GetLastError());
        return false;
    }

    dx12log::Write("[DX12] Fence and event created");
    return true;
}

// timedOut отделяет зависший GPU от сбоя API: это разные диагнозы, и
// treating timeout как device removal было бы неверным утверждением.
// Значение UINT64_MAX от GetCompletedValue означает device removal, а не
// «GPU всё завершил». Microsoft: при removed device fence возвращает
// UINT64_MAX, поэтому сравнение >= value ложно принимало бы потерю
// устройства за успешное завершение. Проверяем оба пути: до
// SetEventOnCompletion и после пробуждения события.
static constexpr uint64_t kFenceRemovedMarker = 0xFFFFFFFFFFFFFFFFull;

bool CHW12::CheckFenceForRemoval(uint64_t completed, uint64_t value, const wchar_t* stage)
{
    if (completed != kFenceRemovedMarker)
        return true;

    dx12log::WriteWide(L"[DX12] %s: GetCompletedValue returned UINT64_MAX, device removed "
                       L"(waiting for %llu)", stage, static_cast<unsigned long long>(value));

    if (m_device)
    {
        const HRESULT reason = m_device->GetDeviceRemovedReason();
        dx12log::Write("[DX12] GetDeviceRemovedReason: 0x%08X", static_cast<unsigned>(reason));
    }

    return false;
}

bool CHW12::WaitForFenceValue(uint64_t value, bool& timedOut)
{
    timedOut = false;

    if (!m_fence || !m_fenceEvent)
    {
        dx12log::Write("[DX12] WaitForFenceValue: fence or event missing");
        return false;
    }

    // Путь 1: до SetEventOnCompletion. UINT64_MAX здесь означал бы, что
    // устройство уже потеряно, и ожидание было бы ложным успехом.
    const uint64_t completedBefore = m_fence->GetCompletedValue();
    if (!CheckFenceForRemoval(completedBefore, value, L"WaitForFenceValue(before wait)"))
        return false;

    if (completedBefore >= value)
        return true;

    HRESULT hr = m_fence->SetEventOnCompletion(value, m_fenceEvent);
    if (FAILED(hr))
    {
        dx12log::Write("[DX12] SetEventOnCompletion(%llu) failed, hr=0x%08X",
                       static_cast<unsigned long long>(value), static_cast<unsigned>(hr));
        return false;
    }

    // Таймаут вместо INFINITE: повисший GPU должен давать диагностику,
    // а не молча подвешивать приложение.
    const DWORD wait = WaitForSingleObject(m_fenceEvent, GPU_WAIT_TIMEOUT_MS);
    if (wait == WAIT_TIMEOUT)
    {
        timedOut = true;
        dx12log::Write("[DX12] GPU wait timed out after %lu ms on fence value %llu",
                       GPU_WAIT_TIMEOUT_MS, static_cast<unsigned long long>(value));
        return false;
    }

    if (wait != WAIT_OBJECT_0)
    {
        dx12log::Write("[DX12] WaitForSingleObject returned %lu, GetLastError=%lu",
                       wait, GetLastError());
        return false;
    }

    // Путь 2: после пробуждения. Событие может быть сигнализировано именно
    // потому, что устройство удалено, и тогда GetCompletedValue равен
    // UINT64_MAX. Проверка обязательна: без неё возвращался бы true.
    const uint64_t completedAfter = m_fence->GetCompletedValue();
    if (!CheckFenceForRemoval(completedAfter, value, L"WaitForFenceValue(after wait)"))
        return false;

    return true;
}

bool CHW12::WaitForGpu()
{
    if (!m_fence || !m_commandQueue)
        return false;

    // До первой отправки команд GPU нечего ждать, а SetEventOnCompletion
    // на fence, который никто не сигналил, завис бы до таймаута.
    if (!m_gpuWorkSubmitted)
        return true;

    // Каноничная идиома: сигналим НОВОЕ значение и ждём именно его.
    const uint64_t value = ++m_fenceValue;

    const HRESULT hr = m_commandQueue->Signal(m_fence.Get(), value);
    if (FAILED(hr))
    {
        dx12log::Write("[DX12] Signal(%llu) failed, hr=0x%08X: not waiting on unsignalled fence",
                       static_cast<unsigned long long>(value), static_cast<unsigned>(hr));
        return false;
    }

    bool timedOut = false;
    if (!WaitForFenceValue(value, timedOut))
        return false;

    return true;
}

void CHW12::MoveToNextFrame()
{
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

// Печатает накопившиеся validation-сообщения. Severity различается:
// CORRUPTION и ERROR — фатальные для корректности кадра, WARNING и INFO —
// диагностика. Без этого при запуске без отладчика ошибки D3D12 не видны.
void CHW12::DrainInfoQueue()
{
    if (!m_infoQueue)
        return;

    const UINT64 count = m_infoQueue->GetNumStoredMessages();
    if (count == 0)
        return;

    dx12log::Write("[DX12] InfoQueue: %llu stored message(s)", static_cast<unsigned long long>(count));

    for (UINT64 i = 0; i < count; ++i)
    {
        SIZE_T size = 0;
        m_infoQueue->GetMessage(i, nullptr, &size);

        D3D12_MESSAGE* msg = static_cast<D3D12_MESSAGE*>(malloc(size));
        if (!msg)
            break;

        if (SUCCEEDED(m_infoQueue->GetMessage(i, msg, &size)))
        {
            const char* severity = "UNKNOWN";
            switch (msg->Severity)
            {
            case D3D12_MESSAGE_SEVERITY_CORRUPTION: severity = "CORRUPTION"; break;
            case D3D12_MESSAGE_SEVERITY_ERROR:      severity = "ERROR";      break;
            case D3D12_MESSAGE_SEVERITY_WARNING:    severity = "WARNING";    break;
            case D3D12_MESSAGE_SEVERITY_INFO:       severity = "INFO";       break;
            case D3D12_MESSAGE_SEVERITY_MESSAGE:    severity = "MESSAGE";    break;
            default: break;
            }

            dx12log::Write("[DX12][validation][%s] %s",
                           severity,
                           msg->pDescription ? msg->pDescription : "?");
        }

        free(msg);
    }

    m_infoQueue->ClearStoredMessages();
}

// Вызывается ТОЛЬКО при подтверждённой аппаратной проблеме: DEVICE_REMOVED,
// DEVICE_RESET или провал синхронизации, после которого состояние GPU
// неизвестно. Обычная ошибка вызова и таймаут сюда не попадают.
void CHW12::ReportGpuFailure(const wchar_t* stage, HRESULT hr)
{
    m_deviceRemoved = true;

    dx12log::WriteWide(L"[DX12] FATAL at %s, hr=0x%08X: GPU state unknown, stopping",
                       stage, static_cast<unsigned>(hr));

    if (!m_device)
        return;

    const HRESULT reason = m_device->GetDeviceRemovedReason();
    dx12log::Write("[DX12] GetDeviceRemovedReason: 0x%08X", static_cast<unsigned>(reason));

    // DRED: page-fault вывод сохраняется после removal и указывает на ресурс.
    ComPtr<ID3D12DeviceRemovedExtendedData> dred;
    if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&dred))))
    {
        D3D12_DRED_PAGE_FAULT_OUTPUT pageFault = {};
        if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pageFault)))
            dx12log::Write("[DX12] DRED page fault VA: 0x%016llX",
                           static_cast<unsigned long long>(pageFault.PageFaultVA));
    }

    DrainInfoQueue();
}

bool CHW12::RenderFrame()
{
    if (!m_initialized)
        return false;

    if (m_deviceRemoved)
        return false;

    FrameResource& frame = m_frameResources[m_frameIndex];

    // Ждём завершения именно этого слота: его аллокатор и лист не должны
    // трогаться CPU, пока GPU читает записанные в них команды.
    if (frame.fenceValue != 0)
    {
        bool timedOut = false;
        if (!WaitForFenceValue(frame.fenceValue, timedOut))
        {
            // Таймаут и сбой API оба означают, что аллокатор нельзя сбрасывать:
            // GPU может всё ещё писать в его память.
            ReportGpuFailure(L"WaitForFenceValue", DXGI_ERROR_DEVICE_REMOVED);
            return false;
        }
    }

    HRESULT hr = frame.commandAllocator->Reset();
    if (FAILED(hr))
    {
        dx12log::Write("[DX12] allocator Reset failed, hr=0x%08X", static_cast<unsigned>(hr));
        ReportGpuFailure(L"allocator Reset", hr);
        return false;
    }

    ID3D12GraphicsCommandList* cmd = frame.commandList.Get();
    hr = cmd->Reset(frame.commandAllocator.Get(), nullptr);
    if (FAILED(hr))
    {
        dx12log::Write("[DX12] command list Reset failed, hr=0x%08X", static_cast<unsigned>(hr));
        ReportGpuFailure(L"command list Reset", hr);
        return false;
    }

    D3D12_RESOURCE_BARRIER toRenderTarget = {};
    toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    toRenderTarget.Transition.pResource = frame.renderTarget.Get();
    toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cmd->ResourceBarrier(1, &toRenderTarget);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += static_cast<SIZE_T>(m_frameIndex) * m_rtvDescriptorSize;
    cmd->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);

    D3D12_RESOURCE_BARRIER toPresent = toRenderTarget;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    cmd->ResourceBarrier(1, &toPresent);

    hr = cmd->Close();
    if (FAILED(hr))
    {
        dx12log::Write("[DX12] command list Close failed, hr=0x%08X", static_cast<unsigned>(hr));
        ReportGpuFailure(L"command list Close", hr);
        return false;
    }

    ID3D12CommandList* lists[] = {cmd};
    m_commandQueue->ExecuteCommandLists(1, lists);
    m_gpuWorkSubmitted = true;

    // Present — единственный источник DEVICE_REMOVED/DEVICE_RESET и единственное
    // место, откуда приходит DXGI_STATUS_OCCLUDED для свёрнутого окна.
    hr = m_swapChain->Present(1, 0);

    const bool occluded = (hr == DXGI_STATUS_OCCLUDED);

    // Обычная ошибка Present: кадр не начинается заново. Не утверждаем
    // device removal — hr говорит только о présent'е.
    if (FAILED(hr) && !occluded)
    {
        dx12log::Write("[DX12] Present failed, hr=0x%08X", static_cast<unsigned>(hr));

        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            ReportGpuFailure(L"Present", hr);

        return false;
    }

    // Сигналим fence и при occlusion: слот должен разблокироваться после
    // восстановления окна, иначе он зависнет навсегда. При провале Signal
    // аллокатор НЕ сбрасывается в следующем кадре — мы остановимся здесь.
    frame.fenceValue = ++m_fenceValue;
    const HRESULT sigHr = m_commandQueue->Signal(m_fence.Get(), frame.fenceValue);
    if (FAILED(sigHr))
    {
        dx12log::Write("[DX12] Signal failed after Present, hr=0x%08X", static_cast<unsigned>(sigHr));
        ReportGpuFailure(L"Signal", sigHr);
        return false;
    }

    if (occluded)
    {
        m_occluded = true;
        MoveToNextFrame();
        return true;
    }

    m_occluded = false;
    MoveToNextFrame();
    DrainInfoQueue();
    return true;
}

bool CHW12::Resize(uint32_t width, uint32_t height)
{
    if (!m_initialized || m_deviceRemoved)
        return false;

    // Нулевой размер — это WM_SIZE от свёрнутого окна, а не ошибка.
    if (width == 0 || height == 0)
        return true;

    if (width == m_width && height == m_height)
        return true;

    dx12log::Write("[DX12] Resize requested: %ux%u -> %ux%u", m_width, m_height, width, height);

    // Провал ожидания GPU: backbuffer'ы не отпускаем, состояние неизвестно.
    if (!WaitForGpu())
    {
        dx12log::Write("[DX12] Resize aborted: WaitForGpu failed, backbuffers kept");
        ReportGpuFailure(L"Resize/WaitForGpu", E_FAIL);
        return false;
    }

    // GPU завершён: все слоты безопасны для пересоздания. Сбрасываем
    // per-frame fence tracking только теперь, после подтверждённого drain.
    for (uint32_t i = 0; i < FRAME_COUNT; ++i)
        m_frameResources[i].fenceValue = 0;

    // Освобождаем командные листы. Close() здесь НЕ вызывается: листы уже
    // закрыты (CreateFrameResources закрывает при создании, RenderFrame
    // закрывает в конце каждого кадра), а повторный Close() по Microsoft Learn
    // возвращает E_FAIL. Освобождение COM-ссылки на лист убирает косвенную
    // ссылку на backbuffers, которую лист держит через pResource барьеров.
    // Аллокаторы освобождаем вместе с листами и пересоздаём ниже: это проще
    // и надёжнее, чем пытаться угадать состояние Reset/Close для каждого.
    for (uint32_t i = 0; i < FRAME_COUNT; ++i)
    {
        m_frameResources[i].commandList.Reset();
        m_frameResources[i].commandAllocator.Reset();
        m_frameResources[i].renderTarget.Reset();
    }

    // Старый RTV-хип тоже держит ссылки на освобождённые буферы через
    // дескрипторы: сбрасываем до ResizeBuffers, а не после.
    m_rtvHeap.Reset();

    HRESULT hr = m_swapChain->ResizeBuffers(
        FRAME_COUNT, width, height, BACKBUFFER_FORMAT, 0);
    if (FAILED(hr))
    {
        // ResizeBuffers провалился при уже отпущенных backbuffer'ах: состояние
        // несогласовано, единственный безопасный ответ — остановить кадры.
        // Освобождённые буферы больше никем не используются: рендеринг прекращён.
        dx12log::Write("[DX12] ResizeBuffers failed, hr=0x%08X: rendering stopped, "
                       "released backbuffers are not referenced anymore",
                       static_cast<unsigned>(hr));
        ReportGpuFailure(L"ResizeBuffers", hr);
        return false;
    }

    m_width = width;
    m_height = height;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Новый RTV-хип на новые буферы: старые дескрипторы уже уничтожены.
    if (!CreateRenderTargetViews())
    {
        // Провал после успешного ResizeBuffers: буферы новые, но без RTV
        // рендерить нельзя. Останавливаемся, не трогая освобождённое.
        dx12log::Write("[DX12] CreateRenderTargetViews failed after resize: rendering stopped");
        ReportGpuFailure(L"CreateRenderTargetViews", E_FAIL);
        return false;
    }

    // Аллокаторы и листы пересоздаются под новые RTV: пустые ComPtr после
    // Reset() выше. CreateFrameResources закрывает лист сразу после создания,
    // поэтому RenderFrame получит закрытый лист и Reset() будет первым
    // обращением к нему, как и при Initialize.
    if (!CreateFrameResources())
    {
        dx12log::Write("[DX12] CreateFrameResources failed after resize: rendering stopped");
        ReportGpuFailure(L"CreateFrameResources", E_FAIL);
        return false;
    }

    dx12log::Write("[DX12] Resized to %ux%u", width, height);
    DrainInfoQueue();
    return true;
}

// Идемпотентный shutdown. Каждый Reset() на ComPtr сам проверяет указатель,
// поэтому частично созданное состояние разбирается корректно: несуществующие
// объекты просто отсутствуют. GPU-ожидание выполняется только если очередь,
// fence и event существуют И GPU действительно получал команды.
void CHW12::Shutdown()
{
    // Повторный вызов (деструктор после явного Shutdown) — no-op.
    // Идемпотентность держится на отдельном флаге, а НЕ на m_stage: стадия
    // отражает только успешные шаги Initialize, а провал внутри CreateDevice
    // (например, D3D12CreateDevice прошёл, а CreateCommandQueue упал) при
    // предварительно проваленном EnableDebugLayer оставляет m_stage == None
    // при уже созданном устройстве. Ранний выход по стадии терял бы его.
    if (m_shutdownComplete)
        return;

    m_shutdownComplete = true;

    const bool canWaitForGpu = m_commandQueue && m_fence && m_fenceEvent && m_gpuWorkSubmitted;

    if (canWaitForGpu)
    {
        if (WaitForGpu())
        {
            dx12log::Write("[DX12] GPU drained before shutdown");
        }
        else
        {
            // Неуспешная синхронизация НЕ считается успешной: объекты
            // освобождаем, но честно пишем, что гарантии нет.
            dx12log::Write("[DX12] WARNING: GPU drain failed, releasing resources without "
                           "confirmation that GPU finished");
        }
    }

    for (uint32_t i = 0; i < FRAME_COUNT; ++i)
    {
        m_frameResources[i].renderTarget.Reset();
        m_frameResources[i].commandList.Reset();
        m_frameResources[i].commandAllocator.Reset();
    }

    m_rtvHeap.Reset();
    m_commandQueue.Reset();
    m_swapChain.Reset();

    if (m_debugLayerEnabled)
    {
        // ReportLiveObjects до сброса device: после Reset() объекты уже
        // уничтожены и отчёт был бы пустым.
        ComPtr<IDXGIDebug1> dxgiDebug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
            dxgiDebug->ReportLiveObjects(kDxgiDebugAll, DXGI_DEBUG_RLO_ALL);
    }

    m_infoQueue.Reset();
    m_device.Reset();
    m_fence.Reset();

    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }

    m_initialized = false;
    m_stage = Stage::None;

    // Полный сброс состояния: объект после Shutdown() должен быть
    // неотличим от свежесозданного, включая частичную инициализацию.
    // Без этого повторный Initialize() унаследовал бы мусорные fenceValue
    // (WaitForFenceValue ждал бы значения, которого fence уже не помнит),
    // старый frameIndex и размеры.
    for (uint32_t i = 0; i < FRAME_COUNT; ++i)
        m_frameResources[i].fenceValue = 0;

    m_frameIndex = 0;
    m_fenceValue = 0;
    m_gpuWorkSubmitted = false;
    m_width = 0;
    m_height = 0;
    m_occluded = false;
    m_deviceRemoved = false;
    m_debugLayerEnabled = false;
    m_rtvDescriptorSize = 0;
    m_shutdownComplete = true;

    dx12log::Write("[DX12] Shutdown complete");
}
