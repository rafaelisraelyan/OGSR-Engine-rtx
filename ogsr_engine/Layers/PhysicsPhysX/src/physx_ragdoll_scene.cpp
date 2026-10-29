#include "physx_ragdoll_v2_internal.h"

#include "PxPhysicsAPI.h"
#include "cooking/PxCooking.h"
#include "extensions/PxDefaultCpuDispatcher.h"
#include "extensions/PxDefaultErrorCallback.h"
#include "extensions/PxD6Joint.h"
#include "extensions/PxExtensionsAPI.h"
#include "extensions/PxRigidBodyExt.h"
#include "geometry/PxTriangleMeshGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace physics_physx
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kAngleEpsilon = 1.0e-4f;
constexpr float kMaterialEpsilon = 1.0e-4f;

bool fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
    return false;
}

class PhysXContext
{
public:
    static PhysXContext& instance()
    {
        static PhysXContext* context = new PhysXContext();
        return *context;
    }

    bool acquire(std::string* error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (refs_ > 0)
        {
            ++refs_;
            return true;
        }
        if (!create(error))
            return false;
        ++refs_;
        return true;
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (refs_ == 0)
            return;
        --refs_;
        if (refs_ == 0)
            destroy();
    }

    PxPhysics* physics()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return physics_;
    }

private:
    bool create(std::string* error)
    {
        foundation_ = PxCreateFoundation(PX_PHYSICS_VERSION, allocator_, errorCallback_);
        if (!foundation_)
            return fail(error, "PxCreateFoundation failed");
        physics_ = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation_, PxTolerancesScale(), false, nullptr);
        if (!physics_)
        {
            foundation_->release();
            foundation_ = nullptr;
            return fail(error, "PxCreatePhysics failed");
        }
        if (!PxInitExtensions(*physics_, nullptr))
        {
            physics_->release();
            physics_ = nullptr;
            foundation_->release();
            foundation_ = nullptr;
            return fail(error, "PxInitExtensions failed");
        }
        return true;
    }

    void destroy()
    {
        if (physics_)
        {
            PxCloseExtensions();
            physics_->release();
            physics_ = nullptr;
        }
        if (foundation_)
        {
            foundation_->release();
            foundation_ = nullptr;
        }
    }

    mutable std::mutex mutex_;
    PxDefaultAllocator allocator_;
    PxDefaultErrorCallback errorCallback_;
    PxFoundation* foundation_ = nullptr;
    PxPhysics* physics_ = nullptr;
    int refs_ = 0;
};

constexpr std::uint32_t kGroupRagdoll = OGSR_PHYSX2_GROUP_RAGDOLL;
constexpr std::uint32_t kGroupStatic = OGSR_PHYSX2_GROUP_STATIC;
constexpr std::uint32_t kFilterRagdoll = OGSR_PHYSX2_FILTER_RAGDOLL;
constexpr std::uint32_t kFilterStatic = OGSR_PHYSX2_FILTER_STATIC;
constexpr std::uint32_t kFilterDisableSelf = OGSR_PHYSX2_FILTER_DISABLE_SELF_COLLISION;

