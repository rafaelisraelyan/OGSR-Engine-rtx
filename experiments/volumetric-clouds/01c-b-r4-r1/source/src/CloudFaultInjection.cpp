#include "CloudFaultInjection.h"

#if defined(OGSR_CLOUDS_FAULT_INJECTION)
#include <Windows.h>

#include <fcntl.h>
#include <io.h>

#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace
{
size_t ReadTestEnv(const char* name, char* buffer, size_t capacity) noexcept
{
    if (!name || !buffer || capacity < 2)
        return 0;
    const DWORD length = GetEnvironmentVariableA(
        name, buffer, static_cast<DWORD>(capacity));
    if (length == 0 || length >= capacity)
    {
        buffer[0] = '\0';
        return 0;
    }
    return static_cast<size_t>(length);
}

bool TestEnvFlagInternal(const char* name) noexcept
{
    char buffer[2]{};
    return ReadTestEnv(name, buffer, sizeof(buffer)) > 0;
}

bool TestEnvEqualsInternal(const char* name, const char* value) noexcept
{
    if (!value)
        return false;
    char buffer[128]{};
    if (ReadTestEnv(name, buffer, sizeof(buffer)) == 0)
        return false;
    return std::strcmp(buffer, value) == 0;
}

enum class LoggerFault
{
    None = 0,
    PostSubmit,
    PostSignal,
    PreRetirement,
    PreRetirementProbe,
    WaitError,
    ApplicationShutdown,
    CloudShutdown,
    RendererShutdown,
};

clouds::CloudFaultPoint ParseFaultPoint()
{
    char raw[64]{};
    if (ReadTestEnv("OGSR_CLOUD_FAULT_POINT", raw, sizeof(raw)) == 0)
        return clouds::CloudFaultPoint::None;
    if (std::strcmp(raw, "wait-for-gpu") == 0)
        return clouds::CloudFaultPoint::WaitForGpu;
    if (std::strcmp(raw, "texture-0") == 0)
        return clouds::CloudFaultPoint::Texture0;
    if (std::strcmp(raw, "texture-1") == 0)
        return clouds::CloudFaultPoint::Texture1;
    if (std::strcmp(raw, "texture-2") == 0)
        return clouds::CloudFaultPoint::Texture2;
    if (std::strcmp(raw, "probe-buffer") == 0)
        return clouds::CloudFaultPoint::ProbeBuffer;
    if (std::strcmp(raw, "probe-transition") == 0)
        return clouds::CloudFaultPoint::ProbeTransition;
    if (std::strcmp(raw, "renderer-post-submit") == 0)
        return clouds::CloudFaultPoint::RendererPostSubmit;
    if (std::strcmp(raw, "renderer-post-signal") == 0)
        return clouds::CloudFaultPoint::RendererPostSignal;
    if (std::strcmp(raw, "pre-submit-wait") == 0)
        return clouds::CloudFaultPoint::PreSubmitWait;
    if (std::strcmp(raw, "shutdown-application-wait") == 0)
        return clouds::CloudFaultPoint::ShutdownApplicationWait;
    if (std::strcmp(raw, "shutdown-cloud-wait") == 0)
        return clouds::CloudFaultPoint::ShutdownCloudWait;
    if (std::strcmp(raw, "shutdown-renderer-wait") == 0)
        return clouds::CloudFaultPoint::ShutdownRendererWait;
    return clouds::CloudFaultPoint::None;
}

LoggerFault ParseLoggerFault()
{
    char raw[64]{};
    if (ReadTestEnv("OGSR_CLOUD_LOGGER_FAULT", raw, sizeof(raw)) == 0)
        return LoggerFault::None;
    if (std::strcmp(raw, "post-submit") == 0)
        return LoggerFault::PostSubmit;
    if (std::strcmp(raw, "post-signal") == 0)
        return LoggerFault::PostSignal;
    if (std::strcmp(raw, "pre-retirement") == 0)
        return LoggerFault::PreRetirement;
    if (std::strcmp(raw, "pre-retirement-probe") == 0)
        return LoggerFault::PreRetirementProbe;
    if (std::strcmp(raw, "wait-error") == 0)
        return LoggerFault::WaitError;
    if (std::strcmp(raw, "application-shutdown") == 0)
        return LoggerFault::ApplicationShutdown;
    if (std::strcmp(raw, "cloud-shutdown") == 0)
        return LoggerFault::CloudShutdown;
    if (std::strcmp(raw, "renderer-shutdown") == 0)
        return LoggerFault::RendererShutdown;
    return LoggerFault::None;
}

clouds::CloudFaultPoint ConfiguredFaultPoint()
{
    // Parsed once: the environment is fixed before the child starts, and the
    // post-submit interval must not perform OS calls.
    static const clouds::CloudFaultPoint point = ParseFaultPoint();
    return point;
}

LoggerFault ConfiguredLoggerFault()
{
    static const LoggerFault fault = ParseLoggerFault();
    return fault;
}

bool LoggerFaultArmedAtStart()
{
    char stage[32]{};
    if (ReadTestEnv("OGSR_CLOUD_LOGGER_FAULT_ARM", stage, sizeof(stage)) == 0)
        return true;
    return std::strcmp(stage, "start") == 0;
}

bool g_armed = false;
bool g_consumed = false;
bool g_armAtStart =
    TestEnvFlagInternal("OGSR_CLOUD_FAULT_ARM_AT_START");

bool g_loggerFaultArmed = LoggerFaultArmedAtStart();
bool g_loggerFaultConsumed = false;

bool g_loggingSuppressed =
    TestEnvFlagInternal("OGSR_CLOUD_TEST_SUPPRESS_LOGGING");
bool g_witnessDisabled =
    TestEnvFlagInternal("OGSR_CLOUD_WITNESS_DISABLE");
bool g_witnessInitialized = false;
clouds::WitnessBlock* g_witness = nullptr;
clouds::PipeStatusBlock* g_pipeStatus = nullptr;
bool g_pipeStatusOpened = false;
bool g_blockedPipesActive = false;
bool g_stdoutOk = false;
bool g_stderrOk = false;
bool g_stdoutRequested = false;
bool g_stderrRequested = false;
int g_stdoutError = 0;
int g_stderrError = 0;
int g_savedStdoutFd = -1;
int g_savedStderrFd = -1;
HANDLE g_savedStdoutHandle = INVALID_HANDLE_VALUE;
HANDLE g_savedStderrHandle = INVALID_HANDLE_VALUE;

void OpenPipeStatus() noexcept
{
    if (g_pipeStatusOpened)
        return;
    g_pipeStatusOpened = true;
    char name[160]{};
    if (ReadTestEnv("OGSR_CLOUD_PIPESTATUS_SHM", name, sizeof(name)) == 0)
        return;
    wchar_t wideName[160]{};
    if (MultiByteToWideChar(CP_UTF8, 0, name, -1, wideName,
                            static_cast<int>(sizeof(wideName) /
                                             sizeof(wideName[0]))) <= 0)
        return;
    const HANDLE mapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE, wideName);
    if (!mapping)
        return;
    void* view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0,
                               sizeof(clouds::PipeStatusBlock));
    CloseHandle(mapping);
    if (!view)
        return;
    auto* block = static_cast<clouds::PipeStatusBlock*>(view);
    if (block->magic != clouds::kPipeStatusMagic ||
        block->version != clouds::kPipeStatusVersion)
        return;
    g_pipeStatus = block;
}

