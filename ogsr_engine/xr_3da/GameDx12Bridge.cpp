#include "stdafx.h"
#include "GameDx12Bridge.h"
#include "GameDx12BridgeCommandLine.h"
#include "GameDx12BridgeDiagnostics.h"
#include "GameDx12BridgeQuarantine.h"
#include "GameDx12LevelGeometry.h"
#include "GameDx12NativeFrame.h"
#include "GameDx12FourVisual.h"
#include "GameDx12OffscreenPass.h"
#include "GameDx12J2ABatch.h"
#include "GameDx12J2APreparation.h"
#include "GameDx12StaticScene.h"
#include "GameDx12GameVisuals.h"
#include "GameDx12Lighting.h"
#include "GameDx12Water.h"

#ifdef OGSR_J2B_M1_RUNTIME_PROBE
#include "FS.h"
#include "LocatorAPI_defs.h"
#endif

#include "igame_level.h"
#include "IGame_Persistent.h"
#include "XR_IOConsole.h"
// F4.1-E: инвентарь R4 через нейтральный интерфейс рендера и список объектов.
#include "../Include/xrRender/RenderVisual.h"
#include "vis_common.h"
#include "xr_object.h"
#include "xr_object_list.h"

#include <algorithm>
#include <vector>

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "Environment.h"
#include "../Layers/xrRender/xrRender_console.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

using Microsoft::WRL::ComPtr;

BridgeQuarantineStorage g_bridgeQuarantine;

namespace
{
constexpr const char* kRuntimeFlag = "-dx12_game_bridge";
constexpr DWORD kServiceFenceTimeoutMs = 5000;

enum class BridgeState : uint8_t
{
    Uninitialized = 0,
    Initializing,
    Ready,
    Disabled,
    ShuttingDown,
    Shutdown
};

struct Matrix4
{
    float m[4][4]{};
};

struct BridgeStateData
{
    BridgeState state = BridgeState::Uninitialized;
    bool runtimeRequested = false;
    bool runtimeChecked = false;
    bool nativeRequested = false;
    bool nativeFailed = false;
    bool nativeGameplayStarted = false;
    bool disabledLogged = false;
    bool gpuWorkSubmitted = false;
    bool gpuUnknown = false;
    bool quarantined = false;
    bool objectsReleased = false;
    bool serviceSubmitConfirmed = false;
    bool levelActive = false;
    bool cameraLogged = false;
    bool shutdownLogged = false;
    bool debugLayerEnabled = false;
    bool gbvEnabled = false;
    bool infoQueueAvailable = false;
    bool diagnosticsTrusted = false;
    bool storageFilterVerified = false;
    bool retrievalFilterVerified = false;
    bool prePolicyFilterUnsafe = false;
    bool matrixBitsRequested = false;
    bool matrixBitsChecked = false;
    bool diagnosticAttempted = false;
    bool diagnosticVisibilityLogged = false;
    bool diagnosticLevelMismatchLogged = false;
    bool diagnosticLevelBound = false;
    bool j2aPending = false;
    bool j2aAttempted = false;
    bool j2aEnabledLogged = false;
    bool m23RequestedChecked = false;
    bool m23Requested = false;
    bool m23EnabledLogged = false;
    bool m23Bound = false;
    bool m23Attempted = false;
    bool m23LevelMismatchLogged = false;
    bool m23DelayLogged = false;
    uint32_t m23FrameDelay = 0;
    uint32_t m23FrameSkip = 0;
    char diagnosticLevelName[128]{};
    char m23LevelName[128]{};
    uint64_t diagnosticLevelSession = 0;
    uint64_t diagnosticLevelEpoch = 0;
    uint64_t m23LevelSession = 0;
    uint64_t m23LevelEpoch = 0;
    DWORD ownerThreadId = 0;

    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12InfoQueue> infoQueue;
    ComPtr<IDXGIAdapter3> nativeMemoryAdapter;

    // J1.2A owner-side geometry state. The bridge owns the device/queue/fence
    // and the adapter owns only the level registry/constants. Loading and
    // frame preparation stay inactive until a full Jupiter payload provides a
    // validated visual placement.
    GameDx12LevelGeometry12 geometry;
    GameDx12FourVisual12 m23;
    // F4.1: полная статическая сцена уровня. Отделена от диагностического M23
    // пути: у неё своя загрузка, свои пулы и своя статистика.
    GameDx12StaticScene12 staticScene;
    gameaux12::GameVisuals gameVisuals;
    xr_vector<ISpatial*> lightQuery;
    xr_vector<ISpatial*> nearLightQuery;
    std::chrono::steady_clock::time_point nextLocalLightTrace{};
    struct
    {
        uint64_t levelSession = 0;
        uint64_t levelEpoch = 0;
        uint32_t frames = 0;
        Fvector anchor{};
        gamelight12::LocalLight frozen;
    } localLightProbe;
    GameDx12OffscreenPass12 offscreen;
    GameDx12NativeFrame12 native;

    uint64_t fenceValue = 0;
    uint64_t levelSession = 0;
    uint64_t captureEpoch = 0;
    uint64_t captureSequence = 0;
    uint64_t capturedFrames = 0;
    uint64_t rejectedFrames = 0;
    uint64_t nativeScenePresents = 0;
    std::chrono::steady_clock::time_point previousNativePresent{};
    struct
    {
        std::chrono::steady_clock::time_point start{};
        uint32_t frames = 0;
        uint32_t gpuFrames = 0;
        double frameCpuMs = 0;
        double prepareCpuMs = 0;
        double shadowCpuMs = 0;
        double sceneCpuMs = 0;
        double waterCpuMs = 0;
        double gpuWaitMs = 0;
        double presentMs = 0;
        double gpuMs = 0;
        uint64_t staticShadowDraws = 0;
        uint64_t dynamicShadowDraws = 0;
        uint64_t spotShadowMaps = 0;
        uint64_t pointShadowFaces = 0;
        uint64_t visibleLights = 0;
        uint64_t shadowedLights = 0;
        uint64_t lightCandidates = 0;
        uint64_t shadowCandidates = 0;
        uint64_t sceneCopies = 0;
    } nativePerf;
    uint64_t prePolicyStorageDenied = 0;
    uint64_t prePolicyRetrievalHidden = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t lastGameFrame = 0;
    uint64_t lastMatrixHash = 0;
    BridgeDiagnosticsVerdict diagnosticsVerdict =
        BridgeDiagnosticsVerdict::Unverified;
    char levelName[128]{};
    // F4.1 full static scene identity/flags.
    bool staticSceneRequested = false;
    bool staticSceneAttempted = false;
    bool staticSceneBound = false;
    bool staticSceneEnabledLogged = false;
    // F4.1-E: инвентарь динамики снимается после того, как мир заспавнен.
    bool dynamicInventoryLogged = false;
    uint64_t staticSceneSession = 0;
    uint64_t staticSceneEpoch = 0;
    uint32_t staticSceneDraws = 0;
};

BridgeStateData& State()
{
    static BridgeStateData state;
    return state;
}

bool EnsureOwnerThread(const char* hook)
{
    BridgeStateData& state = State();
    const DWORD current = ::GetCurrentThreadId();
    if (state.ownerThreadId == 0)
    {
        state.ownerThreadId = current;
        return true;
    }
    if (state.ownerThreadId != current)
    {
        Msg("ph_dx12_bridge: wrong thread hook=%s owner=%lu current=%lu",
            hook,
            static_cast<unsigned long>(state.ownerThreadId),
            static_cast<unsigned long>(current));
        return false;
    }
    return true;
}

bool RuntimeRequested()
{
    BridgeStateData& state = State();
    if (!state.runtimeChecked)
    {
        state.runtimeChecked = true;
        state.nativeRequested = GameDx12Bridge_NativeRequested(Core.Params);
        state.runtimeRequested = state.nativeRequested ||
            GameDx12Bridge_HasExactCommandLineFlag(Core.Params, kRuntimeFlag);
        if (!state.nativeRequested &&
            GameDx12Bridge_HasExactCommandLineFlag(Core.Params, "-dx12_native_game"))
        {
            if (state.runtimeRequested)
                Msg("ph_dx12_native: conflicting flags, using R4 diagnostic mode");
            else
                Msg("ph_dx12_native: preview requires -start with a Jupiter save; using R4");
        }
    }
    return state.runtimeRequested;
}

bool MatrixBitsRequested()
{
    BridgeStateData& state = State();
    if (!state.matrixBitsChecked)
    {
        state.matrixBitsChecked = true;
        const char* value = std::getenv("OGSR_DX12_BRIDGE_MATRIX_BITS");
        state.matrixBitsRequested =
            value != nullptr && value[0] != '\0' &&
            std::strcmp(value, "0") != 0;
    }
    return state.matrixBitsRequested;
}

// F4.0-J2B-M23 opt-in switch. When set, the bridge binds the four-visual
// registry instead of the J1 single-visual diagnostic, and the one-shot frame
// records four real indexed draws plus four isolated identity passes.
bool M23Requested()
{
    BridgeStateData& state = State();
    if (!state.m23RequestedChecked)
    {
        state.m23RequestedChecked = true;
        const char* value = std::getenv("OGSR_DX12_GAME_M23");
        state.m23Requested =
            value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }
    return state.m23Requested || state.nativeRequested;
}

bool B1WorldRequested()
{
    const char* value = std::getenv("OGSR_DX12_GAME_B1_WORLD");
    return State().nativeRequested || (value != nullptr && std::strcmp(value, "1") == 0);
}

bool ForceOverflow(const char* stage)
{
    const char* value =
        std::getenv("OGSR_DX12_GAME_BRIDGE_FORCE_OVERFLOW");
    return value != nullptr && std::strcmp(value, stage) == 0;
}

void DisableTerminal(const char* reason)
{
    BridgeStateData& state = State();
    if (state.state == BridgeState::Shutdown ||
        state.state == BridgeState::ShuttingDown)
        return;
    Msg("ph_dx12_bridge: terminal disable reason='%s'", reason);
    state.state = BridgeState::Disabled;
    state.levelActive = false;
}

void QuarantineObjects()
{
    BridgeStateData& state = State();
    if (!BridgeQuarantineBegin(g_bridgeQuarantine))
    {
        Msg("ph_dx12_bridge: duplicate quarantine attempt ignored; "
            "objects remain owned by the first quarantine");
        state.quarantined = true;
        state.gpuUnknown = true;
        state.objectsReleased = true;
        state.state = BridgeState::Disabled;
        state.levelActive = false;
        return;
    }

    g_bridgeQuarantine.infoQueue = BridgeDetachToQuarantine(state.infoQueue);
    g_bridgeQuarantine.list = BridgeDetachToQuarantine(state.list);
    g_bridgeQuarantine.allocator = BridgeDetachToQuarantine(state.allocator);
    g_bridgeQuarantine.fence = BridgeDetachToQuarantine(state.fence);
    g_bridgeQuarantine.queue = BridgeDetachToQuarantine(state.queue);
    g_bridgeQuarantine.device = BridgeDetachToQuarantine(state.device);
    g_bridgeQuarantine.fenceEvent = state.fenceEvent;
    state.fenceEvent = nullptr;
    state.offscreen.Quarantine(g_bridgeQuarantine);

    state.quarantined = true;
    state.gpuUnknown = true;
    state.objectsReleased = true;
    state.state = BridgeState::Disabled;
    state.levelActive = false;
    Msg("ph_dx12_bridge: GPU completion unknown; DX12 ownership is "
        "quarantined until process exit (no Release, no CloseHandle)");
}

void QuarantineForGeometryUnknown(const char* stage)
{
    BridgeStateData& state = State();
    state.staticScene.Shutdown(false);
    if (state.nativeRequested)
        state.native.Quarantine();
    Msg("ph_dx12_bridge: geometry GPU completion unknown at stage='%s'; "
        "bridge ownership must be retained", stage != nullptr ? stage : "?");
    if (!state.objectsReleased)
        QuarantineObjects();
    state.offscreen.Quarantine(g_bridgeQuarantine);
    // Monotonic owner-visible state. Even if the bridge quarantine was
    // already populated, nothing may be released or resubmitted.
    state.quarantined = true;
    state.gpuUnknown = true;
    state.objectsReleased = true;
    state.state = BridgeState::Disabled;
    state.levelActive = false;
}

// R2B: after a device reset the bridge and geometry epochs advance, but the
// diagnostic binding must only be renewed when the same level/session and a
// safe, already-loaded geometry object still own the new epoch. This never
// resets diagnosticAttempted and never releases quarantined/unknown work.
void RebindDiagnosticAfterDeviceReset(const char* stage)
{
    BridgeStateData& state = State();
    const char* reason = nullptr;

    if (state.state != BridgeState::Ready)
        reason = "bridge is not ready";
    else if (!state.levelActive)
        reason = "level is not active";
    else if (!state.diagnosticLevelBound)
        reason = "diagnostic level was not bound";
    else if (_stricmp(state.levelName, state.diagnosticLevelName) != 0)
        reason = "level name mismatch";
    else if (state.levelSession != state.diagnosticLevelSession)
        reason = "level session mismatch";
    else if (!state.geometry.IsLoaded())
        reason = "geometry is not loaded";
    else if (state.geometry.LevelSession() != state.levelSession)
        reason = "geometry session mismatch";
    else if (state.geometry.LevelEpoch() != state.captureEpoch)
        reason = "geometry epoch was not advanced";
    else if (state.geometry.GpuCompletionUnknown() ||
             state.geometry.IsDrawRecorded() ||
             state.geometry.IsDrawSubmitted())
        reason = "geometry has unknown or in-flight GPU work";
    else if (state.gpuUnknown || state.quarantined ||
             state.objectsReleased)
        reason = "bridge GPU state is unsafe";
    else if (state.diagnosticAttempted)
        reason = "one-shot diagnostic attempt was already consumed";
    else
    {
        const uint64_t previousEpoch = state.diagnosticLevelEpoch;
        state.diagnosticLevelEpoch = state.captureEpoch;
        Msg("ph_dx12_geometry: diagnostic rebound after %s level='%s' "
            "session=%llu epoch=%llu previous_epoch=%llu visual=%u",
            stage != nullptr ? stage : "?",
            state.diagnosticLevelName,
            static_cast<unsigned long long>(state.levelSession),
            static_cast<unsigned long long>(state.captureEpoch),
            static_cast<unsigned long long>(previousEpoch),
            state.geometry.VisualId());
        return;
    }

    Msg("ph_dx12_geometry: diagnostic reset rebind refused stage='%s' "
        "reason='%s' current_level='%s' session=%llu epoch=%llu "
        "bound_level='%s' bound_session=%llu bound_epoch=%llu "
        "geometry_loaded=%d geometry_session=%llu geometry_epoch=%llu "
        "attempted=%d gpu_unknown=%d quarantine=%d draw_recorded=%d "
        "draw_submitted=%d",
        stage != nullptr ? stage : "?", reason, state.levelName,
        static_cast<unsigned long long>(state.levelSession),
        static_cast<unsigned long long>(state.captureEpoch),
        state.diagnosticLevelName,
        static_cast<unsigned long long>(state.diagnosticLevelSession),
        static_cast<unsigned long long>(state.diagnosticLevelEpoch),
        state.geometry.IsLoaded() ? 1 : 0,
        static_cast<unsigned long long>(state.geometry.LevelSession()),
        static_cast<unsigned long long>(state.geometry.LevelEpoch()),
        state.diagnosticAttempted ? 1 : 0,
        state.gpuUnknown ? 1 : 0, state.quarantined ? 1 : 0,
        state.geometry.IsDrawRecorded() ? 1 : 0,
        state.geometry.IsDrawSubmitted() ? 1 : 0);
}

// M23 reset-before-draw recovery. Mirrors the J1 R2B policy: the binding and
// epoch may be renewed only for the same active level/session with a fully
// uploaded registry and no consumed one-shot attempt. Reset-after-draw is
// always refused.
void RebindM23AfterDeviceReset(const char* stage)
{
    BridgeStateData& state = State();
    const char* reason = nullptr;

    if (state.state != BridgeState::Ready)
        reason = "bridge is not ready";
    else if (!state.levelActive)
        reason = "level is not active";
    else if (!state.m23Bound)
        reason = "M23 level was not bound";
    else if (_stricmp(state.levelName, state.m23LevelName) != 0)
        reason = "level name mismatch";
    else if (state.levelSession != state.m23LevelSession)
        reason = "level session mismatch";
    else if (!state.m23.IsLoaded())
        reason = "M23 registry is not loaded";
    else if (state.m23.LevelSession() != state.levelSession)
        reason = "M23 registry session mismatch";
    else if (state.m23.LevelEpoch() != state.captureEpoch)
        reason = "M23 registry epoch was not advanced";
    else if (state.m23.GpuCompletionUnknown())
        reason = "M23 has unknown or in-flight GPU work";
    else if (state.gpuUnknown || state.quarantined ||
             state.objectsReleased)
        reason = "bridge GPU state is unsafe";
    else if (state.m23Attempted)
        reason = "one-shot M23 attempt was already consumed";
    else
    {
        const uint64_t previousEpoch = state.m23LevelEpoch;
        state.m23LevelEpoch = state.captureEpoch;
        Msg("ph_dx12_m23: rebound after %s level='%s' session=%llu "
            "epoch=%llu previous_epoch=%llu generation=%llu",
            stage != nullptr ? stage : "?", state.m23LevelName,
            static_cast<unsigned long long>(state.levelSession),
            static_cast<unsigned long long>(state.captureEpoch),
            static_cast<unsigned long long>(previousEpoch),
            static_cast<unsigned long long>(state.m23.Generation()));
        return;
    }

    Msg("ph_dx12_m23: reset rebind refused stage='%s' reason='%s' "
        "current_level='%s' session=%llu epoch=%llu bound_level='%s' "
        "bound_session=%llu bound_epoch=%llu loaded=%d attempted=%d "
        "gpu_unknown=%d quarantine=%d",
        stage != nullptr ? stage : "?", reason, state.levelName,
        static_cast<unsigned long long>(state.levelSession),
        static_cast<unsigned long long>(state.captureEpoch),
        state.m23LevelName,
        static_cast<unsigned long long>(state.m23LevelSession),
        static_cast<unsigned long long>(state.m23LevelEpoch),
        state.m23.IsLoaded() ? 1 : 0, state.m23Attempted ? 1 : 0,
        state.gpuUnknown ? 1 : 0, state.quarantined ? 1 : 0);
}

void ReleaseObjects()
{
    BridgeStateData& state = State();
    if (state.objectsReleased)
        return;
    if (state.gpuUnknown || state.quarantined)
    {
        QuarantineObjects();
        return;
    }

    state.objectsReleased = true;
    state.native.Release();
    state.offscreen.Release();
    state.infoQueue.Reset();
    state.list.Reset();
    state.allocator.Reset();
    if (state.fenceEvent)
    {
        ::CloseHandle(state.fenceEvent);
        state.fenceEvent = nullptr;
    }
    state.fence.Reset();
    state.queue.Reset();
    state.device.Reset();
    state.infoQueueAvailable = false;
}

// A command list that was Reset successfully belongs to the current
// diagnostic invocation. If recording fails before ExecuteCommandLists, the
// only legal recovery is to close that list and discard its commands. A list
// that cannot be closed is poisoned; the bridge must not return to Ready.
void AbortUnsubmittedDiagnosticState(BridgeStateData& state) noexcept
{
    std::string ignored;
    state.geometry.AbortRecordedDraw(ignored);
    state.offscreen.AbortUnsubmittedFrame();
}

bool RecoverUnsubmittedDiagnosticList(BridgeStateData& state,
                                      const char* stage,
                                      std::string& reason)
{
    AbortUnsubmittedDiagnosticState(state);
    if (state.list == nullptr)
    {
        DisableTerminal("diagnostic command list is missing");
        reason = "diagnostic command list is missing";
        return false;
    }
    const HRESULT hr = state.list->Close();
    if (FAILED(hr))
    {
        Msg("ph_dx12_bridge: diagnostic list recovery failed stage='%s' "
            "hr=0x%08X",
            stage != nullptr ? stage : "?", static_cast<unsigned>(hr));
        DisableTerminal("diagnostic command list recovery");
        reason = "diagnostic command list recovery failed";
        return false;
    }
    return true;
}

class D3D12InfoQueueAdapter final : public IBridgeInfoQueue
{
public:
    explicit D3D12InfoQueueAdapter(ID3D12InfoQueue* queue) noexcept
        : queue_(queue)
    {
    }

