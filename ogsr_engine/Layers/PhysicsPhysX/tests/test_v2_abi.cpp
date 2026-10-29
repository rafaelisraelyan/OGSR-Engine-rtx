#include "physics_physx/physx_ragdoll_c.h"
#include "physics_physx/physx_ragdoll_c_v2.h"
#include "physics_physx/physx_ragdoll_v2_loader.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <windows.h>

namespace
{
int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", message);
    }
}

struct ExtendedInfo
{
    OgsrPhysX2ApiInfo info;
    std::uint32_t tail[2];
};

void testLayout()
{
    check(sizeof(OgsrPhysX2SceneHandle) == 8, "SceneHandle size");
    check(offsetof(OgsrPhysX2SceneHandle, slot) == 0, "SceneHandle.slot offset");
    check(offsetof(OgsrPhysX2SceneHandle, generation) == 4, "SceneHandle.generation offset");

    check(sizeof(OgsrPhysX2RagdollHandle) == 16, "RagdollHandle size");
    check(offsetof(OgsrPhysX2RagdollHandle, sceneSlot) == 0, "RagdollHandle.sceneSlot offset");
    check(offsetof(OgsrPhysX2RagdollHandle, sceneGeneration) == 4, "RagdollHandle.sceneGeneration offset");
    check(offsetof(OgsrPhysX2RagdollHandle, instanceSlot) == 8, "RagdollHandle.instanceSlot offset");
    check(offsetof(OgsrPhysX2RagdollHandle, instanceGeneration) == 12, "RagdollHandle.instanceGeneration offset");

    check(sizeof(OgsrPhysX2ApiInfo) == 32, "ApiInfo size");
    check(offsetof(OgsrPhysX2ApiInfo, structSize) == 0, "ApiInfo.structSize offset");
    check(offsetof(OgsrPhysX2ApiInfo, magic) == 4, "ApiInfo.magic offset");
    check(offsetof(OgsrPhysX2ApiInfo, abiMajor) == 8, "ApiInfo.abiMajor offset");
    check(offsetof(OgsrPhysX2ApiInfo, abiMinor) == 12, "ApiInfo.abiMinor offset");
    check(offsetof(OgsrPhysX2ApiInfo, capabilityFlags) == 16, "ApiInfo.capabilityFlags offset");
    check(offsetof(OgsrPhysX2ApiInfo, maxScenes) == 20, "ApiInfo.maxScenes offset");
    check(offsetof(OgsrPhysX2ApiInfo, maxInstancesPerScene) == 24, "ApiInfo.maxInstancesPerScene offset");
    check(offsetof(OgsrPhysX2ApiInfo, reserved0) == 28, "ApiInfo.reserved0 offset");

    check(sizeof(OgsrPhysX2SceneConfig) == 48, "SceneConfig size");
    check(offsetof(OgsrPhysX2SceneConfig, maxInstances) == 44, "SceneConfig.maxInstances offset");
    check(sizeof(OgsrPhysX2RagdollInstanceConfig) == 32, "InstanceConfig size");
    check(offsetof(OgsrPhysX2RagdollInstanceConfig, collisionMask) == 28, "InstanceConfig.collisionMask offset");
    check(sizeof(OgsrPhysX2StepResult) == 40, "StepResult size");
    check(offsetof(OgsrPhysX2StepResult, stepIndex) == 16, "StepResult.stepIndex offset");
    check(offsetof(OgsrPhysX2StepResult, frameId) == 24, "StepResult.frameId offset");
    check(offsetof(OgsrPhysX2StepResult, simulatedSeconds) == 32, "StepResult.simulatedSeconds offset");
    check(offsetof(OgsrPhysX2StepResult, droppedSeconds) == 36, "StepResult.droppedSeconds offset");
}

void testGetApiInfoContract()
{
    OgsrPhysX2ApiInfo info{};
    info.structSize = OGSR_PHYSX2_API_INFO_V0_SIZE;
    check(OgsrPhysX2GetApiInfo(&info) == 1, "GetApiInfo succeeds for 32 bytes");
    check(info.structSize == OGSR_PHYSX2_API_INFO_V0_SIZE, "GetApiInfo reports the supported size");
    check(info.magic == OGSR_PHYSX2_API_INFO_MAGIC, "GetApiInfo magic");
    check(info.abiMajor == OGSR_PHYSX2_ABI_MAJOR, "GetApiInfo ABI major");
    check(info.abiMinor == OGSR_PHYSX2_ABI_MINOR, "GetApiInfo ABI minor");
    check(info.capabilityFlags == OGSR_PHYSX2_CAP_REQUIRED_P3_1,
          "P3.1-B advertises the implemented production capabilities");
    check(info.maxScenes == OGSR_PHYSX2_SCENE_CAPACITY, "GetApiInfo scene capacity");
    check(info.maxInstancesPerScene == OGSR_PHYSX2_INSTANCE_CAPACITY, "GetApiInfo instance capacity");
    check(info.reserved0 == 0, "GetApiInfo reserved field is zero");

    OgsrPhysX2ApiInfo tooSmall{};
    tooSmall.structSize = OGSR_PHYSX2_API_INFO_V0_SIZE - 1;
    tooSmall.magic = 0xAAAAAAAAu;
    tooSmall.abiMajor = 0xBBBBBBBBu;
    tooSmall.abiMinor = 0xCCCCCCCCu;
    tooSmall.capabilityFlags = 0xDDDDDDDDu;
    tooSmall.maxScenes = 0xEEEEEEEEu;
    tooSmall.maxInstancesPerScene = 0x11111111u;
    tooSmall.reserved0 = 0x22222222u;
    check(OgsrPhysX2GetApiInfo(&tooSmall) == 0, "GetApiInfo rejects structSize < 32");
    check(tooSmall.structSize == 31 && tooSmall.magic == 0xAAAAAAAAu && tooSmall.abiMajor == 0xBBBBBBBBu &&
              tooSmall.abiMinor == 0xCCCCCCCCu && tooSmall.capabilityFlags == 0xDDDDDDDDu &&
              tooSmall.maxScenes == 0xEEEEEEEEu && tooSmall.maxInstancesPerScene == 0x11111111u &&
              tooSmall.reserved0 == 0x22222222u,
          "GetApiInfo does not write when structSize < 32");

    ExtendedInfo extended{};
    extended.info.structSize = sizeof(ExtendedInfo);
    extended.tail[0] = 0x13572468u;
    extended.tail[1] = 0x24681357u;
    check(OgsrPhysX2GetApiInfo(&extended.info) == 1, "GetApiInfo accepts structSize > 32");
    check(extended.info.structSize == OGSR_PHYSX2_API_INFO_V0_SIZE, "GetApiInfo clamps reported size to 32");
    check(extended.tail[0] == 0x13572468u && extended.tail[1] == 0x24681357u,
          "GetApiInfo leaves extension bytes untouched");

    check(OgsrPhysX2GetApiInfo(nullptr) == 0, "GetApiInfo rejects null");
}

