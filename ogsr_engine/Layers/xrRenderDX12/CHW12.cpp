#include "stdafx.h"
#include "CHW12.h"
#include "dx12log.h"
#include "dx12camera.h"
#include "dx12mesh.h"
#include <chrono>
#include <algorithm>
#include <exception>
#include <atomic>
#include <thread>
#include <stdexcept>
#include <vector>
#include "gr/jobs/JobGroup.h"

#include <cstdio>
#include <cstdlib>
#include <dxgidebug.h>

static const float kClearColor[4] = {0.07f, 0.16f, 0.32f, 1.0f};

namespace
{
// F3.5.5A TEST ONLY: сцена из m_mesh.Objects() копируется и детерминированно
// портится до snapshot. Это не traversal и не registry: только проверка
// packet validation.
void ApplyPacketTestFault(std::vector<renderprep12::Object12>& objects,
                          CHW12::PacketTestFault fault)
{
    if (objects.empty())
        return;
    auto& first = objects[0];
    auto& second = objects.size() > 1 ? objects[1] : objects[0];
    switch (fault)
    {
    case CHW12::PacketTestFault::None:
        break;
    case CHW12::PacketTestFault::HideAll:
        for (auto& object : objects)
            object.flags |= renderprep12::Invisible;
        break;
    case CHW12::PacketTestFault::HideSecond:
        second.flags |= renderprep12::Invisible;
        break;
    case CHW12::PacketTestFault::CullSecond:
        DirectX::XMStoreFloat4x4(&second.world,
            DirectX::XMMatrixTranslation(1000.0f, 0.0f, 0.0f));
        break;
    case CHW12::PacketTestFault::Reverse:
        std::reverse(objects.begin(), objects.end());
        break;
    case CHW12::PacketTestFault::StaleVisual:
        first.visual.generation += 1;
        break;
    case CHW12::PacketTestFault::UnsupportedPso:
        first.pso = renderprep12::SyntheticResources12[1];
        break;
    case CHW12::PacketTestFault::UnknownObject:
        first.id = 999;
        break;
    case CHW12::PacketTestFault::InvalidRange:
        first.vertexCount = 37;
        break;
    case CHW12::PacketTestFault::OverflowRange:
        first.firstVertex = UINT32_MAX;
        first.vertexCount = 36;
        break;
    case CHW12::PacketTestFault::Transparent:
        first.flags |= renderprep12::Transparent;
        first.opacity = 0.5f;
        break;
    case CHW12::PacketTestFault::Hud:
        first.flags |= renderprep12::Hud;
        break;
    case CHW12::PacketTestFault::WorldTransform:
        DirectX::XMStoreFloat4x4(&first.world,
            DirectX::XMMatrixTranslation(1.0f, 0.0f, 0.0f));
        break;
    }
}

bool IsIdentityWorld(const DirectX::XMFLOAT4X4& world)
{
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 3; ++column)
            if (world.m[row][column] != (row == column ? 1.0f : 0.0f))
                return false;
    return world.m[3][0] == 0.0f && world.m[3][1] == 0.0f &&
           world.m[3][2] == 0.0f && world.m[3][3] == 1.0f;
}
}

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

