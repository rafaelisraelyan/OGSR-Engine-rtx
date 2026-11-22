#include "Application.h"
#include "Common.h"
#include "CloudSettings.h"
#if defined(OGSR_WEATHER_SHIPPING_CONTROLS)
#include "WeatherEnvironment.h"
#endif

#include <Windows.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace
{
bool ParseUint32(const std::wstring& text, uint32_t& value)
{
    if (text.empty())
        return false;
    errno = 0;
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != L'\0' || errno == ERANGE ||
        parsed > 0xfffffffful)
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool ParseInt(const std::wstring& text, int& value)
{
    if (text.empty())
        return false;
    errno = 0;
    wchar_t* end = nullptr;
    const long parsed = wcstol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != L'\0' || errno == ERANGE)
        return false;
    value = static_cast<int>(parsed);
    return true;
}

bool ParseFloat(const std::wstring& text, float& value)
{
    if (text.empty())
        return false;
    errno = 0;
    wchar_t* end = nullptr;
    const float parsed = wcstof(text.c_str(), &end);
    if (end == text.c_str() || *end != L'\0' || errno == ERANGE ||
        !std::isfinite(parsed))
        return false;
    value = parsed;
    return true;
}

bool ParseWindowSize(const std::wstring& text, uint32_t& width, uint32_t& height)
{
    const size_t separator = text.find(L'x');
    if (separator == std::wstring::npos)
        return false;
    if (!ParseUint32(text.substr(0, separator), width) ||
        !ParseUint32(text.substr(separator + 1), height))
        return false;
    return width >= 320 && width <= 7680 && height >= 200 && height <= 4320;
}

#if defined(OGSR_WEATHER_SHIPPING_CONTROLS)
// Shipping-controls contract: these are the only command-line options the
// harness adapter may register. Everything weather/debug/capture-related is
// absent by construction; this pre-scan makes accidental additions fail loudly.
bool IsShippingAllowedArgument(const std::wstring& argument)
{
    // Public graphics API plus the minimal standalone contract-test lifecycle.
    // --seed/--size/--format are deliberately absent: all three change cloud
    // noise/texture generation and are developer-only noise controls.
    // --adapter/--warp are harness device diagnostics, not a game API.
    // --mode/--window/--frames/--warmup/--resize-test/--no-vsync below are
    // contract-test lifecycle only; the real game shipping adapter must not
    // carry this CLI.
    static const wchar_t* kAllowed[] = {
        L"--mode", L"--window", L"--frames", L"--warmup", L"--no-vsync",
        L"--resize-test", L"--help", L"--profile", L"--clouds",
        L"--cloud-quality", L"--cloud-render-mode", L"--cloud-settings",
        L"--save-cloud-settings",
    };
    for (const wchar_t* name : kAllowed)
    {
        const std::wstring prefix = std::wstring(name) + L"=";
        if (argument == name || argument.rfind(prefix, 0) == 0)
            return true;
    }
    return false;
}
#endif


void PrintCliHelp()
{
#if defined(OGSR_WEATHER_SHIPPING_CONTROLS)
    Log("OGSRCloudsTest - shipping-controls contract harness");
    Log("");
    Log("Public cloud graphics options:");
    Log("  --clouds=on|off                   render clouds on/off (weather unchanged)");
    Log("  --cloud-quality=performance|balanced|quality|reference");
    Log("  --profile=...                     alias for --cloud-quality");
    Log("  --cloud-render-mode=volumetric3d  public render-mode contract");
    Log("  --cloud-settings[=FILE]           load validated public graphics config");
    Log("  --save-cloud-settings[=FILE]      atomically save public graphics config");
    Log("");
    Log("Harness lifecycle (contract-test only; not a game shipping CLI):");
    Log("  --mode=clouds  --window=WxH  --frames=N  --warmup=N");
    Log("  --no-vsync  --resize-test  --help");
    Log("");
    Log("Not registered: --seed, --size, --format, --adapter, --warp and all");
    Log("developer weather/timeline/preset/density/shadow/capture/debug options.");
    Log("Weather is supplied by the CEnvironment contract snapshot.");
    return;
#endif
    Log("OGSRCloudsTest - standalone D3D12 volumetric clouds (V0 slices + V2.1 clouds)");
    Log("");
    Log("Usage:");
    Log("  OGSRCloudsTest.exe [options]");
    Log("");
    Log("Modes:");
    Log("  --mode=slices        V0 noise slice viewer (default, backward compatible)");
    Log("  --mode=clouds        V2.1 volumetric cloud ray marching");
    Log("  --preset=clear       Clear Day cloud preset (clouds mode)");
    Log("  --preset=partly      Partly Cloudy cloud preset (clouds mode)");
    Log("  --preset=overcast    Overcast cloud preset (clouds mode)");
    Log("  --preset=rain        Approaching rain front (clouds mode)");
    Log("  --preset=storm       Storm cloud masses (clouds mode)");
    Log("  --preset=sunset      Low sun / sunset lighting (clouds mode)");
    Log("");
    Log("Options:");
    Log("  --size=64|128|256    initial 3D noise resolution (default 128)");
    Log("  --seed=N             deterministic noise seed (default 1337)");
    Log("  --format=16f|unorm   force the volume format (default: 16F, fallback UNORM)");
    Log("  --adapter=N          select DXGI adapter index from the startup list");
    Log("  --warp               force the Microsoft WARP software adapter");
    Log("  --window=1280x720    client size (default 1280x720)");
    Log("  --cloud-scale=half|full  cloud output scale (default half)");
    Log("  --steps=N            cloud ray-march steps, 8..256 (default: preset)");
    Log("  --shadow-steps=N     sun shadow samples, 1..16 (default 6)");
    Log("  --sun-shadow-distance=M  sun shadow path cap in meters (default 6000)");
    Log("  --jitter=off|spatial deterministic ray jitter (default spatial)");
    Log("  --sun-elevation=F    sun elevation in degrees (diagnostic override)");
    Log("  --sun-azimuth=F      sun azimuth in degrees (diagnostic override)");
    Log("  --lighting=full|sun-only|ambient-only  lighting isolation");
    Log("  --atmosphere=on|off  analytic aerial perspective (default on)");
    Log("  --self-shadow=on|off self-shadow diagnostic (default on)");
    Log("  --slice-height=F     horizontal density slice height (0..1)");
    Log("  --slice-extent=F     horizontal density slice radius (km, 2..60)");
    Log("  --probe-column=F     ray-profile screen column (0..1)");
    Log("  --top-fade=F         cumulus top fade width (0.02..0.30)");
    Log("  --profile=performance|balanced|quality|reference");
    Log("  --clouds=on|off      public cloud rendering switch (weather unchanged)");
    Log("  --cloud-quality=performance|balanced|quality|reference  public quality (alias of --profile)");
    Log("  --cloud-render-mode=volumetric3d  public render-mode contract (classic2d rejected standalone)");
    Log("  --cloud-settings[=FILE]  load validated public graphics settings");
    Log("  --save-cloud-settings[=FILE]  atomically save public graphics settings");
    Log("  --sequence=DIR       render a deterministic frame sequence and exit");
    Log("  --runtime-settings-test=DIR  run runtime quality/clouds/state-machine captures");
#if defined(OGSR_CLOUDS_FAULT_INJECTION)
    Log("  --runtime-settings-fault-test=DIR  isolated GPU failure/retry test");
#endif
    Log("  --motion=static|pan-slow|pan-fast|up|fly|fly-level  sequence camera motion");
    Log("  --camera=x,y,z       sequence start position");
    Log("  --camera-yaw=F       sequence start yaw (degrees)");
    Log("  --camera-pitch=F     sequence start pitch (degrees)");
    Log("  --fov=F              vertical FOV override (degrees, 20..120)");
    Log("  --wind=on|off        wind animation for sequences (default on)");
    Log("  --coverage=F         cloud coverage 0..1");
    Log("  --density=F          cloud density multiplier 0..2");
    Log("  --storm-front=on|off  V-SF-GPU-01 curved 3D storm-front density");
    Log("  --storm-front-debug=off|field|mass|vertical");
#if defined(OGSR_WEATHER_DEVELOPER_TOOLS)
    Log("  --weather-mode=clear|partly|overcast|rain_sf|storm_preview  developer static weather state");
    Log("  --weather-transition=overcast:rain_sf|rain_sf:overcast  developer B1 transition");
    Log("  --transition-phase=F  transition phase 0..1 (default 0)");
    Log("  --weather-timeline[=clear:partly:overcast:rain_sf:partly]  developer B2A timeline");
    Log("  --timeline-duration=F  per-segment duration seconds (default 2.0)");
    Log("  --timeline-time=F  fixed timeline time for capture");
    Log("  --timeline-play  animate timeline in --frames/--sequence");
    Log("  --timeline-interrupt=PHASE:TARGET|SEG:PHASE:TARGET  interrupt event");
    Log("  --timeline-interrupt-duration=F  post-interrupt duration");
    Log("  --frame-timing-log=FILE  developer per-frame GPU timing CSV");
    Log("  --hold-cloud-time  keep cloud animation time fixed during --frames");
    Log("  --fixed-frame-delta=F  developer test-only fixed simulation step");
    Log("  --p0-matrix  run interleaved B2B-P0 timing matrix");
    Log("  --p1-matrix  run interleaved P1 baseline/candidate matrix");
    Log("  --shadow-od-early-out=T  opt-in mode3 shadow OD early-out (0.001|0.01|0.05)");
    Log("  --shadow-inner-only  experimental mode3 inner-only shadow A/B control");
#endif
    Log("  --capture-raw-cloud=1|0  write float cloud-target readback (.bin)");
    Log("  --debug-mode=final|density|transmittance|steps|occupancy|weather|cloud-type|density-slice|density-horizontal|ray-profile|density-vertical-z|macro-shape|cross-type-difference|noise-field|view-optical-depth|storm-front-slice|storm-od|storm-t|storm-mask");
    Log("  --slice-axis=xy|xz|yz  V0 noise slice orientation");
    Log("  --time=SECONDS       fixed cloud time for captures/selftest scenes");
    Log("  --weather-seed=N     deterministic weather map seed (default 7331)");
    Log("  --vertical-noise=F   override vertical noise scale (diagnostic)");
    Log("  --ablation=MODE      profiling/diagnostic: no-weather, no-mid, no-base, no-fine,");
    Log("                       shadow-2, shadow-4, no-type, no-warp, macro-only, no-shadow,");
    Log("                       base-3d, no-base-warp, no-height-variation, no-anisotropy, flat-bottom");
    Log("  --shape-prototype=A|B|none  experimental cloud shape architecture");
    Log("  --formula=none|F1|F2|F3|F4|F5  density formula benchmark variant");
    Log("  --density-model=legacy|reference  legacy V2.3 or V2.3h reference model");
    Log("  --reference-stage=base|height|coverage|erosion  reference model stage (H1-H4)");
    Log("  --noise-mix=r|b|rb   reference shape channel mixture");
    Log("  --coordinate-mode=plain|warped  reference soft 3D warp candidate");
    Log("  --field-mode=direct|multi-r|hash-mod|macro  A02/A03 base-field candidate");
    Log("  --multi-scale=F      multi-r second R scale (0.1..4, default 0.73)");
    Log("  --multi-weight=F     multi-r second R weight (0..1, default 0.25)");
    Log("  --modulation-amplitude=F  hash-mod shape modulation (0..0.5, default 0.10)");
    Log("  --macro-scale=F      A03 macro primary scale in metres (2000..40000)");
    Log("  --macro-secondary-scale=F  A03 macro second scale (800..20000)");
    Log("  --macro-threshold=F  A03 macro occupancy threshold (0.05..0.95)");
    Log("  --macro-softness=F   A03 macro edge softness (0.01..0.50)");
    Log("  --macro-detail=F     A03 periodic detail weight (0..1)");
    Log("  --macro-secondary-weight=F  A03 second octave weight (0..0.7)");
    Log("  --macro-edge-jitter=F  A03 detail edge jitter (0..0.3)");
    Log("  --macro-height=F     A03 macro height weight (0..1)");
    Log("  --macro-seed=N       A03 macro seed (0..1e9)");
    Log("  --macro-coverage=0|1 A03a: coverage remap on macro mass before detail");
    Log("  --macro-seed-hash=0|1 A03a: full-precision macro seed phase");
    Log("  --macro-detail-compensate=0|1 A03a: compensate mean detail attenuation");
    Log("  --macro-coverage-calibrate=0|1 A03a: quantile occupancy calibration");
    Log("  --macro-weather=legacy|quantile|local  F01 W1/W2 weather occupancy");
    Log("  --local-occupancy=0|1 A03b: local occupancy field (3.2/1.4 km)");
    Log("  --storm-local=0|1  A03b: local field on S2new edges only");
    Log("  --macro-probe=0|1    A03a: Float32 macro-stage volume probe");
    Log("  --columns-crosscheck=0|1  F01: columns on the exact volume grid");
    Log("  --density-test=0|1|2 F01: analytic constant/stripe density probe");
    Log("  --reference-density-scale=F  reference density multiplier (0.5..2)");
    Log("  --storm-volume=0|1  A02b Storm volumetric height envelope");
    Log("  --storm-column=0|1  A02c corrected XZ-column Storm envelope");
    Log("  --storm-height-variation=F  Storm top variation in metres (0..2000)");
    Log("  --storm-bottom-variation=F  Storm base variation in metres (0..500)");
    Log("  --storm-gap=keep|macro  A03a: replace the 5 km storm gap with macro mass");
    Log("  --od-test-pattern=0|1  analytic OD bands in the Float32 probe");
    Log("  --density-volume=0|1  A02c Float32 reference density volume dump");
    Log("  --volume-origin=x,y,z  volume grid origin (default camera XZ, y=0)");
    Log("  --volume-cell=F  volume cell size in metres (default from res)");
    Log("  --volume-res=X,Y,Z  volume grid resolution (default 128,96,128)");
    Log("  --reference-shape-low=F  reference shape transfer lower edge (diagnostic)");
    Log("  --reference-shape-high=F reference shape transfer upper edge (diagnostic)");
    Log("  --noise-field-axis=xy|xz|yz  raw Float32 noise-field slice axis");
    Log("  --noise-field-offset=F       normalized offset along the slice normal (0..1)");
    Log("  --formula-bias=F     F1 threshold calibration (-0.20..0.20)");
    Log("  --debug              enable the D3D12 debug layer");
    Log("  --gpu-validation     enable GPU-based validation (implies --debug)");
    Log("  --test-no-validation test-only: simulate missing validation infrastructure");
    Log("  --test-no-info-queue test-only: hide ID3D12InfoQueue while debug layer is on");
    Log("  --test-inject-debug-error test-only: inject a D3D12 ERROR after cloud selftest");
    Log("  --no-vsync           disable vsync");
    Log("  --selftest           run V0 and V2.2 checks and exit");
    Log("  --capture[=DIR]      render captures and exit (mode-dependent)");
    Log("  --scene=NAME         capture only one named cloud scene (diagnostics)");
    Log("  --noise-slice-capture=DIR  XY/XZ/YZ NoiseVolume slices and exit");
    Log("  --noise-slice-index=N     fixed axis index (default resolution/2)");
    Log("  --frames=N           run N frames and exit (automation)");
    Log("  --warmup=N           discard GPU timings before frame N");
    Log("  --resize-test        resize the window during --frames to test ResizeBuffers");
    Log("  --help               show this help");
    Log("");
    Log("Slices controls: 1/2/3 channel, arrows/wheel Z slice, Space animation,");
    Log("D seam diagnostic, N regenerate, 4 toggle 64/128, G/F/O/W/B/E/K parameters.");
    Log("Clouds controls: LMB mouse look, WASD/QE move, Shift faster, 1/2 presets,");
    Log("5/6/7/8/9 debug modes, 0 weather, V cloud type, M cycle debug,");
    Log("J/L sun azimuth, I/K sun elevation, +/- density, [/] coverage,");
    Log(",/. wind speed, Z/X FOV, P pause, R reset scene, F2 switch slices/clouds,");
    Log("H/F1 help.");
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    InitLogging();

    Log("command line: %s", WideToUtf8(GetCommandLineW()).c_str());

    AppOptions options;
    clouds::ApplyClearPreset(options.cloud);
    std::string presetName = "clear";
    bool presetExplicit = false;
    std::string explicitPreset;
    bool transitionPhaseExplicit = false;
#ifdef _DEBUG
    options.renderer.debugLayer = true;
#endif

#if defined(OGSR_WEATHER_SHIPPING_CONTROLS)
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring argument = argv[i];
        if (argument == L"--cloud-settings" ||
            argument == L"--save-cloud-settings")
        {
            if (i + 1 < argc && argv[i + 1][0] != L'-')
                ++i;
            continue;
        }
        if (argument == L"--mode=slices")
        {
            LogError("shipping-controls build exposes cloud rendering only; "
                     "--mode=slices is a developer V0 viewer");
            return 2;
        }
        if (!IsShippingAllowedArgument(argument))
        {
            LogError("shipping-controls build does not register "
                     "developer/harness option: %s",
                     WideToUtf8(argument).c_str());
            return 2;
        }
    }
