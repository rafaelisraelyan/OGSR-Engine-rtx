#pragma once

#ifdef OGSR_PHYSX_RUNTIME
void OgsrPhysXRuntimeOnLevelStart();
void OgsrPhysXRuntimeOnFrame();
void OgsrPhysXRuntimeOnLevelUnload();
#endif

#ifdef OGSR_PHYSX_RAGDOLL

#include "physics_physx/physx_ragdoll_c.h"

#include <cstdint>
#include <string>

bool OgsrPhysXRagdollRequested();
bool OgsrPhysXRagdollTargetRequested();
bool OgsrPhysXRagdollOdeSmokeRequested();
bool OgsrPhysXRagdollMultiRequested();
bool OgsrPhysXRagdollAllHumansRequested();
bool OgsrPhysXRagdollStaticTilesRequested();
bool OgsrPhysXRagdollSelfCollisionRequested();
bool OgsrPhysXRagdollColliderVisRequested();
bool OgsrPhysXRagdollStaticTilesSmokeRequested();
bool OgsrPhysXRagdollStaticTilesBoundarySmokeRequested();
bool OgsrPhysXRagdollG2SingleSmokeRequested();
bool OgsrPhysXRagdollG3RealHitRequested();
const char* OgsrPhysXRagdollG3Weapon();
const char* OgsrPhysXRagdollG3AimBone();
float OgsrPhysXRagdollG3TargetOffsetX();
float OgsrPhysXRagdollG3TargetOffsetZ();
std::uint32_t OgsrPhysXRagdollG3MaxShots();
std::uint32_t OgsrPhysXRagdollMaxRagdolls();
float OgsrPhysXRagdollHitImpulseScale();
float OgsrPhysXRagdollHitImpulseMax();
bool OgsrPhysXRagdollDiagRequested();
bool OgsrPhysXRagdollReloadRequested();
void OgsrPhysXRagdollSmokeReset();
void OgsrPhysXRagdollRequestLevelDisconnect();
bool OgsrPhysXRagdollMatches(const char* name, const char* section);
bool OgsrPhysXRagdollForceFree();
bool OgsrPhysXRagdollSmokeRequested();
bool OgsrPhysXRagdollVisualReplaceRequested();
bool OgsrPhysXRagdollVisualReplaceOdeRequested();
bool OgsrPhysXRagdollRuntimeAlive();
bool OgsrPhysXRagdollRuntimeHasInstance();

bool OgsrPhysXRagdollRuntimeSetStaticMesh(const float* vertices,
                                          uint32_t vertexCount,
                                          const uint32_t* indices,
                                          uint32_t indexCount,
                                          std::string* error);
bool OgsrPhysXRagdollRuntimeCreateInstance(const OgsrPhysXRagdollDefinition& definition,
                                           std::string* error,
                                           uint32_t& bodyCount,
                                           uint32_t& jointCount);
void OgsrPhysXRagdollRuntimeDestroyInstance();
bool OgsrPhysXRagdollRuntimeGetPose(OgsrPhysXRagdollPose& pose, float* bodyWorld, uint32_t maxBodies);
bool OgsrPhysXRagdollRuntimeGetInfo(OgsrPhysXRagdollInfo& info);
bool OgsrPhysXRagdollRuntimeGetDiagnostics(float referenceHeight, OgsrPhysXRagdollDiagnostics& diagnostics);
bool OgsrPhysXRagdollRuntimeStep(float frameDeltaSeconds, std::string* error);
bool OgsrPhysXRagdollRuntimeApplyImpulse(uint16_t body,
                                         const float worldPosition[3],
                                         const float impulse[3],
                                         std::string* error);
bool OgsrPhysXRagdollRuntimeApplyCentralImpulse(uint16_t body, const float impulse[3], std::string* error);
bool OgsrPhysXRagdollRuntimeGetBodyMass(uint16_t body,
                                        OgsrPhysXRagdollBodyMass& mass,
                                        std::string* error);

#endif
