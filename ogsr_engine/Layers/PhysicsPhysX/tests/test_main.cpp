#include "humanoid_fixture.h"

#include "physics_physx/physx_ragdoll_world.h"
#include "physics_physx/physx_runtime.h"
#include "physics_portable/json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

using namespace physics_portable;
using physics_physx::PhysXRagdollWorld;

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

void checkFinite(const RagdollPose& pose, const char* message)
{
    for (const Transform& transform : pose.bodyWorld)
        check(isFinite(transform), message);
}

float minBodyY(const RagdollPose& pose)
{
    float value = std::numeric_limits<float>::max();
    for (const Transform& transform : pose.bodyWorld)
        value = std::min(value, transform.translation.y);
    return value;
}

float angleAroundZ(const Quat& rotation)
{
    const Quat q = normalized(rotation);
    return 2.0f * std::atan2(q.z, q.w);
}

RagdollSimulationConfig testConfig()
{
    RagdollSimulationConfig config{};
    config.gravity = makeVec3(0.0f, -19.62f, 0.0f);
    config.fixedTimestep = 0.01f;
    config.maxSubsteps = 4;
    config.contactOffset = 0.01f;
    config.restOffset = 0.0f;
    config.solverPositionIterations = 12;
    config.solverVelocityIterations = 4;
    return config;
}

void testHumanoidDrop()
{
    const RagdollDescription description = physics_physx_tests::makeHumanoidDescription(SwingLimitMode::Pyramid);
    const RagdollInitialState initial = physics_physx_tests::makeHumanoidInitialState(description);
    std::string error;
    check(validateDescription(description, &error), "humanoid description validates");
    check(validateInitialState(description, initial, &error), "humanoid initial state validates");

    PhysXRagdollWorld world(testConfig());
    check(world.create(description, initial, &error), "humanoid world creates");
    if (!world.isCreated())
    {
        std::printf("  create error: %s\n", error.c_str());
        return;
    }

    check(world.bodyCount() == description.bodies.size(), "all bodies created");
    check(world.jointCount() == description.joints.size(), "all joints created");

    const RagdollPose creationPose = world.pose();
    check(approxEqual(creationPose.entityWorld, initial.entityWorld, 1.0e-4f),
          "rootToEntity reproduces the initial entity transform");
    check(approxEqual(bodyToBoneModel(creationPose, 0), description.bodies[0].bindPose, 1.0e-4f),
          "creation pose maps root body back to model space");

    check(world.stepFixed(10, &error), "humanoid steps 10 fixed ticks");
    RagdollPose pose = world.pose();
    check(pose.stepIndex == 10, "step index increments");
    check(pose.bodyWorld.size() == description.bodies.size(), "pose body count");
    checkFinite(pose, "pose is finite after 10 steps");
    check(pose.bodyWorld[0].translation.y > 0.0f && pose.bodyWorld[0].translation.y < 1.2f,
          "root body stays in a plausible range");
    check(pose.bodyWorld[0].translation.y < initial.bodyWorld[0].translation.y,
          "pelvis falls under gravity");

    check(world.stepFixed(120, &error), "humanoid steps 120 more fixed ticks");
    pose = world.pose();
    check(pose.stepIndex == 130, "step index after 130 ticks");
    checkFinite(pose, "pose is finite after 130 steps");
    check(minBodyY(pose) > -0.5f, "no body falls far below the floor");
    check(minBodyY(pose) < 0.5f, "ragdoll reaches the floor");
    std::printf("  humanoid: root.y=%.3f min.y=%.3f\n", pose.bodyWorld[0].translation.y, minBodyY(pose));

    const std::size_t allocationsBeforeDestroy = world.outstandingAllocations();
    check(allocationsBeforeDestroy > 0, "PhysX allocated tracked memory");
    world.destroy();
    check(!world.isCreated(), "world is destroyed");
    check(world.outstandingAllocations() == 0, "all tracked PhysX allocations are released");
}

void testInitialVelocityHandoff()
{
    const RagdollDescription description = physics_physx_tests::makeHumanoidDescription(SwingLimitMode::Pyramid);
    const RagdollInitialState initial = physics_physx_tests::makeHumanoidInitialState(description);
    PhysXRagdollWorld world(testConfig());
    std::string error;
    check(world.create(description, initial, &error), "velocity world creates");
    if (!world.isCreated())
        return;

    const RagdollPose before = world.pose();
    check(world.stepFixed(5, &error), "velocity world steps");
    const RagdollPose after = world.pose();
    check(after.bodyWorld[0].translation.x > before.bodyWorld[0].translation.x + 0.002f,
          "initial linear velocity moves the root in +X");
    check(after.bodyWorld[0].translation.y < before.bodyWorld[0].translation.y,
          "gravity moves the root down");
}

void testImpulse()
{
    const RagdollDescription description = physics_physx_tests::makeHumanoidDescription(SwingLimitMode::Pyramid);
    const RagdollInitialState initial = physics_physx_tests::makeHumanoidInitialState(description);
    PhysXRagdollWorld world(testConfig());
    std::string error;
    check(world.create(description, initial, &error), "impulse world creates");
    if (!world.isCreated())
        return;

    const RagdollPose before = world.pose();
    const Vec3 chest = before.bodyWorld[2].translation;
    check(world.applyImpulse(2, makeVec3(chest.x, chest.y + 0.2f, chest.z), makeVec3(8.0f, 0.0f, 0.0f), &error),
          "impulse applies to the chest");
    check(world.stepFixed(10, &error), "impulse world steps");
    const RagdollPose after = world.pose();
    checkFinite(after, "pose finite after impulse");
    check(after.bodyWorld[2].translation.x > before.bodyWorld[2].translation.x + 0.005f,
          "chest moves after an impulse");
}

void testAccumulator()
{
    const RagdollDescription description = physics_physx_tests::makeHumanoidDescription(SwingLimitMode::Pyramid);
    const RagdollInitialState initial = physics_physx_tests::makeHumanoidInitialState(description);
    PhysXRagdollWorld world(testConfig());
    std::string error;
    check(world.create(description, initial, &error), "accumulator world creates");
    if (!world.isCreated())
        return;

    check(world.step(0.005f, &error), "sub-step does not fail");
    check(world.pose().stepIndex == 0, "sub-step does not simulate");
    check(world.step(0.005f, &error), "second sub-step completes a fixed step");
    check(world.pose().stepIndex == 1, "accumulator executes one fixed step");
}

