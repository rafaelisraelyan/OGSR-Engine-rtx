#pragma once

#include "physics_portable/ragdoll.h"

namespace physics_physx_tests
{

physics_portable::RagdollDescription makeHumanoidDescription(
    physics_portable::SwingLimitMode swingLimitMode = physics_portable::SwingLimitMode::Pyramid);

physics_portable::RagdollInitialState makeHumanoidInitialState(const physics_portable::RagdollDescription& description);

physics_portable::RagdollDescription makePendulumDescription(physics_portable::SwingLimitMode swingLimitMode);

physics_portable::RagdollInitialState makePendulumInitialState(const physics_portable::RagdollDescription& description);

} // namespace physics_physx_tests
