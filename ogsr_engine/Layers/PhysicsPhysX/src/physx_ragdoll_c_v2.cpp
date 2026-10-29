#include "physics_physx/physx_ragdoll_c_v2.h"

#include "physx_ragdoll_v2_convert.h"
#include "physx_ragdoll_v2_internal.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <windows.h>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
using physics_portable::Vec3;

void writeError(char* error, std::size_t errorSize, const std::string& message)
{
    if (!error || errorSize == 0)
        return;
    const std::size_t count = std::min(errorSize - 1, message.size());
    std::memcpy(error, message.data(), count);
    error[count] = 0;
}

void clearError(char* error, std::size_t errorSize)
{
    if (error && errorSize)
        error[0] = 0;
}

// Test-only fault injection. It is gated by process environment variables and
// never changes the ABI or production behavior when the variables are absent.
// GetEnvironmentVariableA is used instead of getenv so the /MT DLL and the test
// executable (separate CRT copies) observe the same value.
bool faultInjectionEnabled(const char* name)
{
    char value[8] = {};
    const DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
    return length > 0 && value[0] != 0 && value[0] != '0';
}

std::atomic<bool> g_ragdollFaultOnceConsumed{false};
std::atomic<bool> g_sceneFaultOnceConsumed{false};

bool faultInjectionOnce(std::atomic<bool>& consumed, const char* name)
{
    if (!faultInjectionEnabled(name))
        return false;
    bool expected = false;
    return consumed.compare_exchange_strong(expected, true);
}

bool validateSceneConfig(const OgsrPhysX2SceneConfig& config, std::string* error)
{
    auto fail = [error](const char* message) {
        if (error)
            *error = message;
        return false;
    };
    if (!std::isfinite(config.fixedTimestep) || !(config.fixedTimestep > 0.0f))
        return fail("scene fixed timestep is invalid");
    if (!std::isfinite(config.gravityY))
        return fail("scene gravity is invalid");
    if (config.maxSubsteps == 0 || config.maxSubsteps > 16)
        return fail("scene maxSubsteps is invalid");
    if (config.solverPositionIterations == 0 || config.solverPositionIterations > 64 ||
        config.solverVelocityIterations == 0 || config.solverVelocityIterations > 64)
        return fail("scene solver iteration count is invalid");
    if (!std::isfinite(config.contactOffset) || !(config.contactOffset > 0.0f) ||
        !std::isfinite(config.restOffset) || config.restOffset < 0.0f ||
        config.restOffset >= config.contactOffset)
        return fail("scene contact/rest offset is invalid");
    if (config.enableStabilization > 1 || config.collectContacts > 1)
        return fail("scene boolean flag is invalid");
    if (config.workerThreads > 8)
        return fail("scene workerThreads is out of range");
    if (config.maxInstances > OGSR_PHYSX2_INSTANCE_CAPACITY)
        return fail("scene maxInstances exceeds the registry capacity");
    return true;
}

bool validateInstanceConfig(const OgsrPhysX2RagdollInstanceConfig& config, std::string* error)
{
    auto fail = [error](const char* message) {
        if (error)
            *error = message;
        return false;
    };
    if (!std::isfinite(config.linearDamping) || config.linearDamping < 0.0f ||
        !std::isfinite(config.angularDamping) || config.angularDamping < 0.0f)
        return fail("instance damping is invalid");
    if (!std::isfinite(config.staticRootDensity) || !(config.staticRootDensity > 0.0f))
        return fail("instance staticRootDensity is invalid");
    if (config.promoteStaticRoot > 1 || config.disableSelfCollision > 1)
        return fail("instance boolean flag is invalid");

    const std::uint32_t group = config.collisionGroup ? config.collisionGroup : OGSR_PHYSX2_GROUP_RAGDOLL;
    const std::uint32_t mask = config.collisionMask
                                   ? config.collisionMask
                                   : (OGSR_PHYSX2_GROUP_RAGDOLL | OGSR_PHYSX2_GROUP_STATIC);
    const std::uint32_t allowed = OGSR_PHYSX2_GROUP_RAGDOLL | OGSR_PHYSX2_GROUP_STATIC;
    if ((group & ~allowed) != 0 || (mask & ~allowed) != 0 || group == 0 || mask == 0)
        return fail("instance collision group/mask is invalid");
    // The static group is reserved for scene-owned geometry; a ragdoll body
    // must never join it, otherwise the static filter word would stop matching.
    if (group == OGSR_PHYSX2_GROUP_STATIC)
        return fail("instance collision group must not be the static group");
    return true;
}

