#pragma once

#include "CloudWitness.h"

namespace clouds
{
enum class CloudFaultPoint
{
    None = 0,
    WaitForGpu,
    Texture0,
    Texture1,
    Texture2,
    ProbeBuffer,
    ProbeTransition,
    RendererPostSubmit,
    RendererPostSignal,
    PreSubmitWait,
    ShutdownApplicationWait,
    ShutdownCloudWait,
    ShutdownRendererWait,
};

enum class LoggerFaultPoint
{
    PreRetirement = 0,
    PreRetirementProbe,
    WaitError,
    ApplicationShutdown,
    CloudShutdown,
    RendererShutdown,
};

// One-shot fault injection for isolated tests. Compiled to a no-op unless
// OGSR_CLOUDS_FAULT_INJECTION is defined; shipping-controls never defines it.
void ArmCloudFault();
bool ConsumeCloudFault(CloudFaultPoint point);

// Test-only environment helpers. In non-fault builds they always return false.
bool TestEnvFlag(const char* name) noexcept;
bool TestEnvEquals(const char* name, const char* value) noexcept;

// Arms the configured logger fault after a stage-specific setup.
void ArmLoggerFault();

// L5-L7: a real logger call in a safe shutdown stage throws after writing.
void NoteLogForFaultInjection(const char* level, const char* body);

// L1/L2: no-I/O logger failure injection inside the post-submit interval.
// stage 1 = after ExecuteCommandLists, stage 2 = after Signal.
void MaybeThrowPostSubmitLoggerFault(int stage);

// L4: record a configured logger failure without doing any I/O and without
// throwing, so a pre-retirement/wait-error path can continue to terminal.
void MaybeRecordLoggerFault(LoggerFaultPoint point) noexcept;

// Test-only: true when the configured fault will be consumed inside the
// post-submit interval of the next immediate-work submission. The blocked-pipe
// harness uses this to switch stdout/stderr to a full pipe only in the immediate
// that actually reaches the terminal boundary.
bool TerminalFaultPending() noexcept;

// Test-only: true when a pre-retirement GPU wait is about to fail, so the
// blocked-pipe harness switches the pipes only in the retirement attempt that
// reaches the terminal boundary.
bool PreRetirementFaultPending() noexcept;

// Test-only: true when the shutdown retirement wait of the given owner is
// about to fail. Per-owner, so an activation site cannot switch the pipes on for
// a fault that another owner will consume (or nobody will).
bool ShutdownFaultPendingFor(CloudFaultPoint point) noexcept;

// Test-only: exact fault-point predicate, so an activation site can prove that
// the specific fault it will consume is armed (not merely some fault category).
bool FaultArmedForPoint(CloudFaultPoint point) noexcept;
bool LoggerFaultArmedForPoint(LoggerFaultPoint point) noexcept;

// Interactive multi-frame diagnostics accounting. NoteDiagnosticsDeferred is
// called when a deferred sink had to wait because later GPU work was still
// unconfirmed; NoteDiagnosticsUnsafe must never be reached and exists to make a
// violation observable instead of silent.
void NoteGpuIdleConfirmed(unsigned long long submitted,
                          unsigned long long completed) noexcept;
void NoteDiagnosticsDeferred(long inflightSubmissions) noexcept;
void NoteDiagnosticsFlushed(unsigned long long submitted,
                            unsigned long long completed) noexcept;
void NoteDiagnosticsUnsafe() noexcept;
void NoteFrameSubmitted() noexcept;
void NoteFrameFenceConfirmed() noexcept;
void NoteInflightCheck(long inflightSubmissions) noexcept;
void NoteGpuGateInserted() noexcept;
void NoteGpuGateReleased() noexcept;

// Attaches the pre-created shared-memory witness. Safe to call once early;
// failure just disables the witness and never blocks the terminal path.
void InitWitness() noexcept;
bool LoggingSuppressed() noexcept;

// Test-only late pipe switch: the child starts with normal file handles and
// substitutes already-full pipes only when the label matches. This avoids a
// false hang in external GPU/shader libraries during startup. Each stream is
// validated (SetStdHandle / _open_osfhandle / _dup2); a failed installation
// never reports success.
void MaybeActivateBlockedPipes(const char* label) noexcept;
bool BlockedPipesActive() noexcept;

// Test-only: restore the original std handles of every redirected stream, so a
// test that blocked the pipes for a bounded window can finish normally.
void DeactivateBlockedPipes() noexcept;

// Per-stream installation result for the parent harness. Returns false when a
// requested stream could not be redirected. `which`: 1 stdout, 2 stderr.
bool BlockedPipeStreamOk(int which) noexcept;

// Atomic, allocation-free witness record. No pipe/file/mutex operations.
void WitnessEvent(WitnessCode code) noexcept;

// Called immediately after a successful ID3D12CommandQueue::ExecuteCommandLists
// returned, while the submitted command list is still alive. Records the real
// submission count and the submitted ID3D12GraphicsCommandList pointer so the
// parent can prove the fault test observed a real GPU submission.
void NoteCommandListsSubmitted(const void* list) noexcept;

// Called after a fence wait confirmed completion. Records the requested fence
// value and the value the GPU actually completed, so the witness distinguishes
// a real GPU completion from a textual marker.
void NoteFenceConfirmed(unsigned long long fenceValue,
                        unsigned long long completedValue) noexcept;

class LifetimeWitness
{
public:
    explicit LifetimeWitness(WitnessCode armCode) noexcept;
    ~LifetimeWitness() noexcept;

    LifetimeWitness(const LifetimeWitness&) = delete;
    LifetimeWitness& operator=(const LifetimeWitness&) = delete;

private:
    WitnessCode armCode_ = WitnessCode::None;
};
} // namespace clouds
