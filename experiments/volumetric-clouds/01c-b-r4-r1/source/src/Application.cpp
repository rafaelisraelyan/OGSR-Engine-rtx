#include "Application.h"

#include "AuditPolicy.h"
#include "CloudFaultInjection.h"
#include "CloudGpuErrors.h"
#include "ImageIO.h"
#include "Validation.h"

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <locale>
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

std::filesystem::path ExecutablePath()
{
    wchar_t module[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
        return {};
    return std::filesystem::path(module);
}

std::string FileSha256(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return "unavailable";
    const std::vector<uint8_t> data(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                    nullptr, 0) < 0)
    {
        return "unavailable";
    }
    uint8_t digest[32]{};
    const NTSTATUS status = BCryptHash(
        algorithm, nullptr, 0, const_cast<PUCHAR>(data.data()),
        static_cast<ULONG>(data.size()), digest, sizeof(digest));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0)
        return "unavailable";

    char hex[65]{};
    for (int i = 0; i < 32; ++i)
        snprintf(hex + i * 2, 3, "%02X", digest[i]);
    return hex;
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

struct WeatherPixelStats
{
    bool finite = true;
    bool inRange = true;
    std::array<double, 4> mean{};
    std::array<double, 4> minimum{ 1.0, 1.0, 1.0, 1.0 };
    std::array<double, 4> maximum{};
};

WeatherPixelStats AnalyzeWeatherMap(const std::vector<float>& rgba,
                                    uint32_t resolution)
{
    WeatherPixelStats stats{};
    const size_t pixels = static_cast<size_t>(resolution) * resolution;
    if (rgba.size() != pixels * 4)
    {
        stats.finite = false;
        stats.inRange = false;
        return stats;
    }

    for (size_t i = 0; i < pixels; ++i)
    {
        for (size_t channel = 0; channel < 4; ++channel)
        {
            const float value = rgba[i * 4 + channel];
            if (!std::isfinite(value))
            {
                stats.finite = false;
                continue;
            }
            stats.inRange = stats.inRange && value >= 0.0f && value <= 1.0f;
            stats.mean[channel] += value;
            stats.minimum[channel] = std::min(
                stats.minimum[channel], static_cast<double>(value));
            stats.maximum[channel] = std::max(
                stats.maximum[channel], static_cast<double>(value));
        }
    }
    for (double& value : stats.mean)
        value /= static_cast<double>(pixels);
    return stats;
}

double MeanAbsFloatDifference(const std::vector<float>& a,
                              const std::vector<float>& b)
{
    if (a.size() != b.size() || a.empty())
        return 0.0;
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        sum += std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
    return sum / static_cast<double>(a.size());
}

double MaxAbsFloatDifference(const std::vector<float>& a,
                             const std::vector<float>& b)
{
    if (a.size() != b.size() || a.empty())
        return 0.0;
    double maximum = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        maximum = std::max(maximum, std::fabs(
            static_cast<double>(a[i]) - static_cast<double>(b[i])));
    return maximum;
}

struct WeatherSeamMetrics
{
    std::array<double, 4> seamX{};
    std::array<double, 4> seamY{};
    std::array<double, 4> neighborX{};
    std::array<double, 4> neighborY{};
    std::array<double, 4> gradientRatioX{};
    std::array<double, 4> gradientRatioY{};
    bool ok = false;
};

WeatherSeamMetrics AnalyzeWeatherSeams(const std::vector<float>& rgba,
                                       uint32_t resolution)
{
    WeatherSeamMetrics metrics{};
    if (rgba.size() != static_cast<size_t>(resolution) * resolution * 4)
        return metrics;

    for (uint32_t y = 0; y < resolution; ++y)
    {
        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            const size_t left =
                (static_cast<size_t>(y) * resolution + 0) * 4 + channel;
            const size_t right =
                (static_cast<size_t>(y) * resolution + resolution - 1) * 4 +
                channel;
            const size_t before =
                (static_cast<size_t>(y) * resolution + resolution - 2) * 4 +
                channel;
            metrics.seamX[channel] +=
                std::fabs(static_cast<double>(rgba[left]) - rgba[right]);
            metrics.neighborX[channel] +=
                std::fabs(static_cast<double>(rgba[before]) - rgba[right]);
        }
    }
    for (uint32_t x = 0; x < resolution; ++x)
    {
        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            const size_t top = (0 * resolution + x) * 4 + channel;
            const size_t bottom =
                (static_cast<size_t>(resolution - 1) * resolution + x) * 4 +
                channel;
            const size_t before =
                (static_cast<size_t>(resolution - 2) * resolution + x) * 4 +
                channel;
            metrics.seamY[channel] +=
                std::fabs(static_cast<double>(rgba[top]) - rgba[bottom]);
            metrics.neighborY[channel] +=
                std::fabs(static_cast<double>(rgba[before]) - rgba[bottom]);
        }
    }

    const double normalization = static_cast<double>(resolution);
    metrics.ok = true;
    for (uint32_t channel = 0; channel < 4; ++channel)
    {
        metrics.seamX[channel] /= normalization;
        metrics.seamY[channel] /= normalization;
        metrics.neighborX[channel] /= normalization;
        metrics.neighborY[channel] /= normalization;
        metrics.gradientRatioX[channel] =
            metrics.seamX[channel] / (metrics.neighborX[channel] + 1e-6);
        metrics.gradientRatioY[channel] =
            metrics.seamY[channel] / (metrics.neighborY[channel] + 1e-6);
        // Continuity is not "seam == 0": a smooth periodic field has a small
        // finite edge step. Compare it to the adjacent interior gradient and
        // require the same order of magnitude.
        metrics.ok = metrics.ok &&
            metrics.seamX[channel] <=
                metrics.neighborX[channel] * 3.0 + 0.05 &&
            metrics.seamY[channel] <=
                metrics.neighborY[channel] * 3.0 + 0.05;
    }
    return metrics;
}

bool WeatherMapSeamOk(const std::vector<float>& rgba, uint32_t resolution)
{
    return AnalyzeWeatherSeams(rgba, resolution).ok;
}
bool WriteProbeFiles(
    const CloudProbeReadback& probe,
    const std::filesystem::path& directory,
    const clouds::Camera& camera,
    const clouds::CloudParams& params,
    uint32_t width,
    uint32_t height,
    float timeSeconds,
    uint32_t noiseSeed,
    uint32_t weatherSeed,
    uint32_t cloudDivisor,
    const std::string& sceneName)
{
    if (probe.rows == 0 || probe.steps == 0 ||
        probe.values.size() <
            static_cast<size_t>(probe.rows) * probe.steps * 12)
    {
        return false;
    }

    const uint32_t probeX = std::clamp(
        static_cast<int>(params.probeColumn * static_cast<float>(width)),
        0, static_cast<int>(width) - 1);
    const float aspect = static_cast<float>(width) /
                         static_cast<float>(std::max(height, 1u));
    const float tanHalfFov =
        std::tan(DirectX::XMConvertToRadians(camera.fovYDegrees) * 0.5f);
    const DirectX::XMFLOAT3 forward = camera.Forward();
    const DirectX::XMFLOAT3 right = camera.Right();
    const DirectX::XMFLOAT3 up = camera.Up();
    const float layerThickness =
        std::max(params.layerTop - params.layerBottom, 1.0f);
    const float margin =
        params.weatherHeightVariation * 0.5f + layerThickness * 0.35f;
    const float volumeBottom = params.layerBottom - margin;
    const float volumeTop = params.layerTop + margin;

    std::ofstream csv(directory / "probe_profile.csv", std::ios::trunc);
    std::ofstream bin(directory / "probe_float32.bin",
                      std::ios::binary | std::ios::trunc);
    std::ofstream meta(directory / "probe_params.txt", std::ios::trunc);
    if (!csv || !bin || !meta)
        return false;

    csv << "row,step,t,x,y,z,density,height,profile,shaped,"
           "macroR,midG,weatherR,typeValue,coverage\n";
    csv << std::setprecision(6) << std::fixed;

    const uint32_t magic = 0x50524753u; // "SGRP"
    const uint32_t records = 3u;
    bin.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    bin.write(reinterpret_cast<const char*>(&probe.rows), sizeof(probe.rows));
    bin.write(reinterpret_cast<const char*>(&probe.steps),
              sizeof(probe.steps));
    bin.write(reinterpret_cast<const char*>(&records), sizeof(records));
    bin.write(reinterpret_cast<const char*>(probe.values.data()),
              static_cast<std::streamsize>(probe.values.size() *
                                           sizeof(float)));

    for (uint32_t row = 0; row < probe.rows; ++row)
    {
        const float uvx =
            (static_cast<float>(probeX) + 0.5f) / static_cast<float>(width);
        const float uvy =
            (static_cast<float>(row) + 0.5f) / static_cast<float>(height);
        const float ndcX = uvx * 2.0f - 1.0f;
        const float ndcY = 1.0f - uvy * 2.0f;
        DirectX::XMFLOAT3 direction(
            forward.x + right.x * (ndcX * tanHalfFov * aspect) +
                up.x * (ndcY * tanHalfFov),
            forward.y + right.y * (ndcX * tanHalfFov * aspect) +
                up.y * (ndcY * tanHalfFov),
            forward.z + right.z * (ndcX * tanHalfFov * aspect) +
                up.z * (ndcY * tanHalfFov));
        const float length = std::sqrt(direction.x * direction.x +
                                       direction.y * direction.y +
                                       direction.z * direction.z);
        direction.x /= length;
        direction.y /= length;
        direction.z /= length;

        float t0 = 0.0f;
        float t1 = -1.0f;
        if (std::abs(direction.y) < 1e-5f)
        {
            if (camera.position.y >= volumeBottom &&
                camera.position.y <= volumeTop)
            {
                t0 = 0.0f;
                t1 = params.maxDistance;
            }
        }
        else
        {
            const float a = (volumeBottom - camera.position.y) / direction.y;
            const float b = (volumeTop - camera.position.y) / direction.y;
            t0 = std::max(std::min(a, b), 0.0f);
            t1 = std::min(std::max(a, b), params.maxDistance);
        }
        if (t1 <= t0)
            continue;

        const float stepLength =
            (t1 - t0) / static_cast<float>(probe.steps);
        for (uint32_t step = 0; step < probe.steps; ++step)
        {
            const size_t base =
                (static_cast<size_t>(row) * probe.steps + step) * 12;
            const float density = probe.values[base + 0];
            const float sampleHeight = probe.values[base + 1];
            const float profile = probe.values[base + 2];
            const float shaped = probe.values[base + 3];
            const float macroR = probe.values[base + 4];
            const float midG = probe.values[base + 5];
            const float weatherR = probe.values[base + 6];
            const float typeValue = probe.values[base + 7];
            const float coverage = probe.values[base + 8];
            const float t = t0 + (static_cast<float>(step) + 0.5f) *
                                     stepLength;
            const float x = camera.position.x + direction.x * t;
            const float y = camera.position.y + direction.y * t;
            const float z = camera.position.z + direction.z * t;
            csv << row << ',' << step << ',' << t << ',' << x << ',' << y
                << ',' << z << ',' << density << ',' << sampleHeight << ','
                << profile << ',' << shaped << ',' << macroR << ',' << midG
                << ',' << weatherR << ',' << typeValue << ',' << coverage
                << '\n';
        }
    }

    const std::filesystem::path executable = ExecutablePath();
    meta << "OGSR V2.3d world-space probe\n";
    meta << "timestamp: " << TimestampUtc() << "\n";
    meta << "executable: "
         << WideToUtf8(executable.wstring()) << "\n";
    meta << "executable_sha256: " << FileSha256(executable) << "\n";
#ifdef _DEBUG
    meta << "build: Debug\n";
#else
    meta << "build: Release\n";
#endif
    meta << "scene: " << sceneName << "\n";
    meta << "cloud_output: " << width << "x" << height << "\n";
    meta << "cloud_divisor: " << cloudDivisor << "\n";
    meta << "output_aspect: " << aspect << "\n";
    meta << "camera_position: " << camera.position.x << " "
         << camera.position.y << " " << camera.position.z << "\n";
    meta << "camera_yaw_deg: " << camera.yawDegrees << "\n";
    meta << "camera_pitch_deg: " << camera.pitchDegrees << "\n";
    meta << "camera_fov_y_deg: " << camera.fovYDegrees << "\n";
    meta << "cloud_time_s: " << timeSeconds << "\n";
    meta << "noise_seed: " << noiseSeed << "\n";
    meta << "weather_seed: " << weatherSeed << "\n";
    const DirectX::XMFLOAT3 windOffset =
        clouds::WindOffsetWorld(params, timeSeconds);
    meta << "wind_speed: " << params.windSpeed << "\n";
    meta << "wind_direction: " << params.windDirection.x << " "
         << params.windDirection.y << " " << params.windDirection.z << "\n";
    meta << "wind_offset: " << windOffset.x << " " << windOffset.y << " "
         << windOffset.z << "\n";
    meta << "layer_bottom: " << params.layerBottom << "\n";
    meta << "layer_top: " << params.layerTop << "\n";
    meta << "volume_margin: " << margin << "\n";
    meta << "weather_height_variation: "
         << params.weatherHeightVariation << "\n";
    meta << "coverage: " << params.coverage << "\n";
    meta << "density: " << params.density << "\n";
    meta << "extinction: " << params.extinction << "\n";
    meta << "anisotropy: " << params.anisotropy << "\n";
    meta << "noise_scale: " << params.noiseScale << "\n";
    meta << "vertical_noise_scale: " << params.verticalNoiseScale << "\n";
    meta << "step_count: " << params.stepCount << "\n";
    meta << "shadow_step_count: " << params.shadowStepCount << "\n";
    meta << "sun_shadow_distance: " << params.sunShadowDistance << "\n";
    meta << "max_distance: " << params.maxDistance << "\n";
    meta << "sun_elevation_deg: " << params.sunElevationDegrees << "\n";
    meta << "sun_azimuth_deg: " << params.sunAzimuthDegrees << "\n";
    const DirectX::XMFLOAT3 sun = clouds::SunDirection(params);
    meta << "sun_direction: " << sun.x << " " << sun.y << " " << sun.z
         << "\n";
    meta << "sun_intensity: " << params.sunIntensity << "\n";
    meta << "atmosphere_enabled: "
         << (params.atmosphereEnabled ? 1 : 0) << "\n";
    meta << "atmosphere_extinction: " << params.atmosphereExtinction
         << "\n";
    meta << "atmosphere_height_falloff: "
         << params.atmosphereHeightFalloff << "\n";
    meta << "ablation: "
         << clouds::AblationModeName(params.ablationMode) << "\n";
    meta << "jitter: " << clouds::JitterModeName(params.jitterMode) << "\n";
    meta << "lighting: "
         << clouds::LightingModeName(params.lightingMode) << "\n";
    meta << "self_shadow: " << (params.selfShadowEnabled ? 1 : 0) << "\n";
    meta << "probe_column: " << params.probeColumn << "\n";
    meta << "probe_x: " << probeX << "\n";
    meta << "probe_steps: " << probe.steps << "\n";
    meta << "probe_rows: " << probe.rows << "\n";
    meta << "probe_records: 3\n";
    meta << "record0: density,height,profile,shaped\n";
    meta << "record1: macroR,midG,weatherR,typeValue\n";
    meta << "record2: coverage,t_normalized,profile_times_shaped,0\n";
    return true;
}

bool WriteNoiseFieldFiles(
    const CloudProbeReadback& probe,
    const std::filesystem::path& directory,
    const clouds::Camera& camera,
    const clouds::CloudParams& params,
    const clouds::ReferenceControls& reference,
    uint32_t width,
    uint32_t height,
    float timeSeconds,
    uint32_t noiseSeed,
    uint32_t weatherSeed,
    uint32_t cloudDivisor,
    const std::string& sceneName,
    clouds::CloudDensityModel densityModel,
    clouds::ReferenceStage stage)
{
    constexpr uint32_t kSize = 256u;
    constexpr uint32_t kRecords = 3u;
    if (probe.rows != kSize || probe.steps != kSize ||
        probe.values.size() <
            static_cast<size_t>(kSize) * kSize * kRecords * 4u)
    {
        return false;
    }

    const uint32_t axis = reference.noiseFieldAxis % 3u;
    const float offset =
        std::clamp(reference.noiseFieldOffset, 0.0f, 1.0f);
    const float extentKm =
        std::max(params.densitySliceExtentKm, 2.0f);
    const float volumeBottom = params.layerBottom;
    const float volumeTop = params.layerTop;

    std::ofstream bin(directory / "noise_field_float32.bin",
                      std::ios::binary | std::ios::trunc);
    std::ofstream csv(directory / "noise_field_sample.csv",
                      std::ios::trunc);
    std::ofstream stats(directory / "noise_field_stats.csv",
                        std::ios::trunc);
    std::ofstream meta(directory / "noise_field_meta.txt",
                       std::ios::trunc);
    if (!bin || !csv || !stats || !meta)
        return false;
    csv.imbue(std::locale::classic());
    stats.imbue(std::locale::classic());
    meta.imbue(std::locale::classic());

    const uint32_t magic = 0x4E465231u; // "NFR1"
    const uint32_t version = 1u;
    const uint32_t headerUints[8] = {
        magic,
        version,
        probe.rows,
        probe.steps,
        kRecords,
        axis,
        static_cast<uint32_t>(reference.coordinateMode),
        static_cast<uint32_t>(reference.noiseMix),
    };
    const float headerFloats[8] = {
        reference.shapeLow,
        reference.shapeHigh,
        offset,
        extentKm,
        params.noiseScale,
        params.verticalNoiseScale,
        volumeBottom,
        volumeTop,
    };
    bin.write(reinterpret_cast<const char*>(headerUints),
              sizeof(headerUints));
    bin.write(reinterpret_cast<const char*>(headerFloats),
              sizeof(headerFloats));
    bin.write(reinterpret_cast<const char*>(probe.values.data()),
              static_cast<std::streamsize>(probe.values.size() *
                                           sizeof(float)));

    const char* const seriesNames[9] = {
        "noise_r", "noise_g", "noise_b", "shape_field", "transfer",
        "profile", "height_fraction", "density_field",
        "fetch_count",
    };
    const uint32_t seriesIndex[9] = { 0, 1, 2, 3, 4, 5, 6, 7, 11 };

    stats << "channel,mean,stddev,min,max,p05,p50,p95,"
             "nonzero_fraction\n";
    stats << std::setprecision(8) << std::fixed;
    for (uint32_t s = 0; s < 9; ++s)
    {
        std::vector<float> values;
        values.reserve(static_cast<size_t>(kSize) * kSize);
        double sum = 0.0;
        double sumSq = 0.0;
        uint32_t nonzero = 0;
        for (uint32_t i = 0; i < kSize * kSize; ++i)
        {
            const float value =
                probe.values[static_cast<size_t>(i) * 12u + seriesIndex[s]];
            values.push_back(value);
            sum += value;
            sumSq += static_cast<double>(value) * value;
            if (std::fabs(value) > 1e-6f)
                ++nonzero;
        }
        std::sort(values.begin(), values.end());
        const double count = static_cast<double>(values.size());
        const double mean = sum / count;
        const double variance =
            std::max(sumSq / count - mean * mean, 0.0);
        const auto percentile =
            [&values](double p)
            {
                const size_t index = static_cast<size_t>(
                    std::clamp(p, 0.0, 1.0) *
                    static_cast<double>(values.size() - 1));
                return values[index];
            };
        stats << seriesNames[s] << ',' << mean << ','
              << std::sqrt(variance) << ',' << values.front() << ','
              << values.back() << ',' << percentile(0.05) << ','
              << percentile(0.50) << ',' << percentile(0.95) << ','
              << static_cast<double>(nonzero) / count << '\n';
    }

    csv << "x,y,u,v,world_x,world_y,world_z,noise_r,noise_g,noise_b,"
           "shape_field,transfer,profile,height_fraction,"
           "density_field,fetch_count\n";
    csv << std::setprecision(6) << std::fixed;
    for (uint32_t y = 0; y < kSize; y += 4)
    {
        for (uint32_t x = 0; x < kSize; x += 4)
        {
            const size_t base =
                (static_cast<size_t>(y) * kSize + x) * 12u;
            const float u =
                (static_cast<float>(x) + 0.5f) / float(kSize);
            const float v =
                (static_cast<float>(y) + 0.5f) / float(kSize);
            csv << x << ',' << y << ',' << u << ',' << v << ','
                << probe.values[base + 8] << ','
                << probe.values[base + 9] << ','
                << probe.values[base + 10] << ','
                << probe.values[base + 0] << ','
                << probe.values[base + 1] << ','
                << probe.values[base + 2] << ','
                << probe.values[base + 3] << ','
                << probe.values[base + 4] << ','
                << probe.values[base + 5] << ','
                << probe.values[base + 6] << ','
                << probe.values[base + 7] << ','
                << probe.values[base + 11] << '\n';
        }
    }

    const char* const axisName = axis == 0u ? "xy" : (axis == 1u ? "xz" : "yz");
    meta << "OGSR V2.3h-A01 raw noise field\n";
    meta << "timestamp: " << TimestampUtc() << "\n";
    meta << "executable: "
         << WideToUtf8(ExecutablePath().wstring()) << "\n";
    meta << "executable_sha256: " << FileSha256(ExecutablePath()) << "\n";
#ifdef _DEBUG
    meta << "build: Debug\n";
#else
    meta << "build: Release\n";
#endif
    meta << "scene: " << sceneName << "\n";
    meta << "density_model: " << clouds::DensityModelName(densityModel)
         << "\n";
    meta << "reference_stage: " << clouds::ReferenceStageName(stage)
         << "\n";
    meta << "noise_mix: " << clouds::ReferenceNoiseMixName(reference.noiseMix)
         << "\n";
    meta << "coordinate_mode: "
         << clouds::ReferenceCoordinateModeName(reference.coordinateMode)
         << "\n";
    meta << "field_mode: "
         << clouds::ReferenceFieldModeName(reference.fieldMode) << "\n";
    meta << "multi_frequency_scale: "
         << reference.multiFrequencyScale << "\n";
    meta << "multi_frequency_weight: "
         << reference.multiFrequencyWeight << "\n";
    meta << "modulation_amplitude: "
         << reference.modulationAmplitude << "\n";
    meta << "density_scale: " << reference.densityScale << "\n";
    meta << "storm_volume: " << (reference.stormVolume ? 1 : 0) << "\n";
    meta << "storm_height_variation: "
         << reference.stormHeightVariation << "\n";
    meta << "storm_bottom_variation: "
         << reference.stormBottomVariation << "\n";
    meta << "shape_transfer: smoothstep(" << reference.shapeLow << ", "
         << reference.shapeHigh << ")\n";
    meta << "slice_axis: " << axisName << "\n";
    meta << "slice_offset: " << offset << "\n";
    meta << "slice_extent_km: " << extentKm << "\n";
    meta << "field_size: " << kSize << "x" << kSize << "\n";
    meta << "records: 3\n";
    meta << "record0: noise_r,noise_g,noise_b,shape_field\n";
    meta << "record1: transfer,profile,height_fraction,density_field "
            "(H1/H2 transfer*profile; H3+ after coverage remap)\n";
    meta << "record2: world_x,world_y,world_z,fetch_count\n";
    meta << "binary_header: u32 magic,version,rows,steps,records,axis,"
            "coordinate_mode,noise_mix; "
            "f32 shape_low,shape_high,offset,extent_km,noise_scale,"
            "vertical_noise_scale,layer_bottom,layer_top\n";
    meta << "camera: " << camera.position.x << " " << camera.position.y
         << " " << camera.position.z << "\n";
    meta << "time: " << timeSeconds << "\n";
    meta << "noise_seed: " << noiseSeed << "\n";
    meta << "weather_seed: " << weatherSeed << "\n";
    meta << "noise_scale: " << params.noiseScale << "\n";
    meta << "preset_vertical_noise_scale: " << params.verticalNoiseScale
         << "\n";
    meta << "reference_vertical_noise_scale: 1.80\n";
    meta << "layer_bottom: " << params.layerBottom << "\n";
    meta << "layer_top: " << params.layerTop << "\n";
    meta << "density_multiplier: " << params.density << "\n";
    meta << "extinction: " << params.extinction << "\n";
    meta << "cloud_output: " << width << "x" << height << "\n";
    meta << "cloud_divisor: " << cloudDivisor << "\n";
    meta << "cloud_steps: " << params.stepCount << "\n";
    return true;
}

