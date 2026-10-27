#include "humanoid_fixture.h"

#include <cmath>
#include <string>
#include <vector>

using namespace physics_portable;

namespace physics_physx_tests
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;

Transform translation(float x, float y, float z)
{
    return Transform{Quat{}, makeVec3(x, y, z)};
}

float boxInertia(float mass, float halfA, float halfB)
{
    return mass / 3.0f * (halfA * halfA + halfB * halfB);
}

void fillBoxInertia(RagdollBodyDesc& body, const Vec3& half)
{
    body.inertia[0] = boxInertia(body.mass, half.y, half.z);
    body.inertia[1] = 0.0f;
    body.inertia[2] = 0.0f;
    body.inertia[3] = boxInertia(body.mass, half.x, half.z);
    body.inertia[4] = 0.0f;
    body.inertia[5] = boxInertia(body.mass, half.x, half.y);
}

void fillSphereInertia(RagdollBodyDesc& body, float radius)
{
    const float value = 0.4f * body.mass * radius * radius;
    body.inertia[0] = value;
    body.inertia[1] = 0.0f;
    body.inertia[2] = 0.0f;
    body.inertia[3] = value;
    body.inertia[4] = 0.0f;
    body.inertia[5] = value;
}

void fillCylinderInertia(RagdollBodyDesc& body, float radius, float height)
{
    // Contract shape axis is local X; this fixture rotates it to body Y.
    body.inertia[0] = body.mass / 12.0f * (3.0f * radius * radius + height * height);
    body.inertia[1] = 0.0f;
    body.inertia[2] = 0.0f;
    body.inertia[3] = 0.5f * body.mass * radius * radius;
    body.inertia[4] = 0.0f;
    body.inertia[5] = body.inertia[0];
}

struct BodySpec
{
    const char* name;
    BodyId parent;
    Vec3 position;
    float mass;
    ShapeType shapeType;
    Vec3 half;
    float radius;
    float height;
};