PxFilterFlags v2FilterShader(PxFilterObjectAttributes attributes0, PxFilterData filterData0,
                             PxFilterObjectAttributes attributes1, PxFilterData filterData1,
                             PxPairFlags& pairFlags, const void*, PxU32)
{
    if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1))
        return PxFilterFlag::eSUPPRESS;

    const std::uint32_t flags0 = filterData0.word3;
    const std::uint32_t flags1 = filterData1.word3;
    const bool ragdoll0 = (flags0 & kFilterRagdoll) != 0;
    const bool ragdoll1 = (flags1 & kFilterRagdoll) != 0;
    const bool static0 = (flags0 & kFilterStatic) != 0;
    const bool static1 = (flags1 & kFilterStatic) != 0;

    // Exactly one classification flag is required. Invalid/unknown actors are
    // suppressed rather than allowed by accident.
    if (ragdoll0 == static0 || ragdoll1 == static1)
        return PxFilterFlag::eSUPPRESS;
    if (!ragdoll0 && !static0)
        return PxFilterFlag::eSUPPRESS;
    if (!ragdoll1 && !static1)
        return PxFilterFlag::eSUPPRESS;
    if (static0 && static1)
        return PxFilterFlag::eSUPPRESS;

    if (ragdoll0 && filterData0.word2 == 0)
        return PxFilterFlag::eSUPPRESS;
    if (ragdoll1 && filterData1.word2 == 0)
        return PxFilterFlag::eSUPPRESS;

    if (ragdoll0 && ragdoll1 && filterData0.word2 == filterData1.word2)
    {
        if ((flags0 | flags1) & kFilterDisableSelf)
            return PxFilterFlag::eSUPPRESS;
    }

    if ((filterData0.word0 & filterData1.word1) == 0 ||
        (filterData1.word0 & filterData0.word1) == 0)
        return PxFilterFlag::eSUPPRESS;

    pairFlags = PxPairFlag::eCONTACT_DEFAULT;
    if (ragdoll0 || ragdoll1)
    {
        pairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND | PxPairFlag::eNOTIFY_TOUCH_PERSISTS |
                     PxPairFlag::eNOTIFY_TOUCH_LOST | PxPairFlag::eNOTIFY_CONTACT_POINTS;
    }
    return PxFilterFlag::eDEFAULT;
}

PxFilterData ragdollFilterData(std::uint32_t group, std::uint32_t mask, std::uint32_t collisionInstanceId,
                               bool disableSelfCollision)
{
    const std::uint32_t flags = kFilterRagdoll | (disableSelfCollision ? kFilterDisableSelf : 0);
    return PxFilterData(group, mask, collisionInstanceId, flags);
}

PxFilterData staticFilterData()
{
    return PxFilterData(kGroupStatic, kGroupRagdoll, 0, kFilterStatic);
}

bool finiteVec3(const physics_portable::Vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

physics_portable::Transform fromPx(const PxTransform& value)
{
    physics_portable::Transform result;
    result.rotation = physics_portable::Quat{value.q.x, value.q.y, value.q.z, value.q.w};
    result.translation = physics_portable::Vec3{value.p.x, value.p.y, value.p.z};
    return result;
}

PxTransform toPx(const physics_portable::Transform& value)
{
    return PxTransform(PxVec3(value.translation.x, value.translation.y, value.translation.z),
                       PxQuat(value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w));
}

PxVec3 toPx(const physics_portable::Vec3& value)
{
    return PxVec3(value.x, value.y, value.z);
}

physics_portable::Vec3 fromPx(const PxVec3& value)
{
    return physics_portable::Vec3{value.x, value.y, value.z};
}

bool isFreeRange(const physics_portable::RagdollAxisLimit& limit)
{
    return limit.upper - limit.lower >= 2.0f * kPi - kAngleEpsilon;
}

bool isLockedRange(const physics_portable::RagdollAxisLimit& limit)
{
    return std::abs(limit.upper - limit.lower) <= kAngleEpsilon;
}

float clampConeAngle(float value)
{
    return std::min(std::max(std::abs(value), kAngleEpsilon), kPi - kAngleEpsilon);
}

std::pair<float, float> clampAngularPair(float lower, float upper, float minimum, float maximum)
{
    float low = std::min(std::max(lower, minimum), maximum);
    float high = std::min(std::max(upper, minimum), maximum);
    if (low > high)
        low = high = std::min(std::max(0.5f * (lower + upper), minimum), maximum);
    return {low, high};
}

struct ContactTag
{
    std::uint32_t collisionInstanceId = 0;
};

struct StaticFragment
{
    PxRigidStatic* actor = nullptr;
    PxTriangleMesh* mesh = nullptr;
    std::uint32_t triangles = 0;
};

// Scene-level material cache. The v2 ABI keys materials by a 16-bit id only,
// so two definitions that reuse the same id with different parameters cannot
// be represented unambiguously. Such a conflict is rejected instead of
// silently reusing the first material.
struct MaterialEntry
{
    PxMaterial* material = nullptr;
    float friction = 0.0f;
    float restitution = 0.0f;
};
} // namespace

