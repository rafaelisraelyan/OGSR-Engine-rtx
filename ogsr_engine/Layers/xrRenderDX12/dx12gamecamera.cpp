#include "dx12gamecamera.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gamecamera12
{
namespace
{
constexpr double kMatrixZeroTolerance = 2.0e-6;
constexpr double kViewOrthoTolerance = 2.0e-5;
constexpr double kProjectionTolerance = 2.0e-6;
constexpr double kFullTransformAbsTolerance = 1.0e-5;
constexpr double kFullTransformRelTolerance = 1.0e-5;
constexpr double kFarConditioningEpsilon = 1.0e-6;
// 1 mm base plus two float ULPs of the world-position magnitude. This
// accepts normal float construction at Jupiter scale but still rejects a
// 1 m mismatch at 1e6 m coordinates.
constexpr double kPositionAbsTolerance = 1.0e-3;
constexpr double kPositionUlpScale = 2.0;

bool Finite(double value) noexcept
{
    return std::isfinite(value);
}

bool Finite(const DirectX::XMFLOAT4X4& matrix) noexcept
{
    for (const auto& row : matrix.m)
        for (float value : row)
            if (!std::isfinite(value))
                return false;
    return true;
}

bool Finite(const DirectX::XMFLOAT3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool AlmostZero(double value, double tolerance) noexcept
{
    return std::fabs(value) <= tolerance;
}

bool AlmostEqual(double a, double b) noexcept
{
    const double diff = std::fabs(a - b);
    const double scale = std::max({1.0, std::fabs(a), std::fabs(b)});
    return diff <= kFullTransformAbsTolerance ||
           diff <= kFullTransformRelTolerance * scale;
}

bool AlmostEqualMatrix(const DirectX::XMFLOAT4X4& a,
                       const DirectX::XMFLOAT4X4& b) noexcept
{
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t column = 0; column < 4; ++column)
            if (!AlmostEqual(a.m[row][column], b.m[row][column]))
                return false;
    return true;
}

DirectX::XMFLOAT4X4 Multiply(const DirectX::XMFLOAT4X4& a,
                             const DirectX::XMFLOAT4X4& b) noexcept
{
    DirectX::XMFLOAT4X4 result{};
    DirectX::XMStoreFloat4x4(
        &result,
        DirectX::XMMatrixMultiply(DirectX::XMLoadFloat4x4(&a),
                                  DirectX::XMLoadFloat4x4(&b)));
    return result;
}

CameraCaptureResult12 Error(CameraError12 error) noexcept
{
    CameraCaptureResult12 result;
    result.error = error;
    result.camera.reset();
    return result;
}

bool ValidateView(const DirectX::XMFLOAT4X4& view) noexcept
{
    if (!Finite(view))
        return false;

    // Exact structural contract required by RenderSceneSnapshot12.
    if (view.m[0][3] != 0.0f || view.m[1][3] != 0.0f ||
        view.m[2][3] != 0.0f || view.m[3][3] != 1.0f)
        return false;

    double rows[3][3] = {};
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 3; ++column)
            rows[row][column] = view.m[row][column];

    for (uint32_t row = 0; row < 3; ++row)
    {
        double lengthSq = 0.0;
        for (uint32_t column = 0; column < 3; ++column)
            lengthSq += rows[row][column] * rows[row][column];
        if (!Finite(lengthSq) ||
            std::fabs(lengthSq - 1.0) > kViewOrthoTolerance)
            return false;
    }

    for (uint32_t a = 0; a < 3; ++a)
        for (uint32_t b = a + 1; b < 3; ++b)
        {
            double dot = 0.0;
            for (uint32_t column = 0; column < 3; ++column)
                dot += rows[a][column] * rows[b][column];
            if (std::fabs(dot) > kViewOrthoTolerance)
                return false;
        }

    const double determinant =
        rows[0][0] * (rows[1][1] * rows[2][2] - rows[1][2] * rows[2][1]) -
        rows[0][1] * (rows[1][0] * rows[2][2] - rows[1][2] * rows[2][0]) +
        rows[0][2] * (rows[1][0] * rows[2][1] - rows[1][1] * rows[2][0]);
    return Finite(determinant) &&
           std::fabs(determinant - 1.0) <= kViewOrthoTolerance;
}

bool ValidatePosition(const DirectX::XMFLOAT4X4& view,
                      const DirectX::XMFLOAT3& position) noexcept
{
    if (!Finite(position))
        return false;

    // Row-vector view must map the supplied world position to camera-space
    // origin. Compute in double to catch a 1 m mismatch even at large
    // Jupiter-scale coordinates, while allowing normal float construction.
    const double x = position.x;
    const double y = position.y;
    const double z = position.z;
    const double cx = x * view.m[0][0] + y * view.m[1][0] +
                      z * view.m[2][0] + view.m[3][0];
    const double cy = x * view.m[0][1] + y * view.m[1][1] +
                      z * view.m[2][1] + view.m[3][1];
    const double cz = x * view.m[0][2] + y * view.m[1][2] +
                      z * view.m[2][2] + view.m[3][2];
    const double cw = x * view.m[0][3] + y * view.m[1][3] +
                      z * view.m[2][3] + view.m[3][3];
    if (!Finite(cx) || !Finite(cy) || !Finite(cz) || !Finite(cw))
        return false;

    const double residual = std::max(
        std::max(std::fabs(cx), std::fabs(cy)),
        std::max(std::fabs(cz), std::fabs(cw - 1.0)));
    const double scale = std::max(
        {1.0, std::fabs(x), std::fabs(y), std::fabs(z)});
    const double tolerance =
        kPositionAbsTolerance +
        kPositionUlpScale * std::numeric_limits<float>::epsilon() * scale;
    return residual <= tolerance;
}

bool ValidateProjection(const DirectX::XMFLOAT4X4& projection,
                        ProjectionMetadata12& metadata,
                        CameraError12& error) noexcept
{
    if (!Finite(projection))
    {
        error = CameraError12::InvalidProjection;
        return false;
    }

    // Supported X-Ray/D3D row-vector perspective form:
    //   [ p11 0 0 0; 0 p22 0 0; 0 0 p33 1; 0 0 p43 0 ]
    const uint32_t zeroElements[][2] = {
        {0, 1}, {0, 2}, {0, 3},
        {1, 0}, {1, 2}, {1, 3},
        {2, 0}, {2, 1},
        {3, 0}, {3, 1}};
    for (const auto& element : zeroElements)
        if (!AlmostZero(projection.m[element[0]][element[1]],
                        kProjectionTolerance))
        {
            error = CameraError12::UnsupportedProjection;
            return false;
        }

    const double p11 = projection.m[0][0];
    const double p22 = projection.m[1][1];
    const double p33 = projection.m[2][2];
    const double p34 = projection.m[2][3];
    const double p43 = projection.m[3][2];
    const double p44 = projection.m[3][3];

    if (!(p11 > 0.0) || !(p22 > 0.0) || !(p33 > 0.0) ||
        !(p43 < 0.0) || !AlmostZero(p34 - 1.0, kProjectionTolerance) ||
        !AlmostZero(p44, kProjectionTolerance))
    {
        error = CameraError12::UnsupportedProjection;
        return false;
    }

    const double effectiveInverseAspect = p11 / p22;
    const double fovY = 2.0 * std::atan(1.0 / p22);
    const double nearZ = -p43 / p33;
    if (!Finite(effectiveInverseAspect) || !Finite(fovY) ||
        !Finite(nearZ) || !(effectiveInverseAspect > 0.0) ||
        !(fovY > 0.0) || !(fovY < DirectX::XM_PI) ||
        !(nearZ > 0.0))
    {
        error = CameraError12::InvalidProjection;
        return false;
    }

    metadata = {};
    metadata.effectiveInverseAspect = effectiveInverseAspect;
    metadata.fovYRadians = fovY;
    metadata.nearZ = nearZ;
    metadata.nearStatus = ProjectionStatus12::Known;
    metadata.farStatus = ProjectionStatus12::Unavailable;

    // P33 < 1 is not a supported perspective form. P33 == 1 is the possible
    // infinite-far form. For P33 just above one, far extraction is
    // ill-conditioned; leave far unavailable instead of inventing a value.
    if (p33 < 1.0)
    {
        error = CameraError12::UnsupportedProjection;
        return false;
    }
    if (p33 == 1.0 || p33 - 1.0 <= kFarConditioningEpsilon)
        return true;

    const double farZ = -p43 / (p33 - 1.0);
    if (!Finite(farZ) || !(farZ > nearZ))
    {
        error = CameraError12::InvalidProjection;
        return false;
    }
    metadata.farZ = farZ;
    metadata.farStatus = ProjectionStatus12::Known;
    return true;
}

bool ValidateSourceMetadata(const CameraSourceMetadata12& metadata) noexcept
{
    if (metadata.hasFovY &&
        (!Finite(metadata.fovYRadians) || !(metadata.fovYRadians > 0.0) ||
         !(metadata.fovYRadians < DirectX::XM_PI)))
        return false;
    if (metadata.hasAspectScale &&
        (!Finite(metadata.aspectScale) || !(metadata.aspectScale > 0.0)))
        return false;
    if (metadata.hasNearZ &&
        (!Finite(metadata.nearZ) || !(metadata.nearZ > 0.0)))
        return false;
    if (metadata.hasFarZ &&
        (!Finite(metadata.farZ) || !(metadata.farZ > 0.0)))
        return false;
    if (metadata.hasNearZ && metadata.hasFarZ &&
        !(metadata.farZ > metadata.nearZ))
        return false;
    return true;
}
} // namespace