void testInvalidDescription()
{
    RagdollDescription description = physics_physx_tests::makeHumanoidDescription(SwingLimitMode::Pyramid);
    RagdollInitialState initial = physics_physx_tests::makeHumanoidInitialState(description);
    description.bodies[0].mass = 0.0f;
    std::string error;
    PhysXRagdollWorld world(testConfig());
    check(!world.create(description, initial, &error), "invalid dynamic mass is rejected");
    check(!error.empty(), "invalid description returns an error message");
    check(world.outstandingAllocations() == 0, "invalid description leaves no allocations");
}

void testRecreateLifetime()
{
    for (int iteration = 0; iteration < 5; ++iteration)
    {
        const RagdollDescription description = physics_physx_tests::makeHumanoidDescription(SwingLimitMode::Pyramid);
        const RagdollInitialState initial = physics_physx_tests::makeHumanoidInitialState(description);
        PhysXRagdollWorld world(testConfig());
        std::string error;
        check(world.create(description, initial, &error), "recreate world creates");
        if (!world.isCreated())
        {
            std::printf("  recreate error: %s\n", error.c_str());
            return;
        }
        check(world.stepFixed(20, &error), "recreate world steps");
        world.destroy();
        check(world.outstandingAllocations() == 0, "recreate releases all tracked allocations");
    }
}

float runPendulum(SwingLimitMode mode)
{
    const RagdollDescription description = physics_physx_tests::makePendulumDescription(mode);
    const RagdollInitialState initial = physics_physx_tests::makePendulumInitialState(description);
    PhysXRagdollWorld world(testConfig());
    std::string error;
    if (!world.create(description, initial, &error))
    {
        std::printf("  pendulum create error (%s): %s\n", mode == SwingLimitMode::Pyramid ? "pyramid" : "cone",
                    error.c_str());
        return std::numeric_limits<float>::quiet_NaN();
    }

    const Vec3 position = world.pose().bodyWorld[1].translation;
    world.applyImpulse(1, makeVec3(position.x, position.y + 0.4f, position.z), makeVec3(2.0f, 0.0f, 0.0f), &error);
    for (int step = 0; step < 120; ++step)
    {
        // Keep pushing into the asymmetric Z limit so the cone/pyramid
        // difference is observable instead of being sampled during a neutral swing.
        world.applyTorque(1, makeVec3(0.0f, 0.0f, -0.15f), &error);
        if (!world.stepFixed(1, &error))
        {
            std::printf("  pendulum step error: %s\n", error.c_str());
            return std::numeric_limits<float>::quiet_NaN();
        }
    }
    const RagdollPose pose = world.pose();
    checkFinite(pose, "pendulum pose is finite");
    const float angle = angleAroundZ(pose.bodyWorld[1].rotation);
    std::printf("  pendulum %s: z-angle=%.4f\n", mode == SwingLimitMode::Pyramid ? "pyramid" : "cone", angle);
    return angle;
}

void testCylinderMargins()
{
    for (float margin : {0.0f, 0.005f, 0.02f})
    {
        RagdollDescription description;
        description.skeletonName = "cylinder_margin";
        description.rootBody = 0;

        RagdollMaterialDesc material;
        material.id = 0;
        material.friction = 0.8f;
        description.materials.push_back(material);

        RagdollBoneDesc bone;
        bone.id = 0;
        bone.name = "cylinder";
        bone.disposition = BoneDisposition::Body;
        bone.body = 0;
        description.bones.push_back(bone);

        RagdollBodyDesc body;
        body.id = 0;
        body.boneId = 0;
        body.role = BodyRole::Dynamic;
        body.mass = 1.0f;
        body.bindPose = makeTransform(Quat{}, makeVec3(0.0f, 2.0f, 0.0f));
        const float radius = 0.1f;
        const float height = 0.5f;
        body.inertia[0] = 0.5f * body.mass * radius * radius;
        body.inertia[3] = body.mass / 12.0f * (3.0f * radius * radius + height * height);
        body.inertia[5] = body.inertia[3];
        RagdollShapeDesc shape;
        shape.type = ShapeType::Cylinder;
        shape.radius = radius;
        shape.height = height;
        shape.margin = margin;
        shape.materialId = 0;
        shape.sourceBoneId = 0;
        body.shapes.push_back(shape);
        description.bodies.push_back(body);

        RagdollInitialState initial;
        initial.entityWorld = Transform{};
        initial.bodyWorld.push_back(body.bindPose);
        initial.rootToEntity = inverse(initial.bodyWorld[0]);
        initial.linearVelocity.push_back(Vec3{});
        initial.angularVelocity.push_back(Vec3{});
        initial.enabled.push_back(1);

        PhysXRagdollWorld world(testConfig());
        std::string error;
        const bool created = world.create(description, initial, &error);
        check(created, "cylinder world creates");
        if (!created)
        {
            std::printf("  cylinder margin %f error: %s\n", margin, error.c_str());
            continue;
        }
        check(world.stepFixed(160, &error), "cylinder world steps");
        const RagdollPose pose = world.pose();
        checkFinite(pose, "cylinder pose finite");
        const float y = pose.bodyWorld[0].translation.y;
        const float penetration = (0.1f + margin) - y;
        std::printf("  cylinder margin %.3f: center.y=%.4f penetration=%.4f\n", margin, y, penetration);
        check(y > 0.05f && y < 0.8f, "cylinder does not tunnel through the floor");
        check(penetration < 0.04f, "cylinder floor penetration stays below the critical threshold");
    }
}