struct PhysXRagdollScene::Impl final : PxSimulationEventCallback
{
    Impl(PhysXRagdollScene& owner, const OgsrPhysX2SceneConfig& inConfig) : owner(owner), config(inConfig) {}

    PhysXRagdollScene& owner;
    OgsrPhysX2SceneConfig config;
    PxPhysics* physics = nullptr;
    PxDefaultCpuDispatcher* dispatcher = nullptr;
    PxScene* scene = nullptr;
    bool contextAcquired = false;
    bool stepping = false;
    bool failed = false;
    std::uint64_t stepIndex = 0;
    std::uint64_t lastFrameId = 0;
    bool hasLastFrameId = false;
    OgsrPhysX2StepResult lastResult{};
    float accumulator = 0.0f;
    std::uint32_t instanceCount = 0;
    std::uint32_t staticTriangles = 0;

    std::map<std::uint16_t, MaterialEntry> materials;
    std::map<std::uint64_t, StaticFragment> staticFragments;
    std::map<std::uint32_t, ContactTag> contactTags;
    std::map<std::uint32_t, PhysXRagdollInstance*> contactInstances;

    ~Impl() override
    {
        for (auto& entry : staticFragments)
        {
            StaticFragment& fragment = entry.second;
            if (fragment.actor)
            {
                if (scene)
                    scene->removeActor(*fragment.actor);
                fragment.actor->release();
                fragment.actor = nullptr;
            }
            if (fragment.mesh)
            {
                fragment.mesh->release();
                fragment.mesh = nullptr;
            }
        }
        staticFragments.clear();
        contactInstances.clear();
        contactTags.clear();

        for (auto& entry : materials)
        {
            if (entry.second.material)
                entry.second.material->release();
        }
        materials.clear();

        if (scene)
        {
            scene->release();
            scene = nullptr;
        }
        if (dispatcher)
        {
            dispatcher->release();
            dispatcher = nullptr;
        }
        if (contextAcquired)
        {
            PhysXContext::instance().release();
            contextAcquired = false;
        }
    }

    bool initialize(std::string* error)
    {
        if (!PhysXContext::instance().acquire(error))
            return false;
        contextAcquired = true;
        physics = PhysXContext::instance().physics();
        if (!physics)
            return fail(error, "PhysX physics context is null");

        std::uint32_t workers = config.workerThreads ? config.workerThreads : 2;
        workers = std::min<std::uint32_t>(std::max<std::uint32_t>(workers, 1), 8);
        dispatcher = PxDefaultCpuDispatcherCreate(workers);
        if (!dispatcher)
            return fail(error, "PxDefaultCpuDispatcherCreate failed");

        PxSceneDesc sceneDesc(physics->getTolerancesScale());
        sceneDesc.gravity = PxVec3(0.0f, config.gravityY, 0.0f);
        sceneDesc.cpuDispatcher = dispatcher;
        sceneDesc.filterShader = v2FilterShader;
        if (config.collectContacts)
            sceneDesc.simulationEventCallback = this;
        sceneDesc.solverType = PxSolverType::eTGS;
        if (config.enableStabilization)
            sceneDesc.flags |= PxSceneFlag::eENABLE_STABILIZATION;

        scene = physics->createScene(sceneDesc);
        if (!scene)
            return fail(error, "PxScene creation failed");
        return true;
    }

