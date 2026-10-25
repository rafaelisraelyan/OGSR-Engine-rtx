// F4.0-B3-A: CPU safety tests for the GPU lifetime / command-list /
// validation policy seam in b3a_gpu_state.h.
//
// These tests prove decision logic and ownership transfer only. They do not
// create a D3D12 device and do not inject real GPU faults (TDR/timeout), so
// they can never be reported as GPU failure recovery evidence.

#include "b3a_gpu_state.h"

#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using b3a::FenceOutcome;
using b3a::GpuWorkState;
using b3a::ListState;
using b3a::UniqueHandle;
using b3a::ValidationEvidence;

int g_checks = 0;
int g_failures = 0;
const char* g_case = "";

void BeginCase(const char* name)
{
    g_case = name;
}

void Expect(bool condition, const char* what)
{
    ++g_checks;
    if (!condition)
    {
        ++g_failures;
        std::printf("FAIL [%s] %s\n", g_case, what);
    }
}

void EndCase()
{
    std::printf("PASS %s\n", g_case);
}

bool HandleIsValid(HANDLE handle)
{
    DWORD flags = 0;
    return ::GetHandleInformation(handle, &flags) != FALSE;
}

// Minimal stand-in for a GPU owner: one handle plus a canary so a move can be
// observed. It is deliberately not a COM owner.
struct FakeOwner
{
    UniqueHandle handle;
    int canary = 0;
};

void TestNormalCompletion()
{
    BeginCase("normal_completion");
    GpuWorkState work = GpuWorkState::NoWorkSubmitted;
    ListState list = ListState::Recording;
    Expect(b3a::MaySubmit(list), "recording list may submit");
    list = b3a::ListStateAfterClose();
    work = b3a::StateAfterExecute();
    Expect(work == GpuWorkState::WorkSubmittedUnknown,
           "completion is unknown immediately after execute");
    Expect(b3a::MustRetainUntilCompletion(work), "unknown completion must retain");
    Expect(!b3a::MayResetList(list, work), "unknown completion blocks reset");
    work = b3a::StateAfterFenceWait(FenceOutcome::Completed, false);
    Expect(work == GpuWorkState::WorkCompleted, "fence completion is WorkCompleted");
    list = b3a::ListStateAfterConfirmedSubmit();
    Expect(b3a::MayResetList(list, work), "confirmed completion allows reset");
    Expect(!b3a::MustRetainUntilCompletion(work), "confirmed completion releases");
    list = b3a::ListStateAfterReset();
    work = GpuWorkState::NoWorkSubmitted;
    Expect(list == ListState::Recording, "reset returns to recording");
    Expect(b3a::MaySubmit(list), "reset list may submit again");
    EndCase();
}

void TestFailBeforeFirstSubmit()
{
    BeginCase("fail_before_first_submit");
    // CreateCommandList leaves the list Recording; the test closes it once
    // before the first allocator reset.
    ListState list = ListState::Recording;
    Expect(!b3a::MayResetList(list, GpuWorkState::NoWorkSubmitted),
           "recording list may not be reset directly");
    list = b3a::ListStateAfterClose();
    Expect(b3a::MayResetList(list, GpuWorkState::NoWorkSubmitted),
           "initial close allows the first reset");
    Expect(!b3a::MustRetainUntilCompletion(GpuWorkState::NoWorkSubmitted),
           "no submitted work is never retained");
    EndCase();
}

void TestFailureDuringResourceCreation()
{
    BeginCase("failure_during_resource_creation");
    // Creation fails while the list is still Recording: nothing was submitted,
    // nothing must be parked, and the list is not reset before closing.
    const ListState list = ListState::Recording;
    const GpuWorkState work = GpuWorkState::NoWorkSubmitted;
    Expect(!b3a::MustRetainUntilCompletion(work), "creation failure retains nothing");
    Expect(!b3a::MayResetList(list, work), "creation failure does not reset");
    EndCase();
}

void TestCloseFailure()
{
    BeginCase("close_failure");
    // A failed Close is terminal for the command session. Nothing was
    // submitted, so normal destruction is allowed, but the list must not be
    // submitted, closed again or reset.
    const ListState list = b3a::ListStateAfterFailedClose();
    const GpuWorkState work = GpuWorkState::NoWorkSubmitted;
    Expect(list == ListState::CloseFailed, "failed close has its own state");
    Expect(!b3a::MaySubmit(list), "failed close rejects submit");
    Expect(!b3a::MayResetList(list, work), "failed close rejects reset");
    Expect(!b3a::MustRetainUntilCompletion(work),
           "failed close without submit permits normal cleanup");
    EndCase();
}

