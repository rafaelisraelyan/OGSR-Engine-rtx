#include "StdAfx.h"
#include "../../xr_3da/_d3d_extensions.h"
#include "../../xr_3da/xrLevel.h"
#include "../../xr_3da/igame_persistent.h"
#include "../../xr_3da/environment.h"
#include "R_light.h"
#include "light_db.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
#include "../../xr_3da/GameDx12Lighting.h"

gamelight12::LocalRejection gamelight12::ReadWorldLight(const IRender_Light& source, LocalLight& output) noexcept
{
    const auto* lightSource = dynamic_cast<const light*>(&source);
    if (!lightSource)
        return LocalRejection::UnsupportedSource;

    const auto& position = lightSource->position;
    output.id = lightSource->Dx12Id();
    output.position = {position.x, position.y, position.z};
    output.active = lightSource->flags.bActive;
    output.sourceShadow = lightSource->Dx12SourceCastsShadow();
    output.range = lightSource->range * 0.95f; // Эффективный радиус accum_point/spot.
    if (!output.active)
        return LocalRejection::Inactive;
    if (lightSource->flags.bStatic || lightSource->flags.bHudMode)
        return LocalRejection::NotWorld;

    const auto type = lightSource->flags.type;
    if (type != IRender_Light::POINT && type != IRender_Light::SPOT)
        return LocalRejection::UnsupportedType;

    const auto& color = lightSource->color;
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z) || !std::isfinite(output.range) || output.range <= EPS_L ||
        !DecodeLightColor(color.r, color.g, color.b, output.color))
        return LocalRejection::InvalidData;

    // export_to меняет flags.bShadow при R4 distance cull. Snapshot использует
    // исходный флаг владельца и применяет ограничение расстояния для своей камеры.
    output.shadowNear = lightSource->virtual_size;
    output.shadowFar = lightSource->range + EPS_S;
    output.castsShadow = output.sourceShadow && std::isfinite(output.shadowNear) &&
        std::isfinite(output.shadowFar) && output.shadowNear > 0.0f &&
        output.shadowNear < output.shadowFar;
    if (type == IRender_Light::SPOT)
    {
        const auto& direction = lightSource->direction;
        const float cone = lightSource->cone; // Полный угол проекции R4 в радианах.
        if (!std::isfinite(cone) || cone <= EPS_L || cone > deg2rad(121.f) ||
            !NormalizeDirection(direction.x, direction.y, direction.z, output.direction))
            return LocalRejection::InvalidData;
        output.type = LocalType::Spot;
        output.outerCos = std::cos(cone * 0.5f);
        // Плавный край аппроксимирует штатную проекционную текстуру R4.
        output.innerCos = std::cos(cone * 0.4f);
    }
    return LocalRejection::None;
}
#endif

CLight_DB::CLight_DB() {}

CLight_DB::~CLight_DB() {}

void CLight_DB::Load(IReader* fs)
{
    // Lights itself
    sun_adapted = nullptr;

    {
        IReader* F = fs->open_chunk(fsL_LIGHT_DYNAMIC);

        const u32 size = F->length();
        const u32 element = sizeof(Flight) + 4;
        const u32 count = size / element;
        VERIFY(count * element == size);

        for (u32 i = 0; i < count; i++)
        {
            F->advance(sizeof(u32)); // u32 controller = F->r_u32();

            Flight Ldata;
            F->r(&Ldata, sizeof(Flight));

            if (Ldata.type == D3DLIGHT_DIRECTIONAL)
            {
                Fvector tmp_R;
                tmp_R.set(1, 0, 0);

                light* L = Create();
                L->flags.bStatic = true;
                L->set_type(IRender_Light::DIRECT);
                L->set_shadow(false);
                L->set_rotation(Ldata.direction, tmp_R);

                // copy to env-sun
                sun_adapted = L;

                break;
            }
        }

        F->close();
    }

    R_ASSERT(sun_adapted, "Where is sun?");
}