CameraCaptureResult12 ValidateCamera12(const CameraCaptureInput12& input) noexcept
{
    if (input.levelSession == 0)
        return Error(CameraError12::InvalidLevelSession);
    if (input.captureSequence == 0)
        return Error(CameraError12::InvalidCaptureSequence);
    if (input.captureEpoch == 0)
        return Error(CameraError12::InvalidCaptureEpoch);
    if (input.viewportWidth == 0 || input.viewportHeight == 0)
        return Error(CameraError12::InvalidViewport);
    if (!Finite(input.position))
        return Error(CameraError12::InvalidPosition);
    if (!ValidateSourceMetadata(input.sourceMetadata))
        return Error(CameraError12::InvalidCameraMetadata);
    if (!ValidateView(input.view))
        return Error(CameraError12::InvalidView);
    if (!ValidatePosition(input.view, input.position))
        return Error(CameraError12::InvalidPosition);

    ProjectionMetadata12 projectionMetadata{};
    CameraError12 projectionError = CameraError12::None;
    if (!ValidateProjection(input.projection, projectionMetadata, projectionError))
        return Error(projectionError);

    const DirectX::XMFLOAT4X4 viewProjection =
        Multiply(input.view, input.projection);
    if (!Finite(viewProjection))
        return Error(CameraError12::InvalidProjection);

    if (input.hasFullTransform)
    {
        if (!Finite(input.fullTransform) ||
            !AlmostEqualMatrix(viewProjection, input.fullTransform))
            return Error(CameraError12::InconsistentFullTransform);
    }

    CaptureToken12 token{};
    token.sourceGameFrame = input.sourceGameFrame;
    token.captureSequence = input.captureSequence;
    token.levelSession = input.levelSession;
    token.captureEpoch = input.captureEpoch;

    GameCameraSnapshot12 snapshot(
        token, input.view, input.projection, viewProjection,
        input.position, input.viewportWidth, input.viewportHeight,
        projectionMetadata);

    CameraCaptureResult12 result;
    result.error = CameraError12::None;
    result.camera = std::move(snapshot);
    return result;
}

