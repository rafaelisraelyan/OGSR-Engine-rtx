#include "physics_portable/ragdoll.h"
#include "physics_portable/row_matrix.h"
#include "physics_portable/json.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace physics_portable;

namespace
{
int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", message);
    }
}

void checkVec(const Vec3& actual, const Vec3& expected, float epsilon, const char* message)
{
    check(approxEqual(actual, expected, epsilon), message);
    if (!approxEqual(actual, expected, epsilon))
    {
        std::printf("  actual=(%f,%f,%f) expected=(%f,%f,%f)\n", actual.x, actual.y, actual.z, expected.x, expected.y,
                    expected.z);
    }
}

void testTransformComposition()
{
    const Transform a{fromAxisAngle(makeVec3(0.0f, 0.0f, 1.0f), 1.2f), makeVec3(1.0f, 2.0f, 3.0f)};
    const Transform b{fromAxisAngle(makeVec3(1.0f, 0.0f, 0.0f), -0.7f), makeVec3(-4.0f, 0.5f, 2.0f)};
    const Vec3 point = makeVec3(0.3f, -0.2f, 0.9f);

    const Transform combined = a * b;
    const Vec3 sequential = transformPoint(a, transformPoint(b, point));
    checkVec(transformPoint(combined, point), sequential, 1.0e-5f, "transform composition applies b then a");

    const Transform roundTrip = inverse(a) * a;
    checkVec(transformPoint(roundTrip, point), point, 1.0e-5f, "transform inverse round-trip");
    checkVec(transformPoint(inverse(a), transformPoint(a, point)), point, 1.0e-5f, "point inverse round-trip");
}

void testRowMatrixBridge()
{
    const Transform a{fromAxisAngle(makeVec3(0.2f, 0.8f, -0.4f), 1.37f), makeVec3(1.25f, -0.75f, 3.5f)};
    const Transform b{fromAxisAngle(makeVec3(-0.6f, 0.3f, 0.7f), -0.81f), makeVec3(-2.0f, 0.125f, 0.6f)};
    const Vec3 point = makeVec3(-0.4f, 0.8f, 0.15f);

    const RowMatrix4 rowA = rowMatrixFromTransform(a);
    const RowMatrix4 rowB = rowMatrixFromTransform(b);
    const RowMatrix4 rowCombined = rowMatrixMul43(rowA, rowB);

    checkVec(rowMatrixTransformPoint(rowA, point), transformPoint(a, point), 1.0e-5f,
             "single row matrix transform matches contract transform");
    checkVec(rowMatrixTransformPoint(rowCombined, point), transformPoint(a * b, point), 1.0e-5f,
             "mul43 matches contract composition");
    check(approxEqual(transformFromRowMatrix(rowCombined), a * b, 1.0e-5f),
          "transformFromRowMatrix(mul43(A,B)) equals A*B");
}

void testBindHierarchy()
{
    const Transform entity{fromAxisAngle(makeVec3(0.1f, 0.9f, 0.2f), 0.63f), makeVec3(4.0f, -1.0f, 2.0f)};
    const Transform pelvis{fromAxisAngle(makeVec3(0.0f, 1.0f, 0.0f), -0.2f), makeVec3(0.0f, 1.0f, 0.0f)};
    const Transform spine{fromAxisAngle(makeVec3(1.0f, 0.0f, 0.0f), 0.35f), makeVec3(0.1f, 0.2f, 0.0f)};
    const Transform chest{fromAxisAngle(makeVec3(0.0f, 0.0f, 1.0f), -0.4f), makeVec3(0.0f, 0.25f, 0.05f)};

    const Transform pelvisWorld = entity * pelvis;
    const Transform spineWorld = pelvisWorld * spine;
    const Transform chestWorld = spineWorld * chest;

    const RowMatrix4 pelvisRow = rowMatrixMul43(rowMatrixFromTransform(entity), rowMatrixFromTransform(pelvis));
    const RowMatrix4 spineRow = rowMatrixMul43(pelvisRow, rowMatrixFromTransform(spine));
    const RowMatrix4 chestRow = rowMatrixMul43(spineRow, rowMatrixFromTransform(chest));

    check(approxEqual(transformFromRowMatrix(pelvisRow), pelvisWorld, 1.0e-5f), "pelvis bind world");
    check(approxEqual(transformFromRowMatrix(spineRow), spineWorld, 1.0e-5f), "spine bind world");
    check(approxEqual(transformFromRowMatrix(chestRow), chestWorld, 1.0e-5f), "chest bind world");

    RagdollPose pose;
    pose.entityWorld = entity;
    pose.bodyWorld = {pelvisWorld, spineWorld, chestWorld};
    check(approxEqual(bodyToBoneModel(pose, 0), pelvis, 1.0e-5f), "body 0 model-space round-trip");
    check(approxEqual(bodyToBoneModel(pose, 1), pelvis * spine, 1.0e-5f), "body 1 model-space round-trip");
    check(approxEqual(bodyToBoneModel(pose, 2), pelvis * spine * chest, 1.0e-5f),
          "body 2 model-space round-trip");

    const Transform rootToEntity = inverse(pelvisWorld) * entity;
    check(approxEqual(entityFromRoot(pose, 0, rootToEntity), entity, 1.0e-5f), "root_to_entity convention");
}