    PxMaterial* materialFor(std::uint16_t id, const physics_portable::RagdollMaterialDesc& desc, std::string* error)
    {
        const float friction = std::max(0.0f, desc.friction);
        const float restitution = std::max(0.0f, desc.restitution);
        const auto found = materials.find(id);
        if (found != materials.end())
        {
            if (std::abs(found->second.friction - friction) > kMaterialEpsilon ||
                std::abs(found->second.restitution - restitution) > kMaterialEpsilon)
            {
                fail(error, "material id " + std::to_string(id) +
                                " is already used by the scene with different parameters");
                return nullptr;
            }
            return found->second.material;
        }

        PxMaterial* material = physics->createMaterial(friction, friction, restitution);
        if (!material)
        {
            fail(error, "PxMaterial creation failed");
            return nullptr;
        }
        material->setFrictionCombineMode(PxCombineMode::eMULTIPLY);
        material->setRestitutionCombineMode(PxCombineMode::eMULTIPLY);
        materials.emplace(id, MaterialEntry{material, friction, restitution});
        return material;
    }

    bool prepareFragment(const std::vector<physics_portable::Vec3>& vertices,
                         const std::vector<std::uint32_t>& indices, StaticFragment& fragment, std::string* error)
    {
        if (vertices.empty() || indices.empty())
            return fail(error, "static mesh is empty");
        if (indices.size() % 3 != 0)
            return fail(error, "static mesh must contain complete triangles");
        if (indices.size() / 3 > 200000)
            return fail(error, "static mesh exceeds the diagnostic triangle cap");
        for (const physics_portable::Vec3& vertex : vertices)
        {
            if (!finiteVec3(vertex))
                return fail(error, "static mesh contains a non-finite vertex");
        }
        for (std::uint32_t index : indices)
        {
            if (index >= vertices.size())
                return fail(error, "static mesh index is out of range");
        }

        std::vector<PxVec3> points;
        points.reserve(vertices.size());
        for (const physics_portable::Vec3& vertex : vertices)
            points.push_back(toPx(vertex));

        std::vector<PxU32> triangles;
        triangles.reserve(indices.size() * 2);
        for (std::size_t triangle = 0; triangle < indices.size(); triangle += 3)
        {
            const PxU32 a = indices[triangle + 0];
            const PxU32 b = indices[triangle + 1];
            const PxU32 c = indices[triangle + 2];
            triangles.push_back(a);
            triangles.push_back(b);
            triangles.push_back(c);
            triangles.push_back(a);
            triangles.push_back(c);
            triangles.push_back(b);
        }

        PxTriangleMeshDesc desc;
        desc.points.count = static_cast<PxU32>(points.size());
        desc.points.stride = sizeof(PxVec3);
        desc.points.data = points.data();
        desc.triangles.count = static_cast<PxU32>(triangles.size() / 3);
        desc.triangles.stride = sizeof(PxU32) * 3;
        desc.triangles.data = triangles.data();

        PxCookingParams cookingParams(physics->getTolerancesScale());
        fragment.mesh = PxCreateTriangleMesh(cookingParams, desc);
        if (!fragment.mesh)
            return fail(error, "PxCreateTriangleMesh failed");

        fragment.actor = physics->createRigidStatic(PxTransform(PxIdentity));
        if (!fragment.actor)
        {
            fragment.mesh->release();
            fragment.mesh = nullptr;
            return fail(error, "static PxRigidStatic creation failed");
        }

        physics_portable::RagdollMaterialDesc fallback;
        PxMaterial* material = materialFor(0, fallback, error);
        if (!material)
        {
            fragment.actor->release();
            fragment.actor = nullptr;
            fragment.mesh->release();
            fragment.mesh = nullptr;
            return false;
        }

        PxShape* shape = physics->createShape(PxTriangleMeshGeometry(fragment.mesh), *material, true);
        if (!shape)
        {
            fragment.actor->release();
            fragment.actor = nullptr;
            fragment.mesh->release();
            fragment.mesh = nullptr;
            return fail(error, "static triangle shape creation failed");
        }
        shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
        shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
        shape->setSimulationFilterData(staticFilterData());
        fragment.actor->attachShape(*shape);
        shape->release();
        fragment.triangles = static_cast<std::uint32_t>(indices.size() / 3);
        return true;
    }

