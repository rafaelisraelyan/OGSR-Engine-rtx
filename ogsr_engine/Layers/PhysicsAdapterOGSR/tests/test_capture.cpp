#include "physics_adapter_ogsr/capture.h"

#include "physics_portable/json.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace physics_adapter_ogsr;
using namespace physics_portable;

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

float boxInertia(float mass, float a, float b) { return mass / 3.0f * (a * a + b * b); }

CaptureBone makeBone(BoneId id, BoneId parent, const char* name, const Vec3& bindTranslation, bool visible)
{
    CaptureBone bone;
    bone.id = id;
    bone.parent = parent;
    bone.name = name;
    bone.bindLocal = Transform{Quat{}, bindTranslation};
    bone.visible = visible;
    return bone;
}

void setBox(CaptureBone& bone, const Vec3& half, float mass)
{
    bone.hasShape = true;
    bone.shape.type = ShapeType::Box;
    bone.shape.halfExtents = half;
    bone.shape.local = Transform{};
    bone.mass = mass;
    bone.centerOfMass = Vec3{};
    bone.inertia[0] = boxInertia(mass, half.y, half.z);
    bone.inertia[3] = boxInertia(mass, half.x, half.z);
    bone.inertia[5] = boxInertia(mass, half.x, half.y);
}

SkeletonCapture makeCapture()
{
    SkeletonCapture capture;
    capture.skeletonName = "adapter_test";
    capture.ogsrCommit = "test";
    capture.visual = "test_visual";
    capture.modelHash = 0x1234;
    capture.entityWorld = Transform{};
    capture.linearVelocity = makeVec3(0.5f, 0.0f, 0.0f);

    capture.bones.resize(5);
    capture.bones[0] = makeBone(0, kInvalidBone, "pelvis", makeVec3(0.0f, 1.0f, 0.0f), true);
    capture.bones[1] = makeBone(1, 0, "chest", makeVec3(0.0f, 0.2f, 0.0f), true);
    capture.bones[2] = makeBone(2, 1, "merged", makeVec3(0.0f, 0.1f, 0.0f), true);
    capture.bones[3] = makeBone(3, 1, "invisible", makeVec3(0.0f, 0.05f, 0.0f), false);
    capture.bones[4] = makeBone(4, 3, "arm", makeVec3(0.2f, -0.15f, 0.0f), true);

    setBox(capture.bones[0], makeVec3(0.15f, 0.15f, 0.10f), 10.0f);
    setBox(capture.bones[1], makeVec3(0.12f, 0.15f, 0.09f), 5.0f);
    capture.bones[2].hasShape = true;
    capture.bones[2].shape.type = ShapeType::Sphere;
    capture.bones[2].shape.radius = 0.08f;
    capture.bones[2].mass = 1.0f; // ignored: must already be in the parent ODE dMass
    capture.bones[4].hasShape = true;
    capture.bones[4].shape.type = ShapeType::Cylinder;
    capture.bones[4].shape.radius = 0.05f;
    capture.bones[4].shape.height = 0.2f;
    capture.bones[4].mass = 2.0f;
    capture.bones[4].inertia[0] = 0.01f;
    capture.bones[4].inertia[3] = 0.01f;
    capture.bones[4].inertia[5] = 0.01f;

    capture.bones[0].jointType = SourceJointType::Rigid;
    capture.bones[1].jointType = SourceJointType::Joint;
    capture.bones[1].limitCount = 3;
    capture.bones[1].limits[0] = RagdollAxisLimit{AxisSpace::Child, makeVec3(1.0f, 0.0f, 0.0f), -0.5f, 0.5f};
    capture.bones[1].limits[1] = RagdollAxisLimit{AxisSpace::Child, makeVec3(0.0f, 1.0f, 0.0f), -0.4f, 0.6f};
    capture.bones[1].limits[2] = RagdollAxisLimit{AxisSpace::Child, makeVec3(0.0f, 0.0f, 1.0f), -0.3f, 0.7f};
    capture.bones[2].jointType = SourceJointType::Rigid;
    capture.bones[3].jointType = SourceJointType::None;
    capture.bones[4].jointType = SourceJointType::Joint;
    capture.bones[4].limitCount = 1;
    capture.bones[4].limits[0] = RagdollAxisLimit{AxisSpace::Child, makeVec3(1.0f, 0.0f, 0.0f), 0.2f, 1.0f};

    capture.currentBoneModelPose.resize(5);
    capture.currentBoneModelPose[0] = makeTransform(Quat{}, makeVec3(0.0f, 1.0f, 0.0f));
    capture.currentBoneModelPose[1] = makeTransform(Quat{}, makeVec3(0.0f, 1.2f, 0.0f));
    capture.currentBoneModelPose[2] = makeTransform(Quat{}, makeVec3(0.0f, 1.3f, 0.0f));
    capture.currentBoneModelPose[3] = makeTransform(Quat{}, makeVec3(0.0f, 1.25f, 0.0f));
    capture.currentBoneModelPose[4] = makeTransform(Quat{}, makeVec3(0.2f, 1.1f, 0.0f));
    return capture;
}