void testJsonLoadedHumanoid(){
    RagdollCaptureDocument document;
    document.description = physics_physx_tests::makeHumanoidDescription(SwingLimitMode::Pyramid);
    document.initial = physics_physx_tests::makeHumanoidInitialState(document.description);
    document.visual = "synthetic_humanoid";
    document.ogsrCommit = "p1-test";

    std::string json;
    std::string error;
    check(writeRagdollCapture(document, json, &error), "humanoid capture writes JSON");

    RagdollCaptureDocument loaded;
    check(parseRagdollCapture(json, loaded, &error), "humanoid capture parses JSON");
    check(loaded.description.bodies.size() == document.description.bodies.size(), "loaded body count");
    check(loaded.description.joints.size() == document.description.joints.size(), "loaded joint count");

    PhysXRagdollWorld world(testConfig());
    check(world.create(loaded.description, loaded.initial, &error), "PhysX world accepts JSON-loaded fixture");
    if (world.isCreated())
    {
        check(world.stepFixed(30, &error), "JSON-loaded PhysX ragdoll steps");
        checkFinite(world.pose(), "JSON-loaded pose is finite");
    }
}

void testRuntimeContext()
{
    physics_physx::RuntimeConfig config;
    config.fixedTimestep = 0.01f;
    config.gravityY = -19.62f;
    config.workerThreads = 2;
    config.maxSubsteps = 4;

    physics_physx::RuntimeStaticMesh floor;
    floor.vertices = {
        makeVec3(-5.0f, 0.0f, -5.0f),
        makeVec3(5.0f, 0.0f, -5.0f),
        makeVec3(5.0f, 0.0f, 5.0f),
        makeVec3(-5.0f, 0.0f, 5.0f),
    };
    floor.indices = {0, 1, 2, 0, 2, 3};

    physics_physx::PhysXRuntimeContext context;
    std::string error;
    check(context.initialize(config, &error), "PhysX runtime context initializes");
    if (!context.isInitialized())
    {
        std::printf("  runtime init error: %s\n", error.c_str());
        return;
    }
    check(context.setStaticMesh(floor, &error), "PhysX runtime accepts a static triangle mesh");
    check(context.staticTriangleCount() == 2, "PhysX runtime reports two static triangles");
    check(context.createTestBox(makeVec3(0.0f, 2.0f, 0.0f), makeVec3(0.25f, 0.25f, 0.25f), 100.0f, &error),
          "PhysX runtime creates a test box");
    check(context.dynamicBodyCount() == 1, "PhysX runtime reports one dynamic test body");

    bool stepped = true;
    for (int i = 0; i < 160; ++i)
    {
        if (!context.step(0.01f, &error))
        {
            stepped = false;
            std::printf("  runtime step error: %s\n", error.c_str());
            break;
        }
    }
    check(stepped, "PhysX runtime steps fixed frames");
    const physics_physx::RuntimeTestBodyState state = context.testBodyState();
    const float y = state.pose.translation.y;
    std::printf("  runtime box: y=%.4f contacts=%u steps=%u\n", y, state.contactCount, state.stepCount);
    check(state.stepCount >= 160, "PhysX runtime advanced fixed steps");
    check(state.hasContact && state.contactCount > 0, "PhysX test box contacts the static surface");
    check(y > 0.15f && y < 1.0f, "PhysX test box rests on the static surface");

    context.shutdown();
    check(!context.isInitialized(), "PhysX runtime context shuts down");
}

void testRealCaptureFixture()
{
    const char* path = std::getenv("OGSR_RAGDOLL_FIXTURE");
    if (!path || !*path)
    {
        std::printf("  real capture fixture: skipped (OGSR_RAGDOLL_FIXTURE not set)\n");
        return;
    }

    std::ifstream input(path, std::ios::binary);
    check(input.good(), "real capture fixture opens");
    if (!input.good())
        return;
    const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    check(!json.empty(), "real capture fixture is not empty");

    RagdollCaptureDocument document;
    std::string error;
    check(parseRagdollCapture(json, document, &error), "real capture fixture parses");
    if (!error.empty())
        std::printf("  real capture parse error: %s\n", error.c_str());
    if (!error.empty())
        return;

    std::printf("  real capture: bones=%zu bodies=%zu joints=%zu\n", document.description.bones.size(),
                document.description.bodies.size(), document.description.joints.size());
    check(!document.description.bones.empty(), "real capture has bones");
    check(!document.description.bodies.empty(), "real capture has bodies");
    check(!document.description.joints.empty(), "real capture has joints");
    check(validateDescription(document.description, &error), "real capture description validates");
    check(validateInitialState(document.description, document.initial, &error),
          "real capture initial state validates");
    if (!error.empty())
        std::printf("  real capture validation error: %s\n", error.c_str());

    for (const RagdollBodyDesc& body : document.description.bodies)
    {
        if (body.role != BodyRole::Dynamic && body.role != BodyRole::FreeBody)
            continue;
        Vec3 eigenvalues{};
        Quat axes{};
        check(diagonalizeInertia(body.inertia, eigenvalues, axes, &error),
              "real capture dynamic inertia is positive definite");
        check(eigenvalues.x > 0.0f && eigenvalues.y > 0.0f && eigenvalues.z > 0.0f,
              "real capture principal inertia is positive");
    }

    PhysXRagdollWorld world(testConfig());
    check(world.create(document.description, document.initial, &error), "real capture creates in PhysX");
    if (!world.isCreated())
    {
        std::printf("  real capture create error: %s\n", error.c_str());
        return;
    }

    check(world.bodyCount() == document.description.bodies.size(), "real capture body count");
    check(world.jointCount() == document.description.joints.size(), "real capture joint count");
    const RagdollPose creation = world.pose();
    check(approxEqual(creation.entityWorld, document.initial.entityWorld, 1.0e-3f),
          "real capture entity transform readback");
    for (BodyId body = 0; body < document.description.bodies.size(); ++body)
    {
        check(approxEqual(creation.bodyWorld[body], document.initial.bodyWorld[body], 1.0e-3f),
              "real capture body pose readback");
        const Transform model = bodyToBoneModel(creation, body);
        const Transform expected = inverse(document.initial.entityWorld) * document.initial.bodyWorld[body];
        check(approxEqual(model, expected, 1.0e-3f), "real capture body-to-bone readback");
    }

    check(world.stepFixed(120, &error), "real capture steps 120 fixed ticks");
    const RagdollPose pose = world.pose();
    checkFinite(pose, "real capture pose is finite");
    for (BodyId body = 0; body < document.description.bodies.size(); ++body)
        check(isFinite(bodyToBoneModel(pose, body)), "real capture bone-space pose is finite");

    world.destroy();
    check(world.outstandingAllocations() == 0, "real capture releases all PhysX allocations");
}

