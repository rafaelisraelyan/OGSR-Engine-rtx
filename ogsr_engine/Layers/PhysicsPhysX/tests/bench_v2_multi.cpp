#include "physics_physx/physx_ragdoll_c.h"
#include "physics_physx/physx_ragdoll_c_v2.h"

#include "v2_fixture_builder.h"

#include "physics_portable/json.h"
#include "physics_portable/ragdoll.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
using physics_portable::RagdollCaptureDocument;
using physics_portable::Vec3;

constexpr float kFixedTimestep = 0.01f;
constexpr std::uint32_t kWarmupSteps = 100;
constexpr std::uint32_t kMeasuredSteps = 600;
constexpr std::uint32_t kPoseReads = 2000;

double nowMs()
{
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}

double privateBytesMB()
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters)))
        return 0.0;
    return static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
}

bool loadFixture(const char* path, RagdollCaptureDocument& document)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.good())
    {
        std::printf("cannot open fixture %s\n", path);
        return false;
    }
    const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::string error;
    if (!physics_portable::parseRagdollCapture(json, document, &error))
    {
        std::printf("cannot parse fixture: %s\n", error.c_str());
        return false;
    }
    return true;
}

OgsrPhysX2SceneConfig sceneConfig()
{
    OgsrPhysX2SceneConfig config{};
    config.structSize = sizeof(config);
    config.fixedTimestep = kFixedTimestep;
    config.gravityY = -19.62f;
    config.workerThreads = 2;
    config.maxSubsteps = 4;
    config.solverPositionIterations = 16;
    config.solverVelocityIterations = 4;
    config.contactOffset = 0.02f;
    config.restOffset = 0.0f;
    config.enableStabilization = 1;
    config.collectContacts = 1;
    config.maxInstances = OGSR_PHYSX2_INSTANCE_CAPACITY;
    return config;
}

OgsrPhysX2RagdollInstanceConfig instanceConfig()
{
    OgsrPhysX2RagdollInstanceConfig config{};
    config.structSize = sizeof(config);
    config.linearDamping = 0.05f;
    config.angularDamping = 0.2f;
    config.staticRootDensity = 300.0f;
    config.promoteStaticRoot = 1;
    config.disableSelfCollision = 1;
    config.collisionGroup = OGSR_PHYSX2_GROUP_RAGDOLL;
    config.collisionMask = OGSR_PHYSX2_GROUP_RAGDOLL | OGSR_PHYSX2_GROUP_STATIC;
    return config;
}

bool setFloor(OgsrPhysX2SceneHandle scene)
{
    const float vertices[] = {-12.0f, 0.0f, -12.0f, 12.0f, 0.0f, -12.0f,
                              12.0f,  0.0f, 12.0f,  -12.0f, 0.0f, 12.0f};
    const std::uint32_t indices[] = {0, 1, 2, 0, 2, 3};
    char error[512] = {};
    return OgsrPhysX2SceneSetStaticMesh(scene, vertices, 4, indices, 6, error, sizeof(error)) == 1;
}

Vec3 spawnPosition(std::uint32_t index)
{
    return Vec3{static_cast<float>(index % 4) * 2.0f - 3.0f, 3.0f,
                static_cast<float>(index / 4) * 2.0f - 1.0f};
}

struct Measurement
{
    std::uint32_t ragdolls = 0;
    std::uint32_t bodies = 0;
    std::uint32_t joints = 0;
    double sceneCreateMs = 0.0;
    double createMsPerInstance = 0.0;
    double destroyMsPerInstance = 0.0;
    double stepAverageMs = 0.0;
    double stepP95Ms = 0.0;
    double stepMaxMs = 0.0;
    double poseReadUs = 0.0;
    std::uint32_t activeContactPairsMax = 0;
    double maxContactImpulse = 0.0;
    std::uint64_t droppedSteps = 0;
    std::uint64_t executedSteps = 0;
    double memoryAfterSceneMB = 0.0;
    double memoryAfterInstancesMB = 0.0;
    double memoryAfterDestroyMB = 0.0;
};

