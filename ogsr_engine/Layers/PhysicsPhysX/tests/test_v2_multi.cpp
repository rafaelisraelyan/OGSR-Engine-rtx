#include "physics_physx/physx_ragdoll_c_v2.h"
#include "physics_physx/physx_ragdoll_c.h"

#include "v2_fixture_builder.h"

#include "physics_portable/json.h"
#include "physics_portable/ragdoll.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace
{
using physics_portable::RagdollDescription;
using physics_portable::RagdollInitialState;
using physics_portable::ShapeType;
using physics_portable::Transform;
using physics_portable::Vec3;

int g_checks = 0;
int g_failures = 0;
std::string g_fixturePath;

bool loadFixture(physics_portable::RagdollCaptureDocument& document)
{
    if (g_fixturePath.empty())
    {
        std::printf("FAIL: fixture path is empty\n");
        return false;
    }
    std::ifstream input(g_fixturePath, std::ios::binary);
    if (!input.good())
    {
        std::printf("FAIL: cannot open fixture %s\n", g_fixturePath.c_str());
        return false;
    }
    const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::string error;
    if (!physics_portable::parseRagdollCapture(json, document, &error))
    {
        std::printf("FAIL: cannot parse fixture: %s\n", error.c_str());
        return false;
    }
    return true;
}

void check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", message);
    }
}

bool contains(const char* text, const char* needle)
{
    return text && needle && std::strstr(text, needle) != nullptr;
}

OgsrPhysX2SceneConfig sceneConfig(float gravityY = -19.62f, bool collectContacts = true)
{
    OgsrPhysX2SceneConfig config{};
    config.structSize = sizeof(config);
    config.fixedTimestep = 0.01f;
    config.gravityY = gravityY;
    config.workerThreads = 2;
    config.maxSubsteps = 4;
    config.solverPositionIterations = 16;
    config.solverVelocityIterations = 4;
    config.contactOffset = 0.02f;
    config.restOffset = 0.0f;
    config.enableStabilization = 1;
    config.collectContacts = collectContacts ? 1 : 0;
    config.maxInstances = OGSR_PHYSX2_INSTANCE_CAPACITY;
    return config;
}

OgsrPhysX2RagdollInstanceConfig instanceConfig(bool disableSelfCollision = true)
{
    OgsrPhysX2RagdollInstanceConfig config{};
    config.structSize = sizeof(config);
    config.linearDamping = 0.05f;
    config.angularDamping = 0.2f;
    config.staticRootDensity = 300.0f;
    config.promoteStaticRoot = 1;
    config.disableSelfCollision = disableSelfCollision ? 1 : 0;
    config.collisionGroup = OGSR_PHYSX2_GROUP_RAGDOLL;
    config.collisionMask = OGSR_PHYSX2_GROUP_RAGDOLL | OGSR_PHYSX2_GROUP_STATIC;
    return config;
}

void floorMesh(float minX, float maxX, float minZ, float maxZ, std::vector<float>& vertices,
               std::vector<std::uint32_t>& indices)
{
    vertices = {minX, 0.0f, minZ, maxX, 0.0f, minZ, maxX, 0.0f, maxZ, minX, 0.0f, maxZ};
    indices = {0, 1, 2, 0, 2, 3};
}

bool createScene(const OgsrPhysX2SceneConfig& config, OgsrPhysX2SceneHandle& scene)
{
    char error[512] = {};
    return OgsrPhysX2SceneCreate(&config, &scene, error, sizeof(error)) == 1;
}

bool setFloor(OgsrPhysX2SceneHandle scene)
{
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    floorMesh(-10.0f, 10.0f, -10.0f, 10.0f, vertices, indices);
    char error[512] = {};
    return OgsrPhysX2SceneSetStaticMesh(scene, vertices.data(), static_cast<std::uint32_t>(vertices.size() / 3),
                                        indices.data(), static_cast<std::uint32_t>(indices.size()), error,
                                        sizeof(error)) == 1;
}

bool createCaptureRagdoll(OgsrPhysX2SceneHandle scene, physics_physx_tests::V2Fixture& fixture,
                          const physics_portable::RagdollCaptureDocument& document, const Vec3& desiredPosition,
                          OgsrPhysX2RagdollHandle& handle)
{
    const Vec3 base = document.initial.entityWorld.translation;
    const Vec3 offset{desiredPosition.x - base.x, desiredPosition.y - base.y, desiredPosition.z - base.z};
    fixture.build(document.description, document.initial, offset);
    const OgsrPhysX2RagdollInstanceConfig config = instanceConfig();
    char error[512] = {};
    return OgsrPhysX2RagdollCreate(scene, &fixture.definition, &config, &handle, error, sizeof(error)) == 1;
}

bool createSingleBody(OgsrPhysX2SceneHandle scene, const Vec3& position, float radius,
                      OgsrPhysX2RagdollHandle& handle, float friction = 0.8f, char* errorOut = nullptr,
                      std::size_t errorSize = 0)
{
    RagdollDescription description;
    description.skeletonName = "single_body";
    description.rootBody = 0;

    physics_portable::RagdollMaterialDesc material;
    material.id = 0;
    material.friction = friction;
    material.restitution = 0.0f;
    description.materials.push_back(material);

    physics_portable::RagdollBoneDesc bone;
    bone.id = 0;
    bone.parent = physics_portable::kInvalidBone;
    bone.name = "root";
    bone.disposition = physics_portable::BoneDisposition::Body;
    bone.body = 0;
    description.bones.push_back(bone);

    physics_portable::RagdollBodyDesc body;
    body.id = 0;
    body.boneId = 0;
    body.parentBody = physics_portable::kInvalidBody;
    body.role = physics_portable::BodyRole::Dynamic;
    body.mass = 1.0f;
    body.bindPose = Transform{};
    const float inertia = 0.4f * body.mass * radius * radius;
    body.inertia[0] = inertia;
    body.inertia[3] = inertia;
    body.inertia[5] = inertia;
    physics_portable::RagdollShapeDesc shape;
    shape.type = ShapeType::Sphere;
    shape.radius = radius;
    shape.materialId = 0;
    shape.sourceBoneId = 0;
    body.shapes.push_back(shape);
    description.bodies.push_back(body);

    RagdollInitialState initial;
    Transform world;
    world.translation = position;
    initial.entityWorld = world;
    initial.rootToEntity = Transform{};
    initial.bodyWorld.push_back(world);
    initial.linearVelocity.push_back(Vec3{});
    initial.angularVelocity.push_back(Vec3{});
    initial.enabled.push_back(1);

    physics_physx_tests::V2Fixture fixture;
    fixture.build(description, initial);
    const OgsrPhysX2RagdollInstanceConfig config = instanceConfig();
    char error[512] = {};
    const int created = OgsrPhysX2RagdollCreate(scene, &fixture.definition, &config, &handle, error, sizeof(error));
    if (errorOut && errorSize)
        std::snprintf(errorOut, errorSize, "%s", error);
    return created == 1;
}

