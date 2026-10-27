#pragma once

#ifdef OGSR_PHYSX_RAGDOLL

#include "physics_portable/ragdoll.h"

#include "../xrCore/_matrix.h"
#include "../xrCore/_vector3d.h"

#include <cstdint>
#include <string>
#include <vector>

class CPhysicsShell;
class IKinematics;
struct SHit;
struct OgsrPhysXRagdollState;

struct OgsrPhysXRagdollCapture
{
    physics_portable::RagdollDescription description;
    physics_portable::RagdollInitialState initial;
    std::vector<physics_portable::Transform> initialBoneWorld;
    std::vector<physics_portable::Transform> animationBoneModel;
    std::vector<std::uint16_t> boneBody; // bone -> body, or kInvalidBody
    std::vector<std::uint16_t> bodyBone; // body -> bone
    std::vector<std::uint8_t> boneKind;  // 0 = body, 1 = merged into parent body, 2 = frozen animation
    std::vector<physics_portable::Transform> mergedLocal;
};

bool OgsrPhysXRagdollBuildCapture(IKinematics& kinematics,
                                  CPhysicsShell& shell,
                                  const Fmatrix& entityWorld,
                                  const Fvector& velocity,
                                  OgsrPhysXRagdollCapture& capture,
                                  std::string* error);

bool OgsrPhysXRagdollSetStaticFragment(const Fvector& center, std::string* error);

bool OgsrPhysXRagdollActivate(const OgsrPhysXRagdollCapture& capture,
                              IKinematics& kinematics,
                              const Fmatrix& entityWorld,
                              const Fvector& center,
                              OgsrPhysXRagdollState*& state,
                              std::uint32_t& bodyCount,
                              std::uint32_t& jointCount,
                              std::string* error);

void OgsrPhysXRagdollRelease(OgsrPhysXRagdollState* state);

bool OgsrPhysXRagdollPublish(OgsrPhysXRagdollState& state,
                             IKinematics& kinematics,
                             Fmatrix& entityWorld,
                             std::string* error);

bool OgsrPhysXRagdollApplyBoneHit(OgsrPhysXRagdollState& state,
                                  const SHit& hit,
                                  const physics_portable::Vec3& boneSpacePosition,
                                  std::string* error);

bool OgsrPhysXRagdollApplyCentralImpulse(OgsrPhysXRagdollState& state,
                                         const physics_portable::Vec3& impulse,
                                         std::string* error);

void OgsrPhysXRagdollSmokeTick();

#endif // OGSR_PHYSX_RAGDOLL