// F3.5.4A: L фиксируется до Initialize, потому что каждая draw list слота
// создаёт собственную пару allocator/list. Значение ограничено массивом
// FrameResource; больше MAX_DRAW_LISTS не выделяется.
bool CHW12::SetDrawListCount(uint32_t count)
{
    // Массивы карантина и FrameResource обязаны иметь один предел.
    static_assert(MAX_DRAW_LISTS == kMaxDrawLists,
                  "quarantine and FrameResource draw list bounds must match");
    if (m_initialized)
        return false;
    if (count == 0)
        return false;
    if (count > MAX_DRAW_LISTS)
        return false;
    // Пустые draw lists не допускаются: L не больше N. Проверка явная,
    // чтобы конфигурация отклонялась, а не молча вырождалась в пустые списки.
    if (count > m_drawCount)
        return false;
    m_drawListCount = count;
    return true;
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

    bool gpuValidationEnabled = false;
#if OGSR_DX12_ENABLE_GBV
    // F3.5.4A: GBV включается только по запросу. Корректность проверяется
    // с ней, производительность — отдельным прогоном без неё.
    if (m_gpuValidationRequested)
    {
        ComPtr<ID3D12Debug1> debug1;
        if (SUCCEEDED(debug.As(&debug1)))
        {
            debug1->SetEnableGPUBasedValidation(TRUE);
            gpuValidationEnabled = true;
        }
        else
            dx12log::Write("[DX12] ID3D12Debug1 unavailable: GPU-based validation not enabled");
    }
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

    dx12log::Write("[DX12] Debug layer enabled, GPU-based validation=%d",
                   gpuValidationEnabled ? 1 : 0);
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

    // F3.5.5A: packet path is owner-list only; test faults exist only for it.
    if (m_drawSource == DrawSource::Packets && m_multiList)
    {
        dx12log::Write("[DX12] Initialize refused: packet draw requires a single owner list");
        return false;
    }
    if (m_drawSource != DrawSource::Packets && m_packetTestFault != PacketTestFault::None)
    {
        dx12log::Write("[DX12] Initialize refused: packet test fault requires packet draw");
        return false;
    }

    // Новая попытка инициализации (в том числе после неуспешной, когда
    // Shutdown уже приводил объект к исходному состоянию): идемпотентность
    // Shutdown снимается, иначе cleanup после повторной попыки стал бы no-op.
    m_shutdownComplete = false;
    // F3.5.3B: m_prepFailed обязан сбрасываться вместе с m_recordFailed.
    // Иначе один сбой подготовки кадра навсегда блокирует RenderFrame(),
    // и последовательность Shutdown → Initialize не восстанавливает рендер.
    m_prepFailed = false;
    m_recordFailed=false; m_recordFrames=m_recordSamples=0;
    m_visiblePackets=0;
    m_recordMs=m_queueSubmitMs=m_recordScheduleMs=m_recordWaitMs=0;
    m_timestampFrequency=m_gpuSamples=0; m_gpuMs=0;
    m_frameIntervalMs=0; m_frameIntervals=0; m_lastPresentTick={};
    m_timingSampleCount=0; m_timingSamplesDropped=0;


    m_width = width;
    m_height = height;

    if (m_debugLayerRequested)
        EnableDebugLayer();
    else
        dx12log::Write("[DX12] Debug layer disabled by request");

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
    if (FAILED(m_commandQueue->GetTimestampFrequency(&m_timestampFrequency))) m_timestampFrequency=0;

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

        // F3.5.4A: bounded набор draw lists + отдельный End-список.
        // Количество пар фиксируется до Initialize, поэтому кадр не создаёт
        // новые allocator/list и не выходит за пределы массива.
        if (m_multiList)
        {
            auto& frame = m_frameResources[i];
            frame.drawListCount = m_drawListCount;
            for (uint32_t j = 0; j < frame.drawListCount; ++j)
            {
                auto& resource = frame.drawRecording[j];
                hr=m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&resource.allocator));
                if (FAILED(hr)) {
                    dx12log::Write("[DX12] CreateCommandAllocator(draw %u/%u) failed, hr=0x%08X",
                                   j, frame.drawListCount, static_cast<unsigned>(hr));
                    return false;
                }
                hr=m_device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,resource.allocator.Get(),nullptr,IID_PPV_ARGS(&resource.list));
                if (FAILED(hr)) {
                    dx12log::Write("[DX12] CreateCommandList(draw %u/%u) failed, hr=0x%08X",
                                   j, frame.drawListCount, static_cast<unsigned>(hr));
                    return false;
                }
                hr=resource.list->Close();
                if (FAILED(hr))
                {
                    dx12log::Write("[DX12] Close(draw %u/%u) failed, hr=0x%08X",
                                   j, frame.drawListCount, static_cast<unsigned>(hr));
                    return false;
                }
            }

            auto& end = frame.ownerEnd;
            hr=m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&end.allocator));
            if (FAILED(hr)) {
                dx12log::Write("[DX12] CreateCommandAllocator(end) failed, hr=0x%08X",
                               static_cast<unsigned>(hr));
                return false;
            }
            hr=m_device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,end.allocator.Get(),nullptr,IID_PPV_ARGS(&end.list));
            if (FAILED(hr)) {
                dx12log::Write("[DX12] CreateCommandList(end) failed, hr=0x%08X",
                               static_cast<unsigned>(hr));
                return false;
            }
            hr=end.list->Close();
            if (FAILED(hr))
            {
                dx12log::Write("[DX12] Close(end) failed, hr=0x%08X",
                               static_cast<unsigned>(hr));
                return false;
            }
        }

        if (m_timestampFrequency) {
            auto& frame=m_frameResources[i];
            D3D12_QUERY_HEAP_DESC query={}; query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP; query.Count=2;
            hr=m_device->CreateQueryHeap(&query,IID_PPV_ARGS(&frame.timestamps));
            if (FAILED(hr)) return false;
            D3D12_HEAP_PROPERTIES heap={}; heap.Type=D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC buffer={}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width=16; buffer.Height=1; buffer.DepthOrArraySize=1; buffer.MipLevels=1;
            buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            hr=m_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,IID_PPV_ARGS(&frame.timestampReadback));
            if (FAILED(hr)) return false;
            frame.timestampPending=false;
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

    dx12log::Write("[recording] resources mode=%s slots=%u listsPerSlot=%u",(m_drawSource==DrawSource::Packets)?"packets":((m_parallelRecording && m_drawListCount>=2)?"parallel":(m_multiList?"multi":"legacy")),FRAME_COUNT,m_multiList?(m_drawListCount+2):1);
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
        m_readbackAllocatedBytes = totalBytes;

        if (m_backgroundTest)
        {
            // F3.5.5A TEST ONLY: 4x4 D32 region in the tail of the same
            // readback buffer. Offset must be 512-byte aligned.
            const UINT64 alignedOffset = (totalBytes + 511ull) & ~511ull;
            D3D12_RESOURCE_DESC depthDesc = {};
            depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            depthDesc.Width = 4;
            depthDesc.Height = 4;
            depthDesc.DepthOrArraySize = 1;
            depthDesc.MipLevels = 1;
            depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
            depthDesc.SampleDesc.Count = 1;
            depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            D3D12_PLACED_SUBRESOURCE_FOOTPRINT depthLayout = {};
            UINT depthRows = 0;
            UINT64 depthRowSize = 0;
            UINT64 depthTotal = 0;
            m_device->GetCopyableFootprints(
                &depthDesc, 0, 1, alignedOffset,
                &depthLayout, &depthRows, &depthRowSize, &depthTotal);
            if (depthTotal == 0 || depthTotal == UINT64_MAX ||
                depthRows != 4 || depthLayout.Footprint.Width < 4 ||
                depthLayout.Footprint.Height < 4 ||
                alignedOffset > UINT64_MAX - depthTotal)
            {
                dx12log::Write("[DX12][readback] invalid background depth layout: "
                               "rows=%u total=%llu", depthRows,
                               static_cast<unsigned long long>(depthTotal));
                return false;
            }
            m_backgroundDepthOffset = alignedOffset;
            m_backgroundDepthBytes = depthTotal;
            m_backgroundDepthFootprint = depthLayout;
            m_readbackAllocatedBytes = alignedOffset + depthTotal;
        }
    }

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = m_readbackAllocatedBytes;
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

