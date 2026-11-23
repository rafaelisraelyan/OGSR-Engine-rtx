#pragma once

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include <array>
#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

// Единственный swapchain выбранного игрового HWND в режиме DX12.
// Device, очередь и общий монотонный fence принадлежат владельцу рендера.
class GameDx12NativeFrame12
{
public:
    static constexpr uint32_t kFrameCount = 2;

    struct Recording
    {
        ID3D12GraphicsCommandList* list = nullptr;
        uint32_t slot = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsv{};
        ID3D12Resource* color = nullptr;
        ID3D12Resource* depth = nullptr;
        ID3D12Resource* opaqueColor = nullptr;
        ID3D12Device* device = nullptr;
        double slotFenceWaitMs = 0.0;

        void BeginWater() const noexcept;
        void EndWater() const noexcept;
    };

    struct Owner
    {
        ID3D12Device& device;
        ID3D12CommandQueue& queue;
        ID3D12Fence& fence;
        HANDLE event;
        uint64_t& fenceValue;
    };

    struct GpuTiming
    {
        uint64_t present = 0;
        double milliseconds = 0;
    };

    GameDx12NativeFrame12() = default;
    GameDx12NativeFrame12(const GameDx12NativeFrame12&) = delete;
    GameDx12NativeFrame12& operator=(const GameDx12NativeFrame12&) = delete;
    ~GameDx12NativeFrame12();

    HRESULT Initialize(HWND window, IDXGIFactory2& factory, const Owner& owner);
    HRESULT Begin(HWND window, const Owner& owner, Recording& recording);
    HRESULT Present(const Owner& owner, uint64_t sceneFrame);
    HRESULT Cancel() noexcept;
    HRESULT Drain(const Owner& owner) noexcept;
    bool CompletionUnknown() const noexcept { return m_unknown; }
    bool IsInitialized() const noexcept { return m_swapchain != nullptr; }
    uint64_t PresentCount() const noexcept { return m_presentCount; }
    const GpuTiming& LastGpuTiming() const noexcept { return m_lastGpuTiming; }
    void Release() noexcept;
    void Quarantine() noexcept;

private:
    template <typename T> using Ptr = Microsoft::WRL::ComPtr<T>;

    struct Slot
    {
        Ptr<ID3D12Resource> color;
        Ptr<ID3D12Resource> opaqueColor;
        Ptr<ID3D12CommandAllocator> allocator;
        Ptr<ID3D12GraphicsCommandList> list;
        uint64_t fence = 0;
        uint64_t timingPresent = 0;
    };

    HRESULT Wait(const Owner& owner, uint64_t value) noexcept;
    HRESULT Resize(const Owner& owner, uint32_t width, uint32_t height);
    HRESULT CreateTargets(ID3D12Device& device, uint32_t width, uint32_t height);
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv(uint32_t index) const noexcept;
    bool RecordCapture(ID3D12Device& device, Slot& slot);
    HRESULT FinishCapture(const Owner& owner, uint64_t fence);

    std::array<Slot, kFrameCount> m_slots;
    Ptr<IDXGISwapChain3> m_swapchain;
    Ptr<ID3D12DescriptorHeap> m_rtv;
    Ptr<ID3D12DescriptorHeap> m_dsv;
    Ptr<ID3D12Resource> m_depth;
    Ptr<ID3D12QueryHeap> m_timingQueries;
    Ptr<ID3D12Resource> m_timingReadback;
    Ptr<ID3D12Resource> m_captureReadback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_captureLayout{};
    wchar_t m_capturePath[MAX_PATH]{};
    uint64_t m_captureStride = 0;
    uint32_t m_captureCount = 0;
    uint64_t m_timestampFrequency = 0;
    GpuTiming m_lastGpuTiming;
    uint32_t m_rtvStride = 0;
    uint32_t m_dsvStride = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_slot = 0;
    uint64_t m_presentCount = 0;
    bool m_recording = false;
    bool m_submitted = false;
    bool m_unknown = false;
    bool m_measureFrame = false;
};

#endif