RagdollDescription makeHumanoid(SwingLimitMode swingLimitMode)
{
    const BodySpec specs[] = {
        {"pelvis", kInvalidBody, makeVec3(0.0f, 1.0f, 0.0f), 12.0f, ShapeType::Box, makeVec3(0.12f, 0.12f, 0.08f), 0.0f, 0.0f},
        {"spine", 0, makeVec3(0.0f, 1.18f, 0.0f), 10.0f, ShapeType::Box, makeVec3(0.10f, 0.14f, 0.07f), 0.0f, 0.0f},
        {"chest", 1, makeVec3(0.0f, 1.38f, 0.0f), 14.0f, ShapeType::Box, makeVec3(0.14f, 0.15f, 0.09f), 0.0f, 0.0f},
        {"head", 2, makeVec3(0.0f, 1.60f, 0.0f), 5.0f, ShapeType::Sphere, Vec3{}, 0.10f, 0.0f},
        {"upper_arm_l", 2, makeVec3(-0.22f, 1.48f, 0.0f), 3.0f, ShapeType::Cylinder, Vec3{}, 0.05f, 0.24f},
        {"forearm_l", 4, makeVec3(-0.44f, 1.30f, 0.0f), 2.0f, ShapeType::Box, makeVec3(0.045f, 0.11f, 0.045f), 0.0f, 0.0f},
        {"hand_l", 5, makeVec3(-0.52f, 1.18f, 0.0f), 0.5f, ShapeType::Sphere, Vec3{}, 0.04f, 0.0f},
        {"upper_arm_r", 2, makeVec3(0.22f, 1.48f, 0.0f), 3.0f, ShapeType::Cylinder, Vec3{}, 0.05f, 0.24f},
        {"forearm_r", 7, makeVec3(0.44f, 1.30f, 0.0f), 2.0f, ShapeType::Box, makeVec3(0.045f, 0.11f, 0.045f), 0.0f, 0.0f},
        {"hand_r", 8, makeVec3(0.52f, 1.18f, 0.0f), 0.5f, ShapeType::Sphere, Vec3{}, 0.04f, 0.0f},
        {"thigh_l", 0, makeVec3(-0.10f, 0.78f, 0.0f), 7.0f, ShapeType::Box, makeVec3(0.07f, 0.16f, 0.07f), 0.0f, 0.0f},
        {"shin_l", 10, makeVec3(-0.10f, 0.46f, 0.0f), 4.0f, ShapeType::Box, makeVec3(0.06f, 0.16f, 0.06f), 0.0f, 0.0f},
        {"foot_l", 11, makeVec3(-0.10f, 0.18f, 0.06f), 1.0f, ShapeType::Box, makeVec3(0.06f, 0.05f, 0.12f), 0.0f, 0.0f},
        {"thigh_r", 0, makeVec3(0.10f, 0.78f, 0.0f), 7.0f, ShapeType::Box, makeVec3(0.07f, 0.16f, 0.07f), 0.0f, 0.0f},
        {"shin_r", 13, makeVec3(0.10f, 0.46f, 0.0f), 4.0f, ShapeType::Box, makeVec3(0.06f, 0.16f, 0.06f), 0.0f, 0.0f},
        {"foot_r", 14, makeVec3(0.10f, 0.18f, 0.06f), 1.0f, ShapeType::Box, makeVec3(0.06f, 0.05f, 0.12f), 0.0f, 0.0f},
    };

    RagdollDescription description;
    description.skeletonName = "p1_synthetic_humanoid";
    description.sourceHash = 0x50314658;
    description.rootBody = 0;

    RagdollMaterialDesc material;
    material.id = 0;
    material.name = "synthetic";
    material.friction = 0.8f;
    material.restitution = 0.0f;
    description.materials.push_back(material);

    std::vector<Vec3> positions;
    for (std::size_t index = 0; index < sizeof(specs) / sizeof(specs[0]); ++index)
    {
        const BodySpec& spec = specs[index];
        positions.push_back(spec.position);

        RagdollBodyDesc body;
        body.id = static_cast<BodyId>(index);
        body.boneId = static_cast<BoneId>(index);
        body.parentBody = spec.parent;
        body.role = BodyRole::Dynamic;
        body.isFixed = false;
        body.mass = spec.mass;
        body.centerOfMass = makeVec3(0.0f, 0.0f, 0.0f);
        body.collisionGroup = 1;
        body.bindPose = spec.parent == kInvalidBody
                            ? translation(spec.position.x, spec.position.y, spec.position.z)
                            : translation(spec.position.x - positions[spec.parent].x,
                                          spec.position.y - positions[spec.parent].y,
                                          spec.position.z - positions[spec.parent].z);

        RagdollShapeDesc shape;
        shape.type = spec.shapeType;
        shape.local = Transform{};
        shape.halfExtents = spec.half;
        shape.radius = spec.radius;
        shape.height = spec.height;
        shape.margin = 0.0f;
        shape.materialId = 0;
        shape.sourceBoneId = body.boneId;
        if (spec.shapeType == ShapeType::Cylinder)
        {
            // Contract X axis -> body Y axis.
            shape.local.rotation = fromAxisAngle(makeVec3(0.0f, 0.0f, 1.0f), kPi * 0.5f);
        }
        body.shapes.push_back(shape);

        if (spec.shapeType == ShapeType::Box)
            fillBoxInertia(body, spec.half);
        else if (spec.shapeType == ShapeType::Sphere)
            fillSphereInertia(body, spec.radius);
        else
            fillCylinderInertia(body, spec.radius, spec.height);
        description.bodies.push_back(body);

        RagdollBoneDesc bone;
        bone.id = body.boneId;
        bone.parent = spec.parent == kInvalidBody ? kInvalidBone : static_cast<BoneId>(spec.parent);
        bone.name = spec.name;
        bone.bindLocal = body.bindPose;
        bone.visible = true;
        bone.disposition = BoneDisposition::Body;
        bone.body = body.id;
        description.bones.push_back(bone);
    }

    auto addJoint = [&](JointKind kind, BodyId parent, BodyId child, std::uint8_t axisCount,
                        RagdollAxisLimit* axes, SwingLimitMode mode) {
        RagdollJointDesc joint;
        joint.id = static_cast<JointId>(description.joints.size());
        joint.boneId = static_cast<BoneId>(child);
        joint.parentBody = parent;
        joint.childBody = child;
        joint.kind = kind;
        joint.parentFrame = translation(positions[child].x - positions[parent].x,
                                        positions[child].y - positions[parent].y,
                                        positions[child].z - positions[parent].z);
        joint.childFrame = Transform{};
        joint.axisCount = axisCount;
        joint.swingLimitMode = mode;
        for (std::uint8_t axis = 0; axis < axisCount; ++axis)
            joint.axes[axis] = axes[axis];
        description.joints.push_back(joint);
    };

    auto ball = [&](BodyId parent, BodyId child) { addJoint(JointKind::Ball, parent, child, 0, nullptr, swingLimitMode); };
    auto fixed = [&](BodyId parent, BodyId child) { addJoint(JointKind::Fixed, parent, child, 0, nullptr, swingLimitMode); };
    auto hinge = [&](BodyId parent, BodyId child) {
        RagdollAxisLimit axes[3]{};
        axes[0].space = AxisSpace::Child;
        axes[0].axis = makeVec3(1.0f, 0.0f, 0.0f);
        axes[0].lower = -1.2f;
        axes[0].upper = 1.2f;
        addJoint(JointKind::Hinge, parent, child, 1, axes, swingLimitMode);
    };
    auto euler3 = [&](BodyId parent, BodyId child) {
        RagdollAxisLimit axes[3]{};
        axes[0] = RagdollAxisLimit{AxisSpace::Child, makeVec3(1.0f, 0.0f, 0.0f), -0.7f, 0.7f};
        axes[1] = RagdollAxisLimit{AxisSpace::Child, makeVec3(0.0f, 1.0f, 0.0f), -0.5f, 0.9f};
        axes[2] = RagdollAxisLimit{AxisSpace::Child, makeVec3(0.0f, 0.0f, 1.0f), -0.4f, 0.6f};
        axes[0].sourceLower = axes[0].lower;
        axes[0].sourceUpper = axes[0].upper;
        axes[1].sourceLower = axes[1].lower;
        axes[1].sourceUpper = axes[1].upper;
        axes[2].sourceLower = axes[2].lower;
        axes[2].sourceUpper = axes[2].upper;
        addJoint(JointKind::Euler3, parent, child, 3, axes, swingLimitMode);
    };

    ball(0, 1);
    ball(1, 2);
    ball(2, 3);
    euler3(2, 4);
    hinge(4, 5);
    fixed(5, 6);
    euler3(2, 7);
    hinge(7, 8);
    fixed(8, 9);
    euler3(0, 10);
    hinge(10, 11);
    fixed(11, 12);
    euler3(0, 13);
    hinge(13, 14);
    fixed(14, 15);

    return description;
}