#endif

    for (int i = 1; i < argc; ++i)
    {
        const std::wstring argument = argv[i];
        // Public cloud graphics options are handled before the legacy argument
        // chain. This keeps the legacy chain below the MSVC nested-block limit
        // and makes the public/developer surface boundary explicit.
        if (argument == L"--clouds" ||
            argument.rfind(L"--clouds=", 0) == 0)
        {
            if (argument == L"--clouds")
            {
                LogError("--clouds requires on or off");
                return 2;
            }
            const std::wstring value = argument.substr(9);
            bool enabled = true;
            if (value == L"on")
                enabled = true;
            else if (value == L"off")
                enabled = false;
            else
            {
                LogError("invalid --clouds value: %s (expected on or off)",
                         WideToUtf8(value).c_str());
                return 2;
            }
            if (options.cloudsExplicit &&
                options.cloudGraphics.cloudsEnabled != enabled)
            {
                LogError("conflicting --clouds values");
                return 2;
            }
            options.cloudGraphics.cloudsEnabled = enabled;
            options.cloudsExplicit = true;
            continue;
        }
        if (argument.rfind(L"--cloud-quality=", 0) == 0)
        {
            const std::wstring value = argument.substr(16);
            clouds::CloudQuality quality = clouds::CloudQuality::Quality;
            if (!clouds::ParseCloudQuality(WideToUtf8(value), quality))
            {
                LogError("invalid --cloud-quality value: %s (expected "
                         "performance, balanced, quality or reference)",
                         WideToUtf8(value).c_str());
                return 2;
            }
            if (options.cloudQualityExplicit &&
                options.cloudGraphics.quality != quality)
            {
                LogError("conflicting --cloud-quality/--profile values");
                return 2;
            }
            options.cloudGraphics.quality = quality;
            options.cloudQualityExplicit = true;
            options.qualityProfile = clouds::CloudQualityName(quality);
            continue;
        }
        if (argument.rfind(L"--cloud-render-mode=", 0) == 0)
        {
            const std::wstring value = argument.substr(20);
            clouds::CloudRenderMode renderMode =
                clouds::CloudRenderMode::Volumetric3D;
            if (!clouds::ParseCloudRenderMode(WideToUtf8(value), renderMode))
            {
                LogError("invalid --cloud-render-mode value: %s (expected "
                         "volumetric3d or classic2d)",
                         WideToUtf8(value).c_str());
                return 2;
            }
            if (renderMode == clouds::CloudRenderMode::Classic2D)
            {
                LogError("--cloud-render-mode=classic2d is a future game-side "
                         "contract and is not implemented in standalone; use "
                         "volumetric3d or configure the safe fallback");
                return 2;
            }
            if (options.cloudRenderModeExplicit &&
                options.cloudGraphics.renderMode != renderMode)
            {
                LogError("conflicting --cloud-render-mode values");
                return 2;
            }
            options.cloudGraphics.renderMode = renderMode;
            options.cloudRenderModeExplicit = true;
            continue;
        }
        if (argument == L"--cloud-settings" ||
            argument.rfind(L"--cloud-settings=", 0) == 0)
        {
            if (argument == L"--cloud-settings")
            {
                if (i + 1 < argc && argv[i + 1][0] != L'-')
                    options.cloudSettingsPath = argv[++i];
                else
                    options.cloudSettingsPath = L"cloud_settings.json";
            }
            else
            {
                options.cloudSettingsPath = argument.substr(17);
            }
            if (options.cloudSettingsPath.empty())
            {
                LogError("--cloud-settings requires a path");
                return 2;
            }
            continue;
        }
        if (argument == L"--save-cloud-settings" ||
            argument.rfind(L"--save-cloud-settings=", 0) == 0)
        {
            options.saveCloudSettings = true;
            if (argument == L"--save-cloud-settings")
            {
                if (i + 1 < argc && argv[i + 1][0] != L'-')
                    options.saveCloudSettingsPath = argv[++i];
            }
            else
            {
                options.saveCloudSettingsPath = argument.substr(22);
            }
            if (options.saveCloudSettingsPath.empty())
                options.saveCloudSettingsPath = options.cloudSettingsPath;
            if (options.saveCloudSettingsPath.empty())
                options.saveCloudSettingsPath = L"cloud_settings.json";
            continue;
        }
#if defined(OGSR_WEATHER_DEVELOPER_TOOLS)
        if (argument.rfind(L"--fixed-frame-delta=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(21), value) ||
                value < 0.001f || value > 1.0f)
            {
                LogError("invalid --fixed-frame-delta value: %s "
                         "(expected 0.001..1.0 seconds)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.fixedFrameDelta = value;
            continue;
        }
        if (argument.rfind(L"--runtime-settings-test=", 0) == 0)
        {
            options.runtimeSettingsTestDir = argument.substr(24);
            if (options.runtimeSettingsTestDir.empty())
            {
                LogError("--runtime-settings-test requires a directory");
                return 2;
            }
            continue;
        }
#if defined(OGSR_CLOUDS_FAULT_INJECTION)
        if (argument.rfind(L"--runtime-settings-fault-test=", 0) == 0)
        {
            options.runtimeSettingsFaultTestDir = argument.substr(30);
            if (options.runtimeSettingsFaultTestDir.empty())
            {
                LogError("--runtime-settings-fault-test requires a directory");
                return 2;
            }
            continue;
        }
#endif
        if (argument == L"--p1-matrix")
        {
            options.p1Matrix = true;
            continue;
        }
        if (argument == L"--shadow-inner-only")
        {
            options.shadowInnerOnly = true;
            continue;
        }
        if (argument.rfind(L"--shadow-od-early-out=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(22), value) ||
                !(std::fabs(value - 0.001f) < 1.0e-6f ||
                  std::fabs(value - 0.01f) < 1.0e-6f ||
                  std::fabs(value - 0.05f) < 1.0e-6f))
            {
                LogError("invalid --shadow-od-early-out value: %s "
                         "(expected 0.001, 0.01 or 0.05)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.shadowOdEarlyOutTransmittance = value;
            continue;
        }
#endif
        if (argument == L"--warp")
        {
            options.renderer.forceWarp = true;
        }
        else if (argument == L"--debug")
        {
            options.renderer.debugLayer = true;
        }
        else if (argument == L"--no-debug")
        {
            options.renderer.debugLayer = false;
            options.renderer.gpuValidation = false;
        }
        else if (argument == L"--gpu-validation")
        {
            options.renderer.debugLayer = true;
            options.renderer.gpuValidation = true;
        }
        else if (argument.rfind(L"--mode=", 0) == 0)
        {
            const std::wstring mode = argument.substr(7);
            if (mode == L"slices")
                options.mode = clouds::RenderMode::Slices;
            else if (mode == L"clouds")
                options.mode = clouds::RenderMode::Clouds;
            else
            {
                LogError("invalid --mode value: %s (expected slices or clouds)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--preset=", 0) == 0)
        {
            const std::wstring preset = argument.substr(9);
            if (!clouds::ApplyPreset(WideToUtf8(preset), options.cloud))
            {
                LogError("invalid --preset value: %s "
                         "(expected clear, partly, overcast, rain, storm, sunset)",
                         WideToUtf8(preset).c_str());
                return 2;
            }
            presetName = WideToUtf8(preset);
            options.cloudPreset = presetName;
            presetExplicit = true;
            explicitPreset = presetName;
        }
#if defined(OGSR_WEATHER_DEVELOPER_TOOLS)
        else if (argument.rfind(L"--weather-mode=", 0) == 0)
        {
            const std::wstring mode = argument.substr(15);
            clouds::StaticWeatherMode parsed = clouds::StaticWeatherMode::Clear;
            if (!clouds::ParseStaticWeatherMode(WideToUtf8(mode), parsed))
            {
                LogError("invalid --weather-mode value: %s "
                         "(expected clear, partly, overcast, rain_sf, storm_preview)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
            options.weatherMode = parsed;
            options.weatherModeExplicit = true;
            clouds::ApplyStaticWeatherMode(parsed, options.cloud);
            presetName = clouds::StaticWeatherModeBasePreset(parsed);
            options.cloudPreset = presetName;
        }
        else if (argument.rfind(L"--weather-transition=", 0) == 0)
        {
            const std::wstring spec = argument.substr(21);
            const size_t separator = spec.find(L':');
            if (separator == std::wstring::npos)
            {
                LogError("invalid --weather-transition value: expected SRC:DST");
                return 2;
            }
            clouds::StaticWeatherMode source = clouds::StaticWeatherMode::Overcast;
            clouds::StaticWeatherMode target = clouds::StaticWeatherMode::RainSF;
            if (!clouds::ParseStaticWeatherMode(
                    WideToUtf8(spec.substr(0, separator)), source) ||
                !clouds::ParseStaticWeatherMode(
                    WideToUtf8(spec.substr(separator + 1)), target))
            {
                LogError("invalid --weather-transition value: expected "
                         "overcast:rain_sf or rain_sf:overcast");
                return 2;
            }
            const bool supported =
                (source == clouds::StaticWeatherMode::Overcast &&
                 target == clouds::StaticWeatherMode::RainSF) ||
                (source == clouds::StaticWeatherMode::RainSF &&
                 target == clouds::StaticWeatherMode::Overcast);
            if (!supported)
            {
                LogError("--weather-transition Gate B1 supports only "
                         "overcast:rain_sf or rain_sf:overcast");
                return 2;
            }
            options.weatherTransitionExplicit = true;
            options.transitionSource = source;
            options.transitionTarget = target;
            options.transitionPhase = 0.0f;
            clouds::ApplyStaticWeatherMode(source, options.cloud);
            presetName = clouds::StaticWeatherModeBasePreset(source);
            options.cloudPreset = presetName;
        }
        else if (argument.rfind(L"--transition-phase=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(19), value) ||
                value < 0.0f || value > 1.0f)
            {
                LogError("invalid --transition-phase value: %s "
                         "(expected 0..1)", WideToUtf8(argument).c_str());
                return 2;
            }
            options.transitionPhase = value;
            transitionPhaseExplicit = true;
        }
        else if (argument == L"--weather-timeline" ||
                 argument.rfind(L"--weather-timeline=", 0) == 0)
        {
            std::vector<clouds::StaticWeatherMode> modes;
            if (argument == L"--weather-timeline")
            {
                modes = {
                    clouds::StaticWeatherMode::Clear,
                    clouds::StaticWeatherMode::Partly,
                    clouds::StaticWeatherMode::Overcast,
                    clouds::StaticWeatherMode::RainSF,
                    clouds::StaticWeatherMode::Partly,
                };
            }
            else
            {
                const std::wstring spec = argument.substr(19);
                size_t start = 0;
                while (start <= spec.size())
                {
                    const size_t separator = spec.find(L':', start);
                    const std::wstring token = spec.substr(
                        start, separator == std::wstring::npos
                            ? std::wstring::npos : separator - start);
                    clouds::StaticWeatherMode mode =
                        clouds::StaticWeatherMode::Clear;
                    if (token.empty() ||
                        !clouds::ParseStaticWeatherMode(
                            WideToUtf8(token), mode) ||
                        mode == clouds::StaticWeatherMode::StormPreview)
                    {
                        LogError("invalid --weather-timeline value: %s "
                                 "(expected colon-separated clear, partly, "
                                 "overcast, rain_sf; storm_preview excluded)",
                                 WideToUtf8(argument).c_str());
                        return 2;
                    }
                    modes.push_back(mode);
                    if (separator == std::wstring::npos)
                        break;
                    start = separator + 1;
                }
                if (modes.size() < 2)
                {
                    LogError("--weather-timeline requires at least two modes");
                    return 2;
                }
            }
            options.weatherTimelineExplicit = true;
            options.weatherTimeline.modes = std::move(modes);
            const clouds::StaticWeatherMode first =
                options.weatherTimeline.modes.front();
            clouds::ApplyStaticWeatherMode(first, options.cloud);
            presetName = clouds::StaticWeatherModeBasePreset(first);
            options.cloudPreset = presetName;
        }
        else if (argument.rfind(L"--timeline-duration=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(20), value) ||
                value < 0.05f || value > 60.0f)
            {
                LogError("invalid --timeline-duration value: %s "
                         "(expected 0.05..60)", WideToUtf8(argument).c_str());
                return 2;
            }
            options.weatherTimeline.segmentDuration = value;
            options.weatherTimelineDurationExplicit = true;
        }
        else if (argument.rfind(L"--timeline-time=", 0) == 0)
        {
            float value = -1.0f;
            if (!ParseFloat(argument.substr(16), value) || value < 0.0f)
            {
                LogError("invalid --timeline-time value: %s",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.weatherTimelineTime = value;
            options.weatherTimelineTimeExplicit = true;
        }
        else if (argument == L"--timeline-play")
        {
            options.weatherTimelinePlay = true;
        }
        else if (argument.rfind(L"--timeline-interrupt=", 0) == 0)
        {
            const std::wstring spec = argument.substr(21);
            std::vector<std::wstring> tokens;
            size_t start = 0;
            while (start <= spec.size())
            {
                const size_t separator = spec.find(L':', start);
                tokens.push_back(spec.substr(
                    start, separator == std::wstring::npos
                        ? std::wstring::npos : separator - start));
                if (separator == std::wstring::npos)
                    break;
                start = separator + 1;
            }
            if (tokens.size() < 2 || tokens.size() > 3)
            {
                LogError("invalid --timeline-interrupt value: expected "
                         "PHASE:TARGET or SEGMENT:PHASE:TARGET");
                return 2;
            }
            int segment = 0;
            float phase = 0.0f;
            const std::wstring& phaseText = tokens.size() == 3
                ? tokens[1] : tokens[0];
            const std::wstring& targetText = tokens.back();
            if ((tokens.size() == 3 &&
                 (!ParseInt(tokens[0], segment) || segment < 0)) ||
                !ParseFloat(phaseText, phase) ||
                phase < 0.0f || phase > 1.0f)
            {
                LogError("invalid --timeline-interrupt value: %s",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            clouds::StaticWeatherMode target =
                clouds::StaticWeatherMode::Overcast;
            if (!clouds::ParseStaticWeatherMode(
                    WideToUtf8(targetText), target) ||
                target == clouds::StaticWeatherMode::StormPreview)
            {
                LogError("invalid --timeline-interrupt target: %s",
                         WideToUtf8(targetText).c_str());
                return 2;
            }
            options.weatherTimeline.hasInterrupt = true;
            options.weatherTimelineInterruptExplicit = true;
            options.weatherTimeline.interruptSegment = segment;
            options.weatherTimeline.interruptPhase = phase;
            options.weatherTimeline.interruptTarget = target;
        }
        else if (argument.rfind(L"--timeline-interrupt-duration=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(30), value) ||
                value < 0.05f || value > 60.0f)
            {
                LogError("invalid --timeline-interrupt-duration value: %s",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.weatherTimeline.interruptDuration = value;
            options.weatherTimelineInterruptDurationExplicit = true;
        }
        else if (argument.rfind(L"--frame-timing-log=", 0) == 0)
        {
            options.frameTimingLogPath = WideToUtf8(argument.substr(19));
            if (options.frameTimingLogPath.empty())
            {
                LogError("--frame-timing-log requires a path");
                return 2;
            }
        }
        else if (argument == L"--hold-cloud-time")
        {
            options.holdCloudTime = true;
        }
        else if (argument == L"--p0-matrix")
        {
            options.p0Matrix = true;
        }
#endif
        else if (argument.rfind(L"--steps=", 0) == 0)
        {
            uint32_t value = 0;
            if (!ParseUint32(argument.substr(8), value) || value < 8 || value > 256)
            {
                LogError("invalid --steps value: %s (expected 8..256)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cloud.stepCount = value;
            options.cloudStepsExplicit = true;
        }
        else if (argument.rfind(L"--coverage=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(11), value) || value < 0.0f || value > 1.0f)
            {
                LogError("invalid --coverage value: %s (expected 0..1)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cloud.coverage = value;
            options.cloudCoverageExplicit = true;
        }
        else if (argument.rfind(L"--density=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(10), value) || value < 0.0f || value > 2.0f)
            {
                LogError("invalid --density value: %s (expected 0..2)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cloud.density = value;
            options.cloudDensityExplicit = true;
        }
        else if (argument.rfind(L"--window=", 0) == 0)
        {
            if (!ParseWindowSize(argument.substr(9), options.windowWidth,
                                options.windowHeight))
            {
                LogError("invalid --window value: %s (expected WxH, e.g. 1920x1080)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--weather-seed=", 0) == 0)
        {
            uint32_t value = 0;
            if (!ParseUint32(argument.substr(15), value))
            {
                LogError("invalid --weather-seed value: %s",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.weatherSeed = value;
        }
        else if (argument.rfind(L"--vertical-noise=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(17), value) || value < 0.1f ||
                value > 16.0f)
            {
                LogError("invalid --vertical-noise value: %s (expected 0.1..16)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cloud.verticalNoiseScale = value;
            options.verticalNoiseExplicit = true;
        }
        else if (argument.rfind(L"--shape-prototype=", 0) == 0)
        {
            const std::wstring mode = argument.substr(18);
            if (mode == L"none")
                options.shapePrototype = clouds::ShapePrototype::None;
            else if (mode == L"A" || mode == L"a")
                options.shapePrototype = clouds::ShapePrototype::A;
            else if (mode == L"B" || mode == L"b")
                options.shapePrototype = clouds::ShapePrototype::B;
            else
            {
                LogError("invalid --shape-prototype value: %s "
                         "(expected none, A or B)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--formula=", 0) == 0)
        {
            const std::wstring mode = argument.substr(10);
            if (mode == L"none")
                options.cloudFormula = clouds::CloudFormula::None;
            else if (mode == L"F1" || mode == L"f1")
                options.cloudFormula = clouds::CloudFormula::F1;
            else if (mode == L"F2" || mode == L"f2")
                options.cloudFormula = clouds::CloudFormula::F2;
            else if (mode == L"F3" || mode == L"f3")
                options.cloudFormula = clouds::CloudFormula::F3;
            else if (mode == L"F4" || mode == L"f4")
                options.cloudFormula = clouds::CloudFormula::F4;
            else if (mode == L"F5" || mode == L"f5")
                options.cloudFormula = clouds::CloudFormula::F5;
            else
            {
                LogError("invalid --formula value: %s "
                         "(expected none, F1..F5)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--density-model=", 0) == 0)
        {
            const std::wstring mode = argument.substr(16);
            if (mode == L"legacy")
                options.densityModel = clouds::CloudDensityModel::Legacy;
            else if (mode == L"reference")
                options.densityModel = clouds::CloudDensityModel::Reference;
            else
            {
                LogError("invalid --density-model value: %s "
                         "(expected legacy or reference)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--reference-stage=", 0) == 0)
        {
            const std::wstring stage = argument.substr(18);
            if (stage == L"base")
                options.referenceStage = clouds::ReferenceStage::Base;
            else if (stage == L"height")
                options.referenceStage = clouds::ReferenceStage::Height;
            else if (stage == L"coverage")
                options.referenceStage = clouds::ReferenceStage::Coverage;
            else if (stage == L"erosion")
                options.referenceStage = clouds::ReferenceStage::Erosion;
            else
            {
                LogError("invalid --reference-stage value: %s "
                         "(expected base, height, coverage or erosion)",
                         WideToUtf8(stage).c_str());
                return 2;
            }
            options.referenceStageExplicit = true;
        }
        else if (argument.rfind(L"--noise-mix=", 0) == 0)
        {
            const std::wstring mix = argument.substr(12);
            if (mix == L"r")
                options.reference.noiseMix = clouds::ReferenceNoiseMix::ROnly;
            else if (mix == L"b")
                options.reference.noiseMix = clouds::ReferenceNoiseMix::BOnly;
            else if (mix == L"rb")
                options.reference.noiseMix = clouds::ReferenceNoiseMix::RPlusB;
            else
            {
                LogError("invalid --noise-mix value: %s "
                         "(expected r, b or rb)",
                         WideToUtf8(mix).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--coordinate-mode=", 0) == 0)
        {
            const std::wstring mode = argument.substr(18);
            if (mode == L"plain")
                options.reference.coordinateMode =
                    clouds::ReferenceCoordinateMode::Plain;
            else if (mode == L"warped")
                options.reference.coordinateMode =
                    clouds::ReferenceCoordinateMode::Warped;
            else
            {
                LogError("invalid --coordinate-mode value: %s "
                         "(expected plain or warped)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--field-mode=", 0) == 0)
        {
            const std::wstring mode = argument.substr(13);
            if (mode == L"direct")
                options.reference.fieldMode =
                    clouds::ReferenceFieldMode::Direct;
            else if (mode == L"multi-r")
                options.reference.fieldMode =
                    clouds::ReferenceFieldMode::MultiFrequencyR;
            else if (mode == L"hash-mod")
                options.reference.fieldMode =
                    clouds::ReferenceFieldMode::LowFrequencyModulation;
            else if (mode == L"macro")
                options.reference.fieldMode =
                    clouds::ReferenceFieldMode::NonTilingMacro;
            else
            {
                LogError("invalid --field-mode value: %s "
                         "(expected direct, multi-r, hash-mod or macro)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--multi-scale=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(14), value) || value < 0.1f ||
                value > 4.0f)
            {
                LogError("invalid --multi-scale value: %s (expected 0.1..4)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.multiFrequencyScale = value;
        }
        else if (argument.rfind(L"--multi-weight=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(15), value) || value < 0.0f ||
                value > 1.0f)
            {
                LogError("invalid --multi-weight value: %s (expected 0..1)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.multiFrequencyWeight = value;
        }
        else if (argument.rfind(L"--modulation-amplitude=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(23), value) || value < 0.0f ||
                value > 0.5f)
            {
                LogError("invalid --modulation-amplitude value: %s "
                         "(expected 0..0.5)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.modulationAmplitude = value;
        }
        else if (argument.rfind(L"--macro-scale=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(14), value) || value < 2000.0f ||
                value > 40000.0f)
            {
                LogError("invalid --macro-scale value: %s "
                         "(expected 2000..40000 m)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.macroScale = value;
        }
        else if (argument.rfind(L"--macro-secondary-scale=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(24), value) || value < 800.0f ||
                value > 20000.0f)
            {
                LogError("invalid --macro-secondary-scale value: %s "
                         "(expected 800..20000 m)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.macroSecondaryScale = value;
        }
        else if (argument.rfind(L"--macro-threshold=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(18), value) || value < 0.05f ||
                value > 0.95f)
            {
                LogError("invalid --macro-threshold value: %s "
                         "(expected 0.05..0.95)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.macroThreshold = value;
        }
        else if (argument.rfind(L"--macro-softness=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(17), value) || value < 0.01f ||
                value > 0.50f)
            {
                LogError("invalid --macro-softness value: %s "
                         "(expected 0.01..0.50)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.macroSoftness = value;
        }
        else if (argument.rfind(L"--macro-detail=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(15), value) || value < 0.0f ||
                value > 1.0f)
            {
                LogError("invalid --macro-detail value: %s "
                         "(expected 0..1)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.macroDetailWeight = value;
        }
        else if (argument.rfind(L"--macro-secondary-weight=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(25), value) || value < 0.0f ||
                value > 0.70f)
            {
                LogError("invalid --macro-secondary-weight value: %s "
                         "(expected 0..0.7)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.macroSecondaryWeight = value;
        }
        else if (argument.rfind(L"--macro-edge-jitter=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(20), value) || value < 0.0f ||
                value > 0.30f)
            {
                LogError("invalid --macro-edge-jitter value: %s "
                         "(expected 0..0.3)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.macroEdgeJitter = value;
        }
        else if (argument.rfind(L"--macro-height=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(15), value) || value < 0.0f ||
                value > 1.0f)
            {
                LogError("invalid --macro-height value: %s "
                         "(expected 0..1)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.macroHeightWeight = value;
        }
        else if (argument.rfind(L"--macro-seed=", 0) == 0)
        {
            uint32_t value = 0;
            if (!ParseUint32(argument.substr(13), value) || value > 1000000000u)
            {
                LogError("invalid --macro-seed value: %s "
                         "(expected 0..1e9)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.macroSeed = value;
        }
        else if (argument.rfind(L"--macro-coverage=", 0) == 0)
        {
            const std::wstring value = argument.substr(17);
            if (value == L"0")
                options.reference.macroCoveragePreserve = false;
            else if (value == L"1")
                options.reference.macroCoveragePreserve = true;
            else
            {
                LogError("invalid --macro-coverage value: %s "
                         "(expected 0 or 1)",
                         WideToUtf8(value).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--macro-seed-hash=", 0) == 0)
        {
            const std::wstring value = argument.substr(18);
            if (value == L"0")
                options.reference.macroSeedHash = false;
            else if (value == L"1")
                options.reference.macroSeedHash = true;
            else
            {
                LogError("invalid --macro-seed-hash value: %s "
                         "(expected 0 or 1)",
                         WideToUtf8(value).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--macro-detail-compensate=", 0) == 0)
        {
            const std::wstring value = argument.substr(26);
            if (value == L"0")
                options.reference.macroDetailCompensate = false;
            else if (value == L"1")
                options.reference.macroDetailCompensate = true;
            else
            {
                LogError("invalid --macro-detail-compensate value: %s "
                         "(expected 0 or 1)",
                         WideToUtf8(value).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--macro-coverage-calibrate=", 0) == 0)
        {
            const std::wstring value = argument.substr(27);
            if (value == L"0")
                options.reference.macroCoverageCalibrate = false;
            else if (value == L"1")
                options.reference.macroCoverageCalibrate = true;
            else
            {
                LogError("invalid --macro-coverage-calibrate value: %s "
                         "(expected 0 or 1)",
                         WideToUtf8(value).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--columns-crosscheck=", 0) == 0)
        {
            const std::wstring value = argument.substr(21);
            if (value == L"0")
                options.columnsCrosscheck = false;
            else if (value == L"1")
                options.columnsCrosscheck = true;
            else
            {
                LogError("invalid --columns-crosscheck value: %s "
                         "(expected 0 or 1)",
                         WideToUtf8(value).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--density-test=", 0) == 0)
        {
            uint32_t value = 0;
            if (!ParseUint32(argument.substr(15), value) || value > 2u)
            {
                LogError("invalid --density-test value: %s "
                         "(expected 0, 1 or 2)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.densityTestPattern = value;
        }
        else if (argument.rfind(L"--macro-weather=", 0) == 0)
        {
            const std::wstring value = argument.substr(16);
            if (value == L"legacy")
            {
                options.reference.macroWeatherQuantile = false;
                options.reference.macroWeatherLocal = false;
            }
            else if (value == L"quantile")
            {
                options.reference.macroWeatherQuantile = true;
                options.reference.macroWeatherLocal = false;
            }
            else if (value == L"local")
            {
                options.reference.macroWeatherQuantile = true;
                options.reference.macroWeatherLocal = true;
            }
            else
            {
                LogError("invalid --macro-weather value: %s "
                         "(expected legacy, quantile or local)",
                         WideToUtf8(value).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--local-occupancy=", 0) == 0)
        {
            const std::wstring value = argument.substr(18);
            if (value == L"0")
                options.reference.macroLocalOccupancy = false;
            else if (value == L"1")
                options.reference.macroLocalOccupancy = true;
            else
            {
                LogError("invalid --local-occupancy value: %s "
                         "(expected 0 or 1)",
                         WideToUtf8(value).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--storm-local=", 0) == 0)
        {
            const std::wstring value = argument.substr(14);
            if (value == L"0")
                options.reference.stormLocalOccupancy = false;
            else if (value == L"1")
                options.reference.stormLocalOccupancy = true;
            else
            {
                LogError("invalid --storm-local value: %s "
                         "(expected 0 or 1)",
                         WideToUtf8(value).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--macro-probe=", 0) == 0)
        {
            const std::wstring value = argument.substr(14);
            if (value == L"0")
                options.macroProbe = false;
            else if (value == L"1")
                options.macroProbe = true;
            else
            {
                LogError("invalid --macro-probe value: %s "
                         "(expected 0 or 1)",
                         WideToUtf8(value).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--storm-gap=", 0) == 0)
        {
            const std::wstring value = argument.substr(12);
            if (value == L"keep")
                options.reference.stormMacroGapReplace = false;
            else if (value == L"macro")
                options.reference.stormMacroGapReplace = true;
            else
            {
                LogError("invalid --storm-gap value: %s "
                         "(expected keep or macro)",
                         WideToUtf8(value).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--reference-density-scale=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(26), value) || value < 0.5f ||
                value > 2.0f)
            {
                LogError("invalid --reference-density-scale value: %s "
                         "(expected 0.5..2)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.densityScale = value;
        }
        else if (argument.rfind(L"--storm-volume=", 0) == 0)
        {
            const std::wstring value = argument.substr(15);
            if (value == L"0")
                options.reference.stormVolume = false;
            else if (value == L"1")
                options.reference.stormVolume = true;
            else
            {
                LogError("invalid --storm-volume value: %s "
                         "(expected 0 or 1)",
                         WideToUtf8(value).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--storm-height-variation=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(25), value) || value < 0.0f ||
                value > 2000.0f)
            {
                LogError("invalid --storm-height-variation value: %s "
                         "(expected 0..2000 m)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.stormHeightVariation = value;
        }
        else if (argument.rfind(L"--storm-bottom-variation=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(25), value) || value < 0.0f ||
                value > 500.0f)
            {
                LogError("invalid --storm-bottom-variation value: %s "
                         "(expected 0..500 m)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.stormBottomVariation = value;
        }
        else if (argument.rfind(L"--storm-column=", 0) == 0)
        {
            const std::wstring value = argument.substr(15);
            if (value == L"0")
                options.reference.stormColumnEnvelope = false;
            else if (value == L"1")
                options.reference.stormColumnEnvelope = true;
            else
            {
                LogError("invalid --storm-column value: %s "
                         "(expected 0 or 1)",
                         WideToUtf8(value).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--od-test-pattern=", 0) == 0)
        {
            const std::wstring value = argument.substr(18);
            if (value == L"0")
                options.reference.odTestPattern = false;
            else if (value == L"1")
                options.reference.odTestPattern = true;
            else
            {
                LogError("invalid --od-test-pattern value: %s "
                         "(expected 0 or 1)",
                         WideToUtf8(value).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--density-volume=", 0) == 0)
        {
            const std::wstring value = argument.substr(17);
            if (value == L"0")
                options.densityVolume = false;
            else if (value == L"1")
                options.densityVolume = true;
            else
            {
                LogError("invalid --density-volume value: %s "
                         "(expected 0 or 1)",
                         WideToUtf8(value).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--volume-origin=", 0) == 0)
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (swscanf_s(argument.c_str() + 16, L"%f,%f,%f", &x, &y, &z) != 3)
            {
                LogError("invalid --volume-origin value: %s "
                         "(expected x,y,z)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.volumeOrigin = DirectX::XMFLOAT3(x, y, z);
            options.volumeOriginExplicit = true;
        }
        else if (argument.rfind(L"--volume-cell=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(14), value) || value < 0.5f ||
                value > 1000.0f)
            {
                LogError("invalid --volume-cell value: %s "
                         "(expected 0.5..1000 m)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.volumeCellSize = value;
        }
        else if (argument.rfind(L"--volume-res=", 0) == 0)
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (swscanf_s(argument.c_str() + 13, L"%f,%f,%f", &x, &y, &z) != 3 ||
                x < 1.0f || y < 1.0f || z < 1.0f ||
                x > 1024.0f || y > 512.0f || z > 1024.0f)
            {
                LogError("invalid --volume-res value: %s "
                         "(expected X,Y,Z within 1..1024/512)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.volumeResX = static_cast<uint32_t>(x);
            options.volumeResY = static_cast<uint32_t>(y);
            options.volumeResZ = static_cast<uint32_t>(z);
        }
        else if (argument.rfind(L"--reference-shape-low=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(22), value) || value < 0.0f ||
                value > 0.98f)
            {
                LogError("invalid --reference-shape-low value: %s "
                         "(expected 0..0.98)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.shapeLow = value;
        }
        else if (argument.rfind(L"--reference-shape-high=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(23), value) || value < 0.01f ||
                value > 1.5f)
            {
                LogError("invalid --reference-shape-high value: %s "
                         "(expected 0.01..1.5)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.shapeHigh = value;
        }
        else if (argument.rfind(L"--noise-field-axis=", 0) == 0)
        {
            const std::wstring axis = argument.substr(19);
            if (axis == L"xy")
                options.reference.noiseFieldAxis = 0;
            else if (axis == L"xz")
                options.reference.noiseFieldAxis = 1;
            else if (axis == L"yz")
                options.reference.noiseFieldAxis = 2;
            else
            {
                LogError("invalid --noise-field-axis value: %s "
                         "(expected xy, xz or yz)",
                         WideToUtf8(axis).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--noise-field-offset=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(21), value) || value < 0.0f ||
                value > 1.0f)
            {
                LogError("invalid --noise-field-offset value: %s "
                         "(expected 0..1)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.reference.noiseFieldOffset = value;
        }
        else if (argument.rfind(L"--formula-bias=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(15), value) || value < -1.0f ||
                value > 1.0f)
            {
                LogError("invalid --formula-bias value: %s "
                         "(expected -1..1; F1/F3 use -0.20..0.20, F5 uses 0..1)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cloud.formulaBias = value;
            options.formulaBiasExplicit = true;
        }
        else if (argument.rfind(L"--ablation=", 0) == 0)
        {
            const std::wstring mode = argument.substr(11);
            if (mode == L"none")
                options.cloudAblation = clouds::CloudAblationMode::None;
            else if (mode == L"no-weather")
                options.cloudAblation = clouds::CloudAblationMode::NoWeather;
            else if (mode == L"no-mid")
                options.cloudAblation = clouds::CloudAblationMode::NoMid;
            else if (mode == L"no-base")
                options.cloudAblation = clouds::CloudAblationMode::NoBase;
            else if (mode == L"no-fine")
                options.cloudAblation = clouds::CloudAblationMode::NoFine;
            else if (mode == L"shadow-2")
                options.cloudAblation = clouds::CloudAblationMode::Shadow2;
            else if (mode == L"shadow-4")
                options.cloudAblation = clouds::CloudAblationMode::Shadow4;
            else if (mode == L"no-type")
                options.cloudAblation = clouds::CloudAblationMode::NoType;
            else if (mode == L"no-warp")
                options.cloudAblation = clouds::CloudAblationMode::NoWarp;
            else if (mode == L"macro-only")
                options.cloudAblation = clouds::CloudAblationMode::MacroOnly;
            else if (mode == L"no-shadow")
                options.cloudAblation = clouds::CloudAblationMode::NoShadow;
            else if (mode == L"base-3d")
                options.cloudAblation = clouds::CloudAblationMode::Base3D;
            else if (mode == L"no-base-warp")
                options.cloudAblation = clouds::CloudAblationMode::NoBaseWarp;
            else if (mode == L"no-height-variation")
                options.cloudAblation =
                    clouds::CloudAblationMode::NoHeightVariation;
            else if (mode == L"no-anisotropy")
                options.cloudAblation = clouds::CloudAblationMode::NoAnisotropy;
            else if (mode == L"flat-bottom")
                options.cloudAblation = clouds::CloudAblationMode::FlatBottom;
            else if (mode == L"no-type-height-profile")
                options.cloudAblation =
                    clouds::CloudAblationMode::NoTypeHeightProfile;
            else if (mode == L"no-type-thresholds")
                options.cloudAblation =
                    clouds::CloudAblationMode::NoTypeThresholds;
            else if (mode == L"no-type-anisotropy")
                options.cloudAblation =
                    clouds::CloudAblationMode::NoTypeAnisotropy;
            else if (mode == L"no-type-local-bounds")
                options.cloudAblation =
                    clouds::CloudAblationMode::NoTypeLocalBounds;
            else if (mode == L"no-type-shape-blending")
                options.cloudAblation =
                    clouds::CloudAblationMode::NoTypeShapeBlending;
            else
            {
                LogError("invalid --ablation value: %s",
                         WideToUtf8(mode).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--cloud-scale=", 0) == 0)
        {
            const std::wstring scale = argument.substr(14);
            if (scale == L"half")
                options.cloudDivisor = 2;
            else if (scale == L"full")
                options.cloudDivisor = 1;
            else
            {
                LogError("invalid --cloud-scale value: %s (expected half or full)",
                         WideToUtf8(scale).c_str());
                return 2;
            }
            options.cloudScaleExplicit = true;
        }
        else if (argument.rfind(L"--sun-elevation=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(16), value) || value < -10.0f ||
                value > 90.0f)
            {
                LogError("invalid --sun-elevation value: %s (expected -10..90)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cloud.sunElevationDegrees = value;
            options.sunElevationExplicit = true;
        }
        else if (argument.rfind(L"--sun-azimuth=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(14), value) || value < -360.0f ||
                value > 360.0f)
            {
                LogError("invalid --sun-azimuth value: %s (expected -360..360)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cloud.sunAzimuthDegrees = value;
            options.sunAzimuthExplicit = true;
        }
        else if (argument.rfind(L"--camera=", 0) == 0)
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (swscanf_s(argument.c_str() + 9, L"%f,%f,%f", &x, &y, &z) != 3)
            {
                LogError("invalid --camera value: %s (expected x,y,z)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cameraPosition = DirectX::XMFLOAT3(x, y, z);
            options.cameraExplicit = true;
        }
        else if (argument.rfind(L"--camera-yaw=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(13), value))
            {
                LogError("invalid --camera-yaw value: %s",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cameraYawDegrees = value;
            options.cameraYawExplicit = true;
        }
        else if (argument.rfind(L"--camera-pitch=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(15), value) || value < -85.0f ||
                value > 85.0f)
            {
                LogError("invalid --camera-pitch value: %s (expected -85..85)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cameraPitchDegrees = value;
            options.cameraPitchExplicit = true;
        }
        else if (argument.rfind(L"--fov=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(6), value) || value < 20.0f ||
                value > 120.0f)
            {
                LogError("invalid --fov value: %s (expected 20..120)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.fovYDegrees = value;
            options.fovExplicit = true;
        }
        else if (argument.rfind(L"--shadow-steps=", 0) == 0)
        {
            uint32_t value = 0;
            if (!ParseUint32(argument.substr(15), value) || value < 1 ||
                value > 16)
            {
                LogError("invalid --shadow-steps value: %s (expected 1..16)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cloud.shadowStepCount = value;
            options.shadowStepsExplicit = true;
        }
        else if (argument.rfind(L"--sun-shadow-distance=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(21), value) || value < 100.0f ||
                value > 30000.0f)
            {
                LogError("invalid --sun-shadow-distance value: %s "
                         "(expected 100..30000 m)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cloud.sunShadowDistance = value;
            options.sunShadowDistanceExplicit = true;
        }
        else if (argument.rfind(L"--jitter=", 0) == 0)
        {
            const std::wstring mode = argument.substr(9);
            if (mode == L"off")
                options.cloud.jitterMode = clouds::CloudJitterMode::Off;
            else if (mode == L"spatial")
                options.cloud.jitterMode = clouds::CloudJitterMode::Spatial;
            else
            {
                LogError("invalid --jitter value: %s (expected off or spatial)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
            options.jitterExplicit = true;
        }
        else if (argument.rfind(L"--lighting=", 0) == 0)
        {
            const std::wstring mode = argument.substr(11);
            if (mode == L"full")
                options.cloud.lightingMode = clouds::CloudLightingMode::Full;
            else if (mode == L"sun-only")
                options.cloud.lightingMode = clouds::CloudLightingMode::SunOnly;
            else if (mode == L"ambient-only")
                options.cloud.lightingMode =
                    clouds::CloudLightingMode::AmbientOnly;
            else
            {
                LogError("invalid --lighting value: %s "
                         "(expected full, sun-only or ambient-only)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
            options.lightingExplicit = true;
        }
        else if (argument.rfind(L"--atmosphere=", 0) == 0)
        {
            const std::wstring mode = argument.substr(13);
            if (mode == L"on")
                options.cloud.atmosphereEnabled = true;
            else if (mode == L"off")
                options.cloud.atmosphereEnabled = false;
            else
            {
                LogError("invalid --atmosphere value: %s (expected on or off)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
            options.atmosphereExplicit = true;
        }
        else if (argument.rfind(L"--slice-height=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(15), value) || value < 0.0f ||
                value > 1.0f)
            {
                LogError("invalid --slice-height value: %s (expected 0..1)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cloud.densitySliceHeight = value;
            options.sliceHeightExplicit = true;
        }
        else if (argument.rfind(L"--slice-extent=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(15), value) || value < 2.0f ||
                value > 60.0f)
            {
                LogError("invalid --slice-extent value: %s (expected 2..60 km)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cloud.densitySliceExtentKm = value;
            options.sliceExtentExplicit = true;
        }
        else if (argument.rfind(L"--probe-column=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(15), value) || value < 0.0f ||
                value > 1.0f)
            {
                LogError("invalid --probe-column value: %s (expected 0..1)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cloud.probeColumn = value;
            options.probeColumnExplicit = true;
        }
        else if (argument.rfind(L"--top-fade=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(11), value) || value < 0.02f ||
                value > 0.30f)
            {
                LogError("invalid --top-fade value: %s (expected 0.02..0.30)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.cloud.topFade = value;
            options.topFadeExplicit = true;
        }
        else if (argument.rfind(L"--self-shadow=", 0) == 0)
        {
            const std::wstring mode = argument.substr(14);
            if (mode == L"on")
                options.cloud.selfShadowEnabled = true;
            else if (mode == L"off")
                options.cloud.selfShadowEnabled = false;
            else
            {
                LogError("invalid --self-shadow value: %s (expected on or off)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
            options.selfShadowExplicit = true;
        }
        else if (argument.rfind(L"--breakup=", 0) == 0)
        {
            const std::wstring mode = argument.substr(10);
            if (mode == L"on")
                options.cloud.breakupEnabled = true;
            else if (mode == L"off")
                options.cloud.breakupEnabled = false;
            else
            {
                LogError("invalid --breakup value: %s (expected on or off)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
            options.breakupExplicit = true;
        }
        else if (argument.rfind(L"--front-height=", 0) == 0)
        {
            const std::wstring mode = argument.substr(15);
            if (mode == L"on")
                options.cloud.heightFront = true;
            else if (mode == L"off")
                options.cloud.heightFront = false;
            else
            {
                LogError("invalid --front-height value: %s "
                         "(expected on or off)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
            options.frontHeightExplicit = true;
        }
        else if (argument.rfind(L"--storm-front=", 0) == 0)
        {
            const std::wstring mode = argument.substr(14);
            if (mode == L"on")
                options.cloud.stormFront = true;
            else if (mode == L"off")
                options.cloud.stormFront = false;
            else
            {
                LogError("invalid --storm-front value: %s "
                         "(expected on or off)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
            options.stormFrontExplicit = true;
        }
        else if (argument.rfind(L"--storm-front-debug=", 0) == 0)
        {
            const std::wstring mode = argument.substr(20);
            if (mode == L"off")
                options.cloud.stormFrontDebug = 0;
            else if (mode == L"field")
                options.cloud.stormFrontDebug = 1;
            else if (mode == L"mass")
                options.cloud.stormFrontDebug = 2;
            else if (mode == L"vertical")
                options.cloud.stormFrontDebug = 3;
            else
            {
                LogError("invalid --storm-front-debug value: %s "
                         "(expected off, field, mass, vertical)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
            options.stormFrontDebugExplicit = true;
        }
        else if (argument.rfind(L"--capture-raw-cloud=", 0) == 0)
        {
            const std::wstring value = argument.substr(20);
            if (value == L"1")
                options.captureRawCloud = true;
            else if (value == L"0")
                options.captureRawCloud = false;
            else
            {
                LogError("invalid --capture-raw-cloud value: %s "
                         "(expected 0 or 1)",
                         WideToUtf8(value).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--profile=", 0) == 0)
        {
            const std::wstring profile = argument.substr(10);
            clouds::CloudQuality quality = clouds::CloudQuality::Quality;
            if (!clouds::ParseCloudQuality(WideToUtf8(profile), quality))
            {
                LogError("invalid --profile value: %s (expected performance, "
                         "balanced, quality or reference)",
                         WideToUtf8(profile).c_str());
                return 2;
            }
            if (options.cloudQualityExplicit &&
                options.cloudGraphics.quality != quality)
            {
                LogError("conflicting --cloud-quality/--profile values");
                return 2;
            }
            options.cloudGraphics.quality = quality;
            options.cloudQualityExplicit = true;
            options.qualityProfile = clouds::CloudQualityName(quality);
        }
        else if (argument.rfind(L"--time=", 0) == 0)
        {
            float value = 0.0f;
            if (!ParseFloat(argument.substr(7), value) || value < 0.0f ||
                value > 3600.0f)
            {
                LogError("invalid --time value: %s (expected 0..3600 seconds)",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.initialCloudTime = value;
        }
        else if (argument.rfind(L"--slice-axis=", 0) == 0)
        {
            const std::wstring axis = argument.substr(13);
            if (axis == L"xy")
                options.sliceAxis = 0;
            else if (axis == L"xz")
                options.sliceAxis = 1;
            else if (axis == L"yz")
                options.sliceAxis = 2;
            else
            {
                LogError("invalid --slice-axis value: %s (expected xy/xz/yz)",
                         WideToUtf8(axis).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--debug-mode=", 0) == 0)
        {
            const std::wstring mode = argument.substr(13);
            options.cloudDebugExplicit = true;
            if (mode == L"final")
                options.cloudDebug = clouds::CloudDebugMode::Final;
            else if (mode == L"density")
                options.cloudDebug = clouds::CloudDebugMode::Density;
            else if (mode == L"transmittance")
                options.cloudDebug = clouds::CloudDebugMode::Transmittance;
            else if (mode == L"steps")
                options.cloudDebug = clouds::CloudDebugMode::StepCount;
            else if (mode == L"occupancy")
                options.cloudDebug = clouds::CloudDebugMode::Occupancy;
            else if (mode == L"weather")
                options.cloudDebug = clouds::CloudDebugMode::Weather;
            else if (mode == L"cloud-type" || mode == L"type")
                options.cloudDebug = clouds::CloudDebugMode::CloudType;
            else if (mode == L"density-slice" || mode == L"slice")
                options.cloudDebug = clouds::CloudDebugMode::DensitySlice;
            else if (mode == L"density-horizontal")
                options.cloudDebug = clouds::CloudDebugMode::DensityHorizontal;
            else if (mode == L"ray-profile")
                options.cloudDebug = clouds::CloudDebugMode::RayProfile;
            else if (mode == L"density-vertical-z")
                options.cloudDebug = clouds::CloudDebugMode::DensityVerticalZ;
            else if (mode == L"macro-shape")
                options.cloudDebug = clouds::CloudDebugMode::MacroShape;
            else if (mode == L"cross-type-difference")
                options.cloudDebug = clouds::CloudDebugMode::CrossTypeDifference;
            else if (mode == L"noise-field")
                options.cloudDebug = clouds::CloudDebugMode::NoiseField;
            else if (mode == L"view-optical-depth" ||
                     mode == L"optical-depth-view")
                options.cloudDebug =
                    clouds::CloudDebugMode::ViewOpticalDepth;
            else if (mode == L"storm-front-slice")
                options.cloudDebug =
                    clouds::CloudDebugMode::StormFrontSlice;
            else if (mode == L"storm-od")
                options.cloudDebug =
                    clouds::CloudDebugMode::StormOpticalDepth;
            else if (mode == L"storm-t")
                options.cloudDebug =
                    clouds::CloudDebugMode::StormTransmittance;
            else if (mode == L"storm-mask")
                options.cloudDebug =
                    clouds::CloudDebugMode::StormFrontMask;
            else if (mode == L"sun-depth" || mode == L"optical-depth")
                options.cloudDebug = clouds::CloudDebugMode::SunOpticalDepth;
            else if (mode == L"sun-transmittance")
                options.cloudDebug = clouds::CloudDebugMode::SunTransmittance;
            else if (mode == L"shadow-diff")
                options.cloudDebug = clouds::CloudDebugMode::ShadowDensityDifference;
            else if (mode == L"atmosphere-transmittance")
                options.cloudDebug = clouds::CloudDebugMode::AtmosphereTransmittance;
            else if (mode == L"shadow-path")
                options.cloudDebug = clouds::CloudDebugMode::ShadowPathLength;
            else
            {
                LogError("invalid --debug-mode value: %s", WideToUtf8(mode).c_str());
                return 2;
            }
        }
        else if (argument == L"--test-no-validation")
        {
            // Test-only hook: simulate missing debug/GPU validation
            // infrastructure while keeping the real GPU alive.
            options.renderer.testNoValidation = true;
        }
        else if (argument == L"--test-no-info-queue")
        {
            // Test-only hook: keep the debug layer but hide ID3D12InfoQueue.
            options.renderer.testNoInfoQueue = true;
        }
        else if (argument == L"--test-inject-debug-error")
        {
            // Test-only hook: inject one D3D12 ERROR after the cloud selftest
            // so the final validation verdict must fail.
            options.injectDebugError = true;
        }
        else if (argument == L"--no-vsync")
        {
            options.renderer.vsync = false;
        }
        else if (argument == L"--selftest")
        {
            options.selfTest = true;
        }
        else if (argument == L"--resize-test")
        {
            options.resizeTest = true;
        }
        else if (argument.rfind(L"--warmup=", 0) == 0)
        {
            uint32_t value = 0;
            if (!ParseUint32(argument.substr(9), value))
            {
                LogError("invalid --warmup value: %s",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.warmupFrames = value;
        }
        else if (argument == L"--help" || argument == L"-h" || argument == L"/?")
        {
            options.help = true;
        }
        else if (argument == L"--capture")
        {
            options.capture = true;
            if (i + 1 < argc && argv[i + 1][0] != L'-')
                options.captureDir = argv[++i];
        }
        else if (argument.rfind(L"--capture=", 0) == 0)
        {
            options.capture = true;
            options.captureDir = argument.substr(10);
        }
        else if (argument.rfind(L"--sequence=", 0) == 0)
        {
            options.sequenceDir = argument.substr(11);
        }
        else if (argument == L"--sequence")
        {
            if (i + 1 < argc && argv[i + 1][0] != L'-')
                options.sequenceDir = argv[++i];
            else
            {
                LogError("--sequence requires a directory");
                return 2;
            }
        }
        else if (argument.rfind(L"--motion=", 0) == 0)
        {
            const std::wstring motion = argument.substr(9);
            if (motion == L"static" || motion == L"pan-slow" ||
                motion == L"pan-fast" || motion == L"up" || motion == L"fly" ||
                motion == L"fly-level")
                options.motionName = WideToUtf8(motion);
            else
            {
                LogError("invalid --motion value: %s (expected static, "
                         "pan-slow, pan-fast, up, fly or fly-level)",
                         WideToUtf8(motion).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--wind=", 0) == 0)
        {
            const std::wstring mode = argument.substr(7);
            if (mode == L"on")
                options.windDisabled = false;
            else if (mode == L"off")
                options.windDisabled = true;
            else
            {
                LogError("invalid --wind value: %s (expected on or off)",
                         WideToUtf8(mode).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--noise-slice-capture=", 0) == 0)
        {
            options.noiseSliceCaptureDir = argument.substr(22);
        }
        else if (argument == L"--noise-slice-capture")
        {
            if (i + 1 < argc && argv[i + 1][0] != L'-')
                options.noiseSliceCaptureDir = argv[++i];
            else
            {
                LogError("--noise-slice-capture requires a directory");
                return 2;
            }
        }
        else if (argument.rfind(L"--noise-slice-index=", 0) == 0)
        {
            uint32_t value = 0;
            if (!ParseUint32(argument.substr(20), value))
            {
                LogError("invalid --noise-slice-index value: %s",
                         WideToUtf8(argument).c_str());
                return 2;
            }
            options.noiseSliceIndex = value;
        }
        else if (argument.rfind(L"--scene=", 0) == 0)
        {
            options.sceneName = WideToUtf8(argument.substr(8));
        }
        else if (argument.rfind(L"--size=", 0) == 0)
        {
            uint32_t value = 0;
            if (!ParseUint32(argument.substr(7), value))
            {
                LogError("invalid --size value: %s", WideToUtf8(argument).c_str());
                return 2;
            }
            options.noise.resolution = value;
        }
        else if (argument.rfind(L"--seed=", 0) == 0)
        {
            uint32_t value = 0;
            if (!ParseUint32(argument.substr(7), value))
            {
                LogError("invalid --seed value: %s", WideToUtf8(argument).c_str());
                return 2;
            }
            options.noise.seed = value;
        }
        else if (argument.rfind(L"--adapter=", 0) == 0)
        {
            int value = -1;
            if (!ParseInt(argument.substr(10), value) || value < 0)
            {
                LogError("invalid --adapter value: %s", WideToUtf8(argument).c_str());
                return 2;
            }
            options.renderer.adapterIndex = value;
        }
        else if (argument.rfind(L"--format=", 0) == 0)
        {
            const std::wstring format = argument.substr(9);
            if (format == L"16f")
                options.preferredFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
            else if (format == L"unorm")
                options.preferredFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            else
            {
                LogError("invalid --format value: %s (expected 16f or unorm)",
                         WideToUtf8(format).c_str());
                return 2;
            }
        }
        else if (argument.rfind(L"--frames=", 0) == 0)
        {
            uint32_t value = 0;
            if (!ParseUint32(argument.substr(9), value) || value == 0)
            {
                LogError("invalid --frames value: %s", WideToUtf8(argument).c_str());
                return 2;
            }
            options.frames = value;
        }
        else
        {
            LogError("unknown argument: %s", WideToUtf8(argument).c_str());
            PrintCliHelp();
            return 2;
        }
    }

    options.cloud.ablationMode = options.cloudAblation;

    // Public graphics settings precedence:
    //   explicit command line > validated user graphics config > profile default.
    // Weather/debug keys in a graphics config are ignored by the schema parser
    // and can never reach the weather controller.
    bool effectiveClouds = options.cloudGraphics.cloudsEnabled;
    clouds::CloudQuality effectiveQuality = options.cloudGraphics.quality;
    clouds::CloudRenderMode effectiveRenderMode =
        options.cloudGraphics.renderMode;
    if (!options.cloudSettingsPath.empty())
    {
        const clouds::CloudSettingsLoadResult loaded =
            clouds::LoadCloudSettings(options.cloudSettingsPath);
        Log("cloud settings: path=%s found=%d loaded=%d error=%d",
            options.cloudSettingsPath.string().c_str(),
            loaded.fileFound ? 1 : 0, loaded.loaded ? 1 : 0,
            loaded.error ? 1 : 0);
        for (const std::string& warning : loaded.warnings)
            LogWarn("cloud settings: %s", warning.c_str());
        if (!loaded.unknownKeys.empty())
        {
            std::string keys;
            for (size_t index = 0; index < loaded.unknownKeys.size(); ++index)
            {
                if (index != 0)
                    keys += ", ";
                keys += loaded.unknownKeys[index];
            }
            Log("cloud settings: ignored %zu unknown key(s): %s "
                "(weather/debug/noise keys are never applied)",
                loaded.unknownKeys.size(), keys.c_str());
        }
        if (loaded.loaded)
        {
            if (!options.cloudsExplicit && loaded.hasCloudsEnabled)
                effectiveClouds = loaded.settings.cloudsEnabled;
            if (!options.cloudQualityExplicit && loaded.hasQuality)
                effectiveQuality = loaded.settings.quality;
            if (!options.cloudRenderModeExplicit && loaded.hasRenderMode)
                effectiveRenderMode = loaded.settings.renderMode;
        }
        else
        {
            LogWarn("cloud settings: no valid user values; safe defaults used");
        }
    }
    options.cloudGraphics.cloudsEnabled = effectiveClouds;
    options.cloudGraphics.quality = effectiveQuality;
    options.cloudGraphics.renderMode = effectiveRenderMode;
    options.qualityProfile = clouds::CloudQualityName(effectiveQuality);

#if defined(OGSR_WEATHER_SHIPPING_CONTROLS)
    // The contract proxy has no V0 slice-viewer surface. Cloud mode is the
    // only rendering mode; the game adapter will choose its renderer instead.
    options.mode = clouds::RenderMode::Clouds;
    Log("shipping-controls surface: public={clouds,cloud_quality,"
        "cloud_render_mode,cloud_settings}; lifecycle=contract-test-only; "
        "noise/seed/size/format overrides=disabled");
    // Shipping-controls contract: weather is read from the environment
    // snapshot, never from CLI/config/hotkeys. This standalone adapter is a
    // contract proxy; it does not claim a real game CEnvironment integration.
    const clouds::EnvironmentWeatherSnapshot environment =
        clouds::ReadStandaloneEnvironmentContract();
    clouds::ApplyStaticWeatherMode(environment.mode, options.cloud);
    options.weatherMode = environment.mode;
    options.weatherModeExplicit = true;
    options.weatherSeed = environment.seed;
    options.initialCloudTime = environment.timeSeconds;
    options.weatherSource = "CEnvironment-contract-standalone";
    Log("weather source: %s snapshot; manual weather controls are not "
        "registered", options.weatherSource.c_str());
    Log("weather environment snapshot: mode=%s seed=%u time=%.3f",
        clouds::StaticWeatherModeName(environment.mode), environment.seed,
        environment.timeSeconds);
#else
    options.weatherSource = "developer-standalone";
    Log("weather source: %s (developer controls registered)",
        options.weatherSource.c_str());
#endif

    // Quality profiles set reproducible defaults; explicit CLI values win.
    if (options.shapePrototype != clouds::ShapePrototype::None &&
        options.cloudFormula != clouds::CloudFormula::None)
    {
        LogError("--shape-prototype and --formula are mutually exclusive");
        return 2;
    }

    if (options.densityModel == clouds::CloudDensityModel::Reference &&
        options.cloudFormula != clouds::CloudFormula::None)
    {
        LogError("--density-model=reference is isolated from --formula; "
                 "use --formula=none");
        return 2;
    }
    if (options.densityModel == clouds::CloudDensityModel::Reference &&
        options.shapePrototype != clouds::ShapePrototype::None)
    {
        LogError("--density-model=reference is isolated from "
                 "--shape-prototype; use --shape-prototype=none");
        return 2;
    }
    if (options.densityModel == clouds::CloudDensityModel::Legacy &&
        options.referenceStageExplicit)
    {
        LogError("--reference-stage is only valid with "
                 "--density-model=reference");
        return 2;
    }
    if (options.densityModel == clouds::CloudDensityModel::Legacy &&
        (options.reference.densityScale != 1.0f ||
         options.reference.stormVolume ||
         options.reference.stormColumnEnvelope ||
         options.reference.stormHeightVariation > 0.0f ||
         options.reference.stormBottomVariation > 0.0f ||
         options.densityVolume))
    {
        LogError("A02b/A02c density/storm controls require "
                 "--density-model=reference");
        return 2;
    }
    if (options.reference.stormColumnEnvelope &&
        !options.reference.stormVolume)
    {
        LogError("--storm-column=1 requires --storm-volume=1");
        return 2;
    }
    if (options.reference.macroLocalOccupancy)
    {
        if (options.densityModel != clouds::CloudDensityModel::Reference)
        {
            LogError("--local-occupancy=1 requires "
                     "--density-model=reference");
            return 2;
        }
        if (options.reference.fieldMode !=
            clouds::ReferenceFieldMode::NonTilingMacro)
        {
            LogError("--local-occupancy=1 requires --field-mode=macro");
            return 2;
        }
        if (options.referenceStage != clouds::ReferenceStage::Coverage)
        {
            LogError("--local-occupancy=1 requires "
                     "--reference-stage=coverage");
            return 2;
        }
        if (options.reference.macroWeatherQuantile ||
            options.reference.macroWeatherLocal ||
            options.reference.macroCoverageCalibrate)
        {
            LogError("--local-occupancy=1 is isolated from W1/W2/C3-2");
            return 2;
        }
    }
    if (options.reference.stormLocalOccupancy)
    {
        if (options.densityModel != clouds::CloudDensityModel::Reference ||
            !options.reference.stormVolume)
        {
            LogError("--storm-local=1 requires --density-model=reference "
                     "and --storm-volume=1");
            return 2;
        }
        if (options.reference.fieldMode !=
            clouds::ReferenceFieldMode::Direct)
        {
            LogError("--storm-local=1 requires the S2new direct path");
            return 2;
        }
    }
    if (options.reference.fieldMode !=
        clouds::ReferenceFieldMode::Direct)
    {
        if (options.densityModel != clouds::CloudDensityModel::Reference)
        {
            LogError("--field-mode=multi-r|hash-mod requires "
                     "--density-model=reference");
            return 2;
        }
        if (options.reference.noiseMix != clouds::ReferenceNoiseMix::ROnly)
        {
            LogError("--field-mode=multi-r|hash-mod requires "
                     "--noise-mix=r");
            return 2;
        }
        if (options.reference.coordinateMode !=
            clouds::ReferenceCoordinateMode::Plain)
        {
            LogError("--field-mode=multi-r|hash-mod is isolated from "
                     "--coordinate-mode=warped; use plain");
            return 2;
        }
    }
    if (options.cloudDebug == clouds::CloudDebugMode::NoiseField &&
        options.densityModel != clouds::CloudDensityModel::Reference)
    {
        LogError("--debug-mode=noise-field requires "
                 "--density-model=reference");
        return 2;
    }
    if (options.reference.shapeLow + 0.01f >
        options.reference.shapeHigh)
    {
        LogError("--reference-shape-low must be lower than "
                 "--reference-shape-high by at least 0.01");
        return 2;
    }

    if (options.renderer.forceWarp && options.renderer.gpuValidation)
    {
        LogError("--gpu-validation is a hardware D3D12 validation mode and is "
                 "not supported on WARP; run the WARP functional selftest "
                 "without --gpu-validation");
        return 2;
    }

    if (!options.qualityProfile.empty())
    {
        uint32_t profileSteps = 128;
        uint32_t profileShadow = 6;
        uint32_t profileDivisor = 2;
        if (options.qualityProfile == "performance")
        {
            profileSteps = 64;
            profileShadow = 2;
        }
        else if (options.qualityProfile == "balanced")
        {
            profileSteps = 96;
            profileShadow = 4;
        }
        else if (options.qualityProfile == "reference")
        {
            profileShadow = 6;
            profileDivisor = 1;
        }
        if (!options.cloudStepsExplicit)
        {
            options.cloud.stepCount = profileSteps;
            options.cloudStepsExplicit = true;
        }
        if (!options.shadowStepsExplicit)
        {
            options.cloud.shadowStepCount = profileShadow;
            options.shadowStepsExplicit = true;
        }
        if (!options.cloudScaleExplicit)
            options.cloudDivisor = profileDivisor;
    }
    if (!options.sequenceDir.empty() && options.frames == 0)
        options.frames = 60;

    if (options.help)
    {
        PrintCliHelp();
        return 0;
    }

    if (options.saveCloudSettings)
    {
        std::string saveError;
        if (!clouds::SaveCloudSettings(
                options.saveCloudSettingsPath, options.cloudGraphics,
                saveError))
        {
            LogError("cloud settings save failed: %s", saveError.c_str());
            return 2;
        }
        Log("cloud settings saved: path=%s clouds_enabled=%d "
            "render_mode=%s quality=%s",
            options.saveCloudSettingsPath.string().c_str(),
            options.cloudGraphics.cloudsEnabled ? 1 : 0,
            clouds::CloudRenderModeName(options.cloudGraphics.renderMode),
            clouds::CloudQualityName(options.cloudGraphics.quality));
    }

    if (!options.runtimeSettingsTestDir.empty() ||
        !options.runtimeSettingsFaultTestDir.empty())
    {
        if (options.mode != clouds::RenderMode::Clouds)
        {
            LogError("runtime settings tests require --mode=clouds");
            return 2;
        }
        if (options.capture || !options.sequenceDir.empty() ||
            options.selfTest || options.p0Matrix || options.p1Matrix)
        {
            LogError("runtime settings tests cannot be combined with "
                     "capture/sequence/selftest/matrices");
            return 2;
        }
    }

    if (options.p0Matrix || options.p1Matrix)
    {
        const bool p1 = options.p1Matrix;
        if (options.frameTimingLogPath.empty())
        {
            LogError(p1 ? "--p1-matrix requires --frame-timing-log"
                        : "--p0-matrix requires --frame-timing-log");
            return 2;
        }
        if (options.capture || !options.sequenceDir.empty() || options.selfTest)
        {
            LogError(p1 ? "--p1-matrix cannot be combined with capture/sequence/selftest"
                        : "--p0-matrix cannot be combined with capture/sequence/selftest");
            return 2;
        }
        if (!options.weatherTimelineExplicit)
        {
            LogError(p1 ? "--p1-matrix requires --weather-timeline"
                        : "--p0-matrix requires --weather-timeline");
            return 2;
        }
    }
    if (options.shadowOdEarlyOutTransmittance > 0.0f && options.shadowInnerOnly)
    {
        LogError("--shadow-od-early-out and --shadow-inner-only are mutually exclusive");
        return 2;
    }
    if ((options.shadowOdEarlyOutTransmittance > 0.0f || options.shadowInnerOnly) &&
        !options.weatherTimelineExplicit)
    {
        LogError("shadow prototype flags require --weather-timeline");
        return 2;
    }

    if (options.weatherModeExplicit)
    {
        const char* basePreset =
            clouds::StaticWeatherModeBasePreset(options.weatherMode);
        if (presetExplicit && explicitPreset != basePreset)
        {
            LogError("--weather-mode=%s conflicts with --preset=%s "
                     "(expected --preset=%s)",
                     clouds::StaticWeatherModeName(options.weatherMode),
                     explicitPreset.c_str(), basePreset);
            return 2;
        }
        const bool wantsStormFront =
            clouds::StaticWeatherModeUsesStormFront(options.weatherMode);
        if (options.stormFrontExplicit &&
            options.cloud.stormFront != wantsStormFront)
        {
            LogError("--weather-mode=%s conflicts with --storm-front=%s "
                     "(expected --storm-front=%s)",
                     clouds::StaticWeatherModeName(options.weatherMode),
                     options.cloud.stormFront ? "on" : "off",
                     wantsStormFront ? "on" : "off");
            return 2;
        }
        options.cloud.stormFront = wantsStormFront;
        options.cloudPreset = basePreset;
        presetName = basePreset;
        if (options.capture && options.sceneName.empty())
        {
            LogError("--weather-mode with --capture requires "
                     "--scene=<matching legacy scene>");
            return 2;
        }
    }

    if (transitionPhaseExplicit && !options.weatherTransitionExplicit)
    {
        LogError("--transition-phase requires --weather-transition");
        return 2;
    }
    if (options.weatherTransitionExplicit)
    {
        if (options.weatherModeExplicit)
        {
            LogError("--weather-transition conflicts with --weather-mode");
            return 2;
        }
        if (options.stormFrontExplicit)
        {
            LogError("--weather-transition owns the storm-front state; "
                     "do not pass --storm-front");
            return 2;
        }
        if (options.cloudFormula != clouds::CloudFormula::None ||
            options.densityModel != clouds::CloudDensityModel::Legacy ||
            options.shapePrototype != clouds::ShapePrototype::None ||
            options.cloudAblation != clouds::CloudAblationMode::None)
        {
            LogError("--weather-transition Gate B1 supports only "
                     "legacy density, formula=none, shape=none, ablation=none");
            return 2;
        }
        if (options.capture && options.sceneName.empty())
        {
            LogError("--weather-transition with --capture requires "
                     "--scene=<matching source scene>");
            return 2;
        }
        const char* sourceBase =
            clouds::StaticWeatherModeBasePreset(options.transitionSource);
        if (presetExplicit && explicitPreset != sourceBase)
        {
            LogError("--weather-transition source %s conflicts with "
                     "--preset=%s (expected --preset=%s)",
                     clouds::StaticWeatherModeName(options.transitionSource),
                     explicitPreset.c_str(), sourceBase);
            return 2;
        }
        options.cloudPreset = sourceBase;
        presetName = sourceBase;
        options.cloud.stormFront =
            clouds::StaticWeatherModeUsesStormFront(options.transitionSource);
    }

    if (options.weatherTimelineExplicit)
    {
        if (options.weatherModeExplicit)
        {
            LogError("--weather-timeline conflicts with --weather-mode");
            return 2;
        }
        if (options.weatherTransitionExplicit || transitionPhaseExplicit)
        {
            LogError("--weather-timeline conflicts with --weather-transition");
            return 2;
        }
        if (options.stormFrontExplicit)
        {
            LogError("--weather-timeline owns storm-front modes; "
                     "do not pass --storm-front");
            return 2;
        }
        if (presetExplicit)
        {
            LogError("--weather-timeline selects its own presets; "
                     "do not pass --preset");
            return 2;
        }
        if (options.cloudFormula != clouds::CloudFormula::None ||
            options.densityModel != clouds::CloudDensityModel::Legacy ||
            options.shapePrototype != clouds::ShapePrototype::None ||
            options.cloudAblation != clouds::CloudAblationMode::None)
        {
            LogError("--weather-timeline supports only legacy density, "
                     "formula=none, shape=none, ablation=none");
            return 2;
        }
        if (options.capture && options.sceneName.empty())
        {
            LogError("--weather-timeline with --capture requires --scene");
            return 2;
        }
        if (options.capture && options.weatherTimelinePlay)
        {
            LogError("--timeline-play requires --sequence or --frames, "
                     "not --capture");
            return 2;
        }
        if (options.weatherTimeline.modes.size() < 2)
        {
            LogError("--weather-timeline requires at least two modes");
            return 2;
        }
        if (options.weatherTimeline.segmentDuration < 0.05f ||
            options.weatherTimeline.segmentDuration > 60.0f)
        {
            LogError("timeline segment duration must be 0.05..60 s");
            return 2;
        }
        if (options.weatherTimeline.hasInterrupt)
        {
            const int segmentCount =
                static_cast<int>(options.weatherTimeline.modes.size()) - 1;
            if (options.weatherTimeline.interruptSegment < 0 ||
                options.weatherTimeline.interruptSegment >= segmentCount)
            {
                LogError("timeline interrupt segment is out of range");
                return 2;
            }
            if (options.weatherTimeline.interruptPhase < 0.0f ||
                options.weatherTimeline.interruptPhase > 1.0f)
            {
                LogError("timeline interrupt phase must be 0..1");
                return 2;
            }
            if (options.weatherTimeline.interruptTarget ==
                clouds::StaticWeatherMode::StormPreview)
            {
                LogError("timeline interrupt target cannot be storm_preview");
                return 2;
            }
            if (!options.weatherTimelineInterruptDurationExplicit)
                options.weatherTimeline.interruptDuration =
                    options.weatherTimeline.segmentDuration;
            if (options.weatherTimeline.interruptDuration < 0.05f ||
                options.weatherTimeline.interruptDuration > 60.0f)
            {
                LogError("timeline interrupt duration must be 0.05..60 s");
                return 2;
            }
        }
        else if (options.weatherTimelineInterruptDurationExplicit)
        {
            LogError("--timeline-interrupt-duration requires "
                     "--timeline-interrupt");
            return 2;
        }
        const clouds::StaticWeatherMode first =
            options.weatherTimeline.modes.front();
        options.cloudPreset = clouds::StaticWeatherModeBasePreset(first);
        presetName = options.cloudPreset;
        options.cloud.stormFront =
            clouds::StaticWeatherModeUsesStormFront(first);
    }
    else if (options.weatherTimelinePlay ||
             options.weatherTimelineTimeExplicit ||
             options.weatherTimelineDurationExplicit ||
             options.weatherTimelineInterruptExplicit ||
             options.weatherTimelineInterruptDurationExplicit)
    {
        LogError("timeline options require --weather-timeline");
        return 2;
    }

    if (options.capture && options.captureDir.empty())
        options.captureDir = ExecutableDirectory() / L"artifacts" / L"capture";
    if (options.resizeTest && options.frames == 0)
        options.frames = 90;

    const std::string validation = options.noise.Validate();
    if (!validation.empty())
    {
        LogError("invalid noise parameters: %s", validation.c_str());
        return 2;
    }

    Log("options: mode=%s preset=%s window=%ux%u steps=%u shadow=%u "
        "sunShadow=%.0f coverage=%.2f density=%.2f "
        "time=%.2f size=%u seed=%u adapter=%d warp=%d debug=%d gpuValidation=%d "
        "testNoValidation=%d testNoInfoQueue=%d vsync=%d selftest=%d capture=%d frames=%u "
        "ablation=%s shape=%s formula=%s densityModel=%s referenceStage=%s "
        "noiseMix=%s coordinateMode=%s fieldMode=%s shape=%.3f-%.3f "
        "multi=%.3f/%.3f mod=%.3f densityScale=%.2f "
        "storm=%d/%.0f/%.0f/%d "
        "macro=%.0f/%.0f/%.2f/%.2f/%.2f/%.2f/%.2f/%.2f/%u/%d/%d/%d/%d "
        "noiseFieldAxis=%u noiseFieldOffset=%.3f "
        "jitter=%s lighting=%s atmosphere=%d profile=%s "
        "sequence=%d motion=%s wind=%d injectError=%d",
        options.mode == clouds::RenderMode::Clouds ? "clouds" : "slices",
        presetName.c_str(),
        options.windowWidth, options.windowHeight, options.cloud.stepCount,
        options.cloud.shadowStepCount, options.cloud.sunShadowDistance,
        options.cloud.coverage, options.cloud.density,
        options.initialCloudTime,
        options.noise.resolution, options.noise.seed,
        options.renderer.adapterIndex, options.renderer.forceWarp ? 1 : 0,
        options.renderer.debugLayer ? 1 : 0,
        options.renderer.gpuValidation ? 1 : 0,
        options.renderer.testNoValidation ? 1 : 0,
        options.renderer.testNoInfoQueue ? 1 : 0,
        options.renderer.vsync ? 1 : 0,
        options.selfTest ? 1 : 0, options.capture ? 1 : 0, options.frames,
        clouds::AblationModeName(options.cloudAblation),
        clouds::ShapePrototypeName(options.shapePrototype),
        clouds::CloudFormulaName(options.cloudFormula),
        clouds::DensityModelName(options.densityModel),
        clouds::ReferenceStageName(options.referenceStage),
        clouds::ReferenceNoiseMixName(options.reference.noiseMix),
        clouds::ReferenceCoordinateModeName(options.reference.coordinateMode),
        clouds::ReferenceFieldModeName(options.reference.fieldMode),
        options.reference.shapeLow, options.reference.shapeHigh,
        options.reference.multiFrequencyScale,
        options.reference.multiFrequencyWeight,
        options.reference.modulationAmplitude,
        options.reference.densityScale,
        options.reference.stormVolume ? 1 : 0,
        options.reference.stormHeightVariation,
        options.reference.stormBottomVariation,
        options.reference.stormMacroGapReplace ? 1 : 0,
        options.reference.macroScale,
        options.reference.macroSecondaryScale,
        options.reference.macroThreshold,
        options.reference.macroSoftness,
        options.reference.macroDetailWeight,
        options.reference.macroSecondaryWeight,
        options.reference.macroEdgeJitter,
        options.reference.macroHeightWeight,
        options.reference.macroSeed,
        options.reference.macroCoveragePreserve ? 1 : 0,
        options.reference.macroSeedHash ? 1 : 0,
        options.reference.macroDetailCompensate ? 1 : 0,
        options.reference.macroCoverageCalibrate ? 1 : 0,
        options.reference.noiseFieldAxis, options.reference.noiseFieldOffset,
        clouds::JitterModeName(options.cloud.jitterMode),
        clouds::LightingModeName(options.cloud.lightingMode),
        options.cloud.atmosphereEnabled ? 1 : 0,
        options.qualityProfile.empty() ? "none" : options.qualityProfile.c_str(),
        options.sequenceDir.empty() ? 0 : 1, options.motionName.c_str(),
        options.windDisabled ? 0 : 1, options.injectDebugError ? 1 : 0);
    Log("storm front: mode=%d debug=%u raw_cloud=%d",
        options.cloud.stormFront ? 1 : 0,
        options.cloud.stormFrontDebug, options.captureRawCloud ? 1 : 0);
    if (options.weatherModeExplicit)
    {
        Log("weather mode: %s%s",
            clouds::StaticWeatherModeName(options.weatherMode),
            clouds::StaticWeatherModeIsDeveloperOnly(options.weatherMode)
                ? " (developer preview, NOT ACCEPTED)" : "");
    }
    else
    {
        Log("weather mode: legacy preset path");
    }
    if (options.weatherTransitionExplicit)
    {
        const float phase = std::clamp(options.transitionPhase, 0.0f, 1.0f);
        const float mix = phase * phase * (3.0f - 2.0f * phase);
        Log("weather transition: source=%s target=%s phase=%.3f mix=%.3f "
            "(developer-only B1)",
            clouds::StaticWeatherModeName(options.transitionSource),
            clouds::StaticWeatherModeName(options.transitionTarget),
            phase, mix);
    }

    const bool showErrors = !options.selfTest && !options.capture &&
                            options.frames == 0;

    try
    {
        Application app(std::move(options));
        return app.Run();
    }
    catch (const std::exception& exception)
    {
        LogError("fatal error: %s", exception.what());
        if (showErrors)
        {
            const std::wstring message = Utf8ToWide(exception.what());
            MessageBoxW(nullptr, message.c_str(), L"OGSR Облака V0 — ошибка",
                        MB_OK | MB_ICONERROR);
        }
        return 1;
    }
}
