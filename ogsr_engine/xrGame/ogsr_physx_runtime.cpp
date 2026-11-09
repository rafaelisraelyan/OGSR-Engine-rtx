#include "stdafx.h"

#ifdef OGSR_PHYSX_RUNTIME

#include "ogsr_physx_runtime.h"

#include "Level.h"
#include "MainMenu.h"
#include "../xr_3da/xr_area.h"
#include "../xr_3da/xr_object.h"
#include "../Layers/PhysicsPhysX/include/physics_physx/physx_runtime_c.h"
#ifdef OGSR_PHYSX_RAGDOLL
#include "ogsr_physx_ragdoll.h"
#include "../Layers/PhysicsPhysX/include/physics_physx/physx_ragdoll_c.h"
#include "../Layers/PhysicsPhysX/include/physics_physx/physx_ragdoll_c_v2.h"
#include "../Layers/PhysicsPhysX/include/physics_physx/physx_ragdoll_v2_loader.h"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <windows.h>

namespace
{
using Clock = std::chrono::steady_clock;

constexpr float kStaticHalfExtentXZ = 16.0f;
constexpr float kStaticHalfExtentY = 8.0f;
constexpr float kV2StaticHalfExtentXZ = 48.0f;
constexpr float kV2StaticHalfExtentY = 24.0f;
constexpr float kV2StaticTileSizeXZ = 96.0f;
constexpr float kV2StaticTileSizeY = 48.0f;
constexpr float kV2StaticTileQueryMargin = 2.0f;
constexpr float kV2StaticTileSafetyMargin = 12.0f;
constexpr std::uint32_t kDefaultMaxStaticTiles = 8;
constexpr std::uint32_t kDefaultMaxStaticTileTriangles = 65536;
constexpr std::uint32_t kMaxStaticTileTrianglesPerTile = 16384;
constexpr float kAllHumanDefaultHitImpulseMax = 30.0f;
constexpr std::uint32_t kMinRagdolls = 2;
constexpr std::uint32_t kMaxRagdollsExperimental = 40;
constexpr std::uint32_t kMaxStaticTriangles = 4096;
constexpr std::uint32_t kMaxSubsteps = 4;
constexpr float kFixedStep = 0.01f;
constexpr size_t kErrorSize = 512;

using CreateFn = OgsrPhysXRuntimeHandle (*)(const OgsrPhysXRuntimeConfig*, char*, size_t);
using DestroyFn = void (*)(OgsrPhysXRuntimeHandle);
using SetStaticMeshFn = int (*)(OgsrPhysXRuntimeHandle, const float*, uint32_t, const uint32_t*, uint32_t, char*,
                                size_t);
using CreateTestBoxFn = int (*)(OgsrPhysXRuntimeHandle, float, float, float, float, float, float, float, char*, size_t);
using StepFn = int (*)(OgsrPhysXRuntimeHandle, float, char*, size_t);
using GetStateFn = int (*)(OgsrPhysXRuntimeHandle, OgsrPhysXRuntimeState*);
#ifdef OGSR_PHYSX_RAGDOLL
using RagdollRuntimeCreateFn = OgsrPhysXRuntimeHandle (*)(const OgsrPhysXRagdollConfig*, char*, size_t);
using RagdollRuntimeDestroyFn = void (*)(OgsrPhysXRuntimeHandle);
using RagdollSetStaticMeshFn = int (*)(OgsrPhysXRuntimeHandle, const float*, uint32_t, const uint32_t*, uint32_t,
                                       char*, size_t);
using RagdollCreateFn = int (*)(OgsrPhysXRuntimeHandle, const OgsrPhysXRagdollDefinition*, char*, size_t);
using RagdollDestroyFn = void (*)(OgsrPhysXRuntimeHandle);
using RagdollStepFn = int (*)(OgsrPhysXRuntimeHandle, float, char*, size_t);
using RagdollGetPoseFn = int (*)(OgsrPhysXRuntimeHandle, OgsrPhysXRagdollPose*, float*, uint32_t);
using RagdollApplyImpulseFn = int (*)(OgsrPhysXRuntimeHandle, uint16_t, const float[3], const float[3], char*, size_t);
using RagdollApplyCentralImpulseFn = int (*)(OgsrPhysXRuntimeHandle, uint16_t, const float[3], char*, size_t);
using RagdollGetBodyMassFn = int (*)(OgsrPhysXRuntimeHandle, uint16_t, OgsrPhysXRagdollBodyMass*, char*, size_t);
using RagdollGetInfoFn = int (*)(OgsrPhysXRuntimeHandle, OgsrPhysXRagdollInfo*);
using RagdollGetDiagnosticsFn = int (*)(OgsrPhysXRuntimeHandle, float, OgsrPhysXRagdollDiagnostics*);
#endif

struct Api
{
    HMODULE module = nullptr;
    CreateFn create = nullptr;
    DestroyFn destroy = nullptr;
    SetStaticMeshFn setStaticMesh = nullptr;
    CreateTestBoxFn createTestBox = nullptr;
    StepFn step = nullptr;
    GetStateFn getState = nullptr;
#ifdef OGSR_PHYSX_RAGDOLL
    RagdollRuntimeCreateFn ragdollRuntimeCreate = nullptr;
    RagdollRuntimeDestroyFn ragdollRuntimeDestroy = nullptr;
    RagdollSetStaticMeshFn ragdollSetStaticMesh = nullptr;
    RagdollCreateFn ragdollCreate = nullptr;
    RagdollDestroyFn ragdollDestroy = nullptr;
    RagdollStepFn ragdollStep = nullptr;
    RagdollGetPoseFn ragdollGetPose = nullptr;
    RagdollApplyImpulseFn ragdollApplyImpulse = nullptr;
    RagdollApplyCentralImpulseFn ragdollApplyCentralImpulse = nullptr;
    RagdollGetBodyMassFn ragdollGetBodyMass = nullptr;
    RagdollGetInfoFn ragdollGetInfo = nullptr;
    RagdollGetDiagnosticsFn ragdollGetDiagnostics = nullptr;
#endif
};

Api g_api;
OgsrPhysXRuntimeHandle g_handle = nullptr;
bool g_requested = false;
bool g_requested_parsed = false;
bool g_smoke_parsed = false;
bool g_smoke = false;
bool g_init_failed = false;
bool g_was_paused = false;
bool g_smoke_pause_active = false;
#ifdef OGSR_DIAGNOSTIC_RUNTIME_WORKAROUND
bool g_archive_runtime_prepared = false;
#endif
std::uint32_t g_frames = 0;
double g_step_ms_total = 0.0;
double g_step_ms_max = 0.0;

#ifdef OGSR_PHYSX_RAGDOLL
OgsrPhysXRuntimeHandle g_ragdoll_handle = nullptr;
bool g_ragdoll_alive = false;
bool g_ragdoll_active = false;
bool g_ragdoll_parsed = false;
bool g_ragdoll_requested = false;
bool g_ragdoll_smoke = false;
bool g_ragdoll_force_free = false;
bool g_ragdoll_ode_smoke = false;
bool g_ragdoll_multi = false;
bool g_ragdoll_reload = false;
bool g_ragdoll_visual_replace = false;
bool g_ragdoll_visual_replace_ode = false;
std::string g_ragdoll_target;
std::uint32_t g_ragdoll_frames = 0;
std::uint32_t g_ragdoll_smoke_phase = 0;
std::uint32_t g_ragdoll_smoke_active_frames = 0;
bool g_ragdoll_smoke_time_scaled = false;
double g_ragdoll_ms_total = 0.0;
double g_ragdoll_ms_max = 0.0;
std::uint32_t g_ragdoll_steps = 0;
bool g_ragdoll_multi_instances = false;
bool g_ragdoll_all_humans = false;
bool g_ragdoll_static_tiles = false;
bool g_ragdoll_static_tiles_smoke = false;
bool g_ragdoll_static_tiles_boundary_smoke = false;
bool g_ragdoll_g2_single_smoke = false;
bool g_ragdoll_g3_real_hit = false;
std::string g_ragdoll_g3_weapon = "wpn_pm";
std::string g_ragdoll_g3_aim_bone = "bip01_spine";
float g_ragdoll_g3_target_offset_x = 0.0f;
float g_ragdoll_g3_target_offset_z = -3.0f;
std::uint32_t g_ragdoll_g3_max_shots = 6;
std::uint32_t g_ragdoll_max_ragdolls = 2;
float g_ragdoll_hit_impulse_scale = 1.0f;
float g_ragdoll_hit_impulse_max = 0.0f;
bool g_ragdoll_hit_impulse_max_explicit = false;
bool g_ragdoll_self_collision = false;
bool g_ragdoll_collider_vis = false;
bool g_ragdoll_diag = true;

struct V2StaticTile
{
    std::uint64_t key = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::uint32_t triangles = 0;
    double extractMs = 0.0;
    double cookMs = 0.0;
    double totalMs = 0.0;
};

std::map<std::uint64_t, V2StaticTile> g_v2_static_tiles;
std::uint32_t g_v2_static_tile_triangles = 0;
std::uint32_t g_v2_max_static_tiles = kDefaultMaxStaticTiles;
std::uint32_t g_v2_max_static_tile_triangles = kDefaultMaxStaticTileTriangles;

void loadStaticTileBudgetOverrides();
void clearStaticTiles();

physics_physx::V2ModuleLease g_v2_lease;
physics_physx::V2ApiTable g_v2_api{};
OgsrPhysX2SceneHandle g_v2_scene{};
bool g_v2_scene_alive = false;
bool g_v2_scene_destroy_failed = false;
std::uint64_t g_v2_frame_id = 0;
std::uint64_t g_v2_step_index = 0;
std::uint32_t g_v2_live_instances = 0;
std::uint32_t g_v2_scene_triangles = 0;
Fvector g_v2_static_center{};
float g_v2_static_half_xz = 0.0f;
float g_v2_static_half_y = 0.0f;
bool g_v2_static_bounds_valid = false;

void parseRagdollFlags()
{
    if (g_ragdoll_parsed)
        return;
    g_ragdoll_parsed = true;
    const char* params = Core.Params;
    if (!params)
        return;

    std::vector<std::string> tokens;
    const char* cursor = params;
    while (*cursor)
    {
        while (*cursor == ' ')
            ++cursor;
        if (!*cursor)
            break;
        const char* start = nullptr;
        if (*cursor == '"')
        {
            ++cursor;
            start = cursor;
            while (*cursor && *cursor != '"')
                ++cursor;
            tokens.emplace_back(start, static_cast<std::size_t>(cursor - start));
            if (*cursor == '"')
                ++cursor;
        }
        else
        {
            start = cursor;
            while (*cursor && *cursor != ' ')
                ++cursor;
            tokens.emplace_back(start, static_cast<std::size_t>(cursor - start));
        }
    }

    for (std::size_t index = 0; index < tokens.size(); ++index)
    {
        if (tokens[index] == "-physx_all_humans")
            g_ragdoll_all_humans = true;
    }

    for (std::size_t index = 0; index < tokens.size(); ++index)
    {
        if (tokens[index] == "-physx_ragdoll")
        {
            if (index + 1 < tokens.size() && !tokens[index + 1].empty() && tokens[index + 1][0] != '-')
            {
                g_ragdoll_target = tokens[index + 1];
                g_ragdoll_requested = true;
            }
            else if (g_ragdoll_all_humans)
            {
                g_ragdoll_requested = true;
            }
            else
            {
                Msg("! ph_runtime: -physx_ragdoll needs an NPC name or section");
            }
        }
        else if (tokens[index] == "-physx_ragdoll_smoke")
        {
            g_ragdoll_smoke = true;
        }
        else if (tokens[index] == "-physx_ragdoll_force_free")
        {
            g_ragdoll_force_free = true;
        }
        else if (tokens[index] == "-physx_ragdoll_ode_smoke")
        {
            if (index + 1 < tokens.size() && !tokens[index + 1].empty() && tokens[index + 1][0] != '-')
            {
                g_ragdoll_target = tokens[index + 1];
                g_ragdoll_requested = true;
                g_ragdoll_smoke = true;
                g_ragdoll_ode_smoke = true;
            }
            else
            {
                Msg("! ph_runtime: -physx_ragdoll_ode_smoke needs an NPC name or section");
            }
        }
        else if (tokens[index] == "-physx_ragdoll_multi")
        {
            g_ragdoll_multi = true;
        }
        else if (tokens[index] == "-physx_multi_instances")
        {
            g_ragdoll_multi_instances = true;
        }
        else if (tokens[index] == "-physx_all_humans")
        {
            g_ragdoll_all_humans = true;
        }
        else if (tokens[index] == "-physx_static_tiles")
        {
            g_ragdoll_static_tiles = true;
        }
        else if (tokens[index] == "-physx_static_tiles_smoke")
        {
            g_ragdoll_static_tiles = true;
            g_ragdoll_static_tiles_smoke = true;
        }
        else if (tokens[index] == "-physx_static_tiles_boundary_smoke")
        {
            g_ragdoll_static_tiles = true;
            g_ragdoll_static_tiles_smoke = true;
            g_ragdoll_static_tiles_boundary_smoke = true;
        }
        else if (tokens[index] == "-physx_g2_single_smoke")
        {
            g_ragdoll_g2_single_smoke = true;
        }
        else if (tokens[index] == "-physx_g3_real_hit")
        {
            g_ragdoll_g3_real_hit = true;
        }
        else if (tokens[index] == "-physx_g3_weapon")
        {
            if (index + 1 < tokens.size() && !tokens[index + 1].empty() && tokens[index + 1][0] != '-')
                g_ragdoll_g3_weapon = tokens[index + 1];
        }
        else if (tokens[index] == "-physx_g3_aim_bone")
        {
            if (index + 1 < tokens.size() && !tokens[index + 1].empty() && tokens[index + 1][0] != '-')
                g_ragdoll_g3_aim_bone = tokens[index + 1];
        }
        else if (tokens[index] == "-physx_g3_target_offset")
        {
            if (index + 1 < tokens.size())
            {
                float x = 0.0f;
                float z = -3.0f;
                if (std::sscanf(tokens[index + 1].c_str(), "%f,%f", &x, &z) == 2)
                {
                    g_ragdoll_g3_target_offset_x = x;
                    g_ragdoll_g3_target_offset_z = z;
                }
                else
                {
                    Msg("! ph_runtime: -physx_g3_target_offset expects x,z; using (%.2f,%.2f)",
                        g_ragdoll_g3_target_offset_x, g_ragdoll_g3_target_offset_z);
                }
            }
        }
        else if (tokens[index] == "-physx_g3_max_shots")
        {
            if (index + 1 < tokens.size())
            {
                char* end = nullptr;
                const unsigned long value = std::strtoul(tokens[index + 1].c_str(), &end, 10);
                if (end && *end == 0 && value >= 1 && value <= 20)
                    g_ragdoll_g3_max_shots = static_cast<std::uint32_t>(value);
                else
                    Msg("! ph_runtime: -physx_g3_max_shots expects 1..20; using %u",
                        g_ragdoll_g3_max_shots);
            }
        }
        else if (tokens[index] == "-physx_max_ragdolls")
        {
            if (index + 1 < tokens.size())
            {
                char* end = nullptr;
                const unsigned long value = std::strtoul(tokens[index + 1].c_str(), &end, 10);
                if (end && *end == 0 && value >= kMinRagdolls && value <= kMaxRagdollsExperimental)
                {
                    g_ragdoll_max_ragdolls = static_cast<std::uint32_t>(value);
                }
                else
                {
                    Msg("! ph_runtime: -physx_max_ragdolls expects %u..%u; using %u", kMinRagdolls,
                        kMaxRagdollsExperimental, g_ragdoll_max_ragdolls);
                }
            }
            else
            {
                Msg("! ph_runtime: -physx_max_ragdolls needs a value; using %u", g_ragdoll_max_ragdolls);
            }
        }
        else if (tokens[index] == "-physx_hit_impulse_scale")
        {
            if (index + 1 < tokens.size())
            {
                char* end = nullptr;
                const double value = std::strtod(tokens[index + 1].c_str(), &end);
                if (end && *end == 0 && value >= 0.0 && value <= 10.0)
                    g_ragdoll_hit_impulse_scale = static_cast<float>(value);
                else
                    Msg("! ph_runtime: -physx_hit_impulse_scale expects 0..10; using %.2f",
                        g_ragdoll_hit_impulse_scale);
            }
        }
        else if (tokens[index] == "-physx_hit_impulse_max")
        {
            if (index + 1 < tokens.size())
            {
                char* end = nullptr;
                const double value = std::strtod(tokens[index + 1].c_str(), &end);
                if (end && *end == 0 && std::isfinite(value) && value >= 0.0 && value <= 1000.0)
                {
                    g_ragdoll_hit_impulse_max = static_cast<float>(value);
                    g_ragdoll_hit_impulse_max_explicit = true;
                }
                else
                {
                    Msg("! ph_runtime: -physx_hit_impulse_max expects finite 0..1000; value ignored "
                        "(explicit=%d)",
                        g_ragdoll_hit_impulse_max_explicit ? 1 : 0);
                }
            }
            else
            {
                Msg("! ph_runtime: -physx_hit_impulse_max needs a value; value ignored (explicit=%d)",
                    g_ragdoll_hit_impulse_max_explicit ? 1 : 0);
            }
        }
        else if (tokens[index] == "-physx_ragdoll_diag")
        {
            if (index + 1 < tokens.size())
            {
                if (tokens[index + 1] == "0" || tokens[index + 1] == "off")
                    g_ragdoll_diag = false;
                else if (tokens[index + 1] == "1" || tokens[index + 1] == "on")
                    g_ragdoll_diag = true;
                else
                    Msg("! ph_runtime: -physx_ragdoll_diag expects 0|1; using %d", g_ragdoll_diag ? 1 : 0);
            }
        }
        else if (tokens[index] == "-physx_ragdoll_reload")
        {
            g_ragdoll_reload = true;
        }
        else if (tokens[index] == "-physx_self_collision")
        {
            g_ragdoll_self_collision = true;
        }
        else if (tokens[index] == "-physx_collider_vis")
        {
            g_ragdoll_collider_vis = true;
        }
        else if (tokens[index] == "-physx_ragdoll_visual_replace")
        {
            g_ragdoll_visual_replace = true;
        }
        else if (tokens[index] == "-physx_ragdoll_visual_replace_ode")
        {
            g_ragdoll_visual_replace_ode = true;
        }
    }
    if (g_ragdoll_smoke)
        g_ragdoll_force_free = true;
    if (g_ragdoll_g3_real_hit)
    {
        // G3 is a single-target real-weapon harness. It uses the v2 backend
        // and the normal free-ragdoll death path, but never injects a
        // synthetic SHit.
        g_ragdoll_requested = true;
        g_ragdoll_multi_instances = true;
        g_ragdoll_force_free = true;
        g_ragdoll_smoke = true;
        g_ragdoll_g2_single_smoke = true;
    }
    if (g_ragdoll_all_humans)
    {
        // Experimental all-human mode: no explicit NPC target, v2 backend and
        // free-ragdoll death path are mandatory.
        g_ragdoll_requested = true;
        g_ragdoll_multi_instances = true;
        g_ragdoll_force_free = true;
    }
    if (g_ragdoll_multi && g_ragdoll_multi_instances)
    {
        Msg("! ph_runtime: -physx_multi_instances conflicts with diagnostic -physx_ragdoll_multi; "
            "v2 path disabled");
        g_ragdoll_multi_instances = false;
        g_ragdoll_requested = false;
    }
    const float effectiveHitMax =
        (g_ragdoll_all_humans && !g_ragdoll_hit_impulse_max_explicit) ? kAllHumanDefaultHitImpulseMax
                                                                       : g_ragdoll_hit_impulse_max;
    if (g_ragdoll_all_humans)
    {
        Msg("ph_runtime: all-human hit impulse policy default=30 effective=%g explicit=%d",
            effectiveHitMax, g_ragdoll_hit_impulse_max_explicit ? 1 : 0);
        Msg("ph_runtime: all-human v2 mode requested max_ragdolls=%u static_tiles=%d hit_scale=%.2f "
            "hit_max=%.2f explicit=%d self_collision=%d diag=%d",
            g_ragdoll_max_ragdolls, g_ragdoll_static_tiles ? 1 : 0, g_ragdoll_hit_impulse_scale,
            effectiveHitMax, g_ragdoll_hit_impulse_max_explicit ? 1 : 0,
            g_ragdoll_self_collision ? 1 : 0, g_ragdoll_diag ? 1 : 0);
    }
    else if (g_ragdoll_multi_instances)
        Msg("ph_runtime: multi-instance v2 mode requested max_ragdolls=%u static_tiles=%d hit_max=%.2f "
            "explicit=%d self_collision=%d",
            g_ragdoll_max_ragdolls, g_ragdoll_static_tiles ? 1 : 0, g_ragdoll_hit_impulse_max,
            g_ragdoll_hit_impulse_max_explicit ? 1 : 0, g_ragdoll_self_collision ? 1 : 0);
    if (g_ragdoll_self_collision)
        Msg("ph_runtime: selective self-collision allowed pairs: 0-8 0-10 5-8 5-10 8-10; "
            "other same-instance pairs suppressed");
    if (g_ragdoll_g3_real_hit)
        Msg("ph_runtime: G3 real weapon harness weapon=[%s] aim_bone=[%s] target_offset=(%.2f,%.2f) "
            "max_shots=%u",
            g_ragdoll_g3_weapon.c_str(), g_ragdoll_g3_aim_bone.c_str(), g_ragdoll_g3_target_offset_x,
            g_ragdoll_g3_target_offset_z, g_ragdoll_g3_max_shots);
    if (g_ragdoll_requested)
        Msg("ph_runtime: NPC ragdoll requested target=[%s] smoke=%d force_free=%d", g_ragdoll_target.c_str(),
            g_ragdoll_smoke ? 1 : 0, g_ragdoll_force_free ? 1 : 0);
}

} // namespace

