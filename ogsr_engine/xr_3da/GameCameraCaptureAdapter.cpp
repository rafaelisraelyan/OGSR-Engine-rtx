#include "stdafx.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE

#include "GameCameraCaptureAdapter.h"
#include "GameCameraCaptureDiagnostics.h"
#include "device.h"
#include "igame_level.h"

#include "../Layers/xrRenderDX12/dx12gamecamera.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>

namespace
{
struct B2CameraCaptureState
{
    gamecamera12::CameraPublicationState12 publication;

    std::thread::id owner{};

    bool initialized = false;
    bool enabled = false;
    bool shuttingDown = false;
    bool levelActive = false;
    bool teardownPending = false;

    bool hasPendingToken = false;
    gamecamera12::CaptureToken12 pendingToken{};
    b2camera_diagnostics::MatrixDumpGate dumpGate;

    bool dumpRequested = false;
    bool samplesRequested = false;

    uint64_t nextLevelSession = 1;
    uint64_t currentLevelSession = 0;
    uint64_t nextCaptureSequence = 1;

    char levelName[128]{};

    uint64_t captureAttempts = 0;
    uint64_t validatedCaptures = 0;
    uint64_t publishedCaptures = 0;
    uint64_t validationRejects = 0;
    uint64_t resetInvalidations = 0;
    uint64_t teardownInvalidations = 0;
    uint64_t staleTokens = 0;
    gamecamera12::CameraError12 lastError =
        gamecamera12::CameraError12::None;
};

B2CameraCaptureState& State()
{
    static B2CameraCaptureState state;
    return state;
}

bool OnOwnerThread()
{
    return std::this_thread::get_id() == State().owner;
}

const char* FarStatusName(gamecamera12::ProjectionStatus12 status)
{
    switch (status)
    {
    case gamecamera12::ProjectionStatus12::Known:
        return "known";
    case gamecamera12::ProjectionStatus12::Estimated:
        return "estimated";
    case gamecamera12::ProjectionStatus12::Unavailable:
        return "unavailable";
    }
    return "unknown";
}

void CopyMatrix(const Fmatrix& source, DirectX::XMFLOAT4X4& destination)
{
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t column = 0; column < 4; ++column)
            destination.m[row][column] = source.m[row][column];
}

void CopyLevelName(const char* name)
{
    B2CameraCaptureState& state = State();
    if (name && name[0])
        std::snprintf(state.levelName, sizeof(state.levelName), "%s", name);
    else
        std::snprintf(state.levelName, sizeof(state.levelName), "%s",
                      "unknown");
}

void LogLastError(const char* operation)
{
    B2CameraCaptureState& state = State();
    Msg("[B2.3] %s failed: error=%d session=%llu epoch=%llu", operation,
        static_cast<int>(state.lastError),
        static_cast<unsigned long long>(state.currentLevelSession),
        static_cast<unsigned long long>(state.publication.CurrentEpoch()));
}

void LogCameraSample(const gamecamera12::CameraCaptureInput12& input,
                     const gamecamera12::CaptureToken12& token)
{
    B2CameraCaptureState& state = State();
    Msg("[B2.3] camera sample level='%s' session=%llu epoch=%llu "
        "sequence=%llu frame=%u position=(%.3f,%.3f,%.3f) viewport=%ux%u",
        state.levelName,
        static_cast<unsigned long long>(token.levelSession),
        static_cast<unsigned long long>(token.captureEpoch),
        static_cast<unsigned long long>(token.captureSequence),
        token.sourceGameFrame, input.position.x, input.position.y,
        input.position.z, input.viewportWidth, input.viewportHeight);
    for (uint32_t row = 0; row < 4; ++row)
    {
        Msg("[B2.3] sample view[%u] = %.9g %.9g %.9g %.9g", row,
            input.view.m[row][0], input.view.m[row][1],
            input.view.m[row][2], input.view.m[row][3]);
        Msg("[B2.3] sample proj[%u] = %.9g %.9g %.9g %.9g", row,
            input.projection.m[row][0], input.projection.m[row][1],
            input.projection.m[row][2], input.projection.m[row][3]);
        Msg("[B2.3] sample full[%u] = %.9g %.9g %.9g %.9g", row,
            input.fullTransform.m[row][0], input.fullTransform.m[row][1],
            input.fullTransform.m[row][2], input.fullTransform.m[row][3]);
    }
}

