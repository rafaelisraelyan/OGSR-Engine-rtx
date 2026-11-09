#include "synthetic_fixture.h"

#include "physics_portable/ragdoll.h"

#include <array>
#include <cstdint>
#include <string>

namespace physics_physx_tests
{
namespace
{
using physics_portable::BoneDisposition;
using physics_portable::BodyRole;
using physics_portable::JointKind;
using physics_portable::RagdollBodyDesc;
using physics_portable::RagdollBoneDesc;
using physics_portable::RagdollCaptureDocument;
using physics_portable::RagdollJointDesc;
using physics_portable::RagdollMaterialDesc;
using physics_portable::RagdollShapeDesc;
using physics_portable::ShapeType;
using physics_portable::Transform;
using physics_portable::Vec3;

constexpr std::size_t kBodyCount = 11;
constexpr std::size_t kJointCount = 10;

const std::array<Vec3, kBodyCount> kPositions = {
    Vec3{0.00f, 1.00f, 0.00f},  // pelvis/root
    Vec3{-0.12f, 0.75f, 0.00f}, // left thigh
    Vec3{-0.12f, 0.40f, 0.00f}, // left calf
    Vec3{0.12f, 0.75f, 0.00f},  // right thigh
    Vec3{0.12f, 0.40f, 0.00f},  // right calf
    Vec3{0.00f, 1.30f, 0.00f},  // chest/spine
    Vec3{0.00f, 1.60f, 0.00f},  // head
    Vec3{-0.25f, 1.30f, 0.00f}, // left upper arm
    Vec3{-0.45f, 1.30f, 0.00f}, // left forearm
    Vec3{0.25f, 1.30f, 0.00f},  // right upper arm
    Vec3{0.45f, 1.30f, 0.00f},  // right forearm
};

const std::array<std::int32_t, kBodyCount> kParents = {
    -1, 0, 1, 0, 3, 0, 5, 5, 7, 5, 9,
};

const std::array<float, kBodyCount> kRadii = {
    0.198821245f, 0.100f, 0.090f, 0.100f, 0.090f, 0.160f,
    0.110f, 0.070f, 0.060f, 0.070f, 0.060f,
};

// The mass distribution mirrors the historical OGSR 11-body baseline so the
// synthetic CI fixture exercises the same total/root mass assertions without
// containing any captured game data.
const std::array<float, kBodyCount> kMasses = {
    0.0f, 7.4636364f, 7.11363602f, 7.4636364f, 7.11363602f, 14.4636364f,
    7.4636364f, 7.4636364f, 8.16363621f, 7.4636364f, 8.16363621f,
};

Transform relativeTransform(std::size_t body)
{
    const std::int32_t parent = kParents[body];
    if (parent < 0)
        return physics_portable::makeTransform({}, kPositions[body]);

    const Vec3 delta = kPositions[body] - kPositions[static_cast<std::size_t>(parent)];
    return physics_portable::makeTransform({}, delta);
}

void addBody(RagdollCaptureDocument& document, std::size_t index)
{
    RagdollBoneDesc bone{};
    bone.id = static_cast<physics_portable::BoneId>(index);
    bone.parent = kParents[index] < 0
                      ? physics_portable::kInvalidBone
                      : static_cast<physics_portable::BoneId>(kParents[index]);
    bone.name = "synthetic_" + std::to_string(index);
    bone.bindLocal = relativeTransform(index);
    bone.visible = true;
    bone.disposition = BoneDisposition::Body;
    bone.body = static_cast<physics_portable::BodyId>(index);
    document.description.bones.push_back(bone);

    RagdollBodyDesc body{};
    body.id = static_cast<physics_portable::BodyId>(index);
    body.boneId = static_cast<physics_portable::BoneId>(index);
    body.parentBody = kParents[index] < 0
                          ? physics_portable::kInvalidBody
                          : static_cast<physics_portable::BodyId>(kParents[index]);
    body.role = index == 0 ? BodyRole::StaticRoot : BodyRole::Dynamic;
    body.isFixed = index == 0;
    body.mass = kMasses[index];
    body.centerOfMass = {};
    const float inertia = index == 0 ? 0.0f : 0.4f * body.mass * kRadii[index] * kRadii[index];
    body.inertia[0] = inertia;
    body.inertia[3] = inertia;
    body.inertia[5] = inertia;
    body.bindPose = relativeTransform(index);
    body.collisionGroup = 0;

    RagdollShapeDesc shape{};
    shape.type = ShapeType::Sphere;
    shape.local = physics_portable::makeTransform(physics_portable::Quat{}, physics_portable::Vec3{});
    shape.radius = kRadii[index];
    shape.height = 0.1f;
    shape.materialId = 0;
    shape.sourceBoneId = static_cast<physics_portable::BoneId>(index);
    body.shapes.push_back(shape);

    document.description.bodies.push_back(body);
}

void addJoint(RagdollCaptureDocument& document, std::size_t child)
{
    RagdollJointDesc joint{};
    joint.id = static_cast<physics_portable::JointId>(child - 1);
    joint.boneId = static_cast<physics_portable::BoneId>(child);
    joint.parentBody = static_cast<physics_portable::BodyId>(kParents[child]);
    joint.childBody = static_cast<physics_portable::BodyId>(child);
    joint.kind = JointKind::Ball;
    joint.parentFrame = relativeTransform(child);
    joint.childFrame = physics_portable::makeTransform(physics_portable::Quat{}, physics_portable::Vec3{});
    joint.axisCount = 0;
    joint.friction = 1.0f;
    document.description.joints.push_back(joint);
}
} // namespace

bool makeSyntheticRagdollCapture(RagdollCaptureDocument& document)
{
    document = RagdollCaptureDocument{};
    document.ogsrCommit = "synthetic-ci-fixture";
    document.visual = "synthetic_humanoid";
    document.modelHash = 0;
    document.fixCopJoints = false;

    document.description.schemaVersion = 1;
    document.description.skeletonName = "synthetic_humanoid";
    document.description.sourceHash = 0;
    document.description.rootBody = 0;

    for (std::size_t body = 0; body < kBodyCount; ++body)
        addBody(document, body);
    for (std::size_t child = 1; child < kBodyCount; ++child)
        addJoint(document, child);

    RagdollMaterialDesc material{};
    material.id = 0;
    material.name = "synthetic_material";
    material.friction = 0.8f;
    material.restitution = 0.0f;
    material.spring = 1.0f;
    material.damping = 1.0f;
    document.description.materials.push_back(material);

    document.initial.entityWorld =
        physics_portable::makeTransform(physics_portable::Quat{}, physics_portable::Vec3{0.0f, 1.0f, 0.0f});
    document.initial.rootToEntity = physics_portable::makeTransform(physics_portable::Quat{}, physics_portable::Vec3{});
    document.initial.bodyWorld.resize(kBodyCount);
    document.initial.linearVelocity.assign(kBodyCount, physics_portable::Vec3{});
    document.initial.angularVelocity.assign(kBodyCount, physics_portable::Vec3{});
    document.initial.enabled.assign(kBodyCount, 1);
    for (std::size_t body = 0; body < kBodyCount; ++body)
        document.initial.bodyWorld[body] = physics_portable::makeTransform({}, kPositions[body]);

    return document.description.bodies.size() == kBodyCount &&
           document.description.joints.size() == kJointCount;
}

} // namespace physics_physx_tests
