#include "stdafx.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE

#include "GameCameraCaptureAdapter.h"
#include "GameCameraCaptureDiagnostics.h"
#include "device.h"
#include "igame_level.h"

#include "../Layers/xrRenderDX12/dx12gamecamera.h"
#if defined(OGSR_ENABLE_EXPERIMENTAL_DX12_B3_CAMERA_EXPORT) || defined(OGSR_ENABLE_EXPERIMENTAL_DX12_B3_B2_CAMERA_STREAM)
#include "../Layers/xrRenderDX12/dx12cameracapturefile.h"
#endif
#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_B3_B2_CAMERA_STREAM
#include "../Layers/xrRenderDX12/b3b2_camera_stream.h"
#endif

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <limits>
#include <string>
#include <thread>
#include <vector>

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

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_B3_CAMERA_EXPORT
    bool exportRequested = false;
    bool exportAttempted = false;
    wchar_t exportPath[1024]{};
#endif

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_B3_B2_CAMERA_STREAM
    bool streamRequested = false;
    bool streamStarted = false;
    wchar_t streamPipeName[256]{};
    b3b2::CameraStreamProducer streamProducer;
    uint64_t lastLoggedStreamSequence = 0;
    uint64_t streamSentLogCount = 0;
#endif
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

#if defined(OGSR_ENABLE_EXPERIMENTAL_DX12_B3_CAMERA_EXPORT) || defined(OGSR_ENABLE_EXPERIMENTAL_DX12_B3_B2_CAMERA_STREAM)
bool IsParameterSpace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
           value == '\v' || value == '\f';
}
#endif

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_B3_CAMERA_EXPORT
constexpr const char* kB3ExportPrefix = "-b3_camera_export=";
constexpr size_t kB3ExportPrefixLength = 18;

// Exact "-b3_camera_export=<path>" lookup (optionally quoted). Substring
// occurrences and the bare flag are rejected.
bool ExtractB3ExportPath(const char* params, wchar_t* out,
                         size_t outCount) noexcept
{
    if (params == nullptr || out == nullptr || outCount < 2)
        return false;
    const char* cursor = params;
    while (*cursor)
    {
        while (*cursor && IsParameterSpace(*cursor))
            ++cursor;
        if (!*cursor)
            break;
        const char* token = cursor;
        if (*cursor == '"')
        {
            ++cursor;
            token = cursor;
            while (*cursor && *cursor != '"')
                ++cursor;
        }
        else
        {
            while (*cursor && !IsParameterSpace(*cursor))
                ++cursor;
        }
        const size_t length = static_cast<size_t>(cursor - token);
        if (length > kB3ExportPrefixLength &&
            std::strncmp(token, kB3ExportPrefix, kB3ExportPrefixLength) == 0)
        {
            const char* value = token + kB3ExportPrefixLength;
            const size_t valueLength = length - kB3ExportPrefixLength;
            if (valueLength >= outCount)
                return false;
            const int converted = ::MultiByteToWideChar(
                CP_UTF8, 0, value, static_cast<int>(valueLength), out,
                static_cast<int>(outCount) - 1);
            if (converted <= 0)
                return false;
            out[converted] = L'\0';
            return true;
        }
        if (*cursor == '"')
            ++cursor;
    }
    return false;
}

