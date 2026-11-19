#pragma once

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "GameDx12ReadbackCoverage.h"
#include "GameDx12VisualMask.h"
#include "GameDx12M23Lifetime.h"
#include "dx12fourvisualplan.h"
#include "dx12objectconstants.h"
#include "dx12xrayasset.h"

#include <DirectXMath.h>

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class GameDx12OffscreenPass12;

// F4.0-J2B-M23: bounded owner-side registry and diagnostic frame for four real
// Jupiter visuals (9, 105, 106, 107).
//
// Lifetime contract:
//   - the four VB/IB pairs are created and uploaded transactionally by one
//     owner submission; the exact upload fence is captured and confirmed
//     before any draw is recorded;
//   - the combined pass records exactly four DrawIndexedInstanced calls in one
//     owner command list, followed by four isolated single-visual passes that
//     provide the per-visual pixel oracle;
//   - every submission captures the exact owner fence/device identity and the
//     last-draw-use fence; resources are only released after that fence and
//     the device-removed reason are positively confirmed;
//   - unknown completion quarantines the GPU objects for process lifetime
//     instead of releasing them.
//
// The class is deliberately separate from the J1 single-visual adapter and
// from the synthetic GpuMeshRegistry12 so M23 cannot regress either path.
struct GameDx12FourVisualOwner12
{
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12Fence* fence = nullptr;
    HANDLE fenceEvent = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    uint64_t* fenceValue = nullptr;
};

class GameDx12FourVisual12
{
public:
    static constexpr DWORD kFenceTimeoutMs = 10000;
    static constexpr uint32_t kSlotCount = fourvisual12::kSlotCount;

    struct SlotResult12
    {
        uint32_t visualId = 0;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;
        uint64_t combinedPixels = 0;
        uint64_t isolatedPixels = 0;
        uint32_t combinedMinX = 0;
        uint32_t combinedMinY = 0;
        uint32_t combinedMaxX = 0;
        uint32_t combinedMaxY = 0;
        bool pngSaved = false;
    };

    struct Result12
    {
        uint64_t uploadFence = 0;
        uint64_t combinedFence = 0;
        uint64_t isolatedFences[kSlotCount] = {};
        uint64_t lastDrawUseFence = 0;
        uint32_t combinedDrawCount = 0;
        uint32_t isolatedDrawCount = 0;
        uint32_t cullMode = 0; // D3D12_CULL_MODE
        bool gpuCompleted = false;
        bool imageGatePass = false;
        bool pngSaved = false;
        GameDx12ReadbackCoverage12 combinedCoverage{};
        SlotResult12 slots[kSlotCount];
    };

    GameDx12FourVisual12() = default;
    ~GameDx12FourVisual12();
    GameDx12FourVisual12(const GameDx12FourVisual12&) = delete;
    GameDx12FourVisual12& operator=(const GameDx12FourVisual12&) = delete;

    // Owner-only VFS load/upload of all four visuals. Reads $level$/level and
    // $level$/level.geom once, validates the M0 counts and tint/plan contract,
    // then uploads four independent VB/IB pairs in one submission. No draw is
    // recorded. On failure before submission nothing is left published; on
    // failure after submission the class quarantines itself.
    bool LoadFromVfs(const GameDx12FourVisualOwner12& owner,
                     const char* levelName,
                     uint64_t levelSession,
                     uint64_t levelEpoch,
                     std::string& reason);

    // Owner-thread one-shot diagnostic. Records and submits the combined
    // four-draw pass, then four isolated passes, reading back and saving PNGs
    // when pngDirectory is non-empty. Returns false on any safety or image
    // gate failure; Result12::gpuCompleted distinguishes "GPU confirmed,
    // oracle failed" from "completion unknown".
    bool RenderDiagnostic(const GameDx12FourVisualOwner12& owner,
                          const DirectX::XMFLOAT4X4& logicalViewProjection,
                          const float cameraPosition[3],
                          GameDx12OffscreenPass12& offscreen,
                          uint32_t width,
                          uint32_t height,
                          const wchar_t* pngDirectory,
                          Result12* result,
                          std::string& reason);

    void OnLevelTeardown(uint64_t nextEpoch);
    void OnDeviceReset(uint64_t nextEpoch);

    // True after an upload/pass submission that has not been positively
    // confirmed, or after a recorded pass without completion.
    bool GpuCompletionUnknown() const noexcept
    {
        return m_gpuUnknown || m_quarantined || m_passRecorded ||
               m_passSubmitted || (m_uploadSubmitted && !m_uploadConfirmed);
    }

    // Safe only when the owner confirms that no M23 GPU work remains.
    // Anything else is moved to process-lifetime quarantine, never released.
    void Shutdown(bool gpuCompletionConfirmed);

