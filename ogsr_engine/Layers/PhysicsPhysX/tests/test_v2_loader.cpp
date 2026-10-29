#include "physics_physx/physx_ragdoll_v2_loader.h"

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

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

const char* const kAllV2Symbols[] = {
    "OgsrPhysX2GetApiInfo",
    "OgsrPhysX2SceneCreate",
    "OgsrPhysX2SceneDestroy",
    "OgsrPhysX2SceneIsAlive",
    "OgsrPhysX2SceneSetStaticMesh",
    "OgsrPhysX2SceneAddStaticFragment",
    "OgsrPhysX2SceneStep",
    "OgsrPhysX2RagdollCreate",
    "OgsrPhysX2RagdollDestroy",
    "OgsrPhysX2RagdollIsAlive",
    "OgsrPhysX2RagdollGetPose",
    "OgsrPhysX2RagdollApplyImpulse",
    "OgsrPhysX2RagdollApplyCentralImpulse",
    "OgsrPhysX2RagdollGetBodyMass",
    "OgsrPhysX2RagdollGetInfo",
    "OgsrPhysX2RagdollGetDiagnostics",
};

struct FakeApi
{
    OgsrPhysX2ApiInfo info{};
    bool provideInfo = true;
    bool infoReturnsSuccess = true;
    std::uint32_t reportedStructSize = OGSR_PHYSX2_API_INFO_V0_SIZE;
    std::set<std::string> symbols;
    int infoCalls = 0;
    int sceneCreateCalls = 0;
    int sceneStepCalls = 0;
    int ragdollCreateCalls = 0;
    std::vector<std::string> resolved;

    FakeApi()
    {
        info.structSize = OGSR_PHYSX2_API_INFO_V0_SIZE;
        info.magic = OGSR_PHYSX2_API_INFO_MAGIC;
        info.abiMajor = OGSR_PHYSX2_ABI_MAJOR;
        info.abiMinor = OGSR_PHYSX2_ABI_MINOR;
        info.capabilityFlags = OGSR_PHYSX2_CAP_REQUIRED_P3_1;
        info.maxScenes = OGSR_PHYSX2_SCENE_CAPACITY;
        info.maxInstancesPerScene = OGSR_PHYSX2_INSTANCE_CAPACITY;
        info.reserved0 = 0;
        for (const char* symbol : kAllV2Symbols)
            symbols.insert(symbol);
    }
};

FakeApi* g_fake = nullptr;

int OGSR_PHYSX2_CALL fakeGetApiInfo(OgsrPhysX2ApiInfo* out)
{
    ++g_fake->infoCalls;
    if (!out || out->structSize < OGSR_PHYSX2_API_INFO_V0_SIZE)
        return 0;
    if (!g_fake->infoReturnsSuccess)
        return 0;
    *out = g_fake->info;
    out->structSize = g_fake->reportedStructSize;
    return 1;
}

int OGSR_PHYSX2_CALL fakeSceneCreate(const OgsrPhysX2SceneConfig*, OgsrPhysX2SceneHandle*, char*, std::size_t)
{
    ++g_fake->sceneCreateCalls;
    return 0;
}