    void releaseFragment(StaticFragment& fragment)
    {
        if (fragment.actor)
        {
            if (scene)
                scene->removeActor(*fragment.actor);
            fragment.actor->release();
            fragment.actor = nullptr;
        }
        if (fragment.mesh)
        {
            fragment.mesh->release();
            fragment.mesh = nullptr;
        }
        staticTriangles -= fragment.triangles;
        fragment.triangles = 0;
    }

    bool replaceFragment(std::uint64_t key, const std::vector<physics_portable::Vec3>& vertices,
                         const std::vector<std::uint32_t>& indices, std::string* error)
    {
        if (stepping)
            return fail(error, "static geometry cannot change during simulate/fetch");
        if (!scene)
            return fail(error, "scene is not initialized");

        StaticFragment prepared;
        if (!prepareFragment(vertices, indices, prepared, error))
            return false;

        auto existing = staticFragments.find(key);
        if (existing != staticFragments.end())
            scene->removeActor(*existing->second.actor);

        scene->addActor(*prepared.actor);
        if (existing != staticFragments.end())
        {
            StaticFragment old = existing->second;
            staticTriangles -= old.triangles;
            if (old.actor)
                old.actor->release();
            if (old.mesh)
                old.mesh->release();
            existing->second = prepared;
        }
        else
        {
            staticFragments.emplace(key, prepared);
        }
        staticTriangles += prepared.triangles;
        owner.resetAllFiltering();
        return true;
    }

    bool createInstance(const RagdollInstanceCreateInfo& info, PhysXRagdollInstance*& outInstance,
                        std::string* error)
    {
        if (stepping)
            return fail(error, "ragdoll creation is not allowed during simulate/fetch");
        if (failed)
            return fail(error, "scene is in a failed state");
        const std::uint32_t limit = config.maxInstances ? config.maxInstances : OGSR_PHYSX2_INSTANCE_CAPACITY;
        if (instanceCount >= limit)
            return fail(error, "scene instance limit reached");

        auto* instance = new PhysXRagdollInstance(owner, info);
        if (!instance->initialize(error))
        {
            delete instance;
            return false;
        }
        ++instanceCount;
        outInstance = instance;
        return true;
    }

    void destroyInstance(PhysXRagdollInstance* instance)
    {
        if (!instance)
            return;
        delete instance;
        if (instanceCount > 0)
            --instanceCount;
    }

    void* registerContact(std::uint32_t collisionInstanceId, PhysXRagdollInstance* instance)
    {
        ContactTag& tag = contactTags[collisionInstanceId];
        tag.collisionInstanceId = collisionInstanceId;
        contactInstances[collisionInstanceId] = instance;
        return &tag;
    }

    void unregisterContact(std::uint32_t collisionInstanceId)
    {
        contactInstances.erase(collisionInstanceId);
        contactTags.erase(collisionInstanceId);
    }

    PhysXRagdollInstance* findContact(std::uint32_t collisionInstanceId) const
    {
        const auto found = contactInstances.find(collisionInstanceId);
        return found != contactInstances.end() ? found->second : nullptr;
    }

    void resetAllFiltering()
    {
        if (!scene)
            return;
        for (const auto& entry : contactInstances)
        {
            if (!entry.second)
                continue;
            for (PxRigidActor* actor : entry.second->actors())
            {
                if (actor)
                    scene->resetFiltering(*actor);
            }
        }
    }

    bool step(float deltaSeconds, std::uint64_t frameId, OgsrPhysX2StepResult* result, std::string* error)
    {
        if (failed)
            return fail(error, "scene is in a failed state");
        if (stepping)
            return fail(error, "reentrant SceneStep is not allowed");
        if (!scene)
            return fail(error, "scene is not initialized");
        if (!result || result->structSize < sizeof(OgsrPhysX2StepResult))
            return fail(error, "step result buffer is invalid");
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f)
            return fail(error, "delta time is invalid");
        if (hasLastFrameId && frameId == lastFrameId)
        {
            *result = lastResult;
            return true;
        }
        if (hasLastFrameId && frameId < lastFrameId)
            return fail(error, "non-monotonic frame id");

