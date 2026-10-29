#include "v2_fixture_builder.h"

#include "physics_portable/row_matrix.h"

#include <cstdio>
#include <cstring>

namespace physics_physx_tests
{
namespace
{
void storeRow(const physics_portable::Transform& transform, float* out)
{
    const physics_portable::RowMatrix4 row = physics_portable::rowMatrixFromTransform(transform);
    std::memcpy(out, row.m, sizeof(row.m));
}
} // namespace

void V2Fixture::build(const physics_portable::RagdollDescription& description,
                      const physics_portable::RagdollInitialState& initial,
                      const physics_portable::Vec3& offset)
{
    bones.clear();
    bodies.clear();
    joints.clear();
    shapes.clear();
    materials.clear();
    bodyWorld.clear();
    linearVelocity.clear();
    angularVelocity.clear();
    enabled.clear();

    bones.reserve(description.bones.size());
    for (const physics_portable::RagdollBoneDesc& source : description.bones)
    {
        OgsrPhysXRagdollBone bone{};
        bone.id = source.id;
        bone.parent = source.parent;
        bone.visible = source.visible ? 1 : 0;
        bone.disposition = static_cast<std::uint8_t>(source.disposition);
        bone.body = source.body;
        bone.flags = source.sourceShapeFlags;
        storeRow(source.bindLocal, bone.bindLocal);
        std::snprintf(bone.name, sizeof(bone.name), "%s", source.name.c_str());
        bones.push_back(bone);
    }

    materials.reserve(description.materials.size());
    for (const physics_portable::RagdollMaterialDesc& source : description.materials)
    {
        OgsrPhysXRagdollMaterial material{};
        material.id = source.id;
        material.friction = source.friction;
        material.restitution = source.restitution;
        material.spring = source.spring;
        material.damping = source.damping;
        std::snprintf(material.name, sizeof(material.name), "%s", source.name.c_str());
        materials.push_back(material);
    }

    bodies.reserve(description.bodies.size());
    for (const physics_portable::RagdollBodyDesc& source : description.bodies)
    {
        OgsrPhysXRagdollBody body{};
        body.id = source.id;
        body.boneId = source.boneId;
        body.parentBody = source.parentBody;
        body.role = static_cast<std::uint8_t>(source.role);
        body.isFixed = source.isFixed ? 1 : 0;
        body.mass = source.mass;
        body.centerOfMass[0] = source.centerOfMass.x;
        body.centerOfMass[1] = source.centerOfMass.y;
        body.centerOfMass[2] = source.centerOfMass.z;
        std::memcpy(body.inertia, source.inertia, sizeof(body.inertia));
        storeRow(source.bindPose, body.bindPose);
        body.firstShape = static_cast<std::uint32_t>(shapes.size());
        body.shapeCount = static_cast<std::uint32_t>(source.shapes.size());
        body.collisionGroup = source.collisionGroup;
        for (const physics_portable::RagdollShapeDesc& shapeSource : source.shapes)
        {
            OgsrPhysXRagdollShape shape{};
            shape.type = static_cast<std::uint8_t>(shapeSource.type);
            storeRow(shapeSource.local, shape.local);
            shape.halfExtents[0] = shapeSource.halfExtents.x;
            shape.halfExtents[1] = shapeSource.halfExtents.y;
            shape.halfExtents[2] = shapeSource.halfExtents.z;
            shape.radius = shapeSource.radius;
            shape.height = shapeSource.height;
            shape.margin = shapeSource.margin;
            shape.materialId = shapeSource.materialId;
            shape.sourceBoneId = shapeSource.sourceBoneId;
            shape.flags = shapeSource.flags;
            shapes.push_back(shape);
        }
        bodies.push_back(body);
    }

    joints.reserve(description.joints.size());
    for (const physics_portable::RagdollJointDesc& source : description.joints)
    {
        OgsrPhysXRagdollJoint joint{};
        joint.id = source.id;
        joint.boneId = source.boneId;
        joint.parentBody = source.parentBody;
        joint.childBody = source.childBody;
        joint.kind = static_cast<std::uint8_t>(source.kind);
        joint.axisCount = source.axisCount;
        joint.breakable = source.breakable ? 1 : 0;
        joint.swingLimitMode = static_cast<std::uint8_t>(source.swingLimitMode);
        storeRow(source.parentFrame, joint.parentFrame);
        storeRow(source.childFrame, joint.childFrame);
        joint.erp = source.erp;
        joint.cfm = source.cfm;
        joint.friction = source.friction;
        joint.maxForce = source.maxForce;
        joint.targetVelocity = source.targetVelocity;
        joint.breakForce = source.breakForce;
        joint.breakTorque = source.breakTorque;
        joint.flags = source.sourceFlags;
        for (std::size_t axis = 0; axis < source.axisCount && axis < OGSR_PHYSX_RAGDOLL_MAX_AXES; ++axis)
        {
            const physics_portable::RagdollAxisLimit& sourceAxis = source.axes[axis];
            OgsrPhysXRagdollAxis& axisOut = joint.axes[axis];
            axisOut.axis[0] = sourceAxis.axis.x;
            axisOut.axis[1] = sourceAxis.axis.y;
            axisOut.axis[2] = sourceAxis.axis.z;
            axisOut.space = static_cast<std::uint8_t>(sourceAxis.space);
            axisOut.lower = sourceAxis.lower;
            axisOut.upper = sourceAxis.upper;
            axisOut.sourceLower = sourceAxis.sourceLower;
            axisOut.sourceUpper = sourceAxis.sourceUpper;
            axisOut.spring = sourceAxis.spring;
            axisOut.damping = sourceAxis.damping;
            axisOut.zero = sourceAxis.zero;
        }
        joints.push_back(joint);
    }

    bodyWorld.resize(initial.bodyWorld.size() * 16);
    linearVelocity.resize(initial.linearVelocity.size() * 3);
    angularVelocity.resize(initial.angularVelocity.size() * 3);
    enabled.assign(initial.bodyWorld.size(), 1);
    for (std::size_t body = 0; body < initial.bodyWorld.size(); ++body)
    {
        physics_portable::Transform world = initial.bodyWorld[body];
        world.translation.x += offset.x;
        world.translation.y += offset.y;
        world.translation.z += offset.z;
        storeRow(world, bodyWorld.data() + body * 16);
        const physics_portable::Vec3& linear = initial.linearVelocity[body];
        linearVelocity[body * 3 + 0] = linear.x;
        linearVelocity[body * 3 + 1] = linear.y;
        linearVelocity[body * 3 + 2] = linear.z;
        const physics_portable::Vec3& angular = initial.angularVelocity[body];
        angularVelocity[body * 3 + 0] = angular.x;
        angularVelocity[body * 3 + 1] = angular.y;
        angularVelocity[body * 3 + 2] = angular.z;
        if (!initial.enabled.empty())
            enabled[body] = initial.enabled[body] ? 1 : 0;
    }

    physics_portable::Transform entityWorld = initial.entityWorld;
    entityWorld.translation.x += offset.x;
    entityWorld.translation.y += offset.y;
    entityWorld.translation.z += offset.z;

    definition = OgsrPhysXRagdollDefinition{};
    definition.rootBody = description.rootBody;
    definition.boneCount = static_cast<std::uint32_t>(bones.size());
    definition.bodyCount = static_cast<std::uint32_t>(bodies.size());
    definition.jointCount = static_cast<std::uint32_t>(joints.size());
    definition.shapeCount = static_cast<std::uint32_t>(shapes.size());
    definition.materialCount = static_cast<std::uint32_t>(materials.size());
    definition.bones = bones.data();
    definition.bodies = bodies.data();
    definition.joints = joints.data();
    definition.shapes = shapes.data();
    definition.materials = materials.data();
    storeRow(entityWorld, definition.entityWorld);
    storeRow(initial.rootToEntity, definition.rootToEntity);
    definition.initialBodyWorld = bodyWorld.data();
    definition.initialLinearVelocity = linearVelocity.data();
    definition.initialAngularVelocity = angularVelocity.data();
    definition.initialEnabled = enabled.data();
}

} // namespace physics_physx_tests
