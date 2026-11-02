#pragma once

#include "dx12xrayasset.h"
#include "dx12xraylevelgeometry.h"

// F4.0-J1.1: CPU-only handoff from the strict level/level.geom reader to the
// existing X-Ray GPU asset path.
//
// The adapter never touches D3D12. It copies the compact 32-byte-stride source
// vertices into XrayVertex12 (position from offset 0, dummy normal/UV for the
// flat-fallback material), keeps the uint16 index buffer unchanged and
// re-validates bounds, sphere coverage and index ranges before the owner is
// allowed to create GPU resources.
bool BuildXrayLevelAsset12(const xraylevel12::Geometry& geometry,
                           XrayAssetData12& asset,
                           const char** reason);
