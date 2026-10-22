#include "Application.h"

#include "AuditPolicy.h"
#include "ImageIO.h"
#include "Validation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
std::string TimestampUtc()
{
    SYSTEMTIME now{};
    GetSystemTime(&now);
    char buffer[64]{};
    snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02uZ",
             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    return buffer;
}
const char* ValidationVerdictName(audit::ValidationVerdict verdict)
{
    switch (verdict)
    {
    case audit::ValidationVerdict::Pass: return "PASS";
    case audit::ValidationVerdict::Fail: return "FAIL";
    case audit::ValidationVerdict::Inconclusive: return "INCONCLUSIVE";
    default: return "NOT REQUESTED";
    }
}
} // namespace

Application::Application(AppOptions options)
    : options_(std::move(options))
{
    channel_ = 2;
    slice_ = static_cast<float>(options_.noise.resolution) * 0.5f;
    view_.resolution = options_.noise.resolution;
    view_.sliceIndex = slice_;
    view_.channel = channel_;
}

Application::~Application()
{
    Shutdown();
}

bool Application::Initialize()
{
    Log("=== OGSR Volumetric Clouds V0 (standalone D3D12) ===");
#ifdef _DEBUG
    Log("build: Debug");
#else
    Log("build: Release");
#endif
    Log("executable directory: %s",
        WideToUtf8(ExecutableDirectory().wstring()).c_str());

    if (!window_.Create(L"OGSR Облака V0", 1280, 720,
                        [this](UINT message, WPARAM wparam, LPARAM lparam)
                        {
                            OnWindowEvent(message, wparam, lparam);
                        }))
    {
        Fail("CreateWindowExW failed");
    }

    renderer_.Initialize(window_.Handle(), options_.renderer);
    noise_.Initialize(renderer_, options_.noise, options_.preferredFormat);
    viewer_.Initialize(renderer_);

    options_.noise = noise_.Params();
    view_.resolution = noise_.Resolution();
    slice_ = static_cast<float>(noise_.Resolution()) * 0.5f;
    view_.sliceIndex = slice_;

    Log("adapter: %s", renderer_.AdapterInfo().c_str());
    Log("noise format: %s", noise_.FormatName());
    Log("noise parameters: %s", options_.noise.Describe().c_str());
    UpdateTitle(true);

    if (renderer_.DebugErrorCount() != 0)
    {
        LogWarn("D3D12 debug layer reported %llu errors during initialization",
                static_cast<unsigned long long>(renderer_.DebugErrorCount()));
    }

    initialized_ = true;
    return true;
}

int Application::Run()
{
    if (!Initialize())
        return 1;

    const audit::ValidationStatus preflight = PreflightValidation();
    if (preflight.verdict == audit::ValidationVerdict::Inconclusive ||
        preflight.verdict == audit::ValidationVerdict::Fail)
    {
        int result = preflight.verdict == audit::ValidationVerdict::Inconclusive ? 3 : 1;
        const bool cleanShutdown = Shutdown();
        if (!cleanShutdown && result == 0)
            result = 2;
        Log("application finished with code %d", result);
        return result;
    }

    int result = 0;
    if (options_.selfTest || options_.capture)
    {
        if (options_.selfTest)
            result = std::max(result, RunSelfTest());
        if (options_.capture)
            result = std::max(result, RunCapture());
    }
    else
    {
        result = RunInteractive();
    }

    const bool cleanShutdown = Shutdown();
    if (!cleanShutdown && result == 0)
        result = 2;
    Log("application finished with code %d", result);
    return result;
}

