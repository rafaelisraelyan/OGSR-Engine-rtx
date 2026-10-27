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
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

namespace
{
using Clock = std::chrono::steady_clock;

constexpr float kStaticHalfExtentXZ = 16.0f;
constexpr float kStaticHalfExtentY = 8.0f;
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
        if (tokens[index] == "-physx_ragdoll")
        {
            if (index + 1 < tokens.size() && !tokens[index + 1].empty() && tokens[index + 1][0] != '-')
            {
                g_ragdoll_target = tokens[index + 1];
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
        else if (tokens[index] == "-physx_ragdoll_reload")
        {
            g_ragdoll_reload = true;
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

bool OgsrPhysXRagdollReloadRequested()
{
    parseRagdollFlags();
    return g_ragdoll_reload;
}

void OgsrPhysXRagdollRequestLevelDisconnect() { Engine.Event.Defer("KERNEL:disconnect"); }

bool OgsrPhysXRagdollMatches(const char* name, const char* section)
{
    parseRagdollFlags();
    if (!g_ragdoll_requested || g_ragdoll_target.empty())
        return false;
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
bool OgsrPhysXRagdollRuntimeAlive() { return g_ragdoll_alive && g_ragdoll_handle != nullptr; }
bool OgsrPhysXRagdollRuntimeHasInstance() { return g_ragdoll_active; }
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
// Smoke-only reload. The engine crash path after a bare disconnect dereferences
// Level() in CGamePersistent, so the replacement level is created synchronously
// from a frame callback that runs after the disconnect handler but before
// GamePersistent::OnFrame.
class CSmokeReload final : public pureFrame
{
public:
    void OnFrame() override
    {
        Device.seqFrame.Remove(this);
        Msg("ph_runtime_smoke: reloading level");
        Engine.Event.Signal("KERNEL:start", u64(xr_strdup("all/single/alife/new")), u64(xr_strdup("localhost")));
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

bool readStaticFragment(const Fvector& center,
                        std::vector<float>& vertices,
                        std::vector<std::uint32_t>& indices,
                        std::uint32_t& triangleCount)
{
    Fvector zAxis(0.0f, 0.0f, 1.0f);
    Fvector yAxis(0.0f, 1.0f, 0.0f);
    Fvector sizes(kStaticHalfExtentXZ * 2.0f, kStaticHalfExtentY * 2.0f, kStaticHalfExtentXZ * 2.0f);
    std::vector<Fvector> triangleVertices;
    const bool found = Level().ObjectSpace.BoxQuery(center, zAxis, yAxis, sizes, &triangleVertices);
    if (!found || triangleVertices.empty())
    {
        Msg("! ph_runtime: no static level triangles in the diagnostic box");
        return false;
    }

    const std::size_t sourceTriangles = triangleVertices.size() / 3;
    triangleCount = static_cast<std::uint32_t>(std::min<std::size_t>(sourceTriangles, kMaxStaticTriangles));
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
            if (menu->IsActive())
                menu->Activate(false);
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
#endif

void initialize()
{
#ifdef OGSR_PHYSX_RAGDOLL
    if (OgsrPhysXRagdollRequested())
    {
        prepareArchiveRuntime();
        if (!g_pGameLevel)
            return;
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
