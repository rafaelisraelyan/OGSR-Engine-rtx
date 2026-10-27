#include "physics_physx/physx_ragdoll_c.h"

#include "physics_physx/physx_ragdoll_world.h"
#include "physics_portable/row_matrix.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{
using physics_portable::BodyId;
using physics_portable::BoneId;
using physics_portable::JointId;
using physics_portable::Transform;
using physics_portable::Vec3;

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

struct RagdollRuntime
{
    explicit RagdollRuntime(const physics_portable::RagdollSimulationConfig& simulationConfig)
        : config(simulationConfig), world(simulationConfig)
    {
    }

    physics_portable::RagdollSimulationConfig config;
    physics_physx::PhysXRagdollWorld world;
    std::vector<float> staticVertices;
    std::vector<uint32_t> staticIndices;
    std::uint32_t bodyCount = 0;
    std::uint32_t jointCount = 0;
};

RagdollRuntime* asRuntime(OgsrPhysXRuntimeHandle handle)
{
    return static_cast<RagdollRuntime*>(handle);
}

bool finite(const float* values, size_t count)
{
    for (size_t index = 0; index < count; ++index)
    {
        if (!std::isfinite(values[index]))
            return false;
    }
    return true;
}

bool finite(const Vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Transform transformFromRow(const float* values)
{
    physics_portable::RowMatrix4 row{};
    std::memcpy(row.m, values, sizeof(row.m));
    return physics_portable::transformFromRowMatrix(row);
}

void rowFromTransform(const Transform& value, float* out)
{
    const physics_portable::RowMatrix4 row = physics_portable::rowMatrixFromTransform(value);
    std::memcpy(out, row.m, sizeof(row.m));
}

std::string boundedName(const char* value, size_t capacity)
{
    if (!value)
        return {};
    size_t length = 0;
    while (length < capacity && value[length] != 0)
        ++length;
    return std::string(value, length);
}

template <typename Enum>
bool validEnum(std::uint8_t value, std::uint8_t count)
{
    return value < count;
}

bool convertDefinition(const OgsrPhysXRagdollDefinition& source,
                       physics_portable::RagdollDescription& description,
                       physics_portable::RagdollInitialState& initial,
                       std::string& error)
{
    if (source.boneCount > OGSR_PHYSX_RAGDOLL_MAX_BONES || source.bodyCount > OGSR_PHYSX_RAGDOLL_MAX_BODIES ||
        source.jointCount > OGSR_PHYSX_RAGDOLL_MAX_JOINTS || source.shapeCount > OGSR_PHYSX_RAGDOLL_MAX_SHAPES ||
        source.materialCount > OGSR_PHYSX_RAGDOLL_MAX_MATERIALS)
    {
        error = "ragdoll definition exceeds a C ABI cap";
        return false;
    }
    if (source.boneCount && !source.bones)
    {
        error = "ragdoll bones pointer is null";
        return false;
    }
    if (source.bodyCount && !source.bodies)
    {
        error = "ragdoll bodies pointer is null";
        return false;
    }
    if (source.jointCount && !source.joints)
    {
        error = "ragdoll joints pointer is null";
        return false;
    }
    if (source.shapeCount && !source.shapes)
    {
        error = "ragdoll shapes pointer is null";
        return false;
    }
    if (source.materialCount && !source.materials)
    {
        error = "ragdoll materials pointer is null";
        return false;
    }
    if (source.bodyCount && (!source.initialBodyWorld || !source.initialLinearVelocity || !source.initialAngularVelocity))
    {
        error = "ragdoll initial state pointer is null";
        return false;
    }
    if (source.rootBody >= source.bodyCount)
    {
        error = "ragdoll root body is out of range";
        return false;
    }
    if (!finite(source.entityWorld, 16) || !finite(source.rootToEntity, 16))
    {
        error = "ragdoll entity transform is not finite";
        return false;
    }

    description = physics_portable::RagdollDescription{};
    description.schemaVersion = 1;
    description.rootBody = source.rootBody;
    description.bones.reserve(source.boneCount);
    for (uint32_t index = 0; index < source.boneCount; ++index)
    {
        const OgsrPhysXRagdollBone& src = source.bones[index];
        if (!validEnum<physics_portable::BoneDisposition>(src.disposition, 4))
        {
            error = "invalid bone disposition";
            return false;
        }
        if (!finite(src.bindLocal, 16))
        {
            error = "bone bind transform is not finite";
            return false;
        }
        physics_portable::RagdollBoneDesc bone;
        bone.id = src.id;
        bone.parent = src.parent;
        bone.name = boundedName(src.name, OGSR_PHYSX_RAGDOLL_MAX_NAME);
        bone.bindLocal = transformFromRow(src.bindLocal);
        bone.visible = src.visible != 0;
        bone.disposition = static_cast<physics_portable::BoneDisposition>(src.disposition);
        bone.body = src.body;
        bone.sourceShapeFlags = src.flags;
        description.bones.push_back(std::move(bone));
    }

    description.materials.reserve(source.materialCount);
    for (uint32_t index = 0; index < source.materialCount; ++index)
    {
        const OgsrPhysXRagdollMaterial& src = source.materials[index];
        if (!std::isfinite(src.friction) || !std::isfinite(src.restitution) || !std::isfinite(src.spring) ||
            !std::isfinite(src.damping))
        {
            error = "material contains a non-finite value";
            return false;
        }
        physics_portable::RagdollMaterialDesc material;
        material.id = src.id;
        material.name = boundedName(src.name, sizeof(src.name));
        material.friction = src.friction;
        material.restitution = src.restitution;
        material.spring = src.spring;
        material.damping = src.damping;
        description.materials.push_back(std::move(material));
    }

    description.bodies.reserve(source.bodyCount);
    for (uint32_t index = 0; index < source.bodyCount; ++index)
    {
        const OgsrPhysXRagdollBody& src = source.bodies[index];
        if (!validEnum<physics_portable::BodyRole>(src.role, 3))
        {
            error = "invalid body role";
            return false;
        }
        if (src.firstShape > source.shapeCount || src.firstShape + src.shapeCount > source.shapeCount)
        {
            error = "body shape range is out of bounds";
            return false;
        }
        if (!std::isfinite(src.mass) || !finite(src.centerOfMass, 3) || !finite(src.inertia, 6) ||
            !finite(src.bindPose, 16))
        {
            error = "body contains a non-finite value";
            return false;
        }
        physics_portable::RagdollBodyDesc body;
        body.id = src.id;
        body.boneId = src.boneId;
        body.parentBody = src.parentBody;
        body.role = static_cast<physics_portable::BodyRole>(src.role);
        body.isFixed = src.isFixed != 0;
        body.mass = src.mass;
        body.centerOfMass = Vec3{src.centerOfMass[0], src.centerOfMass[1], src.centerOfMass[2]};
        std::memcpy(body.inertia, src.inertia, sizeof(body.inertia));
        body.bindPose = transformFromRow(src.bindPose);
        body.collisionGroup = src.collisionGroup;
        body.shapes.reserve(src.shapeCount);
        for (uint32_t shapeIndex = 0; shapeIndex < src.shapeCount; ++shapeIndex)
        {
            const OgsrPhysXRagdollShape& shapeSrc = source.shapes[src.firstShape + shapeIndex];
            if (!validEnum<physics_portable::ShapeType>(shapeSrc.type, 3))
            {
                error = "invalid shape type";
                return false;
            }
            if (!finite(shapeSrc.local, 16) || !finite(shapeSrc.halfExtents, 3) || !std::isfinite(shapeSrc.radius) ||
                !std::isfinite(shapeSrc.height) || !std::isfinite(shapeSrc.margin))
            {
                error = "shape contains a non-finite value";
                return false;
            }
            physics_portable::RagdollShapeDesc shape;
            shape.type = static_cast<physics_portable::ShapeType>(shapeSrc.type);
            shape.local = transformFromRow(shapeSrc.local);
            shape.halfExtents =
                Vec3{shapeSrc.halfExtents[0], shapeSrc.halfExtents[1], shapeSrc.halfExtents[2]};
            shape.radius = shapeSrc.radius;
            shape.height = shapeSrc.height;
            shape.margin = shapeSrc.margin;
            shape.materialId = shapeSrc.materialId;
            shape.sourceBoneId = shapeSrc.sourceBoneId;
            shape.flags = shapeSrc.flags;
            body.shapes.push_back(shape);
        }
        description.bodies.push_back(std::move(body));
    }

    description.joints.reserve(source.jointCount);
    for (uint32_t index = 0; index < source.jointCount; ++index)
    {
        const OgsrPhysXRagdollJoint& src = source.joints[index];
        if (!validEnum<physics_portable::JointKind>(src.kind, 6) ||
            !validEnum<physics_portable::SwingLimitMode>(src.swingLimitMode, 3) || src.axisCount > 3)
        {
            error = "invalid joint enum or axis count";
            return false;
        }
        if (!finite(src.parentFrame, 16) || !finite(src.childFrame, 16) || !std::isfinite(src.erp) ||
            !std::isfinite(src.cfm) || !std::isfinite(src.friction) || !std::isfinite(src.maxForce) ||
            !std::isfinite(src.targetVelocity) || !std::isfinite(src.breakForce) || !std::isfinite(src.breakTorque))
        {
            error = "joint contains a non-finite value";
            return false;
        }
        physics_portable::RagdollJointDesc joint;
        joint.id = src.id;
        joint.boneId = src.boneId;
        joint.parentBody = src.parentBody;
        joint.childBody = src.childBody;
        joint.kind = static_cast<physics_portable::JointKind>(src.kind);
        joint.parentFrame = transformFromRow(src.parentFrame);
        joint.childFrame = transformFromRow(src.childFrame);
        joint.axisCount = src.axisCount;
        joint.erp = src.erp;
        joint.cfm = src.cfm;
        joint.friction = src.friction;
        joint.maxForce = src.maxForce;
        joint.targetVelocity = src.targetVelocity;
        joint.breakable = src.breakable != 0;
        joint.breakForce = src.breakForce;
        joint.breakTorque = src.breakTorque;
        joint.swingLimitMode = static_cast<physics_portable::SwingLimitMode>(src.swingLimitMode);
        joint.sourceFlags = src.flags;
        for (uint32_t axis = 0; axis < src.axisCount; ++axis)
        {
            const OgsrPhysXRagdollAxis& axisSrc = src.axes[axis];
            if (!validEnum<physics_portable::AxisSpace>(axisSrc.space, 3) || !finite(axisSrc.axis, 3) ||
                !std::isfinite(axisSrc.lower) || !std::isfinite(axisSrc.upper) ||
                !std::isfinite(axisSrc.sourceLower) || !std::isfinite(axisSrc.sourceUpper) ||
                !std::isfinite(axisSrc.spring) || !std::isfinite(axisSrc.damping) || !std::isfinite(axisSrc.zero))
            {
                error = "joint axis contains an invalid value";
                return false;
            }
            physics_portable::RagdollAxisLimit limit;
            limit.space = static_cast<physics_portable::AxisSpace>(axisSrc.space);
            limit.axis = Vec3{axisSrc.axis[0], axisSrc.axis[1], axisSrc.axis[2]};
            limit.lower = axisSrc.lower;
            limit.upper = axisSrc.upper;
            limit.sourceLower = axisSrc.sourceLower;
            limit.sourceUpper = axisSrc.sourceUpper;
            limit.spring = axisSrc.spring;
            limit.damping = axisSrc.damping;
            limit.zero = axisSrc.zero;
            joint.axes[axis] = limit;
        }
        description.joints.push_back(std::move(joint));
    }

    initial = physics_portable::RagdollInitialState{};
    initial.entityWorld = transformFromRow(source.entityWorld);
    initial.rootToEntity = transformFromRow(source.rootToEntity);
    initial.bodyWorld.resize(source.bodyCount);
    initial.linearVelocity.resize(source.bodyCount);
    initial.angularVelocity.resize(source.bodyCount);
    initial.enabled.resize(source.bodyCount, 1);
    for (uint32_t index = 0; index < source.bodyCount; ++index)
    {
        const float* world = source.initialBodyWorld + index * 16;
        if (!finite(world, 16))
        {
            error = "initial body transform is not finite";
            return false;
        }
        initial.bodyWorld[index] = transformFromRow(world);
        const float* linear = source.initialLinearVelocity + index * 3;
        const float* angular = source.initialAngularVelocity + index * 3;
        if (!finite(linear, 3) || !finite(angular, 3))
        {
            error = "initial body velocity is not finite";
            return false;
        }
        initial.linearVelocity[index] = Vec3{linear[0], linear[1], linear[2]};
        initial.angularVelocity[index] = Vec3{angular[0], angular[1], angular[2]};
        if (source.initialEnabled)
            initial.enabled[index] = source.initialEnabled[index] ? 1 : 0;
    }
    return true;
}

physics_portable::RagdollSimulationConfig makeConfig(const OgsrPhysXRagdollConfig& source)
{
    physics_portable::RagdollSimulationConfig config;
    config.fixedTimestep = source.fixedTimestep;
    config.gravity = Vec3{0.0f, source.gravityY, 0.0f};
    config.maxSubsteps = static_cast<int>(source.maxSubsteps);
    config.solverPositionIterations = source.solverPositionIterations;
    config.solverVelocityIterations = source.solverVelocityIterations;
    config.contactOffset = source.contactOffset;
    config.restOffset = source.restOffset;
    config.enableStabilization = source.enableStabilization != 0;
    config.promoteStaticRoot = source.promoteStaticRoot != 0;
    config.staticRootDensity = source.staticRootDensity;
    config.linearDamping = source.linearDamping;
    config.angularDamping = source.angularDamping;
    config.disableSelfCollision = source.disableSelfCollision != 0;
    config.collectDiagnostics = source.collectDiagnostics != 0;
    return config;
}
} // namespace