bool OgsrPhysXRagdollRequested()
{
    parseRagdollFlags();
    return g_ragdoll_requested && !g_ragdoll_ode_smoke;
}

bool OgsrPhysXRagdollTargetRequested()
{
    parseRagdollFlags();
    return g_ragdoll_requested;
}

bool OgsrPhysXRagdollOdeSmokeRequested()
{
    parseRagdollFlags();
    return g_ragdoll_ode_smoke;
}

bool OgsrPhysXRagdollMultiRequested()
{
    parseRagdollFlags();
    return g_ragdoll_multi;
}

bool OgsrPhysXRagdollMultiInstancesRequested()
{
    parseRagdollFlags();
    return g_ragdoll_multi_instances;
}

bool OgsrPhysXRagdollAllHumansRequested()
{
    parseRagdollFlags();
    return g_ragdoll_all_humans;
}

bool OgsrPhysXRagdollStaticTilesRequested()
{
    parseRagdollFlags();
    return g_ragdoll_static_tiles;
}

bool OgsrPhysXRagdollSelfCollisionRequested()
{
    parseRagdollFlags();
    return g_ragdoll_self_collision;
}

bool OgsrPhysXRagdollColliderVisRequested()
{
    parseRagdollFlags();
    return g_ragdoll_collider_vis;
}

