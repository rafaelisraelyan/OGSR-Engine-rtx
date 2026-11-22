#pragma once

// V-WEATHER-01C-A: public cloud graphics settings schema and persistence.
// This module deliberately contains no weather controls. Unknown keys,
// including injected weather keys, are ignored and can never mutate weather.

#include <Windows.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace clouds
{
enum class CloudQuality
{
    Performance = 0,
    Balanced,
    Quality,
    Reference,
};

enum class CloudRenderMode
{
    Volumetric3D = 0,
    Classic2D,
};

inline const char* CloudQualityName(CloudQuality quality)
{
    switch (quality)
    {
    case CloudQuality::Performance: return "performance";
    case CloudQuality::Balanced: return "balanced";
    case CloudQuality::Quality: return "quality";
    case CloudQuality::Reference: return "reference";
    }
    return "quality";
}

inline const char* CloudRenderModeName(CloudRenderMode mode)
{
    switch (mode)
    {
    case CloudRenderMode::Volumetric3D: return "volumetric3d";
    case CloudRenderMode::Classic2D: return "classic2d";
    }
    return "volumetric3d";
}

inline bool ParseCloudQuality(std::string_view text, CloudQuality& quality)
{
    if (text == "performance") { quality = CloudQuality::Performance; return true; }
    if (text == "balanced") { quality = CloudQuality::Balanced; return true; }
    if (text == "quality") { quality = CloudQuality::Quality; return true; }
    if (text == "reference") { quality = CloudQuality::Reference; return true; }
    return false;
}

inline bool ParseCloudRenderMode(
    std::string_view text, CloudRenderMode& mode)
{
    if (text == "volumetric3d") { mode = CloudRenderMode::Volumetric3D; return true; }
    if (text == "classic2d") { mode = CloudRenderMode::Classic2D; return true; }
    return false;
}

struct CloudGraphicsSettings
{
    int schemaVersion = 1;
    bool cloudsEnabled = true;
    CloudQuality quality = CloudQuality::Quality;
    CloudRenderMode renderMode = CloudRenderMode::Volumetric3D;
};

struct CloudQualityProfile
{
    uint32_t raySteps = 128;
    uint32_t shadowSteps = 6;
    uint32_t outputDivisor = 2;
};

inline CloudQualityProfile GetCloudQualityProfile(CloudQuality quality)
{
    switch (quality)
    {
    case CloudQuality::Performance:
        return CloudQualityProfile{64, 2, 2};
    case CloudQuality::Balanced:
        return CloudQualityProfile{96, 4, 2};
    case CloudQuality::Quality:
        return CloudQualityProfile{128, 6, 2};
    case CloudQuality::Reference:
        return CloudQualityProfile{128, 6, 1};
    }
    return CloudQualityProfile{};
}

struct CloudSettingsLoadResult
{
    CloudGraphicsSettings settings{};
    bool fileFound = false;
    bool loaded = false;
    bool error = false;
    bool unsupportedVersion = false;
    bool classic2dFallback = false;
    bool hasCloudsEnabled = false;
    bool hasQuality = false;
    bool hasRenderMode = false;
    std::vector<std::string> unknownKeys;
    std::vector<std::string> warnings;
};

inline CloudGraphicsSettings DefaultCloudGraphicsSettings()
{
    return CloudGraphicsSettings{};
}

namespace settings_detail
{
inline void SkipSpace(const std::string& text, size_t& index)
{
    while (index < text.size() &&
           (text[index] == ' ' || text[index] == '\t' ||
            text[index] == '\r' || text[index] == '\n'))
        ++index;
}

inline bool ParseString(
    const std::string& text, size_t& index, std::string& value,
    std::string& error)
{
    if (index >= text.size() || text[index] != '"')
    {
        error = "expected string";
        return false;
    }
    ++index;
    value.clear();
    while (index < text.size())
    {
        const char c = text[index++];
        if (c == '"')
            return true;
        if (c != '\\')
        {
            value.push_back(c);
            continue;
        }
        if (index >= text.size())
        {
            error = "unterminated escape";
            return false;
        }
        const char e = text[index++];
        switch (e)
        {
        case '"': value.push_back('"'); break;
        case '\\': value.push_back('\\'); break;
        case '/': value.push_back('/'); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        default:
            error = "unsupported string escape";
            return false;
        }
    }
    error = "unterminated string";
    return false;
}

inline bool ParseFlatObject(
    const std::string& text, std::map<std::string, std::string>& values,
    std::string& error)
{
    size_t index = 0;
    SkipSpace(text, index);
    if (index >= text.size() || text[index] != '{')
    {
        error = "root must be an object";
        return false;
    }
    ++index;
    for (;;)
    {
        SkipSpace(text, index);
        if (index < text.size() && text[index] == '}')
        {
            ++index;
            break;
        }
        std::string key;
        if (!ParseString(text, index, key, error))
            return false;
        SkipSpace(text, index);
        if (index >= text.size() || text[index] != ':')
        {
            error = "expected ':'";
            return false;
        }
        ++index;
        SkipSpace(text, index);
        std::string value;
        if (index < text.size() && text[index] == '"')
        {
            if (!ParseString(text, index, value, error))
                return false;
        }
        else
        {
            const size_t start = index;
            while (index < text.size() && text[index] != ',' &&
                   text[index] != '}')
                ++index;
            value = text.substr(start, index - start);
            const size_t last = value.find_last_not_of(" \t\r\n");
            value = last == std::string::npos
                ? std::string() : value.substr(0, last + 1);
            if (value.empty() || value[0] == '{' || value[0] == '[')
            {
                error = "unsupported or empty value";
                return false;
            }
        }
        values[key] = value;
        SkipSpace(text, index);
        if (index < text.size() && text[index] == ',')
        {
            ++index;
            continue;
        }
        if (index < text.size() && text[index] == '}')
        {
            ++index;
            break;
        }
        error = "expected ',' or '}'";
        return false;
    }
    SkipSpace(text, index);
    if (index != text.size())
    {
        error = "trailing characters";
        return false;
    }
    return true;
}

inline bool ParseBool(const std::string& text, bool& value)
{
    if (text == "true") { value = true; return true; }
    if (text == "false") { value = false; return true; }
    return false;
}

inline bool ParseInt(const std::string& text, int& value)
{
    if (text.empty())
        return false;
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || *end != '\0')
        return false;
    value = static_cast<int>(parsed);
    return true;
}
} // namespace settings_detail