int OGSR_PHYSX2_CALL fakeSceneDestroy(OgsrPhysX2SceneHandle, char*, std::size_t) { return 0; }
int OGSR_PHYSX2_CALL fakeSceneIsAlive(OgsrPhysX2SceneHandle) { return 0; }
int OGSR_PHYSX2_CALL fakeSceneSetStaticMesh(OgsrPhysX2SceneHandle, const float*, std::uint32_t,
                                             const std::uint32_t*, std::uint32_t, char*, std::size_t)
{
    return 0;
}
int OGSR_PHYSX2_CALL fakeSceneAddStaticFragment(OgsrPhysX2SceneHandle, std::uint64_t, const float*, std::uint32_t,
                                                const std::uint32_t*, std::uint32_t, char*, std::size_t)
{
    return 0;
}
int OGSR_PHYSX2_CALL fakeSceneStep(OgsrPhysX2SceneHandle, float, std::uint64_t, OgsrPhysX2StepResult*, char*,
                                   std::size_t)
{
    ++g_fake->sceneStepCalls;
    return 0;
}
int OGSR_PHYSX2_CALL fakeRagdollCreate(OgsrPhysX2SceneHandle, const OgsrPhysXRagdollDefinition*,
                                       const OgsrPhysX2RagdollInstanceConfig*, OgsrPhysX2RagdollHandle*, char*,
                                       std::size_t)
{
    ++g_fake->ragdollCreateCalls;
    return 0;
}
int OGSR_PHYSX2_CALL fakeRagdollDestroy(OgsrPhysX2RagdollHandle, char*, std::size_t) { return 0; }
int OGSR_PHYSX2_CALL fakeRagdollIsAlive(OgsrPhysX2RagdollHandle) { return 0; }
int OGSR_PHYSX2_CALL fakeRagdollGetPose(OgsrPhysX2RagdollHandle, OgsrPhysXRagdollPose*, float*, std::uint32_t)
{
    return 0;
}
int OGSR_PHYSX2_CALL fakeRagdollApplyImpulse(OgsrPhysX2RagdollHandle, std::uint16_t, const float[3],
                                             const float[3], char*, std::size_t)
{
    return 0;
}
int OGSR_PHYSX2_CALL fakeRagdollApplyCentralImpulse(OgsrPhysX2RagdollHandle, std::uint16_t, const float[3], char*,
                                                    std::size_t)
{
    return 0;
}
int OGSR_PHYSX2_CALL fakeRagdollGetBodyMass(OgsrPhysX2RagdollHandle, std::uint16_t, OgsrPhysXRagdollBodyMass*, char*,
                                            std::size_t)
{
    return 0;
}
int OGSR_PHYSX2_CALL fakeRagdollGetInfo(OgsrPhysX2RagdollHandle, OgsrPhysXRagdollInfo*) { return 0; }
int OGSR_PHYSX2_CALL fakeRagdollGetDiagnostics(OgsrPhysX2RagdollHandle, float, OgsrPhysXRagdollDiagnostics*)
{
    return 0;
}

void* fakeResolve(void* context, const char* name)
{
    FakeApi* fake = static_cast<FakeApi*>(context);
    fake->resolved.emplace_back(name);
    const std::string symbol(name);
    if (symbol == "OgsrPhysX2GetApiInfo")
        return fake->provideInfo ? reinterpret_cast<void*>(&fakeGetApiInfo) : nullptr;
    if (fake->symbols.count(symbol) == 0)
        return nullptr;

    if (symbol == "OgsrPhysX2SceneCreate")
        return reinterpret_cast<void*>(&fakeSceneCreate);
    if (symbol == "OgsrPhysX2SceneDestroy")
        return reinterpret_cast<void*>(&fakeSceneDestroy);
    if (symbol == "OgsrPhysX2SceneIsAlive")
        return reinterpret_cast<void*>(&fakeSceneIsAlive);
    if (symbol == "OgsrPhysX2SceneSetStaticMesh")
        return reinterpret_cast<void*>(&fakeSceneSetStaticMesh);
    if (symbol == "OgsrPhysX2SceneAddStaticFragment")
        return reinterpret_cast<void*>(&fakeSceneAddStaticFragment);
    if (symbol == "OgsrPhysX2SceneStep")
        return reinterpret_cast<void*>(&fakeSceneStep);
    if (symbol == "OgsrPhysX2RagdollCreate")
        return reinterpret_cast<void*>(&fakeRagdollCreate);
    if (symbol == "OgsrPhysX2RagdollDestroy")
        return reinterpret_cast<void*>(&fakeRagdollDestroy);
    if (symbol == "OgsrPhysX2RagdollIsAlive")
        return reinterpret_cast<void*>(&fakeRagdollIsAlive);
    if (symbol == "OgsrPhysX2RagdollGetPose")
        return reinterpret_cast<void*>(&fakeRagdollGetPose);
    if (symbol == "OgsrPhysX2RagdollApplyImpulse")
        return reinterpret_cast<void*>(&fakeRagdollApplyImpulse);
    if (symbol == "OgsrPhysX2RagdollApplyCentralImpulse")
        return reinterpret_cast<void*>(&fakeRagdollApplyCentralImpulse);
    if (symbol == "OgsrPhysX2RagdollGetBodyMass")
        return reinterpret_cast<void*>(&fakeRagdollGetBodyMass);
    if (symbol == "OgsrPhysX2RagdollGetInfo")
        return reinterpret_cast<void*>(&fakeRagdollGetInfo);
    if (symbol == "OgsrPhysX2RagdollGetDiagnostics")
        return reinterpret_cast<void*>(&fakeRagdollGetDiagnostics);
    return nullptr;
}

