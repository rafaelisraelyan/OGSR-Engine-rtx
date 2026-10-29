#include "physics_physx/physx_ragdoll_v2_loader.h"

#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace physics_physx
{
namespace
{
template <typename Fn>
Fn resolveAs(const V2ResolveProc& resolve, void* context, const char* name)
{
    // The only function-pointer cast in the loader: a symbol is cast to the
    // exact typedef for its frozen v2 name. v1 pointers are never cast to v2.
    return reinterpret_cast<Fn>(resolve(context, name));
}

V2NegotiationResult fail(V2NegotiationStatus status, std::string message)
{
    V2NegotiationResult result;
    result.status = status;
    result.error = std::move(message);
    return result;
}

V2NegotiationResult missingExport(const char* name, const OgsrPhysX2ApiInfo& info)
{
    V2NegotiationResult result;
    result.status = V2NegotiationStatus::MissingExport;
    result.error = std::string("missing mandatory v2 export: ") + name;
    result.info = info;
    return result;
}
} // namespace

V2NegotiationResult negotiateV2(const V2ResolveProc& resolve, void* context, const V2Requirements& requirements)
{
    if (!resolve)
        return fail(V2NegotiationStatus::NullResolver, "v2 resolver is null");

    const OgsrPhysX2GetApiInfoFn getApiInfo =
        resolveAs<OgsrPhysX2GetApiInfoFn>(resolve, context, "OgsrPhysX2GetApiInfo");
    if (!getApiInfo)
        return fail(V2NegotiationStatus::MissingApiInfo, "OgsrPhysX2GetApiInfo is not exported");

    OgsrPhysX2ApiInfo info{};
    info.structSize = OGSR_PHYSX2_API_INFO_V0_SIZE;
    if (!getApiInfo(&info))
        return fail(V2NegotiationStatus::InfoCallFailed, "OgsrPhysX2GetApiInfo returned failure");

    if (info.structSize != OGSR_PHYSX2_API_INFO_V0_SIZE)
        return fail(V2NegotiationStatus::InvalidInfoSize, "OgsrPhysX2GetApiInfo returned an unsupported struct size");

    if (info.magic != OGSR_PHYSX2_API_INFO_MAGIC)
        return fail(V2NegotiationStatus::BadMagic, "OgsrPhysX2GetApiInfo magic mismatch");

    if (info.abiMajor != OGSR_PHYSX2_ABI_MAJOR)
        return fail(V2NegotiationStatus::UnsupportedMajor, "unsupported v2 ABI major");

    if (info.abiMinor < requirements.minimumMinor)
        return fail(V2NegotiationStatus::UnsupportedMinor, "unsupported v2 ABI minor");

    if ((info.capabilityFlags & requirements.requiredCapabilities) != requirements.requiredCapabilities)
        return fail(V2NegotiationStatus::MissingCapabilities, "required v2 capabilities are not advertised");

    if (info.maxScenes < requirements.minimumScenes || info.maxInstancesPerScene < requirements.minimumInstances)
        return fail(V2NegotiationStatus::InvalidCapacity, "v2 capacity is below the required minimum");

    V2ApiTable api;
    api.getApiInfo = getApiInfo;

    api.sceneCreate = resolveAs<OgsrPhysX2SceneCreateFn>(resolve, context, "OgsrPhysX2SceneCreate");
    if (!api.sceneCreate)
        return missingExport("OgsrPhysX2SceneCreate", info);
    api.sceneDestroy = resolveAs<OgsrPhysX2SceneDestroyFn>(resolve, context, "OgsrPhysX2SceneDestroy");
    if (!api.sceneDestroy)
        return missingExport("OgsrPhysX2SceneDestroy", info);
    api.sceneIsAlive = resolveAs<OgsrPhysX2SceneIsAliveFn>(resolve, context, "OgsrPhysX2SceneIsAlive");
    if (!api.sceneIsAlive)
        return missingExport("OgsrPhysX2SceneIsAlive", info);
    api.sceneSetStaticMesh = resolveAs<OgsrPhysX2SceneSetStaticMeshFn>(resolve, context, "OgsrPhysX2SceneSetStaticMesh");
    if (!api.sceneSetStaticMesh)
        return missingExport("OgsrPhysX2SceneSetStaticMesh", info);
    api.sceneAddStaticFragment =
        resolveAs<OgsrPhysX2SceneAddStaticFragmentFn>(resolve, context, "OgsrPhysX2SceneAddStaticFragment");
    if (!api.sceneAddStaticFragment)
        return missingExport("OgsrPhysX2SceneAddStaticFragment", info);
    api.sceneStep = resolveAs<OgsrPhysX2SceneStepFn>(resolve, context, "OgsrPhysX2SceneStep");
    if (!api.sceneStep)
        return missingExport("OgsrPhysX2SceneStep", info);

    api.ragdollCreate = resolveAs<OgsrPhysX2RagdollCreateFn>(resolve, context, "OgsrPhysX2RagdollCreate");
    if (!api.ragdollCreate)
        return missingExport("OgsrPhysX2RagdollCreate", info);
    api.ragdollDestroy = resolveAs<OgsrPhysX2RagdollDestroyFn>(resolve, context, "OgsrPhysX2RagdollDestroy");
    if (!api.ragdollDestroy)
        return missingExport("OgsrPhysX2RagdollDestroy", info);
    api.ragdollIsAlive = resolveAs<OgsrPhysX2RagdollIsAliveFn>(resolve, context, "OgsrPhysX2RagdollIsAlive");
    if (!api.ragdollIsAlive)
        return missingExport("OgsrPhysX2RagdollIsAlive", info);
    api.ragdollGetPose = resolveAs<OgsrPhysX2RagdollGetPoseFn>(resolve, context, "OgsrPhysX2RagdollGetPose");
    if (!api.ragdollGetPose)
        return missingExport("OgsrPhysX2RagdollGetPose", info);
    api.ragdollApplyImpulse =
        resolveAs<OgsrPhysX2RagdollApplyImpulseFn>(resolve, context, "OgsrPhysX2RagdollApplyImpulse");
    if (!api.ragdollApplyImpulse)
        return missingExport("OgsrPhysX2RagdollApplyImpulse", info);
    api.ragdollApplyCentralImpulse = resolveAs<OgsrPhysX2RagdollApplyCentralImpulseFn>(
        resolve, context, "OgsrPhysX2RagdollApplyCentralImpulse");
    if (!api.ragdollApplyCentralImpulse)
        return missingExport("OgsrPhysX2RagdollApplyCentralImpulse", info);
    api.ragdollGetBodyMass =
        resolveAs<OgsrPhysX2RagdollGetBodyMassFn>(resolve, context, "OgsrPhysX2RagdollGetBodyMass");
    if (!api.ragdollGetBodyMass)
        return missingExport("OgsrPhysX2RagdollGetBodyMass", info);
    api.ragdollGetInfo = resolveAs<OgsrPhysX2RagdollGetInfoFn>(resolve, context, "OgsrPhysX2RagdollGetInfo");
    if (!api.ragdollGetInfo)
        return missingExport("OgsrPhysX2RagdollGetInfo", info);
    api.ragdollGetDiagnostics =
        resolveAs<OgsrPhysX2RagdollGetDiagnosticsFn>(resolve, context, "OgsrPhysX2RagdollGetDiagnostics");
    if (!api.ragdollGetDiagnostics)
        return missingExport("OgsrPhysX2RagdollGetDiagnostics", info);

    V2NegotiationResult result;
    result.status = V2NegotiationStatus::Ready;
    result.info = info;
    result.api = api;
    return result;
}

#ifdef _WIN32
namespace
{
void* win32Resolve(void* context, const char* name)
{
    if (!context || !name)
        return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(context), name));
}
} // namespace