inline CloudSettingsLoadResult ParseCloudSettingsText(const std::string& text)
{
    CloudSettingsLoadResult result;
    result.settings = DefaultCloudGraphicsSettings();
    std::map<std::string, std::string> values;
    std::string error;
    std::string json = text;
    if (json.size() >= 3 &&
        static_cast<unsigned char>(json[0]) == 0xEF &&
        static_cast<unsigned char>(json[1]) == 0xBB &&
        static_cast<unsigned char>(json[2]) == 0xBF)
        json.erase(0, 3);
    if (!settings_detail::ParseFlatObject(json, values, error))
    {
        result.error = true;
        result.warnings.push_back("invalid settings JSON: " + error);
        return result;
    }

    auto get = [&values](const char* key) -> const std::string*
    {
        const auto it = values.find(key);
        return it == values.end() ? nullptr : &it->second;
    };

    // Unknown keys are deliberately ignored. This is the isolation boundary
    // that makes weather/debug injection through a graphics config inert.
    for (const auto& entry : values)
    {
        const std::string& key = entry.first;
        if (key != "schema_version" && key != "clouds_enabled" &&
            key != "cloud_render_mode" && key != "cloud_quality")
            result.unknownKeys.push_back(key);
    }

    if (const std::string* version = get("schema_version"))
    {
        int parsed = 0;
        if (!settings_detail::ParseInt(*version, parsed) || parsed != 1)
        {
            result.unsupportedVersion = true;
            result.error = true;
            result.warnings.push_back(
                "unsupported cloud settings schema_version");
            result.settings = DefaultCloudGraphicsSettings();
            return result;
        }
    }

    if (const std::string* enabled = get("clouds_enabled"))
    {
        bool parsed = true;
        if (settings_detail::ParseBool(*enabled, parsed))
        {
            result.settings.cloudsEnabled = parsed;
            result.hasCloudsEnabled = true;
        }
        else
        {
            result.error = true;
            result.warnings.push_back(
                "invalid clouds_enabled; using safe default true");
        }
    }

    if (const std::string* qualityText = get("cloud_quality"))
    {
        CloudQuality quality = CloudQuality::Quality;
        if (ParseCloudQuality(*qualityText, quality))
        {
            result.settings.quality = quality;
            result.hasQuality = true;
        }
        else
        {
            result.error = true;
            result.warnings.push_back(
                "invalid cloud_quality; using safe default quality");
        }
    }

    if (const std::string* modeText = get("cloud_render_mode"))
    {
        CloudRenderMode mode = CloudRenderMode::Volumetric3D;
        if (ParseCloudRenderMode(*modeText, mode))
        {
            result.hasRenderMode = true;
            if (mode == CloudRenderMode::Classic2D)
            {
                result.classic2dFallback = true;
                result.settings.renderMode = CloudRenderMode::Volumetric3D;
                result.warnings.push_back(
                    "classic2d is a future game-side contract; "
                    "standalone falls back to volumetric3d");
            }
            else
            {
                result.settings.renderMode = mode;
            }
        }
        else
        {
            result.error = true;
            result.warnings.push_back(
                "invalid cloud_render_mode; using safe default volumetric3d");
        }
    }

    result.loaded = true;
    return result;
}

