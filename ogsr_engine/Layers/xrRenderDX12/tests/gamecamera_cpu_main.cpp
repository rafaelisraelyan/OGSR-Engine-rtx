// F4.0-B2.1/B2.2: isolated CPU-only game camera snapshot tests.
//
// The test executable has no Engine, DX11/R4, D3D12 device, VFS or game
// archive dependency. DirectXMath is used only as a CPU math library.

#include "dx12gamecamera.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(GAMECAMERA_STL_HARDENING_TEST)
static_assert(_MSVC_STL_HARDENING == 1,
              "STL hardening test configuration must enable MSVC STL hardening");
#endif

#ifdef GAMECAMERA_OOM_TEST
namespace
{
std::atomic<bool> g_fail_next_allocation{false};
std::atomic<std::size_t> g_allocation_count{0};
std::atomic<std::size_t> g_allocation_bytes{0};

void* AllocateOrThrow(std::size_t size)
{
    if (g_fail_next_allocation.exchange(false, std::memory_order_acq_rel))
        throw std::bad_alloc();
    if (void* p = std::malloc(size == 0 ? 1 : size))
    {
        g_allocation_count.fetch_add(1, std::memory_order_relaxed);
        g_allocation_bytes.fetch_add(size, std::memory_order_relaxed);
        return p;
    }
    throw std::bad_alloc();
}
} // namespace

void* operator new(std::size_t size) { return AllocateOrThrow(size); }
void* operator new[](std::size_t size) { return AllocateOrThrow(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

namespace
{
using DirectX::XMFLOAT3;
using DirectX::XMFLOAT4;
using DirectX::XMFLOAT4X4;

struct TestContext
{
    int passed = 0;
    int failed = 0;

    void Check(bool condition, const char* name, const std::string& detail = {})
    {
        if (condition)
        {
            ++passed;
            std::printf("[PASS] %s\n", name);
        }
        else
        {
            ++failed;
            std::printf("[FAIL] %s%s%s\n", name,
                        detail.empty() ? "" : " : ", detail.c_str());
        }
    }
};

bool Near(double a, double b, double tolerance = 1e-5)
{
    return std::isfinite(a) && std::isfinite(b) &&
           std::fabs(a - b) <= tolerance;
}

bool NearRelative(double a, double b, double tolerance = 1e-5)
{
    if (!std::isfinite(a) || !std::isfinite(b))
        return false;
    const double scale = std::max({1.0, std::fabs(a), std::fabs(b)});
    return std::fabs(a - b) <= tolerance * scale;
}

bool Near(const DirectX::XMFLOAT4& a, const DirectX::XMFLOAT4& b,
          double tolerance = 1e-5)
{
    return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) &&
           Near(a.z, b.z, tolerance) && Near(a.w, b.w, tolerance);
}

XMFLOAT4X4 Multiply(const XMFLOAT4X4& a, const XMFLOAT4X4& b)
{
    XMFLOAT4X4 out{};
    DirectX::XMStoreFloat4x4(
        &out, DirectX::XMMatrixMultiply(DirectX::XMLoadFloat4x4(&a),
                                        DirectX::XMLoadFloat4x4(&b)));
    return out;
}

XMFLOAT4X4 Transpose(const XMFLOAT4X4& m)
{
    XMFLOAT4X4 out{};
    DirectX::XMStoreFloat4x4(
        &out, DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&m)));
    return out;
}

XMFLOAT4 Clip(const XMFLOAT4X4& vp, const XMFLOAT3& p)
{
    // Independent direct row-vector arithmetic; not a tested helper.
    const double x = static_cast<double>(p.x);
    const double y = static_cast<double>(p.y);
    const double z = static_cast<double>(p.z);
    XMFLOAT4 out{};
    out.x = static_cast<float>(
        x * vp.m[0][0] + y * vp.m[1][0] + z * vp.m[2][0] + vp.m[3][0]);
    out.y = static_cast<float>(
        x * vp.m[0][1] + y * vp.m[1][1] + z * vp.m[2][1] + vp.m[3][1]);
    out.z = static_cast<float>(
        x * vp.m[0][2] + y * vp.m[1][2] + z * vp.m[2][2] + vp.m[3][2]);
    out.w = static_cast<float>(
        x * vp.m[0][3] + y * vp.m[1][3] + z * vp.m[2][3] + vp.m[3][3]);
    return out;
}

XMFLOAT4 Ndc(const XMFLOAT4& clip)
{
    return {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w, clip.w};
}

XMFLOAT4X4 MakeView(DirectX::XMVECTOR eye, DirectX::XMVECTOR target,
                   DirectX::XMVECTOR up)
{
    XMFLOAT4X4 out{};
    DirectX::XMStoreFloat4x4(
        &out, DirectX::XMMatrixLookAtLH(eye, target, up));
    return out;
}

XMFLOAT4X4 MakeProjection(float fovYRadians, float aspect, float nearZ,
                          float farZ)
{
    XMFLOAT4X4 out{};
    DirectX::XMStoreFloat4x4(
        &out, DirectX::XMMatrixPerspectiveFovLH(fovYRadians, aspect, nearZ, farZ));
    return out;
}

XMFLOAT4X4 MakeRhProjection(float fovYRadians, float aspect, float nearZ,
                            float farZ)
{
    XMFLOAT4X4 out{};
    DirectX::XMStoreFloat4x4(
        &out, DirectX::XMMatrixPerspectiveFovRH(fovYRadians, aspect, nearZ, farZ));
    return out;
}

gamecamera12::CameraCaptureInput12 MakeInput()
{
    const float aspect = 16.0f / 9.0f;
    const auto eye = DirectX::XMVectorSet(3.0f, 4.0f, -5.0f, 1.0f);
    const auto forward = DirectX::XMVectorSet(2.0f / 3.0f, -1.0f / 3.0f,
                                              2.0f / 3.0f, 0.0f);
    const auto target = DirectX::XMVectorAdd(eye, forward);
    const auto up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);

    gamecamera12::CameraCaptureInput12 input;
    input.view = MakeView(eye, target, up);
    input.projection = MakeProjection(DirectX::XMConvertToRadians(60.0f),
                                     aspect, 0.1f, 100.0f);
    input.fullTransform = Multiply(input.view, input.projection);
    input.hasFullTransform = true;
    input.position = {3.0f, 4.0f, -5.0f};
    input.sourceGameFrame = 17;
    input.captureSequence = 1;
    input.levelSession = 11;
    input.captureEpoch = 1;
    input.viewportWidth = 1920;
    input.viewportHeight = 1080;
    return input;
}

std::vector<renderprep12::Object12> MakeSceneObjects(std::size_t count)
{
    std::vector<renderprep12::Object12> objects(count);
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
        objects[i].id = i + 1;
        objects[i].world.m[0][0] = 1.0f;
        objects[i].world.m[1][1] = 1.0f;
        objects[i].world.m[2][2] = 1.0f;
        objects[i].world.m[3][3] = 1.0f;
    }
    return objects;
}

void TestGoldenOracle(TestContext& ctx)
{
    auto input = MakeInput();
    const auto result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.error == gamecamera12::CameraError12::None &&
              result.camera.has_value(), "golden input validates");
    if (!result.camera)
        return;

    const XMFLOAT3 point{8.649915823f, 2.942809042f, -2.178511302f};
    const XMFLOAT4 clip = Clip(result.camera->ViewProjection(), point);
    const XMFLOAT4 ndc = Ndc(clip);
    ctx.Check(Near(clip, {1.948557159f, 1.732050808f, 5.905905906f, 6.0f}),
              "golden clip matches independent arithmetic",
              "clip=" + std::to_string(clip.x) + "," +
                  std::to_string(clip.y) + "," + std::to_string(clip.z) +
                  "," + std::to_string(clip.w));
    ctx.Check(Near(ndc, {0.324759526f, 0.288675135f, 0.984317651f, 6.0f}),
              "golden NDC matches independent arithmetic");

    const XMFLOAT4X4 reversed = Multiply(input.projection, input.view);
    const XMFLOAT4 wrong = Clip(reversed, point);
    ctx.Check(!Near(wrong, clip),
              "Projection*View order is rejected by golden oracle");

    const XMFLOAT4X4 doubled = Transpose(result.camera->ViewProjection());
    const XMFLOAT4 doubledClip = Clip(doubled, point);
    ctx.Check(!Near(doubledClip, clip),
              "double transpose is rejected by golden oracle");

    XMFLOAT4X4 translated = input.view;
    translated.m[3][0] += 1.0f;
    const XMFLOAT4 translatedClip = Clip(
        Multiply(translated, input.projection), point);
    ctx.Check(!Near(translatedClip, clip),
              "wrong translation placement is rejected by golden oracle");

    const DirectX::XMFLOAT4X4 rh = MakeRhProjection(
        DirectX::XMConvertToRadians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    auto rhInput = input;
    rhInput.projection = rh;
    rhInput.hasFullTransform = false;
    const auto rhResult = gamecamera12::ValidateCamera12(rhInput);
    ctx.Check(rhResult.error != gamecamera12::CameraError12::None,
              "right-handed projection is rejected");

    const DirectX::XMFLOAT4X4 ortho = DirectX::XMFLOAT4X4{};
    auto orthoInput = input;
    orthoInput.projection = ortho;
    orthoInput.hasFullTransform = false;
    const auto orthoResult = gamecamera12::ValidateCamera12(orthoInput);
    ctx.Check(orthoResult.error == gamecamera12::CameraError12::UnsupportedProjection,
              "unsupported projection is diagnosed");
}