renderprep12::RenderSceneSnapshot12 BuildSceneSnapshot12(
    const GameCameraSnapshot12& camera,
    std::span<const renderprep12::Object12> objects,
    uint64_t expectedLevelSession)
{
    if (expectedLevelSession == 0 ||
        camera.LevelSession() != expectedLevelSession)
        throw std::invalid_argument("camera/scene level session mismatch");

    return renderprep12::RenderSceneSnapshot12(
        camera.View(), camera.Projection(), objects, camera.LevelSession());
}

CapturedSceneSnapshot12 BuildPublishedScene12(
    const CameraPublicationState12& state,
    const CaptureToken12& expectedToken,
    std::span<const renderprep12::Object12> objects)
{
    const auto& published = state.Published();
    if (!published)
        throw std::logic_error("no published camera");
    if (published->Token() != expectedToken)
        throw std::logic_error("published camera does not match expected token");
    return CapturedSceneSnapshot12(*published, objects, published->Token());
}

void CameraPublicationState12::InvalidatePublication() noexcept
{
    m_pending.reset();
    m_published.reset();
}

CameraError12 CameraPublicationState12::BeginLevelSession(
    uint64_t levelSession) noexcept
{
    if (levelSession == 0)
        return CameraError12::InvalidLevelSession;
    if (m_state == State12::Shutdown)
        return CameraError12::Shutdown;
    if (m_state == State12::Teardown)
        return CameraError12::Teardown;
    if (m_state != State12::NoLevel)
        return CameraError12::SessionAlreadyActive;
    if (m_sequenceExhausted)
        return CameraError12::CaptureSequenceOverflow;
    if (m_lastLevelSession == std::numeric_limits<uint64_t>::max())
        return CameraError12::LevelSessionOverflow;
    if (levelSession <= m_lastLevelSession)
        return CameraError12::NonMonotonicLevelSession;
    if (m_epoch == std::numeric_limits<uint64_t>::max())
        return CameraError12::CaptureEpochOverflow;

    ++m_epoch;
    m_levelSession = levelSession;
    m_lastLevelSession = levelSession;
    m_state = State12::Live;
    InvalidatePublication();
    // m_lastSequence intentionally survives level changes: the host owns one
    // monotonic capture sequence across the process.
    return CameraError12::None;
}