    UINT64 Stored() noexcept override
    {
        return queue_ != nullptr ? queue_->GetNumStoredMessages() : 0;
    }
    UINT64 Allowed() noexcept override
    {
        return queue_ != nullptr
            ? queue_->GetNumMessagesAllowedByStorageFilter()
            : 0;
    }
    UINT64 Denied() noexcept override
    {
        return queue_ != nullptr
            ? queue_->GetNumMessagesDeniedByStorageFilter()
            : 0;
    }
    UINT64 Retrievable() noexcept override
    {
        return queue_ != nullptr
            ? queue_->GetNumStoredMessagesAllowedByRetrievalFilter()
            : 0;
    }
    UINT64 Discarded() noexcept override
    {
        return queue_ != nullptr
            ? queue_->GetNumMessagesDiscardedByMessageCountLimit()
            : 0;
    }
    HRESULT GetMessage(UINT64 index, D3D12_MESSAGE* message,
                       SIZE_T* size) noexcept override
    {
        if (queue_ == nullptr)
            return E_POINTER;
        return queue_->GetMessage(index, message, size);
    }
    void ClearStorageFilter() noexcept override
    {
        if (queue_ != nullptr)
            queue_->ClearStorageFilter();
    }
    HRESULT PushEmptyStorageFilter() noexcept override
    {
        return queue_ != nullptr ? queue_->PushEmptyStorageFilter() : E_POINTER;
    }
    void ClearRetrievalFilter() noexcept override
    {
        if (queue_ != nullptr)
            queue_->ClearRetrievalFilter();
    }
    HRESULT PushEmptyRetrievalFilter() noexcept override
    {
        return queue_ != nullptr
            ? queue_->PushEmptyRetrievalFilter()
            : E_POINTER;
    }
    HRESULT StorageFilterAllowAll(bool& allowAll) noexcept override
    {
        return BridgeInfoQueueReadFilterAllowAll(queue_, true, allowAll);
    }
    HRESULT RetrievalFilterAllowAll(bool& allowAll) noexcept override
    {
        return BridgeInfoQueueReadFilterAllowAll(queue_, false, allowAll);
    }

private:
    ID3D12InfoQueue* queue_ = nullptr;
};

void PrepareInfoQueue()
{
    BridgeStateData& state = State();
    state.infoQueueAvailable = state.infoQueue != nullptr;
    state.storageFilterVerified = false;
    state.retrievalFilterVerified = false;
    state.prePolicyStorageDenied = 0;
    state.prePolicyRetrievalHidden = 0;
    state.prePolicyFilterUnsafe = false;

    if (!state.infoQueue)
    {
        Msg("ph_dx12_bridge: infoqueue unavailable; diagnostics UNVERIFIED");
        return;
    }

    state.prePolicyStorageDenied =
        state.infoQueue->GetNumMessagesDeniedByStorageFilter();
    // F4.1: полная статическая сцена выполняет тысячи draw и порождает поток
    // сообщений Debug Layer. При дефолтном лимите очередь отбрасывает
    // сообщения, и итоговый вердикт становится UNVERIFIED. Лимит поднимается
    // заранее, чтобы диагностика не теряла данные.
    state.infoQueue->SetMessageCountLimit(1ull << 18);
    const UINT64 stored = state.infoQueue->GetNumStoredMessages();
    const UINT64 retrievable =
        state.infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
    state.prePolicyRetrievalHidden =
        stored > retrievable ? stored - retrievable : 0;

    bool storageUnsafe = false;
    bool retrievalUnsafe = false;
    if (FAILED(BridgeInfoQueueReadFilterUnsafe(
            state.infoQueue.Get(), true, storageUnsafe)))
        storageUnsafe = true;
    if (FAILED(BridgeInfoQueueReadFilterUnsafe(
            state.infoQueue.Get(), false, retrievalUnsafe)))
        retrievalUnsafe = true;
    state.prePolicyFilterUnsafe = storageUnsafe || retrievalUnsafe;

    state.infoQueue->ClearStorageFilter();
    HRESULT hr = state.infoQueue->PushEmptyStorageFilter();
    if (FAILED(hr))
        Msg("ph_dx12_bridge: PushEmptyStorageFilter failed hr=0x%08X",
            static_cast<unsigned>(hr));
    state.infoQueue->ClearRetrievalFilter();
    hr = state.infoQueue->PushEmptyRetrievalFilter();
    if (FAILED(hr))
        Msg("ph_dx12_bridge: PushEmptyRetrievalFilter failed hr=0x%08X",
            static_cast<unsigned>(hr));

    D3D12InfoQueueAdapter adapter(state.infoQueue.Get());
    bool storageAllowAll = false;
    bool retrievalAllowAll = false;
    if (SUCCEEDED(adapter.StorageFilterAllowAll(storageAllowAll)))
        state.storageFilterVerified = storageAllowAll;
    if (SUCCEEDED(adapter.RetrievalFilterAllowAll(retrievalAllowAll)))
        state.retrievalFilterVerified = retrievalAllowAll;

    Msg("ph_dx12_bridge: infoqueue policy=allow-all "
        "preDenied=%llu preRetrievalHidden=%llu preFilterUnsafe=%d "
        "storageVerified=%d retrievalVerified=%d",
        static_cast<unsigned long long>(state.prePolicyStorageDenied),
        static_cast<unsigned long long>(state.prePolicyRetrievalHidden),
        state.prePolicyFilterUnsafe ? 1 : 0,
        state.storageFilterVerified ? 1 : 0,
        state.retrievalFilterVerified ? 1 : 0);
}

BridgeInfoQueueResult EvaluateInfoQueue(const char* phase)
{
    BridgeStateData& state = State();
    BridgeInfoQueueResult result;
    result.scope = phase != nullptr ? phase : "unknown";
    result.debugLayerEnabled = state.debugLayerEnabled;
    result.gbvEnabled = state.gbvEnabled;
    result.gpuCompletionConfirmed = state.serviceSubmitConfirmed;

    if (!state.infoQueue)
    {
        result.infoQueueAvailable = false;
        result.verdict = BridgeDiagnosticsVerdict::Unverified;
        result.reason = "InfoQueue unavailable";
    }
    else
    {
        D3D12InfoQueueAdapter adapter(state.infoQueue.Get());
        BridgeInfoQueuePolicy policy;
        policy.debugLayerEnabled = state.debugLayerEnabled;
        policy.gbvEnabled = state.gbvEnabled;
        policy.gpuCompletionConfirmed = state.serviceSubmitConfirmed;
        policy.prePolicyFilterUnsafe = state.prePolicyFilterUnsafe;
        policy.prePolicyStorageDenied = state.prePolicyStorageDenied;
        policy.prePolicyRetrievalHidden = state.prePolicyRetrievalHidden;
        result = EvaluateBridgeInfoQueue(adapter, policy, phase);
    }

    state.diagnosticsVerdict = result.verdict;
    state.diagnosticsTrusted =
        result.verdict == BridgeDiagnosticsVerdict::Pass &&
        result.lifetimeVerified;
    state.storageFilterVerified = result.storageFilterVerified;
    state.retrievalFilterVerified = result.retrievalFilterVerified;

    Msg("ph_dx12_bridge: infoqueue phase=%s verdict=%s scope=%s "
        "debug=%d gbv=%d gbvVerified=%d available=%d storageFilter=%d "
        "retrievalFilter=%d stored=%llu retrievable=%llu read=%llu "
        "readFailures=%llu sizeMismatches=%llu attempts=%u snapshots=%u "
        "consistent=%d allowed=%llu denied=%llu "
        "discarded=%llu "
        "preDenied=%llu preRetrievalHidden=%llu info=%llu message=%llu "
        "warning=%llu error=%llu corruption=%llu unknown=%llu malformed=%llu "
        "allocFailures=%llu completion=%d lifetime=%d reason='%s'",
        phase != nullptr ? phase : "unknown",
        BridgeDiagnosticsVerdictName(result.verdict),
        result.scope,
        result.debugLayerEnabled ? 1 : 0,
        result.gbvEnabled ? 1 : 0,
        result.gbvVerified ? 1 : 0,
        result.infoQueueAvailable ? 1 : 0,
        result.storageFilterVerified ? 1 : 0,
        result.retrievalFilterVerified ? 1 : 0,
        static_cast<unsigned long long>(result.stored),
        static_cast<unsigned long long>(result.retrievable),
        static_cast<unsigned long long>(result.readSuccessfully),
        static_cast<unsigned long long>(result.readFailures),
        static_cast<unsigned long long>(result.sizeMismatches),
        static_cast<unsigned>(result.collectionAttempts),
        static_cast<unsigned>(result.counterSnapshotPairs),
        result.windowConsistent ? 1 : 0,
        static_cast<unsigned long long>(result.allowed),
        static_cast<unsigned long long>(result.denied),
        static_cast<unsigned long long>(result.discardedByLimit),
        static_cast<unsigned long long>(result.prePolicyStorageDenied),
        static_cast<unsigned long long>(result.prePolicyRetrievalHidden),
        static_cast<unsigned long long>(result.info),
        static_cast<unsigned long long>(result.message),
        static_cast<unsigned long long>(result.warning),
        static_cast<unsigned long long>(result.error),
        static_cast<unsigned long long>(result.corruption),
        static_cast<unsigned long long>(result.unknownSeverity),
        static_cast<unsigned long long>(result.malformedDescriptions),
        static_cast<unsigned long long>(result.allocationFailures),
        result.gpuCompletionConfirmed ? 1 : 0,
        result.lifetimeVerified ? 1 : 0,
        result.reason != nullptr ? result.reason : "");

    if (result.firstFailure[0] != '\0')
        Msg("ph_dx12_bridge: infoqueue first_failure='%s'",
            result.firstFailure);

    // F4.1: текст предупреждений и ошибок. Без него вердикт FAIL невозможно
    // разобрать: счётчики показывают «47 warning», но не причину.
    if (state.infoQueue &&
        result.verdict != BridgeDiagnosticsVerdict::Pass)
    {
        const UINT64 stored = state.infoQueue->GetNumStoredMessages();
        uint32_t logged = 0;
        for (UINT64 index = 0; index < stored && logged < 10; ++index)
        {
            SIZE_T size = 0;
            if (FAILED(state.infoQueue->GetMessage(index, nullptr, &size)) ||
                size == 0)
                continue;
            std::vector<uint8_t> buffer(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
            if (FAILED(state.infoQueue->GetMessage(index, message, &size)))
                continue;
            if (message->Severity == D3D12_MESSAGE_SEVERITY_INFO ||
                message->Severity == D3D12_MESSAGE_SEVERITY_MESSAGE)
                continue;
            if (message->pDescription == nullptr)
                continue;
            char text[241] = {};
            size_t copy = message->DescriptionByteLength;
            if (copy > sizeof(text) - 1)
                copy = sizeof(text) - 1;
            std::memcpy(text, message->pDescription, copy);
            for (size_t i = 0; i < copy; ++i)
                if (text[i] == '\n' || text[i] == '\r')
                    text[i] = ' ';
            Msg("ph_dx12_bridge: infoqueue detail severity=%u id=%u text='%s'",
                static_cast<unsigned>(message->Severity),
                static_cast<unsigned>(message->ID), text);
            ++logged;
        }
    }

    return result;
}

enum class BridgeWaitMode : uint8_t
{
    Real = 0,
    InjectTimeout,
    InjectFailed,
    InjectUnexpected
};

inline const char* BridgeWaitModeName(BridgeWaitMode mode) noexcept
{
    switch (mode)
    {
    case BridgeWaitMode::Real: return "real";
    case BridgeWaitMode::InjectTimeout: return "inject-timeout";
    case BridgeWaitMode::InjectFailed: return "inject-failed";
    case BridgeWaitMode::InjectUnexpected: return "inject-unexpected";
    }
    return "unknown";
}

struct BridgeWaitResult
{
    BridgeWaitMode mode = BridgeWaitMode::Real;
    bool injected = false;
    bool valid = true;
    DWORD code = WAIT_FAILED;
    DWORD lastError = 0;
};

inline const char* BridgeWaitSourceName(const BridgeWaitResult& wait) noexcept
{
    if (!wait.valid)
        return "invalid-test-adapter";
    return wait.injected ? "injected" : "real";
}

using BridgeWaitApi = DWORD (WINAPI*)(HANDLE, DWORD);

BridgeWaitResult BridgeWaitForSingleObject(
    HANDLE handle, DWORD timeout, BridgeWaitMode mode,
    BridgeWaitApi waitApi = ::WaitForSingleObject) noexcept
{
    switch (mode)
    {
    case BridgeWaitMode::InjectTimeout:
        return {mode, true, true, WAIT_TIMEOUT, 0};
    case BridgeWaitMode::InjectFailed:
        return {mode, true, true, WAIT_FAILED, ERROR_INVALID_HANDLE};
    case BridgeWaitMode::InjectUnexpected:
        return {mode, true, true, 0x1234u, 0};
    case BridgeWaitMode::Real:
        break;
    }

    if (waitApi == nullptr)
        return {mode, false, false, WAIT_FAILED, 0};

    const DWORD code = waitApi(handle, timeout);
    const DWORD lastError = code == WAIT_FAILED ? ::GetLastError() : 0;
    return {mode, false, true, code, lastError};
}

bool ServiceSubmit()
{
    BridgeStateData& state = State();
    if (state.state == BridgeState::Disabled ||
        state.state == BridgeState::ShuttingDown ||
        state.state == BridgeState::Shutdown ||
        state.gpuUnknown || state.quarantined || state.objectsReleased ||
        state.geometry.GpuCompletionUnknown())
    {
        Msg("ph_dx12_bridge: service submit refused: completion unknown "
            "or owner state is terminal");
        return false;
    }
    const char* injected =
        std::getenv("OGSR_DX12_GAME_BRIDGE_FAIL_SUBMIT");

    if (injected != nullptr && std::strcmp(injected, "overflow") == 0)
        state.fenceValue = std::numeric_limits<uint64_t>::max();

    if (state.fenceValue == std::numeric_limits<uint64_t>::max())
    {
        Msg("ph_dx12_bridge: fence value overflow before submit");
        return false;
    }

    HRESULT hr = state.allocator->Reset();
    if (FAILED(hr))
    {
        Msg("ph_dx12_bridge: service allocator Reset failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return false;
    }
    hr = state.list->Reset(state.allocator.Get(), nullptr);
    if (FAILED(hr))
    {
        Msg("ph_dx12_bridge: service list Reset failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return false;
    }
    hr = state.list->Close();
    if (FAILED(hr))
    {
        Msg("ph_dx12_bridge: service list Close failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return false;
    }

    ID3D12CommandList* lists[] = {state.list.Get()};
    state.queue->ExecuteCommandLists(1, lists);
    state.gpuWorkSubmitted = true;

    const auto failAfterSubmit = [&](const char* stage) -> bool {
        Msg("ph_dx12_bridge: service failure after submit stage='%s'; "
            "quarantining all DX12 ownership", stage);
        QuarantineObjects();
        return false;
    };

    if (injected != nullptr && std::strcmp(injected, "signal") == 0)
        return failAfterSubmit("signal-injected");

    const uint64_t expected = ++state.fenceValue;
    hr = state.queue->Signal(state.fence.Get(), expected);
    if (FAILED(hr))
    {
        Msg("ph_dx12_bridge: service Signal failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return failAfterSubmit("signal-failed");
    }

    if (injected != nullptr && std::strcmp(injected, "setevent") == 0)
        return failAfterSubmit("setevent-injected");

    hr = state.fence->SetEventOnCompletion(expected, state.fenceEvent);
    if (FAILED(hr))
    {
        Msg("ph_dx12_bridge: SetEventOnCompletion failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return failAfterSubmit("setevent-failed");
    }

    if (injected != nullptr && std::strcmp(injected, "wait") == 0)
        return failAfterSubmit("wait-injected");

    BridgeWaitMode waitMode = BridgeWaitMode::Real;
    if (injected != nullptr && std::strcmp(injected, "timeout") == 0)
        waitMode = BridgeWaitMode::InjectTimeout;
    else if (injected != nullptr && std::strcmp(injected, "wait-failed") == 0)
        waitMode = BridgeWaitMode::InjectFailed;
    else if (injected != nullptr && std::strcmp(injected, "unexpected") == 0)
        waitMode = BridgeWaitMode::InjectUnexpected;

    const BridgeWaitResult wait = BridgeWaitForSingleObject(
        state.fenceEvent, kServiceFenceTimeoutMs, waitMode);
    if (wait.code != WAIT_OBJECT_0)
    {
        const char* failureStage = "wait-unexpected";
        if (wait.code == WAIT_FAILED)
            failureStage = "wait-failed";
        else if (wait.code == WAIT_TIMEOUT)
            failureStage = "wait-timeout";

        Msg("ph_dx12_bridge: service fence wait failed "
            "wait_mode=%s wait_source=%s wait_valid=%d wait_code=%lu "
            "last_error=%lu timeout=%lu failure_stage='%s'",
            BridgeWaitModeName(wait.mode),
            BridgeWaitSourceName(wait),
            wait.valid ? 1 : 0,
            static_cast<unsigned long>(wait.code),
            static_cast<unsigned long>(wait.lastError),
            static_cast<unsigned long>(kServiceFenceTimeoutMs),
            failureStage);
        return failAfterSubmit(failureStage);
    }

    if (injected != nullptr && std::strcmp(injected, "completed") == 0)
        return failAfterSubmit("completed-injected");

    const uint64_t completed = state.fence->GetCompletedValue();
    HRESULT removed = state.device->GetDeviceRemovedReason();
    uint64_t completionValue = completed;
    if (injected != nullptr && std::strcmp(injected, "sentinel") == 0)
        completionValue = std::numeric_limits<uint64_t>::max();
    if (injected != nullptr && std::strcmp(injected, "removed") == 0)
        removed = E_FAIL;

    if (completionValue == std::numeric_limits<uint64_t>::max() ||
        completionValue != expected || removed != S_OK)
    {
        Msg("ph_dx12_bridge: service completion unconfirmed "
            "expected=%llu completed=%llu removed=0x%08X",
            static_cast<unsigned long long>(expected),
            static_cast<unsigned long long>(completionValue),
            static_cast<unsigned>(removed));
        return failAfterSubmit("completion-unconfirmed");
    }

    state.serviceSubmitConfirmed = true;
    Msg("ph_dx12_bridge: service submit confirmed fence=%llu "
        "(no game draw performed)",
        static_cast<unsigned long long>(expected));
    return true;
}

bool EnsureInitialized()
{
    BridgeStateData& state = State();
    if (state.state == BridgeState::Ready)
        return true;
    if (state.state == BridgeState::Disabled ||
        state.state == BridgeState::ShuttingDown ||
        state.state == BridgeState::Shutdown)
        return false;
    if (state.state == BridgeState::Initializing)
        return false;

    if (!RuntimeRequested())
    {
        state.state = BridgeState::Disabled;
        if (!state.disabledLogged)
        {
            state.disabledLogged = true;
            Msg("ph_dx12_bridge: disabled: runtime flag absent");
        }
        return false;
    }

    if (const char* injected = std::getenv("OGSR_DX12_GAME_BRIDGE_FAIL_INIT");
        injected != nullptr && injected[0] != '\0')
    {
        Msg("ph_dx12_bridge: injected init failure stage='%s'; "
            "bridge disabled, R4 continues", injected);
        state.state = BridgeState::Disabled;
        return false;
    }

    state.state = BridgeState::Initializing;
    Msg("ph_dx12_bridge: feature=on runtime=1 initializing");

    ComPtr<ID3D12Debug> debug;
    bool debugEnabled = false;
    bool gbvEnabled = false;
    // GBV подменяет шейдеры и добавляет GPU-команды. Обычный игровой кадр
    // использует этот режим только по запросу. Диагностический мост сохраняет
    // прежний режим проверки своего bootstrap.
    const char* validation = std::getenv("OGSR_DX12_GAME_VALIDATION");
    const bool wantValidation = !state.nativeRequested ||
        (validation != nullptr && std::strcmp(validation, "1") == 0);
    if (wantValidation && SUCCEEDED(::D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf()))))
    {
        debug->EnableDebugLayer();
        debugEnabled = true;
        ComPtr<ID3D12Debug1> debug1;
        if (SUCCEEDED(debug.As(&debug1)))
        {
            debug1->SetEnableGPUBasedValidation(TRUE);
            gbvEnabled = true;
        }
    }
    state.debugLayerEnabled = debugEnabled;
    state.gbvEnabled = gbvEnabled;
    Msg("ph_dx12_bridge: debug_layer=%d gbv=%d",
        debugEnabled ? 1 : 0, gbvEnabled ? 1 : 0);

    ComPtr<IDXGIFactory6> factory;
    HRESULT hr = ::CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr))
    {
        Msg("ph_dx12_bridge: CreateDXGIFactory1 failed hr=0x%08X",
            static_cast<unsigned>(hr));
        state.state = BridgeState::Disabled;
        return false;
    }

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0;; ++i)
    {
        ComPtr<IDXGIAdapter1> candidate;
        hr = factory->EnumAdapterByGpuPreference(
            i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(candidate.GetAddressOf()));
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(hr))
        {
            Msg("ph_dx12_bridge: EnumAdapterByGpuPreference(%u) failed "
                "hr=0x%08X", i, static_cast<unsigned>(hr));
            break;
        }
        DXGI_ADAPTER_DESC1 desc = {};
        if (FAILED(candidate->GetDesc1(&desc)))
            continue;
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;
        adapter = candidate;
        Msg("ph_dx12_bridge: adapter='%ls' vendor=0x%04X device=0x%04X "
            "software=0",
            desc.Description, desc.VendorId, desc.DeviceId);
        break;
    }
    if (!adapter)
    {
        Msg("ph_dx12_bridge: no hardware adapter found");
        state.state = BridgeState::Disabled;
        return false;
    }
    if (state.nativeRequested)
        adapter.As(&state.nativeMemoryAdapter);

    hr = ::D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                             IID_PPV_ARGS(state.device.GetAddressOf()));
    if (FAILED(hr))
    {
        Msg("ph_dx12_bridge: D3D12CreateDevice failed hr=0x%08X",
            static_cast<unsigned>(hr));
        state.state = BridgeState::Disabled;
        return false;
    }
    Msg("ph_dx12_bridge: device created");

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = state.device->CreateCommandQueue(
        &queueDesc, IID_PPV_ARGS(state.queue.GetAddressOf()));
    if (FAILED(hr))
    {
        Msg("ph_dx12_bridge: CreateCommandQueue failed hr=0x%08X",
            static_cast<unsigned>(hr));
        ReleaseObjects();
        state.state = BridgeState::Disabled;
        return false;
    }
    Msg("ph_dx12_bridge: queue created");

