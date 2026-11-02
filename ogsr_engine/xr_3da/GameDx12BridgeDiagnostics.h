#pragma once

// F4.0-G1B Round 2: trustworthy, bounded D3D12 InfoQueue diagnostics.
//
// The evaluator is header-only so the real-D3D12 probe and the mock probe can
// exercise the exact production logic. It never converts an incomplete read
// into PASS: unavailable queues, failed reads, unknown severities, malformed
// descriptions, lost messages, unverified filters or unknown GPU completion
// all produce UNVERIFIED.
//
// The filter policy is explicit: clear both filters, push empty (allow-all)
// filters, then verify that both filters are allow-all. Messages denied before
// that policy are recorded separately; they make the lifetime UNVERIFIED even
// if the post-policy window itself is clean.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>

enum class BridgeDiagnosticsVerdict : uint8_t
{
    Pass = 0,
    Fail,
    Unverified
};

inline const char* BridgeDiagnosticsVerdictName(
    BridgeDiagnosticsVerdict verdict) noexcept
{
    switch (verdict)
    {
    case BridgeDiagnosticsVerdict::Pass: return "PASS";
    case BridgeDiagnosticsVerdict::Fail: return "FAIL";
    case BridgeDiagnosticsVerdict::Unverified: return "UNVERIFIED";
    }
    return "UNVERIFIED";
}

struct BridgeInfoQueuePolicy
{
    bool debugLayerEnabled = false;
    bool gbvEnabled = false;
    bool gpuCompletionConfirmed = false;
    bool prePolicyFilterUnsafe = false;
    bool producerQuiescenceConfirmed = false;
    bool lifetimeHistoryVerified = false;
    uint64_t prePolicyStorageDenied = 0;
    uint64_t prePolicyRetrievalHidden = 0;
};

struct BridgeInfoQueueResult
{
    BridgeDiagnosticsVerdict verdict = BridgeDiagnosticsVerdict::Unverified;
    const char* scope = "unknown";
    bool debugLayerEnabled = false;
    bool gbvEnabled = false;
    bool gbvVerified = false;
    bool infoQueueAvailable = false;
    bool storageFilterVerified = false;
    bool retrievalFilterVerified = false;
    bool prePolicyFilterUnsafe = false;
    bool gpuCompletionConfirmed = false;
    bool lifetimeVerified = false;
    uint64_t stored = 0;
    uint64_t retrievable = 0;
    uint64_t readSuccessfully = 0;
    uint64_t readFailures = 0;
    uint64_t sizeMismatches = 0;
    uint64_t allowed = 0;
    uint64_t denied = 0;
    uint64_t discardedByLimit = 0;
    uint64_t prePolicyStorageDenied = 0;
    uint64_t prePolicyRetrievalHidden = 0;
    uint64_t info = 0;
    uint64_t message = 0;
    uint64_t warning = 0;
    uint64_t error = 0;
    uint64_t corruption = 0;
    uint64_t unknownSeverity = 0;
    uint64_t malformedDescriptions = 0;
    uint64_t allocationFailures = 0;
    uint32_t collectionAttempts = 0;
    uint32_t counterSnapshotPairs = 0;
    // True only for the bounded window; this is not an atomicity guarantee.
    bool windowConsistent = false;
    char firstFailure[256] = {};
    const char* reason = "";
};

class IBridgeInfoQueue
{
public:
    virtual ~IBridgeInfoQueue() = default;

    virtual UINT64 Stored() noexcept = 0;
    virtual UINT64 Allowed() noexcept = 0;
    virtual UINT64 Denied() noexcept = 0;
    virtual UINT64 Retrievable() noexcept = 0;
    virtual UINT64 Discarded() noexcept = 0;

    virtual HRESULT GetMessage(UINT64 index, D3D12_MESSAGE* message,
                               SIZE_T* size) noexcept = 0;
    virtual void ClearStorageFilter() noexcept = 0;
    virtual HRESULT PushEmptyStorageFilter() noexcept = 0;
    virtual void ClearRetrievalFilter() noexcept = 0;
    virtual HRESULT PushEmptyRetrievalFilter() noexcept = 0;
    virtual HRESULT StorageFilterAllowAll(bool& allowAll) noexcept = 0;
    virtual HRESULT RetrievalFilterAllowAll(bool& allowAll) noexcept = 0;
};