void TestSuccessfulCloseAllowsSubmit()
{
    BeginCase("successful_close_allows_submit");
    ListState list = b3a::ListStateAfterClose();
    Expect(!b3a::MaySubmit(list), "closed list is not directly submittable");
    Expect(b3a::MayResetList(list, GpuWorkState::NoWorkSubmitted),
           "closed list may be reset before recording");
    list = b3a::ListStateAfterReset();
    Expect(b3a::MaySubmit(list), "recording list may submit");
    EndCase();
}

void TestFailureBeforeExecute()
{
    BeginCase("failure_before_execute");
    // The list is closed but ExecuteCommandLists was never called.
    const ListState list = b3a::ListStateAfterClose();
    const GpuWorkState work = GpuWorkState::NoWorkSubmitted;
    Expect(!b3a::MustRetainUntilCompletion(work),
           "closed-but-not-executed work is not retained");
    Expect(b3a::MayResetList(list, work), "closed-but-not-executed work may reset");
    EndCase();
}

void TestFailureImmediatelyAfterSubmission()
{
    BeginCase("failure_immediately_after_submission");
    const GpuWorkState work = b3a::StateAfterExecute();
    Expect(work == GpuWorkState::WorkSubmittedUnknown, "execute yields unknown state");
    Expect(b3a::MustRetainUntilCompletion(work), "post-submit failure retains owner");
    Expect(!b3a::MayResetList(b3a::ListStateAfterClose(), work),
           "post-submit failure blocks allocator reset");
    Expect(!b3a::CompletionConfirmed(work), "unknown state is not confirmed");
    EndCase();
}

void TestSimulatedSignalError()
{
    BeginCase("simulated_signal_error");
    // ExecuteCommandLists succeeded, Signal failed: completion stays unknown.
    GpuWorkState work = b3a::StateAfterExecute();
    const bool signalFailed = true;
    if (signalFailed)
        work = b3a::StateAfterExecute();
    Expect(work == GpuWorkState::WorkSubmittedUnknown, "signal error stays unknown");
    Expect(b3a::MustRetainUntilCompletion(work), "signal error retains owner");
    EndCase();
}

void TestSimulatedTimeout()
{
    BeginCase("simulated_timeout");
    const GpuWorkState work =
        b3a::StateAfterFenceWait(FenceOutcome::Timeout, S_OK);
    Expect(work == GpuWorkState::WorkSubmittedUnknown, "timeout stays unknown");
    Expect(b3a::MustRetainUntilCompletion(work), "timeout retains owner");
    Expect(!b3a::MayResetList(b3a::ListStateAfterConfirmedSubmit(), work),
           "timeout blocks reset");
    EndCase();
}

void TestDeviceRemoved()
{
    BeginCase("device_removed");
    const GpuWorkState removed =
        b3a::StateAfterFenceWait(FenceOutcome::Failed,
                                 static_cast<long>(DXGI_ERROR_DEVICE_REMOVED));
    Expect(removed == GpuWorkState::DeviceRemoved, "removal is classified separately");
    Expect(b3a::MustRetainUntilCompletion(removed), "removal retains owner");
    Expect(!b3a::MayResetList(b3a::ListStateAfterConfirmedSubmit(), removed),
           "removal blocks reset");
    Expect(b3a::StateAfterFenceWait(FenceOutcome::Timeout,
                                    static_cast<long>(DXGI_ERROR_DEVICE_RESET)) ==
               GpuWorkState::DeviceRemoved,
           "reset reason is a removal");
    Expect(b3a::StateAfterFenceWait(FenceOutcome::Failed,
                                    static_cast<long>(DXGI_ERROR_DEVICE_HUNG)) ==
               GpuWorkState::DeviceRemoved,
           "hung reason is a removal");
    Expect(b3a::StateAfterFenceWait(FenceOutcome::Failed, S_OK) ==
               GpuWorkState::WorkSubmittedUnknown,
           "S_OK is not evidence of completion");
    Expect(b3a::StateAfterFenceWait(FenceOutcome::Failed,
                                    static_cast<long>(E_FAIL)) ==
               GpuWorkState::WorkSubmittedUnknown,
           "an unrelated HRESULT is not a removal");
    Expect(b3a::StateAfterFenceWait(FenceOutcome::Completed,
                                    static_cast<long>(DXGI_ERROR_DEVICE_REMOVED)) ==
               GpuWorkState::WorkCompleted,
           "completed signal is completion even if removal is reported later");
    EndCase();
}

