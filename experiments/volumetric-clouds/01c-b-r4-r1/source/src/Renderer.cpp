#include "Renderer.h"

#include "AuditPolicy.h"
#include "CloudFaultInjection.h"

#include <dxgidebug.h>
#include <d3d12sdklayers.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace
{
[[noreturn]] void TerminalImmediateFailure(const char* label,
                                           const char* stage) noexcept
{
    TerminalFailClosed(
        "immediate GPU work SUBMITTED but GPU completion is UNKNOWN; "
        "TERMINAL fail-closed before any stack unwind or GPU resource release",
        label, stage);
}

[[noreturn]] void TerminalPreSubmitFailure(const char* label,
                                            const char* stage) noexcept
{
    TerminalFailClosed(
        "immediate GPU work pre-submit wait failed; GPU completion is UNKNOWN; "
        "TERMINAL fail-closed before any GPU resource release",
        label, stage);
}

[[noreturn]] void TerminalPresentFailure(const char* stage) noexcept
{
    TerminalFailClosed(
        "present GPU work submitted but the frame fence was not established; "
        "GPU completion is UNKNOWN; TERMINAL fail-closed before any GPU "
        "resource release",
        stage);
}

[[noreturn]] void TerminalFenceWaitFailure(const char* context) noexcept
{
    TerminalFailClosed(
        "GPU fence completion is UNKNOWN; TERMINAL fail-closed before any GPU "
        "resource release",
        context);
}

bool IsLiveObjectMessage(D3D12_MESSAGE_ID id)
{
    switch (id)
    {
    case D3D12_MESSAGE_ID_LIVE_DEVICE:
    case D3D12_MESSAGE_ID_LIVE_SWAPCHAIN:
    case D3D12_MESSAGE_ID_LIVE_COMMANDQUEUE:
    case D3D12_MESSAGE_ID_LIVE_COMMANDALLOCATOR:
    case D3D12_MESSAGE_ID_LIVE_PIPELINESTATE:
    case D3D12_MESSAGE_ID_LIVE_COMMANDLIST12:
    case D3D12_MESSAGE_ID_LIVE_RESOURCE:
    case D3D12_MESSAGE_ID_LIVE_DESCRIPTORHEAP:
    case D3D12_MESSAGE_ID_LIVE_ROOTSIGNATURE:
    case D3D12_MESSAGE_ID_LIVE_LIBRARY:
    case D3D12_MESSAGE_ID_LIVE_HEAP:
    case D3D12_MESSAGE_ID_LIVE_MONITOREDFENCE:
    case D3D12_MESSAGE_ID_LIVE_QUERYHEAP:
    case D3D12_MESSAGE_ID_LIVE_COMMANDSIGNATURE:
    case D3D12_MESSAGE_ID_LIVE_PIPELINELIBRARY:
        return true;
    default:
        return false;
    }
}

const char* SeverityName(D3D12_MESSAGE_SEVERITY severity)
{
    switch (severity)
    {
    case D3D12_MESSAGE_SEVERITY_CORRUPTION: return "CORRUPTION";
    case D3D12_MESSAGE_SEVERITY_ERROR: return "ERROR";
    case D3D12_MESSAGE_SEVERITY_WARNING: return "WARNING";
    case D3D12_MESSAGE_SEVERITY_INFO: return "INFO";
    case D3D12_MESSAGE_SEVERITY_MESSAGE: return "MESSAGE";
    default: return "UNKNOWN";
    }
}

const char* FeatureLevelName(D3D_FEATURE_LEVEL level)
{
    switch (level)
    {
    case D3D_FEATURE_LEVEL_12_2: return "12_2";
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    default: return "unknown";
    }
}

std::string DescribeAdapter(const DXGI_ADAPTER_DESC1& desc)
{
    std::ostringstream s;
    s << WideToUtf8(desc.Description)
      << " vendor=0x" << std::hex << desc.VendorId
      << " device=0x" << desc.DeviceId
      << " subsys=0x" << desc.SubSysId
      << std::dec
      << " vram=" << (desc.DedicatedVideoMemory / (1024ull * 1024ull)) << "MiB"
      << " shared=" << (desc.SharedSystemMemory / (1024ull * 1024ull)) << "MiB"
      << (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE ? " software" : " hardware");
    return s.str();
}
} // namespace

Renderer::Renderer() = default;

Renderer::~Renderer()
{
    Shutdown();
}

void Renderer::EnableDebugLayer(const Options& options)
{
    if (options.testNoValidation)
    {
        LogWarn("[test] --test-no-validation: simulating unavailable D3D12 debug/GPU validation infrastructure");
        return;
    }

    if (!options.debugLayer && !options.gpuValidation)
        return;

    ComPtr<ID3D12Debug> debug;
    const HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
    if (FAILED(hr))
    {
        LogWarn("D3D12 debug layer is unavailable (%s). "
                "Install the Windows 'Graphics Tools' optional feature for validation.",
                HrToString(hr).c_str());
        return;
    }

    debug->EnableDebugLayer();
    debugLayerEnabled_ = true;

    ComPtr<ID3D12Debug1> debug1;
    if (SUCCEEDED(debug.As(&debug1)))
    {
        debug1->SetEnableSynchronizedCommandQueueValidation(TRUE);
        if (options.gpuValidation)
        {
            debug1->SetEnableGPUBasedValidation(TRUE);
            gpuValidationEnabled_ = true;
        }
    }
    else if (options.gpuValidation)
    {
        LogWarn("ID3D12Debug1 is unavailable; GPU-based validation was not enabled");
    }

    ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings))))
    {
        dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }

    Log("D3D12 debug layer enabled%s", gpuValidationEnabled_ ? " (GPU-based validation ON)" : "");
}

