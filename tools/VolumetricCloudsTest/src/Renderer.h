#pragma once

#include "Common.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Owns the D3D12 device, command queue, swap chain and all per-frame
// synchronization. It knows nothing about noise generation or Win32 input.
class Renderer
{
public:
    struct Options
    {
        bool forceWarp = false;
        int adapterIndex = -1;
        bool debugLayer = false;
        bool gpuValidation = false;
        bool vsync = true;
        // Test-only hook: force the validation infrastructure to be treated as
        // unavailable without touching the real GPU/OS configuration.
        bool testNoValidation = false;
        // Test-only hook: keep the debug layer but hide ID3D12InfoQueue.
        bool testNoInfoQueue = false;
    };

    struct Frame
    {
        ID3D12GraphicsCommandList* list = nullptr;
        UINT backBufferIndex = 0;
        UINT width = 0;
        UINT height = 0;
        bool valid = false;
    };

    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void Initialize(HWND window, const Options& options);
    void Shutdown();

    // ResizeBuffers is only called when the window has a non-zero client area.
    void Resize(UINT width, UINT height);

    Frame BeginFrame();
    void BeginRenderPass(const Frame& frame);
    void EndRenderPass(const Frame& frame);

    // Closes and submits the frame, presents it, and (when captureRgba is not
    // null) copies the back buffer to a readback buffer and returns tightly
    // packed RGBA8 pixels. The capture path waits for the frame fence.
    void Present(Frame& frame, std::vector<uint8_t>* captureRgba);

    void WaitForGpu();
    void ExecuteImmediate(
        const std::function<void(ID3D12GraphicsCommandList*)>& record,
        const char* label);

    ID3D12Device* Device() const { return device_.Get(); }
    ID3D12CommandQueue* Queue() const { return queue_.Get(); }
    ID3D12DescriptorHeap* SrvHeap() const { return srvHeap_.Get(); }

    D3D12_CPU_DESCRIPTOR_HANDLE SrvCpuHandle(UINT slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle(UINT slot) const;

    UINT Width() const { return width_; }
    UINT Height() const { return height_; }
    bool UsingWarp() const { return usingWarp_; }
    const std::wstring& AdapterName() const { return adapterName_; }
    const std::string& AdapterInfo() const { return adapterInfo_; }
    uint64_t DebugErrorCount() const { return debugErrors_; }
    uint64_t DebugWarningCount() const { return debugWarnings_; }
    uint64_t LiveObjectMessageCount() const { return liveObjectMessages_; }
    uint64_t LiveNonDeviceObjectCount() const { return liveNonDeviceObjects_; }
    bool DebugLayerEnabled() const { return debugLayerEnabled_; }
    bool GpuValidationEnabled() const { return gpuValidationEnabled_; }
    bool InfoQueueAvailable() const { return infoQueueAvailable_; }
    bool DebugLayerRequested() const
    {
        return options_.debugLayer || options_.gpuValidation;
    }
    bool GpuValidationRequested() const { return options_.gpuValidation; }
    bool DeviceRemoved() const { return deviceRemoved_; }
    HRESULT LastDeviceRemovedReason() const { return lastDeviceRemovedReason_; }

private:
    static constexpr UINT kBackBufferCount = 3;
    static constexpr UINT kSrvUavDescriptorCount = 16;

    void EnableDebugLayer(const Options& options);
    void CreateDevice(const Options& options);
    void CreateSwapChain(HWND window);
    void CreateBackBuffers();
    void CreateFrameObjects();
    void EnsureScreenReadback();

    void WaitForFence(uint64_t value, const char* context, DWORD timeoutMs = 30000);
    void SignalFence(uint64_t value, const char* context);
    void DrainDebugMessages(const char* phase);
    void ReportDeviceRemoved(const char* context, bool fenceSentinel = false);
    void ReportLiveObjects();

    HWND window_ = nullptr;
    Options options_{};
    UINT width_ = 0;
    UINT height_ = 0;
    UINT rtvDescriptorSize_ = 0;
    UINT srvUavDescriptorSize_ = 0;
    bool initialized_ = false;
    bool usingWarp_ = false;
    bool debugLayerEnabled_ = false;
    bool gpuValidationEnabled_ = false;
    bool infoQueueAvailable_ = false;
    bool occluded_ = false;
    bool deviceRemoved_ = false;
    HRESULT lastDeviceRemovedReason_ = S_OK;

    std::wstring adapterName_;
    std::string adapterInfo_;

    ComPtr<IDXGIFactory4> factory_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swapChain_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12DescriptorHeap> srvHeap_;
    ComPtr<ID3D12Resource> backBuffers_[kBackBufferCount];
    ComPtr<ID3D12CommandAllocator> frameAllocators_[kBackBufferCount];
    ComPtr<ID3D12GraphicsCommandList> frameLists_[kBackBufferCount];
    ComPtr<ID3D12CommandAllocator> immediateAllocator_;
    ComPtr<ID3D12GraphicsCommandList> immediateList_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    uint64_t fenceValue_ = 0;
    uint64_t backBufferFenceValues_[kBackBufferCount]{};

    ComPtr<ID3D12Resource> screenReadback_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT screenFootprint_{};
    UINT screenRowPitch_ = 0;
    UINT screenReadbackWidth_ = 0;
    UINT screenReadbackHeight_ = 0;
    uint64_t screenReadbackBytes_ = 0;

    ComPtr<ID3D12InfoQueue> infoQueue_;
    uint64_t debugErrors_ = 0;
    uint64_t debugWarnings_ = 0;
    uint64_t liveObjectMessages_ = 0;
    uint64_t liveNonDeviceObjects_ = 0;
};