inline bool BridgeInfoQueueFilterIsAllowAll(
    const D3D12_INFO_QUEUE_FILTER& filter) noexcept
{
    return filter.AllowList.NumSeverities == 0 &&
           filter.AllowList.NumCategories == 0 &&
           filter.AllowList.NumIDs == 0 &&
           filter.DenyList.NumSeverities == 0 &&
           filter.DenyList.NumCategories == 0 &&
           filter.DenyList.NumIDs == 0;
}

constexpr SIZE_T kMaxBridgeInfoQueueFilterBytes = 1024 * 1024;
constexpr SIZE_T kMaxBridgeInfoQueueMessageBytes = 1024 * 1024;

inline void* BridgeInfoQueueDefaultAllocate(std::size_t size) noexcept
{
    return std::malloc(size);
}

inline void* BridgeInfoQueueDefaultAllocateMessage(std::size_t size) noexcept
{
    return std::malloc(size);
}

inline bool BridgeInfoQueueFilterArrayValid(
    const void* allocationBase, SIZE_T allocationSize, const void* pointer,
    UINT count, std::size_t elementSize) noexcept
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(allocationBase);
    const uintptr_t end = base + allocationSize;
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);

    if (count == 0)
    {
        // The D3D12 API reports empty lists with a non-null one-past-end
        // pointer. No element is dereferenced, so that pointer is harmless;
        // anything outside the returned buffer is not a valid contract.
        return pointer == nullptr ||
               (end >= base && address >= base && address <= end);
    }
    if (pointer == nullptr || elementSize == 0)
        return false;
    if (end < base || address < base || address >= end)
        return false;
    return static_cast<uintptr_t>(count) <= (end - address) / elementSize;
}

inline bool BridgeInfoQueueFilterArraysValid(
    const D3D12_INFO_QUEUE_FILTER& filter, SIZE_T allocationSize) noexcept
{
    return BridgeInfoQueueFilterArrayValid(
               &filter, allocationSize, filter.AllowList.pSeverityList,
               filter.AllowList.NumSeverities, sizeof(D3D12_MESSAGE_SEVERITY)) &&
           BridgeInfoQueueFilterArrayValid(
               &filter, allocationSize, filter.AllowList.pCategoryList,
               filter.AllowList.NumCategories, sizeof(D3D12_MESSAGE_CATEGORY)) &&
           BridgeInfoQueueFilterArrayValid(
               &filter, allocationSize, filter.AllowList.pIDList,
               filter.AllowList.NumIDs, sizeof(D3D12_MESSAGE_ID)) &&
           BridgeInfoQueueFilterArrayValid(
               &filter, allocationSize, filter.DenyList.pSeverityList,
               filter.DenyList.NumSeverities, sizeof(D3D12_MESSAGE_SEVERITY)) &&
           BridgeInfoQueueFilterArrayValid(
               &filter, allocationSize, filter.DenyList.pCategoryList,
               filter.DenyList.NumCategories, sizeof(D3D12_MESSAGE_CATEGORY)) &&
           BridgeInfoQueueFilterArrayValid(
               &filter, allocationSize, filter.DenyList.pIDList,
               filter.DenyList.NumIDs, sizeof(D3D12_MESSAGE_ID));
}

template <typename TGetFilter>
inline HRESULT BridgeInfoQueueReadFilterBuffer(
    TGetFilter&& getFilter, SIZE_T& size, void*& buffer,
    D3D12_INFO_QUEUE_FILTER*& filter,
    void* (*allocate)(std::size_t) noexcept =
        BridgeInfoQueueDefaultAllocate) noexcept
{
    buffer = nullptr;
    filter = nullptr;
    size = 0;

    SIZE_T requested = 0;
    HRESULT hr = getFilter(nullptr, &requested);
    if (hr == S_FALSE && requested == 0)
        return E_FAIL;
    if (FAILED(hr))
        return hr;
    if (requested < sizeof(D3D12_INFO_QUEUE_FILTER))
        return E_FAIL;
    if (requested > kMaxBridgeInfoQueueFilterBytes)
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);

    void* memory = allocate(requested);
    if (memory == nullptr)
        return E_OUTOFMEMORY;

    SIZE_T actual = requested;
    hr = getFilter(reinterpret_cast<D3D12_INFO_QUEUE_FILTER*>(memory), &actual);
    if (hr != S_OK)
    {
        std::free(memory);
        return FAILED(hr) ? hr : E_FAIL;
    }
    if (actual < sizeof(D3D12_INFO_QUEUE_FILTER) || actual > requested)
    {
        std::free(memory);
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    }
    D3D12_INFO_QUEUE_FILTER* candidate =
        reinterpret_cast<D3D12_INFO_QUEUE_FILTER*>(memory);
    if (!BridgeInfoQueueFilterArraysValid(*candidate, actual))
    {
        std::free(memory);
        return E_FAIL;
    }

    buffer = memory;
    filter = candidate;
    size = actual;
    return S_OK;
}

