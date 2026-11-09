#pragma once

// C ABI for the optional NPC ragdoll runtime. All payloads are plain old data;
// no STL or PhysX type crosses the DLL boundary. The game builds the definition
// in memory from the OGSR capture adapter and the DLL converts it internally.

#include "physics_physx/physx_runtime_c.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define OGSR_PHYSX_RAGDOLL_MAX_NAME 64
#define OGSR_PHYSX_RAGDOLL_MAX_BONES 256
#define OGSR_PHYSX_RAGDOLL_MAX_BODIES 128
#define OGSR_PHYSX_RAGDOLL_MAX_JOINTS 128
#define OGSR_PHYSX_RAGDOLL_MAX_SHAPES 512
#define OGSR_PHYSX_RAGDOLL_MAX_MATERIALS 32
#define OGSR_PHYSX_RAGDOLL_MAX_AXES 3

typedef struct OgsrPhysXRagdollConfig
{
    float fixedTimestep;
    float gravityY;
    uint32_t workerThreads;
    uint32_t maxSubsteps;
    uint32_t solverPositionIterations;
    uint32_t solverVelocityIterations;
    float contactOffset;
    float restOffset;
    uint32_t enableStabilization;
    float staticRootDensity;
    uint32_t promoteStaticRoot;
    float linearDamping;
    float angularDamping;
    uint32_t disableSelfCollision;
    uint32_t collectDiagnostics;
} OgsrPhysXRagdollConfig;

typedef struct OgsrPhysXRagdollAxis
{
    float axis[3];
    uint8_t space;
    uint8_t pad[3];
    float lower;
    float upper;
    float sourceLower;
    float sourceUpper;
    float spring;
    float damping;
    float zero;
} OgsrPhysXRagdollAxis;

typedef struct OgsrPhysXRagdollBone
{
    uint16_t id;
    uint16_t parent;
    uint8_t visible;
    uint8_t disposition;
    uint16_t body;
    uint32_t flags;
    float bindLocal[16]; // row-major, relative to the parent bone
    char name[OGSR_PHYSX_RAGDOLL_MAX_NAME];
} OgsrPhysXRagdollBone;

typedef struct OgsrPhysXRagdollShape
{
    uint8_t type;
    uint8_t pad[3];
    float local[16]; // row-major, body frame -> shape frame
    float halfExtents[3];
    float radius;
    float height;
    float margin;
    uint16_t materialId;
    uint16_t sourceBoneId;
    uint32_t flags;
} OgsrPhysXRagdollShape;

typedef struct OgsrPhysXRagdollBody
{
    uint16_t id;
    uint16_t boneId;
    uint16_t parentBody;
    uint8_t role;
    uint8_t isFixed;
    float mass;
    float centerOfMass[3];
    float inertia[6];
    float bindPose[16]; // row-major, parent-relative
    uint32_t firstShape;
    uint32_t shapeCount;
    uint32_t collisionGroup;
} OgsrPhysXRagdollBody;

typedef struct OgsrPhysXRagdollJoint
{
    uint16_t id;
    uint16_t boneId;
    uint16_t parentBody;
    uint16_t childBody;
    uint8_t kind;
    uint8_t axisCount;
    uint8_t breakable;
    uint8_t swingLimitMode;
    float parentFrame[16];
    float childFrame[16];
    OgsrPhysXRagdollAxis axes[OGSR_PHYSX_RAGDOLL_MAX_AXES];
    float erp;
    float cfm;
    float friction;
    float maxForce;
    float targetVelocity;
    float breakForce;
    float breakTorque;
    uint32_t flags;
} OgsrPhysXRagdollJoint;

typedef struct OgsrPhysXRagdollMaterial
{
    uint16_t id;
    uint16_t pad;
    float friction;
    float restitution;
    float spring;
    float damping;
    char name[32];
} OgsrPhysXRagdollMaterial;

typedef struct OgsrPhysXRagdollDefinition
{
    uint16_t rootBody;
    uint16_t pad;
    uint32_t boneCount;
    uint32_t bodyCount;
    uint32_t jointCount;
    uint32_t shapeCount;
    uint32_t materialCount;
    const OgsrPhysXRagdollBone* bones;
    const OgsrPhysXRagdollBody* bodies;
    const OgsrPhysXRagdollJoint* joints;
    const OgsrPhysXRagdollShape* shapes;
    const OgsrPhysXRagdollMaterial* materials;
    float entityWorld[16];
    float rootToEntity[16];
    const float* initialBodyWorld;        // bodyCount * 16
    const float* initialLinearVelocity;   // bodyCount * 3
    const float* initialAngularVelocity;  // bodyCount * 3
    const uint8_t* initialEnabled;        // bodyCount, nullable
} OgsrPhysXRagdollDefinition;