bool OgsrPhysXRagdollStaticTilesSmokeRequested()
{
    parseRagdollFlags();
    return g_ragdoll_static_tiles_smoke;
}

bool OgsrPhysXRagdollStaticTilesBoundarySmokeRequested()
{
    parseRagdollFlags();
    return g_ragdoll_static_tiles_boundary_smoke;
}

bool OgsrPhysXRagdollG2SingleSmokeRequested()
{
    parseRagdollFlags();
    return g_ragdoll_g2_single_smoke;
}

bool OgsrPhysXRagdollG3RealHitRequested()
{
    parseRagdollFlags();
    return g_ragdoll_g3_real_hit;
}

const char* OgsrPhysXRagdollG3Weapon()
{
    parseRagdollFlags();
    return g_ragdoll_g3_weapon.c_str();
}

const char* OgsrPhysXRagdollG3AimBone()
{
    parseRagdollFlags();
    return g_ragdoll_g3_aim_bone.c_str();
}

float OgsrPhysXRagdollG3TargetOffsetX()
{
    parseRagdollFlags();
    return g_ragdoll_g3_target_offset_x;
}

float OgsrPhysXRagdollG3TargetOffsetZ()
{
    parseRagdollFlags();
    return g_ragdoll_g3_target_offset_z;
}

std::uint32_t OgsrPhysXRagdollG3MaxShots()
{
    parseRagdollFlags();
    return g_ragdoll_g3_max_shots;
}

std::uint32_t OgsrPhysXRagdollMaxRagdolls()
{
    parseRagdollFlags();
    return g_ragdoll_max_ragdolls;
}

float OgsrPhysXRagdollHitImpulseScale()
{
    parseRagdollFlags();
    return g_ragdoll_hit_impulse_scale;
}

float OgsrPhysXRagdollHitImpulseMax()
{
    parseRagdollFlags();
    if (g_ragdoll_all_humans && !g_ragdoll_hit_impulse_max_explicit)
        return kAllHumanDefaultHitImpulseMax;
    return g_ragdoll_hit_impulse_max;
}

bool OgsrPhysXRagdollDiagRequested()
{
    parseRagdollFlags();
    return g_ragdoll_diag;
}

bool OgsrPhysXRagdollReloadRequested()
{
    parseRagdollFlags();
    return g_ragdoll_reload;
}

void OgsrPhysXRagdollRequestLevelDisconnect() { Engine.Event.Defer("KERNEL:disconnect"); }

bool OgsrPhysXRagdollMatches(const char* name, const char* section)
{
    parseRagdollFlags();
    if (!g_ragdoll_requested)
        return false;
    if (g_ragdoll_all_humans)
        return true;
    // The production multi-instance mode accepts any stalker; the target name
    // only seeds the smoke scenario.
    if (g_ragdoll_multi_instances)
        return true;
    if (g_ragdoll_target.empty())
        return false;
    if (name && _stricmp(name, g_ragdoll_target.c_str()) == 0)
        return true;
    if (section && _stricmp(section, g_ragdoll_target.c_str()) == 0)
        return true;
    return false;
}

bool OgsrPhysXRagdollTargetMatches(const char* name, const char* section)
{
    parseRagdollFlags();
    if (!g_ragdoll_requested)
        return false;
    if (g_ragdoll_target.empty())
        return true;
    if (name && _stricmp(name, g_ragdoll_target.c_str()) == 0)
        return true;
    if (section && _stricmp(section, g_ragdoll_target.c_str()) == 0)
        return true;
    return false;
}

bool OgsrPhysXRagdollForceFree() { return g_ragdoll_force_free; }
bool OgsrPhysXRagdollSmokeRequested()
{
    parseRagdollFlags();
    return g_ragdoll_smoke;
}
bool OgsrPhysXRagdollVisualReplaceRequested()
{
    parseRagdollFlags();
    return g_ragdoll_visual_replace;
}
bool OgsrPhysXRagdollVisualReplaceOdeRequested()
{
    parseRagdollFlags();
    return g_ragdoll_visual_replace_ode;
}
bool OgsrPhysXRagdollRuntimeAlive()
{
    parseRagdollFlags();
    if (g_ragdoll_multi_instances)
        return true; // the v2 scene is created lazily on the first activation
    return g_ragdoll_alive && g_ragdoll_handle != nullptr;
}
bool OgsrPhysXRagdollRuntimeHasInstance()
{
    if (g_ragdoll_multi_instances)
        return false; // capacity is enforced by the v2 scene and live counter
    return g_ragdoll_active;
}
#endif