inline bool BridgeInfoQueueFilterAllowsCriticalSeverities(
    const D3D12_INFO_QUEUE_FILTER& filter) noexcept
{
    const auto contains = [](const D3D12_INFO_QUEUE_FILTER_DESC& desc,
                             D3D12_MESSAGE_SEVERITY severity) {
        for (UINT i = 0; i < desc.NumSeverities; ++i)
            if (desc.pSeverityList[i] == severity)
                return true;
        return false;
    };

    if (contains(filter.DenyList, D3D12_MESSAGE_SEVERITY_WARNING) ||
        contains(filter.DenyList, D3D12_MESSAGE_SEVERITY_ERROR) ||
        contains(filter.DenyList, D3D12_MESSAGE_SEVERITY_CORRUPTION))
        return false;

    if (filter.DenyList.NumCategories != 0 || filter.DenyList.NumIDs != 0)
        return false;
    if (filter.AllowList.NumCategories != 0 || filter.AllowList.NumIDs != 0)
        return false;
    if (filter.AllowList.NumSeverities == 0)
        return true;
    return contains(filter.AllowList, D3D12_MESSAGE_SEVERITY_WARNING) &&
           contains(filter.AllowList, D3D12_MESSAGE_SEVERITY_ERROR) &&
           contains(filter.AllowList, D3D12_MESSAGE_SEVERITY_CORRUPTION);
}

inline HRESULT BridgeInfoQueueReadFilterUnsafe(
    ID3D12InfoQueue* queue, bool storage, bool& unsafe,
    void* (*allocate)(std::size_t) noexcept =
        BridgeInfoQueueDefaultAllocate) noexcept
{
    unsafe = true;
    if (queue == nullptr)
        return E_POINTER;

    const auto getFilter =
        [queue, storage](D3D12_INFO_QUEUE_FILTER* filter,
                         SIZE_T* size) noexcept -> HRESULT {
        return storage ? queue->GetStorageFilter(filter, size)
                       : queue->GetRetrievalFilter(filter, size);
    };

    SIZE_T size = 0;
    void* buffer = nullptr;
    D3D12_INFO_QUEUE_FILTER* filter = nullptr;
    const HRESULT hr = BridgeInfoQueueReadFilterBuffer(
        getFilter, size, buffer, filter, allocate);
    if (FAILED(hr))
        return hr;
    if (filter == nullptr)
        return E_FAIL;

    unsafe = !BridgeInfoQueueFilterAllowsCriticalSeverities(*filter);
    std::free(buffer);
    return S_OK;
}

inline HRESULT BridgeInfoQueueReadFilterAllowAll(
    ID3D12InfoQueue* queue, bool storage, bool& allowAll,
    void* (*allocate)(std::size_t) noexcept =
        BridgeInfoQueueDefaultAllocate) noexcept
{
    allowAll = false;
    if (queue == nullptr)
        return E_POINTER;

    const auto getFilter =
        [queue, storage](D3D12_INFO_QUEUE_FILTER* filter,
                         SIZE_T* size) noexcept -> HRESULT {
        return storage ? queue->GetStorageFilter(filter, size)
                       : queue->GetRetrievalFilter(filter, size);
    };

    SIZE_T size = 0;
    void* buffer = nullptr;
    D3D12_INFO_QUEUE_FILTER* filter = nullptr;
    const HRESULT hr = BridgeInfoQueueReadFilterBuffer(
        getFilter, size, buffer, filter, allocate);
    if (FAILED(hr))
        return hr;
    if (filter == nullptr)
        return E_FAIL;

    allowAll = BridgeInfoQueueFilterIsAllowAll(*filter);
    std::free(buffer);
    return S_OK;
}