        result->structSize = sizeof(OgsrPhysX2StepResult);
        result->steps = 0;
        result->droppedSteps = 0;
        result->reserved0 = 0;
        result->stepIndex = stepIndex;
        result->frameId = frameId;
        result->simulatedSeconds = 0.0f;
        result->droppedSeconds = 0.0f;

        if (deltaSeconds == 0.0f)
        {
            lastFrameId = frameId;
            hasLastFrameId = true;
            lastResult = *result;
            return true;
        }

        const float fixed = config.fixedTimestep;
        const std::uint32_t maxSubsteps = config.maxSubsteps;
        const float acceptedDelta = std::min(deltaSeconds, 0.25f);
        float dropped = deltaSeconds - acceptedDelta;
        accumulator += acceptedDelta;
        const float maxAccumulator = fixed * static_cast<float>(maxSubsteps) * 2.0f;
        if (accumulator > maxAccumulator)
        {
            dropped += accumulator - maxAccumulator;
            accumulator = maxAccumulator;
        }

        std::uint32_t executed = 0;
        stepping = true;
        while (accumulator + 1.0e-7f >= fixed && executed < maxSubsteps)
        {
            for (const auto& entry : contactInstances)
            {
                if (entry.second)
                    entry.second->resetContactStep();
            }
            scene->simulate(fixed);
            if (!scene->fetchResults(true))
            {
                stepping = false;
                failed = true;
                return fail(error, "PxScene::fetchResults failed");
            }
            ++stepIndex;
            accumulator -= fixed;
            ++executed;
        }
        stepping = false;

        if (accumulator >= fixed)
        {
            const float keep = std::fmod(accumulator, fixed);
            dropped += accumulator - keep;
            accumulator = keep;
        }

        result->steps = executed;
        result->droppedSteps = static_cast<std::uint32_t>(std::floor(dropped / fixed));
        result->stepIndex = stepIndex;
        result->simulatedSeconds = static_cast<float>(executed) * fixed;
        result->droppedSeconds = dropped;

        lastFrameId = frameId;
        hasLastFrameId = true;
        lastResult = *result;
        return true;
    }

    void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
    void onWake(PxActor**, PxU32) override {}
    void onSleep(PxActor**, PxU32) override {}
    void onTrigger(PxTriggerPair*, PxU32) override {}
    void onAdvance(const PxRigidBody* const*, const PxTransform*, const PxU32) override {}

    void onContact(const PxContactPairHeader& header, const PxContactPair* pairs, PxU32 count) override
    {
        // A touch-lost event may reference an actor/shape that has already been
        // released. The SDK explicitly permits dangling pointers in that case,
        // so only the removal flags decide whether a pointer may be read.
        const bool actorRemoved0 = header.flags.isSet(PxContactPairHeaderFlag::eREMOVED_ACTOR_0);
        const bool actorRemoved1 = header.flags.isSet(PxContactPairHeaderFlag::eREMOVED_ACTOR_1);
        PxActor* actor0 = actorRemoved0 ? nullptr : header.actors[0];
        PxActor* actor1 = actorRemoved1 ? nullptr : header.actors[1];
        const std::uint32_t id0 = tagId(actor0);
        const std::uint32_t id1 = tagId(actor1);
        PhysXRagdollInstance* instance0 = findContact(id0);
        PhysXRagdollInstance* instance1 = findContact(id1);
        if (!instance0 && !instance1)
            return;

        for (PxU32 index = 0; index < count; ++index)
        {
            const PxContactPair& pair = pairs[index];
            const bool shapeRemoved = pair.flags.isSet(PxContactPairFlag::eREMOVED_SHAPE_0) ||
                                      pair.flags.isSet(PxContactPairFlag::eREMOVED_SHAPE_1);
            if (shapeRemoved)
                continue;

            const bool found = pair.events.isSet(PxPairFlag::eNOTIFY_TOUCH_FOUND);
            const bool persist = pair.events.isSet(PxPairFlag::eNOTIFY_TOUCH_PERSISTS);
            const bool lost = pair.events.isSet(PxPairFlag::eNOTIFY_TOUCH_LOST);

            float impulse = 0.0f;
            if (pair.contactImpulses)
            {
                for (PxU32 contact = 0; contact < pair.contactCount; ++contact)
                    impulse += std::abs(pair.contactImpulses[contact]);
            }
            float penetration = 0.0f;
            PxContactPairPoint points[16];
            const PxU32 extracted = pair.extractContacts(points, 16);
            for (PxU32 point = 0; point < extracted; ++point)
                penetration = std::max(penetration, -points[point].separation);

            if (instance0)
                instance0->onContact(pair.contactCount, impulse, penetration, found, persist, lost);
            if (instance1)
                instance1->onContact(pair.contactCount, impulse, penetration, found, persist, lost);
        }
    }

    static std::uint32_t tagId(PxActor* actor)
    {
        if (!actor || !actor->userData)
            return 0;
        return static_cast<ContactTag*>(actor->userData)->collisionInstanceId;
    }
};