// V2.3h-A02c: raw linear optical-depth probe. Values come from the compute
// shader probe buffer (R32 float4 records), not from the composited sRGB
// capture. The binary is the analysis source; the BMP is display only.
bool WriteOpticalDepthFloat32(
    const std::vector<float>& records,
    const std::filesystem::path& directory,
    uint32_t width, uint32_t height,
    const clouds::Camera& camera,
    const clouds::CloudParams& params,
    const clouds::ReferenceControls& reference,
    clouds::CloudDensityModel densityModel,
    clouds::ReferenceStage stage,
    float timeSeconds,
    uint32_t noiseSeed,
    uint32_t weatherSeed,
    uint32_t cloudDivisor,
    bool windDisabled,
    const char* sceneName,
    const char* presetName)
{
    const size_t pixels = static_cast<size_t>(width) * height;
    if (records.size() != pixels * 4u)
        return false;

    double sumMapped = 0.0;
    double sumOd = 0.0;
    float minMapped = 1e30f;
    float maxMapped = -1e30f;
    float maxOd = 0.0f;
    bool finite = true;
    for (size_t i = 0; i < pixels; ++i)
    {
        const float mapped = records[i * 4u + 0u];
        const float od = records[i * 4u + 1u];
        if (!std::isfinite(mapped) || !std::isfinite(od))
            finite = false;
        sumMapped += mapped;
        sumOd += od;
        minMapped = std::min(minMapped, mapped);
        maxMapped = std::max(maxMapped, mapped);
        maxOd = std::max(maxOd, od);
    }

    std::ofstream bin(directory / "optical_depth_float32.bin",
                      std::ios::binary | std::ios::trunc);
    if (!bin)
        return false;
    bin.write(reinterpret_cast<const char*>(records.data()),
              static_cast<std::streamsize>(records.size() *
                                           sizeof(float)));
    if (!bin.good())
        return false;

    std::ofstream meta(directory / "optical_depth_float32.json",
                       std::ios::trunc);
    if (!meta)
        return false;
    meta << std::setprecision(9);
    meta << "{\n";
    meta << "  \"format\": \"OGSR_V2_3h_A02c_optical_depth_float32\",\n";
    meta << "  \"timestamp_utc\": \"" << TimestampUtc() << "\",\n";
    meta << "  \"executable_sha256\": \""
         << FileSha256(ExecutablePath()) << "\",\n";
#ifdef _DEBUG
    meta << "  \"build\": \"Debug\",\n";
#else
    meta << "  \"build\": \"Release\",\n";
#endif
    meta << "  \"scene\": \"" << sceneName << "\",\n";
    meta << "  \"preset\": \"" << presetName << "\",\n";
    meta << "  \"width\": " << width << ",\n";
    meta << "  \"height\": " << height << ",\n";
    meta << "  \"row_order\": \"top_left\",\n";
    meta << "  \"channels\": 4,\n";
    meta << "  \"record_stride_bytes\": 16,\n";
    meta << "  \"component_bytes\": 4,\n";
    meta << "  \"component_type\": \"float32_le\",\n";
    meta << "  \"channel_order\": [\"mapped_od\", \"od\", "
            "\"reserved_0\", \"reserved_1\"],\n";
    meta << "  \"mapped_od_definition\": \"od / (od + 1)\",\n";
    meta << "  \"od_definition\": \"-log(max(cloud_transmittance, 1e-6)), "
            "cloud-only, before atmosphere\",\n";
    meta << "  \"od_inverse\": \"od = mapped_od / (1 - mapped_od)\",\n";
    meta << "  \"linear_values\": true,\n";
    meta << "  \"exposure_applied\": false,\n";
    meta << "  \"tone_mapping_applied\": false,\n";
    meta << "  \"srgb_applied\": false,\n";
    meta << "  \"atmosphere_applied\": false,\n";
    meta << "  \"source\": \"CloudRayMarch.hlsl debugMode 19 -> "
            "ProbeOutput float32 structured buffer; composite bypassed\",\n";
    meta << "  \"camera\": [" << camera.position.x << ", "
         << camera.position.y << ", " << camera.position.z << "],\n";
    meta << "  \"camera_yaw_deg\": " << camera.yawDegrees << ",\n";
    meta << "  \"camera_pitch_deg\": " << camera.pitchDegrees << ",\n";
    meta << "  \"fov_y_deg\": " << camera.fovYDegrees << ",\n";
    meta << "  \"cloud_time_s\": " << timeSeconds << ",\n";
    meta << "  \"wind_disabled\": " << (windDisabled ? 1 : 0) << ",\n";
    meta << "  \"noise_seed\": " << noiseSeed << ",\n";
    meta << "  \"weather_seed\": " << weatherSeed << ",\n";
    meta << "  \"density_model\": \""
         << clouds::DensityModelName(densityModel) << "\",\n";
    meta << "  \"reference_stage\": \""
         << clouds::ReferenceStageName(stage) << "\",\n";
    meta << "  \"reference_noise_mix\": \""
         << clouds::ReferenceNoiseMixName(reference.noiseMix) << "\",\n";
    meta << "  \"reference_field_mode\": \""
         << clouds::ReferenceFieldModeName(reference.fieldMode) << "\",\n";
    meta << "  \"macro_seed\": " << reference.macroSeed << ",\n";
    meta << "  \"macro_coverage_preserve\": "
         << (reference.macroCoveragePreserve ? 1 : 0) << ",\n";
    meta << "  \"macro_seed_hash\": "
         << (reference.macroSeedHash ? 1 : 0) << ",\n";
    meta << "  \"macro_detail_compensate\": "
         << (reference.macroDetailCompensate ? 1 : 0) << ",\n";
    meta << "  \"macro_weather_quantile\": "
         << (reference.macroWeatherQuantile ? 1 : 0) << ",\n";
    meta << "  \"macro_weather_local\": "
         << (reference.macroWeatherLocal ? 1 : 0) << ",\n";
    meta << "  \"macro_local_occupancy\": "
         << (reference.macroLocalOccupancy ? 1 : 0) << ",\n";
    meta << "  \"storm_local_occupancy\": "
         << (reference.stormLocalOccupancy ? 1 : 0) << ",\n";
    meta << "  \"macro_coverage_calibrate\": "
         << (reference.macroCoverageCalibrate ? 1 : 0) << ",\n";
    meta << "  \"coverage\": " << params.coverage << ",\n";
    meta << "  \"density\": " << params.density << ",\n";
    meta << "  \"extinction\": " << params.extinction << ",\n";
    meta << "  \"cloud_steps\": " << params.stepCount << ",\n";
    meta << "  \"cloud_divisor\": " << cloudDivisor << ",\n";
    meta << "  \"cloud_output\": [" << width << ", " << height << "],\n";
    meta << "  \"od_test_pattern\": "
         << (reference.odTestPattern ? "true" : "false") << ",\n";
    meta << "  \"finite\": " << (finite ? "true" : "false") << ",\n";
    meta << "  \"mean_mapped_od\": " << (sumMapped / pixels) << ",\n";
    meta << "  \"mean_od\": " << (sumOd / pixels) << ",\n";
    meta << "  \"min_mapped_od\": " << minMapped << ",\n";
    meta << "  \"max_mapped_od\": " << maxMapped << ",\n";
    meta << "  \"max_od\": " << maxOd << "\n";
    meta << "}\n";
    return meta.good();
}

// V-SF-GPU-01: synchronous RGBA32F readback of the float cloud target for
// raw cloud-only OD/T diagnostics. The source target is R16G16B16A16_FLOAT;
// this is not an 8-bit screenshot and it bypasses atmosphere and the beauty
// transmittance early-out.
bool WriteRawCloud(
    const CloudReadback& readback,
    const std::filesystem::path& directory,
    uint32_t mode,
    const clouds::Camera& camera,
    const clouds::CloudParams& params,
    uint32_t stepCount,
    const char* sceneName)
{
    const size_t pixels =
        static_cast<size_t>(readback.width) * readback.height;
    if (readback.width == 0 || readback.height == 0 ||
        readback.rgba.size() != pixels * 4u)
        return false;

    const std::string stem(sceneName);
    const std::string binName = stem + "_raw_cloud.bin";
    std::ofstream bin(directory / binName,
                      std::ios::binary | std::ios::trunc);
    if (!bin)
        return false;

    const uint32_t magic = 0x444C4356u; // "VCLD" little-endian
    const uint32_t version = 1u;
    const uint32_t width = readback.width;
    const uint32_t height = readback.height;
    const uint32_t channels = 4u;
    const uint32_t modeValue = mode;
    const uint32_t reserved = 0u;
    bin.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    bin.write(reinterpret_cast<const char*>(&version), sizeof(version));
    bin.write(reinterpret_cast<const char*>(&width), sizeof(width));
    bin.write(reinterpret_cast<const char*>(&height), sizeof(height));
    bin.write(reinterpret_cast<const char*>(&channels), sizeof(channels));
    bin.write(reinterpret_cast<const char*>(&modeValue), sizeof(modeValue));
    bin.write(reinterpret_cast<const char*>(&reserved), sizeof(reserved));
    bin.write(reinterpret_cast<const char*>(readback.rgba.data()),
              static_cast<std::streamsize>(readback.rgba.size() *
                                           sizeof(float)));
    if (!bin.good())
        return false;

    std::ofstream meta(directory / (stem + "_raw_cloud.json"),
                       std::ios::trunc);
    if (!meta)
        return false;
    meta << std::setprecision(9);
    meta << "{\n";
    meta << "  \"format\": \"OGSR_V_SF_GPU_01_raw_cloud_v1\",\n";
    meta << "  \"source\": \"R16G16B16A16_FLOAT cloud target read back as RGBA32F\",\n";
    meta << "  \"cloud_only\": true,\n";
    meta << "  \"atmosphere_applied\": false,\n";
    meta << "  \"beauty_early_out\": false,\n";
    meta << "  \"mode\": " << mode << ",\n";
    meta << "  \"mode_name\": \""
         << clouds::DebugModeName(
                static_cast<clouds::CloudDebugMode>(mode)) << "\",\n";
    meta << "  \"width\": " << readback.width << ",\n";
    meta << "  \"height\": " << readback.height << ",\n";
    meta << "  \"channels\": 4,\n";
    meta << "  \"component_type\": \"float32_le\",\n";
    if (mode == 21u)
        meta << "  \"channel_order\": [\"mapped_od\", \"cloud_transmittance\", \"raw_od\", \"reserved\"],\n";
    else if (mode == 22u)
        meta << "  \"channel_order\": [\"cloud_transmittance\", \"reserved\", \"reserved\", \"reserved\"],\n";
    else if (mode == 23u)
        meta << "  \"channel_order\": [\"nominal_front_mask\", \"cloud_transmittance\", \"raw_od\", \"density_occupancy\"],\n";
    else
        meta << "  \"channel_order\": [\"r\", \"g\", \"b\", \"a\"],\n";
    meta << "  \"raw_bin\": \"" << binName << "\",\n";
    meta << "  \"scene\": \"" << sceneName << "\",\n";
    meta << "  \"camera_position\": [" << camera.position.x << ", "
         << camera.position.y << ", " << camera.position.z << "],\n";
    meta << "  \"camera_yaw_deg\": " << camera.yawDegrees << ",\n";
    meta << "  \"camera_pitch_deg\": " << camera.pitchDegrees << ",\n";
    meta << "  \"camera_fov_y_deg\": " << camera.fovYDegrees << ",\n";
    meta << "  \"storm_front\": " << (params.stormFront ? "true" : "false")
         << ",\n";
    meta << "  \"storm_front_debug\": " << params.stormFrontDebug << ",\n";
    meta << "  \"layer_bottom\": " << params.layerBottom << ",\n";
    meta << "  \"layer_top\": " << params.layerTop << ",\n";
    meta << "  \"weather_height_variation\": "
         << params.weatherHeightVariation << ",\n";
    meta << "  \"front_offset\": " << params.weatherFrontOffset << ",\n";
    meta << "  \"extinction\": " << params.extinction << ",\n";
    meta << "  \"density\": " << params.density << ",\n";
    meta << "  \"cloud_steps\": " << stepCount << ",\n";
    meta << "  \"note\": \"Raw OD/T integrate the complete ray without the "
            "T<0.005 beauty early-out. The source cloud target is half-float.\"\n";
    meta << "}\n";
    return meta.good();
}

// V2.3h-A02c: reference density volume. Four float32 channels per voxel:
// raw shape, shaped transfer, height envelope, final density. Extinction is
// density * LayerShape.w (constant coefficient, recorded in the JSON).
bool WriteDensityVolumeFloat32(
    const std::vector<float>& records,
    const std::filesystem::path& directory,
    uint32_t gridX, uint32_t gridY, uint32_t gridZ,
    const DirectX::XMFLOAT3& origin, float cellXZ, float cellY,
    const clouds::CloudParams& params,
    const clouds::ReferenceControls& reference,
    clouds::CloudDensityModel densityModel,
    clouds::ReferenceStage stage,
    float timeSeconds, uint32_t noiseSeed, uint32_t weatherSeed,
    bool windDisabled,
    const char* sceneName, const char* variantName)
{
    const size_t voxels =
        static_cast<size_t>(gridX) * gridY * gridZ;
    if (records.size() != voxels * 4u)
        return false;

    bool finite = true;
    double sumDensity = 0.0;
    float maxDensity = 0.0f;
    size_t occupied = 0;
    for (size_t i = 0; i < voxels; ++i)
    {
        const float density = records[i * 4u + 3u];
        if (!std::isfinite(density) || !std::isfinite(records[i * 4u]))
            finite = false;
        sumDensity += density;
        maxDensity = std::max(maxDensity, density);
        if (density > 1e-4f)
            ++occupied;
    }

    std::ofstream bin(directory / "density_volume_float32.bin",
                      std::ios::binary | std::ios::trunc);
    if (!bin)
        return false;
    bin.write(reinterpret_cast<const char*>(records.data()),
              static_cast<std::streamsize>(records.size() *
                                           sizeof(float)));
    if (!bin.good())
        return false;

    std::ofstream meta(directory / "density_volume_float32.json",
                       std::ios::trunc);
    if (!meta)
        return false;
    meta << std::setprecision(9);
    meta << "{\n";
    meta << "  \"format\": \"OGSR_V2_3h_A02c_density_volume_float32\",\n";
    meta << "  \"timestamp_utc\": \"" << TimestampUtc() << "\",\n";
    meta << "  \"executable_sha256\": \""
         << FileSha256(ExecutablePath()) << "\",\n";
#ifdef _DEBUG
    meta << "  \"build\": \"Debug\",\n";
#else
    meta << "  \"build\": \"Release\",\n";
#endif
    meta << "  \"scene\": \"" << sceneName << "\",\n";
    meta << "  \"variant\": \"" << variantName << "\",\n";
    meta << "  \"density_test_pattern\": " << reference.densityTestPattern
         << ",\n";
    meta << "  \"shader_permutation\": \""
         << clouds::ReferencePermutationName(reference) << "\",\n";
    meta << "  \"grid\": [" << gridX << ", " << gridY << ", " << gridZ
         << "],\n";
    meta << "  \"world_origin\": [" << origin.x << ", " << origin.y << ", "
         << origin.z << "],\n";
    meta << "  \"cell_size_xz_m\": " << cellXZ << ",\n";
    meta << "  \"cell_size_y_m\": " << cellY << ",\n";
    meta << "  \"cell_centre\": \"origin + ((x+0.5)*cellXZ, "
            "(y+0.5)*cellY, (z+0.5)*cellXZ)\",\n";
    meta << "  \"index_order\": \"x fastest, then y, then z\",\n";
    meta << "  \"channels\": 4,\n";
    meta << "  \"record_stride_bytes\": 16,\n";
    meta << "  \"component_type\": \"float32_le\",\n";
    meta << "  \"channel_order\": [\"raw_shape\", \"shaped\", "
            "\"envelope\", \"density\"],\n";
    meta << "  \"channel_definitions\": {\n";
    meta << "    \"raw_shape\": \"base field after the A01/A02 field mode, "
            "before the smoothstep transfer\",\n";
    meta << "    \"shaped\": \"smoothstep transfer plus the single coverage "
            "remap; the exact multiplier of envelope in density\",\n";
    meta << "    \"envelope\": \"height profile (0 outside the local column "
            "bounds); the storm column/gap mask is separate\",\n";
    meta << "    \"density\": \"saturate(shaped * envelope * gap * density_"
            "multiplier), exact renderer value\"\n";
    meta << "  },\n";
    meta << "  \"gap_derivation\": \"density / (shaped * envelope * "
            "density_multiplier) where the denominator is non-zero; stage < 4 "
            "has no erosion term\",\n";
    meta << "  \"density_definition\": \"saturate(shaped * envelope * "
            "gap * density_multiplier), exact renderer value\",\n";
    meta << "  \"extinction_definition\": \"sigma = density * "
            "extinction_coefficient\",\n";
    meta << "  \"extinction_coefficient\": " << params.extinction << ",\n";
    meta << "  \"source\": \"CloudDensityVolume.hlsl (reference density "
            "function, no composite)\",\n";
    meta << "  \"density_model\": \""
         << clouds::DensityModelName(densityModel) << "\",\n";
    meta << "  \"reference_stage\": \""
         << clouds::ReferenceStageName(stage) << "\",\n";
    meta << "  \"reference_noise_mix\": \""
         << clouds::ReferenceNoiseMixName(reference.noiseMix) << "\",\n";
    meta << "  \"reference_field_mode\": \""
         << clouds::ReferenceFieldModeName(reference.fieldMode) << "\",\n";
    meta << "  \"macro_scale_m\": " << reference.macroScale << ",\n";
    meta << "  \"macro_secondary_scale_m\": "
         << reference.macroSecondaryScale << ",\n";
    meta << "  \"macro_threshold\": " << reference.macroThreshold << ",\n";
    meta << "  \"macro_softness\": " << reference.macroSoftness << ",\n";
    meta << "  \"macro_detail_weight\": "
         << reference.macroDetailWeight << ",\n";
    meta << "  \"macro_secondary_weight\": "
         << reference.macroSecondaryWeight << ",\n";
    meta << "  \"macro_edge_jitter\": "
         << reference.macroEdgeJitter << ",\n";
    meta << "  \"macro_height_weight\": "
         << reference.macroHeightWeight << ",\n";
    meta << "  \"macro_seed\": " << reference.macroSeed << ",\n";
    meta << "  \"macro_coverage_preserve\": "
         << (reference.macroCoveragePreserve ? 1 : 0) << ",\n";
    meta << "  \"macro_seed_hash\": "
         << (reference.macroSeedHash ? 1 : 0) << ",\n";
    meta << "  \"macro_detail_compensate\": "
         << (reference.macroDetailCompensate ? 1 : 0) << ",\n";
    meta << "  \"macro_weather_quantile\": "
         << (reference.macroWeatherQuantile ? 1 : 0) << ",\n";
    meta << "  \"macro_weather_local\": "
         << (reference.macroWeatherLocal ? 1 : 0) << ",\n";
    meta << "  \"macro_local_occupancy\": "
         << (reference.macroLocalOccupancy ? 1 : 0) << ",\n";
    meta << "  \"storm_local_occupancy\": "
         << (reference.stormLocalOccupancy ? 1 : 0) << ",\n";
    meta << "  \"macro_coverage_calibrate\": "
         << (reference.macroCoverageCalibrate ? 1 : 0) << ",\n";
    meta << "  \"coverage\": " << params.coverage << ",\n";
    meta << "  \"density_parameter\": " << params.density << ",\n";
    meta << "  \"density_scale\": " << reference.densityScale << ",\n";
    meta << "  \"storm_volume\": " << (reference.stormVolume ? 1 : 0)
         << ",\n";
    meta << "  \"storm_column_envelope\": "
         << (reference.stormColumnEnvelope ? 1 : 0) << ",\n";
    meta << "  \"storm_macro_gap_replace\": "
         << (reference.stormMacroGapReplace ? 1 : 0) << ",\n";
    meta << "  \"storm_height_variation_m\": "
         << reference.stormHeightVariation << ",\n";
    meta << "  \"storm_bottom_variation_m\": "
         << reference.stormBottomVariation << ",\n";
    meta << "  \"layer_bottom_m\": " << params.layerBottom << ",\n";
    meta << "  \"layer_top_m\": " << params.layerTop << ",\n";
    meta << "  \"cloud_time_s\": " << timeSeconds << ",\n";
    meta << "  \"wind_disabled\": " << (windDisabled ? 1 : 0) << ",\n";
    meta << "  \"noise_seed\": " << noiseSeed << ",\n";
    meta << "  \"weather_seed\": " << weatherSeed << ",\n";
    meta << "  \"finite\": " << (finite ? "true" : "false") << ",\n";
    meta << "  \"occupied_voxel_fraction_1e4\": "
         << static_cast<double>(occupied) / voxels << ",\n";
    meta << "  \"mean_density\": " << (sumDensity / voxels) << ",\n";
    meta << "  \"max_density\": " << maxDensity << "\n";
    meta << "}\n";
    return meta.good();
}

