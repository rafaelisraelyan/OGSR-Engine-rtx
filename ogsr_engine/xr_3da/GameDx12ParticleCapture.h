#pragma once
#include "GameDx12ParticlePackets.h"

class IRenderVisual;

// XR_3DA links the R4 static library. No change to render interfaces or
// vtables. Call only on the game owner before seqFrameMT, with the original
// camera basis; no live particle pointer may escape this call.
namespace PS
{
void CaptureNativeParticleRoots(std::span<IRenderVisual* const> roots, gameparticle12::Snapshot& snapshot);
}

// Read-only owner-thread adapter to the R4 blender library. Returns false for
// unknown, Lua-overridden or non-classic particle shaders.
#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
bool GameDx12ReadR4ClassicParticleSource(const char* name, uint32_t& blendId,
    uint32_t& priority, bool& clampTexture, bool& strictSorting);
#endif
