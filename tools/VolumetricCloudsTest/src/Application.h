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
#include "WeatherMap.h"

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
    uint32_t weatherSeed = 7331;
    clouds::CloudAblationMode cloudAblation = clouds::CloudAblationMode::None;
    clouds::ShapePrototype shapePrototype = clouds::ShapePrototype::None;
    clouds::CloudFormula cloudFormula = clouds::CloudFormula::None;
    clouds::CloudDensityModel densityModel = clouds::CloudDensityModel::Legacy;
    clouds::ReferenceStage referenceStage = clouds::ReferenceStage::Height;
    clouds::ReferenceControls reference{};
    bool referenceStageExplicit = false;
    DXGI_FORMAT preferredFormat = DXGI_FORMAT_UNKNOWN;
    bool selfTest = false;
    bool capture = false;
    bool resizeTest = false;
    bool injectDebugError = false;
    bool help = false;
    bool cloudStepsExplicit = false;
    bool cloudDebugExplicit = false;
    bool cloudCoverageExplicit = false;
    bool cloudDensityExplicit = false;
    bool verticalNoiseExplicit = false;
    bool shadowStepsExplicit = false;
    bool sunShadowDistanceExplicit = false;
    bool lightingExplicit = false;
    bool jitterExplicit = false;
    bool atmosphereExplicit = false;
    bool selfShadowExplicit = false;
    bool sliceHeightExplicit = false;
    bool sliceExtentExplicit = false;
    bool probeColumnExplicit = false;
    bool topFadeExplicit = false;
    bool formulaBiasExplicit = false;
    bool cloudScaleExplicit = false;
    bool sunElevationExplicit = false;
    bool sunAzimuthExplicit = false;
    bool cameraExplicit = false;
    bool cameraYawExplicit = false;
    bool cameraPitchExplicit = false;
    bool fovExplicit = false;
    DirectX::XMFLOAT3 cameraPosition{ 0.0f, 500.0f, -1200.0f };
    float cameraYawDegrees = 0.0f;
    float cameraPitchDegrees = 9.0f;
    float fovYDegrees = 62.0f;
    std::string qualityProfile;
    std::filesystem::path sequenceDir;
    std::string motionName = "static";
    bool windDisabled = false;
    float initialCloudTime = 0.0f;
    uint32_t warmupFrames = 0;
    uint32_t cloudDivisor = 2;
    uint32_t sliceAxis = 0;
    std::filesystem::path noiseSliceCaptureDir;
    uint32_t noiseSliceIndex = 0;
    std::string sceneName;
    uint32_t frames = 0;
    uint32_t windowWidth = 1280;
    uint32_t windowHeight = 720;
    std::filesystem::path captureDir;
    // A02c Float32 diagnostics.
    bool densityVolume = false;
    // A03a: write the macro-stage channels alongside the density volume.
    // Requires --density-volume=1 and --field-mode=macro.
    bool macroProbe = false;
    // F01: sample DensityColumns on exactly the DensityVolume grid (same XZ
    // cell and Y centres) for a shared-coordinate comparison.
    bool columnsCrosscheck = false;
    bool volumeOriginExplicit = false;
    DirectX::XMFLOAT3 volumeOrigin{ 0.0f, 0.0f, 0.0f };
    float volumeCellSize = 0.0f; // 0 = derive from --volume-res
    uint32_t volumeResX = 128;
    uint32_t volumeResY = 96;
    uint32_t volumeResZ = 128;
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
    int RunCloudSequence(const std::filesystem::path& directory);
    int RunNoiseSliceCapture(const std::filesystem::path& directory);

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
    void StartCloudTransition(const std::string& name, float seconds);
    void UpdateCloudTransition(float deltaSeconds);
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
    WeatherMap weatherMap_;
    clouds::Camera camera_;

    ViewConstants view_{};
    float slice_ = 64.0f;
    uint32_t channel_ = 2;
    uint32_t sliceAxis_ = 0;
    bool diagnostic_ = false;
    bool animate_ = false;
    bool noiseDirty_ = false;
    bool running_ = true;
    bool initialized_ = false;
    bool cloudPaused_ = false;
    bool mouseLook_ = false;
    bool cloudFramePathLogged_ = false;
    bool weatherTransitionActive_ = false;
    float weatherTransitionElapsed_ = 0.0f;
    float weatherTransitionDuration_ = 0.0f;
    std::string weatherTransitionTarget_;
    clouds::CloudParams weatherTransitionStart_{};
    clouds::CloudParams weatherTransitionTargetParams_{};
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;
    float cloudTime_ = 0.0f;
    std::string cloudPreset_ = "clear";
    clouds::CloudDebugMode cloudDebug_ = clouds::CloudDebugMode::Final;
    uint64_t renderedFrames_ = 0;
    std::chrono::steady_clock::time_point lastTitleUpdate_{};
};
