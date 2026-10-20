#include "dx12objectconstants.h"
#include "dx12renderprep.h"
#include "dx12camera.h"
#include "dx12transform_tests.h"
#include "gr/jobs/JobSystem.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace smoketransform12
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
            std::printf("[transform] FAIL: %s\n", name);
        }
    }
};

bool Near(float a, float b, float epsilon = 1.0e-5f)
{
    return std::fabs(a - b) <= epsilon;
}

bool NearVector(DirectX::FXMVECTOR a, DirectX::FXMVECTOR b,
                float epsilon = 1.0e-5f)
{
    DirectX::XMFLOAT4 av, bv;
    DirectX::XMStoreFloat4(&av, a);
    DirectX::XMStoreFloat4(&bv, b);
    return Near(av.x, bv.x, epsilon) && Near(av.y, bv.y, epsilon) &&
           Near(av.z, bv.z, epsilon) && Near(av.w, bv.w, epsilon);
}

DirectX::XMFLOAT4X4 MakeWorld(float sx, float sy, float sz,
                             float rx, float ry, float rz,
                             float tx, float ty, float tz)
{
    const DirectX::XMMATRIX world =
        DirectX::XMMatrixScaling(sx, sy, sz) *
        DirectX::XMMatrixRotationRollPitchYaw(rx, ry, rz) *
        DirectX::XMMatrixTranslation(tx, ty, tz);
    DirectX::XMFLOAT4X4 result;
    DirectX::XMStoreFloat4x4(&result, world);
    return result;
}

void TestBasicTransforms(TestState& test)
{
    const DirectX::XMVECTOR point = DirectX::XMVectorSet(1.0f, 2.0f, 3.0f, 1.0f);

    {
        const DirectX::XMFLOAT4X4 world = MakeWorld(1, 1, 1, 0, 0, 0, 0, 0, 0);
        test.Expect(NearVector(
                        DirectX::XMVector4Transform(
                            point, DirectX::XMLoadFloat4x4(&world)),
                        point),
                    "identity transform");
    }
    {
        const DirectX::XMFLOAT4X4 world = MakeWorld(1, 1, 1, 0, 0, 0, 1, 2, 3);
        const DirectX::XMVECTOR actual = DirectX::XMVector4Transform(
            DirectX::XMVectorSet(0, 0, 0, 1), DirectX::XMLoadFloat4x4(&world));
        test.Expect(NearVector(actual, DirectX::XMVectorSet(1, 2, 3, 1)),
                    "translation row-vector convention");
    }
    {
        const DirectX::XMFLOAT4X4 world = MakeWorld(1, 1, 1, 0, DirectX::XM_PIDIV2, 0, 0, 0, 0);
        const DirectX::XMVECTOR actual = DirectX::XMVector4Transform(
            DirectX::XMVectorSet(1, 0, 0, 1), DirectX::XMLoadFloat4x4(&world));
        test.Expect(NearVector(actual, DirectX::XMVectorSet(0, 0, -1, 1)),
                    "rotation Y 90 degrees");
    }
    {
        const DirectX::XMFLOAT4X4 world = MakeWorld(2, 3, 4, 0, 0, 0, 0, 0, 0);
        const DirectX::XMVECTOR actual = DirectX::XMVector4Transform(
            point, DirectX::XMLoadFloat4x4(&world));
        test.Expect(NearVector(actual, DirectX::XMVectorSet(2, 6, 12, 1)),
                    "scale transform");
    }
    {
        const DirectX::XMFLOAT4X4 world = MakeWorld(-2, 1, 0.5f, 0, 0, 0, 0, 0, 0);
        test.Expect(dx12object12::IsValidTransformWorld(world),
                    "negative and fractional scale is valid affine");
    }
}

void TestMatrixConvention(TestState& test)
{
    const DirectX::XMFLOAT4X4 world =
        MakeWorld(1.25f, 0.75f, 2.0f, 0.2f, 0.6f, -0.3f, -1.0f, 2.0f, 3.0f);

    SmokeCamera12 camera;
    camera.SetAspect(16.0f / 9.0f);
    const DirectX::XMFLOAT4X4 viewProjection = camera.CaptureViewProjection();

    DirectX::XMFLOAT4X4 storedWorld;
    DirectX::XMFLOAT4X4 storedViewProjection;
    DirectX::XMStoreFloat4x4(&storedWorld,
                             DirectX::XMMatrixTranspose(
                                 DirectX::XMLoadFloat4x4(&world)));
    DirectX::XMStoreFloat4x4(&storedViewProjection,
                             DirectX::XMMatrixTranspose(
                                 DirectX::XMLoadFloat4x4(&viewProjection)));

    // HLSL default column-major load of the stored transpose restores the
    // logical row-vector matrix.
    const DirectX::XMMATRIX loadedWorld =
        DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&storedWorld));
    const DirectX::XMMATRIX loadedViewProjection =
        DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&storedViewProjection));

    const DirectX::XMVECTOR point = DirectX::XMVectorSet(0.3f, -0.2f, 0.7f, 1.0f);
    const DirectX::XMVECTOR expected = DirectX::XMVector4Transform(
        DirectX::XMVector4Transform(
            point, DirectX::XMLoadFloat4x4(&world)),
        DirectX::XMLoadFloat4x4(&viewProjection));
    const DirectX::XMVECTOR actual = DirectX::XMVector4Transform(
        DirectX::XMVector4Transform(point, loadedWorld), loadedViewProjection);

    test.Expect(NearVector(expected, actual),
                "transposed CB load preserves World then ViewProjection order");
    test.Expect(memcmp(&storedWorld, &world, sizeof(world)) != 0,
                "stored World really is transposed");
}

