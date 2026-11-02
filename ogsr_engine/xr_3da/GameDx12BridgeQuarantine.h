#pragma once

// F4.0-G1B Round 2: minimal process-lifetime quarantine storage.
//
// This is deliberately a trivial aggregate: raw COM pointers, a raw HANDLE and
// a bool. It has no destructor and performs no release/CloseHandle. The
// experimental bridge transfers ownership here only when GPU completion is
// unknown; the OS reclaims the process resources at process exit.
//
// It is not a recovery design for a future native renderer. It exists solely
// to make the G1B emergency path fail-safe.

#include <cstdint>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct BridgeQuarantineStorage
{
    bool occupied = false;
    IUnknown* device = nullptr;
    IUnknown* queue = nullptr;
    IUnknown* fence = nullptr;
    IUnknown* allocator = nullptr;
    IUnknown* list = nullptr;
    IUnknown* infoQueue = nullptr;
    HANDLE fenceEvent = nullptr;
};

extern BridgeQuarantineStorage g_bridgeQuarantine;

inline bool BridgeQuarantineBegin(BridgeQuarantineStorage& storage) noexcept
{
    if (storage.occupied)
        return false;
    storage.occupied = true;
    return true;
}

template <typename T>
T* BridgeDetachToQuarantine(ComPtr<T>& object) noexcept
{
    T* raw = object.Get();
    object.Detach();
    return raw;
}