physics_physx::V2Requirements requirements()
{
    physics_physx::V2Requirements result;
    result.minimumMinor = OGSR_PHYSX2_ABI_MINOR;
    result.requiredCapabilities = OGSR_PHYSX2_CAP_REQUIRED_P3_1;
    result.minimumScenes = 1;
    result.minimumInstances = 2;
    return result;
}

void checkEmptyApi(const physics_physx::V2ApiTable& api, const char* message)
{
    check(api.getApiInfo == nullptr && api.sceneCreate == nullptr && api.sceneDestroy == nullptr &&
              api.sceneIsAlive == nullptr && api.sceneSetStaticMesh == nullptr &&
              api.sceneAddStaticFragment == nullptr && api.sceneStep == nullptr && api.ragdollCreate == nullptr &&
              api.ragdollDestroy == nullptr && api.ragdollIsAlive == nullptr && api.ragdollGetPose == nullptr &&
              api.ragdollApplyImpulse == nullptr && api.ragdollApplyCentralImpulse == nullptr &&
              api.ragdollGetBodyMass == nullptr && api.ragdollGetInfo == nullptr &&
              api.ragdollGetDiagnostics == nullptr,
          message);
}

void testReady()
{
    FakeApi fake;
    g_fake = &fake;
    const physics_physx::V2NegotiationResult result = physics_physx::negotiateV2(&fakeResolve, &fake, requirements());
    check(result.status == physics_physx::V2NegotiationStatus::Ready, "ready negotiation succeeds");
    check(result.error.empty(), "ready negotiation has no error");
    check(result.api.getApiInfo != nullptr, "ready table has GetApiInfo");
    check(result.api.sceneCreate != nullptr, "ready table has SceneCreate");
    check(result.api.sceneStep != nullptr, "ready table has SceneStep");
    check(result.api.ragdollCreate != nullptr, "ready table has RagdollCreate");
    check(result.api.ragdollGetDiagnostics != nullptr, "ready table has GetDiagnostics");
    check(fake.infoCalls == 1, "ready negotiation calls GetApiInfo once");
    check(fake.sceneCreateCalls == 0, "ready negotiation does not call SceneCreate");
    check(fake.sceneStepCalls == 0, "ready negotiation does not call SceneStep");
    check(fake.ragdollCreateCalls == 0, "ready negotiation does not call RagdollCreate");
    for (const std::string& name : fake.resolved)
        check(name.rfind("OgsrPhysX2", 0) == 0, "resolver only requests v2 symbols");
}

void testV1OnlyDll()
{
    FakeApi fake;
    fake.provideInfo = false;
    g_fake = &fake;
    const physics_physx::V2NegotiationResult result = physics_physx::negotiateV2(&fakeResolve, &fake, requirements());
    check(result.status == physics_physx::V2NegotiationStatus::MissingApiInfo, "v1-only DLL is rejected");
    check(!result.error.empty(), "v1-only DLL reports an error");
    check(fake.infoCalls == 0, "v1-only DLL does not call GetApiInfo");
    checkEmptyApi(result.api, "v1-only DLL yields an empty v2 table");
    for (const std::string& name : fake.resolved)
        check(name.rfind("OgsrPhysX2", 0) == 0, "v1-only DLL resolver requests no v1 symbols");
}

void testMissingMandatorySymbol()
{
    FakeApi fake;
    fake.symbols.erase("OgsrPhysX2SceneCreate");
    g_fake = &fake;
    const physics_physx::V2NegotiationResult result = physics_physx::negotiateV2(&fakeResolve, &fake, requirements());
    check(result.status == physics_physx::V2NegotiationStatus::MissingExport, "missing mandatory symbol is rejected");
    check(result.error.find("OgsrPhysX2SceneCreate") != std::string::npos, "missing symbol is named in the error");
    checkEmptyApi(result.api, "missing symbol yields an empty v2 table");
    check(fake.sceneCreateCalls == 0, "missing symbol does not call SceneCreate");
    check(fake.sceneStepCalls == 0, "missing symbol does not call SceneStep");
    check(fake.ragdollCreateCalls == 0, "missing symbol does not call RagdollCreate");
}