bool runCase(std::uint32_t ragdollCount, const RagdollCaptureDocument& document, Measurement& out)
{
    out = Measurement{};
    out.ragdolls = ragdollCount;
    out.memoryAfterSceneMB = privateBytesMB();

    const OgsrPhysX2SceneConfig config = sceneConfig();
    OgsrPhysX2SceneHandle scene{};
    char error[512] = {};
    const double sceneStart = nowMs();
    if (OgsrPhysX2SceneCreate(&config, &scene, error, sizeof(error)) != 1)
    {
        std::printf("SceneCreate failed: %s\n", error);
        return false;
    }
    out.sceneCreateMs = nowMs() - sceneStart;
    if (!setFloor(scene))
    {
        std::printf("SceneSetStaticMesh failed\n");
        OgsrPhysX2SceneDestroy(scene, nullptr, 0);
        return false;
    }
    out.memoryAfterSceneMB = privateBytesMB();

    const Vec3 base = document.initial.entityWorld.translation;
    const OgsrPhysX2RagdollInstanceConfig instance = instanceConfig();
    std::vector<physics_physx_tests::V2Fixture> fixtures(ragdollCount);
    std::vector<OgsrPhysX2RagdollHandle> handles(ragdollCount);

    const double createStart = nowMs();
    for (std::uint32_t index = 0; index < ragdollCount; ++index)
    {
        const Vec3 desired = spawnPosition(index);
        const Vec3 offset{desired.x - base.x, desired.y - base.y, desired.z - base.z};
        fixtures[index].build(document.description, document.initial, offset);
        if (OgsrPhysX2RagdollCreate(scene, &fixtures[index].definition, &instance, &handles[index], error,
                                    sizeof(error)) != 1)
        {
            std::printf("RagdollCreate %u failed: %s\n", index, error);
            for (std::uint32_t created = 0; created < index; ++created)
                OgsrPhysX2RagdollDestroy(handles[created], nullptr, 0);
            OgsrPhysX2SceneDestroy(scene, nullptr, 0);
            return false;
        }
    }
    out.createMsPerInstance = (nowMs() - createStart) / static_cast<double>(ragdollCount);
    out.memoryAfterInstancesMB = privateBytesMB();

    OgsrPhysXRagdollInfo info{};
    if (OgsrPhysX2RagdollGetInfo(handles.front(), &info) == 1)
    {
        out.bodies = info.bodyCount * ragdollCount;
        out.joints = info.jointCount * ragdollCount;
    }

    OgsrPhysX2StepResult result{};
    result.structSize = sizeof(result);
    std::uint64_t frame = 0;
    for (std::uint32_t step = 0; step < kWarmupSteps; ++step)
    {
        if (OgsrPhysX2SceneStep(scene, kFixedTimestep, ++frame, &result, error, sizeof(error)) != 1)
        {
            std::printf("warmup SceneStep failed: %s\n", error);
            return false;
        }
        // Contact diagnostics are sampled outside the timed loop so that the
        // per-step cost reflects simulation only.
        for (const OgsrPhysX2RagdollHandle handle : handles)
        {
            OgsrPhysXRagdollDiagnostics diagnostics{};
            if (OgsrPhysX2RagdollGetDiagnostics(handle, 0.0f, &diagnostics) != 1)
                continue;
            out.activeContactPairsMax = std::max(out.activeContactPairsMax, diagnostics.activeContactPairs);
            out.maxContactImpulse = std::max(out.maxContactImpulse,
                                             static_cast<double>(diagnostics.totalContactImpulse));
        }
    }

    std::vector<double> stepTimes;
    stepTimes.reserve(kMeasuredSteps);
    for (std::uint32_t step = 0; step < kMeasuredSteps; ++step)
    {
        const double start = nowMs();
        if (OgsrPhysX2SceneStep(scene, kFixedTimestep, ++frame, &result, error, sizeof(error)) != 1)
        {
            std::printf("measured SceneStep failed: %s\n", error);
            return false;
        }
        stepTimes.push_back(nowMs() - start);
        out.droppedSteps += result.droppedSteps;
        out.executedSteps += result.steps;
    }

    double total = 0.0;
    for (double value : stepTimes)
        total += value;
    out.stepAverageMs = total / static_cast<double>(stepTimes.size());
    std::vector<double> sorted = stepTimes;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t p95Index =
        std::min(sorted.size() - 1, static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(sorted.size()))) - 1);
    out.stepP95Ms = sorted[p95Index];
    out.stepMaxMs = sorted.back();

    std::vector<float> bodyWorld(OGSR_PHYSX_RAGDOLL_MAX_BODIES * 16);
    OgsrPhysXRagdollPose pose{};
    const double poseStart = nowMs();
    for (std::uint32_t read = 0; read < kPoseReads; ++read)
    {
        if (OgsrPhysX2RagdollGetPose(handles.front(), &pose, bodyWorld.data(), OGSR_PHYSX_RAGDOLL_MAX_BODIES) != 1)
        {
            std::printf("GetPose failed\n");
            return false;
        }
    }
    out.poseReadUs = (nowMs() - poseStart) * 1000.0 / static_cast<double>(kPoseReads);

    const double destroyStart = nowMs();
    for (const OgsrPhysX2RagdollHandle handle : handles)
    {
        if (OgsrPhysX2RagdollDestroy(handle, nullptr, 0) != 1)
            std::printf("RagdollDestroy failed\n");
    }
    out.destroyMsPerInstance = (nowMs() - destroyStart) / static_cast<double>(ragdollCount);
    out.memoryAfterDestroyMB = privateBytesMB();
    OgsrPhysX2SceneDestroy(scene, nullptr, 0);
    return true;
}