void testCylinderOrientation()
{
    for (float margin : {0.0f, 0.01f, 0.02f})
    {
        float horizontalY = 0.0f;
        float verticalY = 0.0f;
        for (bool vertical : {false, true})
        {
            RagdollDescription description;
            description.skeletonName = vertical ? "cylinder_vertical" : "cylinder_horizontal";
            description.rootBody = 0;

            RagdollMaterialDesc material;
            material.id = 0;
            material.friction = 0.8f;
            description.materials.push_back(material);

            RagdollBoneDesc cylinderBone;
            cylinderBone.id = 0;
            cylinderBone.name = "support";
            cylinderBone.disposition = BoneDisposition::Body;
            cylinderBone.body = 0;
            description.bones.push_back(cylinderBone);

            RagdollBodyDesc cylinder;
            cylinder.id = 0;
            cylinder.boneId = 0;
            cylinder.role = BodyRole::StaticRoot;
            cylinder.isFixed = true;
            cylinder.bindPose = Transform{};
            RagdollShapeDesc cylinderShape;
            cylinderShape.type = ShapeType::Cylinder;
            cylinderShape.radius = 0.1f;
            cylinderShape.height = 0.5f;
            cylinderShape.margin = margin;
            cylinderShape.materialId = 0;
            cylinderShape.sourceBoneId = 0;
            if (vertical)
                cylinderShape.local.rotation = fromAxisAngle(makeVec3(0.0f, 0.0f, 1.0f), 3.14159265f * 0.5f);
            cylinder.shapes.push_back(cylinderShape);
            description.bodies.push_back(cylinder);

            RagdollBoneDesc sphereBone;
            sphereBone.id = 1;
            sphereBone.name = "probe";
            sphereBone.disposition = BoneDisposition::Body;
            sphereBone.body = 1;
            description.bones.push_back(sphereBone);

            RagdollBodyDesc sphere;
            sphere.id = 1;
            sphere.boneId = 1;
            sphere.role = BodyRole::Dynamic;
            sphere.mass = 1.0f;
            sphere.bindPose = makeTransform(Quat{}, makeVec3(0.0f, 1.0f, 0.0f));
            const float sphereRadius = 0.05f;
            const float sphereInertia = 0.4f * sphere.mass * sphereRadius * sphereRadius;
            sphere.inertia[0] = sphereInertia;
            sphere.inertia[3] = sphereInertia;
            sphere.inertia[5] = sphereInertia;
            RagdollShapeDesc sphereShape;
            sphereShape.type = ShapeType::Sphere;
            sphereShape.radius = sphereRadius;
            sphereShape.materialId = 0;
            sphereShape.sourceBoneId = 1;
            sphere.shapes.push_back(sphereShape);
            description.bodies.push_back(sphere);

            RagdollInitialState initial;
            initial.entityWorld = Transform{};
            initial.bodyWorld.push_back(Transform{});
            initial.bodyWorld.push_back(sphere.bindPose);
            initial.rootToEntity = Transform{};
            initial.linearVelocity.assign(2, Vec3{});
            initial.angularVelocity.assign(2, Vec3{});
            initial.enabled.assign(2, 1);

            PhysXRagdollWorld world(testConfig());
            std::string error;
            if (!world.create(description, initial, &error))
            {
                std::printf("  cylinder orientation create error: %s\n", error.c_str());
                check(false, "cylinder orientation world creates");
                continue;
            }
            check(world.stepFixed(300, &error), "cylinder orientation world steps");
            const RagdollPose pose = world.pose();
            checkFinite(pose, "cylinder orientation pose finite");
            const float y = pose.bodyWorld[1].translation.y;
            if (vertical)
                verticalY = y;
            else
                horizontalY = y;
        }
        std::printf("  cylinder orientation margin %.3f: horizontal=%.4f vertical=%.4f\n", margin, horizontalY,
                    verticalY);
        check(horizontalY > 0.08f, "sphere rests on horizontal cylinder");
        check(verticalY > 0.08f, "sphere rests on vertical cylinder");
        check(verticalY > horizontalY + 0.08f, "cylinder axis orientation changes the support height");
    }
}