namespace
{
bool isSmokeRequested()
{
    if (g_smoke_parsed)
        return g_smoke;
    g_smoke_parsed = true;
    const char* params = Core.Params;
    constexpr char flag[] = "-physx_runtime_smoke";
    if (params)
    {
        const char* match = strstr(params, flag);
        if (match)
        {
            const bool leftBoundary = match == params || match[-1] == ' ';
            const bool rightBoundary = match[sizeof(flag) - 1] == 0 || match[sizeof(flag) - 1] == ' ';
            g_smoke = leftBoundary && rightBoundary;
        }
    }
    return g_smoke;
}

void smokeTick()
{
    if (!isSmokeRequested())
        return;
    static std::uint32_t frame = 0;
    ++frame;
    if (frame == 240)
    {
        g_smoke_pause_active = true;
        Device.Pause(TRUE, TRUE, TRUE, "physx_runtime_smoke");
        Msg("ph_runtime_smoke: pause enabled");
    }
    else if (frame == 360)
    {
        g_smoke_pause_active = false;
        Device.Pause(FALSE, TRUE, TRUE, "physx_runtime_smoke");
        Msg("ph_runtime_smoke: pause disabled");
    }
    else if (frame == 480)
    {
        Device.time_factor(0.5f);
        Msg("ph_runtime_smoke: time_factor=0.5");
    }
    else if (frame == 600)
    {
        Device.time_factor(1.0f);
        Msg("ph_runtime_smoke: time_factor=1.0");
    }
    else if (frame == 720)
    {
        Msg("ph_runtime_smoke: requesting level disconnect");
        Engine.Event.Defer("KERNEL:disconnect");
    }
}

#ifdef OGSR_DIAGNOSTIC_RUNTIME_WORKAROUND
std::string startArgumentValue()
{
    const char* params = Core.Params;
    if (!params)
        return {};
    const char* flag = std::strstr(params, "-start ");
    if (!flag)
        return {};
    const char* value = flag + std::strlen("-start ");
    while (*value == ' ')
        ++value;
    if (*value == '"')
    {
        ++value;
        const char* end = std::strchr(value, '"');
        return end ? std::string(value, static_cast<std::size_t>(end - value)) : std::string{};
    }
    const char* end = std::strchr(value, ' ');
    return std::string(value, end ? static_cast<std::size_t>(end - value) : std::strlen(value));
}

std::string startServerOptions(const std::string& start)
{
    const char* server = std::strstr(start.c_str(), "server(");
    if (!server)
        return {};
    server += std::strlen("server(");
    const char* end = std::strchr(server, ')');
    return end ? std::string(server, static_cast<std::size_t>(end - server)) : std::string{};
}

std::string startClientOptions(const std::string& start)
{
    const char* client = std::strstr(start.c_str(), "client(");
    if (!client)
        return "localhost";
    client += std::strlen("client(");
    const char* end = std::strchr(client, ')');
    return end ? std::string(client, static_cast<std::size_t>(end - client)) : std::string{"localhost"};
}

// Smoke-only reload. The engine crash path after a bare disconnect dereferences
// Level() in CGamePersistent, so the replacement level is created synchronously
// from a frame callback that runs after the disconnect handler but before
// GamePersistent::OnFrame. Reload the same -start target when possible so the
// diagnostic can activate a fresh ragdoll on the replacement level; fall back to
// a new game when the command line has no -start value.
class CSmokeReload final : public pureFrame
{
public:
    void OnFrame() override
    {
        Device.seqFrame.Remove(this);
        const std::string start = startArgumentValue();
        std::string server = startServerOptions(start);
        std::string client = startClientOptions(start);
        if (server.empty())
        {
            server = "all/single/alife/new";
            client = "localhost";
        }
        Msg("ph_runtime_smoke: reloading level server=[%s] client=[%s]", server.c_str(), client.c_str());
        Engine.Event.Signal("KERNEL:start", u64(xr_strdup(server.c_str())), u64(xr_strdup(client.c_str())));
    }
};

CSmokeReload g_smoke_reload;
bool g_smoke_reload_done = false;
int g_ragdoll_reload_count = 0;
#endif

bool isRequested()
{
    if (g_requested_parsed)
        return g_requested;
    g_requested_parsed = true;
    const char* params = Core.Params;
    if (!params)
        return false;
    constexpr char flag[] = "-physx_runtime";
    constexpr std::size_t flagLength = sizeof(flag) - 1;
    const char* match = params;
    while ((match = strstr(match, flag)) != nullptr)
    {
        const bool leftBoundary = match == params || match[-1] == ' ';
        const bool rightBoundary = match[flagLength] == 0 || match[flagLength] == ' ';
        if (leftBoundary && rightBoundary)
        {
            g_requested = true;
            return true;
        }
        ++match;
    }
    return false;
}

void unloadApi()
{
    if (g_api.module)
        FreeLibrary(g_api.module);
    g_api = Api{};
}

bool loadApi()
{
    if (g_api.module)
        return true;
    g_api.module = LoadLibraryA("ogsr_physx_runtime.dll");
    if (!g_api.module)
    {
        Msg("! ph_runtime: cannot load ogsr_physx_runtime.dll (error=%lu); check PhysX runtime DLLs and the bridge",
            GetLastError());
        return false;
    }
    g_api.create = reinterpret_cast<CreateFn>(GetProcAddress(g_api.module, "OgsrPhysXRuntimeCreate"));
    g_api.destroy = reinterpret_cast<DestroyFn>(GetProcAddress(g_api.module, "OgsrPhysXRuntimeDestroy"));
    g_api.setStaticMesh = reinterpret_cast<SetStaticMeshFn>(GetProcAddress(g_api.module, "OgsrPhysXRuntimeSetStaticMesh"));
    g_api.createTestBox = reinterpret_cast<CreateTestBoxFn>(GetProcAddress(g_api.module, "OgsrPhysXRuntimeCreateTestBox"));
    g_api.step = reinterpret_cast<StepFn>(GetProcAddress(g_api.module, "OgsrPhysXRuntimeStep"));
    g_api.getState = reinterpret_cast<GetStateFn>(GetProcAddress(g_api.module, "OgsrPhysXRuntimeGetState"));
#ifdef OGSR_PHYSX_RAGDOLL
    if (OgsrPhysXRagdollRequested())
    {
        g_api.ragdollRuntimeCreate =
            reinterpret_cast<RagdollRuntimeCreateFn>(GetProcAddress(g_api.module, "OgsrPhysXRagdollRuntimeCreate"));
        g_api.ragdollRuntimeDestroy =
            reinterpret_cast<RagdollRuntimeDestroyFn>(GetProcAddress(g_api.module, "OgsrPhysXRagdollRuntimeDestroy"));
        g_api.ragdollSetStaticMesh =
            reinterpret_cast<RagdollSetStaticMeshFn>(GetProcAddress(g_api.module, "OgsrPhysXRagdollSetStaticMesh"));
        g_api.ragdollCreate = reinterpret_cast<RagdollCreateFn>(GetProcAddress(g_api.module, "OgsrPhysXRagdollCreate"));
        g_api.ragdollDestroy = reinterpret_cast<RagdollDestroyFn>(GetProcAddress(g_api.module, "OgsrPhysXRagdollDestroy"));
        g_api.ragdollStep = reinterpret_cast<RagdollStepFn>(GetProcAddress(g_api.module, "OgsrPhysXRagdollStep"));
        g_api.ragdollGetPose = reinterpret_cast<RagdollGetPoseFn>(GetProcAddress(g_api.module, "OgsrPhysXRagdollGetPose"));
        g_api.ragdollApplyImpulse =
            reinterpret_cast<RagdollApplyImpulseFn>(GetProcAddress(g_api.module, "OgsrPhysXRagdollApplyImpulse"));
        g_api.ragdollApplyCentralImpulse = reinterpret_cast<RagdollApplyCentralImpulseFn>(
            GetProcAddress(g_api.module, "OgsrPhysXRagdollApplyCentralImpulse"));
        g_api.ragdollGetBodyMass =
            reinterpret_cast<RagdollGetBodyMassFn>(GetProcAddress(g_api.module, "OgsrPhysXRagdollGetBodyMass"));
        g_api.ragdollGetInfo = reinterpret_cast<RagdollGetInfoFn>(GetProcAddress(g_api.module, "OgsrPhysXRagdollGetInfo"));
        g_api.ragdollGetDiagnostics =
            reinterpret_cast<RagdollGetDiagnosticsFn>(GetProcAddress(g_api.module, "OgsrPhysXRagdollGetDiagnostics"));
        if (!g_api.ragdollRuntimeCreate || !g_api.ragdollRuntimeDestroy || !g_api.ragdollSetStaticMesh ||
            !g_api.ragdollCreate || !g_api.ragdollDestroy || !g_api.ragdollStep || !g_api.ragdollGetPose ||
            !g_api.ragdollApplyImpulse || !g_api.ragdollApplyCentralImpulse || !g_api.ragdollGetBodyMass ||
            !g_api.ragdollGetInfo || !g_api.ragdollGetDiagnostics)
        {
            Msg("! ph_runtime: bridge DLL is missing the NPC ragdoll exports");
            unloadApi();
            return false;
        }
    }
#endif
    if (!g_api.create || !g_api.destroy || !g_api.setStaticMesh || !g_api.createTestBox || !g_api.step || !g_api.getState)
    {
        Msg("! ph_runtime: bridge DLL is missing expected exports");
        unloadApi();
        return false;
    }
    return true;
}

void shutdown()
{
#ifdef OGSR_PHYSX_RAGDOLL
    if (g_v2_scene_alive)
    {
        OgsrPhysXRagdollReleaseAllResult release = OgsrPhysXRagdollReleaseAllV2();
        if (!release.complete())
        {
            Msg("ph_runtime: v2 instance teardown incomplete: remaining=%u; retrying", release.remaining);
            release = OgsrPhysXRagdollReleaseAllV2();
        }
        for (const OgsrPhysX2RagdollHandle& handle : release.handles)
            Msg("! ph_runtime: quarantined instance scene=(%u,%u) instance=(%u,%u)", handle.sceneSlot,
                handle.sceneGeneration, handle.instanceSlot, handle.instanceGeneration);
        Msg("ph_runtime: v2 teardown state: remaining=%u live=%u lease_refs=%u", release.remaining,
            OgsrPhysXRagdollV2LiveCount(), g_v2_lease.resourceRefs());

        char destroyError[kErrorSize] = {};
        bool destroyed = false;
        for (int attempt = 0; attempt < 2 && !destroyed; ++attempt)
        {
            destroyed = g_v2_api.sceneDestroy &&
                        g_v2_api.sceneDestroy(g_v2_scene, destroyError, sizeof(destroyError));
            if (!destroyed)
                Msg("! ph_runtime: v2 SceneDestroy attempt %d failed: %s", attempt + 1,
                    destroyError[0] ? destroyError : "no error");
        }
        if (!destroyed)
        {
            Msg("! ph_runtime: v2 SceneDestroy failed (%s); keeping scene and DLL loaded",
                destroyError[0] ? destroyError : "no error");
            g_v2_scene_destroy_failed = true;
        }
        else
        {
            // SceneDestroy is authoritative: every backend instance in the
            // scene is gone, including any quarantined ones. Reconcile the game
            // states and their per-instance lease references.
            if (!release.complete())
                OgsrPhysXRagdollReconcileSceneDestroyV2();
            g_v2_scene_alive = false;
            g_v2_scene_destroy_failed = false;
            g_v2_static_bounds_valid = false;
            clearStaticTiles();
            g_v2_scene = OgsrPhysX2SceneHandle{};
            g_v2_live_instances = 0;
            g_v2_api = physics_physx::V2ApiTable{};
            g_v2_lease.releaseResourceRef();
            Msg("ph_runtime: v2 scene destroyed; lease_refs=%u", g_v2_lease.resourceRefs());
        }
    }
    if (!g_v2_scene_alive && g_v2_lease.isLoaded())
    {
        std::string unloadError;
        if (!g_v2_lease.unload(&unloadError))
            Msg("! ph_runtime: v2 DLL unload deferred: %s", unloadError.c_str());
    }
    if (g_ragdoll_handle)
    {
        if (g_api.ragdollRuntimeDestroy)
            g_api.ragdollRuntimeDestroy(g_ragdoll_handle);
        g_ragdoll_handle = nullptr;
        g_ragdoll_alive = false;
        g_ragdoll_active = false;
        Msg("ph_runtime: NPC ragdoll runtime destroyed");
    }
#endif
    const bool hadContext = g_handle != nullptr;
    if (g_handle && g_api.destroy)
        g_api.destroy(g_handle);
    g_handle = nullptr;
    unloadApi();
    if (hadContext)
        Msg("ph_runtime: level context destroyed");
}

bool readStaticFragmentSized(const Fvector& center,
                             float halfXZ,
                             float halfY,
                             std::uint32_t maxTriangles,
                             std::vector<float>& vertices,
                             std::vector<std::uint32_t>& indices,
                             std::uint32_t& triangleCount,
                             bool* truncated = nullptr)
{
    Fvector zAxis(0.0f, 0.0f, 1.0f);
    Fvector yAxis(0.0f, 1.0f, 0.0f);
    Fvector sizes(halfXZ * 2.0f, halfY * 2.0f, halfXZ * 2.0f);
    std::vector<Fvector> triangleVertices;
    const bool found = Level().ObjectSpace.BoxQuery(center, zAxis, yAxis, sizes, &triangleVertices);
    if (!found || triangleVertices.empty())
    {
        Msg("! ph_runtime: no static level triangles in the diagnostic box");
        return false;
    }

    const std::size_t sourceTriangles = triangleVertices.size() / 3;
    if (truncated)
        *truncated = sourceTriangles > maxTriangles;
    triangleCount = static_cast<std::uint32_t>(std::min<std::size_t>(sourceTriangles, maxTriangles));
    vertices.reserve(triangleCount * 9);
    indices.reserve(triangleCount * 3);
    for (std::size_t triangle = 0; triangle < triangleCount; ++triangle)
    {
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const Fvector& source = triangleVertices[triangle * 3 + corner];
            vertices.push_back(source.x);
            vertices.push_back(source.y);
            vertices.push_back(source.z);
            indices.push_back(static_cast<std::uint32_t>(indices.size()));
        }
    }
    return true;
}

bool readStaticFragmentTile(const Fvector& center,
                            float halfXZ,
                            float halfY,
                            std::uint32_t maxTriangles,
                            std::vector<float>& vertices,
                            std::vector<std::uint32_t>& indices,
                            std::uint32_t& triangleCount,
                            std::uint32_t& sourceTriangles,
                            bool& truncated,
                            std::string* error)
{
    Fvector zAxis(0.0f, 0.0f, 1.0f);
    Fvector yAxis(0.0f, 1.0f, 0.0f);
    Fvector sizes((halfXZ + kV2StaticTileQueryMargin) * 2.0f,
                  (halfY + kV2StaticTileQueryMargin) * 2.0f,
                  (halfXZ + kV2StaticTileQueryMargin) * 2.0f);
    std::vector<Fvector> triangleVertices;
    const bool found = Level().ObjectSpace.BoxQuery(center, zAxis, yAxis, sizes, &triangleVertices);
    if (!found || triangleVertices.empty())
    {
        if (error)
            *error = "no static level triangles in the tile";
        return false;
    }

    const Fvector minimum(center.x - halfXZ, center.y - halfY, center.z - halfXZ);
    const Fvector maximum(center.x + halfXZ, center.y + halfY, center.z + halfXZ);
    sourceTriangles = static_cast<std::uint32_t>(triangleVertices.size() / 3);
    triangleCount = 0;
    truncated = false;
    for (std::size_t triangle = 0; triangle < triangleVertices.size() / 3; ++triangle)
    {
        const Fvector& a = triangleVertices[triangle * 3 + 0];
        const Fvector& b = triangleVertices[triangle * 3 + 1];
        const Fvector& c = triangleVertices[triangle * 3 + 2];
        const Fvector centroid((a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f,
                               (a.z + b.z + c.z) / 3.0f);
        // Centroid ownership: each triangle belongs to exactly one tile even
        // when adjacent query boxes overlap. This avoids duplicate static
        // triangles at cell boundaries.
        if (centroid.x < minimum.x || centroid.x >= maximum.x || centroid.y < minimum.y ||
            centroid.y >= maximum.y || centroid.z < minimum.z || centroid.z >= maximum.z)
            continue;
        if (triangleCount >= maxTriangles)
        {
            truncated = true;
            if (error)
                *error = "static tile exceeds the per-tile triangle cap";
            return false;
        }
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const Fvector& source = triangleVertices[triangle * 3 + corner];
            vertices.push_back(source.x);
            vertices.push_back(source.y);
            vertices.push_back(source.z);
            indices.push_back(static_cast<std::uint32_t>(indices.size()));
        }
        ++triangleCount;
    }
    if (triangleCount == 0)
    {
        if (error)
            *error = "no static triangles after centroid filtering";
        return false;
    }
    return true;
}

bool readStaticFragment(const Fvector& center,
                        std::vector<float>& vertices,
                        std::vector<std::uint32_t>& indices,
                        std::uint32_t& triangleCount)
{
    return readStaticFragmentSized(center, kStaticHalfExtentXZ, kStaticHalfExtentY, kMaxStaticTriangles, vertices,
                                   indices, triangleCount);
}

