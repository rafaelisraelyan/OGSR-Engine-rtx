#pragma once

#include <Windows.h>

#include <cstddef>

namespace clouds
{
// Fixed-layout, pre-created shared-memory witness. The parent test process
// creates and initializes the mapping; the child only performs atomic stores.
// No heap, mutex, file or pipe I/O is used here.
constexpr LONG kWitnessMagic = 0x52435734; // 'RCW4'
constexpr LONG kWitnessVersion = 1;
constexpr size_t kWitnessEventCapacity = 256;

enum class WitnessCode : LONG
{
    None = 0,

    ExecuteCommandLists = 1,
    SignalIssued = 2,
    FenceConfirmed = 3,
    FaultInjected = 4,
    LoggerFaultInjected = 5,
    TerminalAction = 6,
    WaitStarted = 7,
    WaitFailed = 8,
    DeviceRemoved = 9,

    ArmImmediateOwners = 10,
    ReleaseImmediateOwners = 11,
    ArmCloudOutputCandidates = 12,
    ReleaseCloudOutputCandidates = 13,
    ArmProbeCandidate = 14,
    ReleaseProbeCandidate = 15,
    ArmCloudReadback = 16,
    ReleaseCloudReadback = 17,
    ArmPresentFrame = 18,
    ReleasePresentFrame = 19,
    ArmCloudOwners = 20,
    ReleaseCloudOwners = 21,
    ArmRendererOwners = 22,
    ReleaseRendererOwners = 23,
    ArmWeatherMapOwners = 24,
    ReleaseWeatherMapOwners = 25,
    ArmNoiseOwners = 26,
    ReleaseNoiseOwners = 27,
    ArmResizeBackBuffers = 28,
    ReleaseResizeBackBuffers = 29,
    ArmProbeReadback = 30,
    ReleaseProbeReadback = 31,
    ArmRawProbeReadback = 32,
    ReleaseRawProbeReadback = 33,
    ArmDensityReadback = 34,
    ReleaseDensityReadback = 35,
    ArmNoiseResize = 36,
    ReleaseNoiseResize = 37,

    StartupBegin = 40,
    StartupWindow = 41,
    StartupRenderer = 42,
    StartupNoise = 43,
    StartupViewer = 44,
    StartupWeather = 45,
    StartupCloud = 46,
    StartupCompositor = 47,
    StartupProfiler = 48,
    StartupFirstFrame = 49,

    BlockedPipesActivated = 50,
    BlockedPipesFailed = 51,
    BlockedPipesDeactivated = 52,
    PipeProbeStarted = 53,
    PipeProbeNotRequested = 54,

