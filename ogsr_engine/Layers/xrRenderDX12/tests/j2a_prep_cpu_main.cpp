#include "dx12packetplan.h"
#include "dx12renderprep.h"
#include "dx12scopeguard.h"

#include "gr/jobs/JobSystem.h"

#include <DirectXMath.h>

#include <chrono>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

double Ms(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start)
        .count();
}

bool TestScopeGuard()
{
    int calls = 0;
    {
        auto guard = dx12scope::MakeScopeGuard([&calls]() { ++calls; });
        (void)guard;
    }
    const bool normalOk = calls == 1;

    calls = 0;
    bool threw = false;
    try
    {
        auto guard = dx12scope::MakeScopeGuard([&calls]() { ++calls; });
        (void)guard;
        std::vector<uint8_t> allocation;
        allocation.resize(std::numeric_limits<size_t>::max());
    }
    catch (const std::length_error&)
    {
        threw = true;
    }
    catch (const std::bad_alloc&)
    {
        threw = true;
    }
    const bool exceptionOk = threw && calls == 1;
    std::printf("J2A_SCOPE_GUARD normal=%s exception=%s result=%s\n",
                normalOk ? "PASS" : "FAIL",
                exceptionOk ? "PASS" : "FAIL",
                (normalOk && exceptionOk) ? "PASS" : "FAIL");
    return normalOk && exceptionOk;
}

renderprep12::Object12 MakeObject(uint64_t id)
{
    renderprep12::Object12 object;
    object.id = id;
    object.firstIndex = 0;
    object.indexCount = 36;
    object.baseVertex = 0;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            object.world.m[row][column] = row == column ? 1.0f : 0.0f;
    object.minimum = {-1.0f, -1.0f, -1.0f};
    object.maximum = {1.0f, 1.0f, 1.0f};
    object.sphere.center = {0.0f, 0.0f, 0.0f};
    object.sphere.radius = 1.8f;
    object.visibility.sectorId = 0;
    object.visibility.mask = ~0u;
    object.visual = {1, 1};
    object.material = {1, 1};
    object.pso = {1, 1};
    return object;
}
} // namespace

int main()
{
    const bool guardOk = TestScopeGuard();
    constexpr uint32_t kObjects = 512;
    std::vector<renderprep12::Object12> objects;
    objects.reserve(kObjects);
    for (uint32_t i = 0; i < kObjects; ++i)
        objects.push_back(MakeObject(i + 1));

    const DirectX::XMFLOAT4X4 view = [] {
        DirectX::XMFLOAT4X4 result;
        const DirectX::XMMATRIX m = DirectX::XMMatrixLookAtLH(
            DirectX::XMVectorSet(0.0f, 0.0f, -10.0f, 1.0f),
            DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),
            DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
        DirectX::XMStoreFloat4x4(&result, m);
        return result;
    }();
    const DirectX::XMFLOAT4X4 projection = [] {
        DirectX::XMFLOAT4X4 result;
        const DirectX::XMMATRIX m = DirectX::XMMatrixPerspectiveFovLH(
            DirectX::XMConvertToRadians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
        DirectX::XMStoreFloat4x4(&result, m);
        return result;
    }();

    gr::jobs::JobSystem jobs;
    gr::jobs::JobConfig config;
    config.backgroundWorkers = 0;
    config.automaticWorkers = true;
    config.maxAutoBackgroundWorkers = 4;
    config.defaultGrainSize = 16;
    config.enableStats = true;
    if (!jobs.Initialize(config))
    {
        std::printf("J2A_PREP_BENCH result=FAIL reason=job-system-init\n");
        return 1;
    }

    const renderprep12::RenderSceneSnapshot12 snapshot(
        view, projection, objects, 1);
    bool ok = true;
    double serialMs = 0.0;
    double parallelMs = 0.0;
    uint32_t packets = 0;
    uint32_t visible = 0;
    for (int iteration = 0; iteration < 20; ++iteration)
    {
        renderprep12::OrderingTimings12 serialTiming;
        renderprep12::OrderingTimings12 parallelTiming;
        renderprep12::OrderingTimings12 verifyTiming;
        const auto start = Clock::now();
        const renderprep12::OrderedFrame12 serial =
            renderprep12::PrepareOrdered(snapshot, 1,
                                         renderprep12::Mode::Serial, nullptr,
                                         serialTiming);
        serialMs += Ms(start);
        const auto parallelStart = Clock::now();
        const renderprep12::OrderedFrame12 parallel =
            renderprep12::PrepareOrdered(snapshot, 1,
                                         renderprep12::Mode::Parallel, &jobs,
                                         parallelTiming);
        parallelMs += Ms(parallelStart);
        const renderprep12::OrderedFrame12 verify =
            renderprep12::PrepareOrdered(snapshot, 1,
                                         renderprep12::Mode::Verify, &jobs,
                                         verifyTiming);
        if (!renderprep12::Equivalent(serial, parallel) ||
            !renderprep12::Equivalent(serial, verify))
            ok = false;
        const renderprep12::FrameBatchPlan12 plan =
            renderprep12::BuildPacketBatchPlan(parallel, 4, 8);
        if (!renderprep12::ValidatePacketBatchPlan(parallel, plan).ok)
            ok = false;
        packets = static_cast<uint32_t>(parallel.frame.packets.size());
        visible = 0;
        for (const renderprep12::DrawPacket12& packet :
             parallel.frame.packets)
            if (packet.visible)
                ++visible;
    }
    jobs.Shutdown();
    std::printf(
        "J2A_PREP_BENCH result=%s objects=%u packets=%u visible=%u "
        "serial_ms=%.3f parallel_ms=%.3f guard=%s\n",
        (ok && guardOk) ? "PASS" : "FAIL", kObjects, packets, visible,
        serialMs / 20.0, parallelMs / 20.0,
        guardOk ? "PASS" : "FAIL");
    return (ok && guardOk) ? 0 : 1;
}
