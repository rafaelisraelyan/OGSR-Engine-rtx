#pragma once

// F4.0-B2.1/B2.2: isolated CPU-only game camera snapshot.
//
// This layer consumes copies of the X-Ray logical View/Projection matrices.
// It never reads Device, CLevel, CCameraManager or any other engine state.
// The output is immutable and can be handed to CPU workers by value.

#include <cstdint>
#include <optional>
#include <span>

#include <DirectXMath.h>

#include "dx12renderprep.h"

namespace gamecamera12
{
enum class CameraError12 : uint8_t
{
    None = 0,
    InvalidLevelSession,
    InvalidCaptureSequence,
    CaptureSequenceOverflow,
    InvalidCaptureEpoch,
    CaptureEpochOverflow,
    SessionAlreadyActive,
    NonMonotonicLevelSession,
    LevelSessionOverflow,
    InvalidView,
    InvalidProjection,
    UnsupportedProjection,
    InvalidViewport,
    InvalidPosition,
    InvalidCameraMetadata,
    InconsistentFullTransform,
    NoLiveSession,
    Teardown,
    Shutdown,
    StaleLevelSession,
    StaleCaptureEpoch,
    NoPendingCamera,
    CameraMismatch,
    InvalidCamera
};

// Far-plane recovery confidence. Known is used only when the stored matrix
// conditions the value well enough; Estimated is reserved for a future
// non-authoritative estimate; Unavailable means no value is invented.
enum class ProjectionStatus12 : uint8_t
{
    Known = 0,
    Estimated,
    Unavailable
};

struct ProjectionMetadata12
{
    double effectiveInverseAspect = 0.0;
    double fovYRadians = 0.0;
    double nearZ = 0.0;
    double farZ = 0.0;
    ProjectionStatus12 nearStatus = ProjectionStatus12::Unavailable;
    ProjectionStatus12 farStatus = ProjectionStatus12::Unavailable;
};

// Advisory copies of game-published camera fields. They are validated for
// finite/positive ranges but are never authoritative: Projection() and the
// projection-derived ProjectionMetadata12 remain the source of truth.
struct CameraSourceMetadata12
{
    bool hasFovY = false;
    double fovYRadians = 0.0;
    bool hasAspectScale = false;
    double aspectScale = 0.0;
    bool hasNearZ = false;
    double nearZ = 0.0;
    bool hasFarZ = false;
    double farZ = 0.0;
};

struct CameraCaptureInput12
{
    DirectX::XMFLOAT4X4 view{};
    DirectX::XMFLOAT4X4 projection{};
    bool hasFullTransform = false;
    DirectX::XMFLOAT4X4 fullTransform{};
    DirectX::XMFLOAT3 position{};

    // Device.dwFrame copy. It is intentionally not treated as monotonic.
    uint32_t sourceGameFrame = 0;
    uint64_t captureSequence = 0;
    uint64_t levelSession = 0;
    uint64_t captureEpoch = 0;

    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    CameraSourceMetadata12 sourceMetadata{};
};

struct CaptureToken12
{
    uint32_t sourceGameFrame = 0;
    uint64_t captureSequence = 0;
    uint64_t levelSession = 0;
    uint64_t captureEpoch = 0;

    bool operator==(const CaptureToken12&) const = default;
    bool operator!=(const CaptureToken12&) const = default;
};

struct CameraCaptureResult12;
CameraCaptureResult12 ValidateCamera12(const CameraCaptureInput12& input) noexcept;

class CapturedSceneSnapshot12;
class CameraPublicationState12;

class GameCameraSnapshot12 final
{
public:
    GameCameraSnapshot12() = delete;
    GameCameraSnapshot12(const GameCameraSnapshot12&) = default;
    GameCameraSnapshot12(GameCameraSnapshot12&&) noexcept = default;
    GameCameraSnapshot12& operator=(const GameCameraSnapshot12&) = default;
    GameCameraSnapshot12& operator=(GameCameraSnapshot12&&) noexcept = default;
    ~GameCameraSnapshot12() = default;