CHW12::DepthTestResult CHW12::VerifyBackgroundOnly()
{
    DepthTestResult result;
    result.passed = false;

    if (!m_backgroundTest || !m_readbackBuffer || m_backgroundDepthBytes == 0)
    {
        snprintf(result.message, sizeof(result.message),
                 "background oracle requires test readback buffers");
        return result;
    }
    if (m_visiblePackets != 0)
    {
        snprintf(result.message, sizeof(result.message),
                 "background oracle expects zero Draw, got %u visible packets",
                 m_visiblePackets);
        return result;
    }

    bool timedOut = false;
    if (!WaitForFenceValue(m_fenceValue, timedOut))
    {
        snprintf(result.message, sizeof(result.message),
                 "background oracle fence wait failed%s",
                 timedOut ? " (timeout)" : "");
        return result;
    }

    const UINT64 readBytes = m_backgroundDepthOffset + m_backgroundDepthBytes;
    void* mapped = nullptr;
    D3D12_RANGE readRange = {};
    readRange.Begin = 0;
    readRange.End = readBytes;
    if (FAILED(m_readbackBuffer->Map(0, &readRange, &mapped)))
    {
        snprintf(result.message, sizeof(result.message), "background oracle Map failed");
        return result;
    }

    struct BackgroundUnmapGuard
    {
        ID3D12Resource* resource;
        ~BackgroundUnmapGuard()
        {
            if (resource)
            {
                D3D12_RANGE written = {};
                written.Begin = 0;
                written.End = 0;
                resource->Unmap(0, &written);
            }
        }
    } unmapGuard{m_readbackBuffer.Get()};

    const uint8_t* data = static_cast<const uint8_t*>(mapped);
    const uint8_t expected[4] = {
        static_cast<uint8_t>(kClearColor[0] * 255.0f + 0.5f),
        static_cast<uint8_t>(kClearColor[1] * 255.0f + 0.5f),
        static_cast<uint8_t>(kClearColor[2] * 255.0f + 0.5f),
        255
    };

    const UINT rowPitch = m_copyableFootprint.Footprint.RowPitch;
    const UINT64 offset = m_copyableFootprint.Offset;
    for (uint32_t y = 0; y < m_height; ++y)
    {
        const uint8_t* row = data + offset + static_cast<UINT64>(y) * rowPitch;
        for (uint32_t x = 0; x < m_width; ++x)
        {
            const uint8_t* pixel = row + static_cast<UINT64>(x) * 4;
            if (pixel[0] != expected[0] || pixel[1] != expected[1] ||
                pixel[2] != expected[2] || pixel[3] != expected[3])
            {
                snprintf(result.message, sizeof(result.message),
                         "background mismatch at (%u,%u): got %u,%u,%u,%u",
                         x, y, pixel[0], pixel[1], pixel[2], pixel[3]);
                return result;
            }
        }
    }

    const UINT depthRowPitch = m_backgroundDepthFootprint.Footprint.RowPitch;
    const uint8_t* depthData = data + m_backgroundDepthOffset;
    for (uint32_t y = 0; y < 4; ++y)
    {
        for (uint32_t x = 0; x < 4; ++x)
        {
            float depth = 0.0f;
            memcpy(&depth, depthData + static_cast<UINT64>(y) * depthRowPitch +
                           static_cast<UINT64>(x) * sizeof(float), sizeof(depth));
            if (depth != 1.0f)
            {
                snprintf(result.message, sizeof(result.message),
                         "depth clear mismatch at (%u,%u): %f", x, y,
                         static_cast<double>(depth));
                return result;
            }
        }
    }

    result.passed = true;
    snprintf(result.message, sizeof(result.message),
             "background color and depth clear verified");
    dx12log::Write("[background-frame] PASS: %s", result.message);
    return result;
}

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
        for (uint32_t j=0;j<m_frameResources[i].drawListCount && j<kMaxDrawLists;++j) {
            q.slots[i].drawAllocators[j]=m_frameResources[i].drawRecording[j].allocator.Detach();
            q.slots[i].drawLists[j]=m_frameResources[i].drawRecording[j].list.Detach();
        }
        q.slots[i].endAllocator=m_frameResources[i].ownerEnd.allocator.Detach();
        q.slots[i].endList=m_frameResources[i].ownerEnd.list.Detach();
        q.slots[i].timestamps=m_frameResources[i].timestamps.Detach();
        q.slots[i].timestampReadback=m_frameResources[i].timestampReadback.Detach();
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
                   "reading them; drawListsPerSlot=%u endLists=%u",
                   m_multiList?m_drawListCount:0u,m_multiList?FRAME_COUNT:0u);
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