void testCaptureRules()
{
    SkeletonCapture capture = makeCapture();
    RagdollCaptureDocument document;
    RagdollBuildReport report;
    std::string error;
    check(buildRagdollCapture(capture, document, report, &error), "capture builder succeeds");
    if (!error.empty())
        std::printf("  build error: %s\n", error.c_str());
    check(document.description.bodies.size() == 3, "two visible bodies plus arm body are created");
    check(document.description.joints.size() == 2, "two joints are created");
    check(document.description.bones.size() == 5, "all bones retained");
    check(document.description.bones[2].disposition == BoneDisposition::MergedIntoParent,
          "jtRigid bone is merged");
    check(document.description.bones[2].body == 1, "merged bone references chest body");
    check(document.description.bones[3].disposition == BoneDisposition::SkippedInvisible,
          "invisible bone is skipped");
    check(document.description.bones[3].body == kInvalidBody, "invisible bone has no body");
    check(document.description.bones[4].body == 2, "visible descendant receives its own body");
    check(document.description.bodies[1].shapes.size() == 2, "merged sphere is folded into chest shapes");
    check(document.description.bodies[1].mergedBones.size() == 1, "merged bone list is preserved");
    check(document.description.joints[1].parentBody == 1, "arm skips invisible bone and joins chest");
    check(std::abs(document.description.joints[1].axes[0].lower) < 1.0e-6f &&
              std::abs(document.description.joints[1].axes[0].upper - 0.8f) < 1.0e-6f,
          "OGSR positive-lower limit is shifted into the effective [0,0.8] range");
    check(document.description.rootBody == 0, "root body is pelvis");
    check(approxEqual(document.initial.entityWorld, capture.entityWorld, 1.0e-5f),
          "capture entity transform preserved");
    check(approxEqual(bodyToBoneModel(
                          RagdollPose{ document.initial.entityWorld, document.initial.bodyWorld, 0 }, 0),
                      capture.currentBoneModelPose[0], 1.0e-5f),
          "initial body pose round-trips to model space");

    std::string json;
    check(writeRagdollCapture(document, json, &error), "capture document serializes");
    RagdollCaptureDocument parsed;
    check(parseRagdollCapture(json, parsed, &error), "capture document parses");
    check(parsed.description.bodies.size() == 3, "parsed body count");
    check(parsed.description.joints.size() == 2, "parsed joint count");
}

void testBranchingBoneOrder()
{
    SkeletonCapture capture;
    capture.skeletonName = "branching";
    capture.bones.resize(4);
    capture.currentBoneModelPose.resize(4);
    for (BoneId id = 0; id < 4; ++id)
    {
        CaptureBone bone;
        bone.id = id;
        bone.name = "bone";
        bone.visible = true;
        bone.jointType = id == 0 ? SourceJointType::Rigid : SourceJointType::Joint;
        bone.bindLocal = id == 0 ? Transform{} : makeTransform(Quat{}, makeVec3(0.0f, 0.1f * id, 0.0f));
        if (id == 0)
            bone.parent = kInvalidBone;
        else if (id == 3)
            bone.parent = 1;
        else
            bone.parent = 0;
        setBox(bone, makeVec3(0.05f, 0.05f, 0.05f), 1.0f);
        capture.bones[id] = bone;
        capture.currentBoneModelPose[id] = makeTransform(Quat{}, makeVec3(0.0f, 1.0f + 0.1f * id, 0.0f));
    }
    // Correct parent IDs differ from the DFS output order: 0 -> 1 -> 3, then 0 -> 2.
    capture.bones[1].parent = 0;
    capture.bones[2].parent = 0;
    capture.bones[3].parent = 1;

    RagdollCaptureDocument document;
    RagdollBuildReport report;
    std::string error;
    check(buildRagdollCapture(capture, document, report, &error), "branching skeleton builds");
    check(document.description.bones.size() == 4, "branching skeleton retains all bones by id");
    check(document.description.bones[2].id == 2 && document.description.bones[3].id == 3,
          "branching skeleton bones are id-indexed");
    check(document.description.bodies[document.description.bones[3].body].boneId == 3,
          "branching skeleton body back-reference matches bone id");
}

void testInvalidCapture()
{
    SkeletonCapture capture = makeCapture();
    RagdollCaptureDocument document;
    RagdollBuildReport report;
    std::string error;
    check(buildRagdollCapture(capture, document, report, &error), "report reset setup builds");
    check(!report.bodyBones.empty(), "report reset setup populated");
    capture.currentBoneModelPose.pop_back();
    check(!buildRagdollCapture(capture, document, report, &error), "pose count mismatch is rejected");
    check(report.bodyBones.empty() && report.mergedBones.empty() && report.skippedInvisibleBones.empty(),
          "failed build clears the previous build report");
    check(!error.empty(), "pose count error has a message");

    capture = makeCapture();
    capture.bones[1].parent = 1;
    check(!buildRagdollCapture(capture, document, report, &error), "bone cycle is rejected");

    capture = makeCapture();
    capture.bones[1].mass = 0.0f;
    check(!buildRagdollCapture(capture, document, report, &error), "zero dynamic mass is rejected");

    capture = makeCapture();
    capture.bones[0].limitCount = 4;
    check(!buildRagdollCapture(capture, document, report, &error), "more than three joint limits is rejected");

    capture = makeCapture();
    capture.bones[4].jointType = SourceJointType::Wheel;
    check(!buildRagdollCapture(capture, document, report, &error), "unsupported wheel joint is rejected");
}

} // namespace

int main()
{
    testCaptureRules();
    testBranchingBoneOrder();
    testInvalidCapture();
    std::printf("physics_adapter_ogsr tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
