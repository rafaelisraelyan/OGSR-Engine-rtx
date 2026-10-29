#pragma once

#ifdef OGSR_PHYSX_RAGDOLL

#include "physics_portable/ragdoll.h"
#include "physics_physx/physx_ragdoll_c_v2.h"

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
                              std::uint32_t ownerId,
                              OgsrPhysXRagdollState*& state,
                              std::uint32_t& bodyCount,
                              std::uint32_t& jointCount,
                              std::string* error);

void OgsrPhysXRagdollRelease(OgsrPhysXRagdollState* state);
// A wrapper can outlive its backend instance after SceneDestroy
// reconciliation. It stays valid for its NPC owner but must not publish pose
// or accept impulses anymore.
bool OgsrPhysXRagdollIsActive(const OgsrPhysXRagdollState* state);

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

void OgsrPhysXRagdollSmokeCameraStop();
bool OgsrPhysXRagdollSmokeCameraActive();

// P3.1-C multi-instance (v2) runtime. These functions are implemented in
// ogsr_physx_runtime.cpp (DLL/scene lifetime) and ogsr_physx_ragdoll.cpp
// (per-NPC state). They are only used when -physx_multi_instances is set.
bool OgsrPhysXRagdollMultiInstancesRequested();
bool OgsrPhysXRagdollTargetMatches(const char* name, const char* section);
bool OgsrPhysXRagdollV2SceneAlive();

bool OgsrPhysXRagdollV2Create(const Fvector& center,
                              const OgsrPhysXRagdollDefinition& definition,
                              const OgsrPhysX2RagdollInstanceConfig& config,
                              OgsrPhysX2RagdollHandle& handle,
                              std::string* error);
bool OgsrPhysXRagdollV2Destroy(OgsrPhysX2RagdollHandle handle, std::string* error);
bool OgsrPhysXRagdollV2GetPose(OgsrPhysX2RagdollHandle handle,
                               OgsrPhysXRagdollPose& pose,
                               float* bodyWorld,
                               std::uint32_t maxBodies);
bool OgsrPhysXRagdollV2GetAnyPose(OgsrPhysXRagdollPose& pose, float* bodyWorld, std::uint32_t maxBodies);
bool OgsrPhysXRagdollV2GetInfo(OgsrPhysX2RagdollHandle handle, OgsrPhysXRagdollInfo& info);
bool OgsrPhysXRagdollV2GetBodyMass(OgsrPhysX2RagdollHandle handle,
                                   std::uint16_t body,
                                   OgsrPhysXRagdollBodyMass& mass,
                                   std::string* error);
bool OgsrPhysXRagdollV2GetDiagnostics(OgsrPhysX2RagdollHandle handle,
                                      float referenceHeight,
                                      OgsrPhysXRagdollDiagnostics& diagnostics);
bool OgsrPhysXRagdollV2ApplyImpulse(OgsrPhysX2RagdollHandle handle,
                                    std::uint16_t body,
                                    const float worldPosition[3],
                                    const float impulse[3],
                                    std::string* error);
bool OgsrPhysXRagdollV2ApplyCentralImpulse(OgsrPhysX2RagdollHandle handle,
                                           std::uint16_t body,
                                           const float impulse[3],
                                           std::string* error);
struct OgsrPhysXRagdollReleaseAllResult
{
    std::uint32_t remaining = 0;
    std::vector<OgsrPhysX2RagdollHandle> handles;
    bool complete() const { return remaining == 0; }
};

OgsrPhysXRagdollReleaseAllResult OgsrPhysXRagdollReleaseAllV2();
// SceneDestroy is authoritative: it destroys every backend instance in the
// scene. Call this after a successful SceneDestroy to release the per-instance
// DLL resource references and invalidate backend handles. Detached wrappers are
// deleted here; game-owned wrappers are kept alive for their NPC owner and must
// be released later through OgsrPhysXRagdollRelease.
void OgsrPhysXRagdollReconcileSceneDestroyV2();
void OgsrPhysXRagdollV2ReleaseLeaseRef();
std::uint32_t OgsrPhysXRagdollV2LiveCount();
std::uint64_t OgsrPhysXRagdollV2StepIndex();

struct OgsrPhysXRagdollV2StateInfo
{
    std::uint32_t ownerId = 0;
    OgsrPhysX2RagdollHandle handle{};
    std::uint32_t publishFrames = 0;
    std::uint32_t callbackCalls = 0;
    std::uint32_t bodyCount = 0;
    std::uint32_t jointCount = 0;
};
bool OgsrPhysXRagdollV2GetStateInfo(std::uint32_t ownerId, OgsrPhysXRagdollV2StateInfo& info);

#endif // OGSR_PHYSX_RAGDOLL
