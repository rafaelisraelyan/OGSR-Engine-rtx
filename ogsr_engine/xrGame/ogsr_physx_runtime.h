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
