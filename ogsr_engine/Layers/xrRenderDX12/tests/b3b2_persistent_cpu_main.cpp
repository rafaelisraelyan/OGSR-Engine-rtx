// F4.0-B3-B2B: CPU-only negative and lifecycle tests for the persistent
// consumer policy. No D3D12 device is created; GPU failures are exercised
// through the same pure completion/retention decisions used by the renderer.

#include "b3a_gpu_state.h"
#include "b3b2_camera_stream.h"
#include "b3b2_persistent_policy.h"
#include "dx12cameracapturefile.h"
#include "dx12gamecamera.h"
#include "dx12fencewait.h"

#include <Windows.h>
#include <DirectXMath.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using cameracapture12::CameraCaptureFile12;
using b3b2::InvalidateReason;
using b3b2::ReceiverValidationState;
namespace fs = std::filesystem;

namespace
{
int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* what)
{
    ++g_checks;
    if (!condition)
    {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

bool WriteMarker(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out << text;
    out.flush();
    out.close();
    return static_cast<bool>(out);
}

bool MarkerExists(const fs::path& path)
{
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec;
}

std::string ReadMarker(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::string text;
    char buffer[1024];
    while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0)
        text.append(buffer, static_cast<size_t>(in.gcount()));
    return text;
}

struct LifetimeOwner
{
    b3a::GpuWorkState state = b3a::GpuWorkState::NoWorkSubmitted;
    fs::path directory;

    ~LifetimeOwner()
    {
        try
        {
            WriteMarker(directory / "owner_destructor.txt",
                        "owner_destructor_called=1\n");
        }
        catch (...)
        {
        }
    }
};

int RunLifetimeChild(const std::string& mode, const fs::path& directory)
{
    std::error_code ec;
    fs::create_directories(directory, ec);
    LifetimeOwner owner;
    owner.directory = directory;
    auto terminalAction = [&]() noexcept {
        WriteMarker(directory / "terminal_action.txt",
                    "terminal_exit=90\n"
                    "gpu_release_before_completion=0\n");
        b3a::TerminalExit(b3a::kTerminalGpuExitCode);
    };
    b3a::UnknownCompletionGuard guard(owner.state, terminalAction);

    auto record = [&](const char* state) {
        WriteMarker(directory / "state_before_exit.txt", state);
    };

    try
    {
        if (mode == "geometry_signal_failure" ||
            mode == "geometry_arm_failure" ||
            mode == "geometry_wait_failure" ||
            mode == "geometry_timeout")
        {
            owner.state = b3a::StateAfterExecute();
            owner.state = b3a::GpuWorkState::WorkSubmittedUnknown;
            record("WorkSubmittedUnknown\n");
            return 0;
        }
        if (mode == "geometry_device_removed")
        {
            owner.state = b3a::GpuWorkState::DeviceRemoved;
            record("DeviceRemoved\n");
            return 0;
        }
        if (mode == "geometry_exception_after_execute")
        {
            owner.state = b3a::StateAfterExecute();
            record("WorkSubmittedUnknown\n");
            throw std::runtime_error("exception after ExecuteCommandLists");
        }
        if (mode == "geometry_success")
        {
            owner.state = b3a::StateAfterFenceWait(b3a::FenceOutcome::Completed, 0);
            record("WorkCompleted\n");
            return 0;
        }
        if (mode == "failed_close_before_submit" ||
            mode == "failure_before_gpu_resources")
        {
            owner.state = b3a::GpuWorkState::NoWorkSubmitted;
            record("NoWorkSubmitted\n");
            return 0;
        }
        if (mode == "repeated_frames_success")
        {
            for (int frame = 0; frame < 3; ++frame)
            {
                owner.state = b3a::StateAfterExecute();
                owner.state =
                    b3a::StateAfterFenceWait(b3a::FenceOutcome::Completed, 0);
            }
            record("WorkCompleted\n");
            return 0;
        }
    }
    catch (...)
    {
        // Return through the guard, which owns the unknown-completion policy.
        return 0;
    }
    record("UnknownMode\n");
    return 2;
}

bool RunLifetimeSubprocess(const char* mode, const char* expectedState,
                           int expectedExit, bool expectOwnerDestructor,
                           bool expectTerminalAction)
{
    wchar_t module[MAX_PATH] = {};
    if (::GetModuleFileNameW(nullptr, module, MAX_PATH) == 0)
        return false;
    const fs::path directory =
        fs::temp_directory_path() /
        (std::string("b3b2b_lifetime_") + mode + "_" +
         std::to_string(::GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(directory, ec);
    fs::create_directories(directory, ec);
    if (ec)
        return false;

    const std::wstring exe(module);
    const std::wstring modeW(mode, mode + std::strlen(mode));
    const std::wstring dirW = directory.wstring();
    std::wstring command = L"\"" + exe + L"\" --lifetime-child " + modeW +
                           L" \"" + dirW + L"\"";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr,
                          FALSE, 0, nullptr, nullptr, &startup, &process))
        return false;
    const DWORD wait = ::WaitForSingleObject(process.hProcess, 30000);
    DWORD exitCode = 0;
    if (wait == WAIT_OBJECT_0)
        ::GetExitCodeProcess(process.hProcess, &exitCode);
    else
        ::TerminateProcess(process.hProcess, 0xDEAD);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);

    const bool ownerCalled = MarkerExists(directory / "owner_destructor.txt");
    const bool terminalCalled = MarkerExists(directory / "terminal_action.txt");
    const std::string terminal = ReadMarker(directory / "terminal_action.txt");
    const std::string state = ReadMarker(directory / "state_before_exit.txt");
    const bool terminalContentOk =
        !expectTerminalAction ||
        (terminal.find("terminal_exit=90") != std::string::npos &&
         terminal.find("gpu_release_before_completion=0") != std::string::npos);
    const bool stateOk =
        expectedState == nullptr || state.find(expectedState) != std::string::npos;
    const bool ok = wait == WAIT_OBJECT_0 && exitCode == static_cast<DWORD>(
                                                          expectedExit) &&
                    ownerCalled == expectOwnerDestructor &&
                    terminalCalled == expectTerminalAction &&
                    terminalContentOk && stateOk;
    if (!ok)
    {
        std::printf(
            "LIFETIME_FAIL mode=%s wait=%lu exit=%lu expected_exit=%d "
            "owner=%d expected_owner=%d terminal=%d expected_terminal=%d "
            "state=%s expected_state=%s\n",
            mode, static_cast<unsigned long>(wait),
            static_cast<unsigned long>(exitCode), expectedExit,
            ownerCalled ? 1 : 0, expectOwnerDestructor ? 1 : 0,
            terminalCalled ? 1 : 0, expectTerminalAction ? 1 : 0,
            state.c_str(), expectedState != nullptr ? expectedState : "");
    }
    fs::remove_all(directory, ec);
    return ok;
}

