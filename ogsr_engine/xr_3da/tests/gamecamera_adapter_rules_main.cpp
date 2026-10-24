// F4.0-B2.3 narrow CPU tests for adapter diagnostics and dump publication gate.
#include "../GameCameraCaptureDiagnostics.h"

#include <cstdio>

namespace
{
using gamecamera12::CameraError12;

struct TestContext
{
    int passed = 0;
    int failed = 0;

    void Check(bool condition, const char* name)
    {
        if (condition)
        {
            ++passed;
            std::printf("[PASS] %s\n", name);
        }
        else
        {
            ++failed;
            std::printf("[FAIL] %s\n", name);
        }
    }
};

gamecamera12::CaptureToken12 MakeToken(uint32_t frame, uint64_t sequence,
                                       uint64_t session, uint64_t epoch)
{
    gamecamera12::CaptureToken12 token{};
    token.sourceGameFrame = frame;
    token.captureSequence = sequence;
    token.levelSession = session;
    token.captureEpoch = epoch;
    return token;
}

gamecamera12::CameraCaptureInput12 MakeInput(
    const gamecamera12::CaptureToken12& token)
{
    gamecamera12::CameraCaptureInput12 input{};
    input.sourceGameFrame = token.sourceGameFrame;
    input.captureSequence = token.captureSequence;
    input.levelSession = token.levelSession;
    input.captureEpoch = token.captureEpoch;
    return input;
}

void TestErrorSelection(TestContext& ctx)
{
    gamecamera12::CameraCaptureResult12 result{};
    result.camera.reset();

    const CameraError12 errors[] = {
        CameraError12::InvalidView,
        CameraError12::InvalidProjection,
        CameraError12::InvalidPosition,
        CameraError12::InconsistentFullTransform,
        CameraError12::InvalidViewport,
        CameraError12::StaleCaptureEpoch,
        CameraError12::StaleLevelSession,
    };
    for (const CameraError12 error : errors)
    {
        result.error = error;
        ctx.Check(b2camera_diagnostics::SelectCaptureError(result) == error,
                  "concrete CameraError12 preserved");
    }

    result.error = CameraError12::None;
    result.camera.reset();
    ctx.Check(b2camera_diagnostics::SelectCaptureError(result) ==
                  CameraError12::InvalidCamera,
              "error=None/empty-camera becomes InvalidCamera");
}

void TestCommandLineArguments(TestContext& ctx)
{
    const char* capture = "-b2_camera_capture";
    const char* dump = "-b2_camera_dump";

    ctx.Check(!b2camera_diagnostics::HasExactArgument(nullptr, capture),
              "null params does not enable capture");
    ctx.Check(!b2camera_diagnostics::HasExactArgument("", capture),
              "empty params does not enable capture");
    ctx.Check(b2camera_diagnostics::HasExactArgument(capture, capture),
              "exact capture argument is accepted");
    ctx.Check(b2camera_diagnostics::HasExactArgument(dump, dump),
              "exact dump argument is accepted");
    ctx.Check(!b2camera_diagnostics::HasExactArgument(dump, capture),
              "dump alone does not enable capture");
    ctx.Check(b2camera_diagnostics::HasExactArgument(
                  "  -b2_camera_capture  ", capture),
              "leading and trailing spaces are ignored");
    ctx.Check(b2camera_diagnostics::HasExactArgument(
                  "\t-b2_camera_dump\r\n-b2_camera_capture\t", capture),
              "tab and CRLF separated arguments are parsed");
    ctx.Check(b2camera_diagnostics::HasExactArgument(
                  "-b2_camera_capture\t-b2_camera_dump", capture),
              "both flags are independently recognized");
    ctx.Check(!b2camera_diagnostics::HasExactArgument(
                  "-b2_camera_capture_disabled", capture),
              "prefix collision is rejected");
    ctx.Check(!b2camera_diagnostics::HasExactArgument(
                  "-b2_camera_capture_extra", capture),
              "suffix collision is rejected");
    ctx.Check(!b2camera_diagnostics::HasExactArgument(
                  "-b2_camera_capture=0", capture),
              "explicit =0 is rejected");
    ctx.Check(!b2camera_diagnostics::HasExactArgument(
                  "-b2_camera_capture=1", capture),
              "explicit =1 is rejected");
    ctx.Check(!b2camera_diagnostics::HasExactArgument(
                  "--something=-b2_camera_capture", capture),
              "embedded occurrence is rejected");
    ctx.Check(!b2camera_diagnostics::HasExactArgument(
                  "\"-b2_camera_capture_disabled\"", capture),
              "quoted unrelated argument is rejected");
    ctx.Check(b2camera_diagnostics::HasExactArgument(
                  "\"-b2_camera_capture\"", capture),
              "quoted standalone flag is accepted");
    ctx.Check(b2camera_diagnostics::HasExactArgument(
                  "\"-b2_camera_dump\"", dump),
              "quoted standalone dump flag is accepted");
    ctx.Check(!b2camera_diagnostics::HasExactArgument(
                  "\"unrelated -b2_camera_capture\"", capture),
              "flag inside quoted argument is rejected");
    ctx.Check(!b2camera_diagnostics::HasExactArgument(
                  "\"-b2_camera_capture\"extra", capture),
              "suffix after quoted flag is rejected");
    ctx.Check(b2camera_diagnostics::HasExactArgument(
                  "-b2_camera_capture -b2_camera_capture", capture),
              "repeated exact flag is accepted");
    ctx.Check(b2camera_diagnostics::HasExactArgument(
                  "-b2_camera_capture", capture),
              "exact unquoted flag is accepted");
    ctx.Check(!b2camera_diagnostics::HasExactArgument(
                  "-B2_CAMERA_CAPTURE", capture),
              "case mismatch is rejected");
    ctx.Check(!b2camera_diagnostics::HasExactArgument(
                  "\"-b2_camera_capture", capture),
              "unterminated quote is rejected");
    ctx.Check(b2camera_diagnostics::HasExactArgument(
                  "foo \"bar\" -b2_camera_dump", dump),
              "exact flag after unrelated quoted argument is accepted");
    ctx.Check(b2camera_diagnostics::HasExactArgument(
                  "-b2_camera_samples", "-b2_camera_samples"),
              "exact camera samples flag is accepted");
    ctx.Check(!b2camera_diagnostics::HasExactArgument(
                  "-b2_camera_samples_extra", "-b2_camera_samples"),
              "camera samples prefix collision is rejected");
}

void TestDumpGate(TestContext& ctx)
{
    b2camera_diagnostics::MatrixDumpGate gate;
    const auto tokenA = MakeToken(101, 1, 10, 2);
    const auto tokenB = MakeToken(102, 2, 10, 2);
    const auto tokenC = MakeToken(103, 3, 11, 3);

    gate.Reset(true);
    gate.OnCapture(MakeInput(tokenB), tokenB);
    ctx.Check(!gate.OnPublished(tokenA), "wrong token does not publish dump");
    ctx.Check(!gate.Dumped(), "wrong token does not mark session dumped");

    gate.OnCapture(MakeInput(tokenB), tokenB);
    ctx.Check(gate.OnPublished(tokenB), "successful publish enables dump");
    ctx.Check(gate.Dumped(), "first successful publish marks dumped");
    ctx.Check(!gate.OnPublished(tokenB),
              "second publish does not duplicate dump");

    gate.Reset(true);
    gate.OnCapture(MakeInput(tokenA), tokenA);
    gate.ClearPending();
    ctx.Check(!gate.OnPublished(tokenA),
              "cleared pending capture cannot dump");

    gate.Reset(true);
    gate.OnCapture(MakeInput(tokenA), tokenA);
    gate.OnCapture(MakeInput(tokenB), tokenB);
    ctx.Check(gate.OnPublished(tokenB),
              "newest capture token dumps after publish");
    ctx.Check(gate.Input().sourceGameFrame == tokenB.sourceGameFrame,
              "dump gate owns the published frame input");

    gate.Reset(false);
    gate.OnCapture(MakeInput(tokenC), tokenC);
    ctx.Check(!gate.OnPublished(tokenC),
              "runtime dump disabled produces no dump");
    ctx.Check(!gate.Dumped(),
              "disabled dump does not mark session documented");

    gate.Reset(true);
    gate.OnCapture(MakeInput(tokenC), tokenC);
    ctx.Check(gate.OnPublished(tokenC),
              "new session enables a new dump");
    ctx.Check(gate.Dumped(), "new session dump is marked");
}
} // namespace

int main()
{
    TestContext ctx;
    std::printf("F4.0-B2.3 adapter rules CPU tests\n");
    TestErrorSelection(ctx);
    TestCommandLineArguments(ctx);
    TestDumpGate(ctx);
    std::printf("SUMMARY passed=%d failed=%d\n", ctx.passed, ctx.failed);
    return ctx.failed == 0 ? 0 : 1;
}
