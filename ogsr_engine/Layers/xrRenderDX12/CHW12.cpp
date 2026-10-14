#include "stdafx.h"
#include "CHW12.h"

#include <cstdlib>
#include <dxgidebug.h>

static const float kClearColor[4] = {0.07f, 0.16f, 0.32f, 1.0f};

// DXGI_DEBUG_ALL из dxgidebug.h — extern-объявление, требующее dxguid.lib.
// Для smoke-целей достаточно локального значения: линковка остаётся минимальной.
static const GUID kDxgiDebugAll = {
    0xe48ae283, 0xda80, 0x490b, {0x87, 0xe6, 0x43, 0xe9, 0xa9, 0xcf, 0xda, 0x08}};

CHW12::~CHW12()
{
    Shutdown();
}

// Debug Layer + DRED. Вызывается до создания устройства: позднее включение
// валидации не применяется к уже созданному device.
bool CHW12::EnableDebugLayer()
{
    ComPtr<ID3D12Debug> debug;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        return false;

    debug->EnableDebugLayer();

    ComPtr<ID3D12Debug1> debug1;
    if (SUCCEEDED(debug.As(&debug1)))
    {
        debug1->SetEnableGPUBasedValidation(TRUE);
    }

    // DRED: авто-breadcrumbs и page-fault вывод выживают device removal,
    // без них причина падения остаётся неизвестной.
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))))
    {
        dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }

    m_debugLayerEnabled = true;
    wprintf(L"[DX12] Debug layer + GPU-based validation + DRED enabled\n");
    return true;
}

bool CHW12::Initialize(HWND hwnd, uint32_t width, uint32_t height)
{
    if (m_initialized)
        return false;

    m_width = width;
    m_height = height;

    EnableDebugLayer();

    if (!CreateDevice())
        return false;

    if (!CreateSwapChain(hwnd, width, height))
        return false;

    if (!CreateRenderTargetViews())
        return false;

    if (!CreateFrameResources())
        return false;

    HRESULT hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr))
        return false;

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent)
        return false;

    m_initialized = true;
    return true;
}

bool CHW12::CreateDevice()
{
    ComPtr<IDXGIFactory6> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr))
        return false;

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
            return false;

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
        return false;

    DXGI_ADAPTER_DESC1 desc1 = {};
    if (FAILED(adapter->GetDesc1(&desc1)))
        return false;
    wprintf(L"[DX12] GPU: %s (vendor: %04X, device: %04X)\n",
            desc1.Description, desc1.VendorId, desc1.DeviceId);

    hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
    if (FAILED(hr))
        return false;

    wprintf(L"[DX12] Device created, feature level: 11.0\n");

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

    hr = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
    if (FAILED(hr))
        return false;

    // Info queue: validation-сообщения D3D12 без него остаются только в
    // Output отладчика и теряются при запуске без него.
    if (m_debugLayerEnabled)
        m_device.As(&m_infoQueue);

    wprintf(L"[DX12] Command queue created\n");
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
        return false;

    hr = swapChain1.As(&m_swapChain);
    if (FAILED(hr))
        return false;

    // Alt+Enter переводит swapchain в exclusive fullscreen, который этот
    // bootstrap не умеет обрабатывать: подавляем переключатель.
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    wprintf(L"[DX12] Swapchain created: %ux%u, %u backbuffers\n", width, height, FRAME_COUNT);
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
        return false;

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < FRAME_COUNT; ++i)
    {
        hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_frameResources[i].renderTarget));
        if (FAILED(hr))
            return false;

        m_device->CreateRenderTargetView(
            m_frameResources[i].renderTarget.Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_rtvDescriptorSize;
    }

    wprintf(L"[DX12] RTV descriptors created\n");
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
            return false;

        hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         m_frameResources[i].commandAllocator.Get(), nullptr,
                                         IID_PPV_ARGS(&m_frameResources[i].commandList));
        if (FAILED(hr))
            return false;

        // Создание возвращает лист в открытом состоянии; закрываем, чтобы
        // Reset() в RenderFrame() был первым обращением к нему.
        hr = m_frameResources[i].commandList->Close();
        if (FAILED(hr))
            return false;
    }

    wprintf(L"[DX12] Command allocators and lists created\n");
    return true;
}

bool CHW12::WaitForFenceValue(uint64_t value)
{
    if (!m_fence)
        return false;

    if (m_fence->GetCompletedValue() >= value)
        return true;

    HRESULT hr = m_fence->SetEventOnCompletion(value, m_fenceEvent);
    if (FAILED(hr))
        return false;

    // Таймаут вместо INFINITE: повисший GPU должен давать диагностику,
    // а не молча подвешивать приложение.
    return WaitForSingleObject(m_fenceEvent, GPU_WAIT_TIMEOUT_MS) == WAIT_OBJECT_0;
}

