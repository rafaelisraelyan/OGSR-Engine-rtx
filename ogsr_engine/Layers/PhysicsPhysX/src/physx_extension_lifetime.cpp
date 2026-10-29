#include "physics_physx/physx_extension_lifetime.h"

#include <PxPhysicsAPI.h>

#include <mutex>

namespace physics_physx
{
namespace
{
std::mutex g_mutex;
PhysXExtensionOwner g_owner = PhysXExtensionOwner::None;
int g_refCount = 0;
bool g_initialized = false;

const char* ownerName(PhysXExtensionOwner owner)
{
    switch (owner)
    {
    case PhysXExtensionOwner::LegacyV1:
        return "legacy v1";
    case PhysXExtensionOwner::MultiV2:
        return "multi v2";
    default:
        return "none";
    }
}

bool fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
    return false;
}
} // namespace

bool acquirePhysXExtensions(PhysXExtensionOwner owner, physx::PxPhysics& physics, std::string* error)
{
    if (owner == PhysXExtensionOwner::None)
        return fail(error, "PhysX extension owner is none");

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_owner != PhysXExtensionOwner::None && g_owner != owner)
    {
        return fail(error, std::string("PhysX extensions are already owned by the ") + ownerName(g_owner) +
                               " runtime; simultaneous v1/v2 use is not supported");
    }

    if (!g_initialized)
    {
        if (!PxInitExtensions(physics, nullptr))
            return fail(error, "PxInitExtensions failed");
        g_initialized = true;
    }

    g_owner = owner;
    ++g_refCount;
    return true;
}

void releasePhysXExtensions(PhysXExtensionOwner owner)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (owner == PhysXExtensionOwner::None || g_owner != owner || g_refCount <= 0)
        return;

    --g_refCount;
    if (g_refCount == 0)
    {
        if (g_initialized)
        {
            PxCloseExtensions();
            g_initialized = false;
        }
        g_owner = PhysXExtensionOwner::None;
    }
}

PhysXExtensionOwner physXExtensionOwner()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_owner;
}

int physXExtensionRefCount()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_refCount;
}

} // namespace physics_physx
