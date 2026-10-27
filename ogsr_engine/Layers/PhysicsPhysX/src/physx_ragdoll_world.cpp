#include "physics_physx/physx_ragdoll_world.h"

#include "PxPhysicsAPI.h"
#include "cooking/PxCooking.h"
#include "geometry/PxConvexCoreGeometry.h"
#include "geometry/PxTriangleMeshGeometry.h"
#include "extensions/PxDefaultCpuDispatcher.h"
#include "extensions/PxDefaultSimulationFilterShader.h"
#include "extensions/PxD6Joint.h"
#include "extensions/PxExtensionsAPI.h"
#include "extensions/PxJointLimit.h"
#include "extensions/PxRigidBodyExt.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <malloc.h>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace physx;

namespace physics_physx
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kAngleEpsilon = 1.0e-4f;

PxVec3 toPx(const physics_portable::Vec3& value) { return PxVec3(value.x, value.y, value.z); }
physics_portable::Vec3 fromPx(const PxVec3& value) { return physics_portable::Vec3{value.x, value.y, value.z}; }
PxQuat toPx(const physics_portable::Quat& value) { return PxQuat(value.x, value.y, value.z, value.w); }
physics_portable::Quat fromPx(const PxQuat& value)
{
    return physics_portable::Quat{value.x, value.y, value.z, value.w};
}
PxTransform toPx(const physics_portable::Transform& value) { return PxTransform(toPx(value.translation), toPx(value.rotation)); }
physics_portable::Transform fromPx(const PxTransform& value)
{
    return physics_portable::Transform{fromPx(value.q), fromPx(value.p)};
}

class TrackingAllocator final : public PxAllocatorCallback
{
public:
    void* allocate(std::size_t size, const char*, const char*, int) override
    {
        void* memory = _aligned_malloc(size, 16);
        if (!memory)
            return nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        allocations_.emplace(memory, size);
        return memory;
    }

    void deallocate(void* memory) override
    {
        if (!memory)
            return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            allocations_.erase(memory);
        }
        _aligned_free(memory);
    }

    std::size_t outstanding() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return allocations_.size();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<void*, std::size_t> allocations_;
};

class CollectingErrorCallback final : public PxErrorCallback
{
public:
    void reportError(PxErrorCode::Enum code, const char* message, const char* file, int line) override
    {
        std::ostringstream stream;
        stream << "PhysX error " << static_cast<int>(code);
        if (file)
            stream << " (" << file << ':' << line << ')';
        if (message)
            stream << ": " << message;
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = stream.str();
    }

    std::string lastError() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastError_;
    }

private:
    mutable std::mutex mutex_;
    std::string lastError_;
};

bool g_suppressRagdollSelfCollision = false;
bool g_collectRagdollContacts = false;

PxFilterFlags ragdollFilterShader(PxFilterObjectAttributes attributes0,
                                  PxFilterData filterData0,
                                  PxFilterObjectAttributes attributes1,
                                  PxFilterData filterData1,
                                  PxPairFlags& pairFlags,
                                  const void* constantBlock,
                                  PxU32 constantBlockSize)
{
    if (g_suppressRagdollSelfCollision && filterData0.word3 != 0 && filterData0.word3 == filterData1.word3)
        return PxFilterFlag::eSUPPRESS;

    const PxFilterFlags flags =
        PxDefaultSimulationFilterShader(attributes0, filterData0, attributes1, filterData1, pairFlags, constantBlock,
                                        constantBlockSize);
    if (g_collectRagdollContacts)
        pairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND | PxPairFlag::eNOTIFY_TOUCH_PERSISTS |
                     PxPairFlag::eNOTIFY_TOUCH_LOST | PxPairFlag::eNOTIFY_CONTACT_POINTS;
    return flags;
}

class RagdollContactCollector final : public PxSimulationEventCallback
{
public:
    void resetStep()
    {
        contactCount = 0;
        totalImpulse = 0.0f;
        maxPenetration = 0.0f;
        found = 0;
        persist = 0;
        lost = 0;
    }

    void resetScene()
    {
        resetStep();
        activePairs = 0;
    }

    void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
    void onWake(PxActor**, PxU32) override {}
    void onSleep(PxActor**, PxU32) override {}
    void onTrigger(PxTriggerPair*, PxU32) override {}
    void onAdvance(const PxRigidBody* const*, const PxTransform*, const PxU32) override {}

    void onContact(const PxContactPairHeader&, const PxContactPair* pairs, PxU32 count) override
    {
        for (PxU32 index = 0; index < count; ++index)
        {
            const PxContactPair& pair = pairs[index];
            const bool hasFound = pair.events.isSet(PxPairFlag::eNOTIFY_TOUCH_FOUND);
            const bool hasPersist = pair.events.isSet(PxPairFlag::eNOTIFY_TOUCH_PERSISTS);
            const bool hasLost = pair.events.isSet(PxPairFlag::eNOTIFY_TOUCH_LOST);
            if (hasFound)
            {
                ++found;
                ++activePairs;
            }
            if (hasPersist)
                ++persist;
            if (hasLost)
            {
                ++lost;
                if (activePairs > 0)
                    --activePairs;
            }
            if (!hasFound && !hasPersist)
                continue;

            contactCount += pair.contactCount;
            if (pair.contactImpulses)
            {
                for (PxU32 contact = 0; contact < pair.contactCount; ++contact)
                    totalImpulse += std::abs(pair.contactImpulses[contact]);
            }
            PxContactPairPoint points[16];
            const PxU32 extracted = pair.extractContacts(points, 16);
            for (PxU32 point = 0; point < extracted; ++point)
                maxPenetration = std::max(maxPenetration, -points[point].separation);
        }
    }

    std::uint32_t contactCount = 0;
    std::uint32_t found = 0;
    std::uint32_t persist = 0;
    std::uint32_t lost = 0;
    std::uint32_t activePairs = 0;
    float totalImpulse = 0.0f;
    float maxPenetration = 0.0f;
};

bool fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
    return false;
}

std::mutex g_extensionsMutex;
int g_extensionsRefCount = 0;