bool CHW12::WaitForGpu()
{
    if (!m_fence || !m_commandQueue)
        return false;

    // Каноничная идиома: сигналим НОВОЕ значение и ждём именно его.
    // Сигнал ранее отправленного значения (старая версия) ничего не
    // гарантирует и при втором источнике сигналов молча пропускает ожидание.
    const uint64_t value = ++m_fenceValue;
    m_commandQueue->Signal(m_fence.Get(), value);
    return WaitForFenceValue(value);
}

void CHW12::MoveToNextFrame()
{
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

// Печатает накопившиеся validation-сообщения в stdout. Без этого при запуске
// без отладчика ошибки D3D12 не видны вообще.
void CHW12::DrainInfoQueue()
{
    if (!m_infoQueue)
        return;

    const UINT64 count = m_infoQueue->GetNumStoredMessages();
    for (UINT64 i = 0; i < count; ++i)
    {
        SIZE_T size = 0;
        m_infoQueue->GetMessage(i, nullptr, &size);

        D3D12_MESSAGE* msg = static_cast<D3D12_MESSAGE*>(malloc(size));
        if (!msg)
            break;

        if (SUCCEEDED(m_infoQueue->GetMessage(i, msg, &size)))
            printf("[DX12][validation] %s\n", msg->pDescription);

        free(msg);
    }
    m_infoQueue->ClearStoredMessages();
}

void CHW12::ReportDeviceRemoved(HRESULT hr)
{
    m_deviceRemoved = true;

    wprintf(L"[DX12] FATAL: device removed, hr=0x%08X\n", static_cast<unsigned>(hr));

    if (!m_device)
        return;

    const HRESULT reason = m_device->GetDeviceRemovedReason();
    wprintf(L"[DX12] GetDeviceRemovedReason: 0x%08X\n", static_cast<unsigned>(reason));

    // DRED: page-fault вывод сохраняется после removal и указывает на ресурс.
    ComPtr<ID3D12DeviceRemovedExtendedData> dred;
    if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&dred))))
    {
        D3D12_DRED_PAGE_FAULT_OUTPUT pageFault = {};
        if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pageFault)))
            wprintf(L"[DX12] DRED page fault VA: 0x%016llX\n",
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
    if (frame.fenceValue != 0 && !WaitForFenceValue(frame.fenceValue))
    {
        ReportDeviceRemoved(DXGI_ERROR_DEVICE_REMOVED);
        return false;
    }

    HRESULT hr = frame.commandAllocator->Reset();
    if (FAILED(hr))
        return false;

    ID3D12GraphicsCommandList* cmd = frame.commandList.Get();
    hr = cmd->Reset(frame.commandAllocator.Get(), nullptr);
    if (FAILED(hr))
        return false;

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
        return false;

    ID3D12CommandList* lists[] = {cmd};
    m_commandQueue->ExecuteCommandLists(1, lists);

    // Present — единственный источник DEVICE_REMOVED/DEVICE_RESET и единственное
    // место, откуда приходит DXGI_STATUS_OCCLUDED для свёрнутого окна.
    hr = m_swapChain->Present(1, 0);

    // Сигналим fence и при occlusion: слот должен разблокироваться после
    // восстановления окна, иначе он зависнет навсегда.
    frame.fenceValue = ++m_fenceValue;
    if (FAILED(m_commandQueue->Signal(m_fence.Get(), frame.fenceValue)))
    {
        ReportDeviceRemoved(DXGI_ERROR_DEVICE_REMOVED);
        return false;
    }

    if (hr == DXGI_STATUS_OCCLUDED)
    {
        m_occluded = true;
        MoveToNextFrame();
        return true;
    }

    m_occluded = false;

    if (FAILED(hr))
    {
        ReportDeviceRemoved(hr);
        return false;
    }

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

    if (!WaitForGpu())
        return false;

    for (uint32_t i = 0; i < FRAME_COUNT; ++i)
        m_frameResources[i].renderTarget.Reset();

    HRESULT hr = m_swapChain->ResizeBuffers(
        FRAME_COUNT, width, height, BACKBUFFER_FORMAT, 0);
    if (FAILED(hr))
    {
        // ResizeBuffers провалился при уже отпущенных backbuffer'ах: состояние
        // несогласовано, единственный безопасный ответ — остановить кадры.
        ReportDeviceRemoved(hr);
        return false;
    }

    m_width = width;
    m_height = height;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Старый RTV-хип указывает на освобождённые буферы: пересоздаём целиком.
    m_rtvHeap.Reset();
    if (!CreateRenderTargetViews())
        return false;

    wprintf(L"[DX12] Resized to %ux%u\n", width, height);
    DrainInfoQueue();
    return true;
}

void CHW12::Shutdown()
{
    if (!m_initialized)
        return;

    WaitForGpu();

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
        // ReportLiveObjects: список неосвобождённых D3D-объектов в stdout.
        ComPtr<IDXGIDebug1> dxgiDebug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
            dxgiDebug->ReportLiveObjects(kDxgiDebugAll, DXGI_DEBUG_RLO_ALL);
    }

    m_device.Reset();
    m_fence.Reset();

    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }

    m_initialized = false;
    wprintf(L"[DX12] Shutdown complete\n");
}
