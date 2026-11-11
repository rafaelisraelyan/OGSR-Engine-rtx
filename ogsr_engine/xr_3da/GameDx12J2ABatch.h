#pragma once

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "dx12j2bbatch.h"

// Engine-side owner adapter. VFS access happens here on the owner thread; the
// CPU batch core receives only the immutable Snapshot12. No GPU resource is
// created.
//
// The adapter never mutates the global $level$ path. It accepts only the
// already-selected owner level and verifies that live identity before reading.
bool GameDx12J2ABatch_ReadSnapshot(const char* levelName,
                                  const j2bbatch12::Identity12& identity,
                                  j2bbatch12::Snapshot12& snapshot,
                                  const j2bbatch12::Limits12& limits,
                                  j2bbatch12::ErrorInfo12& error) noexcept;

bool GameDx12J2ABatch_PrepareFromVfs(
    const char* levelName, const j2bbatch12::Identity12& identity,
    uint32_t sectorId, uint32_t rootVisualId,
    const std::array<uint32_t, j2bbatch12::kTargetVisualCount>& visuals,
    gr::jobs::JobSystem* jobs, bool parallel,
    j2bbatch12::BatchResult12& result,
    const j2bbatch12::Limits12& limits) noexcept;

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
