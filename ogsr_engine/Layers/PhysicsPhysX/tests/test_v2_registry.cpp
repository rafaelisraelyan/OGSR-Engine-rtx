#include "physics_physx/physx_ragdoll_registry.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

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

constexpr std::uint32_t kMaxGeneration = std::numeric_limits<std::uint32_t>::max();

void resetAllocators()
{
    physics_physx::RagdollRegistry::resetGenerationAllocatorsForTest();
}

struct MockHooks final : physics_physx::RagdollRegistryHooks
{
    struct InstanceRecord
    {
        physics_physx::RagdollSceneContext scene;
        void* backend = nullptr;
    };

    int scenesCreated = 0;
    int scenesDestroyed = 0;
    int instancesCreated = 0;
    int instancesDestroyed = 0;
    bool failScene = false;
    bool failInstance = false;
    std::vector<void*> sceneBackends;
    std::vector<InstanceRecord> instances;
    std::vector<physics_physx::RagdollSceneContext> destroyedInstances;

    bool createScene(void** out, std::string* error) override
    {
        if (failScene)
        {
            if (error)
                *error = "mock scene failure";
            return false;
        }
        ++scenesCreated;
        void* backend = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000 + scenesCreated));
        sceneBackends.push_back(backend);
        *out = backend;
        return true;
    }

    void destroyScene(void* scene) override
    {
        (void)scene;
        ++scenesDestroyed;
    }

    bool createInstance(const physics_physx::RagdollSceneContext& scene, void** out, std::string* error) override
    {
        if (failInstance)
        {
            if (error)
                *error = "mock instance failure";
            return false;
        }
        ++instancesCreated;
        void* backend = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x2000 + instancesCreated));
        *out = backend;
        instances.push_back(InstanceRecord{scene, backend});
        return true;
    }

    void destroyInstance(const physics_physx::RagdollSceneContext& scene, void* instance) override
    {
        (void)instance;
        ++instancesDestroyed;
        destroyedInstances.push_back(scene);
    }
};

bool containsBackend(const std::vector<void*>& values, void* value)
{
    for (void* candidate : values)
    {
        if (candidate == value)
            return true;
    }
    return false;
}

void testRegistryARejectsHandlesFromRegistryB()
{
    resetAllocators();

    OgsrPhysX2SceneHandle sceneA{};
    OgsrPhysX2RagdollHandle instanceA{};
    MockHooks hooksA;
    {
        physics_physx::RagdollRegistry registryA(hooksA);
        check(registryA.createScene(&sceneA), "registry A scene create");
        check(registryA.createInstance(sceneA, &instanceA), "registry A instance create");
    }
    check(hooksA.instancesDestroyed == 1, "registry A destructor destroys its instance");
    check(hooksA.scenesDestroyed == 1, "registry A destructor destroys its scene");

    MockHooks hooksB;
    physics_physx::RagdollRegistry registryB(hooksB);
    OgsrPhysX2SceneHandle sceneB{};
    check(registryB.createScene(&sceneB), "registry B scene create");
    check(sceneB.generation != sceneA.generation, "registry B scene generation differs from A");
    check(!registryB.isSceneAlive(sceneA), "registry B rejects old scene A handle");

    OgsrPhysX2RagdollHandle instanceB{};
    check(registryB.createInstance(sceneB, &instanceB), "registry B instance create");
    check(instanceB.instanceGeneration != instanceA.instanceGeneration,
          "registry B instance generation differs from A");
    check(!registryB.isInstanceAlive(instanceA), "registry B rejects old instance A handle");
    check(registryB.collisionInstanceId(instanceA) == 0, "old instance A has no collision id in B");

    std::string error;
    check(!registryB.destroyScene(sceneA, &error), "registry B cannot destroy old scene A");
    check(!error.empty(), "old scene A destroy reports an error");
    error.clear();
    check(!registryB.destroyInstance(instanceA, &error), "registry B cannot destroy old instance A");
    check(!error.empty(), "old instance A destroy reports an error");
    check(registryB.isInstanceAlive(instanceB), "registry B own instance survives old handles");
    check(registryB.shutdown(), "registry B shutdown");
}