CameraCaptureResult12 CameraPublicationState12::Capture(
    const CameraCaptureInput12& input) noexcept
{
    // A Capture call is a new frame attempt: it cancels the previous pending
    // camera even if this attempt itself is rejected. Historical Published is
    // deliberately untouched and requires an explicit frame-local token.
    m_pending.reset();
    if (m_state == State12::Shutdown)
        return Error(CameraError12::Shutdown);
    if (m_state == State12::Teardown)
        return Error(CameraError12::Teardown);
    if (m_state != State12::Live)
        return Error(CameraError12::NoLiveSession);
    if (m_epoch == std::numeric_limits<uint64_t>::max())
    {
        InvalidatePublication();
        m_state = State12::NoLevel;
        return Error(CameraError12::CaptureEpochOverflow);
    }
    if (input.levelSession == 0)
        return Error(CameraError12::InvalidLevelSession);
    if (input.levelSession != m_levelSession)
        return Error(CameraError12::StaleLevelSession);
    if (input.captureEpoch == 0)
        return Error(CameraError12::InvalidCaptureEpoch);
    if (input.captureEpoch != m_epoch)
        return Error(CameraError12::StaleCaptureEpoch);
    if (input.captureSequence == 0)
        return Error(CameraError12::InvalidCaptureSequence);
    if (m_sequenceExhausted)
        return Error(CameraError12::CaptureSequenceOverflow);
    if (input.captureSequence <= m_lastSequence)
        return Error(CameraError12::InvalidCaptureSequence);

    CameraCaptureResult12 validated = ValidateCamera12(input);
    if (validated.error != CameraError12::None)
        return validated;

    m_lastSequence = input.captureSequence;
    if (m_lastSequence == std::numeric_limits<uint64_t>::max())
        m_sequenceExhausted = true;
    m_pending = validated.camera;
    return validated;
}

