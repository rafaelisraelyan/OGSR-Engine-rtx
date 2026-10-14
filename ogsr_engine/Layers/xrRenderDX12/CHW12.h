#pragma once

#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// F2.1-B: минимальный DX12 bootstrap для отдельного smoke-исполняемого файла.
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
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }
    bool IsDeviceRemoved() const { return m_deviceRemoved; }
    bool IsOccluded() const { return m_occluded; }

private:
    static constexpr uint32_t FRAME_COUNT = 3;
    static constexpr DXGI_FORMAT BACKBUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DWORD GPU_WAIT_TIMEOUT_MS = 5000;

    struct FrameResource
    {
        ComPtr<ID3D12CommandAllocator> commandAllocator;
        ComPtr<ID3D12GraphicsCommandList> commandList;
        ComPtr<ID3D12Resource> renderTarget;
        uint64_t fenceValue = 0;
    };

    bool CreateDevice();
    bool CreateSwapChain(HWND hwnd, uint32_t width, uint32_t height);
    bool CreateRenderTargetViews();
    bool CreateFrameResources();
    bool EnableDebugLayer();
    bool WaitForFenceValue(uint64_t value);
    bool WaitForGpu();
    void MoveToNextFrame();
    void DrainInfoQueue();
    void ReportDeviceRemoved(HRESULT hr);

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

    // Synchronization
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    uint64_t m_fenceValue = 0;

    // State
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_initialized = false;
    bool m_deviceRemoved = false;
    bool m_occluded = false;
    bool m_debugLayerEnabled = false;
};