    hr = state.device->CreateFence(
        0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(state.fence.GetAddressOf()));
    if (FAILED(hr))
    {
        Msg("ph_dx12_bridge: CreateFence failed hr=0x%08X",
            static_cast<unsigned>(hr));
        ReleaseObjects();
        state.state = BridgeState::Disabled;
        return false;
    }
    state.fenceEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!state.fenceEvent)
    {
        Msg("ph_dx12_bridge: CreateEvent failed GetLastError=%lu",
            static_cast<unsigned long>(::GetLastError()));
        ReleaseObjects();
        state.state = BridgeState::Disabled;
        return false;
    }
    Msg("ph_dx12_bridge: fence created");

    state.device.As(&state.infoQueue);
    PrepareInfoQueue();

    hr = state.device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(state.allocator.GetAddressOf()));
    if (FAILED(hr))
    {
        Msg("ph_dx12_bridge: CreateCommandAllocator failed hr=0x%08X",
            static_cast<unsigned>(hr));
        ReleaseObjects();
        state.state = BridgeState::Disabled;
        return false;
    }
    hr = state.device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, state.allocator.Get(), nullptr,
        IID_PPV_ARGS(state.list.GetAddressOf()));
    if (FAILED(hr))
    {
        Msg("ph_dx12_bridge: CreateCommandList failed hr=0x%08X",
            static_cast<unsigned>(hr));
        ReleaseObjects();
        state.state = BridgeState::Disabled;
        return false;
    }
    hr = state.list->Close();
    if (FAILED(hr))
    {
        Msg("ph_dx12_bridge: initial command list Close failed hr=0x%08X",
            static_cast<unsigned>(hr));
        ReleaseObjects();
        state.state = BridgeState::Disabled;
        return false;
    }

    if (!ServiceSubmit())
    {
        if (!state.gpuWorkSubmitted && !state.quarantined)
            ReleaseObjects();
        state.state = BridgeState::Disabled;
        return false;
    }

    const BridgeInfoQueueResult diagnostics = EvaluateInfoQueue("init");
    if (diagnostics.verdict == BridgeDiagnosticsVerdict::Fail)
    {
        Msg("ph_dx12_bridge: InfoQueue diagnostics FAIL; bridge disabled, "
            "R4 continues");
        ReleaseObjects();
        state.state = BridgeState::Disabled;
        return false;
    }

    state.state = BridgeState::Ready;
    Msg("ph_dx12_bridge: ready diagnostics_trusted=%d",
        state.diagnosticsTrusted ? 1 : 0);
    return true;
}

void CopyMatrix(const Fmatrix& source, Matrix4& destination)
{
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t column = 0; column < 4; ++column)
            destination.m[row][column] = source.m[row][column];
}

bool FiniteMatrix(const Matrix4& matrix)
{
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t column = 0; column < 4; ++column)
            if (!std::isfinite(matrix.m[row][column]))
                return false;
    return true;
}

bool FiniteVector(const Fvector& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool ValidView(const Matrix4& view)
{
    if (!FiniteMatrix(view))
        return false;
    const float a = view.m[0][0], b = view.m[0][1], c = view.m[0][2];
    const float d = view.m[1][0], e = view.m[1][1], f = view.m[1][2];
    const float g = view.m[2][0], h = view.m[2][1], i = view.m[2][2];
    const float determinant =
        a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    return std::fabs(determinant) > 1.0e-8f;
}

bool ValidProjection(const Matrix4& projection)
{
    if (!FiniteMatrix(projection))
        return false;
    return std::fabs(projection.m[2][3]) > 1.0e-8f ||
           std::fabs(projection.m[3][3]) > 1.0e-8f;
}

bool UninitializedCamera(const Matrix4& view, const Fvector& position)
{
    const bool zeroPosition = std::fabs(position.x) < 1.0e-6f &&
                              std::fabs(position.y) < 1.0e-6f &&
                              std::fabs(position.z) < 1.0e-6f;
    if (!zeroPosition)
        return false;
    for (uint32_t row = 0; row < 4; ++row)
    {
        for (uint32_t column = 0; column < 4; ++column)
        {
            const float expected = (row == column) ? 1.0f : 0.0f;
            if (std::fabs(view.m[row][column] - expected) > 1.0e-6f)
                return false;
        }
    }
    return true;
}

uint64_t HashMatrix(const Matrix4& view, const Matrix4& projection,
                    const Matrix4& fullTransform)
{
    uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](const void* data, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    };
    mix(view.m, sizeof(view.m));
    mix(projection.m, sizeof(projection.m));
    mix(fullTransform.m, sizeof(fullTransform.m));
    return hash;
}

void LogCamera(const Matrix4& view, const Matrix4& projection,
               const Matrix4& fullTransform, uint64_t hash)
{
    BridgeStateData& state = State();
    Msg("ph_dx12_bridge: camera frame=%u session=%llu epoch=%llu "
        "sequence=%llu viewport=%ux%u position=(%.3f,%.3f,%.3f) "
        "matrix_hash=%016llX level='%s'",
        state.lastGameFrame,
        static_cast<unsigned long long>(state.levelSession),
        static_cast<unsigned long long>(state.captureEpoch),
        static_cast<unsigned long long>(state.captureSequence),
        state.width, state.height,
        Device.vCameraPosition.x, Device.vCameraPosition.y,
        Device.vCameraPosition.z,
        static_cast<unsigned long long>(hash), state.levelName);

    if (!state.cameraLogged)
    {
        state.cameraLogged = true;
        for (uint32_t row = 0; row < 4; ++row)
            Msg("ph_dx12_bridge: view[%u] = %.9g %.9g %.9g %.9g", row,
                view.m[row][0], view.m[row][1], view.m[row][2],
                view.m[row][3]);
        for (uint32_t row = 0; row < 4; ++row)
            Msg("ph_dx12_bridge: projection[%u] = %.9g %.9g %.9g %.9g", row,
                projection.m[row][0], projection.m[row][1],
                projection.m[row][2], projection.m[row][3]);
        for (uint32_t row = 0; row < 4; ++row)
            Msg("ph_dx12_bridge: full[%u] = %.9g %.9g %.9g %.9g", row,
                fullTransform.m[row][0], fullTransform.m[row][1],
                fullTransform.m[row][2], fullTransform.m[row][3]);
    }
}

void LogCameraBits(const Matrix4& view, const Matrix4& projection,
                   const Matrix4& fullTransform)
{
    BridgeStateData& state = State();
    Msg("ph_dx12_bridge: matrix_bits frame=%u session=%llu epoch=%llu "
        "sequence=%llu viewport=%ux%u",
        state.lastGameFrame,
        static_cast<unsigned long long>(state.levelSession),
        static_cast<unsigned long long>(state.captureEpoch),
        static_cast<unsigned long long>(state.captureSequence),
        state.width, state.height);

    const auto logMatrix = [](const char* name, const Matrix4& matrix) {
        uint32_t bits[16] = {};
        std::memcpy(bits, matrix.m, sizeof(bits));
        Msg("ph_dx12_bridge: bits %s=%08X,%08X,%08X,%08X,"
            "%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,"
            "%08X,%08X,%08X,%08X",
            name,
            bits[0], bits[1], bits[2], bits[3],
            bits[4], bits[5], bits[6], bits[7],
            bits[8], bits[9], bits[10], bits[11],
            bits[12], bits[13], bits[14], bits[15]);
    };
    logMatrix("view", view);
    logMatrix("projection", projection);
    logMatrix("full", fullTransform);
}

struct FrustumPlane
{
    float nx = 0.0f;
    float ny = 0.0f;
    float nz = 0.0f;
    float d = 0.0f;
};

bool NormalizePlane(FrustumPlane& plane)
{
    const float length =
        std::sqrt(plane.nx * plane.nx + plane.ny * plane.ny +
                  plane.nz * plane.nz);
    if (!std::isfinite(length) || length < 1.0e-9f)
        return false;
    plane.nx /= length;
    plane.ny /= length;
    plane.nz /= length;
    plane.d /= length;
    return true;
}

bool BuildFrustumPlanes(const Matrix4& fullTransform,
                        FrustumPlane planes[6])
{
    // Row-vector convention: clip.x = dot(p, column0), etc. D3D clip volume
    // is -w <= x,y <= w and 0 <= z <= w.
    float column[4][4] = {};
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t index = 0; index < 4; ++index)
            column[index][row] = fullTransform.m[row][index];

    const auto set = [&planes, &column](uint32_t index, const float sign0,
                                        const float sign3) {
        FrustumPlane& plane = planes[index];
        plane.nx = column[0][0] * sign0 + column[3][0] * sign3;
        plane.ny = column[0][1] * sign0 + column[3][1] * sign3;
        plane.nz = column[0][2] * sign0 + column[3][2] * sign3;
        plane.d = column[0][3] * sign0 + column[3][3] * sign3;
        return NormalizePlane(plane);
    };
    const auto setAxis = [&planes, &column](uint32_t index, const float sign1,
                                            const float sign3) {
        FrustumPlane& plane = planes[index];
        plane.nx = column[1][0] * sign1 + column[3][0] * sign3;
        plane.ny = column[1][1] * sign1 + column[3][1] * sign3;
        plane.nz = column[1][2] * sign1 + column[3][2] * sign3;
        plane.d = column[1][3] * sign1 + column[3][3] * sign3;
        return NormalizePlane(plane);
    };

    if (!set(0, 1.0f, 1.0f) || !set(1, -1.0f, 1.0f) ||
        !setAxis(2, 1.0f, 1.0f) || !setAxis(3, -1.0f, 1.0f))
        return false;

    FrustumPlane& nearPlane = planes[4];
    nearPlane.nx = column[2][0];
    nearPlane.ny = column[2][1];
    nearPlane.nz = column[2][2];
    nearPlane.d = column[2][3];
    FrustumPlane& farPlane = planes[5];
    farPlane.nx = column[3][0] - column[2][0];
    farPlane.ny = column[3][1] - column[2][1];
    farPlane.nz = column[3][2] - column[2][2];
    farPlane.d = column[3][3] - column[2][3];
    return NormalizePlane(nearPlane) && NormalizePlane(farPlane);
}

bool SphereInFrustum(const Matrix4& fullTransform, const float center[3],
                     float radius, float distances[6])
{
    FrustumPlane planes[6];
    if (!BuildFrustumPlanes(fullTransform, planes))
        return false;
    bool visible = true;
    for (uint32_t index = 0; index < 6; ++index)
    {
        const FrustumPlane& plane = planes[index];
        const float distance = plane.nx * center[0] +
                               plane.ny * center[1] +
                               plane.nz * center[2] + plane.d;
        distances[index] = distance;
        if (distance < -radius)
            visible = false;
    }
    return visible;
}
} // namespace

#ifdef OGSR_J2B_M1_RUNTIME_PROBE
void RunJ2BBatchRuntimeProbe(const char* levelName, uint64_t session,
                             uint64_t epoch) noexcept
{
    FS_Path* path = FS.get_path(fsgame::level);
    const char* before = (path != nullptr && path->m_Add != nullptr)
                             ? path->m_Add
                             : "";
    char beforeCopy[260] = {};
    std::snprintf(beforeCopy, sizeof(beforeCopy), "%s", before);

    j2bbatch12::BatchResult12 result;
    const std::array<uint32_t, 4> visuals = {9, 105, 106, 107};
    const bool ok = GameDx12J2ABatch_PrepareFromVfs(
        levelName, {session, epoch}, 129, 17431, visuals, nullptr, false,
        result, j2bbatch12::Limits12{});

    uint64_t contentHash = 1469598103934665603ull;
    for (const j2bbatch12::Slot12& slot : result.slots)
    {
        const uint32_t values[] = {
            slot.source.visualId,    slot.source.sourceVbId,
            slot.source.sourceVBase, slot.source.sourceVCount,
            slot.source.sourceIbId,  slot.source.sourceIBase,
            slot.source.sourceICount, slot.vertexCount,
            slot.indexCount,         slot.instance.sectorId,
            slot.instance.rootVisualId, slot.instance.occurrenceOrdinal,
            slot.instance.visualId};
        const uint64_t hashes[] = {slot.assetVbHash, slot.assetIbHash};
        contentHash = j2bbatch12::HashBytes(contentHash, values,
                                            sizeof(values));
        contentHash = j2bbatch12::HashBytes(contentHash, hashes,
                                            sizeof(hashes));
    }

    const char* after = (path != nullptr && path->m_Add != nullptr)
                            ? path->m_Add
                            : "";
    const bool unchanged = std::strcmp(beforeCopy, after) == 0;
    Msg("ph_dx12_j2b_m1_probe: level='%s' session=%llu epoch=%llu ok=%d "
        "hash=%016llX content_hash=%016llX error=%u text='%s' "
        "path_before='%s' path_after='%s' path_unchanged=%d",
        levelName != nullptr ? levelName : "?",
        static_cast<unsigned long long>(session),
        static_cast<unsigned long long>(epoch), ok ? 1 : 0,
        static_cast<unsigned long long>(result.stableHash),
        static_cast<unsigned long long>(contentHash),
        static_cast<unsigned>(result.error.code), result.error.text,
        beforeCopy, after, unchanged ? 1 : 0);
}
#endif

bool GameDx12Bridge_CreateNativeViewport()
{
    if (!RuntimeRequested() || !State().nativeRequested)
        return true;
    if (!EnsureOwnerThread("native_create") || !EnsureInitialized())
        return false;
    BridgeStateData& state = State();
    ComPtr<IDXGIFactory2> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
    if (SUCCEEDED(hr))
    {
        const GameDx12NativeFrame12::Owner owner{
            *state.device.Get(), *state.queue.Get(), *state.fence.Get(),
            state.fenceEvent, state.fenceValue};
        hr = state.native.Initialize(Device.m_hWnd, *factory.Get(), owner);
    }
    if (FAILED(hr))
    {
        Msg("ph_dx12_native: initialization failed hr=0x%08X", static_cast<unsigned>(hr));
        ReleaseObjects();
        state.state = BridgeState::Disabled;
        return false;
    }
    Msg("ph_dx12_native: DX12 owns game HWND and swapchain");
    return true;
}

bool GameDx12Bridge_UsesNativeViewport()
{
    return State().nativeRequested;
}

