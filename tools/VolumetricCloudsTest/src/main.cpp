#include "Application.h"
#include "Common.h"

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

void PrintCliHelp()
{
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
    Log("  --sequence=DIR       render a deterministic frame sequence and exit");
    Log("  --motion=static|pan-slow|pan-fast|up|fly|fly-level  sequence camera motion");
    Log("  --camera=x,y,z       sequence start position");
    Log("  --camera-yaw=F       sequence start yaw (degrees)");
    Log("  --camera-pitch=F     sequence start pitch (degrees)");
    Log("  --fov=F              vertical FOV override (degrees, 20..120)");
    Log("  --wind=on|off        wind animation for sequences (default on)");
    Log("  --coverage=F         cloud coverage 0..1");
    Log("  --density=F          cloud density multiplier 0..2");
    Log("  --debug-mode=final|density|transmittance|steps|occupancy|weather|cloud-type|density-slice|density-horizontal|ray-profile|density-vertical-z|macro-shape|cross-type-difference|noise-field|view-optical-depth");
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
#ifdef _DEBUG
    options.renderer.debugLayer = true;
#endif

    for (int i = 1; i < argc; ++i)
    {
        const std::wstring argument = argv[i];
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
        }
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
        else if (argument.rfind(L"--profile=", 0) == 0)
        {
            const std::wstring profile = argument.substr(10);
            if (profile == L"performance")
                options.qualityProfile = "performance";
            else if (profile == L"balanced")
                options.qualityProfile = "balanced";
            else if (profile == L"quality")
                options.qualityProfile = "quality";
            else if (profile == L"reference")
                options.qualityProfile = "reference";
            else
            {
                LogError("invalid --profile value: %s (expected performance, "
                         "balanced, quality or reference)",
                         WideToUtf8(profile).c_str());
                return 2;
            }
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
