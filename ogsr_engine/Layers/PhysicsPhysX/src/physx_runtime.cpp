#include "physics_physx/physx_runtime.h"

#include "PxPhysicsAPI.h"
#include "cooking/PxCooking.h"
#include "extensions/PxDefaultAllocator.h"
#include "extensions/PxDefaultCpuDispatcher.h"
#include "extensions/PxExtensionsAPI.h"
#include "extensions/PxRigidBodyExt.h"
#include "geometry/PxTriangleMeshGeometry.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

using namespace physx;

namespace physics_physx
{
namespace
{
PxVec3 toPx(const physics_portable::Vec3& value) { return PxVec3(value.x, value.y, value.z); }
physics_portable::Vec3 fromPx(const PxVec3& value) { return physics_portable::Vec3{value.x, value.y, value.z}; }
physics_portable::Transform fromPx(const PxTransform& value)
{
    return physics_portable::Transform{
        physics_portable::Quat{value.q.x, value.q.y, value.q.z, value.q.w},
        physics_portable::Vec3{value.p.x, value.p.y, value.p.z}};
}

bool fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
    return false;
}

class RuntimeErrorCallback final : public PxErrorCallback
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

// The runtime test scene owns one static triangle mesh and one dynamic box.
// Report pair events so the smoke test can distinguish "resting on geometry"
// from a body that merely stopped moving in mid-air.
PxFilterFlags runtimeFilterShader(PxFilterObjectAttributes,
                                  PxFilterData,
                                  PxFilterObjectAttributes,
                                  PxFilterData,
                                  PxPairFlags& pairFlags,
                                  const void*,
                                  PxU32)
{
    pairFlags = PxPairFlag::eSOLVE_CONTACT | PxPairFlag::eDETECT_DISCRETE_CONTACT |
                PxPairFlag::eNOTIFY_TOUCH_FOUND | PxPairFlag::eNOTIFY_TOUCH_LOST;
    return PxFilterFlag::eDEFAULT;
}

class ContactRecorder final : public PxSimulationEventCallback
{
public:
    void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
    void onWake(PxActor**, PxU32) override {}
    void onSleep(PxActor**, PxU32) override {}
    void onContact(const PxContactPairHeader&, const PxContactPair*, PxU32 pairCount) override
    {
        contactCount_.fetch_add(pairCount, std::memory_order_relaxed);
    }
    void onTrigger(PxTriggerPair*, PxU32) override {}
    void onAdvance(const PxRigidBody* const*, const PxTransform*, PxU32) override {}

    std::uint32_t contactCount() const { return contactCount_.load(std::memory_order_relaxed); }
    void reset() { contactCount_.store(0, std::memory_order_relaxed); }

private:
    std::atomic<std::uint32_t> contactCount_{0};
};

bool finiteVec(const physics_portable::Vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
} // namespace

struct PhysXRuntimeContext::Impl
{
    RuntimeConfig config;
    PxDefaultAllocator allocator;
    RuntimeErrorCallback errors;
    PxFoundation* foundation = nullptr;
    PxPhysics* physics = nullptr;
    PxDefaultCpuDispatcher* dispatcher = nullptr;
    PxScene* scene = nullptr;
    PxMaterial* material = nullptr;
    PxRigidStatic* staticActor = nullptr;
    PxTriangleMesh* staticMesh = nullptr;
    PxRigidDynamic* testBody = nullptr;
    std::vector<PxRigidDynamic*> dynamics;
    ContactRecorder contacts;
    std::uint32_t staticTriangles = 0;
    std::uint32_t dynamicBodies = 0;
    std::uint32_t stepCount = 0;
    std::uint32_t lastSubsteps = 0;
    float accumulator = 0.0f;
    RuntimeTestBodyState state;

    void releaseActors()
    {
        for (PxRigidDynamic* dynamic : dynamics)
        {
            if (dynamic)
                dynamic->release();
        }
        dynamics.clear();
        testBody = nullptr;
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
        dynamicBodies = 0;
    }

