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

struct CloudPixelStats
{
    bool finite = true;
    double meanRgb = 0.0;
    double meanTransmittance = 0.0;
    double minTransmittance = 1.0;
    double maxRgb = 0.0;
    double minRgb = 1.0;
};

CloudPixelStats AnalyzeCloudPixels(const CloudReadback& readback)
{
    CloudPixelStats stats{};
    const size_t pixels =
        static_cast<size_t>(readback.width) * readback.height;
    if (pixels == 0 || readback.rgba.size() != pixels * 4)
    {
        stats.finite = false;
        return stats;
    }

    double sumRgb = 0.0;
    double sumTransmittance = 0.0;
    for (size_t i = 0; i < pixels; ++i)
    {
        const float r = readback.rgba[i * 4 + 0];
        const float g = readback.rgba[i * 4 + 1];
        const float b = readback.rgba[i * 4 + 2];
        const float a = readback.rgba[i * 4 + 3];
        if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) ||
            !std::isfinite(a))
        {
            stats.finite = false;
            continue;
        }
        sumRgb += std::max(0.0, static_cast<double>(r + g + b) / 3.0);
        sumTransmittance += a;
        stats.minTransmittance = std::min(
            stats.minTransmittance, static_cast<double>(a));
        stats.maxRgb = std::max(
            stats.maxRgb, static_cast<double>(std::max(r, std::max(g, b))));
        stats.minRgb = std::min(
            stats.minRgb, static_cast<double>(std::max(r, std::max(g, b))));
    }

    stats.meanRgb = sumRgb / static_cast<double>(pixels);
    stats.meanTransmittance = sumTransmittance / static_cast<double>(pixels);
    return stats;
}

double MeanAbsCloudDifference(const CloudReadback& a, const CloudReadback& b)
{
    if (a.width != b.width || a.height != b.height ||
        a.rgba.size() != b.rgba.size() || a.rgba.empty())
        return 0.0;

    double sum = 0.0;
    for (size_t i = 0; i < a.rgba.size(); ++i)
        sum += std::fabs(a.rgba[i] - b.rgba[i]);
    return sum / static_cast<double>(a.rgba.size());
}

double MaxAbsCloudDifference(const CloudReadback& a, const CloudReadback& b)
{
    if (a.width != b.width || a.height != b.height ||
        a.rgba.size() != b.rgba.size() || a.rgba.empty())
        return 0.0;

    double maximum = 0.0;
    for (size_t i = 0; i < a.rgba.size(); ++i)
        maximum = std::max(maximum, std::fabs(
            static_cast<double>(a.rgba[i]) - static_cast<double>(b.rgba[i])));
    return maximum;
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
    cloudPreset_ = options_.cloudPreset;
    cloudDebug_ = options_.cloudDebug;
    cloudTime_ = options_.initialCloudTime;
}

Application::~Application()
{
    Shutdown();
}