void DumpMatricesOnce(const gamecamera12::CameraCaptureInput12& input,
                      const gamecamera12::CaptureToken12& token)
{
    B2CameraCaptureState& state = State();
    // This marker is emitted only from the one-shot dump path, immediately
    // after successful PublishPending with the exact captured token. It is
    // independent of the throttled "camera published" diagnostic.
    Msg("[B2.3] publish-confirmed-for-dump session=%llu epoch=%llu "
        "sequence=%llu frame=%u",
        static_cast<unsigned long long>(token.levelSession),
        static_cast<unsigned long long>(token.captureEpoch),
        static_cast<unsigned long long>(token.captureSequence),
        token.sourceGameFrame);
    Msg("[B2.3] matrix dump level='%s' session=%llu epoch=%llu "
        "sequence=%llu frame=%u",
        state.levelName,
        static_cast<unsigned long long>(token.levelSession),
        static_cast<unsigned long long>(token.captureEpoch),
        static_cast<unsigned long long>(token.captureSequence),
        token.sourceGameFrame);
    for (uint32_t row = 0; row < 4; ++row)
    {
        Msg("[B2.3] view[%u] = %.9g %.9g %.9g %.9g", row,
            input.view.m[row][0], input.view.m[row][1],
            input.view.m[row][2], input.view.m[row][3]);
        Msg("[B2.3] proj[%u] = %.9g %.9g %.9g %.9g", row,
            input.projection.m[row][0], input.projection.m[row][1],
            input.projection.m[row][2], input.projection.m[row][3]);
        Msg("[B2.3] full[%u] = %.9g %.9g %.9g %.9g", row,
            input.fullTransform.m[row][0], input.fullTransform.m[row][1],
            input.fullTransform.m[row][2], input.fullTransform.m[row][3]);
    }
}
} // namespace

void B2CameraCapture_Initialize()
{
    B2CameraCaptureState& state = State();
    if (state.initialized)
        return;

    state.initialized = true;
    state.owner = std::this_thread::get_id();

    const char* params = Core.Params;
    state.dumpRequested =
        b2camera_diagnostics::HasExactArgument(params, "-b2_camera_dump");
    state.samplesRequested =
        b2camera_diagnostics::HasExactArgument(params, "-b2_camera_samples");
    state.enabled =
        b2camera_diagnostics::HasExactArgument(params, "-b2_camera_capture");

    if (state.enabled)
        Msg("[B2.3] CPU camera capture enabled (startup opt-in)");
}

void B2CameraCapture_OnLevelReady(const char* levelName)
{
    B2CameraCaptureState& state = State();
    if (!state.initialized || !state.enabled || state.shuttingDown)
        return;
    if (!OnOwnerThread())
    {
        Msg("[B2.3] OnLevelReady ignored: not owner thread");
        return;
    }
    if (state.levelActive || state.teardownPending)
    {
        Msg("[B2.3] OnLevelReady ignored: session already active");
        return;
    }
    if (state.nextLevelSession == 0)
    {
        state.lastError = gamecamera12::CameraError12::LevelSessionOverflow;
        LogLastError("OnLevelReady");
        return;
    }

    const uint64_t session = state.nextLevelSession;
    const gamecamera12::CameraError12 error =
        state.publication.BeginLevelSession(session);
    if (error != gamecamera12::CameraError12::None)
    {
        state.lastError = error;
        LogLastError("BeginLevelSession");
        return;
    }

    state.nextLevelSession =
        (session == std::numeric_limits<uint64_t>::max()) ? 0 : session + 1;
    state.currentLevelSession = session;
    state.levelActive = true;
    state.teardownPending = false;
    state.hasPendingToken = false;
    state.dumpGate.Reset(state.dumpRequested);
    CopyLevelName(levelName);
    state.lastError = gamecamera12::CameraError12::None;

    Msg("[B2.3] level ready level='%s' session=%llu epoch=%llu",
        state.levelName,
        static_cast<unsigned long long>(state.currentLevelSession),
        static_cast<unsigned long long>(state.publication.CurrentEpoch()));
}

void B2CameraCapture_OnLevelTeardownBegin()
{
    B2CameraCaptureState& state = State();
    if (!state.initialized || !state.enabled || state.shuttingDown)
        return;
    if (!OnOwnerThread())
    {
        Msg("[B2.3] OnLevelTeardownBegin ignored: not owner thread");
        return;
    }

    state.hasPendingToken = false;
    state.dumpGate.ClearPending();
    state.levelActive = false;
    state.teardownPending = true;
    ++state.teardownInvalidations;

    const gamecamera12::CameraError12 error =
        state.publication.BeginTeardown();
    if (error != gamecamera12::CameraError12::None &&
        error != gamecamera12::CameraError12::NoLiveSession &&
        error != gamecamera12::CameraError12::Teardown)
    {
        state.lastError = error;
        LogLastError("BeginTeardown");
    }
    Msg("[B2.3] teardown begin level='%s' session=%llu epoch=%llu",
        state.levelName,
        static_cast<unsigned long long>(state.currentLevelSession),
        static_cast<unsigned long long>(state.publication.CurrentEpoch()));
}

