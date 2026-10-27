#include "physics_adapter_ogsr/capture.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace physics_adapter_ogsr
{
namespace
{
using namespace physics_portable;

bool fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
    return false;
}

bool finite(const Transform& value) { return isFinite(value); }

JointKind jointKindFor(SourceJointType type, std::uint8_t axisCount)
{
    switch (type)
    {
    case SourceJointType::Cloth: return JointKind::Ball;
    case SourceJointType::Joint:
        if (axisCount >= 3)
            return JointKind::Euler3;
        if (axisCount == 1)
            return JointKind::Hinge;
        return JointKind::Ball;
    case SourceJointType::Wheel: return JointKind::Hinge2;
    case SourceJointType::Slider: return JointKind::Slider;
    case SourceJointType::None:
    case SourceJointType::Rigid:
    default: return JointKind::Fixed;
    }
}

std::pair<float, float> effectiveOgsrLimits(float lower, float upper)
{
    constexpr float pi = 3.14159265358979323846f;
    float lo = lower;
    float hi = upper;
    if (lo < -pi)
    {
        hi -= (lo + pi);
        lo = -pi;
    }
    if (lo > 0.0f)
    {
        hi -= lo;
        lo = 0.0f;
    }
    if (hi > pi)
    {
        lo -= (hi - pi);
        hi = pi;
    }
    if (hi < 0.0f)
    {
        lo -= hi;
        hi = 0.0f;
    }
    return {lo, hi};
}

RagdollShapeDesc makeShape(const CaptureShape& source, BoneId bone)
{
    RagdollShapeDesc shape;
    shape.type = source.type;
    shape.local = source.local;
    shape.halfExtents = source.halfExtents;
    shape.radius = source.radius;
    shape.height = source.height;
    shape.margin = source.margin;
    shape.materialId = source.materialId;
    shape.sourceBoneId = bone;
    shape.flags = source.flags;
    return shape;
}

} // namespace