void GameDx12Bridge_ApplyNativeVideoMode(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
    {
        Msg("ph_dx12_native: invalid video mode %ux%u", width, height);
        return;
    }
    // Размеры игровой камеры остаются привязаны к ресурсам R4.
    // DX12 пересоздаёт цели кадра по новому размеру клиентской области.
    GameDx12Bridge_OnDeviceReset(Device.dwWidth, Device.dwHeight);
    if (State().state != BridgeState::Ready)
        return;
    const HWND window = Device.m_hWnd;
    RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    const auto style = static_cast<DWORD>(GetWindowLongPtr(window, GWL_STYLE));
    const auto exStyle = static_cast<DWORD>(GetWindowLongPtr(window, GWL_EXSTYLE));
    if (!AdjustWindowRectEx(&rect, style, FALSE, exStyle) ||
        !SetWindowPos(window, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
        Msg("ph_dx12_native: could not apply video mode %ux%u error=%lu",
            width, height, GetLastError());
}

// F4.1 evidence-only свободная камера (определение ниже, файловая область).
bool FlyCameraMatrix(Matrix4& result, uint64_t sequence, float position[3]);

namespace
{
bool DrainNative(BridgeStateData& state, const char* stage)
{
    if (state.native.CompletionUnknown())
    {
        QuarantineForGeometryUnknown(stage);
        return false;
    }
    if (!state.native.IsInitialized())
        return true;
    const GameDx12NativeFrame12::Owner owner{
        *state.device.Get(), *state.queue.Get(), *state.fence.Get(),
        state.fenceEvent, state.fenceValue};
    const HRESULT hr = state.native.Drain(owner);
    if (SUCCEEDED(hr))
        return true;
    Msg("ph_dx12_native: drain failed stage=%s hr=0x%08X",
        stage, static_cast<unsigned>(hr));
    state.m23.Shutdown(false);
    QuarantineForGeometryUnknown(stage);
    return false;
}

bool LightingFlag(const char* name)
{
    const char* value = std::getenv(name);
    return value && std::strcmp(value, "1") == 0;
}

struct LightingDiagnostics
{
    bool sceneLog = false;
    bool traceNearest = false;
    uint64_t lightId = 0;
    std::array<uint64_t, gamelight12::MaxLocalLightTrace> fixedIds{};
    uint32_t fixedCount = 0;
    bool freezeRecord = false;
    uint32_t probeFrames = 0;
    uint32_t probeDelay = 0;
    gamelight12::Vec3 origin;
};

const LightingDiagnostics& LightingDiagnosticOptions()
{
    static const LightingDiagnostics options = []
    {
        LightingDiagnostics result;
        const bool sceneArgument = GameDx12Bridge_HasExactCommandLineFlag(Core.Params, "-SceneLog");
        result.sceneLog = sceneArgument || LightingFlag("OGSR_DX12_GAME_SCENE_LOG");
        result.traceNearest = sceneArgument || LightingFlag("OGSR_DX12_GAME_TRACE_LIGHTS");
        const auto readId = [](const char* begin, const char* end)
        {
            uint64_t id = 0;
            const auto parsed = std::from_chars(begin, end, id);
            return parsed.ec == std::errc{} && parsed.ptr == end ? id : uint64_t{0};
        };
        const char* argument = nullptr;
        const bool hasLightId = GameDx12Bridge_HasExactCommandLineFlag(Core.Params, "-LightId", &argument);
        if (hasLightId)
        {
            if (*argument == '"')
                ++argument;
            argument += std::strlen("-LightId");
            if (*argument == '"')
                ++argument;
            argument += std::strspn(argument, " \t");
            const bool quoted = *argument == '"';
            if (quoted)
                ++argument;
            const char* end = argument + std::strcspn(argument, quoted ? "\"" : " \t");
            if (!quoted || (*end == '"' && (end[1] == '\0' || end[1] == ' ' || end[1] == '\t')))
                result.lightId = readId(argument, end);
        }
        else if (const char* value = std::getenv("OGSR_DX12_GAME_TRACE_LIGHT_ID"))
            result.lightId = readId(value, value + std::strlen(value));
        const auto readBounded = [&readId](const char* key, uint32_t maxValue)
        {
            const char* value = std::getenv(key);
            return value ? static_cast<uint32_t>((std::min)(
                readId(value, value + std::strlen(value)), uint64_t{maxValue})) : 0u;
        };
        result.probeFrames = readBounded("OGSR_DX12_GAME_LIGHT_PROBE_FRAMES", 600);
        result.probeDelay = readBounded("OGSR_DX12_GAME_LIGHT_PROBE_DELAY", 3600);
        result.freezeRecord = result.lightId != 0 && LightingFlag("OGSR_DX12_GAME_FREEZE_LIGHT_RECORD");
        if (const char* first = std::getenv("OGSR_DX12_GAME_FIXED_LIGHT_IDS"); first && *first)
        {
            const char* cursor = first;
            while (result.fixedCount < result.fixedIds.size())
            {
                const char* end = cursor + std::strcspn(cursor, ",");
                const auto id = readId(cursor, end);
                if (id == 0 || std::find(result.fixedIds.begin(), result.fixedIds.end(), id) != result.fixedIds.end())
                {
                    result.fixedCount = 0;
                    break;
                }
                result.fixedIds[result.fixedCount++] = id;
                if (*end == '\\0')
                    break;
                cursor = end + 1;
                if (result.fixedCount == result.fixedIds.size())
                {
                    result.fixedCount = 0; // Лишний элемент или завершающая запятая.
                    break;
                }
            }
        }
        if (result.fixedCount < 2 || result.freezeRecord)
            result.fixedCount = 0;
        if (const char* value = std::getenv("OGSR_DX12_GAME_LIGHT_PROBE_ORIGIN"))
        {
            gamelight12::Vec3 origin;
            const char* end = value + std::strlen(value);
            bool valid = true;
            for (float* component : {&origin.x, &origin.y, &origin.z})
            {
                const auto parsed = std::from_chars(value, end, *component);
                const bool last = component == &origin.z;
                if (parsed.ec != std::errc{} || !std::isfinite(*component) ||
                    (last ? parsed.ptr != end : parsed.ptr == end || *parsed.ptr != ','))
                {
                    valid = false;
                    break;
                }
                value = parsed.ptr + (last ? 0 : 1);
            }
            if (valid)
                result.origin = origin;
        }
        result.sceneLog = result.sceneLog || result.lightId != 0 || result.fixedCount != 0 || result.probeFrames != 0;
        Msg("ph_dx12_f43fr2: diagnostics fixed_count=%u fixed_ids=(%llu,%llu,%llu) freeze_record=%d "
            "probe_frames=%u probe_delay=%u probe_origin=(%.3f,%.3f,%.3f)",
            result.fixedCount, static_cast<unsigned long long>(result.fixedIds[0]),
            static_cast<unsigned long long>(result.fixedIds[1]), static_cast<unsigned long long>(result.fixedIds[2]),
            result.freezeRecord ? 1 : 0, result.probeFrames, result.probeDelay,
            result.origin.x, result.origin.y, result.origin.z);
        Msg("ph_dx12_f43fr1: diagnostics scene_log=%d trace_nearest=%d trace_light_id=%llu "
            "scene_log_arg=%d light_id_arg=%d light_id_arg_valid=%d scene_view='%s'",
            result.sceneLog ? 1 : 0, result.traceNearest && result.lightId == 0 ? 1 : 0,
            static_cast<unsigned long long>(result.lightId), sceneArgument ? 1 : 0,
            hasLightId ? 1 : 0, hasLightId && result.lightId != 0 ? 1 : 0,
            std::getenv("OGSR_DX12_GAME_SCENE_VIEW") ? std::getenv("OGSR_DX12_GAME_SCENE_VIEW") : "0");
        return result;
    }();
    return options;
}

void OverrideDiagnosticLights(BridgeStateData& state, const LightingDiagnostics& options,
                              const float position[3], gamelight12::FrameSnapshot& snapshot)
{
    using gamelight12::FrameSnapshot;
    auto& probe = state.localLightProbe;
    auto& lights = snapshot.localLights;
    auto& count = snapshot.localCount;
    count = 0;
    snapshot.probeMode = options.freezeRecord ? FrameSnapshot::ProbeMode::FrozenRecord :
        FrameSnapshot::ProbeMode::FixedSelection;
    if (options.freezeRecord && probe.frozen.id != 0)
    {
        lights[count++] = probe.frozen;
        return;
    }

    // Только диагностический путь. Запрос привязан к исходной точке
    // комнаты и продолжает читать живые игровые источники при повороте.
    constexpr float probeRadius = 24.0f;
    g_SpatialSpace->q_sphere(state.nearLightQuery, 0, STYPE_LIGHTSOURCE, probe.anchor, probeRadius);
    const bool limitShadows = ps_r2_ls_flags.test(R2FLAG_LIGHT_NO_DIST_SHADOWS);
    for (ISpatial* entry : state.nearLightQuery)
    {
        const IRender_Light* source = entry->dcast_Light();
        gamelight12::LocalLight candidate;
        if (!source || gamelight12::ReadWorldLight(*source, candidate) != gamelight12::LocalRejection::None ||
            (std::max)({candidate.color.x, candidate.color.y, candidate.color.z}) <= 0.0f)
            continue;
        if (options.freezeRecord)
        {
            if (candidate.id != options.lightId)
                continue;
            // Замороженная запись без карты, чтобы её вклад зависел только
            // от данных GPU и выбранного пикселя.
            candidate.castsShadow = false;
            probe.frozen = candidate;
            lights[count++] = candidate;
            break;
        }
        if (std::find(options.fixedIds.begin(), options.fixedIds.begin() + options.fixedCount, candidate.id) ==
            options.fixedIds.begin() + options.fixedCount)
            continue;
        const double dx = double{candidate.position.x} - position[0];
        const double dy = double{candidate.position.y} - position[1];
        const double dz = double{candidate.position.z} - position[2];
        const double distanceSquared = dx * dx + dy * dy + dz * dz;
        const double rangeSquared = double{candidate.range} * candidate.range;
        const double luminance = 0.2126 * candidate.color.x + 0.7152 * candidate.color.y + 0.0722 * candidate.color.z;
        candidate.selectionScore = luminance * rangeSquared / (rangeSquared + distanceSquared);
        candidate.castsShadow = candidate.castsShadow && (!limitShadows || distanceSquared <= 10000.0);
        lights[count++] = candidate;
        if (count == options.fixedCount)
            break;
    }
    std::sort(lights.begin(), lights.begin() + count,
        [](const auto& left, const auto& right) { return left.id < right.id; });
}

void CaptureLocalLights(BridgeStateData& state, const DirectX::XMFLOAT4X4& viewProjection,
                        const float position[3], gamelight12::FrameSnapshot& snapshot)
{
    using gamelight12::LocalRejection;
    const auto& options = LightingDiagnosticOptions();
    const uint64_t traceLightId = options.lightId;
    snapshot.tracedLightId = traceLightId;
    snapshot.diagnosticOrigin = options.origin;
    const bool overrideLights = options.fixedCount != 0 || options.freezeRecord;
    const bool traceRequested = traceLightId != 0 || options.traceNearest || options.fixedCount != 0;
    if (overrideLights || options.probeFrames != 0)
    {
        auto& probe = state.localLightProbe;
        if (probe.levelSession != snapshot.levelSession || probe.levelEpoch != snapshot.levelEpoch)
        {
            probe = {};
            probe.levelSession = snapshot.levelSession;
            probe.levelEpoch = snapshot.levelEpoch;
            probe.anchor.set(position[0], position[1], position[2]);
        }
        if (probe.frames < options.probeDelay + options.probeFrames)
        {
            snapshot.diagnosticBurst = probe.frames >= options.probeDelay;
            ++probe.frames;
        }
    }
    bool trace = snapshot.diagnosticBurst;
    if (!trace && traceRequested)
    {
        const auto now = std::chrono::steady_clock::now();
        trace = now >= state.nextLocalLightTrace;
        if (trace)
            state.nextLocalLightTrace = now + std::chrono::seconds(3);
    }

    if (!g_SpatialSpace || !snapshot.localEnabled)
    {
        if (trace && traceLightId != 0)
        {
            auto& record = snapshot.localTrace[snapshot.localTraceCount++];
            record.id = traceLightId;
            record.distance = record.radius = -1.0f;
            record.rejection = LocalRejection::Disabled;
        }
        return;
    }

    Fmatrix view{};
    static_assert(sizeof(view.m) == sizeof(viewProjection.m));
    std::memcpy(view.m, viewProjection.m, sizeof(view.m));
    CFrustum frustum{};
    frustum.CreateFromMatrix(view, FRUSTUM_P_LRTB | FRUSTUM_P_FAR);

    // Расстояние от глаза до угла near plane текущей проекции.
    const float scaleX = std::hypot(viewProjection._11, viewProjection._21, viewProjection._31);
    const float scaleY = std::hypot(viewProjection._12, viewProjection._22, viewProjection._32);
    const float nearGuard = VIEWPORT_NEAR * std::hypot(1.0f,
        1.0f / (std::max)(0.01f, scaleX), 1.0f / (std::max)(0.01f, scaleY));
    const Fvector eye{position[0], position[1], position[2]};
    struct ClearQuery
    {
        xr_vector<ISpatial*>& frustum;
        xr_vector<ISpatial*>& nearLights;
        ~ClearQuery() { frustum.clear(); nearLights.clear(); }
    } clearQuery{state.lightQuery, state.nearLightQuery};
    g_SpatialSpace->q_frustum(state.lightQuery, 0, STYPE_LIGHTSOURCE, frustum);
    g_SpatialSpace->q_sphere(state.nearLightQuery, 0, STYPE_LIGHTSOURCE, eye, nearGuard);
    const bool limitDistantShadows = ps_r2_ls_flags.test(R2FLAG_LIGHT_NO_DIST_SHADOWS);
    const auto consider = [&](ISpatial* entry, bool inFrustum, bool inNearQuery)
    {
        IRender_Light* source = entry->dcast_Light();
        if (!source)
            return;
        gamelight12::LocalLight candidate{};
        auto rejection = gamelight12::ReadWorldLight(*source, candidate);
        const double dx = double{candidate.position.x} - position[0];
        const double dy = double{candidate.position.y} - position[1];
        const double dz = double{candidate.position.z} - position[2];
        const double distanceSquared = dx * dx + dy * dy + dz * dz;
        const double rangeSquared = double{candidate.range} * candidate.range;
        const double luminance = 0.2126 * candidate.color.x +
                                 0.7152 * candidate.color.y +
                                 0.0722 * candidate.color.z;
        if (rejection == LocalRejection::None && luminance <= 0.0)
            rejection = LocalRejection::NoEnergy;
        const auto& sphere = entry->spatial.sphere;
        const float nearRadius = sphere.R + nearGuard;
        const bool nearVolume = std::isfinite(nearRadius) && nearRadius > 0.0f &&
            eye.distance_to_sqr(sphere.P) <= nearRadius * nearRadius;
        if (rejection == LocalRejection::None && !inFrustum && !nearVolume)
            rejection = LocalRejection::OutsideNearSphere;
        const bool eligible = rejection == LocalRejection::None;
        bool cameraInside = false;
        if (eligible)
        {
            candidate.castsShadow = candidate.castsShadow &&
                (!limitDistantShadows || distanceSquared <= 10000.0);
            const double axial = -(dx * candidate.direction.x + dy * candidate.direction.y +
                dz * candidate.direction.z);
            cameraInside = distanceSquared <= rangeSquared &&
                (candidate.type == gamelight12::LocalType::Point ||
                 (axial >= 0.0 && axial * axial >= distanceSquared * candidate.outerCos * candidate.outerCos));
            candidate.nearVolume = nearVolume;
            candidate.selectionScore = luminance * rangeSquared / (rangeSquared + distanceSquared);
            ++snapshot.localCandidateCount;

            uint32_t insert = 0;
            for (; insert < snapshot.localCount; ++insert)
            {
                const auto& current = snapshot.localLights[insert];
                if (candidate.nearVolume != current.nearVolume)
                {
                    if (candidate.nearVolume)
                        break;
                    continue;
                }
                if (candidate.selectionScore > current.selectionScore ||
                    (candidate.selectionScore == current.selectionScore && candidate.id < current.id))
                    break;
            }
            if (insert < snapshot.localLights.size())
            {
                if (snapshot.localCount < snapshot.localLights.size())
                    ++snapshot.localCount;
                for (uint32_t index = snapshot.localCount - 1; index > insert; --index)
                    snapshot.localLights[index] = snapshot.localLights[index - 1];
                snapshot.localLights[insert] = candidate;
            }
        }

        const bool fixedTrace = traceLightId == 0 && options.fixedCount != 0;
        if (!trace || candidate.id == 0 || (traceLightId != 0 && candidate.id != traceLightId) ||
            (fixedTrace && std::find(options.fixedIds.begin(), options.fixedIds.begin() + options.fixedCount,
                candidate.id) == options.fixedIds.begin() + options.fixedCount) || !std::isfinite(distanceSquared))
            return;
        gamelight12::LocalLightTrace record;
        record.id = candidate.id;
        record.distance = static_cast<float>(std::sqrt(distanceSquared));
        record.radius = candidate.range;
        record.position = candidate.position;
        record.direction = candidate.direction;
        record.color = candidate.color;
        record.outerCos = candidate.outerCos;
        record.innerCos = candidate.innerCos;
        record.type = candidate.type;
        record.inFrustumQuery = inFrustum;
        record.inNearQuery = inNearQuery;
        record.nearVolume = nearVolume;
        record.rejection = rejection;
        record.active = candidate.active;
        record.candidate = eligible;
        record.cameraInside = cameraInside;
        record.sourceShadow = candidate.sourceShadow;
        record.shadowEligible = eligible && candidate.castsShadow;
        uint32_t insert = 0;
        for (; insert < snapshot.localTraceCount; ++insert)
        {
            const auto& other = snapshot.localTrace[insert];
            if (record.distance < other.distance ||
                (record.distance == other.distance && record.id < other.id))
                break;
        }
        if (insert == snapshot.localTrace.size())
            return;
        if (snapshot.localTraceCount < snapshot.localTrace.size())
            ++snapshot.localTraceCount;
        for (uint32_t index = snapshot.localTraceCount - 1; index > insert; --index)
            snapshot.localTrace[index] = snapshot.localTrace[index - 1];
        snapshot.localTrace[insert] = record;
    };

    for (ISpatial* entry : state.lightQuery)
        consider(entry, true, trace &&
            std::find(state.nearLightQuery.begin(), state.nearLightQuery.end(), entry) != state.nearLightQuery.end());
    for (ISpatial* entry : state.nearLightQuery)
    {
        if (std::find(state.lightQuery.begin(), state.lightQuery.end(), entry) == state.lightQuery.end())
            consider(entry, false, true);
    }

    // При изменении ранга тот же набор света получает те же GPU-индексы.
    std::sort(snapshot.localLights.begin(), snapshot.localLights.begin() + snapshot.localCount,
        [](const auto& left, const auto& right) { return left.id < right.id; });
    const auto slotOf = [&](uint64_t id)
    {
        for (uint32_t i = 0; i < snapshot.localCount; ++i)
            if (snapshot.localLights[i].id == id)
                return static_cast<int32_t>(i);
        return int32_t{-1};
    };
    const auto addMissing = [&](uint64_t id)
    {
        const auto begin = snapshot.localTrace.begin();
        const auto end = begin + snapshot.localTraceCount;
        if (std::find_if(begin, end, [id](const auto& entry) { return entry.id == id; }) == end &&
            snapshot.localTraceCount < snapshot.localTrace.size())
        {
            auto& record = snapshot.localTrace[snapshot.localTraceCount++];
            record.id = id;
            record.distance = record.radius = -1.0f;
            record.rejection = LocalRejection::NotInSpatialQuery;
        }
    };
    if (trace && traceLightId != 0)
        addMissing(traceLightId);
    else if (trace)
        for (uint32_t i = 0; i < options.fixedCount; ++i)
            addMissing(options.fixedIds[i]);

    for (uint32_t i = 0; i < snapshot.localTraceCount; ++i)
    {
        auto& record = snapshot.localTrace[i];
        record.productionSlot = slotOf(record.id);
        if (record.candidate && record.productionSlot < 0)
            record.rejection = LocalRejection::LightingBudget;
    }
    if (overrideLights)
        OverrideDiagnosticLights(state, options, position, snapshot);
    for (uint32_t i = 0; i < snapshot.localTraceCount; ++i)
        snapshot.localTrace[i].gpuSlot = slotOf(snapshot.localTrace[i].id);
    for (uint32_t i = 0; i < snapshot.localCount; ++i)
        snapshot.localShadowCandidateCount += snapshot.localLights[i].castsShadow ? 1u : 0u;
}

gamelight12::FrameSnapshot CaptureLighting(BridgeStateData& state,
                                           const DirectX::XMFLOAT4X4& viewProjection,
                                           const float position[3])
{
    gamelight12::FrameSnapshot snapshot{};
    snapshot.levelSession = state.levelSession;
    snapshot.levelEpoch = state.captureEpoch;
    snapshot.frame = Device.dwFrame;

    static const bool lighting = LightingFlag("OGSR_DX12_GAME_LIGHTING");
    if (!lighting || !g_pGamePersistent)
    {
        CaptureLocalLights(state, viewProjection, position, snapshot);
        return snapshot;
    }

    const CEnvDescriptorMixer* env = g_pGamePersistent->Environment().CurrentEnv;
    if (!env || !FiniteVector(env->ambient) || !std::isfinite(ps_r2_sun_lumscale_amb))
    {
        CaptureLocalLights(state, viewProjection, position, snapshot);
        return snapshot;
    }

    // Ambient и оттенок отражённого света берутся из одного погодного снимка R4.
    const float ambientScale = (std::max)(0.f, ps_r2_sun_lumscale_amb);
    const auto ambient = [ambientScale](float value)
    {
        return (std::max)(value * 2.f, 0.001f) * ambientScale;
    };
    snapshot.ambient = {ambient(env->ambient.x), ambient(env->ambient.y), ambient(env->ambient.z)};
    if (!std::isfinite(snapshot.ambient.x) || !std::isfinite(snapshot.ambient.y) ||
        !std::isfinite(snapshot.ambient.z))
    {
        CaptureLocalLights(state, viewProjection, position, snapshot);
        return snapshot;
    }

    static const bool hemi = LightingFlag("OGSR_DX12_GAME_HEMI");
    if (hemi && std::isfinite(ps_r2_sun_lumscale_hemi))
    {
        const bool cop = g_pGamePersistent->Environment().USED_COP_WEATHER;
        const float scale = 2.0f * (std::max)(0.0f, ps_r2_sun_lumscale_hemi);
        const auto tint = [scale](float value)
        {
            return (std::max)(0.0f, value * 2.0f + EPS) * scale;
        };
        const auto& copColor = env->hemi_color_unmodded;
        const auto& skyColor = env->sky_color;
        snapshot.hemi = {tint(cop ? copColor.x : skyColor.x),
                         tint(cop ? copColor.y : skyColor.y),
                         tint(cop ? copColor.z : skyColor.z)};
        snapshot.hemiEnabled = std::isfinite(snapshot.hemi.x) &&
            std::isfinite(snapshot.hemi.y) && std::isfinite(snapshot.hemi.z);
        if (!snapshot.hemiEnabled)
            snapshot.hemi = {};
    }

    static const bool sun = LightingFlag("OGSR_DX12_GAME_SUN");
    static const bool local = LightingFlag("OGSR_DX12_GAME_LOCAL_LIGHTS");
    static const bool shadows = LightingFlag("OGSR_DX12_GAME_SHADOWS");
    const auto& direction = env->sun_dir;
    const auto& color = env->sun_color;
    snapshot.enabled = true;
    snapshot.sunEnabled = sun && ps_r2_ls_flags.test(R2FLAG_SUN) && direction.y < 0.0f &&
        gamelight12::NormalizeDirection(direction.x, direction.y, direction.z, snapshot.sunDirection) &&
        gamelight12::DecodeLightColor(color.x * ps_r2_sun_lumscale, color.y * ps_r2_sun_lumscale,
                                      color.z * ps_r2_sun_lumscale, snapshot.sunColor) &&
        (std::max)({snapshot.sunColor.x, snapshot.sunColor.y, snapshot.sunColor.z}) > 0.0f;
    snapshot.localEnabled = local && !ps_r2_ls_flags_ext.test(R2FLAGEXT_DISABLE_LIGHT);
    snapshot.shadowsEnabled = shadows && snapshot.sunEnabled;
    CaptureLocalLights(state, viewProjection, position, snapshot);
    return snapshot;
}

gamewater12::Frame CaptureWater()
{
    gamewater12::Frame snapshot{};
    if ((!gamewater12::WaterEnabled() && !gamewater12::PuddlesEnabled()) || !g_pGamePersistent)
        return snapshot;
    const auto& environment = g_pGamePersistent->Environment();
    const auto* env = environment.CurrentEnv;
    if (!env || !std::isfinite(Device.fTimeGlobal) || !std::isfinite(env->wind_direction) ||
        !std::isfinite(env->wind_velocity) || !std::isfinite(env->rain_density) ||
        !std::isfinite(environment.wetness_factor))
        return snapshot;
    gamelight12::Vec3 sky;
    if (!gamelight12::DecodeLightColor(env->sky_color.x, env->sky_color.y, env->sky_color.z, sky))
        return snapshot;
    const float wind = std::clamp(env->wind_velocity * 0.001f, 0.0f, 1.0f);
    snapshot.weather = {Device.fTimeGlobal, wind * std::sin(env->wind_direction),
        wind * std::cos(env->wind_direction), std::clamp(env->rain_density, 0.0f, 1.0f)};
    snapshot.sky = {sky.x, sky.y, sky.z};
    snapshot.wetness = std::clamp(environment.wetness_factor, 0.0f, 1.0f);
    snapshot.valid = true;
    return snapshot;
}

void RenderNativeFrame(BridgeStateData& state, const Matrix4& fullTransform)
{
    if (state.nativeFailed)
        return;
    const GameDx12NativeFrame12::Owner owner{
        *state.device.Get(), *state.queue.Get(), *state.fence.Get(),
        state.fenceEvent, state.fenceValue};
    using FrameClock = std::chrono::steady_clock;
    const auto elapsed = [](auto start, auto end)
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    };
    const auto start = FrameClock::now();
    GameDx12NativeFrame12::Recording recording;
    HRESULT hr = state.native.Begin(Device.m_hWnd, owner, recording);
    const auto begin = FrameClock::now();
    if (hr == S_FALSE)
        return;
    uint32_t draws = 0;
    GameDx12FourVisual12::NativeTimings12 timings;
    std::string reason;
    bool sceneRecorded = false;
    GameDx12StaticScene12::AuxStats auxDraws;
    GameDx12StaticScene12::FrameStats12 sceneStats;
    gamelight12::FrameSnapshot lighting;
    const bool sceneDetail = LightingDiagnosticOptions().sceneLog;
    double prepareMs = 0.0;
    if (SUCCEEDED(hr) && state.staticSceneBound && state.staticScene.IsLoaded() &&
        state.staticScene.LevelSession() == state.levelSession &&
        state.staticScene.LevelEpoch() == state.captureEpoch)
    {
        DirectX::XMFLOAT4X4 viewProjection;
        Matrix4 flyTransform{};
        float position[]{Device.vCameraPosition.x,
                         Device.vCameraPosition.y,
                         Device.vCameraPosition.z};
        if (FlyCameraMatrix(flyTransform, state.captureSequence, position))
        {
            std::memcpy(&viewProjection, flyTransform.m, sizeof(viewProjection));
        }
        else
        {
            std::memcpy(&viewProjection, fullTransform.m, sizeof(viewProjection));
        }
        if (recording.width != state.width || recording.height != state.height)
        {
            const float sourceAspect =
                static_cast<float>(state.width) / state.height;
            const float clientAspect =
                static_cast<float>(recording.width) / recording.height;
            for (auto& row : viewProjection.m)
                row[0] *= sourceAspect / clientAspect;
        }
        try
        {
            const GameDx12StaticSceneOwner12 upload{
                state.device.Get(), state.queue.Get(), state.fence.Get(), state.fenceEvent,
                state.allocator.Get(), state.list.Get(), &state.fenceValue};
            lighting = CaptureLighting(state, viewProjection, position);
            const bool prepared = state.gameVisuals.Prepare(*g_pGameLevel, position, state.staticScene, upload, reason);
            if (sceneDetail)
                prepareMs = elapsed(begin, FrameClock::now());
            if (!prepared ||
                !state.staticScene.RecordNativeFrame(
                    recording, viewProjection, position, state.gameVisuals.Snapshot(),
                    lighting, CaptureWater(), sceneStats, auxDraws, reason))
                hr = E_FAIL;
            else
                sceneRecorded = true;
        }
        catch (...)
        {
            hr = E_FAIL;
            reason = "exception preparing the scene frame";
        }
        if (sceneRecorded)
        {
            draws = sceneStats.draws + auxDraws.rigidDraws + auxDraws.animatedDraws + auxDraws.detailDraws;
            state.staticSceneDraws = sceneStats.draws;
            timings.mode = "static";
            timings.recordMs = sceneStats.record_ms;
            timings.snapshotMs = sceneStats.cull_ms;
            timings.serialMs = 0.0;
            timings.parallelMs = 0.0;
        }
        if (FAILED(hr))
            state.native.Cancel();
    }
    else if (SUCCEEDED(hr) && state.m23Bound && state.m23.IsLoaded() &&
        state.m23.LevelSession() == state.levelSession &&
        state.m23.LevelEpoch() == state.captureEpoch)
    {
        DirectX::XMFLOAT4X4 viewProjection;
        std::memcpy(&viewProjection, fullTransform.m, sizeof(viewProjection));
        if (recording.width != state.width || recording.height != state.height)
        {
            const float sourceAspect = static_cast<float>(state.width) / state.height;
            const float clientAspect = static_cast<float>(recording.width) / recording.height;
            for (auto& row : viewProjection.m)
                row[0] *= sourceAspect / clientAspect;
        }
        const float position[]{Device.vCameraPosition.x, Device.vCameraPosition.y,
                               Device.vCameraPosition.z};
        try
        {
            if (!state.m23.RecordNativeFrame(recording.list, recording.slot, viewProjection,
                                             position, draws, timings, reason))
                hr = E_FAIL;
        }
        catch (...)
        {
            hr = E_FAIL;
        }
        if (FAILED(hr))
            state.native.Cancel();
    }
    const auto presentStart = FrameClock::now();
    if (SUCCEEDED(hr))
        hr = state.native.Present(owner, draws != 0 ? state.nativeScenePresents + 1 : 0);
    const auto presentEnd = FrameClock::now();
    if (FAILED(hr))
    {
        state.nativeFailed = true;
        Msg("ph_dx12_native: frame blocked hr=0x%08X reason='%s'; restart to select R4",
            static_cast<unsigned>(hr), reason.c_str());
        if (state.native.CompletionUnknown() || state.staticScene.GpuCompletionUnknown())
        {
            state.m23.Shutdown(false);
            QuarantineForGeometryUnknown("native_frame");
        }
        return;
    }
    const auto count = state.native.PresentCount();
    if (hr == S_OK && sceneRecorded && sceneDetail)
    {
        auto& perf = state.nativePerf;
        if (perf.frames == 0)
            perf.start = start;
        ++perf.frames;
        perf.frameCpuMs += elapsed(begin, presentStart);
        perf.prepareCpuMs += prepareMs;
        perf.shadowCpuMs += sceneStats.shadowCpuMs;
        perf.sceneCpuMs += sceneStats.sceneCpuMs;
        perf.waterCpuMs += sceneStats.waterCpuMs;
        perf.gpuWaitMs += recording.slotFenceWaitMs;
        perf.presentMs += elapsed(presentStart, presentEnd);
        const auto& gpu = state.native.LastGpuTiming();
        if (gpu.present != 0)
        {
            ++perf.gpuFrames;
            perf.gpuMs += gpu.milliseconds;
        }
        perf.staticShadowDraws += sceneStats.staticShadowDraws;
        perf.dynamicShadowDraws += sceneStats.dynamicShadowDraws;
        perf.spotShadowMaps += sceneStats.spotShadowMaps;
        perf.pointShadowFaces += sceneStats.pointShadowFaces;
        perf.visibleLights += lighting.localCount;
        perf.shadowedLights += sceneStats.shadowedLights;
        perf.lightCandidates += lighting.localCandidateCount;
        perf.shadowCandidates += lighting.localShadowCandidateCount;
        perf.sceneCopies += sceneStats.sceneCopies;
        if (presentEnd - perf.start >= std::chrono::seconds(3))
        {
            const double frames = perf.frames;
            Msg("ph_dx12_f43f: perf frames=%u frame_wall_ms=%.3f frame_cpu_ms=%.3f prepare_cpu_ms=%.3f "
                "shadow_cpu_ms=%.3f scene_cpu_ms=%.3f water_cpu_ms=%.3f static_shadow_draws=%.2f "
                "dynamic_shadow_draws=%.2f spot_shadow_maps=%.2f point_shadow_faces=%.2f "
                "visible_lights=%.2f shadowed_lights=%.2f light_candidates=%.2f shadow_candidates=%.2f "
                "scene_copies=%.2f gpu_wait_ms=%.3f present_ms=%.3f gpu_ms=%.3f gpu_samples=%u validation=%d",
                perf.frames, elapsed(perf.start, presentEnd) / frames, perf.frameCpuMs / frames,
                perf.prepareCpuMs / frames, perf.shadowCpuMs / frames, perf.sceneCpuMs / frames,
                perf.waterCpuMs / frames, perf.staticShadowDraws / frames, perf.dynamicShadowDraws / frames,
                perf.spotShadowMaps / frames, perf.pointShadowFaces / frames, perf.visibleLights / frames,
                perf.shadowedLights / frames, perf.lightCandidates / frames, perf.shadowCandidates / frames,
                perf.sceneCopies / frames, perf.gpuWaitMs / frames, perf.presentMs / frames,
                perf.gpuFrames == 0 ? 0.0 : perf.gpuMs / perf.gpuFrames, perf.gpuFrames, state.gbvEnabled ? 1 : 0);
            perf = {};
        }
    }
    if (hr == S_OK && sceneRecorded)
        state.gameVisuals.LogPresent(count, state.fenceValue, auxDraws, state.staticScene.AuxResources());
    if (hr == S_OK && draws != 0)
    {
        const uint64_t frames = ++state.nativeScenePresents;
        const double frameMs = state.previousNativePresent == FrameClock::time_point{}
            ? 0.0 : elapsed(state.previousNativePresent, presentEnd);
        state.previousNativePresent = presentEnd;
        static const bool detailed = []
        {
            const char* value = std::getenv("OGSR_DX12_GAME_METRICS");
            return value != nullptr && std::strcmp(value, "1") == 0;
        }();
        if (detailed && frames <= 480)
        {
            const auto& gpu = state.native.LastGpuTiming();
            Msg("ph_dx12_metrics: frame=%llu present=%llu fence=%llu slot=%u draws=%u camera_hash=%016llX "
                "position=(%.3f,%.3f,%.3f) direction=(%.3f,%.3f,%.3f) mode=%s frame_ms=%.6f begin_wait_ms=%.6f "
                "snapshot_ms=%.6f serial_ms=%.6f parallel_ms=%.6f record_ms=%.6f "
                "present_ms=%.6f gpu_present=%llu gpu_ms=%.6f",
                static_cast<unsigned long long>(frames), static_cast<unsigned long long>(count),
                static_cast<unsigned long long>(state.fenceValue), recording.slot, draws,
                static_cast<unsigned long long>(state.lastMatrixHash),
                Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z,
                Device.vCameraDirection.x, Device.vCameraDirection.y, Device.vCameraDirection.z,
                timings.mode, frameMs, elapsed(start, begin),
                timings.snapshotMs, timings.serialMs, timings.parallelMs, timings.recordMs,
                elapsed(presentStart, presentEnd), static_cast<unsigned long long>(gpu.present), gpu.milliseconds);
        }
        if (frames == 1 || frames == 30 || frames % 120 == 0)
        {
            DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
            if (state.nativeMemoryAdapter)
                state.nativeMemoryAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
                                                               &memory);
            Msg("ph_dx12_gr: scene_present=%llu mode=%s draws=%u camera_hash=%016llX "
                "position=(%.3f,%.3f,%.3f) snapshot_ms=%.6f serial_ms=%.6f parallel_ms=%.6f "
                "record_ms=%.6f local_usage=%llu local_budget=%llu",
                static_cast<unsigned long long>(frames), timings.mode, draws,
                static_cast<unsigned long long>(state.lastMatrixHash),
                Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z,
                timings.snapshotMs, timings.serialMs, timings.parallelMs, timings.recordMs,
                static_cast<unsigned long long>(memory.CurrentUsage),
                static_cast<unsigned long long>(memory.Budget));
        }
    }
    if (hr == S_OK && (count == 1 || count == 30 || count % 120 == 0))
        Msg("ph_dx12_native: Present=%llu scene_draws=%u fence=%llu window=%ux%u camera_hash=%016llX",
            static_cast<unsigned long long>(count), draws,
            static_cast<unsigned long long>(state.fenceValue), recording.width, recording.height,
            static_cast<unsigned long long>(state.lastMatrixHash));
}
}