// V2.3h-A03a: macro-stage Float32 probe. Four channels per voxel:
// macroRaw (two-octave field before transfer), macroMass (after the threshold
// smoothstep and edge jitter), detailTransfer (the periodic R transfer), and
// macroMassCovered (the mass after the single coverage remap).
bool WriteMacroProbeFloat32(
    const std::vector<float>& records,
    const std::filesystem::path& directory,
    uint32_t gridX, uint32_t gridY, uint32_t gridZ,
    const DirectX::XMFLOAT3& origin, float cellXZ, float cellY,
    const clouds::CloudParams& params,
    const clouds::ReferenceControls& reference,
    uint32_t noiseSeed, bool localOccupancy)
{
    const size_t voxels =
        static_cast<size_t>(gridX) * gridY * gridZ;
    if (records.size() != voxels * 4u)
        return false;

    bool finite = true;
    double sumRaw = 0.0;
    double sumMass = 0.0;
    double sumDetail = 0.0;
    double sumCovered = 0.0;
    float maxRaw = 0.0f;
    float maxMass = 0.0f;
    for (size_t i = 0; i < voxels; ++i)
    {
        for (uint32_t c = 0; c < 4u; ++c)
        {
            if (!std::isfinite(records[i * 4u + c]))
                finite = false;
        }
        sumRaw += records[i * 4u];
        sumMass += records[i * 4u + 1u];
        sumDetail += records[i * 4u + 2u];
        sumCovered += records[i * 4u + 3u];
        maxRaw = std::max(maxRaw, records[i * 4u]);
        maxMass = std::max(maxMass, records[i * 4u + 1u]);
    }

    const char* baseName = localOccupancy
        ? "occupancy_probe_float32" : "macro_probe_float32";
    std::ofstream bin(directory / (std::string(baseName) + ".bin"),
                      std::ios::binary | std::ios::trunc);
    if (!bin)
        return false;
    bin.write(reinterpret_cast<const char*>(records.data()),
              static_cast<std::streamsize>(records.size() *
                                           sizeof(float)));
    if (!bin.good())
        return false;

    std::ofstream meta(directory / (std::string(baseName) + ".json"),
                       std::ios::trunc);
    if (!meta)
        return false;
    meta << std::setprecision(9);
    meta << "{\n";
    meta << "  \"format\": \""
         << (localOccupancy
                 ? "OGSR_V2_3h_A03b_occupancy_probe_float32"
                 : "OGSR_V2_3h_A03a_macro_probe_float32")
         << "\",\n";
    meta << "  \"timestamp_utc\": \"" << TimestampUtc() << "\",\n";
    meta << "  \"executable_sha256\": \""
         << FileSha256(ExecutablePath()) << "\",\n";
    meta << "  \"shader_permutation\": \""
         << clouds::ReferencePermutationName(reference) << "\",\n";
    meta << "  \"density_test_pattern\": " << reference.densityTestPattern
         << ",\n";
    meta << "  \"grid\": [" << gridX << ", " << gridY << ", " << gridZ
         << "],\n";
    meta << "  \"world_origin\": [" << origin.x << ", " << origin.y << ", "
         << origin.z << "],\n";
    meta << "  \"cell_size_xz_m\": " << cellXZ << ",\n";
    meta << "  \"cell_size_y_m\": " << cellY << ",\n";
    meta << "  \"cell_centre\": \"origin + ((x+0.5)*cellXZ, "
            "(y+0.5)*cellY, (z+0.5)*cellXZ)\",\n";
    meta << "  \"index_order\": \"x fastest, then y, then z\",\n";
    meta << "  \"channels\": 4,\n";
    meta << "  \"record_stride_bytes\": 16,\n";
    meta << "  \"component_type\": \"float32_le\",\n";
    if (localOccupancy)
    {
        meta << "  \"channel_order\": [\"macro_anomaly\", \"local_raw\", "
                "\"occupancy\", \"detail_transfer\"],\n";
        meta << "  \"channel_definitions\": {\n";
        const float weatherCoverage = std::pow(
            std::clamp(params.coverage, 0.02f, 0.98f), 1.2f);
        const float occupancyCut = 0.160f * static_cast<float>(
            clouds::NormalQuantile(1.0 - weatherCoverage));
        meta << "    \"macro_anomaly\": \"valueNoise(p/6000) - "
                "valueNoise(p/12000); low-frequency seed offset removed\",\n";
        meta << "    \"local_raw\": \"0.65*valueNoise(p/2400) + "
                "0.35*valueNoise(p/1000) - LocalMean4(+/-7200 m XZ), "
                "non-periodic\",\n";
        meta << "    \"occupancy\": \"smoothstep(cut-0.05, cut+0.05, "
                "local_raw + 0.12*macro_anomaly)\",\n";
        meta << "    \"detail_transfer\": \"smoothstep(shape_low, shape_high, "
                "periodic R)\"\n";
        meta << "  },\n";
        meta << "  \"occupancy_cut\": " << occupancyCut << ",\n";
        meta << "  \"occupancy_half_width\": 0.05,\n";
        meta << "  \"weather_gamma\": 1.2,\n";
        meta << "  \"macro_anomaly_weight\": 0.12,\n";
        meta << "  \"local_scales_m\": [2400, 1000],\n";
        meta << "  \"local_weights\": [0.65, 0.35],\n";
        meta << "  \"local_mean_taps_m\": 7200,\n";
    }
    else
    {
        meta << "  \"channel_order\": [\"macro_raw\", \"macro_mass\", "
                "\"detail_transfer\", \"macro_mass_covered\"],\n";
        meta << "  \"channel_definitions\": {\n";
        meta << "    \"macro_raw\": \"lerp(valueNoise(p/scale1), "
                "valueNoise(p/scale2), secondary_weight), before transfer\",\n";
        meta << "    \"macro_mass\": \"smoothstep(threshold, threshold+softness, "
                "macro_raw + edge_jitter*(detail-0.5))\",\n";
        meta << "    \"detail_transfer\": \"smoothstep(shape_low, shape_high, "
                "periodic R)\",\n";
        meta << "    \"macro_mass_covered\": \"coverage remap of macro_mass; the "
                "value used before the detail multiplier when "
                "macro_coverage_preserve=1\"\n";
        meta << "  },\n";
    }
    meta << "  \"reference_field_mode\": \""
         << clouds::ReferenceFieldModeName(reference.fieldMode) << "\",\n";
    meta << "  \"reference_stage\": \""
         << clouds::ReferenceStageName(clouds::ReferenceStage::Coverage)
         << "\",\n";
    meta << "  \"macro_scale_m\": " << reference.macroScale << ",\n";
    meta << "  \"macro_secondary_scale_m\": "
         << reference.macroSecondaryScale << ",\n";
    meta << "  \"macro_threshold\": " << reference.macroThreshold << ",\n";
    meta << "  \"macro_softness\": " << reference.macroSoftness << ",\n";
    meta << "  \"macro_detail_weight\": "
         << reference.macroDetailWeight << ",\n";
    meta << "  \"macro_secondary_weight\": "
         << reference.macroSecondaryWeight << ",\n";
    meta << "  \"macro_edge_jitter\": "
         << reference.macroEdgeJitter << ",\n";
    meta << "  \"macro_height_weight\": "
         << reference.macroHeightWeight << ",\n";
    meta << "  \"macro_seed\": " << reference.macroSeed << ",\n";
    meta << "  \"macro_coverage_preserve\": "
         << (reference.macroCoveragePreserve ? 1 : 0) << ",\n";
    meta << "  \"macro_seed_hash\": "
         << (reference.macroSeedHash ? 1 : 0) << ",\n";
    meta << "  \"macro_detail_compensate\": "
         << (reference.macroDetailCompensate ? 1 : 0) << ",\n";
    meta << "  \"macro_weather_quantile\": "
         << (reference.macroWeatherQuantile ? 1 : 0) << ",\n";
    meta << "  \"macro_weather_local\": "
         << (reference.macroWeatherLocal ? 1 : 0) << ",\n";
    meta << "  \"macro_local_occupancy\": "
         << (reference.macroLocalOccupancy ? 1 : 0) << ",\n";
    meta << "  \"storm_local_occupancy\": "
         << (reference.stormLocalOccupancy ? 1 : 0) << ",\n";
    meta << "  \"macro_coverage_calibrate\": "
         << (reference.macroCoverageCalibrate ? 1 : 0) << ",\n";
    meta << "  \"coverage\": " << params.coverage << ",\n";
    meta << "  \"noise_seed\": " << noiseSeed << ",\n";
    meta << "  \"finite\": " << (finite ? "true" : "false") << ",\n";
    meta << "  \"mean_macro_raw\": " << (sumRaw / voxels) << ",\n";
    meta << "  \"mean_macro_mass\": " << (sumMass / voxels) << ",\n";
    meta << "  \"mean_detail_transfer\": " << (sumDetail / voxels) << ",\n";
    meta << "  \"mean_macro_mass_covered\": " << (sumCovered / voxels)
         << ",\n";
    meta << "  \"max_macro_raw\": " << maxRaw << ",\n";
    meta << "  \"max_macro_mass\": " << maxMass << "\n";
    meta << "}\n";
    return meta.good();
}

bool WriteStormBoundaryFloat32(
    const std::vector<float>& records,
    const std::filesystem::path& directory,
    uint32_t gridX, uint32_t gridZ,
    const DirectX::XMFLOAT3& origin, float cellSize,
    float sampleY0, float sampleY1,
    const clouds::CloudParams& params,
    const clouds::ReferenceControls& reference,
    const char* sceneName)
{
    const size_t columns = static_cast<size_t>(gridX) * gridZ;
    if (records.size() != columns * 4u)
        return false;

    bool finite = true;
    for (float value : records)
    {
        if (!std::isfinite(value))
            finite = false;
    }

    std::ofstream bin(directory / "storm_boundary_float32.bin",
                      std::ios::binary | std::ios::trunc);
    if (!bin)
        return false;
    bin.write(reinterpret_cast<const char*>(records.data()),
              static_cast<std::streamsize>(records.size() *
                                           sizeof(float)));
    if (!bin.good())
        return false;

    std::ofstream meta(directory / "storm_boundary_float32.json",
                       std::ios::trunc);
    if (!meta)
        return false;
    meta << std::setprecision(9);
    meta << "{\n";
    meta << "  \"format\": \"OGSR_V2_3h_A02c_storm_boundary_float32\",\n";
    meta << "  \"timestamp_utc\": \"" << TimestampUtc() << "\",\n";
    meta << "  \"executable_sha256\": \""
         << FileSha256(ExecutablePath()) << "\",\n";
    meta << "  \"scene\": \"" << sceneName << "\",\n";
    meta << "  \"grid\": [" << gridX << ", 1, " << gridZ << "],\n";
    meta << "  \"world_origin\": [" << origin.x << ", " << origin.y << ", "
         << origin.z << "],\n";
    meta << "  \"cell_size_m\": " << cellSize << ",\n";
    meta << "  \"index_order\": \"x fastest, then z\",\n";
    meta << "  \"channels\": 4,\n";
    meta << "  \"channel_order\": [\"bottom_at_y0\", \"top_at_y0\", "
            "\"bottom_at_y1\", \"top_at_y1\"],\n";
    meta << "  \"sample_y0_m\": " << sampleY0 << ",\n";
    meta << "  \"sample_y1_m\": " << sampleY1 << ",\n";
    meta << "  \"storm_volume\": " << (reference.stormVolume ? 1 : 0)
         << ",\n";
    meta << "  \"storm_column_envelope\": "
         << (reference.stormColumnEnvelope ? 1 : 0) << ",\n";
    meta << "  \"storm_macro_gap_replace\": "
         << (reference.stormMacroGapReplace ? 1 : 0) << ",\n";
    meta << "  \"storm_height_variation_m\": "
         << reference.stormHeightVariation << ",\n";
    meta << "  \"storm_bottom_variation_m\": "
         << reference.stormBottomVariation << ",\n";
    meta << "  \"layer_bottom_m\": " << params.layerBottom << ",\n";
    meta << "  \"layer_top_m\": " << params.layerTop << ",\n";
    meta << "  \"finite\": " << (finite ? "true" : "false") << "\n";
    meta << "}\n";
    return meta.good();
}

// A02c: fine vertical columns. Four consecutive Y samples are packed into one
// float4 record; density only. This is a true 3D sampling of the density
// function (not a screen projection) at a fixed metric step.
bool WriteDensityColumnsFloat32(
    const std::vector<float>& records,
    const std::filesystem::path& directory,
    uint32_t columnsX, uint32_t packets, uint32_t columnsZ,
    const DirectX::XMFLOAT3& origin, float cellXZ,
    float yStart, float yStep,
    const clouds::CloudParams& params,
    const clouds::ReferenceControls& reference,
    clouds::CloudDensityModel densityModel,
    clouds::ReferenceStage stage,
    float timeSeconds, uint32_t noiseSeed, uint32_t weatherSeed,
    bool windDisabled, bool crosscheck,
    const char* sceneName, const char* variantName)
{
    const size_t count =
        static_cast<size_t>(columnsX) * packets * columnsZ;
    if (records.size() != count * 4u)
        return false;

    bool finite = true;
    for (float value : records)
    {
        if (!std::isfinite(value))
            finite = false;
    }

    std::ofstream bin(directory / "density_columns_float32.bin",
                      std::ios::binary | std::ios::trunc);
    if (!bin)
        return false;
    bin.write(reinterpret_cast<const char*>(records.data()),
              static_cast<std::streamsize>(records.size() *
                                           sizeof(float)));
    if (!bin.good())
        return false;

    std::ofstream meta(directory / "density_columns_float32.json",
                       std::ios::trunc);
    if (!meta)
        return false;
    meta << std::setprecision(9);
    meta << "{\n";
    meta << "  \"format\": \"OGSR_V2_3h_A02c_density_columns_float32\",\n";
    meta << "  \"timestamp_utc\": \"" << TimestampUtc() << "\",\n";
    meta << "  \"executable_sha256\": \""
         << FileSha256(ExecutablePath()) << "\",\n";
    meta << "  \"scene\": \"" << sceneName << "\",\n";
    meta << "  \"variant\": \"" << variantName << "\",\n";
    meta << "  \"columns_crosscheck\": " << (crosscheck ? 1 : 0) << ",\n";
    meta << "  \"density_test_pattern\": " << reference.densityTestPattern
         << ",\n";
    meta << "  \"shader_permutation\": \""
         << clouds::ReferencePermutationName(reference) << "\",\n";
    meta << "  \"columns\": [" << columnsX << ", " << columnsZ << "],\n";
    meta << "  \"packets\": " << packets << ",\n";
    meta << "  \"samples_per_column\": " << packets * 4u << ",\n";
    meta << "  \"world_origin\": [" << origin.x << ", " << origin.y << ", "
         << origin.z << "],\n";
    meta << "  \"cell_size_xz_m\": " << cellXZ << ",\n";
    meta << "  \"column_y_start_m\": " << yStart << ",\n";
    meta << "  \"column_y_step_m\": " << yStep << ",\n";
    meta << "  \"index_order\": \"x fastest, then packet, then z\",\n";
    meta << "  \"record_channel_order\": [\"density_y0\", \"density_y1\", "
            "\"density_y2\", \"density_y3\"],\n";
    meta << "  \"sample_y\": \"yStart + (packet * 4 + channel + 0.5) * "
            "yStep\",\n";
    meta << "  \"channels\": 4,\n";
    meta << "  \"component_type\": \"float32_le\",\n";
    meta << "  \"density_definition\": \"same value as the renderer: "
            "saturate(shaped * envelope * gap * density_multiplier)\",\n";
    meta << "  \"extinction_definition\": \"sigma = density * "
            "extinction_coefficient\",\n";
    meta << "  \"extinction_coefficient\": " << params.extinction << ",\n";
    meta << "  \"open_boundary\": \"samples with density below the chosen "
            "threshold are empty; no artificial floor/ceiling\",\n";
    meta << "  \"density_model\": \""
         << clouds::DensityModelName(densityModel) << "\",\n";
    meta << "  \"reference_stage\": \""
         << clouds::ReferenceStageName(stage) << "\",\n";
    meta << "  \"reference_noise_mix\": \""
         << clouds::ReferenceNoiseMixName(reference.noiseMix) << "\",\n";
    meta << "  \"reference_field_mode\": \""
         << clouds::ReferenceFieldModeName(reference.fieldMode) << "\",\n";
    meta << "  \"macro_scale_m\": " << reference.macroScale << ",\n";
    meta << "  \"macro_secondary_scale_m\": "
         << reference.macroSecondaryScale << ",\n";
    meta << "  \"macro_threshold\": " << reference.macroThreshold << ",\n";
    meta << "  \"macro_softness\": " << reference.macroSoftness << ",\n";
    meta << "  \"macro_detail_weight\": "
         << reference.macroDetailWeight << ",\n";
    meta << "  \"macro_secondary_weight\": "
         << reference.macroSecondaryWeight << ",\n";
    meta << "  \"macro_edge_jitter\": "
         << reference.macroEdgeJitter << ",\n";
    meta << "  \"macro_height_weight\": "
         << reference.macroHeightWeight << ",\n";
    meta << "  \"macro_seed\": " << reference.macroSeed << ",\n";
    meta << "  \"macro_coverage_preserve\": "
         << (reference.macroCoveragePreserve ? 1 : 0) << ",\n";
    meta << "  \"macro_seed_hash\": "
         << (reference.macroSeedHash ? 1 : 0) << ",\n";
    meta << "  \"macro_detail_compensate\": "
         << (reference.macroDetailCompensate ? 1 : 0) << ",\n";
    meta << "  \"macro_weather_quantile\": "
         << (reference.macroWeatherQuantile ? 1 : 0) << ",\n";
    meta << "  \"macro_weather_local\": "
         << (reference.macroWeatherLocal ? 1 : 0) << ",\n";
    meta << "  \"macro_local_occupancy\": "
         << (reference.macroLocalOccupancy ? 1 : 0) << ",\n";
    meta << "  \"storm_local_occupancy\": "
         << (reference.stormLocalOccupancy ? 1 : 0) << ",\n";
    meta << "  \"macro_coverage_calibrate\": "
         << (reference.macroCoverageCalibrate ? 1 : 0) << ",\n";
    meta << "  \"coverage\": " << params.coverage << ",\n";
    meta << "  \"density_parameter\": " << params.density << ",\n";
    meta << "  \"density_scale\": " << reference.densityScale << ",\n";
    meta << "  \"storm_volume\": " << (reference.stormVolume ? 1 : 0)
         << ",\n";
    meta << "  \"storm_column_envelope\": "
         << (reference.stormColumnEnvelope ? 1 : 0) << ",\n";
    meta << "  \"storm_macro_gap_replace\": "
         << (reference.stormMacroGapReplace ? 1 : 0) << ",\n";
    meta << "  \"storm_height_variation_m\": "
         << reference.stormHeightVariation << ",\n";
    meta << "  \"storm_bottom_variation_m\": "
         << reference.stormBottomVariation << ",\n";
    meta << "  \"layer_bottom_m\": " << params.layerBottom << ",\n";
    meta << "  \"layer_top_m\": " << params.layerTop << ",\n";
    meta << "  \"cloud_time_s\": " << timeSeconds << ",\n";
    meta << "  \"wind_disabled\": " << (windDisabled ? 1 : 0) << ",\n";
    meta << "  \"noise_seed\": " << noiseSeed << ",\n";
    meta << "  \"weather_seed\": " << weatherSeed << ",\n";
    meta << "  \"finite\": " << (finite ? "true" : "false") << "\n";
    meta << "}\n";
    return meta.good();
}

} // namespace

Application::Application(AppOptions options)
    : options_(std::move(options))
{
    channel_ = 2;
    slice_ = static_cast<float>(options_.noise.resolution) * 0.5f;
    sliceAxis_ = options_.sliceAxis;
    view_.resolution = options_.noise.resolution;
    view_.sliceIndex = slice_;
    view_.sliceAxis = sliceAxis_;
    view_.channel = channel_;
    cloudPreset_ = options_.cloudPreset;
    cloudDebug_ = options_.cloudDebug;
    cloudTime_ = options_.initialCloudTime;
    cloudsEnabled_ = options_.cloudGraphics.cloudsEnabled;
    cloudRenderMode_ = options_.cloudGraphics.renderMode;
    cloudQuality_ = options_.cloudGraphics.quality;
    cloudSettingsState_.Initialize(clouds::CloudGraphicsState{
        cloudsEnabled_, cloudQuality_, cloudRenderMode_});
    lastAppliedSettingsGeneration_ = cloudSettingsState_.Generation();
}

Application::~Application()
{
    Shutdown();
    if (frameTimingLog_.is_open())
        frameTimingLog_.close();
}

void Application::ApplyPendingCloudSettings()
{
    if (!cloudSettingsState_.HasPending())
        return;

    const clouds::CloudGraphicsState pending = cloudSettingsState_.Pending();
    if (!ApplyCloudGraphicsState(pending, "frame-boundary-commit"))
    {
        cloudSettingsState_.CancelPending();
        LogWarn("cloud settings pending change cancelled");
        return;
    }
    const uint64_t generation = cloudSettingsState_.Commit();
    lastAppliedSettingsGeneration_ = generation;
    Log("cloud settings commit: generation=%llu pending=%d",
        static_cast<unsigned long long>(generation), 0);
}

bool Application::ApplyCloudGraphicsState(
    const clouds::CloudGraphicsState& state, const char* reason)
{
    if (state.renderMode == clouds::CloudRenderMode::Classic2D)
    {
        LogWarn("cloud settings: classic2d is unsupported in standalone; "
                "change rejected");
        return false;
    }

    const clouds::CloudQualityProfile profile =
        clouds::GetCloudQualityProfile(state.quality);
    const uint32_t oldDivisor = options_.cloudDivisor;
    const bool divisorChanged = oldDivisor != profile.outputDivisor;

    uint64_t resourceGeneration = cloudRayMarcher_.ResourceGeneration();
    if (divisorChanged && cloudRayMarcher_.IsInitialized())
    {
        try
        {
            resourceGeneration =
                cloudRayMarcher_.SetOutputDivisor(profile.outputDivisor);
        }
        catch (const clouds::CloudResourceError& e)
        {
            LogErrorBestEffort(
                "cloud settings GPU apply failed (recoverable): %s; old "
                "resources, settings and generation retained", e.what());
            return false;
        }
        catch (const clouds::CloudGpuFatalError& e)
        {
            (void)e;
            gpuConfigurationFatal_ = true;
            clouds::WitnessEvent(clouds::WitnessCode::FaultInjected);
            TerminalFailClosed(
                "cloud settings GPU apply fatal; GPU completion is UNKNOWN");
        }
        catch (...)
        {
            gpuConfigurationFatal_ = true;
            clouds::WitnessEvent(clouds::WitnessCode::FaultInjected);
            TerminalFailClosed(
                "cloud settings GPU apply unknown fatal; GPU completion is "
                "UNKNOWN");
        }
    }

    // Publish public state only after the GPU resource transaction succeeded.
    options_.cloudGraphics.cloudsEnabled = state.cloudsEnabled;
    options_.cloudGraphics.quality = state.quality;
    options_.cloudGraphics.renderMode = state.renderMode;
    options_.cloud.stepCount = profile.raySteps;
    options_.cloud.shadowStepCount = profile.shadowSteps;
    options_.cloudDivisor = profile.outputDivisor;

    cloudsEnabled_ = state.cloudsEnabled;
    cloudQuality_ = state.quality;
    cloudRenderMode_ = state.renderMode;

    Log("cloud settings applied: reason=%s clouds_enabled=%d "
        "quality=%s render_mode=%s steps=%u shadows=%u divisor=%u "
        "output=%ux%u format=%s resource_generation=%llu",
        reason, cloudsEnabled_ ? 1 : 0,
        clouds::CloudQualityName(cloudQuality_),
        clouds::CloudRenderModeName(cloudRenderMode_),
        options_.cloud.stepCount, options_.cloud.shadowStepCount,
        options_.cloudDivisor, cloudRayMarcher_.OutputWidth(),
        cloudRayMarcher_.OutputHeight(), cloudRayMarcher_.FormatName(),
        static_cast<unsigned long long>(resourceGeneration));
    return true;
}

void Application::RequestCloudQualityStep(int direction)
{
    const clouds::CloudGraphicsState current =
        cloudSettingsState_.Effective();
    int quality = static_cast<int>(current.quality);
    quality = (quality + (direction < 0 ? 3 : 1)) % 4;
    clouds::CloudGraphicsState next = current;
    next.quality = static_cast<clouds::CloudQuality>(quality);
    const clouds::CloudSettingsRequest request =
        cloudSettingsState_.Request(next);
    Log("cloud settings requested: quality=%s result=%s",
        clouds::CloudQualityName(next.quality),
        clouds::CloudGraphicsStateMachine::RequestName(request));
}

void Application::RequestCloudsToggle()
{
    clouds::CloudGraphicsState next = cloudSettingsState_.Effective();
    next.cloudsEnabled = !next.cloudsEnabled;
    const clouds::CloudSettingsRequest request =
        cloudSettingsState_.Request(next);
    Log("cloud settings requested: clouds_enabled=%d result=%s",
        next.cloudsEnabled ? 1 : 0,
        clouds::CloudGraphicsStateMachine::RequestName(request));
}

void Application::LogRuntimeWeatherState(const char* stage) const
{
    Log("runtime weather state: stage=%s timeline_active=%d "
        "timeline_time=%.6f transition_active=%d transition_time=%.6f "
        "cloud_time=%.6f paused=%d weather_seed=%u noise_seed=%u "
        "wind_speed=%.3f clouds_enabled=%d quality=%s "
        "settings_generation=%llu resource_generation=%llu",
        stage, weatherTimelineActive_ ? 1 : 0, weatherTimelineTime_,
        weatherTransitionActive_ ? 1 : 0, weatherTransitionElapsed_,
        cloudTime_, cloudPaused_ ? 1 : 0, options_.weatherSeed,
        options_.noise.seed, options_.cloud.windSpeed,
        cloudsEnabled_ ? 1 : 0, clouds::CloudQualityName(cloudQuality_),
        static_cast<unsigned long long>(lastAppliedSettingsGeneration_),
        static_cast<unsigned long long>(cloudRayMarcher_.ResourceGeneration()));
}

