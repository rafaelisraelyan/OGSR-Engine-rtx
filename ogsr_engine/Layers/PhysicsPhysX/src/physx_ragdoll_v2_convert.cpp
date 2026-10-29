#include "physx_ragdoll_v2_convert.h"

#include "physics_portable/row_matrix.h"

#include <cmath>
#include <cstring>
#include <string>
#include <utility>

namespace physics_physx::v2
{
namespace
{
using physics_portable::BodyId;
using physics_portable::Transform;
using physics_portable::Vec3;

bool finite(const float* values, std::size_t count)
{
    for (std::size_t index = 0; index < count; ++index)
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

std::string boundedName(const char* value, std::size_t capacity)
{
    if (!value)
        return {};
    std::size_t length = 0;
    while (length < capacity && value[length] != 0)
        ++length;
    return std::string(value, length);
}

template <typename Enum>
bool validEnum(std::uint8_t value, std::uint8_t count)
{
    return value < count;
}
} // namespace

void storeRow(const Transform& value, float* out)
{
    const physics_portable::RowMatrix4 row = physics_portable::rowMatrixFromTransform(value);
    std::memcpy(out, row.m, sizeof(row.m));
}

bool convertDefinition(const OgsrPhysXRagdollDefinition& source,
                       physics_portable::RagdollDescription& description,
                       physics_portable::RagdollInitialState& initial, std::string* error)
{
    auto fail = [error](const char* message) {
        if (error)
            *error = message;
        return false;
    };

    if (source.boneCount > OGSR_PHYSX_RAGDOLL_MAX_BONES || source.bodyCount > OGSR_PHYSX_RAGDOLL_MAX_BODIES ||
        source.jointCount > OGSR_PHYSX_RAGDOLL_MAX_JOINTS || source.shapeCount > OGSR_PHYSX_RAGDOLL_MAX_SHAPES ||
        source.materialCount > OGSR_PHYSX_RAGDOLL_MAX_MATERIALS)
        return fail("ragdoll definition exceeds a C ABI cap");
    if (source.boneCount && !source.bones)
        return fail("ragdoll bones pointer is null");
    if (source.bodyCount && !source.bodies)
        return fail("ragdoll bodies pointer is null");
    if (source.jointCount && !source.joints)
        return fail("ragdoll joints pointer is null");
    if (source.shapeCount && !source.shapes)
        return fail("ragdoll shapes pointer is null");
    if (source.materialCount && !source.materials)
        return fail("ragdoll materials pointer is null");
    if (source.bodyCount && (!source.initialBodyWorld || !source.initialLinearVelocity || !source.initialAngularVelocity))
        return fail("ragdoll initial state pointer is null");
    if (source.rootBody >= source.bodyCount)
        return fail("ragdoll root body is out of range");
    if (!finite(source.entityWorld, 16) || !finite(source.rootToEntity, 16))
        return fail("ragdoll entity transform is not finite");

    description = physics_portable::RagdollDescription{};
    description.schemaVersion = 1;
    description.rootBody = source.rootBody;
    description.bones.reserve(source.boneCount);
    for (std::uint32_t index = 0; index < source.boneCount; ++index)
    {
        const OgsrPhysXRagdollBone& src = source.bones[index];
        if (!validEnum<physics_portable::BoneDisposition>(src.disposition, 4))
            return fail("invalid bone disposition");
        if (!finite(src.bindLocal, 16))
            return fail("bone bind transform is not finite");
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
    for (std::uint32_t index = 0; index < source.materialCount; ++index)
    {
        const OgsrPhysXRagdollMaterial& src = source.materials[index];
        if (!std::isfinite(src.friction) || !std::isfinite(src.restitution) || !std::isfinite(src.spring) ||
            !std::isfinite(src.damping))
            return fail("material contains a non-finite value");
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
    for (std::uint32_t index = 0; index < source.bodyCount; ++index)
    {
        const OgsrPhysXRagdollBody& src = source.bodies[index];
        if (!validEnum<physics_portable::BodyRole>(src.role, 3))
            return fail("invalid body role");
        if (src.firstShape > source.shapeCount || src.firstShape + src.shapeCount > source.shapeCount)
            return fail("body shape range is out of bounds");
        if (!std::isfinite(src.mass) || !finite(src.centerOfMass, 3) || !finite(src.inertia, 6) ||
            !finite(src.bindPose, 16))
            return fail("body contains a non-finite value");

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
        for (std::uint32_t shapeIndex = 0; shapeIndex < src.shapeCount; ++shapeIndex)
        {
            const OgsrPhysXRagdollShape& shapeSrc = source.shapes[src.firstShape + shapeIndex];
            if (!validEnum<physics_portable::ShapeType>(shapeSrc.type, 3))
                return fail("invalid shape type");
            if (!finite(shapeSrc.local, 16) || !finite(shapeSrc.halfExtents, 3) || !std::isfinite(shapeSrc.radius) ||
                !std::isfinite(shapeSrc.height) || !std::isfinite(shapeSrc.margin))
                return fail("shape contains a non-finite value");
            physics_portable::RagdollShapeDesc shape;
            shape.type = static_cast<physics_portable::ShapeType>(shapeSrc.type);
            shape.local = transformFromRow(shapeSrc.local);
            shape.halfExtents = Vec3{shapeSrc.halfExtents[0], shapeSrc.halfExtents[1], shapeSrc.halfExtents[2]};
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
    for (std::uint32_t index = 0; index < source.jointCount; ++index)
    {
        const OgsrPhysXRagdollJoint& src = source.joints[index];
        if (!validEnum<physics_portable::JointKind>(src.kind, 6) ||
            !validEnum<physics_portable::SwingLimitMode>(src.swingLimitMode, 3) || src.axisCount > 3)
            return fail("invalid joint enum or axis count");
        if (!finite(src.parentFrame, 16) || !finite(src.childFrame, 16) || !std::isfinite(src.erp) ||
            !std::isfinite(src.cfm) || !std::isfinite(src.friction) || !std::isfinite(src.maxForce) ||
            !std::isfinite(src.targetVelocity) || !std::isfinite(src.breakForce) || !std::isfinite(src.breakTorque))
            return fail("joint contains a non-finite value");

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
        for (std::uint32_t axis = 0; axis < src.axisCount; ++axis)
        {
            const OgsrPhysXRagdollAxis& axisSrc = src.axes[axis];
            if (!validEnum<physics_portable::AxisSpace>(axisSrc.space, 3) || !finite(axisSrc.axis, 3) ||
                !std::isfinite(axisSrc.lower) || !std::isfinite(axisSrc.upper) ||
                !std::isfinite(axisSrc.sourceLower) || !std::isfinite(axisSrc.sourceUpper) ||
                !std::isfinite(axisSrc.spring) || !std::isfinite(axisSrc.damping) || !std::isfinite(axisSrc.zero))
                return fail("joint axis contains an invalid value");
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
    for (std::uint32_t index = 0; index < source.bodyCount; ++index)
    {
        const float* world = source.initialBodyWorld + index * 16;
        if (!finite(world, 16))
            return fail("initial body transform is not finite");
        initial.bodyWorld[index] = transformFromRow(world);
        const float* linear = source.initialLinearVelocity + index * 3;
        const float* angular = source.initialAngularVelocity + index * 3;
        if (!finite(linear, 3) || !finite(angular, 3))
            return fail("initial body velocity is not finite");
        initial.linearVelocity[index] = Vec3{linear[0], linear[1], linear[2]};
        initial.angularVelocity[index] = Vec3{angular[0], angular[1], angular[2]};
        if (source.initialEnabled)
            initial.enabled[index] = source.initialEnabled[index] ? 1 : 0;
    }
    return true;
}

physics_portable::RagdollSimulationConfig makeSimulationConfig(const OgsrPhysX2SceneConfig& sceneConfig,
                                                               const OgsrPhysX2RagdollInstanceConfig& instanceConfig)
{
    physics_portable::RagdollSimulationConfig config;
    config.fixedTimestep = sceneConfig.fixedTimestep;
    config.gravity = Vec3{0.0f, sceneConfig.gravityY, 0.0f};
    config.maxSubsteps = static_cast<int>(sceneConfig.maxSubsteps);
    config.solverPositionIterations = sceneConfig.solverPositionIterations;
    config.solverVelocityIterations = sceneConfig.solverVelocityIterations;
    config.contactOffset = sceneConfig.contactOffset;
    config.restOffset = sceneConfig.restOffset;
    config.enableStabilization = sceneConfig.enableStabilization != 0;
    config.promoteStaticRoot = instanceConfig.promoteStaticRoot != 0;
    config.staticRootDensity = instanceConfig.staticRootDensity;
    config.linearDamping = instanceConfig.linearDamping;
    config.angularDamping = instanceConfig.angularDamping;
    config.disableSelfCollision = instanceConfig.disableSelfCollision != 0;
    config.collectDiagnostics = sceneConfig.collectContacts != 0;
    return config;
}

} // namespace physics_physx::v2