void testReuseAndStale()
{
    resetAllocators();
    MockHooks hooks;
    physics_physx::RagdollRegistry registry(hooks);

    OgsrPhysX2SceneHandle scene{};
    check(registry.createScene(&scene), "scene create");
    check(scene.generation != 0, "scene generation is nonzero");

    OgsrPhysX2RagdollHandle a{};
    check(registry.createInstance(scene, &a), "A create");
    check(a.instanceGeneration != 0, "A generation is nonzero");
    const std::uint32_t aId = registry.collisionInstanceId(a);
    check(aId != 0, "A collision id is nonzero");

    check(registry.destroyInstance(a), "A destroy");
    check(!registry.isInstanceAlive(a), "A is stale after destroy");
    check(registry.collisionInstanceId(a) == 0, "stale A collision id is zero");

    OgsrPhysX2RagdollHandle b{};
    check(registry.createInstance(scene, &b), "B create");
    check(b.instanceSlot == a.instanceSlot, "B reuses A's per-scene slot");
    check(b.instanceGeneration != a.instanceGeneration, "B has a new generation");
    check(registry.isInstanceAlive(b), "B is alive");
    check(!registry.isInstanceAlive(a), "A remains stale after B create");
    check(registry.collisionInstanceId(b) != aId, "B has a new collision id");

    std::string error;
    check(!registry.destroyInstance(a, &error), "stale A destroy is rejected");
    check(!error.empty(), "stale A destroy reports an error");
    check(registry.isInstanceAlive(b), "B is unaffected by stale A destroy");

    check(registry.destroyInstance(b), "B destroy");
    check(registry.destroyScene(scene), "scene destroy");
    check(!registry.isSceneAlive(scene), "scene is stale after destroy");
}

void testDoubleDestroyAndIndependent()
{
    resetAllocators();
    MockHooks hooks;
    physics_physx::RagdollRegistry registry(hooks);

    OgsrPhysX2SceneHandle scene{};
    check(registry.createScene(&scene), "double-destroy scene create");
    OgsrPhysX2RagdollHandle a{};
    OgsrPhysX2RagdollHandle b{};
    check(registry.createInstance(scene, &a), "double-destroy A create");
    check(registry.createInstance(scene, &b), "double-destroy B create");

    check(registry.destroyInstance(a), "A first destroy succeeds");
    std::string error;
    check(!registry.destroyInstance(a, &error), "A second destroy is rejected");
    check(!error.empty(), "A second destroy reports an error");
    check(registry.isInstanceAlive(b), "B survives A double destroy");
    check(hooks.instancesDestroyed == 1, "A double destroy releases only once");

    check(registry.destroyInstance(b), "B destroy");
    check(registry.isSceneAlive(scene), "scene survives instance destroys");
    check(registry.destroyScene(scene), "double-destroy scene cleanup");
}

void testSceneDestroyInvalidatesInstances()
{
    resetAllocators();
    MockHooks hooks;
    physics_physx::RagdollRegistry registry(hooks);

    OgsrPhysX2SceneHandle scene{};
    check(registry.createScene(&scene), "scene-destroy scene create");
    OgsrPhysX2RagdollHandle a{};
    OgsrPhysX2RagdollHandle b{};
    check(registry.createInstance(scene, &a), "scene-destroy A create");
    check(registry.createInstance(scene, &b), "scene-destroy B create");

    check(registry.destroyScene(scene), "scene destroy with two instances");
    check(!registry.isSceneAlive(scene), "scene handle invalid after destroy");
    check(!registry.isInstanceAlive(a), "A invalid after scene destroy");
    check(!registry.isInstanceAlive(b), "B invalid after scene destroy");
    check(hooks.instancesDestroyed == 2, "scene destroy defensively releases both instances");
    check(hooks.scenesDestroyed == 1, "scene backend destroyed once");

    std::string error;
    check(!registry.destroyInstance(a, &error), "stale instance destroy after scene destroy is rejected");
    check(!error.empty(), "stale instance destroy reports an error");
}

