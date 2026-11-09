#include "physics_physx/physx_ragdoll_c.h"

#include "physics_portable/ragdoll.h"
#include "physics_portable/row_matrix.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

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

void storeRow(const physics_portable::Transform& transform, float* out)
{
    const physics_portable::RowMatrix4 row = physics_portable::rowMatrixFromTransform(transform);
    std::memcpy(out, row.m, sizeof(row.m));
}

void storeIdentity(float* out)
{
    std::memset(out, 0, 16 * sizeof(float));
    out[0] = 1.0f;
    out[5] = 1.0f;
    out[10] = 1.0f;
    out[15] = 1.0f;
}
} // namespace

int main()
{
    const OgsrPhysXRagdollConfig config{
        0.01f, -19.62f, 2, 4, 8, 2, 0.02f, 0.0f, 1, 300.0f, 1, 0.05f, 0.2f, 1, 1};

    char error[512] = {};
    OgsrPhysXRuntimeHandle runtime = OgsrPhysXRagdollRuntimeCreate(&config, error, sizeof(error));
    check(runtime != nullptr, error);

    const float vertices[] = {
        -5.0f, 0.0f, -5.0f,
        5.0f, 0.0f, -5.0f,
        5.0f, 0.0f, 5.0f,
        -5.0f, 0.0f, 5.0f};
    const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
    check(OgsrPhysXRagdollSetStaticMesh(runtime, vertices, 4, indices, 6, error, sizeof(error)) == 1, error);

    OgsrPhysXRagdollBone bone{};
    bone.id = 0;
    bone.parent = 0xFFFF;
    bone.visible = 1;
    bone.disposition = static_cast<uint8_t>(physics_portable::BoneDisposition::Body);
    bone.body = 0;
    storeIdentity(bone.bindLocal);
    std::snprintf(bone.name, sizeof(bone.name), "root");

    OgsrPhysXRagdollShape shape{};
    shape.type = static_cast<uint8_t>(physics_portable::ShapeType::Box);
    storeIdentity(shape.local);
    shape.halfExtents[0] = 0.25f;
    shape.halfExtents[1] = 0.25f;
    shape.halfExtents[2] = 0.25f;
    shape.margin = 0.0f;

    OgsrPhysXRagdollBody body{};
    body.id = 0;
    body.boneId = 0;
    body.parentBody = 0xFFFF;
    body.role = static_cast<uint8_t>(physics_portable::BodyRole::Dynamic);
    body.mass = 1.0f;
    body.firstShape = 0;
    body.shapeCount = 1;
    storeIdentity(body.bindPose);
    const float inertia = 1.0f / 6.0f;
    body.inertia[0] = inertia;
    body.inertia[3] = inertia;
    body.inertia[5] = inertia;

    float bodyWorld[16];
    storeIdentity(bodyWorld);
    bodyWorld[12] = 0.0f;
    bodyWorld[13] = 2.0f;
    bodyWorld[14] = 0.0f;
    float zeroVelocity[3] = {0.0f, 0.0f, 0.0f};
    const uint8_t enabled = 1;

    OgsrPhysXRagdollDefinition definition{};
    definition.rootBody = 0;
    definition.boneCount = 1;
    definition.bodyCount = 1;
    definition.jointCount = 0;
    definition.shapeCount = 1;
    definition.bones = &bone;
    definition.bodies = &body;
    definition.shapes = &shape;
    const physics_portable::Transform entityWorld{};
    physics_portable::Transform rootBodyWorld{};
    rootBodyWorld.translation = physics_portable::Vec3{0.0f, 2.0f, 0.0f};
    const physics_portable::Transform rootToEntity =
        physics_portable::inverse(rootBodyWorld) * entityWorld;
    storeRow(entityWorld, definition.entityWorld);
    storeRow(rootToEntity, definition.rootToEntity);
    definition.initialBodyWorld = bodyWorld;
    definition.initialLinearVelocity = zeroVelocity;
    definition.initialAngularVelocity = zeroVelocity;
    definition.initialEnabled = &enabled;

    check(OgsrPhysXRagdollCreate(runtime, &definition, error, sizeof(error)) == 1, error);

    OgsrPhysXRagdollInfo info{};
    check(OgsrPhysXRagdollGetInfo(runtime, &info) == 1, "info");
    check(info.bodyCount == 1, "body count");
    check(info.jointCount == 0, "joint count");
    check(info.staticTriangles == 2, "static triangle count");

    for (int frame = 0; frame < 120; ++frame)
        check(OgsrPhysXRagdollStep(runtime, 0.02f, error, sizeof(error)) == 1, error);

    OgsrPhysXRagdollPose pose{};
    float poseBodies[16] = {};
    check(OgsrPhysXRagdollGetPose(runtime, &pose, poseBodies, 1) == 1, "pose");
    check(pose.bodyCount == 1, "pose body count");
    const float settledY = poseBodies[13];
    check(std::isfinite(settledY), "finite settled y");
    check(settledY > -1.0f && settledY < 1.0f, "box stayed on the static mesh");

    check(OgsrPhysXRagdollGetPose(runtime, &pose, nullptr, 0) == 0 && pose.bodyCount == 1,
          "small pose buffer reports the required size");

    OgsrPhysXRagdollBodyMass bodyMass{};
    check(OgsrPhysXRagdollGetBodyMass(runtime, 0, &bodyMass, error, sizeof(error)) == 1, error);
    check(bodyMass.body == 0, "body mass keeps the requested body ID");
    check(bodyMass.mass > 0.0f, "body mass is positive");
    check(std::isfinite(bodyMass.worldCenterOfMass[0]) && std::isfinite(bodyMass.worldCenterOfMass[1]) &&
              std::isfinite(bodyMass.worldCenterOfMass[2]),
          "body mass world COM is finite");
    check(OgsrPhysXRagdollGetBodyMass(runtime, 7, &bodyMass, error, sizeof(error)) == 0,
          "body mass rejects an out-of-range body");
    const float centralImpulse[3] = {0.0f, 1.0f, 0.0f};
    check(OgsrPhysXRagdollApplyCentralImpulse(runtime, 0, centralImpulse, error, sizeof(error)) == 1, error);

    const float impulsePosition[3] = {0.0f, settledY, 0.0f};
    const float impulse[3] = {0.0f, 20.0f, 0.0f};
    check(OgsrPhysXRagdollApplyImpulse(runtime, 0, impulsePosition, impulse, error, sizeof(error)) == 1, error);
    for (int frame = 0; frame < 10; ++frame)
        check(OgsrPhysXRagdollStep(runtime, 0.02f, error, sizeof(error)) == 1, error);

    OgsrPhysXRagdollDestroy(runtime);
    check(OgsrPhysXRagdollGetInfo(runtime, &info) == 1 && info.bodyCount == 0, "ragdoll destroy");
    check(OgsrPhysXRagdollCreate(runtime, &definition, error, sizeof(error)) == 1, "recreate after destroy");

    OgsrPhysXRagdollRuntimeDestroy(runtime);

    std::printf("physics_physx_ragdoll_abi tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