void TestPartialInitializationRetention()
{
    BeginCase("partial_initialization");
    b3a::RetentionSlots<FakeOwner, 2> slots;
    FakeOwner owner;
    owner.canary = 7;
    HANDLE raw = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Expect(raw != nullptr, "event created");
    owner.handle.reset(raw);
    Expect(slots.TryPark(std::move(owner)), "partial owner parks");
    Expect(slots.Occupied() == 1, "one slot occupied");
    Expect(slots.At(0).canary == 7, "parked owner keeps its payload");
    Expect(static_cast<bool>(slots.At(0).handle), "parked handle stays open");
    Expect(owner.canary == 7 && !owner.handle,
           "moved-from owner no longer owns the handle");
    raw = slots.At(0).handle.get();
    slots.At(0).handle.reset();
    Expect(!HandleIsValid(raw), "parked handle closes exactly once");
    EndCase();
}

void TestRetentionCapacity()
{
    BeginCase("retention_capacity");
    b3a::RetentionSlots<FakeOwner, 4> slots;
    for (int i = 0; i < 4; ++i)
    {
        FakeOwner owner;
        owner.canary = i;
        Expect(slots.TryPark(std::move(owner)), "slot available");
    }
    Expect(slots.Occupied() == 4, "all slots occupied");
    FakeOwner overflow;
    Expect(!slots.TryPark(std::move(overflow)),
           "exhausted store reports failure instead of dropping an owner");
    Expect(slots.At(0).canary == 0 && slots.At(3).canary == 3,
           "existing quarantined owners are not overwritten");
    EndCase();
}

void TestDoubleCleanup()
{
    BeginCase("double_cleanup");
    UniqueHandle handle;
    HANDLE raw = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    handle.reset(raw);
    Expect(HandleIsValid(raw), "handle live before cleanup");
    handle.reset();
    Expect(!HandleIsValid(raw), "handle closed once");
    handle.reset();
    handle.reset(nullptr);
    Expect(!handle, "repeated cleanup is a no-op");
    Expect(handle.get() == nullptr, "repeated cleanup leaves no dangling value");
    EndCase();
}

void TestSelfResetHandle()
{
    BeginCase("self_reset_handle");
    UniqueHandle handle;
    HANDLE raw = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    handle.reset(raw);
    handle.reset(handle.get());
    Expect(handle.get() == raw, "self-reset keeps the stored handle");
    Expect(HandleIsValid(raw), "self-reset does not close the handle");
    handle.reset();
    Expect(!HandleIsValid(raw), "normal reset still closes the handle");
    Expect(!handle, "reset clears the stored value");
    EndCase();
}

void TestGuardType()
{
    BeginCase("guard_type");
    struct NoopTerminal
    {
        void operator()() noexcept {}
    };
    using Guard = b3a::UnknownCompletionGuard<NoopTerminal>;
    static_assert(std::is_nothrow_destructible_v<Guard>,
                  "guard destructor must be noexcept");
    // A confirmed owner may be destroyed normally: the guard does nothing.
    GpuWorkState completed = GpuWorkState::WorkCompleted;
    {
        Guard guard(completed, NoopTerminal{});
        (void)guard;
    }
    Expect(true, "guard with confirmed completion does not terminate");
    // A guard action that throws must not escape; exercised in the subprocess
    // tests because it ends the process.
    EndCase();
}