void TestTransformedBounds(TestState& test)
{
    const DirectX::XMFLOAT4X4 world =
        MakeWorld(2.0f, 3.0f, 4.0f, 0.1f, 0.4f, -0.2f, 5.0f, 6.0f, 7.0f);

    renderprep12::Object12 object{};
    object.id = 1;
    object.firstVertex = 0;
    object.vertexCount = 36;
    object.minimum = {-1.0f, -1.0f, -1.0f};
    object.maximum = {1.0f, 1.0f, 1.0f};
    object.sphere.center = {0.0f, 0.0f, 0.0f};
    object.sphere.radius = std::sqrt(3.0f);
    object.world = world;

    SmokeCamera12 camera;
    camera.SetAspect(16.0f / 9.0f);
    const renderprep12::RenderSceneSnapshot12 snapshot(
        camera.CaptureViewProjection(), std::span<const renderprep12::Object12>(
                                            &object, 1), 1);
    renderprep12::Timings12 timings;
    const renderprep12::PreparedFrame12 frame = renderprep12::Prepare(
        snapshot, 1, renderprep12::Mode::Serial, nullptr, timings);

    test.Expect(frame.packets.size() == 1, "bounds frame has one packet");
    if (frame.packets.size() != 1)
        return;

    const renderprep12::DrawPacket12& packet = frame.packets[0];
    double expectedMin[3] = {0, 0, 0};
    double expectedMax[3] = {0, 0, 0};
    for (int corner = 0; corner < 8; ++corner)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            double value = world.m[3][axis];
            for (int column = 0; column < 3; ++column)
            {
                const double local =
                    (corner & (1 << column)) ? 1.0 : -1.0;
                value += local * world.m[column][axis];
            }
            if (corner == 0)
            {
                expectedMin[axis] = value;
                expectedMax[axis] = value;
            }
            else
            {
                expectedMin[axis] = (std::min)(expectedMin[axis], value);
                expectedMax[axis] = (std::max)(expectedMax[axis], value);
            }
        }
    }
    const float actualMin[3] = {packet.worldMinimum.x, packet.worldMinimum.y,
                                packet.worldMinimum.z};
    const float actualMax[3] = {packet.worldMaximum.x, packet.worldMaximum.y,
                                packet.worldMaximum.z};
    for (int axis = 0; axis < 3; ++axis)
    {
        test.Expect(std::fabs(actualMin[axis] - expectedMin[axis]) < 1.0e-4,
                    "world AABB minimum includes transform");
        test.Expect(std::fabs(actualMax[axis] - expectedMax[axis]) < 1.0e-4,
                    "world AABB maximum includes transform");
    }
}