V2NegotiationResult negotiateV2FromModule(void* module, const V2Requirements& requirements)
{
    if (!module)
        return fail(V2NegotiationStatus::NullResolver, "v2 module is null");
    return negotiateV2(&win32Resolve, module, requirements);
}

V2ModuleLease::~V2ModuleLease()
{
    unload();
}

bool V2ModuleLease::load(const char* path, std::string* error)
{
    if (module_)
    {
        if (error)
            *error = "v2 module is already loaded";
        return false;
    }
    if (!path || !*path)
    {
        if (error)
            *error = "v2 module path is empty";
        return false;
    }

    HMODULE module = LoadLibraryA(path);
    if (!module)
    {
        if (error)
            *error = "LoadLibrary failed: " + std::to_string(GetLastError());
        return false;
    }

    module_ = module;
    api_ = V2ApiTable{};
    if (error)
        error->clear();
    return true;
}

bool V2ModuleLease::unload(std::string* error)
{
    if (!module_)
        return true;
    if (resourceRefs_ != 0)
    {
        if (error)
            *error = "v2 module still has " + std::to_string(resourceRefs_) + " active resource(s)";
        return false;
    }

    // Invalidate the table before dropping the module reference. A copied
    // function pointer remains a caller contract violation, but no lease-owned
    // table can be used after this point.
    api_ = V2ApiTable{};
    if (!FreeLibrary(static_cast<HMODULE>(module_)))
    {
        if (error)
            *error = "FreeLibrary failed: " + std::to_string(GetLastError());
        return false;
    }

    module_ = nullptr;
    if (error)
        error->clear();
    return true;
}