bool poseFinite(OgsrPhysX2RagdollHandle handle)
{
    OgsrPhysXRagdollPose pose{};
    std::vector<float> body(OGSR_PHYSX_RAGDOLL_MAX_BODIES * 16);
    if (OgsrPhysX2RagdollGetPose(handle, &pose, body.data(), OGSR_PHYSX_RAGDOLL_MAX_BODIES) != 1)
        return false;
    if (pose.bodyCount == 0)
        return false;
    for (std::uint32_t index = 0; index < pose.bodyCount * 16; ++index)
    {
        if (!std::isfinite(body[index]))
            return false;
    }
    return true;
}

float poseX(OgsrPhysX2RagdollHandle handle)
{
    OgsrPhysXRagdollPose pose{};
    std::vector<float> body(OGSR_PHYSX_RAGDOLL_MAX_BODIES * 16);
    if (OgsrPhysX2RagdollGetPose(handle, &pose, body.data(), OGSR_PHYSX_RAGDOLL_MAX_BODIES) != 1)
        return std::numeric_limits<float>::quiet_NaN();
    return body[12];
}

float poseY(OgsrPhysX2RagdollHandle handle)
{
    OgsrPhysXRagdollPose pose{};
    std::vector<float> body(OGSR_PHYSX_RAGDOLL_MAX_BODIES * 16);
    if (OgsrPhysX2RagdollGetPose(handle, &pose, body.data(), OGSR_PHYSX_RAGDOLL_MAX_BODIES) != 1)
        return std::numeric_limits<float>::quiet_NaN();
    return body[13];
}

struct PoseSnapshot
{
    OgsrPhysXRagdollPose pose{};
    std::vector<float> bodies;
};

bool snapshot(OgsrPhysX2RagdollHandle handle, PoseSnapshot& out)
{
    out.bodies.assign(OGSR_PHYSX_RAGDOLL_MAX_BODIES * 16, 0.0f);
    return OgsrPhysX2RagdollGetPose(handle, &out.pose, out.bodies.data(), OGSR_PHYSX_RAGDOLL_MAX_BODIES) == 1;
}

float bodyComponent(const PoseSnapshot& value, std::uint32_t body, std::uint32_t component)
{
    return value.bodies[static_cast<std::size_t>(body) * 16 + component];
}

float minBodyY(const PoseSnapshot& value)
{
    float result = std::numeric_limits<float>::max();
    for (std::uint32_t body = 0; body < value.pose.bodyCount; ++body)
        result = std::min(result, bodyComponent(value, body, 13));
    return result;
}