void testSceneReuse()
{
    resetAllocators();
    MockHooks hooks;
    physics_physx::RagdollRegistry registry(hooks);

    OgsrPhysX2SceneHandle first{};
    check(registry.createScene(&first), "reuse first scene create");
    check(registry.destroyScene(first), "reuse first scene destroy");

    OgsrPhysX2SceneHandle second{};
    check(registry.createScene(&second), "reuse second scene create");
    check(second.slot == first.slot, "scene slot is reused");
    check(second.generation != first.generation, "scene generation changes on reuse");
    check(!registry.isSceneAlive(first), "old scene handle is stale");
    check(registry.isSceneAlive(second), "new scene handle is alive");

    OgsrPhysX2RagdollHandle instance{};
    check(!registry.createInstance(first, &instance), "old scene handle cannot create an instance");
    check(instance.instanceGeneration == 0 && instance.sceneGeneration == 0,
          "failed old-scene create returns a zero handle");

    std::string error;
    check(!registry.destroyScene(first, &error), "old scene handle cannot destroy the new scene");
    check(!error.empty(), "old scene destroy reports an error");
    check(registry.isSceneAlive(second), "new scene survives old handle destroy");

    check(registry.destroyScene(second), "reuse second scene cleanup");
}

void testStaleRagdollAfterSceneReuse()
{
    resetAllocators();
    MockHooks hooks;
    physics_physx::RagdollRegistry registry(hooks);

    OgsrPhysX2SceneHandle first{};
    check(registry.createScene(&first), "stale-ragdoll first scene create");
    check(registry.destroyScene(first), "stale-ragdoll first scene destroy");

    OgsrPhysX2SceneHandle second{};
    check(registry.createScene(&second), "stale-ragdoll second scene create");
    OgsrPhysX2RagdollHandle live{};
    check(registry.createInstance(second, &live), "stale-ragdoll live instance create");

    OgsrPhysX2RagdollHandle old{first.slot, first.generation, live.instanceSlot, live.instanceGeneration};
    check(!registry.isInstanceAlive(old), "old scene generation cannot address new instance");
    check(registry.collisionInstanceId(old) == 0, "old scene generation has no collision id");

    std::string error;
    check(!registry.destroyInstance(old, &error), "old scene generation cannot destroy new instance");
    check(!error.empty(), "old scene generation destroy reports an error");
    check(registry.isInstanceAlive(live), "live instance survives old handle destroy");

    check(registry.destroyInstance(live), "stale-ragdoll live destroy");
    check(registry.destroyScene(second), "stale-ragdoll scene cleanup");
}