gamecamera12::CameraCaptureInput12 MakeInput(uint64_t session, uint64_t epoch,
                                             uint64_t sequence)
{
    gamecamera12::CameraCaptureInput12 input{};
    const DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(
        DirectX::XMVectorSet(10.0f, 5.0f, -3.0f, 0.0f),
        DirectX::XMVectorSet(0.0f, 1.0f, 4.0f, 0.0f),
        DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    const DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XM_PIDIV4, 16.0f / 9.0f, 0.2f, 350.0f);
    DirectX::XMStoreFloat4x4(&input.view, view);
    DirectX::XMStoreFloat4x4(&input.projection, projection);
    DirectX::XMStoreFloat4x4(&input.fullTransform, view * projection);
    input.hasFullTransform = true;
    input.position = {10.0f, 5.0f, -3.0f};
    input.sourceGameFrame = static_cast<uint32_t>(sequence);
    input.captureSequence = sequence;
    input.levelSession = session;
    input.captureEpoch = epoch;
    input.viewportWidth = 1920;
    input.viewportHeight = 1080;
    return input;
}

CameraCaptureFile12 MakeCapture(uint64_t session = 2, uint64_t epoch = 9,
                                uint64_t sequence = 1)
{
    CameraCaptureFile12 capture{};
    std::snprintf(capture.levelName, sizeof(capture.levelName), "%s", "jupiter");
    capture.input = MakeInput(session, epoch, sequence);
    return capture;
}

bool AcceptHello(ReceiverValidationState& state, uint64_t generation,
                 uint32_t pid, uint64_t start, uint64_t session, uint64_t epoch)
{
    state.BeginConnection(generation);
    b3b2::HelloPayload hello{pid, start, session, epoch};
    std::string reason;
    return state.ObserveHello(hello, reason);
}

