#include "dx12cpuscene.h"
#include "dx12log.h"
#include "gr/jobs/JobSystem.h"
#include <vector>
#include <memory>
#include <cmath>
#include <limits>
#include <cstring>

namespace dx12cpuscene
{
bool RunSelfTests()
{
    unsigned checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) { ++failures; dx12log::Write("[cpu-selftest] FAIL: %s", message); }
    };
    for (uint32_t workers : {0u, 1u, 2u, 4u, 8u})
    {
        gr::jobs::JobSystem jobs;
        gr::jobs::JobConfig config;
        config.backgroundWorkers = workers;
        config.automaticWorkers = false;
        check(jobs.Initialize(config), "initialize");
        for (uint32_t count : {256u, 1024u, 4096u, 16384u})
        {
            std::vector<ObjectDesc> objects(count);
            BuildObjectTable(objects.data(), count, 0xC0FFEE);
            const auto original = objects;
            std::vector<ObjectState> serial(count), parallel(count + 2);
            auto writes = std::make_unique<std::atomic<uint32_t>[]>(count);
            // Защитные элементы проверяют границы выходного диапазона.
            std::memset(&parallel.front(), 0x39, sizeof(ObjectState));
            std::memset(&parallel.back(), 0x39, sizeof(ObjectState));
            const auto guard = parallel.front();
            for (float time : {0.0f, 1.25f, 50.0f})
            {
                CameraSnapshot camera{};
                BuildCameraSnapshot(camera, time, time == 1.25f ? 4.0f / 3.0f : 16.0f / 9.0f);
                const auto reference = PrepareSerial(serial.data(), objects.data(), count, camera, time);
                for (uint32_t grain : {1u, 64u, 65536u})
                {
                    for (uint32_t i = 0; i < count; ++i) writes[i] = 0;
                    const auto result = PrepareParallel(jobs, parallel.data() + 1, objects.data(),
                        count, camera, time, grain, writes.get());
                    float deviation = 0;
                    check(CompareStates(serial.data(), parallel.data() + 1, count, 0, &deviation) == 0,
                        "serial/parallel matrices and visibility");
                    check(reference.checksum == result.checksum && reference.visible == result.visible,
                        "checksum and visible count");
                    bool once = true;
                    for (uint32_t i = 0; i < count; ++i) once &= writes[i].load() == 1;
                    check(once, "exactly one writer per object");
                    check(std::memcmp(&parallel.front(), &guard, sizeof(guard)) == 0 &&
                        std::memcmp(&parallel.back(), &guard, sizeof(guard)) == 0, "output guards");
                }
                check(std::memcmp(objects.data(), original.data(), count * sizeof(ObjectDesc)) == 0,
                    "immutable object input");
                // Независимый oracle: точка (радиус=0) проверяется в clip space,
                // а не повторным использованием шести извлечённых плоскостей.
                auto points = objects;
                for (auto& point : points) { point.scale = 0; point.orbitRadius *= 4; }
                PrepareSerial(serial.data(), points.data(), count, camera, time);
                const auto vp = DirectX::XMLoadFloat4x4(&camera.viewProj);
                bool correct = true;
                unsigned inside = 0;
                for (uint32_t i = 0; i < count; ++i)
                {
                    const auto& m = serial[i].world;
                    DirectX::XMFLOAT4 clip;
                    DirectX::XMStoreFloat4(&clip, DirectX::XMVector4Transform(
                        DirectX::XMVectorSet(m._41, m._42, m._43, 1), vp));
                    const bool visible = clip.x >= -clip.w && clip.x <= clip.w &&
                        clip.y >= -clip.w && clip.y <= clip.w && clip.z >= 0 && clip.z <= clip.w;
                    correct &= serial[i].visible == static_cast<uint32_t>(visible);
                    inside += visible ? 1u : 0u;
                    correct &= std::abs(m._41 - std::cos(time * points[i].orbitSpeed + points[i].phase) *
                        points[i].orbitRadius) < 1e-4f && m._44 == 1;
                }
                check(correct && inside > 0 && inside < count, "clip-space oracle and world translation");
            }
        }
        const auto stats = jobs.GetStats();
        check(stats.tasksSubmitted == stats.tasksCompleted && stats.jobFailures == 0, "drained tasks");
        jobs.Shutdown();
        check(jobs.GetTotalParticipantCount() == 0, "shutdown");
    }
    ObjectState a{}, b{};
    b.world._11 = std::numeric_limits<float>::quiet_NaN();
    check(CompareStates(&a, &b, 1, 1e-4f, nullptr) == 1, "NaN rejected");
    b.world._11 = std::numeric_limits<float>::infinity();
    check(CompareStates(&a, &b, 1, 1e-4f, nullptr) == 1, "infinity rejected");
    dx12log::Write("[cpu-selftest] %s checks=%u failures=%u workers=0,1,2,4,8 sizes=256,1024,4096,16384 grains=1,64,65536",
        failures ? "FAIL" : "PASS", checks, failures);
    return failures == 0;
}
}