bool Application::Initialize()
{
    Log("=== OGSR Volumetric Clouds V1 (standalone D3D12) ===");
#ifdef _DEBUG
    Log("build: Debug");
#else
    Log("build: Release");
#endif
    Log("executable directory: %s",
        WideToUtf8(ExecutableDirectory().wstring()).c_str());
    Log("render mode: %s",
        options_.mode == clouds::RenderMode::Clouds ? "clouds" : "slices");

    if (!window_.Create(L"OGSR Облака V1", options_.windowWidth,
                        options_.windowHeight,
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
    cloudRayMarcher_.Initialize(renderer_, renderer_.Width(), renderer_.Height());
    cloudCompositor_.Initialize(renderer_);
    cloudProfiler_.Initialize(renderer_);

    options_.noise = noise_.Params();
    view_.resolution = noise_.Resolution();
    slice_ = static_cast<float>(noise_.Resolution()) * 0.5f;
    view_.sliceIndex = slice_;

    Log("adapter: %s", renderer_.AdapterInfo().c_str());
    Log("noise format: %s", noise_.FormatName());
    Log("noise parameters: %s", options_.noise.Describe().c_str());
    Log("cloud output: %ux%u %s, steps=%u coverage=%.2f density=%.2f",
        cloudRayMarcher_.OutputWidth(), cloudRayMarcher_.OutputHeight(),
        cloudRayMarcher_.FormatName(), options_.cloud.stepCount,
        options_.cloud.coverage, options_.cloud.density);
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

    if (gpuIdle)
    {
        cloudProfiler_.CollectAll();
        cloudProfiler_.LogSummary();
    }
    Log("shutdown: cloud ray marcher");
    cloudRayMarcher_.Shutdown();
    Log("shutdown: cloud compositor");
    cloudCompositor_.Shutdown();
    cloudProfiler_.Shutdown();
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
        {
            renderer_.Resize(pendingWidth, pendingHeight);
            cloudRayMarcher_.Resize(renderer_.Width(), renderer_.Height());
        }

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

        if (options_.mode == clouds::RenderMode::Clouds)
        {
            UpdateCloudInput(delta);
        }
        else if (animate_)
        {
            slice_ += delta * 18.0f;
            const float maximum = static_cast<float>(noise_.Resolution() - 1);
            while (slice_ > maximum)
                slice_ -= static_cast<float>(noise_.Resolution());
            UpdateViewConstants();
        }
        else
        {
            UpdateViewConstants();
        }
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

    cloudProfiler_.Collect(frame.backBufferIndex);

    if (options_.mode == clouds::RenderMode::Clouds)
    {
        if (!cloudFramePathLogged_)
        {
            Log("cloud frame path: CloudRayMarcher compute + CloudCompositor draw active");
            cloudFramePathLogged_ = true;
        }
        const clouds::CloudFrameConstants constants = BuildCloudConstants(cloudDebug_);
        cloudProfiler_.BeginFrame(frame.list, frame.backBufferIndex);
        cloudRayMarcher_.Render(frame.list, frame.backBufferIndex, constants,
                                noise_.SrvGpuHandle());
        cloudProfiler_.EndCloud(frame.list, frame.backBufferIndex);

        renderer_.BeginRenderPass(frame);
        cloudCompositor_.Draw(frame.list,
                              cloudRayMarcher_.ConstantsAddress(frame.backBufferIndex),
                              cloudRayMarcher_.SrvGpuHandle(frame.backBufferIndex),
                              frame.width, frame.height);
        cloudProfiler_.EndComposite(frame.list, frame.backBufferIndex);
        renderer_.EndRenderPass(frame);
        cloudProfiler_.Resolve(frame.list, frame.backBufferIndex);
    }
    else
    {
        renderer_.BeginRenderPass(frame);
        viewer_.Draw(frame.list, frame.width, frame.height, view_,
                     noise_.SrvGpuHandle());
        renderer_.EndRenderPass(frame);
    }

    const uint32_t frameIndex = frame.backBufferIndex;
    renderer_.Present(frame, capture ? pixels : nullptr);
    if (capture)
        cloudProfiler_.Collect(frameIndex);
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
    if (message == WM_MOUSEMOVE &&
        options_.mode == clouds::RenderMode::Clouds && mouseLook_)
    {
        const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
        const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
        camera_.Rotate(static_cast<float>(x - lastMouseX_),
                       static_cast<float>(y - lastMouseY_));
        lastMouseX_ = x;
        lastMouseY_ = y;
        UpdateTitle(true);
        return;
    }
    if (message == WM_LBUTTONDOWN &&
        options_.mode == clouds::RenderMode::Clouds)
    {
        mouseLook_ = true;
        lastMouseX_ = static_cast<int>(static_cast<short>(LOWORD(lparam)));
        lastMouseY_ = static_cast<int>(static_cast<short>(HIWORD(lparam)));
        SetCapture(window_.Handle());
        return;
    }
    if (message == WM_LBUTTONUP)
    {
        mouseLook_ = false;
        ReleaseCapture();
        return;
    }

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

        if (options_.mode == clouds::RenderMode::Clouds)
        {
            camera_.fovYDegrees = std::clamp(
                camera_.fovYDegrees - static_cast<float>(steps) * 2.0f,
                35.0f, 90.0f);
        }
        else
        {
            slice_ = std::clamp(
                slice_ + static_cast<float>(steps), 0.0f,
                static_cast<float>(noise_.Resolution() - 1));
            UpdateViewConstants();
        }
        UpdateTitle(true);
    }
}

void Application::HandleKey(WPARAM key, LPARAM lparam)
{
    if (options_.mode == clouds::RenderMode::Clouds)
    {
        HandleCloudKey(key, lparam);
        return;
    }

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

    case VK_F2:
        if (!repeat)
        {
            options_.mode = options_.mode == clouds::RenderMode::Clouds
                ? clouds::RenderMode::Slices
                : clouds::RenderMode::Clouds;
            if (options_.mode == clouds::RenderMode::Clouds)
                cloudRayMarcher_.Resize(renderer_.Width(), renderer_.Height());
            Log("render mode switched to %s",
                options_.mode == clouds::RenderMode::Clouds ? "clouds" : "slices");
            UpdateTitle(true);
        }
        return;

    default:
        return;
    }

    UpdateViewConstants();
    UpdateTitle(true);
}