void TestProtocolAndCameraNegatives()
{
    // 1 Invalid HELLO.
    std::vector<uint8_t> badHello(31, 0);
    b3b2::HelloPayload hello{};
    Check(!b3b2::ParseHello(badHello, hello), "invalid HELLO rejected");

    // 2 Invalid camera CRC.
    CameraCaptureFile12 capture = MakeCapture();
    std::vector<uint8_t> wire;
    cameracapture12::FileError encodeError = cameracapture12::FileError::None;
    Check(cameracapture12::Encode(capture, wire, encodeError),
          "valid camera encodes for CRC test");
    if (!wire.empty())
        wire[wire.size() - 1] ^= 0x5A;
    CameraCaptureFile12 decoded{};
    cameracapture12::FileError decodeError = cameracapture12::FileError::None;
    Check(!cameracapture12::Decode(wire, decoded, decodeError) &&
              decodeError == cameracapture12::FileError::CrcMismatch,
          "camera CRC mismatch rejected");

    // 3 Invalid level name.
    Check(!b3b2b::LevelNamesEqual("zaton", "jupiter"),
          "invalid level name rejected");
    Check(b3b2b::LevelNamesEqual("JUPITER", "jupiter"),
          "level name comparison is case-insensitive");

    ReceiverValidationState validation;
    Check(AcceptHello(validation, 1, 10, 0x100, 2, 9),
          "validation HELLO accepted");
    std::string reason;
    Check(validation.ObserveCamera(MakeCapture(2, 9, 1), reason),
          "first camera accepted");
    Check(validation.ObserveCamera(MakeCapture(2, 9, 3), reason),
          "sequence gap accepted");

    // 4 Reordered sequence.
    Check(!validation.ObserveCamera(MakeCapture(2, 9, 2), reason),
          "reordered sequence rejected");

    // 5 Duplicate sequence.
    Check(!validation.ObserveCamera(MakeCapture(2, 9, 3), reason),
          "duplicate sequence rejected");

    // 6 Stale session.
    Check(!validation.ObserveCamera(MakeCapture(1, 9, 4), reason),
          "stale session rejected");

    // 7 Stale epoch.
    Check(!validation.ObserveCamera(MakeCapture(2, 8, 4), reason),
          "stale epoch rejected");

    // 8 Invalidation before render.
    Check(validation.ObserveInvalidate({2, 10, InvalidateReason::Reset}, reason),
          "invalidation before render accepted");
    Check(!validation.ObserveCamera(MakeCapture(2, 9, 4), reason),
          "camera invalidated before render is rejected");
    Check(validation.ObserveCamera(MakeCapture(2, 10, 1), reason),
          "post-invalidation camera accepted");

    // 12 Receiver reconnect / new producer process generation.
    ReceiverValidationState reconnected;
    Check(AcceptHello(reconnected, 1, 10, 0x100, 1, 1),
          "first producer generation accepted");
    Check(reconnected.ObserveCamera(MakeCapture(1, 1, 1), reason),
          "first producer camera accepted");
    Check(AcceptHello(reconnected, 2, 11, 0x200, 1, 1),
          "new producer generation HELLO accepted");
    Check(reconnected.ObserveCamera(MakeCapture(1, 1, 1), reason),
          "reused sequence from new producer accepted");
}