void testSceneAwareHooks()
{
    resetAllocators();
    MockHooks hooks;
    physics_physx::RagdollRegistry registry(hooks);

    OgsrPhysX2SceneHandle sceneA{};
    OgsrPhysX2SceneHandle sceneB{};
    check(registry.createScene(&sceneA), "scene-aware A create");
    check(registry.createScene(&sceneB), "scene-aware B create");
    check(sceneA.slot != sceneB.slot, "scene-aware scenes use different slots");

    OgsrPhysX2RagdollHandle a1{};
    OgsrPhysX2RagdollHandle a2{};
    OgsrPhysX2RagdollHandle b1{};
    OgsrPhysX2RagdollHandle b2{};
    check(registry.createInstance(sceneA, &a1), "scene-aware A1 create");
    check(registry.createInstance(sceneA, &a2), "scene-aware A2 create");
    check(registry.createInstance(sceneB, &b1), "scene-aware B1 create");
    check(registry.createInstance(sceneB, &b2), "scene-aware B2 create");

    check(hooks.instances.size() == 4, "scene-aware hooks recorded four instances");
    check(hooks.instances[0].scene.handle.slot == sceneA.slot, "A1 hook gets scene A handle");
    check(hooks.instances[1].scene.handle.slot == sceneA.slot, "A2 hook gets scene A handle");
    check(hooks.instances[2].scene.handle.slot == sceneB.slot, "B1 hook gets scene B handle");
    check(hooks.instances[3].scene.handle.slot == sceneB.slot, "B2 hook gets scene B handle");
    check(hooks.instances[0].scene.backend == hooks.instances[1].scene.backend, "A instances share scene A backend");
    check(hooks.instances[2].scene.backend == hooks.instances[3].scene.backend, "B instances share scene B backend");
    check(hooks.instances[0].scene.backend != hooks.instances[2].scene.backend, "A and B backends differ");
    check(containsBackend(hooks.sceneBackends, hooks.instances[0].scene.backend), "A backend is a created scene backend");
    check(containsBackend(hooks.sceneBackends, hooks.instances[2].scene.backend), "B backend is a created scene backend");
    check(a1.sceneSlot == sceneA.slot && a1.sceneGeneration == sceneA.generation, "A1 handle points to scene A");
    check(b1.sceneSlot == sceneB.slot && b1.sceneGeneration == sceneB.generation, "B1 handle points to scene B");

    check(registry.destroyScene(sceneA), "scene-aware A destroy");
    check(!registry.isSceneAlive(sceneA), "scene-aware A invalid");
    check(!registry.isInstanceAlive(a1) && !registry.isInstanceAlive(a2), "scene-aware A instances invalid");
    check(registry.isSceneAlive(sceneB), "scene-aware B survives A destroy");
    check(registry.isInstanceAlive(b1) && registry.isInstanceAlive(b2), "scene-aware B instances survive A destroy");
    check(hooks.instancesDestroyed == 2, "scene-aware A destroy releases two instances");
    check(hooks.scenesDestroyed == 1, "scene-aware A destroy releases one scene");
    for (const physics_physx::RagdollSceneContext& context : hooks.destroyedInstances)
    {
        check(context.handle.slot == sceneA.slot, "destroyed instance context belongs to scene A");
        check(context.backend == hooks.instances[0].scene.backend, "destroyed instance context uses A backend");
    }

    const int createdBeforeFailure = hooks.instancesCreated;
    hooks.failInstance = true;
    OgsrPhysX2RagdollHandle failed{};
    std::string error;
    check(!registry.createInstance(sceneB, &failed, &error), "scene-aware failed create is rejected");
    check(!error.empty(), "scene-aware failed create reports an error");
    check(failed.instanceGeneration == 0, "scene-aware failed create returns zero handle");
    check(hooks.instancesCreated == createdBeforeFailure, "scene-aware failed create creates no backend");
    check(registry.isInstanceAlive(b1) && registry.isInstanceAlive(b2), "scene-aware B instances survive failed create");
    hooks.failInstance = false;

    OgsrPhysX2RagdollHandle recovered{};
    check(registry.createInstance(sceneB, &recovered), "scene-aware create after failure");
    check(registry.isInstanceAlive(recovered), "scene-aware recovered instance alive");
    check(registry.shutdown(), "scene-aware shutdown");
}

void testShutdown()
{
    resetAllocators();
    MockHooks hooks;
    physics_physx::RagdollRegistry registry(hooks);

    OgsrPhysX2SceneHandle scene{};
    check(registry.createScene(&scene), "shutdown scene create");
    OgsrPhysX2RagdollHandle a{};
    OgsrPhysX2RagdollHandle b{};
    check(registry.createInstance(scene, &a), "shutdown A create");
    check(registry.createInstance(scene, &b), "shutdown B create");
    check(registry.hasActiveResources(), "shutdown registry has active resources");

    std::string error;
    check(registry.shutdown(&error), "shutdown succeeds");
    check(error.empty(), "shutdown has no error");
    check(hooks.instancesDestroyed == 2, "shutdown destroys exactly two instances");
    check(hooks.scenesDestroyed == 1, "shutdown destroys exactly one scene");
    check(!registry.hasActiveResources(), "shutdown leaves no active resources");
    check(registry.isShutdown(), "registry reports shutdown");
    check(!registry.isSceneAlive(scene), "shutdown invalidates scene");
    check(!registry.isInstanceAlive(a) && !registry.isInstanceAlive(b), "shutdown invalidates instances");

    const int instancesDestroyed = hooks.instancesDestroyed;
    const int scenesDestroyed = hooks.scenesDestroyed;
    check(registry.shutdown(&error), "second shutdown is safe");
    check(hooks.instancesDestroyed == instancesDestroyed, "second shutdown does not destroy instances again");
    check(hooks.scenesDestroyed == scenesDestroyed, "second shutdown does not destroy scene again");

    OgsrPhysX2SceneHandle after{};
    error.clear();
    check(!registry.createScene(&after, &error), "create after shutdown is rejected");
    check(!error.empty(), "create after shutdown reports an error");
    error.clear();
    check(!registry.destroyScene(scene, &error), "destroy after shutdown is rejected");
    check(!registry.destroyInstance(a, &error), "instance destroy after shutdown is rejected");
}