void CHW12::CollectGpuTiming(FrameResource& frame)
{
    // Только owner, после подтверждённого fence/drain; новых ожиданий нет.
    if (!frame.timestampPending || !m_timestampFrequency) return;
    frame.timestampPending=false;
    uint64_t* ticks=nullptr;
    const D3D12_RANGE read={0,16};
    if (FAILED(frame.timestampReadback->Map(0,&read,reinterpret_cast<void**>(&ticks)))) {
        dx12log::Write("[recording-gpu] timestamp Map failed"); return;
    }
    if (frame.timestampSample && ticks[1]>=ticks[0]) {
        const double gpuMs=double(ticks[1]-ticks[0])*1000.0/double(m_timestampFrequency);
        m_gpuMs+=gpuMs; ++m_gpuSamples;
        // F3.5.4B: GPU-время привязывается к срезу кадра, в котором был
        // записан timestamp. Map/Unmap и запись в массив — вне Present.
        if (frame.timestampSampleIndex>=0 &&
            static_cast<uint32_t>(frame.timestampSampleIndex)<m_timingSampleCount)
            m_timingSamples[frame.timestampSampleIndex].gpuMs=gpuMs;
    }
    frame.timestampSampleIndex=-1;
    frame.timestampSample=false;
    const D3D12_RANGE written={0,0}; frame.timestampReadback->Unmap(0,&written);
}

bool CHW12::InjectRecordingFailure(unsigned stage, unsigned list) {
    if (m_recordFaultStage!=stage || m_recordFaultList!=list) return false;
    m_recordFaultStage=m_recordFaultList=~0u; m_recordFailed=true;
    dx12log::Write("[recording-fault] stage=%u list=%u submitted=%llu",stage,list,(unsigned long long)m_recordFrames);
    return true;
}