void V2ModuleLease::addResourceRef()
{
    ++resourceRefs_;
}

bool V2ModuleLease::releaseResourceRef()
{
    if (resourceRefs_ == 0)
        return false;
    --resourceRefs_;
    return true;
}

std::uint32_t V2ModuleLease::resourceRefs() const
{
    return resourceRefs_;
}

bool V2ModuleLease::isLoaded() const
{
    return module_ != nullptr;
}

bool V2ModuleLease::isApiValid() const
{
    return module_ != nullptr && api_.getApiInfo != nullptr;
}

void* V2ModuleLease::module() const
{
    return module_;
}

const V2ApiTable& V2ModuleLease::api() const
{
    return api_;
}

V2NegotiationResult V2ModuleLease::negotiate(const V2Requirements& requirements)
{
    if (!module_)
        return fail(V2NegotiationStatus::NullResolver, "v2 module is not loaded");

    V2NegotiationResult result = negotiateV2FromModule(module_, requirements);
    api_ = result.status == V2NegotiationStatus::Ready ? result.api : V2ApiTable{};
    // The lease owns the table. Do not return a second copy of function
    // pointers that could outlive unload()/FreeLibrary.
    result.api = V2ApiTable{};
    return result;
}
#endif

const char* v2NegotiationStatusName(V2NegotiationStatus status)
{
    switch (status)
    {
    case V2NegotiationStatus::Ready:
        return "ready";
    case V2NegotiationStatus::NullResolver:
        return "null-resolver";
    case V2NegotiationStatus::MissingApiInfo:
        return "missing-api-info";
    case V2NegotiationStatus::InfoCallFailed:
        return "info-call-failed";
    case V2NegotiationStatus::BadMagic:
        return "bad-magic";
    case V2NegotiationStatus::UnsupportedMajor:
        return "unsupported-major";
    case V2NegotiationStatus::UnsupportedMinor:
        return "unsupported-minor";
    case V2NegotiationStatus::InvalidInfoSize:
        return "invalid-info-size";
    case V2NegotiationStatus::MissingCapabilities:
        return "missing-capabilities";
    case V2NegotiationStatus::InvalidCapacity:
        return "invalid-capacity";
    case V2NegotiationStatus::MissingExport:
        return "missing-export";
    }
    return "unknown";
}

} // namespace physics_physx