void testDestructorShutdown()
{
    resetAllocators();
    MockHooks hooks;
    {
        physics_physx::RagdollRegistry registry(hooks);
        OgsrPhysX2SceneHandle scene{};
        check(registry.createScene(&scene), "destructor scene create");
        OgsrPhysX2RagdollHandle a{};
        OgsrPhysX2RagdollHandle b{};
        check(registry.createInstance(scene, &a), "destructor A create");
        check(registry.createInstance(scene, &b), "destructor B create");
    }
    check(hooks.instancesDestroyed == 2, "destructor releases two instances");
    check(hooks.scenesDestroyed == 1, "destructor releases one scene");
}

void testPerSceneCapacity()
{
    resetAllocators();
    MockHooks hooks;
    physics_physx::RagdollRegistryConfig config;
    config.sceneCapacity = 2;
    config.instanceCapacity = OGSR_PHYSX2_INSTANCE_CAPACITY;
    physics_physx::RagdollRegistry registry(hooks, config);

    OgsrPhysX2SceneHandle sceneA{};
    OgsrPhysX2SceneHandle sceneB{};
    check(registry.createScene(&sceneA), "per-scene capacity A create");
    check(registry.createScene(&sceneB), "per-scene capacity B create");

    std::vector<OgsrPhysX2RagdollHandle> instancesA;
    std::vector<OgsrPhysX2RagdollHandle> instancesB;
    for (std::uint32_t index = 0; index < OGSR_PHYSX2_INSTANCE_CAPACITY; ++index)
    {
        OgsrPhysX2RagdollHandle a{};
        OgsrPhysX2RagdollHandle b{};
        check(registry.createInstance(sceneA, &a), "per-scene capacity A instance create");
        check(registry.createInstance(sceneB, &b), "per-scene capacity B instance create");
        instancesA.push_back(a);
        instancesB.push_back(b);
    }
    check(instancesA.size() == OGSR_PHYSX2_INSTANCE_CAPACITY, "A reaches declared per-scene capacity");
    check(instancesB.size() == OGSR_PHYSX2_INSTANCE_CAPACITY, "B reaches declared per-scene capacity");
    check(registry.isInstanceAlive(instancesA.front()) && registry.isInstanceAlive(instancesA.back()),
          "A first and last instances alive");
    check(registry.isInstanceAlive(instancesB.front()) && registry.isInstanceAlive(instancesB.back()),
          "B first and last instances alive");

    OgsrPhysX2RagdollHandle overflowA{};
    OgsrPhysX2RagdollHandle overflowB{};
    std::string error;
    check(!registry.createInstance(sceneA, &overflowA, &error), "A per-scene capacity is enforced");
    check(!error.empty(), "A capacity error is reported");
    error.clear();
    check(!registry.createInstance(sceneB, &overflowB, &error), "B per-scene capacity is enforced");
    check(!error.empty(), "B capacity error is reported");

    check(registry.destroyInstance(instancesA.front()), "A capacity slot release");
    OgsrPhysX2RagdollHandle reusedA{};
    check(registry.createInstance(sceneA, &reusedA), "A capacity slot reuse");
    check(reusedA.instanceSlot == instancesA.front().instanceSlot, "A reuses the freed per-scene slot");

    check(registry.destroyScene(sceneA), "per-scene capacity A destroy");
    check(registry.isSceneAlive(sceneB), "per-scene capacity B survives A destroy");
    check(registry.isInstanceAlive(instancesB.front()) && registry.isInstanceAlive(instancesB.back()),
          "B instances survive A destroy");
    check(registry.shutdown(), "per-scene capacity shutdown");
}