inline bool BridgeInfoQueueDescriptionValid(
    const D3D12_MESSAGE* message, SIZE_T returnedSize,
    SIZE_T allocatedCapacity) noexcept
{
    if (message == nullptr ||
        allocatedCapacity < sizeof(D3D12_MESSAGE) ||
        returnedSize < sizeof(D3D12_MESSAGE) ||
        returnedSize > allocatedCapacity)
        return false;

    const uintptr_t base = reinterpret_cast<uintptr_t>(message);
    const uintptr_t allocationEnd = base + allocatedCapacity;
    const uintptr_t returnedEnd = base + returnedSize;
    if (allocationEnd < base || returnedEnd < base ||
        returnedEnd > allocationEnd)
        return false;

    if (message->DescriptionByteLength == 0)
        return true;
    if (message->pDescription == nullptr)
        return false;

    const uintptr_t description =
        reinterpret_cast<uintptr_t>(message->pDescription);
    if (description < base || description >= returnedEnd)
        return false;
    const uintptr_t length =
        static_cast<uintptr_t>(message->DescriptionByteLength);
    if (length > returnedEnd - description)
        return false;
    return true;
}

inline void BridgeInfoQueueRememberFailure(BridgeInfoQueueResult& result,
                                           const char* description,
                                           SIZE_T descriptionLength)
{
    if (description == nullptr || descriptionLength == 0 ||
        result.firstFailure[0] != '\0')
        return;

    const SIZE_T copy = descriptionLength < (sizeof(result.firstFailure) - 1)
        ? descriptionLength
        : (sizeof(result.firstFailure) - 1);
    std::memcpy(result.firstFailure, description, copy);
    result.firstFailure[copy] = '\0';
}

constexpr uint32_t kBridgeInfoQueueMaxCollectionAttempts = 3;
constexpr uint32_t kBridgeInfoQueueMaxCounterSnapshotPairs = 3;

// A counter snapshot is deliberately a plain value type. It is not atomic:
// the five ID3D12InfoQueue counters are separate API calls. The evaluator
// requires two consecutive equal snapshots, but that only detects changes
// between the reads; it cannot prove the absence of an ABA change.
struct BridgeInfoQueueCounters
{
    uint64_t stored = 0;
    uint64_t allowed = 0;
    uint64_t denied = 0;
    uint64_t retrievable = 0;
    uint64_t discarded = 0;
};

inline bool BridgeInfoQueueCountersEqual(
    const BridgeInfoQueueCounters& left,
    const BridgeInfoQueueCounters& right) noexcept
{
    return left.stored == right.stored && left.allowed == right.allowed &&
           left.denied == right.denied &&
           left.retrievable == right.retrievable &&
           left.discarded == right.discarded;
}

inline BridgeInfoQueueCounters BridgeInfoQueueCaptureCounters(
    IBridgeInfoQueue& queue) noexcept
{
    BridgeInfoQueueCounters counters;
    counters.stored = queue.Stored();
    counters.allowed = queue.Allowed();
    counters.denied = queue.Denied();
    counters.retrievable = queue.Retrievable();
    counters.discarded = queue.Discarded();
    return counters;
}

inline bool BridgeInfoQueueReadStableCounters(
    IBridgeInfoQueue& queue, BridgeInfoQueueCounters& out,
    uint32_t& pairs) noexcept
{
    BridgeInfoQueueCounters last;
    for (uint32_t pair = 0; pair < kBridgeInfoQueueMaxCounterSnapshotPairs;
         ++pair)
    {
        const BridgeInfoQueueCounters first =
            BridgeInfoQueueCaptureCounters(queue);
        last = BridgeInfoQueueCaptureCounters(queue);
        pairs = pair + 1;
        if (BridgeInfoQueueCountersEqual(first, last))
        {
            out = last;
            return true;
        }
    }
    out = last;
    return false;
}