void TestStrictFenceAndAcceptance()
{
    struct FenceCase
    {
        long reason;
        bool expected;
        const char* name;
    };
    const FenceCase cases[] = {
        {0, true, "S_OK"},
        {1, false, "S_FALSE"},
        {static_cast<long>(0x80004005u), false, "E_FAIL"},
        {static_cast<long>(0x887A0005u), false, "DEVICE_REMOVED"},
        {static_cast<long>(0x887A0006u), false, "DEVICE_HUNG"},
        {static_cast<long>(0x887A0007u), false, "DEVICE_RESET"},
        {static_cast<long>(0x887A0020u), false, "DRIVER_INTERNAL_ERROR"},
    };
    for (const FenceCase& test : cases)
    {
        const b3b2b::FenceEvidence evidence = b3b2b::EvaluateFence(
            true, true, true, 7, 7, test.reason);
        Check(evidence.confirmed == test.expected,
              test.expected ? "S_OK fence confirms completion"
                            : "non-S_OK fence is not completion");
    }
    Check(!b3b2b::IsDeviceRemovedReason(0),
          "S_OK is not a device-removal reason");
    Check(!b3b2b::IsDeviceRemovedReason(1),
          "S_FALSE is not treated as device removal");
    Check(!b3b2b::IsDeviceRemovedReason(static_cast<long>(0x80004005u)),
          "E_FAIL is not treated as a known device-removal code");
    Check(b3b2b::IsDeviceRemovedReason(static_cast<long>(0x887A0005u)),
          "DEVICE_REMOVED is classified as device removal");
    Check(b3b2b::ClassifyFenceFailure(1) ==
              b3b2b::FenceFailureDisposition::UnknownCompletion,
          "S_FALSE fence failure stays UnknownCompletion");
    Check(b3b2b::ClassifyFenceFailure(static_cast<long>(0x80004005u)) ==
              b3b2b::FenceFailureDisposition::UnknownCompletion,
          "E_FAIL fence failure stays UnknownCompletion");
    Check(b3b2b::ClassifyFenceFailure(static_cast<long>(0x887A0005u)) ==
              b3b2b::FenceFailureDisposition::DeviceRemoved,
          "DEVICE_REMOVED fence failure is DeviceRemoved");
    Check(!b3b2b::EvaluateFence(true, true, true, 7, 8, 0).confirmed,
          "pending fence is not completion");
    Check(!b3b2b::EvaluateFence(true, true, true,
                                0xFFFFFFFFFFFFFFFFull, 7, 0)
               .confirmed,
          "UINT64_MAX fence is not completion");
    Check(!b3b2b::EvaluateFence(true, true, false, 7, 7, 0).confirmed,
          "spurious wait is not completion");
    Check(!b3b2b::EvaluateFence(false, true, true, 7, 7, 0).confirmed,
          "signal failure is not completion");
    Check(!b3b2b::EvaluateFence(true, false, false, 7, 7, 0).confirmed,
          "SetEventOnCompletion failure is not completion");
    Check(!b3b2b::EvaluateFence(true, true, false, 7, 7, 0).confirmed,
          "wait timeout is not completion");

    Check(std::strcmp(b3b2b::EvaluateAcceptance(true, "PASS", true, true),
                      "PASS") == 0,
          "GPU+validation+evidence+visibility passes");
    Check(std::strcmp(b3b2b::EvaluateAcceptance(true, "PASS", false, true),
                      "UNVERIFIED") == 0,
          "evidence failure is UNVERIFIED");
    Check(std::strcmp(b3b2b::EvaluateAcceptance(true, "PASS", true, false),
                      "UNVERIFIED") == 0,
          "missing visibility is UNVERIFIED");
    Check(std::strcmp(b3b2b::EvaluateAcceptance(true, "UNVERIFIED", true, true),
                      "UNVERIFIED") == 0,
          "unverified validation is UNVERIFIED");
    Check(std::strcmp(b3b2b::EvaluateAcceptance(true, "FAIL", true, true),
                      "FAIL") == 0,
          "validation failure is FAIL");
    Check(std::strcmp(b3b2b::EvaluateAcceptance(false, "PASS", true, true),
                      "FAIL") == 0,
          "GPU failure is FAIL");
    Check(b3b2b::AcceptanceExitCode("PASS") == 0,
          "acceptance PASS exit code is 0");
    Check(b3b2b::AcceptanceExitCode("UNVERIFIED") == 3,
          "acceptance UNVERIFIED exit code is 3");
    Check(b3b2b::AcceptanceExitCode("FAIL") == 1,
          "acceptance FAIL exit code is 1");
}

