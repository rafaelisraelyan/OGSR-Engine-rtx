#pragma once

// OGSR PhysX ragdoll C ABI v2 (P3.1-A foundation).
//
// This header is intentionally free of PhysX, STL and X-Ray types. It is
// usable from C11 and C++ and freezes the public v2 layout before any scene or
// instance implementation exists. P3.1-A exports only OgsrPhysX2GetApiInfo;
// the remaining v2 entry points are declared for the frozen ABI and are not
// exported by the P3.1-A DLL.
//
// The v1 ABI in physx_ragdoll_c.h is frozen and unchanged. v2 entry points
// always use the OgsrPhysX2 prefix and never reuse a v1 name.

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#ifdef OGSR_PHYSX_RUNTIME_EXPORTS
#define OGSR_PHYSX2_API __declspec(dllexport)
#elif defined(OGSR_PHYSX2_STATIC)
#define OGSR_PHYSX2_API
#else
#define OGSR_PHYSX2_API __declspec(dllimport)
#endif
#define OGSR_PHYSX2_CALL __cdecl
#else
#define OGSR_PHYSX2_API
#define OGSR_PHYSX2_CALL
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#define OGSR_PHYSX2_ABI_MAJOR 2u
#define OGSR_PHYSX2_ABI_MINOR 0u
#define OGSR_PHYSX2_API_INFO_MAGIC 0x4F505832u /* "OPX2" */
#define OGSR_PHYSX2_API_INFO_V0_SIZE 32u
#define OGSR_PHYSX2_SCENE_CAPACITY 4u
#define OGSR_PHYSX2_INSTANCE_CAPACITY 64u

#define OGSR_PHYSX2_CAP_SCENE (1u << 0)
#define OGSR_PHYSX2_CAP_MULTI_INSTANCE (1u << 1)
#define OGSR_PHYSX2_CAP_INSTANCE_HANDLES (1u << 2)
#define OGSR_PHYSX2_CAP_STATIC_FRAGMENTS (1u << 3)
#define OGSR_PHYSX2_CAP_CONTACT_ATTRIBUTION (1u << 4)
#define OGSR_PHYSX2_CAP_FRAME_IDEMPOTENT_STEP (1u << 5)
#define OGSR_PHYSX2_CAP_PER_INSTANCE_DIAGNOSTICS (1u << 6)
#define OGSR_PHYSX2_CAP_CONFIGURABLE_WORKERS (1u << 7) /* optional */

#define OGSR_PHYSX2_CAP_REQUIRED_P3_1                                                                                  \
    (OGSR_PHYSX2_CAP_SCENE | OGSR_PHYSX2_CAP_MULTI_INSTANCE | OGSR_PHYSX2_CAP_INSTANCE_HANDLES |                       \
     OGSR_PHYSX2_CAP_STATIC_FRAGMENTS | OGSR_PHYSX2_CAP_CONTACT_ATTRIBUTION |                                           \
     OGSR_PHYSX2_CAP_FRAME_IDEMPOTENT_STEP | OGSR_PHYSX2_CAP_PER_INSTANCE_DIAGNOSTICS)

/* PxFilterData word0/word1/word3 policy values from the P3.0 ADR. */
#define OGSR_PHYSX2_GROUP_RAGDOLL (1u << 0)
#define OGSR_PHYSX2_GROUP_STATIC (1u << 1)
#define OGSR_PHYSX2_GROUP_ODE_PROXY (1u << 2) /* reserved; not in the P3.1 scene */
#define OGSR_PHYSX2_FILTER_RAGDOLL (1u << 0)
#define OGSR_PHYSX2_FILTER_STATIC (1u << 1)
#define OGSR_PHYSX2_FILTER_DISABLE_SELF_COLLISION (1u << 2)

/* v1 POD payloads are declared here to keep this header standalone. Their
 * layout remains defined by physx_ragdoll_c.h and must not change. */
typedef struct OgsrPhysXRagdollDefinition OgsrPhysXRagdollDefinition;
typedef struct OgsrPhysXRagdollPose OgsrPhysXRagdollPose;
typedef struct OgsrPhysXRagdollBodyMass OgsrPhysXRagdollBodyMass;
typedef struct OgsrPhysXRagdollInfo OgsrPhysXRagdollInfo;
typedef struct OgsrPhysXRagdollDiagnostics OgsrPhysXRagdollDiagnostics;

#pragma pack(push, 8)

typedef struct OgsrPhysX2SceneHandle
{
    uint32_t slot;
    uint32_t generation;
} OgsrPhysX2SceneHandle;

typedef struct OgsrPhysX2RagdollHandle
{
    uint32_t sceneSlot;
    uint32_t sceneGeneration;
    uint32_t instanceSlot;
    uint32_t instanceGeneration;
} OgsrPhysX2RagdollHandle;