bool StaticSceneRequested()
{
    static const bool enabled = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_STATIC_SCENE");
        return !(value != nullptr && std::strcmp(value, "0") == 0);
    }();
    return enabled;
}

// F4.1-E: включение машинно-читаемого сравнения R4 <-> DX12.
bool ComparisonRequested()
{
    static const bool enabled = []
    {
        const char* value = std::getenv("OGSR_DX12_CMP");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

// F4.1-E: инвентарь динамических игровых объектов (спавн-объекты уровня).
//
// Двери в CoP — это не статика level.geom, а CPhysicObject со скелетным
// визуалом (аудит: PhysicObject.cpp:25-97, GameObject.cpp:262-263). Наш
// статический конвейер их не покрывает принципиально, поэтому их надо
// считать и классифицировать, а не смешивать с пропусками статики.
void LogDynamicObjectInventory()
{
    if (!ComparisonRequested())
        return;
    if (g_pGameLevel == nullptr)
        return;

    const u32 total = g_pGameLevel->Objects.o_count();
    Msg("ph_cmp: dyn summary objects=%u", total);

    // Группировка по (тип визуала, секция) без лишних аллокаций.
    struct Bucket12
    {
        u32 type = 0;
        u32 count = 0;
        u32 visible = 0;
        char section[64] = {};
    };
    std::vector<Bucket12> buckets;
    buckets.reserve(256);
    u32 withVisual = 0;
    u32 withoutVisual = 0;
    u32 visibleCount = 0;
    for (u32 index = 0; index < total; ++index)
    {
        CObject* object = g_pGameLevel->Objects.o_get_by_iterator(index);
        if (object == nullptr)
            continue;
        IRenderVisual* visual = object->Visual();
        const u32 type = visual != nullptr ? visual->getType() : 0xFFFFFFFFu;
        if (visual != nullptr)
            ++withVisual;
        else
            ++withoutVisual;
        const bool visible = object->getVisible() && visual != nullptr;
        if (visible)
            ++visibleCount;

        const char* section = object->cNameSect().c_str();
        Bucket12* bucket = nullptr;
        for (auto& candidate : buckets)
        {
            if (candidate.type == type &&
                std::strncmp(candidate.section, section, sizeof(candidate.section) - 1) == 0)
            {
                bucket = &candidate;
                break;
            }
        }
        if (bucket == nullptr)
        {
            Bucket12 created;
            created.type = type;
            std::snprintf(created.section, sizeof(created.section), "%s", section);
            buckets.push_back(created);
            bucket = &buckets.back();
        }
        ++bucket->count;
        if (visible)
            ++bucket->visible;
    }

    std::sort(buckets.begin(), buckets.end(),
              [](const Bucket12& left, const Bucket12& right)
              { return left.count > right.count; });
    Msg("ph_cmp: dyn summary objects=%u with_visual=%u without_visual=%u "
        "visible=%u buckets=%zu",
        total, withVisual, withoutVisual, visibleCount, buckets.size());
    const size_t limit = buckets.size() < 24 ? buckets.size() : 24;
    for (size_t index = 0; index < limit; ++index)
    {
        const Bucket12& bucket = buckets[index];
        Msg("ph_cmp: dyn bucket type=%u count=%u visible=%u section='%s'",
            bucket.type, bucket.count, bucket.visible, bucket.section);
    }
}
//
// Оба рендерера индексируют статику уровня одинаково: index в контейнере
// fsL_VISUALS(3) == id в CRender::Visuals == index в pools.visuals. Поэтому
// сравнение идёт прямым join по id, без эвристик.
//
// Печатаются только факта: что R4 загрузил (тип, сфера), что DX12 считает
// отрисовываемым и с какой причиной отказа. Отдельно — ид, где тип по R4 и по
// нашему парсеру расходятся: это дефект парсера, а не «неподдержанный тип».
void LogVisualComparison(const xraylevel12::LevelPools& pools,
                         const levelhierarchy12::LevelHierarchy& hierarchy)
{
    if (!ComparisonRequested())
        return;
    if (Render == nullptr)
    {
        Msg("ph_cmp: inv role=r4 unavailable reason='render interface is null'");
        return;
    }

    const int r4Count = static_cast<int>(Render->getVisualCount());
    Msg("ph_cmp: inv role=r4 count=%d pools_visuals=%zu hierarchy_visuals=%zu",
        r4Count, pools.visuals.size(), hierarchy.visuals.size());

    // F4.1-E: детали уровня — отдельный статический путь (CDetailManager),
    // он не входит в контейнер визуалов. Фиксируем наличие файла, чтобы
    // отчёт опирался на факт, а не на предположение.
    if (FS.exist("$level$", "level.details"))
    {
        Msg("ph_cmp: details present=1 path='$level$/level.details' "
            "supported=0 note='CDetailManager instanced clutter, not read by DX12'");
    }
    else
    {
        Msg("ph_cmp: details present=0");
    }

    const int count =
        (std::min)(r4Count, static_cast<int>(pools.visuals.size()));
    uint32_t r4Types[16] = {};
    uint32_t parsedTypes[16] = {};
    uint32_t typeMismatch = 0;
    uint32_t r4NormalButUnsupported = 0;
    for (int index = 0; index < r4Count; ++index)
    {
        IRenderVisual* visual = Render->getVisual(index);
        if (visual == nullptr)
        {
            Msg("ph_cmp: inv role=r4 id=%d state=missing", index);
            continue;
        }
        const u32 type = visual->getType();
        const vis_data& data = visual->getVisData();
        if (type < 16)
            ++r4Types[type];
        const bool inPools = index < static_cast<int>(pools.visuals.size());
        const auto& parsed = inPools ? pools.visuals[static_cast<size_t>(index)]
                                     : pools.visuals.front();
        if (inPools && parsed.type < 16)
            ++parsedTypes[parsed.type];
        const bool mismatch = inPools && parsed.type != type;
        if (mismatch)
            ++typeMismatch;
        if (inPools && type == 0 && !parsed.drawable)
            ++r4NormalButUnsupported;
        Msg("ph_cmp: inv role=r4 id=%d type=%u cx=%.3f cy=%.3f cz=%.3f r=%.3f "
            "dx12_drawable=%d dx12_reject=%u dx12_layout=%u mismatch=%d",
            index, static_cast<unsigned>(type), data.sphere.P.x,
            data.sphere.P.y, data.sphere.P.z, data.sphere.R,
            inPools ? (parsed.drawable ? 1 : 0) : -1,
            inPools ? static_cast<unsigned>(parsed.reject) : 0u,
            inPools ? static_cast<unsigned>(parsed.layout) : 0u,
            mismatch ? 1 : 0);
    }

    char r4Histogram[192] = {};
    char parsedHistogram[192] = {};
    size_t usedR4 = 0;
    size_t usedParsed = 0;
    for (uint32_t type = 0; type < 16; ++type)
    {
        if (r4Types[type] != 0)
        {
            const int written = std::snprintf(
                r4Histogram + usedR4, sizeof(r4Histogram) - usedR4,
                "type%u=%u ", type, r4Types[type]);
            if (written > 0 && static_cast<size_t>(written) < sizeof(r4Histogram) - usedR4)
                usedR4 += static_cast<size_t>(written);
        }
        if (parsedTypes[type] != 0)
        {
            const int written = std::snprintf(
                parsedHistogram + usedParsed, sizeof(parsedHistogram) - usedParsed,
                "type%u=%u ", type, parsedTypes[type]);
            if (written > 0 && static_cast<size_t>(written) < sizeof(parsedHistogram) - usedParsed)
                usedParsed += static_cast<size_t>(written);
        }
    }
    Msg("ph_cmp: hist role=r4 %s", r4Histogram);
    Msg("ph_cmp: hist role=dx12 %s", parsedHistogram);
    Msg("ph_cmp: inv summary r4_count=%d compared=%d type_mismatch=%u "
        "r4_normal_unsupported=%u",
        r4Count, count, typeMismatch, r4NormalButUnsupported);
}

// F4.1 evidence-only свободная камера.
//
// Позволяет снять одну и ту же РЕАЛЬНУЮ сцену из произвольной точки карты,
// не подменяя ни геометрию, ни проекцию: ориентация берётся из штатной
// игровой view-матрицы, проекция — штатная Device.mProject. Камера актёра и
// обычный кадр продолжают работать без этого флага.
//
// Формат OGSR_DX12_GAME_FLY: "x,y,z,yawDeg[,pitchDeg[,spinDegPerSecond]]".
bool FlyCameraMatrix(Matrix4& result, uint64_t sequence, float position[3])
{
    static const bool prepared = []
    {
        return true;
    }();
    (void)prepared;
    static char spec[256] = {};
    static bool specLoaded = false;
    static float origin[3] = {0.0f, 0.0f, 0.0f};
    static float yawDegrees = 0.0f;
    static float pitchDegrees = 0.0f;
    static float spinDegrees = 0.0f;
    static float drift[3] = {0.0f, 0.0f, 0.0f};
    static bool valid = false;
    if (!specLoaded)
    {
        specLoaded = true;
        const char* value = std::getenv("OGSR_DX12_GAME_FLY");
        if (value != nullptr && value[0] != '\0' &&
            std::strlen(value) < sizeof(spec))
        {
            std::snprintf(spec, sizeof(spec), "%s", value);
            const int parsed = std::sscanf(spec, "%f,%f,%f,%f,%f,%f,%f,%f,%f",
                                           &origin[0], &origin[1], &origin[2],
                                           &yawDegrees, &pitchDegrees,
                                           &spinDegrees, &drift[0], &drift[1],
                                           &drift[2]);
            valid = parsed >= 4;
        }
        if (valid)
        {
            Msg("ph_dx12_f41: fly camera origin=(%.2f,%.2f,%.2f) yaw=%.2f "
                "pitch=%.2f spin=%.2f drift=(%.2f,%.2f,%.2f)",
                origin[0], origin[1], origin[2], yawDegrees, pitchDegrees,
                spinDegrees, drift[0], drift[1], drift[2]);
        }
    }
    if (!valid)
        return false;

    // Дрейф задаёт скорость в м/с и позволяет доказать именно перемещение
    // камеры, а не только поворот.
    const float seconds = static_cast<float>(sequence) / 60.0f;
    const float eye[3] = {origin[0] + drift[0] * seconds,
                          origin[1] + drift[1] * seconds,
                          origin[2] + drift[2] * seconds};
    std::memcpy(position, eye, sizeof(eye));

    const float yaw =
        (yawDegrees + spinDegrees * static_cast<float>(sequence) / 60.0f) *
        (3.14159265358979323846f / 180.0f);
    const float pitch = pitchDegrees * (3.14159265358979323846f / 180.0f);
    const float cosYaw = std::cos(yaw);
    const float sinYaw = std::sin(yaw);
    const float cosPitch = std::cos(pitch);
    const float sinPitch = std::sin(pitch);

    // Базис берётся из штатной игровой view-матрицы: X-Ray хранит right/up/dir
    // в столбцах 0..2 (row-vector view). Так ориентация гарантированно
    // совпадает с игровой, включая соглашения уровня.
    float right[3] = {Device.mView.m[0][0], Device.mView.m[1][0],
                      Device.mView.m[2][0]};
    float up[3] = {Device.mView.m[0][1], Device.mView.m[1][1],
                   Device.mView.m[2][1]};
    float forward[3] = {Device.mView.m[0][2], Device.mView.m[1][2],
                        Device.mView.m[2][2]};

    const auto rotateYaw = [cosYaw, sinYaw](float v[3])
    {
        const float x = v[0];
        const float z = v[2];
        v[0] = x * cosYaw - z * sinYaw;
        v[2] = x * sinYaw + z * cosYaw;
    };
    rotateYaw(right);
    rotateYaw(up);
    rotateYaw(forward);

    if (pitchDegrees != 0.0f)
    {
        for (int c = 0; c < 3; ++c)
        {
            const float up0 = up[c];
            const float forward0 = forward[c];
            up[c] = up0 * cosPitch + forward0 * sinPitch;
            forward[c] = -up0 * sinPitch + forward0 * cosPitch;
        }
    }

    const auto dot3 = [](const float a[3], const float b[3])
    { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };

    Matrix4 view{};
    for (int i = 0; i < 3; ++i)
    {
        view.m[i][0] = right[i];
        view.m[i][1] = up[i];
        view.m[i][2] = forward[i];
        view.m[i][3] = 0.0f;
    }
    view.m[3][0] = -dot3(right, eye);
    view.m[3][1] = -dot3(up, eye);
    view.m[3][2] = -dot3(forward, eye);
    view.m[3][3] = 1.0f;

    // fullTransform = View * Projection (row-vector convention): p*world * V * P.
    Matrix4 product{};
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            float sum = 0.0f;
            for (int inner = 0; inner < 4; ++inner)
                sum += view.m[row][inner] * Device.mProject.m[inner][column];
            product.m[row][column] = sum;
        }
    }
    result = product;
    return true;
}
// F4.1: единый проход загрузки полной статики уровня.
//
// Штатные данные и штатный обход: VFS-снапшот (level + level.geom), парсер
// иерархии уровня, обход ВСЕХ секторов, общеуровневый индекс пулов. Никаких
// ручных списков визуалов и никакого выбора сектора здесь нет.
void LoadFullStaticScene(BridgeStateData& state)
{
    const j2bbatch12::Identity12 identity{state.levelSession, state.captureEpoch};
    j2bbatch12::Limits12 snapshotLimits{};
    // Полный уровень: лимиты вертикального среза (16/128 МБ) рассчитаны на
    // один сектор и здесь недостаточны.
    snapshotLimits.maxLevelBytes = 128ull * 1024ull * 1024ull;
    snapshotLimits.maxGeomBytes = 1024ull * 1024ull * 1024ull;

    const auto totalStart = std::chrono::steady_clock::now();
    j2bbatch12::Snapshot12 snapshot;
    j2bbatch12::ErrorInfo12 error;
    if (!GameDx12J2ABatch_ReadSnapshot(state.levelName, identity, snapshot,
                                       snapshotLimits, error))
    {
        Msg("ph_dx12_f41: load failed stage=snapshot code=%u reason='%s'",
            static_cast<unsigned>(error.code), error.text);
        return;
    }
    Msg("ph_dx12_f41: snapshot level_bytes=%zu geom_bytes=%zu open_ms=%.3f "
        "copy_ms=%.3f",
        snapshot.levelBytes.size(), snapshot.geomBytes.size(), snapshot.vfsOpenMs,
        snapshot.vfsCopyMs);

    levelhierarchy12::LevelHierarchy hierarchy;
    std::string reason;
    if (!levelhierarchy12::Parse(snapshot.levelBytes, hierarchy, reason))
    {
        Msg("ph_dx12_f41: load failed stage=hierarchy reason='%s'",
            reason.c_str());
        return;
    }
    std::vector<levelhierarchy12::LeafInstance> leaves;
    levelhierarchy12::CollectStats collectStats;
    const levelhierarchy12::CollectBudget budget{4000000u, 20000000u, 40000000u};
    const auto collectStatus = levelhierarchy12::CollectLeaves(
        hierarchy, budget, leaves, collectStats, reason);
    if (collectStatus != levelhierarchy12::CollectStatus::Complete)
    {
        Msg("ph_dx12_f41: load failed stage=collect status=%u nodes=%llu "
            "edges=%llu instances=%u reason='%s'",
            static_cast<unsigned>(collectStatus),
            static_cast<unsigned long long>(collectStats.nodeVisits),
            static_cast<unsigned long long>(collectStats.edgeVisits),
            collectStats.instances, reason.c_str());
        return;
    }
    Msg("ph_dx12_f41: hierarchy sectors=%zu visuals=%zu leaves=%zu "
        "node_visits=%llu edge_visits=%llu",
        hierarchy.sectors.size(), hierarchy.visuals.size(), leaves.size(),
        static_cast<unsigned long long>(collectStats.nodeVisits),
        static_cast<unsigned long long>(collectStats.edgeVisits));

    // F4.1: гистограмма типов и типы корней секторов. Нужна, чтобы дыры в
    // геометрии объяснялись фактами (какие типы пропускает leaf-обход), а не
    // догадками.
    {
        uint32_t typeCounts[16] = {};
        size_t maxType = 0;
        for (const auto& node : hierarchy.visuals)
        {
            if (node.type < 16)
                ++typeCounts[node.type];
            maxType = (std::max)(maxType, static_cast<size_t>(node.type));
        }
        char histogram[256] = {};
        size_t used = 0;
        for (size_t type = 0; type <= (std::min)(maxType, size_t{15}); ++type)
        {
            if (typeCounts[type] == 0)
                continue;
            const int written = std::snprintf(histogram + used,
                                              sizeof(histogram) - used,
                                              "type%zu=%u ", type,
                                              typeCounts[type]);
            if (written <= 0 || static_cast<size_t>(written) >= sizeof(histogram) - used)
                break;
            used += static_cast<size_t>(written);
        }
        uint32_t rootTypes[16] = {};
        for (const auto& sector : hierarchy.sectors)
        {
            if (sector.rootVisualId < hierarchy.visuals.size())
            {
                const uint8_t type = hierarchy.visuals[sector.rootVisualId].type;
                if (type < 16)
                    ++rootTypes[type];
            }
        }
        Msg("ph_dx12_f41: visual_types %s", histogram);
        Msg("ph_dx12_f41: sector_root_types normal=%u hierarchy=%u "
            "progressive=%u lod=%u tree=%u other=%u",
            rootTypes[0], rootTypes[1], rootTypes[2], rootTypes[6], rootTypes[7],
            rootTypes[3] + rootTypes[4] + rootTypes[5] + rootTypes[8] +
                rootTypes[9] + rootTypes[10] + rootTypes[11] + rootTypes[12]);
    }

    xraylevel12::Limits readLimits{};
    readLimits.maxLevelBytes = snapshotLimits.maxLevelBytes;
    readLimits.maxGeomBytes = snapshotLimits.maxGeomBytes;
    xraylevel12::LevelPools pools;
    const xraylevel12::ReadError readError = xraylevel12::ReadLevelPools(
        snapshot.levelBytes, snapshot.geomBytes, pools, reason, readLimits);
    if (readError != xraylevel12::ReadError::None)
    {
        Msg("ph_dx12_f41: load failed stage=pools error=%u reason='%s'",
            static_cast<unsigned>(readError), reason.c_str());
        return;
    }
    GameDx12StaticScene12_LogDeclarationInventory(pools);

    // F4.1-E: сопоставление загруженного R4 и поддержанного DX12 по id.
    LogVisualComparison(pools, hierarchy);

    // Мировые границы загружаемой статики: нужны и для отчёта, и для выбора
    // позиций доказательных кадров.
    {
        float minimum[3] = {1e30f, 1e30f, 1e30f};
        float maximum[3] = {-1e30f, -1e30f, -1e30f};
        uint32_t counted = 0;
        double totalRadius = 0.0;
        for (const auto& visual : pools.visuals)
        {
            if (!visual.drawable)
                continue;
            ++counted;
            totalRadius += visual.sphere_radius;
            for (int c = 0; c < 3; ++c)
            {
                const float low = visual.sphere_center[c] - visual.sphere_radius;
                const float high = visual.sphere_center[c] + visual.sphere_radius;
                if (low < minimum[c])
                    minimum[c] = low;
                if (high > maximum[c])
                    maximum[c] = high;
            }
        }
        Msg("ph_dx12_f41: bounds min=(%.1f,%.1f,%.1f) max=(%.1f,%.1f,%.1f) "
            "size=(%.1f,%.1f,%.1f) visuals=%u avg_radius=%.3f",
            minimum[0], minimum[1], minimum[2], maximum[0], maximum[1],
            maximum[2], maximum[0] - minimum[0], maximum[1] - minimum[1],
            maximum[2] - minimum[2], counted,
            counted != 0 ? totalRadius / counted : 0.0);
    }

    // R4 рисует статику через секторы, поэтому недостижимый visual не должен
    // попадать в кадр. Обход собственный: levelhierarchy12::CollectLeaves
    // эмитит только MT_NORMAL (это корректно для старого среза), а полная
    // карта содержит ещё и drawable MT_PROGRESSIVE с SWI-окнами.
    //
    // MT_TREE_ST/MT_TREE_PM намеренно не раскрываются: они рисуются только
    // через per-instance tree_data, которого в этом этапе нет. Иначе их
    // дочерняя геометрия попала бы в мир без инстансных трансформаций.
    {
        std::unordered_map<uint32_t, size_t> byId;
        byId.reserve(hierarchy.visuals.size() * 2);
        for (size_t index = 0; index < hierarchy.visuals.size(); ++index)
            byId.emplace(hierarchy.visuals[index].id, index);

        std::vector<uint8_t> reachable(hierarchy.visuals.size(), 0);
        uint32_t reachableNormal = 0;
        uint32_t reachableProgressive = 0;
        uint32_t reachableTree = 0;
        uint32_t lodNodes = 0;
        uint32_t lodLadderNodes = 0;
        uint32_t lodSpreadNodes = 0;
        uint32_t skippedTree = 0;
        uint32_t skippedType = 0;
        uint32_t cycleGuard = 0;

        struct Frame12
        {
            uint32_t visualIndex = 0;
            uint32_t depth = 0;
        };
        std::vector<Frame12> stack;
        stack.reserve(4096);
        for (const auto& sector : hierarchy.sectors)
        {
            const auto root = byId.find(sector.rootVisualId);
            if (root != byId.end())
                stack.push_back({static_cast<uint32_t>(root->second), 0});
        }
        while (!stack.empty())
        {
            const Frame12 current = stack.back();
            stack.pop_back();
            if (current.visualIndex >= hierarchy.visuals.size() ||
                current.depth > levelhierarchy12::kMaxDepth)
                continue;
            if (++cycleGuard > 4000000u)
                break;
            const auto& node = hierarchy.visuals[current.visualIndex];
            if (reachable[current.visualIndex] != 0)
                continue;
            reachable[current.visualIndex] = 1;

            const bool isTree = node.type == 7 || node.type == 11;
            const bool drawable =
                node.type == levelhierarchy12::kMtNormal || node.type == 2u ||
                isTree;
            if (drawable)
            {
                if (node.type == levelhierarchy12::kMtNormal)
                    ++reachableNormal;
                else if (isTree)
                    ++reachableTree;
                else
                    ++reachableProgressive;
                continue;
            }
            if (isTree)
            {
                ++skippedTree;
                continue;
            }
            if (node.type != levelhierarchy12::kMtHierarchy &&
                node.type != 6) // MT_HIERARHY / MT_LOD
            {
                ++skippedType;
                continue;
            }
            if (node.type == 6)
            {
                // R4 для крупного объекта добавляет ВСЕХ детей FLOD
                // (r__dsgraph_build.cpp:552-557), а импостор рисует отдельно.
                // Но если у детей совпадает центр и отличаются размеры — это
                // лестница детализации одного объекта, и рисовать надо один
                // уровень. Решение принимается по данным уровня, а не по
                // допущению.
                ++lodNodes;
                float parentCenter[3] = {
                    (node.bounds.minimum[0] + node.bounds.maximum[0]) * 0.5f,
                    (node.bounds.minimum[1] + node.bounds.maximum[1]) * 0.5f,
                    (node.bounds.minimum[2] + node.bounds.maximum[2]) * 0.5f};
                const float parentSize =
                    (std::max)(node.bounds.maximum[0] - node.bounds.minimum[0],
                               (std::max)(node.bounds.maximum[1] - node.bounds.minimum[1],
                                          node.bounds.maximum[2] - node.bounds.minimum[2]));
                bool ladder = node.children.size() > 1;
                if (ladder && parentSize > 0.0f)
                {
                    const float tolerance = parentSize * 0.02f;
                    for (const uint32_t childId : node.children)
                    {
                        const auto child = byId.find(childId);
                        if (child == byId.end())
                            continue;
                        const auto& childBounds =
                            hierarchy.visuals[child->second].bounds;
                        const float childCenter[3] = {
                            (childBounds.minimum[0] + childBounds.maximum[0]) * 0.5f,
                            (childBounds.minimum[1] + childBounds.maximum[1]) * 0.5f,
                            (childBounds.minimum[2] + childBounds.maximum[2]) * 0.5f};
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            if (std::fabs(childCenter[axis] - parentCenter[axis]) >
                                tolerance)
                            {
                                ladder = false;
                                break;
                            }
                        }
                        if (!ladder)
                            break;
                    }
                }
                if (ladder)
                {
                    ++lodLadderNodes;
                }
                else
                {
                    ++lodSpreadNodes;
                }
                // R4 добавляет ВСЕХ детей FLOD в ближней зоне
                // (r__dsgraph_build.cpp:552-557) и не выбирает ребёнка по
                // индексу. Повторяем это поведение, чтобы покрытие совпадало;
                // счётчики выше остаются диагностикой семантики LOD.
                for (const uint32_t childId : node.children)
                {
                    const auto child = byId.find(childId);
                    if (child != byId.end())
                        stack.push_back({static_cast<uint32_t>(child->second),
                                         current.depth + 1});
                }
                continue;
            }
            for (const uint32_t childId : node.children)
            {
                const auto child = byId.find(childId);
                if (child != byId.end())
                    stack.push_back(
                        {static_cast<uint32_t>(child->second), current.depth + 1});
            }
        }

        uint32_t referenced = 0;
        uint32_t dropped = 0;
        uint32_t loggedDropped = 0;
        for (size_t index = 0; index < pools.visuals.size(); ++index)
        {
            const auto& visual = pools.visuals[index];
            const auto found = byId.find(visual.id);
            const bool inTree =
                found != byId.end() && reachable[found->second] != 0;
            if (!visual.drawable)
                continue;
            if (inTree)
            {
                ++referenced;
                continue;
            }
            if (loggedDropped < 12)
            {
                ++loggedDropped;
                Msg("ph_dx12_f41: dropped id=%u type=%u vcount=%u icount=%u "
                    "c=(%.1f,%.1f,%.1f) r=%.2f",
                    visual.id, static_cast<unsigned>(visual.type),
                    visual.vcount, visual.icount, visual.sphere_center[0],
                    visual.sphere_center[1], visual.sphere_center[2],
                    visual.sphere_radius);
            }
            pools.visuals[index].drawable = false;
            pools.visuals[index].reject = xraylevel12::ReadError::VisualNotFound;
            --pools.visuals_drawable;
            ++pools.visuals_rejected;
            ++dropped;
        }
        Msg("ph_dx12_f41: reachability normal=%u progressive=%u tree=%u "
            "referenced=%u dropped_unreachable=%u skipped_tree_nodes=%u "
            "skipped_other_types=%u lod_nodes=%u lod_ladder=%u lod_spread=%u",
            reachableNormal, reachableProgressive, reachableTree, referenced,
            dropped, skippedTree, skippedType, lodNodes, lodLadderNodes,
            lodSpreadNodes);
    }

    std::vector<std::string> textureNames;
    std::vector<GameDx12StaticMaterial12> materials;
    GameDx12StaticScene12_ResolveMaterials(snapshot.levelBytes, pools,
                                           textureNames, materials);
    if ((gamewater12::WaterEnabled() || gamewater12::PuddlesEnabled()) &&
        std::find(textureNames.begin(), textureNames.end(), gamewater12::WaterNormal) == textureNames.end())
        textureNames.emplace_back(gamewater12::WaterNormal);
    std::vector<GameDx12SceneTexture12> textures;
    GameDx12TextureReadStats12 textureStats;
    GameDx12J2ABatch_ReadTextureNames(textureNames, textures, textureStats,
                                      2ull * 1024ull * 1024ull * 1024ull);
    Msg("ph_dx12_f41: textures unique=%u decoded=%u missing=%u unsupported=%u "
        "bytes=%llu",
        textureStats.requested, textureStats.decoded, textureStats.missing,
        textureStats.unsupported,
        static_cast<unsigned long long>(textureStats.bytes));

    GameDx12StaticSceneOwner12 owner;
    owner.device = state.device.Get();
    owner.queue = state.queue.Get();
    owner.fence = state.fence.Get();
    owner.fenceEvent = state.fenceEvent;
    owner.allocator = state.allocator.Get();
    owner.list = state.list.Get();
    owner.fenceValue = &state.fenceValue;

    std::string loadReason;
    if (!state.staticScene.Load(owner, state.levelName, state.levelSession,
                                state.captureEpoch, pools,
                                std::span<const uint8_t>(snapshot.geomBytes),
                                textures, materials, loadReason))
    {
        Msg("ph_dx12_f41: load failed stage=gpu reason='%s'",
            loadReason.c_str());
        return;
    }

    if (gamewater12::PuddlesEnabled())
        state.staticScene.SetPuddles(gamewater12::CaptureLevelPuddles());

    state.staticSceneBound = true;
    state.staticSceneSession = state.levelSession;
    state.staticSceneEpoch = state.captureEpoch;
    const auto& stats = state.staticScene.Stats();
    Msg("ph_dx12_f43: materials overrides=%u fallback_opaque=%u missing_srv=%u unsupported=%u",
        stats.material_overrides, stats.draws_without_texture, stats.material_missing_srv,
        stats.unsupported_materials);
    Msg("ph_dx12_f41: ready level='%s' session=%llu epoch=%llu "
        "sources=%u draws_potential=%u total_ms=%.3f",
        state.levelName, static_cast<unsigned long long>(state.staticSceneSession),
        static_cast<unsigned long long>(state.staticSceneEpoch),
        state.staticScene.SourceCount(), stats.visuals_drawable,
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - totalStart).count());
}

