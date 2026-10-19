#include "stdafx.h"
#include "CHW12.h"
#include "dx12log.h"
#include "dx12camera.h"
#include "dx12mesh.h"
#include <chrono>
#include <exception>

#include <cstdio>
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

    // Терминальное состояние: GPU-ресурсы переданы карантину, потому что GPU
    // мог ещё их читать. Повторный Initialize запрещён навсегда: аллокаторы и
    // командные листы из карантина потенциально используются GPU, и запись в
    // них со стороны CPU была бы use-after-free.
    //
    // Проверяется именно m_terminalGpuFailure, а не m_shutdownComplete:
    // штатное завершение не мешает повторной инициализации, и
    // последовательность Initialize → Shutdown → Initialize должна работать.
    if (m_terminalGpuFailure)
    {
        dx12log::Write("[DX12] Initialize refused: terminal GPU failure, resources are "
                       "quarantined and command allocators may still be in use by GPU");
        return false;
    }

    // Карантин process-global, а не member: он переживает уничтожение этого
    // CHW12 и удерживает GPU-ресурсы до завершения процесса. Поэтому занятость
    // проверяется на объекте, а не на флаге экземпляра.
    //
    // Смысл инварианта: пока в карантине лежит партия ресурсов, GPU может их
    // читать. Новый экземпляр, создающий свои аллокаторы и командные листы,
    // потреблял бы ту же видеопамять и тот же GPU. Для standalone Smoke это
    // single-instance policy: единственный владелец карантина — единственный
    // допущенный к работе с GPU объект.
    //
    // Это НЕ многопоточная реализация. Карантин не синхронизируется: тест
    // и движок создают CHW12 последовательно, в одном потоке.
    if (g_gpuQuarantine.occupied)
    {
        dx12log::Write("[DX12] Initialize refused: quarantine is occupied, GPU resources "
                       "from a previous instance are still retained and may be in use");
        return false;
    }

    // Новая попытка инициализации (в том числе после неуспешной, когда
    // Shutdown уже приводил объект к исходному состоянию): идемпотентность
    // Shutdown снимается, иначе cleanup после повторной попыки стал бы no-op.
    m_shutdownComplete = false;
    m_recordFailed=false; m_recordFrames=m_recordSamples=0;
    m_recordMs=m_queueSubmitMs=0;


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

    if (!CreateDepthBuffer())
    {
        dx12log::Write("[DX12] Initialize failed at CreateDepthBuffer");
        return false;
    }
    m_stage = Stage::DepthBuffer;

    // F3.1: камера. SetAspect обязателен: иначе проекция останется от
    // соотношения сторон по умолчанию, и куб растянется — тихая регрессия
    // без validation error.
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    if (!m_camera.SetAspect(aspect))
    {
        dx12log::Write("[DX12] Initialize failed at camera SetAspect");
        return false;
    }

    // F3.1: два куба. Загружаем ПОСЛЕ CreateSynchronization, когда
    // command queue и fence существуют: команды копии вершин идут в очередь,
    // а ожидание их завершения выполняется сразу ниже через WaitForGpu.
    // До этого ожидания upload buffer не освобождается, поэтому GPU успевает
    // прочитать данные. Вершинный буфер при resize не пересоздаётся.
    if (!m_mesh.Create(
            m_device.Get(),
            m_commandQueue.Get(),
            m_frameResources[0].commandAllocator.Get(),
            m_frameResources[0].commandList.Get()))
    {
        dx12log::Write("[DX12] Initialize failed at mesh Create");
        return false;
    }

    // Треугольник отправил команды копии через ExecuteCommandLists, поэтому
    // GPU теперь точно получил работу. Флаг обязателен: без него WaitForGpu
    // решит, что ждать нечего, и вернёт true, не дождавшись копии, после
    // чего upload buffer был бы освобождён при ещё читающем его GPU.
    m_gpuWorkSubmitted = true;

    // Разоваое ожидание GPU на загрузку геометрии. Signal нового значения
    // fence ожидает всю ранее отправленную работу, включая копию вершин.
    //
    // При провале ожидания здесь НЕ вызываем m_mesh.Release(): команды
    // копии уже стоят в очереди GPU, и освобождение upload buffer означало
    // бы use-after-free на стороне GPU.
    //
    // Ресурсы также НЕ освобождаются на этом пути сразу: выход из Initialize
    // с false приводит к деструктору, тот вызывает Shutdown, и если состояние
    // GPU окажется Unknown, Shutdown передаст ресурсы карантину, а не
    // освободит их. Решение принимает DetermineGpuLifetime(), не этот код.
    //
    // Разбор всех ветвей провала ожидания:
    //  - Signal() провалился: копия в очереди GPU. Освобождение unsafe.
    //  - SetEventOnCompletion провалился: копия в очереди GPU. Unsafe.
    //  - WaitForSingleObject таймаут: GPU завис, копия может не выполниться,
    //    но может и выполняться прямо сейчас. Состояние неизвестно, unsafe.
    //  - CheckFenceForRemoval увидел UINT64_MAX: устройство потеряно, GPU
    //    ничего не выполняет. Здесь безопасно, но решение принимается по
    //    фактическому HRESULT GetDeviceRemovedReason(), а не по флагу:
    //    DeviceRemoved освобождает, Unknown карантинит.
    if (!WaitForGpu())
    {
        dx12log::Write("[DX12] Initialize failed at triangle upload WaitForGpu: "
                       "GPU state unknown, resources retained for Shutdown to resolve");
        return false;
    }

    m_mesh.OnUploadComplete();

    // F3.1: readback buffer для автоматического depth-теста (только тестовый
    // режим). Создаётся после depth buffer, потому что GetCopyableFootprints
    // вызывается на описании backbuffer, а readback зависит от размера
    // backbuffer. При resize пересоздаётся вместе с ним (§5.6 ТЗ).
    if (m_testMode && !CreateReadback())
    {
        dx12log::Write("[DX12] Initialize failed at CreateReadback");
        return false;
    }

    m_stage = Stage::Ready;
    m_initialized = true;
    dx12log::Write("[DX12] Initialize complete: %ux%u, %u frames in flight, mesh ready",
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

// F3.1: аллокатор, командный лист и per-frame constant buffer создаются один
// раз на слот кадра и переиспользуются: аллокатор нельзя сбрасывать, пока
// GPU не закончил с ним. CB — upload heap, Map один раз, Unmap не
// вызывается. Это инвариант: добавляющий Unmap «для симметрии» получает
// невалидный mapped на следующем кадре.
//
// Map без Unmap — штатный режим для upload-heap CB. Запись выполняется
// только после ожидания fence слота (RenderFrame ждёт frame.fenceValue в
// начале), поэтому GPU завершил предыдущую отправку слота и единственный
// потенциальный читатель CB этого слота мёртв.
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

        if (m_multiList) for (auto& resource:m_frameResources[i].extra) {
            hr=m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&resource.allocator));
            if (FAILED(hr)) return false;
            hr=m_device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,resource.allocator.Get(),nullptr,IID_PPV_ARGS(&resource.list));
            if (FAILED(hr)) return false;
            hr=resource.list->Close();
            if (FAILED(hr)) return false;
        }

        // Per-frame constant buffer: 256 байт (требование выравнивания),
        // upload heap, GENERIC_READ (требование API для upload heap),
        // pOptimizedClearValue = nullptr (требование API для DIMENSION_BUFFER).
        //
        // Содержимое — одна float4x4 ViewProjection (64 байта), остальное
        // резерв. World-матрицы нет, поэтому WVP не существует как понятие.
        D3D12_HEAP_PROPERTIES uploadHeapProps = {};
        uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        uploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        uploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

        D3D12_RESOURCE_DESC cbDesc = {};
        cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        cbDesc.Width = 256;  // D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT
        cbDesc.Height = 1;
        cbDesc.DepthOrArraySize = 1;
        cbDesc.MipLevels = 1;
        cbDesc.Format = DXGI_FORMAT_UNKNOWN;
        cbDesc.SampleDesc.Count = 1;
        cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        hr = m_device->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_frameResources[i].constantBuffer));
        if (FAILED(hr))
        {
            dx12log::Write("[DX12] CreateCommittedResource(CB %u) failed, hr=0x%08X",
                           i, static_cast<unsigned>(hr));
            return false;
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange = {0, 0};
        hr = m_frameResources[i].constantBuffer->Map(0, &readRange, &mapped);
        if (FAILED(hr))
        {
            dx12log::Write("[DX12] CB(%u) Map failed, hr=0x%08X", i, static_cast<unsigned>(hr));
            m_frameResources[i].constantBuffer.Reset();
            return false;
        }
        m_frameResources[i].mapped = mapped;
    }

    dx12log::Write("[recording] resources mode=%s slots=%u listsPerSlot=%u",m_multiList?"multi":"legacy",FRAME_COUNT,m_multiList?4:1);
    dx12log::Write("[DX12] Command allocators, lists and per-frame CBs created");
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