// One-shot transactional export of the exactly-published B2.3 camera.
// Called only after PublishPending succeeded for this frame's token.
void MaybeExportPublishedCamera(B2CameraCaptureState& state,
                                const gamecamera12::CaptureToken12& token)
{
    if (!state.exportRequested || state.exportAttempted)
        return;

    cameracapture12::ExportGateState gate;
    gate.exportRequested = state.exportRequested;
    gate.alreadyExported = state.exportAttempted;
    gate.ownerThread = OnOwnerThread();
    gate.shuttingDown = state.shuttingDown;
    gate.levelActive = state.levelActive;
    gate.teardownPending = state.teardownPending;
    gate.levelNameMatches = state.levelName[0] != '\0';

    const auto& published = state.publication.Published();
    gate.hasPublished = published.has_value();
    gate.publishedTokenMatches =
        published.has_value() && published->Token() == token;

    cameracapture12::CameraCaptureFile12 capture{};
    std::vector<uint8_t> bytes;
    cameracapture12::FileError fileError = cameracapture12::FileError::None;
    if (gate.hasPublished && gate.publishedTokenMatches)
    {
        const gamecamera12::GameCameraSnapshot12& snapshot = *published;
        std::snprintf(capture.levelName, sizeof(capture.levelName), "%s",
                      state.levelName);
        capture.input.view = snapshot.View();
        capture.input.projection = snapshot.Projection();
        capture.input.fullTransform = snapshot.ViewProjection();
        capture.input.hasFullTransform = true;
        capture.input.position = snapshot.Position();
        capture.input.viewportWidth = snapshot.ViewportWidth();
        capture.input.viewportHeight = snapshot.ViewportHeight();
        capture.input.levelSession = token.levelSession;
        capture.input.captureEpoch = token.captureEpoch;
        capture.input.captureSequence = token.captureSequence;
        capture.input.sourceGameFrame = token.sourceGameFrame;
        gate.cameraValid =
            cameracapture12::Encode(capture, bytes, fileError);
        if (!gate.cameraValid)
        {
            Msg("[B3-B1] camera export rejected: codec=%s",
                cameracapture12::FileErrorMessage(fileError));
            return;
        }
    }

    const cameracapture12::ExportDecision decision =
        cameracapture12::EvaluateExportGate(gate);
    if (decision != cameracapture12::ExportDecision::Write)
    {
        Msg("[B3-B1] camera export skipped: %s",
            cameracapture12::ExportDecisionName(decision));
        return;
    }

    state.exportAttempted = true;
    cameracapture12::WriteError writeError = cameracapture12::WriteError::None;
    if (!cameracapture12::WriteFileAtomic(state.exportPath, bytes, writeError))
    {
        Msg("[B3-B1] camera export failed: %s level='%s' session=%llu "
            "epoch=%llu sequence=%llu frame=%u",
            cameracapture12::WriteErrorMessage(writeError), state.levelName,
            static_cast<unsigned long long>(token.levelSession),
            static_cast<unsigned long long>(token.captureEpoch),
            static_cast<unsigned long long>(token.captureSequence),
            token.sourceGameFrame);
        return;
    }

    Msg("[B3-B1] camera export written level='%s' session=%llu epoch=%llu "
        "sequence=%llu frame=%u bytes=%zu path='%ls'",
        state.levelName,
        static_cast<unsigned long long>(token.levelSession),
        static_cast<unsigned long long>(token.captureEpoch),
        static_cast<unsigned long long>(token.captureSequence),
        token.sourceGameFrame, bytes.size(), state.exportPath);
}
#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_B3_CAMERA_EXPORT

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_B3_B2_CAMERA_STREAM
constexpr const char* kStreamPipePrefix = "-b3_camera_stream_pipe=";
constexpr size_t kStreamPipePrefixLength =
    sizeof("-b3_camera_stream_pipe=") - 1;

bool ExtractStreamPipeName(const char* params, wchar_t* out,
                           size_t outCount) noexcept
{
    if (params == nullptr || out == nullptr || outCount < 2)
        return false;
    const char* cursor = params;
    while (*cursor)
    {
        while (*cursor && IsParameterSpace(*cursor))
            ++cursor;
        if (!*cursor)
            break;
        const char* token = cursor;
        if (*cursor == '"')
        {
            ++cursor;
            token = cursor;
            while (*cursor && *cursor != '"')
                ++cursor;
        }
        else
        {
            while (*cursor && !IsParameterSpace(*cursor))
                ++cursor;
        }
        const size_t length = static_cast<size_t>(cursor - token);
        if (length > kStreamPipePrefixLength &&
            std::strncmp(token, kStreamPipePrefix, kStreamPipePrefixLength) == 0)
        {
            const char* value = token + kStreamPipePrefixLength;
            const size_t valueLength = length - kStreamPipePrefixLength;
            if (valueLength >= outCount)
                return false;
            const int converted = ::MultiByteToWideChar(
                CP_UTF8, 0, value, static_cast<int>(valueLength), out,
                static_cast<int>(outCount) - 1);
            if (converted <= 0)
                return false;
            out[converted] = L'\0';
            return true;
        }
        if (*cursor == '"')
            ++cursor;
    }
    return false;
}