void TestProjectionMetadata(TestContext& ctx)
{
    auto input = MakeInput();
    auto result = gamecamera12::ValidateCamera12(input);
    if (!result.camera)
    {
        ctx.Check(false, "projection metadata base validation");
        return;
    }
    const auto& metadata = result.camera->ProjectionMetadata();
    ctx.Check(Near(metadata.effectiveInverseAspect, 9.0 / 16.0) &&
              Near(metadata.fovYRadians, DirectX::XMConvertToRadians(60.0f)) &&
              Near(metadata.nearZ, 0.1) && NearRelative(metadata.farZ, 100.0, 1e-4) &&
              metadata.nearStatus == gamecamera12::ProjectionStatus12::Known &&
              metadata.farStatus == gamecamera12::ProjectionStatus12::Known,
              "projection metadata fov/aspect/near/far");

    input.projection = MakeProjection(DirectX::XMConvertToRadians(60.0f),
                                      16.0f / 9.0f * 1.2f, 0.2f, 250.0f);
    input.hasFullTransform = false;
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.camera.has_value() &&
              Near(result.camera->ProjectionMetadata().effectiveInverseAspect,
                   1.0 / (16.0 / 9.0 * 1.2)) &&
              Near(result.camera->ProjectionMetadata().nearZ, 0.2) &&
              NearRelative(result.camera->ProjectionMetadata().farZ, 250.0, 1e-4),
              "projection metadata supports aspect multiplier and interpolation");

    input.projection = MakeProjection(DirectX::XMConvertToRadians(60.0f),
                                      21.0f / 9.0f, 0.05f, 500.0f);
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.camera.has_value() &&
              Near(result.camera->ProjectionMetadata().effectiveInverseAspect,
                   9.0 / 21.0) &&
              Near(result.camera->ProjectionMetadata().nearZ, 0.05) &&
              NearRelative(result.camera->ProjectionMetadata().farZ, 500.0, 1e-4),
              "projection metadata 21:9");

    input = MakeInput();
    input.sourceMetadata.hasFovY = true;
    input.sourceMetadata.fovYRadians = 0.5;
    input.sourceMetadata.hasAspectScale = true;
    input.sourceMetadata.aspectScale = 0.25;
    input.sourceMetadata.hasNearZ = true;
    input.sourceMetadata.nearZ = 5.0;
    input.sourceMetadata.hasFarZ = true;
    input.sourceMetadata.farZ = 10.0;
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.camera.has_value() &&
              Near(result.camera->ProjectionMetadata().fovYRadians,
                   DirectX::XMConvertToRadians(60.0f)) &&
              Near(result.camera->ProjectionMetadata().nearZ, 0.1) &&
              NearRelative(result.camera->ProjectionMetadata().farZ, 100.0),
              "projection remains authoritative over advisory source metadata");

    input.projection = MakeProjection(DirectX::XMConvertToRadians(60.0f),
                                      16.0f / 9.0f, 0.1f, 100.0f);
    input.hasFullTransform = false;
    input.projection.m[2][2] = 1.0f;
    input.projection.m[3][2] = -0.1f;
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.camera.has_value() &&
              result.camera->ProjectionMetadata().farStatus ==
                  gamecamera12::ProjectionStatus12::Unavailable &&
              Near(result.camera->ProjectionMetadata().nearZ, 0.1),
              "P33 near one leaves far unavailable without invention");

    input.projection.m[2][2] = std::nextafter(1.0f, 0.0f);
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.error == gamecamera12::CameraError12::UnsupportedProjection,
              "P33 below one is rejected");

    input.projection.m[2][2] = std::nextafter(1.0f, 2.0f);
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.camera.has_value() &&
              result.camera->ProjectionMetadata().farStatus ==
                  gamecamera12::ProjectionStatus12::Unavailable,
              "P33 just above one leaves far unavailable");

    input.projection.m[2][2] = 1.01f;
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.camera.has_value() &&
              result.camera->ProjectionMetadata().farStatus ==
                  gamecamera12::ProjectionStatus12::Known &&
              Near(result.camera->ProjectionMetadata().farZ, 10.0, 1e-3),
              "P33 above conditioning threshold restores finite far");

    input.projection.m[3][2] = 0.1f;
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.error == gamecamera12::CameraError12::UnsupportedProjection,
              "positive P43 is rejected");

    input = MakeInput();
    input.projection.m[2][2] = std::numeric_limits<float>::quiet_NaN();
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.error == gamecamera12::CameraError12::InvalidProjection,
              "NaN projection rejected");

    input = MakeInput();
    input.projection.m[3][2] = std::numeric_limits<float>::infinity();
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.error == gamecamera12::CameraError12::InvalidProjection,
              "infinite projection rejected");

    input.projection = DirectX::XMFLOAT4X4{};
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.error == gamecamera12::CameraError12::UnsupportedProjection,
              "identity projection unsupported");
}

void TestMatrixFormsAndDepth(TestContext& ctx)
{
    auto input = MakeInput();
    input.hasFullTransform = false;

    DirectX::XMStoreFloat4x4(&input.view, DirectX::XMMatrixIdentity());
    input.position = {0.0f, 0.0f, 0.0f};
    auto result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.camera.has_value(), "identity view validates");

    DirectX::XMStoreFloat4x4(&input.view,
                             DirectX::XMMatrixTranslation(-1.0f, -2.0f, -3.0f));
    input.position = {1.0f, 2.0f, 3.0f};
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.camera.has_value(), "translation view validates");

    DirectX::XMStoreFloat4x4(&input.view,
                             DirectX::XMMatrixRotationY(0.5f));
    input.position = {0.0f, 0.0f, 0.0f};
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.camera.has_value(), "rotation view validates");

    input.view.m[0][0] += 1.0e-6f;
    input.view.m[1][1] -= 1.0e-6f;
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.camera.has_value(),
              "rotation rounding within orthogonality tolerance validates");

    input.hasFullTransform = false;
    DirectX::XMStoreFloat4x4(&input.view, DirectX::XMMatrixIdentity());
    input.position = {0.0f, 0.0f, 0.0f};
    input.projection = MakeProjection(DirectX::XMConvertToRadians(60.0f),
                                      16.0f / 9.0f, 0.1f, 100.0f);
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.camera.has_value(), "depth fixture validates");
    if (result.camera)
    {
        const auto& vp = result.camera->ViewProjection();
        ctx.Check(Near(Ndc(Clip(vp, {0.0f, 0.0f, 0.1f})).z, 0.0, 2e-5),
                  "near maps to depth zero");
        ctx.Check(Near(Ndc(Clip(vp, {0.0f, 0.0f, 100.0f})).z, 1.0, 2e-5),
                  "far maps to depth one");
        const XMFLOAT4 behind = Clip(vp, {0.0f, 0.0f, -1.0f});
        ctx.Check(Near(behind.w, -1.0, 2e-5),
                  "behind-camera clip w is finite and negative");
        const XMFLOAT4 atCamera = Clip(vp, {0.0f, 0.0f, 0.0f});
        ctx.Check(Near(atCamera.w, 0.0, 2e-5),
                  "at-camera clip w is zero");
    }

    input.projection.m[2][2] = -input.projection.m[2][2];
    input.projection.m[3][2] = -input.projection.m[3][2];
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.error == gamecamera12::CameraError12::UnsupportedProjection,
              "reversed depth projection is rejected");
}

void TestCameraPosition(TestContext& ctx)
{
    auto input = MakeInput();
    auto result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.camera.has_value(), "golden position matches view");

    input.position.x += 1.0f;
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.error == gamecamera12::CameraError12::InvalidPosition,
              "one-meter inconsistent position rejected");

    input = MakeInput();
    input.position.x += 10000.0f;
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.error == gamecamera12::CameraError12::InvalidPosition,
              "10000-meter inconsistent position rejected");

    input = MakeInput();
    DirectX::XMStoreFloat4x4(&input.view,
                             DirectX::XMMatrixTranslation(-1000000.0f, -2000000.0f,
                                                          3000000.0f));
    input.position = {1000000.0f, 2000000.0f, -3000000.0f};
    input.hasFullTransform = false;
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.camera.has_value(),
              "large finite translated camera position validates");

    input.position.x += 1.0f;
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.error == gamecamera12::CameraError12::InvalidPosition,
              "one-meter mismatch rejected at large finite coordinates");

    // Jupiter-scale values and a rounding-sized perturbation.
    const DirectX::XMVECTOR eye =
        DirectX::XMVectorSet(-349.078796f, 6.664894f, 393.332458f, 1.0f);
    const DirectX::XMVECTOR target =
        DirectX::XMVectorSet(-348.078796f, 6.664894f, 393.332458f, 1.0f);
    const DirectX::XMVECTOR up =
        DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);
    input = MakeInput();
    input.view = MakeView(eye, target, up);
    input.position = {-349.078796f, 6.664894f, 393.332458f};
    input.hasFullTransform = false;
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.camera.has_value(),
              "Jupiter-scale camera position validates");

    input.position.x += 1.0e-5f;
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.camera.has_value(),
              "Jupiter-scale float rounding position validates");

    gamecamera12::CameraPublicationState12 state;
    (void)state.BeginLevelSession(60);
    input = MakeInput();
    input.levelSession = 60;
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 1;
    input.position.x += 1.0f;
    const auto captured = state.Capture(input);
    ctx.Check(captured.error == gamecamera12::CameraError12::InvalidPosition &&
                  !captured.camera.has_value() && !state.HasPending() &&
                  !state.HasPublished(),
              "invalid position is not published");
}

