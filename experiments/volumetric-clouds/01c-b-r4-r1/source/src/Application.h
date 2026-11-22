#pragma once

#include "NoiseTypes.h"
#include "NoiseVolume.h"
#include "AuditPolicy.h"
#include "CloudCompositor.h"
#include "CloudProfiler.h"
#include "CloudRayMarcher.h"
#include "CloudTypes.h"
#include "WeatherTimeline.h"
#include "CloudSettings.h"
#include "CloudGraphicsState.h"
#include "Renderer.h"
#include "SliceViewer.h"
#include "Window.h"
#include "WeatherMap.h"

#include <chrono>
#include <cstdint>
#include <dxgiformat.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct AppOptions
{
    Renderer::Options renderer{};
    NoiseParams noise{};
    clouds::RenderMode mode = clouds::RenderMode::Slices;
    clouds::CloudParams cloud{};
    clouds::CloudGraphicsSettings cloudGraphics{};
    bool cloudsExplicit = false;
    bool cloudQualityExplicit = false;
    bool cloudRenderModeExplicit = false;
    std::filesystem::path cloudSettingsPath;
    bool saveCloudSettings = false;
    std::filesystem::path saveCloudSettingsPath;
    std::string weatherSource = "developer-standalone";
    clouds::CloudDebugMode cloudDebug = clouds::CloudDebugMode::Final;
    std::string cloudPreset = "clear";
    bool weatherModeExplicit = false;
    clouds::StaticWeatherMode weatherMode = clouds::StaticWeatherMode::Clear;
    bool weatherTransitionExplicit = false;
    clouds::StaticWeatherMode transitionSource = clouds::StaticWeatherMode::Overcast;
    clouds::StaticWeatherMode transitionTarget = clouds::StaticWeatherMode::RainSF;
    float transitionPhase = 0.0f;
    bool weatherTimelineExplicit = false;
    bool weatherTimelinePlay = false;
    bool p0Matrix = false;
    bool p1Matrix = false;
    float shadowOdEarlyOutTransmittance = 0.0f;
    bool shadowInnerOnly = false;
    bool weatherTimelineTimeExplicit = false;
    bool weatherTimelineDurationExplicit = false;
    bool weatherTimelineInterruptExplicit = false;
    bool weatherTimelineInterruptDurationExplicit = false;
    float weatherTimelineTime = 0.0f;
    clouds::WeatherTimelinePlan weatherTimeline{};
    std::string frameTimingLogPath;
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
    bool breakupExplicit = false;
    bool frontHeightExplicit = false;
    bool stormFrontExplicit = false;
    bool stormFrontDebugExplicit = false;
    bool captureRawCloud = false;
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
    std::filesystem::path runtimeSettingsTestDir;
    std::filesystem::path runtimeSettingsFaultTestDir;
    std::string motionName = "static";
    bool windDisabled = false;
    float initialCloudTime = 0.0f;
    bool holdCloudTime = false;
    float fixedFrameDelta = 0.0f;
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
    int RunP0Matrix();
    int RunRuntimeSettingsTest();
    int RunRuntimeSettingsFaultTest();
    void ApplyPendingCloudSettings();
    bool ApplyCloudGraphicsState(
        const clouds::CloudGraphicsState& state, const char* reason);
    void RequestCloudQualityStep(int direction);
    void RequestCloudsToggle();
    void LogRuntimeWeatherState(const char* stage) const;
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
    clouds::CloudParams BuildWeatherTransitionEndpoint(
        clouds::StaticWeatherMode mode) const;
    void ApplyWeatherTransitionOverrides(clouds::CloudParams& params) const;
    void StartWeatherTimeline();
    void UpdateWeatherTimeline(float deltaSeconds);
    clouds::WeatherTimelineVisual EvaluateTimeline(float timeSeconds) const;
    void RequestWeatherTimelineInterrupt(clouds::StaticWeatherMode target);
    clouds::CloudFrameConstants BuildTimelineConstants(
        clouds::CloudDebugMode debug) const;
    void WriteFrameTiming();

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
    bool cloudDisabledLogged_ = false;
    bool cloudsEnabled_ = true;
    clouds::CloudRenderMode cloudRenderMode_ =
        clouds::CloudRenderMode::Volumetric3D;
    clouds::CloudQuality cloudQuality_ = clouds::CloudQuality::Quality;
    clouds::CloudGraphicsStateMachine cloudSettingsState_;
    uint64_t lastAppliedSettingsGeneration_ = 0;
    bool gpuConfigurationFatal_ = false;
    bool weatherTransitionActive_ = false;
    float weatherTransitionElapsed_ = 0.0f;
    float weatherTransitionDuration_ = 0.0f;
    std::string weatherTransitionTarget_;
    clouds::CloudParams weatherTransitionStart_{};
    clouds::CloudParams weatherTransitionTargetParams_{};
    bool weatherTimelineActive_ = false;
    float weatherTimelineTime_ = 0.0f;
    std::ofstream frameTimingLog_;
    uint64_t frameTimingRow_ = 0;
    std::string frameTimingLabel_;
    int frameTimingBlock_ = -1;
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;
    float cloudTime_ = 0.0f;
    std::string cloudPreset_ = "clear";
    clouds::CloudDebugMode cloudDebug_ = clouds::CloudDebugMode::Final;
    uint64_t renderedFrames_ = 0;
    std::chrono::steady_clock::time_point lastTitleUpdate_{};
};