void MaybeStreamPublishedCamera(B2CameraCaptureState& state,
                                const gamecamera12::CaptureToken12& token)
{
    if (!state.streamStarted || !state.streamProducer.Running())
        return;
    const auto& published = state.publication.Published();
    if (!published || published->Token() != token)
        return;

    const gamecamera12::GameCameraSnapshot12& snapshot = *published;
    b3b2::CameraPacket packet;
    std::snprintf(packet.capture.levelName,
                  sizeof(packet.capture.levelName), "%s", state.levelName);
    packet.capture.input.view = snapshot.View();
    packet.capture.input.projection = snapshot.Projection();
    packet.capture.input.fullTransform = snapshot.ViewProjection();
    packet.capture.input.hasFullTransform = true;
    packet.capture.input.position = snapshot.Position();
    packet.capture.input.viewportWidth = snapshot.ViewportWidth();
    packet.capture.input.viewportHeight = snapshot.ViewportHeight();
    packet.capture.input.levelSession = token.levelSession;
    packet.capture.input.captureEpoch = token.captureEpoch;
    packet.capture.input.captureSequence = token.captureSequence;
    packet.capture.input.sourceGameFrame = token.sourceGameFrame;
    state.streamProducer.Publish(packet);

    b3b2::SentSample sent;
    if (!state.streamProducer.CopyLastSent(sent))
        return;
    if (sent.packet.capture.input.captureSequence ==
        state.lastLoggedStreamSequence)
        return;
    state.lastLoggedStreamSequence =
        sent.packet.capture.input.captureSequence;
    ++state.streamSentLogCount;
    if (state.streamSentLogCount > 3)
        return;

    const cameracapture12::CameraCaptureFile12& sentCapture =
        sent.packet.capture;
    Msg("[B3-B2] stream sent sample=%llu level='%s' session=%llu epoch=%llu "
        "sequence=%llu frame=%u hash=%016llX position=(%.3f,%.3f,%.3f) "
        "viewport=%ux%u",
        static_cast<unsigned long long>(state.streamSentLogCount),
        sentCapture.levelName,
        static_cast<unsigned long long>(sentCapture.input.levelSession),
        static_cast<unsigned long long>(sentCapture.input.captureEpoch),
        static_cast<unsigned long long>(sentCapture.input.captureSequence),
        sentCapture.input.sourceGameFrame,
        static_cast<unsigned long long>(sent.hash),
        sentCapture.input.position.x,
        sentCapture.input.position.y,
        sentCapture.input.position.z,
        sentCapture.input.viewportWidth,
        sentCapture.input.viewportHeight);
    for (uint32_t row = 0; row < 4; ++row)
    {
        Msg("[B3-B2] sent view[%u] = %.9g %.9g %.9g %.9g", row,
            sent.packet.capture.input.view.m[row][0],
            sent.packet.capture.input.view.m[row][1],
            sent.packet.capture.input.view.m[row][2],
            sent.packet.capture.input.view.m[row][3]);
        Msg("[B3-B2] sent proj[%u] = %.9g %.9g %.9g %.9g", row,
            sent.packet.capture.input.projection.m[row][0],
            sent.packet.capture.input.projection.m[row][1],
            sent.packet.capture.input.projection.m[row][2],
            sent.packet.capture.input.projection.m[row][3]);
        Msg("[B3-B2] sent full[%u] = %.9g %.9g %.9g %.9g", row,
            sent.packet.capture.input.fullTransform.m[row][0],
            sent.packet.capture.input.fullTransform.m[row][1],
            sent.packet.capture.input.fullTransform.m[row][2],
            sent.packet.capture.input.fullTransform.m[row][3]);
    }
}