void testPartialNegotiation()
{
    FakeApi fake;
    fake.symbols.erase("OgsrPhysX2RagdollGetDiagnostics");
    g_fake = &fake;
    const physics_physx::V2NegotiationResult result = physics_physx::negotiateV2(&fakeResolve, &fake, requirements());
    check(result.status == physics_physx::V2NegotiationStatus::MissingExport, "late missing symbol is rejected");
    checkEmptyApi(result.api, "late missing symbol yields an empty v2 table");
    check(fake.sceneCreateCalls == 0 && fake.sceneStepCalls == 0 && fake.ragdollCreateCalls == 0,
          "partial negotiation calls no scene/ragdoll function");
}

void testBadMagic()
{
    FakeApi fake;
    fake.info.magic = 0;
    g_fake = &fake;
    const physics_physx::V2NegotiationResult result = physics_physx::negotiateV2(&fakeResolve, &fake, requirements());
    check(result.status == physics_physx::V2NegotiationStatus::BadMagic, "bad magic is rejected");
    checkEmptyApi(result.api, "bad magic yields an empty v2 table");
}

void testUnsupportedVersion()
{
    FakeApi fake;
    fake.info.abiMajor = 3;
    g_fake = &fake;
    physics_physx::V2NegotiationResult result = physics_physx::negotiateV2(&fakeResolve, &fake, requirements());
    check(result.status == physics_physx::V2NegotiationStatus::UnsupportedMajor, "unsupported major is rejected");
    checkEmptyApi(result.api, "unsupported major yields an empty v2 table");

    FakeApi minorFake;
    minorFake.info.abiMinor = 0;
    g_fake = &minorFake;
    physics_physx::V2Requirements minorRequirements = requirements();
    minorRequirements.minimumMinor = 1;
    result = physics_physx::negotiateV2(&fakeResolve, &minorFake, minorRequirements);
    check(result.status == physics_physx::V2NegotiationStatus::UnsupportedMinor, "unsupported minor is rejected");
    checkEmptyApi(result.api, "unsupported minor yields an empty v2 table");
}

void testMissingCapabilities()
{
    FakeApi fake;
    fake.info.capabilityFlags = 0;
    g_fake = &fake;
    const physics_physx::V2NegotiationResult result = physics_physx::negotiateV2(&fakeResolve, &fake, requirements());
    check(result.status == physics_physx::V2NegotiationStatus::MissingCapabilities,
          "missing required capability is rejected");
    checkEmptyApi(result.api, "missing capability yields an empty v2 table");
}

void testInvalidInfoSize()
{
    FakeApi fake;
    fake.reportedStructSize = 64;
    g_fake = &fake;
    const physics_physx::V2NegotiationResult result = physics_physx::negotiateV2(&fakeResolve, &fake, requirements());
    check(result.status == physics_physx::V2NegotiationStatus::InvalidInfoSize, "invalid info size is rejected");
    checkEmptyApi(result.api, "invalid info size yields an empty v2 table");
}

void testInfoCallFailure()
{
    FakeApi fake;
    fake.infoReturnsSuccess = false;
    g_fake = &fake;
    const physics_physx::V2NegotiationResult result = physics_physx::negotiateV2(&fakeResolve, &fake, requirements());
    check(result.status == physics_physx::V2NegotiationStatus::InfoCallFailed, "GetApiInfo failure is rejected");
    checkEmptyApi(result.api, "GetApiInfo failure yields an empty v2 table");
}

void testInvalidCapacity()
{
    FakeApi fake;
    fake.info.maxScenes = 0;
    g_fake = &fake;
    const physics_physx::V2NegotiationResult result = physics_physx::negotiateV2(&fakeResolve, &fake, requirements());
    check(result.status == physics_physx::V2NegotiationStatus::InvalidCapacity, "invalid capacity is rejected");
    checkEmptyApi(result.api, "invalid capacity yields an empty v2 table");
}

void testNullResolver()
{
    const physics_physx::V2NegotiationResult result = physics_physx::negotiateV2(nullptr, nullptr, requirements());
    check(result.status == physics_physx::V2NegotiationStatus::NullResolver, "null resolver is rejected");
    checkEmptyApi(result.api, "null resolver yields an empty v2 table");
}

