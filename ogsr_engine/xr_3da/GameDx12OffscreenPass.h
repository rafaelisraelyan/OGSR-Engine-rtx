#pragma once

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct BridgeQuarantineStorage;

// Owner-side offscreen color/depth/readback target. The bridge owns the
// device/queue/fence/allocator/list; this class owns only the target
// resources and descriptor heaps. Release is legal only after confirmed GPU
// completion; unknown completion is moved into BridgeQuarantineStorage.
class GameDx12OffscreenPass12
{
public:
    GameDx12OffscreenPass12() = default;
    ~GameDx12OffscreenPass12();
    GameDx12OffscreenPass12(const GameDx12OffscreenPass12&) = delete;
    GameDx12OffscreenPass12& operator=(const GameDx12OffscreenPass12&) = delete;
    bool Create(ID3D12Device* device, uint32_t width, uint32_t height,
                std::string& reason);
    bool RecordBegin(ID3D12GraphicsCommandList* list,
                     const float clearColor[4], std::string& reason);
    bool RecordEnd(ID3D12GraphicsCommandList* list, std::string& reason);
    // Restores color-state tracking when a recorded list is abandoned before
    // ExecuteCommandLists. Safe only for an unsubmitted list.
    void AbortUnsubmittedFrame() noexcept;
    // Clears pending-record tracking only after the matching Draw fence has
    // been confirmed complete. Never call while submission is unknown.
    void CommitSubmittedFrame() noexcept;
    bool Readback(std::string& reason);
    bool SavePng(const wchar_t* path, std::string& reason) const;
    void Release();
    void Quarantine(BridgeQuarantineStorage& quarantine);

    bool IsCreated() const { return m_created; }
    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }
    uint32_t RowPitch() const { return m_rowPitch; }
    const std::vector<uint8_t>& Pixels() const { return m_pixels; }

private:
    ComPtr<ID3D12Resource> m_color;
    ComPtr<ID3D12Resource> m_depth;
    ComPtr<ID3D12Resource> m_readback;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_dsv{};
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_rowPitch = 0;
    uint64_t m_readbackBytes = 0;
    bool m_created = false;
    bool m_colorCopySource = false;
    bool m_recordStarted = false;
    bool m_colorCopySourceBeforeRecord = false;
    std::vector<uint8_t> m_pixels;
};

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