bool acquireExtensions(PxPhysics& physics, std::string* error)
{
    std::lock_guard<std::mutex> lock(g_extensionsMutex);
    if (g_extensionsRefCount == 0)
    {
        if (!PxInitExtensions(physics, nullptr))
            return fail(error, "PxInitExtensions failed");
    }
    ++g_extensionsRefCount;
    return true;
}

void releaseExtensions()
{
    std::lock_guard<std::mutex> lock(g_extensionsMutex);
    if (g_extensionsRefCount > 0)
    {
        --g_extensionsRefCount;
        if (g_extensionsRefCount == 0)
            PxCloseExtensions();
    }
}

bool isFiniteTransform(const physics_portable::Transform& value)
{
    return physics_portable::isFinite(value);
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

} // namespace

struct PhysXRagdollWorld::Impl
{
    explicit Impl(const physics_portable::RagdollSimulationConfig& inConfig) : config(inConfig) {}

    physics_portable::RagdollSimulationConfig config;
    physics_portable::RagdollDescription description;
    physics_portable::RagdollInitialState initial;
    physics_portable::Transform rootToEntity{};

    TrackingAllocator allocator;
    CollectingErrorCallback errors;
    PxFoundation* foundation = nullptr;
    PxPhysics* physics = nullptr;
    PxDefaultCpuDispatcher* dispatcher = nullptr;
    PxScene* scene = nullptr;
    PxRigidStatic* floor = nullptr;
    std::vector<PxRigidActor*> actors;
    std::vector<PxRigidDynamic*> dynamics;
    std::vector<PxD6Joint*> joints;
    std::vector<physics_portable::Transform> bodyBindWorld;
    std::map<std::uint16_t, PxMaterial*> materials;
    std::vector<PxMaterial*> materialList;

    std::vector<physics_portable::Vec3> staticVertices;
    std::vector<std::uint32_t> staticIndices;
    PxRigidStatic* staticActor = nullptr;
    PxTriangleMesh* staticMesh = nullptr;
    std::uint32_t staticTriangles = 0;

    float accumulator = 0.0f;
    std::uint64_t stepIndex = 0;
    std::string lastError;
    bool created = false;
    bool extensionsInitialized = false;

    RagdollContactCollector contacts;
    std::uint32_t externalImpulses = 0;
    float totalMass = 0.0f;
    float rootMass = 0.0f;

    void setError(const std::string& message)
    {
        lastError = message;
    }

    void clearScene()
    {
        for (PxD6Joint* joint : joints)
        {
            if (joint)
                joint->release();
        }
        joints.clear();

        for (PxRigidActor* actor : actors)
        {
            if (actor)
                actor->release();
        }
        actors.clear();
        dynamics.clear();
        bodyActors.clear();
        floor = nullptr;

        if (staticActor)
        {
            staticActor->release();
            staticActor = nullptr;
        }
        if (staticMesh)
        {
            staticMesh->release();
            staticMesh = nullptr;
        }
        staticTriangles = 0;

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
        for (PxMaterial* material : materialList)
        {
            if (material)
                material->release();
        }
        materialList.clear();
        materials.clear();

        if (extensionsInitialized)
        {
            releaseExtensions();
            extensionsInitialized = false;
        }
        if (physics)
        {
            physics->release();
            physics = nullptr;
        }
        if (foundation)
        {
            foundation->release();
            foundation = nullptr;
        }

        created = false;
        accumulator = 0.0f;
        stepIndex = 0;
        externalImpulses = 0;
        totalMass = 0.0f;
        rootMass = 0.0f;
        contacts.resetScene();
    }

    bool initialize()
    {
        foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errors);
        if (!foundation)
            return fail(&lastError, "PxCreateFoundation failed: " + errors.lastError());

        physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale(), false, nullptr);
        if (!physics)
            return fail(&lastError, "PxCreatePhysics failed: " + errors.lastError());

        if (!acquireExtensions(*physics, &lastError))
            return false;
        extensionsInitialized = true;

        dispatcher = PxDefaultCpuDispatcherCreate(2);
        if (!dispatcher)
            return fail(&lastError, "PxDefaultCpuDispatcherCreate failed");

        PxSceneDesc sceneDesc(physics->getTolerancesScale());
        sceneDesc.gravity = toPx(config.gravity);
        sceneDesc.cpuDispatcher = dispatcher;
        g_suppressRagdollSelfCollision = config.disableSelfCollision;
        g_collectRagdollContacts = config.collectDiagnostics;
        sceneDesc.filterShader = ragdollFilterShader;
        if (config.collectDiagnostics)
        {
            contacts.resetScene();
            sceneDesc.simulationEventCallback = &contacts;
        }
        sceneDesc.solverType = PxSolverType::eTGS;
        if (config.enableStabilization)
            sceneDesc.flags |= PxSceneFlag::eENABLE_STABILIZATION;

        scene = physics->createScene(sceneDesc);
        if (!scene)
            return fail(&lastError, "PxScene creation failed: " + errors.lastError());

        PxMaterial* fallback = physics->createMaterial(0.8f, 0.8f, 0.0f);
        if (!fallback)
            return fail(&lastError, "fallback PxMaterial creation failed");
        fallback->setFrictionCombineMode(PxCombineMode::eMULTIPLY);
        fallback->setRestitutionCombineMode(PxCombineMode::eMULTIPLY);
        materials.emplace(0, fallback);
        materialList.push_back(fallback);

        for (const physics_portable::RagdollMaterialDesc& source : description.materials)
        {
            PxMaterial* material = physics->createMaterial(
                std::max(0.0f, source.friction), std::max(0.0f, source.friction), std::max(0.0f, source.restitution));
            if (!material)
                return fail(&lastError, "PxMaterial creation failed");
            material->setFrictionCombineMode(PxCombineMode::eMULTIPLY);
            material->setRestitutionCombineMode(PxCombineMode::eMULTIPLY);
            materials[source.id] = material;
            materialList.push_back(material);
        }

        const PxTransform floorPose(PxVec3(0.0f, -0.05f, 0.0f));
        floor = physics->createRigidStatic(floorPose);
        if (!floor)
            return fail(&lastError, "floor actor creation failed");
        actors.push_back(floor);
        PxBoxGeometry floorGeometry(50.0f, 0.05f, 50.0f);
        PxShape* floorShape = physics->createShape(floorGeometry, *materials[0], true);
        if (!floorShape)
            return fail(&lastError, "floor shape creation failed");
        floor->attachShape(*floorShape);
        floorShape->release();
        scene->addActor(*floor);
        return true;
    }

    bool applyStaticMesh()
    {
        if (staticVertices.empty() || staticIndices.empty())
            return true;
        if (staticIndices.size() % 3 != 0)
            return fail(&lastError, "static mesh must contain complete triangles");
        if (staticIndices.size() / 3 > 200000)
            return fail(&lastError, "static mesh exceeds the diagnostic triangle cap");
        for (const physics_portable::Vec3& vertex : staticVertices)
        {
            if (!physics_portable::isFinite(vertex))
                return fail(&lastError, "static mesh contains a non-finite vertex");
        }
        for (std::uint32_t index : staticIndices)
        {
            if (index >= staticVertices.size())
                return fail(&lastError, "static mesh index is out of range");
        }

        if (staticActor)
        {
            staticActor->release();
            staticActor = nullptr;
        }
        if (staticMesh)
        {
            staticMesh->release();
            staticMesh = nullptr;
        }

        std::vector<PxVec3> points;
        points.reserve(staticVertices.size());
        for (const physics_portable::Vec3& vertex : staticVertices)
            points.push_back(toPx(vertex));

        // OGSR triangle winding is not globally consistent. Cook both faces,
        // as in the P2.0 runtime context.
        std::vector<PxU32> triangles;
        triangles.reserve(staticIndices.size() * 2);
        for (std::size_t triangle = 0; triangle < staticIndices.size(); triangle += 3)
        {
            const PxU32 a = staticIndices[triangle + 0];
            const PxU32 b = staticIndices[triangle + 1];
            const PxU32 c = staticIndices[triangle + 2];
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
        staticMesh = PxCreateTriangleMesh(cookingParams, desc);
        if (!staticMesh)
            return fail(&lastError, "PxCreateTriangleMesh failed: " + errors.lastError());

        staticActor = physics->createRigidStatic(PxTransform(PxIdentity));
        if (!staticActor)
        {
            staticMesh->release();
            staticMesh = nullptr;
            return fail(&lastError, "static PxRigidStatic creation failed");
        }
        PxShape* shape = physics->createShape(PxTriangleMeshGeometry(staticMesh), *materialFor(0), true);
        if (!shape)
        {
            staticActor->release();
            staticActor = nullptr;
            staticMesh->release();
            staticMesh = nullptr;
            return fail(&lastError, "static triangle shape creation failed");
        }
        shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
        shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
        staticActor->attachShape(*shape);
        shape->release();
        scene->addActor(*staticActor);
        staticTriangles = static_cast<std::uint32_t>(staticIndices.size() / 3);
        return true;
    }

    PxMaterial* materialFor(std::uint16_t id) const
    {
        const auto found = materials.find(id);
        if (found != materials.end())
            return found->second;
        return materials.begin()->second;
    }

    bool createShape(PxRigidActor& actor, const physics_portable::RagdollShapeDesc& source)
    {
        if (source.flags & physics_portable::shapeFlag(physics_portable::ShapeFlag::NoPhysics))
            return true;

        PxGeometry* geometry = nullptr;
        PxBoxGeometry boxGeometry;
        PxSphereGeometry sphereGeometry;
        PxConvexCoreGeometry cylinderGeometry;

        switch (source.type)
        {
        case physics_portable::ShapeType::Box:
            boxGeometry = PxBoxGeometry(source.halfExtents.x, source.halfExtents.y, source.halfExtents.z);
            geometry = &boxGeometry;
            break;
        case physics_portable::ShapeType::Sphere:
            sphereGeometry = PxSphereGeometry(source.radius);
            geometry = &sphereGeometry;
            break;
        case physics_portable::ShapeType::Cylinder:
        {
            // Contract cylinder axis is local +X. OGSR's source cylinder is local +Y;
            // the adapter must put that Y->X rotation into RagdollShapeDesc::local.
            const PxConvexCore::Cylinder core(source.height, source.radius);
            cylinderGeometry = PxConvexCoreGeometry(core, source.margin);
            geometry = &cylinderGeometry;
            break;
        }
        default:
            return fail(&lastError, "unsupported shape type");
        }

        PxShape* shape = physics->createShape(*geometry, *materialFor(source.materialId), true);
        if (!shape)
            return fail(&lastError, "PxShape creation failed: " + errors.lastError());

        shape->setLocalPose(toPx(source.local));
        shape->setContactOffset(std::max(1.0e-4f, std::max(config.restOffset, config.contactOffset)));
        shape->setRestOffset(config.restOffset);
        shape->setSimulationFilterData(PxFilterData(0, 0, 0, 1));
        actor.attachShape(*shape);
        shape->release();
        return true;
    }

    bool computeBindWorlds()
    {
        bodyBindWorld.assign(description.bodies.size(), physics_portable::Transform{});
        for (physics_portable::BodyId body = 0; body < description.bodies.size(); ++body)
        {
            physics_portable::Transform world = description.bodies[body].bindPose;
            physics_portable::BodyId parent = description.bodies[body].parentBody;
            std::size_t depth = 0;
            while (parent != physics_portable::kInvalidBody)
            {
                if (parent >= description.bodies.size() || ++depth > description.bodies.size())
                    return fail(&lastError, "body bind hierarchy is cyclic or out of range");
                world = description.bodies[parent].bindPose * world;
                parent = description.bodies[parent].parentBody;
            }
            if (!isFiniteTransform(world))
                return fail(&lastError, "body bind hierarchy produces a non-finite transform");
            bodyBindWorld[body] = world;
        }
        return true;
    }

    bool makeJointFrames(const physics_portable::RagdollJointDesc& source,
                         PxTransform& parentFrame,
                         PxTransform& childFrame)
    {
        parentFrame = toPx(source.parentFrame);
        childFrame = toPx(source.childFrame);
        if (source.kind != physics_portable::JointKind::Hinge &&
            source.kind != physics_portable::JointKind::Slider &&
            source.kind != physics_portable::JointKind::Euler3)
            return true;
        if (source.axisCount == 0)
            return fail(&lastError, "joint needs an axis but has none");

        const physics_portable::Transform parentBind = bodyBindWorld[source.parentBody];
        const physics_portable::Transform childBind = bodyBindWorld[source.childBody];
        const physics_portable::Quat relativeRotation = inverse(parentBind).rotation * childBind.rotation;

        auto axisInChildSpace = [&](const physics_portable::RagdollAxisLimit& axis) {
            physics_portable::Vec3 result = axis.axis;
            switch (axis.space)
            {
            case physics_portable::AxisSpace::Child:
                break;
            case physics_portable::AxisSpace::Parent:
                result = rotate(conjugate(relativeRotation), axis.axis);
                break;
            case physics_portable::AxisSpace::World:
                result = rotate(conjugate(childBind.rotation), axis.axis);
                break;
            }
            return normalized(result);
        };

        if (source.kind == physics_portable::JointKind::Hinge ||
            source.kind == physics_portable::JointKind::Slider)
        {
            const physics_portable::Vec3 axis = axisInChildSpace(source.axes[0]);
            if (lengthSquared(axis) < 1.0e-8f)
                return fail(&lastError, "joint axis is zero");
            const PxQuat childRotation = PxShortestRotation(PxVec3(1.0f, 0.0f, 0.0f), toPx(axis));
            const PxQuat parentRotation = toPx(relativeRotation) * childRotation;
            parentFrame = PxTransform(parentFrame.p, parentRotation);
            childFrame = PxTransform(childFrame.p, childRotation);
            return true;
        }

        if (source.axisCount < 3)
            return fail(&lastError, "Euler3 joint needs three axes");
        const PxVec3 x = toPx(axisInChildSpace(source.axes[0]));
        // OGSR full_control stores a default axis[1] and derives the middle
        // Euler axis as axis2 x axis0. Use that derivation so swing1 limits
        // land on the actual ODE axis, including the {2,0,1} mapping.
        const PxVec3 zInput = toPx(axisInChildSpace(source.axes[2]));
        const PxVec3 y = zInput.cross(x).getNormalized();
        if (x.magnitudeSquared() < 1.0e-8f || y.magnitudeSquared() < 1.0e-8f)
            return fail(&lastError, "Euler3 axes are degenerate");
        const PxVec3 z = x.cross(y);
        const PxQuat childRotation = PxQuat(PxMat33(x, y, z));
        const PxQuat parentRotation = toPx(relativeRotation) * childRotation;
        parentFrame = PxTransform(parentFrame.p, parentRotation);
        childFrame = PxTransform(childFrame.p, childRotation);
        return true;
    }

    bool createActors()
    {
        actors.reserve(description.bodies.size() + 1);
        dynamics.reserve(description.bodies.size());
        for (std::size_t index = 0; index < description.bodies.size(); ++index)
        {
            const physics_portable::RagdollBodyDesc& body = description.bodies[index];
            const physics_portable::Transform& world = initial.bodyWorld[index];
            if (!isFiniteTransform(world))
                return fail(&lastError, "initial body transform is not finite");

            const bool promoteRoot = body.role == physics_portable::BodyRole::StaticRoot &&
                                     body.id == description.rootBody && config.promoteStaticRoot &&
                                     !body.shapes.empty();
            const bool isStatic =
                (body.role == physics_portable::BodyRole::StaticRoot || body.isFixed) && !promoteRoot;
            PxRigidActor* actor = nullptr;
            if (isStatic)
            {
                actor = physics->createRigidStatic(toPx(world));
            }
            else
            {
                PxRigidDynamic* dynamic = physics->createRigidDynamic(toPx(world));
                if (!dynamic)
                    return fail(&lastError, "PxRigidDynamic creation failed");
                dynamic->setSolverIterationCounts(config.solverPositionIterations, config.solverVelocityIterations);
                dynamic->setMaxLinearVelocity(150.0f);
                dynamic->setMaxAngularVelocity(9.817f);
                dynamic->setLinearDamping(config.linearDamping);
                dynamic->setAngularDamping(config.angularDamping);
                dynamics.push_back(dynamic);
                actor = dynamic;
            }
            if (!actor)
                return fail(&lastError, "PxRigidActor creation failed");
            actors.push_back(actor);

            for (const physics_portable::RagdollShapeDesc& shape : body.shapes)
            {
                if (!createShape(*actor, shape))
                    return false;
            }

            PxU32 simulationShapeCount = 0;
            {
                const PxU32 shapeCount = actor->getNbShapes();
                std::vector<PxShape*> attachedShapes(shapeCount);
                actor->getShapes(attachedShapes.data(), shapeCount);
                for (PxShape* attached : attachedShapes)
                {
                    if (attached && (attached->getFlags() & PxShapeFlag::eSIMULATION_SHAPE))
                        ++simulationShapeCount;
                }
            }

            if (!isStatic)
            {
                PxRigidDynamic* dynamic = static_cast<PxRigidDynamic*>(actor);
                if (promoteRoot)
                {
                    // Body 0 is the synthetic OGSR root: shapes from the
                    // merged pelvis/spine bones, zero ODE mass. Give it mass
                    // and inertia from its real collision shapes instead of a
                    // hand-picked constant.
                    if (simulationShapeCount == 0)
                        return fail(&lastError, "promoted static root has no simulation shapes");
                    if (!PxRigidBodyExt::updateMassAndInertia(*dynamic, config.staticRootDensity))
                        return fail(&lastError, "updateMassAndInertia failed for promoted static root");
                    rootMass = dynamic->getMass();
                    const PxVec3 rootInertia = dynamic->getMassSpaceInertiaTensor();
                    const PxTransform rootMassPose = dynamic->getCMassLocalPose();
                    if (!(rootMass > 0.0f) || !std::isfinite(rootMass) || !std::isfinite(rootInertia.x) ||
                        !std::isfinite(rootInertia.y) || !std::isfinite(rootInertia.z) ||
                        !(rootInertia.x > 0.0f) || !(rootInertia.y > 0.0f) || !(rootInertia.z > 0.0f) ||
                        !rootMassPose.isValid())
                    {
                        return fail(&lastError, "promoted static root has invalid mass properties");
                    }
                }
                else
                {
                    physics_portable::Vec3 eigenvalues{};
                    physics_portable::Quat principalAxes{};
                    if (!physics_portable::diagonalizeInertia(body.inertia, eigenvalues, principalAxes, &lastError))
                        return false;

                    dynamic->setMass(body.mass);
                    // The actor frame is the bone frame. The mass frame is the COM frame;
                    // the principal-axis rotation makes the diagonal inertia valid.
                    dynamic->setCMassLocalPose(PxTransform(toPx(body.centerOfMass), toPx(principalAxes)));
                    dynamic->setMassSpaceInertiaTensor(toPx(eigenvalues));
                }
                totalMass += dynamic->getMass();

                const std::size_t bodyIndex = static_cast<std::size_t>(body.id);
                dynamic->setLinearVelocity(toPx(initial.linearVelocity[bodyIndex]), false);
                dynamic->setAngularVelocity(toPx(initial.angularVelocity[bodyIndex]), false);
                if (!initial.enabled.empty() && !initial.enabled[bodyIndex])
                    dynamic->setActorFlag(PxActorFlag::eDISABLE_SIMULATION, true);
            }

            scene->addActor(*actor);
        }

        bodyActors.resize(description.bodies.size(), nullptr);
        for (std::size_t index = 0; index < description.bodies.size(); ++index)
        {
            // The floor occupies slot 0 in actors; bodies follow it.
            bodyActors[index] = actors[index + 1];
        }
        return true;
    }

    std::vector<PxRigidActor*> bodyActors;

    PxRigidActor* bodyActor(physics_portable::BodyId body) const
    {
        if (body >= bodyActors.size())
            return nullptr;
        return bodyActors[body];
    }

    bool configureJoint(PxD6Joint& joint, const physics_portable::RagdollJointDesc& source)
    {
        auto motionFor = [](const physics_portable::RagdollAxisLimit& limit) -> PxD6Motion::Enum {
            if (isFreeRange(limit))
                return PxD6Motion::eFREE;
            if (isLockedRange(limit))
                return PxD6Motion::eLOCKED;
            return PxD6Motion::eLIMITED;
        };
        auto linearMotionFor = [](const physics_portable::RagdollAxisLimit& limit) -> PxD6Motion::Enum {
            return isLockedRange(limit) ? PxD6Motion::eLOCKED : PxD6Motion::eLIMITED;
        };

        joint.setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
        joint.setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
        joint.setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
        joint.setMotion(PxD6Axis::eTWIST, PxD6Motion::eLOCKED);
        joint.setMotion(PxD6Axis::eSWING1, PxD6Motion::eLOCKED);
        joint.setMotion(PxD6Axis::eSWING2, PxD6Motion::eLOCKED);

        switch (source.kind)
        {
        case physics_portable::JointKind::Ball:
            joint.setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
            joint.setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
            joint.setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);
            return true;

        case physics_portable::JointKind::Fixed:
            return true;

        case physics_portable::JointKind::Hinge:
            if (source.axisCount < 1)
                return fail(&lastError, "hinge joint has no axis");
            joint.setMotion(PxD6Axis::eTWIST, motionFor(source.axes[0]));
            if (joint.getMotion(PxD6Axis::eTWIST) == PxD6Motion::eLIMITED)
            {
                const auto twistRange = clampAngularPair(source.axes[0].lower, source.axes[0].upper,
                                                       -kPi + kAngleEpsilon, kPi - kAngleEpsilon);
                joint.setTwistLimit(PxJointAngularLimitPair(twistRange.first, twistRange.second));
            }
            return true;

        case physics_portable::JointKind::Euler3:
        {
            if (source.axisCount < 3)
                return fail(&lastError, "Euler3 joint has fewer than three axes");
            const PxD6Motion::Enum twistMotion = motionFor(source.axes[0]);
            const PxD6Motion::Enum swing1Motion = motionFor(source.axes[1]);
            const PxD6Motion::Enum swing2Motion = motionFor(source.axes[2]);
            joint.setMotion(PxD6Axis::eTWIST, twistMotion);
            joint.setMotion(PxD6Axis::eSWING1, swing1Motion);
            joint.setMotion(PxD6Axis::eSWING2, swing2Motion);

            if (twistMotion == PxD6Motion::eLIMITED)
            {
                const auto twistRange = clampAngularPair(source.axes[0].lower, source.axes[0].upper,
                                                       -kPi + kAngleEpsilon, kPi - kAngleEpsilon);
                joint.setTwistLimit(PxJointAngularLimitPair(twistRange.first, twistRange.second));
            }

            const bool swing1Locked = swing1Motion == PxD6Motion::eLOCKED;
            const bool swing2Locked = swing2Motion == PxD6Motion::eLOCKED;
            const bool swing1Limited = swing1Motion == PxD6Motion::eLIMITED;
            const bool swing2Limited = swing2Motion == PxD6Motion::eLIMITED;
            const bool bothSwingLimited = swing1Limited && swing2Limited;
            const bool oneSwingLimitedOneLocked =
                (swing1Limited && swing2Locked) || (swing2Limited && swing1Locked);
            const bool pyramidSupported = bothSwingLimited || oneSwingLimitedOneLocked;
            const bool pyramidRequested =
                source.swingLimitMode == physics_portable::SwingLimitMode::Pyramid ||
                (source.swingLimitMode == physics_portable::SwingLimitMode::Auto && pyramidSupported);

            if (pyramidRequested && pyramidSupported)
            {
                const auto yRange = clampAngularPair(
                    swing1Limited ? source.axes[1].lower : 0.0f,
                    swing1Limited ? source.axes[1].upper : 0.0f,
                    -kPi + kAngleEpsilon, kPi - kAngleEpsilon);
                const auto zRange = clampAngularPair(
                    swing2Limited ? source.axes[2].lower : 0.0f,
                    swing2Limited ? source.axes[2].upper : 0.0f,
                    -kPi + kAngleEpsilon, kPi - kAngleEpsilon);
                joint.setPyramidSwingLimit(
                    PxJointLimitPyramid(yRange.first, yRange.second, zRange.first, zRange.second));
            }
            else if (swing1Motion == PxD6Motion::eLIMITED || swing2Motion == PxD6Motion::eLIMITED)
            {
                const float yAngle = swing1Motion == PxD6Motion::eLIMITED
                                         ? clampConeAngle(std::max(std::abs(source.axes[1].lower), std::abs(source.axes[1].upper)))
                                         : kPi - kAngleEpsilon;
                const float zAngle = swing2Motion == PxD6Motion::eLIMITED
                                         ? clampConeAngle(std::max(std::abs(source.axes[2].lower), std::abs(source.axes[2].upper)))
                                         : kPi - kAngleEpsilon;
                joint.setSwingLimit(PxJointLimitCone(yAngle, zAngle));
            }
            return true;
        }

        case physics_portable::JointKind::Slider:
            if (source.axisCount < 1)
                return fail(&lastError, "slider joint has no axis");
            joint.setMotion(PxD6Axis::eX, linearMotionFor(source.axes[0]));
            if (joint.getMotion(PxD6Axis::eX) == PxD6Motion::eLIMITED)
            {
                joint.setLinearLimit(
                    PxD6Axis::eX,
                    PxJointLinearLimitPair(
                        physics->getTolerancesScale(), source.axes[0].lower, source.axes[0].upper));
            }
            if (source.axisCount >= 2)
            {
                joint.setMotion(PxD6Axis::eTWIST, motionFor(source.axes[1]));
                if (joint.getMotion(PxD6Axis::eTWIST) == PxD6Motion::eLIMITED)
                {
                    const auto twistRange = clampAngularPair(source.axes[1].lower, source.axes[1].upper,
                                                           -kPi + kAngleEpsilon, kPi - kAngleEpsilon);
                    joint.setTwistLimit(PxJointAngularLimitPair(twistRange.first, twistRange.second));
                }
            }
            return true;

        case physics_portable::JointKind::Hinge2:
            return fail(&lastError, "Hinge2 is not implemented in P1");
        default:
            return fail(&lastError, "unsupported joint kind");
        }
    }

    bool createJoints()
    {
        for (const physics_portable::RagdollJointDesc& source : description.joints)
        {
            PxRigidActor* parent = bodyActor(source.parentBody);
            PxRigidActor* child = bodyActor(source.childBody);
            if (!parent || !child)
                return fail(&lastError, "joint references an unknown body");
            PxTransform parentFrame;
            PxTransform childFrame;
            if (!makeJointFrames(source, parentFrame, childFrame))
                return false;
            PxD6Joint* joint = PxD6JointCreate(
                *physics, parent, parentFrame, child, childFrame);
            if (!joint)
                return fail(&lastError, "PxD6JointCreate failed: " + errors.lastError());
            if (!configureJoint(*joint, source))
            {
                joint->release();
                return false;
            }
            if (source.breakable)
                joint->setBreakForce(source.breakForce, source.breakTorque);
            joints.push_back(joint);
        }
        return true;
    }

    bool create(const physics_portable::RagdollDescription& inDescription,
                const physics_portable::RagdollInitialState& inInitial)
    {
        clearScene();
        lastError.clear();
        description = inDescription;
        initial = inInitial;
        rootToEntity = inInitial.rootToEntity;
        accumulator = 0.0f;
        stepIndex = 0;

        if (!physics_portable::validateDescription(description, &lastError))
            return false;
        if (!physics_portable::validateInitialState(description, initial, &lastError))
            return false;
        if (!(config.fixedTimestep > 0.0f) || !std::isfinite(config.fixedTimestep) ||
            config.maxSubsteps <= 0 || config.solverPositionIterations == 0 ||
            config.solverVelocityIterations == 0 || !physics_portable::isFinite(config.gravity) ||
            !std::isfinite(config.contactOffset) || !std::isfinite(config.restOffset) ||
            config.contactOffset <= 0.0f || config.restOffset < 0.0f ||
            config.restOffset >= config.contactOffset ||
            !(config.staticRootDensity > 0.0f) || !std::isfinite(config.staticRootDensity) ||
            !std::isfinite(config.linearDamping) || config.linearDamping < 0.0f ||
            !std::isfinite(config.angularDamping) || config.angularDamping < 0.0f)
            return fail(&lastError, "simulation config is invalid");
        if (!computeBindWorlds())
            return false;
        if (!initialize())
        {
            clearScene();
            return false;
        }
        if (!applyStaticMesh())
        {
            clearScene();
            return false;
        }
        if (!createActors())
        {
            clearScene();
            return false;
        }
        if (!createJoints())
        {
            clearScene();
            return false;
        }

        created = true;
        return true;
    }

    bool simulateFixed()
    {
        contacts.resetStep();
        scene->simulate(config.fixedTimestep);
        if (!scene->fetchResults(true))
            return fail(&lastError, "PxScene::fetchResults failed: " + errors.lastError());
        ++stepIndex;
        return true;
    }

    physics_portable::RagdollPose pose() const
    {
        physics_portable::RagdollPose result;
        result.stepIndex = stepIndex;
        if (!created || bodyActors.empty())
            return result;

        result.bodyWorld.resize(bodyActors.size());
        for (std::size_t index = 0; index < bodyActors.size(); ++index)
        {
            const PxRigidActor* actor = bodyActors[index];
            if (!actor)
                continue;
            const PxTransform world = actor->getGlobalPose();
            result.bodyWorld[index] = fromPx(world);
        }
        if (description.rootBody < result.bodyWorld.size())
            result.entityWorld = result.bodyWorld[description.rootBody] * rootToEntity;
        return result;
    }

    RagdollDiagnostics diagnostics(const physics_portable::Vec3& referencePosition) const
    {
        RagdollDiagnostics result;
        result.stepIndex = stepIndex;
        result.externalImpulses = externalImpulses;
        result.contactCount = contacts.contactCount;
        result.maxPenetration = contacts.maxPenetration;
        result.totalContactImpulse = contacts.totalImpulse;
        result.contactFound = contacts.found;
        result.contactPersist = contacts.persist;
        result.contactLost = contacts.lost;
        result.activeContactPairs = contacts.activePairs;
        if (!created)
            return result;

        const PxVec3 gravity = toPx(config.gravity);
        const PxVec3 reference = toPx(referencePosition);
        for (const PxRigidDynamic* dynamic : dynamics)
        {
            if (!dynamic)
                continue;
            ++result.dynamicBodies;
            if (!dynamic->isSleeping())
                ++result.awakeBodies;

            const float mass = dynamic->getMass();
            if (!(mass > 0.0f))
                continue;
            const PxTransform actorWorld = dynamic->getGlobalPose();
            const PxTransform massWorld = actorWorld * dynamic->getCMassLocalPose();
            const PxVec3 linear = dynamic->getLinearVelocity();
            const PxVec3 angular = dynamic->getAngularVelocity();

            result.kineticEnergy += 0.5 * static_cast<double>(mass) * linear.magnitudeSquared();
            const PxVec3 angularMass = massWorld.q.rotateInv(angular);
            const PxVec3 inertia = dynamic->getMassSpaceInertiaTensor();
            result.kineticEnergy += 0.5 * static_cast<double>(
                                             inertia.x * angularMass.x * angularMass.x +
                                             inertia.y * angularMass.y * angularMass.y +
                                             inertia.z * angularMass.z * angularMass.z);
            result.potentialEnergy += static_cast<double>(-mass) * gravity.dot(massWorld.p - reference);
            result.maxAngularSpeed = std::max(result.maxAngularSpeed, angular.magnitude());
            result.totalMass += mass;
        }
        result.totalEnergy = result.kineticEnergy + result.potentialEnergy;

        const PxRigidActor* root = bodyActor(description.rootBody);
        const PxRigidDynamic* rootDynamic = root ? root->is<PxRigidDynamic>() : nullptr;
        result.rootMass = rootDynamic ? rootDynamic->getMass() : 0.0f;

        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kEpsilon = 1.0e-4f;
        for (std::size_t index = 0; index < joints.size(); ++index)
        {
            const PxD6Joint* joint = joints[index];
            if (!joint)
                continue;
            const PxTransform relative = joint->getRelativeTransform();
            result.maxJointAnchorError = std::max(result.maxJointAnchorError, relative.p.magnitude());

            if (index >= description.joints.size())
                continue;
            const physics_portable::RagdollJointDesc& source = description.joints[index];
            auto normalizeAngle = [](float angle) {
                while (angle > kPi)
                    angle -= 2.0f * kPi;
                while (angle <= -kPi)
                    angle += 2.0f * kPi;
                return angle;
            };
            auto checkLimit = [&](const physics_portable::RagdollAxisLimit& limit, float angle) {
                const float range = limit.upper - limit.lower;
                if (range >= 2.0f * kPi - kEpsilon)
                    return;
                angle = normalizeAngle(angle);
                const float violation = std::max(0.0f, std::max(angle - limit.upper, limit.lower - angle));
                result.maxJointLimitError = std::max(result.maxJointLimitError, violation);
            };

            if (source.kind == physics_portable::JointKind::Hinge && source.axisCount >= 1)
            {
                checkLimit(source.axes[0], joint->getTwistAngle());
            }
            else if (source.kind == physics_portable::JointKind::Euler3 && source.axisCount >= 3)
            {
                checkLimit(source.axes[0], joint->getTwistAngle());
                checkLimit(source.axes[1], joint->getSwingYAngle());
                checkLimit(source.axes[2], joint->getSwingZAngle());
            }
        }
        return result;
    }
};

