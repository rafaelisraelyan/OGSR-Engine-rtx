#include "stdafx.h"

#ifdef OGSR_DIAGNOSTIC_RUNTIME_WORKAROUND

#include "Level.h"
#include "Hit.h"
#include "MainMenu.h"
#include "ai/stalker/ai_stalker.h"
#include "../Include/xrRender/Kinematics.h"

#include <cstring>
#include <string>

namespace
{
bool g_parsed = false;
bool g_enabled = false;
std::string g_target;
std::uint32_t g_frame = 0;
bool g_fired = false;
bool g_checked = false;
u16 g_target_id = u16(-1);

void parse()
{
    if (g_parsed)
        return;
    g_parsed = true;
    const char* params = Core.Params;
    if (!params)
        return;
    const char* flag = strstr(params, "-ode_death_smoke");
    if (!flag)
        return;
    const char* value = flag + std::strlen("-ode_death_smoke");
    while (*value == ' ')
        ++value;
    if (!*value || *value == '-')
        return;
    const char* end = value;
    while (*end && *end != ' ')
        ++end;
    g_target.assign(value, static_cast<std::size_t>(end - value));
    g_enabled = !g_target.empty();
    Msg("ode_death_smoke: parsed target=[%s] enabled=%d", g_target.c_str(), g_enabled ? 1 : 0);
}

CAI_Stalker* findTarget()
{
    for (u32 index = 0; index < Level().Objects.o_count(); ++index)
    {
        CObject* object = Level().Objects.o_get_by_iterator(index);
        CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(object);
        if (!stalker || !stalker->g_Alive() || !stalker->Visual())
            continue;
        if (g_target == stalker->cName().c_str() || g_target == stalker->cNameSect().c_str())
            return stalker;
    }
    return nullptr;
}

u16 rootBone(IKinematics& kinematics)
{
    const u16 pelvis = kinematics.LL_BoneID("bip01_pelvis");
    return pelvis == BI_NONE ? kinematics.LL_GetBoneRoot() : pelvis;
}
} // namespace

void OgsrOdeSmokeTick()
{
    parse();
    if (!g_enabled)
        return;

    // The archive test runtime has no usable main-menu UI factory, so the
    // normal -start path leaves the level paused. This is a test-only,
    // flag-gated archive workaround: it does not change production MainMenu
    // behavior and is compiled only with OGSR_DIAGNOSTIC_RUNTIME_WORKAROUND.
    static bool runtimePrepared = false;
    if (!runtimePrepared)
    {
        runtimePrepared = true;
        CMainMenu* menu = MainMenu();
        Msg("ode_death_smoke: runtime state paused=%d menu_active=%d level_ready=%d entity=%u objects=%u",
            Device.Paused() ? 1 : 0, menu && menu->IsActive() ? 1 : 0,
            g_pGameLevel ? (g_pGameLevel->bReady ? 1 : 0) : 0,
            Level().CurrentEntity() ? Level().CurrentEntity()->ID() : 0, Level().Objects.o_count());
        if (menu)
        {
            Device.seqFrame.Remove(menu);
            if (menu->IsActive())
                menu->Activate(false);
            Device.seqRender.Remove(menu);
        }
        if (Device.Paused())
            Device.Pause(FALSE, TRUE, TRUE, "ode_death_smoke");
        Msg("ode_death_smoke: archive runtime workaround applied");
    }
    if (Device.Paused())
        Device.Pause(FALSE, TRUE, TRUE, "ode_death_smoke");

    ++g_frame;
    if (g_frame < 300)
        return;

    if (!g_fired)
    {
        CAI_Stalker* target = findTarget();
        if (!target)
        {
            if (g_frame % 300 == 0)
            {
                u32 stalkers = 0;
                for (u32 index = 0; index < Level().Objects.o_count(); ++index)
                {
                    if (smart_cast<CAI_Stalker*>(Level().Objects.o_get_by_iterator(index)))
                        ++stalkers;
                }
                Msg("ode_death_smoke: waiting frame=%u stalkers=%u", g_frame, stalkers);
            }
            return;
        }
        IKinematics* kinematics = smart_cast<IKinematics*>(target->Visual());
        if (!kinematics)
            return;
        Fvector direction(0.0f, 0.0f, 1.0f);
        Fvector position(0.0f, 0.0f, 0.0f);
        SHit hit(1000.0f, direction, Level().CurrentEntity(), rootBone(*kinematics), position, 8.0f,
                 ALife::eHitTypeFireWound, 0.0f, false);
        g_fired = true;
        g_target_id = target->ID();
        Msg("ode_death_smoke: lethal hit target=[%s / %s] id=%u", target->cName().c_str(),
            target->cNameSect().c_str(), target->ID());
        target->Hit(&hit);
        return;
    }

    if (!g_checked && g_frame > 360)
    {
        g_checked = true;
        CAI_Stalker* target = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(g_target_id));
        Msg("ode_death_smoke: check target=%d shell=%d alive=%d", target ? 1 : 0,
            target && target->PPhysicsShell() ? 1 : 0, target && target->g_Alive() ? 1 : 0);
    }
}

#endif // OGSR_DIAGNOSTIC_RUNTIME_WORKAROUND
