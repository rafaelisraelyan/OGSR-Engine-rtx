#pragma once

// Generation-safe scene/instance registry for the v2 ragdoll ABI.
//
// This component is deliberately independent of PhysX, OGSR and the game. It
// owns only slot/generation/collision-id bookkeeping and delegates backend
// resource creation/destruction to RagdollRegistryHooks. P3.1-A tests use mock
// hooks; P3.1-B can implement the hooks with a real PhysX scene/instance.
//
// The registry is not thread-safe. All calls are expected on the game/main
// thread, outside simulate/fetch. Generation allocation is process-wide within
// the DLL/runtime lifetime so a new registry cannot accept handles issued by a
// destroyed registry.

#include "physics_physx/physx_ragdoll_c_v2.h"

#include <cstdint>
#include <string>
#include <vector>

namespace physics_physx
{

struct RagdollSceneContext
{
    OgsrPhysX2SceneHandle handle{};
    void* backend = nullptr;
};

struct RagdollInstanceCreateInfo
{
    OgsrPhysX2RagdollHandle handle{};
    std::uint32_t collisionInstanceId = 0;
    // Valid only for the duration of createInstance(). The backend must copy
    // everything it needs; it must not retain these caller-owned pointers.
    const OgsrPhysXRagdollDefinition* definition = nullptr;
    const OgsrPhysX2RagdollInstanceConfig* config = nullptr;
};

class RagdollRegistryHooks
{
public:
    virtual ~RagdollRegistryHooks() = default;

    // Create/destroy backend resources. Create returns false and sets an
    // optional diagnostic on failure; the backend must not leave partial
    // resources behind. Destroy must be noexcept in practice.
    virtual bool createScene(const OgsrPhysX2SceneConfig& config, void** outScene, std::string* error) = 0;
    virtual void destroyScene(void* scene) = 0;
    virtual bool createInstance(const RagdollSceneContext& scene, const RagdollInstanceCreateInfo& info,
                                void** outInstance, std::string* error) = 0;
    virtual void destroyInstance(const RagdollSceneContext& scene, void* instance) = 0;
};

struct RagdollRegistryConfig
{
    std::uint32_t sceneCapacity = OGSR_PHYSX2_SCENE_CAPACITY;
    // Per-scene instance capacity. GetApiInfo.maxInstancesPerScene reports the
    // same value; total registry capacity is sceneCapacity * instanceCapacity.
    std::uint32_t instanceCapacity = OGSR_PHYSX2_INSTANCE_CAPACITY;
};

class RagdollRegistry
{
public:
    explicit RagdollRegistry(RagdollRegistryHooks& hooks, RagdollRegistryConfig config = {});
    ~RagdollRegistry();

    RagdollRegistry(const RagdollRegistry&) = delete;
    RagdollRegistry& operator=(const RagdollRegistry&) = delete;

    bool createScene(const OgsrPhysX2SceneConfig& config, OgsrPhysX2SceneHandle* outScene,
                     std::string* error = nullptr);
    bool destroyScene(OgsrPhysX2SceneHandle scene, std::string* error = nullptr);
    bool isSceneAlive(OgsrPhysX2SceneHandle scene) const;

    bool createInstance(OgsrPhysX2SceneHandle scene, const OgsrPhysXRagdollDefinition& definition,
                        const OgsrPhysX2RagdollInstanceConfig& config, OgsrPhysX2RagdollHandle* outInstance,
                        std::string* error = nullptr);
    bool destroyInstance(OgsrPhysX2RagdollHandle instance, std::string* error = nullptr);
    bool isInstanceAlive(OgsrPhysX2RagdollHandle instance) const;

    // Backend pointers for validated handles; nullptr for stale handles.
    void* sceneBackend(OgsrPhysX2SceneHandle scene) const;
    void* instanceBackend(OgsrPhysX2RagdollHandle instance) const;

    // 0 when the handle is stale/invalid. Nonzero IDs are unique among live
    // instances of one scene and are retired on destroy (never reused).
    std::uint32_t collisionInstanceId(OgsrPhysX2RagdollHandle instance) const;

    // Explicit deterministic teardown. Destroys every live instance and scene
    // through the hooks, invalidates all handles, and is idempotent. The game
    // must remove bone callbacks before calling this; the registry cannot know
    // about IKinematics. The destructor calls shutdown() as a safety net.
    bool shutdown(std::string* error = nullptr);
    bool isShutdown() const;
    bool hasActiveResources() const;

    // Test-only generation/collision-id injection. These manipulate the
    // process-wide allocators and are intended for sequential unit tests only.
    static bool setNextSceneGenerationForTest(std::uint32_t next);
    static bool setNextInstanceGenerationForTest(std::uint32_t next);
    static void resetGenerationAllocatorsForTest();
    void setNextCollisionInstanceIdForTest(std::uint32_t next);

private:
    struct InstanceSlot
    {
        std::uint32_t generation = 0;
        bool alive = false;
        bool retired = false;
        std::uint32_t sceneSlot = 0;
        std::uint32_t sceneGeneration = 0;
        std::uint32_t collisionInstanceId = 0;
        void* backend = nullptr;
    };

    struct SceneSlot
    {
        std::uint32_t generation = 0;
        bool alive = false;
        bool retired = false;
        void* backend = nullptr;
        std::vector<InstanceSlot> instances;
    };

    SceneSlot* resolveScene(OgsrPhysX2SceneHandle scene);
    const SceneSlot* resolveScene(OgsrPhysX2SceneHandle scene) const;
    InstanceSlot* resolveInstance(OgsrPhysX2RagdollHandle instance);
    const InstanceSlot* resolveInstance(OgsrPhysX2RagdollHandle instance) const;

    void invalidateSceneSlot(SceneSlot& slot);
    void invalidateInstanceSlot(InstanceSlot& slot);
    RagdollSceneContext sceneContext(const SceneSlot& scene, OgsrPhysX2SceneHandle handle) const;

    RagdollRegistryHooks& hooks_;
    RagdollRegistryConfig config_;
    std::vector<SceneSlot> scenes_;
    std::uint32_t nextCollisionInstanceId_ = 1;
    bool shutdown_ = false;
};

} // namespace physics_physx
