#include "physics_physx/physx_ragdoll_c_v2.h"

// P3.1-A exports only the ABI information entry point. Scene/instance entry
// points are declared in the frozen header but are intentionally not exported
// until P3.1-B/C implements them.
extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2GetApiInfo(OgsrPhysX2ApiInfo* info)
{
    if (!info || info->structSize < OGSR_PHYSX2_API_INFO_V0_SIZE)
        return 0;

    // Never write beyond the caller-declared capacity. structSize is 32 for
    // v2.0; larger callers keep their extension bytes untouched.
    info->structSize = OGSR_PHYSX2_API_INFO_V0_SIZE;
    info->magic = OGSR_PHYSX2_API_INFO_MAGIC;
    info->abiMajor = OGSR_PHYSX2_ABI_MAJOR;
    info->abiMinor = OGSR_PHYSX2_ABI_MINOR;
    info->capabilityFlags = 0; // P3.1-A: registry foundation only, no production scene capabilities.
    info->maxScenes = OGSR_PHYSX2_SCENE_CAPACITY;
    info->maxInstancesPerScene = OGSR_PHYSX2_INSTANCE_CAPACITY;
    info->reserved0 = 0;
    return 1;
}
