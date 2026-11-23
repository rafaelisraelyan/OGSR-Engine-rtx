#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace gameaux12
{
enum class SurfaceMode : uint8_t
{
    Auto,
    Opaque,
    Cutout,
    OverlayOpaque,
    OverlayCutout,
    OverlayBlend,
    OverlayMultiply,
    Translucent,
    Unsupported,
};

inline constexpr char MaterialLowerAscii(char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

inline std::string CanonicalShaderName(std::string_view shader)
{
    std::string name;
    name.reserve(shader.size());
    for (const char value : shader)
        name.push_back(value == '\\' || value == '/' ? '_' : MaterialLowerAscii(value));
    return name;
}

enum class TransparentMaterial : uint8_t
{
    None = 0,
    Water = 2,
    Glass = 3,
};

inline bool TransparencyEnabled()
{
    static const bool enabled = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_TRANSPARENCY");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

// Текстуру s_base задают normal-проходы соответствующих .s R4.
// level.puddles загружается отдельной фазой и не является level visual.
inline constexpr std::string_view WaterBaseTexture(std::string_view canonicalShader)
{
    if (canonicalShader == "effects_water")
        return "water\\water_water";
    if (canonicalShader == "effects_waterstuden")
        return "water\\water_studen";
    if (canonicalShader == "effects_waterryaska" || canonicalShader == "effects_waterunderground")
        return "water\\water_ryaska1";
    return {};
}

inline TransparentMaterial ClassifyTransparentMaterial(std::string_view shader)
{
    if (!TransparencyEnabled())
        return TransparentMaterial::None;

    const auto name = CanonicalShaderName(shader);
    // Порядок групп соответствует sorting(2, false) и sorting(3, true) R4.
    if (!WaterBaseTexture(name).empty())
        return TransparentMaterial::Water;
    if (name == "models_transparent" || name == "models_xwindows")
        return TransparentMaterial::Glass;
    return TransparentMaterial::None;
}

inline bool UsesWallmarkClamp(std::string_view shader)
{
    const auto name = CanonicalShaderName(shader);
    return name == "effects_wallmark" || name == "effects_wallmarkmult" ||
           name == "effects_wallmarkblend" || name == "effects_wallmarkset";
}

inline SurfaceMode ClassifySurface(std::string_view shader)
{
    const auto name = CanonicalShaderName(shader);

    // ResourceManager_Scripting переводит имена шейдеров в имена Lua-модулей.
    // Здесь используются состояния прохода normal из одноимённых скриптов R4.
    if (name == "effects_wallmark" || name == "effects_wallmarkmult")
        return SurfaceMode::OverlayMultiply;
    if (name == "effects_wallmarkblend" || name == "effects_wallmark_blood")
        return SurfaceMode::OverlayBlend;
    if (name == "effects_wallmarkset")
        return SurfaceMode::OverlayOpaque;
    if (name == "models_transparent" || name == "models_xwindows" ||
        (TransparencyEnabled() && !WaterBaseTexture(name).empty()))
        return SurfaceMode::Translucent;
    if (name == "models_pautina" || name == "models_selflight_det_3_aref" ||
        name.starts_with("models_xdistort") ||
        (TransparencyEnabled() && name == "effects_water_puddles"))
        return SurfaceMode::Unsupported;

    if (name == "models_model" || name == "models_model_pn")
        return SurfaceMode::Opaque;
    if (name == "models_model_aref" || name == "models_model_aref_pn" ||
        name == "details_blend" || name == "details_set")
        return SurfaceMode::Cutout;

    // R4 различает ветви alpha reference и alpha blend. Учитываем явный
    // идентификатор aref для штатных семейств уровня и растительности.
    if ((name.starts_with("def_shaders_") || name.starts_with("flora_")) &&
        (name.ends_with("_aref") || name.find("_aref_") != std::string::npos))
        return SurfaceMode::Cutout;

    return SurfaceMode::Auto;
}

inline constexpr const char* SurfaceModeName(SurfaceMode mode)
{
    switch (mode)
    {
    case SurfaceMode::Auto: return "auto";
    case SurfaceMode::Opaque: return "opaque";
    case SurfaceMode::Cutout: return "cutout";
    case SurfaceMode::OverlayOpaque: return "overlay_opaque";
    case SurfaceMode::OverlayCutout: return "overlay_cutout";
    case SurfaceMode::OverlayBlend: return "overlay_blend";
    case SurfaceMode::OverlayMultiply: return "overlay_multiply";
    case SurfaceMode::Translucent: return "translucent";
    case SurfaceMode::Unsupported: return "unsupported";
    }
    return "unsupported";
}

inline std::string CanonicalBaseTextureName(std::string_view textures)
{
    // В базовом проходе blenders используют первый элемент L_textures.
    const auto first = textures.substr(0, textures.find(','));
    if (first.empty() || first.size() > 255)
        return {};

    std::string name;
    name.reserve(first.size());
    for (char value : first)
    {
        value = MaterialLowerAscii(value);
        if (value == '/')
            value = '\\';
        if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
              value == '_' || value == '-' || value == '.' || value == '\\' || value == '#'))
            return {};
        name.push_back(value);
    }

    // Как fix_texture_name, сводим исходные расширения к DDS-имени в VFS.
    if (name.ends_with(".dds") || name.ends_with(".tga") || name.ends_with(".bmp") ||
        name.ends_with(".ogm"))
        name.resize(name.size() - 4);

    const std::string_view path = name;
    size_t start = 0;
    for (;;)
    {
        const size_t end = path.find('\\', start);
        const auto component = path.substr(start, end - start);
        if (component.empty() || component == "." || component == ".." ||
            component.back() == '.')
            return {};
        if (end == path.npos)
            break;
        start = end + 1;
    }
    return name;
}

inline std::string SurfaceBaseTextureName(std::string_view shader, std::string_view textures)
{
    if (TransparencyEnabled())
    {
        const auto waterTexture = WaterBaseTexture(CanonicalShaderName(shader));
        if (!waterTexture.empty())
            return std::string(waterTexture);
    }
    return CanonicalBaseTextureName(textures);
}
}