RagdollInitialState makeInitial(const RagdollDescription& description, const Vec3& velocity)
{
    RagdollInitialState initial;
    initial.entityWorld = Transform{};
    initial.bodyWorld.resize(description.bodies.size());
    initial.linearVelocity.assign(description.bodies.size(), velocity);
    initial.angularVelocity.assign(description.bodies.size(), Vec3{});
    initial.enabled.assign(description.bodies.size(), 1);

    for (BodyId body = 0; body < description.bodies.size(); ++body)
    {
        Transform world = description.bodies[body].bindPose;
        BodyId parent = description.bodies[body].parentBody;
        while (parent != kInvalidBody)
        {
            world = description.bodies[parent].bindPose * world;
            parent = description.bodies[parent].parentBody;
        }
        initial.bodyWorld[body] = world;
    }

    initial.rootToEntity = inverse(initial.bodyWorld[description.rootBody]) * initial.entityWorld;
    return initial;
}

} // namespace

RagdollDescription makeHumanoidDescription(SwingLimitMode swingLimitMode) { return makeHumanoid(swingLimitMode); }

RagdollInitialState makeHumanoidInitialState(const RagdollDescription& description)
{
    return makeInitial(description, makeVec3(0.2f, 0.0f, -0.1f));
}

RagdollDescription makePendulumDescription(SwingLimitMode swingLimitMode)
{
    RagdollDescription description;
    description.skeletonName = "p1_pendulum";
    description.sourceHash = 0x50315044;
    description.rootBody = 0;

    RagdollMaterialDesc material;
    material.id = 0;
    material.friction = 0.8f;
    material.restitution = 0.0f;
    description.materials.push_back(material);

    RagdollBodyDesc root;
    root.id = 0;
    root.boneId = 0;
    root.parentBody = kInvalidBody;
    root.role = BodyRole::StaticRoot;
    root.isFixed = true;
    root.mass = 0.0f;
    root.bindPose = Transform{};
    description.bodies.push_back(root);

    RagdollBodyDesc child;
    child.id = 1;
    child.boneId = 1;
    child.parentBody = 0;
    child.role = BodyRole::Dynamic;
    child.mass = 1.0f;
    child.centerOfMass = Vec3{};
    child.bindPose = translation(0.0f, -1.0f, 0.0f);
    fillSphereInertia(child, 0.2f);
    RagdollShapeDesc sphere;
    sphere.type = ShapeType::Sphere;
    sphere.radius = 0.2f;
    sphere.materialId = 0;
    sphere.sourceBoneId = 1;
    child.shapes.push_back(sphere);
    description.bodies.push_back(child);

    RagdollBoneDesc rootBone;
    rootBone.id = 0;
    rootBone.parent = kInvalidBone;
    rootBone.name = "pivot";
    rootBone.disposition = BoneDisposition::Body;
    rootBone.body = 0;
    description.bones.push_back(rootBone);

    RagdollBoneDesc childBone;
    childBone.id = 1;
    childBone.parent = 0;
    childBone.name = "bob";
    childBone.bindLocal = child.bindPose;
    childBone.disposition = BoneDisposition::Body;
    childBone.body = 1;
    description.bones.push_back(childBone);

    RagdollJointDesc joint;
    joint.id = 0;
    joint.boneId = 1;
    joint.parentBody = 0;
    joint.childBody = 1;
    joint.kind = JointKind::Euler3;
    joint.parentFrame = Transform{};
    joint.childFrame = translation(0.0f, 1.0f, 0.0f);
    joint.axisCount = 3;
    joint.axes[0] = RagdollAxisLimit{AxisSpace::Child, makeVec3(1.0f, 0.0f, 0.0f), 0.0f, 0.0f};
    joint.axes[1] = RagdollAxisLimit{AxisSpace::Child, makeVec3(0.0f, 1.0f, 0.0f), 0.0f, 0.0f};
    joint.axes[2] = RagdollAxisLimit{AxisSpace::Child, makeVec3(0.0f, 0.0f, 1.0f), -0.2f, 0.8f};
    joint.axes[2].sourceLower = joint.axes[2].lower;
    joint.axes[2].sourceUpper = joint.axes[2].upper;
    joint.swingLimitMode = swingLimitMode;
    description.joints.push_back(joint);

    return description;
}

RagdollInitialState makePendulumInitialState(const RagdollDescription& description)
{
    return makeInitial(description, Vec3{});
}

} // namespace physics_physx_tests