bool finiteVec3(const float values[3])
{
    return std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]);
}

bool finiteVec3(const Vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

struct BackendHooks final : physics_physx::RagdollRegistryHooks
{
    bool createScene(const OgsrPhysX2SceneConfig& config, void** outScene, std::string* error) override
    {
        auto* scene = new physics_physx::PhysXRagdollScene(config);
        if (!scene->initialize(error))
        {
            delete scene;
            return false;
        }
        *outScene = scene;
        return true;
    }

    void destroyScene(void* scene) override
    {
        delete static_cast<physics_physx::PhysXRagdollScene*>(scene);
    }

    bool createInstance(const physics_physx::RagdollSceneContext& scene,
                        const physics_physx::RagdollInstanceCreateInfo& info, void** outInstance,
                        std::string* error) override
    {
        auto* backendScene = static_cast<physics_physx::PhysXRagdollScene*>(scene.backend);
        if (!backendScene)
        {
            if (error)
                *error = "scene backend is null";
            return false;
        }
        physics_physx::PhysXRagdollInstance* instance = nullptr;
        if (!backendScene->createInstance(info, instance, error))
            return false;
        *outInstance = instance;
        return true;
    }

    void destroyInstance(const physics_physx::RagdollSceneContext& scene, void* instance) override
    {
        auto* backendScene = static_cast<physics_physx::PhysXRagdollScene*>(scene.backend);
        if (backendScene && instance)
            backendScene->destroyInstance(static_cast<physics_physx::PhysXRagdollInstance*>(instance));
    }
};

struct Runtime
{
    BackendHooks hooks;
    physics_physx::RagdollRegistry registry;
    Runtime() : registry(hooks) {}
};

Runtime& runtime()
{
    static Runtime instance;
    return instance;
}

physics_physx::PhysXRagdollScene* sceneFor(OgsrPhysX2SceneHandle scene)
{
    return static_cast<physics_physx::PhysXRagdollScene*>(runtime().registry.sceneBackend(scene));
}

physics_physx::PhysXRagdollInstance* instanceFor(OgsrPhysX2RagdollHandle instance)
{
    return static_cast<physics_physx::PhysXRagdollInstance*>(runtime().registry.instanceBackend(instance));
}
} // namespace

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2GetApiInfo(OgsrPhysX2ApiInfo* info)
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

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2SceneCreate(const OgsrPhysX2SceneConfig* config,
                                                                      OgsrPhysX2SceneHandle* outScene,
                                                                      char* error, std::size_t errorSize)
{
    clearError(error, errorSize);
    if (!config || !outScene)
    {
        writeError(error, errorSize, "scene config or output handle is null");
        return 0;
    }
    *outScene = OgsrPhysX2SceneHandle{};
    if (config->structSize < sizeof(OgsrPhysX2SceneConfig))
    {
        writeError(error, errorSize, "scene config structSize is too small");
        return 0;
    }
    std::string message;
    if (!validateSceneConfig(*config, &message))
    {
        writeError(error, errorSize, message);
        return 0;
    }
    try
    {
        if (!runtime().registry.createScene(*config, outScene, &message))
        {
            writeError(error, errorSize, message);
            return 0;
        }
        return 1;
    }
    catch (const std::exception& exception)
    {
        writeError(error, errorSize, exception.what());
        return 0;
    }
    catch (...)
    {
        writeError(error, errorSize, "unknown exception in SceneCreate");
        return 0;
    }
}

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2SceneDestroy(OgsrPhysX2SceneHandle scene, char* error,
                                                                       std::size_t errorSize)
{
    clearError(error, errorSize);
    if (!runtime().registry.isSceneAlive(scene))
    {
        writeError(error, errorSize, "stale scene handle");
        return 0;
    }
    auto* backend = sceneFor(scene);
    if (!backend || backend->isStepping())
    {
        writeError(error, errorSize, "scene cannot be destroyed during simulate/fetch");
        return 0;
    }
    if (faultInjectionOnce(g_sceneFaultOnceConsumed, "OGSR_PHYSX2_FAULT_SCENE_DESTROY_ONCE") ||
        faultInjectionEnabled("OGSR_PHYSX2_FAULT_SCENE_DESTROY"))
    {
        writeError(error, errorSize, "fault injection: SceneDestroy failed");
        return 0;
    }
    std::string message;
    if (!runtime().registry.destroyScene(scene, &message))
    {
        writeError(error, errorSize, message);
        return 0;
    }
    return 1;
}

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2SceneIsAlive(OgsrPhysX2SceneHandle scene)
{
    return runtime().registry.isSceneAlive(scene) ? 1 : 0;
}

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2SceneSetStaticMesh(
    OgsrPhysX2SceneHandle scene, const float* vertices, std::uint32_t vertexCount, const std::uint32_t* indices,
    std::uint32_t indexCount, char* error, std::size_t errorSize)
{
    clearError(error, errorSize);
    if (!runtime().registry.isSceneAlive(scene))
    {
        writeError(error, errorSize, "stale scene handle");
        return 0;
    }
    if (!vertices || !indices || vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0)
    {
        writeError(error, errorSize, "invalid static mesh argument");
        return 0;
    }
    if (vertexCount > 1000000u || indexCount / 3 > 200000u)
    {
        writeError(error, errorSize, "static mesh exceeds the diagnostic cap");
        return 0;
    }
    std::vector<Vec3> convertedVertices(vertexCount);
    for (std::uint32_t index = 0; index < vertexCount; ++index)
    {
        convertedVertices[index] = Vec3{vertices[index * 3 + 0], vertices[index * 3 + 1], vertices[index * 3 + 2]};
        if (!finiteVec3(convertedVertices[index]))
        {
            writeError(error, errorSize, "static mesh contains a non-finite vertex");
            return 0;
        }
    }
    std::vector<std::uint32_t> convertedIndices(indices, indices + indexCount);
    auto* backend = sceneFor(scene);
    std::string message;
    if (!backend || !backend->setStaticMesh(convertedVertices, convertedIndices, &message))
    {
        writeError(error, errorSize, message.empty() ? "static mesh upload failed" : message);
        return 0;
    }
    return 1;
}

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2SceneAddStaticFragment(
    OgsrPhysX2SceneHandle scene, std::uint64_t key, const float* vertices, std::uint32_t vertexCount,
    const std::uint32_t* indices, std::uint32_t indexCount, char* error, std::size_t errorSize)
{
    clearError(error, errorSize);
    if (!runtime().registry.isSceneAlive(scene))
    {
        writeError(error, errorSize, "stale scene handle");
        return 0;
    }
    if (!vertices || !indices || vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0)
    {
        writeError(error, errorSize, "invalid static fragment argument");
        return 0;
    }
    if (vertexCount > 1000000u || indexCount / 3 > 200000u)
    {
        writeError(error, errorSize, "static fragment exceeds the diagnostic cap");
        return 0;
    }
    std::vector<Vec3> convertedVertices(vertexCount);
    for (std::uint32_t index = 0; index < vertexCount; ++index)
    {
        convertedVertices[index] = Vec3{vertices[index * 3 + 0], vertices[index * 3 + 1], vertices[index * 3 + 2]};
        if (!finiteVec3(convertedVertices[index]))
        {
            writeError(error, errorSize, "static fragment contains a non-finite vertex");
            return 0;
        }
    }
    std::vector<std::uint32_t> convertedIndices(indices, indices + indexCount);
    auto* backend = sceneFor(scene);
    std::string message;
    if (!backend || !backend->addStaticFragment(key, convertedVertices, convertedIndices, &message))
    {
        writeError(error, errorSize, message.empty() ? "static fragment upload failed" : message);
        return 0;
    }
    return 1;
}

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2SceneStep(
    OgsrPhysX2SceneHandle scene, float deltaSeconds, std::uint64_t frameId, OgsrPhysX2StepResult* result,
    char* error, std::size_t errorSize)
{
    clearError(error, errorSize);
    if (!runtime().registry.isSceneAlive(scene))
    {
        writeError(error, errorSize, "stale scene handle");
        return 0;
    }
    if (!result || result->structSize < sizeof(OgsrPhysX2StepResult))
    {
        writeError(error, errorSize, "step result buffer is invalid");
        return 0;
    }
    auto* backend = sceneFor(scene);
    std::string message;
    if (!backend || !backend->step(deltaSeconds, frameId, result, &message))
    {
        writeError(error, errorSize, message.empty() ? "scene step failed" : message);
        return 0;
    }
    return 1;
}

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollCreate(
    OgsrPhysX2SceneHandle scene, const OgsrPhysXRagdollDefinition* definition,
    const OgsrPhysX2RagdollInstanceConfig* config, OgsrPhysX2RagdollHandle* outHandle, char* error,
    std::size_t errorSize)
{
    clearError(error, errorSize);
    if (!outHandle)
    {
        writeError(error, errorSize, "ragdoll output handle is null");
        return 0;
    }
    *outHandle = OgsrPhysX2RagdollHandle{};
    if (!runtime().registry.isSceneAlive(scene))
    {
        writeError(error, errorSize, "stale scene handle");
        return 0;
    }
    if (!definition || !config)
    {
        writeError(error, errorSize, "ragdoll definition or config is null");
        return 0;
    }
    if (config->structSize < sizeof(OgsrPhysX2RagdollInstanceConfig))
    {
        writeError(error, errorSize, "instance config structSize is too small");
        return 0;
    }
    std::string message;
    if (!validateInstanceConfig(*config, &message))
    {
        writeError(error, errorSize, message);
        return 0;
    }
    try
    {
        if (!runtime().registry.createInstance(scene, *definition, *config, outHandle, &message))
        {
            writeError(error, errorSize, message);
            return 0;
        }
        return 1;
    }
    catch (const std::exception& exception)
    {
        writeError(error, errorSize, exception.what());
        return 0;
    }
    catch (...)
    {
        writeError(error, errorSize, "unknown exception in RagdollCreate");
        return 0;
    }
}

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollDestroy(OgsrPhysX2RagdollHandle handle, char* error,
                                                                        std::size_t errorSize)
{
    clearError(error, errorSize);
    auto* instance = instanceFor(handle);
    if (!instance)
    {
        writeError(error, errorSize, "stale ragdoll handle");
        return 0;
    }
    if (instance->isSceneStepping())
    {
        writeError(error, errorSize, "ragdoll cannot be destroyed during simulate/fetch");
        return 0;
    }
    if (faultInjectionOnce(g_ragdollFaultOnceConsumed, "OGSR_PHYSX2_FAULT_RAGDOLL_DESTROY_ONCE") ||
        faultInjectionEnabled("OGSR_PHYSX2_FAULT_RAGDOLL_DESTROY"))
    {
        writeError(error, errorSize, "fault injection: RagdollDestroy failed");
        return 0;
    }
    std::string message;
    if (!runtime().registry.destroyInstance(handle, &message))
    {
        writeError(error, errorSize, message);
        return 0;
    }
    return 1;
}

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollIsAlive(OgsrPhysX2RagdollHandle handle)
{
    return runtime().registry.isInstanceAlive(handle) ? 1 : 0;
}

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollGetPose(
    OgsrPhysX2RagdollHandle handle, OgsrPhysXRagdollPose* pose, float* bodyWorld, std::uint32_t maxBodies)
{
    auto* instance = instanceFor(handle);
    if (!instance || !pose)
        return 0;
    physics_portable::RagdollPose local;
    std::string message;
    if (!instance->pose(local, &message))
        return 0;

    const std::uint32_t bodyCount = static_cast<std::uint32_t>(local.bodyWorld.size());
    if (bodyCount == 0 || maxBodies < bodyCount || !bodyWorld)
        return 0;

    pose->bodyCount = bodyCount;
    pose->stepIndexLow = static_cast<std::uint32_t>(local.stepIndex & 0xFFFFFFFFu);
    pose->stepIndexHigh = static_cast<std::uint32_t>(local.stepIndex >> 32);
    physics_physx::v2::storeRow(local.entityWorld, pose->entityWorld);

    for (std::uint32_t body = 0; body < pose->bodyCount; ++body)
        physics_physx::v2::storeRow(local.bodyWorld[body], bodyWorld + static_cast<std::size_t>(body) * 16);
    return 1;
}

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollApplyImpulse(
    OgsrPhysX2RagdollHandle handle, std::uint16_t body, const float worldPosition[3], const float impulse[3],
    char* error, std::size_t errorSize)
{
    clearError(error, errorSize);
    auto* instance = instanceFor(handle);
    if (!instance)
    {
        writeError(error, errorSize, "stale ragdoll handle");
        return 0;
    }
    if (body >= instance->bodyCount() || !worldPosition || !impulse || !finiteVec3(worldPosition) ||
        !finiteVec3(impulse))
    {
        writeError(error, errorSize, "invalid impulse body or vector");
        return 0;
    }
    std::string message;
    if (!instance->applyImpulse(body, Vec3{worldPosition[0], worldPosition[1], worldPosition[2]},
                                Vec3{impulse[0], impulse[1], impulse[2]}, &message))
    {
        writeError(error, errorSize, message);
        return 0;
    }
    return 1;
}

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollApplyCentralImpulse(
    OgsrPhysX2RagdollHandle handle, std::uint16_t body, const float impulse[3], char* error,
    std::size_t errorSize)
{
    clearError(error, errorSize);
    auto* instance = instanceFor(handle);
    if (!instance)
    {
        writeError(error, errorSize, "stale ragdoll handle");
        return 0;
    }
    if (body >= instance->bodyCount() || !impulse || !finiteVec3(impulse))
    {
        writeError(error, errorSize, "invalid central impulse body or vector");
        return 0;
    }
    std::string message;
    if (!instance->applyCentralImpulse(body, Vec3{impulse[0], impulse[1], impulse[2]}, &message))
    {
        writeError(error, errorSize, message);
        return 0;
    }
    return 1;
}

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollGetBodyMass(
    OgsrPhysX2RagdollHandle handle, std::uint16_t body, OgsrPhysXRagdollBodyMass* mass, char* error,
    std::size_t errorSize)
{
    clearError(error, errorSize);
    auto* instance = instanceFor(handle);
    if (!instance || !mass)
    {
        writeError(error, errorSize, "stale ragdoll handle or null mass output");
        return 0;
    }
    if (body >= instance->bodyCount())
    {
        writeError(error, errorSize, "body index is out of range");
        return 0;
    }
    physics_physx::BodyMassProperties properties;
    std::string message;
    if (!instance->bodyMass(body, properties, &message))
    {
        writeError(error, errorSize, message);
        return 0;
    }
    *mass = OgsrPhysXRagdollBodyMass{};
    mass->body = body;
    mass->mass = properties.mass;
    physics_physx::v2::storeRow(properties.actorWorld, mass->actorWorld);
    physics_physx::v2::storeRow(properties.massWorld, mass->massWorld);
    mass->centerOfMassLocal[0] = properties.centerOfMassLocal.x;
    mass->centerOfMassLocal[1] = properties.centerOfMassLocal.y;
    mass->centerOfMassLocal[2] = properties.centerOfMassLocal.z;
    mass->worldCenterOfMass[0] = properties.worldCenterOfMass.x;
    mass->worldCenterOfMass[1] = properties.worldCenterOfMass.y;
    mass->worldCenterOfMass[2] = properties.worldCenterOfMass.z;
    mass->inertiaMassSpace[0] = properties.inertiaMassSpace.x;
    mass->inertiaMassSpace[1] = properties.inertiaMassSpace.y;
    mass->inertiaMassSpace[2] = properties.inertiaMassSpace.z;
    return 1;
}

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollGetInfo(OgsrPhysX2RagdollHandle handle,
                                                                        OgsrPhysXRagdollInfo* info)
{
    auto* instance = instanceFor(handle);
    if (!instance || !info)
        return 0;
    *info = instance->info();
    return 1;
}

extern "C" OGSR_PHYSX2_API int OGSR_PHYSX2_CALL OgsrPhysX2RagdollGetDiagnostics(
    OgsrPhysX2RagdollHandle handle, float referenceHeight, OgsrPhysXRagdollDiagnostics* diagnostics)
{
    auto* instance = instanceFor(handle);
    if (!instance || !diagnostics || !std::isfinite(referenceHeight))
        return 0;
    *diagnostics = instance->diagnostics(physics_portable::Vec3{0.0f, referenceHeight, 0.0f});
    return 1;
}