float maxBodyDisplacement(const PoseSnapshot& before, const PoseSnapshot& after)
{
    float result = 0.0f;
    const std::uint32_t count = std::min(before.pose.bodyCount, after.pose.bodyCount);
    for (std::uint32_t body = 0; body < count; ++body)
    {
        const float dx = bodyComponent(after, body, 12) - bodyComponent(before, body, 12);
        const float dy = bodyComponent(after, body, 13) - bodyComponent(before, body, 13);
        const float dz = bodyComponent(after, body, 14) - bodyComponent(before, body, 14);
        result = std::max(result, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return result;
}

bool snapshotFinite(const PoseSnapshot& value)
{
    if (value.pose.bodyCount == 0)
        return false;
    for (std::uint32_t index = 0; index < value.pose.bodyCount * 16; ++index)
    {
        if (!std::isfinite(value.bodies[index]))
            return false;
    }
    return true;
}

bool step(OgsrPhysX2SceneHandle scene, float delta, std::uint64_t frameId, OgsrPhysX2StepResult& result)
{
    result = OgsrPhysX2StepResult{};
    result.structSize = sizeof(result);
    char error[512] = {};
    return OgsrPhysX2SceneStep(scene, delta, frameId, &result, error, sizeof(error)) == 1;
}

void testTwoRagdollsOneScene()
{
    OgsrPhysX2SceneHandle scene{};
    check(createScene(sceneConfig(), scene), "two-ragdoll scene create");
    check(setFloor(scene), "two-ragdoll floor upload");

    physics_portable::RagdollCaptureDocument document;
    check(loadFixture(document), "two-ragdoll fixture loads");
    physics_physx_tests::V2Fixture fixtureA;
    physics_physx_tests::V2Fixture fixtureB;
    OgsrPhysX2RagdollHandle a{};
    OgsrPhysX2RagdollHandle b{};
    check(createCaptureRagdoll(scene, fixtureA, document, Vec3{-1.5f, 3.0f, 0.0f}, a), "humanoid A create");
    check(createCaptureRagdoll(scene, fixtureB, document, Vec3{1.5f, 3.0f, 0.0f}, b), "humanoid B create");
    check(OgsrPhysX2RagdollIsAlive(a) == 1 && OgsrPhysX2RagdollIsAlive(b) == 1, "both humanoids alive");

    OgsrPhysXRagdollInfo infoA{};
    OgsrPhysXRagdollInfo infoB{};
    check(OgsrPhysX2RagdollGetInfo(a, &infoA) == 1, "A info");
    check(OgsrPhysX2RagdollGetInfo(b, &infoB) == 1, "B info");
    check(infoA.bodyCount == 11 && infoA.jointCount == 10, "A is 11/10");
    check(infoB.bodyCount == 11 && infoB.jointCount == 10, "B is 11/10");
    check(infoA.staticTriangles == 2, "scene static triangles visible to A");
    check(std::abs(infoA.totalMass - 92.2128f) < 0.2f, "A total mass matches v1 baseline");
    check(std::abs(infoA.rootMass - 9.87639f) < 0.1f, "A root mass matches v1 baseline");
    std::printf("two-ragdoll evidence: A=%u/%u mass=%.4f root=%.4f, B=%u/%u mass=%.4f root=%.4f\n",
                infoA.bodyCount, infoA.jointCount, infoA.totalMass, infoA.rootMass, infoB.bodyCount,
                infoB.jointCount, infoB.totalMass, infoB.rootMass);
    std::printf("handle evidence: A=(%u,%u,%u,%u) B=(%u,%u,%u,%u)\n", a.sceneSlot, a.sceneGeneration, a.instanceSlot,
                a.instanceGeneration, b.sceneSlot, b.sceneGeneration, b.instanceSlot, b.instanceGeneration);

    OgsrPhysXRagdollBodyMass rootMass{};
    check(OgsrPhysX2RagdollGetBodyMass(a, 0, &rootMass, nullptr, 0) == 1, "A root body mass");
    check(std::abs(rootMass.mass - 9.87639f) < 0.1f, "A root body mass matches v1 baseline");
    check(std::isfinite(rootMass.worldCenterOfMass[0]) && std::isfinite(rootMass.worldCenterOfMass[1]) &&
              std::isfinite(rootMass.worldCenterOfMass[2]),
          "A root COM is finite");
    std::printf("root mass evidence: mass=%.4f com=(%.4f,%.4f,%.4f) inertia=(%.4f,%.4f,%.4f)\n", rootMass.mass,
                rootMass.worldCenterOfMass[0], rootMass.worldCenterOfMass[1], rootMass.worldCenterOfMass[2],
                rootMass.inertiaMassSpace[0], rootMass.inertiaMassSpace[1], rootMass.inertiaMassSpace[2]);

    OgsrPhysX2StepResult result{};
    check(step(scene, 0.01f, 1, result), "first scene step");
    check(result.steps == 1 && result.stepIndex == 1, "first step advances one tick");
    check(poseFinite(a) && poseFinite(b), "first poses finite");

    for (std::uint64_t frame = 2; frame <= 121; ++frame)
        check(step(scene, 0.01f, frame, result), "humanoid drop step");

    check(poseFinite(a) && poseFinite(b), "both humanoid poses finite after drop");
    OgsrPhysXRagdollDiagnostics diagnosticsA{};
    OgsrPhysXRagdollDiagnostics diagnosticsB{};
    check(OgsrPhysX2RagdollGetDiagnostics(a, 0.0f, &diagnosticsA) == 1, "A diagnostics");
    check(OgsrPhysX2RagdollGetDiagnostics(b, 0.0f, &diagnosticsB) == 1, "B diagnostics");
    check(diagnosticsA.dynamicBodies == 11 && diagnosticsB.dynamicBodies == 11, "both have 11 dynamic bodies");
    check(std::isfinite(diagnosticsA.totalEnergy) && std::isfinite(diagnosticsB.totalEnergy),
          "both energy diagnostics finite");
    check(diagnosticsA.stepIndex == diagnosticsB.stepIndex, "both instances share scene step index");

    const float beforeX = poseX(a);
    OgsrPhysX2StepResult repeated{};
    check(step(scene, 0.01f, 121, repeated), "same frame id returns cached result");
    check(repeated.stepIndex == diagnosticsA.stepIndex, "cached step keeps scene index");
    check(std::abs(poseX(a) - beforeX) < 1.0e-5f, "cached step does not simulate");
    check(!step(scene, 0.01f, 120, repeated), "decreasing frame id is rejected");

    check(OgsrPhysX2RagdollDestroy(a, nullptr, 0) == 1, "destroy A");
    check(OgsrPhysX2RagdollIsAlive(a) == 0, "A stale after destroy");
    check(OgsrPhysX2RagdollIsAlive(b) == 1, "B survives A destroy");
    check(step(scene, 0.01f, 122, result), "B continues after A destroy");
    check(poseFinite(b), "B pose finite after A destroy");
    check(OgsrPhysX2RagdollDestroy(a, nullptr, 0) == 0, "double destroy A rejected");

    OgsrPhysX2RagdollHandle c{};
    check(createCaptureRagdoll(scene, fixtureA, document, Vec3{0.0f, 3.0f, 0.0f}, c), "create C in freed slot");
    check(OgsrPhysX2RagdollIsAlive(c) == 1, "C alive");
    check(step(scene, 0.01f, 123, result), "B/C step");
    check(poseFinite(b) && poseFinite(c), "B/C poses finite");
    check(OgsrPhysX2RagdollDestroy(c, nullptr, 0) == 1, "destroy C");
    check(OgsrPhysX2RagdollDestroy(b, nullptr, 0) == 1, "destroy B");
    check(OgsrPhysX2SceneDestroy(scene, nullptr, 0) == 1, "destroy two-ragdoll scene");
}

void testTwoScenesIndependent()
{
    OgsrPhysX2SceneHandle sceneA{};
    OgsrPhysX2SceneHandle sceneB{};
    check(createScene(sceneConfig(), sceneA), "two-scene A create");
    check(createScene(sceneConfig(), sceneB), "two-scene B create");
    check(setFloor(sceneA) && setFloor(sceneB), "two-scene floors");

    physics_portable::RagdollCaptureDocument document;
    check(loadFixture(document), "two-scene fixture loads");
    physics_physx_tests::V2Fixture fixtureA1;
    physics_physx_tests::V2Fixture fixtureA2;
    physics_physx_tests::V2Fixture fixtureB1;
    OgsrPhysX2RagdollHandle a1{};
    OgsrPhysX2RagdollHandle a2{};
    OgsrPhysX2RagdollHandle b1{};
    check(createCaptureRagdoll(sceneA, fixtureA1, document, Vec3{-1.0f, 3.0f, 0.0f}, a1), "scene A ragdoll A1");
    check(createCaptureRagdoll(sceneA, fixtureA2, document, Vec3{1.0f, 3.0f, 0.0f}, a2), "scene A ragdoll A2");
    check(createCaptureRagdoll(sceneB, fixtureB1, document, Vec3{0.0f, 3.0f, 0.0f}, b1), "scene B ragdoll B1");

    check(OgsrPhysX2RagdollDestroy(a1, nullptr, 0) == 1, "destroy A1");
    check(OgsrPhysX2RagdollIsAlive(a2) == 1, "A2 survives A1 destroy");
    check(OgsrPhysX2RagdollIsAlive(b1) == 1, "B1 survives A1 destroy");

    OgsrPhysX2StepResult result{};
    check(step(sceneA, 0.01f, 1, result), "scene A step");
    check(step(sceneB, 0.01f, 1, result), "scene B step");
    check(poseFinite(a2) && poseFinite(b1), "A2/B1 poses finite");

    check(OgsrPhysX2SceneDestroy(sceneA, nullptr, 0) == 1, "destroy scene A");
    check(OgsrPhysX2RagdollIsAlive(a2) == 0, "A2 invalid after scene A destroy");
    check(OgsrPhysX2RagdollIsAlive(b1) == 1, "B1 survives scene A destroy");
    check(step(sceneB, 0.01f, 2, result), "scene B continues after A destroy");
    check(poseFinite(b1), "B1 pose finite after scene A destroy");
    check(OgsrPhysX2RagdollDestroy(b1, nullptr, 0) == 1, "destroy B1");
    check(OgsrPhysX2SceneDestroy(sceneB, nullptr, 0) == 1, "destroy scene B");
}

void testSceneDestroyWithInstances()
{
    OgsrPhysX2SceneHandle scene{};
    check(createScene(sceneConfig(), scene), "scene-destroy scene create");
    check(setFloor(scene), "scene-destroy floor");
    OgsrPhysX2RagdollHandle a{};
    OgsrPhysX2RagdollHandle b{};
    check(createSingleBody(scene, Vec3{-1.0f, 1.0f, 0.0f}, 0.25f, a), "scene-destroy A");
    check(createSingleBody(scene, Vec3{1.0f, 1.0f, 0.0f}, 0.25f, b), "scene-destroy B");
    check(OgsrPhysX2SceneDestroy(scene, nullptr, 0) == 1, "scene destroy with two instances");
    check(OgsrPhysX2RagdollIsAlive(a) == 0 && OgsrPhysX2RagdollIsAlive(b) == 0,
          "scene destroy invalidates both instances");
    check(OgsrPhysX2RagdollDestroy(a, nullptr, 0) == 0, "old A destroy rejected");

    OgsrPhysX2SceneHandle next{};
    check(createScene(sceneConfig(), next), "new scene after destroy");
    check(OgsrPhysX2SceneIsAlive(scene) == 0, "old scene handle rejected");
    OgsrPhysX2RagdollHandle c{};
    check(createSingleBody(next, Vec3{0.0f, 1.0f, 0.0f}, 0.25f, c), "new scene instance");
    check(OgsrPhysX2SceneDestroy(next, nullptr, 0) == 1, "new scene cleanup");
}

void testContactImpulse()
{
    OgsrPhysX2SceneHandle scene{};
    check(createScene(sceneConfig(0.0f), scene), "contact scene create");
    OgsrPhysX2RagdollHandle a{};
    OgsrPhysX2RagdollHandle b{};
    check(createSingleBody(scene, Vec3{-0.30f, 0.0f, 0.0f}, 0.25f, a), "contact A create");
    check(createSingleBody(scene, Vec3{0.30f, 0.0f, 0.0f}, 0.25f, b), "contact B create");

    const float initialBx = poseX(b);
    const float impulse[3] = {5.0f, 0.0f, 0.0f};
    check(OgsrPhysX2RagdollApplyCentralImpulse(a, 0, impulse, nullptr, 0) == 1, "central impulse on A");

    bool contactA = false;
    bool contactB = false;
    float impulseA = 0.0f;
    float impulseB = 0.0f;
    OgsrPhysX2StepResult result{};
    for (std::uint64_t frame = 1; frame <= 40; ++frame)
    {
        check(step(scene, 0.01f, frame, result), "contact step");
        OgsrPhysXRagdollDiagnostics diagnosticsA{};
        OgsrPhysXRagdollDiagnostics diagnosticsB{};
        check(OgsrPhysX2RagdollGetDiagnostics(a, 0.0f, &diagnosticsA) == 1, "contact A diagnostics");
        check(OgsrPhysX2RagdollGetDiagnostics(b, 0.0f, &diagnosticsB) == 1, "contact B diagnostics");
        if (diagnosticsA.contactFound > 0)
            contactA = true;
        if (diagnosticsB.contactFound > 0)
            contactB = true;
        impulseA = std::max(impulseA, diagnosticsA.totalContactImpulse);
        impulseB = std::max(impulseB, diagnosticsB.totalContactImpulse);
    }

    const float finalBx = poseX(b);
    check(contactA && contactB, "contact attributed to both instances");
    check(impulseA > 0.0f && impulseB > 0.0f, "nonzero contact impulse on both instances");
    check(finalBx > initialBx + 0.01f, "B moves because of A contact");
    check(std::isfinite(finalBx), "B final position finite");
    std::printf("contact evidence: impulseA=%.4f impulseB=%.4f Bdx=%.4f\n", impulseA, impulseB,
                finalBx - initialBx);
    check(OgsrPhysX2RagdollDestroy(a, nullptr, 0) == 1, "contact A destroy");
    check(OgsrPhysX2RagdollDestroy(b, nullptr, 0) == 1, "contact B destroy");
    check(OgsrPhysX2SceneDestroy(scene, nullptr, 0) == 1, "contact scene destroy");
}

void testTwoFullRagdollsContact()
{
    OgsrPhysX2SceneHandle scene{};
    check(createScene(sceneConfig(), scene), "full-contact scene create");
    check(setFloor(scene), "full-contact floor upload");

    physics_portable::RagdollCaptureDocument document;
    check(loadFixture(document), "full-contact fixture loads");

    physics_physx_tests::V2Fixture fixtureA;
    physics_physx_tests::V2Fixture fixtureB;
    OgsrPhysX2RagdollHandle a{};
    OgsrPhysX2RagdollHandle b{};
    const Vec3 spawnA{-1.2f, 3.0f, 0.0f};
    const Vec3 spawnB{0.0f, 3.0f, 0.0f};
    check(createCaptureRagdoll(scene, fixtureA, document, spawnA, a), "full-contact A create");
    check(createCaptureRagdoll(scene, fixtureB, document, spawnB, b), "full-contact B create");

    OgsrPhysXRagdollInfo infoA{};
    OgsrPhysXRagdollInfo infoB{};
    check(OgsrPhysX2RagdollGetInfo(a, &infoA) == 1, "full-contact A info");
    check(OgsrPhysX2RagdollGetInfo(b, &infoB) == 1, "full-contact B info");
    check(infoA.bodyCount == 11 && infoA.jointCount == 10, "full-contact A is 11/10");
    check(infoB.bodyCount == 11 && infoB.jointCount == 10, "full-contact B is 11/10");
    check(infoA.staticTriangles == 2 && infoB.staticTriangles == 2,
          "full-contact static geometry is scene-owned");
    check(a.instanceSlot != b.instanceSlot, "full-contact A and B use different instance slots");

    OgsrPhysX2StepResult result{};
    check(step(scene, 0.01f, 1, result), "full-contact pre-step");

    PoseSnapshot initialA{};
    PoseSnapshot initialB{};
    check(snapshot(a, initialA), "full-contact initial A pose");
    check(snapshot(b, initialB), "full-contact initial B pose");
    const float initialBx = bodyComponent(initialB, 0, 12);
    const float initialBy = bodyComponent(initialB, 0, 13);
    const float initialBz = bodyComponent(initialB, 0, 14);

    const float impulse[3] = {60.0f, 0.0f, 0.0f};
    check(OgsrPhysX2RagdollApplyCentralImpulse(a, 5, impulse, nullptr, 0) == 1,
          "full-contact central impulse on A spine");
    std::printf("full-rig setup: impulse=(%.4f,%.4f,%.4f) body=5 "
                "spawnA=(%.4f,%.4f,%.4f) spawnB=(%.4f,%.4f,%.4f) B0=(%.4f,%.4f,%.4f)\n",
                impulse[0], impulse[1], impulse[2], spawnA.x, spawnA.y, spawnA.z, spawnB.x, spawnB.y,
                spawnB.z, initialBx, initialBy, initialBz);

    bool contactSeen = false;
    std::uint64_t contactFrame = 0;
    float contactBMinY = 0.0f;
    float contactBx = 0.0f;
    std::uint32_t contactFoundA = 0;
    std::uint32_t contactFoundB = 0;
    std::uint32_t contactPairsA = 0;
    std::uint32_t contactPairsB = 0;
    std::uint32_t totalFoundB = 0;
    std::uint32_t totalPersistB = 0;
    std::uint32_t totalLostB = 0;
    float maxImpulseA = 0.0f;
    float maxImpulseB = 0.0f;
    std::uint64_t impulseFrame = 0;
    float firstImpulseA = 0.0f;
    float firstImpulseB = 0.0f;
    bool airborneImpulse = false;
    float maxAnchorA = 0.0f;
    float maxAnchorB = 0.0f;
    float maxLimitA = 0.0f;
    float maxLimitB = 0.0f;

    for (std::uint64_t frame = 2; frame <= 400; ++frame)
    {
        check(step(scene, 0.01f, frame, result), "full-contact step");
        OgsrPhysXRagdollDiagnostics diagnosticsA{};
        OgsrPhysXRagdollDiagnostics diagnosticsB{};
        check(OgsrPhysX2RagdollGetDiagnostics(a, 0.0f, &diagnosticsA) == 1, "full-contact A diagnostics");
        check(OgsrPhysX2RagdollGetDiagnostics(b, 0.0f, &diagnosticsB) == 1, "full-contact B diagnostics");
        totalFoundB += diagnosticsB.contactFound;
        totalPersistB += diagnosticsB.contactPersist;
        totalLostB += diagnosticsB.contactLost;
        maxAnchorA = std::max(maxAnchorA, diagnosticsA.maxJointAnchorError);
        maxAnchorB = std::max(maxAnchorB, diagnosticsB.maxJointAnchorError);
        maxLimitA = std::max(maxLimitA, diagnosticsA.maxJointLimitError);
        maxLimitB = std::max(maxLimitB, diagnosticsB.maxJointLimitError);
        maxImpulseA = std::max(maxImpulseA, diagnosticsA.totalContactImpulse);
        maxImpulseB = std::max(maxImpulseB, diagnosticsB.totalContactImpulse);
        if (contactSeen && !airborneImpulse)
        {
            PoseSnapshot airborneB{};
            if (snapshot(b, airborneB) && minBodyY(airborneB) > 0.5f &&
                diagnosticsB.totalContactImpulse > 0.0f)
            {
                airborneImpulse = true;
                impulseFrame = frame;
                firstImpulseA = diagnosticsA.totalContactImpulse;
                firstImpulseB = diagnosticsB.totalContactImpulse;
            }
        }

        if (!contactSeen && diagnosticsB.contactFound > 0)
        {
            PoseSnapshot currentB{};
            if (snapshot(b, currentB))
            {
                contactSeen = true;
                contactFrame = frame;
                contactBMinY = minBodyY(currentB);
                contactBx = bodyComponent(currentB, 0, 12);
                contactFoundA = diagnosticsA.contactFound;
                contactFoundB = diagnosticsB.contactFound;
                contactPairsA = diagnosticsA.activeContactPairs;
                contactPairsB = diagnosticsB.activeContactPairs;
            }
        }
    }

    check(contactSeen, "full rigs produce a B contact event");
    check(contactBMinY > 0.3f, "first B contact happens while B is airborne");
    check(contactFoundA > 0, "A reports the same contact event");
    check(contactFoundB > 0, "B reports the contact event");
    check(maxImpulseA > 0.0f && maxImpulseB > 0.0f,
          "nonzero contact impulse on both full ragdolls");
    check(airborneImpulse, "nonzero A<->B impulse is observed while B is airborne");
    check(contactPairsA > 0 && contactPairsB > 0, "contact pair is attributed to both full ragdolls");

    PoseSnapshot finalA{};
    PoseSnapshot finalB{};
    check(snapshot(a, finalA), "full-contact final A pose");
    check(snapshot(b, finalB), "full-contact final B pose");
    check(snapshotFinite(initialA) && snapshotFinite(initialB) && snapshotFinite(finalA) && snapshotFinite(finalB),
          "all full-rig transforms are finite");
    const float finalBx = bodyComponent(finalB, 0, 12);
    const float finalBy = bodyComponent(finalB, 0, 13);
    const float finalBz = bodyComponent(finalB, 0, 14);
    const float displacementB = maxBodyDisplacement(initialB, finalB);
    check(finalBx > contactBx + 0.02f, "B is pushed away from A by the contact");
    check(displacementB > 0.05f, "B pose changes after the contact");

    OgsrPhysXRagdollDiagnostics finalDiagA{};
    OgsrPhysXRagdollDiagnostics finalDiagB{};
    check(OgsrPhysX2RagdollGetDiagnostics(a, 0.0f, &finalDiagA) == 1, "full-contact final A diagnostics");
    check(OgsrPhysX2RagdollGetDiagnostics(b, 0.0f, &finalDiagB) == 1, "full-contact final B diagnostics");
    check(std::isfinite(finalDiagA.kineticEnergy) && std::isfinite(finalDiagA.potentialEnergy) &&
              std::isfinite(finalDiagA.totalEnergy) && std::isfinite(finalDiagA.maxAngularSpeed) &&
              std::isfinite(finalDiagA.maxJointAnchorError) && std::isfinite(finalDiagA.maxJointLimitError),
          "A final diagnostics are finite");
    check(std::isfinite(finalDiagB.kineticEnergy) && std::isfinite(finalDiagB.potentialEnergy) &&
              std::isfinite(finalDiagB.totalEnergy) && std::isfinite(finalDiagB.maxAngularSpeed) &&
              std::isfinite(finalDiagB.maxJointAnchorError) && std::isfinite(finalDiagB.maxJointLimitError),
          "B final diagnostics are finite");
    check(maxAnchorA <= 0.05f && maxAnchorB <= 0.05f, "joint anchor error stays within the v1 threshold");
    check(maxLimitA <= 0.2f && maxLimitB <= 0.2f, "joint limit error stays within the v1 threshold");
    check(finalDiagA.stepIndex == finalDiagB.stepIndex, "A and B share the scene step index");

    std::printf("full-rig pose B: initial=(%.4f,%.4f,%.4f) final=(%.4f,%.4f,%.4f) maxBodyDisplacement=%.4f\n",
                initialBx, initialBy, initialBz, finalBx, finalBy, finalBz, displacementB);
    std::printf("full-rig contact evidence: frame=%llu BminY=%.4f Bx_contact=%.4f Bx_final=%.4f dB=%.4f "
                "foundA=%u foundB=%u pairsA=%u pairsB=%u\n",
                static_cast<unsigned long long>(contactFrame), contactBMinY, contactBx, finalBx, displacementB,
                contactFoundA, contactFoundB, contactPairsA, contactPairsB);
    std::printf("full-rig impulse evidence: frame=%llu impA=%.4f impB=%.4f maxA=%.4f maxB=%.4f\n",
                static_cast<unsigned long long>(impulseFrame), firstImpulseA, firstImpulseB, maxImpulseA,
                maxImpulseB);
    std::printf("full-rig stability: anchorA=%.5f anchorB=%.5f limitA=%.5f limitB=%.5f KE_A=%.4f KE_B=%.4f\n",
                maxAnchorA, maxAnchorB, maxLimitA, maxLimitB, finalDiagA.kineticEnergy, finalDiagB.kineticEnergy);
    std::printf("full-rig B events: found=%u persist=%u lost=%u\n", totalFoundB, totalPersistB, totalLostB);

    check(OgsrPhysX2RagdollDestroy(a, nullptr, 0) == 1, "full-contact destroy A");
    check(OgsrPhysX2RagdollIsAlive(a) == 0, "full-contact A is stale after destroy");
    check(OgsrPhysX2RagdollIsAlive(b) == 1, "full-contact B survives A destroy");
    for (std::uint64_t frame = 401; frame <= 460; ++frame)
        check(step(scene, 0.01f, frame, result), "full-contact post-destroy step");
    PoseSnapshot afterDestroyB{};
    check(snapshot(b, afterDestroyB) && snapshotFinite(afterDestroyB), "B pose stays finite after A destroy");
    check(OgsrPhysX2RagdollGetInfo(b, &infoB) == 1 && infoB.staticTriangles == 2,
          "static geometry survives A destroy");
    check(OgsrPhysX2RagdollGetDiagnostics(b, 0.0f, &finalDiagB) == 1 &&
              std::isfinite(finalDiagB.kineticEnergy),
          "B diagnostics stay valid after A destroy");
    check(OgsrPhysX2RagdollDestroy(b, nullptr, 0) == 1, "full-contact destroy B");
    check(OgsrPhysX2SceneDestroy(scene, nullptr, 0) == 1, "full-contact scene destroy");
}

void testContactLossOnDestroy()
{
    OgsrPhysX2SceneHandle scene{};
    check(createScene(sceneConfig(0.0f), scene), "contact-loss scene create");

    OgsrPhysX2RagdollHandle a{};
    OgsrPhysX2RagdollHandle b{};
    check(createSingleBody(scene, Vec3{-0.24f, 0.0f, 0.0f}, 0.25f, a), "contact-loss A create");
    check(createSingleBody(scene, Vec3{0.24f, 0.0f, 0.0f}, 0.25f, b), "contact-loss B create");

    OgsrPhysX2StepResult result{};
    for (std::uint64_t frame = 1; frame <= 5; ++frame)
        check(step(scene, 0.01f, frame, result), "contact-loss contact step");

    OgsrPhysXRagdollDiagnostics diagnosticsB{};
    check(OgsrPhysX2RagdollGetDiagnostics(b, 0.0f, &diagnosticsB) == 1, "contact-loss B diagnostics");
    check(diagnosticsB.contactFound > 0 || diagnosticsB.contactPersist > 0,
          "B is in contact before the loss");
    check(diagnosticsB.activeContactPairs > 0, "active pairs are nonzero before the loss");

    // Natural separation: push both bodies apart and expect TOUCH_LOST plus a
    // per-step active-pair count that returns to zero.
    const float pushA[3] = {-3.0f, 0.0f, 0.0f};
    const float pushB[3] = {3.0f, 0.0f, 0.0f};
    check(OgsrPhysX2RagdollApplyCentralImpulse(a, 0, pushA, nullptr, 0) == 1, "contact-loss push A");
    check(OgsrPhysX2RagdollApplyCentralImpulse(b, 0, pushB, nullptr, 0) == 1, "contact-loss push B");
    bool naturalLost = false;
    for (std::uint64_t frame = 6; frame <= 60; ++frame)
    {
        check(step(scene, 0.01f, frame, result), "contact-loss separation step");
        check(OgsrPhysX2RagdollGetDiagnostics(b, 0.0f, &diagnosticsB) == 1, "contact-loss B diagnostics");
        if (diagnosticsB.contactLost > 0)
            naturalLost = true;
    }
    check(naturalLost, "natural separation emits a contact-loss event");
    check(diagnosticsB.activeContactPairs == 0, "active pairs return to zero after separation");
    std::printf("contact-loss evidence: natural_lost=%d last_lost=%u active=%u\n", naturalLost ? 1 : 0,
                diagnosticsB.contactLost, diagnosticsB.activeContactPairs);
    check(OgsrPhysX2RagdollDestroy(a, nullptr, 0) == 1, "contact-loss destroy separated A");
    check(OgsrPhysX2RagdollDestroy(b, nullptr, 0) == 1, "contact-loss destroy separated B");

    // Destroy one instance while it is still touching the other. PhysX does not
    // guarantee TOUCH_LOST when an actor is released outside simulate(); the
    // contract is: no crash, no access to released shapes, and no phantom
    // active pairs on the surviving instance.
    OgsrPhysX2RagdollHandle c{};
    OgsrPhysX2RagdollHandle d{};
    check(createSingleBody(scene, Vec3{-0.24f, 0.0f, 0.0f}, 0.25f, c), "destroy-contact C create");
    check(createSingleBody(scene, Vec3{0.24f, 0.0f, 0.0f}, 0.25f, d), "destroy-contact D create");
    for (std::uint64_t frame = 61; frame <= 65; ++frame)
        check(step(scene, 0.01f, frame, result), "destroy-contact contact step");
    OgsrPhysXRagdollDiagnostics diagnosticsD{};
    check(OgsrPhysX2RagdollGetDiagnostics(d, 0.0f, &diagnosticsD) == 1, "destroy-contact D diagnostics");
    check(diagnosticsD.contactFound > 0 || diagnosticsD.contactPersist > 0,
          "D is in contact before C destroy");
    check(diagnosticsD.activeContactPairs > 0, "D active pairs are nonzero before C destroy");

    check(OgsrPhysX2RagdollDestroy(c, nullptr, 0) == 1, "destroy-contact destroy C");
    check(step(scene, 0.01f, 66, result), "destroy-contact step after C destroy");
    check(OgsrPhysX2RagdollGetDiagnostics(d, 0.0f, &diagnosticsD) == 1,
          "destroy-contact D diagnostics after destroy");
    check(diagnosticsD.activeContactPairs == 0, "D active pairs drop to zero after C destroy");
    check(OgsrPhysX2RagdollIsAlive(d) == 1, "D survives the destroy-contact");
    std::printf("destroy-contact evidence: lost=%u active=%u\n", diagnosticsD.contactLost,
                diagnosticsD.activeContactPairs);

    for (std::uint64_t frame = 67; frame <= 70; ++frame)
        check(step(scene, 0.01f, frame, result), "destroy-contact settle step");
    check(OgsrPhysX2RagdollGetDiagnostics(d, 0.0f, &diagnosticsD) == 1 &&
              diagnosticsD.activeContactPairs == 0,
          "no phantom active pairs remain after C destroy");
    check(poseFinite(d), "D pose stays finite after the destroy-contact");
    check(OgsrPhysX2RagdollDestroy(d, nullptr, 0) == 1, "destroy-contact destroy D");
    check(OgsrPhysX2SceneDestroy(scene, nullptr, 0) == 1, "contact-loss scene destroy");
}

void testMaterialConflictRejected()
{
    OgsrPhysX2SceneHandle scene{};
    check(createScene(sceneConfig(), scene), "material-conflict scene create");
    check(setFloor(scene), "material-conflict floor upload");

    OgsrPhysX2RagdollHandle rejected{};
    char error[512] = {};
    check(!createSingleBody(scene, Vec3{0.0f, 1.0f, 0.0f}, 0.25f, rejected, 0.2f, error, sizeof(error)),
          "conflicting scene material id is rejected");
    check(std::string(error).find("material") != std::string::npos,
          "material conflict diagnostic names the material");
    check(OgsrPhysX2RagdollIsAlive(rejected) == 0, "material conflict publishes no handle");
    check(OgsrPhysX2SceneIsAlive(scene) == 1, "scene survives the material conflict");

    OgsrPhysX2RagdollHandle accepted{};
    check(createSingleBody(scene, Vec3{0.0f, 1.0f, 0.0f}, 0.25f, accepted),
          "matching scene material id is accepted");
    check(OgsrPhysX2RagdollDestroy(accepted, nullptr, 0) == 1, "material-conflict body destroy");
    check(OgsrPhysX2SceneDestroy(scene, nullptr, 0) == 1, "material-conflict scene destroy");
}

void testStaticFragments()
{
    OgsrPhysX2SceneHandle scene{};
    check(createScene(sceneConfig(), scene), "fragment scene create");

    std::vector<float> leftVertices;
    std::vector<float> rightVertices;
    std::vector<std::uint32_t> indices;
    floorMesh(-10.0f, 0.0f, -10.0f, 10.0f, leftVertices, indices);
    floorMesh(0.0f, 10.0f, -10.0f, 10.0f, rightVertices, indices);
    char error[512] = {};
    check(OgsrPhysX2SceneAddStaticFragment(scene, 1, leftVertices.data(),
                                           static_cast<std::uint32_t>(leftVertices.size() / 3), indices.data(),
                                           static_cast<std::uint32_t>(indices.size()), error, sizeof(error)) == 1,
          "left fragment");
    check(OgsrPhysX2SceneAddStaticFragment(scene, 2, rightVertices.data(),
                                           static_cast<std::uint32_t>(rightVertices.size() / 3), indices.data(),
                                           static_cast<std::uint32_t>(indices.size()), error, sizeof(error)) == 1,
          "right fragment");

    OgsrPhysX2RagdollHandle a{};
    OgsrPhysX2RagdollHandle b{};
    check(createSingleBody(scene, Vec3{-0.5f, 1.0f, 0.0f}, 0.25f, a), "fragment A create");
    check(createSingleBody(scene, Vec3{0.5f, 1.0f, 0.0f}, 0.25f, b), "fragment B create");
    OgsrPhysX2StepResult result{};
    for (std::uint64_t frame = 1; frame <= 150; ++frame)
        check(step(scene, 0.01f, frame, result), "fragment drop step");
    check(poseY(a) > 0.1f && poseY(a) < 0.6f, "A rests on left fragment");
    check(poseY(b) > 0.1f && poseY(b) < 0.6f, "B rests on right fragment");

    OgsrPhysXRagdollInfo info{};
    check(OgsrPhysX2RagdollGetInfo(a, &info) == 1, "fragment info");
    const std::uint32_t trianglesBefore = info.staticTriangles;
    check(trianglesBefore == 4, "two fragments contribute four triangles");

    std::vector<std::uint32_t> badIndices = {0, 1, 2, 0, 2, 99};
    check(OgsrPhysX2SceneAddStaticFragment(scene, 1, leftVertices.data(),
                                           static_cast<std::uint32_t>(leftVertices.size() / 3), badIndices.data(),
                                           static_cast<std::uint32_t>(badIndices.size()), error, sizeof(error)) == 0,
          "invalid fragment replacement rejected");
    check(OgsrPhysX2RagdollGetInfo(a, &info) == 1 && info.staticTriangles == trianglesBefore,
          "failed replacement keeps old geometry");
    for (std::uint64_t frame = 151; frame <= 160; ++frame)
        check(step(scene, 0.01f, frame, result), "post-failure step");
    check(poseY(a) > 0.1f && poseY(b) > 0.1f, "both bodies still supported after failure");

    check(OgsrPhysX2SceneAddStaticFragment(scene, 1, leftVertices.data(),
                                           static_cast<std::uint32_t>(leftVertices.size() / 3), indices.data(),
                                           static_cast<std::uint32_t>(indices.size()), error, sizeof(error)) == 1,
          "valid fragment replacement");
    check(OgsrPhysX2RagdollGetInfo(a, &info) == 1 && info.staticTriangles == trianglesBefore,
          "valid replacement keeps triangle count");
    check(OgsrPhysX2RagdollDestroy(a, nullptr, 0) == 1, "fragment A destroy");
    check(OgsrPhysX2RagdollIsAlive(b) == 1, "fragment B survives A destroy");
    check(OgsrPhysX2RagdollDestroy(b, nullptr, 0) == 1, "fragment B destroy");
    check(OgsrPhysX2SceneDestroy(scene, nullptr, 0) == 1, "fragment scene destroy");
}

void testFrameRates()
{
    auto runRate = [](float delta, std::uint32_t expectedSteps, const char* message) {
        OgsrPhysX2SceneHandle scene{};
        check(createScene(sceneConfig(0.0f, false), scene), "frame-rate scene create");
        OgsrPhysX2RagdollHandle body{};
        check(createSingleBody(scene, Vec3{0.0f, 0.0f, 0.0f}, 0.25f, body), "frame-rate body create");
        OgsrPhysX2StepResult result{};
        check(step(scene, delta, 1, result), message);
        check(result.steps == expectedSteps, message);
        check(result.stepIndex == expectedSteps, "frame-rate step index matches executed steps");
        check(OgsrPhysX2RagdollDestroy(body, nullptr, 0) == 1, "frame-rate body destroy");
        check(OgsrPhysX2SceneDestroy(scene, nullptr, 0) == 1, "frame-rate scene destroy");
    };

    runRate(1.0f / 30.0f, 3, "30 FPS executes three steps");
    runRate(1.0f / 60.0f, 1, "60 FPS executes one step");
    runRate(1.0f / 120.0f, 0, "120 FPS accumulates without simulating");

    OgsrPhysX2SceneHandle scene{};
    check(createScene(sceneConfig(0.0f, false), scene), "large-delta scene create");
    OgsrPhysX2RagdollHandle body{};
    check(createSingleBody(scene, Vec3{0.0f, 0.0f, 0.0f}, 0.25f, body), "large-delta body create");
    OgsrPhysX2StepResult result{};
    check(step(scene, 0.1f, 1, result), "large delta step");
    check(result.steps == 4 && result.droppedSteps > 0, "large delta is capped and dropped");
    const std::uint64_t largeStepIndex = result.stepIndex;
    check(step(scene, 0.1f, 1, result), "large delta same frame id");
    check(result.stepIndex == largeStepIndex && result.steps == 4, "same frame id returns cached result");
    check(step(scene, 0.0f, 2, result), "zero delta step");
    check(result.steps == 0 && result.stepIndex == largeStepIndex, "zero delta does not simulate");
    check(!step(scene, 0.01f, 0, result), "decreasing frame id after cache is rejected");
    check(OgsrPhysX2RagdollDestroy(body, nullptr, 0) == 1, "large-delta body destroy");
    check(OgsrPhysX2SceneDestroy(scene, nullptr, 0) == 1, "large-delta scene destroy");
}

void testInvalidInputs()
{
    OgsrPhysX2SceneHandle scene{};
    check(OgsrPhysX2SceneCreate(nullptr, &scene, nullptr, 0) == 0, "null scene config rejected");
    OgsrPhysX2SceneConfig bad{};
    bad.structSize = 4;
    check(OgsrPhysX2SceneCreate(&bad, &scene, nullptr, 0) == 0, "small scene config rejected");
    bad = sceneConfig();
    bad.fixedTimestep = -1.0f;
    check(OgsrPhysX2SceneCreate(&bad, &scene, nullptr, 0) == 0, "negative timestep rejected");
    check(createScene(sceneConfig(), scene), "invalid-input scene create");

    OgsrPhysX2RagdollHandle ragdoll{};
    check(OgsrPhysX2RagdollCreate(scene, nullptr, nullptr, &ragdoll, nullptr, 0) == 0,
          "null definition/config rejected");
    OgsrPhysXRagdollDefinition emptyDefinition{};
    OgsrPhysX2RagdollInstanceConfig config = instanceConfig();
    config.structSize = 4;
    check(OgsrPhysX2RagdollCreate(scene, &emptyDefinition, &config, &ragdoll, nullptr, 0) == 0,
          "small instance config rejected");
    config = instanceConfig();
    check(OgsrPhysX2RagdollCreate(scene, &emptyDefinition, &config, &ragdoll, nullptr, 0) == 0,
          "empty definition rejected");
    check(OgsrPhysX2SceneIsAlive(scene) == 1, "scene survives invalid create");

    check(OgsrPhysX2RagdollIsAlive(ragdoll) == 0, "zero ragdoll handle invalid");
    check(OgsrPhysX2RagdollDestroy(ragdoll, nullptr, 0) == 0, "zero ragdoll destroy rejected");
    check(OgsrPhysX2SceneIsAlive(OgsrPhysX2SceneHandle{}) == 0, "zero scene handle invalid");
    check(OgsrPhysX2RagdollGetPose(ragdoll, nullptr, nullptr, 0) == 0, "null pose rejected");
    check(OgsrPhysX2RagdollGetInfo(ragdoll, nullptr) == 0, "null info rejected");
    check(OgsrPhysX2RagdollGetDiagnostics(ragdoll, 0.0f, nullptr) == 0, "null diagnostics rejected");

    OgsrPhysX2StepResult result{};
    result.structSize = sizeof(result);
    check(OgsrPhysX2SceneStep(scene, 0.01f, 1, nullptr, nullptr, 0) == 0, "null step result rejected");
    check(OgsrPhysX2SceneStep(scene, -1.0f, 1, &result, nullptr, 0) == 0, "negative delta rejected");
    check(OgsrPhysX2SceneSetStaticMesh(scene, nullptr, 0, nullptr, 0, nullptr, 0) == 0,
          "null static mesh rejected");
    check(OgsrPhysX2RagdollApplyCentralImpulse(ragdoll, 0, nullptr, nullptr, 0) == 0,
          "null central impulse rejected");
    check(OgsrPhysX2SceneDestroy(scene, nullptr, 0) == 1, "invalid-input scene cleanup");
}

void testPartialFailure()
{
    OgsrPhysX2SceneHandle scene{};
    check(createScene(sceneConfig(), scene), "partial-failure scene create");
    check(setFloor(scene), "partial-failure floor");
    OgsrPhysX2RagdollHandle a{};
    check(createSingleBody(scene, Vec3{-1.0f, 1.0f, 0.0f}, 0.25f, a), "partial-failure A create");

    OgsrPhysXRagdollDefinition invalid{};
    OgsrPhysX2RagdollInstanceConfig config = instanceConfig();
    OgsrPhysX2RagdollHandle failed{};
    check(OgsrPhysX2RagdollCreate(scene, &invalid, &config, &failed, nullptr, 0) == 0,
          "invalid B create rejected");
    check(OgsrPhysX2RagdollIsAlive(a) == 1, "A survives failed B create");
    OgsrPhysX2StepResult result{};
    check(step(scene, 0.01f, 1, result), "A steps after failed B create");
    check(poseFinite(a), "A pose finite after failed B create");

    OgsrPhysX2RagdollHandle b{};
    check(createSingleBody(scene, Vec3{1.0f, 1.0f, 0.0f}, 0.25f, b), "B create after failure");
    check(OgsrPhysX2RagdollIsAlive(b) == 1, "B alive after recovery");
    check(OgsrPhysX2RagdollDestroy(a, nullptr, 0) == 1, "partial-failure A destroy");
    check(OgsrPhysX2RagdollDestroy(b, nullptr, 0) == 1, "partial-failure B destroy");
    check(OgsrPhysX2SceneDestroy(scene, nullptr, 0) == 1, "partial-failure scene destroy");
}

void testFaultInjection()
{
    OgsrPhysX2SceneHandle scene{};
    check(createScene(sceneConfig(), scene), "fault-injection scene create");
    OgsrPhysX2RagdollHandle body{};
    check(createSingleBody(scene, Vec3{0.0f, 1.0f, 0.0f}, 0.25f, body), "fault-injection body create");

    SetEnvironmentVariableA("OGSR_PHYSX2_FAULT_RAGDOLL_DESTROY", "1");
    char error[512] = {};
    check(OgsrPhysX2RagdollDestroy(body, error, sizeof(error)) == 0,
          "faulted RagdollDestroy is rejected");
    check(contains(error, "fault injection"), "faulted RagdollDestroy names the injection");
    check(OgsrPhysX2RagdollIsAlive(body) == 1, "instance stays alive after a failed destroy");
    OgsrPhysX2StepResult result{};
    check(step(scene, 0.01f, 1, result), "scene still steps after a failed instance destroy");
    SetEnvironmentVariableA("OGSR_PHYSX2_FAULT_RAGDOLL_DESTROY", nullptr);
    check(OgsrPhysX2RagdollDestroy(body, nullptr, 0) == 1,
          "RagdollDestroy succeeds after the fault is cleared");
    check(OgsrPhysX2RagdollIsAlive(body) == 0, "instance is invalid after fault recovery");
    check(OgsrPhysX2SceneDestroy(scene, nullptr, 0) == 1, "fault-injection scene destroy");
}

} // namespace

int main(int argc, char** argv)
{
    g_fixturePath = argc > 1 ? argv[1] : "artifacts/fixtures-p1/ragdoll_sim_default_stalker.json";
    std::printf("fixture: %s\n", g_fixturePath.c_str());
    testTwoRagdollsOneScene();
    testTwoScenesIndependent();
    testSceneDestroyWithInstances();
    testContactImpulse();
    testTwoFullRagdollsContact();
    testContactLossOnDestroy();
    testMaterialConflictRejected();
    testFaultInjection();
    testStaticFragments();
    testFrameRates();
    testInvalidInputs();
    testPartialFailure();

    std::printf("physics_physx_v2_multi tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