void testHingeAxisZ()
{
    RagdollDescription description;
    description.skeletonName = "hinge_z";
    description.rootBody = 0;

    RagdollMaterialDesc material;
    material.id = 0;
    description.materials.push_back(material);

    RagdollBoneDesc rootBone;
    rootBone.id = 0;
    rootBone.name = "pivot";
    rootBone.disposition = BoneDisposition::Body;
    rootBone.body = 0;
    description.bones.push_back(rootBone);

    RagdollBodyDesc root;
    root.id = 0;
    root.boneId = 0;
    root.role = BodyRole::StaticRoot;
    root.isFixed = true;
    description.bodies.push_back(root);

    RagdollBoneDesc childBone;
    childBone.id = 1;
    childBone.name = "arm";
    childBone.parent = 0;
    childBone.bindLocal = makeTransform(Quat{}, makeVec3(0.0f, -1.0f, 0.0f));
    childBone.disposition = BoneDisposition::Body;
    childBone.body = 1;
    description.bones.push_back(childBone);

    RagdollBodyDesc child;
    child.id = 1;
    child.boneId = 1;
    child.parentBody = 0;
    child.role = BodyRole::Dynamic;
    child.mass = 1.0f;
    child.bindPose = childBone.bindLocal;
    const float inertia = 0.4f * 1.0f * 0.2f * 0.2f;
    child.inertia[0] = inertia;
    child.inertia[3] = inertia;
    child.inertia[5] = inertia;
    RagdollShapeDesc sphere;
    sphere.type = ShapeType::Sphere;
    sphere.radius = 0.2f;
    sphere.materialId = 0;
    sphere.sourceBoneId = 1;
    child.shapes.push_back(sphere);
    description.bodies.push_back(child);

    RagdollJointDesc joint;
    joint.id = 0;
    joint.boneId = 1;
    joint.parentBody = 0;
    joint.childBody = 1;
    joint.kind = JointKind::Hinge;
    joint.parentFrame = Transform{};
    joint.childFrame = makeTransform(Quat{}, makeVec3(0.0f, 1.0f, 0.0f));
    joint.axisCount = 1;
    joint.axes[0] = RagdollAxisLimit{AxisSpace::Child, makeVec3(0.0f, 0.0f, 1.0f), -0.6f, 0.6f};
    description.joints.push_back(joint);

    RagdollInitialState initial;
    initial.entityWorld = Transform{};
    initial.bodyWorld.push_back(Transform{});
    initial.bodyWorld.push_back(child.bindPose);
    initial.rootToEntity = Transform{};
    initial.linearVelocity.assign(2, Vec3{});
    initial.angularVelocity.assign(2, Vec3{});
    initial.enabled.assign(2, 1);

    PhysXRagdollWorld world(testConfig());
    std::string error;
    check(world.create(description, initial, &error), "Z-axis hinge world creates");
    if (!world.isCreated())
        return;
    for (int step = 0; step < 120; ++step)
    {
        world.applyTorque(1, makeVec3(0.0f, 0.0f, 0.08f), &error);
        if (!world.stepFixed(1, &error))
            break;
    }
    const RagdollPose pose = world.pose();
    checkFinite(pose, "Z-axis hinge pose finite");
    const float angle = angleAroundZ(pose.bodyWorld[1].rotation);
    std::printf("  hinge axis Z: z-angle=%.4f\n", angle);
    check(std::abs(angle) > 0.05f, "hinge uses the captured Z axis");

    for (int step = 0; step < 80; ++step)
    {
        world.applyTorque(1, makeVec3(0.1f, 0.0f, 0.0f), &error);
        if (!world.stepFixed(1, &error))
            break;
    }
    const Quat lockedRotation = normalized(world.pose().bodyWorld[1].rotation);
    const float lockedAngle = 2.0f * std::atan2(lockedRotation.x, lockedRotation.w);
    std::printf("  hinge axis Z: locked x-angle=%.4f\n", lockedAngle);
    check(std::abs(lockedAngle) < 0.3f, "hinge locks the non-axis X rotation");
}

void testMoveAssignment()
{
    const RagdollDescription description = physics_physx_tests::makeHumanoidDescription(SwingLimitMode::Pyramid);
    const RagdollInitialState initial = physics_physx_tests::makeHumanoidInitialState(description);
    PhysXRagdollWorld target;
    PhysXRagdollWorld source;
    std::string error;
    check(source.create(description, initial, &error), "move test source world creates");
    target = std::move(source);
    check(target.isCreated(), "move assignment transfers ownership");
    check(!source.isCreated(), "move assignment leaves source empty");
    check(target.stepFixed(5, &error), "moved world steps");
    target.destroy();
    check(target.outstandingAllocations() == 0, "move-assigned world releases all allocations");
}

void testNonBindInitialPose()
{
    const RagdollDescription description = physics_physx_tests::makeHumanoidDescription(SwingLimitMode::Pyramid);
    RagdollInitialState initial = physics_physx_tests::makeHumanoidInitialState(description);
    initial.bodyWorld[4] = initial.bodyWorld[4] * makeTransform(fromAxisAngle(makeVec3(0.0f, 1.0f, 0.0f), 0.3f),
                                                               makeVec3(-0.03f, 0.0f, 0.0f));
    initial.bodyWorld[10] = initial.bodyWorld[10] * makeTransform(fromAxisAngle(makeVec3(1.0f, 0.0f, 0.0f), -0.2f),
                                                                  makeVec3(0.0f, -0.02f, 0.0f));

    PhysXRagdollWorld world(testConfig());
    std::string error;
    check(world.create(description, initial, &error), "non-bind initial pose creates");
    if (!world.isCreated())
    {
        std::printf("  non-bind error: %s\n", error.c_str());
        return;
    }
    check(world.stepFixed(60, &error), "non-bind initial pose steps");
    const RagdollPose pose = world.pose();
    checkFinite(pose, "non-bind initial pose remains finite");
    check(minBodyY(pose) > -0.5f, "non-bind initial pose does not explode through the floor");
}

void testConeAndPyramid()
{
    const float cone = runPendulum(SwingLimitMode::Cone);
    const float pyramid = runPendulum(SwingLimitMode::Pyramid);
    check(std::isfinite(cone), "cone pendulum produces a finite angle");
    check(std::isfinite(pyramid), "pyramid pendulum produces a finite angle");
    if (std::isfinite(cone) && std::isfinite(pyramid))
    {
        check(std::abs(pyramid) <= std::abs(cone) + 0.15f,
              "pyramid asymmetric limit does not swing farther than the symmetric cone");
    }
}

void testRejectedNoPhysicsRoot()
{
    RagdollDescription description = physics_physx_tests::makeHumanoidDescription(SwingLimitMode::Pyramid);
    auto& root = description.bodies[description.rootBody];
    root.role = BodyRole::StaticRoot;
    root.isFixed = true;
    root.mass = 0.0f;
    for (RagdollShapeDesc& shape : root.shapes)
        shape.flags |= shapeFlag(ShapeFlag::NoPhysics);

    RagdollSimulationConfig config = testConfig();
    config.promoteStaticRoot = true;
    PhysXRagdollWorld world(config);
    std::string error;
    const bool created = world.create(description, physics_physx_tests::makeHumanoidInitialState(description), &error);
    check(!created, "promoted root with only NoPhysics shapes is rejected");
    check(!error.empty(), "NoPhysics root failure reports an error");
}

void testHinge2Rejected()
{
    RagdollDescription description = physics_physx_tests::makeHumanoidDescription(SwingLimitMode::Pyramid);
    check(!description.joints.empty(), "Hinge2 test has joints");
    description.joints[0].kind = JointKind::Hinge2;
    description.joints[0].axisCount = 1;
    PhysXRagdollWorld world(testConfig());
    std::string error;
    const bool created = world.create(description, physics_physx_tests::makeHumanoidInitialState(description), &error);
    check(!created, "Hinge2 ragdoll is rejected");
    check(!error.empty(), "Hinge2 failure reports an error");
}

