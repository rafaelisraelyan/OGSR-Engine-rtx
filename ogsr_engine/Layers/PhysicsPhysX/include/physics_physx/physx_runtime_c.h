#pragma once

// C ABI for the optional runtime PhysX DLL. The game does not link PhysX
// libraries directly: it loads this DLL only when -physx_runtime is requested.

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#ifdef OGSR_PHYSX_RUNTIME_EXPORTS
#define OGSR_PHYSX_RUNTIME_API __declspec(dllexport)
#else
#define OGSR_PHYSX_RUNTIME_API __declspec(dllimport)
#endif
#else
#define OGSR_PHYSX_RUNTIME_API
#endif

#ifdef __cplusplus
extern "C"
{
#endif

typedef void* OgsrPhysXRuntimeHandle;

typedef struct OgsrPhysXRuntimeConfig
{
    float fixedTimestep;
    float gravityY;
    uint32_t workerThreads;
    uint32_t maxSubsteps;
} OgsrPhysXRuntimeConfig;

typedef struct OgsrPhysXRuntimeState
{
    float position[3];
    float rotation[4]; // x, y, z, w
    uint32_t stepCount;
    uint32_t lastSubsteps;
    uint32_t contactCount;
    uint32_t staticTriangles;
    uint32_t dynamicBodies;
    uint32_t hasContact;
} OgsrPhysXRuntimeState;

OGSR_PHYSX_RUNTIME_API OgsrPhysXRuntimeHandle OgsrPhysXRuntimeCreate(const OgsrPhysXRuntimeConfig* config,
                                                                    char* error,
                                                                    size_t errorSize);
OGSR_PHYSX_RUNTIME_API void OgsrPhysXRuntimeDestroy(OgsrPhysXRuntimeHandle handle);
OGSR_PHYSX_RUNTIME_API int OgsrPhysXRuntimeSetStaticMesh(OgsrPhysXRuntimeHandle handle,
                                                        const float* vertices,
                                                        uint32_t vertexCount,
                                                        const uint32_t* indices,
                                                        uint32_t indexCount,
                                                        char* error,
                                                        size_t errorSize);
OGSR_PHYSX_RUNTIME_API int OgsrPhysXRuntimeCreateTestBox(OgsrPhysXRuntimeHandle handle,
                                                        float x,
                                                        float y,
                                                        float z,
                                                        float halfX,
                                                        float halfY,
                                                        float halfZ,
                                                        float density,
                                                        char* error,
                                                        size_t errorSize);
OGSR_PHYSX_RUNTIME_API int OgsrPhysXRuntimeStep(OgsrPhysXRuntimeHandle handle,
                                               float frameDeltaSeconds,
                                               char* error,
                                               size_t errorSize);
OGSR_PHYSX_RUNTIME_API int OgsrPhysXRuntimeGetState(OgsrPhysXRuntimeHandle handle,
                                                    OgsrPhysXRuntimeState* state);

#ifdef __cplusplus
}
#endif