void Renderer::Initialize(HWND window, const Options& options)
{
    if (initialized_)
        return;
    window_ = window;
    options_ = options;

    RECT client{};
    if (!GetClientRect(window_, &client))
        Fail("GetClientRect failed");
    width_ = static_cast<UINT>(std::max<LONG>(1, client.right - client.left));
    height_ = static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top));

    EnableDebugLayer(options_);

    const UINT factoryFlags = debugLayerEnabled_ ? DXGI_CREATE_FACTORY_DEBUG : 0;
    HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_));
    if (FAILED(hr) && factoryFlags != 0)
    {
        LogWarn("CreateDXGIFactory2(debug) failed (%s); retrying without DXGI debug",
                HrToString(hr).c_str());
        hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_));
    }
    ThrowIfFailed(hr, "CreateDXGIFactory2");

    CreateDevice(options_);

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_)),
                  "CreateCommandQueue");

    ThrowIfFailed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                       IID_PPV_ARGS(&fence_)),
                  "CreateFence");
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_)
        Fail("CreateEventW failed");

    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    srvUavDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.NumDescriptors = kBackBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_)),
                  "Create RTV descriptor heap");

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
    srvHeapDesc.NumDescriptors = kSrvUavDescriptorCount;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device_->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap_)),
                  "Create SRV/UAV descriptor heap");

    CreateSwapChain(window_);
    CreateBackBuffers();
    CreateFrameObjects();

    if (debugLayerEnabled_)
    {
        if (options_.testNoInfoQueue)
        {
            LogWarn("[test] --test-no-info-queue: debug layer enabled but ID3D12InfoQueue is hidden");
        }
        else if (SUCCEEDED(device_.As(&infoQueue_)))
        {
            infoQueueAvailable_ = true;
            infoQueue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
            infoQueue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
            infoQueue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);
            Log("D3D12 info queue attached");
        }
        else
        {
            LogWarn("ID3D12InfoQueue is unavailable; debug messages cannot be captured");
        }
    }

    initialized_ = true;
    Log("renderer initialized: %ux%u, %u back buffers, %s",
        width_, height_, kBackBufferCount,
        usingWarp_ ? "WARP" : "hardware adapter");
}

void Renderer::CreateDevice(const Options& options)
{
    struct Candidate
    {
        ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC1 desc;
    };

    std::vector<Candidate> candidates;
    for (UINT index = 0;; ++index)
    {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT hr = factory_->EnumAdapters1(index, adapter.GetAddressOf());
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        ThrowIfFailed(hr, "IDXGIFactory1::EnumAdapters1");

        DXGI_ADAPTER_DESC1 desc{};
        ThrowIfFailed(adapter->GetDesc1(&desc), "IDXGIAdapter1::GetDesc1");
        Log("adapter[%u] %s", index, DescribeAdapter(desc).c_str());
        candidates.push_back({adapter, desc});
    }

    if (candidates.empty())
        Fail("no DXGI adapters were enumerated");

    ComPtr<IDXGIAdapter1> selected;
    DXGI_ADAPTER_DESC1 selectedDesc{};
    bool selectedIsWarp = false;

    if (!options.forceWarp)
    {
        int chosenIndex = -1;
        if (options.adapterIndex >= 0)
        {
            if (static_cast<size_t>(options.adapterIndex) >= candidates.size())
            {
                std::ostringstream s;
                s << "--adapter=" << options.adapterIndex << " is out of range (0.."
                  << (candidates.size() - 1) << ")";
                Fail(s.str());
            }
            chosenIndex = options.adapterIndex;
        }
        else
        {
            SIZE_T bestMemory = 0;
            for (size_t i = 0; i < candidates.size(); ++i)
            {
                if (candidates[i].desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                    continue;
                if (chosenIndex < 0 || candidates[i].desc.DedicatedVideoMemory > bestMemory)
                {
                    chosenIndex = static_cast<int>(i);
                    bestMemory = candidates[i].desc.DedicatedVideoMemory;
                }
            }
            if (chosenIndex < 0)
                chosenIndex = 0;
        }

        selected = candidates[static_cast<size_t>(chosenIndex)].adapter;
        selectedDesc = candidates[static_cast<size_t>(chosenIndex)].desc;
    }

    HRESULT hr = E_FAIL;
    if (!options.forceWarp)
    {
        hr = D3D12CreateDevice(selected.Get(), D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&device_));
        if (FAILED(hr))
        {
            LogWarn("hardware D3D12 device creation failed (%s); falling back to WARP",
                    HrToString(hr).c_str());
            selected.Reset();
        }
    }

    if (!device_)
    {
        usingWarp_ = true;
        selectedIsWarp = true;
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory_->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)),
                      "EnumWarpAdapter");
        selected = nullptr;
        ComPtr<IDXGIAdapter1> warpAdapter1;
        ThrowIfFailed(warpAdapter.As(&warpAdapter1), "Query WARP adapter");
        selected = warpAdapter1;
        ThrowIfFailed(selected->GetDesc1(&selectedDesc), "Get WARP adapter desc");
        ThrowIfFailed(D3D12CreateDevice(selected.Get(), D3D_FEATURE_LEVEL_11_0,
                                        IID_PPV_ARGS(&device_)),
                      "D3D12CreateDevice (WARP)");
    }

    usingWarp_ = selectedIsWarp;
    adapterName_ = selectedDesc.Description;
    adapterInfo_ = DescribeAdapter(selectedDesc);

    Log("selected device: %s", adapterInfo_.c_str());

    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevels{};
    featureLevels.NumFeatureLevels = static_cast<UINT>(std::size(levels));
    featureLevels.pFeatureLevelsRequested = levels;
    featureLevels.MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,
                                               &featureLevels,
                                               sizeof(featureLevels))))
    {
        Log("maximum feature level: %s", FeatureLevelName(featureLevels.MaxSupportedFeatureLevel));
    }

    if (usingWarp_)
        LogWarn("running on WARP: results validate correctness, not GPU performance");
}

