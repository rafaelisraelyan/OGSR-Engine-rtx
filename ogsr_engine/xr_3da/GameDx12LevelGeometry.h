#pragma once

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "dx12geometryregistry.h"
#include "dx12objectconstants.h"
#include "dx12packetplan.h"
#include "dx12xrayasset.h"
#include "dx12xraylevelgeometry.h"

#include <DirectXMath.h>

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// F4.0-J1.2A: owner-side Jupiter level-geometry adapter.
//
// The class is intentionally not a renderer and does not own a device, queue,
// fence, allocator or command list. The GameDx12Bridge owner supplies those
// objects. LoadFromVfs uses the real game VFS ($level$/level and
// $level$/level.geom), validates one visual through xraylevel12::ReadVisual,
// builds the CPU asset through BuildXrayLevelAsset12, uploads it through the
// existing GpuMeshRegistry12 and waits for the bridge-owned fence before it
// publishes the loaded state.
//
// PrepareFrame consumes the real game ViewProjection and a placement world
// supplied by the level/object owner. It never invents a world transform and
// never uses a test camera. RecordDraw only records the single prepared
// indexed draw on a caller-owned list; submission and fence confirmation stay
// with the owner.
struct GameDx12LevelGeometryOwner12
{
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12Fence* fence = nullptr;
    HANDLE fenceEvent = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    uint64_t* fenceValue = nullptr;
};

class GameDx12LevelGeometry12
{
public:
    static constexpr DWORD kUploadFenceTimeoutMs = 10000;

    GameDx12LevelGeometry12() = default;
    ~GameDx12LevelGeometry12();

    GameDx12LevelGeometry12(const GameDx12LevelGeometry12&) = delete;
    GameDx12LevelGeometry12& operator=(const GameDx12LevelGeometry12&) = delete;

    // Owner-only VFS load and upload. On failure the registry is either
    // released (no GPU submission) or quarantined (completion unknown); the
    // output reason is best-effort and may be empty on allocation failure.
    bool LoadFromVfs(const GameDx12LevelGeometryOwner12& owner,
                     const char* levelName,
                     uint32_t visualId,
                     uint64_t levelSession,
                     uint64_t levelEpoch,
                     std::string& reason);

    // Owner-side frame preparation. logicalViewProjection is the game
    // View*Projection matrix in row-vector convention; logicalWorld is the
    // real placement transform. The helper transposes both for the existing
    // column-major HLSL constants.
    bool PrepareFrame(const DirectX::XMFLOAT4X4& logicalViewProjection,
                      const DirectX::XMFLOAT4X4& logicalWorld,
                      std::string& reason);

    // Records the single indexed draw on a caller-owned list. Submission is a
    // separate owner step: MarkDrawSubmitted captures the exact owner fence and
    // device together with the positive fence value. OnDrawCompleted accepts
    // only that captured COM identity and verifies it before releasing the
    // draw lifetime.
    bool RecordDraw(ID3D12GraphicsCommandList* list,
                    std::string& reason);
    bool MarkDrawSubmitted(uint64_t fenceValue, ID3D12Fence* fence,
                           ID3D12Device* device, std::string& reason);
    bool OnDrawCompleted(uint64_t fenceValue, ID3D12Fence* fence,
                         ID3D12Device* device, std::string& reason);
    // Safe only before submission. Clears a recorded-but-never-submitted draw.
    bool AbortRecordedDraw(std::string& reason);

    void OnLevelTeardown(uint64_t nextEpoch);
    void OnDeviceReset(uint64_t nextEpoch);

    // Owner-visible monotonic lifetime state. True after an unknown upload/
    // draw completion or when a recorded draw has no verified completion.
    bool GpuCompletionUnknown() const noexcept
    {
        return m_gpuUnknown || m_drawRecorded ||
               (m_uploadSubmitted && !m_uploadConfirmed);
    }

    // Returns true only when the owner confirms that no GPU work remains.
    // Otherwise the registry and constants are moved to process-lifetime
    // quarantine and are never released. A recorded draw always quarantines
    // unless OnDrawCompleted() already verified its exact fence.
    void Shutdown(bool gpuCompletionConfirmed);

    bool IsLoaded() const { return m_loaded; }
    bool HasPreparedFrame() const { return m_prepared; }
    bool IsDrawRecorded() const { return m_drawRecorded; }
    bool IsDrawSubmitted() const { return m_drawSubmitted; }
    bool IsDrawCompleted() const { return m_drawCompleted; }
    uint64_t DrawFenceValue() const { return m_drawFenceValue; }
    uint32_t VisualId() const { return m_visualId; }
    uint32_t IndexCount() const { return m_assetIndexCount; }
    // Returns the authored asset bounds. The bridge uses these only for the
    // opt-in diagnostic frustum check; no synthetic world transform is built.
    bool GetBounds(float minimum[3], float maximum[3], float center[3],
                   float& radius) const;
    uint64_t LevelSession() const { return m_levelSession; }
    uint64_t LevelEpoch() const { return m_levelEpoch; }
    uint64_t Generation() const { return m_generation; }
    const std::string& LastError() const { return m_lastError; }

private:
    bool LoadFromVfsImpl(const GameDx12LevelGeometryOwner12& owner,
                         const char* levelName,
                         uint32_t visualId,
                         uint64_t levelSession,
                         uint64_t levelEpoch,
                         std::string& reason);
    bool ReadVfsFile(const char* name, uint64_t maxBytes,
                     std::vector<uint8_t>& bytes, std::string& reason);
    bool CreateConstants(ID3D12Device* device, std::string& reason);
    bool Fail(std::string& reason, const char* text);
    bool Fail(std::string& reason, const std::string& text);
    void InvalidateFrame();
    void ReleaseGpu();
    void QuarantineGpu();

    GpuMeshRegistry12 m_registry;
    XrayAssetData12 m_asset;
    ComPtr<ID3D12Resource> m_constants;
    void* m_constantsMapped = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS m_frameCBVA = 0;
    D3D12_GPU_VIRTUAL_ADDRESS m_objectCBVA = 0;
    ResolvedFrame12 m_resolved;

    uint32_t m_visualId = 0;
    uint32_t m_assetIndexCount = 0;
    uint64_t m_levelSession = 0;
    uint64_t m_levelEpoch = 0;
    uint64_t m_generation = 0;
    uint64_t m_uploadFenceValue = 0;
    bool m_loaded = false;
    bool m_prepared = false;
    bool m_drawRecorded = false;
    bool m_drawSubmitted = false;
    bool m_drawCompleted = false;
    uint64_t m_drawFenceValue = 0;
    // Captured only for the active submission. These references are released
    // on confirmed completion or pre-submit abort; they are deliberately kept
    // while completion is unknown so geometry cannot outlive its owner objects.
    ComPtr<ID3D12Fence> m_drawFence;
    ComPtr<ID3D12Device> m_drawDevice;
    bool m_uploadSubmitted = false;
    bool m_uploadConfirmed = false;
    bool m_gpuUnknown = false;
    std::string m_lastError;
};

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