// F3.1: один общий depth buffer на все три слота кадра. Корректен при
// единственной direct queue: CPU в него не пишет никогда, а внутри одной
// очереди команды выполняются строго в порядке отправки (§7 ТЗ).
//
// Барьеров на depth не добавляем: ресурс создаётся в DEPTH_WRITE и
// остаётся в этом состоянии весь кадр. InitialResourceState для текстур
// применяется по-настоящему (у буферов он игнорируется), поэтому DEPTH_WRITE
// корректен и барьер «в» него не нужен.
//
// D3D12_CLEAR_VALUE передаётся, но это ОПТИМИЗАЦИЯ очистки, не требование
// API: параметр pOptimizedClearValue помечен [in, optional]. Несовпадение
// значения со ClearDepthStencilView не сломало бы рендер — только замедлило
// очистку. Мы очищаем ровно этим значением каждый кадр и получаем
// оптимизированную очистку.
bool CHW12::CreateDepthBuffer()
{
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = m_width;
    desc.Height = m_height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&m_depthBuffer));
    if (FAILED(hr))
    {
        dx12log::Write("[DX12] CreateCommittedResource(depth) failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        return false;
    }

    // DSV heap: один дескриптор, FLAG_NONE (не shader-visible: глубина
    // не сэмплируется в шейдере на F3.1).
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDesc.NumDescriptors = 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    hr = m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_dsvHeap));
    if (FAILED(hr))
    {
        dx12log::Write("[DX12] CreateDescriptorHeap(DSV) failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        m_depthBuffer.Reset();
        return false;
    }

    m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    // DSVFormat в PSO обязан совпадать с форматом ресурса. Расхождение
    // даёт validation error, то есть детектируемо.
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

    m_dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsvDesc, m_dsv);

    dx12log::Write("[DX12] Depth buffer created: %ux%u, D32_FLOAT", m_width, m_height);
    return true;
}

// F3.1: readback buffer для автоматического depth-теста. Создаётся ТОЛЬКО
// в тестовом режиме и включается в карантин наравне с остальными
// GPU-ресурсами.
//
// CopyResource здесь неприменим: backbuffer — Texture2D, readback — Buffer,
// CopyResource требует одинакового типа ресурсов. Копирование текстуры в
// буфер делается через CopyTextureRegion с placed footprint.
//
// Размер буфера — из pTotalBytes, а не RowPitch * numRows: первое может
// быть меньше за счёт упаковки, и выделение по нему не оставляет мусора.
bool CHW12::CreateReadback()
{
    if (m_frameResources[0].renderTarget)
    {
        D3D12_RESOURCE_DESC bbDesc = m_frameResources[0].renderTarget->GetDesc();
        UINT numRows = 0;
        UINT64 rowSizeInBytes = 0;
        UINT64 totalBytes = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};

        m_device->GetCopyableFootprints(
            &bbDesc, 0, 1, 0,
            &layout, &numRows, &rowSizeInBytes, &totalBytes);

        // Допустимость результата проверяется, а не предполагается.
        // GetCopyableFootprints может исключать padding последней строки,
        // поэтому totalBytes < numRows * RowPitch — не ошибка (BLOCKER 3).
        // UINT64_MAX от pTotalBytes означает сбой API (overflow).
        if (totalBytes == 0 || totalBytes == UINT64_MAX ||
            numRows == 0 || numRows == UINT_MAX ||
            rowSizeInBytes == 0 || rowSizeInBytes == UINT64_MAX ||
            layout.Footprint.RowPitch == 0 || layout.Footprint.Width == 0 ||
            layout.Footprint.Height == 0 || layout.Footprint.Depth == 0 ||
            layout.Offset == UINT64_MAX)
        {
            dx12log::Write("[DX12][readback] GetCopyableFootprints returned an "
                           "unusable layout: rows=%u totalBytes=%llu rowPitch=%u",
                           numRows, static_cast<unsigned long long>(totalBytes),
                           layout.Footprint.RowPitch);
            return false;
        }

        // Проверка переполнения: numRows * RowPitch не должно переполнить UINT64.
        // Это не требование к totalBytes (padding последней строки может быть
        // исключён), а защита арифметики при вычислении read range.
        const UINT64 numRows64 = static_cast<UINT64>(numRows);
        const UINT64 rowPitch64 = static_cast<UINT64>(layout.Footprint.RowPitch);
        if (numRows64 > UINT64_MAX / rowPitch64)
        {
            dx12log::Write("[DX12][readback] numRows * RowPitch overflow: rows=%u rowPitch=%u",
                           numRows, layout.Footprint.RowPitch);
            return false;
        }

        // totalBytes должен быть не меньше размера одной строки (rowSizeInBytes),
        // иначе чтение хотя бы одной строки невозможно.
        if (totalBytes < rowSizeInBytes)
        {
            dx12log::Write("[DX12][readback] totalBytes (%llu) < rowSizeInBytes (%llu)",
                           static_cast<unsigned long long>(totalBytes),
                           static_cast<unsigned long long>(rowSizeInBytes));
            return false;
        }

        // Footprint.Width должен быть не меньше ширины backbuffer: каждая
        // строка текстуры должна поместиться в readback строку.
        if (layout.Footprint.Width < bbDesc.Width)
        {
            dx12log::Write("[DX12][readback] Footprint.Width (%u) < backbuffer width (%u)",
                           layout.Footprint.Width, bbDesc.Width);
            return false;
        }

        m_copyableFootprint = layout;
        m_readbackTotalBytes = totalBytes;
    }

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = m_readbackTotalBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_readbackBuffer));
    if (FAILED(hr))
    {
        dx12log::Write("[DX12] CreateCommittedResource(readback) failed, hr=0x%08X",
                       static_cast<unsigned>(hr));
        return false;
    }

    // BLOCKER A: Нет CPU Map в CreateReadback. Map выполняется только в
    // VerifyDepthTest после подтверждения fence completion, с корректным
    // D3D12_RANGE для реально читаемых байтов, и Unmap после чтения.
    dx12log::Write("[DX12] Readback buffer created: %llu bytes, rowPitch=%u, offset=%u",
                   static_cast<unsigned long long>(m_readbackTotalBytes),
                   m_copyableFootprint.Footprint.RowPitch,
                   m_copyableFootprint.Offset);
    return true;
}