void TestViewAndValidation(TestContext& ctx)
{
    auto input = MakeInput();
    input.fullTransform.m[3][0] += 0.25f;
    auto result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.error == gamecamera12::CameraError12::InconsistentFullTransform,
              "inconsistent supplied FullTransform rejected");

    input = MakeInput();
    input.view.m[0][0] = std::numeric_limits<float>::quiet_NaN();
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.error == gamecamera12::CameraError12::InvalidView,
              "NaN view rejected");

    input = MakeInput();
    input.view.m[1][1] = 0.0f;
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.error == gamecamera12::CameraError12::InvalidView,
              "singular view rejected");

    input = MakeInput();
    input.view.m[0][3] = 1.0e-6f;
    ctx.Check(gamecamera12::ValidateCamera12(input).error ==
                  gamecamera12::CameraError12::InvalidView,
              "structural _14 small nonzero is rejected");
    input = MakeInput();
    input.view.m[1][3] = -1.0e-6f;
    ctx.Check(gamecamera12::ValidateCamera12(input).error ==
                  gamecamera12::CameraError12::InvalidView,
              "structural _24 small nonzero is rejected");
    input = MakeInput();
    input.view.m[2][3] = 1.0e-6f;
    ctx.Check(gamecamera12::ValidateCamera12(input).error ==
                  gamecamera12::CameraError12::InvalidView,
              "structural _34 small nonzero is rejected");
    input = MakeInput();
    input.view.m[3][3] = 0.999999f;
    ctx.Check(gamecamera12::ValidateCamera12(input).error ==
                  gamecamera12::CameraError12::InvalidView,
              "structural _44 not exactly one is rejected");

    input = MakeInput();
    input.view.m[0][0] = -input.view.m[0][0];
    result = gamecamera12::ValidateCamera12(input);
    ctx.Check(result.error == gamecamera12::CameraError12::InvalidView,
              "left/right basis reflection rejected");

    input = MakeInput();
    input.captureSequence = 0;
    ctx.Check(gamecamera12::ValidateCamera12(input).error ==
                  gamecamera12::CameraError12::InvalidCaptureSequence,
              "zero capture sequence rejected");
    input = MakeInput();
    input.levelSession = 0;
    ctx.Check(gamecamera12::ValidateCamera12(input).error ==
                  gamecamera12::CameraError12::InvalidLevelSession,
              "zero level session rejected");
    input = MakeInput();
    input.captureEpoch = 0;
    ctx.Check(gamecamera12::ValidateCamera12(input).error ==
                  gamecamera12::CameraError12::InvalidCaptureEpoch,
              "zero capture epoch rejected");
    input = MakeInput();
    input.viewportWidth = 0;
    ctx.Check(gamecamera12::ValidateCamera12(input).error ==
                  gamecamera12::CameraError12::InvalidViewport,
              "zero viewport rejected");
    input = MakeInput();
    input.position.x = std::numeric_limits<float>::infinity();
    ctx.Check(gamecamera12::ValidateCamera12(input).error ==
                  gamecamera12::CameraError12::InvalidPosition,
              "non-finite camera position rejected");
    input = MakeInput();
    input.sourceMetadata.hasFovY = true;
    input.sourceMetadata.fovYRadians = -1.0;
    ctx.Check(gamecamera12::ValidateCamera12(input).error ==
                  gamecamera12::CameraError12::InvalidCameraMetadata,
              "invalid source metadata rejected");
}

void TestSnapshotIndependence(TestContext& ctx)
{
    auto input = MakeInput();
    auto result = gamecamera12::ValidateCamera12(input);
    if (!result.camera)
    {
        ctx.Check(false, "snapshot independence baseline");
        return;
    }
    gamecamera12::GameCameraSnapshot12 snapshot = *result.camera;
    input.view.m[3][0] = 12345.0f;
    input.projection.m[0][0] = 0.0f;
    ctx.Check(Near(snapshot.View().m[3][0], MakeInput().view.m[3][0]) &&
              Near(snapshot.Projection().m[0][0],
                   MakeInput().projection.m[0][0]),
              "snapshot owns values independently of input mutation");
}