void Renderer::CreateSwapChain(HWND window)
{
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width_;
    desc.Height = height_;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kBackBufferCount;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    desc.Flags = 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(factory_->CreateSwapChainForHwnd(
                      queue_.Get(), window, &desc, nullptr, nullptr,
                      swapChain1.GetAddressOf()),
                  "CreateSwapChainForHwnd");
    ThrowIfFailed(swapChain1.As(&swapChain_), "Query IDXGISwapChain3");
    ThrowIfFailed(factory_->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER),
                  "MakeWindowAssociation");
}

void Renderer::CreateBackBuffers()
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kBackBufferCount; ++i)
    {
        ThrowIfFailed(swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i])),
                      "IDXGISwapChain3::GetBuffer");
        device_->CreateRenderTargetView(backBuffers_[i].Get(), nullptr, handle);
        handle.ptr += rtvDescriptorSize_;
    }
}

void Renderer::CreateFrameObjects()
{
    for (UINT i = 0; i < kBackBufferCount; ++i)
    {
        ThrowIfFailed(device_->CreateCommandAllocator(
                          D3D12_COMMAND_LIST_TYPE_DIRECT,
                          IID_PPV_ARGS(&frameAllocators_[i])),
                      "CreateCommandAllocator (frame)");
        ThrowIfFailed(device_->CreateCommandList(
                          0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                          frameAllocators_[i].Get(), nullptr,
                          IID_PPV_ARGS(&frameLists_[i])),
                      "CreateCommandList (frame)");
        ThrowIfFailed(frameLists_[i]->Close(), "Close new frame command list");
    }

    ThrowIfFailed(device_->CreateCommandAllocator(
                      D3D12_COMMAND_LIST_TYPE_DIRECT,
                      IID_PPV_ARGS(&immediateAllocator_)),
                  "CreateCommandAllocator (immediate)");
    ThrowIfFailed(device_->CreateCommandList(
                      0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                      immediateAllocator_.Get(), nullptr,
                      IID_PPV_ARGS(&immediateList_)),
                  "CreateCommandList (immediate)");
    ThrowIfFailed(immediateList_->Close(), "Close new immediate command list");
}

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::SrvCpuHandle(UINT slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(slot) * srvUavDescriptorSize_;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE Renderer::SrvGpuHandle(UINT slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(slot) * srvUavDescriptorSize_;
    return handle;
}

Renderer::Frame Renderer::BeginFrame()
{
    Frame frame{};
    if (!initialized_ || !swapChain_ || width_ == 0 || height_ == 0)
        return frame;

    const UINT index = swapChain_->GetCurrentBackBufferIndex();
    if (index >= kBackBufferCount || !backBuffers_[index])
        Fail("swap chain returned an invalid back buffer index");

    if (backBufferFenceValues_[index] != 0)
    {
        if (!TryWaitForFence(backBufferFenceValues_[index],
                            "wait for back buffer reuse"))
            TerminalFenceWaitFailure("back buffer reuse");
        clouds::NoteFrameFenceConfirmed();
    }

    // The reused back buffer is free, but that fence says nothing about frames
    // submitted after it: with three back buffers, later submissions can still
    // be in flight. Deferred present/debug diagnostics are therefore emitted
    // only when the queue fence has caught up with the LAST submission. The
    // check is non-blocking, so several frames stay in flight and CPU/GPU
    // overlap is preserved (no WaitForGpu here).
    FlushDiagnosticsIfIdle("frame");

    ThrowIfFailed(frameAllocators_[index]->Reset(), "Reset frame command allocator");
    ThrowIfFailed(frameLists_[index]->Reset(frameAllocators_[index].Get(), nullptr),
                  "Reset frame command list");

    ID3D12DescriptorHeap* heaps[] = { srvHeap_.Get() };
    frameLists_[index]->SetDescriptorHeaps(1, heaps);

    frame.list = frameLists_[index].Get();
    frame.backBufferIndex = index;
    frame.width = width_;
    frame.height = height_;
    frame.valid = true;
    return frame;
}

void Renderer::BeginRenderPass(const Frame& frame)
{
    if (!frame.valid)
        return;

    ID3D12Resource* backBuffer = backBuffers_[frame.backBufferIndex].Get();
    TransitionResource(frame.list, backBuffer,
                       D3D12_RESOURCE_STATE_PRESENT,
                       D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(frame.backBufferIndex) * rtvDescriptorSize_;

    const float clearColor[4] = { 0.035f, 0.04f, 0.055f, 1.0f };
    frame.list->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    frame.list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<FLOAT>(frame.width);
    viewport.Height = static_cast<FLOAT>(frame.height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    frame.list->RSSetViewports(1, &viewport);

    D3D12_RECT scissor{ 0, 0, static_cast<LONG>(frame.width),
                        static_cast<LONG>(frame.height) };
    frame.list->RSSetScissorRects(1, &scissor);
}

void Renderer::EndRenderPass(const Frame& frame)
{
    if (!frame.valid)
        return;
    TransitionResource(frame.list, backBuffers_[frame.backBufferIndex].Get(),
                       D3D12_RESOURCE_STATE_RENDER_TARGET,
                       D3D12_RESOURCE_STATE_PRESENT);
}

void Renderer::Present(Frame& frame, std::vector<uint8_t>* captureRgba)
{
    if (!frame.valid)
        return;

    clouds::LifetimeWitness owners(clouds::WitnessCode::ArmPresentFrame);
    ID3D12Resource* backBuffer = backBuffers_[frame.backBufferIndex].Get();

    if (captureRgba)
    {
        EnsureScreenReadback();
        TransitionResource(frame.list, backBuffer,
                           D3D12_RESOURCE_STATE_PRESENT,
                           D3D12_RESOURCE_STATE_COPY_SOURCE);

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = screenReadback_.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = screenFootprint_;

        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = backBuffer;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = 0;

        frame.list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        TransitionResource(frame.list, backBuffer,
                           D3D12_RESOURCE_STATE_COPY_SOURCE,
                           D3D12_RESOURCE_STATE_PRESENT);
    }

    // The frame fence must be established before any post-submit diagnostic.
    // No logger, info-queue drain or other blocking sink is called here.
    uint64_t fenceValue = 0;
    bool submitted = false;
    try
    {
        ThrowIfFailed(frame.list->Close(), "Close frame command list");
        ID3D12CommandList* lists[] = { frame.list };
        queue_->ExecuteCommandLists(1, lists);
        submitted = true;
        clouds::NoteCommandListsSubmitted(frame.list);
        clouds::WitnessEvent(clouds::WitnessCode::ExecuteCommandLists);

        fenceValue = ++fenceValue_;
        if (!TrySignalFence(fenceValue, "signal frame fence"))
            TerminalPresentFailure("frame fence signal");
        lastSubmittedFence_ = fenceValue;
        clouds::NoteFrameSubmitted();
        backBufferFenceValues_[frame.backBufferIndex] = fenceValue;
        clouds::WitnessEvent(clouds::WitnessCode::SignalIssued);

        // Capture runs headless and should not be paced by the monitor refresh.
        const bool vsync = options_.vsync && captureRgba == nullptr;
        const HRESULT presentResult = swapChain_->Present(vsync ? 1u : 0u, 0);
        if (presentResult == DXGI_STATUS_OCCLUDED)
        {
            if (!occluded_)
                pendingPresentNotice_ = 1;
            occluded_ = true;
        }
        else
        {
            if (occluded_)
                pendingPresentNotice_ = 2;
            occluded_ = false;
#if defined(OGSR_CLOUDS_FAULT_INJECTION)
            // Test-only: exercise the deferred present-notice path (the same
            // path a real occlusion takes) while later submissions may still be
            // unconfirmed. The notice is emitted only after a confirmed idle.
            if (clouds::TestEnvFlag("OGSR_CLOUD_FORCE_PRESENT_NOTICE"))
            {
                pendingPresentNotice_ = 1;
                clouds::WitnessEvent(
                    clouds::WitnessCode::DiagnosticNoticeDeferred);
            }
#endif
            if (FAILED(presentResult))
            {
                ReportDeviceRemoved("Present");
                clouds::WitnessEvent(clouds::WitnessCode::FaultInjected);
                TerminalPresentFailure("Present failed");
            }
        }
    }
    catch (...)
    {
        if (submitted)
            TerminalPresentFailure("present/signal after submit");
        throw;
    }

    if (captureRgba)
    {
        if (!TryWaitForFence(fenceValue, "wait for capture copy"))
            TerminalPresentFailure("capture fence completion unknown");
        clouds::WitnessEvent(clouds::WitnessCode::FenceConfirmed);
        clouds::NoteFenceConfirmed(fenceValue, fence_->GetCompletedValue());
        FlushPendingPresentNotice();

        // Completion is confirmed: the remaining capture diagnostics may
        // propagate normally without risking an in-flight resource.
        D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(screenReadbackBytes_) };
        void* mapped = nullptr;
        ThrowIfFailed(screenReadback_->Map(0, &readRange, &mapped),
                      "Map capture readback buffer");
        captureRgba->resize(static_cast<size_t>(width_) * height_ * 4);
        const auto* bytes = static_cast<const uint8_t*>(mapped);
        for (UINT y = 0; y < height_; ++y)
        {
            std::memcpy(captureRgba->data() + static_cast<size_t>(y) * width_ * 4,
                        bytes + static_cast<size_t>(y) * screenRowPitch_,
                        static_cast<size_t>(width_) * 4);
        }
        const D3D12_RANGE writtenRange{ 0, 0 };
        screenReadback_->Unmap(0, &writtenRange);

        DrainDebugMessages("frame");
    }
    else
    {
        // No frame-local fence confirmation on the interactive path. Defer
        // the info-queue drain to the next BeginFrame/Shutdown protected wait.
        pendingFrameDebugDrain_ = true;
    }

    frame.valid = false;
}

void Renderer::FlushPendingPresentNotice()
{
    if (pendingPresentNotice_ == 1)
    {
        pendingPresentNotice_ = 0;
        LogWarnBestEffort(
            "window is occluded; presenting without rendering pressure");
    }
    else if (pendingPresentNotice_ == 2)
    {
        pendingPresentNotice_ = 0;
        LogBestEffort("window restored from occlusion");
    }
}

void Renderer::EnsureScreenReadback()
{
    if (screenReadback_ && screenReadbackWidth_ == width_ &&
        screenReadbackHeight_ == height_)
        return;

    screenReadback_.Reset();
    screenReadbackWidth_ = 0;
    screenReadbackHeight_ = 0;
    screenReadbackBytes_ = 0;

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = width_;
    textureDesc.Height = height_;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    UINT rowCount = 0;
    UINT64 totalBytes = 0;
    UINT64 rowSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    device_->GetCopyableFootprints(&textureDesc, 0, 1, 0,
                                   &footprint, &rowCount, &rowSize, &totalBytes);

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    ThrowIfFailed(device_->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                      IID_PPV_ARGS(&screenReadback_)),
                  "Create screen readback buffer");

    screenFootprint_ = footprint;
    screenRowPitch_ = footprint.Footprint.RowPitch;
    screenReadbackWidth_ = width_;
    screenReadbackHeight_ = height_;
    screenReadbackBytes_ = totalBytes;
    Log("capture readback buffer: %ux%u rowPitch=%u bytes=%llu",
        width_, height_, screenRowPitch_,
        static_cast<unsigned long long>(screenReadbackBytes_));
}

void Renderer::Resize(UINT width, UINT height)
{
    if (!initialized_ || !swapChain_ || width == 0 || height == 0)
        return;
    if (width == width_ && height == height_)
        return;

    // The resize retires every back buffer. Completion must be confirmed
    // before the first release and before any diagnostic can throw.
    if (deviceRemoved_)
    {
        TerminalFailClosed(
            "resize: device removed; GPU completion is UNKNOWN; TERMINAL "
            "fail-closed before resetting back buffers");
    }
    if (!TryWaitForGpu())
    {
        TerminalFailClosed(
            "resize: GPU completion is UNKNOWN; TERMINAL fail-closed before "
            "resetting back buffers");
    }

    LogBestEffort("resize: %ux%u -> %ux%u", width_, height_, width, height);
    clouds::LifetimeWitness backBufferOwners(clouds::WitnessCode::ArmResizeBackBuffers);

    for (auto& buffer : backBuffers_)
        buffer.Reset();
    screenReadback_.Reset();
    screenReadbackWidth_ = 0;
    screenReadbackHeight_ = 0;
    screenReadbackBytes_ = 0;

    ThrowIfFailed(swapChain_->ResizeBuffers(
                      0, width, height, DXGI_FORMAT_UNKNOWN, 0),
                  "IDXGISwapChain3::ResizeBuffers");

    width_ = width;
    height_ = height;
    CreateBackBuffers();

    for (auto& value : backBufferFenceValues_)
        value = 0;
    DrainDebugMessages("resize");
}

bool Renderer::TrySignalFence(uint64_t value, const char* context) noexcept
{
    const HRESULT hr = queue_->Signal(fence_.Get(), value);
    if (SUCCEEDED(hr))
        return true;
    // No ThrowIfFailed/Fail here: their logging is a blocking sink on a
    // post-submit failure path, and a thrown exception would allocate on the
    // mandatory path to TerminalFailClosed. The caller reaches terminal.
    ReportDeviceRemoved(context);
    return false;
}

bool Renderer::TryWaitForFence(uint64_t value, const char* context,
                               DWORD timeoutMs) noexcept
{
    if (!fence_ || !fenceEvent_)
        return false;

    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;)
    {
        // GetCompletedValue() returns UINT64_MAX after device removal. This
        // check must happen before any >= comparison: otherwise the sentinel
        // looks like a completed fence and the allocator can be reused on a
        // dead device.
        const uint64_t completed = fence_->GetCompletedValue();
        const audit::FenceCompletion state =
            audit::ClassifyFenceCompletion(completed, value);
        if (state == audit::FenceCompletion::Completed)
            return true;
        if (state == audit::FenceCompletion::DeviceRemoved)
        {
            ReportDeviceRemoved(context, true);
            clouds::WitnessEvent(clouds::WitnessCode::DeviceRemoved);
            clouds::WitnessEvent(clouds::WitnessCode::WaitFailed);
            return false;
        }

        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
        {
            ReportDeviceRemoved(context);
            clouds::WitnessEvent(clouds::WitnessCode::WaitFailed);
            return false;
        }

        const ULONGLONG remaining64 = deadline - now;
        const DWORD remaining = remaining64 > MAXDWORD
            ? MAXDWORD
            : static_cast<DWORD>(remaining64);

        const HRESULT hr = fence_->SetEventOnCompletion(value, fenceEvent_);
        if (FAILED(hr))
        {
            ReportDeviceRemoved(context);
            clouds::WitnessEvent(clouds::WitnessCode::WaitFailed);
            return false;
        }

        const DWORD waitResult = WaitForSingleObject(fenceEvent_, remaining);
        if (waitResult == WAIT_TIMEOUT)
        {
            // Re-check the fence after waking: completion and timeout can race.
            const uint64_t after = fence_->GetCompletedValue();
            const audit::FenceCompletion afterState =
                audit::ClassifyFenceCompletion(after, value);
            if (afterState == audit::FenceCompletion::Completed)
                return true;
            if (afterState == audit::FenceCompletion::DeviceRemoved)
            {
                ReportDeviceRemoved(context, true);
                clouds::WitnessEvent(clouds::WitnessCode::DeviceRemoved);
                clouds::WitnessEvent(clouds::WitnessCode::WaitFailed);
                return false;
            }

            ReportDeviceRemoved(context);
            clouds::WitnessEvent(clouds::WitnessCode::WaitFailed);
            return false;
        }
        if (waitResult != WAIT_OBJECT_0)
        {
            ReportDeviceRemoved(context);
            clouds::WitnessEvent(clouds::WitnessCode::WaitFailed);
            return false;
        }

        // The event can be stale/spurious; loop and verify the actual fence
        // value instead of assuming that a signaled event means completion.
    }
}