extern "C"
{
OGSR_PHYSX_RUNTIME_API OgsrPhysXRuntimeHandle OgsrPhysXRagdollRuntimeCreate(const OgsrPhysXRagdollConfig* config,
                                                                           char* error,
                                                                           size_t errorSize)
{
    clearError(error, errorSize);
    try
    {
        if (!config)
        {
            writeError(error, errorSize, "ragdoll runtime config is null");
            return nullptr;
        }
        if (!(config->fixedTimestep > 0.0f) || !std::isfinite(config->fixedTimestep) ||
            config->maxSubsteps == 0 || config->solverPositionIterations == 0 ||
            config->solverVelocityIterations == 0 || !std::isfinite(config->gravityY) ||
            !std::isfinite(config->contactOffset) || !std::isfinite(config->restOffset) ||
            config->contactOffset <= 0.0f || config->restOffset < 0.0f ||
            config->restOffset >= config->contactOffset ||
            !(config->staticRootDensity > 0.0f) || !std::isfinite(config->staticRootDensity) ||
            !std::isfinite(config->linearDamping) || config->linearDamping < 0.0f ||
            !std::isfinite(config->angularDamping) || config->angularDamping < 0.0f)
        {
            writeError(error, errorSize, "ragdoll runtime config is invalid");
            return nullptr;
        }
        return new RagdollRuntime(makeConfig(*config));
    }
    catch (const std::exception& exception)
    {
        writeError(error, errorSize, exception.what());
        return nullptr;
    }
    catch (...)
    {
        writeError(error, errorSize, "unknown exception in ragdoll runtime create");
        return nullptr;
    }
}

OGSR_PHYSX_RUNTIME_API void OgsrPhysXRagdollRuntimeDestroy(OgsrPhysXRuntimeHandle handle)
{
    try
    {
        auto* runtime = asRuntime(handle);
        if (!runtime)
            return;
        runtime->world.destroy();
        delete runtime;
    }
    catch (...)
    {
    }
}

OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollSetStaticMesh(OgsrPhysXRuntimeHandle handle,
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
        auto* runtime = asRuntime(handle);
        if (!runtime)
        {
            writeError(error, errorSize, "ragdoll runtime handle is null");
            return 0;
        }
        if (!vertices || !indices || vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0)
        {
            writeError(error, errorSize, "invalid static mesh argument");
            return 0;
        }
        if (indexCount / 3 > 200000)
        {
            writeError(error, errorSize, "static mesh exceeds the triangle cap");
            return 0;
        }
        if (!finite(vertices, static_cast<size_t>(vertexCount) * 3))
        {
            writeError(error, errorSize, "static mesh contains a non-finite vertex");
            return 0;
        }
        std::vector<Vec3> converted;
        converted.reserve(vertexCount);
        for (uint32_t index = 0; index < vertexCount; ++index)
            converted.push_back(Vec3{vertices[index * 3 + 0], vertices[index * 3 + 1], vertices[index * 3 + 2]});
        for (uint32_t index = 0; index < indexCount; ++index)
        {
            if (indices[index] >= vertexCount)
            {
                writeError(error, errorSize, "static mesh index is out of range");
                return 0;
            }
        }
        runtime->staticVertices.resize(static_cast<size_t>(vertexCount) * 3);
        std::memcpy(runtime->staticVertices.data(), vertices, static_cast<size_t>(vertexCount) * 3 * sizeof(float));
        runtime->staticIndices.assign(indices, indices + indexCount);
        std::string message;
        if (!runtime->world.setStaticMesh(converted, runtime->staticIndices, &message))
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
        writeError(error, errorSize, "unknown exception in ragdoll static mesh upload");
        return 0;
    }
}

OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollCreate(OgsrPhysXRuntimeHandle handle,
                                                 const OgsrPhysXRagdollDefinition* definition,
                                                 char* error,
                                                 size_t errorSize)
{
    clearError(error, errorSize);
    try
    {
        auto* runtime = asRuntime(handle);
        if (!runtime || !definition)
        {
            writeError(error, errorSize, "ragdoll runtime handle or definition is null");
            return 0;
        }
        std::string message;
        if (!finite(definition->entityWorld, 16) || !finite(definition->rootToEntity, 16))
        {
            writeError(error, errorSize, "entity transform is not finite");
            return 0;
        }
        physics_portable::RagdollDescription description;
        physics_portable::RagdollInitialState initial;
        if (!convertDefinition(*definition, description, initial, message))
        {
            writeError(error, errorSize, message);
            return 0;
        }
        runtime->world.destroy();
        runtime->bodyCount = 0;
        runtime->jointCount = 0;
        if (!runtime->world.create(description, initial, &message))
        {
            runtime->world.destroy();
            writeError(error, errorSize, message);
            return 0;
        }
        runtime->bodyCount = static_cast<uint32_t>(runtime->world.bodyCount());
        runtime->jointCount = static_cast<uint32_t>(runtime->world.jointCount());
        return 1;
    }
    catch (const std::exception& exception)
    {
        writeError(error, errorSize, exception.what());
        return 0;
    }
    catch (...)
    {
        writeError(error, errorSize, "unknown exception in ragdoll create");
        return 0;
    }
}

