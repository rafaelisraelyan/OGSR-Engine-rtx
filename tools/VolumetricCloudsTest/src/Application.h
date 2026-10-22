#pragma once

#include "NoiseTypes.h"
#include "NoiseVolume.h"
#include "AuditPolicy.h"
#include "Renderer.h"
#include "SliceViewer.h"
#include "Window.h"

#include <chrono>
#include <cstdint>
#include <dxgiformat.h>
#include <filesystem>
#include <string>
#include <vector>

struct AppOptions
{
    Renderer::Options renderer{};
    NoiseParams noise{};
    DXGI_FORMAT preferredFormat = DXGI_FORMAT_UNKNOWN;
    bool selfTest = false;
    bool capture = false;
    bool resizeTest = false;
    bool help = false;
    uint32_t frames = 0;
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
    int RunCapture();

    bool RenderOneFrame(bool capture, std::vector<uint8_t>* pixels);
    void RegenerateIfNeeded();
    void ChangeResolution(uint32_t resolution);
    void OnWindowEvent(UINT message, WPARAM wparam, LPARAM lparam);
    void HandleKey(WPARAM key, LPARAM lparam);
    void UpdateViewConstants();
    void UpdateTitle(bool force);
    void PrintHelp() const;
    void MarkNoiseDirty(const char* reason);

    AppOptions options_;
    Window window_;
    Renderer renderer_;
    NoiseVolume noise_;
    SliceViewer viewer_;

    ViewConstants view_{};
    float slice_ = 64.0f;
    uint32_t channel_ = 2;
    bool diagnostic_ = false;
    bool animate_ = false;
    bool noiseDirty_ = false;
    bool running_ = true;
    bool initialized_ = false;
    uint64_t renderedFrames_ = 0;
    std::chrono::steady_clock::time_point lastTitleUpdate_{};
};