CameraError12 CameraPublicationState12::PublishPending(
    const CaptureToken12& expectedToken) noexcept
{
    if (m_state == State12::Shutdown)
        return CameraError12::Shutdown;
    if (m_state == State12::Teardown)
        return CameraError12::Teardown;
    if (m_state != State12::Live)
        return CameraError12::NoLiveSession;
    if (m_epoch == std::numeric_limits<uint64_t>::max())
    {
        InvalidatePublication();
        m_state = State12::NoLevel;
        return CameraError12::CaptureEpochOverflow;
    }
    if (expectedToken.levelSession != m_levelSession)
        return CameraError12::StaleLevelSession;
    if (expectedToken.captureEpoch != m_epoch)
        return CameraError12::StaleCaptureEpoch;
    if (!m_pending)
        return CameraError12::NoPendingCamera;
    if (expectedToken != m_pending->Token())
        return CameraError12::CameraMismatch;

    m_published = m_pending;
    m_pending.reset();
    return CameraError12::None;
}

CameraError12 CameraPublicationState12::BeginReset() noexcept
{
    if (m_state == State12::Shutdown)
        return CameraError12::Shutdown;
    if (m_state == State12::Teardown)
        return CameraError12::Teardown;
    if (m_state != State12::Live)
        return CameraError12::NoLiveSession;
    if (m_epoch == std::numeric_limits<uint64_t>::max())
    {
        InvalidatePublication();
        m_state = State12::NoLevel;
        return CameraError12::CaptureEpochOverflow;
    }

    ++m_epoch;
    InvalidatePublication();
    return CameraError12::None;
}

CameraError12 CameraPublicationState12::BeginDisable() noexcept
{
    if (m_state == State12::Shutdown)
        return CameraError12::Shutdown;
    if (m_state == State12::Teardown)
        return CameraError12::Teardown;
    if (m_epoch == std::numeric_limits<uint64_t>::max())
    {
        InvalidatePublication();
        m_state = State12::NoLevel;
        return CameraError12::CaptureEpochOverflow;
    }

    ++m_epoch;
    m_state = State12::NoLevel;
    InvalidatePublication();
    return CameraError12::None;
}

CameraError12 CameraPublicationState12::BeginTeardown() noexcept
{
    if (m_state == State12::Shutdown)
        return CameraError12::Shutdown;
    if (m_state == State12::Teardown)
        return CameraError12::Teardown;
    if (m_state != State12::Live)
        return CameraError12::NoLiveSession;
    if (m_epoch == std::numeric_limits<uint64_t>::max())
    {
        InvalidatePublication();
        m_state = State12::Teardown;
        return CameraError12::CaptureEpochOverflow;
    }

    ++m_epoch;
    m_state = State12::Teardown;
    InvalidatePublication();
    return CameraError12::None;
}

CameraError12 CameraPublicationState12::CompleteTeardown() noexcept
{
    if (m_state == State12::Shutdown)
        return CameraError12::Shutdown;
    if (m_state != State12::Teardown)
        return CameraError12::NoLiveSession;

    InvalidatePublication();
    m_state = State12::NoLevel;
    return CameraError12::None;
}

CameraError12 CameraPublicationState12::Shutdown() noexcept
{
    if (m_state == State12::Shutdown)
        return CameraError12::Shutdown;
    if (m_epoch == std::numeric_limits<uint64_t>::max())
    {
        InvalidatePublication();
        m_state = State12::Shutdown;
        return CameraError12::CaptureEpochOverflow;
    }

    ++m_epoch;
    m_state = State12::Shutdown;
    InvalidatePublication();
    return CameraError12::None;
}

bool CameraPublicationState12::RejectStaleWorkerResult(
    const CaptureToken12& token) const noexcept
{
    if (m_state != State12::Live ||
        m_epoch == std::numeric_limits<uint64_t>::max() || !m_published)
        return true;
    // The final successful frame at UINT64_MAX is still a valid worker result
    // until publication is invalidated. sequence exhaustion only blocks new
    // captures and new level sessions.
    if (token.captureEpoch != m_epoch)
        return true;
    return token != m_published->Token();
}
} // namespace gamecamera12