void Application::HandleCloudKey(WPARAM key, LPARAM lparam)
{
    const bool repeat = (lparam & (1LL << 30)) != 0;

    switch (key)
    {
    case VK_ESCAPE:
        mouseLook_ = false;
        ReleaseCapture();
        running_ = false;
        window_.Close();
        return;

    case '1':
        if (!repeat)
            ApplyCloudPreset("clear", true);
        break;
    case '2':
        if (!repeat)
            ApplyCloudPreset("overcast", true);
        break;

    case '5':
        cloudDebug_ = clouds::CloudDebugMode::Final;
        break;
    case '6':
        cloudDebug_ = clouds::CloudDebugMode::Density;
        break;
    case '7':
        cloudDebug_ = clouds::CloudDebugMode::Transmittance;
        break;
    case '8':
        cloudDebug_ = clouds::CloudDebugMode::StepCount;
        break;
    case '9':
        cloudDebug_ = clouds::CloudDebugMode::Occupancy;
        break;
    case 'M':
        if (!repeat)
        {
            const uint32_t next =
                (static_cast<uint32_t>(cloudDebug_) + 1u) % 5u;
            cloudDebug_ = static_cast<clouds::CloudDebugMode>(next);
            Log("cloud debug mode: %s", clouds::DebugModeName(cloudDebug_));
        }
        break;

    case 'P':
        if (!repeat)
        {
            cloudPaused_ = !cloudPaused_;
            Log("cloud animation %s", cloudPaused_ ? "paused" : "running");
        }
        break;
    case 'R':
        if (!repeat)
        {
            mouseLook_ = false;
            ReleaseCapture();
            camera_.Reset();
            cloudTime_ = 0.0f;
            cloudPaused_ = false;
            cloudDebug_ = clouds::CloudDebugMode::Final;
            ApplyCloudPreset("clear", true);
            Log("cloud scene reset to the reproducible Clear Day camera");
        }
        break;

    case 'J':
        options_.cloud.sunAzimuthDegrees -= 5.0f;
        break;
    case 'L':
        options_.cloud.sunAzimuthDegrees += 5.0f;
        break;
    case 'I':
        options_.cloud.sunElevationDegrees = std::clamp(
            options_.cloud.sunElevationDegrees + 3.0f, 2.0f, 88.0f);
        break;
    case 'K':
        options_.cloud.sunElevationDegrees = std::clamp(
            options_.cloud.sunElevationDegrees - 3.0f, 2.0f, 88.0f);
        break;

    case VK_OEM_PLUS:
        options_.cloud.density = std::clamp(
            options_.cloud.density + 0.05f, 0.0f, 2.0f);
        break;
    case VK_OEM_MINUS:
        options_.cloud.density = std::clamp(
            options_.cloud.density - 0.05f, 0.0f, 2.0f);
        break;
    case VK_OEM_4:
        options_.cloud.coverage = std::clamp(
            options_.cloud.coverage - 0.02f, 0.0f, 1.0f);
        break;
    case VK_OEM_6:
        options_.cloud.coverage = std::clamp(
            options_.cloud.coverage + 0.02f, 0.0f, 1.0f);
        break;

    case 'Z':
        camera_.fovYDegrees =
            std::clamp(camera_.fovYDegrees - 2.0f, 35.0f, 90.0f);
        break;
    case 'X':
        camera_.fovYDegrees =
            std::clamp(camera_.fovYDegrees + 2.0f, 35.0f, 90.0f);
        break;

    case VK_OEM_COMMA:
        options_.cloud.windSpeed =
            std::clamp(options_.cloud.windSpeed - 5.0f, 0.0f, 120.0f);
        break;
    case VK_OEM_PERIOD:
        options_.cloud.windSpeed =
            std::clamp(options_.cloud.windSpeed + 5.0f, 0.0f, 120.0f);
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

    case 'H':
    case VK_F1:
        if (!repeat)
            PrintHelp();
        return;

    case VK_F2:
        if (!repeat)
        {
            options_.mode = options_.mode == clouds::RenderMode::Clouds
                ? clouds::RenderMode::Slices
                : clouds::RenderMode::Clouds;
            if (options_.mode == clouds::RenderMode::Clouds)
                cloudRayMarcher_.Resize(renderer_.Width(), renderer_.Height());
            Log("render mode switched to %s",
                options_.mode == clouds::RenderMode::Clouds ? "clouds" : "slices");
            UpdateTitle(true);
        }
        return;

    default:
        return;
    }

    UpdateTitle(true);
}

