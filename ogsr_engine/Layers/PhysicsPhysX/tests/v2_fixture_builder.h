#pragma once

#include "physics_physx/physx_ragdoll_c.h"
#include "physics_portable/ragdoll.h"

#include <cstdint>
#include <vector>

namespace physics_physx_tests
{

struct V2Fixture
{
    std::vector<OgsrPhysXRagdollBone> bones;
    std::vector<OgsrPhysXRagdollBody> bodies;
    std::vector<OgsrPhysXRagdollJoint> joints;
    std::vector<OgsrPhysXRagdollShape> shapes;
    std::vector<OgsrPhysXRagdollMaterial> materials;
    std::vector<float> bodyWorld;
    std::vector<float> linearVelocity;
    std::vector<float> angularVelocity;
    std::vector<std::uint8_t> enabled;
    OgsrPhysXRagdollDefinition definition{};

    void build(const physics_portable::RagdollDescription& description,
               const physics_portable::RagdollInitialState& initial,
               const physics_portable::Vec3& offset = physics_portable::Vec3{});
};

} // namespace physics_physx_tests
