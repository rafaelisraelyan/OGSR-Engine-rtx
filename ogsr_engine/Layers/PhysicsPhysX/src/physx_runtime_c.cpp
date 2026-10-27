#include "physics_physx/physx_runtime_c.h"
#include "physics_physx/physx_runtime.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace
{
void writeError(char* error, size_t errorSize, const std::string& message)
{
    if (!error || errorSize == 0)
        return;
    const size_t count = std::min(errorSize - 1, message.size());
    std::memcpy(error, message.data(), count);
    error[count] = 0;
}

void clearError(char* error, size_t errorSize)
{
    if (error && errorSize)
        error[0] = 0;
}

physics_physx::PhysXRuntimeContext* asContext(OgsrPhysXRuntimeHandle handle)
{
    return static_cast<physics_physx::PhysXRuntimeContext*>(handle);
}
} // namespace

extern "C"
{
OGSR_PHYSX_RUNTIME_API OgsrPhysXRuntimeHandle OgsrPhysXRuntimeCreate(const OgsrPhysXRuntimeConfig* config,
                                                                    char* error,
                                                                    size_t errorSize)
{
    clearError(error, errorSize);
    try
    {
        if (!config)
        {
            writeError(error, errorSize, "runtime config is null");
            return nullptr;
        }
        physics_physx::RuntimeConfig runtimeConfig;
        runtimeConfig.fixedTimestep = config->fixedTimestep;
        runtimeConfig.gravityY = config->gravityY;
        runtimeConfig.workerThreads = config->workerThreads;
        runtimeConfig.maxSubsteps = config->maxSubsteps;

        auto* context = new physics_physx::PhysXRuntimeContext();
        std::string message;
        if (!context->initialize(runtimeConfig, &message))
        {
            writeError(error, errorSize, message);
            delete context;
            return nullptr;
        }
        return context;
    }
    catch (const std::exception& exception)
    {
        writeError(error, errorSize, exception.what());
        return nullptr;
    }
    catch (...)
    {
        writeError(error, errorSize, "unknown exception in PhysX runtime create");
        return nullptr;
    }
}

OGSR_PHYSX_RUNTIME_API void OgsrPhysXRuntimeDestroy(OgsrPhysXRuntimeHandle handle)
{
    try
    {
        auto* context = asContext(handle);
        if (context)
        {
            context->shutdown();
            delete context;
        }
    }
    catch (...)
    {
    }
}

OGSR_PHYSX_RUNTIME_API int OgsrPhysXRuntimeSetStaticMesh(OgsrPhysXRuntimeHandle handle,
                                                        const float* vertices,
                                                        uint32_t vertexCount,
                                                        const uint32_t* indices,
                                                        uint32_t indexCount,
                                                        char* error,
                                                        size_t errorSize)
{
    clearError(error, errorSize);
    try
    {
        auto* context = asContext(handle);
        if (!context || !vertices || !indices || vertexCount == 0 || indexCount == 0)
        {
            writeError(error, errorSize, "invalid static mesh argument");
            return 0;
        }
        physics_physx::RuntimeStaticMesh mesh;
        mesh.vertices.reserve(vertexCount);
        for (uint32_t index = 0; index < vertexCount; ++index)
        {
            mesh.vertices.push_back(physics_portable::Vec3{vertices[index * 3 + 0], vertices[index * 3 + 1],
                                                           vertices[index * 3 + 2]});
        }
        mesh.indices.assign(indices, indices + indexCount);
        std::string message;
        if (!context->setStaticMesh(mesh, &message))
        {
            writeError(error, errorSize, message);
            return 0;
        }
        return 1;
    }
    catch (const std::exception& exception)
    {
        writeError(error, errorSize, exception.what());
        return 0;
    }
    catch (...)
    {
        writeError(error, errorSize, "unknown exception in PhysX runtime static mesh upload");
        return 0;
    }
}

OGSR_PHYSX_RUNTIME_API int OgsrPhysXRuntimeCreateTestBox(OgsrPhysXRuntimeHandle handle,
                                                        float x,
                                                        float y,
                                                        float z,
                                                        float halfX,
                                                        float halfY,
                                                        float halfZ,
                                                        float density,
                                                        char* error,
                                                        size_t errorSize)
{
    clearError(error, errorSize);
    try
    {
        auto* context = asContext(handle);
        if (!context)
        {
            writeError(error, errorSize, "runtime handle is null");
            return 0;
        }
        std::string message;
        if (!context->createTestBox(physics_portable::Vec3{x, y, z},
                                    physics_portable::Vec3{halfX, halfY, halfZ}, density, &message))
        {
            writeError(error, errorSize, message);
            return 0;
        }
        return 1;
    }
    catch (const std::exception& exception)
    {
        writeError(error, errorSize, exception.what());
        return 0;
    }
    catch (...)
    {
        writeError(error, errorSize, "unknown exception in PhysX runtime test box creation");
        return 0;
    }
}

OGSR_PHYSX_RUNTIME_API int OgsrPhysXRuntimeStep(OgsrPhysXRuntimeHandle handle,
                                               float frameDeltaSeconds,
                                               char* error,
                                               size_t errorSize)
{
    clearError(error, errorSize);
    try
    {
        auto* context = asContext(handle);
        if (!context)
        {
            writeError(error, errorSize, "runtime handle is null");
            return 0;
        }
        std::string message;
        if (!context->step(frameDeltaSeconds, &message))
        {
            writeError(error, errorSize, message);
            return 0;
        }
        return 1;
    }
    catch (const std::exception& exception)
    {
        writeError(error, errorSize, exception.what());
        return 0;
    }
    catch (...)
    {
        writeError(error, errorSize, "unknown exception in PhysX runtime step");
        return 0;
    }
}

OGSR_PHYSX_RUNTIME_API int OgsrPhysXRuntimeGetState(OgsrPhysXRuntimeHandle handle,
                                                    OgsrPhysXRuntimeState* state)
{
    if (!state)
        return 0;
    std::memset(state, 0, sizeof(*state));
    try
    {
        auto* context = asContext(handle);
        if (!context)
            return 0;
        const physics_physx::RuntimeTestBodyState source = context->testBodyState();
        state->position[0] = source.pose.translation.x;
        state->position[1] = source.pose.translation.y;
        state->position[2] = source.pose.translation.z;
        state->rotation[0] = source.pose.rotation.x;
        state->rotation[1] = source.pose.rotation.y;
        state->rotation[2] = source.pose.rotation.z;
        state->rotation[3] = source.pose.rotation.w;
        state->stepCount = source.stepCount;
        state->lastSubsteps = source.lastSubsteps;
        state->contactCount = source.contactCount;
        state->staticTriangles = context->staticTriangleCount();
        state->dynamicBodies = context->dynamicBodyCount();
        state->hasContact = source.hasContact ? 1u : 0u;
        return 1;
    }
    catch (...)
    {
        return 0;
    }
}
}