RagdollDescription makeSingleBodyDescription()
{
    RagdollDescription description;
    description.rootBody = 0;

    RagdollBoneDesc bone;
    bone.id = 0;
    bone.parent = kInvalidBone;
    bone.name = "root";
    bone.bindLocal = Transform{};
    bone.body = 0;
    description.bones.push_back(bone);

    RagdollBodyDesc body;
    body.id = 0;
    body.boneId = 0;
    body.parentBody = kInvalidBody;
    body.role = BodyRole::Dynamic;
    body.mass = 2.0f;
    body.centerOfMass = makeVec3(0.0f, 0.0f, 0.0f);
    body.inertia[0] = 0.2f;
    body.inertia[3] = 0.4f;
    body.inertia[5] = 0.6f;
    body.bindPose = Transform{};
    RagdollShapeDesc shape;
    shape.type = ShapeType::Box;
    shape.local = Transform{};
    shape.halfExtents = makeVec3(0.2f, 0.2f, 0.2f);
    body.shapes.push_back(shape);
    description.bodies.push_back(body);
    return description;
}

RagdollInitialState makeSingleBodyInitialState()
{
    Transform bodyWorld;
    bodyWorld.translation = makeVec3(0.0f, 2.0f, 0.0f);
    RagdollInitialState initial;
    initial.entityWorld = bodyWorld;
    initial.rootToEntity = Transform{};
    initial.bodyWorld.push_back(bodyWorld);
    initial.linearVelocity.push_back(Vec3{});
    initial.angularVelocity.push_back(Vec3{});
    initial.enabled.push_back(1);
    return initial;
}

RagdollSimulationConfig makeImpulseTestConfig()
{
    RagdollSimulationConfig config = testConfig();
    config.gravity = makeVec3(0.0f, 0.0f, 0.0f);
    config.linearDamping = 0.0f;
    config.angularDamping = 0.0f;
    config.maxSubsteps = 1;
    config.promoteStaticRoot = false;
    config.collectDiagnostics = true;
    return config;
}

void testImpulseAtCenterOfMassAndOffset()
{
    const RagdollDescription description = makeSingleBodyDescription();

    {
        PhysXRagdollWorld world(makeImpulseTestConfig());
        std::string error;
        check(world.create(description, makeSingleBodyInitialState(), &error), "COM impulse world creates");
        check(world.applyImpulse(0, makeVec3(0.0f, 2.0f, 0.0f), makeVec3(0.0f, 0.0f, 1.0f), &error),
              "COM impulse applies");
        check(world.stepFixed(1, &error), "COM impulse world steps");
        const physics_physx::RagdollDiagnostics diagnostics = world.diagnostics(makeVec3(0.0f, 0.0f, 0.0f));
        check(diagnostics.externalImpulses == 1, "COM impulse is applied exactly once");
        check(diagnostics.maxAngularSpeed < 1.0e-4f, "COM impulse adds no angular velocity");
        check(world.pose().bodyWorld[0].translation.z > 0.004f, "COM impulse moves the body in +Z");
    }

    {
        PhysXRagdollWorld world(makeImpulseTestConfig());
        std::string error;
        check(world.create(description, makeSingleBodyInitialState(), &error), "offset impulse world creates");
        check(world.applyImpulse(0, makeVec3(1.0f, 2.0f, 0.0f), makeVec3(0.0f, 0.0f, 1.0f), &error),
              "offset impulse applies");
        check(world.stepFixed(1, &error), "offset impulse world steps");
        const physics_physx::RagdollDiagnostics diagnostics = world.diagnostics(makeVec3(0.0f, 0.0f, 0.0f));
        const float expected = 1.0f / 0.4f;
        check(std::abs(diagnostics.maxAngularSpeed - expected) < 1.0e-3f,
              "offset impulse produces the analytic angular velocity");
    }
}

void testBoneSpaceHitMapping()
{
    const Quat initialRotation = fromAxisAngle(makeVec3(0.0f, 1.0f, 0.0f), 1.57079632679f);
    Transform initialBody;
    initialBody.rotation = initialRotation;
    initialBody.translation = makeVec3(1.0f, 2.0f, 3.0f);

    Transform initialBone = initialBody;
    initialBone.rotation = initialRotation * fromAxisAngle(makeVec3(0.0f, 0.0f, 1.0f), 0.35f);
    initialBone.translation = makeVec3(1.0f, 2.4f, 3.0f);
    const Transform mergedLocal = inverse(initialBody) * initialBone;

    const Quat currentRotation = fromAxisAngle(makeVec3(0.0f, 1.0f, 0.0f), 0.6f);
    Transform currentBody;
    currentBody.rotation = currentRotation;
    currentBody.translation = makeVec3(4.0f, 5.0f, 6.0f);

    const Vec3 hitLocal = makeVec3(0.2f, 0.0f, 0.0f);
    const Transform bodyBone = resolveBoneWorld(BoneWorldKind::Body, currentBody, mergedLocal, Transform{}, Transform{});
    check(approxEqual(bodyBone, currentBody), "body-kind bone uses the body world transform");
    const Vec3 bodyHit = resolveWorldHitPoint(bodyBone, hitLocal);
    check(approxEqual(bodyHit, transformPoint(currentBody, hitLocal)), "body-kind hit is transformed once");

    const Transform mergedBone =
        resolveBoneWorld(BoneWorldKind::Merged, currentBody, mergedLocal, Transform{}, Transform{});
    const Vec3 mergedHit = resolveWorldHitPoint(mergedBone, hitLocal);
    check(approxEqual(mergedBone, currentBody * mergedLocal), "merged bone uses body * mergedLocal");
    check(approxEqual(mergedHit, transformPoint(currentBody * mergedLocal, hitLocal)),
          "merged pelvis/spine hit is transformed once");
    check(std::abs(length(mergedHit - mergedBone.translation) - length(hitLocal)) < 1.0e-4f,
          "rotating the parent body preserves the bone-space offset length");

    Transform entityWorld;
    entityWorld.rotation = fromAxisAngle(makeVec3(1.0f, 0.0f, 0.0f), 0.25f);
    entityWorld.translation = makeVec3(-1.0f, 0.5f, 2.0f);
    Transform animationBone;
    animationBone.rotation = fromAxisAngle(makeVec3(0.0f, 0.0f, 1.0f), -0.2f);
    animationBone.translation = makeVec3(0.1f, 1.2f, -0.3f);
    const Transform frozenBone =
        resolveBoneWorld(BoneWorldKind::Frozen, currentBody, mergedLocal, entityWorld, animationBone);
    check(approxEqual(frozenBone, entityWorld * animationBone), "frozen bone follows the animated model transform");

    const Vec3 boneOrigin = resolveWorldHitPoint(bodyBone, Vec3{});
    check(approxEqual(boneOrigin, bodyBone.translation),
          "zero bone-space position is an explicit valid bone-origin point");
}