void testSceneGenerationNearOverflow()
{
    resetAllocators();
    MockHooks hooks;
    physics_physx::RagdollRegistryConfig config;
    config.sceneCapacity = 2;
    config.instanceCapacity = 2;
    physics_physx::RagdollRegistry registry(hooks, config);

    check(physics_physx::RagdollRegistry::setNextSceneGenerationForTest(kMaxGeneration - 1),
          "inject near-max scene generation");
    OgsrPhysX2SceneHandle first{};
    check(registry.createScene(&first), "scene-overflow first create");
    check(first.generation == kMaxGeneration - 1, "scene generation reaches max-1");
    const std::uint32_t firstSlot = first.slot;
    check(registry.destroyScene(first), "scene-overflow first destroy");

    OgsrPhysX2SceneHandle last{};
    check(registry.createScene(&last), "scene-overflow last create");
    check(last.generation == kMaxGeneration, "scene generation reaches UINT32_MAX");
    check(registry.destroyScene(last), "scene-overflow last destroy");

    OgsrPhysX2SceneHandle exhausted{};
    std::string error;
    check(!registry.createScene(&exhausted, &error), "scene generation exhaustion fails");
    check(!error.empty(), "scene generation exhaustion reports an error");

    check(physics_physx::RagdollRegistry::setNextSceneGenerationForTest(1), "reset scene generation for retirement check");
    OgsrPhysX2SceneHandle next{};
    check(registry.createScene(&next), "scene-overflow next create after reset");
    check(next.slot != firstSlot, "retired max-generation scene slot is not reused");
    check(registry.shutdown(), "scene-overflow shutdown");
}

void testInstanceGenerationNearOverflow()
{
    resetAllocators();
    MockHooks hooks;
    physics_physx::RagdollRegistryConfig config;
    config.sceneCapacity = 1;
    config.instanceCapacity = 2;
    physics_physx::RagdollRegistry registry(hooks, config);

    OgsrPhysX2SceneHandle scene{};
    check(registry.createScene(&scene), "instance-overflow scene create");
    check(physics_physx::RagdollRegistry::setNextInstanceGenerationForTest(kMaxGeneration - 1),
          "inject near-max instance generation");
    OgsrPhysX2RagdollHandle first{};
    check(registry.createInstance(scene, &first), "instance-overflow first create");
    check(first.instanceGeneration == kMaxGeneration - 1, "instance generation reaches max-1");
    const std::uint32_t firstSlot = first.instanceSlot;
    check(registry.destroyInstance(first), "instance-overflow first destroy");

    OgsrPhysX2RagdollHandle last{};
    check(registry.createInstance(scene, &last), "instance-overflow last create");
    check(last.instanceGeneration == kMaxGeneration, "instance generation reaches UINT32_MAX");
    check(registry.destroyInstance(last), "instance-overflow last destroy");

    OgsrPhysX2RagdollHandle exhausted{};
    std::string error;
    check(!registry.createInstance(scene, &exhausted, &error), "instance generation exhaustion fails");
    check(!error.empty(), "instance generation exhaustion reports an error");

    check(physics_physx::RagdollRegistry::setNextInstanceGenerationForTest(1),
          "reset instance generation for retirement check");
    OgsrPhysX2RagdollHandle next{};
    check(registry.createInstance(scene, &next), "instance-overflow next create after reset");
    check(next.instanceSlot != firstSlot, "retired max-generation instance slot is not reused");
    check(registry.shutdown(), "instance-overflow shutdown");
}