void B2CameraCapture_OnLevelTeardownComplete()
{
    B2CameraCaptureState& state = State();
    if (!state.initialized || !state.enabled || state.shuttingDown)
        return;
    if (!OnOwnerThread())
    {
        Msg("[B2.3] OnLevelTeardownComplete ignored: not owner thread");
        return;
    }

    const gamecamera12::CameraError12 error =
        state.publication.CompleteTeardown();
    state.hasPendingToken = false;
    state.dumpGate.ClearPending();
    state.levelActive = false;
    state.teardownPending = false;
    state.currentLevelSession = 0;
    if (error != gamecamera12::CameraError12::None &&
        error != gamecamera12::CameraError12::NoLiveSession)
    {
        state.lastError = error;
        LogLastError("CompleteTeardown");
    }
    Msg("[B2.3] teardown complete epoch=%llu",
        static_cast<unsigned long long>(state.publication.CurrentEpoch()));
}

void B2CameraCapture_OnCameraFrameReady()
{
    B2CameraCaptureState& state = State();
    if (!state.initialized || !state.enabled || state.shuttingDown)
        return;
    if (!OnOwnerThread())
        return;
    if (!state.levelActive || state.teardownPending)
        return;
    if (!Device.b_is_Ready || !Device.b_is_Active ||
        Device.dwPrecacheFrame != 0 || !g_loading_events.empty())
        return;
    if (!g_pGameLevel || !g_pGameLevel->bReady)
        return;
    if (Device.dwWidth == 0 || Device.dwHeight == 0)
        return;
    if (state.nextCaptureSequence == 0)
    {
        if ((state.captureAttempts % 120) == 0)
            Msg("[B2.3] capture sequence exhausted");
        return;
    }

    gamecamera12::CameraCaptureInput12 input{};
    CopyMatrix(Device.mView, input.view);
    CopyMatrix(Device.mProject, input.projection);
    CopyMatrix(Device.mFullTransform, input.fullTransform);
    input.hasFullTransform = true;
    input.position = {Device.vCameraPosition.x, Device.vCameraPosition.y,
                      Device.vCameraPosition.z};
    input.sourceGameFrame = Device.dwFrame;
    input.viewportWidth = Device.dwWidth;
    input.viewportHeight = Device.dwHeight;
    input.levelSession = state.currentLevelSession;
    input.captureSequence = state.nextCaptureSequence;
    input.captureEpoch = state.publication.CurrentEpoch();

    state.nextCaptureSequence =
        (state.nextCaptureSequence == std::numeric_limits<uint64_t>::max())
            ? 0
            : state.nextCaptureSequence + 1;
    ++state.captureAttempts;

    const gamecamera12::CameraCaptureResult12 result =
        state.publication.Capture(input);
    if (result.error != gamecamera12::CameraError12::None ||
        !result.camera)
    {
        state.hasPendingToken = false;
        state.dumpGate.ClearPending();
        state.lastError =
            b2camera_diagnostics::SelectCaptureError(result);
        ++state.validationRejects;
        if (state.validationRejects == 1 ||
            (state.validationRejects % 60) == 0)
        {
            Msg("[B2.3] capture rejected level='%s' frame=%u error=%d "
                "position=(%.3f,%.3f,%.3f)",
                state.levelName, input.sourceGameFrame,
                static_cast<int>(state.lastError),
                input.position.x, input.position.y, input.position.z);
        }
        return;
    }

    state.pendingToken = result.camera->Token();
    state.dumpGate.OnCapture(input, state.pendingToken);
    state.hasPendingToken = true;
    state.lastError = gamecamera12::CameraError12::None;
    ++state.validatedCaptures;
}