void GameDx12Bridge_OnLevelReady(const char* levelName)
{
    if (!EnsureOwnerThread("level_ready"))
        return;
    BridgeStateData& state = State();
    if (state.state == BridgeState::Shutdown ||
        state.state == BridgeState::ShuttingDown)
        return;
    if (!RuntimeRequested())
    {
        if (state.state == BridgeState::Uninitialized)
            state.state = BridgeState::Disabled;
        if (!state.disabledLogged)
        {
            state.disabledLogged = true;
            Msg("ph_dx12_bridge: disabled: runtime flag absent");
        }
        return;
    }
    if (!EnsureInitialized())
        return;
    if (state.levelActive)
    {
        Msg("ph_dx12_bridge: duplicate level ready ignored");
        return;
    }
    if (ForceOverflow("session"))
        state.levelSession = std::numeric_limits<uint64_t>::max();
    if (ForceOverflow("epoch"))
        state.captureEpoch = std::numeric_limits<uint64_t>::max();
    if (state.levelSession == std::numeric_limits<uint64_t>::max() ||
        state.captureEpoch == std::numeric_limits<uint64_t>::max())
    {
        DisableTerminal("session/epoch overflow at level ready");
        return;
    }
    ++state.levelSession;
    ++state.captureEpoch;
    state.captureSequence = 0;
    state.capturedFrames = 0;
    state.rejectedFrames = 0;
    state.cameraLogged = false;
    state.nativeGameplayStarted = false;
    state.nativeScenePresents = 0;
    state.previousNativePresent = {};
    state.nativePerf = {};
    state.levelActive = true;
    std::snprintf(state.levelName, sizeof(state.levelName), "%s",
                  levelName && levelName[0] ? levelName : "?");
    Msg("ph_dx12_bridge: level ready level='%s' session=%llu epoch=%llu",
        state.levelName,
        static_cast<unsigned long long>(state.levelSession),
        static_cast<unsigned long long>(state.captureEpoch));

    // F4.1: обычный native runtime загружает ВСЮ статическую сцену уровня.
    // Диагностические списки (M23/J1) остаются отдельными opt-in путями.
    state.staticSceneRequested = state.nativeRequested && StaticSceneRequested();
    state.staticSceneAttempted = false;
    state.staticSceneBound = false;
    state.staticSceneSession = 0;
    state.staticSceneEpoch = 0;
    state.staticSceneDraws = 0;
    if (state.staticSceneRequested)
    {
        if (!state.staticSceneEnabledLogged)
        {
            state.staticSceneEnabledLogged = true;
            Msg("ph_dx12_f41: full static scene enabled");
        }
        state.staticSceneAttempted = true;
        try
        {
            LoadFullStaticScene(state);
        }
        catch (...)
        {
            Msg("ph_dx12_f41: load failed stage=exception");
        }
        if (!state.gpuUnknown && !state.quarantined &&
            state.staticScene.GpuCompletionUnknown())
            QuarantineForGeometryUnknown("f41_static_scene");
    }
    else if (state.nativeRequested && !state.staticSceneEnabledLogged)
    {
        state.staticSceneEnabledLogged = true;
        Msg("ph_dx12_f41: full static scene disabled by "
            "OGSR_DX12_GAME_STATIC_SCENE=0");
    }

#ifdef OGSR_J2B_M1_RUNTIME_PROBE
    RunJ2BBatchRuntimeProbe(state.levelName, state.levelSession,
                            state.captureEpoch);
#endif

    // J1.2A diagnostic load path. R2A requires an explicit game-level
    // selection so visual ID 9 cannot accidentally run on Zaton. The loaded
    // geometry is bound to the current level name/session/epoch and is never
    // reused after a level transition.
    state.diagnosticLevelBound = false;
    state.diagnosticLevelSession = 0;
    state.diagnosticLevelEpoch = 0;
    state.diagnosticLevelMismatchLogged = false;
    state.diagnosticLevelName[0] = '\0';

    state.m23Bound = false;
    state.m23Attempted = false;
    state.m23LevelSession = 0;
    state.m23LevelEpoch = 0;
    state.m23LevelMismatchLogged = false;
    state.m23LevelName[0] = '\0';

    if (M23Requested())
    {
        if (!state.m23EnabledLogged)
        {
            state.m23EnabledLogged = true;
            Msg("ph_dx12_m23: enabled visuals=9,105,106,107");
        }
        state.m23FrameDelay = 0;
        state.m23FrameSkip = 0;
        state.m23DelayLogged = false;
        const char* delayValue =
            std::getenv("OGSR_DX12_GAME_M23_FRAME_DELAY");
        if (delayValue != nullptr && delayValue[0] != '\0')
        {
            char* delayEnd = nullptr;
            const unsigned long parsedDelay =
                std::strtoul(delayValue, &delayEnd, 10);
            if (delayEnd != delayValue && *delayEnd == '\0' &&
                parsedDelay <= 100000ul)
                state.m23FrameDelay = static_cast<uint32_t>(parsedDelay);
            else
                Msg("ph_dx12_m23: invalid OGSR_DX12_GAME_M23_FRAME_DELAY");
        }
        const char* requestedLevel = state.nativeRequested
            ? "jupiter" : std::getenv("OGSR_DX12_GAME_DIAGNOSTIC_LEVEL");
        if (requestedLevel == nullptr || requestedLevel[0] == '\0')
        {
            Msg("ph_dx12_m23: skipped: OGSR_DX12_GAME_DIAGNOSTIC_LEVEL is "
                "required");
        }
        else if (std::strlen(requestedLevel) >=
                 sizeof(state.m23LevelName))
        {
            Msg("ph_dx12_m23: requested level name too long");
        }
        else
        {
            std::snprintf(state.m23LevelName, sizeof(state.m23LevelName),
                          "%s", requestedLevel);
            if (_stricmp(state.m23LevelName, state.levelName) != 0)
            {
                Msg("ph_dx12_m23: skipped: level='%s' requested='%s'",
                    state.levelName, state.m23LevelName);
            }
            else
            {
                GameDx12FourVisualOwner12 owner;
                owner.device = state.device.Get();
                owner.queue = state.queue.Get();
                owner.fence = state.fence.Get();
                owner.fenceEvent = state.fenceEvent;
                owner.allocator = state.allocator.Get();
                owner.list = state.list.Get();
                owner.fenceValue = &state.fenceValue;
                constexpr uint32_t jupiterSector = 129;
                constexpr uint32_t jupiterRootVisual = 17431;
                std::array<uint32_t, j2bbatch12::kTargetVisualCount> visuals{};
                for (size_t slot = 0; slot < visuals.size(); ++slot)
                    visuals[slot] = fourvisual12::kPlan[slot].visualId;

                const j2bbatch12::Identity12 expected{
                    state.levelSession, state.captureEpoch};
                std::string reason;
                j2bbatch12::ErrorInfo12 cpuError;
                const bool world = B1WorldRequested();
                bool loaded = false;
                const j2bbatch12::Limits12 limits{};
                if (world)
                {
                    uint32_t sector = jupiterSector;
#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE
                    const char* alphaProbe = std::getenv("OGSR_DX12_GAME_B2_ALPHA_PROBE");
                    if (state.nativeRequested && alphaProbe && std::strcmp(alphaProbe, "1") == 0)
                        sector = 130;
#endif
                    j2bbatch12::Snapshot12 snapshot;
                    j2bbatch12::SceneBatch12 scene;
                    const bool read = GameDx12J2ABatch_ReadSnapshot(
                        state.levelName, expected, snapshot, limits, cpuError);
                    const bool prepared = read && j2bbatch12::PrepareSceneSector(
                        snapshot, expected, sector, scene, limits);
                    if (read && !prepared)
                        cpuError = scene.error;
                    if (prepared)
                    {
                        const char* materialMode = std::getenv("OGSR_DX12_GAME_B2_TEXTURES");
                        const bool textured = state.nativeRequested ||
                            (materialMode != nullptr && std::strcmp(materialMode, "1") == 0);
                        std::vector<GameDx12SceneTexture12> textures;
                        if (textured)
                            GameDx12J2ABatch_ReadTextures(scene, textures);
                        loaded = state.m23.LoadScene(
                            owner, state.levelName, {state.levelSession, state.captureEpoch},
                            std::move(scene), textured ? &textures : nullptr, reason);
                    }
                }
                else
                {
                    j2bbatch12::BatchResult12 batch;
                    if (!GameDx12J2ABatch_PrepareFromVfs(
                            state.levelName, expected, jupiterSector, jupiterRootVisual,
                            visuals, nullptr, false, batch, limits))
                        cpuError = batch.error;
                    else
                        loaded = state.m23.LoadFromBatch(
                            owner, state.levelName, {state.levelSession, state.captureEpoch},
                            std::move(batch), reason);
                }
                if (!loaded)
                {
                    const char* failure = cpuError.code != j2bbatch12::Error12::None
                        ? cpuError.text : reason.c_str();
                    Msg("ph_dx12_m23: load failed reason='%s'",
                        failure[0] != '\0' ? failure : "allocation or unknown failure");
                    if (state.m23.ListPoisoned())
                        QuarantineForGeometryUnknown(
                            "m23_upload_list_poisoned");
                }
                else
                {
                    state.m23Bound = true;
                    state.m23LevelSession = state.levelSession;
                    state.m23LevelEpoch = state.captureEpoch;
                    Msg("ph_dx12_m23: bound level='%s' session=%llu "
                        "epoch=%llu mode=%s",
                        state.m23LevelName,
                        static_cast<unsigned long long>(
                            state.m23LevelSession),
                        static_cast<unsigned long long>(
                            state.m23LevelEpoch),
                        world ? "world" : "four-visual");
                }
                if (!state.gpuUnknown && !state.quarantined &&
                    state.m23.GpuCompletionUnknown())
                    QuarantineForGeometryUnknown("m23_level_ready");
            }
        }
    }

    // J1.2A diagnostic load path. R2A requires an explicit game-level
    // selection so visual ID 9 cannot accidentally run on Zaton. The loaded
    // geometry is bound to the current level name/session/epoch and is never
    // reused after a level transition. M23 owns the frame when requested.
    if (M23Requested())
    {
        state.diagnosticLevelBound = false;
        state.diagnosticLevelName[0] = '\0';
    }
    else
    {

    const char* geometryVisual =
        std::getenv("OGSR_DX12_GAME_GEOMETRY_VISUAL");
    if (geometryVisual != nullptr && geometryVisual[0] != '\0')
    {
        const char* requestedLevel =
            std::getenv("OGSR_DX12_GAME_DIAGNOSTIC_LEVEL");
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(geometryVisual, &end, 10);
        if (end == geometryVisual || *end != '\0' ||
            parsed > 0xFFFFul)
        {
            Msg("ph_dx12_geometry: invalid OGSR_DX12_GAME_GEOMETRY_VISUAL");
        }
        else if (requestedLevel == nullptr || requestedLevel[0] == '\0')
        {
            Msg("ph_dx12_geometry: diagnostic skipped: "
                "OGSR_DX12_GAME_DIAGNOSTIC_LEVEL is required");
        }
        else if (std::strlen(requestedLevel) >=
                 sizeof(state.diagnosticLevelName))
        {
            Msg("ph_dx12_geometry: diagnostic level name too long");
        }
        else
        {
            std::snprintf(state.diagnosticLevelName,
                          sizeof(state.diagnosticLevelName), "%s",
                          requestedLevel);
            if (_stricmp(state.diagnosticLevelName, state.levelName) != 0)
            {
                Msg("ph_dx12_geometry: diagnostic skipped: level='%s' "
                    "requested='%s'",
                    state.levelName, state.diagnosticLevelName);
            }
            else
            {
                GameDx12LevelGeometryOwner12 owner;
                owner.device = state.device.Get();
                owner.queue = state.queue.Get();
                owner.fence = state.fence.Get();
                owner.fenceEvent = state.fenceEvent;
                owner.allocator = state.allocator.Get();
                owner.list = state.list.Get();
                owner.fenceValue = &state.fenceValue;
                std::string reason;
                const bool loaded = state.geometry.LoadFromVfs(
                    owner, state.levelName, static_cast<uint32_t>(parsed),
                    state.levelSession, state.captureEpoch, reason);
                if (!loaded)
                {
                    Msg("ph_dx12_geometry: load failed reason='%s'",
                        reason.empty() ? "allocation or unknown failure"
                                       : reason.c_str());
                }
                else
                {
                    state.diagnosticLevelSession = state.levelSession;
                    state.diagnosticLevelEpoch = state.captureEpoch;
                    state.diagnosticLevelBound = true;
                    Msg("ph_dx12_geometry: diagnostic bound level='%s' "
                        "session=%llu epoch=%llu visual=%u",
                        state.diagnosticLevelName,
                        static_cast<unsigned long long>(
                            state.diagnosticLevelSession),
                        static_cast<unsigned long long>(
                            state.diagnosticLevelEpoch),
                        static_cast<uint32_t>(parsed));
                }
                if (state.geometry.GpuCompletionUnknown())
                    QuarantineForGeometryUnknown("level_ready");
            }
        }
    }

    } // M23 / J1 diagnostic selection

    // F4.0-J2A opt-in CPU preparation. It runs once on the first validated
    // frame after level ready, when the real game camera is available. The
    // first prototype is intentionally limited to Jupiter.
    state.j2aPending = GameDx12J2A_IsEnabled() &&
                       _stricmp(state.levelName, "jupiter") == 0;
    state.j2aAttempted = false;
    if (state.j2aPending && !state.j2aEnabledLogged)
    {
        state.j2aEnabledLogged = true;
        Msg("ph_dx12_j2a: enabled");
    }
}