    void release()
    {
        releaseActors();
        if (scene)
        {
            scene->release();
            scene = nullptr;
        }
        if (material)
        {
            material->release();
            material = nullptr;
        }
        if (dispatcher)
        {
            dispatcher->release();
            dispatcher = nullptr;
        }
        if (physics)
        {
            PxCloseExtensions();
            physics->release();
            physics = nullptr;
        }
        if (foundation)
        {
            foundation->release();
            foundation = nullptr;
        }
    }
};

PhysXRuntimeContext::PhysXRuntimeContext() : impl_(std::make_unique<Impl>()) {}

PhysXRuntimeContext::~PhysXRuntimeContext() { shutdown(); }

bool PhysXRuntimeContext::initialize(const RuntimeConfig& config, std::string* error)
{
    if (!impl_)
        return fail(error, "runtime context is not allocated");
    if (impl_->scene)
        return fail(error, "runtime context is already initialized");
    if (!(config.fixedTimestep > 0.0f) || !std::isfinite(config.fixedTimestep) ||
        config.maxSubsteps == 0 || config.workerThreads == 0)
        return fail(error, "invalid runtime config");

    impl_->config = config;
    impl_->foundation = PxCreateFoundation(PX_PHYSICS_VERSION, impl_->allocator, impl_->errors);
    if (!impl_->foundation)
        return fail(error, "PxCreateFoundation failed: " + impl_->errors.lastError());

    impl_->physics = PxCreatePhysics(PX_PHYSICS_VERSION, *impl_->foundation, PxTolerancesScale(), false, nullptr);
    if (!impl_->physics)
    {
        impl_->release();
        return fail(error, "PxCreatePhysics failed: " + impl_->errors.lastError());
    }
    if (!PxInitExtensions(*impl_->physics, nullptr))
    {
        impl_->release();
        return fail(error, "PxInitExtensions failed: " + impl_->errors.lastError());
    }

    impl_->dispatcher = PxDefaultCpuDispatcherCreate(config.workerThreads);
    if (!impl_->dispatcher)
    {
        impl_->release();
        return fail(error, "PxDefaultCpuDispatcherCreate failed");
    }

    PxSceneDesc sceneDesc(impl_->physics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0.0f, config.gravityY, 0.0f);
    sceneDesc.cpuDispatcher = impl_->dispatcher;
    sceneDesc.filterShader = runtimeFilterShader;
    sceneDesc.solverType = PxSolverType::eTGS;
    impl_->scene = impl_->physics->createScene(sceneDesc);
    if (!impl_->scene)
    {
        impl_->release();
        return fail(error, "PxScene creation failed: " + impl_->errors.lastError());
    }
    impl_->scene->setSimulationEventCallback(&impl_->contacts);

    impl_->material = impl_->physics->createMaterial(0.8f, 0.8f, 0.0f);
    if (!impl_->material)
    {
        impl_->release();
        return fail(error, "PxMaterial creation failed: " + impl_->errors.lastError());
    }
    return true;
}

void PhysXRuntimeContext::shutdown()
{
    if (impl_)
        impl_->release();
}

bool PhysXRuntimeContext::isInitialized() const { return impl_ && impl_->scene != nullptr; }

bool PhysXRuntimeContext::setStaticMesh(const RuntimeStaticMesh& mesh, std::string* error)
{
    if (!isInitialized())
        return fail(error, "runtime context is not initialized");
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3 != 0)
        return fail(error, "static mesh must contain complete triangles");
    if (mesh.indices.size() / 3 > 200000)
        return fail(error, "static mesh exceeds the diagnostic triangle cap");
    for (const physics_portable::Vec3& vertex : mesh.vertices)
    {
        if (!finiteVec(vertex))
            return fail(error, "static mesh contains a non-finite vertex");
    }
    for (std::uint32_t index : mesh.indices)
    {
        if (index >= mesh.vertices.size())
            return fail(error, "static mesh index is out of range");
    }

    if (impl_->staticActor)
    {
        impl_->staticActor->release();
        impl_->staticActor = nullptr;
    }
    if (impl_->staticMesh)
    {
        impl_->staticMesh->release();
        impl_->staticMesh = nullptr;
    }

    std::vector<PxVec3> points;
    points.reserve(mesh.vertices.size());
    for (const physics_portable::Vec3& vertex : mesh.vertices)
        points.push_back(toPx(vertex));

    // Triangle meshes use one-sided contact generation. OGSR static geometry
    // does not guarantee a globally consistent winding, so cook each source
    // triangle with a reversed duplicate as recommended by PhysX.
    std::vector<PxU32> triangles;
    triangles.reserve(mesh.indices.size() * 2);
    for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3)
    {
        const PxU32 a = mesh.indices[triangle + 0];
        const PxU32 b = mesh.indices[triangle + 1];
        const PxU32 c = mesh.indices[triangle + 2];
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

    PxCookingParams cookingParams(impl_->physics->getTolerancesScale());
    impl_->staticMesh = PxCreateTriangleMesh(cookingParams, desc);
    if (!impl_->staticMesh)
    {
        return fail(error, "PxCreateTriangleMesh failed: " + impl_->errors.lastError());
    }

    impl_->staticActor = impl_->physics->createRigidStatic(PxTransform(PxIdentity));
    if (!impl_->staticActor)
    {
        impl_->staticMesh->release();
        impl_->staticMesh = nullptr;
        return fail(error, "static PxRigidStatic creation failed");
    }
    PxShape* shape = impl_->physics->createShape(PxTriangleMeshGeometry(impl_->staticMesh), *impl_->material, true);
    if (!shape)
    {
        impl_->staticActor->release();
        impl_->staticActor = nullptr;
        impl_->staticMesh->release();
        impl_->staticMesh = nullptr;
        return fail(error, "static triangle shape creation failed");
    }
    shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
    shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
    impl_->staticActor->attachShape(*shape);
    shape->release();
    impl_->scene->addActor(*impl_->staticActor);
    impl_->staticTriangles = static_cast<std::uint32_t>(mesh.indices.size() / 3);
    return true;
}