void TestPublicationState(TestContext& ctx)
{
    gamecamera12::CameraPublicationState12 state;
    ctx.Check(state.BeginLevelSession(11) == gamecamera12::CameraError12::None,
              "begin level session");
    ctx.Check(state.BeginLevelSession(12) ==
                  gamecamera12::CameraError12::SessionAlreadyActive,
              "second begin while live rejected");

    auto input = MakeInput();
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 1;
    auto captured = state.Capture(input);
    ctx.Check(captured.error == gamecamera12::CameraError12::None &&
              captured.camera.has_value(), "capture valid pending camera");
    if (!captured.camera)
        return;

    const auto tokenA = captured.camera->Token();
    ctx.Check(state.PublishPending(tokenA) ==
                  gamecamera12::CameraError12::None,
              "publish pending A by token");
    ctx.Check(state.Published().has_value() &&
              !state.RejectStaleWorkerResult(tokenA),
              "published A is current");
    ctx.Check(state.PublishPending(tokenA) ==
                  gamecamera12::CameraError12::NoPendingCamera,
              "re-publish without pending rejected");

    // A different token cannot publish the pending A camera.
    input = MakeInput();
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 2;
    captured = state.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "capture A for token mismatch");
        return;
    }
    const auto tokenA2 = captured.camera->Token();
    auto tokenB = tokenA2;
    tokenB.captureSequence += 1;
    ctx.Check(state.PublishPending(tokenB) ==
                  gamecamera12::CameraError12::CameraMismatch,
              "mismatched token cannot publish pending A");
    ctx.Check(state.Pending().has_value(),
              "mismatched token leaves pending untouched");

    // Capture A then B: token A cannot publish B.
    input = MakeInput();
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 3;
    captured = state.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "capture B for token mismatch");
        return;
    }
    ctx.Check(state.PublishPending(tokenA2) ==
                  gamecamera12::CameraError12::CameraMismatch,
              "old pending token cannot publish newer pending B");
    ctx.Check(state.PublishPending(captured.camera->Token()) ==
                  gamecamera12::CameraError12::None,
              "publish pending B by exact token");

    // Capture, reset, then old token must be rejected.
    input = MakeInput();
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 4;
    captured = state.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "capture before reset");
        return;
    }
    const auto resetToken = captured.camera->Token();
    ctx.Check(state.BeginReset() == gamecamera12::CameraError12::None,
              "reset invalidates pending/publication");
    ctx.Check(state.PublishPending(resetToken) ==
                  gamecamera12::CameraError12::StaleCaptureEpoch,
              "publish old token after reset rejects stale epoch");
    ctx.Check(state.RejectStaleWorkerResult(resetToken),
              "old worker result rejected after reset");
    ctx.Check(!state.HasPublished() && !state.HasPending(),
              "reset clears publication identity");

    // New capture after reset, then a level reload with an old published token.
    input = MakeInput();
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 5;
    captured = state.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "capture after reset");
        return;
    }
    ctx.Check(state.PublishPending(captured.camera->Token()) ==
                  gamecamera12::CameraError12::None,
              "publish after reset");
    const auto oldLevelToken = captured.camera->Token();

    ctx.Check(state.BeginTeardown() == gamecamera12::CameraError12::None,
              "begin teardown before level reload");
    ctx.Check(!state.Published().has_value() &&
              !state.HasPending(),
              "teardown clears old publication");
    ctx.Check(state.BeginLevelSession(12) ==
                  gamecamera12::CameraError12::Teardown,
              "new session before complete teardown rejected");
    ctx.Check(state.CompleteTeardown() == gamecamera12::CameraError12::None,
              "complete teardown returns to no-level");
    ctx.Check(state.CompleteTeardown() ==
                  gamecamera12::CameraError12::NoLiveSession,
              "repeated complete teardown rejected");
    ctx.Check(state.BeginLevelSession(12) == gamecamera12::CameraError12::None,
              "begin second level session after teardown");
    ctx.Check(state.PublishPending(oldLevelToken) ==
                  gamecamera12::CameraError12::StaleLevelSession,
              "old level token rejected after reload");

    input = MakeInput();
    input.levelSession = 12;
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 6;
    captured = state.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "capture new level session");
        return;
    }
    ctx.Check(state.PublishPending(captured.camera->Token()) ==
                  gamecamera12::CameraError12::None,
              "publish new level session");

    // A second teardown/complete cycle must not reuse the session ID or epoch.
    ctx.Check(state.BeginTeardown() == gamecamera12::CameraError12::None,
              "begin teardown before same-session test");
    ctx.Check(state.CompleteTeardown() == gamecamera12::CameraError12::None,
              "complete teardown before same-session test");
    ctx.Check(state.BeginLevelSession(12) ==
                  gamecamera12::CameraError12::NonMonotonicLevelSession,
              "same level session ID rejected");
    ctx.Check(state.BeginLevelSession(11) ==
                  gamecamera12::CameraError12::NonMonotonicLevelSession,
              "lower level session ID rejected");
    ctx.Check(state.BeginLevelSession(13) == gamecamera12::CameraError12::None,
              "strictly increasing level session accepted");
    ctx.Check(state.PublishPending(oldLevelToken) ==
                  gamecamera12::CameraError12::StaleLevelSession,
              "old identity rejected after second reload");
    input = MakeInput();
    input.levelSession = 13;
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 7;
    captured = state.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "capture after second reload");
        return;
    }
    ctx.Check(state.PublishPending(captured.camera->Token()) ==
                  gamecamera12::CameraError12::None,
              "publish after second reload");

    // Sequence monotonicity and rollover.
    input = MakeInput();
    input.levelSession = 13;
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 7;
    ctx.Check(state.Capture(input).error ==
                  gamecamera12::CameraError12::InvalidCaptureSequence,
              "duplicate sequence rejected");

    gamecamera12::CameraPublicationState12 overflowState;
    (void)overflowState.BeginLevelSession(20);
    input = MakeInput();
    input.levelSession = 20;
    input.captureEpoch = overflowState.CurrentEpoch();
    input.captureSequence = std::numeric_limits<uint64_t>::max();
    input.sourceGameFrame = 9001;
    const auto maxCaptured = overflowState.Capture(input);
    ctx.Check(maxCaptured.error == gamecamera12::CameraError12::None &&
              maxCaptured.camera.has_value(),
              "sequence max accepted once");
    if (!maxCaptured.camera)
        return;
    const auto maxToken = maxCaptured.camera->Token();
    ctx.Check(overflowState.PublishPending(maxToken) ==
                  gamecamera12::CameraError12::None,
              "sequence max can publish");
    const auto sequenceObjects = MakeSceneObjects(2);
    try
    {
        const auto maxEnvelope = gamecamera12::BuildPublishedScene12(
            overflowState, maxToken, sequenceObjects);
        ctx.Check(maxEnvelope.Token() == maxToken,
                  "sequence max builds scene");
    }
    catch (const std::exception& error)
    {
        ctx.Check(false, "sequence max scene build", error.what());
    }
    ctx.Check(!overflowState.RejectStaleWorkerResult(maxToken),
              "max sequence worker token accepted");
    auto otherToken = maxToken;
    otherToken.sourceGameFrame += 1;
    ctx.Check(overflowState.RejectStaleWorkerResult(otherToken),
              "other worker token rejected at max sequence");

    input = MakeInput();
    input.levelSession = 20;
    input.captureEpoch = overflowState.CurrentEpoch();
    input.captureSequence = 1;
    ctx.Check(overflowState.Capture(input).error ==
                  gamecamera12::CameraError12::CaptureSequenceOverflow,
              "sequence overflow controlled");
    ctx.Check(!overflowState.RejectStaleWorkerResult(maxToken),
              "published max sequence remains current after rejected capture");

    ctx.Check(overflowState.BeginReset() ==
                  gamecamera12::CameraError12::None,
              "sequence max reset");
    ctx.Check(overflowState.RejectStaleWorkerResult(maxToken),
              "reset invalidates max sequence worker token");
    try
    {
        (void)gamecamera12::BuildPublishedScene12(
            overflowState, maxToken, sequenceObjects);
        ctx.Check(false, "reset must remove max sequence publication");
    }
    catch (const std::logic_error&)
    {
        ctx.Check(true, "reset removes max sequence publication");
    }
    catch (...)
    {
        ctx.Check(false, "reset max sequence build must be logic_error");
    }

    input = MakeInput();
    input.levelSession = 20;
    input.captureEpoch = overflowState.CurrentEpoch();
    input.captureSequence = 1;
    ctx.Check(overflowState.Capture(input).error ==
                  gamecamera12::CameraError12::CaptureSequenceOverflow,
              "sequence exhaustion survives reset");
    ctx.Check(overflowState.BeginTeardown() ==
                  gamecamera12::CameraError12::None,
              "sequence overflow teardown");
    ctx.Check(overflowState.CompleteTeardown() ==
                  gamecamera12::CameraError12::None,
              "sequence overflow complete teardown");
    ctx.Check(overflowState.BeginLevelSession(21) ==
                  gamecamera12::CameraError12::CaptureSequenceOverflow,
              "sequence overflow rejects a new session");
    ctx.Check(overflowState.CurrentLevelSession() == 20,
              "sequence overflow preserves last level session");
    input.levelSession = 21;
    input.captureEpoch = overflowState.CurrentEpoch();
    input.captureSequence = 1;
    ctx.Check(overflowState.Capture(input).error ==
                  gamecamera12::CameraError12::NoLiveSession,
              "sequence overflow remains fail-closed across sessions");
    ctx.Check(overflowState.Shutdown() ==
                  gamecamera12::CameraError12::None,
              "sequence exhaustion shutdown terminal");

    // Frame ID rollover.
    gamecamera12::CameraPublicationState12 frameState;
    (void)frameState.BeginLevelSession(30);
    input = MakeInput();
    input.levelSession = 30;
    input.captureEpoch = frameState.CurrentEpoch();
    input.captureSequence = 1;
    input.sourceGameFrame = std::numeric_limits<uint32_t>::max();
    ctx.Check(frameState.Capture(input).error ==
                  gamecamera12::CameraError12::None,
              "frame max accepted");
    input.captureSequence = 2;
    input.sourceGameFrame = 0;
    ctx.Check(frameState.Capture(input).error ==
                  gamecamera12::CameraError12::None &&
              frameState.Pending().has_value() &&
              frameState.Pending()->SourceGameFrame() == 0,
              "frame rollover accepted");

    // Disable invalidates and permits a later session.
    ctx.Check(state.BeginDisable() == gamecamera12::CameraError12::None,
              "disable invalidates publication");
    input = MakeInput();
    input.levelSession = 13;
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 8;
    ctx.Check(state.Capture(input).error ==
                  gamecamera12::CameraError12::NoLiveSession,
              "capture while disabled rejected");
    ctx.Check(state.BeginLevelSession(14) == gamecamera12::CameraError12::None,
              "session can begin after disable");
    input.levelSession = 14;
    input.captureEpoch = state.CurrentEpoch();
    ctx.Check(state.Capture(input).error == gamecamera12::CameraError12::None,
              "capture after disable and new session");

    // Teardown is terminal even with a normal (non-exhausted) epoch.
    gamecamera12::CameraPublicationState12 teardownState;
    (void)teardownState.BeginLevelSession(41);
    input = MakeInput();
    input.levelSession = 41;
    input.captureEpoch = teardownState.CurrentEpoch();
    input.captureSequence = 1;
    captured = teardownState.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "capture before teardown");
        return;
    }
    const auto teardownToken = captured.camera->Token();
    (void)teardownState.PublishPending(teardownToken);
    ctx.Check(teardownState.BeginTeardown() ==
                  gamecamera12::CameraError12::None,
              "teardown invalidates publication");
    ctx.Check(!teardownState.HasPublished() && !teardownState.HasPending(),
              "teardown clears publication");
    ctx.Check(teardownState.PublishPending(teardownToken) ==
                  gamecamera12::CameraError12::Teardown,
              "publish after teardown rejected");
    ctx.Check(teardownState.Capture(input).error ==
                  gamecamera12::CameraError12::Teardown,
              "capture after teardown rejected");
    ctx.Check(teardownState.BeginLevelSession(42) ==
                  gamecamera12::CameraError12::Teardown,
              "level session after teardown rejected");
    ctx.Check(teardownState.BeginDisable() ==
                  gamecamera12::CameraError12::Teardown,
              "disable after teardown rejected");
    ctx.Check(teardownState.CompleteTeardown() ==
                  gamecamera12::CameraError12::None,
              "complete teardown after teardown");
    ctx.Check(!teardownState.HasPublished() &&
              !teardownState.HasPending(),
              "complete teardown does not publish");
    ctx.Check(teardownState.BeginLevelSession(42) ==
                  gamecamera12::CameraError12::None,
              "level session after complete teardown");
    input = MakeInput();
    input.levelSession = 42;
    input.captureEpoch = teardownState.CurrentEpoch();
    input.captureSequence = 2;
    captured = teardownState.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "capture after complete teardown");
        return;
    }
    ctx.Check(teardownState.PublishPending(captured.camera->Token()) ==
                  gamecamera12::CameraError12::None,
              "publish after complete teardown");
    ctx.Check(teardownState.PublishPending(teardownToken) ==
                  gamecamera12::CameraError12::StaleLevelSession,
              "pre-teardown token rejected after new session");

    // Session IDs cannot wrap; a max session leaves no next identity.
    gamecamera12::CameraPublicationState12 maxSessionState;
    ctx.Check(maxSessionState.BeginLevelSession(
                  std::numeric_limits<uint64_t>::max()) ==
                  gamecamera12::CameraError12::None,
              "max level session accepted");
    ctx.Check(maxSessionState.CompleteTeardown() ==
                  gamecamera12::CameraError12::NoLiveSession,
              "complete teardown while live rejected");
    ctx.Check(maxSessionState.BeginTeardown() ==
                  gamecamera12::CameraError12::None,
              "max session teardown");
    ctx.Check(maxSessionState.CompleteTeardown() ==
                  gamecamera12::CameraError12::None,
              "max session complete teardown");
    ctx.Check(maxSessionState.BeginLevelSession(1) ==
                  gamecamera12::CameraError12::LevelSessionOverflow,
              "level session overflow controlled");
    ctx.Check(maxSessionState.CurrentLevelSession() ==
                  std::numeric_limits<uint64_t>::max(),
              "failed session allocation preserves last identity");

    // Shutdown is terminal and rejects an old token.
    gamecamera12::CameraPublicationState12 shutdownState;
    (void)shutdownState.BeginLevelSession(40);
    input = MakeInput();
    input.levelSession = 40;
    input.captureEpoch = shutdownState.CurrentEpoch();
    input.captureSequence = 1;
    captured = shutdownState.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "capture before shutdown");
        return;
    }
    const auto shutdownToken = captured.camera->Token();
    ctx.Check(shutdownState.PublishPending(shutdownToken) ==
                  gamecamera12::CameraError12::None,
              "publish before shutdown");
    ctx.Check(shutdownState.Shutdown() == gamecamera12::CameraError12::None,
              "shutdown");
    const auto shutdownEpoch = shutdownState.CurrentEpoch();
    ctx.Check(shutdownState.Shutdown() ==
                  gamecamera12::CameraError12::Shutdown,
              "second shutdown is idempotent");
    ctx.Check(shutdownState.CurrentEpoch() == shutdownEpoch,
              "second shutdown does not advance epoch");
    ctx.Check(!shutdownState.HasPublished() && !shutdownState.HasPending(),
              "shutdown clears publication");
    ctx.Check(shutdownState.RejectStaleWorkerResult(shutdownToken),
              "worker token rejected after shutdown");
    ctx.Check(shutdownState.PublishPending(shutdownToken) ==
                  gamecamera12::CameraError12::Shutdown,
              "publish after shutdown rejected");
    ctx.Check(shutdownState.Capture(input).error ==
                  gamecamera12::CameraError12::Shutdown,
              "capture after shutdown rejected");
    ctx.Check(shutdownState.BeginLevelSession(43) ==
                  gamecamera12::CameraError12::Shutdown,
              "level session after shutdown rejected");
    ctx.Check(shutdownState.CompleteTeardown() ==
                  gamecamera12::CameraError12::Shutdown,
              "complete teardown after shutdown rejected");
}