void testGenerationMaxRelease()
{
    resetAllocators();
    MockHooks hooks;
    physics_physx::RagdollRegistryConfig config;
    config.sceneCapacity = 1;
    config.instanceCapacity = 1;
    physics_physx::RagdollRegistry registry(hooks, config);

    check(physics_physx::RagdollRegistry::setNextSceneGenerationForTest(kMaxGeneration),
          "inject max scene generation");
    OgsrPhysX2SceneHandle scene{};
    check(registry.createScene(&scene), "max scene generation create");
    check(scene.generation == kMaxGeneration, "max scene generation reached");
    check(registry.destroyScene(scene), "max scene generation destroy");
    check(!registry.isSceneAlive(scene), "max scene generation invalidated");
    OgsrPhysX2SceneHandle nextScene{};
    check(!registry.createScene(&nextScene), "max scene generation cannot wrap and reuse");

    resetAllocators();
    MockHooks instanceHooks;
    physics_physx::RagdollRegistry instanceRegistry(instanceHooks, config);
    OgsrPhysX2SceneHandle host{};
    check(instanceRegistry.createScene(&host), "max instance generation host create");
    check(physics_physx::RagdollRegistry::setNextInstanceGenerationForTest(kMaxGeneration),
          "inject max instance generation");
    OgsrPhysX2RagdollHandle instance{};
    check(instanceRegistry.createInstance(host, &instance), "max instance generation create");
    check(instance.instanceGeneration == kMaxGeneration, "max instance generation reached");
    check(instanceRegistry.destroyInstance(instance), "max instance generation destroy");
    OgsrPhysX2RagdollHandle nextInstance{};
    check(!instanceRegistry.createInstance(host, &nextInstance), "max instance generation cannot wrap and reuse");
    check(instanceRegistry.shutdown(), "max instance generation host cleanup");
}

void testCollisionIdExhaustion()
{
    resetAllocators();
    MockHooks hooks;
    physics_physx::RagdollRegistry registry(hooks);

    OgsrPhysX2SceneHandle scene{};
    check(registry.createScene(&scene), "collision-exhaustion scene create");
    registry.setNextCollisionInstanceIdForTest(kMaxGeneration);

    OgsrPhysX2RagdollHandle failed{};
    std::string error;
    check(!registry.createInstance(scene, &failed, &error), "collision id exhaustion rejects create");
    check(!error.empty(), "collision id exhaustion reports an error");
    check(failed.instanceGeneration == 0, "collision id exhaustion returns a zero handle");
    check(registry.isSceneAlive(scene), "scene survives collision id exhaustion");
    check(hooks.instancesCreated == 0, "collision id exhaustion creates no backend");
    check(hooks.instancesDestroyed == 0, "collision id exhaustion destroys no backend");

    registry.setNextCollisionInstanceIdForTest(1);
    OgsrPhysX2RagdollHandle recovered{};
    check(registry.createInstance(scene, &recovered), "create after collision id reset");
    check(registry.collisionInstanceId(recovered) == 1, "collision id restarts at one");
    check(registry.shutdown(), "collision-exhaustion shutdown");
}

void testInvalidHandles()
{
    resetAllocators();
    MockHooks hooks;
    physics_physx::RagdollRegistry registry(hooks);

    OgsrPhysX2SceneHandle zeroScene{};
    check(!registry.isSceneAlive(zeroScene), "zero scene handle is invalid");
    OgsrPhysX2SceneHandle wrongScene{999, 1};
    check(!registry.isSceneAlive(wrongScene), "out-of-range scene slot is invalid");

    OgsrPhysX2RagdollHandle zeroRagdoll{};
    check(!registry.isInstanceAlive(zeroRagdoll), "zero ragdoll handle is invalid");
    OgsrPhysX2RagdollHandle wrongRagdoll{0, 1, 999, 1};
    check(!registry.isInstanceAlive(wrongRagdoll), "out-of-range instance slot is invalid");
    check(registry.collisionInstanceId(zeroRagdoll) == 0, "zero ragdoll collision id is zero");

    std::string error;
    check(!registry.destroyScene(zeroScene, &error), "zero scene destroy is rejected");
    check(!error.empty(), "zero scene destroy reports an error");
    error.clear();
    check(!registry.destroyInstance(zeroRagdoll, &error), "zero ragdoll destroy is rejected");
    check(!error.empty(), "zero ragdoll destroy reports an error");
}

