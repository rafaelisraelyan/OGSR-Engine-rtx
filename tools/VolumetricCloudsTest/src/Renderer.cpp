#include "Renderer.h"

#include "AuditPolicy.h"

#include <dxgidebug.h>
#include <d3d12sdklayers.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iterator>
#include <sstream>

namespace
{
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
        WaitForFence(backBufferFenceValues_[index], "wait for back buffer reuse");

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

    ThrowIfFailed(frame.list->Close(), "Close frame command list");
    ID3D12CommandList* lists[] = { frame.list };
    queue_->ExecuteCommandLists(1, lists);

    const uint64_t fenceValue = ++fenceValue_;
    SignalFence(fenceValue, "signal frame fence");
    backBufferFenceValues_[frame.backBufferIndex] = fenceValue;

    // Capture runs headless and should not be paced by the monitor refresh.
    const bool vsync = options_.vsync && captureRgba == nullptr;
    const HRESULT presentResult = swapChain_->Present(vsync ? 1u : 0u, 0);
    if (presentResult == DXGI_STATUS_OCCLUDED)
    {
        if (!occluded_)
            LogWarn("window is occluded; presenting without rendering pressure");
        occluded_ = true;
    }
    else
    {
        if (occluded_)
            Log("window restored from occlusion");
        occluded_ = false;
        if (FAILED(presentResult))
        {
            ReportDeviceRemoved("Present");
            ThrowIfFailed(presentResult, "IDXGISwapChain3::Present");
        }
    }

    if (captureRgba)
    {
        WaitForFence(fenceValue, "wait for capture copy");

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
    }

    DrainDebugMessages("frame");
    frame.valid = false;
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

    Log("resize: %ux%u -> %ux%u", width_, height_, width, height);
    WaitForGpu();

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

void Renderer::SignalFence(uint64_t value, const char* context)
{
    const HRESULT hr = queue_->Signal(fence_.Get(), value);
    if (FAILED(hr))
    {
        ReportDeviceRemoved(context);
        ThrowIfFailed(hr, context);
    }
}

void Renderer::WaitForFence(uint64_t value, const char* context, DWORD timeoutMs)
{
    if (!fence_ || !fenceEvent_)
        Fail("fence is not initialized");

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
            return;
        if (state == audit::FenceCompletion::DeviceRemoved)
        {
            ReportDeviceRemoved(context, true);
            std::ostringstream s;
            s << context
              << ": ID3D12Fence::GetCompletedValue() returned UINT64_MAX "
                 "(device removed)";
            Fail(s.str());
        }

        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
        {
            ReportDeviceRemoved(context);
            std::ostringstream s;
            s << context << ": GPU fence timeout after " << timeoutMs << " ms";
            Fail(s.str());
        }

        const ULONGLONG remaining64 = deadline - now;
        const DWORD remaining = remaining64 > MAXDWORD
            ? MAXDWORD
            : static_cast<DWORD>(remaining64);

        const HRESULT hr = fence_->SetEventOnCompletion(value, fenceEvent_);
        if (FAILED(hr))
        {
            ReportDeviceRemoved(context);
            ThrowIfFailed(hr, "ID3D12Fence::SetEventOnCompletion");
        }

        const DWORD waitResult = WaitForSingleObject(fenceEvent_, remaining);
        if (waitResult == WAIT_TIMEOUT)
        {
            // Re-check the fence after waking: completion and timeout can race.
            const uint64_t after = fence_->GetCompletedValue();
            const audit::FenceCompletion afterState =
                audit::ClassifyFenceCompletion(after, value);
            if (afterState == audit::FenceCompletion::Completed)
                return;
            if (afterState == audit::FenceCompletion::DeviceRemoved)
            {
                ReportDeviceRemoved(context, true);
                std::ostringstream s;
                s << context
                  << ": ID3D12Fence::GetCompletedValue() returned UINT64_MAX "
                     "after wait timeout (device removed)";
                Fail(s.str());
            }

            ReportDeviceRemoved(context);
            std::ostringstream s;
            s << context << ": GPU fence timeout after " << timeoutMs << " ms";
            Fail(s.str());
        }
        if (waitResult != WAIT_OBJECT_0)
        {
            ReportDeviceRemoved(context);
            std::ostringstream s;
            s << context << ": WaitForSingleObject failed (result "
              << waitResult << ")";
            Fail(s.str());
        }

        // The event can be stale/spurious; loop and verify the actual fence
        // value instead of assuming that a signaled event means completion.
    }
}

void Renderer::WaitForGpu()
{
    if (!initialized_ || !queue_ || !fence_)
        return;
    const uint64_t value = ++fenceValue_;
    SignalFence(value, "signal full GPU wait");
    WaitForFence(value, "wait for GPU idle");
}

