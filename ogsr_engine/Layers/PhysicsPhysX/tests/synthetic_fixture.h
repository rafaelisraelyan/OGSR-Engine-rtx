#pragma once

#include "physics_portable/json.h"

namespace physics_physx_tests
{

// Builds a deterministic, self-authored 11-body / 10-joint humanoid-like
// capture document for CI. It intentionally contains no OGSR/game asset data.
bool makeSyntheticRagdollCapture(physics_portable::RagdollCaptureDocument& document);

} // namespace physics_physx_tests