// F3.1: автоматический depth-test.
// 1. Синхронизация GPU через fence.
// 2. Map readback с корректным D3D12_RANGE после fence completion.
// 3. Проверка трёх контрольных пикселей.
// 4. Unmap с D3D12_RANGE{0,0} после чтения.
// BLOCKER A: CPU Map только после fence, Unmap после чтения.
// BLOCKER B: каждый пиксель логируется отдельно, без overlapping snprintf.
CHW12::DepthTestResult CHW12::VerifyDepthTest()
{
    DepthTestResult result;
    result.passed = false;

    // Шаг 1: синхронизация GPU. Readback копирование — GPU операция,
    // поэтому ждём завершения fence перед CPU-чтением.
    bool timedOut = false;
    const uint64_t targetFence = m_fenceValue;
    if (!WaitForFenceValue(targetFence, timedOut))
    {
        const char* reason = timedOut ? "GPU timeout" : "fence wait failed";
        snprintf(result.message, sizeof(result.message),
                 "GPU synchronization failed: %s (fence=%llu)", reason,
                 static_cast<unsigned long long>(targetFence));
        dx12log::Write("[DX12] VerifyDepthTest: %s", result.message);
        return result;
    }

    // Шаг 2: Map readback с корректным D3D12_RANGE.
    // CPU читает байты [0, m_readbackTotalBytes), поэтому End = totalBytes.
    // BLOCKER A: Map только после подтверждения fence completion.
    void* mapped = nullptr;
    D3D12_RANGE readRange = {};
    readRange.Begin = 0;
    readRange.End = m_readbackTotalBytes;
    HRESULT mapHr = m_readbackBuffer->Map(0, &readRange, &mapped);
    if (FAILED(mapHr))
    {
        snprintf(result.message, sizeof(result.message),
                 "Map failed after fence completion, hr=0x%08X", static_cast<unsigned>(mapHr));
        dx12log::Write("[DX12] VerifyDepthTest: %s", result.message);
        return result;
    }

    // Гарантия: Unmap выполняется при любом выходе из этой функции.
    struct UnmapGuard
    {
        ID3D12Resource* res;
        bool active;
        UnmapGuard(ID3D12Resource* r) : res(r), active(true) {}
        ~UnmapGuard()
        {
            if (active && res)
            {
                D3D12_RANGE writeRange = {};
                writeRange.Begin = 0;
                writeRange.End = 0; // CPU не изменял данные
                res->Unmap(0, &writeRange);
            }
        }
    } guard(m_readbackBuffer.Get());

    // Шаг 3: проверка пикселей.
    // Контрольные точки обоснованы геометрией:
    //   Камера eye=(0,0,-6), target=(0,0,0), FOV=60°, 1280x720.
    //   Ближний куб: центр (0,0,0), z∈[-1,1]. Задняя грань z=-1 (зелёный)
    //     на расстоянии 5 — ближайшая поверхность в центре экрана.
    //   Дальний куб: центр (0,0,2), z∈[0,4]. Передняя грань z=0 (синий)
    //     на расстоянии 6 — виден выше ближнего куба.
    //   Фон: (18,41,82) = kClearColor {0.07, 0.16, 0.32} * 255.
    struct ControlPoint
    {
        uint32_t x, y;
        const char* name;
        float expectedRgba[4];
    };

    const ControlPoint points[] = {
        {640, 360, "center (near cube back face)", {0.0f, 1.0f, 0.0f, 1.0f}},
        {640, 180, "upper (far cube front face)", {0.0f, 0.0f, 1.0f, 1.0f}},
        {640, 40,  "top (background)", {18.0f/255.0f, 41.0f/255.0f, 82.0f/255.0f, 1.0f}},
    };

    const UINT64 offset = m_copyableFootprint.Offset;
    const UINT rowPitch = m_copyableFootprint.Footprint.RowPitch;
    const uint8_t* data = static_cast<const uint8_t*>(mapped);

    // Полный RGBA-кадр без padding; readback уже защищён прежним fence.
    uint64_t imageHash = 14695981039346656037ull;
    for (uint32_t y=0;y<m_height;++y) {
        const uint64_t row=offset+static_cast<uint64_t>(y)*rowPitch;
        const uint64_t bytes=static_cast<uint64_t>(m_width)*4;
        if (row>m_readbackTotalBytes || bytes>m_readbackTotalBytes-row) {
            snprintf(result.message,sizeof(result.message),"invalid image footprint"); return result;
        }
        for (uint64_t x=0;x<bytes;++x) { imageHash^=data[row+x]; imageHash*=1099511628211ull; }
    }
    dx12log::Write("[depth-frame] hash=%016llx",(unsigned long long)imageHash);

    bool allPassed = true;
    // BLOCKER B: каждый пиксель логируется отдельно, без конкатенации
    // через тот же буфер. Отдельные сообщения для каждого контрольного пункта.
    for (const auto& pt : points)
    {
        float actual[4] = {};
        bool readOk = false;

        if (pt.x < m_width && pt.y < m_height)
        {
            const UINT64 pixelIndex = offset + static_cast<UINT64>(pt.y) * rowPitch +
                                      static_cast<UINT64>(pt.x) * 4;
            if (pixelIndex + 4 <= m_readbackTotalBytes)
            {
                const uint8_t* pixel = data + pixelIndex;
                actual[0] = pixel[0] / 255.0f;
                actual[1] = pixel[1] / 255.0f;
                actual[2] = pixel[2] / 255.0f;
                actual[3] = pixel[3] / 255.0f;
                readOk = true;
            }
        }

        if (!readOk)
        {
            dx12log::Write("[DX12] VerifyDepthTest: [%u,%u] %s: read failed",
                           pt.x, pt.y, pt.name);
            allPassed = false;
            continue;
        }

        const bool ok = (actual[0] == pt.expectedRgba[0] &&
                         actual[1] == pt.expectedRgba[1] &&
                         actual[2] == pt.expectedRgba[2] &&
                         actual[3] == pt.expectedRgba[3]);

        dx12log::Write("[DX12] VerifyDepthTest: [%u,%u] %s: "
                       "expected(%.3f,%.3f,%.3f,%.3f) actual(%.3f,%.3f,%.3f,%.3f) %s",
                       pt.x, pt.y, pt.name,
                       pt.expectedRgba[0], pt.expectedRgba[1],
                       pt.expectedRgba[2], pt.expectedRgba[3],
                       actual[0], actual[1], actual[2], actual[3],
                       ok ? "PASS" : "FAIL");

        if (!ok)
            allPassed = false;
    }

    result.passed = allPassed;
    snprintf(result.message, sizeof(result.message),
             "depth-test %s", allPassed ? "PASS" : "FAIL");
    return result;
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

bool CHW12::IsDeviceRemovedConfirmed()
{
    if (!m_device)
        return false;

    // GetDeviceRemovedReason() возвращает S_OK, если устройство НЕ удалено,
    // и код причины в противном случае. Это фактический HRESULT, а не наш
    // диагностический флаг m_deviceRemoved, который выставляется также при
    // обычных ошибках API и таймаутах.
    const HRESULT reason = m_device->GetDeviceRemovedReason();
    if (reason == S_OK)
        return false;

    dx12log::Write("[DX12] GetDeviceRemovedReason=0x%08X: D3D12 device is removed, "
                   "commands are not being executed", static_cast<unsigned>(reason));
    return true;
}

GpuLifetime CHW12::DetermineGpuLifetime()
{
    // 1. GPU не получал команд: к памяти ресурсов не обращается никто.
    if (!m_gpuWorkSubmitted)
        return GpuLifetime::NoWorkSubmitted;

    // 2. Работа отправлялась, но объекты синхронизации отсутствуют.
    //    Подтвердить завершение нечем, поэтому это НЕ «безопасно», а Unknown.
    //    Такой случай возникает при частично успешном Initialize, где
    //    CreateSynchronization() не был достигнут, но команды уже в очереди.
    if (!m_commandQueue || !m_fence || !m_fenceEvent)
    {
        dx12log::Write("[DX12] GPU work was submitted but synchronization objects "
                       "are missing: completion cannot be confirmed");
        return GpuLifetime::Unknown;
    }

    // 3. Успешный дренаж — единственный путь подтверждённого завершения.
    if (WaitForGpu())
        return GpuLifetime::WorkCompleted;

    // 4. Ожидание не удалось. Решает фактический HRESULT устройства, а не
    //    диагностический m_deviceRemoved.
    if (IsDeviceRemovedConfirmed())
        return GpuLifetime::DeviceRemoved;

    // 5. Устройство живо, но завершение не подтверждено: GPU может читать
    //    ресурсы прямо сейчас.
    return GpuLifetime::Unknown;
}

void CHW12::QuarantineGpuResources()
{
    // Повторное занятие запрещено: иначе указатели первой партии были бы
    // потеряны безвозвратно, а объекты остались бы незакрытыми.
    if (g_gpuQuarantine.occupied)
    {
        dx12log::Write("[DX12] CRITICAL: quarantine already occupied, cannot "
                       "quarantine a second set of GPU resources");
        return;
    }

    GpuQuarantine& q = g_gpuQuarantine;

    // Передача через Detach(): счётчик ссылок не меняется, AddRef не нужен.
    // После этого деструкторы CHW12 не освобождают объекты, потому что
    // соответствующие ComPtr равны nullptr.
    q.device       = m_device.Detach();
    q.commandQueue = m_commandQueue.Detach();
    q.swapChain    = m_swapChain.Detach();
    q.rtvHeap      = m_rtvHeap.Detach();

    for (uint32_t i = 0; i < FRAME_COUNT && i < kQuarantineFrameCount; ++i)
    {
        q.slots[i].renderTarget     = m_frameResources[i].renderTarget.Detach();
        q.slots[i].commandAllocator = m_frameResources[i].commandAllocator.Detach();
        q.slots[i].commandList      = m_frameResources[i].commandList.Detach();
        for (unsigned j=0;j<3;++j) {
            q.slots[i].extraAllocators[j]=m_frameResources[i].extra[j].allocator.Detach();
            q.slots[i].extraLists[j]=m_frameResources[i].extra[j].list.Detach();
        }
        q.slots[i].fenceValue       = m_frameResources[i].fenceValue;

        // Per-frame constant buffer. Владение передаётся через Detach(),
        // CPU-указатель mapped обнуляется в том же вызове. Это не косметика:
        // после Detach объект принадлежит карантину, и оставшийся валидным
        // mapped позволял бы писать в upload heap, который потенциально
        // ещё читает GPU.
        q.constantBuffers[i] = m_frameResources[i].constantBuffer.Detach();
        m_frameResources[i].mapped = nullptr;
    }

    // Depth buffer и DSV heap. Depth buffer — чистый GPU-ресурс, CPU в него
    // не пишет никогда, но при неизвестном GPU completion он может ещё
    // читаться, поэтому передаётся карантину.
    q.depthBuffer = m_depthBuffer.Detach();
    q.dsvHeap     = m_dsvHeap.Detach();

    // Readback buffer (только тестовый режим). mappedReadback обнуляется
    // там же — readback-буфер тоже Map'ится, и запись в него после передачи
    // владения была бы ошибкой того же класса.
    q.readbackBuffer = m_readbackBuffer.Detach();
    mappedReadback = nullptr;

    // Синхронизация. Fence удерживается по той же причине: при таймауте
    // runtime сохраняет регистрацию SetEventOnCompletion и может записать в
    // event после того, как CPU прекратил ожидание.
    q.fence      = m_fence.Detach();

    // Event — Win32 handle, не COM. Владение передаётся присваиванием,
    // поле обнуляется, чтобы CloseHandle далее стал no-op. Это обязательно:
    // закрытие event при активной регистрации в runtime — неопределённое
    // поведение на стороне драйвера.
    q.fenceEvent = m_fenceEvent;
    m_fenceEvent = nullptr;

    // Ресурсы меша передаются тем же способом.
    m_mesh.Quarantine(q.vertexBuffer, q.uploadBuffer,
                      q.rootSignature, q.pipelineState);

    // m_infoQueue намеренно НЕ карантинируется: это CPU-объект сбора
    // validation-сообщений, GPU к его памяти не обращается. Он освобождается
    // штатным путём ниже, как и раньше.

    q.occupied = true;

    dx12log::Write("[DX12] GPU resources quarantined: ownership transferred to "
                   "process-lifetime storage via Detach(), GPU may still be "
                   "reading them");
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

// F3.3.0a TEST ONLY: одноразовый отказ WM_TIMER-пути. Реализован отдельной
// точкой входа, а не проверкой флага внутри RenderFrame(), чтобы главный цикл
// (busy loop, вызывает RenderFrame() постоянно) физически не мог израсходовать
// инъекцию раньше таймера. Ни device, ни fence, ни ресурсы, ни JobSystem при
// инъекции не затрагиваются — возвращается только false.
bool CHW12::RenderFrameFromTimer()
{
    if (m_timerFaultArmed)
    {
        m_timerFaultArmed = false;
        return false;
    }
    return RenderFrame();
}

bool CHW12::InjectRecordingFailure(unsigned stage, unsigned list) {
    if (m_recordFaultStage!=stage || m_recordFaultList!=list) return false;
    m_recordFaultStage=m_recordFaultList=~0u; m_recordFailed=true;
    dx12log::Write("[recording-fault] stage=%u list=%u submitted=%llu",stage,list,(unsigned long long)m_recordFrames);
    return true;
}

bool CHW12::RenderFrame()
{
    if (!m_initialized)
        return false;

    if (m_deviceRemoved)
        return false;

    if (m_recordFailed || m_prepFailed || m_preparing) return false;
    m_preparing=true;
    struct PreparingGuard { bool& active; ~PreparingGuard() { active=false; } } guard{m_preparing};
    renderprep12::PreparedFrame12 prepared;
    try {
        const auto start=std::chrono::steady_clock::now();
        const renderprep12::RenderSceneSnapshot12 snapshot(m_camera.CaptureViewProjection(),m_mesh.Objects(),m_sceneGeneration);
        const double snapshotMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        renderprep12::Timings12 timing;
        prepared=renderprep12::Prepare(snapshot,m_sceneGeneration,m_prepMode,m_jobs,timing);
        ++m_prepFrames;
        if (m_prepFrames>30) {
            ++m_prepSamples; m_snapshotMs+=snapshotMs; m_serialMs+=timing.serial;
            m_parallelMs+=timing.parallel; m_submitMs+=timing.submit; m_waitMs+=timing.wait;
            m_mergeMs+=timing.merge;
        }
    } catch (const std::exception& e) {
        m_prepFailed=true; dx12log::Write("[renderprep] FAIL: %s",e.what()); return false;
    } catch (...) {
        m_prepFailed=true; dx12log::Write("[renderprep] FAIL: unknown exception"); return false;
    }
    FrameResource& frame = m_frameResources[m_frameIndex];

    // Ждём завершения именно этого слота: его аллокатор и лист не должны
    // трогаться CPU, пока GPU читает записанные в них команды.
    if (frame.fenceValue != 0)
    {
        bool timedOut = false;
        // F3.3.0a: ожидание GPU fence — отдельная синхронизация от CPU
        // JobGroup::Wait, поэтому измеряется отдельно и не смешивается с ним.
        const auto fenceStart=std::chrono::steady_clock::now();
        const bool fenceCompleted=WaitForFenceValue(frame.fenceValue, timedOut);
        m_gpuFenceMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-fenceStart).count();
        ++m_gpuFenceWaits;
        if (!fenceCompleted)
        {
            // Таймаут и сбой API оба означают, что аллокатор нельзя сбрасывать:
            // GPU может всё ещё писать в его память.
            ReportGpuFailure(L"WaitForFenceValue", DXGI_ERROR_DEVICE_REMOVED);
            return false;
        }
    }

    const auto recordStart=std::chrono::steady_clock::now();
    ID3D12GraphicsCommandList* commands[4]={frame.commandList.Get(),frame.extra[0].list.Get(),frame.extra[1].list.Get(),frame.extra[2].list.Get()};
    ID3D12CommandAllocator* allocators[4]={frame.commandAllocator.Get(),frame.extra[0].allocator.Get(),frame.extra[1].allocator.Get(),frame.extra[2].allocator.Get()};
    const unsigned listCount=m_multiList?4u:1u;
    HRESULT hr=S_OK;
    for (unsigned i=0;i<listCount;++i) {
        if (InjectRecordingFailure(0,i)) return false;
        hr=allocators[i]->Reset();
        if (FAILED(hr)) { m_recordFailed=true; ReportGpuFailure(L"allocator Reset",hr); return false; }
        if (InjectRecordingFailure(1,i)) return false;
        hr=commands[i]->Reset(allocators[i],nullptr);
        if (FAILED(hr)) { m_recordFailed=true; ReportGpuFailure(L"command list Reset",hr); return false; }
    }
    const auto closeList=[&](unsigned index) {
        if (InjectRecordingFailure(2,index) || InjectRecordingFailure(3,index)) return false;
        const HRESULT closeHr=commands[index]->Close();
        if (FAILED(closeHr)) { m_recordFailed=true; ReportGpuFailure(L"command list Close",closeHr); return false; }
        return true;
    };
    ID3D12GraphicsCommandList* cmd=commands[0];

    D3D12_RESOURCE_BARRIER toRenderTarget = {};
    toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    toRenderTarget.Transition.pResource = frame.renderTarget.Get();
    toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cmd->ResourceBarrier(1, &toRenderTarget);

    // F3.1: OMSetRenderTargets с DSV. Без depth-теста ближний куб не
    // перекрывал бы дальний, и depth-тест не был бы наблюдаемым.
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += static_cast<SIZE_T>(m_frameIndex) * m_rtvDescriptorSize;
    cmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &m_dsv);

    // Viewport и scissor из актуальных m_width/m_height: без них DX12
    // rasterization не определена, а кубы должны оставаться видимыми
    // после любого resize. Значения берутся из текущего состояния CHW12,
    // а не кэшируются, поэтому resize их не ломает.
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    cmd->RSSetViewports(1, &viewport);

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(m_width);
    scissor.bottom = static_cast<LONG>(m_height);
    cmd->RSSetScissorRects(1, &scissor);

    cmd->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);

    // F3.1: ClearDepthStencilView каждый кадр. Depth buffer общий на все
    // слоты, но команды в одной очереди выполняются строго в порядке
    // отправки, поэтому clear кадра N+1 не может выполниться раньше
    // draw кадра N (§7 ТЗ).
    cmd->ClearDepthStencilView(m_dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // F3.1: расчёт ViewProjection камерой. Одна матрица на кадр, world-матрицы
    // нет (мировые координаты запечены в вершины).
    const auto& viewProj = prepared.shaderCamera;

    // F3.1: единственная запись CB за кадр. memcpy, а не поэлементная
    // запись: содержимое — одна float4x4, и memcpy быстрее и проще.
    // Запись выполняется после ожидания fence слота (начало RenderFrame)
    // и после Reset аллокатора, поэтому GPU завершил предыдущую отправку
    // слота и CB этого слота безопасен для записи.
    memcpy(frame.mapped, &viewProj, sizeof(viewProj));

    // F3.1: два draw call с ОБЩИМ адресом CB. Между ними память CB не
    // перезаписывается ни при каких условиям: матрица одна на кадр и от
    // объекта не зависита (§2.3 ТЗ). GBV не ловит перезапись: адрес
    // выровнен и валиден, но второй куб получил бы матрицу первого.
    //
    // Ближний куб (вершины [0, 36)) рисуется ПЕРВЫМ, дальний ([36, 72))
    // ПОСЛЕДНИМ. Порядок обратен painter's algorithm намеренно: если
    // depth-тест выключен или сломан, дальний куб рисуется последним и
    // перезаписывает ближний — тест падает. При работающем depth-тесте
    // ближний куб первым пишет цвет A и глубину, дальний проваливает
    // LESS и не пишет ничего.
    if (m_multiList) {
        if (prepared.packets.size()!=2) { m_recordFailed=true; return false; }
        if (!closeList(0)) return false;
        for (unsigned i=0;i<2;++i) {
            cmd=commands[i+1];
            // Каждый Draw list самодостаточен: состояние соседнего list не наследуется.
            cmd->OMSetRenderTargets(1,&rtvHandle,FALSE,&m_dsv);
            cmd->RSSetViewports(1,&viewport); cmd->RSSetScissorRects(1,&scissor);
            const auto& packet=prepared.packets[i];
            // Draw задаёт PSO/root signature, topology, VB и root CBV.
            m_mesh.Draw(cmd,frame.constantBuffer->GetGPUVirtualAddress(),packet.object.firstVertex,packet.object.vertexCount);
            if (!closeList(i+1)) return false;
        }
        cmd=commands[3];
    } else {
        for (const auto& packet : prepared.packets)
            m_mesh.Draw(cmd,frame.constantBuffer->GetGPUVirtualAddress(),packet.object.firstVertex,packet.object.vertexCount);
    }

    // F3.1: readback для автоматического depth-тест (только тестовый режим).
    // Копирование выполняется после обоих Draw и ДО барьера в PRESENT.
    if (m_testMode && m_readbackBuffer)
    {
        // Барьер backbuffer RENDER_TARGET -> COPY_SOURCE.
        // CopyResource здесь неприменим: backbuffer — Texture2D,
        // readback — Buffer, CopyResource требует одинакового типа.
        D3D12_RESOURCE_BARRIER toCopySource = {};
        toCopySource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopySource.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        toCopySource.Transition.pResource = frame.renderTarget.Get();
        toCopySource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toCopySource.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toCopySource.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        cmd->ResourceBarrier(1, &toCopySource);

        // D3D12_TEXTURE_COPY_TYPE имеет РОВНО ДВА значения
        // (d3d12.h:3329-3333, SDK 10.0.26100.0):
        //   D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX = 0
        //   D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT = 1
        // Значения SUBRESOURCE_FOOTPRINT не существует. Type и поле union
        // ОБЯЗНЫ согласовываться: при PLACED_FOOTPRINT читается
        // PlacedFootprint, при SUBRESOURCE_INDEX — SubresourceIndex.
        // Несовпадение = UB, не ошибка компиляции.

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource       = m_readbackBuffer.Get();
        dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = m_copyableFootprint;  // Offset = 0

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource        = frame.renderTarget.Get();
        src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;

        cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        // Барьер обратно COPY_SOURCE -> PRESENT.
        D3D12_RESOURCE_BARRIER toPresentFromCopy = toCopySource;
        toPresentFromCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        toPresentFromCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        cmd->ResourceBarrier(1, &toPresentFromCopy);
    }
    else
    {
        // Обычный режим: RENDER_TARGET → PRESENT (единственный переход).
        D3D12_RESOURCE_BARRIER toPresent = toRenderTarget;
        toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        cmd->ResourceBarrier(1, &toPresent);
    }

    if (!closeList(m_multiList?3u:0u)) return false;
    const double recordingMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-recordStart).count();
    ID3D12CommandList* lists[4]={commands[0],commands[1],commands[2],commands[3]};
    const auto submitStart=std::chrono::steady_clock::now();
    m_commandQueue->ExecuteCommandLists(listCount,lists);
    const double submissionMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-submitStart).count();
    ++m_recordFrames;
    if (m_recordFrames>30) { ++m_recordSamples; m_recordMs+=recordingMs; m_queueSubmitMs+=submissionMs; }
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

    // F3.1-DIAG: измерение времени Resize.
    LARGE_INTEGER diagStart, diagNow;
    static LARGE_INTEGER s_diagFreq = {};
    if (s_diagFreq.QuadPart == 0)
        QueryPerformanceFrequency(&s_diagFreq);
    QueryPerformanceCounter(&diagStart);

    // Провал ожидания GPU: backbuffer'ы не отпускаем, состояние неизвестно.
    if (!WaitForGpu())
    {
        dx12log::Write("[DX12] Resize aborted: WaitForGpu failed, backbuffers kept");
        ReportGpuFailure(L"Resize/WaitForGpu", E_FAIL);
        return false;
    }
    QueryPerformanceCounter(&diagNow);
    const double diagWaitMs = static_cast<double>(diagNow.QuadPart - diagStart.QuadPart) * 1000.0 /
                              static_cast<double>(s_diagFreq.QuadPart);
    dx12log::Write("[diag] Resize: WaitForGpu=%.1f ms", diagWaitMs);

    // GPU завершён: все слоты безопасны для пересоздания. Сбрасываем
    // per-frame fence tracking только теперь, после подтверждённого drain.
    for (uint32_t i = 0; i < FRAME_COUNT; ++i)
        m_frameResources[i].fenceValue = 0;

    // F3.1: освобождаем командные листы и per-frame CB. Close() здесь НЕ
    // вызывается: листы уже закрыты (CreateFrameResources закрывает при
    // создании, RenderFrame закрывает в конце каждого кадра), а повторный
    // Close() по Microsoft Learn возвращает E_FAIL. Освобождение COM-ссылки
    // на лист убирает косвенную ссылку на backbuffers, которую лист держит
    // через pResource барьеров. Аллокаторы освобождаем вместе с листами и
    // пересоздаём ниже: это проще и надёжнее, чем пытаться угадать состояние
    // Reset/Close для каждого.
    for (uint32_t i = 0; i < FRAME_COUNT; ++i)
    {
        for (auto& resource:m_frameResources[i].extra) {
            resource.list.Reset(); resource.allocator.Reset();
        }
        m_frameResources[i].commandList.Reset();
        m_frameResources[i].commandAllocator.Reset();
        m_frameResources[i].renderTarget.Reset();

        // CB: mapped обнуляется ДО Reset, чтобы dangling указатель на
        // освобождённый upload heap не существовал даже временно.
        m_frameResources[i].mapped = nullptr;
        m_frameResources[i].constantBuffer.Reset();
    }

    // Старый RTV-хип тоже держит ссылки на освобождённые буферы через
    // дескрипторы: сбрасываем до ResizeBuffers, а не после.
    m_rtvHeap.Reset();

    // F3.1: освобождаем depth buffer и DSV heap. Depth buffer зависит от
    // размера окна (m_width × m_height), поэтому пересоздаётся при resize.
    m_depthBuffer.Reset();
    m_dsvHeap.Reset();

    // F3.1: освобождаем readback buffer. RowPitch и число строк зависят от
    // размера backbuffer, поэтому readback пересоздаётся вместе с ним.
    mappedReadback = nullptr;
    m_readbackBuffer.Reset();

    QueryPerformanceCounter(&diagStart);
    HRESULT hr = m_swapChain->ResizeBuffers(
        FRAME_COUNT, width, height, BACKBUFFER_FORMAT, 0);
    if (FAILED(hr))
    {
        // ResizeBuffers провалился при уже отпущенных backbuffer'ах: состояние
        // нессогласовано, единственный безопасный ответ — остановить кадры.
        // Освобождённые буферы больше никем не используются: рендеринг прекращён.
        dx12log::Write("[DX12] ResizeBuffers failed, hr=0x%08X: rendering stopped, "
                       "released backbuffers are not referenced anymore",
                       static_cast<unsigned>(hr));
        ReportGpuFailure(L"ResizeBuffers", hr);
        return false;
    }
    QueryPerformanceCounter(&diagNow);
    dx12log::Write("[diag] Resize: ResizeBuffers=%.1f ms",
                   static_cast<double>(diagNow.QuadPart - diagStart.QuadPart) * 1000.0 /
                   static_cast<double>(s_diagFreq.QuadPart));

    m_width = width;
    m_height = height;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Новый RTV-хип на новые буферы: старые дескрипторы уже уничтожены.
    QueryPerformanceCounter(&diagStart);
    if (!CreateRenderTargetViews())
    {
        // Провал после успешного ResizeBuffers: буферы новые, но без RTV
        // рендерить нельзя. Останавливаемся, не трогая освобождённое.
        dx12log::Write("[DX12] CreateRenderTargetViews failed after resize: rendering stopped");
        ReportGpuFailure(L"CreateRenderTargetViews", E_FAIL);
        return false;
    }
    QueryPerformanceCounter(&diagNow);
    dx12log::Write("[diag] Resize: CreateRenderTargetViews=%.1f ms",
                   static_cast<double>(diagNow.QuadPart - diagStart.QuadPart) * 1000.0 /
                   static_cast<double>(s_diagFreq.QuadPart));

    // Аллокаторы, листы и per-frame CB пересоздаются под новые RTV: пустые
    // ComPtr после Reset() выше. CreateFrameResources закрывает лист сразу
    // после создания, поэтому RenderFrame получит закрытый лист и Reset()
    // будет первым обращением к нему, как и при Initialize.
    QueryPerformanceCounter(&diagStart);
    if (!CreateFrameResources())
    {
        dx12log::Write("[DX12] CreateFrameResources failed after resize: rendering stopped");
        ReportGpuFailure(L"CreateFrameResources", E_FAIL);
        return false;
    }
    QueryPerformanceCounter(&diagNow);
    dx12log::Write("[diag] Resize: CreateFrameResources=%.1f ms",
                   static_cast<double>(diagNow.QuadPart - diagStart.QuadPart) * 1000.0 /
                   static_cast<double>(s_diagFreq.QuadPart));

    // F3.1: depth buffer пересоздан под новый размер. Старый RowPitch, Offset
    // и totalBytes после resize НЕДЕЙСТВИТЕЛЬНЫ: они описывали прежний
    // backbuffer. Использование сохранённого footprint от прежнего размера
    // дало бы чтение за пределами буфера.
    QueryPerformanceCounter(&diagStart);
    if (!CreateDepthBuffer())
    {
        dx12log::Write("[DX12] CreateDepthBuffer failed after resize: rendering stopped");
        ReportGpuFailure(L"CreateDepthBuffer", E_FAIL);
        return false;
    }
    QueryPerformanceCounter(&diagNow);
    dx12log::Write("[diag] Resize: CreateDepthBuffer=%.1f ms",
                   static_cast<double>(diagNow.QuadPart - diagStart.QuadPart) * 1000.0 /
                   static_cast<double>(s_diagFreq.QuadPart));

    // F3.1: readback пересоздан с пересчётом footprint и totalBytes на
    // НОВОМ дескрипторе backbuffer.
    if (m_testMode && !CreateReadback())
    {
        dx12log::Write("[DX12] CreateReadback failed after resize: rendering stopped");
        ReportGpuFailure(L"CreateReadback", E_FAIL);
        return false;
    }

    // F3.1: SetAspect обязателен. Иначе проекция останется от старого
    // соотношения сторон, куб растянется, и это НЕ validation error,
    // а тихая визуальная регрессия.
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    if (!m_camera.SetAspect(aspect))
    {
        dx12log::Write("[DX12] camera SetAspect failed after resize: rendering stopped");
        ReportGpuFailure(L"camera SetAspect", E_FAIL);
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
    dx12log::Write("[renderprep] %s mode=%u frames=%llu samples=%llu packets=2 snapshot=%.6f serial=%.6f parallel=%.6f submit=%.6f wait=%.6f merge=%.6f gpuFence=%.6f ms",
        m_prepFailed ? "FAIL" : "PASS",static_cast<unsigned>(m_prepMode),
        (unsigned long long)m_prepFrames,(unsigned long long)m_prepSamples,
        m_prepSamples?m_snapshotMs/m_prepSamples:0,m_prepSamples?m_serialMs/m_prepSamples:0,
        m_prepSamples?m_parallelMs/m_prepSamples:0,m_prepSamples?m_submitMs/m_prepSamples:0,
        m_prepSamples?m_waitMs/m_prepSamples:0,m_prepSamples?m_mergeMs/m_prepSamples:0,
        m_gpuFenceWaits?m_gpuFenceMs/m_gpuFenceWaits:0);
    dx12log::Write("[recording] %s mode=%s lists=%u frames=%llu samples=%llu recording=%.6f submission=%.6f ms",
        m_recordFailed?"ABORTED":"PASS",m_multiList?"multi":"legacy",m_multiList?4:1,
        (unsigned long long)m_recordFrames,(unsigned long long)m_recordSamples,
        m_recordSamples?m_recordMs/m_recordSamples:0,m_recordSamples?m_queueSubmitMs/m_recordSamples:0);
    ++m_sceneGeneration;
    m_jobs=nullptr;

    // Политика освобождения ресурсов на основе фактического состояния GPU.
    // Предположение «процесс скоро выйдет» не используется: lifetime
    // COM-объекта и lifetime GPU-доступа к его памяти — разные вещи, и
    // Microsoft требует хранить ресурсы до подтверждённого завершения
    // их использования GPU.
    const GpuLifetime lifetime = DetermineGpuLifetime();

    switch (lifetime)
    {
    case GpuLifetime::WorkCompleted:
        dx12log::Write("[DX12] GPU drained before shutdown");
        break;

    case GpuLifetime::DeviceRemoved:
        // Устройство потеряно: команд не выполняется, обращения к памяти
        // ресурсов невозможны. Освобождение безопасно.
        break;

    case GpuLifetime::NoWorkSubmitted:
        // GPU не получал команд: обращаться к ресурсам некому.
        break;

    case GpuLifetime::Unknown:
        // Состояние неизвестно, устройство живо. GPU может читать ресурсы
        // прямо сейчас, поэтому освобождение небезопасно. Владение
        // передаётся карантину с process lifetime.
        dx12log::Write("[DX12] LIMITATION: GPU drain failed and D3D12 device is "
                       "NOT removed: GPU may still be reading resources. GPU "
                       "resources are quarantined until process exit to avoid "
                       "GPU use-after-free. Rendering is stopped and command "
                       "allocators are not reused.");
        QuarantineGpuResources();
        m_terminalGpuFailure = true;
        break;
    }

    // Освобождение GPU-ресурсов выполняется во всех состояниях, кроме
    // Unknown. В Unknown ComPtr уже равны nullptr после Detach(), поэтому
    // эти Reset() стали бы no-op'ами, но явное разделение понятнее.
    if (lifetime != GpuLifetime::Unknown)
    {
        // Обнуляем mapped ДО Reset: dangling указатель на освобождённый
        // upload heap не должен существовать даже временно.
        for (uint32_t i = 0; i < FRAME_COUNT; ++i)
        {
            m_frameResources[i].mapped = nullptr;
            m_frameResources[i].constantBuffer.Reset();
            m_frameResources[i].renderTarget.Reset();
            for (auto& resource:m_frameResources[i].extra) {
                resource.list.Reset(); resource.allocator.Reset();
            }
            m_frameResources[i].commandList.Reset();
            m_frameResources[i].commandAllocator.Reset();
        }

        // Depth buffer и DSV heap. Depth buffer — чистый GPU-ресурс, но
        // при штатном дренаже освобождается безопасно.
        m_depthBuffer.Reset();
        m_dsvHeap.Reset();

        // Readback buffer (только тестовый режим). mappedReadback обнуляется
        // ДО Reset, как и mapped для CB.
        mappedReadback = nullptr;
        m_readbackBuffer.Reset();

        m_rtvHeap.Reset();
        m_commandQueue.Reset();
        m_swapChain.Reset();

        // Ресурсы меша. В карантин они уходят через Quarantine(),
        // поэтому здесь вызов достигается только в штатных состояниях.
        m_mesh.Release();
    }

    if (m_debugLayerEnabled)
    {
        // ReportLiveObjects до сброса device: после Reset() объекты уже
        // уничтожены и отчёт был бы пустым.
        ComPtr<IDXGIDebug1> dxgiDebug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
            dxgiDebug->ReportLiveObjects(kDxgiDebugAll, DXGI_DEBUG_RLO_ALL);
    }

    // m_infoQueue — CPU-объект сбора validation-сообщений, GPU к его памяти
    // не обращается, поэтому освобждается во всех состояниях, включая Unknown.
    // В карантин он не передаётся.
    m_infoQueue.Reset();

    // m_device и m_fence после Detach() в карантине равны nullptr, поэтому
    // Reset() здесь безопасен и в аварийном состоянии: он не тронет объекты,
    // владение которыми уже передано. m_fenceEvent обнулён в карантине
    // присваиванием, поэтому CloseHandle не закрывает quarantined handle.
    m_device.Reset();
    m_fence.Reset();

    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }

    m_initialized = false;
    m_stage = Stage::None;

    // Сброс состояния до «свежесозданного» допустим ТОЛЬКО в штатных
    // состояниях. В Unknown ресурсы находятся в карантине, и сброс
    // m_gpuWorkSubmitted сделал бы повторный Initialize опасным: WaitForGpu()
    // с false вернул бы true без ожидания, после чего рендеринг начал бы
    // писать в аллокаторы, которые GPU потенциально ещё читает.
    //
    // m_terminalGpuFailure не сбрасывается никогда: он и означает, что
    // повторная инициализация запрещена.
    if (lifetime != GpuLifetime::Unknown)
    {
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
        m_dsvDescriptorSize = 0;
    }

    m_shutdownComplete = true;

    dx12log::Write("[DX12] Shutdown complete");
}