bool Application::Initialize()
{
    clouds::WitnessEvent(clouds::WitnessCode::StartupBegin);
    Log("=== OGSR Volumetric Clouds V2 (standalone D3D12) ===");
#ifdef _DEBUG
    Log("build: Debug");
#else
    Log("build: Release");
#endif
    Log("executable directory: %s",
        WideToUtf8(ExecutableDirectory().wstring()).c_str());
    Log("render mode: %s",
        options_.mode == clouds::RenderMode::Clouds ? "clouds" : "slices");

    if (!window_.Create(L"OGSR Облака V2", options_.windowWidth,
                        options_.windowHeight,
                        [this](UINT message, WPARAM wparam, LPARAM lparam)
                        {
                            OnWindowEvent(message, wparam, lparam);
                        }))
    {
        Fail("CreateWindowExW failed");
    }
    clouds::WitnessEvent(clouds::WitnessCode::StartupWindow);

    renderer_.Initialize(window_.Handle(), options_.renderer);
    clouds::WitnessEvent(clouds::WitnessCode::StartupRenderer);
    noise_.Initialize(renderer_, options_.noise, options_.preferredFormat);
    clouds::WitnessEvent(clouds::WitnessCode::StartupNoise);
    viewer_.Initialize(renderer_);
    clouds::WitnessEvent(clouds::WitnessCode::StartupViewer);
    weatherMap_.Initialize(renderer_, options_.weatherSeed);
    clouds::WitnessEvent(clouds::WitnessCode::StartupWeather);
    cloudRayMarcher_.Initialize(renderer_, renderer_.Width(), renderer_.Height(),
                                options_.cloudDivisor);
    clouds::WitnessEvent(clouds::WitnessCode::StartupCloud);
    cloudCompositor_.Initialize(renderer_);
    clouds::WitnessEvent(clouds::WitnessCode::StartupCompositor);
    cloudProfiler_.Initialize(renderer_);
    clouds::WitnessEvent(clouds::WitnessCode::StartupProfiler);

    if (options_.weatherTimelineExplicit)
        StartWeatherTimeline();
    if (!options_.frameTimingLogPath.empty())
    {
        const std::filesystem::path timingPath(options_.frameTimingLogPath);
        std::error_code timingEc;
        if (!timingPath.parent_path().empty())
            std::filesystem::create_directories(
                timingPath.parent_path(), timingEc);
        frameTimingLog_.open(timingPath, std::ios::trunc);
        if (!frameTimingLog_.good())
            Fail("cannot open frame timing log: " +
                 timingPath.string());
        frameTimingLog_ << "frame,cloud_ms,composite_ms,total_ms,"
                           "cloud_time,timeline_time,timeline_end,kind,source,"
                           "target,outer,source_mix,outer_mix,phase,"
                           "p0_label,p0_block\n";
        frameTimingLog_ << std::setprecision(9);
    }

    options_.noise = noise_.Params();
    view_.resolution = noise_.Resolution();
    slice_ = static_cast<float>(noise_.Resolution()) * 0.5f;
    view_.sliceIndex = slice_;

    Log("adapter: %s", renderer_.AdapterInfo().c_str());
    Log("noise format: %s", noise_.FormatName());
    Log("noise parameters: %s", options_.noise.Describe().c_str());
    Log("cloud output: %ux%u %s, steps=%u coverage=%.2f density=%.2f"
        " (divisor %u)",
        cloudRayMarcher_.OutputWidth(), cloudRayMarcher_.OutputHeight(),
        cloudRayMarcher_.FormatName(), options_.cloud.stepCount,
        options_.cloud.coverage, options_.cloud.density,
        options_.cloudDivisor);
    Log("cloud graphics settings: enabled=%d render_mode=%s quality=%s",
        cloudsEnabled_ ? 1 : 0,
        clouds::CloudRenderModeName(cloudRenderMode_),
        clouds::CloudQualityName(cloudQuality_));
    Log("cloud quality actual: profile=%s steps=%u shadow_steps=%u "
        "scale=%s divisor=%u target=%ux%u format=%s",
        clouds::CloudQualityName(cloudQuality_), options_.cloud.stepCount,
        options_.cloud.shadowStepCount,
        options_.cloudDivisor == 1 ? "full" : "half",
        options_.cloudDivisor, cloudRayMarcher_.OutputWidth(),
        cloudRayMarcher_.OutputHeight(), cloudRayMarcher_.FormatName());
    Log("weather controller source: %s", options_.weatherSource.c_str());
    Log("weather map: %ux%u %s, seed %u, generation %.3f ms",
        weatherMap_.Resolution(), weatherMap_.Resolution(),
        weatherMap_.FormatName(), weatherMap_.Seed(),
        weatherMap_.LastGenerateMs());
    Log("cloud ablation: %s",
        clouds::AblationModeName(options_.cloud.ablationMode));
    Log("weather evolution: NOT IMPLEMENTED (reserved for V2.4)");
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
#if defined(OGSR_CLOUDS_FAULT_INJECTION)
    if (!options_.runtimeSettingsFaultTestDir.empty())
    {
        result = RunRuntimeSettingsFaultTest();
    }
    else
#endif
    if (!options_.runtimeSettingsTestDir.empty())
    {
        result = RunRuntimeSettingsTest();
    }
    else if (options_.p0Matrix || options_.p1Matrix)
    {
        result = RunP0Matrix();
    }
    else if (options_.selfTest || options_.capture ||
        !options_.sequenceDir.empty() ||
        !options_.noiseSliceCaptureDir.empty())
    {
        if (options_.selfTest)
            result = std::max(result, RunSelfTest());
        if (!options_.noiseSliceCaptureDir.empty())
            result = std::max(result, RunNoiseSliceCapture(options_.noiseSliceCaptureDir));
        if (!options_.sequenceDir.empty())
            result = std::max(result, RunCloudSequence(options_.sequenceDir));
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
    if (renderer_.DebugErrorCount() != 0 && result == 0)
    {
        LogError("validation errors detected after shutdown: %llu",
                 static_cast<unsigned long long>(renderer_.DebugErrorCount()));
        result = 1;
    }
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
#if defined(OGSR_CLOUDS_FAULT_INJECTION)
    if (clouds::ShutdownFaultPendingFor(clouds::CloudFaultPoint::ShutdownApplicationWait))
        clouds::MaybeActivateBlockedPipes("application-shutdown");
#endif
    const bool anyGpu = renderer_.Device() != nullptr ||
                        cloudRayMarcher_.IsInitialized();
    if (!initialized_ && !anyGpu)
    {
        window_.Destroy();
        return !renderer_.DeviceRemoved();
    }

    // Pre-retirement boundary: completion must be confirmed before the first
    // consumer release and before any diagnostic can throw. This also covers a
    // partially completed Initialize(), where initialized_ is still false but
    // the renderer/cloud objects already own GPU resources.
    if (renderer_.Device())
    {
        if (renderer_.DeviceRemoved())
        {
            TerminalFailClosed(
                "shutdown: device removed; GPU completion is UNKNOWN; "
                "TERMINAL fail-closed before releasing any GPU-owned resource");
        }
        if (clouds::ConsumeCloudFault(
                clouds::CloudFaultPoint::ShutdownApplicationWait))
        {
            clouds::WitnessEvent(clouds::WitnessCode::FaultInjected);
            clouds::WitnessEvent(clouds::WitnessCode::WaitFailed);
            TerminalFailClosed(
                "shutdown: injected GPU wait failure; completion is UNKNOWN; "
                "TERMINAL fail-closed before releasing any GPU-owned resource");
        }
        if (!renderer_.TryWaitForGpu())
        {
            TerminalFailClosed(
                "shutdown: GPU completion is UNKNOWN; TERMINAL fail-closed "
                "before releasing any GPU-owned resource");
        }
    }

    LogBestEffort("shutdown: GPU idle; releasing resources");

    // Everything below runs with a confirmed idle GPU. Diagnostics are
    // best-effort so a logger failure cannot prevent the release sequence.
    try
    {
        cloudProfiler_.CollectAll();
        cloudProfiler_.LogSummary();
    }
    catch (...)
    {
        LogErrorBestEffort("shutdown: profiler collection failed");
    }

    const auto shutdownComponent = [](const char* name, auto& component)
    {
        LogBestEffort("shutdown: %s", name);
        try
        {
            component.Shutdown();
        }
        catch (...)
        {
            LogErrorBestEffort("shutdown: %s cleanup failed", name);
        }
    };

    shutdownComponent("cloud ray marcher", cloudRayMarcher_);
    shutdownComponent("weather map", weatherMap_);
    shutdownComponent("cloud compositor", cloudCompositor_);
    try
    {
        cloudProfiler_.Shutdown();
    }
    catch (...)
    {
        LogErrorBestEffort("shutdown: profiler cleanup failed");
    }
    shutdownComponent("slice viewer", viewer_);
    shutdownComponent("noise volume", noise_);
    shutdownComponent("renderer", renderer_);
    LogBestEffort("shutdown: window");
    window_.Destroy();

    const bool clean = !renderer_.DeviceRemoved();
    if (renderer_.DeviceRemoved())
    {
        try
        {
            LogErrorBestEffort("shutdown: DEVICE REMOVAL reason %s",
                               HrToString(renderer_.LastDeviceRemovedReason())
                                   .c_str());
        }
        catch (...)
        {
        }
    }
    LogBestEffort(clean ? "shutdown: complete"
                        : "shutdown: complete (GPU not idle)");
    initialized_ = false;
    return clean;
}

int Application::RunP0Matrix()
{
    if (!frameTimingLog_.is_open())
        Fail("--p0-matrix requires an open frame timing log");

    if (options_.cameraExplicit)
        camera_.position = options_.cameraPosition;
    if (options_.cameraYawExplicit)
        camera_.yawDegrees = options_.cameraYawDegrees;
    if (options_.cameraPitchExplicit)
        camera_.pitchDegrees = options_.cameraPitchDegrees;
    if (options_.fovExplicit)
        camera_.fovYDegrees = options_.fovYDegrees;
    if (options_.windDisabled)
        options_.cloud.windSpeed = 0.0f;
    cloudTime_ = options_.initialCloudTime;
    weatherTimelineActive_ = true;
    options_.weatherTimelinePlay = false;

    window_.Show(SW_SHOW);
    UpdateWindow(window_.Handle());
    window_.PumpMessages();

    struct P0Config
    {
        const char* name;
        clouds::WeatherTimelinePlan plan;
        float time;
        bool selfShadow;
        float shadowOdTransmittance = 0.0f;
        bool shadowInnerOnly = false;
    };

    const auto twoState = [](clouds::StaticWeatherMode source,
                             clouds::StaticWeatherMode target)
    {
        clouds::WeatherTimelinePlan plan;
        plan.modes = { source, target };
        plan.segmentDuration = 1.0f;
        return plan;
    };
    const auto interrupt = []()
    {
        clouds::WeatherTimelinePlan plan;
        plan.modes = { clouds::StaticWeatherMode::Overcast,
                       clouds::StaticWeatherMode::RainSF };
        plan.segmentDuration = 1.0f;
        plan.hasInterrupt = true;
        plan.interruptSegment = 0;
        plan.interruptPhase = 0.65f;
        plan.interruptTarget = clouds::StaticWeatherMode::Partly;
        plan.interruptDuration = 1.0f;
        return plan;
    };

    std::vector<P0Config> configs;
    configs.push_back({ "static_overcast",
                        twoState(clouds::StaticWeatherMode::Overcast,
                                 clouds::StaticWeatherMode::Overcast),
                        0.0f, true });
    configs.push_back({ "static_partly",
                        twoState(clouds::StaticWeatherMode::Partly,
                                 clouds::StaticWeatherMode::Partly),
                        0.0f, true });
    configs.push_back({ "static_rain_sf",
                        twoState(clouds::StaticWeatherMode::RainSF,
                                 clouds::StaticWeatherMode::RainSF),
                        0.0f, true });
    configs.push_back({ "two_overcast_rain_p50",
                        twoState(clouds::StaticWeatherMode::Overcast,
                                 clouds::StaticWeatherMode::RainSF),
                        0.5f, true });
    configs.push_back({ "two_rain_partly_p50",
                        twoState(clouds::StaticWeatherMode::RainSF,
                                 clouds::StaticWeatherMode::Partly),
                        0.5f, true });
    configs.push_back({ "two_base718",
                        twoState(clouds::StaticWeatherMode::Overcast,
                                 clouds::StaticWeatherMode::RainSF),
                        0.65f, true });
    configs.push_back({ "three_outer_eps", interrupt(), 0.6501f, true });
    configs.push_back({ "three_outer_025", interrupt(), 0.9f, true });
    configs.push_back({ "three_outer_050", interrupt(), 1.15f, true });
    configs.push_back({ "three_outer_075", interrupt(), 1.4f, true });
    configs.push_back({ "three_outer_100", interrupt(), 1.65f, true });
    configs.push_back({ "static_overcast_shadow_off",
                        twoState(clouds::StaticWeatherMode::Overcast,
                                 clouds::StaticWeatherMode::Overcast),
                        0.0f, false });
    configs.push_back({ "two_overcast_rain_p50_shadow_off",
                        twoState(clouds::StaticWeatherMode::Overcast,
                                 clouds::StaticWeatherMode::RainSF),
                        0.5f, false });
    configs.push_back({ "three_outer_050_shadow_off",
                        interrupt(), 1.15f, false });

    if (options_.p1Matrix)
    {
        configs.push_back({ "od001_three_025", interrupt(), 0.9f, true,
                            0.001f, false });
        configs.push_back({ "od001_three_050", interrupt(), 1.15f, true,
                            0.001f, false });
        configs.push_back({ "od001_three_075", interrupt(), 1.4f, true,
                            0.001f, false });
        configs.push_back({ "od01_three_025", interrupt(), 0.9f, true,
                            0.01f, false });
        configs.push_back({ "od01_three_050", interrupt(), 1.15f, true,
                            0.01f, false });
        configs.push_back({ "od01_three_075", interrupt(), 1.4f, true,
                            0.01f, false });
        configs.push_back({ "od05_three_050", interrupt(), 1.15f, true,
                            0.05f, false });
        configs.push_back({ "inner_three_025", interrupt(), 0.9f, true,
                            0.0f, true });
        configs.push_back({ "inner_three_050", interrupt(), 1.15f, true,
                            0.0f, true });
        configs.push_back({ "inner_three_075", interrupt(), 1.4f, true,
                            0.0f, true });
    }

    const int cycles = 4;
    const int warmupFrames = 3;
    const int measuredFrames = 30;
    int measured = 0;

    const auto runBlock = [&](const P0Config& config, int cycle)
    {
        options_.weatherTimeline = config.plan;
        weatherTimelineTime_ = std::clamp(
            config.time, 0.0f,
            clouds::WeatherTimelineEffectiveEnd(config.plan));
        options_.cloud.selfShadowEnabled = config.selfShadow;
        options_.shadowOdEarlyOutTransmittance =
            config.shadowOdTransmittance;
        options_.shadowInnerOnly = config.shadowInnerOnly;

        frameTimingLabel_ = std::string("warmup_") + config.name;
        frameTimingBlock_ = -1;
        for (int i = 0; i < warmupFrames; ++i)
        {
            if (!RenderOneFrame(false, nullptr))
                Fail("P0 matrix warmup render failed");
            if (!window_.PumpMessages())
                return false;
        }

        frameTimingLabel_ = config.name;
        frameTimingBlock_ = cycle;
        for (int i = 0; i < measuredFrames; ++i)
        {
            if (!RenderOneFrame(false, nullptr))
                Fail("P0 matrix render failed");
            if (!window_.PumpMessages())
                return false;
        }
        return true;
    };

    // Global warmup on the most expensive state, then four interleaved cycles
    // (cycle 0 is conditioning and is excluded from the measured aggregates).
    const P0Config& heater = configs[8]; // three_outer_050
    frameTimingLabel_ = "global_warmup";
    frameTimingBlock_ = -2;
    options_.weatherTimeline = heater.plan;
    weatherTimelineTime_ = heater.time;
    options_.cloud.selfShadowEnabled = true;
    const int heaterFrames = renderer_.Width() >= 1600 ? 60 : 180;
    for (int i = 0; i < heaterFrames; ++i)
    {
        if (!RenderOneFrame(false, nullptr))
            Fail("P0 matrix global warmup failed");
        if (!window_.PumpMessages())
            break;
    }
    cloudProfiler_.ResetStatistics();

    Log("P0 matrix: %zu configs, %d cycles, %d warmup + %d measured frames",
        configs.size(), cycles, warmupFrames, measuredFrames);
    bool keepRunning = true;
    for (int cycle = 0; cycle < cycles && keepRunning; ++cycle)
    {
        for (size_t index = 0; index < configs.size() && keepRunning; ++index)
        {
            const size_t ordered = (cycle % 2 == 0)
                ? index : configs.size() - 1 - index;
            const P0Config& config = configs[ordered];
            if (!runBlock(config, cycle))
            {
                keepRunning = false;
                break;
            }
            measured += measuredFrames;
            Log("P0 block cycle=%d config=%s self_shadow=%d time=%.6f",
                cycle, config.name, config.selfShadow ? 1 : 0, config.time);
        }
    }
    frameTimingLabel_.clear();
    frameTimingBlock_ = -1;
    Log("P0 matrix complete: %d measured frames", measured);
    return keepRunning ? 0 : 1;
}

int Application::RunRuntimeSettingsTest()
{
    if (options_.mode != clouds::RenderMode::Clouds)
        Fail("--runtime-settings-test requires --mode=clouds");

    const std::filesystem::path directory =
        std::filesystem::absolute(options_.runtimeSettingsTestDir);
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec)
        Fail("cannot create runtime settings directory: " + ec.message());

    if (options_.windDisabled)
        options_.cloud.windSpeed = 0.0f;
    if (options_.cameraExplicit)
        camera_.position = options_.cameraPosition;
    if (options_.cameraYawExplicit)
        camera_.yawDegrees = options_.cameraYawDegrees;
    if (options_.cameraPitchExplicit)
        camera_.pitchDegrees = options_.cameraPitchDegrees;
    if (options_.fovExplicit)
        camera_.fovYDegrees = options_.fovYDegrees;
    if (weatherTimelineActive_)
        weatherTimelineTime_ = clouds::WeatherTimelineClampTime(
            options_.weatherTimeline, options_.weatherTimelineTime);

    window_.Show(SW_SHOW);
    UpdateWindow(window_.Handle());
    window_.PumpMessages();

    const uint32_t originalWidth = renderer_.Width();
    const uint32_t originalHeight = renderer_.Height();
    std::vector<uint8_t> pixels;
    const float dt = 1.0f / 60.0f;

    const auto renderStage = [&](const std::string& name, uint32_t frames)
    {
        for (uint32_t frame = 0; frame < frames; ++frame)
        {
            // Match the normal cloud-input order: pause freezes both cloud
            // time and the weather timeline; cloud on/off must not.
            if (!cloudPaused_)
            {
                if (!options_.holdCloudTime)
                    cloudTime_ += dt;
                if (weatherTimelineActive_)
                    UpdateWeatherTimeline(dt);
            }

            const bool capture = frame + 1 == frames;
            pixels.clear();
            if (!RenderOneFrame(capture, capture ? &pixels : nullptr))
                Fail("runtime settings render failed: " + name);
            if (!window_.PumpMessages())
                Fail("window closed during runtime settings test");
        }

        const std::filesystem::path path =
            directory / (name + ".bmp");
        if (!WriteBmp32(path, renderer_.Width(), renderer_.Height(),
                        pixels.data()))
            Fail("failed to write " + path.string());
        LogRuntimeWeatherState(name.c_str());
    };

    const auto requestStage = [&](const std::string& name,
                                  const clouds::CloudGraphicsState& state)
    {
        const clouds::CloudSettingsRequest request =
            cloudSettingsState_.Request(state);
        if (request == clouds::CloudSettingsRequest::Unsupported)
            Fail("runtime settings request unsupported: " + name);
        Log("runtime settings stage requested: stage=%s result=%s",
            name.c_str(),
            clouds::CloudGraphicsStateMachine::RequestName(request));
        renderStage(name, 2);
    };

    renderStage("00_initial", 2);

    const clouds::CloudQuality qualityOrder[] = {
        clouds::CloudQuality::Performance,
        clouds::CloudQuality::Balanced,
        clouds::CloudQuality::Quality,
        clouds::CloudQuality::Reference,
        clouds::CloudQuality::Balanced,
    };
    int qualityIndex = 1;
    for (clouds::CloudQuality quality : qualityOrder)
    {
        clouds::CloudGraphicsState state = cloudSettingsState_.Current();
        state.quality = quality;
        char name[64]{};
        snprintf(name, sizeof(name), "%02d_%s", qualityIndex++,
                 clouds::CloudQualityName(quality));
        requestStage(name, state);
    }

    {
        clouds::CloudGraphicsState state = cloudSettingsState_.Current();
        state.cloudsEnabled = false;
        requestStage("06_clouds_off", state);
    }
    {
        clouds::CloudGraphicsState state = cloudSettingsState_.Current();
        state.cloudsEnabled = true;
        requestStage("07_clouds_on", state);
    }

    cloudPaused_ = true;
    renderStage("08_paused", 2);
    const float pausedTimeline = weatherTimelineTime_;
    const float pausedCloudTime = cloudTime_;
    Log("runtime pause check: timeline_before=%.6f timeline_after=%.6f "
        "cloud_before=%.6f cloud_after=%.6f unchanged=%d",
        pausedTimeline, weatherTimelineTime_, pausedCloudTime, cloudTime_,
        (pausedTimeline == weatherTimelineTime_ &&
         pausedCloudTime == cloudTime_) ? 1 : 0);
    cloudPaused_ = false;
    renderStage("09_resumed", 2);
    Log("runtime resume check: timeline=%.6f cloud_time=%.6f advanced=%d",
        weatherTimelineTime_, cloudTime_,
        (weatherTimelineTime_ != pausedTimeline ||
         cloudTime_ != pausedCloudTime) ? 1 : 0);

    renderer_.Resize(960, 540);
    cloudRayMarcher_.Resize(renderer_.Width(), renderer_.Height());
    renderStage("10_resize_960x540", 2);
    renderer_.Resize(originalWidth, originalHeight);
    cloudRayMarcher_.Resize(renderer_.Width(), renderer_.Height());
    renderStage("11_resize_back", 2);

    {
        clouds::CloudGraphicsState state = cloudSettingsState_.Current();
        state.quality = clouds::CloudQuality::Quality;
        requestStage("12_quality_after_resize", state);
    }

    Log("runtime settings test complete: generation=%llu "
        "resource_generation=%llu",
        static_cast<unsigned long long>(cloudSettingsState_.Generation()),
        static_cast<unsigned long long>(
            cloudRayMarcher_.ResourceGeneration()));
    return 0;
}

#if defined(OGSR_CLOUDS_FAULT_INJECTION)
int Application::RunRuntimeSettingsFaultTest()
{
    if (options_.mode != clouds::RenderMode::Clouds)
        Fail("--runtime-settings-fault-test requires --mode=clouds");
    const std::filesystem::path directory =
        std::filesystem::absolute(options_.runtimeSettingsFaultTestDir);
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec)
        Fail("cannot create fault test directory: " + ec.message());

    if (options_.cameraExplicit)
        camera_.position = options_.cameraPosition;
    if (options_.cameraYawExplicit)
        camera_.yawDegrees = options_.cameraYawDegrees;
    if (options_.cameraPitchExplicit)
        camera_.pitchDegrees = options_.cameraPitchDegrees;
    if (options_.fovExplicit)
        camera_.fovYDegrees = options_.fovYDegrees;
    if (weatherTimelineActive_)
        weatherTimelineTime_ = clouds::WeatherTimelineClampTime(
            options_.weatherTimeline, options_.weatherTimelineTime);

    window_.Show(SW_SHOW);
    UpdateWindow(window_.Handle());
    window_.PumpMessages();

    const uint32_t originalWidth = renderer_.Width();
    const uint32_t originalHeight = renderer_.Height();
    std::vector<uint8_t> pixels;
    const float dt = 1.0f / 60.0f;

    const auto renderStage = [&](const std::string& name, uint32_t frames)
    {
        for (uint32_t frame = 0; frame < frames; ++frame)
        {
            if (!options_.holdCloudTime)
                cloudTime_ += dt;
            if (weatherTimelineActive_)
                UpdateWeatherTimeline(dt);
            const bool capture = frame + 1 == frames;
            pixels.clear();
            if (!RenderOneFrame(capture, capture ? &pixels : nullptr))
                Fail("fault-test render failed: " + name);
            if (!window_.PumpMessages())
                Fail("window closed during fault test");
        }
        const std::filesystem::path path = directory / (name + ".bmp");
        if (!WriteBmp32(path, renderer_.Width(), renderer_.Height(),
                        pixels.data()))
            Fail("failed to write " + path.string());
        LogRuntimeWeatherState(name.c_str());
    };

    renderStage("00_before_fault", 2);

    const bool armAtResize =
        clouds::TestEnvEquals("OGSR_CLOUD_FAULT_STAGE", "resize");
    if (!armAtResize)
        clouds::ArmCloudFault();
    if (clouds::TestEnvEquals("OGSR_CLOUD_LOGGER_FAULT_ARM", "quality"))
        clouds::ArmLoggerFault();

    {
        clouds::CloudGraphicsState next = cloudSettingsState_.Effective();
        next.quality = clouds::CloudQuality::Reference;
        const clouds::CloudSettingsRequest request =
            cloudSettingsState_.Request(next);
        Log("fault test first request: result=%s target=reference",
            clouds::CloudGraphicsStateMachine::RequestName(request));
    }
    renderStage("01_after_failed_apply", 2);
    Log("fault test after failed apply: current_quality=%s pending=%d "
        "divisor=%u output=%ux%u resource_generation=%llu",
        clouds::CloudQualityName(cloudSettingsState_.Current().quality),
        cloudSettingsState_.HasPending() ? 1 : 0,
        options_.cloudDivisor, cloudRayMarcher_.OutputWidth(),
        cloudRayMarcher_.OutputHeight(),
        static_cast<unsigned long long>(cloudRayMarcher_.ResourceGeneration()));

    {
        clouds::CloudGraphicsState next = cloudSettingsState_.Effective();
        next.quality = clouds::CloudQuality::Reference;
        const clouds::CloudSettingsRequest request =
            cloudSettingsState_.Request(next);
        Log("fault test retry request: result=%s target=reference",
            clouds::CloudGraphicsStateMachine::RequestName(request));
    }
    renderStage("02_retry_reference", 2);
    renderStage("03_retry_settled", 1);
    Log("fault test after retry: current_quality=%s pending=%d "
        "divisor=%u output=%ux%u resource_generation=%llu",
        clouds::CloudQualityName(cloudSettingsState_.Current().quality),
        cloudSettingsState_.HasPending() ? 1 : 0,
        options_.cloudDivisor, cloudRayMarcher_.OutputWidth(),
        cloudRayMarcher_.OutputHeight(),
        static_cast<unsigned long long>(cloudRayMarcher_.ResourceGeneration()));

    if (armAtResize)
    {
        Log("fault test arming renderer terminal fault at resize");
        clouds::ArmCloudFault();
    }
    if (clouds::TestEnvEquals("OGSR_CLOUD_LOGGER_FAULT_ARM", "resize"))
        clouds::ArmLoggerFault();
    renderer_.Resize(960, 540);
    cloudRayMarcher_.Resize(renderer_.Width(), renderer_.Height());
    renderStage("04_resize_960x540", 2);
    renderer_.Resize(originalWidth, originalHeight);
    cloudRayMarcher_.Resize(renderer_.Width(), renderer_.Height());
    renderStage("05_resize_back", 2);

    {
        clouds::CloudGraphicsState next = cloudSettingsState_.Effective();
        next.cloudsEnabled = false;
        cloudSettingsState_.Request(next);
    }
    renderStage("06_clouds_off", 2);
    {
        clouds::CloudGraphicsState next = cloudSettingsState_.Effective();
        next.cloudsEnabled = true;
        cloudSettingsState_.Request(next);
    }
    renderStage("07_clouds_on", 2);

    Log("runtime settings fault test complete: settings_generation=%llu "
        "resource_generation=%llu",
        static_cast<unsigned long long>(cloudSettingsState_.Generation()),
        static_cast<unsigned long long>(
            cloudRayMarcher_.ResourceGeneration()));
    return 0;
}
#endif

int Application::RunInteractive()
{
    window_.Show(SW_SHOW);
    UpdateWindow(window_.Handle());
    UpdateTitle(true);

    auto previous = std::chrono::steady_clock::now();
    bool wasMinimized = false;
    // Diagnostics produced after RenderOneFrame/Present run in an interval where
    // later GPU submissions may still be unconfirmed (the interactive path
    // returns without waiting). They are deferred into this fixed,
    // allocation-free buffer and emitted only when the renderer reports a fully
    // confirmed GPU idle, so no blocking diagnostic sink can sit between a
    // submission and its confirmation.
    char deferredLogs[16][192]{};
    int deferredLogCount = 0;
    const auto deferLog = [&deferredLogs, &deferredLogCount]
        (const char* format, auto... args)
    {
        if (deferredLogCount < 16)
        {
            snprintf(deferredLogs[deferredLogCount], sizeof(deferredLogs[0]),
                     format, args...);
            ++deferredLogCount;
        }
    };
    const auto flushDeferred = [&]()
    {
        if (deferredLogCount == 0)
            return;
        // Never emit a diagnostic while later GPU work is unconfirmed.
        if (!renderer_.GpuIdleConfirmed())
            return;
        for (int index = 0; index < deferredLogCount; ++index)
            Log("%s", deferredLogs[index]);
        deferredLogCount = 0;
    };
#if defined(OGSR_CLOUDS_FAULT_INJECTION)
    // Address-directed interactive multi-frame test: keep several REAL frame
    // submissions unconfirmed (a queue gate after frame 0), reuse an old back
    // buffer, and verify that no blocking diagnostic is attempted meanwhile.
    const bool inflightTest = clouds::TestEnvFlag("OGSR_CLOUD_INFLIGHT_TEST");
    // 0 = burst not yet done, 1 = pending window open, 2 = window closed.
    int inflightPhase = 0;
#else
    const bool inflightTest = false;
#endif
    while (running_ && window_.PumpMessages())
    {
        if (window_.IsMinimized())
        {
            if (!wasMinimized)
            {
                // Reached after the previous interactive Present: defer instead
                // of logging into a possibly unconfirmed GPU interval.
                deferLog("window minimized; rendering paused");
                wasMinimized = true;
            }
            Sleep(50);
            previous = std::chrono::steady_clock::now();
#if defined(OGSR_CLOUDS_FAULT_INJECTION)
            if (inflightTest && inflightPhase == 1)
            {
                // Complete the real minimize/restore cycle: the "restored"
                // message is deferred like every other post-Present diagnostic.
                ShowWindow(window_.Handle(), SW_RESTORE);
            }
#endif
            continue;
        }
        if (wasMinimized)
        {
            deferLog("window restored; resuming rendering");
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
        if (options_.fixedFrameDelta > 0.0f)
            delta = options_.fixedFrameDelta;

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

        // Discard warmup timestamps before the first measured frame. Resetting
        // after the frame would lose one measured sample.
        if (options_.warmupFrames != 0 &&
            renderedFrames_ == options_.warmupFrames)
        {
            cloudProfiler_.ResetStatistics();
            deferLog("performance warmup complete after %u frames",
                     options_.warmupFrames);
        }

        if (!RenderOneFrame(false, nullptr))
        {
            Sleep(1);
            continue;
        }

        ++renderedFrames_;
        UpdateTitle(false);

#if defined(OGSR_CLOUDS_FAULT_INJECTION)
        if (inflightTest && inflightPhase == 1 && wasMinimized == false)
        {
            // The window was restored: confirm a full idle and drain the
            // deferred diagnostics that had to wait for it.
            if (!renderer_.TryWaitForGpu())
            {
                TerminalFailClosed(
                    "inflight test: deferred window completion is UNKNOWN; "
                    "TERMINAL fail-closed");
            }
            flushDeferred();
            inflightPhase = 2;
        }
        if (inflightTest && inflightPhase == 0 && renderedFrames_ >= 1)
        {
            // Frame 0 is submitted and confirmed with a real full-idle wait.
            if (!renderer_.TryWaitForGpu())
            {
                TerminalFailClosed(
                    "inflight test: frame 0 completion is UNKNOWN; "
                    "TERMINAL fail-closed");
            }

            // Build a genuinely pending backlog: real frame command lists
            // submitted back to back with no intervening wait. If this GPU is
            // fast enough to drain them, submit more; without a confirmed
            // pending submission the test would prove nothing and must fail.
            int burstFrames = 0;
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                for (int burst = 0; burst < 2; ++burst)
                {
                    if (RenderOneFrame(false, nullptr))
                        ++burstFrames;
                }
                if (!renderer_.GpuIdleConfirmed())
                    break;
            }
            if (renderer_.GpuIdleConfirmed() || burstFrames < 2)
            {
                clouds::NoteDiagnosticsUnsafe();
                Fail("inflight test: could not keep submissions in flight");
            }

            // Install the full pipes only NOW, while later submissions are
            // unconfirmed. Every diagnostic from here on would hang if it were
            // emitted into the blocked stream.
            clouds::MaybeActivateBlockedPipes("inflight-diagnostics");

            clouds::NoteInflightCheck(
                static_cast<long>(renderer_.InflightSubmissionCount()));

            // Probe the exact gate that BeginFrame uses. While later frames are
            // unconfirmed it must refuse to touch the blocked stream; if it ever
            // flushed, the child would hang here and the harness would report a
            // timeout = FAIL. Once the GPU is idle a flush is legitimate and is
            // deliberately not probed (a permanently full pipe would block it).
            const bool pendingNow = !renderer_.GpuIdleConfirmed();
            const bool flushedWhilePending =
                pendingNow && renderer_.FlushDiagnosticsIfIdle("inflight-pending");
            if (!pendingNow || flushedWhilePending)
            {
                clouds::NoteDiagnosticsUnsafe();
                Fail("inflight test: diagnostics gate did not defer pending work");
            }

            // The pending window is over: restore the real streams BEFORE any
            // further logging. A permanently full pipe would (correctly) block
            // a legitimate post-idle flush, which is not what this test measures.
            clouds::DeactivateBlockedPipes();
            // Real minimize/restore cycle; its messages go through the same
            // deferred buffer and are flushed only after a confirmed idle.
            ShowWindow(window_.Handle(), SW_MINIMIZE);
            inflightPhase = 1;
        }
#endif        flushDeferred();

        if (options_.resizeTest)
        {
            // Exercise ResizeBuffers with two different client sizes while the
            // debug layer is active. The exact outer size is not important;
            // WM_SIZE supplies the real client dimensions.
            if (renderedFrames_ == 20)
            {
                deferLog("resize test: requesting 960x540 outer window");
                SetWindowPos(window_.Handle(), nullptr, 0, 0, 960, 540,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            else if (renderedFrames_ == 50)
            {
                deferLog("resize test: requesting 1280x800 outer window");
                SetWindowPos(window_.Handle(), nullptr, 0, 0, 1280, 800,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }

        if (options_.frames != 0 && renderedFrames_ >= options_.frames)
        {
            deferLog("frame limit reached (%llu frames)",
                     static_cast<unsigned long long>(renderedFrames_));
            break;
        }
    }

    // The interactive path returns from Present without waiting for the frame
    // fence, so the GPU completion state is unknown here. Confirm it before the
    // deferred/escalating diagnostics are emitted: a blocked diagnostic sink in
    // an interval with unknown completion is exactly what this gate forbids.
    if (!renderer_.TryWaitForGpu())
    {
        TerminalFailClosed(
            "interactive loop end: GPU completion is UNKNOWN; TERMINAL "
            "fail-closed before reporting loop diagnostics");
    }
    for (int i = 0; i < deferredLogCount; ++i)
        Log("%s", deferredLogs[i]);

    Log("interactive loop ended after %llu frames; debug errors=%llu warnings=%llu",
        static_cast<unsigned long long>(renderedFrames_),
        static_cast<unsigned long long>(renderer_.DebugErrorCount()),
        static_cast<unsigned long long>(renderer_.DebugWarningCount()));
    Log("weather controller state: source=%s mode=%s timeline_active=%d "
        "timeline_time=%.6f transition_active=%d transition_time=%.6f "
        "cloud_time=%.6f clouds_enabled=%d",
        options_.weatherSource.c_str(),
        clouds::StaticWeatherModeName(options_.weatherMode),
        weatherTimelineActive_ ? 1 : 0, weatherTimelineTime_,
        weatherTransitionActive_ ? 1 : 0, weatherTransitionElapsed_,
        cloudTime_, cloudsEnabled_ ? 1 : 0);

    return renderer_.DebugErrorCount() == 0 ? 0 : 2;
}

bool Application::RenderOneFrame(bool capture, std::vector<uint8_t>* pixels)
{
    clouds::WitnessEvent(clouds::WitnessCode::StartupFirstFrame);
    if (!noise_.IsInitialized())
        Fail("RenderOneFrame called before the noise volume was initialized");
    if (capture && !pixels)
        Fail("RenderOneFrame capture requested without an output buffer");

    // V-WEATHER-01C-B: public graphics setting changes are committed only at a
    // frame boundary, before any new frame commands or descriptors are used.
    ApplyPendingCloudSettings();
    if (gpuConfigurationFatal_)
        Fail("cloud GPU reconfiguration is fail-closed; no further GPU "
             "submission is allowed");

    Renderer::Frame frame = renderer_.BeginFrame();
    if (!frame.valid)
        return false;

    cloudProfiler_.Collect(frame.backBufferIndex);
    WriteFrameTiming();

    if (options_.mode == clouds::RenderMode::Clouds)
    {
        if (!cloudsEnabled_)
        {
            if (!cloudDisabledLogged_)
            {
                Log("cloud rendering disabled by public setting; weather "
                    "controller state is unaffected");
                cloudDisabledLogged_ = true;
            }
            renderer_.BeginRenderPass(frame);
            renderer_.EndRenderPass(frame);
        }
        else
        {
        if (!cloudFramePathLogged_)
        {
            Log("cloud frame path: CloudRayMarcher compute + CloudCompositor draw active");
            cloudFramePathLogged_ = true;
        }
        const clouds::CloudFrameConstants constants = BuildCloudConstants(cloudDebug_);
        cloudProfiler_.BeginFrame(frame.list, frame.backBufferIndex);
        cloudRayMarcher_.Render(frame.list, frame.backBufferIndex, constants,
                                noise_.SrvGpuHandle(),
                                weatherMap_.SrvGpuHandle(),
                                options_.shapePrototype,
                                options_.cloudFormula,
                                options_.densityModel);
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
            LogErrorBestEffort("input action failed: %s", e.what());
            running_ = false;
            window_.Close();
        }
        catch (...)
        {
            LogErrorBestEffort(
                "input action failed with a non-standard exception");
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

#if !defined(OGSR_WEATHER_DEVELOPER_TOOLS)
    // Shipping-controls contract: the V0 slice viewer's noise controls
    // (seed/size/frequency/octaves/worley/blend/erosion/gain/regenerate) are
    // developer-only. No slice hotkeys are registered in this build.
    switch (key)
    {
    case VK_ESCAPE:
        running_ = false;
        window_.Close();
        return;
    case 'H':
    case VK_F1:
        if ((lparam & (1LL << 30)) == 0)
            PrintHelp();
        return;
    default:
        return;
    }
#else
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
#endif
}

void Application::HandleCloudKey(WPARAM key, LPARAM lparam)
{
    const bool repeat = (lparam & (1LL << 30)) != 0;

#if !defined(OGSR_WEATHER_DEVELOPER_TOOLS)
    // Shipping-controls contract: no weather/debug hotkeys are registered.
    // Only lifecycle, pause, FOV and help remain.
    switch (key)
    {
    case VK_ESCAPE:
        mouseLook_ = false;
        ReleaseCapture();
        running_ = false;
        window_.Close();
        return;
    case 'P':
        if (!repeat)
        {
            cloudPaused_ = !cloudPaused_;
            Log("cloud animation %s", cloudPaused_ ? "paused" : "running");
        }
        break;
    case 'C':
        if (!repeat)
            RequestCloudsToggle();
        break;
    case VK_F6:
        if (!repeat)
        {
            const int direction =
                (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
            RequestCloudQualityStep(direction);
        }
        break;
    case 'Z':
        camera_.fovYDegrees =
            std::clamp(camera_.fovYDegrees - 2.0f, 35.0f, 90.0f);
        break;
    case 'X':
        camera_.fovYDegrees =
            std::clamp(camera_.fovYDegrees + 2.0f, 35.0f, 90.0f);
        break;
    case 'H':
    case VK_F1:
        if (!repeat)
            PrintHelp();
        return;
    default:
        return;
    }
    UpdateTitle(true);
#else
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
        {
            if (options_.weatherTimelineExplicit)
                RequestWeatherTimelineInterrupt(
                    clouds::StaticWeatherMode::Clear);
            else
                StartCloudTransition("clear", 5.0f);
        }
        break;
    case '2':
        if (!repeat)
        {
            if (options_.weatherTimelineExplicit)
                RequestWeatherTimelineInterrupt(
                    clouds::StaticWeatherMode::Overcast);
            else
                StartCloudTransition("overcast", 5.0f);
        }
        break;
    case '3':
        if (!repeat)
        {
            if (options_.weatherTimelineExplicit)
                RequestWeatherTimelineInterrupt(
                    clouds::StaticWeatherMode::Partly);
            else
                StartCloudTransition("partly", 5.0f);
        }
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
    case '0':
        cloudDebug_ = clouds::CloudDebugMode::Weather;
        break;
    case 'V':
        cloudDebug_ = clouds::CloudDebugMode::CloudType;
        break;
    case 'D':
        cloudDebug_ = clouds::CloudDebugMode::DensitySlice;
        break;
    case 'M':
        if (!repeat)
        {
            const uint32_t next =
                (static_cast<uint32_t>(cloudDebug_) + 1u) % 18u;
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
    case 'C':
        if (!repeat)
            RequestCloudsToggle();
        break;
    case VK_F6:
        if (!repeat)
        {
            const int direction =
                (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
            RequestCloudQualityStep(direction);
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
            weatherTransitionActive_ = false;
            weatherTransitionTarget_.clear();
            ApplyCloudPreset("clear", true);
            Log("cloud scene reset to the reproducible Clear Day camera");
        }
        break;
    case VK_F5:
        if (!repeat)
        {
            static const char* kWeatherOrder[] = {
                "clear", "partly", "overcast", "rain", "storm", "sunset"
            };
            constexpr int kWeatherCount =
                static_cast<int>(sizeof(kWeatherOrder) / sizeof(kWeatherOrder[0]));
            int current = 0;
            for (int i = 0; i < kWeatherCount; ++i)
            {
                if (cloudPreset_ == kWeatherOrder[i])
                {
                    current = i;
                    break;
                }
            }
            const bool reverse = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const int next = (current + (reverse ? kWeatherCount - 1 : 1)) %
                             kWeatherCount;
            StartCloudTransition(kWeatherOrder[next], 6.0f);
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
#endif
}

void Application::UpdateCloudInput(float deltaSeconds)
{
    if (options_.mode != clouds::RenderMode::Clouds)
        return;

    if (!options_.weatherTimelineExplicit)
        UpdateCloudTransition(deltaSeconds);
    if (!cloudPaused_ && !options_.holdCloudTime)
        cloudTime_ += deltaSeconds;
    if (!cloudPaused_)
        UpdateWeatherTimeline(deltaSeconds);

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
    weatherTransitionActive_ = false;
    weatherTransitionTarget_.clear();
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

void Application::StartCloudTransition(const std::string& name, float seconds)
{
    clouds::CloudParams target = options_.cloud;
    if (!clouds::ApplyPreset(name, target))
    {
        LogWarn("unknown weather transition target: %s", name.c_str());
        return;
    }

    weatherTransitionStart_ = options_.cloud;
    weatherTransitionTargetParams_ = target;
    weatherTransitionTarget_ = name;
    weatherTransitionElapsed_ = 0.0f;
    weatherTransitionDuration_ = std::max(seconds, 0.1f);
    weatherTransitionActive_ = true;
    Log("weather transition: %s -> %s over %.1f s",
        cloudPreset_.c_str(), name.c_str(), weatherTransitionDuration_);
    UpdateTitle(true);
}

void Application::UpdateCloudTransition(float deltaSeconds)
{
    if (!weatherTransitionActive_ || cloudPaused_)
        return;

    weatherTransitionElapsed_ += deltaSeconds;
    const float t = std::clamp(
        weatherTransitionElapsed_ / weatherTransitionDuration_, 0.0f, 1.0f);
    options_.cloud = clouds::LerpCloudParams(
        weatherTransitionStart_, weatherTransitionTargetParams_, t);

    if (t >= 1.0f)
    {
        options_.cloud = weatherTransitionTargetParams_;
        cloudPreset_ = weatherTransitionTarget_;
        weatherTransitionActive_ = false;
        weatherTransitionTarget_.clear();
        Log("weather transition complete: %s", cloudPreset_.c_str());
    }
    UpdateTitle(true);
}

void Application::ApplyWeatherTransitionOverrides(
    clouds::CloudParams& params) const
{
    // These controls are owned by the CLI/quality/lighting sources, not by the
    // weather preset. Keeping them identical on both endpoints prevents the
    // transition from changing sun, exposure, quality or wind.
    params.sunAzimuthDegrees = options_.cloud.sunAzimuthDegrees;
    params.sunElevationDegrees = options_.cloud.sunElevationDegrees;
    // Exposure belongs to the weather endpoint in this standalone gate. Do not
    // copy the source exposure onto the target; that would break the exact
    // Gate A rain_sf endpoint. The common transition state blends it.
    params.stepCount = options_.cloud.stepCount;
    params.shadowStepCount = options_.cloud.shadowStepCount;
    params.sunShadowDistance = options_.cloud.sunShadowDistance;
    params.maxDistance = options_.cloud.maxDistance;
    params.selfShadowEnabled = options_.cloud.selfShadowEnabled;
    params.breakupEnabled = options_.cloud.breakupEnabled;
    params.heightFront = options_.cloud.heightFront;
    params.lightingMode = options_.cloud.lightingMode;
    params.jitterMode = options_.cloud.jitterMode;
    params.atmosphereEnabled = options_.cloud.atmosphereEnabled;
    params.atmosphereExtinction = options_.cloud.atmosphereExtinction;
    params.atmosphereHeightFalloff = options_.cloud.atmosphereHeightFalloff;
    params.multiScatterStrength = options_.cloud.multiScatterStrength;
    params.ambientScale = options_.cloud.ambientScale;
    params.windSpeed = options_.cloud.windSpeed;
    params.windDirection = options_.cloud.windDirection;
    params.weatherMapWorldSize = options_.cloud.weatherMapWorldSize;
    params.stormFrontDebug = options_.cloud.stormFrontDebug;

    if (options_.cloudCoverageExplicit)
        params.coverage = options_.cloud.coverage;
    if (options_.cloudDensityExplicit)
        params.density = options_.cloud.density;
    if (options_.verticalNoiseExplicit)
        params.verticalNoiseScale = options_.cloud.verticalNoiseScale;
    if (options_.topFadeExplicit)
        params.topFade = options_.cloud.topFade;
    if (options_.formulaBiasExplicit)
        params.formulaBias = options_.cloud.formulaBias;
}

clouds::CloudParams Application::BuildWeatherTransitionEndpoint(
    clouds::StaticWeatherMode mode) const
{
    clouds::CloudParams params{};
    clouds::ApplyStaticWeatherMode(mode, params);
    ApplyWeatherTransitionOverrides(params);
    return params;
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
    if (options_.weatherTimelineExplicit)
        return BuildTimelineConstants(debug);
    if (options_.weatherTransitionExplicit)
    {
        const clouds::CloudParams source =
            BuildWeatherTransitionEndpoint(options_.transitionSource);
        const clouds::CloudParams target =
            BuildWeatherTransitionEndpoint(options_.transitionTarget);
        const float phase = std::clamp(options_.transitionPhase, 0.0f, 1.0f);
        if (phase <= 0.0f)
        {
            return clouds::BuildCloudConstants(
                camera_, source, width, height, cloudTime_, debug,
                options_.densityModel, options_.referenceStage,
                options_.reference, options_.noise.seed);
        }
        if (phase >= 1.0f)
        {
            return clouds::BuildCloudConstants(
                camera_, target, width, height, cloudTime_, debug,
                options_.densityModel, options_.referenceStage,
                options_.reference, options_.noise.seed);
        }
        const float mix = phase * phase * (3.0f - 2.0f * phase);
        clouds::CloudParams common =
            clouds::LerpCloudParams(source, target, mix);
        ApplyWeatherTransitionOverrides(common);
        return clouds::BuildTransitionCloudConstants(
            camera_, common, source, target, mix, width, height,
            cloudTime_, debug, options_.densityModel,
            options_.referenceStage, options_.reference, options_.noise.seed);
    }
    return clouds::BuildCloudConstants(
        camera_, options_.cloud, width, height, cloudTime_, debug,
        options_.densityModel, options_.referenceStage, options_.reference,
        options_.noise.seed);
}

void Application::StartWeatherTimeline()
{
    weatherTimelineActive_ = true;
    const float effectiveEnd =
        clouds::WeatherTimelineEffectiveEnd(options_.weatherTimeline);
    weatherTimelineTime_ =
        clouds::WeatherTimelineClampTime(
            options_.weatherTimeline, options_.weatherTimelineTime);
    std::string sequence;
    for (size_t i = 0; i < options_.weatherTimeline.modes.size(); ++i)
    {
        if (i)
            sequence += " -> ";
        sequence += clouds::StaticWeatherModeName(
            options_.weatherTimeline.modes[i]);
    }
    Log("weather timeline: %s; segment=%.3f s; time=%.3f; end=%.3f; "
        "play=%d",
        sequence.c_str(), options_.weatherTimeline.segmentDuration,
        weatherTimelineTime_, effectiveEnd,
        options_.weatherTimelinePlay ? 1 : 0);
    if (options_.weatherTimeline.hasInterrupt)
    {
        Log("weather timeline interrupt: segment=%d phase=%.3f target=%s "
            "duration=%.3f start=%.3f",
            options_.weatherTimeline.interruptSegment,
            options_.weatherTimeline.interruptPhase,
            clouds::StaticWeatherModeName(
                options_.weatherTimeline.interruptTarget),
            options_.weatherTimeline.interruptDuration,
            clouds::WeatherTimelineInterruptStart(options_.weatherTimeline));
    }
}

void Application::UpdateWeatherTimeline(float deltaSeconds)
{
    if (!weatherTimelineActive_ || !options_.weatherTimelinePlay)
        return;
    const float total =
        clouds::WeatherTimelineEffectiveEnd(options_.weatherTimeline);
    if (total <= 0.0f)
        return;
    const float delta = std::isfinite(deltaSeconds)
        ? std::max(deltaSeconds, 0.0f) : 0.0f;
    const float current = std::isfinite(weatherTimelineTime_)
        ? std::max(weatherTimelineTime_, 0.0f) : 0.0f;
    weatherTimelineTime_ = std::min(total, current + delta);
}

clouds::WeatherTimelineVisual Application::EvaluateTimeline(
    float timeSeconds) const
{
    return clouds::EvaluateWeatherTimeline(
        options_.weatherTimeline, timeSeconds);
}

void Application::RequestWeatherTimelineInterrupt(
    clouds::StaticWeatherMode target)
{
    if (!weatherTimelineActive_ ||
        options_.weatherTimeline.modes.size() < 2)
        return;
    if (target == clouds::StaticWeatherMode::StormPreview)
    {
        LogWarn("weather timeline interrupt rejects storm_preview");
        return;
    }

    const int segments =
        static_cast<int>(options_.weatherTimeline.modes.size()) - 1;
    const float duration = options_.weatherTimeline.segmentDuration;
    if (options_.weatherTimeline.hasInterrupt)
    {
        const float interruptTime =
            duration * static_cast<float>(
                options_.weatherTimeline.interruptSegment) +
            duration * options_.weatherTimeline.interruptPhase;
        if (weatherTimelineTime_ >= interruptTime)
        {
            LogWarn("weather timeline interrupt ignored: nested interrupt");
            return;
        }
    }

    const clouds::WeatherTimelineVisual visual =
        EvaluateTimeline(weatherTimelineTime_);
    if (visual.kind == clouds::WeatherTimelineKind::Invalid)
        return;
    if (visual.kind == clouds::WeatherTimelineKind::LegacyInterrupted ||
        visual.kind == clouds::WeatherTimelineKind::ThreeState)
    {
        LogWarn("weather timeline interrupt ignored: already interrupted");
        return;
    }

    const int segment = std::clamp(visual.segment, 0, segments - 1);
    const float segmentStart =
        duration * static_cast<float>(segment);
    const float rawPhase = std::clamp(
        (weatherTimelineTime_ - segmentStart) / duration, 0.0f, 1.0f);
    options_.weatherTimeline.hasInterrupt = true;
    options_.weatherTimeline.interruptSegment = segment;
    options_.weatherTimeline.interruptPhase = rawPhase;
    options_.weatherTimeline.interruptTarget = target;
    options_.weatherTimeline.interruptDuration = duration;
    weatherTimelineTime_ = segmentStart + rawPhase * duration;
    Log("weather timeline interrupt: segment=%d phase=%.4f target=%s "
        "at time %.4f",
        segment, rawPhase, clouds::StaticWeatherModeName(target),
        weatherTimelineTime_);
}

clouds::CloudFrameConstants Application::BuildTimelineConstants(
    clouds::CloudDebugMode debug) const
{
    const uint32_t width = cloudRayMarcher_.OutputWidth() > 0
        ? cloudRayMarcher_.OutputWidth()
        : std::max(1u, renderer_.Width() / 2);
    const uint32_t height = cloudRayMarcher_.OutputHeight() > 0
        ? cloudRayMarcher_.OutputHeight()
        : std::max(1u, renderer_.Height() / 2);
    const clouds::WeatherTimelineVisual visual =
        EvaluateTimeline(weatherTimelineTime_);
    if (visual.kind == clouds::WeatherTimelineKind::Invalid)
        Fail("weather timeline evaluated to an invalid state");

    const auto endpoint = [this](clouds::StaticWeatherMode mode)
    {
        return BuildWeatherTransitionEndpoint(mode);
    };
    const auto buildStatic = [&](const clouds::CloudParams& params)
    {
        return clouds::BuildCloudConstants(
            camera_, params, width, height, cloudTime_, debug,
            options_.densityModel, options_.referenceStage,
            options_.reference, options_.noise.seed);
    };
    const auto buildTransition = [&](const clouds::CloudParams& common,
                                     const clouds::CloudParams& source,
                                     const clouds::CloudParams& target,
                                     float mix)
    {
        return clouds::BuildTransitionCloudConstants(
            camera_, common, source, target, mix, width, height, cloudTime_,
            debug, options_.densityModel, options_.referenceStage,
            options_.reference, options_.noise.seed);
    };

    switch (visual.kind)
    {
    case clouds::WeatherTimelineKind::Legacy:
    {
        const clouds::CloudParams source = endpoint(visual.source);
        const clouds::CloudParams target = endpoint(visual.target);
        if (visual.sourceMix <= 0.0f)
            return buildStatic(source);
        if (visual.sourceMix >= 1.0f)
            return buildStatic(target);
        return buildStatic(clouds::LerpCloudParams(
            source, target, visual.sourceMix));
    }
    case clouds::WeatherTimelineKind::LegacyInterrupted:
    {
        const clouds::CloudParams source = endpoint(visual.source);
        const clouds::CloudParams target = endpoint(visual.target);
        const clouds::CloudParams outer = endpoint(visual.outer);
        const clouds::CloudParams current =
            clouds::LerpCloudParams(source, target, visual.sourceMix);
        if (visual.outerMix <= 0.0f)
            return buildStatic(current);
        if (visual.outerMix >= 1.0f)
            return buildStatic(outer);
        return buildStatic(clouds::LerpCloudParams(
            current, outer, visual.outerMix));
    }
    case clouds::WeatherTimelineKind::TwoState:
    {
        const clouds::CloudParams source = endpoint(visual.source);
        const clouds::CloudParams target = endpoint(visual.target);
        if (visual.sourceMix <= 0.0f)
            return buildStatic(source);
        if (visual.sourceMix >= 1.0f)
            return buildStatic(target);
        return buildTransition(
            clouds::LerpCloudParams(source, target, visual.sourceMix),
            source, target, visual.sourceMix);
    }
    case clouds::WeatherTimelineKind::ThreeState:
    {
        const clouds::CloudParams source = endpoint(visual.source);
        const clouds::CloudParams target = endpoint(visual.target);
        const clouds::CloudParams outer = endpoint(visual.outer);
        if (visual.outerMix <= 0.0f)
        {
            if (visual.sourceMix <= 0.0f)
                return buildStatic(source);
            if (visual.sourceMix >= 1.0f)
                return buildStatic(target);
            return buildTransition(
                clouds::LerpCloudParams(source, target, visual.sourceMix),
                source, target, visual.sourceMix);
        }
        if (visual.outerMix >= 1.0f)
            return buildStatic(outer);
        const clouds::CloudParams inner =
            clouds::LerpCloudParams(source, target, visual.sourceMix);
        const float shadowOdThreshold =
            options_.shadowOdEarlyOutTransmittance > 0.0f
                ? -std::log(options_.shadowOdEarlyOutTransmittance)
                : 0.0f;
        return clouds::BuildThreeStateCloudConstants(
            camera_, clouds::LerpCloudParams(inner, outer, visual.outerMix),
            source, target, outer, visual.sourceMix, visual.outerMix,
            width, height, cloudTime_, debug, options_.densityModel,
            options_.referenceStage, options_.reference, options_.noise.seed,
            shadowOdThreshold, options_.shadowInnerOnly);
    }
    default:
        break;
    }
    Fail("weather timeline state kind is not implemented");
    return buildStatic(options_.cloud);
}

void Application::WriteFrameTiming()
{
    if (!frameTimingLog_.is_open())
        return;
    const clouds::WeatherTimelineVisual visual =
        EvaluateTimeline(weatherTimelineTime_);
    frameTimingLog_ << frameTimingRow_++ << ","
                    << cloudProfiler_.LastCloudMs() << ","
                    << cloudProfiler_.LastCompositeMs() << ","
                    << cloudProfiler_.LastTotalMs() << ","
                    << cloudTime_ << ","
                    << weatherTimelineTime_ << ","
                    << clouds::WeatherTimelineEffectiveEnd(
                           options_.weatherTimeline) << ","
                    << static_cast<int>(visual.kind) << ","
                    << static_cast<int>(visual.source) << ","
                    << static_cast<int>(visual.target) << ","
                    << static_cast<int>(visual.outer) << ","
                    << visual.sourceMix << ","
                    << visual.outerMix << ","
                    << visual.phase << ","
                    << frameTimingLabel_ << ","
                    << frameTimingBlock_ << "\n";
    frameTimingLog_.flush();
}

void Application::UpdateViewConstants()
{
    view_.sliceIndex = slice_;
    view_.sliceAxis = sliceAxis_;
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
            L"OGSR Облака V2 | %s | %s | pos %.0f %.0f %.0f | FOV %.0f | "
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
#if defined(OGSR_WEATHER_SHIPPING_CONTROLS)
    Log("Shipping-controls contract standalone harness:");
    Log("  mouse look, W/S/A/D/Q/E move, Shift faster");
    Log("  Z/X FOV, P pause, H/F1 help, Esc exit");
    Log("  C clouds on/off, F6 quality next, Shift+F6 previous");
    Log("  manual weather/debug controls are not registered");
    return;
#endif
    if (options_.mode == clouds::RenderMode::Clouds)
    {
        Log("Управление облаками (V1):");
        Log("  ЛКМ + мышь       обзор камерой");
        Log("  W/S/A/D/Q/E      движение; Shift — быстрее");
        Log("  колесо, Z/X      FOV");
        Log("  J/L, I/K         азимут и высота солнца");
        Log("  1 / 2 / 3        Clear / Overcast / Partly Cloudy (smooth transition)");
        Log("  F5 (+Shift)      next / previous weather preset");
        Log("  5 / 6 / 7 / 8    final / density / transmittance / step count");
        Log("  9 / 0 / V        occupancy / weather coverage / cloud type");
        Log("  D                density vertical slice");
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
    Log("=== V2 CLOUD SELFTEST (V1 regression + weather) ===");
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

    const uint32_t weatherSeed = weatherMap_.Seed();
    const std::vector<float> weatherFirst = weatherMap_.ReadbackRgba();
    const WeatherPixelStats weatherStats =
        AnalyzeWeatherMap(weatherFirst, weatherMap_.Resolution());
    const WeatherSeamMetrics seamMetrics =
        AnalyzeWeatherSeams(weatherFirst, weatherMap_.Resolution());
    weatherMap_.Generate(weatherSeed);
    const std::vector<float> weatherRepeat = weatherMap_.ReadbackRgba();
    const double weatherDeterminism =
        MaxAbsFloatDifference(weatherFirst, weatherRepeat);
    weatherMap_.Generate(weatherSeed + 1u);
    const std::vector<float> weatherOtherSeed = weatherMap_.ReadbackRgba();
    const double weatherSeedDifference =
        MeanAbsFloatDifference(weatherFirst, weatherOtherSeed);
    weatherMap_.Generate(weatherSeed);
    const WeatherMap::SamplerWrapProbe samplerProbe =
        weatherMap_.ProbeSamplerWrap();

    const bool weatherOk =
        weatherFirst.size() ==
            static_cast<size_t>(weatherMap_.Resolution()) *
                weatherMap_.Resolution() * 4 &&
        weatherStats.finite && weatherStats.inRange &&
        weatherStats.maximum[0] - weatherStats.minimum[0] > 0.15 &&
        weatherStats.maximum[1] - weatherStats.minimum[1] > 0.10 &&
        weatherStats.maximum[2] - weatherStats.minimum[2] > 0.10 &&
        weatherDeterminism <= 1e-6 &&
        weatherSeedDifference > 0.01 &&
        seamMetrics.ok &&
        samplerProbe.maxWrapDifference <= 5e-4f &&
        samplerProbe.maxRgbaDifference <= 5e-4f;

    // The generic selftest checks renderer features (coverage, sun, wind,
    // early exit), not the macro placement. A production macro field can
    // legitimately leave the probe camera in a gap, so the selftest uses a
    // dense continuous macro column: mass = 1 everywhere, detail 0.45.
    clouds::ReferenceControls selftestReference = options_.reference;
    if (selftestReference.fieldMode ==
        clouds::ReferenceFieldMode::NonTilingMacro)
    {
        selftestReference.macroThreshold = 0.0f;
        selftestReference.macroSoftness = 0.01f;
        selftestReference.macroDetailWeight =
            std::max(selftestReference.macroDetailWeight, 0.45f);
        selftestReference.macroEdgeJitter = 0.0f;
        selftestReference.macroHeightWeight = 0.0f;
    }

    const auto renderAtRef = [&](const clouds::CloudParams& p,
                                 const clouds::Camera& c,
                                 clouds::CloudDebugMode debug,
                                 float timeSeconds,
                                 const clouds::ReferenceControls& reference)
    {
        const clouds::CloudFrameConstants constants =
            clouds::BuildCloudConstants(c, p, width, height, timeSeconds, debug,
                                    options_.densityModel,
                                    options_.referenceStage,
                                    reference,
                                    options_.noise.seed);
        return cloudRayMarcher_.Readback(
            constants, noise_.SrvGpuHandle(), weatherMap_.SrvGpuHandle(),
            options_.cloudFormula, options_.densityModel,
            options_.referenceStage);
    };

    const auto renderAt = [&](const clouds::CloudParams& p,
                              const clouds::Camera& c,
                              clouds::CloudDebugMode debug,
                              float timeSeconds)
    {
        return renderAtRef(p, c, debug, timeSeconds, selftestReference);
    };

    const auto render = [&](const clouds::CloudParams& p,
                            const clouds::Camera& c,
                            clouds::CloudDebugMode debug)
    {
        return renderAt(p, c, debug, 0.0f);
    };

    const CloudReadback base = render(params, camera, clouds::CloudDebugMode::Final);
    const CloudPixelStats baseStats = AnalyzeCloudPixels(base);

    clouds::Camera weatherCamera = camera;
    weatherCamera.position.x += 5000.0f;
    const CloudReadback weatherNear =
        render(params, camera, clouds::CloudDebugMode::Weather);
    const CloudReadback weatherFar =
        render(params, weatherCamera, clouds::CloudDebugMode::Weather);
    const double weatherSpatialDifference =
        MeanAbsCloudDifference(weatherNear, weatherFar);

    clouds::Camera boundaryCamera = camera;
    boundaryCamera.position.x = 3900.0f;
    const CloudReadback weatherBoundaryBefore =
        render(params, boundaryCamera, clouds::CloudDebugMode::Weather);
    boundaryCamera.position.x = 4100.0f;
    const CloudReadback weatherBoundaryAfter =
        render(params, boundaryCamera, clouds::CloudDebugMode::Weather);
    const double weatherBoundaryPanDifference =
        MeanAbsCloudDifference(weatherBoundaryBefore, weatherBoundaryAfter);

    clouds::CloudParams overcastParams;
    clouds::ApplyOvercastPreset(overcastParams);
    const CloudReadback clearType =
        render(params, camera, clouds::CloudDebugMode::CloudType);
    const CloudReadback overcastType =
        render(overcastParams, camera, clouds::CloudDebugMode::CloudType);
    const double cloudTypeDifference =
        MeanAbsCloudDifference(clearType, overcastType);

    clouds::CloudParams lowCoverage = params;
    lowCoverage.coverage = 0.20f;
    clouds::CloudParams highCoverage = params;
    highCoverage.coverage = 0.72f;
    const CloudReadback lowOccupancy =
        render(lowCoverage, camera, clouds::CloudDebugMode::Occupancy);
    const CloudReadback highOccupancy =
        render(highCoverage, camera, clouds::CloudDebugMode::Occupancy);
    const CloudPixelStats lowOccupancyStats = AnalyzeCloudPixels(lowOccupancy);
    const CloudPixelStats highOccupancyStats = AnalyzeCloudPixels(highOccupancy);

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

    // V2.3A: sun path is clipped to the conservative slab and the distance
    // cap; the shadow density must match the primary density exactly.
    const CloudReadback sunDepth =
        render(params, camera, clouds::CloudDebugMode::SunOpticalDepth);
    const CloudReadback sunTransmittanceDebug =
        render(params, camera, clouds::CloudDebugMode::SunTransmittance);
    const CloudReadback sunPathDebug =
        render(params, camera, clouds::CloudDebugMode::ShadowPathLength);
    const CloudReadback shadowDifference =
        render(params, camera, clouds::CloudDebugMode::ShadowDensityDifference);
    const CloudPixelStats sunDepthStats = AnalyzeCloudPixels(sunDepth);
    const CloudPixelStats sunTransmittanceStats =
        AnalyzeCloudPixels(sunTransmittanceDebug);
    const CloudPixelStats sunPathStats = AnalyzeCloudPixels(sunPathDebug);
    const CloudPixelStats shadowDifferenceStats =
        AnalyzeCloudPixels(shadowDifference);

    clouds::CloudParams denseShadowParams = params;
    denseShadowParams.extinction *= 2.5f;
    const CloudReadback denseSunTransmittance = render(
        denseShadowParams, camera, clouds::CloudDebugMode::SunTransmittance);
    const CloudPixelStats denseSunTransmittanceStats =
        AnalyzeCloudPixels(denseSunTransmittance);

    clouds::CloudParams horizontalSunParams = params;
    horizontalSunParams.sunElevationDegrees = 0.5f;
    const CloudReadback horizontalSun =
        render(horizontalSunParams, camera, clouds::CloudDebugMode::Final);
    const CloudReadback horizontalSunTransmittance = render(
        horizontalSunParams, camera, clouds::CloudDebugMode::SunTransmittance);
    const CloudPixelStats horizontalSunStats =
        AnalyzeCloudPixels(horizontalSun);
    const CloudPixelStats horizontalSunTransmittanceStats =
        AnalyzeCloudPixels(horizontalSunTransmittance);

    // V23-A03: a sun below the horizon must remove direct and multiple
    // scattering; ambient skylight stays independent.
    clouds::CloudParams sunOnlyBelowParams = params;
    sunOnlyBelowParams.sunElevationDegrees = -10.0f;
    sunOnlyBelowParams.lightingMode = clouds::CloudLightingMode::SunOnly;
    sunOnlyBelowParams.atmosphereEnabled = false;
    const CloudReadback sunOnlyBelow =
        render(sunOnlyBelowParams, camera, clouds::CloudDebugMode::Final);
    const CloudPixelStats sunOnlyBelowStats = AnalyzeCloudPixels(sunOnlyBelow);

    clouds::CloudParams sunOnlyAboveParams = params;
    sunOnlyAboveParams.sunElevationDegrees = 5.0f;
    sunOnlyAboveParams.lightingMode = clouds::CloudLightingMode::SunOnly;
    sunOnlyAboveParams.atmosphereEnabled = false;
    const CloudReadback sunOnlyAbove =
        render(sunOnlyAboveParams, camera, clouds::CloudDebugMode::Final);
    const CloudPixelStats sunOnlyAboveStats = AnalyzeCloudPixels(sunOnlyAbove);

    clouds::CloudParams ambientBelowParams = params;
    ambientBelowParams.sunElevationDegrees = -10.0f;
    ambientBelowParams.lightingMode = clouds::CloudLightingMode::AmbientOnly;
    ambientBelowParams.atmosphereEnabled = false;
    const CloudReadback ambientBelow =
        render(ambientBelowParams, camera, clouds::CloudDebugMode::Final);
    const CloudPixelStats ambientBelowStats = AnalyzeCloudPixels(ambientBelow);

    const bool sunBelowHorizonOk =
        sunOnlyBelowStats.finite && sunOnlyBelowStats.maxRgb < 1e-4f &&
        sunOnlyAboveStats.finite && sunOnlyAboveStats.meanRgb > 1e-3f &&
        ambientBelowStats.finite && ambientBelowStats.meanRgb > 1e-3f;

    clouds::CloudParams zeroCoverageParams = params;
    zeroCoverageParams.coverage = 0.0f;
    const CloudReadback zeroCoverageDepth = render(
        zeroCoverageParams, camera, clouds::CloudDebugMode::SunOpticalDepth);
    const CloudPixelStats zeroCoverageDepthStats =
        AnalyzeCloudPixels(zeroCoverageDepth);

    const bool opticalDepthOk =
        sunDepthStats.finite && sunTransmittanceStats.finite &&
        sunPathStats.finite && sunDepthStats.meanRgb > 1e-3 &&
        sunTransmittanceStats.meanRgb < 0.999 &&
        denseSunTransmittanceStats.meanRgb <
            sunTransmittanceStats.meanRgb - 1e-3 &&
        sunPathStats.maxRgb <= 1.0001f &&
        zeroCoverageDepthStats.maxRgb < 1e-5f;
    const bool shadowConsistencyOk =
        shadowDifferenceStats.finite && shadowDifferenceStats.maxRgb < 1e-3f;
    const bool horizontalSunOk = horizontalSunStats.finite &&
                                 horizontalSunTransmittanceStats.finite &&
                                 horizontalSunTransmittanceStats.maxRgb <=
                                     1.0001f;

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
    // A macro field can legitimately leave this camera in a gap. The early
    // exit is a renderer feature, so exercise it with a dense macro column
    // (mass = 1 everywhere) rather than assuming a full legacy deck.
    clouds::ReferenceControls earlyExitReference = options_.reference;
    if (earlyExitReference.fieldMode ==
        clouds::ReferenceFieldMode::NonTilingMacro)
    {
        earlyExitReference.macroThreshold = 0.0f;
        earlyExitReference.macroSoftness = 0.01f;
        earlyExitReference.macroDetailWeight = 0.0f;
        earlyExitReference.macroEdgeJitter = 0.0f;
        earlyExitReference.macroHeightWeight = 0.0f;
    }
    const CloudReadback earlyExitStep = renderAtRef(
        earlyExitParams, earlyCamera, clouds::CloudDebugMode::StepCount,
        0.0f, earlyExitReference);
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

    clouds::Camera aboveCamera = camera;
    aboveCamera.position = DirectX::XMFLOAT3(0.0f, 2600.0f, -1200.0f);
    aboveCamera.pitchDegrees = -12.0f;
    const CloudReadback above =
        render(params, aboveCamera, clouds::CloudDebugMode::Final);
    const CloudPixelStats aboveStats = AnalyzeCloudPixels(above);

    clouds::Camera largeWorldCamera = camera;
    largeWorldCamera.position = DirectX::XMFLOAT3(200000.0f, 500.0f, 150000.0f);
    const CloudReadback largeWorld =
        render(params, largeWorldCamera, clouds::CloudDebugMode::Final);
    const CloudPixelStats largeWorldStats = AnalyzeCloudPixels(largeWorld);

    clouds::CloudParams extremeBounds = params;
    extremeBounds.weatherHeightVariation = 1200.0f;
    extremeBounds.layerBottom = 600.0f;
    extremeBounds.layerTop = 1600.0f;
    clouds::Camera extremeCamera = camera;
    extremeCamera.position = DirectX::XMFLOAT3(0.0f, 150.0f, -1200.0f);
    extremeCamera.pitchDegrees = 14.0f;
    const CloudReadback extreme =
        render(extremeBounds, extremeCamera, clouds::CloudDebugMode::Final);
    const CloudPixelStats extremeStats = AnalyzeCloudPixels(extreme);

    const bool finiteOk = baseStats.finite && emptyStats.finite &&
                          denseStats.finite && transmittanceStats.finite &&
                          insideStats.finite && windStats.finite &&
                          oppositeWindStats.finite && stepStats.finite &&
                          occupancyStats.finite && earlyExitStats.finite &&
                          lowOccupancyStats.finite &&
                          highOccupancyStats.finite && aboveStats.finite &&
                          largeWorldStats.finite && extremeStats.finite &&
                          sunDepthStats.finite &&
                          sunTransmittanceStats.finite && sunPathStats.finite &&
                          shadowDifferenceStats.finite &&
                          denseSunTransmittanceStats.finite &&
                          horizontalSunStats.finite &&
                          horizontalSunTransmittanceStats.finite &&
                          sunOnlyBelowStats.finite && sunOnlyAboveStats.finite &&
                          ambientBelowStats.finite &&
                          zeroCoverageDepthStats.finite;
    const bool weatherSpatialOk = weatherSpatialDifference > 0.01;
    const bool weatherBoundaryPanOk = weatherBoundaryPanDifference > 0.01;
    const bool cloudTypeOk = cloudTypeDifference > 0.01;
    const bool coverageOk =
        highOccupancyStats.meanRgb > lowOccupancyStats.meanRgb + 0.01;
    const bool cameraVolumesOk =
        aboveStats.finite && aboveStats.meanTransmittance < 0.999 &&
        aboveStats.meanRgb > 1e-4 && largeWorldStats.finite &&
        largeWorldStats.meanTransmittance < 1.0 && extremeStats.finite &&
        extremeStats.meanTransmittance < 0.999;

    clouds::CloudParams transitionFrom;
    clouds::ApplyClearPreset(transitionFrom);
    clouds::CloudParams transitionTo;
    clouds::ApplyOvercastPreset(transitionTo);
    const clouds::CloudParams transitionStart =
        clouds::LerpCloudParams(transitionFrom, transitionTo, 0.0f);
    const clouds::CloudParams transitionMid =
        clouds::LerpCloudParams(transitionFrom, transitionTo, 0.5f);
    const clouds::CloudParams transitionEnd =
        clouds::LerpCloudParams(transitionFrom, transitionTo, 1.0f);
    const bool transitionOk =
        std::fabs(transitionStart.coverage - transitionFrom.coverage) < 1e-5f &&
        std::fabs(transitionEnd.coverage - transitionTo.coverage) < 1e-5f &&
        transitionMid.coverage > transitionFrom.coverage &&
        transitionMid.coverage < transitionTo.coverage &&
        std::fabs(transitionStart.weatherStorminess -
                  transitionFrom.weatherStorminess) < 1e-5f &&
        std::fabs(transitionEnd.weatherStorminess -
                  transitionTo.weatherStorminess) < 1e-5f;
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
    // WARP uses a different transcendental path; the V2.3 lighting adds more
    // nonlinear operations, so the software adapter gets a slightly wider
    // tolerance while hardware keeps the V1/V2 threshold.
    const bool softwareAdapter =
        std::string(renderer_.AdapterInfo()).find("software") !=
        std::string::npos;
    const double windTolerance = softwareAdapter ? 1.2e-2 : 8e-3;
    const bool windOk = windStats.finite && windDifference <= windTolerance;
    const bool zeroWindOk = zeroWindDifference <= 1e-5;
    const bool oppositeWindOk = oppositeWindStats.finite &&
                                oppositeWindDifference <= windTolerance;
    const bool debugModeOk = transmittanceStats.finite &&
                             transmittanceStats.meanTransmittance > 0.999;

    const bool pass = finiteOk && cloudPresent && emptyOk && extinctionOk &&
                      determinismOk && cameraOk && sunOk && windOk &&
                      zeroWindOk && oppositeWindOk && stepCountOk &&
                      debugModeOk && weatherOk && weatherSpatialOk &&
                      weatherBoundaryPanOk && cloudTypeOk && coverageOk &&
                      cameraVolumesOk && transitionOk && opticalDepthOk &&
                      shadowConsistencyOk && horizontalSunOk &&
                      sunBelowHorizonOk;

    report << "\nV2 CLOUD SELFTEST (" << width << "x" << height << ")\n";
    report << "Formula requested: "
           << clouds::CloudFormulaName(options_.cloudFormula) << "\n";
    report << "Formula executed: "
           << clouds::CloudFormulaName(base.formula) << "\n";
    report << "Shader variant: " << base.shaderVariant << "\n";
    report << "Density model requested: "
           << clouds::DensityModelName(options_.densityModel) << "\n";
    report << "Density model executed: "
           << clouds::DensityModelName(base.densityModel) << "\n";
    report << "Reference stage: "
           << clouds::ReferenceStageName(base.referenceStage) << "\n";
    report << "Reference field: "
           << clouds::ReferenceFieldModeName(options_.reference.fieldMode)
           << "\n";
    report << "Render PSO: " << base.renderPso << "\n";
    report << "Probe PSO: " << base.probePso << "\n";
    report << "GPU adapter: " << renderer_.AdapterInfo() << "\n";
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
    report << "above-layer downward mean transmittance: "
           << aboveStats.meanTransmittance << "; mean rgb: "
           << aboveStats.meanRgb << "\n";
    report << "large-world camera mean transmittance: "
           << largeWorldStats.meanTransmittance << "; mean rgb: "
           << largeWorldStats.meanRgb << "\n";
    report << "extreme height-variation mean transmittance: "
           << extremeStats.meanTransmittance << "; mean rgb: "
           << extremeStats.meanRgb << "\n";
    report << "determinism mean abs diff: " << determinism
           << " (threshold 1e-5)\n";
    report << "camera movement mean abs diff: " << cameraDifference
           << " (threshold 1e-4)\n";
    report << "sun movement mean abs diff: " << sunDifference
           << " (threshold 1e-4)\n";
    report << "sun optical depth mean: " << sunDepthStats.meanRgb
           << "; sun transmittance mean: " << sunTransmittanceStats.meanRgb
           << " (dense extinction: " << denseSunTransmittanceStats.meanRgb
           << ")\n";
    report << "sun shadow path max (normalized): " << sunPathStats.maxRgb
           << " (threshold 1.0)\n";
    report << "shadow/primary density max difference: "
           << (shadowDifferenceStats.maxRgb / 1000.0) << " (threshold 1e-6)\n";
    report << "horizontal sun transmittance max: "
           << horizontalSunTransmittanceStats.maxRgb << " (threshold 1.0)\n";
    report << "sun-only below horizon max rgb: "
           << sunOnlyBelowStats.maxRgb << " (threshold 1e-4); sun-only 5 deg "
           << "mean rgb: " << sunOnlyAboveStats.meanRgb
           << "; ambient-only below horizon mean rgb: "
           << ambientBelowStats.meanRgb << "\n";
    report << "wind translation max abs diff: " << windDifference
           << " (threshold " << windTolerance << ")\n";
    report << "zero wind max abs diff: " << zeroWindDifference
           << " (threshold 1e-5)\n";
    report << "opposite wind translation max abs diff: "
           << oppositeWindDifference << " (threshold " << windTolerance
           << ")\n";
    report << "weather map mean R/G/B/A: "
           << weatherStats.mean[0] << "/" << weatherStats.mean[1] << "/"
           << weatherStats.mean[2] << "/" << weatherStats.mean[3] << "\n";
    report << "weather map R range: " << weatherStats.minimum[0] << ".."
           << weatherStats.maximum[0] << "; G range: "
           << weatherStats.minimum[1] << ".." << weatherStats.maximum[1]
           << "\n";
    report << "weather map determinism max abs diff: "
           << weatherDeterminism << " (threshold 1e-6); seed sensitivity mean "
           << weatherSeedDifference << " (threshold 0.01)\n";
    report << "weather map seam check: "
           << (WeatherMapSeamOk(weatherFirst, weatherMap_.Resolution())
                   ? "PASS" : "FAIL") << "\n";
    report << "weather sampler wrap probe max RGBA diff: "
           << samplerProbe.maxWrapDifference << " (threshold 5e-4)\n";
    report << "weather world-space camera diff: " << weatherSpatialDifference
           << " (threshold 0.01)\n";
    report << "weather boundary pan diff: " << weatherBoundaryPanDifference
           << " (threshold 0.01)\n";
    for (uint32_t channel = 0; channel < 4; ++channel)
    {
        report << "weather seam RGBA[" << channel << "]: X seam "
               << seamMetrics.seamX[channel] << " neighbor "
               << seamMetrics.neighborX[channel] << " ratio "
               << seamMetrics.gradientRatioX[channel] << "; Y seam "
               << seamMetrics.seamY[channel] << " neighbor "
               << seamMetrics.neighborY[channel] << " ratio "
               << seamMetrics.gradientRatioY[channel] << "\n";
    }
    report << "cloud-type image diff: " << cloudTypeDifference
           << " (threshold 0.01)\n";
    report << "occupancy low/high coverage mean: "
           << lowOccupancyStats.meanRgb << "/"
           << highOccupancyStats.meanRgb << "\n";
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
    report << (weatherOk ? "[PASS]" : "[FAIL]")
           << " weather map is finite, in range, deterministic and seed-sensitive\n";
    report << (weatherSpatialOk ? "[PASS]" : "[FAIL]")
           << " weather sampling is world-space (camera movement changes it)\n";
    report << (weatherBoundaryPanOk ? "[PASS]" : "[FAIL]")
           << " weather camera pan across world tile boundary is stable\n";
    report << (cloudTypeOk ? "[PASS]" : "[FAIL]")
           << " cloud type changes the weather-to-type mapping\n";
    report << (coverageOk ? "[PASS]" : "[FAIL]")
           << " increasing coverage increases occupied ray-march steps\n";
    report << (cameraVolumesOk ? "[PASS]" : "[FAIL]")
           << " above-layer and large-world cameras render finite cloud volume\n";
    report << (transitionOk ? "[PASS]" : "[FAIL]")
           << " weather parameter interpolation has matching endpoints\n";
    report << (opticalDepthOk ? "[PASS]" : "[FAIL]")
           << " sun optical depth is bounded, monotonic and zero without density\n";
    report << (shadowConsistencyOk ? "[PASS]" : "[FAIL]")
           << " shadow density matches primary density exactly\n";
    report << (horizontalSunOk ? "[PASS]" : "[FAIL]")
           << " near-horizontal sun stays finite and bounded\n";
    report << (sunBelowHorizonOk ? "[PASS]" : "[FAIL]")
           << " sun below horizon removes direct and multiple scattering\n";
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

    const audit::ValidationStatus initialValidation =
        audit::EvaluateValidationStatus(
            renderer_.DebugLayerRequested(), renderer_.DebugLayerEnabled(),
            renderer_.InfoQueueAvailable(),
            renderer_.GpuValidationRequested(),
            renderer_.GpuValidationEnabled(), renderer_.DebugErrorCount());
    const bool initialValidationOk =
        initialValidation.verdict == audit::ValidationVerdict::Pass ||
        initialValidation.verdict == audit::ValidationVerdict::NotRequested;
    const bool initialInconclusive =
        initialValidation.verdict == audit::ValidationVerdict::Inconclusive;
    const bool resolutionOk = analysis.resolution == resolution;
    const bool determinismOk = determinism <= 1e-6;
    const bool seedOk = seedDifference > 1e-3;
    const bool probeOk = probeMaximum <= 1e-4 && probe.negativeControl >= 1e-3;
    const bool samplerOk = samplerProbe.maxFilterError <= 1e-2 &&
                           samplerProbe.maxWrapDifference <= 1e-2 &&
                           samplerProbe.maxClampDifference >= 1e-3;
    const bool v0Pass = analysis.valuesInRange && analysis.nonConstant &&
                        analysis.periodic && resolutionOk && determinismOk &&
                        seedOk && probeOk && samplerOk;

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
    report << "initial validation performed: "
           << (initialValidation.performed ? "yes" : "no") << "\n";
    report << "initial validation verdict: "
           << ValidationVerdictName(initialValidation.verdict) << "\n";
    report << "initial debug errors: " << renderer_.DebugErrorCount() << "\n";
    report << "initial debug warnings: " << renderer_.DebugWarningCount()
           << "\n\n";
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
    report << (initialValidationOk ? "[PASS]"
               : (initialInconclusive ? "[INCONCLUSIVE]" : "[FAIL]"))
           << " initial validation infrastructure and debug errors"
           << (initialValidation.verdict == audit::ValidationVerdict::NotRequested
                   ? " (validation not requested)" : "") << "\n\n";

    const bool cloudOk = RunCloudSelfTest(report) == 0;

    // V23-A02: cloud GPU work can emit validation errors after the initial
    // check. Wait for the GPU, drain the info queue, and recompute the verdict
    // from the accumulated messages before declaring PASS.
    if (!renderer_.TryWaitForGpu())
    {
        TerminalFailClosed(
            "selftest: GPU completion is UNKNOWN after cloud GPU work; "
            "TERMINAL fail-closed before reporting or releasing resources");
    }
    if (options_.injectDebugError)
    {
        if (renderer_.InjectDebugError())
            Log("[test] injected one D3D12 debug ERROR after the cloud selftest");
        else
            LogWarn("[test] could not inject a debug ERROR (info queue unavailable)");
    }
    renderer_.PollDebugMessages("selftest final");

    const audit::ValidationStatus finalValidation =
        audit::EvaluateValidationStatus(
            renderer_.DebugLayerRequested(), renderer_.DebugLayerEnabled(),
            renderer_.InfoQueueAvailable(),
            renderer_.GpuValidationRequested(),
            renderer_.GpuValidationEnabled(), renderer_.DebugErrorCount());
    const bool finalValidationOk =
        finalValidation.verdict == audit::ValidationVerdict::Pass ||
        finalValidation.verdict == audit::ValidationVerdict::NotRequested;
    const bool finalInconclusive =
        finalValidation.verdict == audit::ValidationVerdict::Inconclusive;
    const bool overallPass = v0Pass && cloudOk && finalValidationOk;

    report << "\nfinal validation after cloud GPU work\n";
    report << "final debug errors: " << renderer_.DebugErrorCount() << "\n";
    report << "final debug warnings: " << renderer_.DebugWarningCount()
           << "\n";
    report << "final validation verdict: "
           << ValidationVerdictName(finalValidation.verdict) << "\n";
    report << (finalValidationOk ? "[PASS]"
               : (finalInconclusive ? "[INCONCLUSIVE]" : "[FAIL]"))
           << " final validation infrastructure and debug errors\n\n";

    report << (v0Pass ? "V0 SELFTEST: PASS\n"
                      : "V0 SELFTEST: FAIL\n");
    report << (cloudOk ? "V2 CLOUD SELFTEST: PASS\n"
                       : "V2 CLOUD SELFTEST: FAIL\n");
    report << (overallPass ? "OVERALL SELFTEST: PASS\n"
                           : (finalInconclusive
                                  ? "OVERALL SELFTEST: INCONCLUSIVE\n"
                                  : "OVERALL SELFTEST: FAIL\n"));

    Log("%s", report.str().c_str());
    const std::filesystem::path reportPath = ExecutableDirectory() / "selftest.txt";
    std::ofstream file(reportPath, std::ios::trunc);
    if (file)
        file << report.str();
    else
        LogWarn("could not write %s", reportPath.string().c_str());

    return overallPass ? 0 : (finalInconclusive ? 3 : 1);
}

int Application::RunNoiseSliceCapture(const std::filesystem::path& directory)
{
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec)
        Fail("cannot create noise slice directory: " + ec.message());
    const auto absoluteDirectory = std::filesystem::absolute(directory);
    const uint32_t resolution = noise_.Resolution();
    const std::vector<float> rgba = noise_.ReadbackRgba();
    if (rgba.size() != static_cast<size_t>(resolution) * resolution *
                             resolution * 4)
        Fail("noise slice readback has an unexpected size");

    uint32_t slice = options_.noiseSliceIndex;
    if (slice == 0)
        slice = resolution / 2;
    slice = std::min(slice, resolution - 1);
    const char* axes[3] = { "xy", "xz", "yz" };
    const char* channels[3] = { "perlin", "worley", "combined" };
    std::vector<uint8_t> pixels(static_cast<size_t>(resolution) * resolution * 4);
    std::vector<std::string> files;

    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            for (uint32_t y = 0; y < resolution; ++y)
            {
                for (uint32_t x = 0; x < resolution; ++x)
                {
                    uint32_t sx = x;
                    uint32_t sy = y;
                    uint32_t sz = slice;
                    if (axis == 1)      // XZ: fixed Y
                    {
                        sx = x;
                        sy = slice;
                        sz = y;
                    }
                    else if (axis == 2) // YZ: fixed X
                    {
                        sx = slice;
                        sy = x;
                        sz = y;
                    }
                    const size_t source =
                        ((static_cast<size_t>(sz) * resolution + sy) *
                             resolution + sx) * 4 + channel;
                    const uint8_t value = static_cast<uint8_t>(std::clamp(
                        rgba[source], 0.0f, 1.0f) * 255.0f + 0.5f);
                    const size_t destination =
                        (static_cast<size_t>(y) * resolution + x) * 4;
                    pixels[destination + 0] = value;
                    pixels[destination + 1] = value;
                    pixels[destination + 2] = value;
                    pixels[destination + 3] = 255;
                }
            }

            char filename[96]{};
            snprintf(filename, sizeof(filename), "noise_%s_%s_%03u.bmp",
                     axes[axis], channels[channel], slice);
            const auto path = absoluteDirectory / filename;
            if (!WriteBmp32(path, resolution, resolution, pixels.data()))
                Fail("failed to write " + path.string());
            files.emplace_back(filename);
            Log("captured noise slice %s axis=%s channel=%s slice=%u",
                filename, axes[axis], channels[channel], slice);
        }
    }

    std::ostringstream info;
    info << "OGSR NoiseVolume slice diagnostics\n";
    info << "timestamp: " << TimestampUtc() << "\n";
    info << "adapter: " << renderer_.AdapterInfo() << "\n";
    info << "seed: " << noise_.Params().seed << "\n";
    info << "resolution: " << resolution << "^3\n";
    info << "slice index: " << slice << "\n";
    info << "files:\n";
    for (const auto& file : files)
        info << "  " << file << "\n";
    const auto infoPath = absoluteDirectory / "slice_info.txt";
    std::ofstream infoFile(infoPath, std::ios::trunc);
    if (infoFile)
        infoFile << info.str();
    Log("noise slice capture complete: %zu images in %s", files.size(),
        WideToUtf8(absoluteDirectory.wstring()).c_str());
    return 0;
}

int Application::RunCloudSequence(const std::filesystem::path& sequenceDir)
{
    std::error_code ec;
    std::filesystem::create_directories(sequenceDir, ec);
    if (ec)
        Fail("cannot create sequence directory: " + ec.message());
    const auto absoluteDirectory = std::filesystem::absolute(sequenceDir);

    if (options_.windDisabled)
        options_.cloud.windSpeed = 0.0f;
    if (options_.cameraExplicit)
        camera_.position = options_.cameraPosition;
    if (options_.cameraYawExplicit)
        camera_.yawDegrees = options_.cameraYawDegrees;
    if (options_.cameraPitchExplicit)
        camera_.pitchDegrees = options_.cameraPitchDegrees;

    const uint32_t frameCount = options_.frames > 0 ? options_.frames : 60;
    if (frameCount > 600)
        Fail("sequence frame count must be <= 600");
    if (weatherTimelineActive_)
        weatherTimelineTime_ = clouds::WeatherTimelineClampTime(
            options_.weatherTimeline, options_.weatherTimelineTime);

    const std::string& motion = options_.motionName;
    float yawPerFrame = 0.0f;
    float upPerFrame = 0.0f;
    float forwardPerFrame = 0.0f;
    if (motion == "pan-slow")
        yawPerFrame = 0.25f;
    else if (motion == "pan-fast")
        yawPerFrame = 2.0f;
    else if (motion == "up")
        upPerFrame = 8.0f;
    else if (motion == "fly" || motion == "fly-level")
        forwardPerFrame = 25.0f;
    else if (motion != "static")
        Fail("unknown sequence motion: " + motion);
    const bool levelFlight = motion == "fly-level";

    window_.Show(SW_SHOW);
    UpdateWindow(window_.Handle());
    window_.PumpMessages();

    const uint32_t width = renderer_.Width();
    const uint32_t height = renderer_.Height();
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> previous;
    std::ostringstream metrics;
    double hfSum = 0.0;
    double frameDiffSum = 0.0;
    uint32_t frameDiffCount = 0;
    const float dt = 1.0f / 60.0f;

    const auto luma = [](const uint8_t* p)
    {
        return 0.2126f * static_cast<float>(p[0]) +
               0.7152f * static_cast<float>(p[1]) +
               0.0722f * static_cast<float>(p[2]);
    };

    for (uint32_t frame = 0; frame < frameCount; ++frame)
    {
        if (yawPerFrame != 0.0f)
            camera_.yawDegrees += yawPerFrame;
        if (upPerFrame != 0.0f)
            camera_.position.y += upPerFrame;
        if (forwardPerFrame != 0.0f)
        {
            DirectX::XMFLOAT3 forward = camera_.Forward();
            if (levelFlight)
            {
                forward.y = 0.0f;
                forward = clouds::Normalize(forward);
            }
            camera_.position.x += forward.x * forwardPerFrame;
            camera_.position.y += forward.y * forwardPerFrame;
            camera_.position.z += forward.z * forwardPerFrame;
        }
        if (!options_.holdCloudTime)
            cloudTime_ += dt;
        if (weatherTimelineActive_)
            UpdateWeatherTimeline(dt);

        if (!RenderOneFrame(true, &pixels))
            Fail("sequence render failed");

        char name[64]{};
        snprintf(name, sizeof(name), "frame_%04u.bmp", frame);
        const auto path = absoluteDirectory / name;
        if (!WriteBmp32(path, width, height, pixels.data()))
            Fail("failed to write " + path.string());

        // High-frequency energy is a proxy for grain/shimmer: it measures how
        // far each pixel sits from its four-neighbour average. Known camera
        // motion does not create this signal, sampling noise does.
        double hf = 0.0;
        uint64_t hfCount = 0;
        const size_t rowPitch = static_cast<size_t>(width) * 4;
        for (uint32_t y = 1; y + 1 < height; ++y)
        {
            for (uint32_t x = 1; x + 1 < width; ++x)
            {
                const size_t i = static_cast<size_t>(y) * rowPitch +
                                 static_cast<size_t>(x) * 4;
                const float center = luma(&pixels[i]);
                const float neighbours = 0.25f * (
                    luma(&pixels[i - 4]) + luma(&pixels[i + 4]) +
                    luma(&pixels[i - rowPitch]) +
                    luma(&pixels[i + rowPitch]));
                hf += std::fabs(center - neighbours);
                ++hfCount;
            }
        }
        hf = hfCount > 0 ? hf / static_cast<double>(hfCount) : 0.0;
        hfSum += hf;

        double diff = 0.0;
        if (!previous.empty() && previous.size() == pixels.size())
        {
            double sum = 0.0;
            for (size_t i = 0; i < pixels.size(); ++i)
                sum += std::fabs(static_cast<double>(pixels[i]) -
                                 static_cast<double>(previous[i]));
            diff = sum / static_cast<double>(pixels.size());
            frameDiffSum += diff;
            ++frameDiffCount;
        }
        previous = pixels;

        metrics << "frame " << frame << " hf " << hf << " diff " << diff
                << " camera " << camera_.position.x << " "
                << camera_.position.y << " " << camera_.position.z
                << " yaw " << camera_.yawDegrees << "\n";
        Log("sequence frame %u/%u hf=%.4f diff=%.4f", frame + 1, frameCount,
            hf, diff);
    }

    std::ostringstream summary;
    summary << "OGSR V2.3 cloud sequence\n";
    summary << "timestamp: " << TimestampUtc() << "\n";
    summary << "adapter: " << renderer_.AdapterInfo() << "\n";
    summary << "preset: " << cloudPreset_ << "\n";
    summary << "motion: " << motion << "\n";
    summary << "wind: " << (options_.windDisabled ? "off" : "on") << "\n";
    summary << "jitter: " << clouds::JitterModeName(options_.cloud.jitterMode)
            << "\n";
    summary << "lighting: "
            << clouds::LightingModeName(options_.cloud.lightingMode) << "\n";
    summary << "atmosphere: "
            << (options_.cloud.atmosphereEnabled ? "on" : "off") << "\n";
    summary << "steps: " << options_.cloud.stepCount << "\n";
    summary << "shadow steps: " << options_.cloud.shadowStepCount << "\n";
    summary << "frames: " << frameCount << "\n";
    summary << "mean high-frequency energy: " << (hfSum / frameCount) << "\n";
    summary << "mean frame-to-frame diff: "
            << (frameDiffCount ? frameDiffSum / frameDiffCount : 0.0) << "\n";
    summary << "\nper frame:\n" << metrics.str();

    std::ofstream file(absoluteDirectory / "sequence_metrics.txt",
                       std::ios::trunc);
    if (file)
        file << summary.str();
    Log("sequence complete: %u frames in %s", frameCount,
        WideToUtf8(absoluteDirectory.wstring()).c_str());
    return 0;
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
        { "clouds_partly_horizon", "partly", { 0.0f, 500.0f, -1200.0f }, 0.0f, 9.0f,
          clouds::CloudDebugMode::Final },
        { "clouds_partly_upward", "partly", { 200.0f, 200.0f, -900.0f }, 0.0f, 45.0f,
          clouds::CloudDebugMode::Final },
        { "clouds_overcast_horizon", "overcast", { 0.0f, 250.0f, -1200.0f }, 0.0f,
          7.0f, clouds::CloudDebugMode::Final },
        { "clouds_overcast_upward", "overcast", { 200.0f, 200.0f, -700.0f }, 0.0f,
          45.0f, clouds::CloudDebugMode::Final },
        { "clouds_overcast_aerial", "overcast", { 0.0f, 3000.0f, -1200.0f }, 0.0f,
          -12.0f, clouds::CloudDebugMode::Final },
        { "clouds_clear_aerial", "clear", { 0.0f, 3000.0f, -1200.0f }, 0.0f,
          -10.0f, clouds::CloudDebugMode::Final },
        { "clouds_cumulus_below", "clear", { 0.0f, 100.0f, -1200.0f }, 0.0f,
          20.0f, clouds::CloudDebugMode::Final },
        { "clouds_cumulus_side", "clear", { 1500.0f, 700.0f, -1500.0f }, -60.0f,
          5.0f, clouds::CloudDebugMode::Final },
        { "clouds_stratocumulus_below", "partly", { 0.0f, 100.0f, -1200.0f },
          0.0f, 20.0f, clouds::CloudDebugMode::Final },
        { "clouds_stratocumulus_side", "partly",
          { 1500.0f, 700.0f, -1500.0f }, -60.0f, 5.0f,
          clouds::CloudDebugMode::Final },
        { "clouds_overcast_base", "overcast", { 0.0f, 100.0f, -1200.0f }, 0.0f,
          25.0f, clouds::CloudDebugMode::Final },
        { "clouds_storm_base", "storm", { 0.0f, 100.0f, -1200.0f }, 0.0f,
          20.0f, clouds::CloudDebugMode::Final },
        { "clouds_extreme_height", "clear", { 0.0f, 100.0f, -1200.0f }, 0.0f,
          20.0f, clouds::CloudDebugMode::Final },
        { "clouds_storm_aerial", "storm", { 0.0f, 3200.0f, -1200.0f }, 0.0f,
          -15.0f, clouds::CloudDebugMode::Final },
        { "clouds_rain_horizon", "rain", { 0.0f, 150.0f, -1200.0f }, 0.0f, 8.0f,
          clouds::CloudDebugMode::Final },
        { "clouds_storm_horizon", "storm", { 0.0f, 150.0f, -1200.0f }, 0.0f, 8.0f,
          clouds::CloudDebugMode::Final },
        { "clouds_sunset_horizon", "sunset", { 0.0f, 500.0f, -1200.0f }, 0.0f, 8.0f,
          clouds::CloudDebugMode::Final },
        { "clouds_weather_debug", "clear", { 0.0f, 500.0f, -1200.0f }, 0.0f, 9.0f,
          clouds::CloudDebugMode::Weather },
        { "clouds_cloud_type_debug", "partly", { 0.0f, 500.0f, -1200.0f }, 0.0f, 9.0f,
          clouds::CloudDebugMode::CloudType },
        { "clouds_density_debug", "clear", { 0.0f, 500.0f, -1200.0f }, 0.0f, 9.0f,
          clouds::CloudDebugMode::Density },
        { "clouds_overcast_density_debug", "overcast", { 0.0f, 250.0f, -1200.0f },
          0.0f, 7.0f, clouds::CloudDebugMode::Density },
        { "clouds_overcast_transmittance_debug", "overcast",
          { 0.0f, 250.0f, -1200.0f }, 0.0f, 7.0f,
          clouds::CloudDebugMode::Transmittance },
        { "clouds_density_slice_clear", "clear", { 0.0f, 500.0f, -1200.0f },
          0.0f, 9.0f, clouds::CloudDebugMode::DensitySlice },
        { "clouds_density_slice_overcast", "overcast",
          { 0.0f, 250.0f, -1200.0f }, 0.0f, 7.0f,
          clouds::CloudDebugMode::DensitySlice },
        { "clouds_density_slice_storm", "storm",
          { 0.0f, 150.0f, -1200.0f }, 0.0f, 8.0f,
          clouds::CloudDebugMode::DensitySlice },
        { "clouds_transmittance_debug", "clear", { 0.0f, 500.0f, -1200.0f }, 0.0f,
          9.0f, clouds::CloudDebugMode::Transmittance },
        { "clouds_step_count_debug", "overcast", { 0.0f, 400.0f, -1200.0f }, 0.0f,
          7.0f, clouds::CloudDebugMode::StepCount },
        { "clouds_occupancy_debug", "overcast", { 0.0f, 400.0f, -1200.0f }, 0.0f,
          7.0f, clouds::CloudDebugMode::Occupancy },
        { "clouds_camera_moved", "overcast", { 900.0f, 400.0f, -1500.0f }, 35.0f,
          12.0f, clouds::CloudDebugMode::Final },
        { "clouds_sun_depth_debug", "overcast", { 0.0f, 100.0f, -1200.0f }, 0.0f,
          25.0f, clouds::CloudDebugMode::SunOpticalDepth },
        { "clouds_sun_transmittance_debug", "overcast",
          { 0.0f, 100.0f, -1200.0f }, 0.0f, 25.0f,
          clouds::CloudDebugMode::SunTransmittance },
        { "clouds_shadow_diff_debug", "clear", { 0.0f, 100.0f, -1200.0f },
          0.0f, 20.0f, clouds::CloudDebugMode::ShadowDensityDifference },
        { "clouds_shadow_path_debug", "clear", { 0.0f, 100.0f, -1200.0f },
          0.0f, 20.0f, clouds::CloudDebugMode::ShadowPathLength },
        { "clouds_atmosphere_debug", "clear", { 0.0f, 500.0f, -1200.0f },
          0.0f, 9.0f, clouds::CloudDebugMode::AtmosphereTransmittance },
    };

    const clouds::CloudParams originalParams = options_.cloud;
    const float originalTime = cloudTime_;
    const uint32_t cliSteps = options_.cloud.stepCount;
    const float cliCoverage = options_.cloud.coverage;
    const float cliDensity = options_.cloud.density;
    const float cliVerticalNoise = options_.cloud.verticalNoiseScale;
    const uint32_t cliShadowSteps = options_.cloud.shadowStepCount;
    const float cliSunShadowDistance = options_.cloud.sunShadowDistance;
    const clouds::CloudLightingMode cliLighting = options_.cloud.lightingMode;
    const clouds::CloudJitterMode cliJitter = options_.cloud.jitterMode;
    const bool cliAtmosphere = options_.cloud.atmosphereEnabled;
    const float cliSunElevation = options_.cloud.sunElevationDegrees;
    const float cliSunAzimuth = options_.cloud.sunAzimuthDegrees;
    const bool cliSelfShadow = options_.cloud.selfShadowEnabled;
    const bool cliBreakup = options_.cloud.breakupEnabled;
    const bool cliHeightFront = options_.cloud.heightFront;
    const bool cliStormFront = options_.cloud.stormFront;
    const uint32_t cliStormFrontDebug = options_.cloud.stormFrontDebug;
    const float cliSliceHeight = options_.cloud.densitySliceHeight;
    const float cliSliceExtent = options_.cloud.densitySliceExtentKm;
    const float cliProbeColumn = options_.cloud.probeColumn;
    const float cliTopFade = options_.cloud.topFade;
    const float cliFormulaBias = options_.cloud.formulaBias;
    std::vector<std::string> files;
    std::vector<uint8_t> pixels;
    bool capturedRequestedScene = false;

    for (const Scene& scene : scenes)
    {
        if (!options_.sceneName.empty() &&
            options_.sceneName != scene.name)
            continue;
        capturedRequestedScene = true;
        if (options_.weatherTimelineExplicit)
        {
            const clouds::WeatherTimelineVisual timelineVisual =
                EvaluateTimeline(options_.weatherTimelineTime);
            Log("weather timeline capture: time=%.6f kind=%d "
                "source=%s target=%s outer=%s scene=%s preset=%s",
                options_.weatherTimelineTime,
                static_cast<int>(timelineVisual.kind),
                clouds::StaticWeatherModeName(timelineVisual.source),
                clouds::StaticWeatherModeName(timelineVisual.target),
                clouds::StaticWeatherModeName(timelineVisual.outer),
                scene.name, scene.preset);
        }
        if (options_.weatherTransitionExplicit)
        {
            const char* expectedPreset = clouds::StaticWeatherModeBasePreset(
                options_.transitionSource);
            if (std::string(scene.preset) != expectedPreset)
            {
                Fail(std::string("weather transition source ") +
                     clouds::StaticWeatherModeName(options_.transitionSource) +
                     " requires scene preset " + expectedPreset +
                     ", got " + scene.preset);
            }
            Log("weather transition capture: source=%s target=%s scene=%s "
                "preset=%s phase=%.3f",
                clouds::StaticWeatherModeName(options_.transitionSource),
                clouds::StaticWeatherModeName(options_.transitionTarget),
                scene.name, scene.preset, options_.transitionPhase);
        }
        if (options_.weatherModeExplicit)
        {
            const char* expectedPreset =
                clouds::StaticWeatherModeBasePreset(options_.weatherMode);
            if (std::string(scene.preset) != expectedPreset)
            {
                Fail(std::string("weather mode ") +
                     clouds::StaticWeatherModeName(options_.weatherMode) +
                     " requires scene preset " + expectedPreset +
                     ", got " + scene.preset);
            }
            Log("weather capture: mode=%s scene=%s preset=%s",
                clouds::StaticWeatherModeName(options_.weatherMode),
                scene.name, scene.preset);
        }
        ApplyCloudPreset(scene.preset, false);
        if (options_.weatherModeExplicit)
            clouds::ApplyStaticWeatherMode(options_.weatherMode, options_.cloud);
        if (options_.cloudStepsExplicit)
            options_.cloud.stepCount = cliSteps;
        if (options_.cloudCoverageExplicit)
            options_.cloud.coverage = cliCoverage;
        if (options_.cloudDensityExplicit)
            options_.cloud.density = cliDensity;
        options_.cloud.ablationMode = options_.cloudAblation;
        if (options_.verticalNoiseExplicit)
            options_.cloud.verticalNoiseScale = cliVerticalNoise;
        if (options_.shadowStepsExplicit)
            options_.cloud.shadowStepCount = cliShadowSteps;
        if (options_.sunShadowDistanceExplicit)
            options_.cloud.sunShadowDistance = cliSunShadowDistance;
        if (options_.lightingExplicit)
            options_.cloud.lightingMode = cliLighting;
        if (options_.jitterExplicit)
            options_.cloud.jitterMode = cliJitter;
        if (options_.atmosphereExplicit)
            options_.cloud.atmosphereEnabled = cliAtmosphere;
        if (options_.sunElevationExplicit)
            options_.cloud.sunElevationDegrees = cliSunElevation;
        if (options_.sunAzimuthExplicit)
            options_.cloud.sunAzimuthDegrees = cliSunAzimuth;
        if (options_.selfShadowExplicit)
            options_.cloud.selfShadowEnabled = cliSelfShadow;
        if (options_.breakupExplicit)
            options_.cloud.breakupEnabled = cliBreakup;
        if (options_.frontHeightExplicit)
            options_.cloud.heightFront = cliHeightFront;
        if (options_.stormFrontExplicit)
            options_.cloud.stormFront = cliStormFront;
        if (options_.stormFrontDebugExplicit)
            options_.cloud.stormFrontDebug = cliStormFrontDebug;
        if (options_.sliceHeightExplicit)
            options_.cloud.densitySliceHeight = cliSliceHeight;
        if (options_.sliceExtentExplicit)
            options_.cloud.densitySliceExtentKm = cliSliceExtent;
        if (options_.probeColumnExplicit)
            options_.cloud.probeColumn = cliProbeColumn;
        if (options_.topFadeExplicit)
            options_.cloud.topFade = cliTopFade;
        if (options_.formulaBiasExplicit)
            options_.cloud.formulaBias = cliFormulaBias;
        if (options_.windDisabled)
            options_.cloud.windSpeed = 0.0f;
        if (std::string(scene.name) == "clouds_clear_sun_low")
            options_.cloud.sunElevationDegrees = 8.0f;
        if (std::string(scene.name) == "clouds_extreme_height")
        {
            options_.cloud.layerBottom = 600.0f;
            options_.cloud.layerTop = 1600.0f;
            options_.cloud.weatherHeightVariation = 1200.0f;
        }

        camera_.Reset();
        camera_.position = scene.position;
        camera_.yawDegrees = scene.yaw;
        camera_.pitchDegrees = scene.pitch;
        if (options_.cameraExplicit)
            camera_.position = options_.cameraPosition;
        if (options_.cameraYawExplicit)
            camera_.yawDegrees = options_.cameraYawDegrees;
        if (options_.cameraPitchExplicit)
            camera_.pitchDegrees = options_.cameraPitchDegrees;
        if (options_.fovExplicit)
            camera_.fovYDegrees = options_.fovYDegrees;
        cloudDebug_ = options_.cloudDebugExplicit
            ? options_.cloudDebug : scene.debug;
        cloudTime_ = options_.initialCloudTime;
        if (weatherTimelineActive_)
            weatherTimelineTime_ = clouds::WeatherTimelineClampTime(
                options_.weatherTimeline, options_.weatherTimelineTime);
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
        Log("captured %s (%ux%u, %s, %s) pos %.1f %.1f %.1f yaw %.1f "
            "pitch %.1f fov %.1f",
            path.filename().string().c_str(), renderer_.Width(),
            renderer_.Height(), scene.preset,
            clouds::DebugModeName(cloudDebug_), camera_.position.x,
            camera_.position.y, camera_.position.z, camera_.yawDegrees,
            camera_.pitchDegrees, camera_.fovYDegrees);

        if (options_.captureRawCloud)
        {
            if (cloudDebug_ != clouds::CloudDebugMode::StormOpticalDepth &&
                cloudDebug_ != clouds::CloudDebugMode::StormTransmittance &&
                cloudDebug_ != clouds::CloudDebugMode::StormFrontMask)
            {
                Fail("--capture-raw-cloud requires --debug-mode=storm-od, "
                     "storm-t or storm-mask");
            }
            const clouds::CloudFrameConstants rawConstants =
                BuildCloudConstants(cloudDebug_);
            const CloudReadback raw = cloudRayMarcher_.Readback(
                rawConstants, noise_.SrvGpuHandle(),
                weatherMap_.SrvGpuHandle(), options_.cloudFormula,
                options_.densityModel, options_.referenceStage);
            if (!WriteRawCloud(
                    raw, directory,
                    static_cast<uint32_t>(cloudDebug_), camera_,
                    options_.cloud, options_.cloud.stepCount, scene.name))
            {
                Fail("failed to write raw cloud readback");
            }
            files.emplace_back(std::string(scene.name) + "_raw_cloud.bin");
            files.emplace_back(std::string(scene.name) + "_raw_cloud.json");
            Log("captured raw cloud readback %s mode=%s", scene.name,
                clouds::DebugModeName(cloudDebug_));
        }

        if (cloudDebug_ == clouds::CloudDebugMode::ViewOpticalDepth)
        {
            const uint32_t outputWidth = cloudRayMarcher_.OutputWidth();
            const uint32_t outputHeight = cloudRayMarcher_.OutputHeight();
            const std::vector<float> od =
                cloudRayMarcher_.ReadbackRawRecords(outputWidth * outputHeight);
            if (!WriteOpticalDepthFloat32(
                    od, directory, outputWidth, outputHeight, camera_,
                    options_.cloud, options_.reference, options_.densityModel,
                    options_.referenceStage, cloudTime_, options_.noise.seed,
                    weatherMap_.Seed(), options_.cloudDivisor,
                    options_.windDisabled, scene.name, scene.preset))
            {
                Fail("failed to write Float32 optical-depth map");
            }
            Log("optical-depth float32 readback: %ux%u (mapped_od, od), "
                "exe sha256 %s",
                outputWidth, outputHeight,
                FileSha256(ExecutablePath()).c_str());
        }

        if (options_.densityVolume)
        {
            if (options_.densityModel != clouds::CloudDensityModel::Reference)
                Fail("--density-volume requires --density-model=reference");
            const uint32_t resX = options_.volumeResX;
            const uint32_t resY = options_.volumeResY;
            const uint32_t resZ = options_.volumeResZ;
            const float extentXZ = 16000.0f;
            const float cellXZ = options_.volumeCellSize > 0.0f
                ? options_.volumeCellSize
                : extentXZ / static_cast<float>(resX);
            const float cellY = options_.volumeCellSize > 0.0f
                ? options_.volumeCellSize
                : 4300.0f / static_cast<float>(resY);
            DirectX::XMFLOAT3 origin = options_.volumeOrigin;
            if (!options_.volumeOriginExplicit)
            {
                origin.x = camera_.position.x -
                           0.5f * cellXZ * static_cast<float>(resX);
                origin.y = -100.0f;
                origin.z = camera_.position.z -
                           0.5f * cellXZ * static_cast<float>(resZ);
            }

            clouds::CloudFrameConstants constants =
                BuildCloudConstants(cloudDebug_);
            constants.referenceVolume = DirectX::XMFLOAT4(
                origin.x, origin.y, origin.z, cellXZ);
            constants.referenceGrid = DirectX::XMFLOAT4(
                static_cast<float>(resX), static_cast<float>(resY),
                static_cast<float>(resZ), 0.0f);
            constants.referenceAudit =
                DirectX::XMFLOAT4(1000.0f, 2000.0f, cellY, 0.0f);

            const std::vector<float> volume =
                cloudRayMarcher_.ReadbackDensityVolume(
                    constants, noise_.SrvGpuHandle(),
                    weatherMap_.SrvGpuHandle(), resX, resY, resZ, 0);
            if (!WriteDensityVolumeFloat32(
                    volume, directory, resX, resY, resZ, origin, cellXZ,
                    cellY, options_.cloud, options_.reference,
                    options_.densityModel, options_.referenceStage,
                    cloudTime_, options_.noise.seed, weatherMap_.Seed(),
                    options_.windDisabled, scene.name,
                    directory.filename().string().c_str()))
            {
                Fail("failed to write Float32 density volume");
            }

            if (options_.macroProbe)
            {
                if (options_.reference.fieldMode !=
                    clouds::ReferenceFieldMode::NonTilingMacro)
                {
                    Fail("--macro-probe requires --field-mode=macro");
                }
                const bool localOccupancy =
                    options_.reference.macroLocalOccupancy;
                constants.referenceGrid = DirectX::XMFLOAT4(
                    static_cast<float>(resX), static_cast<float>(resY),
                    static_cast<float>(resZ),
                    localOccupancy ? 4.0f : 3.0f);
                const std::vector<float> macroProbe =
                    cloudRayMarcher_.ReadbackDensityVolume(
                        constants, noise_.SrvGpuHandle(),
                        weatherMap_.SrvGpuHandle(), resX, resY, resZ, 0);
                if (!WriteMacroProbeFloat32(
                        macroProbe, directory, resX, resY, resZ, origin,
                        cellXZ, cellY, options_.cloud, options_.reference,
                        options_.noise.seed, localOccupancy))
                {
                    Fail("failed to write Float32 macro/occupancy probe");
                }
                constants.referenceGrid = DirectX::XMFLOAT4(
                    static_cast<float>(resX), static_cast<float>(resY),
                    static_cast<float>(resZ), 0.0f);
            }

            const std::vector<float> boundary =
                cloudRayMarcher_.ReadbackDensityVolume(
                    constants, noise_.SrvGpuHandle(),
                    weatherMap_.SrvGpuHandle(), resX, 1, resZ, 1);
            if (!WriteStormBoundaryFloat32(
                    boundary, directory, resX, resZ, origin, cellXZ,
                    1000.0f, 2000.0f, options_.cloud,
                    options_.reference, scene.name))
            {
                Fail("failed to write Float32 storm boundary");
            }

            const uint32_t columns = options_.columnsCrosscheck ? resX : 48u;
            const uint32_t packets = options_.columnsCrosscheck
                ? std::max(1u, resY / 4u) : 256u;
            const float columnCell = options_.columnsCrosscheck
                ? cellXZ : extentXZ / static_cast<float>(columns);
            const float columnYStart = options_.columnsCrosscheck
                ? origin.y : -50.0f;
            const float columnYStep = options_.columnsCrosscheck
                ? cellY : 4300.0f / static_cast<float>(packets * 4u);
            constants.referenceVolume = DirectX::XMFLOAT4(
                origin.x, origin.y, origin.z, columnCell);
            constants.referenceGrid = DirectX::XMFLOAT4(
                static_cast<float>(columns), static_cast<float>(packets),
                static_cast<float>(columns), 2.0f);
            constants.referenceAudit = DirectX::XMFLOAT4(
                columnYStart, columnYStep, cellY, 0.0f);
            const std::vector<float> columnData =
                cloudRayMarcher_.ReadbackDensityVolume(
                    constants, noise_.SrvGpuHandle(),
                    weatherMap_.SrvGpuHandle(), columns, packets, columns, 2);
            if (!WriteDensityColumnsFloat32(
                    columnData, directory, columns, packets, columns, origin,
                    columnCell, columnYStart, columnYStep, options_.cloud,
                    options_.reference, options_.densityModel,
                    options_.referenceStage, cloudTime_,
                    options_.noise.seed, weatherMap_.Seed(),
                    options_.windDisabled, options_.columnsCrosscheck,
                    scene.name, directory.filename().string().c_str()))
            {
                Fail("failed to write Float32 density columns");
            }

            Log("density volume readback: %ux%ux%u cell %.3f/%.3f m, "
                "columns %ux%ux%u step %.3f m, exe sha256 %s",
                resX, resY, resZ, cellXZ, cellY, columns, packets, columns,
                columnYStep, FileSha256(ExecutablePath()).c_str());
        }

        if (cloudDebug_ == clouds::CloudDebugMode::RayProfile ||
            cloudDebug_ == clouds::CloudDebugMode::DensityHorizontal)
        {
            const float layerThickness = std::max(
                options_.cloud.layerTop - options_.cloud.layerBottom, 1.0f);
            const float volumeMargin =
                options_.cloud.weatherHeightVariation * 0.5f +
                layerThickness * 0.35f;
            Log("world-probe params: aspect %.6f layerBottom %.3f "
                "layerTop %.3f volumeMargin %.3f steps %u probeColumn %.4f "
                "sliceHeight %.4f sliceExtentKm %.3f",
                static_cast<float>(renderer_.Width()) /
                    static_cast<float>(renderer_.Height()),
                options_.cloud.layerBottom, options_.cloud.layerTop,
                volumeMargin, options_.cloud.stepCount,
                options_.cloud.probeColumn,
                options_.cloud.densitySliceHeight,
                options_.cloud.densitySliceExtentKm);
        }
        if (cloudDebug_ == clouds::CloudDebugMode::RayProfile)
        {
            const CloudProbeReadback probe = cloudRayMarcher_.ReadbackProbe(
                cloudRayMarcher_.OutputHeight(), options_.cloud.stepCount);
            if (!WriteProbeFiles(
                    probe, directory, camera_, options_.cloud,
                    cloudRayMarcher_.OutputWidth(),
                    cloudRayMarcher_.OutputHeight(), cloudTime_,
                    options_.noise.seed, weatherMap_.Seed(),
                    options_.cloudDivisor, scene.name))
            {
                Fail("failed to write float32 ray-profile files");
            }
            Log("probe float32 readback: %u rows x %u steps x 3 records, "
                "exe sha256 %s",
                probe.rows, probe.steps,
                FileSha256(ExecutablePath()).c_str());
        }
        if (cloudDebug_ == clouds::CloudDebugMode::NoiseField)
        {
            if (cloudRayMarcher_.OutputWidth() < 256u ||
                cloudRayMarcher_.OutputHeight() < 256u)
            {
                Fail("noise-field capture requires cloud output at least "
                     "256x256 (use --cloud-scale=full and a larger window)");
            }
            const CloudProbeReadback probe =
                cloudRayMarcher_.ReadbackProbe(256u, 256u);
            if (!WriteNoiseFieldFiles(
                    probe, directory, camera_, options_.cloud,
                    options_.reference,
                    cloudRayMarcher_.OutputWidth(),
                    cloudRayMarcher_.OutputHeight(), cloudTime_,
                    options_.noise.seed, weatherMap_.Seed(),
                    options_.cloudDivisor, scene.name,
                    options_.densityModel, options_.referenceStage))
            {
                Fail("failed to write raw Float32 noise-field files");
            }
            Log("noise-field float32 readback: %u rows x %u steps x 3 "
                "records, axis %u, mix %s, coordinate %s, transfer %.3f..%.3f",
                probe.rows, probe.steps,
                options_.reference.noiseFieldAxis % 3u,
                clouds::ReferenceNoiseMixName(options_.reference.noiseMix),
                clouds::ReferenceCoordinateModeName(
                    options_.reference.coordinateMode),
                options_.reference.shapeLow,
                options_.reference.shapeHigh);
        }
    }

    if (!options_.sceneName.empty() && !capturedRequestedScene)
        Fail("requested cloud scene was not found: " + options_.sceneName);

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