    bool IsLoaded() const noexcept { return m_loaded; }
    bool ListPoisoned() const noexcept { return m_listPoisoned; }
    uint64_t UploadFenceValue() const noexcept { return m_uploadFenceValue; }
    uint64_t LastDrawUseFenceValue() const noexcept
    {
        return m_lastDrawUseFenceValue;
    }
    uint64_t LevelSession() const noexcept { return m_levelSession; }
    uint64_t LevelEpoch() const noexcept { return m_levelEpoch; }
    uint64_t Generation() const noexcept { return m_generation; }
    uint32_t VisualId(uint32_t slot) const noexcept
    {
        return slot < kSlotCount ? m_slots[slot].visualId : 0;
    }
    uint32_t CullMode() const noexcept { return m_cullMode; }
    const std::string& LastError() const noexcept { return m_lastError; }

private:
    struct SlotGpu12
    {
        M23Ptr12<ID3D12Resource> vertexBuffer;
        M23Ptr12<ID3D12Resource> indexBuffer;
        M23Ptr12<ID3D12Resource> uploadVertex;
        M23Ptr12<ID3D12Resource> uploadIndex;
        D3D12_VERTEX_BUFFER_VIEW vertexView{};
        D3D12_INDEX_BUFFER_VIEW indexView{};
        XrayAssetData12 asset;
        uint32_t visualId = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint64_t generation = 0;
    };

    bool Fail(std::string& reason, const char* text);
    bool Fail(std::string& reason, const std::string& text);
    bool LoadFromVfsImpl(const GameDx12FourVisualOwner12& owner,
                         const char* levelName,
                         uint64_t levelSession,
                         uint64_t levelEpoch,
                         std::string& reason);
    bool ReadVfsFile(const char* name, uint64_t maxBytes,
                     std::vector<uint8_t>& bytes, std::string& reason);
    bool CreatePipeline(ID3D12Device* device, std::string& reason);
    bool CreateConstants(ID3D12Device* device, std::string& reason);
    bool CreateGeometryBuffers(ID3D12Device* device,
                               ID3D12GraphicsCommandList* list,
                               std::string& reason);
    bool PrepareStaging(const DirectX::XMFLOAT4X4& logicalViewProjection,
                        const float cameraPosition[3], std::string& reason);
    bool RecordDraws(ID3D12GraphicsCommandList* list, uint32_t slotMask,
                     bool combined, std::string& reason);
    struct PassResult12
    {
        bool gpuConfirmed = false;
        bool completed = false;
        uint64_t fence = 0;
        GameDx12ReadbackCoverage12 coverage{};
    };
    bool RunPass(const GameDx12FourVisualOwner12& owner,
                 GameDx12OffscreenPass12& offscreen, uint32_t slotMask,
                 bool combined, bool wantPng, const wchar_t* pngPath,
                 PassResult12& result, std::string& reason);
    bool SubmitPass(const GameDx12FourVisualOwner12& owner,
                    uint32_t slotMask, std::string& reason);
    bool MarkPassSubmitted(uint32_t slotMask, uint64_t fenceValue,
                           ID3D12Fence* fence, ID3D12Device* device,
                           std::string& reason);
    bool OnPassCompleted(uint64_t fenceValue, ID3D12Fence* fence,
                         ID3D12Device* device, std::string& reason);
    void AbortRecordedPass() noexcept;
    void FailPassUnknown(const char* stage, std::string& reason) noexcept;
    void InvalidateStaging();
    void ReleaseGpu();
    void QuarantineGpu() noexcept;
    bool RenderDiagnosticImpl(
        const GameDx12FourVisualOwner12& owner,
        const DirectX::XMFLOAT4X4& logicalViewProjection,
        const float cameraPosition[3], GameDx12OffscreenPass12& offscreen,
        uint32_t width, uint32_t height, const wchar_t* pngDirectory,
        Result12* result, std::string& reason);
    bool HandleRenderException(std::string& reason) noexcept;
    bool HandleLoadException(std::string& reason) noexcept;
    void LogProbe(const char* stage) noexcept;

    SlotGpu12 m_slots[kSlotCount];
    fourvisual12::StagedTransform12 m_staged[kSlotCount];

    M23Ptr12<ID3D12RootSignature> m_rootSignature;
    M23Ptr12<ID3D12PipelineState> m_pipelineState;
    M23Ptr12<ID3D12Resource> m_constants;
    void* m_constantsMapped = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS m_frameCBVA = 0;
    D3D12_GPU_VIRTUAL_ADDRESS m_objectCBVA[kSlotCount] = {};

    uint32_t m_cullMode = D3D12_CULL_MODE_BACK;
    uint64_t m_levelSession = 0;
    uint64_t m_levelEpoch = 0;
    uint64_t m_generation = 0;
    uint64_t m_uploadFenceValue = 0;
    uint64_t m_lastDrawUseFenceValue = 0;
    bool m_loaded = false;
    bool m_stagingPrepared = false;
    bool m_uploadSubmitted = false;
    bool m_uploadConfirmed = false;
    bool m_passRecorded = false;
    bool m_passSubmitted = false;
    bool m_gpuUnknown = false;
    bool m_quarantined = false;
    bool m_listPoisoned = false;
    bool m_lastRecordingClosed = false;
    bool m_shutdownProbeLogged = false;
    bool m_unknownProbeLogged = false;

    uint32_t m_passSlotMask = 0;
    uint64_t m_passFenceValue = 0;
    // Captured only for the active submission; retained while completion is
    // unknown so resources cannot outlive owner identity checks.
    M23Ptr12<ID3D12Fence> m_passFence;
    M23Ptr12<ID3D12Device> m_passDevice;
    std::string m_lastError;
};

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