RagdollDescription makePromotedRootDescription()
{
    RagdollDescription description;
    description.rootBody = 0;
    RagdollBoneDesc bone;
    bone.id = 0;
    bone.parent = kInvalidBone;
    bone.name = "root";
    bone.body = 0;
    description.bones.push_back(bone);

    RagdollBodyDesc body;
    body.id = 0;
    body.boneId = 0;
    body.parentBody = kInvalidBody;
    body.role = BodyRole::StaticRoot;
    body.isFixed = true;
    body.mass = 0.0f;
    body.centerOfMass = Vec3{};
    body.bindPose = Transform{};
    const float radius = 0.157804526745653f;
    for (int side = -1; side <= 1; side += 2)
    {
        RagdollShapeDesc shape;
        shape.type = ShapeType::Sphere;
        shape.local.translation = makeVec3(0.0f, 1.1f + 0.1f * static_cast<float>(side), 0.0f);
        shape.radius = radius;
        body.shapes.push_back(shape);
    }
    description.bodies.push_back(body);
    return description;
}

void testActualComImpulse()
{
    RagdollSimulationConfig config = makeImpulseTestConfig();
    config.promoteStaticRoot = true;
    config.staticRootDensity = 300.0f;
    const RagdollDescription description = makePromotedRootDescription();
    const Quat rotation = fromAxisAngle(makeVec3(0.0f, 1.0f, 0.0f), 0.4f);
    RagdollInitialState initial;
    initial.entityWorld = makeTransform(rotation, makeVec3(1.0f, 2.0f, 3.0f));
    initial.rootToEntity = Transform{};
    initial.bodyWorld.push_back(initial.entityWorld);
    initial.linearVelocity.push_back(Vec3{});
    initial.angularVelocity.push_back(Vec3{});
    initial.enabled.push_back(1);

    {
        PhysXRagdollWorld world(config);
        std::string error;
        check(world.create(description, initial, &error), "promoted root COM world creates");
        physics_physx::BodyMassProperties mass;
        check(world.massProperties(0, mass, &error), "promoted root mass properties read");
        check(std::abs(mass.mass - 9.87639f) < 0.02f, "promoted root mass is approximately 9.87639 kg");
        check(mass.body == 0, "promoted root mass properties keep the body ID");
        check(world.applyImpulseAtCom(0, makeVec3(0.0f, 0.0f, 1.0f), &error),
              "promoted root central impulse applies at the actual COM");
        check(world.stepFixed(1, &error), "promoted root central impulse world steps");
        const physics_physx::RagdollDiagnostics diagnostics = world.diagnostics(makeVec3(0.0f, 0.0f, 0.0f));
        check(diagnostics.externalImpulses == 1, "central impulse is applied exactly once");
        check(diagnostics.maxAngularSpeed < 1.0e-3f, "central impulse at actual COM adds no angular speed");
    }

    {
        PhysXRagdollWorld world(config);
        std::string error;
        check(world.create(description, initial, &error), "capture COM regression world creates");
        physics_physx::BodyMassProperties mass;
        check(world.massProperties(0, mass, &error), "capture COM regression mass properties read");
        const Vec3 captureCom = transformPoint(mass.actorWorld, description.bodies[0].centerOfMass);
        check(world.applyImpulse(0, captureCom, makeVec3(0.0f, 0.0f, 1.0f), &error),
              "capture COM impulse applies");
        check(world.stepFixed(1, &error), "capture COM regression world steps");
        const physics_physx::RagdollDiagnostics diagnostics = world.diagnostics(makeVec3(0.0f, 0.0f, 0.0f));
        check(diagnostics.maxAngularSpeed > 0.1f,
              "capture COM differs from actual COM and adds measurable angular speed");
    }

    {
        PhysXRagdollWorld world(config);
        std::string error;
        check(world.create(description, initial, &error), "promoted root offset world creates");
        physics_physx::BodyMassProperties mass;
        check(world.massProperties(0, mass, &error), "promoted root offset mass properties read");
        const Vec3 offset{0.2f, 0.0f, 0.0f};
        const Vec3 point{mass.worldCenterOfMass.x + offset.x, mass.worldCenterOfMass.y + offset.y,
                         mass.worldCenterOfMass.z + offset.z};
        const Vec3 impulse{0.0f, 0.0f, 1.0f};
        const Vec3 torque = cross(offset, impulse);
        const Vec3 torqueMass = rotate(conjugate(mass.massWorld.rotation), torque);
        const Vec3 omegaMass{torqueMass.x / mass.inertiaMassSpace.x, torqueMass.y / mass.inertiaMassSpace.y,
                             torqueMass.z / mass.inertiaMassSpace.z};
        const Vec3 omegaWorld = rotate(mass.massWorld.rotation, omegaMass);
        const float expectedAngular = length(omegaWorld);
        check(world.applyImpulse(0, point, impulse, &error), "promoted root offset impulse applies");
        check(world.stepFixed(1, &error), "promoted root offset impulse world steps");
        const physics_physx::RagdollDiagnostics diagnostics = world.diagnostics(makeVec3(0.0f, 0.0f, 0.0f));
        check(std::abs(diagnostics.maxAngularSpeed - expectedAngular) < 1.0e-2f,
              "offset impulse produces the analytic angular speed in the rotated mass frame");
    }
}