void prepareArchiveRuntime()
{
#ifdef OGSR_DIAGNOSTIC_RUNTIME_WORKAROUND
    // The archive test runtime has no usable main-menu UI factory. Keep this
    // behind the explicit archive-workaround build flag; normal P2 builds never
    // compile or execute this block.
    if (!g_archive_runtime_prepared)
    {
        g_archive_runtime_prepared = true;
        if (CMainMenu* menu = MainMenu())
        {
            Device.seqFrame.Remove(menu);
            if (menu->IsActive() && Level().CurrentEntity())
                menu->Activate(false);
            else if (menu->IsActive())
                Msg("ph_runtime: archive runtime workaround defers menu deactivation (no player entity)");
            Device.seqRender.Remove(menu);
            Device.Pause(FALSE, TRUE, TRUE, "physx_runtime");
            if (g_pGameLevel)
            {
                Device.seqFrame.Add(g_pGameLevel);
                Device.seqRender.Add(g_pGameLevel);
            }
            if (load_screen_renderer.b_registered)
            {
                load_screen_renderer.stop();
                Msg("ph_runtime: archive runtime workaround stopped the loading screen");
            }
            const CObject* current = Level().CurrentEntity();
            Msg("ph_runtime: archive runtime workaround menu_active=%d load_screen=%d level_ready=%d "
                "camera=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f,%.2f) entity_id=%u pos=(%.2f,%.2f,%.2f)",
                menu->IsActive() ? 1 : 0, load_screen_renderer.b_registered ? 1 : 0,
                g_pGameLevel ? (g_pGameLevel->bReady ? 1 : 0) : 0, Device.vCameraPosition.x,
                Device.vCameraPosition.y, Device.vCameraPosition.z, Device.vCameraDirection.x,
                Device.vCameraDirection.y, Device.vCameraDirection.z, current ? current->ID() : 0,
                current ? current->Position().x : 0.0f, current ? current->Position().y : 0.0f,
                current ? current->Position().z : 0.0f);
            Msg("ph_runtime: archive runtime workaround deactivated the main menu");
        }
    }
    if (Device.Paused() && !g_smoke_pause_active)
        Device.Pause(FALSE, TRUE, TRUE, "physx_runtime");
    if (load_screen_renderer.b_registered)
    {
        load_screen_renderer.stop();
        static bool loggedLoadScreen = false;
        if (!loggedLoadScreen)
        {
            loggedLoadScreen = true;
            Msg("ph_runtime: archive runtime workaround stopped the loading screen");
        }
    }
    if (!g_smoke_pause_active)
    {
        if (CMainMenu* menu = MainMenu())
        {
            Device.seqRender.Remove(menu);
            Device.seqFrame.Remove(menu);
            if (menu->IsActive())
                menu->Activate(false);
        }
    }
    if (g_pGameLevel)
    {
        Device.seqFrame.Add(g_pGameLevel);
        Device.seqRender.Add(g_pGameLevel);
    }
#endif
}

#ifdef OGSR_PHYSX_RAGDOLL
void initializeRagdoll()
{
    if (g_ragdoll_alive || g_init_failed)
        return;
    shutdown();
    if (!loadApi())
    {
        g_init_failed = true;
        return;
    }
    OgsrPhysXRagdollConfig config{};
    config.fixedTimestep = kFixedStep;
    config.gravityY = -19.62f;
    config.workerThreads = 2;
    config.maxSubsteps = kMaxSubsteps;
    config.solverPositionIterations = 16;
    config.solverVelocityIterations = 4;
    config.contactOffset = 0.02f;
    config.restOffset = 0.0f;
    config.enableStabilization = 1;
    config.staticRootDensity = 300.0f;
    config.promoteStaticRoot = 1;
    config.linearDamping = 0.05f;
    config.angularDamping = 0.2f;
    config.disableSelfCollision = 1;
    config.collectDiagnostics = 1;
    char error[kErrorSize] = {};
    g_ragdoll_handle = g_api.ragdollRuntimeCreate(&config, error, sizeof(error));
    if (!g_ragdoll_handle)
    {
        Msg("! ph_runtime: NPC ragdoll runtime initialization failed: %s", error);
        g_init_failed = true;
        unloadApi();
        return;
    }
    g_ragdoll_alive = true;
    g_ragdoll_active = false;
    g_ragdoll_frames = 0;
    g_ragdoll_ms_total = 0.0;
    g_ragdoll_ms_max = 0.0;
    g_ragdoll_steps = 0;
    Msg("ph_runtime: NPC ragdoll runtime initialized");
}
bool initializeMultiRagdoll(const Fvector& center)
{
    if (g_v2_scene_alive)
        return true;
    if (g_init_failed)
        return false;

    loadStaticTileBudgetOverrides();
    shutdown();
    if (g_v2_scene_alive || g_v2_scene_destroy_failed)
    {
        Msg("! ph_runtime: previous v2 scene teardown is incomplete; refusing a new scene");
        g_init_failed = true;
        return false;
    }
    std::string error;
    if (!g_v2_lease.load("ogsr_physx_runtime.dll", &error))
    {
        Msg("! ph_runtime: cannot load the v2 DLL: %s", error.c_str());
        g_init_failed = true;
        return false;
    }

    physics_physx::V2Requirements requirements;
    requirements.requiredCapabilities = OGSR_PHYSX2_CAP_REQUIRED_P3_1;
    requirements.minimumScenes = 1;
    const std::uint32_t maxRagdolls = OgsrPhysXRagdollMaxRagdolls();
    requirements.minimumInstances = maxRagdolls;
    const physics_physx::V2NegotiationResult negotiation = g_v2_lease.negotiate(requirements);
    if (negotiation.status != physics_physx::V2NegotiationStatus::Ready)
    {
        Msg("! ph_runtime: v2 ABI negotiation failed: %s (%s)",
            physics_physx::v2NegotiationStatusName(negotiation.status), negotiation.error.c_str());
        g_v2_lease.unload();
        g_init_failed = true;
        return false;
    }
    g_v2_api = g_v2_lease.api();

    OgsrPhysX2SceneConfig config{};
    config.structSize = sizeof(config);
    config.fixedTimestep = kFixedStep;
    config.gravityY = -19.62f;
    config.workerThreads = 2;
    config.maxSubsteps = kMaxSubsteps;
    config.solverPositionIterations = 16;
    config.solverVelocityIterations = 4;
    config.contactOffset = 0.02f;
    config.restOffset = 0.0f;
    config.enableStabilization = 1;
    config.collectContacts = 1;
    config.maxInstances = maxRagdolls;

    char buffer[kErrorSize] = {};
    if (!g_v2_api.sceneCreate(&config, &g_v2_scene, buffer, sizeof(buffer)))
    {
        Msg("! ph_runtime: v2 SceneCreate failed: %s", buffer);
        g_v2_lease.unload();
        g_init_failed = true;
        return false;
    }
    // The scene owns a DLL resource from this point. If any later rollback
    // fails, the lease must keep the module loaded.
    g_v2_lease.addResourceRef();

    if (OgsrPhysXRagdollStaticTilesRequested())
    {
        // Opt-in tiled mode: the scene starts empty. Static geometry is added
        // per spatial tile through SceneAddStaticFragment, without replacing
        // the whole scene mesh.
        g_v2_scene_alive = true;
        g_v2_scene_destroy_failed = false;
        g_v2_frame_id = 0;
        g_v2_step_index = 0;
        g_v2_live_instances = 0;
        g_v2_scene_triangles = 0;
        g_v2_static_bounds_valid = false;
        g_v2_static_tiles.clear();
        g_v2_static_tile_triangles = 0;
        Msg("ph_runtime: v2 scene created scene=(%u,%u) caps=0x%08x max_instances=%u static_tiles=1 "
            "max_fragments=%u max_triangles=%u",
            g_v2_scene.slot, g_v2_scene.generation, negotiation.info.capabilityFlags, maxRagdolls,
            g_v2_max_static_tiles, g_v2_max_static_tile_triangles);
        return true;
    }

    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    std::uint32_t sourceTriangles = 0;
    bool truncated = false;
    const bool staticReady = readStaticFragmentSized(center, kV2StaticHalfExtentXZ, kV2StaticHalfExtentY, 8192,
                                                     vertices, indices, sourceTriangles, &truncated);
    if (truncated)
        Msg("! ph_runtime: v2 static fragment exceeds the per-fragment cap; refusing incomplete geometry");
    if (!staticReady || truncated ||
        !g_v2_api.sceneSetStaticMesh(g_v2_scene, vertices.data(),
                                     static_cast<std::uint32_t>(vertices.size() / 3), indices.data(),
                                     static_cast<std::uint32_t>(indices.size()), buffer, sizeof(buffer)))
    {
        Msg("! ph_runtime: v2 static mesh upload failed: %s", buffer);
        char destroyError[kErrorSize] = {};
        const bool destroyed =
            g_v2_api.sceneDestroy && g_v2_api.sceneDestroy(g_v2_scene, destroyError, sizeof(destroyError));
        if (!destroyed)
        {
            Msg("! ph_runtime: v2 rollback SceneDestroy failed (%s); keeping scene handle and DLL lease",
                destroyError[0] ? destroyError : "no error");
            g_v2_scene_alive = true;
            g_v2_scene_destroy_failed = true;
            g_init_failed = true;
            return false;
        }
        g_v2_scene = OgsrPhysX2SceneHandle{};
        g_v2_static_bounds_valid = false;
        clearStaticTiles();
        g_v2_lease.releaseResourceRef();
        g_v2_lease.unload();
        g_init_failed = true;
        return false;
    }

    g_v2_scene_alive = true;
    g_v2_scene_destroy_failed = false;
    g_v2_frame_id = 0;
    g_v2_step_index = 0;
    g_v2_live_instances = 0;
    g_v2_scene_triangles = sourceTriangles;
    g_v2_static_center = center;
    g_v2_static_half_xz = kV2StaticHalfExtentXZ;
    g_v2_static_half_y = kV2StaticHalfExtentY;
    g_v2_static_bounds_valid = true;
    Msg("ph_runtime: v2 scene created scene=(%u,%u) caps=0x%08x max_instances=%u static_tris=%u "
        "center=(%.2f,%.2f,%.2f) static_half=(%.1f,%.1f,%.1f)",
        g_v2_scene.slot, g_v2_scene.generation, negotiation.info.capabilityFlags, maxRagdolls, sourceTriangles,
        center.x, center.y, center.z, kV2StaticHalfExtentXZ, kV2StaticHalfExtentY, kV2StaticHalfExtentXZ);
    return true;
}

void loadStaticTileBudgetOverrides()
{
    char value[32] = {};
    DWORD length = GetEnvironmentVariableA("OGSR_PHYSX2_STATIC_TILE_MAX_FRAGMENTS", value, sizeof(value));
    if (length > 0)
    {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        if (end && *end == 0 && parsed >= 1 && parsed <= 64)
            g_v2_max_static_tiles = static_cast<std::uint32_t>(parsed);
    }
    length = GetEnvironmentVariableA("OGSR_PHYSX2_STATIC_TILE_MAX_TRIANGLES", value, sizeof(value));
    if (length > 0)
    {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        if (end && *end == 0 && parsed >= 1 && parsed <= 2000000)
            g_v2_max_static_tile_triangles = static_cast<std::uint32_t>(parsed);
    }
}

void staticTileFromCell(std::int32_t x,
                        std::int32_t y,
                        std::int32_t z,
                        Fvector& center,
                        std::uint64_t& key)
{
    center.set((static_cast<float>(x) + 0.5f) * kV2StaticTileSizeXZ,
               (static_cast<float>(y) + 0.5f) * kV2StaticTileSizeY,
               (static_cast<float>(z) + 0.5f) * kV2StaticTileSizeXZ);
    key = (static_cast<std::uint64_t>(static_cast<std::uint16_t>(x)) << 32) |
          (static_cast<std::uint64_t>(static_cast<std::uint16_t>(z)) << 16) |
          static_cast<std::uint16_t>(y);
}

bool staticTileBudgetAvailable(std::string* error)
{
    if (g_v2_static_tiles.size() >= g_v2_max_static_tiles)
    {
        if (error)
            *error = "static tile fragment budget exhausted";
        return false;
    }
    if (g_v2_static_tile_triangles >= g_v2_max_static_tile_triangles)
    {
        if (error)
            *error = "static tile triangle budget exhausted";
        return false;
    }
    return true;
}