inline CloudSettingsLoadResult LoadCloudSettings(
    const std::filesystem::path& path)
{
    CloudSettingsLoadResult result;
    result.settings = DefaultCloudGraphicsSettings();
    if (path.empty())
        return result;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec)
        return result;
    result.fileFound = true;
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        result.error = true;
        result.warnings.push_back("cannot open cloud settings file");
        return result;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0 || size > 1024 * 1024)
    {
        result.error = true;
        result.warnings.push_back("cloud settings file is too large");
        return result;
    }
    input.seekg(0, std::ios::beg);
    std::string text(static_cast<size_t>(size), '\0');
    if (size > 0)
        input.read(text.data(), size);
    CloudSettingsLoadResult parsed = ParseCloudSettingsText(text);
    parsed.fileFound = true;
    return parsed;
}

inline bool SaveCloudSettings(
    const std::filesystem::path& path, const CloudGraphicsSettings& settings,
    std::string& error)
{
    error.clear();
    if (path.empty())
    {
        error = "empty cloud settings path";
        return false;
    }
    std::error_code ec;
    const auto parent = path.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, ec);
    if (ec)
    {
        error = "cannot create settings directory: " + ec.message();
        return false;
    }

    std::filesystem::path temp = path;
    temp += L".tmp";
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            error = "cannot open temporary settings file";
            return false;
        }
        output << "{\n"
               << "  \"schema_version\": 1,\n"
               << "  \"clouds_enabled\": "
               << (settings.cloudsEnabled ? "true" : "false") << ",\n"
               << "  \"cloud_render_mode\": \""
               << CloudRenderModeName(settings.renderMode) << "\",\n"
               << "  \"cloud_quality\": \""
               << CloudQualityName(settings.quality) << "\"\n"
               << "}\n";
        output.flush();
        if (!output.good())
        {
            error = "cannot write temporary settings file";
            output.close();
            std::filesystem::remove(temp, ec);
            return false;
        }
    }

    if (!MoveFileExW(
            temp.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        error = "atomic settings replace failed";
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}
} // namespace clouds
