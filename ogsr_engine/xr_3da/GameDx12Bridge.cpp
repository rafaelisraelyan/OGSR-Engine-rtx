#include "stdafx.h"
#include "GameDx12Bridge.h"
#include "GameDx12BridgeCommandLine.h"
#include "GameDx12BridgeDiagnostics.h"
#include "GameDx12BridgeQuarantine.h"
#include "GameDx12LevelGeometry.h"
#include "GameDx12OffscreenPass.h"

#include "igame_level.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

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
    DWORD ownerThreadId = 0;

    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12InfoQueue> infoQueue;

    // J1.2A owner-side geometry state. The bridge owns the device/queue/fence
    // and the adapter owns only the level registry/constants. Loading and
    // frame preparation stay inactive until a full Jupiter payload provides a
    // validated visual placement.
    GameDx12LevelGeometry12 geometry;
    GameDx12OffscreenPass12 offscreen;

    uint64_t fenceValue = 0;
    uint64_t levelSession = 0;
    uint64_t captureEpoch = 0;
    uint64_t captureSequence = 0;
    uint64_t capturedFrames = 0;
    uint64_t rejectedFrames = 0;
    uint64_t prePolicyStorageDenied = 0;
    uint64_t prePolicyRetrievalHidden = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t lastGameFrame = 0;
    uint64_t lastMatrixHash = 0;
    BridgeDiagnosticsVerdict diagnosticsVerdict =
        BridgeDiagnosticsVerdict::Unverified;
    char levelName[128]{};
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
        state.runtimeRequested =
            GameDx12Bridge_HasExactCommandLineFlag(Core.Params, kRuntimeFlag);
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
    if (SUCCEEDED(::D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf()))))
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
} // namespace

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
    state.levelActive = true;
    std::snprintf(state.levelName, sizeof(state.levelName), "%s",
                  levelName && levelName[0] ? levelName : "?");
    Msg("ph_dx12_bridge: level ready level='%s' session=%llu epoch=%llu",
        state.levelName,
        static_cast<unsigned long long>(state.levelSession),
        static_cast<unsigned long long>(state.captureEpoch));

    // J1.2A diagnostic load path. It is opt-in and never synthesizes a world
    // transform or draws. A full runtime can exercise the real VFS ->
    // ReadVisual -> BuildXrayLevelAsset12 -> GpuMeshRegistry12 upload path
    // before J1.2B supplies the validated visual placement.
    const char* geometryVisual =
        std::getenv("OGSR_DX12_GAME_GEOMETRY_VISUAL");
    if (geometryVisual != nullptr && geometryVisual[0] != '\0')
    {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(geometryVisual, &end, 10);
        if (end == geometryVisual || *end != '\0' ||
            parsed > 0xFFFFul)
        {
            Msg("ph_dx12_geometry: invalid OGSR_DX12_GAME_GEOMETRY_VISUAL");
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
            if (state.geometry.GpuCompletionUnknown())
                QuarantineForGeometryUnknown("level_ready");
        }
    }
}

void GameDx12Bridge_OnLevelTeardown()
{
    if (!EnsureOwnerThread("level_teardown"))
        return;
    BridgeStateData& state = State();
    if (!state.levelActive)
        return;
    state.levelActive = false;
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
            Msg("ph_dx12_bridge: invalid camera rejected count=%llu "
                "frame=%u",
                static_cast<unsigned long long>(state.rejectedFrames),
                Device.dwFrame);
        return;
    }

    const uint64_t hash = HashMatrix(view, projection, fullTransform);
    state.lastMatrixHash = hash;
    ++state.capturedFrames;
    if (MatrixBitsRequested())
        LogCameraBits(view, projection, fullTransform);
    if (state.capturedFrames == 1 || (state.capturedFrames % 60) == 0)
        LogCamera(view, projection, fullTransform, hash);
}

bool GameDx12Bridge_RenderOffscreenDiagnostic(
    const DirectX::XMFLOAT4X4& logicalViewProjection,
    const DirectX::XMFLOAT4X4& logicalWorld,
    const wchar_t* pngPath,
    std::string& reason)
{
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

    const float clearColor[4] = {0.03f, 0.05f, 0.09f, 1.0f};
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
    if (state.fence->GetCompletedValue() < expected ||
        state.device->GetDeviceRemovedReason() != S_OK)
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
    state.offscreen.CommitSubmittedFrame();
    if (!state.offscreen.Readback(reason))
        return false;
    if (pngPath != nullptr && !state.offscreen.SavePng(pngPath, reason))
        return false;

    Msg("ph_dx12_geometry: offscreen diagnostic submitted fence=%llu "
        "size=%ux%u",
        static_cast<unsigned long long>(expected), state.width, state.height);
    return true;
}

void GameDx12Bridge_OnDeviceReset(uint32_t width, uint32_t height)
{
    if (!EnsureOwnerThread("device_reset"))
        return;
    BridgeStateData& state = State();
    if (state.state != BridgeState::Ready)
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
    }
    Msg("ph_dx12_bridge: device reset width=%u height=%u epoch=%llu "
        "(offscreen target resized on next diagnostic)",
        width, height,
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
    const bool ownerCompletionKnown = !state.gpuUnknown && !state.quarantined;
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