void TestLifetimeSubprocesses()
{
    Check(RunLifetimeSubprocess("geometry_signal_failure",
                                "WorkSubmittedUnknown", 90, false, true),
          "geometry signal failure retains GPU owner and exits 90");
    Check(RunLifetimeSubprocess("geometry_arm_failure",
                                "WorkSubmittedUnknown", 90, false, true),
          "geometry SetEvent failure retains GPU owner and exits 90");
    Check(RunLifetimeSubprocess("geometry_wait_failure",
                                "WorkSubmittedUnknown", 90, false, true),
          "geometry wait failure retains GPU owner and exits 90");
    Check(RunLifetimeSubprocess("geometry_timeout", "WorkSubmittedUnknown", 90,
                                false, true),
          "geometry fence timeout retains GPU owner and exits 90");
    Check(RunLifetimeSubprocess("geometry_device_removed", "DeviceRemoved", 90,
                                false, true),
          "geometry device removal retains GPU owner and exits 90");
    Check(RunLifetimeSubprocess("geometry_exception_after_execute",
                                "WorkSubmittedUnknown", 90, false, true),
          "exception after Execute retains GPU owner and exits 90");
    Check(RunLifetimeSubprocess("geometry_success", "WorkCompleted", 0, true,
                                false),
          "confirmed geometry completion allows ordinary cleanup");
    Check(RunLifetimeSubprocess("failed_close_before_submit",
                                "NoWorkSubmitted", 0, true, false),
          "failed Close before submit allows ordinary cleanup");
    Check(RunLifetimeSubprocess("failure_before_gpu_resources",
                                "NoWorkSubmitted", 0, true, false),
          "failure before GPU resources allows ordinary cleanup");
    Check(RunLifetimeSubprocess("repeated_frames_success", "WorkCompleted", 0,
                                true, false),
          "repeated confirmed frames allow ordinary cleanup");
}

