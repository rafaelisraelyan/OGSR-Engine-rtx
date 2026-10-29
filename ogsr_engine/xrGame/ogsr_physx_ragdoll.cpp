#include "stdafx.h"

#ifdef OGSR_PHYSX_RAGDOLL

#include "ogsr_physx_ragdoll.h"
#include "ogsr_physx_runtime.h"

#include "Level.h"
#include "Hit.h"
#include "Actor.h"
#include "ActorEffector.h"
#include "GamePersistent.h"
#include "HUDManager.h"
#include "../xr_3da/CustomHUD.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "xrMessages.h"
#include "CameraFirstEye.h"
#include "CharacterPhysicsSupport.h"
#include "ai/stalker/ai_stalker.h"
#include "PHMovementControl.h"
#include "../xr_3da/pure.h"
#include "../xr_3da/Effector.h"
#include "../xr_3da/CameraManager.h"
#include "../xr_3da/xr_area.h"
#include "../xr_3da/xr_object.h"
#include "../xr_3da/Render.h"
#include "../Layers/PhysicsAdapterOGSR/ogsr/ogsr_capture.h"
#include "../Layers/PhysicsPortable/include/physics_portable/row_matrix.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace pp = physics_portable;

namespace
{
constexpr std::uint32_t kStaticCapTriangles = 8192;
constexpr float kStaticSizeX = 48.0f;
constexpr float kStaticSizeY = 24.0f;
constexpr float kStaticSizeZ = 48.0f;
constexpr std::uint32_t kSmokeGiveUpFrame = 1800;

struct CCollectBlends final : public IterateBlendsCallback
{
    std::vector<CBlend*> blends;
    void operator()(CBlend& blend) override { blends.push_back(&blend); }
};

Fvector g_smokeActorStart{};
Fvector g_smokeActorDir{};
bool g_smokeActorStartValid = false;
u16 g_smokeTargetId = u16(-1);
u16 g_smokeMultiSecondId = u16(-1);
bool g_smokeDualPostPending = false;
bool g_smokeDualKeepCamera = false;
std::uint64_t g_smokeDualPostStep = 0;

Fvector g_smokeCameraTarget{};
Fvector g_smokeCameraPosition{};
Fvector g_smokeCameraDirection{};
Fvector g_smokeCameraTop{};
Fvector g_smokeCameraRight{};
float g_smokeCameraFov = 60.0f;
bool g_smokeCameraActive = false;
bool g_smokeCameraReady = false;
bool g_smokeCameraRenderRegistered = false;
bool g_smokeCameraPreRenderRegistered = false;
Flags32 g_smokeHudFlags{};
bool g_smokeHudFlagsSaved = false;
bool g_smokeHudRenderableSaved = false;
bool g_smokeHudRenderablePrevious = true;
bool g_smokeHudPreHidden = false;
bool g_smokeCaptureStarted = false;
std::uint64_t g_smokeCaptureBase = 0;
int g_smokeCaptureIndex = 0;
bool g_smokeCaptureComplete = false;
bool g_smokeCaptureIncomplete = false;
std::uint64_t g_smokeCaptureMaxDelta = 0;
int g_smokePostCheckFrames = 0;
bool g_smokeResetRequested = false;

float g_smokeImpulse = 8.0f;
bool g_smokeImpulseParsed = false;
int g_smokeCameraMode = 0; // 0 = top, 1 = side, 2 = side_left
bool g_smokeCameraParsed = false;
bool g_smokeCapture = false;
bool g_smokeCaptureParsed = false;
bool g_smokeHudHiddenRequested = false;
bool g_smokeHudHiddenParsed = false;
bool g_smokeDoubleStop = false;
bool g_smokeDoubleStopParsed = false;

float smokeImpulse()
{
    if (g_smokeImpulseParsed)
        return g_smokeImpulse;
    g_smokeImpulseParsed = true;
    const char* params = Core.Params;
    const char* flag = params ? strstr(params, "-physx_ragdoll_smoke_impulse") : nullptr;
    if (flag)
    {
        const char* value = flag + std::strlen("-physx_ragdoll_smoke_impulse");
        while (*value == ' ')
            ++value;
        if (*value && *value != '-')
        {
            const float parsed = static_cast<float>(std::atof(value));
            if (parsed > 0.0f && parsed <= 200.0f)
                g_smokeImpulse = parsed;
        }
    }
    Msg("ph_ragdoll_smoke: impulse=%.2f N*s (default=8.00)", g_smokeImpulse);
    return g_smokeImpulse;
}

const char* smokeImpulseTag()
{
    static char tag[16];
    xr_sprintf(tag, sizeof(tag), "%.0f", smokeImpulse());
    return tag;
}

int smokeCameraMode()
{
    if (g_smokeCameraParsed)
        return g_smokeCameraMode;
    g_smokeCameraParsed = true;
    const char* params = Core.Params;
    const char* flag = params ? strstr(params, "-physx_ragdoll_smoke_camera") : nullptr;
    if (flag)
    {
        const char* value = flag + std::strlen("-physx_ragdoll_smoke_camera");
        while (*value == ' ')
            ++value;
        if (strncmp(value, "side_left", 9) == 0)
            g_smokeCameraMode = 2;
        else if (strncmp(value, "side", 4) == 0)
            g_smokeCameraMode = 1;
    }
    Msg("ph_ragdoll_smoke: camera=%s", g_smokeCameraMode == 2 ? "side_left" : g_smokeCameraMode == 1 ? "side" : "top");
    return g_smokeCameraMode;
}

bool smokeCaptureRequested()
{
    if (g_smokeCaptureParsed)
        return g_smokeCapture;
    g_smokeCaptureParsed = true;
    g_smokeCapture = Core.Params && strstr(Core.Params, "-physx_ragdoll_smoke_capture") != nullptr;
    if (g_smokeCapture)
        Msg("ph_ragdoll_smoke: frame capture enabled");
    return g_smokeCapture;
}

bool smokeHudHiddenRequested()
{
    if (g_smokeHudHiddenParsed)
        return g_smokeHudHiddenRequested;
    g_smokeHudHiddenParsed = true;
    g_smokeHudHiddenRequested =
        Core.Params && strstr(Core.Params, "-physx_ragdoll_smoke_hud_hidden") != nullptr;
    if (g_smokeHudHiddenRequested)
        Msg("ph_ragdoll_smoke: HUD pre-hidden regression enabled");
    return g_smokeHudHiddenRequested;
}

bool smokeDoubleStopRequested()
{
    if (g_smokeDoubleStopParsed)
        return g_smokeDoubleStop;
    g_smokeDoubleStopParsed = true;
    g_smokeDoubleStop = Core.Params && strstr(Core.Params, "-physx_ragdoll_smoke_double_stop") != nullptr;
    if (g_smokeDoubleStop)
        Msg("ph_ragdoll_smoke: double-stop regression enabled");
    return g_smokeDoubleStop;
}

void hideSmokeHud()
{
    if (!g_smokeHudRenderableSaved)
    {
        g_smokeHudRenderablePrevious = g_hud ? HUD().IsRenderable() : true;
        g_smokeHudRenderableSaved = true;
    }
    if (g_hud)
    {
        HUD().SetRenderable(false);
        HUD().ShowCrosshair(false);
    }
    if (!g_smokeHudFlagsSaved)
    {
        g_smokeHudFlags = psHUD_Flags;
        g_smokeHudFlagsSaved = true;
    }
    psHUD_Flags.set(HUD_DRAW, FALSE);
    psHUD_Flags.set(HUD_DRAW_RT, FALSE);
    Msg("ph_ragdoll_smoke: HUD hide previous_renderable=%d hud_draw=%d hud_draw_rt=%d",
        g_smokeHudRenderablePrevious ? 1 : 0, psHUD_Flags.test(HUD_DRAW) ? 1 : 0,
        psHUD_Flags.test(HUD_DRAW_RT) ? 1 : 0);
}

void restoreSmokeHud()
{
    if (!g_smokeHudRenderableSaved && !g_smokeHudFlagsSaved)
    {
        Msg("ph_ragdoll_smoke: HUD restore skipped (no ownership)");
        return;
    }
    if (g_smokeHudRenderableSaved)
    {
        if (g_hud)
        {
            HUD().SetRenderable(g_smokeHudRenderablePrevious);
            HUD().ShowCrosshair(true);
        }
        g_smokeHudRenderableSaved = false;
    }
    if (g_smokeHudFlagsSaved)
    {
        psHUD_Flags = g_smokeHudFlags;
        g_smokeHudFlagsSaved = false;
    }
    Msg("ph_ragdoll_smoke: HUD restore renderable=%d hud_draw=%d hud_draw_rt=%d",
        g_hud ? (HUD().IsRenderable() ? 1 : 0) : -1, psHUD_Flags.test(HUD_DRAW) ? 1 : 0,
        psHUD_Flags.test(HUD_DRAW_RT) ? 1 : 0);
}

class CSmokeCameraEffector final : public CEffectorCam
{
public:
    CSmokeCameraEffector() : CEffectorCam((ECamEffectorType)0, 1.0e9f) { bHudAffect = false; }

    BOOL ProcessCam(SCamEffectorInfo& info) override
    {
        if (m_stop)
            return FALSE;
        info.p.set(g_smokeCameraPosition);
        info.d.set(g_smokeCameraDirection);
        info.n.set(g_smokeCameraTop);
        info.r.set(g_smokeCameraRight);
        info.fFov = g_smokeCameraFov;
        info.dont_apply = false;
        info.affected_on_hud = false;
        fLifeTime = 1.0e9f;
        static int logCounter = 0;
        if (++logCounter % 60 == 0)
            Msg("ph_ragdoll_smoke: camera effector frame=%u p=(%.3f,%.3f,%.3f) d=(%.3f,%.3f,%.3f) fov=%.2f",
                Device.dwFrame, info.p.x, info.p.y, info.p.z, info.d.x, info.d.y, info.d.z, info.fFov);
        return TRUE;
    }

    bool m_stop = false;
};

CEffectorCam* g_smokeCameraEffector = nullptr;
CEffectorCam* g_smokeCameraActorEffector = nullptr;

void maybeCaptureSmokeFrames();

class CSmokeCameraRender final : public pureRender
{
public:
    void OnRender() override;
};

CSmokeCameraRender g_smokeCameraRender;

class CSmokeCameraPreRender final : public pureRender
{
public:
    void OnRender() override;
};

CSmokeCameraPreRender g_smokeCameraPreRender;

void stopSmokeCamera()
{
    const bool wasActive = g_smokeCameraActive;
    if (g_smokeCameraEffector)
        static_cast<CSmokeCameraEffector*>(g_smokeCameraEffector)->m_stop = true;
    if (g_smokeCameraActorEffector)
        static_cast<CSmokeCameraEffector*>(g_smokeCameraActorEffector)->m_stop = true;
    g_smokeCameraEffector = nullptr;
    g_smokeCameraActorEffector = nullptr;
    g_smokeCameraActive = false;
    g_smokeCameraReady = false;
    restoreSmokeHud();
    if (wasActive)
        g_smokePostCheckFrames = 5;
    if (g_smokeCameraRenderRegistered)
    {
        Device.seqRender.Remove(&g_smokeCameraRender);
        g_smokeCameraRenderRegistered = false;
    }
    if (g_smokeCameraPreRenderRegistered)
    {
        Device.seqRender.Remove(&g_smokeCameraPreRender);
        g_smokeCameraPreRenderRegistered = false;
    }
}

void captureSmokeFrame(const char* tag, std::uint64_t step)
{
    if (!Render)
        return;
    Render->Screenshot(IRender_interface::SM_NORMAL, nullptr);

    string_path folder;
    FS.update_path(folder, "$screenshots$", "");
    const std::filesystem::path dir(folder);
    std::filesystem::path newest;
    std::filesystem::file_time_type newestTime{};
    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            if (!entry.is_regular_file())
                continue;
            const std::string name = entry.path().filename().string();
            if (name.rfind("ss_", 0) != 0)
                continue;
            const std::string extension = entry.path().extension().string();
            if (extension != ".png" && extension != ".jpg" && extension != ".bmp")
                continue;
            const auto time = entry.last_write_time();
            if (newest.empty() || time > newestTime)
            {
                newest = entry.path();
                newestTime = time;
            }
        }
    }
    catch (const std::exception& error)
    {
        Msg("! ph_ragdoll_smoke: capture scan failed: %s", error.what());
        return;
    }
    if (newest.empty())
    {
        Msg("! ph_ragdoll_smoke: capture file not found");
        return;
    }

    char name[256];
    xr_sprintf(name, sizeof(name), "p22a_%s_step%llu.png", tag, static_cast<unsigned long long>(step));
    const std::filesystem::path target = dir / name;
    std::error_code error;
    std::filesystem::rename(newest, target, error);
    if (error)
        Msg("! ph_ragdoll_smoke: capture rename failed: %s", error.message().c_str());
    else
        Msg("ph_ragdoll_smoke: capture tag=%s step=%llu file=%s", tag,
            static_cast<unsigned long long>(step), name);
}

