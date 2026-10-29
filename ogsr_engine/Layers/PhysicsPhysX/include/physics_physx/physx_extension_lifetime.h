#pragma once

// Process-wide PhysX extensions lifetime.
//
// PhysX 5.6.1 initializes its extensions through a process-global singleton
// (PxInitExtensions / PxCloseExtensions). The singleton is bound to the
// PxPhysics instance that initialized it, so two independent SDK contexts
// (legacy v1 world and the v2 multi-ragdoll backend) must not overlap.
//
// This helper implements a fail-closed exclusive lease:
//   - one owner at a time (LegacyV1 or MultiV2), re-entrant for that owner;
//   - PxInitExtensions is called exactly once for the first acquisition;
//   - PxCloseExtensions is called exactly once when the last reference of the
//     owning path is released;
//   - acquiring for a different owner while the lease is held fails with a
//     diagnostic instead of silently sharing the singleton;
//   - release is idempotent and never drops the reference count below zero.
//
// The v1 and v2 paths therefore remain individually supported, while their
// simultaneous use is rejected until a shared-context design is proven safe.

#include <string>

namespace physx
{
class PxPhysics;
}

namespace physics_physx
{

enum class PhysXExtensionOwner
{
    None,
    LegacyV1,
    MultiV2
};

// Returns false and fills `error` when a different owner already holds the
// lease, or when PxInitExtensions fails. A failed acquisition leaves the
// reference count unchanged.
bool acquirePhysXExtensions(PhysXExtensionOwner owner, physx::PxPhysics& physics, std::string* error);

// Releases one reference of `owner`. Unknown owners, repeated releases and
// underflow are ignored. The last release closes the extensions singleton.
void releasePhysXExtensions(PhysXExtensionOwner owner);

PhysXExtensionOwner physXExtensionOwner();
int physXExtensionRefCount();

} // namespace physics_physx