    const DirectX::XMFLOAT4X4& View() const noexcept { return m_view; }
    const DirectX::XMFLOAT4X4& Projection() const noexcept { return m_projection; }
    const DirectX::XMFLOAT4X4& ViewProjection() const noexcept { return m_viewProjection; }
    const DirectX::XMFLOAT3& Position() const noexcept { return m_position; }
    uint32_t ViewportWidth() const noexcept { return m_viewportWidth; }
    uint32_t ViewportHeight() const noexcept { return m_viewportHeight; }
    const ProjectionMetadata12& ProjectionMetadata() const noexcept { return m_projectionMetadata; }
    const CaptureToken12& Token() const noexcept { return m_token; }
    uint32_t SourceGameFrame() const noexcept { return m_token.sourceGameFrame; }
    uint64_t CaptureSequence() const noexcept { return m_token.captureSequence; }
    uint64_t LevelSession() const noexcept { return m_token.levelSession; }
    uint64_t CaptureEpoch() const noexcept { return m_token.captureEpoch; }

private:
    friend struct CameraCaptureResult12;
    friend CameraCaptureResult12 ValidateCamera12(const CameraCaptureInput12&) noexcept;

    GameCameraSnapshot12(CaptureToken12 token,
                         const DirectX::XMFLOAT4X4& view,
                         const DirectX::XMFLOAT4X4& projection,
                         const DirectX::XMFLOAT4X4& viewProjection,
                         const DirectX::XMFLOAT3& position,
                         uint32_t viewportWidth,
                         uint32_t viewportHeight,
                         const ProjectionMetadata12& projectionMetadata) noexcept
        : m_token(token), m_view(view), m_projection(projection),
          m_viewProjection(viewProjection), m_position(position),
          m_viewportWidth(viewportWidth), m_viewportHeight(viewportHeight),
          m_projectionMetadata(projectionMetadata)
    {
    }

    CaptureToken12 m_token{};
    DirectX::XMFLOAT4X4 m_view{};
    DirectX::XMFLOAT4X4 m_projection{};
    DirectX::XMFLOAT4X4 m_viewProjection{};
    DirectX::XMFLOAT3 m_position{};
    uint32_t m_viewportWidth = 0;
    uint32_t m_viewportHeight = 0;
    ProjectionMetadata12 m_projectionMetadata{};
};

struct CameraCaptureResult12
{
    CameraError12 error = CameraError12::InvalidCamera;
    std::optional<GameCameraSnapshot12> camera;
};

[[nodiscard]]
CameraCaptureResult12 ValidateCamera12(const CameraCaptureInput12& input) noexcept;

// Low-level scene-only conversion from an immutable validated snapshot.
// It carries no publication identity; worker envelopes must use
// BuildPublishedScene12 below.
[[nodiscard]]
renderprep12::RenderSceneSnapshot12 BuildSceneSnapshot12(
    const GameCameraSnapshot12& camera,
    std::span<const renderprep12::Object12> objects,
    uint64_t expectedLevelSession);

// Immutable CPU envelope for workers. RenderSceneSnapshot12 carries the
// level generation but not capture sequence/epoch; the envelope keeps the
// camera token so a reset/teardown can reject an old worker result.
class CapturedSceneSnapshot12 final
{
public:
    CapturedSceneSnapshot12(const CapturedSceneSnapshot12&) = default;
    // RenderSceneSnapshot12 owns a const std::vector<Object12>; its move
    // constructor may copy that vector and throw. Do not claim noexcept.
    CapturedSceneSnapshot12(CapturedSceneSnapshot12&&) = default;
    CapturedSceneSnapshot12& operator=(const CapturedSceneSnapshot12&) = delete;
    CapturedSceneSnapshot12& operator=(CapturedSceneSnapshot12&&) = delete;

    const renderprep12::RenderSceneSnapshot12& Scene() const noexcept { return m_scene; }
    const CaptureToken12& Token() const noexcept { return m_token; }
    uint32_t SourceGameFrame() const noexcept { return m_token.sourceGameFrame; }

private:
    friend CapturedSceneSnapshot12 BuildPublishedScene12(
        const CameraPublicationState12&,
        const CaptureToken12&,
        std::span<const renderprep12::Object12>);