void maybeCaptureSmokeFrames()
{
    const bool dual = OgsrPhysXRagdollMultiInstancesRequested();
    static const std::uint64_t offsets[] = {10, 50, 100, 200, 400, 600};

    // Post-A frame: captured after A is destroyed, while B keeps moving.
    if (g_smokeDualPostPending && smokeCaptureRequested() &&
        OgsrPhysXRagdollV2StepIndex() >= g_smokeDualPostStep + 60)
    {
        g_smokeDualPostPending = false;
        g_smokeDualKeepCamera = false;
        std::vector<float> postBody(OGSR_PHYSX_RAGDOLL_MAX_BODIES * 16);
        OgsrPhysXRagdollPose postPose{};
        if (OgsrPhysXRagdollV2GetAnyPose(postPose, postBody.data(), OGSR_PHYSX_RAGDOLL_MAX_BODIES))
        {
            const std::uint64_t step =
                (static_cast<std::uint64_t>(postPose.stepIndexHigh) << 32) | postPose.stepIndexLow;
            captureSmokeFrame("v2_dual_post", step);
            OgsrPhysXRagdollV2StateInfo b{};
            if (OgsrPhysXRagdollV2GetStateInfo(g_smokeMultiSecondId, b))
                Msg("ph_ragdoll_smoke: v2 dual post step=%llu live=%u B id=%u handle=(%u,%u,%u,%u) "
                    "publish=%u",
                    static_cast<unsigned long long>(step), OgsrPhysXRagdollV2LiveCount(), b.ownerId,
                    b.handle.sceneSlot, b.handle.sceneGeneration, b.handle.instanceSlot,
                    b.handle.instanceGeneration, b.publishFrames);
        }
        stopSmokeCamera();
        return;
    }

    if (!smokeCaptureRequested() || g_smokeCaptureComplete)
        return;
    static std::vector<float> bodyWorld(OGSR_PHYSX_RAGDOLL_MAX_BODIES * 16);

    OgsrPhysXRagdollPose pose{};
    const bool havePose =
        dual ? OgsrPhysXRagdollV2GetAnyPose(pose, bodyWorld.data(), OGSR_PHYSX_RAGDOLL_MAX_BODIES)
             : OgsrPhysXRagdollRuntimeGetPose(pose, bodyWorld.data(), OGSR_PHYSX_RAGDOLL_MAX_BODIES);
    if (!havePose)
        return;
    const std::uint64_t step =
        (static_cast<std::uint64_t>(pose.stepIndexHigh) << 32) | pose.stepIndexLow;

    auto logDual = [&](const char* tag) {
        if (!dual)
            return;
        OgsrPhysXRagdollV2StateInfo a{};
        OgsrPhysXRagdollV2StateInfo b{};
        OgsrPhysXRagdollV2GetStateInfo(g_smokeTargetId, a);
        OgsrPhysXRagdollV2GetStateInfo(g_smokeMultiSecondId, b);
        Msg("ph_ragdoll_smoke: v2 dual frame tag=%s step=%llu live=%u "
            "A id=%u handle=(%u,%u,%u,%u) B id=%u handle=(%u,%u,%u,%u)",
            tag, static_cast<unsigned long long>(step), OgsrPhysXRagdollV2LiveCount(), a.ownerId,
            a.handle.sceneSlot, a.handle.sceneGeneration, a.handle.instanceSlot,
            a.handle.instanceGeneration, b.ownerId, b.handle.sceneSlot, b.handle.sceneGeneration,
            b.handle.instanceSlot, b.handle.instanceGeneration);
    };

    if (!g_smokeCaptureStarted)
    {
        g_smokeCaptureStarted = true;
        g_smokeCaptureBase = step;
        for (std::uint32_t index = 0; index < pose.bodyCount; ++index)
        {
            const float* body = bodyWorld.data() + static_cast<std::size_t>(index) * 16;
            Msg("ph_ragdoll_smoke: initial body=%u pos=(%.4f,%.4f,%.4f)", index, body[12], body[13], body[14]);
        }
        const std::uint64_t delta = step - g_smokeCaptureBase;
        g_smokeCaptureMaxDelta = std::max(g_smokeCaptureMaxDelta, delta);
        if (delta > 5)
            g_smokeCaptureIncomplete = true;
        const char* tag = dual ? "v2_dual_pre" : "pre";
        captureSmokeFrame(tag, step);
        logDual(tag);
        Msg("ph_ragdoll_smoke: capture tag=%s target_step=%llu actual_step=%llu delta=%llu time=%.3f",
            tag, static_cast<unsigned long long>(g_smokeCaptureBase), static_cast<unsigned long long>(step),
            static_cast<unsigned long long>(delta), static_cast<double>(step) * 0.01);
        return;
    }
    if (g_smokeCaptureIndex < 6)
    {
        const std::uint64_t target = g_smokeCaptureBase + offsets[g_smokeCaptureIndex];
        if (step >= target)
        {
            const std::uint64_t delta = step - target;
            g_smokeCaptureMaxDelta = std::max(g_smokeCaptureMaxDelta, delta);
            if (delta > 5)
                g_smokeCaptureIncomplete = true;
            const char* tag = dual ? "v2_dual" : smokeImpulseTag();
            captureSmokeFrame(tag, step);
            logDual(tag);
            Msg("ph_ragdoll_smoke: capture tag=%s target_step=%llu actual_step=%llu delta=%llu time=%.3f",
                tag, static_cast<unsigned long long>(target), static_cast<unsigned long long>(step),
                static_cast<unsigned long long>(delta), static_cast<double>(step) * 0.01);
            ++g_smokeCaptureIndex;
            if (g_smokeCaptureIndex == 6)
            {
                g_smokeCaptureComplete = true;
                Msg("ph_ragdoll_smoke: CAPTURE_%s max_delta=%llu",
                    g_smokeCaptureIncomplete ? "INCOMPLETE" : "COMPLETE",
                    static_cast<unsigned long long>(g_smokeCaptureMaxDelta));
                if (!dual)
                    stopSmokeCamera();
                if (smokeDoubleStopRequested())
                {
                    stopSmokeCamera();
                    Msg("ph_ragdoll_smoke: double stop HUD renderable=%d hud_draw=%d",
                        g_hud ? (HUD().IsRenderable() ? 1 : 0) : -1,
                        psHUD_Flags.test(HUD_DRAW) ? 1 : 0);
                }
            }
        }
    }
}

void CSmokeCameraPreRender::OnRender()
{
    if (!g_smokeCameraActive)
        return;
    const bool mismatch = Device.vCameraPosition.distance_to(g_smokeCameraPosition) > 0.05f ||
                          Device.vCameraDirection.dotproduct(g_smokeCameraDirection) < 0.999f;
    if (mismatch)
    {
        static int logCounter = 0;
        if (++logCounter % 30 == 0)
            Msg("! ph_ragdoll_smoke: pre-render camera overwritten pos=(%.3f,%.3f,%.3f) "
                "dir=(%.3f,%.3f,%.3f) fov=%.2f; applying effector values",
                Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z,
                Device.vCameraDirection.x, Device.vCameraDirection.y, Device.vCameraDirection.z, Device.fFOV);
    }
    Device.vCameraPosition.set(g_smokeCameraPosition);
    Device.vCameraDirection.set(g_smokeCameraDirection);
    Device.vCameraTop.set(g_smokeCameraTop);
    Device.vCameraRight.set(g_smokeCameraRight);
    Device.fFOV = g_smokeCameraFov;
    Device.mView.build_camera_dir(g_smokeCameraPosition, g_smokeCameraDirection, g_smokeCameraTop);
    Device.mProject.build_projection(deg2rad(Device.fFOV), Device.fASPECT, VIEWPORT_NEAR,
                                     g_pGamePersistent->Environment().CurrentEnv->far_plane);
    Device.mFullTransform.mul(Device.mProject, Device.mView);
    if (Render)
        Render->SetCacheXform(Device.mView, Device.mProject);
}

void CSmokeCameraRender::OnRender()
{
    if (!g_smokeCameraActive)
        return;
    OgsrPhysXRagdollPose pose{};
    static std::vector<float> bodyWorld(OGSR_PHYSX_RAGDOLL_MAX_BODIES * 16);
    std::uint64_t step = 0;
    if (OgsrPhysXRagdollRuntimeGetPose(pose, bodyWorld.data(), OGSR_PHYSX_RAGDOLL_MAX_BODIES))
        step = (static_cast<std::uint64_t>(pose.stepIndexHigh) << 32) | pose.stepIndexLow;
    static int logCounter = 0;
    if (++logCounter % 30 == 0)
        Msg("ph_ragdoll_smoke: render camera frame=%u step=%llu pos=(%.3f,%.3f,%.3f) "
            "dir=(%.3f,%.3f,%.3f) view=(%.3f,%.3f,%.3f) fov=%.2f",
            Device.dwFrame, static_cast<unsigned long long>(step), Device.vCameraPosition.x,
            Device.vCameraPosition.y, Device.vCameraPosition.z, Device.vCameraDirection.x,
            Device.vCameraDirection.y, Device.vCameraDirection.z, Device.mView.c.x, Device.mView.c.y,
            Device.mView.c.z, Device.fFOV);
    if (Level().Cameras().Position().distance_to(g_smokeCameraPosition) < 0.05f)
        g_smokeCameraReady = true;
    maybeCaptureSmokeFrames();
}

} // namespace

struct BoneSlot
{
    OgsrPhysXRagdollState* state = nullptr;
    std::uint16_t bone = 0;
};

struct OgsrPhysXRagdollState
{
    OgsrPhysXRagdollCapture capture;
    IKinematics* kinematics = nullptr;
    std::vector<Fmatrix> boneModel;
    std::vector<BoneSlot> slots;
    std::vector<pp::Transform> lastBodyWorld;
    std::vector<std::uint8_t> bodyDynamic;
    std::vector<std::uint16_t> debugBones;
    std::uint32_t publishFrames = 0;
    std::atomic<std::uint32_t> callbackCalls{0};
    std::mutex boneMutex;
    bool callbacksReported = false;
    Fmatrix entityWorld;
    bool reportedInitialError = false;
    bool active = false;
    bool multiV2 = false;
    // True while CCharacterPhysicsSupport::m_physx_ragdoll still owns this
    // wrapper. It becomes false when the NPC detaches (visual replacement or
    // NPC destruction) and the state is quarantined.
    bool gameOwned = true;
    std::uint32_t ownerId = 0;
    OgsrPhysX2RagdollHandle v2Handle{};
};

namespace
{
std::mutex g_v2StatesMutex;
std::vector<OgsrPhysXRagdollState*> g_v2States;
std::vector<OgsrPhysXRagdollState*> g_v2Quarantine;

void registerV2State(OgsrPhysXRagdollState* state)
{
    std::lock_guard<std::mutex> lock(g_v2StatesMutex);
    g_v2States.push_back(state);
}

void unregisterV2State(OgsrPhysXRagdollState* state)
{
    std::lock_guard<std::mutex> lock(g_v2StatesMutex);
    g_v2States.erase(std::remove(g_v2States.begin(), g_v2States.end(), state), g_v2States.end());
    g_v2Quarantine.erase(std::remove(g_v2Quarantine.begin(), g_v2Quarantine.end(), state),
                         g_v2Quarantine.end());
}

void quarantineV2State(OgsrPhysXRagdollState* state)
{
    std::lock_guard<std::mutex> lock(g_v2StatesMutex);
    g_v2States.erase(std::remove(g_v2States.begin(), g_v2States.end(), state), g_v2States.end());
    if (std::find(g_v2Quarantine.begin(), g_v2Quarantine.end(), state) == g_v2Quarantine.end())
        g_v2Quarantine.push_back(state);
}

std::uint32_t v2LiveCount()
{
    std::lock_guard<std::mutex> lock(g_v2StatesMutex);
    return static_cast<std::uint32_t>(g_v2States.size() + g_v2Quarantine.size());
}
} // namespace

