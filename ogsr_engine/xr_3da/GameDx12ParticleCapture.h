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