void GameDx12Bridge_OnLevelTeardown()
{
    if (!EnsureOwnerThread("level_teardown"))
        return;
    BridgeStateData& state = State();
    if (!state.levelActive)
        return;
    if (state.nativeRequested && !DrainNative(state, "native_level_teardown"))
        return;
    state.levelActive = false;
    state.diagnosticLevelBound = false;
    state.m23Bound = false;
    state.m23Attempted = false;
    state.j2aPending = false;
    state.j2aAttempted = false;
    if (ForceOverflow("teardown-epoch"))
        state.captureEpoch = std::numeric_limits<uint64_t>::max();
    if (state.captureEpoch == std::numeric_limits<uint64_t>::max())
    {
        DisableTerminal("epoch overflow at level teardown");
        return;
    }
    ++state.captureEpoch;
    state.geometry.OnLevelTeardown(state.captureEpoch);
    if (state.geometry.GpuCompletionUnknown())
        QuarantineForGeometryUnknown("level_teardown");
    state.m23.OnLevelTeardown(state.captureEpoch);
    if (state.m23.GpuCompletionUnknown())
        QuarantineForGeometryUnknown("m23_level_teardown");
    state.staticSceneBound = false;
    state.staticSceneAttempted = false;
    state.staticSceneSession = 0;
    state.staticSceneEpoch = 0;
    state.staticSceneDraws = 0;
    state.gameVisuals.Reset();
    state.staticScene.OnLevelTeardown(state.captureEpoch);
    if (state.staticScene.GpuCompletionUnknown())
        QuarantineForGeometryUnknown("f41_level_teardown");
    Msg("ph_dx12_bridge: level teardown level='%s' session=%llu epoch=%llu",
        state.levelName,
        static_cast<unsigned long long>(state.levelSession),
        static_cast<unsigned long long>(state.captureEpoch));
}