bool ensureStaticTileAt(std::int32_t x, std::int32_t y, std::int32_t z, std::string* error)
{
    if (!g_v2_scene_alive || !g_v2_api.sceneAddStaticFragment)
    {
        if (error)
            *error = "v2 scene or SceneAddStaticFragment is not available";
        return false;
    }

    Fvector center{};
    std::uint64_t key = 0;
    staticTileFromCell(x, y, z, center, key);
    const auto cached = g_v2_static_tiles.find(key);
    if (cached != g_v2_static_tiles.end())
    {
        Msg("ph_runtime: v2 static tile cache hit key=%llu cell=(%d,%d,%d) tris=%u "
            "extract_ms=%.2f cook_ms=%.2f total_ms=%.2f total_fragments=%zu total_tris=%u",
            static_cast<unsigned long long>(key), x, y, z, cached->second.triangles,
            cached->second.extractMs, cached->second.cookMs, cached->second.totalMs,
            g_v2_static_tiles.size(), g_v2_static_tile_triangles);
        return true;
    }

    if (!staticTileBudgetAvailable(error))
    {
        Msg("! ph_runtime: v2 static tile budget exhausted fragments=%zu/%u triangles=%u/%u; "
            "ODE fallback",
            g_v2_static_tiles.size(), g_v2_max_static_tiles, g_v2_static_tile_triangles,
            g_v2_max_static_tile_triangles);
        return false;
    }

    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    std::uint32_t triangles = 0;
    std::uint32_t sourceTriangles = 0;
    bool truncated = false;
    const auto totalStarted = Clock::now();
    const auto extractStarted = Clock::now();
    if (!readStaticFragmentTile(center, kV2StaticHalfExtentXZ, kV2StaticHalfExtentY,
                                kMaxStaticTileTrianglesPerTile, vertices, indices, triangles, sourceTriangles,
                                truncated, error))
    {
        Msg("! ph_runtime: v2 static tile rejected cell=(%d,%d,%d) truncated=%d src_tris=%u max=%u: %s; "
            "ODE fallback",
            x, y, z, truncated ? 1 : 0, sourceTriangles, kMaxStaticTileTrianglesPerTile,
            error && !error->empty() ? error->c_str() : "no geometry");
        return false;
    }
    const double extractMs =
        std::chrono::duration<double, std::milli>(Clock::now() - extractStarted).count();

    // Overflow-safe total budget check before touching the scene. A rejected
    // tile must not partially replace or extend the existing geometry.
    const std::uint64_t totalTriangles =
        static_cast<std::uint64_t>(g_v2_static_tile_triangles) + static_cast<std::uint64_t>(triangles);
    if (totalTriangles > static_cast<std::uint64_t>(g_v2_max_static_tile_triangles))
    {
        if (error)
            *error = "static tile total triangle budget exhausted";
        Msg("! ph_runtime: v2 static tile triangle budget rejected cell=(%d,%d,%d) current=%u new=%u "
            "total=%llu max=%u; ODE fallback",
            x, y, z, g_v2_static_tile_triangles, triangles,
            static_cast<unsigned long long>(totalTriangles), g_v2_max_static_tile_triangles);
        return false;
    }

    char buffer[kErrorSize] = {};
    const auto cookStarted = Clock::now();
    if (!g_v2_api.sceneAddStaticFragment(
            g_v2_scene, key, vertices.data(), static_cast<std::uint32_t>(vertices.size() / 3),
            indices.data(), static_cast<std::uint32_t>(indices.size()), buffer, sizeof(buffer)))
    {
        if (error)
            *error = buffer[0] ? buffer : "SceneAddStaticFragment failed";
        Msg("! ph_runtime: v2 static tile add failed key=%llu cell=(%d,%d,%d): %s; ODE fallback",
            static_cast<unsigned long long>(key), x, y, z, buffer[0] ? buffer : "no error");
        return false;
    }
    const double cookMs =
        std::chrono::duration<double, std::milli>(Clock::now() - cookStarted).count();
    const double totalMs =
        std::chrono::duration<double, std::milli>(Clock::now() - totalStarted).count();

    V2StaticTile tile;
    tile.key = key;
    tile.x = x;
    tile.y = y;
    tile.z = z;
    tile.triangles = triangles;
    tile.extractMs = extractMs;
    tile.cookMs = cookMs;
    tile.totalMs = totalMs;
    g_v2_static_tiles.emplace(key, tile);
    g_v2_static_tile_triangles += triangles;
    g_v2_scene_triangles += triangles;
    Msg("ph_runtime: v2 static tile add key=%llu cell=(%d,%d,%d) center=(%.2f,%.2f,%.2f) tris=%u "
        "src_tris=%u extract_ms=%.2f cook_ms=%.2f total_ms=%.2f total_fragments=%zu total_tris=%u",
        static_cast<unsigned long long>(key), x, y, z, center.x, center.y, center.z, triangles,
        sourceTriangles, extractMs, cookMs, totalMs, g_v2_static_tiles.size(),
        g_v2_static_tile_triangles);
    return true;
}

bool ensureStaticTile(const Fvector& position, std::string* error)
{
    if (!g_v2_scene_alive || !g_v2_api.sceneAddStaticFragment)
    {
        if (error)
            *error = "v2 scene or SceneAddStaticFragment is not available";
        return false;
    }

    const std::int32_t minX =
        static_cast<std::int32_t>(std::floor((position.x - kV2StaticTileSafetyMargin) / kV2StaticTileSizeXZ));
    const std::int32_t maxX =
        static_cast<std::int32_t>(std::floor((position.x + kV2StaticTileSafetyMargin) / kV2StaticTileSizeXZ));
    const std::int32_t minY =
        static_cast<std::int32_t>(std::floor((position.y - kV2StaticTileSafetyMargin) / kV2StaticTileSizeY));
    const std::int32_t maxY =
        static_cast<std::int32_t>(std::floor((position.y + kV2StaticTileSafetyMargin) / kV2StaticTileSizeY));
    const std::int32_t minZ =
        static_cast<std::int32_t>(std::floor((position.z - kV2StaticTileSafetyMargin) / kV2StaticTileSizeXZ));
    const std::int32_t maxZ =
        static_cast<std::int32_t>(std::floor((position.z + kV2StaticTileSafetyMargin) / kV2StaticTileSizeXZ));

    std::uint32_t coverageTiles = 0;
    for (std::int32_t x = minX; x <= maxX; ++x)
    {
        for (std::int32_t y = minY; y <= maxY; ++y)
        {
            for (std::int32_t z = minZ; z <= maxZ; ++z)
            {
                if (!ensureStaticTileAt(x, y, z, error))
                    return false;
                ++coverageTiles;
            }
        }
    }
    if (coverageTiles > 1)
        Msg("ph_runtime: v2 static tile coverage box loaded %u tiles for pos=(%.2f,%.2f,%.2f)", coverageTiles,
            position.x, position.y, position.z);
    return true;
}

bool staticCoverageAvailable(float x, float y, float z, std::string* error)
{
    if (!OgsrPhysXRagdollStaticTilesRequested())
        return true;
    if (!g_v2_scene_alive)
        return true;

    const Fvector position(x, y, z);
    const std::int32_t minX =
        static_cast<std::int32_t>(std::floor((position.x - kV2StaticTileSafetyMargin) / kV2StaticTileSizeXZ));
    const std::int32_t maxX =
        static_cast<std::int32_t>(std::floor((position.x + kV2StaticTileSafetyMargin) / kV2StaticTileSizeXZ));
    const std::int32_t minY =
        static_cast<std::int32_t>(std::floor((position.y - kV2StaticTileSafetyMargin) / kV2StaticTileSizeY));
    const std::int32_t maxY =
        static_cast<std::int32_t>(std::floor((position.y + kV2StaticTileSafetyMargin) / kV2StaticTileSizeY));
    const std::int32_t minZ =
        static_cast<std::int32_t>(std::floor((position.z - kV2StaticTileSafetyMargin) / kV2StaticTileSizeXZ));
    const std::int32_t maxZ =
        static_cast<std::int32_t>(std::floor((position.z + kV2StaticTileSafetyMargin) / kV2StaticTileSizeXZ));

    std::map<std::uint64_t, bool> missing;
    for (std::int32_t tileX = minX; tileX <= maxX; ++tileX)
    {
        for (std::int32_t tileY = minY; tileY <= maxY; ++tileY)
        {
            for (std::int32_t tileZ = minZ; tileZ <= maxZ; ++tileZ)
            {
                Fvector center{};
                std::uint64_t key = 0;
                staticTileFromCell(tileX, tileY, tileZ, center, key);
                if (g_v2_static_tiles.find(key) == g_v2_static_tiles.end())
                    missing[key] = true;
            }
        }
    }

    if (missing.empty())
        return true;
    if (g_v2_static_tiles.size() + missing.size() > g_v2_max_static_tiles)
    {
        if (error)
            *error = "static tile fragment budget cannot cover the safety box";
        Msg("! ph_runtime: v2 static tile budget prevents boundary coverage cached=%zu missing=%zu max=%u; "
            "ODE fallback",
            g_v2_static_tiles.size(), missing.size(), g_v2_max_static_tiles);
        return false;
    }
    if (g_v2_static_tile_triangles >= g_v2_max_static_tile_triangles)
    {
        if (error)
            *error = "static tile triangle budget exhausted";
        Msg("! ph_runtime: v2 static tile triangle budget exhausted before coverage triangles=%u/%u; "
            "ODE fallback",
            g_v2_static_tile_triangles, g_v2_max_static_tile_triangles);
        return false;
    }
    return true;
}

void getStaticTileStats(std::uint32_t& fragments, std::uint32_t& triangles)
{
    fragments = static_cast<std::uint32_t>(g_v2_static_tiles.size());
    triangles = g_v2_static_tile_triangles;
}

void clearStaticTiles()
{
    g_v2_static_tiles.clear();
    g_v2_static_tile_triangles = 0;
}
#endif

void initialize()
{
#ifdef OGSR_PHYSX_RAGDOLL
    if (OgsrPhysXRagdollRequested())
    {
        prepareArchiveRuntime();
        if (!g_pGameLevel)
            return;
        if (OgsrPhysXRagdollMultiInstancesRequested())
        {
            // The v2 scene is created lazily on the first activation so the
            // static fragment is centered on the action area. One scene per
            // level; never per NPC.
            return;
        }
        initializeRagdoll();
        return;
    }
#endif
    if (!isRequested() || g_init_failed)
        return;
    prepareArchiveRuntime();
    if (!g_pGameLevel)
        return;
    // The archive runtime creates the level before the actor is available.
    // Defer static-fragment extraction until the first frame with an entity.
    CObject* entity = Level().CurrentEntity();
    if (!entity)
        return;

    shutdown();
    if (!loadApi())
    {
        g_init_failed = true;
        return;
    }

    Fvector center = entity->Position();
    if (!_valid(center.x) || !_valid(center.y) || !_valid(center.z))
        center.set(0.0f, 0.0f, 0.0f);

    OgsrPhysXRuntimeConfig config{};
    config.fixedTimestep = kFixedStep;
    config.gravityY = -19.62f;
    config.workerThreads = 2;
    config.maxSubsteps = kMaxSubsteps;
    char error[kErrorSize] = {};
    g_handle = g_api.create(&config, error, sizeof(error));
    if (!g_handle)
    {
        Msg("! ph_runtime: bridge initialization failed: %s", error);
        g_init_failed = true;
        unloadApi();
        return;
    }

    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    std::uint32_t sourceTriangles = 0;
    bool staticReady = readStaticFragment(center, vertices, indices, sourceTriangles);
    if (staticReady &&
        !g_api.setStaticMesh(g_handle, vertices.data(), static_cast<uint32_t>(vertices.size() / 3),
                             indices.data(), static_cast<uint32_t>(indices.size()), error, sizeof(error)))
    {
        Msg("! ph_runtime: static mesh upload failed: %s", error);
        staticReady = false;
    }

    const Fvector testPosition(center.x, center.y + 2.0f, center.z);
    if (!g_api.createTestBox(g_handle, testPosition.x, testPosition.y, testPosition.z, 0.25f, 0.25f, 0.25f, 100.0f,
                             error, sizeof(error)))
    {
        Msg("! ph_runtime: test box creation failed: %s", error);
    }

    g_frames = 0;
    g_step_ms_total = 0.0;
    g_step_ms_max = 0.0;
    Msg("ph_runtime: level context initialized center=(%.2f,%.2f,%.2f) static_tris=%u source_tris=%u",
        center.x, center.y, center.z, staticReady ? sourceTriangles : 0, sourceTriangles);
    OgsrPhysXRuntimeState state{};
    if (g_api.getState(g_handle, &state))
        Msg("ph_runtime: test body at y=%.3f dynamic_bodies=%u", state.position[1], state.dynamicBodies);
}