void writeRow(std::FILE* file, const Measurement& row)
{
    std::fprintf(file, "%u,%u,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%u,%.3f,%llu,%llu,%.2f,%.2f,%.2f\n", row.ragdolls,
                 row.bodies, row.joints, row.stepAverageMs, row.stepP95Ms, row.stepMaxMs, row.createMsPerInstance,
                 row.destroyMsPerInstance, row.poseReadUs, row.activeContactPairsMax, row.maxContactImpulse,
                 static_cast<unsigned long long>(row.droppedSteps),
                 static_cast<unsigned long long>(row.executedSteps), row.memoryAfterSceneMB,
                 row.memoryAfterInstancesMB, row.memoryAfterDestroyMB);
}
} // namespace

int main(int argc, char** argv)
{
    const char* fixturePath = argc > 1 ? argv[1] : "artifacts/fixtures-p1/ragdoll_sim_default_stalker.json";
    const char* outputPath = argc > 2 ? argv[2] : "artifacts/p31b-review/performance.csv";

    RagdollCaptureDocument document;
    if (!loadFixture(fixturePath, document))
        return 1;

    std::ofstream output(outputPath, std::ios::binary);
    if (!output.good())
    {
        std::printf("cannot open output %s\n", outputPath);
        return 1;
    }
    output << "ragdolls,bodies,joints,step_avg_ms,step_p95_ms,step_max_ms,create_ms_per_instance,"
              "destroy_ms_per_instance,pose_read_us,active_contact_pairs_max,max_contact_impulse,dropped_steps,"
              "executed_steps,memory_after_scene_mb,memory_after_instances_mb,memory_after_destroy_mb\n";

    std::printf("fixture: %s\n", fixturePath);
    std::printf("ragdolls,bodies,joints,step_avg_ms,step_p95_ms,step_max_ms,create_ms_per_instance,"
                "destroy_ms_per_instance,pose_read_us,active_contact_pairs_max,max_contact_impulse,dropped_steps,"
                "executed_steps,memory_after_scene_mb,memory_after_instances_mb,memory_after_destroy_mb\n");

    const std::uint32_t counts[] = {1, 2, 4, 8};
    for (const std::uint32_t count : counts)
    {
        Measurement row;
        if (!runCase(count, document, row))
            return 1;
        writeRow(stdout, row);
        output << row.ragdolls << ',' << row.bodies << ',' << row.joints << ',' << row.stepAverageMs << ','
               << row.stepP95Ms << ',' << row.stepMaxMs << ',' << row.createMsPerInstance << ','
               << row.destroyMsPerInstance << ',' << row.poseReadUs << ',' << row.activeContactPairsMax << ','
               << row.maxContactImpulse << ',' << row.droppedSteps << ',' << row.executedSteps << ','
               << row.memoryAfterSceneMB << ',' << row.memoryAfterInstancesMB << ',' << row.memoryAfterDestroyMB
               << '\n';
        std::fflush(stdout);
    }
    output.close();
    return 0;
}