// Regression for the old lifecycle defect: a level session could be reused
// while the process still held a live publication state.
void TestSessionUniqueness(TestContext& ctx)
{
    gamecamera12::CameraPublicationState12 state;
    constexpr uint64_t firstSession = 200;
    ctx.Check(state.BeginLevelSession(firstSession) ==
                  gamecamera12::CameraError12::None,
              "uniqueness begin first session");
    ctx.Check(state.BeginLevelSession(firstSession + 1) ==
                  gamecamera12::CameraError12::SessionAlreadyActive,
              "uniqueness live session rejection");
    ctx.Check(state.BeginTeardown() == gamecamera12::CameraError12::None,
              "uniqueness begin teardown");
    ctx.Check(state.BeginLevelSession(firstSession + 1) ==
                  gamecamera12::CameraError12::Teardown,
              "uniqueness begin before complete");
    ctx.Check(state.CompleteTeardown() == gamecamera12::CameraError12::None,
              "uniqueness complete teardown");
    ctx.Check(state.BeginTeardown() ==
                  gamecamera12::CameraError12::NoLiveSession,
              "uniqueness teardown from no-level rejected");
    ctx.Check(state.BeginLevelSession(firstSession) ==
                  gamecamera12::CameraError12::NonMonotonicLevelSession,
              "uniqueness same session rejected");
    ctx.Check(state.BeginLevelSession(firstSession - 1) ==
                  gamecamera12::CameraError12::NonMonotonicLevelSession,
              "uniqueness lower session rejected");
    ctx.Check(state.BeginLevelSession(firstSession + 1) ==
                  gamecamera12::CameraError12::None,
              "uniqueness greater session accepted");
    ctx.Check(state.BeginTeardown() == gamecamera12::CameraError12::None,
              "uniqueness teardown after accepted session");
    ctx.Check(state.CompleteTeardown() == gamecamera12::CameraError12::None,
              "uniqueness complete after accepted session");
    ctx.Check(state.BeginLevelSession(firstSession + 1) ==
                  gamecamera12::CameraError12::NonMonotonicLevelSession,
              "uniqueness immediate same session rejected");

    gamecamera12::CameraPublicationState12 maxState;
    ctx.Check(maxState.BeginLevelSession(
                  std::numeric_limits<uint64_t>::max()) ==
                  gamecamera12::CameraError12::None,
              "uniqueness max session accepted");
    ctx.Check(maxState.BeginTeardown() ==
                  gamecamera12::CameraError12::None,
              "uniqueness max session teardown");
    ctx.Check(maxState.CompleteTeardown() ==
                  gamecamera12::CameraError12::None,
              "uniqueness max session complete");
    ctx.Check(maxState.BeginLevelSession(1) ==
                  gamecamera12::CameraError12::LevelSessionOverflow,
              "uniqueness max session overflow");
    ctx.Check(maxState.CurrentLevelSession() ==
                  std::numeric_limits<uint64_t>::max(),
              "uniqueness max identity preserved");
}

// Regression for terminal teardown: a process-lifetime publication state must
// complete teardown before starting the next level or same-level QuickLoad.
void TestLifecycleRegression(TestContext& ctx)
{
    gamecamera12::CameraPublicationState12 state;
    constexpr uint64_t jupiter1 = 1001;
    constexpr uint64_t jupiter2 = 1002;
    constexpr uint64_t zaton = 1003;
    constexpr uint64_t quickLoad = 1004;

    ctx.Check(state.BeginLevelSession(jupiter1) ==
                  gamecamera12::CameraError12::None,
              "lifecycle Jupiter begin");
    auto input = MakeInput();
    input.levelSession = jupiter1;
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 1;
    input.sourceGameFrame = 7001;
    auto captured = state.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "lifecycle Jupiter first capture");
        return;
    }
    const auto jupiterToken1 = captured.camera->Token();
    ctx.Check(state.PublishPending(jupiterToken1) ==
                  gamecamera12::CameraError12::None,
              "lifecycle Jupiter first publish");

    ctx.Check(state.BeginTeardown() == gamecamera12::CameraError12::None,
              "lifecycle disconnect teardown");
    ctx.Check(!state.HasPublished() && !state.HasPending(),
              "lifecycle disconnect clears publication");
    ctx.Check(state.Capture(input).error ==
                  gamecamera12::CameraError12::Teardown,
              "lifecycle capture during teardown rejected");
    ctx.Check(state.PublishPending(jupiterToken1) ==
                  gamecamera12::CameraError12::Teardown,
              "lifecycle publish during teardown rejected");
    ctx.Check(state.CompleteTeardown() == gamecamera12::CameraError12::None,
              "lifecycle disconnect complete");
    ctx.Check(state.BeginLevelSession(jupiter2) ==
                  gamecamera12::CameraError12::None,
              "lifecycle Jupiter reconnect begin");
    input = MakeInput();
    input.levelSession = jupiter2;
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 2;
    input.sourceGameFrame = 7002;
    captured = state.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "lifecycle Jupiter reconnect capture");
        return;
    }
    const auto jupiterToken2 = captured.camera->Token();
    ctx.Check(state.PublishPending(jupiterToken2) ==
                  gamecamera12::CameraError12::None,
              "lifecycle Jupiter reconnect publish");
    ctx.Check(state.PublishPending(jupiterToken1) ==
                  gamecamera12::CameraError12::StaleLevelSession,
              "lifecycle old Jupiter token rejected");
    ctx.Check(state.RejectStaleWorkerResult(jupiterToken1) &&
              !state.RejectStaleWorkerResult(jupiterToken2),
              "lifecycle old Jupiter worker rejected");

    // Jupiter -> main menu -> Zaton: another strictly newer session.
    ctx.Check(state.BeginTeardown() == gamecamera12::CameraError12::None,
              "lifecycle main menu teardown");
    ctx.Check(state.CompleteTeardown() == gamecamera12::CameraError12::None,
              "lifecycle main menu complete");
    ctx.Check(state.BeginLevelSession(zaton) ==
                  gamecamera12::CameraError12::None,
              "lifecycle Zaton begin");
    input = MakeInput();
    input.levelSession = zaton;
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 3;
    input.sourceGameFrame = 7003;
    captured = state.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "lifecycle Zaton capture");
        return;
    }
    const auto zatonToken = captured.camera->Token();
    ctx.Check(state.PublishPending(zatonToken) ==
                  gamecamera12::CameraError12::None,
              "lifecycle Zaton publish");
    ctx.Check(state.PublishPending(jupiterToken2) ==
                  gamecamera12::CameraError12::StaleLevelSession,
              "lifecycle Jupiter worker rejected after Zaton");

    // Same-level QuickLoad: CLevel may stay alive, logical session must not.
    ctx.Check(state.BeginTeardown() == gamecamera12::CameraError12::None,
              "lifecycle quickload teardown");
    ctx.Check(state.CompleteTeardown() == gamecamera12::CameraError12::None,
              "lifecycle quickload complete");
    ctx.Check(state.BeginLevelSession(quickLoad) ==
                  gamecamera12::CameraError12::None,
              "lifecycle quickload new session");
    input = MakeInput();
    input.levelSession = quickLoad;
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 4;
    input.sourceGameFrame = 7004;
    captured = state.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "lifecycle quickload capture");
        return;
    }
    const auto quickLoadToken = captured.camera->Token();
    ctx.Check(state.PublishPending(quickLoadToken) ==
                  gamecamera12::CameraError12::None,
              "lifecycle quickload publish");
    ctx.Check(state.PublishPending(zatonToken) ==
                  gamecamera12::CameraError12::StaleLevelSession,
              "lifecycle quickload rejects old session token");

    // Reset changes epoch, keeps session, and invalidates workers.
    const auto resetEpoch = state.CurrentEpoch();
    const auto resetSession = state.CurrentLevelSession();
    ctx.Check(state.BeginReset() == gamecamera12::CameraError12::None,
              "lifecycle reset");
    ctx.Check(state.CurrentEpoch() == resetEpoch + 1 &&
              state.CurrentLevelSession() == resetSession,
              "lifecycle reset changes epoch but not session");
    ctx.Check(!state.HasPublished() && !state.HasPending(),
              "lifecycle reset clears publication");
    ctx.Check(state.PublishPending(quickLoadToken) ==
                  gamecamera12::CameraError12::StaleCaptureEpoch,
              "lifecycle reset rejects old epoch token");
    input = MakeInput();
    input.levelSession = quickLoad;
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 5;
    input.sourceGameFrame = 7005;
    captured = state.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "lifecycle post-reset capture");
        return;
    }
    const auto resetToken = captured.camera->Token();
    ctx.Check(state.PublishPending(resetToken) ==
                  gamecamera12::CameraError12::None,
              "lifecycle post-reset publish");
    ctx.Check(state.Published().has_value() &&
              state.Published()->SourceGameFrame() == 7005,
              "lifecycle post-reset published frame");

    // Shutdown remains terminal after a failed transition.
    ctx.Check(state.BeginLevelSession(quickLoad + 1) ==
                  gamecamera12::CameraError12::SessionAlreadyActive,
              "lifecycle failed transition while live");
    ctx.Check(state.Shutdown() == gamecamera12::CameraError12::None,
              "lifecycle shutdown");
    ctx.Check(state.PublishPending(resetToken) ==
                  gamecamera12::CameraError12::Shutdown,
              "lifecycle shutdown rejects publish");
    ctx.Check(state.BeginLevelSession(quickLoad + 2) ==
                  gamecamera12::CameraError12::Shutdown,
              "lifecycle shutdown rejects begin");
    ctx.Check(state.CompleteTeardown() ==
                  gamecamera12::CameraError12::Shutdown,
              "lifecycle shutdown rejects complete");
}