void GameDx12Bridge_OnFrame()
{
    if (!EnsureOwnerThread("frame"))
        return;
    BridgeStateData& state = State();
    if (state.state != BridgeState::Ready || !state.levelActive)
        return;
    if (!Device.b_is_Ready || !Device.b_is_Active ||
        Device.dwPrecacheFrame != 0 || !g_loading_events.empty())
        return;
    if (!g_pGameLevel || !g_pGameLevel->bReady)
        return;
    if (Device.dwWidth == 0 || Device.dwHeight == 0)
        return;
    if (state.nativeRequested && !state.nativeGameplayStarted)
    {
        state.nativeGameplayStarted = true;
        // В этом preview меню R4 не выводится. Startup -start может
        // оставить его активным после загрузки и убрать уровень из seqFrame.
        if (IsMainMenuActive())
        {
            Msg("ph_dx12_native: releasing startup menu after level load");
            Console->Execute("main_menu off");
            Console->Hide();
        }
    }
    if (state.lastGameFrame == Device.dwFrame)
        return;

    state.lastGameFrame = Device.dwFrame;
    state.width = Device.dwWidth;
    state.height = Device.dwHeight;

    if (ForceOverflow("sequence"))
        state.captureSequence = std::numeric_limits<uint64_t>::max();
    if (state.captureSequence == std::numeric_limits<uint64_t>::max())
    {
        DisableTerminal("capture sequence overflow");
        return;
    }
    ++state.captureSequence;

    Matrix4 view{};
    Matrix4 projection{};
    Matrix4 fullTransform{};
    CopyMatrix(Device.mView, view);
    CopyMatrix(Device.mProject, projection);
    CopyMatrix(Device.mFullTransform, fullTransform);

    if (!FiniteVector(Device.vCameraPosition) ||
        !ValidView(view) || !ValidProjection(projection) ||
        !FiniteMatrix(fullTransform) ||
        UninitializedCamera(view, Device.vCameraPosition))
    {
        ++state.rejectedFrames;
        if (state.rejectedFrames == 1 ||
            (state.rejectedFrames % 60) == 0)
        {
            Msg("ph_dx12_bridge: invalid camera rejected count=%llu "
                "frame=%u position=(%.3f,%.3f,%.3f) view_det=%.9g "
                "proj_23=%.9g proj_33=%.9g",
                static_cast<unsigned long long>(state.rejectedFrames),
                Device.dwFrame, Device.vCameraPosition.x,
                Device.vCameraPosition.y, Device.vCameraPosition.z,
                view.m[0][0] * (view.m[1][1] * view.m[2][2] -
                                view.m[1][2] * view.m[2][1]) -
                    view.m[0][1] * (view.m[1][0] * view.m[2][2] -
                                    view.m[1][2] * view.m[2][0]) +
                    view.m[0][2] * (view.m[1][0] * view.m[2][1] -
                                    view.m[1][1] * view.m[2][0]),
                projection.m[2][3], projection.m[3][3]);
            if (MatrixBitsRequested())
                LogCameraBits(view, projection, fullTransform);
        }
        return;
    }

    const uint64_t hash = HashMatrix(view, projection, fullTransform);
    state.lastMatrixHash = hash;
    if (state.nativeRequested)
    {
        RenderNativeFrame(state, fullTransform);

    // F4.1-E: мир уже заспавнен (несколько кадров прошло), снимаем инвентарь
    // динамики один раз на сессию уровня.
    if (!state.dynamicInventoryLogged && state.nativeScenePresents >= 30)
    {
        state.dynamicInventoryLogged = true;
        try
        {
            LogDynamicObjectInventory();
        }
        catch (...)
        {
            Msg("ph_cmp: dyn summary objects=unavailable");
        }
    }
        return;
    }
    ++state.capturedFrames;
    if (MatrixBitsRequested())
        LogCameraBits(view, projection, fullTransform);
    if (state.capturedFrames == 1 || (state.capturedFrames % 60) == 0)
        LogCamera(view, projection, fullTransform, hash);

    // F4.0-J2A opt-in CPU-only preparation: one bounded immutable snapshot per
    // level session, using the already validated real game camera.
    if (state.j2aPending && !state.j2aAttempted)
    {
        state.j2aAttempted = true;
        state.j2aPending = false;
        DirectX::XMFLOAT4X4 j2aView{};
        DirectX::XMFLOAT4X4 j2aProjection{};
        std::memcpy(&j2aView, Device.mView.m, sizeof(j2aView));
        std::memcpy(&j2aProjection, Device.mProject.m, sizeof(j2aProjection));
        GameDx12J2AResult12 j2aResult;
        try
        {
            (void)GameDx12J2A_RunForBridge(
                state.levelName, state.levelSession, state.captureEpoch,
                j2aView, j2aProjection, j2aResult);
        }
        catch (...)
        {
            // Defensive bridge boundary. GameDx12J2A_RunForBridge is noexcept;
            // keep OnFrame safe even if a future edit breaks that contract.
            j2aResult.ok = false;
            j2aResult.error = GameDx12J2AError12::Unknown;
            j2aResult.instances = 0;
            j2aResult.uniqueVisuals = 0;
            j2aResult.packets = 0;
            j2aResult.visiblePackets = 0;
            j2aResult.culledPackets = 0;
            j2aResult.batches = 0;
            j2aResult.lists = 0;
            j2aResult.stableHash = 0;
            j2aResult.serialMs = 0.0;
            j2aResult.parallelMs = 0.0;
            j2aResult.verifyMs = 0.0;
            try
            {
                Msg("ph_dx12_j2a: failure_detail sector=%u code=%u "
                    "reason='bridge boundary exception'",
                    j2aResult.sectorIndex,
                    static_cast<unsigned>(j2aResult.error));
            }
            catch (...)
            {
            }
        }
    }

    // F4.0-J2B-M23 owner-thread one-shot four-visual frame. It runs after the
    // same real-camera validation as J1, records four real indexed draws in
    // one list and then four isolated identity passes, and never repeats
    // automatically for the same one-shot attempt.
    if (M23Requested() && !state.m23Attempted && state.m23.IsLoaded())
    {
        const bool identityMatches =
            state.m23Bound && state.levelActive &&
            state.levelSession == state.m23LevelSession &&
            state.captureEpoch == state.m23LevelEpoch &&
            state.m23.LevelSession() == state.m23LevelSession &&
            state.m23.LevelEpoch() == state.m23LevelEpoch &&
            _stricmp(state.levelName, state.m23LevelName) == 0;
        if (!identityMatches)
        {
            if (!state.m23LevelMismatchLogged)
            {
                state.m23LevelMismatchLogged = true;
                Msg("ph_dx12_m23: frame blocked: level/session/epoch "
                    "mismatch current='%s' session=%llu epoch=%llu "
                    "bound='%s' session=%llu epoch=%llu",
                    state.levelName,
                    static_cast<unsigned long long>(state.levelSession),
                    static_cast<unsigned long long>(state.captureEpoch),
                    state.m23LevelName,
                    static_cast<unsigned long long>(
                        state.m23LevelSession),
                    static_cast<unsigned long long>(state.m23LevelEpoch));
            }
        }
        else
        {
            if (state.m23FrameSkip < state.m23FrameDelay)
            {
                ++state.m23FrameSkip;
                if (!state.m23DelayLogged)
                {
                    state.m23DelayLogged = true;
                    Msg("ph_dx12_m23: frame delayed frames=%u",
                        state.m23FrameDelay);
                }
                return;
            }
            state.m23Attempted = true;

            DirectX::XMFLOAT4X4 logicalViewProjection = {};            std::memcpy(&logicalViewProjection, fullTransform.m,
                        sizeof(logicalViewProjection));
            const float cameraPosition[3] = {
                Device.vCameraPosition.x, Device.vCameraPosition.y,
                Device.vCameraPosition.z};

            wchar_t pngDirectory[1024] = {};
            constexpr DWORD kPngDirectoryMax = 1024;
            const DWORD pngLength = GetEnvironmentVariableW(
                L"OGSR_DX12_GAME_M23_PNG_DIR", pngDirectory,
                kPngDirectoryMax);
            if (pngLength >= kPngDirectoryMax)
            {
                Msg("ph_dx12_m23: PNG directory too long "
                    "(required=%lu max=%u)",
                    static_cast<unsigned long>(pngLength),
                    static_cast<unsigned>(kPngDirectoryMax));
            }
            const wchar_t* png =
                pngLength > 0 && pngLength < kPngDirectoryMax
                    ? pngDirectory
                    : nullptr;

            GameDx12FourVisualOwner12 owner;
            owner.device = state.device.Get();
            owner.queue = state.queue.Get();
            owner.fence = state.fence.Get();
            owner.fenceEvent = state.fenceEvent;
            owner.allocator = state.allocator.Get();
            owner.list = state.list.Get();
            owner.fenceValue = &state.fenceValue;

            GameDx12FourVisual12::Result12 m23Result;
            std::string reason;
            const bool rendered = state.m23.RenderDiagnostic(
                owner, logicalViewProjection, cameraPosition,
                state.offscreen, state.width, state.height, png,
                &m23Result, reason);

            if (rendered)
            {
                Msg("ph_dx12_m23: image gate PASS draws=%u "
                    "session=%llu epoch=%llu combined_fence=%llu "
                    "last_draw_use_fence=%llu nonclear=%llu/%llu "
                    "png_saved=%d",
                    m23Result.combinedDrawCount,
                    static_cast<unsigned long long>(
                        state.levelSession),
                    static_cast<unsigned long long>(
                        state.captureEpoch),
                    static_cast<unsigned long long>(
                        m23Result.combinedFence),
                    static_cast<unsigned long long>(
                        m23Result.lastDrawUseFence),
                    static_cast<unsigned long long>(
                        m23Result.combinedCoverage.nonClearPixels),
                    static_cast<unsigned long long>(
                        m23Result.combinedCoverage.totalPixels),
                    m23Result.pngSaved ? 1 : 0);
            }
            else if (m23Result.gpuCompleted)
            {
                Msg("ph_dx12_m23: GPU completion confirmed; image/PNG gate "
                    "FAIL session=%llu epoch=%llu image_gate=%d "
                    "nonclear=%llu/%llu png_saved=%d reason='%s'",
                    static_cast<unsigned long long>(
                        state.levelSession),
                    static_cast<unsigned long long>(
                        state.captureEpoch),
                    m23Result.imageGatePass ? 1 : 0,
                    static_cast<unsigned long long>(
                        m23Result.combinedCoverage.nonClearPixels),
                    static_cast<unsigned long long>(
                        m23Result.combinedCoverage.totalPixels),
                    m23Result.pngSaved ? 1 : 0,
                    reason.empty() ? "unknown failure" : reason.c_str());
            }
            else
            {
                Msg("ph_dx12_m23: GPU completion NOT confirmed "
                    "session=%llu epoch=%llu reason='%s'",
                    static_cast<unsigned long long>(
                        state.levelSession),
                    static_cast<unsigned long long>(
                        state.captureEpoch),
                    reason.empty() ? "unknown failure" : reason.c_str());
            }
            EvaluateInfoQueue("m23");
            if (state.m23.ListPoisoned())
                QuarantineForGeometryUnknown("m23_draw_list_poisoned");
            else if (!rendered && state.m23.GpuCompletionUnknown())
                QuarantineForGeometryUnknown("m23_render");
        }
    }

    // F4.0-J1 owner-thread one-shot diagnostic caller. It runs only after the
    // existing camera checks above, uses the real game ViewProjection and the
    // authored static-geometry identity world, and never repeats automatically.
    if (!state.diagnosticAttempted && state.geometry.IsLoaded())
    {
        const bool identityMatches =
            state.diagnosticLevelBound && state.levelActive &&
            state.levelSession == state.diagnosticLevelSession &&
            state.captureEpoch == state.diagnosticLevelEpoch &&
            state.geometry.LevelSession() == state.diagnosticLevelSession &&
            state.geometry.LevelEpoch() == state.diagnosticLevelEpoch &&
            _stricmp(state.levelName, state.diagnosticLevelName) == 0;
        if (!identityMatches)
        {
            if (!state.diagnosticLevelMismatchLogged)
            {
                state.diagnosticLevelMismatchLogged = true;
                Msg("ph_dx12_geometry: diagnostic blocked: "
                    "level/session/epoch mismatch current='%s' "
                    "session=%llu epoch=%llu bound='%s' session=%llu "
                    "epoch=%llu",
                    state.levelName,
                    static_cast<unsigned long long>(state.levelSession),
                    static_cast<unsigned long long>(state.captureEpoch),
                    state.diagnosticLevelName,
                    static_cast<unsigned long long>(
                        state.diagnosticLevelSession),
                    static_cast<unsigned long long>(
                        state.diagnosticLevelEpoch));
            }
            return;
        }

        float minimum[3] = {};
        float maximum[3] = {};
        float center[3] = {};
        float radius = 0.0f;
        if (!state.geometry.GetBounds(minimum, maximum, center, radius))
            return;
        float planeDistances[6] = {};
        const bool visible = SphereInFrustum(fullTransform, center, radius,
                                             planeDistances);
        if (!state.diagnosticVisibilityLogged ||
            (!visible && (state.capturedFrames % 60) == 0))
        {
            state.diagnosticVisibilityLogged = true;
            Msg("ph_dx12_geometry: visibility visual=%u session=%llu "
                "epoch=%llu camera=(%.3f,%.3f,%.3f) "
                "min=(%.3f,%.3f,%.3f) max=(%.3f,%.3f,%.3f) "
                "center=(%.3f,%.3f,%.3f) radius=%.3f "
                "planes=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f] visible=%d",
                state.geometry.VisualId(),
                static_cast<unsigned long long>(state.levelSession),
                static_cast<unsigned long long>(state.captureEpoch),
                Device.vCameraPosition.x, Device.vCameraPosition.y,
                Device.vCameraPosition.z,
                minimum[0], minimum[1], minimum[2],
                maximum[0], maximum[1], maximum[2],
                center[0], center[1], center[2],
                radius, planeDistances[0], planeDistances[1],
                planeDistances[2], planeDistances[3], planeDistances[4],
                planeDistances[5], visible ? 1 : 0);
        }
        if (!visible)
            return;

        state.diagnosticAttempted = true;
        DirectX::XMFLOAT4X4 logicalViewProjection = {};
        std::memcpy(&logicalViewProjection, fullTransform.m,
                    sizeof(logicalViewProjection));
        const DirectX::XMFLOAT4X4 identityWorld = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};

        wchar_t pngPath[MAX_PATH] = {};
        const DWORD pngLength = GetEnvironmentVariableW(
            L"OGSR_DX12_GAME_DIAGNOSTIC_PNG", pngPath, MAX_PATH);
        if (pngLength >= MAX_PATH)
        {
            Msg("ph_dx12_geometry: J1 diagnostic aborted: PNG path too long "
                "(required=%lu max=%u)",
                static_cast<unsigned long>(pngLength),
                static_cast<unsigned>(MAX_PATH));
            EvaluateInfoQueue("diagnostic");
            return;
        }
        const wchar_t* png = pngLength > 0 ? pngPath : nullptr;
        std::string reason;
        GameDx12DiagnosticResult diagnostic;
        const bool rendered = GameDx12Bridge_RenderOffscreenDiagnostic(
            logicalViewProjection, identityWorld, png, reason, &diagnostic);
        if (rendered)
        {
            Msg("ph_dx12_geometry: J1 image gate PASS visual=%u "
                "session=%llu epoch=%llu nonclear=%llu/%llu "
                "bbox=(%u,%u)-(%u,%u) png_saved=%d",
                state.geometry.VisualId(),
                static_cast<unsigned long long>(state.levelSession),
                static_cast<unsigned long long>(state.captureEpoch),
                static_cast<unsigned long long>(
                    diagnostic.coverage.nonClearPixels),
                static_cast<unsigned long long>(
                    diagnostic.coverage.totalPixels),
                diagnostic.coverage.minX, diagnostic.coverage.minY,
                diagnostic.coverage.maxX, diagnostic.coverage.maxY,
                diagnostic.pngSaved ? 1 : 0);
        }
        else if (diagnostic.gpuCompleted)
        {
            Msg("ph_dx12_geometry: J1 GPU completion confirmed; "
                "image/PNG gate FAIL visual=%u session=%llu epoch=%llu "
                "image_gate=%d nonclear=%llu/%llu png_saved=%d "
                "reason='%s'",
                state.geometry.VisualId(),
                static_cast<unsigned long long>(state.levelSession),
                static_cast<unsigned long long>(state.captureEpoch),
                diagnostic.imageGatePass ? 1 : 0,
                static_cast<unsigned long long>(
                    diagnostic.coverage.nonClearPixels),
                static_cast<unsigned long long>(
                    diagnostic.coverage.totalPixels),
                diagnostic.pngSaved ? 1 : 0,
                reason.empty() ? "unknown failure" : reason.c_str());
        }
        else
        {
            Msg("ph_dx12_geometry: J1 GPU completion NOT confirmed "
                "visual=%u reason='%s'",
                state.geometry.VisualId(),
                reason.empty() ? "unknown failure" : reason.c_str());
        }
        EvaluateInfoQueue("diagnostic");
    }
}

bool GameDx12Bridge_RenderOffscreenDiagnostic(
    const DirectX::XMFLOAT4X4& logicalViewProjection,
    const DirectX::XMFLOAT4X4& logicalWorld,
    const wchar_t* pngPath,
    std::string& reason,
    GameDx12DiagnosticResult* result)
{
    GameDx12DiagnosticResult localResult;
    if (result == nullptr)
        result = &localResult;
    *result = {};

    if (!EnsureOwnerThread("offscreen_diagnostic"))
        return false;
    BridgeStateData& state = State();
    if (state.state != BridgeState::Ready || !state.levelActive ||
        state.device == nullptr || state.queue == nullptr ||
        state.fence == nullptr || state.fenceEvent == nullptr ||
        state.allocator == nullptr || state.list == nullptr)
    {
        reason = "bridge is not ready for offscreen diagnostic";
        return false;
    }
    if (!state.geometry.IsLoaded() ||
        state.geometry.GpuCompletionUnknown() ||
        state.geometry.IsDrawRecorded())
    {
        reason = "geometry is not reusable for offscreen diagnostic";
        return false;
    }
    if (state.width == 0 || state.height == 0)
    {
        reason = "game viewport is unavailable";
        return false;
    }
    if (state.fenceValue == std::numeric_limits<uint64_t>::max())
    {
        reason = "fence value overflow before diagnostic draw";
        return false;
    }

    if (state.offscreen.IsCreated() &&
        (state.offscreen.Width() != state.width ||
         state.offscreen.Height() != state.height))
    {
        if (state.geometry.GpuCompletionUnknown() ||
            state.geometry.IsDrawRecorded())
        {
            reason = "cannot resize offscreen target with pending GPU work";
            return false;
        }
        state.offscreen.Release();
    }
    if (!state.offscreen.IsCreated() &&
        !state.offscreen.Create(state.device.Get(), state.width, state.height,
                                reason))
        return false;

    if (!state.geometry.PrepareFrame(logicalViewProjection, logicalWorld,
                                     reason))
        return false;

    HRESULT hr = state.allocator->Reset();
    if (FAILED(hr))
    {
        reason = "diagnostic allocator reset failed";
        return false;
    }
    hr = state.list->Reset(state.allocator.Get(), nullptr);
    if (FAILED(hr))
    {
        reason = "diagnostic command list reset failed";
        return false;
    }

    const float* clearColor = kGameDx12DiagnosticClearColor;
    if (!state.offscreen.RecordBegin(state.list.Get(), clearColor, reason))
    {
        RecoverUnsubmittedDiagnosticList(state, "record-begin", reason);
        return false;
    }
    if (!state.geometry.RecordDraw(state.list.Get(), reason))
    {
        RecoverUnsubmittedDiagnosticList(state, "record-draw", reason);
        return false;
    }
    if (!state.offscreen.RecordEnd(state.list.Get(), reason))
    {
        RecoverUnsubmittedDiagnosticList(state, "record-end", reason);
        return false;
    }
    hr = state.list->Close();
    if (FAILED(hr))
    {
        AbortUnsubmittedDiagnosticState(state);
        DisableTerminal("diagnostic command list close failed");
        reason = "diagnostic command list close failed";
        return false;
    }

    const uint64_t expected = state.fenceValue + 1;
    ID3D12CommandList* lists[] = {state.list.Get()};
    state.queue->ExecuteCommandLists(1, lists);
    state.gpuWorkSubmitted = true;

    // Mark is intentionally after ExecuteCommandLists: from here on the work
    // may be in flight, so a failure must quarantine rather than abort.
    if (!state.geometry.MarkDrawSubmitted(expected, state.fence.Get(),
                                          state.device.Get(), reason))
    {
        QuarantineForGeometryUnknown("diagnostic-mark-submitted");
        return false;
    }
    state.fenceValue = expected;
    hr = state.queue->Signal(state.fence.Get(), expected);
    if (FAILED(hr))
    {
        QuarantineForGeometryUnknown("diagnostic-signal");
        reason = "diagnostic fence signal failed";
        return false;
    }
    if (state.fence->GetCompletedValue() < expected)
    {
        hr = state.fence->SetEventOnCompletion(expected, state.fenceEvent);
        if (FAILED(hr))
        {
            QuarantineForGeometryUnknown("diagnostic-fence-event");
            reason = "diagnostic fence event failed";
            return false;
        }
        if (WaitForSingleObject(state.fenceEvent, kServiceFenceTimeoutMs) !=
            WAIT_OBJECT_0)
        {
            QuarantineForGeometryUnknown("diagnostic-fence-wait");
            reason = "diagnostic fence wait failed";
            return false;
        }
    }
    const HRESULT removedReason = state.device->GetDeviceRemovedReason();
    if (state.fence->GetCompletedValue() < expected ||
        removedReason != S_OK)
    {
        QuarantineForGeometryUnknown("diagnostic-completion");
        reason = "diagnostic GPU completion unconfirmed";
        return false;
    }
    if (!state.geometry.OnDrawCompleted(expected, state.fence.Get(),
                                        state.device.Get(), reason))
    {
        QuarantineForGeometryUnknown("diagnostic-draw-completion");
        return false;
    }
    // Exact Draw fence and device status are confirmed here. Only now may the
    // offscreen pass forget its submitted frame before CPU readback.
    result->gpuCompleted = true;
    state.offscreen.CommitSubmittedFrame();
    if (!state.offscreen.Readback(reason))
        return false;
    result->coverage = state.offscreen.EvaluateCoverage();
    result->imageGatePass = result->coverage.HasVisiblePixels();
    const bool pngRequested = pngPath != nullptr;
    bool pngOk = true;
    if (pngRequested)
    {
        pngOk = state.offscreen.SavePng(pngPath, reason);
        result->pngSaved = pngOk;
    }

    Msg("ph_dx12_geometry: offscreen diagnostic submitted fence=%llu "
        "size=%ux%u visual=%u indices=%u instances=1 "
        "rt=R8G8B8A8_UNORM depth=D32_FLOAT readback=1 removed=0x%08X "
        "gpu_completed=1 image_gate=%d nonclear=%llu/%llu "
        "bbox=(%u,%u)-(%u,%u) png_requested=%d png_saved=%d",
        static_cast<unsigned long long>(expected), state.width, state.height,
        state.geometry.VisualId(), state.geometry.IndexCount(),
        static_cast<unsigned>(removedReason),
        result->imageGatePass ? 1 : 0,
        static_cast<unsigned long long>(result->coverage.nonClearPixels),
        static_cast<unsigned long long>(result->coverage.totalPixels),
        result->coverage.minX, result->coverage.minY,
        result->coverage.maxX, result->coverage.maxY,
        pngRequested ? 1 : 0, result->pngSaved ? 1 : 0);

    if (!result->imageGatePass)
    {
        if (reason.empty())
            reason = "diagnostic image gate failed: zero non-clear pixels";
        return false;
    }
    if (pngRequested && !pngOk)
        return false;
    return true;
}

void GameDx12Bridge_OnDeviceReset(uint32_t width, uint32_t height)
{
    if (!EnsureOwnerThread("device_reset"))
        return;
    BridgeStateData& state = State();
    if (state.state != BridgeState::Ready)
        return;
    if (state.nativeRequested && !DrainNative(state, "native_reset"))
        return;
    state.width = width;
    state.height = height;
    if (state.levelActive)
    {
        if (ForceOverflow("reset-epoch"))
            state.captureEpoch = std::numeric_limits<uint64_t>::max();
        if (state.captureEpoch == std::numeric_limits<uint64_t>::max())
        {
            DisableTerminal("epoch overflow at device reset");
            return;
        }
        ++state.captureEpoch;
        state.captureSequence = 0;
        state.geometry.OnDeviceReset(state.captureEpoch);
        if (state.geometry.GpuCompletionUnknown())
            QuarantineForGeometryUnknown("device_reset");
        state.m23.OnDeviceReset(state.captureEpoch);
        if (state.m23.GpuCompletionUnknown())
            QuarantineForGeometryUnknown("m23_device_reset");
        state.staticScene.OnDeviceReset(state.captureEpoch);
        state.gameVisuals.InvalidateFrame(state.captureEpoch);
        if (state.staticSceneBound)
            state.staticSceneEpoch = state.captureEpoch;
        if (state.staticScene.GpuCompletionUnknown())
            QuarantineForGeometryUnknown("f41_device_reset");
        if (M23Requested())
            RebindM23AfterDeviceReset("device_reset");
        else
            RebindDiagnosticAfterDeviceReset("device_reset");
        if (state.j2aPending)
        {
            state.j2aPending = false;
            state.j2aAttempted = true;
            Msg("ph_dx12_j2a: pending preparation invalidated by device reset");
        }
    }
    Msg("ph_dx12_bridge: device reset mode=%s width=%u height=%u epoch=%llu",
        state.nativeRequested ? "native" : "diagnostic", width, height,
        static_cast<unsigned long long>(state.captureEpoch));
}

void GameDx12Bridge_Shutdown()
{
    if (!EnsureOwnerThread("shutdown"))
        return;
    BridgeStateData& state = State();
    if (state.state == BridgeState::ShuttingDown ||
        state.state == BridgeState::Shutdown)
        return;
    if (state.state == BridgeState::Uninitialized)
    {
        state.state = BridgeState::Shutdown;
        state.objectsReleased = true;
        state.levelActive = false;
        Msg("ph_dx12_bridge: shutdown from uninitialized; terminal");
        return;
    }

    state.state = BridgeState::ShuttingDown;
    if (state.nativeRequested)
        DrainNative(state, "native_shutdown");
    const bool ownerCompletionKnown = !state.gpuUnknown && !state.quarantined;
    state.staticSceneBound = false;
    state.gameVisuals.Reset();
    state.staticScene.Shutdown(ownerCompletionKnown);
    if (state.staticScene.GpuCompletionUnknown())
        QuarantineForGeometryUnknown("f41_shutdown");
    state.m23.Shutdown(ownerCompletionKnown);
    if (state.m23.GpuCompletionUnknown())
        QuarantineForGeometryUnknown("m23_shutdown");
    state.geometry.Shutdown(ownerCompletionKnown);
    if (state.geometry.GpuCompletionUnknown())
        QuarantineForGeometryUnknown("shutdown");
    if (state.gpuUnknown || state.quarantined)
    {
        Msg("ph_dx12_bridge: shutdown with unknown GPU completion; "
            "quarantined objects are not released");
    }
    else
    {
        EvaluateInfoQueue("shutdown");
        ReleaseObjects();
    }
    state.levelActive = false;
    state.state = BridgeState::Shutdown;
    GameDx12J2A_Shutdown();
    if (!state.shutdownLogged)
    {
        state.shutdownLogged = true;
        Msg("ph_dx12_bridge: shutdown complete");
    }
}

bool GameDx12Bridge_IsEnabled()
{
    return State().state == BridgeState::Ready;
}

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