void testPartialCreateFailure()
{
    resetAllocators();
    MockHooks hooks;
    physics_physx::RagdollRegistry registry(hooks);

    hooks.failScene = true;
    OgsrPhysX2SceneHandle failedScene{};
    std::string error;
    check(!registry.createScene(&failedScene, &error), "scene backend failure rejects create");
    check(!error.empty(), "scene backend failure reports an error");
    check(failedScene.generation == 0, "scene backend failure returns a zero handle");
    hooks.failScene = false;

    OgsrPhysX2SceneHandle scene{};
    check(registry.createScene(&scene), "scene create after failure");
    OgsrPhysX2RagdollHandle b{};
    check(registry.createInstance(scene, &b), "partial-failure B create");

    const int createdBefore = hooks.instancesCreated;
    hooks.failInstance = true;
    OgsrPhysX2RagdollHandle failedInstance{};
    check(!registry.createInstance(scene, &failedInstance, &error), "instance backend failure rejects create");
    check(failedInstance.instanceGeneration == 0, "instance backend failure returns a zero handle");
    check(hooks.instancesCreated == createdBefore, "failed instance create creates no backend");
    check(hooks.instancesDestroyed == 0, "failed instance create destroys no backend");
    hooks.failInstance = false;

    OgsrPhysX2RagdollHandle c{};
    check(registry.createInstance(scene, &c), "instance create after failure");
    check(c.instanceSlot != b.instanceSlot, "failed create did not leak its per-scene slot");
    check(registry.isInstanceAlive(b), "B survives failed instance create");
    check(registry.isInstanceAlive(c), "C is alive after recovery");

    check(registry.shutdown(), "partial-failure shutdown");
}

void testCapacityExhaustion()
{
    resetAllocators();
    MockHooks hooks;
    physics_physx::RagdollRegistryConfig config;
    config.sceneCapacity = 1;
    config.instanceCapacity = 2;
    physics_physx::RagdollRegistry registry(hooks, config);

    OgsrPhysX2SceneHandle scene{};
    check(registry.createScene(&scene), "capacity scene create");
    OgsrPhysX2SceneHandle secondScene{};
    std::string error;
    check(!registry.createScene(&secondScene, &error), "scene capacity is enforced");
    check(!error.empty(), "scene capacity error is reported");

    OgsrPhysX2RagdollHandle a{};
    OgsrPhysX2RagdollHandle b{};
    OgsrPhysX2RagdollHandle c{};
    check(registry.createInstance(scene, &a), "capacity A create");
    check(registry.createInstance(scene, &b), "capacity B create");
    error.clear();
    check(!registry.createInstance(scene, &c, &error), "per-scene instance capacity is enforced");
    check(!error.empty(), "instance capacity error is reported");

    check(registry.destroyInstance(a), "capacity A destroy");
    check(registry.createInstance(scene, &c), "capacity slot reuse after destroy");
    check(c.instanceSlot == a.instanceSlot, "capacity reused per-scene slot is the freed slot");
    check(registry.shutdown(), "capacity shutdown");
}

} // namespace

int main()
{
    testRegistryARejectsHandlesFromRegistryB();
    testReuseAndStale();
    testDoubleDestroyAndIndependent();
    testSceneDestroyInvalidatesInstances();
    testSceneReuse();
    testStaleRagdollAfterSceneReuse();
    testSceneAwareHooks();
    testShutdown();
    testDestructorShutdown();
    testPerSceneCapacity();
    testSceneGenerationNearOverflow();
    testInstanceGenerationNearOverflow();
    testGenerationMaxRelease();
    testCollisionIdExhaustion();
    testInvalidHandles();
    testPartialCreateFailure();
    testCapacityExhaustion();

    std::printf("physics_physx_v2_registry tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
