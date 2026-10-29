#define OGSR_PHYSX2_STATIC 1
#include "physics_physx/physx_ragdoll_c_v2.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(OgsrPhysX2SceneHandle) == 8, "SceneHandle size");
_Static_assert(offsetof(OgsrPhysX2SceneHandle, slot) == 0, "SceneHandle.slot");
_Static_assert(offsetof(OgsrPhysX2SceneHandle, generation) == 4, "SceneHandle.generation");

_Static_assert(sizeof(OgsrPhysX2RagdollHandle) == 16, "RagdollHandle size");
_Static_assert(offsetof(OgsrPhysX2RagdollHandle, sceneSlot) == 0, "RagdollHandle.sceneSlot");
_Static_assert(offsetof(OgsrPhysX2RagdollHandle, sceneGeneration) == 4, "RagdollHandle.sceneGeneration");
_Static_assert(offsetof(OgsrPhysX2RagdollHandle, instanceSlot) == 8, "RagdollHandle.instanceSlot");
_Static_assert(offsetof(OgsrPhysX2RagdollHandle, instanceGeneration) == 12, "RagdollHandle.instanceGeneration");

_Static_assert(sizeof(OgsrPhysX2ApiInfo) == 32, "ApiInfo size");
_Static_assert(offsetof(OgsrPhysX2ApiInfo, structSize) == 0, "ApiInfo.structSize");
_Static_assert(offsetof(OgsrPhysX2ApiInfo, magic) == 4, "ApiInfo.magic");
_Static_assert(offsetof(OgsrPhysX2ApiInfo, abiMajor) == 8, "ApiInfo.abiMajor");
_Static_assert(offsetof(OgsrPhysX2ApiInfo, abiMinor) == 12, "ApiInfo.abiMinor");
_Static_assert(offsetof(OgsrPhysX2ApiInfo, capabilityFlags) == 16, "ApiInfo.capabilityFlags");
_Static_assert(offsetof(OgsrPhysX2ApiInfo, maxScenes) == 20, "ApiInfo.maxScenes");
_Static_assert(offsetof(OgsrPhysX2ApiInfo, maxInstancesPerScene) == 24, "ApiInfo.maxInstancesPerScene");
_Static_assert(offsetof(OgsrPhysX2ApiInfo, reserved0) == 28, "ApiInfo.reserved0");

_Static_assert(sizeof(OgsrPhysX2SceneConfig) == 48, "SceneConfig size");
_Static_assert(offsetof(OgsrPhysX2SceneConfig, structSize) == 0, "SceneConfig.structSize");
_Static_assert(offsetof(OgsrPhysX2SceneConfig, fixedTimestep) == 4, "SceneConfig.fixedTimestep");
_Static_assert(offsetof(OgsrPhysX2SceneConfig, gravityY) == 8, "SceneConfig.gravityY");
_Static_assert(offsetof(OgsrPhysX2SceneConfig, workerThreads) == 12, "SceneConfig.workerThreads");
_Static_assert(offsetof(OgsrPhysX2SceneConfig, maxSubsteps) == 16, "SceneConfig.maxSubsteps");
_Static_assert(offsetof(OgsrPhysX2SceneConfig, solverPositionIterations) == 20, "SceneConfig.solverPositionIterations");
_Static_assert(offsetof(OgsrPhysX2SceneConfig, solverVelocityIterations) == 24, "SceneConfig.solverVelocityIterations");
_Static_assert(offsetof(OgsrPhysX2SceneConfig, contactOffset) == 28, "SceneConfig.contactOffset");
_Static_assert(offsetof(OgsrPhysX2SceneConfig, restOffset) == 32, "SceneConfig.restOffset");
_Static_assert(offsetof(OgsrPhysX2SceneConfig, enableStabilization) == 36, "SceneConfig.enableStabilization");
_Static_assert(offsetof(OgsrPhysX2SceneConfig, collectContacts) == 40, "SceneConfig.collectContacts");
_Static_assert(offsetof(OgsrPhysX2SceneConfig, maxInstances) == 44, "SceneConfig.maxInstances");

_Static_assert(sizeof(OgsrPhysX2RagdollInstanceConfig) == 32, "InstanceConfig size");
_Static_assert(offsetof(OgsrPhysX2RagdollInstanceConfig, structSize) == 0, "InstanceConfig.structSize");
_Static_assert(offsetof(OgsrPhysX2RagdollInstanceConfig, linearDamping) == 4, "InstanceConfig.linearDamping");
_Static_assert(offsetof(OgsrPhysX2RagdollInstanceConfig, angularDamping) == 8, "InstanceConfig.angularDamping");
_Static_assert(offsetof(OgsrPhysX2RagdollInstanceConfig, staticRootDensity) == 12, "InstanceConfig.staticRootDensity");
_Static_assert(offsetof(OgsrPhysX2RagdollInstanceConfig, promoteStaticRoot) == 16, "InstanceConfig.promoteStaticRoot");
_Static_assert(offsetof(OgsrPhysX2RagdollInstanceConfig, disableSelfCollision) == 20,
               "InstanceConfig.disableSelfCollision");
_Static_assert(offsetof(OgsrPhysX2RagdollInstanceConfig, collisionGroup) == 24, "InstanceConfig.collisionGroup");
_Static_assert(offsetof(OgsrPhysX2RagdollInstanceConfig, collisionMask) == 28, "InstanceConfig.collisionMask");

_Static_assert(sizeof(OgsrPhysX2StepResult) == 40, "StepResult size");
_Static_assert(offsetof(OgsrPhysX2StepResult, structSize) == 0, "StepResult.structSize");
_Static_assert(offsetof(OgsrPhysX2StepResult, steps) == 4, "StepResult.steps");
_Static_assert(offsetof(OgsrPhysX2StepResult, droppedSteps) == 8, "StepResult.droppedSteps");
_Static_assert(offsetof(OgsrPhysX2StepResult, reserved0) == 12, "StepResult.reserved0");
_Static_assert(offsetof(OgsrPhysX2StepResult, stepIndex) == 16, "StepResult.stepIndex");
_Static_assert(offsetof(OgsrPhysX2StepResult, frameId) == 24, "StepResult.frameId");
_Static_assert(offsetof(OgsrPhysX2StepResult, simulatedSeconds) == 32, "StepResult.simulatedSeconds");
_Static_assert(offsetof(OgsrPhysX2StepResult, droppedSeconds) == 36, "StepResult.droppedSeconds");

int main(void)
{
    OgsrPhysX2SceneHandle scene = {0u, 0u};
    OgsrPhysX2RagdollHandle ragdoll = {0u, 0u, 0u, 0u};
    return (scene.slot == 0u && ragdoll.instanceSlot == 0u) ? 0 : 1;
}