// Returns 0 on success, or a non-zero diagnostic code. Every step is checked:
// a stream is only reported as redirected when SetStdHandle, _open_osfhandle
// and _dup2 all succeeded on it.
int InstallStream(const char* envName, DWORD stdHandle, FILE* stream,
                  HANDLE* savedHandle, int* savedFd) noexcept
{
    char raw[32]{};
    if (ReadTestEnv(envName, raw, sizeof(raw)) == 0)
        return -1; // not requested
    const unsigned long long value = _strtoui64(raw, nullptr, 10);
    if (value == 0)
        return -2;
    const HANDLE handle = reinterpret_cast<HANDLE>(
        static_cast<uintptr_t>(value));

    // Keep the original handle/fd so the test can restore the real stream.
    *savedHandle = GetStdHandle(stdHandle);
    *savedFd = _dup(_fileno(stream));

    if (!SetStdHandle(stdHandle, handle))
        return 1;
    const int fd = _open_osfhandle(
        reinterpret_cast<intptr_t>(handle), _O_WRONLY);
    if (fd == -1)
        return 2;
    if (_dup2(fd, _fileno(stream)) != 0)
        return 3;
    return 0;
}

bool ShutdownLoggerFaultMatches(const char* body)
{
    if (!body)
        return false;
    switch (ConfiguredLoggerFault())
    {
    case LoggerFault::ApplicationShutdown:
        return std::strstr(
                   body, "shutdown: GPU idle; releasing resources") != nullptr;
    case LoggerFault::CloudShutdown:
        return std::strstr(
                   body, "cloud ray marcher shutdown: generation=") != nullptr;
    case LoggerFault::RendererShutdown:
        return std::strstr(body, "renderer shutdown: GPU idle") != nullptr;
    default:
        return false;
    }
}
} // namespace

