#include "CloudSettings.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace
{
int g_failures = 0;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", message);
        ++g_failures;
    }
}
} // namespace

int main()
{
    using namespace clouds;

    // Full valid schema.
    {
        const CloudSettingsLoadResult r = ParseCloudSettingsText(
            R"({
  "schema_version": 1,
  "clouds_enabled": false,
  "cloud_render_mode": "volumetric3d",
  "cloud_quality": "performance"
})");
        Expect(r.loaded && !r.error, "valid schema loads");
        Expect(!r.settings.cloudsEnabled, "clouds_enabled=false applied");
        Expect(r.settings.renderMode == CloudRenderMode::Volumetric3D,
               "volumetric3d applied");
        Expect(r.settings.quality == CloudQuality::Performance,
               "performance applied");
        Expect(r.hasCloudsEnabled && r.hasQuality && r.hasRenderMode,
               "presence flags set");
    }

    // Partial schema keeps safe defaults.
    {
        const CloudSettingsLoadResult r = ParseCloudSettingsText(
            R"({"cloud_quality":"balanced"})");
        Expect(r.loaded && !r.error, "partial schema loads");
        Expect(r.settings.cloudsEnabled, "partial defaults clouds on");
        Expect(r.settings.renderMode == CloudRenderMode::Volumetric3D,
               "partial defaults volumetric");
        Expect(r.settings.quality == CloudQuality::Balanced,
               "partial quality applied");
        Expect(r.hasQuality && !r.hasCloudsEnabled && !r.hasRenderMode,
               "partial presence flags");
    }

    // Weather-looking keys are ignored and cannot mutate public settings.
    {
        const CloudSettingsLoadResult r = ParseCloudSettingsText(
            R"({
  "schema_version": 1,
  "clouds_enabled": false,
  "cloud_quality": "reference",
  "weather_mode": "rain_sf",
  "storm_front": true,
  "coverage": 0.1,
  "density": 9.9,
  "seed": 7,
  "shadow_od": 0.01
})");
        Expect(r.loaded && !r.error, "unknown weather keys ignored");
        Expect(!r.settings.cloudsEnabled, "clouds_enabled still applied");
        Expect(r.settings.quality == CloudQuality::Reference,
               "cloud quality still applied");
        Expect(r.settings.renderMode == CloudRenderMode::Volumetric3D,
               "weather keys did not alter render mode");
        Expect(r.unknownKeys.size() == 6,
               "weather/debug keys reported as unknown");
        Expect(r.unknownKeys.size() >= 5 && r.unknownKeys[0] == "coverage",
               "unknown key names retained for diagnostics");
    }

    // Noise/format keys are developer-only and must stay inert in a public
    // graphics config. They are top-level unknown keys, not rendering controls.
    {
        const CloudSettingsLoadResult r = ParseCloudSettingsText(
            R"({
  "schema_version": 1,
  "clouds_enabled": true,
  "cloud_quality": "balanced",
  "seed": 42,
  "noise_seed": 43,
  "size": 64,
  "format": "unorm"
})");
        Expect(r.loaded && !r.error, "noise keys ignored");
        Expect(r.settings.quality == CloudQuality::Balanced,
               "public quality still applied");
        Expect(r.settings.cloudsEnabled, "clouds field still applied");
        Expect(r.unknownKeys.size() == 4,
               "noise keys reported as unknown");
    }

    // Nested malformed input is rejected, not silently flattened into noise
    // or weather controls.
    {
        const CloudSettingsLoadResult r = ParseCloudSettingsText(
            R"({"clouds_enabled": true, "noise": {"seed": 42}})");
        Expect(r.error && !r.loaded, "nested object is rejected");
        Expect(r.settings.cloudsEnabled, "nested object keeps clouds default");
    }

    // Duplicate known keys use last-key-wins and cannot create a weather path.
    {
        const CloudSettingsLoadResult r = ParseCloudSettingsText(
            R"({"clouds_enabled": false, "clouds_enabled": true})");
        Expect(r.loaded && !r.error, "duplicate key is valid JSON");
        Expect(r.settings.cloudsEnabled, "duplicate key last value wins");
    }

    // UTF-8 BOM is tolerated (Windows editors often write one).
    {
        const CloudSettingsLoadResult r = ParseCloudSettingsText(
            "\xEF\xBB\xBF{\"clouds_enabled\": false}");
        Expect(r.loaded && !r.error, "UTF-8 BOM is tolerated");
        Expect(!r.settings.cloudsEnabled, "BOM config field applied");
    }

    // Corrupt JSON falls back safely.
    {
        const CloudSettingsLoadResult r = ParseCloudSettingsText(
            R"({"cloud_quality": )");
        Expect(r.error, "corrupt JSON is rejected");
        Expect(!r.loaded, "corrupt JSON is not loaded");
        Expect(r.settings.cloudsEnabled, "corrupt JSON defaults clouds on");
        Expect(r.settings.quality == CloudQuality::Quality,
               "corrupt JSON defaults quality");
    }

    // Unknown future schema version falls back safely.
    {
        const CloudSettingsLoadResult r = ParseCloudSettingsText(
            R"({"schema_version": 2, "clouds_enabled": false})");
        Expect(r.error && r.unsupportedVersion, "version 2 is unsupported");
        Expect(r.settings.cloudsEnabled, "version 2 defaults clouds on");
    }

    // Invalid enum value falls back per field.
    {
        const CloudSettingsLoadResult r = ParseCloudSettingsText(
            R"({"cloud_quality": "ultra", "clouds_enabled": true})");
        Expect(r.error, "invalid quality reports error");
        Expect(r.settings.quality == CloudQuality::Quality,
               "invalid quality defaults to quality");
        Expect(r.settings.cloudsEnabled, "valid clouds field still applied");
    }

    // Classic 2D is a future contract; standalone falls back.
    {
        const CloudSettingsLoadResult r = ParseCloudSettingsText(
            R"({"cloud_render_mode": "classic2d"})");
        Expect(r.loaded && r.classic2dFallback,
               "classic2d fallback flagged");
        Expect(r.settings.renderMode == CloudRenderMode::Volumetric3D,
               "classic2d falls back to volumetric");
        Expect(r.hasRenderMode, "classic2d presence retained");
    }

    // Atomic save/reload, overwrite and missing-file defaults.
    {
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "ogsr_01c_a_settings_test";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        const std::filesystem::path path = dir / "cloud_settings.json";
        std::string error;

        CloudSettingsLoadResult missing = LoadCloudSettings(path);
        Expect(!missing.fileFound && !missing.error,
               "missing settings file is safe default");
        Expect(missing.settings.quality == CloudQuality::Quality,
               "missing file default quality");

        CloudGraphicsSettings settings;
        settings.cloudsEnabled = false;
        settings.quality = CloudQuality::Performance;
        settings.renderMode = CloudRenderMode::Volumetric3D;
        Expect(SaveCloudSettings(path, settings, error),
               "save settings succeeds");
        Expect(!std::filesystem::exists(path.wstring() + L".tmp"),
               "atomic temp file removed");

        CloudSettingsLoadResult loaded = LoadCloudSettings(path);
        Expect(loaded.fileFound && loaded.loaded && !loaded.error,
               "saved settings reload");
        Expect(!loaded.settings.cloudsEnabled,
               "saved clouds_enabled reload");
        Expect(loaded.settings.quality == CloudQuality::Performance,
               "saved quality reload");

        settings.cloudsEnabled = true;
        settings.quality = CloudQuality::Reference;
        settings.renderMode = CloudRenderMode::Volumetric3D;
        Expect(SaveCloudSettings(path, settings, error),
               "overwrite settings succeeds");
        loaded = LoadCloudSettings(path);
        Expect(loaded.settings.cloudsEnabled, "overwritten clouds reload");
        Expect(loaded.settings.quality == CloudQuality::Reference,
               "overwritten quality reload");

        {
            std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
            corrupt << "{broken";
        }
        loaded = LoadCloudSettings(path);
        Expect(loaded.fileFound && loaded.error,
               "corrupt saved file reports error");
        Expect(loaded.settings.quality == CloudQuality::Quality,
               "corrupt saved file defaults quality");
        std::filesystem::remove_all(dir, ec);
    }

    // Factory reset is represented by defaults when no valid file exists.
    {
        const CloudGraphicsSettings defaults = DefaultCloudGraphicsSettings();
        Expect(defaults.schemaVersion == 1, "default schema version");
        Expect(defaults.cloudsEnabled, "default clouds on");
        Expect(defaults.quality == CloudQuality::Quality,
               "default quality is quality");
        Expect(defaults.renderMode == CloudRenderMode::Volumetric3D,
               "default render mode is volumetric");
    }

    if (g_failures != 0)
    {
        std::printf("CLOUD SETTINGS TESTS: FAIL (%d)\n", g_failures);
        return 1;
    }
    std::printf("CLOUD SETTINGS TESTS: PASS\n");
    return 0;
}