void testInertiaDiagonalization()
{
    // A rotated, non-diagonal inertia tensor. R diag R^T.
    const Quat rotation = fromAxisAngle(makeVec3(0.3f, 0.8f, -0.5f), 0.9f);
    const Vec3 principal = makeVec3(0.4f, 1.3f, 2.2f);
    const Vec3 ex = rotate(rotation, makeVec3(1.0f, 0.0f, 0.0f));
    const Vec3 ey = rotate(rotation, makeVec3(0.0f, 1.0f, 0.0f));
    const Vec3 ez = rotate(rotation, makeVec3(0.0f, 0.0f, 1.0f));

    auto axisContribution = [](const Vec3& axis, float value) {
        return Vec3{axis.x * axis.x * value, axis.x * axis.y * value, axis.x * axis.z * value};
    };
    const Vec3 cx = axisContribution(ex, principal.x);
    const Vec3 cy = axisContribution(ey, principal.y);
    const Vec3 cz = axisContribution(ez, principal.z);

    float inertia[6] = {
        cx.x + cy.x + cz.x,
        cx.y + cy.y + cz.y,
        cx.z + cy.z + cz.z,
        ex.y * ex.y * principal.x + ey.y * ey.y * principal.y + ez.y * ez.y * principal.z,
        ex.y * ex.z * principal.x + ey.y * ey.z * principal.y + ez.y * ez.z * principal.z,
        ex.z * ex.z * principal.x + ey.z * ey.z * principal.y + ez.z * ez.z * principal.z};

    Vec3 eigenvalues{};
    Quat axes{};
    std::string error;
    check(diagonalizeInertia(inertia, eigenvalues, axes, &error), "inertia diagonalization succeeds");
    check(eigenvalues.x > 0.0f && eigenvalues.y > eigenvalues.x && eigenvalues.z > eigenvalues.y,
          "principal moments are sorted positive");

    const Vec3 rx = rotate(axes, makeVec3(1.0f, 0.0f, 0.0f));
    const Vec3 ry = rotate(axes, makeVec3(0.0f, 1.0f, 0.0f));
    const Vec3 rz = rotate(axes, makeVec3(0.0f, 0.0f, 1.0f));
    const float reconstructedXX =
        rx.x * rx.x * eigenvalues.x + ry.x * ry.x * eigenvalues.y + rz.x * rz.x * eigenvalues.z;
    const float reconstructedYY =
        rx.y * rx.y * eigenvalues.x + ry.y * ry.y * eigenvalues.y + rz.y * rz.y * eigenvalues.z;
    check(std::abs(reconstructedXX - inertia[0]) < 1.0e-3f, "inertia reconstruction xx");
    check(std::abs(reconstructedYY - inertia[3]) < 1.0e-3f, "inertia reconstruction yy");

    // Permuted diagonal moments exercise an odd eigenvector permutation (det -1).
    float diagonal[6] = {1.0f, 0.0f, 0.0f, 3.0f, 0.0f, 2.0f};
    Vec3 diagonalEigenvalues{};
    Quat diagonalAxes{};
    check(diagonalizeInertia(diagonal, diagonalEigenvalues, diagonalAxes, &error),
          "permuted diagonal inertia diagonalizes");
    check(std::abs(diagonalEigenvalues.x - 1.0f) < 1.0e-4f && std::abs(diagonalEigenvalues.y - 2.0f) < 1.0e-4f &&
              std::abs(diagonalEigenvalues.z - 3.0f) < 1.0e-4f,
          "permuted diagonal moments are sorted");
    const Vec3 dx = rotate(diagonalAxes, makeVec3(1.0f, 0.0f, 0.0f));
    const Vec3 dy = rotate(diagonalAxes, makeVec3(0.0f, 1.0f, 0.0f));
    const Vec3 dz = rotate(diagonalAxes, makeVec3(0.0f, 0.0f, 1.0f));
    const float reconstructedDiagonalXX =
        dx.x * dx.x * diagonalEigenvalues.x + dy.x * dy.x * diagonalEigenvalues.y +
        dz.x * dz.x * diagonalEigenvalues.z;
    check(std::abs(reconstructedDiagonalXX - diagonal[0]) < 1.0e-4f,
          "permuted inertia reconstruction uses the corrected eigenvector sign");
    const float determinant = dx.x * (dy.y * dz.z - dy.z * dz.y) -
                              dy.x * (dx.y * dz.z - dx.z * dz.y) +
                              dz.x * (dx.y * dy.z - dx.z * dy.y);
    check(determinant > 0.99f, "permuted inertia eigenbasis is a proper rotation");
}