void TestGpuPolicyNegatives()
{
    // 9/10/26 Invalidation with completed/in-flight GPU work and shutdown.
    Check(b3b2b::MayReset(b3b2b::FrameState::Completed, true),
          "completed frame may be reset");
    Check(!b3b2b::MayReset(b3b2b::FrameState::Submitted, false),
          "in-flight frame may not be reset");
    Check(b3b2b::MustRetain(b3b2b::FrameState::Submitted),
          "in-flight frame resources retained");
    Check(b3b2b::MustRetain(b3b2b::FrameState::CompletionUnknown),
          "unknown completion resources retained");
    Check(!b3b2b::MaySubmit(b3b2b::FrameState::Submitted),
          "submitted frame cannot be re-submitted");
    Check(!b3b2b::MaySubmit(b3b2b::FrameState::Failed),
          "failed frame cannot be submitted");

    // 13 Unexpected viewport change.
    Check(!b3b2b::NeedsTargetRecreate(800, 600, 800, 600, true),
          "same viewport does not recreate targets");
    Check(b3b2b::NeedsTargetRecreate(800, 600, 1024, 768, true),
          "changed viewport recreates after completion");
    Check(!b3b2b::NeedsTargetRecreate(800, 600, 1024, 768, false),
          "changed viewport blocked while GPU completion is unknown");

    // 14/15/16 Failed device, geometry and command-list close.
    const b3b2b::FrameState failed = b3b2b::FrameState::Failed;
    Check(!b3b2b::MaySubmit(failed), "failed device/geometry state cannot submit");
    const b3a::ListState closeFailed = b3a::ListStateAfterFailedClose();
    Check(!b3a::MaySubmit(closeFailed), "failed command-list Close cannot submit");
    Check(!b3a::MayResetList(closeFailed, b3a::GpuWorkState::WorkCompleted),
          "failed command-list Close cannot reset");

    // 17 Signal failure.
    b3b2b::FenceEvidence signal = b3b2b::EvaluateFence(
        false, true, true, 7, 7, 0);
    Check(!signal.confirmed, "signal failure is not completion");
    // 18 SetEventOnCompletion failure.
    b3b2b::FenceEvidence arm = b3b2b::EvaluateFence(
        true, false, false, 7, 7, 0);
    Check(!arm.confirmed, "SetEventOnCompletion failure is not completion");
    // 19 Fence timeout.
    b3b2b::FenceEvidence timeout = b3b2b::EvaluateFence(
        true, true, false, 6, 7, 0);
    Check(!timeout.confirmed, "fence timeout is not completion");
    // 20 UINT64_MAX completed value.
    b3b2b::FenceEvidence marker = b3b2b::EvaluateFence(
        true, true, true, 0xFFFFFFFFFFFFFFFFull, 7, 0);
    Check(!marker.confirmed, "UINT64_MAX is not completion");
    // 21 Device removed.
    b3b2b::FenceEvidence removed = b3b2b::EvaluateFence(
        true, true, true, 7, 7, 0x887A0005);
    Check(!removed.confirmed, "device removed is not completion");
    // Spurious wake.
    b3b2b::FenceEvidence spurious = b3b2b::EvaluateFence(
        true, true, true, 6, 7, 0);
    Check(!spurious.confirmed, "completed < expected is not completion");
    // Normal completion.
    b3b2b::FenceEvidence completed = b3b2b::EvaluateFence(
        true, true, true, 7, 7, 0);
    Check(completed.confirmed && std::strcmp(completed.reason, "Completed") == 0,
          "exact fence completion is confirmed");

    // 22 Failed readback / 25 evidence I/O failure: no positive pixel claim.
    b3b2b::SamplePlan sample = b3b2b::PlanSample(1, 10, false);
    Check(sample.sample && sample.first, "first frame is sampled");
    sample = b3b2b::PlanSample(10, 10, false);
    Check(sample.sample && sample.intermediate, "periodic frame is sampled");
    sample = b3b2b::PlanSample(25, 10, true);
    Check(sample.sample && sample.last, "last frame is sampled");
    Check(!b3b2b::PlanSample(2, 10, false).sample,
          "non-sampled frame is not claimed as readback verified");
    Check(std::strcmp(b3b2b::SampleEvidenceStatus(true, false, true, true),
                      "UNVERIFIED") == 0,
          "failed readback is UNVERIFIED, not a pixel claim");
    Check(std::strcmp(b3b2b::SampleEvidenceStatus(true, true, false, true),
                      "UNVERIFIED") == 0,
          "failed evidence I/O is UNVERIFIED");
    Check(std::strcmp(b3b2b::SampleEvidenceStatus(true, true, true, true),
                      "VISIBLE_DRAW_VERIFIED") == 0,
          "valid readback and evidence are verified");

    // 23 Incomplete InfoQueue / 24 GBV unavailable.
    b3a::ValidationEvidence evidence;
    evidence.debugLayerAvailable = true;
    evidence.queueAvailable = true;
    evidence.enumerationComplete = true;
    evidence.completionConfirmed = true;
    evidence.gbvActive = true;
    Check(std::strcmp(b3a::EvaluateValidation(evidence), "PASS") == 0,
          "complete clean GBV evidence passes");
    evidence.enumerationComplete = false;
    Check(std::strcmp(b3a::EvaluateValidation(evidence), "UNVERIFIED") == 0,
          "incomplete InfoQueue is UNVERIFIED");
    evidence.enumerationComplete = true;
    evidence.gbvActive = false;
    Check(std::strcmp(b3a::EvaluateValidation(evidence), "UNVERIFIED") == 0,
          "GBV unavailable is UNVERIFIED");
    evidence.gbvActive = true;
    evidence.errors = 1;
    Check(std::strcmp(b3a::EvaluateValidation(evidence), "FAIL") == 0,
          "D3D12 validation error is FAIL");
    evidence.errors = 0;
    evidence.discarded = 1;
    Check(std::strcmp(b3a::EvaluateValidation(evidence), "UNVERIFIED") == 0,
          "discarded InfoQueue message is UNVERIFIED");

    // 27/28 Exception before/after submission.
    Check(!b3b2b::MustRetain(b3b2b::FrameState::Idle),
          "exception before submission may release safely");
    Check(b3b2b::MustRetain(b3b2b::AfterSubmit()),
          "exception after submission retains GPU resources");
    Check(b3b2b::AfterConfirmedCompletion() == b3b2b::FrameState::Completed,
          "confirmed completion is Completed");
    Check(b3b2b::AfterFailedClose() == b3b2b::FrameState::Failed,
          "failed Close is Failed");

    // 11/12 Producer disconnect/reconnect logical generation.
    ReceiverValidationState reconnect;
    std::string reason;
    Check(AcceptHello(reconnect, 1, 42, 0x111, 1, 1),
          "disconnect/reconnect initial HELLO");
    Check(reconnect.ObserveCamera(MakeCapture(1, 1, 1), reason),
          "disconnect/reconnect initial camera");
    Check(AcceptHello(reconnect, 2, 42, 0x111, 1, 2),
          "reconnect HELLO with new epoch accepted");
    Check(reconnect.ObserveCamera(MakeCapture(1, 2, 1), reason),
          "reconnect camera accepted");
}

} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc >= 4 && std::strcmp(argv[1], "--lifetime-child") == 0)
        return RunLifetimeChild(argv[2], fs::path(argv[3]));
    std::printf("B3B2B_PERSISTENT_CPU_TESTS\n");
    TestProtocolAndCameraNegatives();
    TestStrictFenceAndAcceptance();
    TestLifetimeSubprocesses();
    TestGpuPolicyNegatives();
    std::printf("B3B2B_PERSISTENT_SUMMARY checks=%d failures=%d\n", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