// Regression for the old failed-Capture defect and for publishing A into a
// failed B frame. Historical Published A remains explicit-token-only.
void TestFailedCaptureAndFrameGate(TestContext& ctx)
{
    // Pending A must be cancelled even when the next Capture fails.
    {
        gamecamera12::CameraPublicationState12 state;
        (void)state.BeginLevelSession(300);
        auto input = MakeInput();
        input.levelSession = 300;
        input.captureEpoch = state.CurrentEpoch();
        input.captureSequence = 1;
        input.sourceGameFrame = 8001;
        const auto capturedA = state.Capture(input);
        if (!capturedA.camera)
        {
            ctx.Check(false, "failed capture pending A");
            return;
        }
        const auto tokenA = capturedA.camera->Token();

        input = MakeInput();
        input.levelSession = 300;
        input.captureEpoch = state.CurrentEpoch();
        input.captureSequence = 2;
        input.sourceGameFrame = 8002;
        input.view.m[0][0] = std::numeric_limits<float>::quiet_NaN();
        const auto failedB = state.Capture(input);
        ctx.Check(failedB.error == gamecamera12::CameraError12::InvalidView &&
                  !failedB.camera.has_value(),
                  "failed capture B diagnosed");
        ctx.Check(!state.HasPending(),
                  "failed capture clears previous pending");
        ctx.Check(state.PublishPending(tokenA) ==
                      gamecamera12::CameraError12::NoPendingCamera,
                  "failed capture cannot publish old pending A");

        input = MakeInput();
        input.levelSession = 300;
        input.captureEpoch = state.CurrentEpoch();
        input.captureSequence = 3;
        input.sourceGameFrame = 8003;
        const auto capturedC = state.Capture(input);
        if (!capturedC.camera)
        {
            ctx.Check(false, "failed capture recovery C");
            return;
        }
        ctx.Check(state.PublishPending(capturedC.camera->Token()) ==
                      gamecamera12::CameraError12::None,
                  "valid capture C after failed B publishes");
        ctx.Check(state.Published().has_value() &&
                  state.Published()->SourceGameFrame() == 8003,
                  "published frame is C after failed B");
    }

    // A published A must not satisfy a frame-local request for failed B.
    {
        gamecamera12::CameraPublicationState12 state;
        (void)state.BeginLevelSession(301);
        auto input = MakeInput();
        input.levelSession = 301;
        input.captureEpoch = state.CurrentEpoch();
        input.captureSequence = 1;
        input.sourceGameFrame = 8101;
        auto capturedA = state.Capture(input);
        if (!capturedA.camera)
        {
            ctx.Check(false, "frame gate published A");
            return;
        }
        const auto tokenA = capturedA.camera->Token();
        ctx.Check(state.PublishPending(tokenA) ==
                      gamecamera12::CameraError12::None,
                  "frame gate publish A");

        input = MakeInput();
        input.levelSession = 301;
        input.captureEpoch = state.CurrentEpoch();
        input.captureSequence = 2;
        input.sourceGameFrame = 8102;
        input.projection.m[2][2] = std::numeric_limits<float>::quiet_NaN();
        const auto failedB = state.Capture(input);
        ctx.Check(failedB.error ==
                      gamecamera12::CameraError12::InvalidProjection &&
                  !state.HasPending(),
                  "frame gate failed B clears pending");
        ctx.Check(state.Published().has_value() &&
                  state.Published()->Token() == tokenA,
                  "failed B preserves historical published A");
        ctx.Check(state.PublishPending(tokenA) ==
                      gamecamera12::CameraError12::NoPendingCamera,
                  "failed B leaves A non-pending");

        gamecamera12::CaptureToken12 tokenB{};
        tokenB.sourceGameFrame = 8102;
        tokenB.captureSequence = 2;
        tokenB.levelSession = 301;
        tokenB.captureEpoch = state.CurrentEpoch();
        const auto objects = MakeSceneObjects(2);

        try
        {
            (void)gamecamera12::BuildPublishedScene12(
                state, tokenB, objects);
            ctx.Check(false, "frame-local B gate must reject A");
        }
        catch (const std::logic_error&)
        {
            ctx.Check(true, "frame-local B gate rejects historical A");
        }
        catch (...)
        {
            ctx.Check(false, "frame-local B gate must throw logic_error");
        }

        try
        {
            const auto historicalA = gamecamera12::BuildPublishedScene12(
                state, tokenA, objects);
            ctx.Check(historicalA.Token() == tokenA &&
                      historicalA.Scene().Objects().size() == objects.size(),
                      "historical A requires explicit A token");
        }
        catch (const std::exception& error)
        {
            ctx.Check(false, "historical A build", error.what());
        }

        input = MakeInput();
        input.levelSession = 301;
        input.captureEpoch = state.CurrentEpoch();
        input.captureSequence = 3;
        input.sourceGameFrame = 8103;
        auto capturedC = state.Capture(input);
        if (!capturedC.camera)
        {
            ctx.Check(false, "frame gate C capture");
            return;
        }
        const auto tokenC = capturedC.camera->Token();
        ctx.Check(state.PublishPending(tokenC) ==
                      gamecamera12::CameraError12::None,
                  "frame gate publish C");

        try
        {
            const auto currentC = gamecamera12::BuildPublishedScene12(
                state, tokenC, objects);
            ctx.Check(currentC.Token() == tokenC,
                      "frame-local C gate builds current C");
        }
        catch (const std::exception& error)
        {
            ctx.Check(false, "frame-local C build", error.what());
        }

        try
        {
            (void)gamecamera12::BuildPublishedScene12(
                state, tokenA, objects);
            ctx.Check(false, "frame-local A gate must reject C");
        }
        catch (const std::logic_error&)
        {
            ctx.Check(true, "frame-local A gate rejects current C");
        }
        catch (...)
        {
            ctx.Check(false, "frame-local A gate must throw logic_error");
        }
    }
}