audit::ValidationStatus Application::PreflightValidation()
{
    const audit::ValidationStatus status = audit::EvaluateValidationStatus(
        renderer_.DebugLayerRequested(), renderer_.DebugLayerEnabled(),
        renderer_.InfoQueueAvailable(), renderer_.GpuValidationRequested(),
        renderer_.GpuValidationEnabled(), renderer_.DebugErrorCount());

    Log("validation preflight: debug requested=%d layer=%d infoQueue=%d "
        "gpu requested=%d enabled=%d errors=%llu -> %s",
        renderer_.DebugLayerRequested() ? 1 : 0,
        renderer_.DebugLayerEnabled() ? 1 : 0,
        renderer_.InfoQueueAvailable() ? 1 : 0,
        renderer_.GpuValidationRequested() ? 1 : 0,
        renderer_.GpuValidationEnabled() ? 1 : 0,
        static_cast<unsigned long long>(renderer_.DebugErrorCount()),
        ValidationVerdictName(status.verdict));

    if (status.verdict == audit::ValidationVerdict::Inconclusive)
    {
        LogError("validation preflight INCONCLUSIVE: requested D3D12 validation "
                 "infrastructure is unavailable (debug layer, ID3D12InfoQueue, "
                 "or GPU-based validation)");
    }
    else if (status.verdict == audit::ValidationVerdict::Fail)
    {
        LogError("validation preflight FAIL: %llu debug layer errors",
                 static_cast<unsigned long long>(renderer_.DebugErrorCount()));
    }

    if (options_.selfTest &&
        (status.verdict == audit::ValidationVerdict::Inconclusive ||
         status.verdict == audit::ValidationVerdict::Fail))
    {
        WritePreflightReport(status);
    }

    return status;
}

void Application::WritePreflightReport(const audit::ValidationStatus& status)
{
    std::ostringstream report;
    report << "OGSR Clouds V0 selftest\n";
    report << "timestamp: " << TimestampUtc() << "\n";
    report << "adapter: " << renderer_.AdapterInfo() << "\n";
    report << "warp: " << (renderer_.UsingWarp() ? "yes" : "no") << "\n";
    report << "requested resolution: " << noise_.Resolution() << "\n";
    report << "debug layer requested: "
           << (renderer_.DebugLayerRequested() ? "yes" : "no") << "\n";
    report << "debug layer enabled: "
           << (renderer_.DebugLayerEnabled() ? "yes" : "no") << "\n";
    report << "ID3D12InfoQueue available: "
           << (renderer_.InfoQueueAvailable() ? "yes" : "no") << "\n";
    report << "gpu based validation requested: "
           << (renderer_.GpuValidationRequested() ? "yes" : "no") << "\n";
    report << "gpu based validation enabled: "
           << (renderer_.GpuValidationEnabled() ? "yes" : "no") << "\n";
    report << "validation performed: no\n";
    report << "validation verdict: " << ValidationVerdictName(status.verdict) << "\n";
    report << "debug errors: " << renderer_.DebugErrorCount() << "\n";
    report << "debug warnings: " << renderer_.DebugWarningCount() << "\n\n";
    report << (status.verdict == audit::ValidationVerdict::Inconclusive
                   ? "V0 SELFTEST: INCONCLUSIVE\n"
                   : "V0 SELFTEST: FAIL\n");

    Log("%s", report.str().c_str());
    const std::filesystem::path reportPath = ExecutableDirectory() / "selftest.txt";
    std::ofstream file(reportPath, std::ios::trunc);
    if (file)
        file << report.str();
    else
        LogWarn("could not write %s", reportPath.string().c_str());
}

bool Application::Shutdown()
{
    if (!initialized_)
    {
        window_.Destroy();
        return !renderer_.DeviceRemoved();
    }

    Log("shutting down");
    bool gpuIdle = true;
    // All consumer objects (PSO, root signature, SRV) are released below.
    // Wait for the GPU first so no in-flight draw can still reference them.
    if (renderer_.DeviceRemoved())
    {
        gpuIdle = false;
        LogWarn("shutdown: device already removed; best-effort cleanup");
    }
    else
    {
        try
        {
            renderer_.WaitForGpu();
        }
        catch (const std::exception& e)
        {
            gpuIdle = false;
            LogError("shutdown GPU wait failed: %s", e.what());
        }
    }

    if (gpuIdle)
        Log("shutdown: GPU idle; releasing resources");
    else
        LogWarn("shutdown: best-effort cleanup after unsuccessful GPU wait");

    Log("shutdown: slice viewer");
    viewer_.Shutdown();
    Log("shutdown: noise volume");
    noise_.Shutdown();
    Log("shutdown: renderer");
    renderer_.Shutdown();
    Log("shutdown: window");
    window_.Destroy();

    const bool clean = gpuIdle && !renderer_.DeviceRemoved();
    if (renderer_.DeviceRemoved())
    {
        LogError("shutdown: DEVICE REMOVAL reason %s",
                 HrToString(renderer_.LastDeviceRemovedReason()).c_str());
    }
    Log(clean ? "shutdown: complete" : "shutdown: complete (best-effort, GPU not idle)");
    initialized_ = false;
    return clean;
}