namespace clouds
{
void InitWitness() noexcept
{
    if (g_witnessInitialized)
        return;
    g_witnessInitialized = true;
    // The pipe-status block is opened regardless of the witness switch, so a
    // witness-disabled case still exposes per-stream installation results.
    OpenPipeStatus();
    if (g_witnessDisabled)
        return;

    char name[160]{};
    if (ReadTestEnv("OGSR_CLOUD_WITNESS_SHM", name, sizeof(name)) == 0)
        return;

    wchar_t wideName[160]{};
    if (MultiByteToWideChar(CP_UTF8, 0, name, -1, wideName,
                            static_cast<int>(sizeof(wideName) /
                                             sizeof(wideName[0]))) <= 0)
        return;

    const HANDLE mapping = OpenFileMappingW(
        FILE_MAP_WRITE, FALSE, wideName);
    if (!mapping)
        return;
    void* view = MapViewOfFile(
        mapping, FILE_MAP_WRITE, 0, 0, sizeof(WitnessBlock));
    CloseHandle(mapping);
    if (!view)
        return;

    auto* block = static_cast<WitnessBlock*>(view);
    if (block->magic != kWitnessMagic || block->version != kWitnessVersion)
        return;
    g_witness = block;
}

bool LoggingSuppressed() noexcept
{
    return g_loggingSuppressed;
}

void MaybeActivateBlockedPipes(const char* label) noexcept
{
    if (g_blockedPipesActive)
        return;
    char expected[96]{};
    if (!label ||
        ReadTestEnv("OGSR_CLOUD_BLOCK_AT_LABEL", expected,
                    sizeof(expected)) == 0 ||
        std::strcmp(label, expected) != 0)
    {
        return;
    }

    OpenPipeStatus();

    // Each stream is installed and validated independently. A stream that was
    // not requested is reported as not requested, never as success.
    const int stdoutCode = InstallStream(
        "OGSR_CLOUD_BLOCK_STDOUT_HANDLE", STD_OUTPUT_HANDLE, stdout,
        &g_savedStdoutHandle, &g_savedStdoutFd);
    g_stdoutRequested = stdoutCode != -1;
    g_stdoutError = stdoutCode > 0 ? stdoutCode : 0;
    g_stdoutOk = stdoutCode == 0;

    const int stderrCode = InstallStream(
        "OGSR_CLOUD_BLOCK_STDERR_HANDLE", STD_ERROR_HANDLE, stderr,
        &g_savedStderrHandle, &g_savedStderrFd);
    g_stderrRequested = stderrCode != -1;
    g_stderrError = stderrCode > 0 ? stderrCode : 0;
    g_stderrOk = stderrCode == 0;

    const bool anyRequested = g_stdoutRequested || g_stderrRequested;
    const bool allOk = (!g_stdoutRequested || g_stdoutOk) &&
                       (!g_stderrRequested || g_stderrOk);

    // Pipe-status shared memory is created unconditionally by the harness, so
    // this survives a witness-disabled case.
    if (g_pipeStatus)
    {
        InterlockedExchange(&g_pipeStatus->stdoutRequested,
                            g_stdoutRequested ? 1 : 0);
        InterlockedExchange(&g_pipeStatus->stdoutOk, g_stdoutOk ? 1 : 0);
        InterlockedExchange(&g_pipeStatus->stderrRequested,
                            g_stderrRequested ? 1 : 0);
        InterlockedExchange(&g_pipeStatus->stderrOk, g_stderrOk ? 1 : 0);
        InterlockedExchange(&g_pipeStatus->stdoutError, g_stdoutError);
        InterlockedExchange(&g_pipeStatus->stderrError, g_stderrError);
        InterlockedExchange(&g_pipeStatus->activated,
                            (allOk && anyRequested) ? 1 : 0);
    }

    if (!anyRequested)
        return;
    if (!allOk)
    {
        // A failed installation is never reported as a successful activation.
        WitnessEvent(WitnessCode::BlockedPipesFailed);
        return;
    }

    g_blockedPipesActive = true;
    WitnessEvent(WitnessCode::BlockedPipesActivated);

    // Sensitivity control. `OGSR_CLOUD_BLOCK_PROBE_TARGET` selects which
    // redirected stream receives one deliberate write: stdout, stderr, or both.
    // Because the pipe has no reader the write must block, the harness must hit
    // its timeout and the parser must count that as FAIL. Triggering the probe
    // per stream is what proves each stream individually, instead of proving
    // only whichever stream happens to be written first.
    char target[16]{};
    if (ReadTestEnv("OGSR_CLOUD_BLOCK_PROBE_TARGET", target,
                    sizeof(target)) > 0)
    {
        LONG probeTarget = 0;
        if (_stricmp(target, "stdout") == 0)
            probeTarget = 1;
        else if (_stricmp(target, "stderr") == 0)
            probeTarget = 2;
        else if (_stricmp(target, "both") == 0)
            probeTarget = 3;

        if (g_pipeStatus)
        {
            InterlockedExchange(&g_pipeStatus->probeTarget, probeTarget);
            InterlockedExchange(&g_pipeStatus->probeStarted,
                                probeTarget != 0 ? 1 : 0);
        }
        if (probeTarget != 0)
            WitnessEvent(WitnessCode::PipeProbeStarted);
        else
            WitnessEvent(WitnessCode::PipeProbeNotRequested);

        if (probeTarget == 1 || probeTarget == 3)
        {
            fputs("blocked pipe probe\n", stdout);
            fflush(stdout);
        }
        if (probeTarget == 2 || probeTarget == 3)
        {
            fputs("blocked pipe probe\n", stderr);
            fflush(stderr);
        }
    }
}

void DeactivateBlockedPipes() noexcept
{
    if (!g_blockedPipesActive)
        return;
    if (g_savedStdoutFd != -1)
    {
        _dup2(g_savedStdoutFd, _fileno(stdout));
        _close(g_savedStdoutFd);
        g_savedStdoutFd = -1;
    }
    if (g_savedStdoutHandle != INVALID_HANDLE_VALUE)
    {
        SetStdHandle(STD_OUTPUT_HANDLE, g_savedStdoutHandle);
        g_savedStdoutHandle = INVALID_HANDLE_VALUE;
    }
    if (g_savedStderrFd != -1)
    {
        _dup2(g_savedStderrFd, _fileno(stderr));
        _close(g_savedStderrFd);
        g_savedStderrFd = -1;
    }
    if (g_savedStderrHandle != INVALID_HANDLE_VALUE)
    {
        SetStdHandle(STD_ERROR_HANDLE, g_savedStderrHandle);
        g_savedStderrHandle = INVALID_HANDLE_VALUE;
    }
    g_blockedPipesActive = false;
    if (g_pipeStatus)
        InterlockedExchange(&g_pipeStatus->deactivated, 1);
    WitnessEvent(WitnessCode::BlockedPipesDeactivated);
}

bool BlockedPipeStreamOk(int which) noexcept
{
    return which == 1 ? g_stdoutOk : g_stderrOk;
}

bool BlockedPipesActive() noexcept
{
    return g_blockedPipesActive;
}

void WitnessEvent(WitnessCode code) noexcept
{
    if (!g_witness)
        return;
    if (code == WitnessCode::TerminalAction)
        InterlockedExchange(&g_witness->terminalFlag, 1);
    const LONG sequence = InterlockedIncrement(&g_witness->sequence);
    const size_t index =
        static_cast<size_t>(sequence - 1) % kWitnessEventCapacity;
    g_witness->events[index].code = static_cast<LONG>(code);
    g_witness->events[index].sequence = sequence;
}

void NoteCommandListsSubmitted(const void* list) noexcept
{
    if (!g_witness)
        return;
    InterlockedIncrement64(&g_witness->submitCount);
    InterlockedExchange64(
        &g_witness->lastSubmittedList,
        static_cast<LONG64>(reinterpret_cast<uintptr_t>(list)));
}

void NoteFenceConfirmed(unsigned long long fenceValue,
                        unsigned long long completedValue) noexcept
{
    if (!g_witness)
        return;
    InterlockedExchange64(&g_witness->lastFenceValue,
                          static_cast<LONG64>(fenceValue));
    InterlockedExchange64(&g_witness->lastCompletedValue,
                          static_cast<LONG64>(completedValue));
    InterlockedIncrement(&g_witness->fenceConfirmedCount);
}

void NoteGpuIdleConfirmed(unsigned long long submitted,
                          unsigned long long completed) noexcept
{
    if (!g_witness)
        return;
    InterlockedExchange64(&g_witness->lastFlushSubmitted,
                          static_cast<LONG64>(submitted));
    InterlockedExchange64(&g_witness->lastFlushCompleted,
                          static_cast<LONG64>(completed));
    WitnessEvent(WitnessCode::GpuIdleConfirmed);
}

void NoteDiagnosticsDeferred(long inflightSubmissions) noexcept
{
    if (!g_witness)
        return;
    InterlockedIncrement(&g_witness->diagDeferCount);
    // Keep the maximum observed in-flight depth for the evidence reader.
    LONG current = InterlockedCompareExchange(&g_witness->inflightAtDefer, 0, 0);
    while (inflightSubmissions > current &&
           InterlockedCompareExchange(&g_witness->inflightAtDefer,
                                      inflightSubmissions, current) != current)
    {
        current = InterlockedCompareExchange(
            &g_witness->inflightAtDefer, 0, 0);
    }
    WitnessEvent(WitnessCode::DiagnosticsDeferred);
}

void NoteDiagnosticsFlushed(unsigned long long submitted,
                            unsigned long long completed) noexcept
{
    if (!g_witness)
        return;
    InterlockedIncrement(&g_witness->diagFlushCount);
    InterlockedExchange64(&g_witness->lastFlushSubmitted,
                          static_cast<LONG64>(submitted));
    InterlockedExchange64(&g_witness->lastFlushCompleted,
                          static_cast<LONG64>(completed));
    WitnessEvent(WitnessCode::DiagnosticsFlushed);
}

void NoteDiagnosticsUnsafe() noexcept
{
    if (!g_witness)
        return;
    InterlockedIncrement(&g_witness->diagUnsafeCount);
    WitnessEvent(WitnessCode::DiagnosticsUnsafe);
}

void NoteFrameSubmitted() noexcept
{
    if (!g_witness)
        return;
    InterlockedIncrement(&g_witness->frameSubmitCount);
    WitnessEvent(WitnessCode::FrameSubmitted);
}

void NoteFrameFenceConfirmed() noexcept
{
    if (!g_witness)
        return;
    InterlockedIncrement(&g_witness->frameConfirmCount);
    WitnessEvent(WitnessCode::FrameFenceConfirmed);
}

void NoteInflightCheck(long inflightSubmissions) noexcept
{
    if (!g_witness)
        return;
    InterlockedExchange(&g_witness->inflightAtCheck, inflightSubmissions);
    WitnessEvent(WitnessCode::InflightCheck);
}

void NoteGpuGateInserted() noexcept
{
    if (!g_witness)
        return;
    InterlockedExchange(&g_witness->gateInserted, 1);
    WitnessEvent(WitnessCode::GpuGateInserted);
}

void NoteGpuGateReleased() noexcept
{
    if (!g_witness)
        return;
    InterlockedExchange(&g_witness->gateReleased, 1);
    WitnessEvent(WitnessCode::GpuGateReleased);
}

void ArmCloudFault()
{
    g_armed = true;
    g_consumed = false;
}

bool TestEnvFlag(const char* name) noexcept
{
    return TestEnvFlagInternal(name);
}

bool TestEnvEquals(const char* name, const char* value) noexcept
{
    return TestEnvEqualsInternal(name, value);
}

void ArmLoggerFault()
{
    g_loggerFaultArmed = true;
}

bool ConsumeCloudFault(CloudFaultPoint point)
{
    if (g_armAtStart)
    {
        g_armed = true;
        g_armAtStart = false;
    }
    const clouds::CloudFaultPoint configured = ConfiguredFaultPoint();
    if (!g_armed || point != configured || g_consumed)
        return false;
    if (configured == CloudFaultPoint::None)
        return false;
    g_consumed = true;
    return true;
}

bool FaultArmedFor(CloudFaultPoint point) noexcept
{
    const bool armed = g_armed || g_armAtStart;
    return armed && !g_consumed && ConfiguredFaultPoint() == point;
}

bool FaultArmedForPoint(CloudFaultPoint point) noexcept
{
    return FaultArmedFor(point);
}

bool LoggerFaultArmedForPoint(LoggerFaultPoint point) noexcept
{
    if (!g_loggerFaultArmed || g_loggerFaultConsumed)
        return false;
    const LoggerFault fault = ConfiguredLoggerFault();
    switch (point)
    {
    case LoggerFaultPoint::PreRetirement:
        return fault == LoggerFault::PreRetirement;
    case LoggerFaultPoint::PreRetirementProbe:
        return fault == LoggerFault::PreRetirementProbe;
    case LoggerFaultPoint::WaitError:
        // Only consumed inside the already-failed wait branch, so it must never
        // by itself make a successful pre-retirement attempt look terminal.
        return fault == LoggerFault::WaitError;
    default:
        return false;
    }
}

bool TerminalFaultPending() noexcept
{
    // True only for the faults that the post-submit interval of the next
    // immediate submission will actually consume.
    if (FaultArmedFor(CloudFaultPoint::RendererPostSubmit) ||
        FaultArmedFor(CloudFaultPoint::RendererPostSignal))
        return true;
    if (g_loggerFaultArmed && !g_loggerFaultConsumed)
    {
        const LoggerFault fault = ConfiguredLoggerFault();
        if (fault == LoggerFault::PostSubmit || fault == LoggerFault::PostSignal)
            return true;
    }
    return false;
}

bool PreRetirementFaultPending() noexcept
{
    // Only a WaitForGpu fault is consumed by the pre-retirement retirement wait.
    // The logger faults are merely recorded there; treating WaitError as
    // "pending" previously switched the pipes on in a call that completed
    // successfully, which is exactly the false-positive this guards against.
    if (FaultArmedFor(CloudFaultPoint::WaitForGpu))
        return true;
    if (g_loggerFaultArmed && !g_loggerFaultConsumed)
    {
        const LoggerFault fault = ConfiguredLoggerFault();
        if (fault == LoggerFault::PreRetirement ||
            fault == LoggerFault::PreRetirementProbe)
            return true;
    }
    return false;
}

bool ShutdownFaultPendingFor(CloudFaultPoint point) noexcept
{
    return FaultArmedFor(point);
}

void MaybeThrowPostSubmitLoggerFault(int stage)
{
    if (!g_loggerFaultArmed || g_loggerFaultConsumed)
        return;
    const LoggerFault fault = ConfiguredLoggerFault();
    const bool match =
        (stage == 1 && fault == LoggerFault::PostSubmit) ||
        (stage == 2 && fault == LoggerFault::PostSignal);
    if (!match)
        return;
    g_loggerFaultConsumed = true;
    WitnessEvent(WitnessCode::LoggerFaultInjected);
    throw std::runtime_error("injected logger failure");
}

void MaybeRecordLoggerFault(LoggerFaultPoint point) noexcept
{
    if (!g_loggerFaultArmed || g_loggerFaultConsumed)
        return;
    const LoggerFault fault = ConfiguredLoggerFault();
    bool match = false;
    switch (point)
    {
    case LoggerFaultPoint::PreRetirement:
        match = fault == LoggerFault::PreRetirement;
        break;
    case LoggerFaultPoint::PreRetirementProbe:
        match = fault == LoggerFault::PreRetirementProbe;
        break;
    case LoggerFaultPoint::WaitError:
        match = fault == LoggerFault::WaitError;
        break;
    default:
        break;
    }
    if (!match)
        return;
    g_loggerFaultConsumed = true;
    WitnessEvent(WitnessCode::LoggerFaultInjected);
}

void NoteLogForFaultInjection(const char* level, const char* body)
{
    (void)level;
    if (!g_loggerFaultArmed || g_loggerFaultConsumed ||
        !ShutdownLoggerFaultMatches(body))
        return;
    g_loggerFaultConsumed = true;
    WitnessEvent(WitnessCode::LoggerFaultInjected);
    throw std::runtime_error("injected logger failure");
}

LifetimeWitness::LifetimeWitness(WitnessCode armCode) noexcept
    : armCode_(armCode)
{
    WitnessEvent(armCode_);
}

LifetimeWitness::~LifetimeWitness() noexcept
{
    const WitnessCode release = static_cast<WitnessCode>(
        static_cast<int>(armCode_) + 1);
    WitnessEvent(release);
}
} // namespace clouds