#ifdef GAMECAMERA_TESTING
void TestEpochExhaustion(TestContext& ctx)
{
    const auto maxEpoch = std::numeric_limits<uint64_t>::max();

    {
        gamecamera12::CameraPublicationState12 state;
        (void)state.BeginLevelSession(50);
        auto input = MakeInput();
        input.levelSession = 50;
        input.captureEpoch = state.CurrentEpoch();
        input.captureSequence = 1;
        auto captured = state.Capture(input);
        if (!captured.camera)
        {
            ctx.Check(false, "epoch exhaustion setup capture");
        }
        else
        {
            ctx.Check(state.PublishPending(captured.camera->Token()) ==
                          gamecamera12::CameraError12::None,
                      "epoch exhaustion setup publish");
            const auto oldToken = captured.camera->Token();
            state.SetEpochForTest(maxEpoch);
            ctx.Check(state.RejectStaleWorkerResult(oldToken),
                      "max epoch rejects published worker");
            ctx.Check(state.BeginTeardown() ==
                          gamecamera12::CameraError12::CaptureEpochOverflow,
                      "teardown at max epoch returns overflow");
            ctx.Check(!state.HasPublished() && !state.HasPending(),
                      "teardown overflow clears publication");
            ctx.Check(state.RejectStaleWorkerResult(oldToken),
                      "teardown overflow rejects old token");
            ctx.Check(state.PublishPending(oldToken) ==
                          gamecamera12::CameraError12::Teardown,
                      "publish after teardown overflow remains teardown");
            ctx.Check(state.CompleteTeardown() ==
                          gamecamera12::CameraError12::None,
                      "complete teardown after max epoch teardown");
            ctx.Check(!state.HasPublished() && !state.HasPending(),
                      "complete teardown after max epoch does not publish");
            ctx.Check(state.BeginLevelSession(51) ==
                          gamecamera12::CameraError12::CaptureEpochOverflow,
                      "new session after max epoch teardown rejected");
        }
    }

    {
        gamecamera12::CameraPublicationState12 state;
        (void)state.BeginLevelSession(51);
        auto input = MakeInput();
        input.levelSession = 51;
        input.captureEpoch = state.CurrentEpoch();
        input.captureSequence = 1;
        auto captured = state.Capture(input);
        if (captured.camera)
        {
            (void)state.PublishPending(captured.camera->Token());
            const auto oldToken = captured.camera->Token();
            state.SetEpochForTest(maxEpoch);
            ctx.Check(state.BeginReset() ==
                          gamecamera12::CameraError12::CaptureEpochOverflow,
                      "reset at max epoch returns overflow");
            ctx.Check(!state.HasPublished() && !state.HasPending(),
                      "reset overflow clears publication");
            ctx.Check(state.RejectStaleWorkerResult(oldToken),
                      "reset overflow rejects old token");
            ctx.Check(state.Capture(input).error ==
                          gamecamera12::CameraError12::NoLiveSession,
                      "capture after reset overflow rejected");
            ctx.Check(state.PublishPending(oldToken) ==
                          gamecamera12::CameraError12::NoLiveSession,
                      "publish after reset overflow rejected");
        }
    }

    {
        gamecamera12::CameraPublicationState12 state;
        (void)state.BeginLevelSession(55);
        auto input = MakeInput();
        input.levelSession = 55;
        input.captureEpoch = state.CurrentEpoch();
        input.captureSequence = 1;
        auto captured = state.Capture(input);
        if (captured.camera)
        {
            (void)state.PublishPending(captured.camera->Token());
            const auto oldToken = captured.camera->Token();
            state.SetEpochForTest(maxEpoch);
            ctx.Check(state.BeginDisable() ==
                          gamecamera12::CameraError12::CaptureEpochOverflow,
                      "disable at max epoch returns overflow");
            ctx.Check(!state.HasPublished() && !state.HasPending(),
                      "disable overflow clears publication");
            ctx.Check(state.PublishPending(oldToken) ==
                          gamecamera12::CameraError12::NoLiveSession,
                      "publish after disable overflow rejected");
            ctx.Check(state.Capture(input).error ==
                          gamecamera12::CameraError12::NoLiveSession,
                      "capture after disable overflow rejected");
        }
    }

    // At max-1 the reset transition reaches max, but captures at max are
    // rejected and clear publication instead of continuing.
    {
        gamecamera12::CameraPublicationState12 state;
        (void)state.BeginLevelSession(56);
        state.SetEpochForTest(maxEpoch - 1);
        auto input = MakeInput();
        input.levelSession = 56;
        input.captureEpoch = maxEpoch - 1;
        input.captureSequence = 1;
        auto captured = state.Capture(input);
        if (captured.camera)
        {
            (void)state.PublishPending(captured.camera->Token());
            ctx.Check(state.BeginReset() ==
                          gamecamera12::CameraError12::None,
                      "reset from max-1 reaches max epoch");
            ctx.Check(state.CurrentEpoch() == maxEpoch,
                      "epoch reaches max without wrap");
            input.captureEpoch = maxEpoch;
            input.captureSequence = 2;
            const auto maxCapture = state.Capture(input);
            ctx.Check(maxCapture.error ==
                          gamecamera12::CameraError12::CaptureEpochOverflow,
                      "capture at reached max epoch fails closed");
            ctx.Check(!state.HasPublished() && !state.HasPending(),
                      "capture at reached max clears publication");
        }
    }

    // A pending camera from max-1 cannot be published after reaching max.
    {
        gamecamera12::CameraPublicationState12 state;
        (void)state.BeginLevelSession(57);
        state.SetEpochForTest(maxEpoch - 1);
        auto input = MakeInput();
        input.levelSession = 57;
        input.captureEpoch = maxEpoch - 1;
        input.captureSequence = 1;
        const auto captured = state.Capture(input);
        if (!captured.camera)
        {
            ctx.Check(false, "max publish pending setup capture");
        }
        else
        {
            const auto oldToken = captured.camera->Token();
            state.SetEpochForTest(maxEpoch);
            ctx.Check(state.PublishPending(oldToken) ==
                          gamecamera12::CameraError12::CaptureEpochOverflow,
                      "publish at max epoch fails closed");
            ctx.Check(!state.HasPublished() && !state.HasPending(),
                      "publish at max clears publication");
            ctx.Check(state.RejectStaleWorkerResult(oldToken),
                      "publish at max rejects old token");
        }
    }

    {
        gamecamera12::CameraPublicationState12 state;
        (void)state.BeginLevelSession(52);
        auto input = MakeInput();
        input.levelSession = 52;
        input.captureEpoch = state.CurrentEpoch();
        input.captureSequence = 1;
        auto captured = state.Capture(input);
        if (captured.camera)
        {
            (void)state.PublishPending(captured.camera->Token());
            const auto oldToken = captured.camera->Token();
            state.SetEpochForTest(maxEpoch);
            ctx.Check(state.RejectStaleWorkerResult(oldToken),
                      "max epoch shutdown setup rejects published worker");
            ctx.Check(state.Shutdown() ==
                          gamecamera12::CameraError12::CaptureEpochOverflow,
                      "shutdown at max epoch returns overflow");
            const auto maxShutdownEpoch = state.CurrentEpoch();
            ctx.Check(state.Shutdown() ==
                          gamecamera12::CameraError12::Shutdown,
                      "second shutdown at max epoch is idempotent");
            ctx.Check(state.CurrentEpoch() == maxShutdownEpoch,
                      "second shutdown at max epoch preserves epoch");
            ctx.Check(!state.HasPublished() && !state.HasPending(),
                      "shutdown overflow clears publication");
            ctx.Check(state.RejectStaleWorkerResult(oldToken),
                      "max shutdown rejects old worker token");
            ctx.Check(state.PublishPending(oldToken) ==
                          gamecamera12::CameraError12::Shutdown,
                      "shutdown overflow remains terminal");
            ctx.Check(state.Capture(input).error ==
                          gamecamera12::CameraError12::Shutdown,
                      "capture after shutdown overflow rejected");
        }
    }

    {
        gamecamera12::CameraPublicationState12 state;
        (void)state.BeginLevelSession(53);
        auto input = MakeInput();
        input.levelSession = 53;
        input.captureEpoch = state.CurrentEpoch();
        input.captureSequence = 1;
        auto captured = state.Capture(input);
        if (!captured.camera)
        {
            ctx.Check(false, "level session overflow setup capture");
        }
        else
        {
            (void)state.PublishPending(captured.camera->Token());
            const auto oldToken = captured.camera->Token();
            ctx.Check(state.BeginTeardown() ==
                          gamecamera12::CameraError12::None,
                      "level session overflow setup teardown");
            ctx.Check(state.CompleteTeardown() ==
                          gamecamera12::CameraError12::None,
                      "level session overflow setup complete teardown");
            state.SetEpochForTest(maxEpoch);
            ctx.Check(state.BeginLevelSession(54) ==
                          gamecamera12::CameraError12::CaptureEpochOverflow,
                      "level session at max epoch returns overflow");
            ctx.Check(state.CurrentEpoch() == maxEpoch,
                      "epoch does not wrap");
            ctx.Check(!state.HasPublished() && !state.HasPending(),
                      "session overflow clears publication");
            ctx.Check(state.RejectStaleWorkerResult(oldToken),
                      "session overflow rejects old token");
            input.levelSession = 54;
            input.captureEpoch = maxEpoch;
            input.captureSequence = 1;
            ctx.Check(state.Capture(input).error ==
                          gamecamera12::CameraError12::NoLiveSession,
                      "capture after session epoch overflow rejected");
        }
    }
}
#endif

// RenderSceneSnapshot12 owns a const vector, so its generated move
// constructor may copy and throw. The envelope must not hide that fact.
static_assert(!std::is_nothrow_move_constructible_v<
                  renderprep12::RenderSceneSnapshot12>,
              "scene move must not be advertised noexcept");
static_assert(!std::is_nothrow_move_constructible_v<
                  gamecamera12::CapturedSceneSnapshot12>,
              "captured envelope move must not be advertised noexcept");

void TestEnvelopeMove(TestContext& ctx)
{
    auto input = MakeInput();
    auto result = gamecamera12::ValidateCamera12(input);
    if (!result.camera)
    {
        ctx.Check(false, "envelope move camera validation");
        return;
    }

    std::vector<renderprep12::Object12> objects(16);
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
        objects[i].id = i + 1;
        objects[i].world.m[0][0] = 1.0f;
        objects[i].world.m[1][1] = 1.0f;
        objects[i].world.m[2][2] = 1.0f;
        objects[i].world.m[3][3] = 1.0f;
    }

    gamecamera12::CameraPublicationState12 state;
    (void)state.BeginLevelSession(input.levelSession);
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 1;
    const auto captured = state.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "envelope move source capture");
        return;
    }
    if (state.PublishPending(captured.camera->Token()) !=
        gamecamera12::CameraError12::None ||
        !state.Published())
    {
        ctx.Check(false, "envelope move source publication");
        return;
    }

    try
    {
        auto envelope = gamecamera12::BuildPublishedScene12(
            state, captured.camera->Token(), objects);
        auto moved = std::move(envelope);
        ctx.Check(moved.Token() == captured.camera->Token() &&
                  moved.Scene().Objects().size() == objects.size() &&
                  moved.Scene().Objects().front().id == 1,
                  "envelope move preserves owned objects and token");
    }
    catch (const std::exception& error)
    {
        ctx.Check(false, "envelope move", error.what());
    }
}

