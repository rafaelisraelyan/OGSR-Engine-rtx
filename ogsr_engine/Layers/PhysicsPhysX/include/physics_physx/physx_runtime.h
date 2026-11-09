#pragma once

// Runtime PhysX context for the OGSR game thread.
//
// This header deliberately contains no PhysX SDK types. The implementation
// owns PxFoundation/PxPhysics/dispatcher/PxScene behind a pimpl and is only
// linked into the game when the experimental runtime build is enabled.

#include "physics_portable/ragdoll.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace physics_physx
{
struct RuntimeConfig
{
    float fixedTimestep = 0.01f;
    float gravityY = -19.62f;
    std::uint32_t workerThreads = 2;
    std::uint32_t maxSubsteps = 4;
};

struct RuntimeStaticMesh
{
    std::vector<physics_portable::Vec3> vertices;
    std::vector<std::uint32_t> indices;
};

struct RuntimeTestBodyState
{
    physics_portable::Transform pose{};
    bool hasContact = false;
    std::uint32_t contactCount = 0;
    std::uint32_t stepCount = 0;
    std::uint32_t lastSubsteps = 0;
};

class PhysXRuntimeContext
{
public:
    PhysXRuntimeContext();
    ~PhysXRuntimeContext();

    PhysXRuntimeContext(const PhysXRuntimeContext&) = delete;
    PhysXRuntimeContext& operator=(const PhysXRuntimeContext&) = delete;

    bool initialize(const RuntimeConfig& config, std::string* error);
    void shutdown();
    bool isInitialized() const;

    bool setStaticMesh(const RuntimeStaticMesh& mesh, std::string* error);
    bool createTestBox(const physics_portable::Vec3& position,
                       const physics_portable::Vec3& halfExtents,
                       float density,
                       std::string* error);

    // Consumes real frame time and executes whole fixed steps. It is safe to
    // call with zero while the game is paused or during level loading.
    bool step(float frameDeltaSeconds, std::string* error);

    RuntimeTestBodyState testBodyState() const;
    std::uint32_t staticTriangleCount() const;
    std::uint32_t dynamicBodyCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace physics_physx