void LogStreamStats(B2CameraCaptureState& state, const char* phase)
{
    const b3b2::StreamCountersSnapshot counters =
        state.streamProducer.Counters();
    const b3b2::EnqueueStats latency =
        state.streamProducer.EnqueueLatency();
    Msg("[B3-B2] stream stats phase=%s published=%llu enqueued=%llu "
        "overwritten=%llu lock_dropped=%llu encoded=%llu sent=%llu "
        "rejected=%llu disconnected=%llu reconnected=%llu "
        "enqueue_ns_median=%llu p95=%llu p99=%llu max=%llu",
        phase,
        static_cast<unsigned long long>(counters.published),
        static_cast<unsigned long long>(counters.enqueued),
        static_cast<unsigned long long>(counters.overwritten),
        static_cast<unsigned long long>(counters.lockDropped),
        static_cast<unsigned long long>(counters.encoded),
        static_cast<unsigned long long>(counters.sent),
        static_cast<unsigned long long>(counters.rejected),
        static_cast<unsigned long long>(counters.disconnected),
        static_cast<unsigned long long>(counters.reconnected),
        static_cast<unsigned long long>(latency.medianNs),
        static_cast<unsigned long long>(latency.p95Ns),
        static_cast<unsigned long long>(latency.p99Ns),
        static_cast<unsigned long long>(latency.maxNs));
}
#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_B3_B2_CAMERA_STREAM
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

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_B3_CAMERA_EXPORT
    state.exportRequested = ExtractB3ExportPath(
        params, state.exportPath, std::size(state.exportPath));
    if (state.exportRequested)
        Msg("[B3-B1] one-shot camera export enabled");
    else if (b2camera_diagnostics::HasExactArgument(params, "-b3_camera_export"))
        Msg("[B3-B1] camera export ignored: expected -b3_camera_export=<path>");
#endif

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_B3_B2_CAMERA_STREAM
    state.streamRequested =
        b2camera_diagnostics::HasExactArgument(params, "-b3_camera_stream");
    if (state.streamRequested && !state.enabled)
    {
        Msg("[B3-B2] camera stream ignored: requires -b2_camera_capture");
    }
    else if (state.streamRequested)
    {
        std::wstring pipeName;
        if (!ExtractStreamPipeName(params, state.streamPipeName,
                                   std::size(state.streamPipeName)))
        {
            pipeName = b3b2::CameraStreamProducer::MakePipeName();
            wcsncpy_s(state.streamPipeName, pipeName.c_str(), _TRUNCATE);
        }
        else
        {
            pipeName = state.streamPipeName;
        }

        std::string error;
        if (state.streamProducer.Start(pipeName, error))
        {
            state.streamStarted = true;
            Msg("[B3-B2] camera stream listening pipe='%ls' local_only=1 "
                "remote_clients=rejected",
                state.streamPipeName);
        }
        else
        {
            Msg("[B3-B2] camera stream disabled: %s", error.c_str());
        }
    }
#endif

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
#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_B3_B2_CAMERA_STREAM
    if (state.streamStarted)
        state.streamProducer.OnLevelReady(
            session, state.publication.CurrentEpoch());
#endif

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
#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_B3_B2_CAMERA_STREAM
    if (state.streamStarted)
        state.streamProducer.Invalidate(
            state.currentLevelSession, state.publication.CurrentEpoch(),
            b3b2::InvalidateReason::Teardown);
#endif
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
#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_B3_B2_CAMERA_STREAM
    MaybeStreamPublishedCamera(state, token);
    if (state.streamStarted && (state.publishedCaptures % 300) == 0)
        LogStreamStats(state, "periodic");
#endif
#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_B3_CAMERA_EXPORT
    MaybeExportPublishedCamera(state, token);
#endif
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
#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_B3_B2_CAMERA_STREAM
    if (state.streamStarted)
        state.streamProducer.Invalidate(
            state.currentLevelSession, state.publication.CurrentEpoch(),
            b3b2::InvalidateReason::Reset);
#endif
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
#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_B3_B2_CAMERA_STREAM
    if (state.streamStarted)
    {
        LogStreamStats(state, "shutdown");
        state.streamProducer.Stop();
        state.streamStarted = false;
    }
#endif
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