bool Renderer::TryWaitForGpu() noexcept
{
    if (!initialized_ || !queue_ || !fence_)
        return true;
    const uint64_t value = ++fenceValue_;
    if (!TrySignalFence(value, "signal full GPU wait"))
        return false;
    lastSubmittedFence_ = value;
    if (!TryWaitForFence(value, "wait for GPU idle"))
        return false;
    lastConfirmedFence_ = value;
    clouds::NoteGpuIdleConfirmed(value, fence_->GetCompletedValue());
    return true;
}

bool Renderer::GpuIdleConfirmed() const noexcept
{
    if (!fence_)
        return false;
    // The device-removal sentinel (UINT64_MAX) must never be read as "idle":
    // completion is unknown, not confirmed.
    if (deviceRemoved_)
        return false;
    if (lastSubmittedFence_ == 0)
        return true;
    const uint64_t completed = fence_->GetCompletedValue();
    if (completed == UINT64_MAX)
        return false;
    return completed >= lastSubmittedFence_;
}

uint32_t Renderer::InflightSubmissionCount() const noexcept
{
    if (!fence_ || lastSubmittedFence_ == 0)
        return 0;
    const uint64_t completed = fence_->GetCompletedValue();
    if (completed == UINT64_MAX || completed >= lastSubmittedFence_)
        return 0;
    // Every submission signals a distinct fence value, so this difference is a
    // conservative lower bound on the frames still executing or queued.
    return static_cast<uint32_t>(lastSubmittedFence_ - completed);
}