PhysXRagdollWorld::PhysXRagdollWorld(const physics_portable::RagdollSimulationConfig& config)
    : impl_(std::make_unique<Impl>(config))
{
}

PhysXRagdollWorld::~PhysXRagdollWorld()
{
    if (impl_)
        impl_->clearScene();
}

PhysXRagdollWorld::PhysXRagdollWorld(PhysXRagdollWorld&&) noexcept = default;
PhysXRagdollWorld& PhysXRagdollWorld::operator=(PhysXRagdollWorld&& other) noexcept
{
    if (this != &other)
    {
        if (impl_)
            impl_->clearScene();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool PhysXRagdollWorld::create(const physics_portable::RagdollDescription& description,
                               const physics_portable::RagdollInitialState& initial,
                               std::string* error)
{
    if (!impl_)
        return fail(error, "world implementation is missing");
    try
    {
        if (!impl_->create(description, initial))
        {
            if (error)
                *error = impl_->lastError;
            return false;
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        impl_->clearScene();
        impl_->lastError = std::string("exception during ragdoll creation: ") + exception.what();
        if (error)
            *error = impl_->lastError;
        return false;
    }
}

void PhysXRagdollWorld::destroy()
{
    if (impl_)
        impl_->clearScene();
}

bool PhysXRagdollWorld::setStaticMesh(const std::vector<physics_portable::Vec3>& vertices,
                                      const std::vector<std::uint32_t>& indices,
                                      std::string* error)
{
    if (!impl_)
        return fail(error, "world implementation is missing");
    impl_->staticVertices = vertices;
    impl_->staticIndices = indices;
    if (!impl_->physics || !impl_->scene)
        return true;
    if (!impl_->applyStaticMesh())
    {
        if (error)
            *error = impl_->lastError;
        return false;
    }
    return true;
}

std::uint32_t PhysXRagdollWorld::staticTriangleCount() const
{
    return impl_ ? impl_->staticTriangles : 0;
}

bool PhysXRagdollWorld::step(float deltaSeconds, std::string* error)
{
    if (!impl_ || !impl_->created)
        return fail(error, "ragdoll world is not created");
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f)
        return fail(error, "delta time is invalid");
    if (deltaSeconds == 0.0f)
        return true;

    impl_->accumulator += std::min(deltaSeconds, 0.25f);
    const float maxAccumulator = impl_->config.fixedTimestep * static_cast<float>(impl_->config.maxSubsteps) * 2.0f;
    if (impl_->accumulator > maxAccumulator)
        impl_->accumulator = maxAccumulator;
    int executed = 0;
    while (impl_->accumulator + 1.0e-7f >= impl_->config.fixedTimestep &&
           executed < impl_->config.maxSubsteps)
    {
        if (!impl_->simulateFixed())
        {
            if (error)
                *error = impl_->lastError;
            return false;
        }
        impl_->accumulator -= impl_->config.fixedTimestep;
        ++executed;
    }

    // Avoid an unbounded catch-up spiral after a long stall.
    if (impl_->accumulator >= impl_->config.fixedTimestep)
        impl_->accumulator = std::fmod(impl_->accumulator, impl_->config.fixedTimestep);
    return true;
}

bool PhysXRagdollWorld::stepFixed(int stepCount, std::string* error)
{
    if (!impl_ || !impl_->created)
        return fail(error, "ragdoll world is not created");
    if (stepCount < 0)
        return fail(error, "step count is negative");
    for (int step = 0; step < stepCount; ++step)
    {
        if (!impl_->simulateFixed())
        {
            if (error)
                *error = impl_->lastError;
            return false;
        }
    }
    return true;
}

physics_portable::RagdollPose PhysXRagdollWorld::pose() const
{
    return impl_ ? impl_->pose() : physics_portable::RagdollPose{};
}

RagdollDiagnostics PhysXRagdollWorld::diagnostics(const physics_portable::Vec3& referencePosition) const
{
    return impl_ ? impl_->diagnostics(referencePosition) : RagdollDiagnostics{};
}

bool PhysXRagdollWorld::massProperties(physics_portable::BodyId body,
                                       BodyMassProperties& properties,
                                       std::string* error) const
{
    properties = BodyMassProperties{};
    if (!impl_ || !impl_->created)
        return fail(error, "ragdoll world is not created");
    PxRigidActor* actor = impl_->bodyActor(body);
    PxRigidDynamic* dynamic = actor ? actor->is<PxRigidDynamic>() : nullptr;
    if (!dynamic)
        return fail(error, "mass properties target is not a dynamic body");

    const PxTransform actorWorld = dynamic->getGlobalPose();
    const PxTransform massLocal = dynamic->getCMassLocalPose();
    const PxTransform massWorld = actorWorld * massLocal;
    const float mass = dynamic->getMass();
    const PxVec3 inertia = dynamic->getMassSpaceInertiaTensor();
    if (!(mass > 0.0f) || !std::isfinite(mass) || !std::isfinite(inertia.x) || !std::isfinite(inertia.y) ||
        !std::isfinite(inertia.z) || !massLocal.isValid())
    {
        return fail(error, "dynamic body has invalid mass properties");
    }

    properties.body = body;
    properties.mass = mass;
    properties.actorWorld = fromPx(actorWorld);
    properties.massWorld = fromPx(massWorld);
    properties.centerOfMassLocal = fromPx(massLocal.p);
    properties.worldCenterOfMass = fromPx(massWorld.p);
    properties.inertiaMassSpace = fromPx(inertia);
    return true;
}

bool PhysXRagdollWorld::applyImpulse(physics_portable::BodyId body,
                                     const physics_portable::Vec3& worldPosition,
                                     const physics_portable::Vec3& impulse,
                                     std::string* error)
{
    if (!impl_ || !impl_->created)
        return fail(error, "ragdoll world is not created");
    PxRigidActor* actor = impl_->bodyActor(body);
    PxRigidDynamic* dynamic = actor ? actor->is<PxRigidDynamic>() : nullptr;
    if (!dynamic)
        return fail(error, "impulse target is not a dynamic body");
    PxRigidBodyExt::addForceAtPos(*dynamic, toPx(impulse), toPx(worldPosition), PxForceMode::eIMPULSE, true);
    ++impl_->externalImpulses;
    return true;
}

bool PhysXRagdollWorld::applyImpulseAtCom(physics_portable::BodyId body,
                                          const physics_portable::Vec3& impulse,
                                          std::string* error)
{
    BodyMassProperties properties;
    if (!massProperties(body, properties, error))
        return false;
    return applyImpulse(body, properties.worldCenterOfMass, impulse, error);
}

bool PhysXRagdollWorld::applyTorque(physics_portable::BodyId body,
                                    const physics_portable::Vec3& torque,
                                    std::string* error)
{
    if (!impl_ || !impl_->created)
        return fail(error, "ragdoll world is not created");
    PxRigidActor* actor = impl_->bodyActor(body);
    PxRigidDynamic* dynamic = actor ? actor->is<PxRigidDynamic>() : nullptr;
    if (!dynamic)
        return fail(error, "torque target is not a dynamic body");
    dynamic->addTorque(toPx(torque), PxForceMode::eIMPULSE, true);
    return true;
}

bool PhysXRagdollWorld::isCreated() const { return impl_ && impl_->created; }

std::size_t PhysXRagdollWorld::bodyCount() const
{
    return impl_ ? impl_->bodyActors.size() : 0;
}

std::size_t PhysXRagdollWorld::jointCount() const
{
    return impl_ ? impl_->joints.size() : 0;
}

std::size_t PhysXRagdollWorld::outstandingAllocations() const
{
    return impl_ ? impl_->allocator.outstanding() : 0;
}

const physics_portable::RagdollDescription& PhysXRagdollWorld::description() const
{
    static const physics_portable::RagdollDescription empty;
    return impl_ ? impl_->description : empty;
}

const physics_portable::RagdollSimulationConfig& PhysXRagdollWorld::config() const
{
    static const physics_portable::RagdollSimulationConfig defaults{};
    return impl_ ? impl_->config : defaults;
}

const std::string& PhysXRagdollWorld::lastError() const
{
    static const std::string empty;
    return impl_ ? impl_->lastError : empty;
}

} // namespace physics_physx
