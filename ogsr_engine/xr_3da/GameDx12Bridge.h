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

void GameDx12Bridge_OnLevelReady(const char* levelName);
void GameDx12Bridge_OnLevelTeardown();
void GameDx12Bridge_OnFrame();
void GameDx12Bridge_OnDeviceReset(uint32_t width, uint32_t height);
void GameDx12Bridge_Shutdown();

bool GameDx12Bridge_IsEnabled();

#else // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

inline void GameDx12Bridge_OnLevelReady(const char*) {}
inline void GameDx12Bridge_OnLevelTeardown() {}
inline void GameDx12Bridge_OnFrame() {}
inline void GameDx12Bridge_OnDeviceReset(uint32_t, uint32_t) {}
inline void GameDx12Bridge_Shutdown() {}
inline bool GameDx12Bridge_IsEnabled() { return false; }

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