void testContactCounterLifetime()
{
    RagdollSimulationConfig config = makeImpulseTestConfig();
    config.gravity = makeVec3(0.0f, -19.62f, 0.0f);
    const RagdollDescription description = makeSingleBodyDescription();

    {
        RagdollInitialState initial = makeSingleBodyInitialState();
        initial.entityWorld = makeTransform(Quat{}, makeVec3(0.0f, 0.1f, 0.0f));
        initial.bodyWorld[0] = initial.entityWorld;
        PhysXRagdollWorld world(config);
        std::string error;
        check(world.create(description, initial, &error), "contact lifetime world creates");
        check(world.stepFixed(5, &error), "contact lifetime world steps");
        const physics_physx::RagdollDiagnostics beforeDestroy =
            world.diagnostics(makeVec3(0.0f, 0.0f, 0.0f));
        check(beforeDestroy.activeContactPairs > 0, "contact with the floor increments active pairs");
        world.destroy();
        const physics_physx::RagdollDiagnostics afterDestroy =
            world.diagnostics(makeVec3(0.0f, 0.0f, 0.0f));
        check(afterDestroy.activeContactPairs == 0, "destroy clears active contact pairs");
    }

    {
        RagdollInitialState initial = makeSingleBodyInitialState();
        initial.entityWorld = makeTransform(Quat{}, makeVec3(0.0f, 5.0f, 0.0f));
        initial.bodyWorld[0] = initial.entityWorld;
        PhysXRagdollWorld world(config);
        std::string error;
        check(world.create(description, initial, &error), "airborne world creates");
        const physics_physx::RagdollDiagnostics beforeStep =
            world.diagnostics(makeVec3(0.0f, 0.0f, 0.0f));
        check(beforeStep.activeContactPairs == 0, "new world has no active pairs before the first step");
    }
}

void testEnergyDiagnostics()
{
    RagdollDescription description;
    description.rootBody = 0;
    RagdollBoneDesc bone;
    bone.id = 0;
    bone.parent = kInvalidBone;
    bone.name = "root";
    bone.bindLocal = Transform{};
    bone.body = 0;
    description.bones.push_back(bone);

    RagdollBodyDesc body;
    body.id = 0;
    body.boneId = 0;
    body.parentBody = kInvalidBody;
    body.role = BodyRole::Dynamic;
    body.mass = 2.0f;
    body.centerOfMass = makeVec3(0.1f, 0.2f, 0.3f);
    body.inertia[0] = 0.2f;
    body.inertia[3] = 0.4f;
    body.inertia[5] = 0.6f;
    body.bindPose = Transform{};
    RagdollShapeDesc shape;
    shape.type = ShapeType::Box;
    shape.local = Transform{};
    shape.halfExtents = makeVec3(0.2f, 0.2f, 0.2f);
    body.shapes.push_back(shape);
    description.bodies.push_back(body);

    const Quat rotation = fromAxisAngle(makeVec3(0.0f, 1.0f, 0.0f), 1.57079632679f);
    Transform bodyWorld;
    bodyWorld.rotation = rotation;
    bodyWorld.translation = makeVec3(1.0f, 2.0f, 3.0f);

    RagdollInitialState initial;
    initial.entityWorld = bodyWorld;
    initial.rootToEntity = Transform{};
    initial.bodyWorld.push_back(bodyWorld);
    initial.linearVelocity.push_back(makeVec3(1.0f, 0.0f, 0.0f));
    initial.angularVelocity.push_back(makeVec3(0.0f, 1.0f, 0.0f));
    initial.enabled.push_back(1);

    RagdollSimulationConfig config = testConfig();
    config.gravity = makeVec3(1.0f, -9.81f, 0.5f);
    std::string error;
    const Vec3 com = transformPoint(bodyWorld, body.centerOfMass);
    const float expectedPotential = -body.mass * dot(config.gravity, com);

    {
        PhysXRagdollWorld world(config);
        check(world.create(description, initial, &error), "energy fixture creates");
        check(world.stepFixed(0, &error), "energy fixture starts at t=0");
        const physics_physx::RagdollDiagnostics diagnostics = world.diagnostics(makeVec3(0.0f, 0.0f, 0.0f));
        const float expectedLinear = 0.5f * body.mass * 1.0f;
        const float expectedRotational = 0.5f * 0.4f;
        check(std::abs(diagnostics.kineticEnergy - (expectedLinear + expectedRotational)) < 1.0e-4,
              "energy diagnostics match rotated-offset body");
        check(std::abs(diagnostics.potentialEnergy - expectedPotential) < 1.0e-4,
              "potential energy uses COM and full gravity");
        check(std::abs(diagnostics.totalEnergy - (expectedLinear + expectedRotational + expectedPotential)) < 1.0e-4,
              "total energy is consistent");
        check(std::isfinite(diagnostics.totalEnergy), "energy diagnostics are finite");
    }

    RagdollInitialState staticInitial = initial;
    staticInitial.linearVelocity[0] = Vec3{};
    staticInitial.angularVelocity[0] = Vec3{};
    PhysXRagdollWorld staticWorld(config);
    check(staticWorld.create(description, staticInitial, &error), "static energy fixture creates");
    check(staticWorld.stepFixed(0, &error), "static energy fixture starts at t=0");
    const physics_physx::RagdollDiagnostics staticDiagnostics =
        staticWorld.diagnostics(makeVec3(0.0f, 0.0f, 0.0f));
    check(std::abs(staticDiagnostics.kineticEnergy) < 1.0e-5, "static pose has zero kinetic energy");
    check(std::abs(staticDiagnostics.potentialEnergy - expectedPotential) < 1.0e-4,
          "static pose keeps the COM-based potential energy");
}

} // namespace

int main()
{
    testHumanoidDrop();
    testInitialVelocityHandoff();
    testImpulse();
    testAccumulator();
    testInvalidDescription();
    testRecreateLifetime();
    testJsonLoadedHumanoid();
    testRuntimeContext();
    testRealCaptureFixture();
    testCylinderMargins();
    testCylinderOrientation();
    testHingeAxisZ();
    testMoveAssignment();
    testNonBindInitialPose();
    testConeAndPyramid();
    testRejectedNoPhysicsRoot();
    testHinge2Rejected();
    testImpulseAtCenterOfMassAndOffset();
    testActualComImpulse();
    testBoneSpaceHitMapping();
    testContactCounterLifetime();
    testEnergyDiagnostics();

    std::printf("physics_physx tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