typedef struct OgsrPhysXRagdollPose
{
    float entityWorld[16];
    uint32_t bodyCount;
    uint32_t stepIndexLow;
    uint32_t stepIndexHigh;
} OgsrPhysXRagdollPose;

typedef struct OgsrPhysXRagdollInfo
{
    uint32_t bodyCount;
    uint32_t jointCount;
    uint32_t staticTriangles;
    float totalMass;
    float rootMass;
} OgsrPhysXRagdollInfo;

typedef struct OgsrPhysXRagdollBodyMass
{
    uint16_t body;
    uint16_t pad;
    float mass;
    float actorWorld[16];       // row-major
    float massWorld[16];        // row-major, actorWorld * getCMassLocalPose()
    float centerOfMassLocal[3]; // actor-local PhysX mass frame
    float worldCenterOfMass[3];
    float inertiaMassSpace[3];  // principal moments
} OgsrPhysXRagdollBodyMass;

typedef struct OgsrPhysXRagdollDiagnostics
{
    double kineticEnergy;
    double potentialEnergy;
    double totalEnergy;
    float maxAngularSpeed;
    float maxJointAnchorError;
    float maxJointLimitError;
    float maxPenetration;
    float totalContactImpulse;
    float totalMass;
    float rootMass;
    uint32_t contactCount;
    uint32_t contactFound;
    uint32_t contactPersist;
    uint32_t contactLost;
    uint32_t activeContactPairs;
    uint32_t awakeBodies;
    uint32_t dynamicBodies;
    uint32_t externalImpulses;
    uint64_t stepIndex;
} OgsrPhysXRagdollDiagnostics;

OGSR_PHYSX_RUNTIME_API OgsrPhysXRuntimeHandle OgsrPhysXRagdollRuntimeCreate(const OgsrPhysXRagdollConfig* config,
                                                                           char* error,
                                                                           size_t errorSize);
OGSR_PHYSX_RUNTIME_API void OgsrPhysXRagdollRuntimeDestroy(OgsrPhysXRuntimeHandle handle);
OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollSetStaticMesh(OgsrPhysXRuntimeHandle handle,
                                                        const float* vertices,
                                                        uint32_t vertexCount,
                                                        const uint32_t* indices,
                                                        uint32_t indexCount,
                                                        char* error,
                                                        size_t errorSize);
OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollCreate(OgsrPhysXRuntimeHandle handle,
                                                 const OgsrPhysXRagdollDefinition* definition,
                                                 char* error,
                                                 size_t errorSize);
OGSR_PHYSX_RUNTIME_API void OgsrPhysXRagdollDestroy(OgsrPhysXRuntimeHandle handle);
OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollStep(OgsrPhysXRuntimeHandle handle,
                                               float frameDeltaSeconds,
                                               char* error,
                                               size_t errorSize);
OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollGetPose(OgsrPhysXRuntimeHandle handle,
                                                  OgsrPhysXRagdollPose* pose,
                                                  float* bodyWorld,
                                                  uint32_t maxBodies);
OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollApplyImpulse(OgsrPhysXRuntimeHandle handle,
                                                       uint16_t body,
                                                       const float worldPosition[3],
                                                       const float impulse[3],
                                                       char* error,
                                                       size_t errorSize);
OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollApplyCentralImpulse(OgsrPhysXRuntimeHandle handle,
                                                              uint16_t body,
                                                              const float impulse[3],
                                                              char* error,
                                                              size_t errorSize);
OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollGetBodyMass(OgsrPhysXRuntimeHandle handle,
                                                      uint16_t body,
                                                      OgsrPhysXRagdollBodyMass* mass,
                                                      char* error,
                                                      size_t errorSize);
OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollGetInfo(OgsrPhysXRuntimeHandle handle, OgsrPhysXRagdollInfo* info);
OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollGetDiagnostics(OgsrPhysXRuntimeHandle handle,
                                                          float referenceHeight,
                                                          OgsrPhysXRagdollDiagnostics* diagnostics);

#ifdef __cplusplus
}
#endif
