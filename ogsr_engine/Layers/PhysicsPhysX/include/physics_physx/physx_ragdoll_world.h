#pragma once

// PhysX 5.6.1 CPU implementation of the portable ragdoll backend.
// This header intentionally hides all PhysX types behind pimpl.

#include "physics_portable/ragdoll.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace physics_physx
{

struct RagdollDiagnostics
{
    double kineticEnergy = 0.0;
    double potentialEnergy = 0.0;
    double totalEnergy = 0.0;
    float maxAngularSpeed = 0.0f;
    float maxJointAnchorError = 0.0f;
    float maxJointLimitError = 0.0f;
    float maxPenetration = 0.0f;
    float totalContactImpulse = 0.0f;
    float totalMass = 0.0f;
    float rootMass = 0.0f;
    std::uint32_t contactCount = 0;
    std::uint32_t contactFound = 0;
    std::uint32_t contactPersist = 0;
    std::uint32_t contactLost = 0;
    std::uint32_t activeContactPairs = 0;
    std::uint32_t awakeBodies = 0;
    std::uint32_t dynamicBodies = 0;
    std::uint32_t externalImpulses = 0;
    std::uint64_t stepIndex = 0;
};

// Effective mass properties read back from the live PxRigidDynamic. This is
// the source of truth for impulse application, logging and A/B measurements;
// it is not the capture-time COM.
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

class PhysXRagdollWorld
{
public:
    explicit PhysXRagdollWorld(const physics_portable::RagdollSimulationConfig& config = {});
    ~PhysXRagdollWorld();

    PhysXRagdollWorld(const PhysXRagdollWorld&) = delete;
    PhysXRagdollWorld& operator=(const PhysXRagdollWorld&) = delete;
    PhysXRagdollWorld(PhysXRagdollWorld&&) noexcept;
    PhysXRagdollWorld& operator=(PhysXRagdollWorld&&) noexcept;

    bool create(const physics_portable::RagdollDescription& description,
                const physics_portable::RagdollInitialState& initial,
                std::string* error = nullptr);
    void destroy();

    // Optional static collision geometry for the level fragment around the
    // ragdoll. Can be called before create(); it is uploaded when the scene
    // is created. A later call replaces the previous mesh.
    bool setStaticMesh(const std::vector<physics_portable::Vec3>& vertices,
                       const std::vector<std::uint32_t>& indices,
                       std::string* error = nullptr);
    std::uint32_t staticTriangleCount() const;

    // Accumulates real time and executes whole fixed timesteps.
    bool step(float deltaSeconds, std::string* error = nullptr);

    // Deterministic helper for tests: executes exactly this many fixed steps.
    bool stepFixed(int stepCount, std::string* error = nullptr);

    physics_portable::RagdollPose pose() const;
    RagdollDiagnostics diagnostics(const physics_portable::Vec3& referencePosition) const;

    // Actual PhysX mass properties. The returned world COM is
    // actorWorld * getCMassLocalPose().p.
    bool massProperties(physics_portable::BodyId body,
                        BodyMassProperties& properties,
                        std::string* error = nullptr) const;

    bool applyImpulse(physics_portable::BodyId body,
                      const physics_portable::Vec3& worldPosition,
                      const physics_portable::Vec3& impulse,
                      std::string* error = nullptr);

    // Applies exactly one impulse at the actual PhysX COM. No torque or
    // compensating impulse is added.
    bool applyImpulseAtCom(physics_portable::BodyId body,
                           const physics_portable::Vec3& impulse,
                           std::string* error = nullptr);

    bool applyTorque(physics_portable::BodyId body,
                     const physics_portable::Vec3& torque,
                     std::string* error = nullptr); // angular impulse (PxForceMode::eIMPULSE)

    bool isCreated() const;
    std::size_t bodyCount() const;
    std::size_t jointCount() const;
    std::size_t outstandingAllocations() const;
    const physics_portable::RagdollDescription& description() const;
    const physics_portable::RagdollSimulationConfig& config() const;
    const std::string& lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace physics_physx