void B2CameraCapture_OnFrameRenderBeginSucceeded()
{
    B2CameraCaptureState& state = State();
    if (!state.initialized || !state.enabled || state.shuttingDown)
        return;
    if (!OnOwnerThread())
        return;
    if (!state.levelActive || !state.hasPendingToken)
        return;

    const bool current =
        state.pendingToken.sourceGameFrame == Device.dwFrame &&
        state.pendingToken.levelSession == state.currentLevelSession &&
        state.pendingToken.captureEpoch == state.publication.CurrentEpoch();
    if (!current)
    {
        state.hasPendingToken = false;
        state.dumpGate.ClearPending();
        state.lastError = gamecamera12::CameraError12::StaleCaptureEpoch;
        ++state.staleTokens;
        if ((state.staleTokens % 60) == 1)
            Msg("[B2.3] stale capture rejected before publish session=%llu "
                "frame=%u",
                static_cast<unsigned long long>(state.currentLevelSession),
                Device.dwFrame);
        return;
    }

    const gamecamera12::CaptureToken12 token = state.pendingToken;
    state.hasPendingToken = false;
    const gamecamera12::CameraError12 error =
        state.publication.PublishPending(token);
    if (error != gamecamera12::CameraError12::None)
    {
        state.dumpGate.ClearPending();
        state.lastError = error;
        if (error == gamecamera12::CameraError12::StaleCaptureEpoch ||
            error == gamecamera12::CameraError12::StaleLevelSession)
            ++state.staleTokens;
        if ((state.validationRejects + state.staleTokens) % 60 == 0)
            LogLastError("PublishPending");
        return;
    }

    state.lastError = gamecamera12::CameraError12::None;
    ++state.publishedCaptures;
    if (state.dumpGate.OnPublished(token))
        DumpMatricesOnce(state.dumpGate.Input(), token);
    if (!state.publication.Published())
        return;
    if (state.samplesRequested && (state.publishedCaptures % 300) == 0)
        LogCameraSample(state.dumpGate.Input(), token);
    const auto& metadata =
        state.publication.Published()->ProjectionMetadata();
    if (state.publishedCaptures == 1 ||
        (state.publishedCaptures % 300) == 0)
    {
        Msg("[B2.3] camera published level='%s' session=%llu epoch=%llu "
            "sequence=%llu frame=%u position=(%.3f,%.3f,%.3f) viewport=%ux%u "
            "fov=%.6f aspect=%.6f near=%.6f farStatus=%s far=%.6f",
            state.levelName,
            static_cast<unsigned long long>(token.levelSession),
            static_cast<unsigned long long>(token.captureEpoch),
            static_cast<unsigned long long>(token.captureSequence),
            token.sourceGameFrame, Device.vCameraPosition.x,
            Device.vCameraPosition.y, Device.vCameraPosition.z, Device.dwWidth,
            Device.dwHeight, metadata.fovYRadians,
            metadata.effectiveInverseAspect, metadata.nearZ,
            FarStatusName(metadata.farStatus), metadata.farZ);
    }
}

void B2CameraCapture_OnFrameRenderBeginFailed()
{
    B2CameraCaptureState& state = State();
    if (!state.initialized || !state.enabled || state.shuttingDown)
        return;
    if (!OnOwnerThread())
    {
        Msg("[B2.3] BeginFailed ignored: not owner thread");
        return;
    }
    state.hasPendingToken = false;
    state.dumpGate.ClearPending();
}

void B2CameraCapture_OnDeviceResetBegin()
{
    B2CameraCaptureState& state = State();
    if (!state.initialized || !state.enabled || state.shuttingDown)
        return;
    if (!OnOwnerThread())
    {
        Msg("[B2.3] reset ignored: not owner thread");
        return;
    }

    state.hasPendingToken = false;
    state.dumpGate.ClearPending();
    ++state.resetInvalidations;
    if (!state.levelActive)
        return;

    const gamecamera12::CameraError12 error =
        state.publication.BeginReset();
    if (error != gamecamera12::CameraError12::None)
    {
        state.lastError = error;
        if (error == gamecamera12::CameraError12::CaptureEpochOverflow ||
            error == gamecamera12::CameraError12::NoLiveSession)
            state.levelActive = false;
        LogLastError("BeginReset");
    }
    Msg("[B2.3] device reset invalidated camera session=%llu epoch=%llu",
        static_cast<unsigned long long>(state.currentLevelSession),
        static_cast<unsigned long long>(state.publication.CurrentEpoch()));
}

void B2CameraCapture_OnEngineShutdown()
{
    B2CameraCaptureState& state = State();
    if (!state.initialized || !state.enabled || state.shuttingDown)
        return;
    if (!OnOwnerThread())
        return;

    state.hasPendingToken = false;
    state.dumpGate.ClearPending();
    state.levelActive = false;
    state.teardownPending = false;
    state.shuttingDown = true;
    const gamecamera12::CameraError12 error = state.publication.Shutdown();
    Msg("[B2.3] engine shutdown state=%d attempts=%llu validated=%llu "
        "published=%llu rejects=%llu reset=%llu teardown=%llu stale=%llu",
        static_cast<int>(error),
        static_cast<unsigned long long>(state.captureAttempts),
        static_cast<unsigned long long>(state.validatedCaptures),
        static_cast<unsigned long long>(state.publishedCaptures),
        static_cast<unsigned long long>(state.validationRejects),
        static_cast<unsigned long long>(state.resetInvalidations),
        static_cast<unsigned long long>(state.teardownInvalidations),
        static_cast<unsigned long long>(state.staleTokens));
}

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE
