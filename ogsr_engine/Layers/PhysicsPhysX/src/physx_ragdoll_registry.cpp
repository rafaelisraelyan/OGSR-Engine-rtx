#include "physics_physx/physx_ragdoll_registry.h"

#include <atomic>
#include <limits>
#include <string>

namespace physics_physx
{
namespace
{
bool fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
    return false;
}

// Process-wide within the DLL/runtime lifetime. A fresh registry must never
// reissue a generation that a destroyed registry already handed out.
class GenerationAllocator
{
public:
    std::uint32_t allocate()
    {
        std::uint32_t current = next_.load(std::memory_order_acquire);
        for (;;)
        {
            if (exhausted_.load(std::memory_order_acquire))
                return 0;
            if (current == 0)
            {
                exhausted_.store(true, std::memory_order_release);
                return 0;
            }
            if (current == std::numeric_limits<std::uint32_t>::max())
            {
                if (next_.compare_exchange_weak(current, 0, std::memory_order_acq_rel))
                {
                    exhausted_.store(true, std::memory_order_release);
                    return std::numeric_limits<std::uint32_t>::max();
                }
                continue;
            }

            const std::uint32_t nextValue = current + 1;
            if (next_.compare_exchange_weak(current, nextValue, std::memory_order_acq_rel))
                return current;
        }
    }

    bool setNext(std::uint32_t next)
    {
        if (next == 0)
            return false;
        next_.store(next, std::memory_order_release);
        exhausted_.store(false, std::memory_order_release);
        return true;
    }

    void reset()
    {
        next_.store(1, std::memory_order_release);
        exhausted_.store(false, std::memory_order_release);
    }

private:
    std::atomic<std::uint32_t> next_{1};
    std::atomic<bool> exhausted_{false};
};

GenerationAllocator g_sceneGenerations;
GenerationAllocator g_instanceGenerations;
} // namespace

RagdollRegistry::RagdollRegistry(RagdollRegistryHooks& hooks, RagdollRegistryConfig config)
    : hooks_(hooks), config_(config)
{
    scenes_.resize(config_.sceneCapacity);
    for (SceneSlot& scene : scenes_)
        scene.instances.resize(config_.instanceCapacity);
}

RagdollRegistry::~RagdollRegistry()
{
    shutdown();
}

RagdollRegistry::SceneSlot* RagdollRegistry::resolveScene(OgsrPhysX2SceneHandle scene)
{
    if (shutdown_ || scene.generation == 0 || scene.slot >= scenes_.size())
        return nullptr;

    SceneSlot& slot = scenes_[scene.slot];
    if (!slot.alive || slot.retired || slot.generation != scene.generation)
        return nullptr;
    return &slot;
}

const RagdollRegistry::SceneSlot* RagdollRegistry::resolveScene(OgsrPhysX2SceneHandle scene) const
{
    return const_cast<RagdollRegistry*>(this)->resolveScene(scene);
}

RagdollRegistry::InstanceSlot* RagdollRegistry::resolveInstance(OgsrPhysX2RagdollHandle instance)
{
    if (shutdown_ || instance.sceneGeneration == 0 || instance.instanceGeneration == 0)
        return nullptr;
    if (instance.sceneSlot >= scenes_.size())
        return nullptr;

    // Scene generation is validated before the instance slot is touched.
    SceneSlot& scene = scenes_[instance.sceneSlot];
    if (!scene.alive || scene.retired || scene.generation != instance.sceneGeneration)
        return nullptr;
    if (instance.instanceSlot >= scene.instances.size())
        return nullptr;

    InstanceSlot& slot = scene.instances[instance.instanceSlot];
    if (!slot.alive || slot.retired || slot.generation != instance.instanceGeneration)
        return nullptr;
    if (slot.sceneSlot != instance.sceneSlot || slot.sceneGeneration != instance.sceneGeneration)
        return nullptr;
    return &slot;
}

const RagdollRegistry::InstanceSlot* RagdollRegistry::resolveInstance(OgsrPhysX2RagdollHandle instance) const
{
    return const_cast<RagdollRegistry*>(this)->resolveInstance(instance);
}

void RagdollRegistry::invalidateSceneSlot(SceneSlot& slot)
{
    slot.alive = false;
    slot.backend = nullptr;
    if (slot.generation == std::numeric_limits<std::uint32_t>::max())
        slot.retired = true;
}

void RagdollRegistry::invalidateInstanceSlot(InstanceSlot& slot)
{
    slot.alive = false;
    slot.backend = nullptr;
    slot.sceneSlot = 0;
    slot.sceneGeneration = 0;
    slot.collisionInstanceId = 0;
    if (slot.generation == std::numeric_limits<std::uint32_t>::max())
        slot.retired = true;
}

RagdollSceneContext RagdollRegistry::sceneContext(const SceneSlot& scene, OgsrPhysX2SceneHandle handle) const
{
    RagdollSceneContext context;
    context.handle = handle;
    context.backend = scene.backend;
    return context;
}

bool RagdollRegistry::createScene(const OgsrPhysX2SceneConfig& config, OgsrPhysX2SceneHandle* outScene,
                                  std::string* error)
{
    if (!outScene)
        return fail(error, "scene output is null");
    *outScene = OgsrPhysX2SceneHandle{};
    if (shutdown_)
        return fail(error, "registry is shut down");

    for (std::uint32_t index = 0; index < scenes_.size(); ++index)
    {
        SceneSlot& slot = scenes_[index];
        if (slot.alive || slot.retired)
            continue;

        const std::uint32_t generation = g_sceneGenerations.allocate();
        if (generation == 0)
            return fail(error, "scene generation space exhausted");

        void* backend = nullptr;
        std::string hookError;
        if (!hooks_.createScene(config, &backend, &hookError))
        {
            return fail(error, hookError.empty() ? "scene backend create failed"
                                                 : "scene backend create failed: " + hookError);
        }

        // A scene slot may be reused only after all of its instance handles
        // have been invalidated. Clear the per-scene table; new instance
        // generations still come from the process-wide allocator.
        for (InstanceSlot& instance : slot.instances)
            instance = InstanceSlot{};

        slot.generation = generation;
        slot.alive = true;
        slot.backend = backend;
        outScene->slot = index;
        outScene->generation = generation;
        return true;
    }

    return fail(error, "scene registry capacity exhausted");
}

bool RagdollRegistry::destroyScene(OgsrPhysX2SceneHandle scene, std::string* error)
{
    SceneSlot* slot = resolveScene(scene);
    if (!slot)
        return fail(error, "stale scene handle");

    // Defensive backend cleanup. OGSR must still remove game bone callbacks
    // before calling this; the registry cannot know about IKinematics.
    const RagdollSceneContext context = sceneContext(*slot, scene);
    for (InstanceSlot& instance : slot->instances)
    {
        if (!instance.alive)
            continue;
        hooks_.destroyInstance(context, instance.backend);
        invalidateInstanceSlot(instance);
    }

    hooks_.destroyScene(slot->backend);
    invalidateSceneSlot(*slot);
    return true;
}

bool RagdollRegistry::isSceneAlive(OgsrPhysX2SceneHandle scene) const
{
    return resolveScene(scene) != nullptr;
}

bool RagdollRegistry::createInstance(OgsrPhysX2SceneHandle scene, const OgsrPhysXRagdollDefinition& definition,
                                     const OgsrPhysX2RagdollInstanceConfig& config,
                                     OgsrPhysX2RagdollHandle* outInstance, std::string* error)
{
    if (!outInstance)
        return fail(error, "ragdoll output is null");
    *outInstance = OgsrPhysX2RagdollHandle{};
    if (shutdown_)
        return fail(error, "registry is shut down");

    SceneSlot* sceneSlot = resolveScene(scene);
    if (!sceneSlot)
        return fail(error, "stale scene handle");

    const RagdollSceneContext context = sceneContext(*sceneSlot, scene);
    for (std::uint32_t index = 0; index < sceneSlot->instances.size(); ++index)
    {
        InstanceSlot& slot = sceneSlot->instances[index];
        if (slot.alive || slot.retired)
            continue;

        // Do not create backend resources if collision identity is exhausted.
        if (nextCollisionInstanceId_ == 0 ||
            nextCollisionInstanceId_ == std::numeric_limits<std::uint32_t>::max())
        {
            return fail(error, "collision instance id exhausted");
        }

        const std::uint32_t generation = g_instanceGenerations.allocate();
        if (generation == 0)
            return fail(error, "instance generation space exhausted");

        RagdollInstanceCreateInfo info;
        info.handle.sceneSlot = scene.slot;
        info.handle.sceneGeneration = scene.generation;
        info.handle.instanceSlot = index;
        info.handle.instanceGeneration = generation;
        info.collisionInstanceId = nextCollisionInstanceId_;
        info.definition = &definition;
        info.config = &config;

        void* backend = nullptr;
        std::string hookError;
        if (!hooks_.createInstance(context, info, &backend, &hookError))
        {
            // The generation and collision identity are consumed. No handle is
            // published and the slot stays free; this is the safe rollback.
            ++nextCollisionInstanceId_;
            return fail(error, hookError.empty() ? "instance backend create failed"
                                                 : "instance backend create failed: " + hookError);
        }

        const std::uint32_t collisionId = nextCollisionInstanceId_++;
        slot.generation = generation;
        slot.alive = true;
        slot.backend = backend;
        slot.sceneSlot = scene.slot;
        slot.sceneGeneration = scene.generation;
        slot.collisionInstanceId = collisionId;

        outInstance->sceneSlot = scene.slot;
        outInstance->sceneGeneration = scene.generation;
        outInstance->instanceSlot = index;
        outInstance->instanceGeneration = generation;
        return true;
    }

    return fail(error, "instance registry capacity exhausted for scene");
}

bool RagdollRegistry::destroyInstance(OgsrPhysX2RagdollHandle instance, std::string* error)
{
    InstanceSlot* slot = resolveInstance(instance);
    if (!slot)
        return fail(error, "stale ragdoll handle");

    SceneSlot& scene = scenes_[instance.sceneSlot];
    const RagdollSceneContext context = sceneContext(scene, OgsrPhysX2SceneHandle{instance.sceneSlot, instance.sceneGeneration});
    hooks_.destroyInstance(context, slot->backend);
    invalidateInstanceSlot(*slot);
    return true;
}

bool RagdollRegistry::isInstanceAlive(OgsrPhysX2RagdollHandle instance) const
{
    return resolveInstance(instance) != nullptr;
}

void* RagdollRegistry::sceneBackend(OgsrPhysX2SceneHandle scene) const
{
    const SceneSlot* slot = resolveScene(scene);
    return slot ? slot->backend : nullptr;
}

void* RagdollRegistry::instanceBackend(OgsrPhysX2RagdollHandle instance) const
{
    const InstanceSlot* slot = resolveInstance(instance);
    return slot ? slot->backend : nullptr;
}

std::uint32_t RagdollRegistry::collisionInstanceId(OgsrPhysX2RagdollHandle instance) const
{
    const InstanceSlot* slot = resolveInstance(instance);
    return slot ? slot->collisionInstanceId : 0;
}

bool RagdollRegistry::shutdown(std::string* error)
{
    if (shutdown_)
        return true;

    for (std::uint32_t sceneIndex = 0; sceneIndex < scenes_.size(); ++sceneIndex)
    {
        SceneSlot& scene = scenes_[sceneIndex];
        if (!scene.alive)
            continue;

        const RagdollSceneContext context =
            sceneContext(scene, OgsrPhysX2SceneHandle{sceneIndex, scene.generation});
        for (InstanceSlot& instance : scene.instances)
        {
            if (!instance.alive)
                continue;
            hooks_.destroyInstance(context, instance.backend);
            invalidateInstanceSlot(instance);
        }
        hooks_.destroyScene(scene.backend);
        invalidateSceneSlot(scene);
    }

    shutdown_ = true;
    if (error)
        error->clear();
    return true;
}

bool RagdollRegistry::isShutdown() const
{
    return shutdown_;
}

bool RagdollRegistry::hasActiveResources() const
{
    for (const SceneSlot& scene : scenes_)
    {
        if (scene.alive)
            return true;
        for (const InstanceSlot& instance : scene.instances)
        {
            if (instance.alive)
                return true;
        }
    }
    return false;
}

bool RagdollRegistry::setNextSceneGenerationForTest(std::uint32_t next)
{
    return g_sceneGenerations.setNext(next);
}

bool RagdollRegistry::setNextInstanceGenerationForTest(std::uint32_t next)
{
    return g_instanceGenerations.setNext(next);
}

void RagdollRegistry::resetGenerationAllocatorsForTest()
{
    g_sceneGenerations.reset();
    g_instanceGenerations.reset();
}

void RagdollRegistry::setNextCollisionInstanceIdForTest(std::uint32_t next)
{
    nextCollisionInstanceId_ = next;
}

} // namespace physics_physx