void update()
{
    prepareArchiveRuntime();
#ifdef OGSR_PHYSX_RAGDOLL
    if (OgsrPhysXRagdollTargetRequested())
    {
        OgsrPhysXRagdollSmokeTick();
        if (OgsrPhysXRagdollMultiInstancesRequested())
        {
            if (!OgsrPhysXRagdollV2SceneAlive())
                return;
            if (Device.Paused() || Device.fTimeDelta <= 0.0f)
                return;
            OgsrPhysX2StepResult stepResult{};
            stepResult.structSize = sizeof(stepResult);
            char stepError[kErrorSize] = {};
            const std::uint64_t frameId = ++g_v2_frame_id;
            if (!g_v2_api.sceneStep(g_v2_scene, Device.fTimeDelta, frameId, &stepResult, stepError,
                                    sizeof(stepError)))
            {
                Msg("! ph_runtime: v2 SceneStep failed: %s", stepError);
                return;
            }
            g_v2_step_index = stepResult.stepIndex;
            if (g_v2_frame_id % 120 == 0)
                Msg("ph_runtime: v2 frame=%llu step=%llu live=%u scene=(%u,%u)", g_v2_frame_id,
                    g_v2_step_index, OgsrPhysXRagdollV2LiveCount(), g_v2_scene.slot, g_v2_scene.generation);
            if (stepResult.droppedSteps > 0)
                Msg("ph_runtime: v2 dropped steps=%u time=%.4f frame=%llu", stepResult.droppedSteps,
                    stepResult.droppedSeconds, static_cast<unsigned long long>(frameId));
            return;
        }
        if (!OgsrPhysXRagdollRequested())
            return;
        if (!g_ragdoll_alive)
        {
            initializeRagdoll();
            if (!g_ragdoll_alive)
                return;
        }
        if (g_ragdoll_smoke && g_ragdoll_active)
        {
            ++g_ragdoll_smoke_active_frames;
            const std::uint32_t phase = g_ragdoll_smoke_phase;
            const std::uint32_t activeFrames = g_ragdoll_smoke_active_frames;
            if (phase == 0 && activeFrames == 300)
            {
                g_smoke_pause_active = true;
                Device.Pause(TRUE, TRUE, TRUE, "physx_ragdoll_smoke");
                Msg("ph_runtime_smoke: ragdoll pause enabled");
                g_ragdoll_smoke_phase = 1;
            }
            else if (phase == 1 && activeFrames == 420)
            {
                g_smoke_pause_active = false;
                Device.Pause(FALSE, TRUE, TRUE, "physx_ragdoll_smoke");
                Msg("ph_runtime_smoke: ragdoll pause disabled");
                g_ragdoll_smoke_phase = 2;
            }
            else if (phase == 2 && activeFrames == 540)
            {
                Device.time_factor(0.5f);
                g_ragdoll_smoke_time_scaled = true;
                Msg("ph_runtime_smoke: ragdoll time_factor=0.5");
                g_ragdoll_smoke_phase = 3;
            }
            else if (phase == 3 && activeFrames == 660)
            {
                Device.time_factor(1.0f);
                g_ragdoll_smoke_time_scaled = false;
                Msg("ph_runtime_smoke: ragdoll time_factor=1.0");
                g_ragdoll_smoke_phase = 4;
            }
        }
        if (!g_ragdoll_active || Device.Paused() || Device.fTimeDelta <= 0.0f)
            return;

        const Clock::time_point start = Clock::now();
        std::string error;
        if (!OgsrPhysXRagdollRuntimeStep(Device.fTimeDelta, &error))
        {
            Msg("! ph_runtime: NPC ragdoll step failed: %s", error.c_str());
            return;
        }
        const double elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        g_ragdoll_ms_total += elapsedMs;
        g_ragdoll_ms_max = std::max(g_ragdoll_ms_max, elapsedMs);
        ++g_ragdoll_frames;
        if (g_ragdoll_frames % 120 == 0)
        {
            OgsrPhysXRagdollPose pose{};
            std::vector<float> bodyWorld(OGSR_PHYSX_RAGDOLL_MAX_BODIES * 16);
            if (OgsrPhysXRagdollRuntimeGetPose(pose, bodyWorld.data(), OGSR_PHYSX_RAGDOLL_MAX_BODIES))
            {
                const std::uint64_t stepIndex =
                    (static_cast<std::uint64_t>(pose.stepIndexHigh) << 32) | pose.stepIndexLow;
                Msg("ph_runtime: npc frame=%u bodies=%u steps=%llu entity_y=%.3f avg_ms=%.3f max_ms=%.3f",
                    g_ragdoll_frames, pose.bodyCount, static_cast<unsigned long long>(stepIndex),
                    pose.entityWorld[13], g_ragdoll_ms_total / static_cast<double>(g_ragdoll_frames),
                    g_ragdoll_ms_max);
            }
            g_ragdoll_ms_total = 0.0;
            g_ragdoll_ms_max = 0.0;
            g_ragdoll_frames = 0;
        }
        return;
    }
#endif
    smokeTick();
    if (g_smoke_pause_active && !Device.Paused())
    {
        static bool loggedPauseLoss = false;
        if (!loggedPauseLoss)
        {
            loggedPauseLoss = true;
            Msg("ph_runtime_smoke: pause state was lost by another subsystem");
        }
    }
    if (!g_handle)
        initialize();
    if (!g_handle)
        return;
    if (Device.Paused())
    {
        if (!g_was_paused)
            Msg("ph_runtime: paused; fixed step suspended");
        g_was_paused = true;
        return;
    }
    g_was_paused = false;
    if (Device.fTimeDelta <= 0.0f)
        return;

    const Clock::time_point start = Clock::now();
    char error[kErrorSize] = {};
    if (!g_api.step(g_handle, Device.fTimeDelta, error, sizeof(error)))
    {
        Msg("! ph_runtime: step failed: %s", error);
        shutdown();
        return;
    }
    const double elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    g_step_ms_total += elapsedMs;
    g_step_ms_max = std::max(g_step_ms_max, elapsedMs);
    ++g_frames;
    if (g_frames % 120 == 0)
    {
        OgsrPhysXRuntimeState state{};
        if (g_api.getState(g_handle, &state))
        {
            Msg("ph_runtime: frame=%u steps=%u sub=%u y=%.3f contacts=%u avg_ms=%.3f max_ms=%.3f",
                g_frames, state.stepCount, state.lastSubsteps, state.position[1], state.contactCount,
                g_step_ms_total / static_cast<double>(g_frames), g_step_ms_max);
        }
        g_step_ms_total = 0.0;
        g_step_ms_max = 0.0;
        g_frames = 0;
    }
}
} // namespace

#ifdef OGSR_PHYSX_RAGDOLL
bool OgsrPhysXRagdollRuntimeSetStaticMesh(const float* vertices,
                                          uint32_t vertexCount,
                                          const uint32_t* indices,
                                          uint32_t indexCount,
                                          std::string* error)
{
    if (!g_ragdoll_alive || !g_api.ragdollSetStaticMesh)
    {
        if (error)
            *error = "NPC ragdoll runtime is not initialized";
        return false;
    }
    char buffer[kErrorSize] = {};
    if (!g_api.ragdollSetStaticMesh(g_ragdoll_handle, vertices, vertexCount, indices, indexCount, buffer,
                                    sizeof(buffer)))
    {
        if (error)
            *error = buffer;
        return false;
    }
    return true;
}

bool OgsrPhysXRagdollRuntimeCreateInstance(const OgsrPhysXRagdollDefinition& definition,
                                           std::string* error,
                                           uint32_t& bodyCount,
                                           uint32_t& jointCount)
{
    if (!g_ragdoll_alive || !g_api.ragdollCreate)
    {
        if (error)
            *error = "NPC ragdoll runtime is not initialized";
        return false;
    }
    char buffer[kErrorSize] = {};
    g_ragdoll_active = false;
    if (!g_api.ragdollCreate(g_ragdoll_handle, &definition, buffer, sizeof(buffer)))
    {
        if (error)
            *error = buffer;
        return false;
    }
    OgsrPhysXRagdollInfo info{};
    bodyCount = 0;
    jointCount = 0;
    if (g_api.ragdollGetInfo(g_ragdoll_handle, &info))
    {
        bodyCount = info.bodyCount;
        jointCount = info.jointCount;
    }
    g_ragdoll_active = true;
    g_ragdoll_frames = 0;
    g_ragdoll_ms_total = 0.0;
    g_ragdoll_ms_max = 0.0;
    return true;
}

void OgsrPhysXRagdollRuntimeDestroyInstance()
{
    if (g_ragdoll_alive && g_api.ragdollDestroy)
        g_api.ragdollDestroy(g_ragdoll_handle);
    g_ragdoll_active = false;
}

bool OgsrPhysXRagdollRuntimeStep(float frameDeltaSeconds, std::string* error)
{
    if (!g_ragdoll_alive || !g_ragdoll_active || !g_api.ragdollStep)
    {
        if (error)
            *error = "NPC ragdoll instance is not active";
        return false;
    }
    char buffer[kErrorSize] = {};
    if (!g_api.ragdollStep(g_ragdoll_handle, frameDeltaSeconds, buffer, sizeof(buffer)))
    {
        if (error)
            *error = buffer;
        return false;
    }
    ++g_ragdoll_steps;
    return true;
}

bool OgsrPhysXRagdollRuntimeGetPose(OgsrPhysXRagdollPose& pose, float* bodyWorld, uint32_t maxBodies)
{
    if (!g_ragdoll_alive || !g_api.ragdollGetPose)
        return false;
    return g_api.ragdollGetPose(g_ragdoll_handle, &pose, bodyWorld, maxBodies) != 0;
}

bool OgsrPhysXRagdollRuntimeGetInfo(OgsrPhysXRagdollInfo& info)
{
    if (!g_ragdoll_alive || !g_api.ragdollGetInfo)
        return false;
    return g_api.ragdollGetInfo(g_ragdoll_handle, &info) != 0;
}

bool OgsrPhysXRagdollRuntimeGetDiagnostics(float referenceHeight, OgsrPhysXRagdollDiagnostics& diagnostics)
{
    if (!g_ragdoll_alive || !g_api.ragdollGetDiagnostics)
        return false;
    return g_api.ragdollGetDiagnostics(g_ragdoll_handle, referenceHeight, &diagnostics) != 0;
}