void testRealV1OnlyDll()
{
    HMODULE module = LoadLibraryA("mock_physx_runtime_v1.dll");
    check(module != nullptr, "load mock v1-only DLL");
    if (!module)
    {
        std::printf("  mock v1 DLL load error=%lu\n", GetLastError());
        return;
    }
    const physics_physx::V2NegotiationResult result =
        physics_physx::negotiateV2FromModule(module, requirements());
    check(result.status == physics_physx::V2NegotiationStatus::MissingApiInfo,
          "real v1-only DLL is rejected at MissingApiInfo");
    checkEmptyApi(result.api, "real v1-only DLL yields an empty v2 table");
    FreeLibrary(module);
}

void testRealV2FullDll()
{
    HMODULE module = LoadLibraryA("mock_physx_runtime_v2.dll");
    check(module != nullptr, "load mock v2 DLL");
    if (!module)
    {
        std::printf("  mock v2 DLL load error=%lu\n", GetLastError());
        return;
    }
    const physics_physx::V2NegotiationResult result =
        physics_physx::negotiateV2FromModule(module, requirements());
    check(result.status == physics_physx::V2NegotiationStatus::Ready, "real v2 DLL negotiates Ready");
    check(result.api.getApiInfo != nullptr && result.api.sceneCreate != nullptr && result.api.sceneStep != nullptr &&
              result.api.ragdollCreate != nullptr,
          "real v2 DLL fills the complete v2 table");
    FreeLibrary(module);
}

void testRealV2MissingDll()
{
    HMODULE module = LoadLibraryA("mock_physx_runtime_v2_missing.dll");
    check(module != nullptr, "load mock v2 missing-symbol DLL");
    if (!module)
    {
        std::printf("  mock v2 missing DLL load error=%lu\n", GetLastError());
        return;
    }
    const physics_physx::V2NegotiationResult result =
        physics_physx::negotiateV2FromModule(module, requirements());
    check(result.status == physics_physx::V2NegotiationStatus::MissingExport,
          "real v2 DLL with a missing export is rejected");
    check(result.error.find("OgsrPhysX2SceneCreate") != std::string::npos,
          "real missing export is named in the error");
    checkEmptyApi(result.api, "real missing export yields an empty v2 table");
    FreeLibrary(module);
}

void testModuleLease()
{
    physics_physx::V2ModuleLease lease;
    std::string error;
    check(lease.load("mock_physx_runtime_v2.dll", &error), "module lease load");
    check(lease.isLoaded(), "module lease loaded");
    check(!lease.isApiValid(), "module lease has no api before negotiation");

    const physics_physx::V2NegotiationResult result = lease.negotiate(requirements());
    check(result.status == physics_physx::V2NegotiationStatus::Ready, "module lease negotiates Ready");
    check(result.api.getApiInfo == nullptr && result.api.sceneCreate == nullptr,
          "module lease result does not copy function pointers");
    check(lease.isApiValid(), "module lease api is valid after negotiation");
    check(lease.api().sceneCreate != nullptr && lease.api().sceneStep != nullptr &&
              lease.api().ragdollCreate != nullptr,
          "module lease table is populated");

    check(lease.unload(&error), "module lease unload");
    check(!lease.isLoaded(), "module lease is unloaded");
    check(!lease.isApiValid(), "module lease api is invalid after unload");
    check(lease.api().getApiInfo == nullptr && lease.api().sceneCreate == nullptr &&
              lease.api().ragdollCreate == nullptr,
          "module lease table is cleared before DLL unload");
    check(lease.unload(&error), "module lease second unload is safe");

    const physics_physx::V2NegotiationResult afterUnload = lease.negotiate(requirements());
    check(afterUnload.status == physics_physx::V2NegotiationStatus::NullResolver,
          "module lease negotiation after unload fails closed");

    check(lease.load("mock_physx_runtime_v2.dll", &error), "module lease reload");
    const physics_physx::V2NegotiationResult afterReload = lease.negotiate(requirements());
    check(afterReload.status == physics_physx::V2NegotiationStatus::Ready, "module lease renegotiates after reload");
    check(lease.unload(&error), "module lease final unload");
}

} // namespace

int main()
{
    testReady();
    testV1OnlyDll();
    testMissingMandatorySymbol();
    testPartialNegotiation();
    testBadMagic();
    testUnsupportedVersion();
    testMissingCapabilities();
    testInvalidInfoSize();
    testInfoCallFailure();
    testInvalidCapacity();
    testNullResolver();
    testRealV1OnlyDll();
    testRealV2FullDll();
    testRealV2MissingDll();
    testModuleLease();

    std::printf("physics_physx_v2_loader tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
