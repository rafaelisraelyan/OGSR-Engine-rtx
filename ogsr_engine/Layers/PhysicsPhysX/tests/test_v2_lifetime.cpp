#include "physics_physx/physx_ragdoll_c.h"
#include "physics_physx/physx_ragdoll_c_v2.h"
#include "physics_physx/physx_ragdoll_v2_loader.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

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

OgsrPhysXRagdollConfig legacyConfig()
{
    OgsrPhysXRagdollConfig config{};
    config.fixedTimestep = 0.01f;
    config.gravityY = -19.62f;
    config.workerThreads = 2;
    config.maxSubsteps = 4;
    config.solverPositionIterations = 16;
    config.solverVelocityIterations = 4;
    config.contactOffset = 0.02f;
    config.restOffset = 0.0f;
    config.enableStabilization = 1;
    config.staticRootDensity = 300.0f;
    config.promoteStaticRoot = 1;
    config.linearDamping = 0.05f;
    config.angularDamping = 0.2f;
    config.disableSelfCollision = 1;
    config.collectDiagnostics = 1;
    return config;
}

OgsrPhysX2SceneConfig multiConfig()
{
    OgsrPhysX2SceneConfig config{};
    config.structSize = sizeof(config);
    config.fixedTimestep = 0.01f;
    config.gravityY = -19.62f;
    config.workerThreads = 2;
    config.maxSubsteps = 4;
    config.solverPositionIterations = 16;
    config.solverVelocityIterations = 4;
    config.contactOffset = 0.02f;
    config.restOffset = 0.0f;
    config.enableStabilization = 1;
    config.collectContacts = 1;
    config.maxInstances = 2;
    return config;
}

bool createMulti(OgsrPhysX2SceneHandle& handle, char* error, std::size_t errorSize)
{
    const OgsrPhysX2SceneConfig config = multiConfig();
    return OgsrPhysX2SceneCreate(&config, &handle, error, errorSize) == 1;
}

bool contains(const char* text, const char* needle)
{
    return text && needle && std::strstr(text, needle) != nullptr;
}

void identity(float* matrix)
{
    std::memset(matrix, 0, 16 * sizeof(float));
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

struct LegacyFixture
{
    OgsrPhysXRagdollBone bone{};
    OgsrPhysXRagdollBody body{};
    OgsrPhysXRagdollShape shape{};
    OgsrPhysXRagdollMaterial material{};
    float entityWorld[16]{};
    float rootToEntity[16]{};
    float bodyWorld[16]{};
    float linear[3]{};
    float angular[3]{};
    std::uint8_t enabled[1]{1};
    OgsrPhysXRagdollDefinition definition{};

    void build(float x, float y, float z)
    {
        identity(entityWorld);
        entityWorld[12] = x;
        entityWorld[13] = y;
        entityWorld[14] = z;
        identity(rootToEntity);
        identity(bodyWorld);
        bodyWorld[12] = x;
        bodyWorld[13] = y;
        bodyWorld[14] = z;

        identity(bone.bindLocal);
        bone.id = 0;
        bone.parent = 0xFFFF;
        bone.visible = 1;
        bone.disposition = 0;
        bone.body = 0;
        std::snprintf(bone.name, sizeof(bone.name), "root");

        material.id = 0;
        material.friction = 0.8f;
        material.restitution = 0.0f;
        material.spring = 1.0f;
        material.damping = 1.0f;

        body.id = 0;
        body.boneId = 0;
        body.parentBody = 0xFFFF;
        body.role = 0;
        body.isFixed = 0;
        body.mass = 1.0f;
        body.centerOfMass[0] = body.centerOfMass[1] = body.centerOfMass[2] = 0.0f;
        body.inertia[0] = body.inertia[3] = body.inertia[5] = 0.1f;
        identity(body.bindPose);
        body.firstShape = 0;
        body.shapeCount = 1;
        body.collisionGroup = 0;

        shape.type = 1;
        identity(shape.local);
        shape.radius = 0.25f;
        shape.materialId = 0;
        shape.sourceBoneId = 0;
        shape.flags = 0;

        definition.rootBody = 0;
        definition.boneCount = 1;
        definition.bodyCount = 1;
        definition.jointCount = 0;
        definition.shapeCount = 1;
        definition.materialCount = 1;
        definition.bones = &bone;
        definition.bodies = &body;
        definition.joints = nullptr;
        definition.shapes = &shape;
        definition.materials = &material;
        std::memcpy(definition.entityWorld, entityWorld, sizeof(entityWorld));
        std::memcpy(definition.rootToEntity, rootToEntity, sizeof(rootToEntity));
        definition.initialBodyWorld = bodyWorld;
        definition.initialLinearVelocity = linear;
        definition.initialAngularVelocity = angular;
        definition.initialEnabled = enabled;
    }
};

bool createLegacy(OgsrPhysXRuntimeHandle& handle, char* error, std::size_t errorSize)
{
    const OgsrPhysXRagdollConfig config = legacyConfig();
    handle = OgsrPhysXRagdollRuntimeCreate(&config, error, errorSize);
    if (!handle)
        return false;

    // The legacy world acquires PhysX extensions when its first instance is
    // created, not when the runtime object is allocated.
    LegacyFixture fixture;
    fixture.build(0.0f, 1.0f, 0.0f);
    if (OgsrPhysXRagdollCreate(handle, &fixture.definition, error, errorSize) != 1)
    {
        OgsrPhysXRagdollRuntimeDestroy(handle);
        handle = nullptr;
        return false;
    }
    return true;
}

bool zeroHandle(const OgsrPhysX2SceneHandle& handle)
{
    return handle.slot == 0 && handle.generation == 0;
}
} // namespace