bool Renderer::FlushDiagnosticsIfIdle(const char* phase)
{
    if (!pendingPresentNotice_ && !pendingFrameDebugDrain_)
        return false;
    if (!GpuIdleConfirmed())
    {
        // Later submissions are still in flight. A diagnostic sink here could
        // block while GPU work is unconfirmed, so it is deferred instead.
        clouds::NoteDiagnosticsDeferred(
            static_cast<long>(InflightSubmissionCount()));
        return false;
    }
    clouds::NoteGpuIdleConfirmed(lastSubmittedFence_,
                                 fence_->GetCompletedValue());
    clouds::NoteDiagnosticsFlushed(lastSubmittedFence_,
                                   fence_->GetCompletedValue());
    FlushPendingPresentNotice();
    if (pendingFrameDebugDrain_)
    {
        pendingFrameDebugDrain_ = false;
        clouds::WitnessEvent(clouds::WitnessCode::DiagnosticDrainDone);
        try
        {
            DrainDebugMessages(phase);
        }
        catch (...)
        {
        }
    }
    return true;
}

void Renderer::ExecuteImmediate(
    const std::function<void(ID3D12GraphicsCommandList*)>& record,
    const char* label)
{
    if (!initialized_)
        Fail("ExecuteImmediate called before renderer initialization");

    clouds::LifetimeWitness owners(clouds::WitnessCode::ArmImmediateOwners);

    // Negative control for the submission witness: this injected failure occurs
    // before the first command list is submitted, so the shared-memory witness
    // must report submitCount == 0 and no execute-command-lists event.
    if (clouds::ConsumeCloudFault(clouds::CloudFaultPoint::PreSubmitWait))
    {
        clouds::WitnessEvent(clouds::WitnessCode::FaultInjected);
        clouds::WitnessEvent(clouds::WitnessCode::WaitFailed);
        TerminalPreSubmitFailure(label, "pre-submit wait (injected)");
    }

    if (!TryWaitForGpu())
    {
        clouds::WitnessEvent(clouds::WitnessCode::WaitFailed);
        TerminalPreSubmitFailure(label, "pre-submit wait");
    }

    clouds::MaybeActivateBlockedPipes(label);

    // The blocked-pipe matrix switches stdout/stderr to a full pipe here: after
    // the pre-submit wait has confirmed GPU idle and before the command list is
    // reset, recorded and submitted. Everything from this statement to the
    // terminal action must stay free of diagnostic sinks, which is exactly what
    // the blocked-pipe cases measure. Activation happens only in the immediate
    // that actually consumes the pending terminal fault, so unrelated immediates
    // keep their normal diagnostics.
#if defined(OGSR_CLOUDS_FAULT_INJECTION)
    if (clouds::TerminalFaultPending())
        clouds::MaybeActivateBlockedPipes("immediate-terminal-fault");
#endif

    ThrowIfFailed(immediateAllocator_->Reset(), "Reset immediate allocator");
    ThrowIfFailed(immediateList_->Reset(immediateAllocator_.Get(), nullptr),
                  "Reset immediate command list");
    ID3D12DescriptorHeap* heaps[] = { srvHeap_.Get() };
    immediateList_->SetDescriptorHeaps(1, heaps);

    const auto start = std::chrono::steady_clock::now();
    record(immediateList_.Get());
    ThrowIfFailed(immediateList_->Close(), "Close immediate command list");

    ID3D12CommandList* lists[] = { immediateList_.Get() };
    queue_->ExecuteCommandLists(1, lists);
    // Recorded only after the real submission call returned; the recorded
    // pointer is the live command list that was just submitted.
    clouds::NoteCommandListsSubmitted(immediateList_.Get());
    clouds::WitnessEvent(clouds::WitnessCode::ExecuteCommandLists);

    // Protected post-submit interval. There is deliberately no logger call
    // and no other blocking I/O between submission and the confirmed fence.
    // Test-only logger failures are injected by a direct throwing hook; the
    // production path has no diagnostic sink here at all.
    uint64_t fenceValue = 0;
    const char* stage = "post-ExecuteCommandLists";
    try
    {
        stage = "post-submit logger fault";
        clouds::MaybeThrowPostSubmitLoggerFault(1);
        if (clouds::ConsumeCloudFault(
                clouds::CloudFaultPoint::RendererPostSubmit))
        {
            clouds::WitnessEvent(clouds::WitnessCode::FaultInjected);
            TerminalImmediateFailure(label, "post-ExecuteCommandLists");
        }

        stage = "signal";
        fenceValue = ++fenceValue_;
        if (!TrySignalFence(fenceValue, "signal immediate GPU work"))
            TerminalImmediateFailure(label, "signal");
        lastSubmittedFence_ = fenceValue;
        clouds::WitnessEvent(clouds::WitnessCode::SignalIssued);

        stage = "post-signal logger fault";
        clouds::MaybeThrowPostSubmitLoggerFault(2);
        if (clouds::ConsumeCloudFault(
                clouds::CloudFaultPoint::RendererPostSignal))
        {
            clouds::WitnessEvent(clouds::WitnessCode::FaultInjected);
            TerminalImmediateFailure(label, "post-signal pre-fence");
        }

        stage = "fence wait";
        if (!TryWaitForFence(fenceValue, "wait for immediate GPU work"))
            TerminalImmediateFailure(label, "fence wait");
    }
    catch (...)
    {
        TerminalImmediateFailure(label, stage);
    }

    clouds::WitnessEvent(clouds::WitnessCode::FenceConfirmed);
    clouds::NoteFenceConfirmed(fenceValue, fence_->GetCompletedValue());

    // Completion is confirmed. The human-readable marker is emitted only now,
    // after the fence, so a blocked logger can no longer hold a submission
    // boundary open.
    LogBestEffort(
        "immediate GPU work '%s': SUBMITTED -> SIGNALLED -> COMPLETION "
        "CONFIRMED", label);

    const auto end = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    LogBestEffort(
        "immediate GPU work '%s' completed in %.2f ms (wall, includes "
        "submit+idle wait)", label, ms);
    try
    {
        DrainDebugMessages(label);
    }
    catch (...)
    {
        LogErrorBestEffort(
            "immediate GPU work '%s': post-fence debug drain failed", label);
    }
}