bool PhysXRuntimeContext::createTestBox(const physics_portable::Vec3& position,
                                        const physics_portable::Vec3& halfExtents,
                                        float density,
                                        std::string* error)
{
    if (!isInitialized())
        return fail(error, "runtime context is not initialized");
    if (!finiteVec(position) || !finiteVec(halfExtents) || !(halfExtents.x > 0.0f) ||
        !(halfExtents.y > 0.0f) || !(halfExtents.z > 0.0f) || !(density > 0.0f) ||
        !std::isfinite(density))
        return fail(error, "invalid test box dimensions or density");

    if (impl_->dynamics.size() >= 64)
        return fail(error, "diagnostic dynamic body cap reached");

    impl_->testBody = impl_->physics->createRigidDynamic(PxTransform(toPx(position), PxQuat(PxIdentity)));
    if (!impl_->testBody)
        return fail(error, "test PxRigidDynamic creation failed");
    PxShape* shape = impl_->physics->createShape(PxBoxGeometry(toPx(halfExtents)), *impl_->material, true);
    if (!shape)
    {
        impl_->testBody->release();
        impl_->testBody = nullptr;
        return fail(error, "test box shape creation failed");
    }
    shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
    shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
    impl_->testBody->attachShape(*shape);
    shape->release();
    PxRigidBodyExt::updateMassAndInertia(*impl_->testBody, density);
    impl_->testBody->setLinearDamping(0.05f);
    impl_->testBody->setAngularDamping(0.05f);
    impl_->scene->addActor(*impl_->testBody);
    impl_->dynamics.push_back(impl_->testBody);
    impl_->dynamicBodies = static_cast<std::uint32_t>(impl_->dynamics.size());
    impl_->accumulator = 0.0f;
    impl_->stepCount = 0;
    impl_->lastSubsteps = 0;
    impl_->state = RuntimeTestBodyState{};
    impl_->state.pose = fromPx(impl_->testBody->getGlobalPose());
    return true;
}

bool PhysXRuntimeContext::step(float frameDeltaSeconds, std::string* error)
{
    if (!isInitialized())
        return fail(error, "runtime context is not initialized");
    if (!std::isfinite(frameDeltaSeconds) || frameDeltaSeconds < 0.0f)
        return fail(error, "invalid frame delta");

    impl_->lastSubsteps = 0;
    if (frameDeltaSeconds <= 0.0f)
        return true;

    impl_->accumulator += std::min(frameDeltaSeconds, 0.25f);
    while (impl_->accumulator + 1.0e-6f >= impl_->config.fixedTimestep &&
           impl_->lastSubsteps < impl_->config.maxSubsteps)
    {
        impl_->scene->simulate(impl_->config.fixedTimestep);
        if (!impl_->scene->fetchResults(true))
            return fail(error, "PxScene::fetchResults failed: " + impl_->errors.lastError());
        impl_->accumulator -= impl_->config.fixedTimestep;
        ++impl_->lastSubsteps;
        ++impl_->stepCount;
        if (impl_->testBody)
        {
            impl_->state.pose = fromPx(impl_->testBody->getGlobalPose());
            impl_->state.hasContact = impl_->contacts.contactCount() > 0;
            impl_->state.contactCount = impl_->contacts.contactCount();
            impl_->state.stepCount = impl_->stepCount;
            impl_->state.lastSubsteps = impl_->lastSubsteps;
        }
    }

    // Do not build an unbounded backlog if a frame was stalled.
    if (impl_->lastSubsteps >= impl_->config.maxSubsteps)
        impl_->accumulator = 0.0f;
    return true;
}

RuntimeTestBodyState PhysXRuntimeContext::testBodyState() const
{
    if (!impl_)
        return {};
    RuntimeTestBodyState result = impl_->state;
    result.stepCount = impl_->stepCount;
    result.lastSubsteps = impl_->lastSubsteps;
    result.hasContact = impl_->contacts.contactCount() > 0;
    result.contactCount = impl_->contacts.contactCount();
    return result;
}

std::uint32_t PhysXRuntimeContext::staticTriangleCount() const
{
    return impl_ ? impl_->staticTriangles : 0;
}

std::uint32_t PhysXRuntimeContext::dynamicBodyCount() const
{
    return impl_ ? impl_->dynamicBodies : 0;
}
} // namespace physics_physx