namespace
{
void boneCallback(CBoneInstance* instance)
{
    auto* slot = static_cast<BoneSlot*>(instance->callback_param());
    if (!slot || !slot->state)
        return;
    ++slot->state->callbackCalls;
    const std::size_t index = slot->bone;
    std::lock_guard<std::mutex> lock(slot->state->boneMutex);
    if (index < slot->state->boneModel.size())
        instance->mTransform = slot->state->boneModel[index];
}

pp::Transform portable(const Fmatrix& matrix)
{
    pp::RowMatrix4 row{};
    std::memcpy(row.m, matrix.m, sizeof(row.m));
    return pp::transformFromRowMatrix(row);
}

Fmatrix engineMatrix(const pp::Transform& transform)
{
    const pp::RowMatrix4 row = pp::rowMatrixFromTransform(transform);
    Fmatrix matrix;
    std::memcpy(matrix.m, row.m, sizeof(row.m));
    return matrix;
}

pp::Transform rowTransform(const float* values)
{
    pp::RowMatrix4 row{};
    std::memcpy(row.m, values, sizeof(row.m));
    return pp::transformFromRowMatrix(row);
}

void storeRow(const pp::Transform& transform, float* out)
{
    const pp::RowMatrix4 row = pp::rowMatrixFromTransform(transform);
    std::memcpy(out, row.m, sizeof(row.m));
}

const char* safeName(const shared_str& value) { return value.c_str(); }

std::uint32_t countBoneCallbacks(IKinematics* kinematics)
{
    if (!kinematics)
        return 0;
    std::uint32_t count = 0;
    const u16 bones = kinematics->LL_BoneCount();
    for (u16 bone = 0; bone < bones; ++bone)
    {
        if (kinematics->LL_GetBoneInstance(bone).callback() != nullptr)
            ++count;
    }
    return count;
}

bool findTarget(CAI_Stalker*& result, CObject*& object, std::string& description)
{
    result = nullptr;
    object = nullptr;
    description.clear();
    const u32 count = Level().Objects.o_count();
    for (u32 index = 0; index < count; ++index)
    {
        CObject* candidate = Level().Objects.o_get_by_iterator(index);
        CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(candidate);
        if (!stalker)
            continue;
        const char* name = safeName(stalker->cName());
        const char* section = safeName(stalker->cNameSect());
        if (!OgsrPhysXRagdollTargetMatches(name, section))
            continue;
        if (result)
        {
            description = "ambiguous NPC target";
            result = nullptr;
            object = nullptr;
            return false;
        }
        result = stalker;
        object = candidate;
        description = std::string(name) + " / " + section;
    }
    return result != nullptr;
}

std::uint16_t rootBoneId(IKinematics& kinematics)
{
    const u16 pelvis = kinematics.LL_BoneID("bip01_pelvis");
    return pelvis == BI_NONE ? kinematics.LL_GetBoneRoot() : pelvis;
}

bool isAliveExtra(CAI_Stalker* stalker, u16 excludeA, u16 excludeB)
{
    return stalker && stalker->ID() != excludeA && stalker->ID() != excludeB && stalker->g_Alive() &&
           stalker->Visual() && !stalker->PPhysicsShell();
}

CAI_Stalker* findAliveExtra(u16 excludeA, u16 excludeB)
{
    Fvector center{};
    bool haveCenter = false;
    CAI_Stalker* anchor = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(g_smokeTargetId));
    if (!anchor)
        anchor = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(excludeB));
    if (anchor)
    {
        center = anchor->Position();
        haveCenter = true;
    }

    CAI_Stalker* best = nullptr;
    float bestDistance = 1.0e30f;
    for (u32 index = 0; index < Level().Objects.o_count(); ++index)
    {
        CAI_Stalker* candidate = smart_cast<CAI_Stalker*>(Level().Objects.o_get_by_iterator(index));
        if (!isAliveExtra(candidate, excludeA, excludeB))
            continue;
        const float distance = haveCenter ? candidate->Position().distance_to(center) : 0.0f;
        if (!best || distance < bestDistance)
        {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

void applyLethalSmokeHit(CAI_Stalker* stalker, const char* label)
{
    if (!stalker)
        return;
    IKinematics* kinematics = smart_cast<IKinematics*>(stalker->Visual());
    if (!kinematics)
        return;
    const u16 bone = rootBoneId(*kinematics);
    Fvector direction(0.0f, 0.0f, 1.0f);
    Fvector position(0.0f, 0.0f, 0.0f);
    SHit hit(1000.0f, direction, Level().CurrentEntity(), bone, position, 8.0f, ALife::eHitTypeFireWound,
             0.0f, false);
    Msg("ph_ragdoll_smoke: multi lethal hit %s id=%u bone=%u pos=(%.2f,%.2f,%.2f)", label, stalker->ID(),
        bone, stalker->Position().x, stalker->Position().y, stalker->Position().z);
    stalker->Hit(&hit);
}
} // namespace

bool OgsrPhysXRagdollBuildCapture(IKinematics& kinematics,
                                  CPhysicsShell& shell,
                                  const Fmatrix& entityWorld,
                                  const Fvector& velocity,
                                  OgsrPhysXRagdollCapture& capture,
                                  std::string* error)
{
    physics_portable::RagdollCaptureDocument document;
    if (!physics_adapter_ogsr::captureFromOgsr(kinematics, shell, entityWorld, velocity, document, error))
        return false;
    if (document.description.bodies.empty() || document.description.bones.empty())
    {
        if (error)
            *error = "capture produced an empty ragdoll";
        return false;
    }

    capture.description = std::move(document.description);
    capture.initial = std::move(document.initial);

    // OGSR's capture marks the synthetic pelvis/spine root as a static body
    // with zero ODE mass. The PhysX backend promotes it to a dynamic body and
    // derives its mass/inertia from the actual shapes; the description keeps
    // the ODE parity role here.
    if (!capture.description.bodies.empty() && capture.description.rootBody < capture.description.bodies.size())
    {
        const pp::RagdollBodyDesc& root = capture.description.bodies[capture.description.rootBody];
        Msg("ph_ragdoll: capture root body=%u bone=%u role=%u mass=%.3f shapes=%zu", root.id, root.boneId,
            static_cast<unsigned>(root.role), root.mass, root.shapes.size());
    }

    const std::size_t boneCount = kinematics.LL_BoneCount();
    const std::size_t bodyCount = capture.description.bodies.size();
    if (capture.description.bones.size() != boneCount || capture.initial.bodyWorld.size() != bodyCount)
    {
        if (error)
            *error = "capture bone or body count is inconsistent";
        return false;
    }

    const pp::Transform entity = portable(entityWorld);
    capture.initialBoneWorld.resize(boneCount);
    capture.animationBoneModel.resize(boneCount);
    for (std::size_t bone = 0; bone < boneCount; ++bone)
    {
        const pp::Transform model = portable(kinematics.LL_GetTransform(static_cast<u16>(bone)));
        capture.animationBoneModel[bone] = model;
        capture.initialBoneWorld[bone] = entity * model;
    }

    capture.boneBody.assign(boneCount, pp::kInvalidBody);
    capture.boneKind.assign(boneCount, 2);
    capture.mergedLocal.assign(boneCount, pp::Transform{});
    capture.bodyBone.assign(bodyCount, pp::kInvalidBone);
    for (std::size_t body = 0; body < bodyCount; ++body)
    {
        const pp::RagdollBodyDesc& bodyDesc = capture.description.bodies[body];
        if (bodyDesc.boneId >= boneCount)
        {
            if (error)
                *error = "body bone id is out of range";
            return false;
        }
        capture.bodyBone[body] = bodyDesc.boneId;
        capture.boneBody[bodyDesc.boneId] = static_cast<pp::BodyId>(body);
        capture.boneKind[bodyDesc.boneId] = 0;
    }

    for (std::size_t bone = 0; bone < boneCount; ++bone)
    {
        const pp::RagdollBoneDesc& boneDesc = capture.description.bones[bone];
        if (boneDesc.disposition == pp::BoneDisposition::MergedIntoParent)
        {
            if (boneDesc.body >= bodyCount)
            {
                if (error)
                    *error = "merged bone body is out of range";
                return false;
            }
            capture.boneBody[bone] = boneDesc.body;
            capture.boneKind[bone] = 1;
            capture.mergedLocal[bone] =
                pp::inverse(capture.initial.bodyWorld[boneDesc.body]) * capture.initialBoneWorld[bone];
        }
    }

    const std::size_t mappedBodies = std::count_if(capture.boneKind.begin(), capture.boneKind.end(),
                                                   [](std::uint8_t kind) { return kind == 0; });
    if (mappedBodies != bodyCount)
    {
        if (error)
            *error = "not every PhysX body has a source bone";
        return false;
    }
    return true;
}

bool OgsrPhysXRagdollSetStaticFragment(const Fvector& center, std::string* error)
{
    Fvector zAxis(0.0f, 0.0f, 1.0f);
    Fvector yAxis(0.0f, 1.0f, 0.0f);
    Fvector sizes(kStaticSizeX, kStaticSizeY, kStaticSizeZ);
    std::vector<Fvector> triangleVertices;
    if (!Level().ObjectSpace.BoxQuery(center, zAxis, yAxis, sizes, &triangleVertices) || triangleVertices.empty())
    {
        if (error)
            *error = "no static level triangles around the NPC";
        return false;
    }

    const std::size_t sourceTriangles = triangleVertices.size() / 3;
    const std::size_t usedTriangles = std::min<std::size_t>(sourceTriangles, kStaticCapTriangles);
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(usedTriangles * 9);
    indices.reserve(usedTriangles * 3);
    Fvector minimum(FLT_MAX, FLT_MAX, FLT_MAX);
    Fvector maximum(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (std::size_t triangle = 0; triangle < usedTriangles; ++triangle)
    {
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const Fvector& source = triangleVertices[triangle * 3 + corner];
            vertices.push_back(source.x);
            vertices.push_back(source.y);
            vertices.push_back(source.z);
            indices.push_back(static_cast<std::uint32_t>(indices.size()));
            minimum.x = std::min(minimum.x, source.x);
            minimum.y = std::min(minimum.y, source.y);
            minimum.z = std::min(minimum.z, source.z);
            maximum.x = std::max(maximum.x, source.x);
            maximum.y = std::max(maximum.y, source.y);
            maximum.z = std::max(maximum.z, source.z);
        }
    }

    if (!OgsrPhysXRagdollRuntimeSetStaticMesh(vertices.data(), static_cast<uint32_t>(vertices.size() / 3),
                                              indices.data(), static_cast<uint32_t>(indices.size()), error))
        return false;

    Msg("ph_ragdoll: static fragment center=(%.2f,%.2f,%.2f) box=(%.1fx%.1fx%.1f) tris=%zu/%zu "
        "bounds_min=(%.2f,%.2f,%.2f) bounds_max=(%.2f,%.2f,%.2f)",
        center.x, center.y, center.z, kStaticSizeX, kStaticSizeY, kStaticSizeZ, usedTriangles, sourceTriangles,
        minimum.x, minimum.y, minimum.z, maximum.x, maximum.y, maximum.z);
    return true;
}

bool OgsrPhysXRagdollActivate(const OgsrPhysXRagdollCapture& capture,
                              IKinematics& kinematics,
                              const Fmatrix& entityWorld,
                              const Fvector& center,
                              std::uint32_t ownerId,
                              OgsrPhysXRagdollState*& state,
                              std::uint32_t& bodyCount,
                              std::uint32_t& jointCount,
                              std::string* error)
{
    state = nullptr;
    bodyCount = 0;
    jointCount = 0;
    if (!OgsrPhysXRagdollRuntimeAlive())
    {
        if (error)
            *error = "NPC ragdoll runtime is not alive";
        return false;
    }
    if (capture.description.bodies.size() > OGSR_PHYSX_RAGDOLL_MAX_BODIES ||
        capture.description.bones.size() > OGSR_PHYSX_RAGDOLL_MAX_BONES ||
        capture.description.joints.size() > OGSR_PHYSX_RAGDOLL_MAX_JOINTS)
    {
        if (error)
            *error = "capture exceeds the C ABI caps";
        return false;
    }
    if (!OgsrPhysXRagdollMultiInstancesRequested() && !OgsrPhysXRagdollSetStaticFragment(center, error))
        return false;

    std::vector<OgsrPhysXRagdollBone> bones;
    std::vector<OgsrPhysXRagdollBody> bodies;
    std::vector<OgsrPhysXRagdollJoint> joints;
    std::vector<OgsrPhysXRagdollShape> shapes;
    std::vector<OgsrPhysXRagdollMaterial> materials;
    bones.reserve(capture.description.bones.size());
    for (const pp::RagdollBoneDesc& source : capture.description.bones)
    {
        OgsrPhysXRagdollBone bone{};
        bone.id = source.id;
        bone.parent = source.parent;
        bone.visible = source.visible ? 1 : 0;
        bone.disposition = static_cast<std::uint8_t>(source.disposition);
        bone.body = source.body;
        bone.flags = source.sourceShapeFlags;
        storeRow(source.bindLocal, bone.bindLocal);
        std::snprintf(bone.name, sizeof(bone.name), "%s", source.name.c_str());
        bones.push_back(bone);
    }

    materials.reserve(capture.description.materials.size());
    for (const pp::RagdollMaterialDesc& source : capture.description.materials)
    {
        OgsrPhysXRagdollMaterial material{};
        material.id = source.id;
        material.friction = source.friction;
        material.restitution = source.restitution;
        material.spring = source.spring;
        material.damping = source.damping;
        std::snprintf(material.name, sizeof(material.name), "%s", source.name.c_str());
        materials.push_back(material);
    }

    bodies.reserve(capture.description.bodies.size());
    for (const pp::RagdollBodyDesc& source : capture.description.bodies)
    {
        OgsrPhysXRagdollBody body{};
        body.id = source.id;
        body.boneId = source.boneId;
        body.parentBody = source.parentBody;
        body.role = static_cast<std::uint8_t>(source.role);
        body.isFixed = source.isFixed ? 1 : 0;
        body.mass = source.mass;
        body.centerOfMass[0] = source.centerOfMass.x;
        body.centerOfMass[1] = source.centerOfMass.y;
        body.centerOfMass[2] = source.centerOfMass.z;
        std::memcpy(body.inertia, source.inertia, sizeof(body.inertia));
        storeRow(source.bindPose, body.bindPose);
        body.firstShape = static_cast<std::uint32_t>(shapes.size());
        body.shapeCount = static_cast<std::uint32_t>(source.shapes.size());
        body.collisionGroup = source.collisionGroup;
        for (const pp::RagdollShapeDesc& shapeSource : source.shapes)
        {
            OgsrPhysXRagdollShape shape{};
            shape.type = static_cast<std::uint8_t>(shapeSource.type);
            storeRow(shapeSource.local, shape.local);
            shape.halfExtents[0] = shapeSource.halfExtents.x;
            shape.halfExtents[1] = shapeSource.halfExtents.y;
            shape.halfExtents[2] = shapeSource.halfExtents.z;
            shape.radius = shapeSource.radius;
            shape.height = shapeSource.height;
            shape.margin = shapeSource.margin;
            shape.materialId = shapeSource.materialId;
            shape.sourceBoneId = shapeSource.sourceBoneId;
            shape.flags = shapeSource.flags;
            shapes.push_back(shape);
        }
        bodies.push_back(body);
    }

    joints.reserve(capture.description.joints.size());
    for (const pp::RagdollJointDesc& source : capture.description.joints)
    {
        OgsrPhysXRagdollJoint joint{};
        joint.id = source.id;
        joint.boneId = source.boneId;
        joint.parentBody = source.parentBody;
        joint.childBody = source.childBody;
        joint.kind = static_cast<std::uint8_t>(source.kind);
        joint.axisCount = source.axisCount;
        joint.breakable = source.breakable ? 1 : 0;
        joint.swingLimitMode = static_cast<std::uint8_t>(source.swingLimitMode);
        storeRow(source.parentFrame, joint.parentFrame);
        storeRow(source.childFrame, joint.childFrame);
        joint.erp = source.erp;
        joint.cfm = source.cfm;
        joint.friction = source.friction;
        joint.maxForce = source.maxForce;
        joint.targetVelocity = source.targetVelocity;
        joint.breakForce = source.breakForce;
        joint.breakTorque = source.breakTorque;
        joint.flags = source.sourceFlags;
        for (std::size_t axis = 0; axis < source.axisCount && axis < OGSR_PHYSX_RAGDOLL_MAX_AXES; ++axis)
        {
            const pp::RagdollAxisLimit& sourceAxis = source.axes[axis];
            OgsrPhysXRagdollAxis& axisOut = joint.axes[axis];
            axisOut.axis[0] = sourceAxis.axis.x;
            axisOut.axis[1] = sourceAxis.axis.y;
            axisOut.axis[2] = sourceAxis.axis.z;
            axisOut.space = static_cast<std::uint8_t>(sourceAxis.space);
            axisOut.lower = sourceAxis.lower;
            axisOut.upper = sourceAxis.upper;
            axisOut.sourceLower = sourceAxis.sourceLower;
            axisOut.sourceUpper = sourceAxis.sourceUpper;
            axisOut.spring = sourceAxis.spring;
            axisOut.damping = sourceAxis.damping;
            axisOut.zero = sourceAxis.zero;
        }
        joints.push_back(joint);
    }

    const std::size_t bodyCountValue = capture.description.bodies.size();
    std::vector<float> bodyWorld(bodyCountValue * 16);
    std::vector<float> linearVelocity(bodyCountValue * 3);
    std::vector<float> angularVelocity(bodyCountValue * 3);
    std::vector<std::uint8_t> enabled(bodyCountValue, 1);
    for (std::size_t body = 0; body < bodyCountValue; ++body)
    {
        storeRow(capture.initial.bodyWorld[body], bodyWorld.data() + body * 16);
        const pp::Vec3& linear = capture.initial.linearVelocity[body];
        linearVelocity[body * 3 + 0] = linear.x;
        linearVelocity[body * 3 + 1] = linear.y;
        linearVelocity[body * 3 + 2] = linear.z;
        const pp::Vec3& angular = capture.initial.angularVelocity[body];
        angularVelocity[body * 3 + 0] = angular.x;
        angularVelocity[body * 3 + 1] = angular.y;
        angularVelocity[body * 3 + 2] = angular.z;
        enabled[body] = capture.initial.enabled.empty() || capture.initial.enabled[body] ? 1 : 0;
    }

    OgsrPhysXRagdollDefinition definition{};
    definition.rootBody = capture.description.rootBody;
    definition.boneCount = static_cast<std::uint32_t>(bones.size());
    definition.bodyCount = static_cast<std::uint32_t>(bodies.size());
    definition.jointCount = static_cast<std::uint32_t>(joints.size());
    definition.shapeCount = static_cast<std::uint32_t>(shapes.size());
    definition.materialCount = static_cast<std::uint32_t>(materials.size());
    definition.bones = bones.data();
    definition.bodies = bodies.data();
    definition.joints = joints.data();
    definition.shapes = shapes.data();
    definition.materials = materials.data();
    storeRow(capture.initial.entityWorld, definition.entityWorld);
    storeRow(capture.initial.rootToEntity, definition.rootToEntity);
    definition.initialBodyWorld = bodyWorld.data();
    definition.initialLinearVelocity = linearVelocity.data();
    definition.initialAngularVelocity = angularVelocity.data();
    definition.initialEnabled = enabled.data();

    OgsrPhysX2RagdollHandle v2Handle{};
    if (OgsrPhysXRagdollMultiInstancesRequested())
    {
        OgsrPhysX2RagdollInstanceConfig v2Config{};
        v2Config.structSize = sizeof(v2Config);
        v2Config.linearDamping = 0.05f;
        v2Config.angularDamping = 0.2f;
        v2Config.staticRootDensity = 300.0f;
        v2Config.promoteStaticRoot = 1;
        v2Config.disableSelfCollision = 1;
        v2Config.collisionGroup = OGSR_PHYSX2_GROUP_RAGDOLL;
        v2Config.collisionMask = OGSR_PHYSX2_GROUP_RAGDOLL | OGSR_PHYSX2_GROUP_STATIC;
        if (!OgsrPhysXRagdollV2Create(center, definition, v2Config, v2Handle, error))
            return false;
        bodyCount = definition.bodyCount;
        jointCount = definition.jointCount;
    }
    else if (!OgsrPhysXRagdollRuntimeCreateInstance(definition, error, bodyCount, jointCount))
        return false;

    auto* created = new OgsrPhysXRagdollState();
    created->capture = capture;
    created->kinematics = &kinematics;
    created->entityWorld = entityWorld;
    created->multiV2 = OgsrPhysXRagdollMultiInstancesRequested();
    created->ownerId = ownerId;
    created->v2Handle = v2Handle;
    created->lastBodyWorld = capture.initial.bodyWorld;
    created->bodyDynamic.resize(capture.description.bodies.size(), 0);
    for (std::size_t body = 0; body < capture.description.bodies.size(); ++body)
    {
        const pp::RagdollBodyDesc& bodyDesc = capture.description.bodies[body];
        const bool promotedRoot = bodyDesc.role == pp::BodyRole::StaticRoot &&
                                  body == capture.description.rootBody && !bodyDesc.shapes.empty();
        created->bodyDynamic[body] =
            (promotedRoot || (bodyDesc.role != pp::BodyRole::StaticRoot && !bodyDesc.isFixed)) ? 1 : 0;
    }
    const char* debugNames[] = {"bip01_pelvis", "bip01_head", "bip01_l_hand", "bip01_r_hand", "bip01_l_foot",
                                "bip01_r_foot"};
    for (const char* name : debugNames)
    {
        const u16 bone = kinematics.LL_BoneID(name);
        if (bone != BI_NONE)
            created->debugBones.push_back(bone);
    }
    created->boneModel.resize(capture.animationBoneModel.size());
    for (std::size_t bone = 0; bone < created->boneModel.size(); ++bone)
        created->boneModel[bone] = engineMatrix(capture.animationBoneModel[bone]);
    created->slots.resize(capture.animationBoneModel.size());
    for (std::size_t bone = 0; bone < created->slots.size(); ++bone)
    {
        created->slots[bone].state = created;
        created->slots[bone].bone = static_cast<std::uint16_t>(bone);
        if (!kinematics.LL_GetBoneVisible(static_cast<u16>(bone)))
            continue;
        kinematics.LL_GetBoneInstance(static_cast<u16>(bone))
            .set_callback(bctCustom, &boneCallback, &created->slots[bone], TRUE);
    }
    created->active = true;
    if (created->multiV2)
        registerV2State(created);
    kinematics.CalculateBones_Invalidate();

    state = created;
    OgsrPhysXRagdollInfo info{};
    const bool haveInfo = created->multiV2 ? OgsrPhysXRagdollV2GetInfo(created->v2Handle, info)
                                           : OgsrPhysXRagdollRuntimeGetInfo(info);
    if (haveInfo)
    {
        Msg("ph_ragdoll: PhysX ragdoll created bodies=%u joints=%u bones=%zu total_mass=%.2f root_mass=%.2f "
            "root_body=%u root_dynamic=%u",
            bodyCount, jointCount, created->boneModel.size(), info.totalMass, info.rootMass,
            capture.description.rootBody,
            capture.description.rootBody < created->bodyDynamic.size()
                ? created->bodyDynamic[capture.description.rootBody]
                : 0);
    }
    else
    {
        Msg("ph_ragdoll: PhysX ragdoll created bodies=%u joints=%u bones=%zu", bodyCount, jointCount,
            created->boneModel.size());
    }
    return true;
}

bool OgsrPhysXRagdollIsActive(const OgsrPhysXRagdollState* state)
{
    return state && state->active;
}

void OgsrPhysXRagdollRelease(OgsrPhysXRagdollState* state)
{
    if (!state)
        return;
    if (g_hud)
        Msg("ph_ragdoll: release HUD before renderable=%d hud_draw=%d hud_draw_rt=%d",
            HUD().IsRenderable() ? 1 : 0, psHUD_Flags.test(HUD_DRAW) ? 1 : 0,
            psHUD_Flags.test(HUD_DRAW_RT) ? 1 : 0);
    if (!g_smokeDualKeepCamera)
        stopSmokeCamera();
    if (g_hud)
        Msg("ph_ragdoll: release HUD after renderable=%d hud_draw=%d hud_draw_rt=%d",
            HUD().IsRenderable() ? 1 : 0, psHUD_Flags.test(HUD_DRAW) ? 1 : 0,
            psHUD_Flags.test(HUD_DRAW_RT) ? 1 : 0);
    std::size_t removedCallbacks = 0;
    for (std::size_t bone = 0; bone < state->slots.size(); ++bone)
    {
        if (!state->kinematics)
            break;
        CBoneInstance& instance = state->kinematics->LL_GetBoneInstance(static_cast<u16>(bone));
        if (instance.callback() == &boneCallback && instance.callback_param() == &state->slots[bone])
        {
            instance.reset_callback();
            ++removedCallbacks;
        }
    }
    if (state->kinematics)
        state->kinematics->CalculateBones_Invalidate();
    Msg("ph_ragdoll: release state=%p npc=%u handle=(%u,%u,%u,%u) removed_callbacks=%zu "
        "active=%d game_owned=%d",
        static_cast<void*>(state), state->ownerId, state->v2Handle.sceneSlot,
        state->v2Handle.sceneGeneration, state->v2Handle.instanceSlot,
        state->v2Handle.instanceGeneration, removedCallbacks, state->active ? 1 : 0,
        state->gameOwned ? 1 : 0);
    if (state->multiV2)
    {
        if (state->active && OgsrPhysXRagdollV2SceneAlive())
        {
            std::string releaseError;
            if (!OgsrPhysXRagdollV2Destroy(state->v2Handle, &releaseError))
            {
                state->gameOwned = false;
                state->kinematics = nullptr;
                Msg("! ph_ragdoll: v2 instance release failed npc=%u state=%p "
                    "handle=(%u,%u,%u,%u): %s; detached quarantine",
                    state->ownerId, static_cast<void*>(state), state->v2Handle.sceneSlot,
                    state->v2Handle.sceneGeneration, state->v2Handle.instanceSlot,
                    state->v2Handle.instanceGeneration, releaseError.c_str());
                quarantineV2State(state);
                return;
            }
            Msg("ph_ragdoll: v2 ragdoll instance released npc=%u state=%p handle=(%u,%u,%u,%u)",
                state->ownerId, static_cast<void*>(state), state->v2Handle.sceneSlot,
                state->v2Handle.sceneGeneration, state->v2Handle.instanceSlot,
                state->v2Handle.instanceGeneration);
        }
        unregisterV2State(state);
    }
    else if (state->active && OgsrPhysXRagdollRuntimeAlive())
    {
        OgsrPhysXRagdollRuntimeDestroyInstance();
        Msg("ph_ragdoll: PhysX ragdoll instance released");
    }
    if (state->multiV2 && !state->active)
        Msg("ph_ragdoll: released retired game-owned wrapper state=%p npc=%u (backend already destroyed)",
            static_cast<void*>(state), state->ownerId);
    delete state;
}

bool OgsrPhysXRagdollPublish(OgsrPhysXRagdollState& state, IKinematics& kinematics, Fmatrix& entityWorld, std::string* error)
{
    if (!state.active)
    {
        if (error)
            *error = "ragdoll is retired";
        return false;
    }
    OgsrPhysXRagdollPose pose{};
    std::vector<float> bodyWorld(OGSR_PHYSX_RAGDOLL_MAX_BODIES * 16);
    const bool havePose = state.multiV2
                              ? OgsrPhysXRagdollV2GetPose(state.v2Handle, pose, bodyWorld.data(),
                                                          OGSR_PHYSX_RAGDOLL_MAX_BODIES)
                              : OgsrPhysXRagdollRuntimeGetPose(pose, bodyWorld.data(),
                                                               OGSR_PHYSX_RAGDOLL_MAX_BODIES);
    if (!havePose)
    {
        if (error)
            *error = "cannot read the PhysX ragdoll pose";
        return false;
    }
    if (pose.bodyCount != state.capture.description.bodies.size())
    {
        if (error)
            *error = "PhysX pose body count changed";
        return false;
    }

    const pp::Transform entity = rowTransform(pose.entityWorld);
    entityWorld = engineMatrix(entity);
    state.entityWorld = entityWorld;
    state.lastBodyWorld.resize(pose.bodyCount);
    for (std::uint32_t body = 0; body < pose.bodyCount; ++body)
        state.lastBodyWorld[body] = rowTransform(bodyWorld.data() + static_cast<std::size_t>(body) * 16);

    const pp::Transform inverseEntity = pp::inverse(entity);
    float maxTranslation = 0.0f;
    {
        std::lock_guard<std::mutex> lock(state.boneMutex);
        for (std::size_t bone = 0; bone < state.boneModel.size(); ++bone)
        {
        const std::uint8_t kind = state.capture.boneKind[bone];
        pp::Transform world;
        if (kind == 0)
        {
            world = state.lastBodyWorld[state.capture.boneBody[bone]];
        }
        else if (kind == 1)
        {
            world = state.lastBodyWorld[state.capture.boneBody[bone]] * state.capture.mergedLocal[bone];
        }
        else
        {
            world = entity * state.capture.animationBoneModel[bone];
        }
        const pp::Transform model = inverseEntity * world;
        state.boneModel[bone] = engineMatrix(model);

        if (!state.reportedInitialError)
        {
            const pp::Vec3 delta{model.translation.x - state.capture.animationBoneModel[bone].translation.x,
                                 model.translation.y - state.capture.animationBoneModel[bone].translation.y,
                                 model.translation.z - state.capture.animationBoneModel[bone].translation.z};
            maxTranslation = std::max(maxTranslation, std::sqrt(pp::dot(delta, delta)));
        }
    }
    if (!state.reportedInitialError)
    {
        state.reportedInitialError = true;
        Msg("ph_ragdoll: first-frame bone continuity max_translation=%.5f m", maxTranslation);
    }
    if (!state.callbacksReported && state.callbackCalls.load() > 0)
    {
        state.callbacksReported = true;
        Msg("ph_ragdoll: bone callbacks active calls=%u", state.callbackCalls.load());
    }

    if (++state.publishFrames % 120 == 0)
    {
        Msg("ph_ragdoll: pose entity=(%.2f,%.2f,%.2f)", entity.translation.x, entity.translation.y,
            entity.translation.z);
        for (std::uint16_t bone : state.debugBones)
        {
            if (bone >= state.boneModel.size())
                continue;
            Fmatrix world;
            world.mul_43(state.entityWorld, state.boneModel[bone]);
            Msg("ph_ragdoll: bone id=%u world=(%.2f,%.2f,%.2f)", bone, world.c.x, world.c.y, world.c.z);
        }
        OgsrPhysXRagdollDiagnostics diagnostics{};
        const bool haveDiagnostics =
            state.multiV2 ? OgsrPhysXRagdollV2GetDiagnostics(state.v2Handle, 0.0f, diagnostics)
                          : OgsrPhysXRagdollRuntimeGetDiagnostics(0.0f, diagnostics);
        if (haveDiagnostics)
        {
            Msg("ph_ragdoll: energy ke=%.2f pe=%.2f total=%.2f max_w=%.2f anchor=%.4f limit=%.4f contacts=%u "
                "found=%u persist=%u lost=%u active=%u pen=%.4f awake=%u/%u impulses=%u",
                diagnostics.kineticEnergy, diagnostics.potentialEnergy, diagnostics.totalEnergy,
                diagnostics.maxAngularSpeed, diagnostics.maxJointAnchorError, diagnostics.maxJointLimitError,
                diagnostics.contactCount, diagnostics.contactFound, diagnostics.contactPersist,
                diagnostics.contactLost, diagnostics.activeContactPairs, diagnostics.maxPenetration,
                diagnostics.awakeBodies, diagnostics.dynamicBodies, diagnostics.externalImpulses);
        }
    }
    }

    kinematics.CalculateBones_Invalidate();
    return true;
}

bool OgsrPhysXRagdollApplyBoneHit(OgsrPhysXRagdollState& state,
                                  const SHit& hit,
                                  const pp::Vec3& boneSpacePosition,
                                  std::string* error)
{
    if (!state.active)
    {
        if (error)
            *error = "ragdoll is retired";
        return false;
    }
    if (hit.phys_impulse() <= 0.0f)
        return true;
    if (state.capture.description.bodies.empty())
    {
        if (error)
            *error = "no PhysX body for the hit";
        return false;
    }

    const std::uint16_t bone = hit.bone();
    std::uint16_t body = state.capture.description.rootBody;
    if (bone < state.capture.boneBody.size())
    {
        const std::uint16_t mapped = state.capture.boneBody[bone];
        if (mapped != pp::kInvalidBody)
            body = mapped;
    }
    if (body >= state.capture.description.bodies.size())
        body = state.capture.description.rootBody;
    if (body >= state.capture.description.bodies.size())
        body = 0;
    if (!state.bodyDynamic.empty() && !state.bodyDynamic[body])
    {
        std::uint16_t replacement = pp::kInvalidBody;
        for (std::size_t index = 0; index < state.bodyDynamic.size(); ++index)
        {
            if (state.bodyDynamic[index])
            {
                replacement = static_cast<std::uint16_t>(index);
                break;
            }
        }
        if (replacement == pp::kInvalidBody)
        {
            if (error)
                *error = "no dynamic PhysX body for the hit";
            return false;
        }
        body = replacement;
    }

    OgsrPhysXRagdollBodyMass mass{};
    const bool haveMass = state.multiV2
                              ? OgsrPhysXRagdollV2GetBodyMass(state.v2Handle, body, mass, error)
                              : OgsrPhysXRagdollRuntimeGetBodyMass(body, mass, error);
    if (!haveMass)
        return false;
    if (mass.body != body)
    {
        if (error)
            *error = "PhysX body ID does not match the requested body";
        return false;
    }

    const pp::Transform bodyWorld = rowTransform(mass.actorWorld);
    const pp::Vec3 comWorld{mass.worldCenterOfMass[0], mass.worldCenterOfMass[1], mass.worldCenterOfMass[2]};

    pp::BoneWorldKind boneKind = pp::BoneWorldKind::Body;
    pp::Transform mergedLocal{};
    pp::Transform animationBoneModel{};
    if (bone < state.capture.boneKind.size())
    {
        if (state.capture.boneKind[bone] == 1)
        {
            boneKind = pp::BoneWorldKind::Merged;
            mergedLocal = state.capture.mergedLocal[bone];
        }
        else if (state.capture.boneKind[bone] == 2)
        {
            boneKind = pp::BoneWorldKind::Frozen;
            animationBoneModel = state.capture.animationBoneModel[bone];
        }
    }
    const pp::Transform boneWorld = pp::resolveBoneWorld(
        boneKind, bodyWorld, mergedLocal, portable(state.entityWorld), animationBoneModel);
    const pp::Vec3 hitWorld = pp::resolveWorldHitPoint(boneWorld, boneSpacePosition);

    Fvector direction = hit.direction();
    if (direction.magnitude() < EPS)
        direction.set(0.0f, 1.0f, 0.0f);
    else
        direction.normalize();
    const pp::Vec3 impulse{direction.x * hit.phys_impulse(), direction.y * hit.phys_impulse(),
                           direction.z * hit.phys_impulse()};
    const pp::Vec3 lever{hitWorld.x - comWorld.x, hitWorld.y - comWorld.y, hitWorld.z - comWorld.z};
    const pp::Vec3 angular = pp::cross(lever, impulse);

    const float worldPosition[3] = {hitWorld.x, hitWorld.y, hitWorld.z};
    const float worldImpulse[3] = {impulse.x, impulse.y, impulse.z};
    const bool applied =
        state.multiV2 ? OgsrPhysXRagdollV2ApplyImpulse(state.v2Handle, body, worldPosition, worldImpulse, error)
                      : OgsrPhysXRagdollRuntimeApplyImpulse(body, worldPosition, worldImpulse, error);
    if (applied)
    {
        Msg("ph_ragdoll: bone impulse body=%u bone=%u hit=(%.3f,%.3f,%.3f) com=(%.3f,%.3f,%.3f) "
            "lever=(%.3f,%.3f,%.3f) impulse=(%.2f,%.2f,%.2f) angular=(%.2f,%.2f,%.2f)",
            body, bone, hitWorld.x, hitWorld.y, hitWorld.z, comWorld.x, comWorld.y, comWorld.z, lever.x, lever.y,
            lever.z, impulse.x, impulse.y, impulse.z, angular.x, angular.y, angular.z);
    }
    return applied;
}

bool OgsrPhysXRagdollApplyCentralImpulse(OgsrPhysXRagdollState& state,
                                         const pp::Vec3& impulse,
                                         std::string* error)
{
    if (!state.active)
    {
        if (error)
            *error = "ragdoll is retired";
        return false;
    }
    if (state.capture.description.bodies.empty())
    {
        if (error)
            *error = "no PhysX body for the central impulse";
        return false;
    }

    std::uint16_t body = state.capture.description.rootBody;
    if (!state.bodyDynamic.empty() && !state.bodyDynamic[body])
    {
        body = pp::kInvalidBody;
        for (std::size_t index = 0; index < state.bodyDynamic.size(); ++index)
        {
            if (state.bodyDynamic[index])
            {
                body = static_cast<std::uint16_t>(index);
                break;
            }
        }
        if (body == pp::kInvalidBody)
        {
            if (error)
                *error = "no dynamic PhysX body for the central impulse";
            return false;
        }
    }

    OgsrPhysXRagdollBodyMass mass{};
    const bool haveMass = state.multiV2
                              ? OgsrPhysXRagdollV2GetBodyMass(state.v2Handle, body, mass, error)
                              : OgsrPhysXRagdollRuntimeGetBodyMass(body, mass, error);
    if (!haveMass)
        return false;
    const pp::Vec3 comWorld{mass.worldCenterOfMass[0], mass.worldCenterOfMass[1], mass.worldCenterOfMass[2]};
    const float worldImpulse[3] = {impulse.x, impulse.y, impulse.z};
    const bool applied =
        state.multiV2 ? OgsrPhysXRagdollV2ApplyCentralImpulse(state.v2Handle, body, worldImpulse, error)
                      : OgsrPhysXRagdollRuntimeApplyCentralImpulse(body, worldImpulse, error);
    if (applied)
    {
        const float magnitude =
            std::sqrt(impulse.x * impulse.x + impulse.y * impulse.y + impulse.z * impulse.z);
        Msg("ph_ragdoll: central impulse body=%u com=(%.3f,%.3f,%.3f) impulse=(%.2f,%.2f,%.2f) "
            "magnitude=%.2f time=%.3f lever=(0.000,0.000,0.000) angular=(0.00,0.00,0.00)",
            body, comWorld.x, comWorld.y, comWorld.z, impulse.x, impulse.y, impulse.z, magnitude,
            Device.fTimeGlobal);
    }
    return applied;
}

OgsrPhysXRagdollReleaseAllResult OgsrPhysXRagdollReleaseAllV2()
{
    std::vector<OgsrPhysXRagdollState*> active;
    std::vector<OgsrPhysXRagdollState*> quarantined;
    {
        std::lock_guard<std::mutex> lock(g_v2StatesMutex);
        active.swap(g_v2States);
        quarantined.swap(g_v2Quarantine);
    }

    auto removeCallbacks = [](OgsrPhysXRagdollState* state) {
        if (!state || !state->kinematics)
            return;
        std::size_t removed = 0;
        for (std::size_t bone = 0; bone < state->slots.size(); ++bone)
        {
            CBoneInstance& instance = state->kinematics->LL_GetBoneInstance(static_cast<u16>(bone));
            if (instance.callback() == &boneCallback && instance.callback_param() == &state->slots[bone])
            {
                instance.reset_callback();
                ++removed;
            }
        }
        state->kinematics->CalculateBones_Invalidate();
        Msg("ph_ragdoll: removed %zu bone callback(s) state=%p npc=%u", removed,
            static_cast<void*>(state), state->ownerId);
    };

    auto releaseOne = [&](OgsrPhysXRagdollState* state) {
        if (!state)
            return;
        const bool ownedByNpc = state->gameOwned;
        removeCallbacks(state);
        // F1: a quarantined state must never retain a pointer to a visual that
        // may be destroyed before the backend instance is released.
        state->kinematics = nullptr;
        if (state->active && OgsrPhysXRagdollV2SceneAlive())
        {
            std::string error;
            if (!OgsrPhysXRagdollV2Destroy(state->v2Handle, &error))
            {
                Msg("! ph_ragdoll: v2 teardown release failed npc=%u state=%p "
                    "handle=(%u,%u,%u,%u) game_owned=%d: %s; keeping for retry",
                    state->ownerId, static_cast<void*>(state), state->v2Handle.sceneSlot,
                    state->v2Handle.sceneGeneration, state->v2Handle.instanceSlot,
                    state->v2Handle.instanceGeneration, ownedByNpc ? 1 : 0, error.c_str());
                std::lock_guard<std::mutex> lock(g_v2StatesMutex);
                if (std::find(g_v2Quarantine.begin(), g_v2Quarantine.end(), state) ==
                    g_v2Quarantine.end())
                    g_v2Quarantine.push_back(state);
                return;
            }
            Msg("ph_ragdoll: v2 instance released on level teardown npc=%u state=%p "
                "handle=(%u,%u,%u,%u)",
                state->ownerId, static_cast<void*>(state), state->v2Handle.sceneSlot,
                state->v2Handle.sceneGeneration, state->v2Handle.instanceSlot,
                state->v2Handle.instanceGeneration);
        }
        state->active = false;
        state->v2Handle = OgsrPhysX2RagdollHandle{};
        if (!ownedByNpc)
        {
            Msg("ph_ragdoll: deleted detached wrapper state=%p npc=%u", static_cast<void*>(state),
                state->ownerId);
            delete state;
        }
        else
        {
            Msg("ph_ragdoll: keeping game-owned wrapper state=%p npc=%u for owner release",
                static_cast<void*>(state), state->ownerId);
        }
    };

    for (OgsrPhysXRagdollState* state : active)
        releaseOne(state);
    for (OgsrPhysXRagdollState* state : quarantined)
        releaseOne(state);

    OgsrPhysXRagdollReleaseAllResult result;
    {
        std::lock_guard<std::mutex> lock(g_v2StatesMutex);
        result.remaining = static_cast<std::uint32_t>(g_v2Quarantine.size());
        for (OgsrPhysXRagdollState* state : g_v2Quarantine)
        {
            if (state)
                result.handles.push_back(state->v2Handle);
        }
    }
    Msg("ph_ragdoll: ReleaseAllV2 complete=%d remaining=%u live=%u", result.complete() ? 1 : 0,
        result.remaining, v2LiveCount());
    return result;
}

void OgsrPhysXRagdollReconcileSceneDestroyV2()
{
    std::vector<OgsrPhysXRagdollState*> states;
    {
        std::lock_guard<std::mutex> lock(g_v2StatesMutex);
        states.swap(g_v2Quarantine);
    }

    std::size_t deleted = 0;
    std::size_t kept = 0;
    for (OgsrPhysXRagdollState* state : states)
    {
        if (!state)
            continue;

        const bool gameOwned = state->gameOwned;
        Msg("ph_ragdoll: reconcile state=%p npc=%u handle=(%u,%u,%u,%u) game_owned=%d active=%d",
            static_cast<void*>(state), state->ownerId, state->v2Handle.sceneSlot,
            state->v2Handle.sceneGeneration, state->v2Handle.instanceSlot,
            state->v2Handle.instanceGeneration, gameOwned ? 1 : 0, state->active ? 1 : 0);

        // SceneDestroy already destroyed the backend instance. Invalidate the
        // handle and stop every path that could still touch the scene or the
        // old visual, but keep the C++ wrapper alive while its NPC owner still
        // holds CCharacterPhysicsSupport::m_physx_ragdoll.
        state->active = false;
        state->v2Handle = OgsrPhysX2RagdollHandle{};
        state->kinematics = nullptr;
        OgsrPhysXRagdollV2ReleaseLeaseRef();

        if (gameOwned)
        {
            ++kept;
            Msg("ph_ragdoll: reconcile kept game-owned wrapper state=%p npc=%u; owner release required",
                static_cast<void*>(state), state->ownerId);
        }
        else
        {
            ++deleted;
            Msg("ph_ragdoll: reconcile deleted detached wrapper state=%p npc=%u",
                static_cast<void*>(state), state->ownerId);
            delete state;
        }
    }

    Msg("ph_ragdoll: reconciled %zu quarantined instance(s) after SceneDestroy: "
        "deleted=%zu kept_game_owned=%zu live=%u",
        states.size(), deleted, kept, v2LiveCount());
}

std::uint32_t OgsrPhysXRagdollV2LiveCount()
{
    return v2LiveCount();
}

bool OgsrPhysXRagdollV2GetStateInfo(std::uint32_t ownerId, OgsrPhysXRagdollV2StateInfo& info)
{
    std::lock_guard<std::mutex> lock(g_v2StatesMutex);
    auto fill = [&](OgsrPhysXRagdollState* state) {
        info.ownerId = ownerId;
        info.handle = state->v2Handle;
        info.publishFrames = state->publishFrames;
        info.callbackCalls = state->callbackCalls.load();
        info.bodyCount = static_cast<std::uint32_t>(state->capture.description.bodies.size());
        info.jointCount = static_cast<std::uint32_t>(state->capture.description.joints.size());
        return true;
    };
    for (OgsrPhysXRagdollState* state : g_v2States)
    {
        if (state && state->ownerId == ownerId)
            return fill(state);
    }
    for (OgsrPhysXRagdollState* state : g_v2Quarantine)
    {
        if (state && state->ownerId == ownerId)
            return fill(state);
    }
    return false;
}

bool OgsrPhysXRagdollV2GetAnyPose(OgsrPhysXRagdollPose& pose, float* bodyWorld, std::uint32_t maxBodies)
{
    std::lock_guard<std::mutex> lock(g_v2StatesMutex);
    for (OgsrPhysXRagdollState* state : g_v2States)
    {
        if (state && state->active)
            return OgsrPhysXRagdollV2GetPose(state->v2Handle, pose, bodyWorld, maxBodies);
    }
    return false;
}

void OgsrPhysXRagdollSmokeReset()
{
    stopSmokeCamera();
    g_smokeActorStartValid = false;
    g_smokeTargetId = u16(-1);
    g_smokeMultiSecondId = u16(-1);
    g_smokeDualPostPending = false;
    g_smokeDualKeepCamera = false;
    g_smokeDualPostStep = 0;
    g_smokeResetRequested = true;
}

void OgsrPhysXRagdollSmokeCameraStop() { stopSmokeCamera(); }

bool OgsrPhysXRagdollSmokeCameraActive() { return g_smokeCameraActive; }

void OgsrPhysXRagdollSmokeTick()
{
    if (!OgsrPhysXRagdollTargetRequested() || !OgsrPhysXRagdollSmokeRequested())
        return;
    if (!OgsrPhysXRagdollRuntimeAlive() && !OgsrPhysXRagdollOdeSmokeRequested())
        return;

    if (g_smokePostCheckFrames > 0)
    {
        --g_smokePostCheckFrames;
        Msg("ph_ragdoll_smoke: post-check frames=%d camera=(%.3f,%.3f,%.3f) hud_renderable=%d "
            "hud_draw=%d hud_draw_rt=%d",
            g_smokePostCheckFrames, Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z,
            g_hud ? (HUD().IsRenderable() ? 1 : 0) : -1, psHUD_Flags.test(HUD_DRAW) ? 1 : 0,
            psHUD_Flags.test(HUD_DRAW_RT) ? 1 : 0);
        if (g_smokePostCheckFrames == 0)
            Msg("ph_ragdoll_smoke: camera/HUD restored");
    }

    static std::uint32_t frame = 0;
    static bool fired = false;
    static bool listed = false;
    static bool reloadRequested = false;
    static std::uint32_t hitFrame = 0;
    static bool destroyed = false;
    static bool visualReplaced = false;
    static bool odeVisualReplaced = false;
    static bool multiSpawned = false;
    static bool multiSecondHit = false;
    static std::uint64_t multiSecondStep = 0;
    static bool multiFirstDestroyed = false;
    static bool multiThirdHit = false;
    static bool multiFinished = false;
    static bool staged = false;
    static std::uint32_t stagedFrame = 0;
    static std::chrono::steady_clock::time_point stagedTime{};
    static u16 multiSecondId = u16(-1);
    static u16 multiThirdId = u16(-1);
    if (g_smokeResetRequested)
    {
        g_smokeResetRequested = false;
        frame = 0;
        fired = false;
        listed = false;
        reloadRequested = false;
        hitFrame = 0;
        destroyed = false;
        visualReplaced = false;
        odeVisualReplaced = false;
        multiSpawned = false;
        multiSecondHit = false;
        multiSecondStep = 0;
        multiFirstDestroyed = false;
        multiThirdHit = false;
        multiFinished = false;
        staged = false;
        stagedFrame = 0;
        stagedTime = {};
        multiSecondId = u16(-1);
        multiThirdId = u16(-1);
    }
    ++frame;
    if (frame == 1 || frame % 300 == 0)
        Msg("ph_ragdoll_smoke: tick frame=%u", frame);
    if (fired)
    {
        if (hitFrame == 0)
            hitFrame = frame;

        if (OgsrPhysXRagdollVisualReplaceRequested() && !visualReplaced && frame - hitFrame >= 300)
        {
            visualReplaced = true;
            CAI_Stalker* target = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(g_smokeTargetId));
            if (target)
            {
                const shared_str oldVisual = target->cNameVisual();
                shared_str replacement;
                for (u32 index = 0; index < Level().Objects.o_count(); ++index)
                {
                    CAI_Stalker* other = smart_cast<CAI_Stalker*>(Level().Objects.o_get_by_iterator(index));
                    if (!other || other == target)
                        continue;
                    const shared_str candidate = other->cNameVisual();
                    if (!candidate.size() || candidate == oldVisual)
                        continue;
                    replacement = candidate;
                    break;
                }
                if (replacement.size())
                {
                    IKinematics* oldK = smart_cast<IKinematics*>(target->Visual());
                    const UpdateCallback oldCallback = oldK ? oldK->GetUpdateCallback() : nullptr;
                    void* const oldParam = oldK ? oldK->GetUpdateCallbackParam() : nullptr;
                    const std::uint32_t oldBoneCallbacks = countBoneCallbacks(oldK);
                    Msg("ph_ragdoll_smoke: replacing visual of NPC with active PhysX ragdoll id=%u "
                        "old=[%s] new=[%s] old_per_bone_callbacks=%u",
                        target->ID(), safeName(oldVisual), safeName(replacement), oldBoneCallbacks);
                    target->cNameVisual_set(replacement);
                    IKinematics* newK = smart_cast<IKinematics*>(target->Visual());
                    const bool globalPreserved =
                        newK && newK->GetUpdateCallback() == oldCallback && newK->GetUpdateCallbackParam() == oldParam;
                    Msg("ph_ragdoll_smoke: visual replacement completed without crash "
                        "global_callback_preserved=%d new_per_bone_callbacks=%u",
                        globalPreserved ? 1 : 0, countBoneCallbacks(newK));
                    if (CCharacterPhysicsSupport* support = target->character_physics_support())
                    {
                        support->OnBeforeVisualChange();
                        support->OnBeforeVisualChange();
                        Msg("ph_ragdoll_smoke: repeated visual cleanup completed without crash");
                    }
                }
                else
                {
                    Msg("! ph_ragdoll_smoke: no replacement visual available for id=%u", target->ID());
                }
            }
        }
        if (OgsrPhysXRagdollMultiRequested() || OgsrPhysXRagdollMultiInstancesRequested())
        {
            // P3.1-C needs at least 600 physics steps with both v2 ragdolls
            // alive before A is destroyed. The diagnostic v1 sequence keeps
            // its original timing.
            const std::uint32_t v2Hold = OgsrPhysXRagdollMultiInstancesRequested() ? 650u : 0u;
            if (!multiSpawned && frame - hitFrame >= 30)
            {
                multiSpawned = true;
                CAI_Stalker* target = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(g_smokeTargetId));
                if (target)
                {
                    Fvector base = target->Position();
                    Fvector first = base;
                    // B is the nearest extra and is offset laterally to the
                    // diagnostic side camera; C is farther along the view axis.
                    first.z += 3.5f;
                    Fvector second = base;
                    second.z -= 4.5f;
                    Level().g_cl_Spawn("sim_default_stalker_2", 0xff, M_SPAWN_OBJECT_LOCAL, first);
                    Level().g_cl_Spawn("sim_default_stalker_2", 0xff, M_SPAWN_OBJECT_LOCAL, second);
                    Msg("ph_ragdoll_smoke: multi spawned two extra stalkers at (%.2f,%.2f,%.2f)",
                        base.x, base.y, base.z);
                }
            }
            if (!multiSecondHit && frame - hitFrame >= 120)
            {
                CAI_Stalker* extra = findAliveExtra(g_smokeTargetId, u16(-1));
                if (extra)
                {
                    multiSecondHit = true;
                    multiSecondId = extra->ID();
                    applyLethalSmokeHit(extra, "second/ODE-fallback");
                    multiSecondStep = OgsrPhysXRagdollV2StepIndex();
                    g_smokeMultiSecondId = multiSecondId;
                    if (OgsrPhysXRagdollMultiInstancesRequested())
                    {
                        // Restart the capture series from B activation so the
                        // frames are counted from the dual-NPC state.
                        g_smokeCaptureStarted = false;
                        g_smokeCaptureBase = 0;
                        g_smokeCaptureIndex = 0;
                        g_smokeCaptureComplete = false;
                        g_smokeCaptureIncomplete = false;
                        g_smokeCaptureMaxDelta = 0;
                    }
                    Msg("ph_ragdoll_smoke: multi second hit id=%u physx_instance=%d v2_live=%u step=%llu",
                        extra->ID(), OgsrPhysXRagdollRuntimeHasInstance() ? 1 : 0,
                        OgsrPhysXRagdollV2LiveCount(),
                        static_cast<unsigned long long>(multiSecondStep));
                    OgsrPhysXRagdollV2StateInfo info{};
                    if (OgsrPhysXRagdollV2GetStateInfo(multiSecondId, info))
                        Msg("ph_ragdoll_smoke: v2 B id=%u handle=(%u,%u,%u,%u) bodies=%u joints=%u publish=%u "
                            "callbacks=%u",
                            multiSecondId, info.handle.sceneSlot, info.handle.sceneGeneration,
                            info.handle.instanceSlot, info.handle.instanceGeneration, info.bodyCount,
                            info.jointCount, info.publishFrames, info.callbackCalls);
                }
                else
                {
                    Msg("! ph_ragdoll_smoke: multi second NPC not spawned yet");
                }
            }
            const bool v2Ready =
                !OgsrPhysXRagdollMultiInstancesRequested() ||
                (multiSecondStep != 0 && OgsrPhysXRagdollV2StepIndex() >= multiSecondStep + 600);
            if (!multiFirstDestroyed && v2Ready && frame - hitFrame >= 180 + v2Hold)
            {
                multiFirstDestroyed = true;
                CAI_Stalker* target = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(g_smokeTargetId));
                if (target)
                {
                    Msg("ph_ragdoll_smoke: multi destroying first PhysX NPC id=%u", target->ID());
                    target->DestroyObject();
                    if (OgsrPhysXRagdollMultiInstancesRequested())
                    {
                        g_smokeDualPostPending = true;
                        g_smokeDualPostStep = OgsrPhysXRagdollV2StepIndex();
                        g_smokeDualKeepCamera = true;
                    }
                }
            }
            if (!multiThirdHit && multiFirstDestroyed && frame - hitFrame >= 240 + v2Hold)
            {
                CAI_Stalker* extra = findAliveExtra(g_smokeTargetId, multiSecondId);
                if (extra)
                {
                    multiThirdHit = true;
                    multiThirdId = extra->ID();
                    applyLethalSmokeHit(extra, "third/reuse");
                    Msg("ph_ragdoll_smoke: multi third hit id=%u physx_instance_before=%d v2_live_before=%u",
                        extra->ID(), OgsrPhysXRagdollRuntimeHasInstance() ? 1 : 0,
                        OgsrPhysXRagdollV2LiveCount());
                }
                else
                {
                    Msg("! ph_ragdoll_smoke: multi third NPC not found");
                }
            }
            if (!multiFinished && multiThirdHit && frame - hitFrame >= 300 + v2Hold)
            {
                multiFinished = true;
                CAI_Stalker* second = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(multiSecondId));
                CAI_Stalker* third = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(multiThirdId));
                Msg("ph_ragdoll_smoke: multi result second=%d shell=%d third=%d shell=%d physx_instance=%d "
                    "v2_live=%u step=%llu",
                    second ? 1 : 0, second && second->PPhysicsShell() ? 1 : 0, third ? 1 : 0,
                    third && third->PPhysicsShell() ? 1 : 0,
                    OgsrPhysXRagdollRuntimeHasInstance() ? 1 : 0, OgsrPhysXRagdollV2LiveCount(),
                    static_cast<unsigned long long>(OgsrPhysXRagdollV2StepIndex()));
                OgsrPhysXRagdollV2StateInfo secondInfo{};
                OgsrPhysXRagdollV2StateInfo thirdInfo{};
                if (OgsrPhysXRagdollV2GetStateInfo(multiSecondId, secondInfo))
                    Msg("ph_ragdoll_smoke: v2 B result handle=(%u,%u,%u,%u) publish=%u callbacks=%u",
                        secondInfo.handle.sceneSlot, secondInfo.handle.sceneGeneration,
                        secondInfo.handle.instanceSlot, secondInfo.handle.instanceGeneration,
                        secondInfo.publishFrames, secondInfo.callbackCalls);
                if (OgsrPhysXRagdollV2GetStateInfo(multiThirdId, thirdInfo))
                    Msg("ph_ragdoll_smoke: v2 C result handle=(%u,%u,%u,%u) publish=%u callbacks=%u",
                        thirdInfo.handle.sceneSlot, thirdInfo.handle.sceneGeneration,
                        thirdInfo.handle.instanceSlot, thirdInfo.handle.instanceGeneration,
                        thirdInfo.publishFrames, thirdInfo.callbackCalls);
            }
            if (OgsrPhysXRagdollReloadRequested() && !reloadRequested && frame - hitFrame >= 300 + v2Hold)
            {
                reloadRequested = true;
                if (smokeCaptureRequested() && !g_smokeCaptureComplete)
                {
                    g_smokeCaptureIncomplete = true;
                    Msg("ph_ragdoll_smoke: CAPTURE_INCOMPLETE (reload before step 600)");
                }
                stopSmokeCamera();
                Msg("ph_ragdoll_smoke: reload gate requesting level disconnect with active ragdoll");
                OgsrPhysXRagdollRequestLevelDisconnect();
            }
            if (reloadRequested)
                return;
            if (frame - hitFrame > 600 + v2Hold && !destroyed)
            {
                destroyed = true;
                CAI_Stalker* third = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(multiThirdId));
                if (third)
                {
                    Msg("ph_ragdoll_smoke: multi destroying third PhysX NPC id=%u", third->ID());
                    third->DestroyObject();
                }
            }
            return;
        }
        if (OgsrPhysXRagdollReloadRequested())
        {
            if (!reloadRequested && frame - hitFrame >= 300)
            {
                reloadRequested = true;
                if (smokeCaptureRequested() && !g_smokeCaptureComplete)
                {
                    g_smokeCaptureIncomplete = true;
                    Msg("ph_ragdoll_smoke: CAPTURE_INCOMPLETE (reload before step 600)");
                }
                stopSmokeCamera();
                Msg("ph_ragdoll_smoke: reload gate requesting level disconnect with active ragdoll");
                OgsrPhysXRagdollRequestLevelDisconnect();
            }
            return;
        }

        if (OgsrPhysXRagdollOdeSmokeRequested() && frame - hitFrame == 60)
        {
            CAI_Stalker* target = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(g_smokeTargetId));
            Msg("ph_ragdoll_smoke: ODE-only death check target=%d shell=%d physx_instance=%d",
                target ? 1 : 0, target && target->PPhysicsShell() ? 1 : 0,
                OgsrPhysXRagdollRuntimeHasInstance() ? 1 : 0);
        }
        if (OgsrPhysXRagdollVisualReplaceOdeRequested() && !odeVisualReplaced && frame - hitFrame == 360)
        {
            odeVisualReplaced = true;
            CAI_Stalker* odeTarget = nullptr;
            for (u32 index = 0; index < Level().Objects.o_count(); ++index)
            {
                CAI_Stalker* candidate = smart_cast<CAI_Stalker*>(Level().Objects.o_get_by_iterator(index));
                if (!candidate || candidate->ID() == g_smokeTargetId || !candidate->PPhysicsShell() ||
                    !candidate->cNameVisual().size())
                    continue;
                odeTarget = candidate;
                break;
            }
            if (odeTarget)
            {
                const shared_str oldVisual = odeTarget->cNameVisual();
                shared_str replacement;
                for (u32 index = 0; index < Level().Objects.o_count(); ++index)
                {
                    CAI_Stalker* other = smart_cast<CAI_Stalker*>(Level().Objects.o_get_by_iterator(index));
                    if (!other || other == odeTarget)
                        continue;
                    const shared_str candidate = other->cNameVisual();
                    if (candidate.size() && candidate != oldVisual)
                    {
                        replacement = candidate;
                        break;
                    }
                }
                if (replacement.size())
                {
                    Msg("ph_ragdoll_smoke: replacing visual of ODE NPC id=%u old=[%s] new=[%s] shell=%d",
                        odeTarget->ID(), safeName(oldVisual), safeName(replacement),
                        odeTarget->PPhysicsShell() ? 1 : 0);
                    odeTarget->cNameVisual_set(replacement);
                    Msg("ph_ragdoll_smoke: ODE visual replacement completed without crash shell=%d",
                        odeTarget->PPhysicsShell() ? 1 : 0);
                }
                else
                {
                    Msg("! ph_ragdoll_smoke: no ODE replacement visual available");
                }
            }
            else
            {
                Msg("! ph_ragdoll_smoke: no ODE corpse with a visual found");
            }
        }
        if (!destroyed && frame - hitFrame > 600 &&
            (!smokeCaptureRequested() || g_smokeCaptureComplete))
        {
            destroyed = true;
            stopSmokeCamera();
            CAI_Stalker* target = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(g_smokeTargetId));
            if (target)
            {
                Msg("ph_ragdoll_smoke: destroying NPC with active PhysX ragdoll id=%u", target->ID());
                target->DestroyObject();
            }
        }
        return;
    }
    if (frame < 300)
        return;
    if (frame == 300)
    {
        Msg("ph_ragdoll_smoke: loaded stalkers:");
        for (u32 index = 0; index < Level().Objects.o_count(); ++index)
        {
            CObject* candidate = Level().Objects.o_get_by_iterator(index);
            CAI_Stalker* other = smart_cast<CAI_Stalker*>(candidate);
            if (other)
                Msg("ph_ragdoll_smoke:   [%s / %s] id=%u pos=(%.2f,%.2f,%.2f) alive=%d shell=%d",
                    safeName(other->cName()), safeName(other->cNameSect()), other->ID(), other->Position().x,
                    other->Position().y, other->Position().z, other->g_Alive() ? 1 : 0,
                    other->PPhysicsShell() ? 1 : 0);
        }
    }
    if (!g_smokeActorStartValid)
    {
        if (CActor* actor = smart_cast<CActor*>(Level().CurrentEntity()))
        {
            if (CCameraFirstEye* camera = static_cast<CCameraFirstEye*>(actor->cam_FirstEye()))
            {
                g_smokeActorStart = actor->Position();
                g_smokeActorDir = camera->Direction();
                if (g_smokeActorDir.magnitude() > EPS)
                    g_smokeActorDir.normalize();
                g_smokeActorStartValid = true;
            }
        }
    }

    CAI_Stalker* stalker = nullptr;
    CObject* object = nullptr;
    std::string description;
    if (!findTarget(stalker, object, description))
    {
        if (!listed && frame >= kSmokeGiveUpFrame)
        {
            listed = true;
            Msg("! ph_ragdoll_smoke: NPC target not found: %s", description.c_str());
            for (u32 index = 0; index < Level().Objects.o_count(); ++index)
            {
                CObject* candidate = Level().Objects.o_get_by_iterator(index);
                CAI_Stalker* other = smart_cast<CAI_Stalker*>(candidate);
                if (other)
                    Msg("ph_ragdoll_smoke: available [%s / %s] id=%u", safeName(other->cName()),
                        safeName(other->cNameSect()), other->ID());
            }
        }
        return;
    }
    if (!stalker->g_Alive() || stalker->PPhysicsShell())
    {
        if (!listed)
        {
            listed = true;
            Msg("! ph_ragdoll_smoke: target [%s] is not alive or already has a shell", description.c_str());
        }
        return;
    }

    if (!staged)
    {
        staged = true;
        stagedFrame = frame;
        stagedTime = std::chrono::steady_clock::now();
        Msg("ph_ragdoll_smoke: staging at actor_start=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f,%.2f)", g_smokeActorStart.x,
            g_smokeActorStart.y, g_smokeActorStart.z, g_smokeActorDir.x, g_smokeActorDir.y, g_smokeActorDir.z);
        return;
    }
    if (frame - stagedFrame < 180)
        return;
    if (stagedTime.time_since_epoch().count() != 0 &&
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stagedTime)
                .count() < 3000)
        return;

    if (g_smokeCameraActive && smokeCameraMode() != 0 && !g_smokeCameraReady)
    {
        if (frame % 60 == 0)
            Msg("ph_ragdoll_smoke: waiting for render camera ready");
        return;
    }

    IKinematics* kinematics = smart_cast<IKinematics*>(stalker->Visual());
    if (!kinematics)
        return;

    // Diagnostic staging: move the target to the actor's front, refresh its
    // spatial sector (otherwise the renderer culls it at the old location), and
    // look at it from the actor's first-person camera.
    CActor* actor = smart_cast<CActor*>(Level().CurrentEntity());
    if (actor && actor->character_physics_support() && actor->character_physics_support()->movement())
    {
        if (CCameraFirstEye* camera = static_cast<CCameraFirstEye*>(actor->cam_FirstEye()))
        {
            // Fixed world axes keep the staged NPC position identical across
            // runs; the live actor/camera direction drifts during the intro.
            Fvector forward(0.0f, 0.0f, -1.0f);
            forward.mul(3.0f);
            Fvector npcPosition = g_smokeActorStart;
            npcPosition.add(forward);
            npcPosition.y = g_smokeActorStart.y;
            stalker->XFORM().c.set(npcPosition);
            if (CCharacterPhysicsSupport* support = stalker->character_physics_support())
            {
                if (support->movement())
                    support->movement()->SetPosition(npcPosition);
            }
            stalker->spatial_move();

            g_smokeCameraTarget = npcPosition;
            const int cameraMode = smokeCameraMode();
            if (cameraMode != 0)
            {
                Fvector target = npcPosition;
                target.y += 1.0f;

                static const Fvector candidates[] = {
                    Fvector(1.0f, 0.0f, 0.0f), Fvector(-1.0f, 0.0f, 0.0f),
                    Fvector(0.0f, 0.0f, 1.0f), Fvector(0.0f, 0.0f, -1.0f),
                    Fvector(0.7071f, 0.0f, 0.7071f), Fvector(-0.7071f, 0.0f, 0.7071f),
                    Fvector(0.7071f, 0.0f, -0.7071f), Fvector(-0.7071f, 0.0f, -0.7071f),
                };
                Fvector chosen(1.0f, 0.0f, 0.0f);
                float chosenHeight = 2.5f;
                bool found = false;
                for (int pass = 0; pass < 2 && !found; ++pass)
                {
                    const float height = pass == 0 ? 2.5f : 3.5f;
                    for (const Fvector& candidate : candidates)
                    {
                        Fvector side = candidate;
                        if (cameraMode == 2)
                            side.invert();
                        Fvector camPos = target;
                        Fvector offset = side;
                        offset.mul(8.0f);
                        camPos.add(offset);
                        camPos.y = target.y + height;
                        Fvector ray = camPos;
                        ray.sub(target);
                        const float range = ray.magnitude();
                        if (range < EPS)
                            continue;
                        ray.mul(1.0f / range);
                        collide::rq_result result;
                        const bool clear = !Level().ObjectSpace.RayPick(
                            target, ray, range, collide::rqtStatic, result, stalker);
                        Msg("ph_ragdoll_smoke: camera candidate side=(%.2f,%.2f,%.2f) height=%.2f clear=%d",
                            side.x, side.y, side.z, height, clear ? 1 : 0);
                        if (clear)
                        {
                            chosen = side;
                            chosenHeight = height;
                            found = true;
                            break;
                        }
                    }
                }
                Fvector offset = chosen;
                offset.mul(8.0f);
                g_smokeCameraPosition.set(target);
                g_smokeCameraPosition.add(offset);
                g_smokeCameraPosition.y = target.y + chosenHeight;
                g_smokeCameraDirection.sub(target, g_smokeCameraPosition);
                if (g_smokeCameraDirection.magnitude() > EPS)
                    g_smokeCameraDirection.normalize();
                g_smokeCameraTop.set(0.0f, 1.0f, 0.0f);
                g_smokeCameraRight.crossproduct(g_smokeCameraTop, g_smokeCameraDirection);
                if (g_smokeCameraRight.magnitude() > EPS)
                    g_smokeCameraRight.normalize();
                g_smokeCameraTop.crossproduct(g_smokeCameraDirection, g_smokeCameraRight);
                if (g_smokeCameraTop.magnitude() > EPS)
                    g_smokeCameraTop.normalize();
                g_smokeCameraFov = 60.0f;
            }
            if (cameraMode != 0 && !g_smokeCameraActive)
            {
                auto* effector = new CSmokeCameraEffector();
                effector->SetType(Level().Cameras().RequestCamEffectorId());
                Level().Cameras().AddCamEffector(effector);
                g_smokeCameraEffector = effector;
                if (actor)
                {
                    auto* actorEffector = new CSmokeCameraEffector();
                    actorEffector->SetType(actor->Cameras().RequestCamEffectorId());
                    actor->Cameras().AddCamEffector(actorEffector);
                    g_smokeCameraActorEffector = actorEffector;
                }
                g_smokeCameraActive = true;
                g_smokeCaptureStarted = false;
                g_smokeCaptureBase = 0;
                g_smokeCaptureIndex = 0;
                g_smokeCaptureComplete = false;
                g_smokeCaptureIncomplete = false;
                g_smokeCaptureMaxDelta = 0;
                g_smokePostCheckFrames = 0;
                if (smokeHudHiddenRequested() && !g_smokeHudPreHidden)
                {
                    g_smokeHudPreHidden = true;
                    if (g_hud)
                        HUD().SetRenderable(false);
                    psHUD_Flags.set(HUD_DRAW, FALSE);
                    psHUD_Flags.set(HUD_DRAW_RT, FALSE);
                    Msg("ph_ragdoll_smoke: HUD pre-hidden for regression");
                }
                hideSmokeHud();
                if (!g_smokeCameraRenderRegistered)
                {
                    Device.seqRender.Add(&g_smokeCameraRender, REG_PRIORITY_LOW - 10000);
                    g_smokeCameraRenderRegistered = true;
                }
                if (!g_smokeCameraPreRenderRegistered)
                {
                    Device.seqRender.Add(&g_smokeCameraPreRender, REG_PRIORITY_HIGH + 1000);
                    g_smokeCameraPreRenderRegistered = true;
                }
                Msg("ph_ragdoll_smoke: camera effector added type=%d", static_cast<int>(effector->GetType()));
            }
            Msg("ph_ragdoll_smoke: camera staged mode=%s actor=(%.2f,%.2f,%.2f) npc=(%.2f,%.2f,%.2f) "
                "pos=(%.3f,%.3f,%.3f) dir=(%.3f,%.3f,%.3f) target=(%.3f,%.3f,%.3f)",
                cameraMode == 2 ? "side_left" : cameraMode == 1 ? "side" : "top", actor->Position().x,
                actor->Position().y, actor->Position().z, npcPosition.x, npcPosition.y, npcPosition.z,
                g_smokeCameraPosition.x, g_smokeCameraPosition.y, g_smokeCameraPosition.z,
                g_smokeCameraDirection.x, g_smokeCameraDirection.y, g_smokeCameraDirection.z,
                g_smokeCameraTarget.x, g_smokeCameraTarget.y + 1.0f, g_smokeCameraTarget.z);
        }
    }

    if (g_smokeCameraActive && smokeCameraMode() != 0)
    {
        const Fvector& camera = Level().Cameras().Position();
        if (camera.distance_to(g_smokeCameraPosition) > 0.05f)
        {
            if (frame % 60 == 0)
                Msg("ph_ragdoll_smoke: hit gate waiting for camera pos=(%.3f,%.3f,%.3f) "
                    "target=(%.3f,%.3f,%.3f)",
                    camera.x, camera.y, camera.z, g_smokeCameraPosition.x, g_smokeCameraPosition.y,
                    g_smokeCameraPosition.z);
            return;
        }
    }

    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - stagedTime)
                                 .count();
        const Fvector& camera = Level().Cameras().Position();
        Msg("ph_ragdoll_smoke: hit gate frame=%u stagedFrame=%u elapsed_ms=%lld active=%d mode=%d "
            "camera=(%.3f,%.3f,%.3f) target=(%.3f,%.3f,%.3f)",
            frame, stagedFrame, static_cast<long long>(elapsed), g_smokeCameraActive ? 1 : 0,
            smokeCameraMode(), camera.x, camera.y, camera.z, g_smokeCameraPosition.x,
            g_smokeCameraPosition.y, g_smokeCameraPosition.z);
    }

    if (IKinematicsAnimated* animated = smart_cast<IKinematicsAnimated*>(stalker->Visual()))
    {
        CCollectBlends collect;
        animated->LL_IterateBlends(collect);
        CBlend* dominant = nullptr;
        float maxAmount = -1.0f;
        for (CBlend* blend : collect.blends)
        {
            if (blend->blendAmount > maxAmount)
            {
                maxAmount = blend->blendAmount;
                dominant = blend;
            }
        }
        for (CBlend* blend : collect.blends)
        {
            blend->timeCurrent = 0.0f;
            blend->speed = 0.0f;
            blend->blendAmount = (blend == dominant) ? 1.0f : 0.0f;
        }
        kinematics->CalculateBones(TRUE);
        Msg("ph_ragdoll_smoke: animation locked blends=%zu dominant_amount=%.3f",
            collect.blends.size(), maxAmount);
    }

    const std::uint16_t bone = rootBoneId(*kinematics);
    Fmatrix rootWorld;
    rootWorld.mul_43(stalker->XFORM(), kinematics->LL_GetTransform(bone));
    const Fmatrix& npcXform = stalker->XFORM();
    Msg("ph_ragdoll_smoke: pre-impulse npc pos=(%.3f,%.3f,%.3f) i=(%.3f,%.3f,%.3f) j=(%.3f,%.3f,%.3f) "
        "k=(%.3f,%.3f,%.3f) root_bone=%u root_world=(%.3f,%.3f,%.3f) impulse=%.2f time=%.3f",
        npcXform.c.x, npcXform.c.y, npcXform.c.z, npcXform.i.x, npcXform.i.y, npcXform.i.z, npcXform.j.x,
        npcXform.j.y, npcXform.j.z, npcXform.k.x, npcXform.k.y, npcXform.k.z, bone, rootWorld.c.x,
        rootWorld.c.y, rootWorld.c.z, smokeImpulse(), Device.fTimeGlobal);
    if (smokeCaptureRequested())
        captureSmokeFrame("pre_alive", 0);

    Fvector direction(0.0f, 0.0f, 1.0f);
    Fvector position(0.0f, 0.0f, 0.0f);
    SHit hit(1000.0f, direction, Level().CurrentEntity(), bone, position, smokeImpulse(), ALife::eHitTypeFireWound,
             0.0f, false);
    fired = true;
    g_smokeTargetId = stalker->ID();
    Msg("ph_ragdoll_smoke: lethal hit target=[%s] id=%u bone=%u pos=(%.2f,%.2f,%.2f) impulse=%.2f ode_only=%d",
        description.c_str(), stalker->ID(), bone, stalker->Position().x, stalker->Position().y,
        stalker->Position().z, smokeImpulse(), OgsrPhysXRagdollOdeSmokeRequested() ? 1 : 0);
    stalker->Hit(&hit);
}

#endif // OGSR_PHYSX_RAGDOLL