    CapturedSceneSnapshot12(const GameCameraSnapshot12& camera,
                            std::span<const renderprep12::Object12> objects,
                            CaptureToken12 token)
        : m_scene(camera.View(), camera.Projection(), objects,
                  camera.LevelSession()),
          m_token(token)
    {
    }

    renderprep12::RenderSceneSnapshot12 m_scene;
    CaptureToken12 m_token;
};

// Owner-thread, process-lifetime CPU publication model. It is intentionally
// not an Engine hook and is not thread-safe: only the owner thread mutates it.
//
//   NoLevel --BeginLevelSession--> Live --BeginTeardown--> Teardown
//      ^                                                    |
//      +---------------- CompleteTeardown -----------------+
//
// BeginLevelSession is accepted only in NoLevel. Shutdown is terminal. Both
// session IDs and capture epochs are process-monotonic; stale tokens never
// become valid again.
class CameraPublicationState12 final
{
public:
    CameraPublicationState12() noexcept = default;

    // Requires NoLevel and a strictly greater levelSession than every previous
    // session. Epoch/sequence exhaustion is irreversible and rejects restart.
    [[nodiscard]] CameraError12 BeginLevelSession(uint64_t levelSession) noexcept;
    [[nodiscard]] CameraCaptureResult12 Capture(const CameraCaptureInput12& input) noexcept;
    // Publishes exactly the pending snapshot. The token only selects/validates
    // identity; no arbitrary camera object is accepted or published.
    [[nodiscard]] CameraError12 PublishPending(const CaptureToken12& expectedToken) noexcept;
    [[nodiscard]] CameraError12 BeginReset() noexcept;
    [[nodiscard]] CameraError12 BeginDisable() noexcept;
    [[nodiscard]] CameraError12 BeginTeardown() noexcept;
    // Completes an explicit teardown and returns to NoLevel. It never
    // publishes a camera and does not reset session/sequence monotonicity.
    [[nodiscard]] CameraError12 CompleteTeardown() noexcept;
    [[nodiscard]] CameraError12 Shutdown() noexcept;

    [[nodiscard]] bool RejectStaleWorkerResult(const CaptureToken12& token) const noexcept;

    uint64_t CurrentLevelSession() const noexcept { return m_levelSession; }
    uint64_t CurrentEpoch() const noexcept { return m_epoch; }
    bool HasPending() const noexcept { return m_pending.has_value(); }
    bool HasPublished() const noexcept { return m_published.has_value(); }
    const std::optional<GameCameraSnapshot12>& Pending() const noexcept { return m_pending; }
    const std::optional<GameCameraSnapshot12>& Published() const noexcept { return m_published; }

#ifdef GAMECAMERA_TESTING
    // Test-only deterministic epoch setup; not part of the production API.
    void SetEpochForTest(uint64_t epoch) noexcept { m_epoch = epoch; }
#endif

private:
    void InvalidatePublication() noexcept;

    enum class State12 : uint8_t
    {
        NoLevel = 0,
        Live,
        Teardown,
        Shutdown
    };

    State12 m_state = State12::NoLevel;
    uint64_t m_levelSession = 0;
    uint64_t m_lastLevelSession = 0;
    uint64_t m_epoch = 0;
    uint64_t m_lastSequence = 0;
    bool m_sequenceExhausted = false;
    std::optional<GameCameraSnapshot12> m_pending;
    std::optional<GameCameraSnapshot12> m_published;
};

// Builds the worker envelope only from the state's current published camera.
// expectedToken is the successful Capture token for the frame being built; a
// stale published frame is rejected even when it is otherwise valid. Throws
// std::logic_error when nothing is published, or when the published token does
// not exactly equal expectedToken (session, epoch, sequence and frame).
[[nodiscard]]
CapturedSceneSnapshot12 BuildPublishedScene12(
    const CameraPublicationState12& state,
    const CaptureToken12& expectedToken,
    std::span<const renderprep12::Object12> objects);
} // namespace gamecamera12