void Renderer::DrainDebugMessages(const char* phase)
{
    if (!infoQueue_)
        return;

    const UINT64 count = infoQueue_->GetNumStoredMessages();
    UINT64 printBudget = 100;
    for (UINT64 i = 0; i < count; ++i)
    {
        SIZE_T length = 0;
        if (FAILED(infoQueue_->GetMessage(i, nullptr, &length)) || length == 0)
            continue;

        std::vector<uint8_t> storage(length);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (FAILED(infoQueue_->GetMessage(i, message, &length)))
            continue;

        const D3D12_MESSAGE_SEVERITY severity = message->Severity;
        const bool liveObject = IsLiveObjectMessage(message->ID);
        if (liveObject)
        {
            ++liveObjectMessages_;
            if (message->ID != D3D12_MESSAGE_ID_LIVE_DEVICE)
                ++liveNonDeviceObjects_;
        }
        if (severity == D3D12_MESSAGE_SEVERITY_ERROR ||
            severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
            ++debugErrors_;
        else if (severity == D3D12_MESSAGE_SEVERITY_WARNING)
            ++debugWarnings_;

        if (printBudget > 0)
        {
            --printBudget;
            Log("[d3d12 %s][%s/%u] %s",
                SeverityName(severity), phase,
                static_cast<unsigned>(message->ID),
                message->pDescription ? message->pDescription : "(no description)");
        }
    }

    if (count > 100)
        Log("[d3d12] %llu additional messages were counted but not printed",
            static_cast<unsigned long long>(count - 100));
    if (count != 0)
        infoQueue_->ClearStoredMessages();
}

void Renderer::PollDebugMessages(const char* phase)
{
    DrainDebugMessages(phase);
}

bool Renderer::InjectDebugError()
{
    if (!infoQueue_)
        return false;

    const char* text = "V2.3 test injected debug error";
    return SUCCEEDED(infoQueue_->AddMessage(
        D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED,
        D3D12_MESSAGE_SEVERITY_ERROR,
        D3D12_MESSAGE_ID_UNKNOWN,
        text));
}

void Renderer::ReportDeviceRemoved(const char* context, bool fenceSentinel)
{
    // This function is reached from post-submit wait/signal/present failure
    // paths. It must not log, enumerate DRED allocations or touch any blocking
    // diagnostic sink: the caller is on a path to TerminalFailClosed.
    (void)context;
    if (!device_)
    {
        if (fenceSentinel)
            deviceRemoved_ = true;
        return;
    }

    lastDeviceRemovedReason_ = device_->GetDeviceRemovedReason();
    if (fenceSentinel || FAILED(lastDeviceRemovedReason_))
        deviceRemoved_ = true;
}

void Renderer::ReportLiveObjects()
{
    if (!debugLayerEnabled_)
        return;

    ComPtr<IDXGIDebug1> dxgiDebug;
    if (FAILED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
    {
        LogWarn("IDXGIDebug1 is unavailable; live-object report was not requested");
        return;
    }

    Log("requesting DXGI live-object summary (OutputDebugString; capture with a debugger)");
    dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
}

void Renderer::Shutdown()
{
#if defined(OGSR_CLOUDS_FAULT_INJECTION)
    if (clouds::ShutdownFaultPendingFor(clouds::CloudFaultPoint::ShutdownRendererWait))
        clouds::MaybeActivateBlockedPipes("renderer-shutdown");
#endif
    if (!initialized_ && !device_)
        return;

    clouds::LifetimeWitness ownerWitness(clouds::WitnessCode::ArmRendererOwners);

    if (initialized_)
    {
        if (deviceRemoved_)
        {
            TerminalFailClosed(
                "renderer shutdown: device already removed; GPU completion is "
                "UNKNOWN; TERMINAL fail-closed before releasing back buffers, "
                "allocators, lists, fence or queue");
        }

        if (clouds::ConsumeCloudFault(
                clouds::CloudFaultPoint::ShutdownRendererWait))
        {
            clouds::WitnessEvent(clouds::WitnessCode::FaultInjected);
            clouds::WitnessEvent(clouds::WitnessCode::WaitFailed);
            TerminalFailClosed(
                "renderer shutdown: injected GPU wait failure; completion is "
                "UNKNOWN; TERMINAL fail-closed before releasing back buffers, "
                "allocators, lists, fence or queue");
        }

        if (!TryWaitForGpu())
        {
            TerminalFailClosed(
                "renderer shutdown: GPU completion is UNKNOWN; TERMINAL "
                "fail-closed before releasing back buffers, allocators, lists, "
                "fence or queue");
        }
    }

    // Completion is confirmed. All remaining diagnostics are best-effort and
    // must not prevent the resource release below.
    FlushPendingPresentNotice();
    if (pendingFrameDebugDrain_)
    {
        pendingFrameDebugDrain_ = false;
        try
        {
            DrainDebugMessages("shutdown");
        }
        catch (...)
        {
        }
    }
    LogBestEffort("renderer shutdown: GPU idle");
    LogBestEffort("renderer shutdown: draining debug messages");
    try
    {
        DrainDebugMessages("shutdown");
    }
    catch (...)
    {
        LogErrorBestEffort("renderer shutdown: debug drain failed");
    }
    LogBestEffort("debug message totals: %llu errors, %llu warnings",
                  static_cast<unsigned long long>(debugErrors_),
                  static_cast<unsigned long long>(debugWarnings_));

    LogBestEffort("renderer shutdown: releasing resources");
    screenReadback_.Reset();
    for (auto& buffer : backBuffers_)
        buffer.Reset();
    rtvHeap_.Reset();
    swapChain_.Reset();

    for (auto& list : frameLists_)
        list.Reset();
    for (auto& allocator : frameAllocators_)
        allocator.Reset();
    immediateList_.Reset();
    immediateAllocator_.Reset();

    if (fenceEvent_)
    {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
    fence_.Reset();
    srvHeap_.Reset();
    queue_.Reset();

    // ReportLiveDeviceObjects must run while the device and info queue are
    // still alive, and after every app-owned resource has been released.
    if (debugLayerEnabled_ && device_ && infoQueue_)
    {
        ComPtr<ID3D12DebugDevice> debugDevice;
        if (SUCCEEDED(device_.As(&debugDevice)))
        {
            LogBestEffort("renderer shutdown: reporting live device objects");
            debugDevice->ReportLiveDeviceObjects(
                D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
            try
            {
                DrainDebugMessages("live objects");
            }
            catch (...)
            {
                LogErrorBestEffort("renderer shutdown: live-object drain failed");
            }
            LogBestEffort(
                "live-object messages emitted: %llu (non-device objects: %llu)",
                static_cast<unsigned long long>(liveObjectMessages_),
                static_cast<unsigned long long>(liveNonDeviceObjects_));
        }
    }

    infoQueue_.Reset();
    infoQueueAvailable_ = false;
    device_.Reset();
    factory_.Reset();

    LogBestEffort("renderer shutdown: requesting DXGI live object report");
    try
    {
        ReportLiveObjects();
    }
    catch (...)
    {
        LogErrorBestEffort("renderer shutdown: DXGI live-object report failed");
    }

    initialized_ = false;
    if (deviceRemoved_)
    {
        try
        {
            LogErrorBestEffort(
                "renderer shutdown complete after DEVICE REMOVAL (reason %s)",
                HrToString(lastDeviceRemovedReason_).c_str());
        }
        catch (...)
        {
        }
    }
    else
    {
        LogBestEffort("renderer shutdown complete");
    }
}