void Application::UpdateCloudInput(float deltaSeconds)
{
    if (options_.mode != clouds::RenderMode::Clouds)
        return;

    if (!cloudPaused_)
        cloudTime_ += deltaSeconds;

    if (GetForegroundWindow() != window_.Handle())
        return;

    const auto pressed = [](int key)
    {
        return (GetAsyncKeyState(key) & 0x8000) != 0 ? 1.0f : 0.0f;
    };
    const float forward = pressed('W') - pressed('S');
    const float strafe = pressed('D') - pressed('A');
    const float vertical = pressed('E') - pressed('Q');
    if (forward == 0.0f && strafe == 0.0f && vertical == 0.0f)
        return;

    const bool fast = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const float speed = fast ? 900.0f : 220.0f;
    camera_.Move(forward, strafe, vertical, speed * deltaSeconds);
}

void Application::ApplyCloudPreset(const char* name, bool logChange)
{
    if (!clouds::ApplyPreset(name, options_.cloud))
    {
        LogWarn("unknown cloud preset: %s", name);
        return;
    }
    cloudPreset_ = name;
    if (logChange)
    {
        Log("cloud preset: %s | layer %.0f..%.0f coverage %.2f density %.2f "
            "steps %u",
            cloudPreset_.c_str(), options_.cloud.layerBottom,
            options_.cloud.layerTop, options_.cloud.coverage,
            options_.cloud.density, options_.cloud.stepCount);
    }
    UpdateTitle(true);
}