PhysXRagdollScene::PhysXRagdollScene(const OgsrPhysX2SceneConfig& config)
    : impl_(std::make_unique<Impl>(*this, config))
{
}

PhysXRagdollScene::~PhysXRagdollScene() = default;

bool PhysXRagdollScene::initialize(std::string* error)
{
    return impl_->initialize(error);
}

bool PhysXRagdollScene::setStaticMesh(const std::vector<physics_portable::Vec3>& vertices,
                                      const std::vector<std::uint32_t>& indices, std::string* error)
{
    return impl_->replaceFragment(0, vertices, indices, error);
}

bool PhysXRagdollScene::addStaticFragment(std::uint64_t key, const std::vector<physics_portable::Vec3>& vertices,
                                          const std::vector<std::uint32_t>& indices, std::string* error)
{
    return impl_->replaceFragment(key, vertices, indices, error);
}

bool PhysXRagdollScene::step(float deltaSeconds, std::uint64_t frameId, OgsrPhysX2StepResult* result,
                             std::string* error)
{
    return impl_->step(deltaSeconds, frameId, result, error);
}

bool PhysXRagdollScene::createInstance(const RagdollInstanceCreateInfo& info, PhysXRagdollInstance*& outInstance,
                                       std::string* error)
{
    return impl_->createInstance(info, outInstance, error);
}

void PhysXRagdollScene::destroyInstance(PhysXRagdollInstance* instance)
{
    impl_->destroyInstance(instance);
}

bool PhysXRagdollScene::isStepping() const { return impl_->stepping; }
std::uint32_t PhysXRagdollScene::staticTriangleCount() const { return impl_->staticTriangles; }
PxPhysics& PhysXRagdollScene::physics() { return *impl_->physics; }
PxScene& PhysXRagdollScene::scene() { return *impl_->scene; }

PxMaterial* PhysXRagdollScene::materialFor(std::uint16_t id, const physics_portable::RagdollMaterialDesc& desc,
                                           std::string* error)
{
    return impl_->materialFor(id, desc, error);
}

void* PhysXRagdollScene::registerContact(std::uint32_t collisionInstanceId, PhysXRagdollInstance* instance)
{
    return impl_->registerContact(collisionInstanceId, instance);
}

void PhysXRagdollScene::unregisterContact(std::uint32_t collisionInstanceId)
{
    impl_->unregisterContact(collisionInstanceId);
}

PhysXRagdollInstance* PhysXRagdollScene::findContact(std::uint32_t collisionInstanceId) const
{
    return impl_->findContact(collisionInstanceId);
}

void PhysXRagdollScene::resetAllFiltering() { impl_->resetAllFiltering(); }
const OgsrPhysX2SceneConfig& PhysXRagdollScene::config() const { return impl_->config; }
std::uint64_t PhysXRagdollScene::stepIndex() const { return impl_->stepIndex; }
bool PhysXRagdollScene::failed() const { return impl_->failed; }

} // namespace physics_physx
