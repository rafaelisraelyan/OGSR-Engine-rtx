#pragma once

#include <cstdint>

// F4.0-G1B: opt-in, in-process DX12 game-frame bridge.
//
// The bridge owns a separate D3D12 device/queue/fence and never touches the
// R4 DX11 device, swapchain, Present or renderer interfaces. It only captures
// validated game camera snapshots and session/frame identity. No draw is
// performed at this stage.
//
// The implementation is compiled only when
// OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE is defined. Runtime activation
// additionally requires the exact startup parameter -dx12_game_bridge.
//
// When the feature is compiled out, the entry points below are inline no-ops:
// no bridge translation unit is linked and no D3D12 object can be created.

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "GameDx12ReadbackCoverage.h"

#include <DirectXMath.h>
#include <string>

void GameDx12Bridge_OnLevelReady(const char* levelName);
void GameDx12Bridge_OnLevelTeardown();
void GameDx12Bridge_OnFrame();
void GameDx12Bridge_OnDeviceReset(uint32_t width, uint32_t height);
void GameDx12Bridge_Shutdown();

bool GameDx12Bridge_IsEnabled();

// R2A diagnostic outcome. gpuCompleted means the exact owner fence/device
// identity was confirmed by OnDrawCompleted. imageGatePass is independent:
// zero non-clear readback pixels must fail the J1 image gate even when the
// GPU completed. pngSaved is true only when a requested PNG was written.
struct GameDx12DiagnosticResult
{
    bool gpuCompleted = false;
    bool imageGatePass = false;
    bool pngSaved = false;
    GameDx12ReadbackCoverage12 coverage{};
};

// J1.2B owner-only diagnostic render. logicalViewProjection and logicalWorld
// must come from the real game camera/placement; this function does not
// synthesize either. pngPath may be null to skip the PNG save.
bool GameDx12Bridge_RenderOffscreenDiagnostic(
    const DirectX::XMFLOAT4X4& logicalViewProjection,
    const DirectX::XMFLOAT4X4& logicalWorld,
    const wchar_t* pngPath,
    std::string& reason,
    GameDx12DiagnosticResult* result = nullptr);

#else // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

inline void GameDx12Bridge_OnLevelReady(const char*) {}
inline void GameDx12Bridge_OnLevelTeardown() {}
inline void GameDx12Bridge_OnFrame() {}
inline void GameDx12Bridge_OnDeviceReset(uint32_t, uint32_t) {}
inline void GameDx12Bridge_Shutdown() {}
inline bool GameDx12Bridge_IsEnabled() { return false; }

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
