#pragma once

// Fail-closed v2 DLL negotiation for the P3.1-A ABI foundation.
//
// The resolver is injectable so the complete negative matrix can be tested
// without loading OGSR or creating a PhysX scene. The Win32 wrapper uses the
// real GetProcAddress path. No v2 scene/step/ragdoll function is called during
// negotiation; only OgsrPhysX2GetApiInfo is invoked, and only after its symbol
// has been resolved by exact name.

#include "physics_physx/physx_ragdoll_c_v2.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace physics_physx
{

using V2ResolveProc = void* (*)(void* context, const char* name);

using OgsrPhysX2GetApiInfoFn = int (OGSR_PHYSX2_CALL*)(OgsrPhysX2ApiInfo*);
using OgsrPhysX2SceneCreateFn = int (OGSR_PHYSX2_CALL*)(const OgsrPhysX2SceneConfig*, OgsrPhysX2SceneHandle*,
                                                         char*, std::size_t);
using OgsrPhysX2SceneDestroyFn = int (OGSR_PHYSX2_CALL*)(OgsrPhysX2SceneHandle, char*, std::size_t);
using OgsrPhysX2SceneIsAliveFn = int (OGSR_PHYSX2_CALL*)(OgsrPhysX2SceneHandle);
using OgsrPhysX2SceneSetStaticMeshFn = int (OGSR_PHYSX2_CALL*)(OgsrPhysX2SceneHandle, const float*, std::uint32_t,
                                                               const std::uint32_t*, std::uint32_t, char*,
                                                               std::size_t);
using OgsrPhysX2SceneAddStaticFragmentFn = int (OGSR_PHYSX2_CALL*)(OgsrPhysX2SceneHandle, std::uint64_t, const float*,
                                                                  std::uint32_t, const std::uint32_t*, std::uint32_t,
                                                                  char*, std::size_t);
using OgsrPhysX2SceneStepFn = int (OGSR_PHYSX2_CALL*)(OgsrPhysX2SceneHandle, float, std::uint64_t,
                                                      OgsrPhysX2StepResult*, char*, std::size_t);
using OgsrPhysX2RagdollCreateFn = int (OGSR_PHYSX2_CALL*)(OgsrPhysX2SceneHandle, const OgsrPhysXRagdollDefinition*,
                                                          const OgsrPhysX2RagdollInstanceConfig*,
                                                          OgsrPhysX2RagdollHandle*, char*, std::size_t);
using OgsrPhysX2RagdollDestroyFn = int (OGSR_PHYSX2_CALL*)(OgsrPhysX2RagdollHandle, char*, std::size_t);
using OgsrPhysX2RagdollIsAliveFn = int (OGSR_PHYSX2_CALL*)(OgsrPhysX2RagdollHandle);
using OgsrPhysX2RagdollGetPoseFn = int (OGSR_PHYSX2_CALL*)(OgsrPhysX2RagdollHandle, OgsrPhysXRagdollPose*, float*,
                                                           std::uint32_t);
using OgsrPhysX2RagdollApplyImpulseFn = int (OGSR_PHYSX2_CALL*)(OgsrPhysX2RagdollHandle, std::uint16_t,
                                                                const float[3], const float[3], char*, std::size_t);
using OgsrPhysX2RagdollApplyCentralImpulseFn = int (OGSR_PHYSX2_CALL*)(OgsrPhysX2RagdollHandle, std::uint16_t,
                                                                       const float[3], char*, std::size_t);
using OgsrPhysX2RagdollGetBodyMassFn = int (OGSR_PHYSX2_CALL*)(OgsrPhysX2RagdollHandle, std::uint16_t,
                                                               OgsrPhysXRagdollBodyMass*, char*, std::size_t);
using OgsrPhysX2RagdollGetInfoFn = int (OGSR_PHYSX2_CALL*)(OgsrPhysX2RagdollHandle, OgsrPhysXRagdollInfo*);
using OgsrPhysX2RagdollGetDiagnosticsFn = int (OGSR_PHYSX2_CALL*)(OgsrPhysX2RagdollHandle, float,
                                                                  OgsrPhysXRagdollDiagnostics*);

struct V2ApiTable
{
    OgsrPhysX2GetApiInfoFn getApiInfo = nullptr;
    OgsrPhysX2SceneCreateFn sceneCreate = nullptr;
    OgsrPhysX2SceneDestroyFn sceneDestroy = nullptr;
    OgsrPhysX2SceneIsAliveFn sceneIsAlive = nullptr;
    OgsrPhysX2SceneSetStaticMeshFn sceneSetStaticMesh = nullptr;
    OgsrPhysX2SceneAddStaticFragmentFn sceneAddStaticFragment = nullptr;
    OgsrPhysX2SceneStepFn sceneStep = nullptr;
    OgsrPhysX2RagdollCreateFn ragdollCreate = nullptr;
    OgsrPhysX2RagdollDestroyFn ragdollDestroy = nullptr;
    OgsrPhysX2RagdollIsAliveFn ragdollIsAlive = nullptr;
    OgsrPhysX2RagdollGetPoseFn ragdollGetPose = nullptr;
    OgsrPhysX2RagdollApplyImpulseFn ragdollApplyImpulse = nullptr;
    OgsrPhysX2RagdollApplyCentralImpulseFn ragdollApplyCentralImpulse = nullptr;
    OgsrPhysX2RagdollGetBodyMassFn ragdollGetBodyMass = nullptr;
    OgsrPhysX2RagdollGetInfoFn ragdollGetInfo = nullptr;
    OgsrPhysX2RagdollGetDiagnosticsFn ragdollGetDiagnostics = nullptr;
};

struct V2Requirements
{
    std::uint32_t minimumMinor = OGSR_PHYSX2_ABI_MINOR;
    std::uint32_t requiredCapabilities = OGSR_PHYSX2_CAP_REQUIRED_P3_1;
    std::uint32_t minimumScenes = 1;
    std::uint32_t minimumInstances = 2;
};

enum class V2NegotiationStatus
{
    Ready,
    NullResolver,
    MissingApiInfo,
    InfoCallFailed,
    BadMagic,
    UnsupportedMajor,
    UnsupportedMinor,
    InvalidInfoSize,
    MissingCapabilities,
    InvalidCapacity,
    MissingExport,
};

struct V2NegotiationResult
{
    V2NegotiationStatus status = V2NegotiationStatus::NullResolver;
    std::string error;
    OgsrPhysX2ApiInfo info{};
    V2ApiTable api{};
};

V2NegotiationResult negotiateV2(const V2ResolveProc& resolve, void* context, const V2Requirements& requirements);

#ifdef _WIN32
// Real Win32 path. module is HMODULE; null is rejected. The wrapper resolves
// symbols with GetProcAddress and delegates all validation to negotiateV2.
V2NegotiationResult negotiateV2FromModule(void* module, const V2Requirements& requirements);

// RAII owner for HMODULE + V2ApiTable. The table is valid only while this
// lease is loaded and isApiValid() is true. unload() clears the table before
// FreeLibrary; callers must not copy function pointers out of the lease.
class V2ModuleLease
{
public:
    V2ModuleLease() = default;
    ~V2ModuleLease();

    V2ModuleLease(const V2ModuleLease&) = delete;
    V2ModuleLease& operator=(const V2ModuleLease&) = delete;

    bool load(const char* path, std::string* error = nullptr);
    bool unload(std::string* error = nullptr);
    bool isLoaded() const;
    bool isApiValid() const;
    void* module() const;
    const V2ApiTable& api() const;
    V2NegotiationResult negotiate(const V2Requirements& requirements);

    // Client-side resource guard. The game runtime registers every live v2
    // scene/instance here; unload() refuses to FreeLibrary while any resource
    // is registered. The lease cannot prevent a hostile caller from calling
    // FreeLibrary directly, so all game calls must go through this object.
    void addResourceRef();
    bool releaseResourceRef();
    std::uint32_t resourceRefs() const;

private:
    void* module_ = nullptr;
    V2ApiTable api_{};
    std::uint32_t resourceRefs_ = 0;
};
#endif

const char* v2NegotiationStatusName(V2NegotiationStatus status);

} // namespace physics_physx