typedef struct OgsrPhysX2ApiInfo
{
    uint32_t structSize; /* in: caller capacity; out: supported v2.0 size (32) */
    uint32_t magic;      /* out: OGSR_PHYSX2_API_INFO_MAGIC */
    uint32_t abiMajor;   /* out: 2 */
    uint32_t abiMinor;   /* out: 0 */
    uint32_t capabilityFlags;
    uint32_t maxScenes;
    uint32_t maxInstancesPerScene;
    uint32_t reserved0; /* must be 0 */
} OgsrPhysX2ApiInfo;

typedef struct OgsrPhysX2SceneConfig
{
    uint32_t structSize;
    float fixedTimestep;               /* 0.01 */
    float gravityY;                    /* -19.62 */
    uint32_t workerThreads;            /* future; 0 = backend default */
    uint32_t maxSubsteps;              /* 4 */
    uint32_t solverPositionIterations; /* 16 */
    uint32_t solverVelocityIterations; /* 4 */
    float contactOffset;
    float restOffset;
    uint32_t enableStabilization;
    uint32_t collectContacts;
    uint32_t maxInstances; /* 0 = default */
} OgsrPhysX2SceneConfig;

typedef struct OgsrPhysX2RagdollInstanceConfig
{
    uint32_t structSize;
    float linearDamping;     /* 0.05 */
    float angularDamping;    /* 0.2 */
    float staticRootDensity; /* 300 */
    uint32_t promoteStaticRoot;
    uint32_t disableSelfCollision;
    uint32_t collisionGroup; /* default OGSR_PHYSX2_GROUP_RAGDOLL */
    uint32_t collisionMask;  /* default RAGDOLL|STATIC */
} OgsrPhysX2RagdollInstanceConfig;

typedef struct OgsrPhysX2StepResult
{
    uint32_t structSize;
    uint32_t steps;
    uint32_t droppedSteps;
    uint32_t reserved0;
    uint64_t stepIndex;
    uint64_t frameId;
    float simulatedSeconds;
    float droppedSeconds;
} OgsrPhysX2StepResult;

#pragma pack(pop)

/* P3.1-A exports only OgsrPhysX2GetApiInfo. The other declarations freeze the
 * ABI for P3.1-B/C; the P3.1-A DLL does not export them. */
OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2GetApiInfo(OgsrPhysX2ApiInfo* info);

OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2SceneCreate(const OgsrPhysX2SceneConfig* config,
                                                           OgsrPhysX2SceneHandle* outScene, char* error,
                                                           size_t errorSize);
OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2SceneDestroy(OgsrPhysX2SceneHandle scene, char* error,
                                                           size_t errorSize);
OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2SceneIsAlive(OgsrPhysX2SceneHandle scene);
OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2SceneSetStaticMesh(OgsrPhysX2SceneHandle scene, const float* vertices,
                                                                 uint32_t vertexCount, const uint32_t* indices,
                                                                 uint32_t indexCount, char* error, size_t errorSize);
OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2SceneAddStaticFragment(OgsrPhysX2SceneHandle scene, uint64_t key,
                                                                     const float* vertices, uint32_t vertexCount,
                                                                     const uint32_t* indices, uint32_t indexCount,
                                                                     char* error, size_t errorSize);
OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2SceneStep(OgsrPhysX2SceneHandle scene, float deltaSeconds,
                                                        uint64_t frameId, OgsrPhysX2StepResult* result, char* error,
                                                        size_t errorSize);

OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollCreate(OgsrPhysX2SceneHandle scene,
                                                            const OgsrPhysXRagdollDefinition* definition,
                                                            const OgsrPhysX2RagdollInstanceConfig* config,
                                                            OgsrPhysX2RagdollHandle* outHandle, char* error,
                                                            size_t errorSize);
OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollDestroy(OgsrPhysX2RagdollHandle handle, char* error,
                                                             size_t errorSize);
OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollIsAlive(OgsrPhysX2RagdollHandle handle);
OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollGetPose(OgsrPhysX2RagdollHandle handle,
                                                             OgsrPhysXRagdollPose* pose, float* bodyWorld,
                                                             uint32_t maxBodies);
OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollApplyImpulse(OgsrPhysX2RagdollHandle handle, uint16_t body,
                                                                  const float worldPosition[3],
                                                                  const float impulse[3], char* error,
                                                                  size_t errorSize);
OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollApplyCentralImpulse(OgsrPhysX2RagdollHandle handle,
                                                                         uint16_t body, const float impulse[3],
                                                                         char* error, size_t errorSize);
OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollGetBodyMass(OgsrPhysX2RagdollHandle handle, uint16_t body,
                                                                 OgsrPhysXRagdollBodyMass* mass, char* error,
                                                                 size_t errorSize);
OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollGetInfo(OgsrPhysX2RagdollHandle handle,
                                                             OgsrPhysXRagdollInfo* info);
OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollGetDiagnostics(OgsrPhysX2RagdollHandle handle,
                                                                    float referenceHeight,
                                                                    OgsrPhysXRagdollDiagnostics* diagnostics);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define OGSR_PHYSX2_STATIC_ASSERT(condition, message) static_assert(condition, message)
#else
#define OGSR_PHYSX2_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif

OGSR_PHYSX2_STATIC_ASSERT(sizeof(OgsrPhysX2SceneHandle) == 8, "v2 SceneHandle size");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2SceneHandle, slot) == 0, "v2 SceneHandle.slot");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2SceneHandle, generation) == 4, "v2 SceneHandle.generation");

OGSR_PHYSX2_STATIC_ASSERT(sizeof(OgsrPhysX2RagdollHandle) == 16, "v2 RagdollHandle size");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2RagdollHandle, sceneSlot) == 0, "v2 RagdollHandle.sceneSlot");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2RagdollHandle, sceneGeneration) == 4, "v2 RagdollHandle.sceneGeneration");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2RagdollHandle, instanceSlot) == 8, "v2 RagdollHandle.instanceSlot");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2RagdollHandle, instanceGeneration) == 12,
                          "v2 RagdollHandle.instanceGeneration");

OGSR_PHYSX2_STATIC_ASSERT(sizeof(OgsrPhysX2ApiInfo) == 32, "v2 ApiInfo size");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2ApiInfo, structSize) == 0, "v2 ApiInfo.structSize");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2ApiInfo, magic) == 4, "v2 ApiInfo.magic");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2ApiInfo, abiMajor) == 8, "v2 ApiInfo.abiMajor");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2ApiInfo, abiMinor) == 12, "v2 ApiInfo.abiMinor");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2ApiInfo, capabilityFlags) == 16, "v2 ApiInfo.capabilityFlags");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2ApiInfo, maxScenes) == 20, "v2 ApiInfo.maxScenes");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2ApiInfo, maxInstancesPerScene) == 24,
                          "v2 ApiInfo.maxInstancesPerScene");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2ApiInfo, reserved0) == 28, "v2 ApiInfo.reserved0");

OGSR_PHYSX2_STATIC_ASSERT(sizeof(OgsrPhysX2SceneConfig) == 48, "v2 SceneConfig size");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2SceneConfig, structSize) == 0, "v2 SceneConfig.structSize");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2SceneConfig, fixedTimestep) == 4, "v2 SceneConfig.fixedTimestep");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2SceneConfig, gravityY) == 8, "v2 SceneConfig.gravityY");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2SceneConfig, workerThreads) == 12, "v2 SceneConfig.workerThreads");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2SceneConfig, maxSubsteps) == 16, "v2 SceneConfig.maxSubsteps");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2SceneConfig, solverPositionIterations) == 20,
                          "v2 SceneConfig.solverPositionIterations");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2SceneConfig, solverVelocityIterations) == 24,
                          "v2 SceneConfig.solverVelocityIterations");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2SceneConfig, contactOffset) == 28, "v2 SceneConfig.contactOffset");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2SceneConfig, restOffset) == 32, "v2 SceneConfig.restOffset");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2SceneConfig, enableStabilization) == 36,
                          "v2 SceneConfig.enableStabilization");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2SceneConfig, collectContacts) == 40, "v2 SceneConfig.collectContacts");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2SceneConfig, maxInstances) == 44, "v2 SceneConfig.maxInstances");

OGSR_PHYSX2_STATIC_ASSERT(sizeof(OgsrPhysX2RagdollInstanceConfig) == 32, "v2 InstanceConfig size");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2RagdollInstanceConfig, structSize) == 0,
                          "v2 InstanceConfig.structSize");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2RagdollInstanceConfig, linearDamping) == 4,
                          "v2 InstanceConfig.linearDamping");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2RagdollInstanceConfig, angularDamping) == 8,
                          "v2 InstanceConfig.angularDamping");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2RagdollInstanceConfig, staticRootDensity) == 12,
                          "v2 InstanceConfig.staticRootDensity");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2RagdollInstanceConfig, promoteStaticRoot) == 16,
                          "v2 InstanceConfig.promoteStaticRoot");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2RagdollInstanceConfig, disableSelfCollision) == 20,
                          "v2 InstanceConfig.disableSelfCollision");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2RagdollInstanceConfig, collisionGroup) == 24,
                          "v2 InstanceConfig.collisionGroup");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2RagdollInstanceConfig, collisionMask) == 28,
                          "v2 InstanceConfig.collisionMask");

OGSR_PHYSX2_STATIC_ASSERT(sizeof(OgsrPhysX2StepResult) == 40, "v2 StepResult size");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2StepResult, structSize) == 0, "v2 StepResult.structSize");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2StepResult, steps) == 4, "v2 StepResult.steps");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2StepResult, droppedSteps) == 8, "v2 StepResult.droppedSteps");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2StepResult, reserved0) == 12, "v2 StepResult.reserved0");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2StepResult, stepIndex) == 16, "v2 StepResult.stepIndex");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2StepResult, frameId) == 24, "v2 StepResult.frameId");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2StepResult, simulatedSeconds) == 32,
                          "v2 StepResult.simulatedSeconds");
OGSR_PHYSX2_STATIC_ASSERT(offsetof(OgsrPhysX2StepResult, droppedSeconds) == 36, "v2 StepResult.droppedSeconds");