#else
namespace clouds
{
void InitWitness() noexcept
{
}

bool LoggingSuppressed() noexcept
{
    return false;
}

void MaybeActivateBlockedPipes(const char*) noexcept
{
}

bool BlockedPipesActive() noexcept
{
    return false;
}

void WitnessEvent(WitnessCode) noexcept
{
}

void NoteCommandListsSubmitted(const void*) noexcept
{
}

void NoteFenceConfirmed(unsigned long long, unsigned long long) noexcept
{
}

void ArmCloudFault()
{
}

bool TestEnvFlag(const char*) noexcept
{
    return false;
}

bool TestEnvEquals(const char*, const char*) noexcept
{
    return false;
}

void ArmLoggerFault()
{
}

bool ConsumeCloudFault(CloudFaultPoint)
{
    return false;
}

void MaybeThrowPostSubmitLoggerFault(int)
{
}

void MaybeRecordLoggerFault(LoggerFaultPoint) noexcept
{
}

bool TerminalFaultPending() noexcept
{
    return false;
}

bool PreRetirementFaultPending() noexcept
{
    return false;
}

bool ShutdownFaultPendingFor(CloudFaultPoint) noexcept
{
    return false;
}

bool FaultArmedForPoint(CloudFaultPoint) noexcept
{
    return false;
}

bool LoggerFaultArmedForPoint(LoggerFaultPoint) noexcept
{
    return false;
}

void DeactivateBlockedPipes() noexcept
{
}

bool BlockedPipeStreamOk(int) noexcept
{
    return false;
}

void NoteGpuIdleConfirmed(unsigned long long, unsigned long long) noexcept
{
}

void NoteDiagnosticsDeferred(long) noexcept
{
}

void NoteDiagnosticsFlushed(unsigned long long, unsigned long long) noexcept
{
}

void NoteDiagnosticsUnsafe() noexcept
{
}

void NoteFrameSubmitted() noexcept
{
}

void NoteFrameFenceConfirmed() noexcept
{
}

void NoteInflightCheck(long) noexcept
{
}

void NoteGpuGateInserted() noexcept
{
}

void NoteGpuGateReleased() noexcept
{
}

void NoteLogForFaultInjection(const char*, const char*)
{
}

LifetimeWitness::LifetimeWitness(WitnessCode) noexcept
{
}

LifetimeWitness::~LifetimeWitness() noexcept
{
}
} // namespace clouds
#endif
