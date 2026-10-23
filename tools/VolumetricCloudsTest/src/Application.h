#pragma once

#include "NoiseTypes.h"
#include "NoiseVolume.h"
#include "AuditPolicy.h"
#include "CloudCompositor.h"
#include "CloudProfiler.h"
#include "CloudRayMarcher.h"
#include "CloudTypes.h"
#include "Renderer.h"
#include "SliceViewer.h"
#include "Window.h"

#include <chrono>
#include <cstdint>
#include <dxgiformat.h>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

struct AppOptions
{
    Renderer::Options renderer{};
    NoiseParams noise{};
    clouds::RenderMode mode = clouds::RenderMode::Slices;
    clouds::CloudParams cloud{};
    clouds::CloudDebugMode cloudDebug = clouds::CloudDebugMode::Final;
    std::string cloudPreset = "clear";
    DXGI_FORMAT preferredFormat = DXGI_FORMAT_UNKNOWN;
    bool selfTest = false;
    bool capture = false;
    bool resizeTest = false;
    bool help = false;
    bool cloudStepsExplicit = false;
    bool cloudCoverageExplicit = false;
    bool cloudDensityExplicit = false;
    float initialCloudTime = 0.0f;
    uint32_t frames = 0;
    uint32_t windowWidth = 1280;
    uint32_t windowHeight = 720;
    std::filesystem::path captureDir;
};

class Application
{
public:
    explicit Application(AppOptions options);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int Run();
    bool Shutdown();

private:
    bool Initialize();
    audit::ValidationStatus PreflightValidation();
    void WritePreflightReport(const audit::ValidationStatus& status);
    int RunInteractive();
    int RunSelfTest();
    int RunCloudSelfTest(std::ostringstream& report);
    int RunCapture();
    int RunCloudCapture(const std::filesystem::path& directory);

    bool RenderOneFrame(bool capture, std::vector<uint8_t>* pixels);
    void RegenerateIfNeeded();
    void ChangeResolution(uint32_t resolution);
    void OnWindowEvent(UINT message, WPARAM wparam, LPARAM lparam);
    void HandleKey(WPARAM key, LPARAM lparam);
    void HandleCloudKey(WPARAM key, LPARAM lparam);
    void UpdateViewConstants();
    void UpdateCloudInput(float deltaSeconds);
    void UpdateTitle(bool force);
    void PrintHelp() const;
    void MarkNoiseDirty(const char* reason);
    void ApplyCloudPreset(const char* name, bool logChange);
    clouds::CloudFrameConstants BuildCloudConstants(
        clouds::CloudDebugMode debug) const;

    AppOptions options_;
    Window window_;
    Renderer renderer_;
    NoiseVolume noise_;
    SliceViewer viewer_;
    CloudRayMarcher cloudRayMarcher_;
    CloudCompositor cloudCompositor_;
    CloudProfiler cloudProfiler_;
    clouds::Camera camera_;

    ViewConstants view_{};
    float slice_ = 64.0f;
    uint32_t channel_ = 2;
    bool diagnostic_ = false;
    bool animate_ = false;
    bool noiseDirty_ = false;
    bool running_ = true;
    bool initialized_ = false;
    bool cloudPaused_ = false;
    bool mouseLook_ = false;
    bool cloudFramePathLogged_ = false;
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;
    float cloudTime_ = 0.0f;
    std::string cloudPreset_ = "clear";
    clouds::CloudDebugMode cloudDebug_ = clouds::CloudDebugMode::Final;
    uint64_t renderedFrames_ = 0;
    std::chrono::steady_clock::time_point lastTitleUpdate_{};
};
