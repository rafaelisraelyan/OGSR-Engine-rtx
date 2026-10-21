#include "dx12geometrycatalog.h"
#include "dx12geometry_tests.h"
#include "dx12renderprep.h"
#include "dx12camera.h"
#include "gr/jobs/JobSystem.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace smokegeometry12
{
namespace
{
struct TestState
{
    unsigned checks = 0;
    unsigned failures = 0;

    void Expect(bool condition, const char* name)
    {
        ++checks;
        if (!condition)
        {
            ++failures;
            std::printf("[geometry] FAIL: %s\n", name);
        }
    }
};

std::vector<renderprep12::Object12> MakeObjects(const GeometryCatalog12& catalog,
                                                uint32_t count)
{
    std::vector<renderprep12::Object12> objects;
    objects.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        const MeshDesc12& mesh = catalog.At(i % catalog.Count());
        renderprep12::Object12 object{};
        object.id = static_cast<uint64_t>(i) + 1;
        object.firstVertex = 0;
        object.vertexCount = 0;
        object.firstIndex = mesh.submesh.firstIndex;
        object.indexCount = mesh.submesh.indexCount;
        object.baseVertex = mesh.submesh.baseVertex;
        object.visual = mesh.handle;
        object.material = renderprep12::SyntheticResources12[0];
        object.pso = renderprep12::SyntheticResources12[0];
        object.minimum = mesh.minimum;
        object.maximum = mesh.maximum;
        object.sphere = mesh.sphere;
        object.flags = 0;
        object.opacity = 1.0f;
        const DirectX::XMMATRIX world =
            DirectX::XMMatrixScaling(0.7f, 0.7f, 0.7f) *
            DirectX::XMMatrixRotationY(0.1f * static_cast<float>(i % 7)) *
            DirectX::XMMatrixTranslation(
                (static_cast<float>(i % 8) - 3.5f) * 0.6f,
                (static_cast<float>(i / 8) - 3.5f) * 0.6f,
                static_cast<float>(i % 2) * 0.25f);
        DirectX::XMStoreFloat4x4(&object.world, world);
        objects.push_back(object);
    }
    return objects;
}

renderprep12::OrderedFrame12 PrepareScene(
    const GeometryCatalog12& catalog, uint32_t count,
    renderprep12::Mode mode, gr::jobs::JobSystem* jobs)
{
    SmokeCamera12 camera;
    camera.SetAspect(16.0f / 9.0f);
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 projection;
    camera.CaptureViewProjectionPair(view, projection);
    const std::vector<renderprep12::Object12> objects = MakeObjects(catalog, count);
    const renderprep12::RenderSceneSnapshot12 snapshot(
        view, projection, objects, 42);
    renderprep12::OrderingTimings12 timing;
    return renderprep12::PrepareOrdered(snapshot, 42, mode, jobs, timing);
}

void TestCatalog(TestState& test)
{
    GeometryCatalog12 catalog;
    test.Expect(catalog.Count() == 3, "catalogue has three meshes");
    const renderprep12::Resource12 handles[] = {
        {1, 1}, {2, 3}, {3, 2}};
    for (uint32_t i = 0; i < 3; ++i)
    {
        const MeshDesc12* mesh = catalog.Find(handles[i]);
        test.Expect(mesh != nullptr, "exact geometry handle found");
        test.Expect(catalog.IndexOf(handles[i]) == static_cast<int32_t>(i),
                    "geometry index stable");
        test.Expect(mesh->submesh.indexCount == mesh->indexCount,
                    "submesh covers mesh indices");
    }
    test.Expect(catalog.Find({1, 2}) == nullptr,
                "stale generation rejected");
    test.Expect(catalog.Find({99, 1}) == nullptr,
                "unknown geometry id rejected");
    test.Expect(catalog.IndexOf({2, 1}) == -1,
                "unknown geometry generation index");
    test.Expect(catalog.At(0).vertexView.BufferLocation == 0,
                "CPU catalogue views start empty");
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = 0x1000;
    vbv.SizeInBytes = 128;
    vbv.StrideInBytes = 28;
    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = 0x2000;
    ibv.SizeInBytes = 256;
    ibv.Format = DXGI_FORMAT_R32_UINT;
    catalog.SetViews(0, vbv, ibv);
    test.Expect(catalog.At(0).vertexView.BufferLocation == 0x1000,
                "catalogue views are owner-updatable");
    test.Expect(catalog.At(0).indexView.Format == DXGI_FORMAT_R32_UINT,
                "index view format preserved");
}

void TestIndexValidation(TestState& test)
{
    GeometryCatalog12 catalog;
    const char* reason = nullptr;
    for (uint32_t meshIndex = 0; meshIndex < catalog.Count(); ++meshIndex)
    {
        const MeshDesc12& mesh = catalog.At(meshIndex);
        std::vector<uint32_t> valid(mesh.indexCount);
        for (uint32_t i = 0; i < mesh.indexCount; ++i)
            valid[i] = i % mesh.vertexCount;
        test.Expect(ValidateIndexData(mesh, valid.data(), valid.size(), &reason),
                    "valid mesh indices accepted");

        std::vector<uint32_t> outside = valid;
        outside[0] = mesh.vertexCount;
        test.Expect(!ValidateIndexData(mesh, outside.data(), outside.size(), &reason),
                    "index outside vertex count rejected");
    }

    const MeshDesc12& cube = catalog.At(0);
    std::vector<uint32_t> validBase(cube.indexCount);
    for (uint32_t i = 0; i < cube.indexCount; ++i)
        validBase[i] = i % 7; // baseVertex=+1 -> vertices 1..7
    MeshDesc12 positiveBase = cube;
    positiveBase.submesh.baseVertex = 1;
    test.Expect(ValidateIndexData(positiveBase, validBase.data(),
                                  validBase.size(), &reason),
                "positive baseVertex accounted for");

    std::vector<uint32_t> badPositive = validBase;
    badPositive[0] = 7; // 7 + 1 == vertexCount
    test.Expect(!ValidateIndexData(positiveBase, badPositive.data(),
                                   badPositive.size(), &reason),
                "positive baseVertex overflow rejected");

    std::vector<uint32_t> validNegative(cube.indexCount);
    for (uint32_t i = 0; i < cube.indexCount; ++i)
        validNegative[i] = (i % 7) + 1; // baseVertex=-1 -> vertices 0..6
    MeshDesc12 negativeBase = cube;
    negativeBase.submesh.baseVertex = -1;
    test.Expect(ValidateIndexData(negativeBase, validNegative.data(),
                                  validNegative.size(), &reason),
                "negative baseVertex accounted for");

    std::vector<uint32_t> badNegative = validNegative;
    badNegative[0] = 0; // 0 - 1 == -1
    test.Expect(!ValidateIndexData(negativeBase, badNegative.data(),
                                   badNegative.size(), &reason),
                "negative baseVertex underflow rejected");

    MeshDesc12 badRange = cube;
    badRange.submesh.firstIndex = badRange.indexCount;
    badRange.submesh.indexCount = 1;
    test.Expect(!ValidateIndexData(badRange, validBase.data(), validBase.size(),
                                   &reason),
                "submesh range beyond index data rejected");
}

void TestResolution(TestState& test)
{
    GeometryCatalog12 catalog;
    const renderprep12::OrderedFrame12 frame =
        PrepareScene(catalog, 64, renderprep12::Mode::Serial, nullptr);
    test.Expect(frame.frame.packets.size() == 64,
                "64 geometry packets prepared");

    ResolvedFrame12 resolved;
    const char* reason = nullptr;
    const bool ok = ResolveGeometryFrame(frame, 42, catalog, resolved, &reason);
    test.Expect(ok, "geometry frame resolves");
    if (!ok)
        return;
    test.Expect(resolved.draws.size() == 64,
                "resolved frame has one draw per packet");
    for (uint32_t i = 0; i < resolved.draws.size(); ++i)
    {
        const int32_t expectedMesh = catalog.IndexOf(
            frame.frame.packets[i].object.visual);
        test.Expect(expectedMesh >= 0 &&
                        resolved.draws[i].meshIndex == static_cast<uint32_t>(expectedMesh),
                    "mesh handle resolves deterministically");
        test.Expect(resolved.draws[i].objectConstantIndex == i,
                    "object constant index matches packet index");
        test.Expect(resolved.draws[i].indexCount != 0,
                    "resolved index range non-empty");
    }

    ResolvedFrame12 again;
    test.Expect(ResolveGeometryFrame(frame, 42, catalog, again, &reason),
                "second resolution succeeds");
    test.Expect(Equivalent(resolved, again),
                "resolution deterministic");
}

void TestNegativeResolution(TestState& test)
{
    GeometryCatalog12 catalog;
    const renderprep12::OrderedFrame12 frame =
        PrepareScene(catalog, 8, renderprep12::Mode::Serial, nullptr);
    ResolvedFrame12 resolved;
    const char* reason = nullptr;
    test.Expect(ResolveGeometryFrame(frame, 42, catalog, resolved, &reason),
                "baseline geometry frame resolves");
    const ResolvedFrame12 baseline = resolved;

    const auto expectFailure = [&](const renderprep12::OrderedFrame12& bad,
                                   const char* name)
    {
        ResolvedFrame12 before = resolved;
        test.Expect(!ResolveGeometryFrame(bad, 42, catalog, resolved, &reason),
                    name);
        test.Expect(Equivalent(resolved, before),
                    "failed resolution preserves published output");
    };

    {
        renderprep12::OrderedFrame12 bad = frame;
        bad.frame.packets[0].object.visual.generation += 1;
        expectFailure(bad, "stale geometry generation rejected before GPU");
    }
    {
        renderprep12::OrderedFrame12 bad = frame;
        bad.frame.packets[0].object.visual.id = 99;
        expectFailure(bad, "unknown geometry id rejected before GPU");
    }
    {
        renderprep12::OrderedFrame12 bad = frame;
        bad.frame.packets[0].object.indexCount += 1;
        expectFailure(bad, "index range mismatch rejected");
    }
    {
        renderprep12::OrderedFrame12 bad = frame;
        bad.frame.packets[0].object.firstIndex = 1000;
        expectFailure(bad, "out-of-range index rejected");
    }
    {
        renderprep12::OrderedFrame12 bad = frame;
        bad.frame.packets[0].object.world.m[0][0] =
            std::numeric_limits<float>::quiet_NaN();
        expectFailure(bad, "invalid geometry world rejected");
    }
    {
        renderprep12::OrderedFrame12 bad = frame;
        bad.frame.packets.resize(257, frame.frame.packets[0]);
        bad.categories[0] = {0, 257};
        expectFailure(bad, "object constant capacity overflow rejected");
    }
    {
        renderprep12::OrderedFrame12 bad = frame;
        bad.categories[0] = {0, 0};
        expectFailure(bad, "invalid category range rejected");
    }
    test.Expect(Equivalent(resolved, baseline),
                "all rejected resolutions left baseline intact");
}

void TestSerialParallel(TestState& test)
{
    GeometryCatalog12 catalog;
    const renderprep12::OrderedFrame12 serial =
        PrepareScene(catalog, 64, renderprep12::Mode::Serial, nullptr);

    gr::jobs::JobSystem jobs;
    gr::jobs::JobConfig config;
    config.backgroundWorkers = 2;
    config.automaticWorkers = false;
    test.Expect(jobs.Initialize(config), "geometry jobs init");
    const renderprep12::OrderedFrame12 parallel =
        PrepareScene(catalog, 64, renderprep12::Mode::Parallel, &jobs);
    test.Expect(renderprep12::Equivalent(serial, parallel),
                "serial/parallel ordered geometry frames equivalent");

    ResolvedFrame12 a, b;
    const char* reason = nullptr;
    test.Expect(ResolveGeometryFrame(serial, 42, catalog, a, &reason),
                "serial geometry resolves");
    test.Expect(ResolveGeometryFrame(parallel, 42, catalog, b, &reason),
                "parallel geometry resolves");
    test.Expect(Equivalent(a, b),
                "serial/parallel resolved geometry equivalent");
    jobs.Shutdown();
}
} // namespace

bool RunGeometryTests()
{
    TestState test;
    try
    {
        TestCatalog(test);
        TestIndexValidation(test);
        TestResolution(test);
        TestNegativeResolution(test);
        TestSerialParallel(test);
    }
    catch (const std::exception& error)
    {
        std::printf("[geometry] FAIL: exception: %s\n", error.what());
        return false;
    }
    catch (...)
    {
        std::printf("[geometry] FAIL: unknown exception\n");
        return false;
    }
    std::printf("[geometry] %s checks=%u failures=%u\n",
                test.failures == 0 ? "PASS" : "FAIL",
                test.checks, test.failures);
    return test.failures == 0;
}
} // namespace smokegeometry12
