#pragma once

#include "physics_physx/physx_ragdoll_c.h"
#include "physics_physx/physx_ragdoll_c_v2.h"
#include "physics_portable/ragdoll.h"

#include <string>

namespace physics_physx::v2
{

bool convertDefinition(const OgsrPhysXRagdollDefinition& source,
                       physics_portable::RagdollDescription& description,
                       physics_portable::RagdollInitialState& initial, std::string* error);

physics_portable::RagdollSimulationConfig makeSimulationConfig(const OgsrPhysX2SceneConfig& sceneConfig,
                                                               const OgsrPhysX2RagdollInstanceConfig& instanceConfig);

void storeRow(const physics_portable::Transform& value, float* out);

} // namespace physics_physx::v2