bool OgsrPhysXRagdollRuntimeApplyImpulse(uint16_t body,
                                         const float worldPosition[3],
                                         const float impulse[3],
                                         std::string* error)
{
    if (!g_ragdoll_alive || !g_api.ragdollApplyImpulse)
    {
        if (error)
            *error = "NPC ragdoll runtime is not initialized";
        return false;
    }
    char buffer[kErrorSize] = {};
    if (!g_api.ragdollApplyImpulse(g_ragdoll_handle, body, worldPosition, impulse, buffer, sizeof(buffer)))
    {
        if (error)
            *error = buffer;
        return false;
    }
    return true;
}

bool OgsrPhysXRagdollRuntimeApplyCentralImpulse(uint16_t body, const float impulse[3], std::string* error)
{
    if (!g_ragdoll_alive || !g_api.ragdollApplyCentralImpulse)
    {
        if (error)
            *error = "NPC ragdoll runtime is not initialized";
        return false;
    }
    char buffer[kErrorSize] = {};
    if (!g_api.ragdollApplyCentralImpulse(g_ragdoll_handle, body, impulse, buffer, sizeof(buffer)))
    {
        if (error)
            *error = buffer;
        return false;
    }
    return true;
}

bool OgsrPhysXRagdollRuntimeGetBodyMass(uint16_t body, OgsrPhysXRagdollBodyMass& mass, std::string* error)
{
    if (!g_ragdoll_alive || !g_api.ragdollGetBodyMass)
    {
        if (error)
            *error = "NPC ragdoll runtime is not initialized";
        return false;
    }
    char buffer[kErrorSize] = {};
    if (!g_api.ragdollGetBodyMass(g_ragdoll_handle, body, &mass, buffer, sizeof(buffer)))
    {
        if (error)
            *error = buffer;
        return false;
    }
    return true;
}

bool OgsrPhysXRagdollV2SceneAlive()
{
    return g_v2_scene_alive;
}

bool OgsrPhysXRagdollV2WithinStaticBounds(float x, float y, float z, float margin)
{
    if (!g_v2_scene_alive || !g_v2_static_bounds_valid)
        return true;
    return std::fabs(x - g_v2_static_center.x) <= g_v2_static_half_xz + margin &&
           std::fabs(y - g_v2_static_center.y) <= g_v2_static_half_y + margin &&
           std::fabs(z - g_v2_static_center.z) <= g_v2_static_half_xz + margin;
}

bool OgsrPhysXRagdollV2StaticCoverageAvailable(float x, float y, float z, std::string* error)
{
    return staticCoverageAvailable(x, y, z, error);
}

void OgsrPhysXRagdollV2GetStaticTileStats(std::uint32_t& fragments, std::uint32_t& triangles)
{
    getStaticTileStats(fragments, triangles);
}

std::uint64_t OgsrPhysXRagdollV2StepIndex()
{
    return g_v2_step_index;
}

bool OgsrPhysXRagdollV2Create(const Fvector& center,
                              const OgsrPhysXRagdollDefinition& definition,
                              const OgsrPhysX2RagdollInstanceConfig& config,
                              OgsrPhysX2RagdollHandle& handle,
                              std::string* error)
{
    handle = OgsrPhysX2RagdollHandle{};
    if (!initializeMultiRagdoll(center))
    {
        if (error)
            *error = "v2 scene is not available";
        return false;
    }
    if (OgsrPhysXRagdollStaticTilesRequested() && !ensureStaticTile(center, error))
        return false;
    if (!g_v2_api.ragdollCreate)
    {
        if (error)
            *error = "v2 ragdollCreate export is missing";
        return false;
    }
    char buffer[kErrorSize] = {};
    if (!g_v2_api.ragdollCreate(g_v2_scene, &definition, &config, &handle, buffer, sizeof(buffer)))
    {
        if (error)
            *error = buffer;
        return false;
    }
    ++g_v2_live_instances;
    g_v2_lease.addResourceRef();
    Msg("ph_ragdoll_v2: instance created scene=(%u,%u) instance=(%u,%u) live=%u", g_v2_scene.slot,
        g_v2_scene.generation, handle.instanceSlot, handle.instanceGeneration, g_v2_live_instances);
    return true;
}

bool OgsrPhysXRagdollV2Destroy(OgsrPhysX2RagdollHandle handle, std::string* error)
{
    if (!g_v2_scene_alive || !g_v2_api.ragdollDestroy)
    {
        if (error)
            *error = "v2 scene is not available";
        return false;
    }
    char buffer[kErrorSize] = {};
    if (!g_v2_api.ragdollDestroy(handle, buffer, sizeof(buffer)))
    {
        if (error)
            *error = buffer;
        return false;
    }
    if (g_v2_live_instances > 0)
        --g_v2_live_instances;
    g_v2_lease.releaseResourceRef();
    Msg("ph_ragdoll_v2: instance destroyed instance=(%u,%u) live=%u", handle.instanceSlot,
        handle.instanceGeneration, g_v2_live_instances);
    return true;
}

void OgsrPhysXRagdollV2ReleaseLeaseRef()
{
    if (g_v2_live_instances > 0)
        --g_v2_live_instances;
    g_v2_lease.releaseResourceRef();
    Msg("ph_ragdoll_v2: lease ref released on reconciliation; live=%u refs=%u", g_v2_live_instances,
        g_v2_lease.resourceRefs());
}

bool OgsrPhysXRagdollV2GetPose(OgsrPhysX2RagdollHandle handle,
                               OgsrPhysXRagdollPose& pose,
                               float* bodyWorld,
                               std::uint32_t maxBodies)
{
    if (!g_v2_scene_alive || !g_v2_api.ragdollGetPose)
        return false;
    return g_v2_api.ragdollGetPose(handle, &pose, bodyWorld, maxBodies) != 0;
}

bool OgsrPhysXRagdollV2GetInfo(OgsrPhysX2RagdollHandle handle, OgsrPhysXRagdollInfo& info)
{
    if (!g_v2_scene_alive || !g_v2_api.ragdollGetInfo)
        return false;
    return g_v2_api.ragdollGetInfo(handle, &info) != 0;
}

bool OgsrPhysXRagdollV2GetBodyMass(OgsrPhysX2RagdollHandle handle,
                                   std::uint16_t body,
                                   OgsrPhysXRagdollBodyMass& mass,
                                   std::string* error)
{
    if (!g_v2_scene_alive || !g_v2_api.ragdollGetBodyMass)
    {
        if (error)
            *error = "v2 scene is not available";
        return false;
    }
    char buffer[kErrorSize] = {};
    if (!g_v2_api.ragdollGetBodyMass(handle, body, &mass, buffer, sizeof(buffer)))
    {
        if (error)
            *error = buffer;
        return false;
    }
    return true;
}

bool OgsrPhysXRagdollV2GetDiagnostics(OgsrPhysX2RagdollHandle handle,
                                      float referenceHeight,
                                      OgsrPhysXRagdollDiagnostics& diagnostics)
{
    if (!g_v2_scene_alive || !g_v2_api.ragdollGetDiagnostics)
        return false;
    return g_v2_api.ragdollGetDiagnostics(handle, referenceHeight, &diagnostics) != 0;
}

bool OgsrPhysXRagdollV2ApplyImpulse(OgsrPhysX2RagdollHandle handle,
                                    std::uint16_t body,
                                    const float worldPosition[3],
                                    const float impulse[3],
                                    std::string* error)
{
    if (!g_v2_scene_alive || !g_v2_api.ragdollApplyImpulse)
    {
        if (error)
            *error = "v2 scene is not available";
        return false;
    }
    char buffer[kErrorSize] = {};
    if (!g_v2_api.ragdollApplyImpulse(handle, body, worldPosition, impulse, buffer, sizeof(buffer)))
    {
        if (error)
            *error = buffer;
        return false;
    }
    return true;
}

bool OgsrPhysXRagdollV2ApplyCentralImpulse(OgsrPhysX2RagdollHandle handle,
                                           std::uint16_t body,
                                           const float impulse[3],
                                           std::string* error)
{
    if (!g_v2_scene_alive || !g_v2_api.ragdollApplyCentralImpulse)
    {
        if (error)
            *error = "v2 scene is not available";
        return false;
    }
    char buffer[kErrorSize] = {};
    if (!g_v2_api.ragdollApplyCentralImpulse(handle, body, impulse, buffer, sizeof(buffer)))
    {
        if (error)
            *error = buffer;
        return false;
    }
    return true;
}
#endif

void OgsrPhysXRuntimeOnLevelStart()
{
#ifdef OGSR_PHYSX_RAGDOLL
    // A reload may interrupt the smoke pause/time-factor sequence. Restore the
    // engine state before the new level starts, otherwise the next session
    // would inherit a paused device or a 0.5 time factor.
    if (g_smoke_pause_active)
    {
        Device.Pause(FALSE, TRUE, TRUE, "physx_ragdoll_smoke_reset");
        g_smoke_pause_active = false;
    }
    if (g_ragdoll_smoke_time_scaled)
    {
        Device.time_factor(1.0f);
        g_ragdoll_smoke_time_scaled = false;
    }
    OgsrPhysXRagdollSmokeReset();
    g_ragdoll_smoke_phase = 0;
    g_ragdoll_smoke_active_frames = 0;
#endif
    initialize();
}
void OgsrPhysXRuntimeOnFrame() { update(); }
void OgsrPhysXRuntimeOnLevelUnload()
{
    if (g_handle)
        Msg("ph_runtime: unloading level");
#ifdef OGSR_PHYSX_RAGDOLL
    if (g_ragdoll_handle)
        Msg("ph_runtime: unloading NPC ragdoll level");
    OgsrPhysXRagdollSmokeCameraStop();
    if (g_ragdoll_smoke_time_scaled)
    {
        Device.time_factor(1.0f);
        g_ragdoll_smoke_time_scaled = false;
    }
#endif
    shutdown();
    g_was_paused = false;
    g_smoke_pause_active = false;
    g_ragdoll_smoke_phase = 0;
    g_ragdoll_smoke_active_frames = 0;
    g_init_failed = false;
#ifdef OGSR_DIAGNOSTIC_RUNTIME_WORKAROUND
    g_archive_runtime_prepared = false;
#ifdef OGSR_PHYSX_RAGDOLL
    const bool ragdollMode = OgsrPhysXRagdollRequested();
    const bool ragdollReload = ragdollMode && OgsrPhysXRagdollReloadRequested();
#else
    const bool ragdollMode = false;
    const bool ragdollReload = false;
#endif
    bool armReload = false;
    if (ragdollReload && g_ragdoll_reload_count < 2)
    {
        ++g_ragdoll_reload_count;
        armReload = true;
        Msg("ph_runtime_smoke: ragdoll reload %d/2 requested", g_ragdoll_reload_count);
    }
    else if (!ragdollMode && isSmokeRequested() && !g_smoke_reload_done)
    {
        g_smoke_reload_done = true;
        armReload = true;
    }
    if (armReload)
    {
        // Priority between CApplication (HIGH + 1000) and the normal frame
        // callbacks. Resort puts the receiver before GamePersistent for the
        // remainder of this frame pass.
        Device.seqFrame.Add(&g_smoke_reload, REG_PRIORITY_HIGH + 500);
        Device.seqFrame.Resort();
        Msg("ph_runtime_smoke: reload receiver armed");
    }
#endif
}

#endif // OGSR_PHYSX_RUNTIME