OGSR_PHYSX_RUNTIME_API void OgsrPhysXRagdollDestroy(OgsrPhysXRuntimeHandle handle)
{
    try
    {
        auto* runtime = asRuntime(handle);
        if (!runtime)
            return;
        runtime->world.destroy();
        runtime->bodyCount = 0;
        runtime->jointCount = 0;
    }
    catch (...)
    {
    }
}

OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollStep(OgsrPhysXRuntimeHandle handle,
                                               float frameDeltaSeconds,
                                               char* error,
                                               size_t errorSize)
{
    clearError(error, errorSize);
    try
    {
        auto* runtime = asRuntime(handle);
        if (!runtime || !runtime->world.isCreated())
        {
            writeError(error, errorSize, "ragdoll is not created");
            return 0;
        }
        std::string message;
        if (!runtime->world.step(frameDeltaSeconds, &message))
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
        writeError(error, errorSize, "unknown exception in ragdoll step");
        return 0;
    }
}

OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollGetPose(OgsrPhysXRuntimeHandle handle,
                                                  OgsrPhysXRagdollPose* pose,
                                                  float* bodyWorld,
                                                  uint32_t maxBodies)
{
    if (!pose)
        return 0;
    std::memset(pose, 0, sizeof(*pose));
    try
    {
        auto* runtime = asRuntime(handle);
        if (!runtime)
            return 0;
        const physics_portable::RagdollPose current = runtime->world.pose();
        pose->bodyCount = static_cast<uint32_t>(current.bodyWorld.size());
        pose->stepIndexLow = static_cast<uint32_t>(current.stepIndex & 0xffffffffull);
        pose->stepIndexHigh = static_cast<uint32_t>(current.stepIndex >> 32);
        rowFromTransform(current.entityWorld, pose->entityWorld);
        if (pose->bodyCount == 0)
            return 0;
        if (!bodyWorld || maxBodies < pose->bodyCount)
            return 0;
        for (uint32_t index = 0; index < pose->bodyCount; ++index)
            rowFromTransform(current.bodyWorld[index], bodyWorld + static_cast<size_t>(index) * 16);
        return 1;
    }
    catch (...)
    {
        return 0;
    }
}

OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollApplyImpulse(OgsrPhysXRuntimeHandle handle,
                                                       uint16_t body,
                                                       const float worldPosition[3],
                                                       const float impulse[3],
                                                       char* error,
                                                       size_t errorSize)
{
    clearError(error, errorSize);
    try
    {
        auto* runtime = asRuntime(handle);
        if (!runtime || !worldPosition || !impulse)
        {
            writeError(error, errorSize, "impulse argument is null");
            return 0;
        }
        if (!finite(worldPosition, 3) || !finite(impulse, 3))
        {
            writeError(error, errorSize, "impulse contains a non-finite value");
            return 0;
        }
        std::string message;
        if (!runtime->world.applyImpulse(body,
                                         Vec3{worldPosition[0], worldPosition[1], worldPosition[2]},
                                         Vec3{impulse[0], impulse[1], impulse[2]}, &message))
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
        writeError(error, errorSize, "unknown exception in ragdoll impulse");
        return 0;
    }
}

OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollApplyCentralImpulse(OgsrPhysXRuntimeHandle handle,
                                                              uint16_t body,
                                                              const float impulse[3],
                                                              char* error,
                                                              size_t errorSize)
{
    clearError(error, errorSize);
    try
    {
        auto* runtime = asRuntime(handle);
        if (!runtime || !impulse)
        {
            writeError(error, errorSize, "central impulse argument is null");
            return 0;
        }
        if (!finite(impulse, 3))
        {
            writeError(error, errorSize, "central impulse contains a non-finite value");
            return 0;
        }
        std::string message;
        if (!runtime->world.applyImpulseAtCom(
                body, Vec3{impulse[0], impulse[1], impulse[2]}, &message))
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
        writeError(error, errorSize, "unknown exception in central ragdoll impulse");
        return 0;
    }
}

OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollGetBodyMass(OgsrPhysXRuntimeHandle handle,
                                                      uint16_t body,
                                                      OgsrPhysXRagdollBodyMass* mass,
                                                      char* error,
                                                      size_t errorSize)
{
    clearError(error, errorSize);
    if (!mass)
    {
        writeError(error, errorSize, "body mass output is null");
        return 0;
    }
    std::memset(mass, 0, sizeof(*mass));
    try
    {
        auto* runtime = asRuntime(handle);
        if (!runtime)
        {
            writeError(error, errorSize, "ragdoll runtime is null");
            return 0;
        }
        physics_physx::BodyMassProperties source;
        std::string message;
        if (!runtime->world.massProperties(body, source, &message))
        {
            writeError(error, errorSize, message);
            return 0;
        }
        mass->body = source.body;
        mass->mass = source.mass;
        rowFromTransform(source.actorWorld, mass->actorWorld);
        rowFromTransform(source.massWorld, mass->massWorld);
        mass->centerOfMassLocal[0] = source.centerOfMassLocal.x;
        mass->centerOfMassLocal[1] = source.centerOfMassLocal.y;
        mass->centerOfMassLocal[2] = source.centerOfMassLocal.z;
        mass->worldCenterOfMass[0] = source.worldCenterOfMass.x;
        mass->worldCenterOfMass[1] = source.worldCenterOfMass.y;
        mass->worldCenterOfMass[2] = source.worldCenterOfMass.z;
        mass->inertiaMassSpace[0] = source.inertiaMassSpace.x;
        mass->inertiaMassSpace[1] = source.inertiaMassSpace.y;
        mass->inertiaMassSpace[2] = source.inertiaMassSpace.z;
        return 1;
    }
    catch (const std::exception& exception)
    {
        writeError(error, errorSize, exception.what());
        return 0;
    }
    catch (...)
    {
        writeError(error, errorSize, "unknown exception in body mass query");
        return 0;
    }
}

OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollGetInfo(OgsrPhysXRuntimeHandle handle, OgsrPhysXRagdollInfo* info)
{
    if (!info)
        return 0;
    std::memset(info, 0, sizeof(*info));
    try
    {
        auto* runtime = asRuntime(handle);
        if (!runtime)
            return 0;
        info->bodyCount = runtime->bodyCount;
        info->jointCount = runtime->jointCount;
        info->staticTriangles = runtime->world.staticTriangleCount();
        const physics_physx::RagdollDiagnostics diagnostics =
            runtime->world.diagnostics(physics_portable::Vec3{0.0f, 0.0f, 0.0f});
        info->totalMass = diagnostics.totalMass;
        info->rootMass = diagnostics.rootMass;
        return 1;
    }
    catch (...)
    {
        return 0;
    }
}

OGSR_PHYSX_RUNTIME_API int OgsrPhysXRagdollGetDiagnostics(OgsrPhysXRuntimeHandle handle,
                                                          float referenceHeight,
                                                          OgsrPhysXRagdollDiagnostics* diagnostics)
{
    if (!diagnostics)
        return 0;
    std::memset(diagnostics, 0, sizeof(*diagnostics));
    try
    {
        auto* runtime = asRuntime(handle);
        if (!runtime)
            return 0;
        const physics_physx::RagdollDiagnostics source =
            runtime->world.diagnostics(physics_portable::Vec3{0.0f, referenceHeight, 0.0f});
        diagnostics->kineticEnergy = source.kineticEnergy;
        diagnostics->potentialEnergy = source.potentialEnergy;
        diagnostics->totalEnergy = source.totalEnergy;
        diagnostics->maxAngularSpeed = source.maxAngularSpeed;
        diagnostics->maxJointAnchorError = source.maxJointAnchorError;
        diagnostics->maxJointLimitError = source.maxJointLimitError;
        diagnostics->maxPenetration = source.maxPenetration;
        diagnostics->totalContactImpulse = source.totalContactImpulse;
        diagnostics->totalMass = source.totalMass;
        diagnostics->rootMass = source.rootMass;
        diagnostics->contactCount = source.contactCount;
        diagnostics->contactFound = source.contactFound;
        diagnostics->contactPersist = source.contactPersist;
        diagnostics->contactLost = source.contactLost;
        diagnostics->activeContactPairs = source.activeContactPairs;
        diagnostics->awakeBodies = source.awakeBodies;
        diagnostics->dynamicBodies = source.dynamicBodies;
        diagnostics->externalImpulses = source.externalImpulses;
        diagnostics->stepIndex = source.stepIndex;
        return 1;
    }
    catch (...)
    {
        return 0;
    }
}
}
