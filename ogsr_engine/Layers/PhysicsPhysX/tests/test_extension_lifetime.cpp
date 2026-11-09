#include "physics_physx/physx_extension_lifetime.h"

#include <PxPhysicsAPI.h>
#include "extensions/PxDefaultAllocator.h"
#include "extensions/PxDefaultErrorCallback.h"

using namespace physx;

#include <cstdio>
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
} // namespace

int main()
{
    PxDefaultAllocator allocator;
    PxDefaultErrorCallback errorCallback;
    PxFoundation* foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback);
    check(foundation != nullptr, "foundation creation");
    if (!foundation)
        return 1;
    PxPhysics* physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale(), false, nullptr);
    check(physics != nullptr, "physics creation");
    if (!physics)
    {
        foundation->release();
        return 1;
    }

    std::string error;
    check(physics_physx::acquirePhysXExtensions(physics_physx::PhysXExtensionOwner::LegacyV1, *physics, &error),
          "v1 acquires the extension lease");
    check(physics_physx::physXExtensionOwner() == physics_physx::PhysXExtensionOwner::LegacyV1,
          "lease owner is v1");
    check(physics_physx::physXExtensionRefCount() == 1, "lease refcount is one");

    error.clear();
    check(!physics_physx::acquirePhysXExtensions(physics_physx::PhysXExtensionOwner::MultiV2, *physics, &error),
          "v2 acquisition is rejected while v1 owns the lease");
    check(error.find("legacy v1") != std::string::npos, "rejection names the v1 owner");
    check(physics_physx::physXExtensionRefCount() == 1, "rejected acquisition does not change the refcount");

    check(physics_physx::acquirePhysXExtensions(physics_physx::PhysXExtensionOwner::LegacyV1, *physics, &error),
          "v1 reentrant acquisition succeeds");
    check(physics_physx::physXExtensionRefCount() == 2, "v1 reentrant acquisition increments the refcount");
    physics_physx::releasePhysXExtensions(physics_physx::PhysXExtensionOwner::LegacyV1);
    check(physics_physx::physXExtensionRefCount() == 1, "v1 release decrements the refcount");

    physics_physx::releasePhysXExtensions(physics_physx::PhysXExtensionOwner::MultiV2);
    check(physics_physx::physXExtensionRefCount() == 1 &&
              physics_physx::physXExtensionOwner() == physics_physx::PhysXExtensionOwner::LegacyV1,
          "release by a non-owner is ignored");

    physics_physx::releasePhysXExtensions(physics_physx::PhysXExtensionOwner::LegacyV1);
    check(physics_physx::physXExtensionRefCount() == 0, "last v1 release drops the refcount to zero");
    check(physics_physx::physXExtensionOwner() == physics_physx::PhysXExtensionOwner::None,
          "last release clears the owner");

    physics_physx::releasePhysXExtensions(physics_physx::PhysXExtensionOwner::LegacyV1);
    check(physics_physx::physXExtensionRefCount() == 0, "double release is ignored");

    check(physics_physx::acquirePhysXExtensions(physics_physx::PhysXExtensionOwner::MultiV2, *physics, &error),
          "v2 acquires after v1 release");
    check(physics_physx::physXExtensionOwner() == physics_physx::PhysXExtensionOwner::MultiV2,
          "lease owner is v2");
    check(physics_physx::physXExtensionRefCount() == 1, "v2 refcount is one");
    physics_physx::releasePhysXExtensions(physics_physx::PhysXExtensionOwner::MultiV2);
    check(physics_physx::physXExtensionRefCount() == 0, "v2 release drops the refcount");

    for (int iteration = 0; iteration < 5; ++iteration)
    {
        check(physics_physx::acquirePhysXExtensions(physics_physx::PhysXExtensionOwner::LegacyV1, *physics, &error),
              "loop v1 acquire");
        physics_physx::releasePhysXExtensions(physics_physx::PhysXExtensionOwner::LegacyV1);
        check(physics_physx::acquirePhysXExtensions(physics_physx::PhysXExtensionOwner::MultiV2, *physics, &error),
              "loop v2 acquire");
        physics_physx::releasePhysXExtensions(physics_physx::PhysXExtensionOwner::MultiV2);
    }

    physics->release();
    foundation->release();

    std::printf("physics_physx_extension_lifetime tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