int Application::RunInteractive()
{
    window_.Show(SW_SHOW);
    UpdateWindow(window_.Handle());
    UpdateTitle(true);

    auto previous = std::chrono::steady_clock::now();
    bool wasMinimized = false;
    while (running_ && window_.PumpMessages())
    {
        if (window_.IsMinimized())
        {
            if (!wasMinimized)
            {
                Log("window minimized; rendering paused");
                wasMinimized = true;
            }
            Sleep(50);
            previous = std::chrono::steady_clock::now();
            continue;
        }
        if (wasMinimized)
        {
            Log("window restored; resuming rendering");
            wasMinimized = false;
        }

        UINT pendingWidth = 0;
        UINT pendingHeight = 0;
        if (window_.ConsumeResize(pendingWidth, pendingHeight))
            renderer_.Resize(pendingWidth, pendingHeight);

        if (renderer_.Width() == 0 || renderer_.Height() == 0)
        {
            Sleep(10);
            continue;
        }

        RegenerateIfNeeded();

        const auto now = std::chrono::steady_clock::now();
        float delta = std::chrono::duration<float>(now - previous).count();
        previous = now;
        delta = std::clamp(delta, 0.0f, 0.1f);

        if (animate_)
        {
            slice_ += delta * 18.0f;
            const float maximum = static_cast<float>(noise_.Resolution() - 1);
            while (slice_ > maximum)
                slice_ -= static_cast<float>(noise_.Resolution());
        }

        UpdateViewConstants();
        if (!RenderOneFrame(false, nullptr))
        {
            Sleep(1);
            continue;
        }

        ++renderedFrames_;
        UpdateTitle(false);

        if (options_.resizeTest)
        {
            // Exercise ResizeBuffers with two different client sizes while the
            // debug layer is active. The exact outer size is not important;
            // WM_SIZE supplies the real client dimensions.
            if (renderedFrames_ == 20)
            {
                Log("resize test: requesting 960x540 outer window");
                SetWindowPos(window_.Handle(), nullptr, 0, 0, 960, 540,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            else if (renderedFrames_ == 50)
            {
                Log("resize test: requesting 1280x800 outer window");
                SetWindowPos(window_.Handle(), nullptr, 0, 0, 1280, 800,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }

        if (options_.frames != 0 && renderedFrames_ >= options_.frames)
        {
            Log("frame limit reached (%llu frames)",
                static_cast<unsigned long long>(renderedFrames_));
            break;
        }
    }

    Log("interactive loop ended after %llu frames; debug errors=%llu warnings=%llu",
        static_cast<unsigned long long>(renderedFrames_),
        static_cast<unsigned long long>(renderer_.DebugErrorCount()),
        static_cast<unsigned long long>(renderer_.DebugWarningCount()));

    return renderer_.DebugErrorCount() == 0 ? 0 : 2;
}

bool Application::RenderOneFrame(bool capture, std::vector<uint8_t>* pixels)
{
    if (!noise_.IsInitialized())
        Fail("RenderOneFrame called before the noise volume was initialized");
    if (capture && !pixels)
        Fail("RenderOneFrame capture requested without an output buffer");

    Renderer::Frame frame = renderer_.BeginFrame();
    if (!frame.valid)
        return false;

    renderer_.BeginRenderPass(frame);
    viewer_.Draw(frame.list, frame.width, frame.height, view_, noise_.SrvGpuHandle());
    renderer_.EndRenderPass(frame);
    renderer_.Present(frame, capture ? pixels : nullptr);
    return true;
}

void Application::RegenerateIfNeeded()
{
    if (!noiseDirty_)
        return;

    Log("regenerating noise: %s", options_.noise.Describe().c_str());
    options_.noise.resolution = noise_.Resolution();
    noise_.Generate(options_.noise);
    view_.resolution = noise_.Resolution();
    slice_ = std::clamp(slice_, 0.0f,
                        static_cast<float>(noise_.Resolution() - 1));
    noiseDirty_ = false;
}

void Application::ChangeResolution(uint32_t resolution)
{
    if (resolution == noise_.Resolution())
        return;

    Log("changing noise resolution: %u^3 -> %u^3",
        noise_.Resolution(), resolution);
    options_.noise.resolution = resolution;
    noise_.ResizeVolume(resolution);
    noise_.Generate(options_.noise);
    view_.resolution = resolution;
    slice_ = static_cast<float>(resolution) * 0.5f;
    noiseDirty_ = false;
    UpdateTitle(true);
}

void Application::OnWindowEvent(UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_KEYDOWN)
    {
        // Never let a C++ exception escape a Win32 window procedure.
        try
        {
            HandleKey(wparam, lparam);
        }
        catch (const std::exception& e)
        {
            LogError("input action failed: %s", e.what());
            running_ = false;
            window_.Close();
        }
        catch (...)
        {
            LogError("input action failed with a non-standard exception");
            running_ = false;
            window_.Close();
        }
    }
    else if (message == WM_MOUSEWHEEL)
    {
        int steps = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
        if (steps == 0)
            steps = GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? 1 : -1;
        slice_ = std::clamp(
            slice_ + static_cast<float>(steps), 0.0f,
            static_cast<float>(noise_.Resolution() - 1));
        UpdateViewConstants();
        UpdateTitle(true);
    }
}

void Application::HandleKey(WPARAM key, LPARAM lparam)
{
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool repeat = (lparam & (1LL << 30)) != 0;
    const float maximumSlice = static_cast<float>(noise_.Resolution() - 1);

    const auto adjustUint = [&](uint32_t& value, int delta, uint32_t low,
                                uint32_t high, const char* name)
    {
        const int64_t candidate = static_cast<int64_t>(value) + delta;
        value = static_cast<uint32_t>(std::clamp<int64_t>(candidate, low, high));
        MarkNoiseDirty(name);
    };
    const auto adjustFloat = [&](float& value, float delta, float low,
                                 float high, const char* name)
    {
        value = std::clamp(value + delta, low, high);
        MarkNoiseDirty(name);
    };

    switch (key)
    {
    case VK_ESCAPE:
        running_ = false;
        window_.Close();
        return;

    case '1':
        channel_ = 0;
        break;
    case '2':
        channel_ = 1;
        break;
    case '3':
        channel_ = 2;
        break;

    case VK_UP:
    case VK_RIGHT:
        slice_ = std::min(maximumSlice, slice_ + 1.0f);
        break;
    case VK_DOWN:
    case VK_LEFT:
        slice_ = std::max(0.0f, slice_ - 1.0f);
        break;

    case VK_SPACE:
        if (!repeat)
            animate_ = !animate_;
        break;
    case 'R':
        if (!repeat)
        {
            slice_ = maximumSlice * 0.5f;
            animate_ = false;
        }
        break;
    case 'D':
        if (!repeat)
            diagnostic_ = !diagnostic_;
        break;
    case 'N':
        if (!repeat)
        {
            Log("manual noise regeneration requested");
            noiseDirty_ = true;
        }
        break;
    case '4':
        if (!repeat)
        {
            ChangeResolution(noise_.Resolution() == 128 ? 64 : 128);
            return;
        }
        break;

    case 'G':
        options_.noise.seed += shift ? static_cast<uint32_t>(-1) : 1u;
        MarkNoiseDirty("seed");
        break;
    case 'F':
        adjustUint(options_.noise.baseFrequency, shift ? -1 : 1, 1, 64,
                   "base frequency");
        break;
    case 'O':
        adjustUint(options_.noise.octaves, shift ? -1 : 1, 1, 8, "octaves");
        break;
    case 'W':
        adjustUint(options_.noise.worleyFrequency, shift ? -1 : 1, 1, 128,
                   "Worley frequency");
        break;
    case 'B':
        adjustFloat(options_.noise.blend, shift ? -0.05f : 0.05f, 0.0f, 1.0f,
                    "blend");
        break;
    case 'E':
        adjustFloat(options_.noise.erosion, shift ? -0.05f : 0.05f, 0.0f, 1.0f,
                    "erosion");
        break;
    case 'K':
        adjustFloat(options_.noise.gain, shift ? -0.05f : 0.05f, 0.1f, 0.9f,
                    "gain");
        break;

    case 'H':
    case VK_F1:
        if (!repeat)
            PrintHelp();
        return;

    default:
        return;
    }

    UpdateViewConstants();
    UpdateTitle(true);
}

void Application::UpdateViewConstants()
{
    view_.sliceIndex = slice_;
    view_.channel = channel_;
    view_.diagnostic = diagnostic_ ? 1u : 0u;
    view_.resolution = noise_.Resolution();
    view_.time = static_cast<float>(renderedFrames_) * 0.016f;
}

void Application::UpdateTitle(bool force)
{
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - lastTitleUpdate_ < std::chrono::milliseconds(100))
        return;
    lastTitleUpdate_ = now;

    const uint32_t resolution = noise_.IsInitialized()
        ? noise_.Resolution()
        : options_.noise.resolution;
    const int z = std::clamp(
        static_cast<int>(std::lround(slice_)), 0,
        static_cast<int>(resolution) - 1);

    wchar_t title[512]{};
    swprintf_s(
        title,
        L"OGSR Облака V0 | %s | Z %d/%u | %u^3 | seed %u | F%u O%u W%u | B%.2f E%.2f | %s%s",
        ChannelName(channel_), z, resolution, resolution,
        options_.noise.seed, options_.noise.baseFrequency,
        options_.noise.octaves, options_.noise.worleyFrequency,
        options_.noise.blend, options_.noise.erosion,
        diagnostic_ ? L"[швы] " : L"",
        animate_ ? L"[анимация]" : L"[пауза]");

    window_.SetTitle(title);
}

void Application::MarkNoiseDirty(const char* reason)
{
    noiseDirty_ = true;
    Log("noise parameter changed (%s): %s", reason,
        options_.noise.Describe().c_str());
}

void Application::PrintHelp() const
{
    Log("Управление:");
    Log("  1 / 2 / 3       Perlin fBm / Worley F1 / Perlin-Worley");
    Log("  Стрелки, колесо  срез по Z");
    Log("  Space            автоматическое движение по Z");
    Log("  R                сброс среза и остановка");
    Log("  D                диагностика швов (плитка 3x3)");
    Log("  N                принудительная перегенерация");
    Log("  4                переключить 64^3 / 128^3");
    Log("  G (+Shift)       seed +/- 1");
    Log("  F (+Shift)       базовая частота +/- 1");
    Log("  O (+Shift)       число октав +/- 1");
    Log("  W (+Shift)       частота Worley +/- 1");
    Log("  B (+Shift)       blend +/- 0.05");
    Log("  E (+Shift)       erosion +/- 0.05");
    Log("  K (+Shift)       gain +/- 0.05");
    Log("  H / F1           эта справка");
    Log("  Esc              выход");
    Log("Текущие параметры: %s", options_.noise.Describe().c_str());
}

int Application::RunSelfTest()
{
    Log("=== V0 SELFTEST ===");
    const NoiseParams baseline = options_.noise;
    const uint32_t resolution = noise_.Resolution();

    Log("selftest baseline: %s", baseline.Describe().c_str());
    noise_.Generate(baseline);
    const std::vector<float> first = noise_.ReadbackRgba();
    const NoiseAnalysis analysis = AnalyzeNoise(first, resolution);
    const NoiseVolume::PeriodicityProbe probe = noise_.ProbePeriodicity();
    const float probeMaximum = std::max(probe.maxDifference[0],
                                        std::max(probe.maxDifference[1],
                                                 probe.maxDifference[2]));
    const NoiseVolume::SamplerWrapProbe samplerProbe =
        noise_.ProbeSamplerWrap(first);

    noise_.Generate(baseline);
    const std::vector<float> second = noise_.ReadbackRgba();
    const double determinism = MaxAbsDifference(first, second);

    NoiseParams seeded = baseline;
    seeded.seed = baseline.seed + 1;
    noise_.Generate(seeded);
    const std::vector<float> seededData = noise_.ReadbackRgba();
    const double seedDifference =
        MeanAbsDifferenceChannel(first, seededData, 2);

    // Restore the user-visible state; the next capture or frame uses baseline.
    noise_.Generate(baseline);
    options_.noise = noise_.Params();

    const audit::ValidationStatus validation = audit::EvaluateValidationStatus(
        renderer_.DebugLayerRequested(), renderer_.DebugLayerEnabled(),
        renderer_.InfoQueueAvailable(), renderer_.GpuValidationRequested(),
        renderer_.GpuValidationEnabled(), renderer_.DebugErrorCount());
    const bool validationOk =
        validation.verdict == audit::ValidationVerdict::Pass ||
        validation.verdict == audit::ValidationVerdict::NotRequested;
    const bool inconclusive =
        validation.verdict == audit::ValidationVerdict::Inconclusive;
    const bool resolutionOk = analysis.resolution == resolution;
    const bool determinismOk = determinism <= 1e-6;
    const bool seedOk = seedDifference > 1e-3;
    const bool probeOk = probeMaximum <= 1e-4 && probe.negativeControl >= 1e-3;
    const bool samplerOk = samplerProbe.maxFilterError <= 1e-2 &&
                           samplerProbe.maxWrapDifference <= 1e-2 &&
                           samplerProbe.maxClampDifference >= 1e-3;
    const bool pass = analysis.valuesInRange && analysis.nonConstant &&
                      analysis.periodic && resolutionOk && determinismOk &&
                      seedOk && probeOk && samplerOk && validationOk;

    std::ostringstream report;
    report << "OGSR Clouds V0 selftest\n";
    report << "timestamp: " << TimestampUtc() << "\n";
    report << "adapter: " << renderer_.AdapterInfo() << "\n";
    report << "warp: " << (renderer_.UsingWarp() ? "yes" : "no") << "\n";
    report << "format: " << noise_.FormatName() << "\n";
    report << "params: " << baseline.Describe() << "\n";
    report << "requested resolution: " << resolution << "\n";
    report << "debug layer requested: "
           << (renderer_.DebugLayerRequested() ? "yes" : "no") << "\n";
    report << "debug layer enabled: "
           << (renderer_.DebugLayerEnabled() ? "yes" : "no") << "\n";
    report << "ID3D12InfoQueue available: "
           << (renderer_.InfoQueueAvailable() ? "yes" : "no") << "\n";
    report << "gpu based validation requested: "
           << (renderer_.GpuValidationRequested() ? "yes" : "no") << "\n";
    report << "gpu based validation enabled: "
           << (renderer_.GpuValidationEnabled() ? "yes" : "no") << "\n";
    report << "validation performed: "
           << (validation.performed ? "yes" : "no") << "\n";
    report << "validation verdict: "
           << ValidationVerdictName(validation.verdict) << "\n";
    report << "debug errors: " << renderer_.DebugErrorCount() << "\n";
    report << "debug warnings: " << renderer_.DebugWarningCount() << "\n\n";
    report << FormatAnalysis(analysis) << "\n";
    report << "determinism max abs diff: " << determinism
           << " (threshold 1e-6)\n";
    report << "periodicity probe max |f(p)-f(p+axis)|: X="
           << probe.maxDifference[0] << " Y=" << probe.maxDifference[1]
           << " Z=" << probe.maxDifference[2] << " (threshold 1e-4)\n";
    report << "periodicity probe negative control: " << probe.negativeControl
           << " (threshold 1e-3)\n";
    report << "sampler WRAP filter error: " << samplerProbe.maxFilterError
           << " (threshold 1e-2)\n";
    report << "sampler WRAP p vs p+axis difference: "
           << samplerProbe.maxWrapDifference << " (threshold 1e-2)\n";
    report << "sampler CLAMP reference difference: "
           << samplerProbe.maxClampDifference << " (threshold 1e-3)\n";
    report << "seed sensitivity mean abs diff (combined): " << seedDifference
           << " (threshold 1e-3)\n\n";
    report << (analysis.valuesInRange ? "[PASS]" : "[FAIL]")
           << " values are inside [0,1]\n";
    report << (analysis.nonConstant ? "[PASS]" : "[FAIL]")
           << " each field is non-constant\n";
    report << (analysis.periodic ? "[PASS]" : "[FAIL]")
           << " periodic seam ratios are below 2.5\n";
    report << (probeOk ? "[PASS]" : "[FAIL]")
           << " direct GPU periodicity probe is below 1e-4 with a non-zero negative control\n";
    report << (samplerOk ? "[PASS]" : "[FAIL]")
           << " WRAP sampler matches CPU trilinear filtering across X/Y/Z seams\n";
    report << (resolutionOk ? "[PASS]" : "[FAIL]")
           << " readback resolution matches the requested volume\n";
    report << (determinismOk ? "[PASS]" : "[FAIL]")
           << " identical parameters produce identical volumes\n";
    report << (seedOk ? "[PASS]" : "[FAIL]")
           << " a different seed changes the combined field\n";
    report << (validationOk ? "[PASS]" : (inconclusive ? "[INCONCLUSIVE]" : "[FAIL]"))
           << " requested validation infrastructure and debug errors"
           << (validation.verdict == audit::ValidationVerdict::NotRequested
                   ? " (validation not requested)" : "") << "\n\n";
    report << (pass ? "V0 SELFTEST: PASS\n"
                    : (inconclusive ? "V0 SELFTEST: INCONCLUSIVE\n"
                                    : "V0 SELFTEST: FAIL\n"));

    Log("%s", report.str().c_str());
    const std::filesystem::path reportPath = ExecutableDirectory() / "selftest.txt";
    std::ofstream file(reportPath, std::ios::trunc);
    if (file)
        file << report.str();
    else
        LogWarn("could not write %s", reportPath.string().c_str());

    return pass ? 0 : (inconclusive ? 3 : 1);
}

int Application::RunCapture()
{
    if (options_.captureDir.empty())
        Fail("capture directory is empty");

    std::error_code ec;
    std::filesystem::create_directories(options_.captureDir, ec);
    if (ec)
        Fail("cannot create capture directory: " + ec.message());
    const auto directory = std::filesystem::absolute(options_.captureDir);

    window_.Show(SW_SHOW);
    UpdateWindow(window_.Handle());
    window_.PumpMessages();

    const uint32_t resolution = noise_.Resolution();
    const uint32_t slices[3] = {
        resolution / 8,
        resolution / 2,
        (resolution * 7) / 8,
    };
    const char* names[3] = { "perlin", "worley", "combined" };
    std::vector<std::string> files;
    std::vector<uint8_t> pixels;

    for (uint32_t channel = 0; channel < 3; ++channel)
    {
        channel_ = channel;
        diagnostic_ = false;
        for (uint32_t index = 0; index < 3; ++index)
        {
            slice_ = static_cast<float>(slices[index]);
            UpdateViewConstants();

            if (!window_.PumpMessages())
                Fail("window closed during capture");
            pixels.clear();
            if (!RenderOneFrame(true, &pixels))
                Fail("capture frame was not rendered");

            char filename[128]{};
            snprintf(filename, sizeof(filename), "%s_z%03u.bmp",
                     names[channel], slices[index]);
            const auto path = directory / filename;
            if (!WriteBmp32(path, renderer_.Width(), renderer_.Height(),
                            pixels.data()))
            {
                Fail("failed to write " + path.string());
            }
            files.emplace_back(filename);
            Log("captured %s (%ux%u)", filename,
                renderer_.Width(), renderer_.Height());
        }
    }

    // Seam diagnostic: a 3x3 tiling of the combined field exposes any
    // discontinuity at the periodic boundaries.
    channel_ = 2;
    diagnostic_ = true;
    slice_ = static_cast<float>(resolution / 2);
    UpdateViewConstants();
    if (!window_.PumpMessages())
        Fail("window closed during capture");
    pixels.clear();
    if (!RenderOneFrame(true, &pixels))
        Fail("diagnostic capture frame was not rendered");

    char diagnosticName[128]{};
    snprintf(diagnosticName, sizeof(diagnosticName), "combined_diag_z%03u.bmp",
             resolution / 2);
    const auto diagnosticPath = directory / diagnosticName;
    if (!WriteBmp32(diagnosticPath, renderer_.Width(), renderer_.Height(),
                    pixels.data()))
    {
        Fail("failed to write " + diagnosticPath.string());
    }
    files.emplace_back(diagnosticName);
    Log("captured %s", diagnosticName);

    std::ostringstream info;
    info << "OGSR Clouds V0 capture\n";
    info << "timestamp: " << TimestampUtc() << "\n";
    info << "adapter: " << renderer_.AdapterInfo() << "\n";
    info << "warp: " << (renderer_.UsingWarp() ? "yes" : "no") << "\n";
    info << "format: " << noise_.FormatName() << "\n";
    info << "params: " << noise_.Params().Describe() << "\n";
    info << "backbuffer: " << renderer_.Width() << "x" << renderer_.Height() << "\n";
    info << "debug layer: "
         << (renderer_.DebugLayerEnabled() ? "enabled" : "unavailable") << "\n";
    info << "debug errors: " << renderer_.DebugErrorCount() << "\n";
    info << "debug warnings: " << renderer_.DebugWarningCount() << "\n";
    info << "files:\n";
    for (const auto& file : files)
        info << "  " << file << "\n";

    const auto infoPath = directory / "capture_info.txt";
    std::ofstream infoFile(infoPath, std::ios::trunc);
    if (infoFile)
        infoFile << info.str();
    else
        LogWarn("could not write %s", infoPath.string().c_str());

    Log("capture complete: %zu images in %s",
        files.size(), WideToUtf8(directory.wstring()).c_str());
    return renderer_.DebugErrorCount() == 0 ? 0 : 2;
}
