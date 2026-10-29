#include "physics_physx/physx_ragdoll_c_v2.h"

#include <cstddef>
#include <cstdint>

// Test-only v2 DLL. MOCK_V2_MISSING_SCENE_CREATE removes one mandatory export
// so the loader's fail-closed path can be exercised through a real DLL.

extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2GetApiInfo(OgsrPhysX2ApiInfo* info)
{
    if (!info || info->structSize < OGSR_PHYSX2_API_INFO_V0_SIZE)
        return 0;
    info->structSize = OGSR_PHYSX2_API_INFO_V0_SIZE;
    info->magic = OGSR_PHYSX2_API_INFO_MAGIC;
    info->abiMajor = OGSR_PHYSX2_ABI_MAJOR;
    info->abiMinor = OGSR_PHYSX2_ABI_MINOR;
    info->capabilityFlags = OGSR_PHYSX2_CAP_REQUIRED_P3_1;
    info->maxScenes = OGSR_PHYSX2_SCENE_CAPACITY;
    info->maxInstancesPerScene = OGSR_PHYSX2_INSTANCE_CAPACITY;
    info->reserved0 = 0;
    return 1;
}

#ifndef MOCK_V2_MISSING_SCENE_CREATE
extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2SceneCreate(const OgsrPhysX2SceneConfig*, OgsrPhysX2SceneHandle*, char*,
                                                      std::size_t)
{
    return 0;
}
#endif

extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2SceneDestroy(OgsrPhysX2SceneHandle, char*, std::size_t) { return 0; }
extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2SceneIsAlive(OgsrPhysX2SceneHandle) { return 0; }
extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2SceneSetStaticMesh(OgsrPhysX2SceneHandle, const float*, std::uint32_t,
                                                              const std::uint32_t*, std::uint32_t, char*, std::size_t)
{
    return 0;
}
extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2SceneAddStaticFragment(OgsrPhysX2SceneHandle, std::uint64_t, const float*,
                                                                 std::uint32_t, const std::uint32_t*, std::uint32_t,
                                                                 char*, std::size_t)
{
    return 0;
}
extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2SceneStep(OgsrPhysX2SceneHandle, float, std::uint64_t,
                                                    OgsrPhysX2StepResult*, char*, std::size_t)
{
    return 0;
}
extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2RagdollCreate(OgsrPhysX2SceneHandle,
                                                        const OgsrPhysXRagdollDefinition*,
                                                        const OgsrPhysX2RagdollInstanceConfig*,
                                                        OgsrPhysX2RagdollHandle*, char*, std::size_t)
{
    return 0;
}
extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2RagdollDestroy(OgsrPhysX2RagdollHandle, char*, std::size_t) { return 0; }
extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2RagdollIsAlive(OgsrPhysX2RagdollHandle) { return 0; }
extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2RagdollGetPose(OgsrPhysX2RagdollHandle, OgsrPhysXRagdollPose*, float*,
                                                          std::uint32_t)
{
    return 0;
}
extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2RagdollApplyImpulse(OgsrPhysX2RagdollHandle, std::uint16_t,
                                                              const float[3], const float[3], char*, std::size_t)
{
    return 0;
}
extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2RagdollApplyCentralImpulse(OgsrPhysX2RagdollHandle, std::uint16_t,
                                                                     const float[3], char*, std::size_t)
{
    return 0;
}
extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2RagdollGetBodyMass(OgsrPhysX2RagdollHandle, std::uint16_t,
                                                             OgsrPhysXRagdollBodyMass*, char*, std::size_t)
{
    return 0;
}
extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2RagdollGetInfo(OgsrPhysX2RagdollHandle, OgsrPhysXRagdollInfo*) { return 0; }
extern "C" int OGSR_PHYSX2_CALL OgsrPhysX2RagdollGetDiagnostics(OgsrPhysX2RagdollHandle, float,
                                                                OgsrPhysXRagdollDiagnostics*)
{
    return 0;
}