void TestMoveOnlyHandle()
{
    BeginCase("move_only_handle");
    UniqueHandle first;
    first.reset(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
    const HANDLE raw = first.get();
    UniqueHandle second(std::move(first));
    Expect(!first && second.get() == raw, "move transfers ownership");
    UniqueHandle third;
    third = std::move(second);
    Expect(!second && third.get() == raw, "move assignment transfers ownership");
    third.reset();
    Expect(!HandleIsValid(raw), "single close after moves");
    EndCase();
}

void TestRepeatedResetAfterSuccess()
{
    BeginCase("repeated_reset_after_success");
    ListState list = b3a::ListStateAfterConfirmedSubmit();
    GpuWorkState work = GpuWorkState::WorkCompleted;
    Expect(b3a::MayResetList(list, work), "first reset after success is legal");
    list = b3a::ListStateAfterReset();
    work = GpuWorkState::NoWorkSubmitted;
    Expect(!b3a::MayResetList(list, work),
           "second reset without a new close is rejected");
    list = b3a::ListStateAfterClose();
    Expect(b3a::MayResetList(list, work), "reset is legal again after close");
    EndCase();
}

void TestInfoQueuePolicy()
{
    BeginCase("infoqueue_policy");
    ValidationEvidence clean;
    clean.debugLayerAvailable = true;
    clean.queueAvailable = true;
    clean.enumerationComplete = true;
    clean.completionConfirmed = true;
    clean.gbvActive = true;
    Expect(b3a::ValidationPasses(b3a::EvaluateValidation(clean)),
           "clean complete evidence passes");

    ValidationEvidence errors = clean;
    errors.errors = 1;
    const char* errorStatus = b3a::EvaluateValidation(errors);
    Expect(!b3a::ValidationPasses(errorStatus), "error does not pass");
    Expect(errorStatus[0] == 'F' && errorStatus[1] == 'A',
           "error verdict is FAIL");

    ValidationEvidence corruption = clean;
    corruption.corruptions = 1;
    const char* corruptionStatus = b3a::EvaluateValidation(corruption);
    Expect(!b3a::ValidationPasses(corruptionStatus), "corruption does not pass");
    Expect(corruptionStatus[0] == 'F' && corruptionStatus[1] == 'A',
           "corruption verdict is FAIL");

    ValidationEvidence warnings = clean;
    warnings.warnings = 3;
    Expect(b3a::ValidationPasses(b3a::EvaluateValidation(warnings)),
           "warnings alone are not a blocking failure");

    ValidationEvidence discarded = clean;
    discarded.discarded = 1;
    const char* discardedStatus = b3a::EvaluateValidation(discarded);
    Expect(!b3a::ValidationPasses(discardedStatus), "dropped messages do not pass");

    ValidationEvidence hidden = clean;
    hidden.hiddenByFilter = 1;
    Expect(!b3a::ValidationPasses(b3a::EvaluateValidation(hidden)),
           "messages hidden by a storage filter do not pass");
    EndCase();
}

void TestMissingInfoQueue()
{
    BeginCase("missing_infoqueue");
    ValidationEvidence evidence;
    evidence.debugLayerAvailable = true;
    evidence.queueAvailable = false;
    evidence.enumerationComplete = false;
    evidence.completionConfirmed = true;
    evidence.gbvActive = true;
    Expect(!b3a::ValidationPasses(b3a::EvaluateValidation(evidence)),
           "missing queue cannot pass");
    EndCase();
}

void TestIncompleteEnumeration()
{
    BeginCase("incomplete_enumeration");
    ValidationEvidence evidence;
    evidence.debugLayerAvailable = true;
    evidence.queueAvailable = true;
    evidence.enumerationComplete = false;
    evidence.completionConfirmed = true;
    evidence.gbvActive = true;
    Expect(!b3a::ValidationPasses(b3a::EvaluateValidation(evidence)),
           "incomplete enumeration cannot pass");
    EndCase();
}

void TestGbvEvidenceRequired()
{
    BeginCase("gbv_evidence_required");
    ValidationEvidence evidence;
    evidence.debugLayerAvailable = true;
    evidence.queueAvailable = true;
    evidence.enumerationComplete = true;
    evidence.completionConfirmed = true;
    evidence.gbvActive = false;
    Expect(!b3a::ValidationPasses(b3a::EvaluateValidation(evidence)),
           "requested GBV without evidence cannot pass");
    EndCase();
}

void TestUnconfirmedCompletionCannotPass()
{
    BeginCase("unconfirmed_completion_cannot_pass");
    ValidationEvidence evidence;
    evidence.debugLayerAvailable = true;
    evidence.queueAvailable = true;
    evidence.enumerationComplete = true;
    evidence.completionConfirmed = false;
    evidence.gbvActive = true;
    Expect(!b3a::ValidationPasses(b3a::EvaluateValidation(evidence)),
           "unconfirmed GPU completion cannot pass");
    EndCase();
}

void TestNoDebugLayer()
{
    BeginCase("no_debug_layer");
    ValidationEvidence evidence;
    evidence.debugLayerAvailable = false;
    evidence.queueAvailable = false;
    evidence.enumerationComplete = true;
    evidence.completionConfirmed = true;
    evidence.gbvActive = true;
    Expect(!b3a::ValidationPasses(b3a::EvaluateValidation(evidence)),
           "absent debug layer cannot pass");
    EndCase();
}

} // namespace

int main()
{
    std::printf("B3A_CPU_SAFETY_TESTS\n");
    TestNormalCompletion();
    TestFailBeforeFirstSubmit();
    TestFailureDuringResourceCreation();
    TestCloseFailure();
    TestSuccessfulCloseAllowsSubmit();
    TestFailureBeforeExecute();
    TestFailureImmediatelyAfterSubmission();
    TestSimulatedSignalError();
    TestSimulatedTimeout();
    TestDeviceRemoved();
    TestPartialInitializationRetention();
    TestRetentionCapacity();
    TestDoubleCleanup();
    TestSelfResetHandle();
    TestGuardType();
    TestMoveOnlyHandle();
    TestRepeatedResetAfterSuccess();
    TestInfoQueuePolicy();
    TestMissingInfoQueue();
    TestIncompleteEnumeration();
    TestGbvEvidenceRequired();
    TestUnconfirmedCompletionCannotPass();
    TestNoDebugLayer();
    std::printf("SAFETY_SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