clouds::CloudFrameConstants Application::BuildCloudConstants(
    clouds::CloudDebugMode debug) const
{
    const uint32_t width = cloudRayMarcher_.OutputWidth() > 0
        ? cloudRayMarcher_.OutputWidth()
        : std::max(1u, renderer_.Width() / 2);
    const uint32_t height = cloudRayMarcher_.OutputHeight() > 0
        ? cloudRayMarcher_.OutputHeight()
        : std::max(1u, renderer_.Height() / 2);
    return clouds::BuildCloudConstants(camera_, options_.cloud, width, height,
                                      cloudTime_, debug);
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
    if (options_.mode == clouds::RenderMode::Clouds)
    {
        const std::wstring preset = Utf8ToWide(cloudPreset_);
        const std::wstring debug = Utf8ToWide(
            clouds::DebugModeName(cloudDebug_));
        swprintf_s(
            title,
            L"OGSR Облака V1 | %s | %s | pos %.0f %.0f %.0f | FOV %.0f | "
            L"steps %u | %.2f ms%s",
            preset.c_str(), debug.c_str(), camera_.position.x,
            camera_.position.y, camera_.position.z, camera_.fovYDegrees,
            options_.cloud.stepCount, cloudProfiler_.LastTotalMs(),
            cloudPaused_ ? L" | pause" : L"");
    }
    else
    {
        swprintf_s(
            title,
            L"OGSR Облака V0 | срезы | %s | Z %d/%u | %u^3 | seed %u | "
            L"F%u O%u W%u | B%.2f E%.2f | %s%s",
            ChannelName(channel_), z, resolution, resolution,
            options_.noise.seed, options_.noise.baseFrequency,
            options_.noise.octaves, options_.noise.worleyFrequency,
            options_.noise.blend, options_.noise.erosion,
            diagnostic_ ? L"[швы] " : L"",
            animate_ ? L"[анимация]" : L"[пауза]");
    }

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
    if (options_.mode == clouds::RenderMode::Clouds)
    {
        Log("Управление облаками (V1):");
        Log("  ЛКМ + мышь       обзор камерой");
        Log("  W/S/A/D/Q/E      движение; Shift — быстрее");
        Log("  колесо, Z/X      FOV");
        Log("  J/L, I/K         азимут и высота солнца");
        Log("  1 / 2            Clear Day / Overcast");
        Log("  5 / 6 / 7 / 8    final / density / transmittance / step count");
        Log("  9                occupancy (шаги с ненулевой плотностью)");
        Log("  M                циклически сменить debug-режим");
        Log("  +/-              density; [ / ] coverage");
        Log("  , / .            скорость ветра");
        Log("  P                пауза анимации ветра");
        Log("  R                сброс к воспроизводимой сцене");
        Log("  F2               переключить slices / clouds");
        Log("  N                перегенерация шума");
        Log("  4                переключить 64^3 / 128^3");
        Log("  H / F1           эта справка");
        Log("  Esc              выход");
        Log("Текущая сцена: %s; камера %.1f %.1f %.1f; FOV %.0f; %s",
            cloudPreset_.c_str(), camera_.position.x, camera_.position.y,
            camera_.position.z, camera_.fovYDegrees,
            clouds::DebugModeName(cloudDebug_));
        return;
    }

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

int Application::RunCloudSelfTest(std::ostringstream& report)
{
    Log("=== V1 CLOUD SELFTEST ===");
    const UINT width = cloudRayMarcher_.OutputWidth();
    const UINT height = cloudRayMarcher_.OutputHeight();
    if (width == 0 || height == 0)
    {
        report << "\nV1 CLOUD SELFTEST: no cloud output texture\n";
        return 1;
    }

    clouds::CloudParams params;
    clouds::ApplyClearPreset(params);
    clouds::Camera camera;
    camera.Reset();

    const auto renderAt = [&](const clouds::CloudParams& p,
                              const clouds::Camera& c,
                              clouds::CloudDebugMode debug,
                              float timeSeconds)
    {
        const clouds::CloudFrameConstants constants =
            clouds::BuildCloudConstants(c, p, width, height, timeSeconds, debug);
        return cloudRayMarcher_.Readback(constants, noise_.SrvGpuHandle());
    };

    const auto render = [&](const clouds::CloudParams& p,
                            const clouds::Camera& c,
                            clouds::CloudDebugMode debug)
    {
        return renderAt(p, c, debug, 0.0f);
    };

    const CloudReadback base = render(params, camera, clouds::CloudDebugMode::Final);
    const CloudPixelStats baseStats = AnalyzeCloudPixels(base);

    clouds::CloudParams emptyParams = params;
    emptyParams.coverage = 0.0f;
    const CloudReadback empty = render(emptyParams, camera, clouds::CloudDebugMode::Final);
    const CloudPixelStats emptyStats = AnalyzeCloudPixels(empty);

    clouds::CloudParams denseParams = params;
    denseParams.extinction *= 2.5f;
    const CloudReadback dense = render(denseParams, camera, clouds::CloudDebugMode::Final);
    const CloudPixelStats denseStats = AnalyzeCloudPixels(dense);

    const CloudReadback repeat = render(params, camera, clouds::CloudDebugMode::Final);
    const double determinism = MeanAbsCloudDifference(base, repeat);

    clouds::Camera movedCamera = camera;
    movedCamera.position.x += 600.0f;
    movedCamera.position.z += 400.0f;
    movedCamera.yawDegrees += 18.0f;
    const CloudReadback moved = render(params, movedCamera, clouds::CloudDebugMode::Final);
    const double cameraDifference = MeanAbsCloudDifference(base, moved);

    clouds::CloudParams movedSun = params;
    movedSun.sunAzimuthDegrees += 70.0f;
    const CloudReadback sun = render(movedSun, camera, clouds::CloudDebugMode::Final);
    const double sunDifference = MeanAbsCloudDifference(base, sun);

    // Wind translation invariance. With the contract
    // noiseXZ = (worldXZ - windDir * speed * time) * noiseScale, translating
    // the camera by the same world offset must leave the cloud field unchanged.
    const DirectX::XMFLOAT3 windDirection =
        clouds::Normalize(params.windDirection);
    clouds::CloudParams windParams = params;
    const CloudReadback windStart =
        renderAt(windParams, camera, clouds::CloudDebugMode::Final, 0.0f);
    clouds::Camera windCamera = camera;
    windCamera.position.x += windDirection.x * windParams.windSpeed;
    windCamera.position.z += windDirection.z * windParams.windSpeed;
    const CloudReadback windEnd =
        renderAt(windParams, windCamera, clouds::CloudDebugMode::Final, 1.0f);
    const double windDifference = MaxAbsCloudDifference(windStart, windEnd);

    clouds::CloudParams zeroWind = params;
    zeroWind.windSpeed = 0.0f;
    const CloudReadback zeroWindStart =
        renderAt(zeroWind, camera, clouds::CloudDebugMode::Final, 0.0f);
    const CloudReadback zeroWindEnd =
        renderAt(zeroWind, camera, clouds::CloudDebugMode::Final, 1.0f);
    const double zeroWindDifference =
        MaxAbsCloudDifference(zeroWindStart, zeroWindEnd);

    clouds::CloudParams oppositeWind = params;
    oppositeWind.windDirection = DirectX::XMFLOAT3(
        -params.windDirection.x, 0.0f, -params.windDirection.z);
    const DirectX::XMFLOAT3 oppositeDirection =
        clouds::Normalize(oppositeWind.windDirection);
    const CloudReadback oppositeStart =
        renderAt(oppositeWind, camera, clouds::CloudDebugMode::Final, 0.0f);
    clouds::Camera oppositeCamera = camera;
    oppositeCamera.position.x +=
        oppositeDirection.x * oppositeWind.windSpeed;
    oppositeCamera.position.z +=
        oppositeDirection.z * oppositeWind.windSpeed;
    const CloudReadback oppositeEnd = renderAt(
        oppositeWind, oppositeCamera, clouds::CloudDebugMode::Final, 1.0f);
    const double oppositeWindDifference =
        MaxAbsCloudDifference(oppositeStart, oppositeEnd);

    const CloudPixelStats windStats = AnalyzeCloudPixels(windStart);
    const CloudPixelStats oppositeWindStats =
        AnalyzeCloudPixels(oppositeStart);

    const CloudReadback stepDebug =
        render(params, camera, clouds::CloudDebugMode::StepCount);
    const CloudReadback occupancyDebug =
        render(params, camera, clouds::CloudDebugMode::Occupancy);
    const CloudPixelStats stepStats = AnalyzeCloudPixels(stepDebug);
    const CloudPixelStats occupancyStats = AnalyzeCloudPixels(occupancyDebug);
    bool stepOrderOk = stepDebug.rgba.size() == occupancyDebug.rgba.size();
    if (stepOrderOk)
    {
        for (size_t i = 0; i < stepDebug.rgba.size(); i += 4)
        {
            if (stepDebug.rgba[i] + 1e-4f < occupancyDebug.rgba[i])
            {
                stepOrderOk = false;
                break;
            }
        }
    }

    clouds::CloudParams earlyExitParams = params;
    earlyExitParams.coverage = 1.0f;
    earlyExitParams.density = 2.0f;
    earlyExitParams.extinction = 0.5f;
    clouds::Camera earlyCamera = camera;
    earlyCamera.position = DirectX::XMFLOAT3(0.0f, 900.0f, -700.0f);
    earlyCamera.pitchDegrees = 55.0f;
    const CloudReadback earlyExitStep = render(
        earlyExitParams, earlyCamera, clouds::CloudDebugMode::StepCount);
    const CloudPixelStats earlyExitStats = AnalyzeCloudPixels(earlyExitStep);
    const bool earlyExitOk = earlyExitStats.finite &&
                             earlyExitStats.minRgb < 0.999;
    const bool stepCountOk = stepStats.finite && occupancyStats.finite &&
                             stepOrderOk && earlyExitOk;

    const CloudReadback transmittance =
        render(params, camera, clouds::CloudDebugMode::Transmittance);
    const CloudPixelStats transmittanceStats = AnalyzeCloudPixels(transmittance);

    clouds::Camera insideCamera = camera;
    insideCamera.position = DirectX::XMFLOAT3(0.0f, 900.0f, -700.0f);
    insideCamera.pitchDegrees = 55.0f;
    const CloudReadback inside =
        render(params, insideCamera, clouds::CloudDebugMode::Final);
    const CloudPixelStats insideStats = AnalyzeCloudPixels(inside);

    const bool finiteOk = baseStats.finite && emptyStats.finite &&
                          denseStats.finite && transmittanceStats.finite &&
                          insideStats.finite && windStats.finite &&
                          oppositeWindStats.finite && stepStats.finite &&
                          occupancyStats.finite && earlyExitStats.finite;
    const bool cloudPresent = baseStats.meanTransmittance < 0.999 &&
                              baseStats.meanRgb > 1e-5;
    const bool emptyOk = emptyStats.finite &&
                         emptyStats.meanTransmittance > 0.999 &&
                         emptyStats.maxRgb < 0.02;
    const bool extinctionOk = denseStats.finite &&
                              denseStats.meanTransmittance <
                                  baseStats.meanTransmittance - 1e-3;
    const bool determinismOk = determinism < 1e-5;
    const bool cameraOk = cameraDifference > 1e-4;
    const bool sunOk = sunDifference > 1e-4;
    const bool windOk = windStats.finite && windDifference <= 2e-3;
    const bool zeroWindOk = zeroWindDifference <= 1e-5;
    const bool oppositeWindOk = oppositeWindStats.finite &&
                                oppositeWindDifference <= 2e-3;
    const bool debugModeOk = transmittanceStats.finite &&
                             transmittanceStats.meanTransmittance > 0.999;

    const bool pass = finiteOk && cloudPresent && emptyOk && extinctionOk &&
                      determinismOk && cameraOk && sunOk && windOk &&
                      zeroWindOk && oppositeWindOk && stepCountOk &&
                      debugModeOk;

    report << "\nV1 CLOUD SELFTEST (" << width << "x" << height << ")\n";
    report << "clear preset mean transmittance: " << baseStats.meanTransmittance
           << "; mean rgb: " << baseStats.meanRgb << "\n";
    report << "coverage=0 mean transmittance: "
           << emptyStats.meanTransmittance << "; max rgb: "
           << emptyStats.maxRgb << "\n";
    report << "2.5x extinction mean transmittance: "
           << denseStats.meanTransmittance << "\n";
    report << "inside-layer upward mean transmittance: "
           << insideStats.meanTransmittance << "; mean rgb: "
           << insideStats.meanRgb << "\n";
    report << "determinism mean abs diff: " << determinism
           << " (threshold 1e-5)\n";
    report << "camera movement mean abs diff: " << cameraDifference
           << " (threshold 1e-4)\n";
    report << "sun movement mean abs diff: " << sunDifference
           << " (threshold 1e-4)\n";
    report << "wind translation max abs diff: " << windDifference
           << " (threshold 2e-3)\n";
    report << "zero wind max abs diff: " << zeroWindDifference
           << " (threshold 1e-5)\n";
    report << "opposite wind translation max abs diff: "
           << oppositeWindDifference << " (threshold 2e-3)\n";
    report << "step-count max executed fraction: " << stepStats.maxRgb
           << "; occupancy max: " << occupancyStats.maxRgb << "\n";
    report << "early-exit step-count min executed fraction: "
           << earlyExitStats.minRgb << " (must be < 0.999)\n";
    report << (finiteOk ? "[PASS]" : "[FAIL]")
           << " cloud output is finite and has no NaN/Inf\n";
    report << (cloudPresent ? "[PASS]" : "[FAIL]")
           << " clear scene contains visible cloud volume\n";
    report << (emptyOk ? "[PASS]" : "[FAIL]")
           << " coverage=0 produces an empty, fully transmissive volume\n";
    report << (extinctionOk ? "[PASS]" : "[FAIL]")
           << " increasing extinction reduces transmittance\n";
    report << (determinismOk ? "[PASS]" : "[FAIL]")
           << " fixed camera/seed/time produces identical output\n";
    report << (cameraOk ? "[PASS]" : "[FAIL]")
           << " camera movement changes the rendered cloud volume\n";
    report << (sunOk ? "[PASS]" : "[FAIL]")
           << " sun direction changes the rendered cloud volume\n";
    report << (windOk ? "[PASS]" : "[FAIL]")
           << " wind translation invariance holds for positive wind\n";
    report << (zeroWindOk ? "[PASS]" : "[FAIL]")
           << " windSpeed=0 leaves the field unchanged over time\n";
    report << (oppositeWindOk ? "[PASS]" : "[FAIL]")
           << " wind translation invariance holds for opposite direction\n";
    report << (stepCountOk ? "[PASS]" : "[FAIL]")
           << " step-count reports executed iterations and respects early exit\n";
    report << (debugModeOk ? "[PASS]" : "[FAIL]")
           << " transmittance debug output is valid\n";

    return pass ? 0 : 1;
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
    const bool v0Pass = analysis.valuesInRange && analysis.nonConstant &&
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

    const bool cloudOk = RunCloudSelfTest(report) == 0;
    const bool overallPass = v0Pass && cloudOk;

    report << (v0Pass ? "V0 SELFTEST: PASS\n"
                      : (inconclusive ? "V0 SELFTEST: INCONCLUSIVE\n"
                                      : "V0 SELFTEST: FAIL\n"));
    report << (cloudOk ? "V1 CLOUD SELFTEST: PASS\n"
                       : "V1 CLOUD SELFTEST: FAIL\n");
    report << (overallPass ? "OVERALL SELFTEST: PASS\n"
                           : (inconclusive ? "OVERALL SELFTEST: INCONCLUSIVE\n"
                                           : "OVERALL SELFTEST: FAIL\n"));

    Log("%s", report.str().c_str());
    const std::filesystem::path reportPath = ExecutableDirectory() / "selftest.txt";
    std::ofstream file(reportPath, std::ios::trunc);
    if (file)
        file << report.str();
    else
        LogWarn("could not write %s", reportPath.string().c_str());

    return overallPass ? 0 : (inconclusive ? 3 : 1);
}

int Application::RunCloudCapture(const std::filesystem::path& captureDir)
{
    std::error_code ec;
    std::filesystem::create_directories(captureDir, ec);
    if (ec)
        Fail("cannot create capture directory: " + ec.message());
    const auto directory = std::filesystem::absolute(captureDir);

    window_.Show(SW_SHOW);
    UpdateWindow(window_.Handle());
    window_.PumpMessages();

    struct Scene
    {
        const char* name;
        const char* preset;
        DirectX::XMFLOAT3 position;
        float yaw;
        float pitch;
        clouds::CloudDebugMode debug;
    };

    const Scene scenes[] = {
        { "clouds_clear_horizon", "clear", { 0.0f, 500.0f, -1200.0f }, 0.0f, 9.0f,
          clouds::CloudDebugMode::Final },
        { "clouds_clear_upward", "clear", { 200.0f, 200.0f, -900.0f }, 0.0f, 45.0f,
          clouds::CloudDebugMode::Final },
        { "clouds_clear_upward_debug", "clear", { 200.0f, 200.0f, -900.0f }, 0.0f,
          45.0f, clouds::CloudDebugMode::Transmittance },
        { "clouds_clear_sun_low", "clear", { 0.0f, 500.0f, -1200.0f }, 0.0f, 9.0f,
          clouds::CloudDebugMode::Final },
        { "clouds_overcast_horizon", "overcast", { 0.0f, 400.0f, -1200.0f }, 0.0f,
          7.0f, clouds::CloudDebugMode::Final },
        { "clouds_overcast_upward", "overcast", { 200.0f, 200.0f, -700.0f }, 0.0f,
          45.0f, clouds::CloudDebugMode::Final },
        { "clouds_density_debug", "clear", { 0.0f, 500.0f, -1200.0f }, 0.0f, 9.0f,
          clouds::CloudDebugMode::Density },
        { "clouds_transmittance_debug", "clear", { 0.0f, 500.0f, -1200.0f }, 0.0f,
          9.0f, clouds::CloudDebugMode::Transmittance },
        { "clouds_step_count_debug", "overcast", { 0.0f, 400.0f, -1200.0f }, 0.0f,
          7.0f, clouds::CloudDebugMode::StepCount },
        { "clouds_occupancy_debug", "overcast", { 0.0f, 400.0f, -1200.0f }, 0.0f,
          7.0f, clouds::CloudDebugMode::Occupancy },
        { "clouds_camera_moved", "overcast", { 900.0f, 400.0f, -1500.0f }, 35.0f,
          12.0f, clouds::CloudDebugMode::Final },
    };

    const clouds::CloudParams originalParams = options_.cloud;
    const float originalTime = cloudTime_;
    const uint32_t cliSteps = options_.cloud.stepCount;
    const float cliCoverage = options_.cloud.coverage;
    const float cliDensity = options_.cloud.density;
    std::vector<std::string> files;
    std::vector<uint8_t> pixels;

    for (const Scene& scene : scenes)
    {
        ApplyCloudPreset(scene.preset, false);
        if (options_.cloudStepsExplicit)
            options_.cloud.stepCount = cliSteps;
        if (options_.cloudCoverageExplicit)
            options_.cloud.coverage = cliCoverage;
        if (options_.cloudDensityExplicit)
            options_.cloud.density = cliDensity;
        if (std::string(scene.name) == "clouds_clear_sun_low")
            options_.cloud.sunElevationDegrees = 8.0f;

        camera_.Reset();
        camera_.position = scene.position;
        camera_.yawDegrees = scene.yaw;
        camera_.pitchDegrees = scene.pitch;
        cloudDebug_ = scene.debug;
        cloudTime_ = options_.initialCloudTime;
        cloudPaused_ = true;
        UpdateTitle(true);

        if (!window_.PumpMessages())
            Fail("window closed during cloud capture");
        pixels.clear();
        if (!RenderOneFrame(true, &pixels))
            Fail("cloud capture frame was not rendered");

        const std::filesystem::path path =
            directory / (std::string(scene.name) + ".bmp");
        if (!WriteBmp32(path, renderer_.Width(), renderer_.Height(),
                        pixels.data()))
        {
            Fail("failed to write " + path.string());
        }
        files.emplace_back(std::string(scene.name) + ".bmp");
        Log("captured %s (%ux%u, %s, %s)", path.filename().string().c_str(),
            renderer_.Width(), renderer_.Height(), scene.preset,
            clouds::DebugModeName(scene.debug));
    }

    options_.cloud = originalParams;
    cloudTime_ = originalTime;

    std::ostringstream info;
    info << "OGSR Clouds V1 capture\n";
    info << "timestamp: " << TimestampUtc() << "\n";
    info << "adapter: " << renderer_.AdapterInfo() << "\n";
    info << "warp: " << (renderer_.UsingWarp() ? "yes" : "no") << "\n";
    info << "noise format: " << noise_.FormatName() << "\n";
    info << "noise params: " << noise_.Params().Describe() << "\n";
    info << "backbuffer: " << renderer_.Width() << "x" << renderer_.Height()
         << "\n";
    info << "cloud output: " << cloudRayMarcher_.OutputWidth() << "x"
         << cloudRayMarcher_.OutputHeight() << " " << cloudRayMarcher_.FormatName()
         << "\n";
    info << "cloud steps: " << options_.cloud.stepCount << "\n";
    info << "cloud time: " << options_.initialCloudTime << " s\n";
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

    cloudProfiler_.CollectAll();
    Log("cloud capture complete: %zu images in %s", files.size(),
        WideToUtf8(directory.wstring()).c_str());
    return renderer_.DebugErrorCount() == 0 ? 0 : 2;
}

int Application::RunCapture()
{
    if (options_.captureDir.empty())
        Fail("capture directory is empty");
    if (options_.mode == clouds::RenderMode::Clouds)
        return RunCloudCapture(options_.captureDir);

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