int main()
{
    char error[512] = {};

    // Each backend works alone.
    OgsrPhysXRuntimeHandle legacy = nullptr;
    check(createLegacy(legacy, error, sizeof(error)), "legacy v1 create alone");
    OgsrPhysXRagdollRuntimeDestroy(legacy);

    OgsrPhysX2SceneHandle multi{};
    check(createMulti(multi, error, sizeof(error)), "multi v2 create alone");
    check(OgsrPhysX2SceneDestroy(multi, error, sizeof(error)) == 1, "multi v2 destroy alone");
    check(OgsrPhysX2SceneDestroy(multi, error, sizeof(error)) == 0, "multi v2 double destroy rejected");

    // v1 owns the extensions: v2 must fail closed, then succeed after v1 dies.
    check(createLegacy(legacy, error, sizeof(error)), "overlap: legacy create");
    OgsrPhysX2SceneHandle rejected{};
    error[0] = 0;
    check(!createMulti(rejected, error, sizeof(error)), "v2 create rejected while v1 owns extensions");
    check(contains(error, "extensions") || contains(error, "foundation"),
          "v2 rejection names the SDK lifetime lease");
    check(zeroHandle(rejected), "rejected v2 scene publishes no handle");
    OgsrPhysXRagdollRuntimeDestroy(legacy);
    check(createMulti(multi, error, sizeof(error)), "v2 create succeeds after v1 destroy");
    check(OgsrPhysX2SceneDestroy(multi, nullptr, 0) == 1, "v2 cleanup after v1 destroy");

    // v2 owns the extensions: v1 must fail closed, then succeed after v2 dies.
    check(createMulti(multi, error, sizeof(error)), "overlap: v2 create");
    OgsrPhysXRuntimeHandle rejectedLegacy = nullptr;
    error[0] = 0;
    check(!createLegacy(rejectedLegacy, error, sizeof(error)), "v1 create rejected while v2 owns extensions");
    check(contains(error, "extensions") || contains(error, "foundation"),
          "v1 rejection names the SDK lifetime lease");
    check(rejectedLegacy == nullptr, "rejected v1 runtime publishes no handle");
    check(OgsrPhysX2SceneDestroy(multi, nullptr, 0) == 1, "v2 cleanup before v1 retry");
    check(createLegacy(legacy, error, sizeof(error)), "v1 create succeeds after v2 destroy");
    OgsrPhysXRagdollRuntimeDestroy(legacy);

    // A partial v2 failure must not poison the lease.
    OgsrPhysX2SceneConfig bad = multiConfig();
    bad.fixedTimestep = 0.0f;
    OgsrPhysX2SceneHandle badHandle{};
    check(OgsrPhysX2SceneCreate(&bad, &badHandle, nullptr, 0) == 0, "invalid v2 config rejected");
    check(createLegacy(legacy, error, sizeof(error)), "v1 acquires after partial v2 failure");
    OgsrPhysXRagdollRuntimeDestroy(legacy);
    check(createMulti(multi, error, sizeof(error)), "v2 acquires after partial v2 failure");
    check(OgsrPhysX2SceneDestroy(multi, nullptr, 0) == 1, "partial-failure v2 cleanup");

    // Repeated alternating loads (level reload analogue).
    for (int iteration = 0; iteration < 5; ++iteration)
    {
        check(createLegacy(legacy, error, sizeof(error)), "loop v1 create");
        OgsrPhysXRagdollRuntimeDestroy(legacy);
        check(createMulti(multi, error, sizeof(error)), "loop v2 create");
        check(OgsrPhysX2SceneDestroy(multi, nullptr, 0) == 1, "loop v2 destroy");
    }

    // DLL lease: negotiation and resource guard.
    physics_physx::V2ModuleLease lease;
    std::string leaseError;
    check(lease.load("ogsr_physx_runtime.dll", &leaseError), "lease loads the real v2 DLL");
    physics_physx::V2Requirements requirements;
    requirements.requiredCapabilities = OGSR_PHYSX2_CAP_REQUIRED_P3_1;
    requirements.minimumScenes = 1;
    requirements.minimumInstances = 2;
    const physics_physx::V2NegotiationResult negotiation = lease.negotiate(requirements);
    check(negotiation.status == physics_physx::V2NegotiationStatus::Ready, "lease negotiates Ready");

    check(createMulti(multi, error, sizeof(error)), "lease-guard scene create");
    lease.addResourceRef();
    std::string unloadError;
    check(!lease.unload(&unloadError), "unload is rejected while a v2 scene is alive");
    check(unloadError.find("active resource") != std::string::npos, "unload rejection names active resources");
    check(OgsrPhysX2SceneDestroy(multi, nullptr, 0) == 1, "lease-guard scene destroy");
    check(lease.releaseResourceRef(), "lease resource ref released");
    check(lease.unload(&leaseError), "unload succeeds after the scene is destroyed");
    check(!lease.isLoaded(), "lease reports unloaded");

    // Fault injection: SceneDestroy failure must keep the scene alive and the
    // lease resource ref; unload must be refused until a successful retry.
    check(lease.load("ogsr_physx_runtime.dll", &leaseError), "fault lease reload");
    check(createMulti(multi, error, sizeof(error)), "fault scene create");
    lease.addResourceRef();
    SetEnvironmentVariableA("OGSR_PHYSX2_FAULT_SCENE_DESTROY", "1");
    char faultError[512] = {};
    check(OgsrPhysX2SceneDestroy(multi, faultError, sizeof(faultError)) == 0,
          "faulted SceneDestroy is rejected");
    check(contains(faultError, "fault injection"), "faulted SceneDestroy names the injection");
    check(OgsrPhysX2SceneIsAlive(multi) == 1, "scene stays alive after a failed destroy");
    std::string faultUnloadError;
    check(!lease.unload(&faultUnloadError), "unload is refused after a failed SceneDestroy");
    SetEnvironmentVariableA("OGSR_PHYSX2_FAULT_SCENE_DESTROY", nullptr);
    check(OgsrPhysX2SceneDestroy(multi, nullptr, 0) == 1,
          "SceneDestroy succeeds after the fault is cleared");
    check(lease.releaseResourceRef(), "fault scene resource ref released");
    check(lease.unload(&leaseError), "unload succeeds after fault recovery");

    std::printf("physics_physx_v2_lifetime tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