RagdollDescription makeMinimalDescription()
{
    RagdollDescription description;
    description.skeletonName = "unit_test";
    description.rootBody = 0;

    RagdollBoneDesc bone;
    bone.id = 0;
    bone.parent = kInvalidBone;
    bone.name = "root";
    bone.visible = true;
    bone.disposition = BoneDisposition::Body;
    bone.body = 0;
    bone.bindLocal = Transform{};
    description.bones.push_back(bone);

    RagdollBodyDesc body;
    body.id = 0;
    body.boneId = 0;
    body.parentBody = kInvalidBody;
    body.role = BodyRole::Dynamic;
    body.mass = 1.0f;
    body.centerOfMass = makeVec3(0.0f, 0.0f, 0.0f);
    body.inertia[0] = 0.1f;
    body.inertia[3] = 0.1f;
    body.inertia[5] = 0.1f;
    body.bindPose = Transform{};
    RagdollShapeDesc shape;
    shape.type = ShapeType::Box;
    shape.halfExtents = makeVec3(0.1f, 0.2f, 0.3f);
    shape.local = Transform{};
    body.shapes.push_back(shape);
    description.bodies.push_back(body);
    return description;
}

void testValidation()
{
    RagdollDescription description = makeMinimalDescription();
    std::string error;
    check(validateDescription(description, &error), "minimal description validates");

    RagdollInitialState initial;
    initial.entityWorld = Transform{};
    initial.rootToEntity = Transform{};
    initial.bodyWorld.push_back(Transform{});
    initial.linearVelocity.push_back(Vec3{});
    initial.angularVelocity.push_back(Vec3{});
    initial.enabled.push_back(1);
    check(validateInitialState(description, initial, &error), "minimal initial state validates");

    RagdollBoneDesc merged;
    merged.id = 1;
    merged.parent = 0;
    merged.name = "merged";
    merged.disposition = BoneDisposition::MergedIntoParent;
    merged.body = 0;
    description.bones.push_back(merged);

    RagdollBoneDesc invisible;
    invisible.id = 2;
    invisible.parent = 0;
    invisible.name = "invisible";
    invisible.visible = false;
    invisible.disposition = BoneDisposition::SkippedInvisible;
    description.bones.push_back(invisible);
    check(validateDescription(description, &error), "merged and invisible bone contract validates");

    description.bones[2].parent = 2;
    check(!validateDescription(description, &error), "cyclic bone hierarchy is rejected");

    description = makeMinimalDescription();
    description.bodies[0].mass = 0.0f;
    check(!validateDescription(description, &error), "dynamic body with zero mass is rejected");

    description = makeMinimalDescription();
    initial.bodyWorld.clear();
    check(!validateInitialState(description, initial, &error), "pose/body count mismatch is rejected");
}