const char* CHW12::ValidatePacketFrame(const renderprep12::OrderedFrame12& frame) const
{
    if (frame.frame.generation != m_sceneGeneration)
        return "stale frame generation";
    if (frame.categories[1].count != 0 || frame.categories[2].count != 0)
        return "transparent/hud packets are unsupported";
    if (frame.categories[0].begin != 0 ||
        frame.categories[0].count != frame.frame.packets.size())
        return "invalid opaque category range";

    for (const renderprep12::DrawPacket12& packet : frame.frame.packets)
    {
        if (!packet.visible)
            return "hidden packet in ordered frame";
        const renderprep12::Object12* bound = nullptr;
        for (const renderprep12::Object12& object : m_mesh.Objects())
            if (object.id == packet.object.id)
            {
                bound = &object;
                break;
            }
        if (!bound)
            return "unknown object id";
        if (packet.object.firstVertex != bound->firstVertex ||
            packet.object.vertexCount != bound->vertexCount)
            return "vertex range does not match binding";
        if (packet.object.visual != bound->visual ||
            packet.object.material != bound->material ||
            packet.object.pso != bound->pso)
            return "stale or unsupported resource binding";
        if (packet.object.vertexCount == 0 ||
            packet.object.firstVertex > SmokeMesh12::kVertexCount ||
            packet.object.vertexCount >
                SmokeMesh12::kVertexCount - packet.object.firstVertex)
            return "invalid vertex range";
        if (packet.object.flags & (renderprep12::Hud | renderprep12::Transparent))
            return "transparent/hud flags are unsupported";
        if (packet.object.opacity != 1.0f)
            return "non-opaque opacity is unsupported";
        // F3.5.5A сохраняет world-baked vertices: world-матрица не должна
        // молча игнорироваться GPU-путём.
        if (!IsIdentityWorld(packet.object.world))
            return "non-identity world is unsupported";
    }
    return nullptr;
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
    // F3.5.5A lifetime: OrderedFrame12 (CPU packets) is local to RenderFrame
    // and outlives Reset/record/Close/Execute. GPU never reads packet memory:
    // command lists and GPU resources are owned by FrameResource and released
    // only under the existing per-slot fence invariant.
    renderprep12::OrderedFrame12 ordered;
    const bool packetDraw = (m_drawSource == DrawSource::Packets);
    try {
        // F3.5.3B TEST ONLY: одноразовый отказ внутри prepare-пути. Нужен,
        // чтобы получить реальный m_prepFailed через штатный catch, а не
        // выставлять флаг в обход обработки ошибок.
        if (m_prepFaultArmed) {
            m_prepFaultArmed = false;
            throw std::runtime_error("preparation fault injection");
        }
        std::vector<renderprep12::Object12> modifiedObjects;
        std::span<const renderprep12::Object12> objects = m_mesh.Objects();
        if (packetDraw && m_packetTestFault != PacketTestFault::None)
        {
            modifiedObjects.assign(m_mesh.Objects().begin(), m_mesh.Objects().end());
            ApplyPacketTestFault(modifiedObjects, m_packetTestFault);
            objects = modifiedObjects;
        }

        double snapshotMs=0, serialMs=0, parallelMs=0;
        double submitMs=0, waitMs=0, mergeMs=0;
        if (packetDraw)
        {
            DirectX::XMFLOAT4X4 view, projection;
            if (!m_camera.CaptureViewProjectionPair(view, projection))
                throw std::runtime_error("camera view/projection capture failed");
            const auto snapshotStart=std::chrono::steady_clock::now();
            const renderprep12::RenderSceneSnapshot12 snapshot(
                view, projection, objects, m_sceneGeneration);
            snapshotMs=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-snapshotStart).count();
            renderprep12::OrderingTimings12 timing;
            ordered=renderprep12::PrepareOrdered(
                snapshot,m_sceneGeneration,m_prepMode,m_jobs,timing);
            serialMs=timing.serial.total;
            parallelMs=timing.parallel.total;
        }
        else
        {
            const auto snapshotStart=std::chrono::steady_clock::now();
            const renderprep12::RenderSceneSnapshot12 snapshot(
                m_camera.CaptureViewProjection(), objects, m_sceneGeneration);
            snapshotMs=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-snapshotStart).count();
            renderprep12::Timings12 timing;
            prepared=renderprep12::Prepare(snapshot,m_sceneGeneration,m_prepMode,m_jobs,timing);
            serialMs=timing.serial; parallelMs=timing.parallel;
            submitMs=timing.submit; waitMs=timing.wait; mergeMs=timing.merge;
        }
        ++m_prepFrames;
        if (m_prepFrames>30) {
            ++m_prepSamples; m_snapshotMs+=snapshotMs; m_serialMs+=serialMs;
            m_parallelMs+=parallelMs; m_submitMs+=submitMs; m_waitMs+=waitMs;
            m_mergeMs+=mergeMs;
        }
    } catch (const std::exception& e) {
        m_prepFailed=true; dx12log::Write("[renderprep] FAIL: %s",e.what()); return false;
    } catch (...) {
        m_prepFailed=true; dx12log::Write("[renderprep] FAIL: unknown exception"); return false;
    }

    uint32_t packetDrawCount=0;
    if (packetDraw)
    {
        const char* reason=ValidatePacketFrame(ordered);
        if (reason)
        {
            // Явный отказ до Reset/Execute: GPU не получает частичный кадр.
            m_prepFailed=true;
            m_recordFailed=true;
            dx12log::Write("[packet-draw] FAIL: %s",reason);
            return false;
        }
        packetDrawCount=static_cast<uint32_t>(ordered.frame.packets.size());
        m_visiblePackets=packetDrawCount;
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

    CollectGpuTiming(frame);
    double scheduleMs=0, recordWaitMs=0;
    int32_t timingIndex=-1;
    const auto recordStart=std::chrono::steady_clock::now();

    // F3.5.4A: L draw lists + owner Begin + owner End. Legacy остаётся
    // одним списком, который содержит Begin, все Draw и End.
    const uint32_t drawLists = m_multiList ? frame.drawListCount : 0;
    const uint32_t listCount = m_multiList ? (drawLists + 2) : 1;
    const uint32_t drawCount = packetDraw ? packetDrawCount : m_drawCount;
    // Параллельная запись имеет смысл только при >=2 draw lists. При L=1
    // режим вырождается в sequential multi, и Reset не должен пропускать
    // единственный draw list.
    const bool parallelDraw = m_parallelRecording && drawLists >= 2;
    const char* modeName = packetDraw ? "packets" :
        (m_multiList ? (parallelDraw ? "parallel" : "multi") : "legacy");

    ID3D12GraphicsCommandList* commands[MAX_DRAW_LISTS + 2] = {};
    ID3D12CommandAllocator* allocators[MAX_DRAW_LISTS + 2] = {};
    commands[0] = frame.commandList.Get();
    allocators[0] = frame.commandAllocator.Get();
    for (uint32_t i = 0; i < drawLists; ++i)
    {
        commands[i + 1] = frame.drawRecording[i].list.Get();
        allocators[i + 1] = frame.drawRecording[i].allocator.Get();
    }
    if (m_multiList)
    {
        commands[drawLists + 1] = frame.ownerEnd.list.Get();
        allocators[drawLists + 1] = frame.ownerEnd.allocator.Get();
    }

    HRESULT hr=S_OK;
    for (uint32_t i=0;i<listCount;++i) {
        // Draw lists в parallel-режиме сбрасываются внутри своих задач:
        // owner не трогает их allocator до Wait.
        if (parallelDraw && i >= 1 && i <= drawLists) continue;
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
    if (frame.timestamps) cmd->EndQuery(frame.timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);

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
    const auto& viewProj = packetDraw ? ordered.frame.shaderCamera : prepared.shaderCamera;

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
    // F3.5.4A: Draw calls распределяются по L спискам в глобальном порядке.
    // Диапазон [first, first+count) непрерывен, поэтому суммарный порядок
    // Draw всегда 0..N-1 независимо от режима и числа списков.
    const auto drawRange = [drawCount, drawLists](uint32_t listIndex) {
        struct Range { uint32_t first; uint32_t count; };
        if (drawLists == 0)
            return Range{0, 0};
        const uint32_t first = static_cast<uint32_t>(
            static_cast<uint64_t>(drawCount) * listIndex / drawLists);
        const uint32_t last = static_cast<uint32_t>(
            static_cast<uint64_t>(drawCount) * (listIndex + 1) / drawLists);
        return Range{first, last - first};
    };
    // Один и тот же набор команд во всех режимах: каждый Draw рисует один
    // из двух кубов, глобальный порядок 0..N-1 не меняется.
    const auto drawObject = [&](ID3D12GraphicsCommandList* list, uint32_t globalIndex) {
        const UINT vertices = static_cast<UINT>(SmokeMesh12::kVertexCount / 2);
        const UINT firstVertex = static_cast<UINT>(globalIndex % 2) * vertices;
        return m_mesh.Draw(list, frame.constantBuffer->GetGPUVirtualAddress(),
                           firstVertex, vertices);
    };

    // F3.5.4A: test-only диагностика фактически записанных Draw. Сумма по
    // спискам обязана совпадать с N во всех режимах; расхождение — отказ
    // кадра, а не «мягкое» предупреждение. Счётчики не влияют на команды.
    uint32_t recordedPerList[MAX_DRAW_LISTS] = {};
    uint32_t recordedTotal = 0;

    if (m_multiList) {
        if (!closeList(0)) return false;
        // Параллельная запись имеет смысл только при >=2 draw lists.
        // При L=1 режим вырождается в sequential multi с одним списком.
        if (parallelDraw) {
            // Все заимствования живут до Wait; owner не меняет mesh/CB/descriptors.
            if (!m_jobs || !m_jobs->IsInitialized()) { m_recordFailed=true; return false; }
            struct Result { HRESULT hr=E_PENDING; bool closed=false; uint32_t draws=0; } results[MAX_DRAW_LISTS];
            std::atomic<unsigned> entered{0}, exited{0};
            std::atomic<bool> overlap{false};
            const unsigned faultStage=m_recordFaultStage, faultList=m_recordFaultList;
            const bool faultDraw=faultList>=1 && faultList<=drawLists;
            const bool rendezvous=faultDraw && faultStage!=~0u && faultStage!=4;
            // F3.5.3B: rendezvous требует минимум двух одновременных
            // исполнителей (background worker + поток, вызывающий Wait).
            // При totalParticipants < 2 система последовательна: второй
            // callback не стартует, пока первый ждёт, и проверка перекрытия
            // вырождается в таймаут. Такой прогон НЕ считается успешным:
            // недостаточная параллельность фиксируется как отказ теста, а не
            // маскируется под подтверждённое перекрытие.
            if (rendezvous)
            {
                const uint32_t participants = m_jobs->GetStats().totalParticipants;
                if (participants < 2)
                {
                    m_recordFailed = true;
                    dx12log::Write("[parallel-fault] insufficient parallelism: "
                                   "totalParticipants=%u, rendezvous requires >=2; test FAILED",
                                   participants);
                    return false;
                }
            }
            if (faultDraw) m_recordFaultStage=m_recordFaultList=~0u;
            std::exception_ptr failure;
            const auto schedulingStart=std::chrono::steady_clock::now();
            try {
                auto group=m_jobs->CreateGroup();
                try {
                    for (uint32_t i=0;i<drawLists;++i) {
                        if (faultDraw && faultStage==4 && i==1)
                            throw std::runtime_error("recording submit injection");
                        auto* draw=commands[i+1]; auto* allocator=allocators[i+1];
                        const SmokeMesh12* mesh=&m_mesh;
                        const auto range=drawRange(i);
                        const auto cb=frame.constantBuffer->GetGPUVirtualAddress();
                        const auto dsv=m_dsv;
                        group.Submit([i,draw,allocator,mesh,range,cb,dsv,rtvHandle,viewport,scissor,
                                      faultStage,faultList,rendezvous,&results,&entered,&exited,&overlap] {
                            struct Exit { std::atomic<unsigned>& value; ~Exit() { ++value; } } exit{exited};
                            ++entered;
                            // Только fault harness: подтверждённое перекрытие двух callbacks.
                            if (rendezvous) {
                                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                                while (entered.load()<2) {
                                    if (std::chrono::steady_clock::now()>deadline) {
                                        results[i].hr=E_ABORT; return;
                                    }
                                    std::this_thread::yield();
                                }
                                overlap=true;
                            }
                            const auto fault=[&](unsigned stage) { return faultList==i+1 && faultStage==stage; };
                            if (fault(5)) throw std::runtime_error("recording callback injection");
                            auto& result=results[i];
                            result.hr=fault(0)?E_FAIL:allocator->Reset();
                            if (FAILED(result.hr)) return;
                            result.hr=fault(1)?E_FAIL:draw->Reset(allocator,nullptr);
                            if (FAILED(result.hr)) return;
                            draw->OMSetRenderTargets(1,&rtvHandle,FALSE,&dsv);
                            draw->RSSetViewports(1,&viewport); draw->RSSetScissorRects(1,&scissor);
                            for (uint32_t d=0;d<range.count;++d) {
                                const uint32_t global=range.first+d;
                                const UINT firstVertex=static_cast<UINT>(global%2)*(SmokeMesh12::kVertexCount/2);
                                if (fault(2) || !mesh->Draw(draw,cb,firstVertex,SmokeMesh12::kVertexCount/2)) {
                                    result.hr=E_FAIL; return;
                                }
                            }
                            result.hr=fault(3)?E_FAIL:draw->Close();
                            result.closed=SUCCEEDED(result.hr);
                            if (result.closed) result.draws=range.count;
                        });
                    }
                } catch (...) { failure=std::current_exception(); }
                scheduleMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-schedulingStart).count();
                const auto waitStart=std::chrono::steady_clock::now();
                // Даже отказ второго Submit не обходит Wait первой задачи.
                //
                // RISK (A2, F3.5.3B): JobGroup::Wait() не имеет таймаута.
                // GPU_WAIT_TIMEOUT_MS здесь не действует — это ожидание CPU.
                // Если worker зависнет внутри callback, главный поток
                // заблокируется навсегда без диагностики. API JobGroup в этой
                // правке намеренно не меняется: риск зафиксирован здесь и в
                // отчёте, смягчение — внешний watchdog на уровне smoke.
                try { group.Wait(); } catch (...) { if (!failure) failure=std::current_exception(); }
                recordWaitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-waitStart).count();
            } catch (...) { failure=std::current_exception(); }
            if (faultDraw && faultStage!=~0u)
                dx12log::Write("[parallel-fault] stage=%u list=%u entered=%u exited=%u overlap=%u submitted=%llu",
                    faultStage,faultList,entered.load(),exited.load(),overlap.load()?1:0,(unsigned long long)m_recordFrames);
            uint32_t closedCount=0;
            for (uint32_t i=0;i<drawLists;++i) if (results[i].closed) ++closedCount;
            if (failure || closedCount!=drawLists) {
                m_recordFailed=true;
                dx12log::Write("[parallel-recording] ABORTED drained=%u closed=%u/%u",
                    entered.load()==exited.load()?1:0,closedCount,drawLists);
                return false;
            }
            for (uint32_t i=0;i<drawLists;++i) {
                recordedPerList[i]=results[i].draws;
                recordedTotal+=results[i].draws;
            }
        } else {
            for (uint32_t i=0;i<drawLists;++i) {
                cmd=commands[i+1];
                // Каждый Draw list самодостаточен: состояние соседнего list не наследуется.
                cmd->OMSetRenderTargets(1,&rtvHandle,FALSE,&m_dsv);
                cmd->RSSetViewports(1,&viewport); cmd->RSSetScissorRects(1,&scissor);
                const auto range=drawRange(i);
                for (uint32_t d=0;d<range.count;++d)
                    if (!drawObject(cmd,range.first+d)) { m_recordFailed=true; return false; }
                recordedPerList[i]=range.count;
                recordedTotal+=range.count;
                if (!closeList(i+1)) return false;
            }
        }
        cmd=commands[drawLists+1];
    } else if (packetDraw) {
        // F3.5.5A: Draw-параметры берутся только из OrderedFrame12.
        // m_drawCount и synthetic %2 в этой ветке не участвуют.
        const auto cbVA=frame.constantBuffer->GetGPUVirtualAddress();
        for (const renderprep12::DrawPacket12& packet : ordered.frame.packets) {
            if (!m_mesh.Draw(cmd,cbVA,packet.object.firstVertex,packet.object.vertexCount)) {
                m_recordFailed=true;
                dx12log::Write("[packet-draw] FAIL: Draw rejected packet range");
                return false;
            }
            ++recordedTotal;
            ++recordedPerList[0];
        }
    } else {
        for (uint32_t d=0;d<drawCount;++d)
            if (!drawObject(cmd,d)) { m_recordFailed=true; return false; }
        recordedPerList[0]=drawCount;
        recordedTotal=drawCount;
    }

    // Инвариант нагрузки: каждый режим записывает ровно N Draw.
    if (recordedTotal != drawCount) {
        m_recordFailed=true;
        dx12log::Write("[recording-draws] FAIL mode=%s lists=%u total=%u expected=%u",
            modeName,m_multiList?drawLists:1,recordedTotal,drawCount);
        return false;
    }
    if (m_recordFrames==0) {
        dx12log::Write("[recording-draws] mode=%s lists=%u total=%u expected=%u",
            modeName,m_multiList?drawLists:1,recordedTotal,drawCount);
        if (packetDraw)
            dx12log::Write("[packet-draw] visible=%u draws=%u",packetDrawCount,recordedTotal);
        if (m_testMode) {
            if (m_multiList) {
                for (uint32_t i=0;i<drawLists;++i)
                    dx12log::Write("[recording-draws] list=%u draws=%u",i+1,recordedPerList[i]);
            } else {
                dx12log::Write("[recording-draws] list=0 draws=%u",recordedPerList[0]);
            }
        }
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

        // F3.5.5A TEST ONLY: copy a 4x4 depth region into the tail of the
        // same readback buffer. The frame still records exactly the same Draw
        // set; this only adds a test readback.
        if (m_backgroundTest && m_backgroundDepthFootprint.Footprint.Width >= 4)
        {
            D3D12_RESOURCE_BARRIER depthToCopy = {};
            depthToCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            depthToCopy.Transition.pResource = m_depthBuffer.Get();
            depthToCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            depthToCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            depthToCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            cmd->ResourceBarrier(1, &depthToCopy);

            D3D12_TEXTURE_COPY_LOCATION depthDst = {};
            depthDst.pResource = m_readbackBuffer.Get();
            depthDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            depthDst.PlacedFootprint = m_backgroundDepthFootprint;

            D3D12_TEXTURE_COPY_LOCATION depthSrc = {};
            depthSrc.pResource = m_depthBuffer.Get();
            depthSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            depthSrc.SubresourceIndex = 0;

            const UINT depthX = (m_width - 4) / 2;
            const UINT depthY = (m_height - 4) / 2;
            D3D12_BOX depthBox = {depthX, depthY, 0, depthX + 4, depthY + 4, 1};
            cmd->CopyTextureRegion(&depthDst, 0, 0, 0, &depthSrc, &depthBox);

            D3D12_RESOURCE_BARRIER depthToWrite = depthToCopy;
            depthToWrite.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            depthToWrite.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            cmd->ResourceBarrier(1, &depthToWrite);
        }

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

    if (frame.timestamps) {
        cmd->EndQuery(frame.timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
        cmd->ResolveQueryData(frame.timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,frame.timestampReadback.Get(),0);
    }
    if (!closeList(m_multiList ? (drawLists+1) : 0)) return false;
    const double recordingMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-recordStart).count();
    ID3D12CommandList* lists[MAX_DRAW_LISTS + 2] = {};
    for (uint32_t i = 0; i < listCount; ++i)
        lists[i] = commands[i];
    const auto submitStart=std::chrono::steady_clock::now();
    m_commandQueue->ExecuteCommandLists(listCount,lists);
    const double submissionMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-submitStart).count();
    ++m_recordFrames;
    frame.timestampPending=bool(frame.timestamps); frame.timestampSample=m_recordFrames>30;
    if (m_recordFrames>30) {
        ++m_recordSamples; m_recordMs+=recordingMs; m_queueSubmitMs+=submissionMs; m_recordScheduleMs+=scheduleMs; m_recordWaitMs+=recordWaitMs;
        // F3.5.4B: один срез на кадр, без I/O. Горячий участок не логируется.
        if (m_timingSampleCount < MAX_TIMING_SAMPLES) {
            timingIndex=static_cast<int32_t>(m_timingSampleCount++);
            auto& sample=m_timingSamples[timingIndex];
            sample.frame=m_recordFrames;
            sample.recordingMs=recordingMs;
            sample.schedulingMs=scheduleMs;
            sample.waitMs=recordWaitMs;
            sample.submissionMs=submissionMs;
            sample.gpuMs=-1;
            sample.intervalMs=-1;
            if (frame.timestampSample) frame.timestampSampleIndex=timingIndex;
        } else {
            ++m_timingSamplesDropped;
        }
    }
    m_gpuWorkSubmitted = true;

    // Present — единственный источник DEVICE_REMOVED/DEVICE_RESET и единственное
    // место, откуда приходит DXGI_STATUS_OCCLUDED для свёрнутого окна.
    hr = m_swapChain->Present(1, 0);

    // F3.5.4A: интервал между Present. Present(1,0) ограничен vsync,
    // поэтому это wall-clock диагностика, а не FPS игры.
    {
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        if (m_lastPresentTick.QuadPart != 0 && freq.QuadPart > 0)
        {
            const double intervalMs =
                static_cast<double>(now.QuadPart - m_lastPresentTick.QuadPart) *
                1000.0 / static_cast<double>(freq.QuadPart);
            m_frameIntervalMs += intervalMs;
            ++m_frameIntervals;
            if (timingIndex>=0)
                m_timingSamples[timingIndex].intervalMs=intervalMs;
        }
        m_lastPresentTick = now;
    }

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
        for (uint32_t j=0;j<m_frameResources[i].drawListCount && j<MAX_DRAW_LISTS;++j) {
            m_frameResources[i].drawRecording[j].list.Reset();
            m_frameResources[i].drawRecording[j].allocator.Reset();
        }
        m_frameResources[i].ownerEnd.list.Reset();
        m_frameResources[i].ownerEnd.allocator.Reset();
        CollectGpuTiming(m_frameResources[i]);
        m_frameResources[i].timestamps.Reset(); m_frameResources[i].timestampReadback.Reset();
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
    dx12log::Write("[renderprep] %s mode=%u frames=%llu samples=%llu packets=%u snapshot=%.6f serial=%.6f parallel=%.6f submit=%.6f wait=%.6f merge=%.6f gpuFence=%.6f ms",
        m_prepFailed ? "FAIL" : "PASS",static_cast<unsigned>(m_prepMode),
        (unsigned long long)m_prepFrames,(unsigned long long)m_prepSamples,
        m_drawSource==DrawSource::Packets?m_visiblePackets:2u,
        m_prepSamples?m_snapshotMs/m_prepSamples:0,m_prepSamples?m_serialMs/m_prepSamples:0,
        m_prepSamples?m_parallelMs/m_prepSamples:0,m_prepSamples?m_submitMs/m_prepSamples:0,
        m_prepSamples?m_waitMs/m_prepSamples:0,m_prepSamples?m_mergeMs/m_prepSamples:0,
        m_gpuFenceWaits?m_gpuFenceMs/m_gpuFenceWaits:0);
    dx12log::Write("[recording] %s mode=%s lists=%u frames=%llu samples=%llu recording=%.6f submission=%.6f ms",
        m_recordFailed?"ABORTED":"PASS",(m_drawSource==DrawSource::Packets)?"packets":((m_parallelRecording && m_drawListCount>=2)?"parallel":(m_multiList?"multi":"legacy")),m_multiList?(m_drawListCount+2):1,
        (unsigned long long)m_recordFrames,(unsigned long long)m_recordSamples,
        m_recordSamples?m_recordMs/m_recordSamples:0,m_recordSamples?m_queueSubmitMs/m_recordSamples:0);
    dx12log::Write("[recording-cpu] scheduling=%.6f wait=%.6f ms",
        m_recordSamples?m_recordScheduleMs/m_recordSamples:0,m_recordSamples?m_recordWaitMs/m_recordSamples:0);
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

    if (lifetime==GpuLifetime::WorkCompleted)
        for (auto& frame:m_frameResources) CollectGpuTiming(frame);
    dx12log::Write("[recording-gpu] available=%u samples=%llu mean=%.6f ms",
        m_timestampFrequency?1:0,(unsigned long long)m_gpuSamples,m_gpuSamples?m_gpuMs/m_gpuSamples:0);
    dx12log::Write("[frame] interval=%.6f ms samples=%llu",
        m_frameIntervals?m_frameIntervalMs/static_cast<double>(m_frameIntervals):0.0,
        static_cast<unsigned long long>(m_frameIntervals));
    dx12log::Write("[metrics] per-frame samples=%u dropped=%u",
        m_timingSampleCount,m_timingSamplesDropped);

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
            for (uint32_t j=0;j<m_frameResources[i].drawListCount && j<MAX_DRAW_LISTS;++j) {
                m_frameResources[i].drawRecording[j].list.Reset();
                m_frameResources[i].drawRecording[j].allocator.Reset();
            }
            m_frameResources[i].ownerEnd.list.Reset();
            m_frameResources[i].ownerEnd.allocator.Reset();
            m_frameResources[i].timestamps.Reset(); m_frameResources[i].timestampReadback.Reset();
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