    GpuIdleConfirmed = 60,
    DiagnosticsDeferred = 61,
    DiagnosticsFlushed = 62,
    DiagnosticsUnsafe = 63,
    GpuGateInserted = 64,
    GpuGateReleased = 65,
    InflightCheck = 66,
    FrameSubmitted = 67,
    FrameFenceConfirmed = 68,
    DiagnosticNoticeDeferred = 69,
    DiagnosticDrainDeferred = 70,
    DiagnosticDrainDone = 71,
};

struct WitnessEventEntry
{
    volatile LONG sequence;
    volatile LONG code;
};

struct WitnessBlock
{
    volatile LONG magic;
    volatile LONG version;
    volatile LONG sequence;
    volatile LONG terminalFlag;
    volatile LONG fenceConfirmedCount;
    volatile LONG reserved0;
    // Proof that a real GPU submission happened: the counter only advances
    // immediately after ID3D12CommandQueue::ExecuteCommandLists returned, and
    // the pointer is the live ID3D12GraphicsCommandList that was submitted.
    volatile LONG64 submitCount;
    volatile LONG64 lastSubmittedList;
    // Fence values observed after a confirmed completion, so a witness reader
    // can tell a real GPU fence apart from a textual marker.
    volatile LONG64 lastFenceValue;
    volatile LONG64 lastCompletedValue;
    // Interactive multi-frame diagnostics accounting.
    volatile LONG diagDeferCount;      // deferred because later work pending
    volatile LONG diagFlushCount;      // flushed after a confirmed full idle
    volatile LONG diagUnsafeCount;     // MUST stay 0: sink while work pending
    volatile LONG inflightAtDefer;     // max pending submissions at deferral
    volatile LONG inflightAtCheck;     // pending submissions at the test check
    volatile LONG frameSubmitCount;    // interactive Present submissions
    volatile LONG frameConfirmCount;   // interactive frame fences confirmed
    volatile LONG gateInserted;
    volatile LONG gateReleased;
    volatile LONG64 lastFlushSubmitted;
    volatile LONG64 lastFlushCompleted;
    WitnessEventEntry events[kWitnessEventCapacity];
};

// Test-only pipe status block. Unlike the fault witness it is ALWAYS created by
// the harness, so a witness-disabled case still reports, in a parent-observable
// form, whether each redirected stream was really installed.
constexpr LONG kPipeStatusMagic = 0x52505331; // 'RPS1'
constexpr LONG kPipeStatusVersion = 1;

struct PipeStatusBlock
{
    volatile LONG magic;
    volatile LONG version;
    volatile LONG stdoutRequested;
    volatile LONG stdoutOk;
    volatile LONG stderrRequested;
    volatile LONG stderrOk;
    volatile LONG activated;
    volatile LONG probeTarget;   // 0 none, 1 stdout, 2 stderr
    volatile LONG probeStarted;
    volatile LONG deactivated;
    volatile LONG stdoutError;   // SetStdHandle/_open_osfhandle/_dup2 code
    volatile LONG stderrError;
};

inline const char* WitnessCodeName(WitnessCode code) noexcept
{
    switch (code)
    {
    case WitnessCode::None: return "none";
    case WitnessCode::ExecuteCommandLists: return "execute-command-lists";
    case WitnessCode::SignalIssued: return "signal-issued";
    case WitnessCode::FenceConfirmed: return "fence-confirmed";
    case WitnessCode::FaultInjected: return "fault-injected";
    case WitnessCode::LoggerFaultInjected: return "logger-fault-injected";
    case WitnessCode::TerminalAction: return "terminal-action";
    case WitnessCode::WaitStarted: return "wait-started";
    case WitnessCode::WaitFailed: return "wait-failed";
    case WitnessCode::DeviceRemoved: return "device-removed";
    case WitnessCode::ArmImmediateOwners: return "arm-immediate-owners";
    case WitnessCode::ReleaseImmediateOwners: return "release-immediate-owners";
    case WitnessCode::ArmCloudOutputCandidates: return "arm-cloud-output-candidates";
    case WitnessCode::ReleaseCloudOutputCandidates: return "release-cloud-output-candidates";
    case WitnessCode::ArmProbeCandidate: return "arm-probe-candidate";
    case WitnessCode::ReleaseProbeCandidate: return "release-probe-candidate";
    case WitnessCode::ArmCloudReadback: return "arm-cloud-readback";
    case WitnessCode::ReleaseCloudReadback: return "release-cloud-readback";
    case WitnessCode::ArmPresentFrame: return "arm-present-frame";
    case WitnessCode::ReleasePresentFrame: return "release-present-frame";
    case WitnessCode::ArmCloudOwners: return "arm-cloud-owners";
    case WitnessCode::ReleaseCloudOwners: return "release-cloud-owners";
    case WitnessCode::ArmRendererOwners: return "arm-renderer-owners";
    case WitnessCode::ReleaseRendererOwners: return "release-renderer-owners";
    case WitnessCode::ArmWeatherMapOwners: return "arm-weather-map-owners";
    case WitnessCode::ReleaseWeatherMapOwners: return "release-weather-map-owners";
    case WitnessCode::ArmNoiseOwners: return "arm-noise-owners";
    case WitnessCode::ReleaseNoiseOwners: return "release-noise-owners";
    case WitnessCode::ArmResizeBackBuffers: return "arm-resize-back-buffers";
    case WitnessCode::ReleaseResizeBackBuffers: return "release-resize-back-buffers";
    case WitnessCode::ArmProbeReadback: return "arm-probe-readback";
    case WitnessCode::ReleaseProbeReadback: return "release-probe-readback";
    case WitnessCode::ArmRawProbeReadback: return "arm-raw-probe-readback";
    case WitnessCode::ReleaseRawProbeReadback: return "release-raw-probe-readback";
    case WitnessCode::ArmDensityReadback: return "arm-density-readback";
    case WitnessCode::ReleaseDensityReadback: return "release-density-readback";
    case WitnessCode::ArmNoiseResize: return "arm-noise-resize";
    case WitnessCode::ReleaseNoiseResize: return "release-noise-resize";
    case WitnessCode::StartupBegin: return "startup-begin";
    case WitnessCode::StartupWindow: return "startup-window";
    case WitnessCode::StartupRenderer: return "startup-renderer";
    case WitnessCode::StartupNoise: return "startup-noise";
    case WitnessCode::StartupViewer: return "startup-viewer";
    case WitnessCode::StartupWeather: return "startup-weather";
    case WitnessCode::StartupCloud: return "startup-cloud";
    case WitnessCode::StartupCompositor: return "startup-compositor";
    case WitnessCode::StartupProfiler: return "startup-profiler";
    case WitnessCode::StartupFirstFrame: return "startup-first-frame";
    case WitnessCode::BlockedPipesActivated: return "blocked-pipes-activated";
    case WitnessCode::BlockedPipesFailed: return "blocked-pipes-failed";
    case WitnessCode::BlockedPipesDeactivated: return "blocked-pipes-deactivated";
    case WitnessCode::PipeProbeStarted: return "pipe-probe-started";
    case WitnessCode::PipeProbeNotRequested: return "pipe-probe-not-requested";
    case WitnessCode::GpuIdleConfirmed: return "gpu-idle-confirmed";
    case WitnessCode::DiagnosticsDeferred: return "diagnostics-deferred";
    case WitnessCode::DiagnosticsFlushed: return "diagnostics-flushed";
    case WitnessCode::DiagnosticsUnsafe: return "diagnostics-unsafe";
    case WitnessCode::GpuGateInserted: return "gpu-gate-inserted";
    case WitnessCode::GpuGateReleased: return "gpu-gate-released";
    case WitnessCode::InflightCheck: return "inflight-check";
    case WitnessCode::FrameSubmitted: return "frame-submitted";
    case WitnessCode::FrameFenceConfirmed: return "frame-fence-confirmed";
    case WitnessCode::DiagnosticNoticeDeferred: return "diagnostic-notice-deferred";
    case WitnessCode::DiagnosticDrainDeferred: return "diagnostic-drain-deferred";
    case WitnessCode::DiagnosticDrainDone: return "diagnostic-drain-done";
    default: return "unknown";
    }
}
} // namespace clouds