void CLight_DB::LoadHemi()
{
    string_path fn_game;
    if (FS.exist(fn_game, fsgame::level, fsgame::level_files::build_lights))
    {
        IReader* F = FS.r_open(fn_game);

        if (IReader* chunk = F->open_chunk(1))
        {
            u32 size = chunk->length();
            u32 element = sizeof(R_Light);
            u32 count = size / element;
            VERIFY(count * element == size);
            v_hemi.reserve(count);

            for (u32 i = 0; i < count; i++)
            {
                R_Light Ldata;

                chunk->r(&Ldata, sizeof(R_Light));

                if (Ldata.type == D3DLIGHT_POINT)
                {
                    light* L = Create();
                    L->flags.bStatic = true;
                    L->set_type(IRender_Light::POINT);

                    Fvector tmp_D, tmp_R;
                    tmp_D.set(0, 0, -1); // forward
                    tmp_R.set(1, 0, 0); // right

                    // point
                    v_hemi.emplace_back(L);
                    L->set_position(Ldata.position);
                    L->set_rotation(tmp_D, tmp_R);
                    L->set_range(Ldata.range);
                    L->set_color(Ldata.diffuse.x, Ldata.diffuse.y, Ldata.diffuse.z);
                    L->set_active(true);
                    L->set_attenuation_params(Ldata.attenuation0, Ldata.attenuation1, Ldata.attenuation2, Ldata.falloff);
                    L->spatial.type = STYPE_LIGHTSOURCEHEMI;
                }
            }

            chunk->close();

            Msg("~ Loaded [%d] STYPE_LIGHTSOURCEHEMI", v_hemi.size());
        }

        FS.r_close(F);
    }
}


void CLight_DB::Unload()
{
    v_hemi.clear();
    sun_adapted.destroy();
}

light* CLight_DB::Create()
{
    light* L = xr_new<light>();
    L->flags.bStatic = false;
    L->set_active(false);
    L->set_shadow(false); // disable shadow by default. caller classes should enable it case by case
    return L;
}

void CLight_DB::add_light(light* L)
{
    if (Device.dwFrame == L->frame_render)
        return;
    L->frame_render = Device.dwFrame;

    if (RImplementation.o.noshadows)
        L->set_shadow(false);

    if (L->flags.bStatic /*&& !ps_r2_ls_flags.test(R2FLAG_R1LIGHTS)*/)
        return;

    if (ps_r2_ls_flags_ext.test(R2FLAGEXT_DISABLE_LIGHT))
        return;

    L->export_to(package);
}

void CLight_DB::UpdateSun() const
{
    // set sun params
    if (sun_adapted)
    {
        ZoneScoped;

        const light* _sun_adapted = smart_cast<light*>(sun_adapted._get());
        CEnvDescriptor& E = *g_pGamePersistent->Environment().CurrentEnv;
        VERIFY(_valid(E.sun_dir));

#ifdef DEBUG
        if (E.sun_dir.y >= 0)
        {
            //			Log("sect_name", E.sect_name.c_str());
            Log("E.sun_dir", E.sun_dir);
            Log("E.wind_direction", E.wind_direction);
            Log("E.wind_velocity", E.wind_velocity);
            Log("E.sun_color", E.sun_color);
            Log("E.rain_color", E.rain_color);
            Log("E.rain_density", E.rain_density);
            Log("E.fog_distance", E.fog_distance);
            Log("E.fog_density", E.fog_density);
            Log("E.fog_color", E.fog_color);
            Log("E.far_plane", E.far_plane);
            Log("E.sky_rotation", E.sky_rotation);
            Log("E.sky_color", E.sky_color);
        }
#endif

        VERIFY(E.sun_dir.y < 0, "Invalid sun direction settings in evironment-config");
        Fvector dir, pos;

        // true sunlight direction
        dir.set(E.sun_dir).normalize();

        pos.mad(Device.vCameraPosition, dir, -500.f);

        sun_adapted->set_rotation(dir, _sun_adapted->right);
        sun_adapted->set_position(pos);
        sun_adapted->set_color(E.sun_color.x * ps_r2_sun_lumscale, E.sun_color.y * ps_r2_sun_lumscale, E.sun_color.z * ps_r2_sun_lumscale);
        sun_adapted->set_range(600.f);
    }
}
    