void testJsonRoundTrip()
{
    RagdollCaptureDocument document;
    document.description = makeMinimalDescription();
    document.visual = "unit_test_visual";
    document.ogsrCommit = "0000000";
    document.initial.entityWorld = Transform{fromAxisAngle(makeVec3(0.0f, 1.0f, 0.0f), 0.3f), makeVec3(1.0f, 2.0f, 3.0f)};
    document.initial.rootToEntity = Transform{};
    document.initial.bodyWorld.push_back(document.initial.entityWorld);
    document.initial.linearVelocity.push_back(makeVec3(0.25f, -0.5f, 0.75f));
    document.initial.angularVelocity.push_back(makeVec3(0.1f, 0.2f, 0.3f));
    document.initial.enabled.push_back(1);

    std::string json;
    std::string error;
    check(writeRagdollCapture(document, json, &error), "capture document writes JSON");
    check(!json.empty(), "JSON output is not empty");

    RagdollCaptureDocument parsed;
    check(parseRagdollCapture(json, parsed, &error), "capture document parses JSON");
    if (!error.empty())
        std::printf("  JSON error: %s\n", error.c_str());
    check(parsed.description.bodies.size() == document.description.bodies.size(), "JSON body count round-trip");
    check(parsed.description.bones.size() == document.description.bones.size(), "JSON bone count round-trip");
    check(parsed.initial.bodyWorld.size() == 1, "JSON initial pose count");
    check(approxEqual(parsed.initial.entityWorld, document.initial.entityWorld, 1.0e-5f),
          "JSON entity transform round-trip");
    check(approxEqual(parsed.initial.bodyWorld[0], document.initial.bodyWorld[0], 1.0e-5f),
          "JSON body transform round-trip");
    check(parsed.visual == document.visual, "JSON source visual round-trip");

    check(!parseRagdollCapture("{", parsed, &error), "malformed JSON is rejected");
    check(error.find("line") != std::string::npos, "malformed JSON error includes line/column");

    std::string invalid = json;
    const std::string mass = "\"mass\":1";
    const auto massPosition = invalid.find(mass);
    check(massPosition != std::string::npos, "mass field found for mutation");
    if (massPosition != std::string::npos)
        invalid.replace(massPosition, mass.size(), "\"mass\":0");
    check(!parseRagdollCapture(invalid, parsed, &error), "semantically invalid JSON is rejected");
    check(error.find("mass") != std::string::npos, "semantic error names the invalid field");

    invalid = json;
    const std::string rootBody = "\"rootBody\":0";
    const auto rootPosition = invalid.find(rootBody);
    check(rootPosition != std::string::npos, "rootBody field found for mutation");
    if (rootPosition != std::string::npos)
        invalid.replace(rootPosition, rootBody.size(), "\"rootBody\":0.5");
    check(!parseRagdollCapture(invalid, parsed, &error), "fractional integer id is rejected");

    invalid = json;
    const std::string rotation = "\"rotation\":[0,0,0,1]";
    const auto rotationPosition = invalid.find(rotation);
    check(rotationPosition != std::string::npos, "rotation field found for mutation");
    if (rotationPosition != std::string::npos)
        invalid.replace(rotationPosition, rotation.size(), "\"rotation\":[0,0,0,2]");
    check(!parseRagdollCapture(invalid, parsed, &error), "non-unit quaternion is rejected");
    check(error.find("unit") != std::string::npos || error.find("rotation") != std::string::npos,
          "non-unit quaternion error names the field");

    RagdollCaptureDocument inconsistent = document;
    inconsistent.initial.rootToEntity.translation.x += 1.0f;
    check(!writeRagdollCapture(inconsistent, json, &error), "inconsistent rootToEntity is rejected");

    const std::string deeplyNested(200, '[');
    check(!parseRagdollCapture(deeplyNested, parsed, &error), "deep JSON nesting is rejected");
    check(error.find("deep") != std::string::npos, "deep nesting error is explicit");
}

} // namespace

int main()
{
    testTransformComposition();
    testRowMatrixBridge();
    testBindHierarchy();
    testInertiaDiagonalization();
    testValidation();
    testJsonRoundTrip();

    std::printf("physics_portable tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