void TestSceneBridge(TestContext& ctx)
{
    auto input = MakeInput();
    auto result = gamecamera12::ValidateCamera12(input);
    if (!result.camera)
    {
        ctx.Check(false, "scene bridge camera validation");
        return;
    }

    renderprep12::Object12 object{};
    object.id = 1;
    object.world = {};
    object.world.m[0][0] = 1.0f;
    object.world.m[1][1] = 1.0f;
    object.world.m[2][2] = 1.0f;
    object.world.m[3][3] = 1.0f;
    std::vector<renderprep12::Object12> objects{object};

    try
    {
        auto scene = gamecamera12::BuildSceneSnapshot12(
            *result.camera, objects, input.levelSession);
        ctx.Check(scene.Generation() == input.levelSession,
                  "scene snapshot preserves level generation");
        ctx.Check(Near(scene.View().m[3][0], input.view.m[3][0]),
                  "scene snapshot preserves view");
        ctx.Check(Near(scene.Camera().m[0][0],
                       result.camera->ViewProjection().m[0][0]),
                  "scene snapshot builds ViewProjection");
        const XMFLOAT4X4 expectedShader = Transpose(result.camera->ViewProjection());
        ctx.Check(Near(scene.ShaderCamera().m[0][0], expectedShader.m[0][0]) &&
                  Near(scene.ShaderCamera().m[3][2], expectedShader.m[3][2]),
                  "existing snapshot performs exactly one shader transpose");

        objects[0].id = 99;
        ctx.Check(scene.Objects().size() == 1 &&
                  scene.Objects()[0].id == 1,
                  "scene owns its object copy");
    }
    catch (const std::exception& error)
    {
        ctx.Check(false, "scene snapshot construction", error.what());
    }

    gamecamera12::CameraPublicationState12 state;
    (void)state.BeginLevelSession(input.levelSession);
    const auto captured = state.Capture(input);
    if (!captured.camera)
    {
        ctx.Check(false, "scene envelope source capture");
        return;
    }
    ctx.Check(state.PublishPending(captured.camera->Token()) ==
                  gamecamera12::CameraError12::None,
              "scene envelope source published");
    if (!state.Published())
    {
        ctx.Check(false, "scene envelope published optional");
        return;
    }
    try
    {
        auto envelope = gamecamera12::BuildPublishedScene12(
            state, captured.camera->Token(), objects);
        ctx.Check(envelope.Scene().Generation() == input.levelSession &&
                  envelope.Token() == captured.camera->Token() &&
                  envelope.SourceGameFrame() == input.sourceGameFrame,
                  "scene envelope carries published capture identity");
        (void)state.BeginReset();
        ctx.Check(state.RejectStaleWorkerResult(envelope.Token()),
                  "published envelope token rejected after reset");
        ctx.Check(envelope.Scene().Generation() == input.levelSession,
                  "old envelope remains immutable but non-current");
    }
    catch (const std::exception& error)
    {
        ctx.Check(false, "captured scene envelope", error.what());
    }

    try
    {
        (void)gamecamera12::BuildSceneSnapshot12(
            *result.camera, objects, input.levelSession + 1);
        ctx.Check(false, "scene session mismatch must throw");
    }
    catch (const std::invalid_argument&)
    {
        ctx.Check(true, "scene session mismatch throws invalid_argument");
    }
    catch (...)
    {
        ctx.Check(false, "scene session mismatch must be invalid_argument");
    }

    gamecamera12::CameraPublicationState12 emptyState;
    gamecamera12::CaptureToken12 emptyToken{};
    try
    {
        (void)gamecamera12::BuildPublishedScene12(
            emptyState, emptyToken, objects);
        ctx.Check(false, "envelope without publication must throw");
    }
    catch (const std::logic_error&)
    {
        ctx.Check(true,
                  "envelope without publication throws logic_error");
    }
    catch (...)
    {
        ctx.Check(false,
                  "envelope without publication must be logic_error");
    }

    gamecamera12::CameraPublicationState12 pendingState;
    (void)pendingState.BeginLevelSession(input.levelSession);
    input.captureEpoch = pendingState.CurrentEpoch();
    input.captureSequence = 1;
    const auto pendingCapture = pendingState.Capture(input);
    if (!pendingCapture.camera)
    {
        ctx.Check(false, "envelope pending-only fixture capture");
    }
    else
    {
        try
        {
            (void)gamecamera12::BuildPublishedScene12(
                pendingState, pendingCapture.camera->Token(), objects);
            ctx.Check(false, "pending-only envelope must throw");
        }
        catch (const std::logic_error&)
        {
            ctx.Check(true,
                      "pending-only envelope cannot bypass publication");
        }
        catch (...)
        {
            ctx.Check(false,
                      "pending-only envelope must be logic_error");
        }
    }
}
#ifdef GAMECAMERA_OOM_TEST
void TestSceneAllocationFailure(TestContext& ctx)
{
    auto input = MakeInput();
    auto result = gamecamera12::ValidateCamera12(input);
    if (!result.camera)
    {
        ctx.Check(false, "scene OOM fixture validation");
        return;
    }

    renderprep12::Object12 object{};
    object.id = 1;
    object.world.m[0][0] = 1.0f;
    object.world.m[1][1] = 1.0f;
    object.world.m[2][2] = 1.0f;
    object.world.m[3][3] = 1.0f;
    std::vector<renderprep12::Object12> objects{object};

    bool caught = false;
    g_fail_next_allocation.store(true, std::memory_order_release);
    try
    {
        auto scene = gamecamera12::BuildSceneSnapshot12(
            *result.camera, objects, input.levelSession);
        (void)scene;
    }
    catch (const std::bad_alloc&)
    {
        caught = true;
    }
    catch (...)
    {
        caught = false;
    }
    g_fail_next_allocation.store(false, std::memory_order_release);

    ctx.Check(caught, "scene allocation failure propagates std::bad_alloc");
    ctx.Check(objects.size() == 1 && objects[0].id == 1,
              "scene allocation failure leaves input objects intact");

    gamecamera12::CameraPublicationState12 state;
    (void)state.BeginLevelSession(input.levelSession);
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 1;
    const auto captured = state.Capture(input);
    if (!captured.camera ||
        state.PublishPending(captured.camera->Token()) !=
            gamecamera12::CameraError12::None ||
        !state.Published())
    {
        ctx.Check(false, "envelope allocation source publication");
        return;
    }

    bool envelopeCaught = false;
    g_fail_next_allocation.store(true, std::memory_order_release);
    try
    {
        auto envelope = gamecamera12::BuildPublishedScene12(
            state, captured.camera->Token(), objects);
        (void)envelope;
    }
    catch (const std::bad_alloc&)
    {
        envelopeCaught = true;
    }
    catch (...)
    {
        envelopeCaught = false;
    }
    g_fail_next_allocation.store(false, std::memory_order_release);

    ctx.Check(envelopeCaught,
              "envelope scene allocation failure propagates std::bad_alloc");
    ctx.Check(objects.size() == 1 && objects[0].id == 1,
              "envelope allocation failure leaves input objects intact");
}

void TestEnvelopeMoveFailure(TestContext& ctx)
{
    auto input = MakeInput();
    auto result = gamecamera12::ValidateCamera12(input);
    if (!result.camera)
    {
        ctx.Check(false, "envelope move OOM fixture validation");
        return;
    }

    std::vector<renderprep12::Object12> objects(64);
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
        objects[i].id = i + 1;
        objects[i].world.m[0][0] = 1.0f;
        objects[i].world.m[1][1] = 1.0f;
        objects[i].world.m[2][2] = 1.0f;
        objects[i].world.m[3][3] = 1.0f;
    }

    gamecamera12::CameraPublicationState12 state;
    (void)state.BeginLevelSession(input.levelSession);
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 1;
    const auto captured = state.Capture(input);
    if (!captured.camera ||
        state.PublishPending(captured.camera->Token()) !=
            gamecamera12::CameraError12::None ||
        !state.Published())
    {
        ctx.Check(false, "envelope move OOM source publication");
        return;
    }

    try
    {
        auto envelope = gamecamera12::BuildPublishedScene12(
            state, captured.camera->Token(), objects);
        bool caught = false;
        g_fail_next_allocation.store(true, std::memory_order_release);
        try
        {
            auto moved = std::move(envelope);
            (void)moved;
        }
        catch (const std::bad_alloc&)
        {
            caught = true;
        }
        catch (...)
        {
            caught = false;
        }
        g_fail_next_allocation.store(false, std::memory_order_release);

        ctx.Check(caught,
                  "envelope move allocation failure reaches catch");
        ctx.Check(envelope.Scene().Objects().size() == objects.size() &&
                  envelope.Scene().Objects().front().id == 1,
                  "envelope move failure leaves source owned objects intact");
    }
    catch (const std::exception& error)
    {
        ctx.Check(false, "envelope move OOM setup", error.what());
    }
}

void TestEnvelopeNoExtraObjectCopy(TestContext& ctx)
{
    auto input = MakeInput();
    auto result = gamecamera12::ValidateCamera12(input);
    if (!result.camera)
    {
        ctx.Check(false, "envelope allocation fixture validation");
        return;
    }

    constexpr std::size_t objectCount = 4096;
    std::vector<renderprep12::Object12> objects(objectCount);
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
        objects[i].id = i + 1;
        objects[i].world.m[0][0] = 1.0f;
        objects[i].world.m[1][1] = 1.0f;
        objects[i].world.m[2][2] = 1.0f;
        objects[i].world.m[3][3] = 1.0f;
    }

    gamecamera12::CameraPublicationState12 state;
    (void)state.BeginLevelSession(input.levelSession);
    input.captureEpoch = state.CurrentEpoch();
    input.captureSequence = 1;
    const auto captured = state.Capture(input);
    if (!captured.camera ||
        state.PublishPending(captured.camera->Token()) !=
            gamecamera12::CameraError12::None ||
        !state.Published())
    {
        ctx.Check(false, "envelope allocation source publication");
        return;
    }

    try
    {
        g_allocation_count.store(0, std::memory_order_relaxed);
        g_allocation_bytes.store(0, std::memory_order_relaxed);
        auto direct = gamecamera12::BuildSceneSnapshot12(
            *state.Published(), objects, input.levelSession);
        const auto directCount =
            g_allocation_count.load(std::memory_order_relaxed);
        const auto directBytes =
            g_allocation_bytes.load(std::memory_order_relaxed);

        g_allocation_count.store(0, std::memory_order_relaxed);
        g_allocation_bytes.store(0, std::memory_order_relaxed);
        auto envelope = gamecamera12::BuildPublishedScene12(
            state, captured.camera->Token(), objects);
        const auto envelopeCount =
            g_allocation_count.load(std::memory_order_relaxed);
        const auto envelopeBytes =
            g_allocation_bytes.load(std::memory_order_relaxed);

        ctx.Check(direct.Objects().size() == objectCount &&
                  envelope.Scene().Objects().size() == objectCount,
                  "large object array copied exactly once per snapshot");
        ctx.Check(envelopeCount == directCount &&
                  envelopeBytes == directBytes,
                  "envelope does not allocate a second object array",
                  "direct=" + std::to_string(directCount) + "/" +
                      std::to_string(directBytes) + " envelope=" +
                      std::to_string(envelopeCount) + "/" +
                      std::to_string(envelopeBytes));
    }
    catch (const std::exception& error)
    {
        ctx.Check(false, "envelope allocation comparison", error.what());
    }
}
#endif

} // namespace

int main()
{
    TestContext ctx;
    std::printf("F4.0-B2.1/B2.2 game camera CPU tests\n");
#if defined(GAMECAMERA_STL_HARDENING_TEST)
    ctx.Check(_MSVC_STL_HARDENING == 1,
              "MSVC STL hardening is enabled");
#endif
    TestGoldenOracle(ctx);
    TestProjectionMetadata(ctx);
    TestMatrixFormsAndDepth(ctx);
    TestCameraPosition(ctx);
    TestViewAndValidation(ctx);
    TestSnapshotIndependence(ctx);
    TestPublicationState(ctx);
    TestSessionUniqueness(ctx);
    TestLifecycleRegression(ctx);
    TestFailedCaptureAndFrameGate(ctx);
#ifdef GAMECAMERA_TESTING
    TestEpochExhaustion(ctx);
#endif
    TestEnvelopeMove(ctx);
    TestSceneBridge(ctx);
#ifdef GAMECAMERA_OOM_TEST
    TestSceneAllocationFailure(ctx);
    TestEnvelopeMoveFailure(ctx);
    TestEnvelopeNoExtraObjectCopy(ctx);
#endif
    std::printf("SUMMARY passed=%d failed=%d\n", ctx.passed, ctx.failed);
    return ctx.failed == 0 ? 0 : 1;
}
