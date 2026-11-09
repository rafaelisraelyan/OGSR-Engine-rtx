#pragma once

#include "physx_ragdoll_v2_convert.h"
#include "physics_physx/physx_ragdoll_c_v2.h"
#include "physics_physx/physx_ragdoll_registry.h"
#include "physics_portable/ragdoll.h"

#include <PxPhysicsAPI.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace physx;

namespace physics_physx
{

class PhysXRagdollInstance;

struct BodyMassProperties
{
    physics_portable::BodyId body = physics_portable::kInvalidBody;
    float mass = 0.0f;
    physics_portable::Transform actorWorld{};
    physics_portable::Transform massWorld{};
    physics_portable::Vec3 centerOfMassLocal{};
    physics_portable::Vec3 worldCenterOfMass{};
    physics_portable::Vec3 inertiaMassSpace{};
};

class PhysXRagdollScene
{
public:
    explicit PhysXRagdollScene(const OgsrPhysX2SceneConfig& config);
    ~PhysXRagdollScene();

    PhysXRagdollScene(const PhysXRagdollScene&) = delete;
    PhysXRagdollScene& operator=(const PhysXRagdollScene&) = delete;

    bool initialize(std::string* error);
    bool setStaticMesh(const std::vector<physics_portable::Vec3>& vertices,
                       const std::vector<std::uint32_t>& indices, std::string* error);
    bool addStaticFragment(std::uint64_t key, const std::vector<physics_portable::Vec3>& vertices,
                           const std::vector<std::uint32_t>& indices, std::string* error);
    bool step(float deltaSeconds, std::uint64_t frameId, OgsrPhysX2StepResult* result, std::string* error);
    bool createInstance(const RagdollInstanceCreateInfo& info, PhysXRagdollInstance*& outInstance,
                        std::string* error);
    void destroyInstance(PhysXRagdollInstance* instance);

    bool isStepping() const;
    std::uint32_t staticTriangleCount() const;
    PxPhysics& physics();
    PxScene& scene();
    PxMaterial* materialFor(std::uint16_t id, const physics_portable::RagdollMaterialDesc& desc,
                            std::string* error);
    void* registerContact(std::uint32_t collisionInstanceId, PhysXRagdollInstance* instance);
    void unregisterContact(std::uint32_t collisionInstanceId);
    PhysXRagdollInstance* findContact(std::uint32_t collisionInstanceId) const;
    void resetAllFiltering();
    const OgsrPhysX2SceneConfig& config() const;
    std::uint64_t stepIndex() const;
    bool failed() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class PhysXRagdollInstance
{
public:
    PhysXRagdollInstance(PhysXRagdollScene& scene, const RagdollInstanceCreateInfo& info);
    ~PhysXRagdollInstance();

    PhysXRagdollInstance(const PhysXRagdollInstance&) = delete;
    PhysXRagdollInstance& operator=(const PhysXRagdollInstance&) = delete;

    bool initialize(std::string* error);
    void destroy();

    bool pose(physics_portable::RagdollPose& pose, std::string* error) const;
    bool applyImpulse(std::uint16_t body, const physics_portable::Vec3& worldPosition,
                      const physics_portable::Vec3& impulse, std::string* error);
    bool applyCentralImpulse(std::uint16_t body, const physics_portable::Vec3& impulse, std::string* error);
    bool bodyMass(std::uint16_t body, BodyMassProperties& properties, std::string* error) const;
    OgsrPhysXRagdollDiagnostics diagnostics(const physics_portable::Vec3& referencePosition) const;
    OgsrPhysXRagdollInfo info() const;

    std::uint32_t collisionInstanceId() const;
    OgsrPhysX2RagdollHandle handle() const;
    std::uint32_t bodyCount() const;
    std::uint32_t jointCount() const;
    bool isCreated() const;
    bool isSceneStepping() const;

    void resetContactStep();
    void onContact(std::uint32_t contactCount, float impulse, float penetration, bool found, bool persist,
                   bool lost);
    const std::vector<PxRigidActor*>& actors() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace physics_physx