// Diagnostic window contract:
//
// - The production caller sets gpuCompletionConfirmed only after the fence
//   for the GPU work covered by this window has completed.
// - The window starts after the allow-all policy is installed and a stable
//   post-policy counter snapshot has been obtained.
// - The window closes at the stable counter snapshot taken after the second
//   filter verification. The final snapshot is the last read of the window.
// - producerQuiescenceConfirmed and lifetimeHistoryVerified are caller
//   assertions. They are false in the current production bridge, so a scoped
//   PASS never claims full lifetime trust. A caller may set them only with a
//   proven ownership/serialization and device-lifetime observation contract.
// - CPU-side producers on other threads are not stopped by this evaluator.
//   Messages produced after the closing snapshot are outside the window.
// - Repeated counter reads are not an atomic snapshot. An ABA change, a
//   transient message added and removed between reads, or a transient filter
//   change can therefore be invisible. A PASS is a bounded observed-window
//   verdict, not a claim that the queue is globally immutable.
inline BridgeInfoQueueResult EvaluateBridgeInfoQueue(
    IBridgeInfoQueue& queue, const BridgeInfoQueuePolicy& policy,
    const char* scope,
    void* (*allocateMessage)(std::size_t) noexcept =
        BridgeInfoQueueDefaultAllocateMessage) noexcept
{
    BridgeInfoQueueResult result;
    result.scope = scope != nullptr ? scope : "unknown";
    result.debugLayerEnabled = policy.debugLayerEnabled;
    result.gbvEnabled = policy.gbvEnabled;
    result.gpuCompletionConfirmed = policy.gpuCompletionConfirmed;
    result.prePolicyStorageDenied = policy.prePolicyStorageDenied;
    result.prePolicyRetrievalHidden = policy.prePolicyRetrievalHidden;
    result.prePolicyFilterUnsafe = policy.prePolicyFilterUnsafe;
    result.infoQueueAvailable = true;
    result.gbvVerified = policy.gbvEnabled && policy.gpuCompletionConfirmed;

    if (!policy.debugLayerEnabled)
    {
        result.reason = "debug layer unavailable";
        return result;
    }
    if (!policy.gpuCompletionConfirmed)
    {
        result.reason = "GPU completion not confirmed";
        return result;
    }

    // Install the explicit allow-all policy before the window starts.
    bool filterOk = true;
    queue.ClearStorageFilter();
    if (FAILED(queue.PushEmptyStorageFilter()))
        filterOk = false;
    queue.ClearRetrievalFilter();
    if (FAILED(queue.PushEmptyRetrievalFilter()))
        filterOk = false;

    bool storageAllowAll = false;
    bool retrievalAllowAll = false;
    const HRESULT storageFilterHr =
        queue.StorageFilterAllowAll(storageAllowAll);
    const HRESULT retrievalFilterHr =
        queue.RetrievalFilterAllowAll(retrievalAllowAll);
    result.storageFilterVerified =
        filterOk && SUCCEEDED(storageFilterHr) && storageAllowAll;
    result.retrievalFilterVerified =
        filterOk && SUCCEEDED(retrievalFilterHr) && retrievalAllowAll;
    if (!result.storageFilterVerified || !result.retrievalFilterVerified)
    {
        result.reason = "allow-all filter policy not verified";
        return result;
    }

    for (uint32_t attempt = 0;
         attempt < kBridgeInfoQueueMaxCollectionAttempts; ++attempt)
    {
        result.collectionAttempts = attempt + 1;

        BridgeInfoQueueCounters before;
        uint32_t pairs = 0;
        if (!BridgeInfoQueueReadStableCounters(queue, before, pairs))
        {
            result.counterSnapshotPairs = pairs;
            result.stored = before.stored;
            result.allowed = before.allowed;
            result.denied = before.denied;
            result.retrievable = before.retrievable;
            result.discardedByLimit = before.discarded;
            result.reason = "counter snapshot unstable before traversal";
            return result;
        }
        result.counterSnapshotPairs = pairs;
        result.stored = before.stored;
        result.allowed = before.allowed;
        result.denied = before.denied;
        result.retrievable = before.retrievable;
        result.discardedByLimit = before.discarded;

        if (before.retrievable != before.stored)
        {
            result.reason = "post-policy retrieval counters are inconsistent";
            return result;
        }
        if (before.discarded != 0)
        {
            result.reason = "messages were discarded before traversal";
            return result;
        }

        result.readSuccessfully = 0;
        result.readFailures = 0;
        result.sizeMismatches = 0;
        result.info = 0;
        result.message = 0;
        result.warning = 0;
        result.error = 0;
        result.corruption = 0;
        result.unknownSeverity = 0;
        result.malformedDescriptions = 0;
        result.allocationFailures = 0;
        result.firstFailure[0] = '\0';

        for (UINT64 index = 0; index < before.stored; ++index)
        {
            SIZE_T allocatedCapacity = 0;
            HRESULT hr = queue.GetMessage(index, nullptr, &allocatedCapacity);
            if (FAILED(hr) || allocatedCapacity < sizeof(D3D12_MESSAGE))
            {
                ++result.readFailures;
                continue;
            }
            if (allocatedCapacity > kMaxBridgeInfoQueueMessageBytes)
            {
                ++result.sizeMismatches;
                ++result.readFailures;
                continue;
            }

            D3D12_MESSAGE* message = static_cast<D3D12_MESSAGE*>(
                allocateMessage(allocatedCapacity));
            if (message == nullptr)
            {
                ++result.allocationFailures;
                ++result.readFailures;
                break;
            }

            SIZE_T returnedSize = allocatedCapacity;
            hr = queue.GetMessage(index, message, &returnedSize);
            if (hr != S_OK || returnedSize < sizeof(D3D12_MESSAGE) ||
                returnedSize > allocatedCapacity)
            {
                if (returnedSize < sizeof(D3D12_MESSAGE) ||
                    returnedSize > allocatedCapacity)
                    ++result.sizeMismatches;
                ++result.readFailures;
                std::free(message);
                continue;
            }
            if (!BridgeInfoQueueDescriptionValid(
                    message, returnedSize, allocatedCapacity))
            {
                ++result.malformedDescriptions;
                std::free(message);
                continue;
            }

            switch (message->Severity)
            {
            case D3D12_MESSAGE_SEVERITY_CORRUPTION:
                ++result.corruption;
                BridgeInfoQueueRememberFailure(
                    result, message->pDescription,
                    message->DescriptionByteLength);
                break;
            case D3D12_MESSAGE_SEVERITY_ERROR:
                ++result.error;
                BridgeInfoQueueRememberFailure(
                    result, message->pDescription,
                    message->DescriptionByteLength);
                break;
            case D3D12_MESSAGE_SEVERITY_WARNING:
                ++result.warning;
                BridgeInfoQueueRememberFailure(
                    result, message->pDescription,
                    message->DescriptionByteLength);
                break;
            case D3D12_MESSAGE_SEVERITY_INFO:
                ++result.info;
                break;
            case D3D12_MESSAGE_SEVERITY_MESSAGE:
                ++result.message;
                break;
            default:
                ++result.unknownSeverity;
                break;
            }

            ++result.readSuccessfully;
            std::free(message);
        }

        // A real severity violation has priority over any consistency
        // uncertainty: FAIL is never weakened to UNVERIFIED.
        if (result.warning != 0 || result.error != 0 ||
            result.corruption != 0)
        {
            result.verdict = BridgeDiagnosticsVerdict::Fail;
            result.reason = "warning/error/corruption present";
            return result;
        }

        // Hard read/data errors are not retried into PASS.
        if (result.readFailures != 0 || result.sizeMismatches != 0 ||
            result.malformedDescriptions != 0 ||
            result.unknownSeverity != 0 ||
            result.allocationFailures != 0)
        {
            result.reason = "message read failed or malformed";
            return result;
        }

        BridgeInfoQueueCounters after;
        pairs = 0;
        if (!BridgeInfoQueueReadStableCounters(queue, after, pairs))
        {
            result.counterSnapshotPairs = pairs;
            result.stored = after.stored;
            result.allowed = after.allowed;
            result.denied = after.denied;
            result.retrievable = after.retrievable;
            result.discardedByLimit = after.discarded;
            result.reason = "counter snapshot unstable after traversal";
            return result;
        }
        result.counterSnapshotPairs = pairs;
        result.stored = after.stored;
        result.allowed = after.allowed;
        result.denied = after.denied;
        result.retrievable = after.retrievable;
        result.discardedByLimit = after.discarded;

        if (!BridgeInfoQueueCountersEqual(before, after))
        {
            const bool storedChanged = after.stored != before.stored;
            const bool allowedChanged = after.allowed != before.allowed;
            const bool retrievableChanged =
                after.retrievable != before.retrievable;

            // A changed denied/discarded counter means a message was hidden
            // or lost. An allowed/retrievable change without a corresponding
            // stored change is also unexplained and must not be retried
            // into a clean PASS.
            // A decrease means messages disappeared from the queue. Even if
            // the new snapshot is stable, a removed critical message must
            // not be retried into PASS.
            if (after.stored < before.stored ||
                after.allowed < before.allowed ||
                after.retrievable < before.retrievable)
            {
                result.reason = "message counters decreased during traversal";
                return result;
            }
            if (after.denied != before.denied ||
                after.discarded != before.discarded ||
                (!storedChanged && (allowedChanged || retrievableChanged)))
            {
                result.reason = "unexplained counter change during traversal";
                return result;
            }
            if (attempt + 1 < kBridgeInfoQueueMaxCollectionAttempts)
            {
                result.reason = "queue changed during traversal; retrying";
                continue;
            }
            result.reason = "queue changed during traversal; retry limit reached";
            return result;
        }

        // The filters must still be allow-all at the end of the window.
        bool storageAllowAllAfter = false;
        bool retrievalAllowAllAfter = false;
        const HRESULT storageAfterHr =
            queue.StorageFilterAllowAll(storageAllowAllAfter);
        const HRESULT retrievalAfterHr =
            queue.RetrievalFilterAllowAll(retrievalAllowAllAfter);
        result.storageFilterVerified =
            SUCCEEDED(storageAfterHr) && storageAllowAllAfter;
        result.retrievalFilterVerified =
            SUCCEEDED(retrievalAfterHr) && retrievalAllowAllAfter;
        if (!result.storageFilterVerified ||
            !result.retrievalFilterVerified)
        {
            result.reason = "filters changed during traversal";
            return result;
        }

        // The filter verification itself can race with a producer. The
        // closing counter snapshot is taken after it and is the last read of
        // the declared window.
        BridgeInfoQueueCounters closing;
        pairs = 0;
        if (!BridgeInfoQueueReadStableCounters(queue, closing, pairs))
        {
            result.counterSnapshotPairs = pairs;
            result.stored = closing.stored;
            result.allowed = closing.allowed;
            result.denied = closing.denied;
            result.retrievable = closing.retrievable;
            result.discardedByLimit = closing.discarded;
            result.reason = "counter snapshot unstable after filter verification";
            return result;
        }
        result.counterSnapshotPairs = pairs;
        result.stored = closing.stored;
        result.allowed = closing.allowed;
        result.denied = closing.denied;
        result.retrievable = closing.retrievable;
        result.discardedByLimit = closing.discarded;

        if (!BridgeInfoQueueCountersEqual(after, closing))
        {
            const bool storedChanged = closing.stored != after.stored;
            const bool allowedChanged = closing.allowed != after.allowed;
            const bool retrievableChanged =
                closing.retrievable != after.retrievable;
            if (closing.stored < after.stored ||
                closing.allowed < after.allowed ||
                closing.retrievable < after.retrievable)
            {
                result.reason =
                    "message counters decreased after filter verification";
                return result;
            }
            if (closing.denied != after.denied ||
                closing.discarded != after.discarded ||
                (!storedChanged && (allowedChanged || retrievableChanged)))
            {
                result.reason =
                    "unexplained counter change after filter verification";
                return result;
            }
            if (attempt + 1 < kBridgeInfoQueueMaxCollectionAttempts)
            {
                result.reason =
                    "queue changed after filter verification; retrying";
                continue;
            }
            result.reason =
                "queue changed after filter verification; retry limit reached";
            return result;
        }

        if (result.readFailures > before.stored ||
            result.readSuccessfully != before.stored - result.readFailures)
        {
            result.reason = "message read accounting mismatch";
            return result;
        }
        if (closing.retrievable != closing.stored)
        {
            result.reason = "final retrieval counters are inconsistent";
            return result;
        }
        if (closing.discarded != 0)
        {
            result.reason = "messages were discarded during the window";
            return result;
        }
        if (policy.prePolicyFilterUnsafe)
        {
            result.reason = "pre-policy filter was unsafe";
            return result;
        }

        result.verdict = BridgeDiagnosticsVerdict::Pass;
        result.windowConsistent = true;
        result.reason = "consistent observed window";
        result.lifetimeVerified =
            policy.producerQuiescenceConfirmed &&
            policy.lifetimeHistoryVerified &&
            policy.prePolicyStorageDenied == 0 &&
            policy.prePolicyRetrievalHidden == 0 &&
            !policy.prePolicyFilterUnsafe &&
            closing.denied == 0 &&
            closing.discarded == 0;
        return result;
    }

    result.reason = "collection retry limit reached";
    return result;
}