void testRealDllExports()
{
    HMODULE module = LoadLibraryA("ogsr_physx_runtime.dll");
    check(module != nullptr, "LoadLibrary(ogsr_physx_runtime.dll)");
    if (!module)
    {
        std::printf("  LoadLibrary error=%lu\n", GetLastError());
        return;
    }

    const char* const v1Names[] = {
        "OgsrPhysXRagdollRuntimeCreate", "OgsrPhysXRagdollRuntimeDestroy", "OgsrPhysXRagdollSetStaticMesh",
        "OgsrPhysXRagdollCreate",        "OgsrPhysXRagdollDestroy",        "OgsrPhysXRagdollStep",
        "OgsrPhysXRagdollGetPose",       "OgsrPhysXRagdollApplyImpulse",   "OgsrPhysXRagdollApplyCentralImpulse",
        "OgsrPhysXRagdollGetBodyMass",   "OgsrPhysXRagdollGetInfo",        "OgsrPhysXRagdollGetDiagnostics",
    };
    for (const char* name : v1Names)
        check(GetProcAddress(module, name) != nullptr, name);

    const char* const v2Names[] = {
        "OgsrPhysX2GetApiInfo",       "OgsrPhysX2SceneCreate",       "OgsrPhysX2SceneDestroy",
        "OgsrPhysX2SceneIsAlive",     "OgsrPhysX2SceneSetStaticMesh", "OgsrPhysX2SceneAddStaticFragment",
        "OgsrPhysX2SceneStep",        "OgsrPhysX2RagdollCreate",     "OgsrPhysX2RagdollDestroy",
        "OgsrPhysX2RagdollIsAlive",   "OgsrPhysX2RagdollGetPose",    "OgsrPhysX2RagdollApplyImpulse",
        "OgsrPhysX2RagdollApplyCentralImpulse", "OgsrPhysX2RagdollGetBodyMass",
        "OgsrPhysX2RagdollGetInfo",   "OgsrPhysX2RagdollGetDiagnostics",
    };
    for (const char* name : v2Names)
        check(GetProcAddress(module, name) != nullptr, name);

    using GetApiInfoFn = int(OGSR_PHYSX2_CALL*)(OgsrPhysX2ApiInfo*);
    const GetApiInfoFn getApiInfo =
        reinterpret_cast<GetApiInfoFn>(GetProcAddress(module, "OgsrPhysX2GetApiInfo"));
    check(getApiInfo != nullptr, "v2 GetApiInfo resolves");
    if (getApiInfo)
    {
        OgsrPhysX2ApiInfo info{};
        info.structSize = OGSR_PHYSX2_API_INFO_V0_SIZE;
        check(getApiInfo(&info) == 1, "resolved v2 GetApiInfo succeeds");
        check(info.capabilityFlags == OGSR_PHYSX2_CAP_REQUIRED_P3_1,
              "resolved v2 GetApiInfo advertises production capabilities");
    }

    physics_physx::V2Requirements requirements;
    requirements.minimumMinor = 0;
    requirements.requiredCapabilities = OGSR_PHYSX2_CAP_REQUIRED_P3_1;
    requirements.minimumScenes = 1;
    requirements.minimumInstances = 2;
    const physics_physx::V2NegotiationResult result =
        physics_physx::negotiateV2FromModule(module, requirements);
    check(result.status == physics_physx::V2NegotiationStatus::Ready,
          "actual P3.1-B DLL negotiates Ready");
    check(result.api.getApiInfo != nullptr && result.api.sceneCreate != nullptr &&
              result.api.sceneStep != nullptr && result.api.ragdollCreate != nullptr &&
              result.api.ragdollGetDiagnostics != nullptr,
          "actual DLL negotiation yields the complete v2 table");

    FreeLibrary(module);
}

} // namespace

int main()
{
    testLayout();
    testGetApiInfoContract();
    testRealDllExports();

    std::printf("physics_physx_v2_abi tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