void Renderer::ExecuteImmediate(
    const std::function<void(ID3D12GraphicsCommandList*)>& record,
    const char* label)
{
    if (!initialized_)
        Fail("ExecuteImmediate called before renderer initialization");

    WaitForGpu();

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
    WaitForGpu();

    const auto end = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    Log("immediate GPU work '%s' completed in %.2f ms (wall, includes submit+idle wait)",
        label, ms);
    DrainDebugMessages(label);
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

void Renderer::ReportDeviceRemoved(const char* context, bool fenceSentinel)
{
    if (!device_)
        return;

    const HRESULT reason = device_->GetDeviceRemovedReason();
    lastDeviceRemovedReason_ = reason;
    const bool removed = fenceSentinel || FAILED(reason);
    if (!removed)
    {
        LogWarn("device removal check (%s): device is healthy (%s)",
                context, HrToString(reason).c_str());
        return;
    }

    if (deviceRemoved_)
    {
        LogWarn("device removal already recorded (%s, reason %s)",
                context, HrToString(lastDeviceRemovedReason_).c_str());
        return;
    }

    deviceRemoved_ = true;
    LogError("DEVICE REMOVED (%s): %s%s", context,
             HrToString(reason).c_str(),
             fenceSentinel ? " [fence GetCompletedValue()==UINT64_MAX]" : "");

    ComPtr<ID3D12DeviceRemovedExtendedData> dred;
    if (FAILED(device_.As(&dred)))
    {
        LogWarn("DRED interface is unavailable for this device");
        return;
    }

    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&breadcrumbs)))
    {
        UINT nodeIndex = 0;
        for (const D3D12_AUTO_BREADCRUMB_NODE* node = breadcrumbs.pHeadAutoBreadcrumbNode;
             node != nullptr && nodeIndex < 8;
             node = node->pNext, ++nodeIndex)
        {
            const UINT last = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
            LogError("DRED cmdlist[%u]: %u breadcrumbs, last completed %u",
                     nodeIndex, node->BreadcrumbCount, last);
            if (node->pCommandHistory)
            {
                const UINT limit = std::min<UINT>(node->BreadcrumbCount, 64);
                for (UINT i = 0; i < limit; ++i)
                    LogError("  op[%u] = %u", i,
                             static_cast<unsigned>(node->pCommandHistory[i]));
            }
        }
    }

    D3D12_DRED_PAGE_FAULT_OUTPUT pageFault{};
    if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pageFault)))
    {
        if (pageFault.PageFaultVA != 0)
        {
            LogError("DRED page fault VA: 0x%llx",
                     static_cast<unsigned long long>(pageFault.PageFaultVA));
            const D3D12_DRED_ALLOCATION_NODE* node = pageFault.pHeadExistingAllocationNode;
            for (UINT i = 0; node != nullptr && i < 16; node = node->pNext, ++i)
            {
                LogError("  existing allocation: %s type=%u",
                         node->ObjectNameA ? node->ObjectNameA : "(unnamed)",
                         static_cast<unsigned>(node->AllocationType));
            }
        }
    }
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
    if (!initialized_ && !device_)
        return;

    if (initialized_)
    {
        bool gpuIdle = true;
        if (deviceRemoved_)
        {
            gpuIdle = false;
            LogWarn("renderer shutdown: device already removed; best-effort cleanup");
        }
        else
        {
            Log("renderer shutdown: waiting for GPU");
            try
            {
                WaitForGpu();
            }
            catch (const std::exception& e)
            {
                gpuIdle = false;
                LogError("renderer shutdown: GPU wait failed: %s", e.what());
            }
        }

        if (gpuIdle)
            Log("renderer shutdown: GPU idle");
        else
            LogWarn("renderer shutdown: best-effort cleanup after unsuccessful GPU wait");
    }

    Log("renderer shutdown: draining debug messages");
    DrainDebugMessages("shutdown");
    Log("debug message totals: %llu errors, %llu warnings",
        static_cast<unsigned long long>(debugErrors_),
        static_cast<unsigned long long>(debugWarnings_));

    Log("renderer shutdown: releasing resources");
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
            Log("renderer shutdown: reporting live device objects");
            debugDevice->ReportLiveDeviceObjects(
                D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
            DrainDebugMessages("live objects");
            Log("live-object messages emitted: %llu (non-device objects: %llu)",
                static_cast<unsigned long long>(liveObjectMessages_),
                static_cast<unsigned long long>(liveNonDeviceObjects_));
        }
    }

    infoQueue_.Reset();
    infoQueueAvailable_ = false;
    device_.Reset();
    factory_.Reset();

    Log("renderer shutdown: requesting DXGI live object report");
    ReportLiveObjects();

    initialized_ = false;
    if (deviceRemoved_)
    {
        LogError("renderer shutdown complete after DEVICE REMOVAL (reason %s)",
                 HrToString(lastDeviceRemovedReason_).c_str());
    }
    else
    {
        Log("renderer shutdown complete");
    }
}