void TestInvalidTransforms(TestState& test)
{
    DirectX::XMFLOAT4X4 identity;
    DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
    test.Expect(dx12object12::IsValidTransformWorld(identity),
                "identity is valid transform");

    DirectX::XMFLOAT4X4 world = identity;
    world.m[0][0] = std::numeric_limits<float>::quiet_NaN();
    test.Expect(!dx12object12::IsValidTransformWorld(world),
                "NaN transform rejected");

    world = identity;
    world.m[3][0] = std::numeric_limits<float>::infinity();
    test.Expect(!dx12object12::IsValidTransformWorld(world),
                "Inf transform rejected");

    world = identity;
    world.m[0][3] = 0.1f;
    test.Expect(!dx12object12::IsValidTransformWorld(world),
                "non-affine last column rejected");

    world = identity;
    world.m[3][3] = 0.9f;
    test.Expect(!dx12object12::IsValidTransformWorld(world),
                "non-affine homogeneous row rejected");

    world = identity;
    world.m[0][0] = 0.0f;
    test.Expect(!dx12object12::IsValidTransformWorld(world),
                "singular transform rejected");

    renderprep12::Object12 object{};
    object.id = 1;
    object.firstVertex = 0;
    object.vertexCount = 36;
    object.minimum = {-1, -1, -1};
    object.maximum = {1, 1, 1};
    object.sphere.radius = std::sqrt(3.0f);
    object.world = identity;
    object.world.m[0][3] = 0.1f;
    SmokeCamera12 camera;
    camera.SetAspect(16.0f / 9.0f);
    const renderprep12::RenderSceneSnapshot12 snapshot(
        camera.CaptureViewProjection(), std::span<const renderprep12::Object12>(
                                            &object, 1), 1);
    renderprep12::Timings12 timings;
    bool threw = false;
    try
    {
        renderprep12::Prepare(snapshot, 1, renderprep12::Mode::Serial,
                              nullptr, timings);
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    test.Expect(threw, "Prepare rejects non-affine transform before GPU");
}

void TestConstantLayout(TestState& test)
{
    using namespace dx12object12;
    test.Expect(sizeof(ObjectConstants12) <= kObjectConstantsSize,
                "object constants fit CBV slice");
    test.Expect(ObjectConstantsOffset(0) == kObjectConstantsOffset,
                "first object constant offset");
    for (uint32_t index = 0; index < kMaxObjects; ++index)
    {
        const uint64_t offset = ObjectConstantsOffset(index);
        test.Expect(offset % kConstantBufferAlignment == 0,
                    "object constant offset is 256-byte aligned");
        test.Expect(offset + kObjectConstantsSize <= kUploadBytes,
                    "object constant slice within upload buffer");
    }
    test.Expect(ObjectConstantsOffset(kMaxObjects - 1) + kObjectConstantsSize ==
                    kUploadBytes,
                "last object constant reaches upload end");
    test.Expect(!IsValidObjectIndex(kMaxObjects),
                "capacity rejects object index == kMaxObjects");
}

void TestSerialParallelEquivalence(TestState& test)
{
    std::array<renderprep12::Object12, 2> objects{};
    for (uint32_t i = 0; i < objects.size(); ++i)
    {
        auto& object = objects[i];
        object.id = i + 1;
        object.firstVertex = 0;
        object.vertexCount = 36;
        object.minimum = {-1, -1, -1};
        object.maximum = {1, 1, 1};
        object.sphere.center = {0, 0, 0};
        object.sphere.radius = std::sqrt(3.0f);
        object.visual = renderprep12::SyntheticResources12[0];
        object.material = renderprep12::SyntheticResources12[0];
        object.pso = renderprep12::SyntheticResources12[0];
        object.world = i == 0
                           ? MakeWorld(1, 1, 1, 0, 0, 0, -0.4f, 0, 0)
                           : MakeWorld(1.25f, 1.25f, 1.25f, 0, DirectX::XMConvertToRadians(30.0f), 0,
                                       0.4f, 0, 2.0f);
    }

    SmokeCamera12 camera;
    camera.SetAspect(16.0f / 9.0f);
    const renderprep12::RenderSceneSnapshot12 snapshot(
        camera.CaptureViewProjection(), objects, 1);
    renderprep12::Timings12 serialTiming;
    const renderprep12::PreparedFrame12 serial = renderprep12::Prepare(
        snapshot, 1, renderprep12::Mode::Serial, nullptr, serialTiming);
    test.Expect(serial.packets.size() == 2, "serial transform frame has two packets");

    gr::jobs::JobSystem jobs;
    gr::jobs::JobConfig config;
    config.backgroundWorkers = 2;
    config.automaticWorkers = false;
    test.Expect(jobs.Initialize(config), "parallel transform test jobs init");
    renderprep12::Timings12 parallelTiming;
    const renderprep12::PreparedFrame12 parallel = renderprep12::Prepare(
        snapshot, 1, renderprep12::Mode::Parallel, &jobs, parallelTiming);
    test.Expect(renderprep12::Equivalent(serial, parallel),
                "serial and parallel transform frames equivalent");
    jobs.Shutdown();
}
} // namespace

bool RunTransformTests()
{
    TestState test;
    try
    {
        TestBasicTransforms(test);
        TestMatrixConvention(test);
        TestTransformedBounds(test);
        TestInvalidTransforms(test);
        TestConstantLayout(test);
        TestSerialParallelEquivalence(test);
    }
    catch (const std::exception& error)
    {
        std::printf("[transform] FAIL: exception: %s\n", error.what());
        return false;
    }
    catch (...)
    {
        std::printf("[transform] FAIL: unknown exception\n");
        return false;
    }
    std::printf("[transform] %s checks=%u failures=%u\n",
                test.failures == 0 ? "PASS" : "FAIL", test.checks,
                test.failures);
    return test.failures == 0;
}
} // namespace smoketransform12