bool buildRagdollCapture(const SkeletonCapture& capture,
                         RagdollCaptureDocument& document,
                         RagdollBuildReport& report,
                         std::string* error)
{
    report = RagdollBuildReport{};
    if (capture.bones.empty())
        return fail(error, "capture has no bones");
    if (capture.currentBoneModelPose.size() != capture.bones.size())
        return fail(error, "current bone pose count does not match bone count");
    if (!finite(capture.entityWorld))
        return fail(error, "capture entity transform is not finite");

    const std::size_t boneCount = capture.bones.size();
    std::vector<Transform> bindWorld(boneCount);
    std::vector<int> bindState(boneCount, 0);
    std::vector<std::vector<BoneId>> children(boneCount);
    std::vector<BoneId> roots;

    for (std::size_t index = 0; index < boneCount; ++index)
    {
        const CaptureBone& bone = capture.bones[index];
        if (bone.id != static_cast<BoneId>(index))
            return fail(error, "capture bones must be indexed by contiguous BoneId");
        if (!finite(bone.bindLocal) || !finite(capture.currentBoneModelPose[index]))
            return fail(error, "capture bone transform is not finite");
        if (bone.parent == kInvalidBone)
        {
            roots.push_back(bone.id);
        }
        else
        {
            if (bone.parent >= boneCount)
                return fail(error, "capture bone parent is out of range");
            children[bone.parent].push_back(bone.id);
        }
        if (bone.limitCount > 3)
            return fail(error, "capture bone has more than three joint limits");
        if (bone.jointType == SourceJointType::Wheel)
            return fail(error, "Wheel/Hinge2 joints are not supported by P1");
    }
    if (roots.empty())
        return fail(error, "capture has no root bone");

    std::function<bool(BoneId)> computeBind = [&](BoneId id) -> bool {
        if (bindState[id] == 1)
            return false;
        if (bindState[id] == 2)
            return true;
        bindState[id] = 1;
        const CaptureBone& bone = capture.bones[id];
        if (bone.parent == kInvalidBone)
            bindWorld[id] = bone.bindLocal;
        else
            bindWorld[id] = bindWorld[bone.parent] * bone.bindLocal;
        if (!finite(bindWorld[id]))
            return false;
        for (BoneId child : children[id])
        {
            if (!computeBind(child))
                return false;
        }
        bindState[id] = 2;
        return true;
    };
    for (BoneId root : roots)
    {
        if (!computeBind(root))
            return fail(error, "capture bind hierarchy produces a non-finite transform");
    }
    for (BoneId bone = 0; bone < boneCount; ++bone)
    {
        if (bindState[bone] != 2)
            return fail(error, "capture bone hierarchy is cyclic or unreachable from a root");
    }

    RagdollDescription description;
    description.schemaVersion = 1;
    description.bones.resize(boneCount);
    description.skeletonName = capture.skeletonName;
    description.sourceHash = capture.modelHash;

    RagdollMaterialDesc material;
    material.id = capture.materialId;
    material.name = capture.materialName;
    material.friction = capture.friction;
    material.restitution = capture.restitution;
    description.materials.push_back(material);

    std::vector<BodyId> bodyByBone(boneCount, kInvalidBody);
    std::vector<BoneId> bodyBone;
    std::vector<Transform> bodyBindWorld;

    auto addJoint = [&](BoneId childBone, BodyId parentBody, BodyId childBody) {
        const CaptureBone& bone = capture.bones[childBone];
        RagdollJointDesc joint;
        joint.id = static_cast<JointId>(description.joints.size());
        joint.boneId = childBone;
        joint.parentBody = parentBody;
        joint.childBody = childBody;
        joint.kind = jointKindFor(bone.jointType, bone.limitCount);
        joint.parentFrame = inverse(bodyBindWorld[parentBody]) * bindWorld[childBone];
        joint.childFrame = Transform{};
        joint.axisCount = bone.limitCount;
        for (std::uint8_t axis = 0; axis < bone.limitCount; ++axis)
        {
            joint.axes[axis] = bone.limits[axis];
            const auto range = effectiveOgsrLimits(bone.limits[axis].lower, bone.limits[axis].upper);
            joint.axes[axis].lower = range.first;
            joint.axes[axis].upper = range.second;
        }
        joint.friction = bone.friction;
        joint.breakable = bone.breakable;
        joint.breakForce = bone.breakForce;
        joint.breakTorque = bone.breakTorque;
        joint.swingLimitMode = bone.swingLimitMode;
        joint.sourceFlags = bone.sourceFlags;
        description.joints.push_back(joint);
    };

    std::function<bool(BoneId, BodyId)> visit = [&](BoneId id, BodyId parentBody) -> bool {
        const CaptureBone& bone = capture.bones[id];
        RagdollBoneDesc boneDesc;
        boneDesc.id = bone.id;
        boneDesc.parent = bone.parent;
        boneDesc.name = bone.name;
        boneDesc.bindLocal = bone.bindLocal;
        boneDesc.visible = bone.visible;
        boneDesc.sourceShapeFlags = bone.hasShape ? bone.shape.flags : 0;

        if (!bone.visible)
        {
            boneDesc.disposition = BoneDisposition::SkippedInvisible;
            boneDesc.body = kInvalidBody;
            report.skippedInvisibleBones.push_back(id);
            description.bones[boneDesc.id] = boneDesc;
            for (BoneId child : children[id])
            {
                if (!visit(child, parentBody))
                    return false;
            }
            return true;
        }

        const bool mergeShapeIntoParent =
            parentBody != kInvalidBody &&
            (bone.jointType == SourceJointType::Rigid || !bone.hasShape);

        if (mergeShapeIntoParent)
        {
            boneDesc.disposition = BoneDisposition::MergedIntoParent;
            boneDesc.body = parentBody;
            description.bones[boneDesc.id] = boneDesc;
            report.mergedBones.push_back(id);
            bodyByBone[id] = parentBody;
            if (bone.hasShape)
            {
                RagdollShapeDesc shape = makeShape(bone.shape, id);
                // P1 captures a single material for all shapes; game-material IDs are
                // diagnostic data until a full material table is exported.
                shape.materialId = capture.materialId;
                // Child bone frame -> parent body bind frame.
                shape.local = inverse(bodyBindWorld[parentBody]) * bindWorld[id] * shape.local;
                description.bodies[parentBody].shapes.push_back(shape);
            }
            description.bodies[parentBody].mergedBones.push_back(id);
            for (BoneId child : children[id])
            {
                if (!visit(child, parentBody))
                    return false;
            }
            return true;
        }

        RagdollBodyDesc body;
        body.id = static_cast<BodyId>(description.bodies.size());
        body.boneId = id;
        body.parentBody = parentBody;
        body.role = (parentBody == kInvalidBody && !bone.hasShape) ? BodyRole::StaticRoot
                    : (bone.jointType == SourceJointType::None) ? BodyRole::FreeBody
                                                                : BodyRole::Dynamic;
        body.isFixed = body.role == BodyRole::StaticRoot;
        body.mass = body.role == BodyRole::StaticRoot ? 0.0f : bone.mass;
        body.centerOfMass = bone.centerOfMass;
        for (int component = 0; component < 6; ++component)
            body.inertia[component] = bone.inertia[component];
        body.bindPose = parentBody == kInvalidBody
                            ? bindWorld[id]
                            : inverse(bodyBindWorld[parentBody]) * bindWorld[id];
        if (bone.hasShape)
        {
            RagdollShapeDesc shape = makeShape(bone.shape, id);
            shape.materialId = capture.materialId;
            body.shapes.push_back(shape);
        }

        if (body.role == BodyRole::Dynamic && !(body.mass > 0.0f))
            return fail(error, "dynamic capture body has non-positive mass");
        if (body.role == BodyRole::FreeBody && !(body.mass > 0.0f))
            return fail(error, "free capture body has non-positive mass");

        const BodyId bodyId = body.id;
        description.bodies.push_back(body);
        bodyBindWorld.push_back(bindWorld[id]);
        bodyBone.push_back(id);
        bodyByBone[id] = bodyId;
        boneDesc.disposition = body.role == BodyRole::FreeBody ? BoneDisposition::FreeBody : BoneDisposition::Body;
        boneDesc.body = bodyId;
        description.bones[boneDesc.id] = boneDesc;
        report.bodyBones.push_back(id);

        if (parentBody != kInvalidBody && bone.jointType != SourceJointType::None &&
            bone.jointType != SourceJointType::Rigid)
        {
            addJoint(id, parentBody, bodyId);
        }

        for (BoneId child : children[id])
        {
            if (!visit(child, bodyId))
                return false;
        }
        return true;
    };

    for (BoneId root : roots)
    {
        if (!visit(root, kInvalidBody))
            return false;
    }

    if (description.bodies.empty())
        return fail(error, "capture produced no physical bodies");
    if (bodyByBone[roots.front()] != kInvalidBody)
        description.rootBody = bodyByBone[roots.front()];
    else
        description.rootBody = 0;

    RagdollInitialState initial;
    initial.entityWorld = capture.entityWorld;
    initial.bodyWorld.resize(description.bodies.size());
    initial.linearVelocity.assign(description.bodies.size(), capture.linearVelocity);
    initial.angularVelocity.assign(description.bodies.size(), Vec3{});
    initial.enabled.assign(description.bodies.size(), 1);
    for (BodyId body = 0; body < description.bodies.size(); ++body)
    {
        const BoneId bone = bodyBone[body];
        initial.bodyWorld[body] = capture.entityWorld * capture.currentBoneModelPose[bone];
    }
    initial.rootToEntity = inverse(initial.bodyWorld[description.rootBody]) * initial.entityWorld;

    std::string validationError;
    if (!validateDescription(description, &validationError))
        return fail(error, "capture description validation failed: " + validationError);
    if (!validateInitialState(description, initial, &validationError))
        return fail(error, "capture initial-state validation failed: " + validationError);

    document.description = std::move(description);
    document.initial = std::move(initial);
    document.ogsrCommit = capture.ogsrCommit;
    document.visual = capture.visual;
    document.modelHash = capture.modelHash;
    document.fixCopJoints = capture.fixCopJoints;
    return true;
}

} // namespace physics_adapter_ogsr
