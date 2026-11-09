#include "stdafx.h"

#ifdef OGSR_DIAGNOSTIC_CAPTURE

#include "Level.h"
#include "entity_alive.h"
#include "ai/stalker/ai_stalker.h"
#include "CharacterPhysicsSupport.h"
#include "PHMovementControl.h"
#include "MainMenu.h"

#include "../xr_3da/XR_IOConsole.h"
#include "../xr_3da/xr_ioc_cmd.h"
#include "../xr_3da/device.h"
#include "../xrPhysics/PHShell.h"
#include "../xrPhysics/PHWorld.h"

#include "../Layers/PhysicsAdapterOGSR/ogsr/ogsr_capture.h"
#include "../Layers/PhysicsAdapterOGSR/include/physics_adapter_ogsr/capture.h"
#include "../Layers/PhysicsPortable/include/physics_portable/json.h"

#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace
{
std::mutex g_capture_mutex;
bool g_capture_pending = false;
u16 g_pending_id = u16(-1);
std::string g_pending_name;
std::string g_pending_path;

bool equals_no_case(const std::string& left, const std::string& right)
{
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        const unsigned char a = static_cast<unsigned char>(left[index]);
        const unsigned char b = static_cast<unsigned char>(right[index]);
        if (std::tolower(a) != std::tolower(b))
            return false;
    }
    return true;
}

std::string sanitize_name(const std::string& source)
{
    std::string result;
    result.reserve(source.size());
    for (const char c : source)
    {
        const unsigned char value = static_cast<unsigned char>(c);
        if (std::isalnum(value) || c == '_' || c == '-')
            result.push_back(c);
        else
            result.push_back('_');
        if (result.size() >= 48)
            break;
    }
    return result.empty() ? "stalker" : result;
}

std::string make_output_path(const std::string& npc_name)
{
    string_path folder;
    const char* root = "$logs$";
    if (!FS.path_exist(root))
        root = "$app_data_root$";

    FS.update_path(folder, root, "");
    string_path file_name;
    xr_sprintf(file_name, sizeof(file_name), "ragdoll_%s_%u_%u.json", sanitize_name(npc_name).c_str(),
               Device.dwFrame, Device.dwTimeGlobal);
    FS.update_path(folder, root, file_name);
    return std::string(folder);
}

CAI_Stalker* find_stalker(const std::string& requested, u16& out_id, std::string& out_name, std::string& error)
{
    CAI_Stalker* by_name = nullptr;
    CAI_Stalker* by_section = nullptr;
    u32 name_matches = 0;
    u32 section_matches = 0;
    std::ostringstream available;

    const u32 count = Level().Objects.o_count();
    for (u32 index = 0; index < count; ++index)
    {
        CObject* object = Level().Objects.o_get_by_iterator(index);
        CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(object);
        if (!stalker)
            continue;

        const std::string name = stalker->cName().c_str();
        const std::string section = stalker->cNameSect().c_str();
        if (available.tellp() < 512)
            available << " [" << name << " / " << section << "]";

        if (equals_no_case(name, requested))
        {
            by_name = stalker;
            ++name_matches;
            out_id = stalker->ID();
            out_name = name;
        }
        if (equals_no_case(section, requested))
        {
            by_section = stalker;
            ++section_matches;
        }
    }

    if (name_matches == 1)
        return by_name;
    if (name_matches > 1)
    {
        error = "ambiguous NPC name; matched " + std::to_string(name_matches) + " loaded stalkers";
        return nullptr;
    }
    if (section_matches == 1)
    {
        out_id = by_section->ID();
        out_name = by_section->cName().c_str();
        return by_section;
    }
    if (section_matches > 1)
    {
        error = "ambiguous NPC section; matched " + std::to_string(section_matches) + " loaded stalkers";
        return nullptr;
    }

    error = "NPC not found; loaded stalkers:" + available.str();
    return nullptr;
}

struct OgsrTemporaryShell
{
    CPhysicsShell* shell = nullptr;
    bool owned = false;

    ~OgsrTemporaryShell()
    {
        if (!owned || !shell)
            return;
        if (shell->isActive())
            shell->Deactivate();
        xr_delete(shell);
        shell = nullptr;
        Msg("ph_dump_ragdoll: temporary diagnostic shell destroyed");
    }
};

u32 count_loaded_stalkers()
{
    u32 result = 0;
    const u32 count = Level().Objects.o_count();
    for (u32 index = 0; index < count; ++index)
    {
        if (smart_cast<CAI_Stalker*>(Level().Objects.o_get_by_iterator(index)))
            ++result;
    }
    return result;
}

u32 count_alive_stalkers()
{
    u32 result = 0;
    const u32 count = Level().Objects.o_count();
    for (u32 index = 0; index < count; ++index)
    {
        CAI_Stalker* candidate = smart_cast<CAI_Stalker*>(Level().Objects.o_get_by_iterator(index));
        if (candidate && candidate->g_Alive())
            ++result;
    }
    return result;
}

void list_loaded_stalkers()
{
    Msg("ph_dump_ragdoll: loaded stalkers:");
    const u32 count = Level().Objects.o_count();
    for (u32 index = 0; index < count; ++index)
    {
        CAI_Stalker* candidate = smart_cast<CAI_Stalker*>(Level().Objects.o_get_by_iterator(index));
        if (candidate)
        {
            CPhysicsShell* shell = candidate->PPhysicsShell();
            Msg("  [%s] id=%u section=[%s] alive=%d shell=%d active=%d", candidate->cName().c_str(),
                candidate->ID(), candidate->cNameSect().c_str(), candidate->g_Alive() ? 1 : 0,
                shell ? 1 : 0, shell && shell->isActive() ? 1 : 0);
        }
    }
}

bool request_capture(const std::string& requested, bool report_failure)
{
    u16 npc_id = u16(-1);
    std::string npc_name;
    std::string error;
    CAI_Stalker* stalker = find_stalker(requested, npc_id, npc_name, error);
    if (!stalker)
    {
        if (report_failure)
            Msg("! ph_dump_ragdoll: %s", error.c_str());
        return false;
    }

    IKinematics* kinematics = smart_cast<IKinematics*>(stalker->Visual());
    if (!kinematics)
    {
        if (report_failure)
            Msg("! ph_dump_ragdoll: [%s] has no valid IKinematics", npc_name.c_str());
        return false;
    }
    // A dead story NPC may have no prebuilt ODE shell. The collector can build
    // a temporary shell for it exactly as for a live NPC; this does not alter
    // health or death state.

    const std::string output_path = make_output_path(npc_name);
    {
        std::lock_guard<std::mutex> lock(g_capture_mutex);
        g_capture_pending = true;
        g_pending_id = npc_id;
        g_pending_name = npc_name;
        g_pending_path = output_path;
    }
    Msg("ph_dump_ragdoll: queued [%s] id=%u for the next level frame; output [%s]", npc_name.c_str(), npc_id,
        output_path.c_str());
    return true;
}

void maybe_auto_request()
{
    static bool handled = false;
#ifdef OGSR_DIAGNOSTIC_RUNTIME_WORKAROUND
    static bool runtime_prepared = false;
#endif
    static u32 first_seen = 0;
    if (handled || !g_pGameLevel)
        return;

    const char* flag = strstr(Core.Params, "-ph_dump_ragdoll ");
    if (!flag)
        return;
    if (!first_seen)
        first_seen = Device.dwTimeGlobal;

#ifdef OGSR_DIAGNOSTIC_RUNTIME_WORKAROUND
    if (!runtime_prepared)
    {
        runtime_prepared = true;
        // This standalone archive-based test runtime has no matching main-menu
        // UI factory instance. Activate(false) is null-safe in the explicit
        // archive workaround build; remove the broken menu from the frame
        // callback as well.
        if (CMainMenu* menu = MainMenu())
        {
            Device.seqFrame.Remove(menu);
            if (menu->IsActive())
                menu->Activate(false);
            Device.seqRender.Remove(menu);
            Device.Pause(FALSE, TRUE, TRUE, "ph_dump_ragdoll");
            if (g_pGameLevel)
            {
                Device.seqFrame.Add(g_pGameLevel);
                Device.seqRender.Add(g_pGameLevel);
            }
            Msg("ph_dump_ragdoll: archive runtime workaround deactivated the main menu");
        }
    }

    // Later start/load stages may pause the demo or reactivate the menu again.
    // Keep the archive diagnostic session running until the one-shot request is
    // done. This block is absent from a normal diagnostic build.
    if (Device.Paused())
        Device.Pause(FALSE, TRUE, TRUE, "ph_dump_ragdoll");
    if (CMainMenu* menu = MainMenu())
    {
        if (menu->IsActive())
            menu->Activate(false);
    }
#endif

    const char* start = flag + xr_strlen("-ph_dump_ragdoll ");
    while (*start == ' ')
        ++start;
    const char* end = start;
    while (*end && *end != ' ')
        ++end;

    const std::string requested(start, end);
    if (equals_no_case(requested, "scan"))
    {
        static u32 next_scan = 0;
        if (next_scan && Device.dwTimeGlobal < next_scan)
            return;
        next_scan = Device.dwTimeGlobal + 30000;
        list_loaded_stalkers();
        const u32 alive = count_alive_stalkers();
        Msg("ph_dump_ragdoll: scan alive=%u total=%u", alive, count_loaded_stalkers());
        if (alive > 0 || Device.dwTimeGlobal - first_seen > 300000)
            handled = true;
        return;
    }

    const u32 stalker_count = count_loaded_stalkers();
    if (!stalker_count)
    {
        if (Device.dwTimeGlobal - first_seen > 90000)
        {
            Msg("! ph_dump_ragdoll: no stalkers online after 90 seconds");
            handled = true;
        }
        return;
    }

    if (equals_no_case(requested, "list"))
    {
        list_loaded_stalkers();
        handled = true;
        return;
    }

    if (request_capture(requested, false))
        handled = true;
    else if (Device.dwTimeGlobal - first_seen > 90000)
    {
        Msg("! ph_dump_ragdoll: requested NPC [%s] was not available after 90 seconds", requested.c_str());
        handled = true;
    }
}

void execute_capture(u16 npc_id, const std::string& expected_name, const std::string& output_path)
{
    if (!g_pGameLevel)
    {
        Msg("! ph_dump_ragdoll: no level is loaded");
        return;
    }

    CObject* object = Level().Objects.net_Find(npc_id);
    CAI_Stalker* entity = smart_cast<CAI_Stalker*>(object);
    if (!entity)
    {
        Msg("! ph_dump_ragdoll: NPC id %u is no longer loaded", npc_id);
        return;
    }
    if (!equals_no_case(entity->cName().c_str(), expected_name))
    {
        Msg("! ph_dump_ragdoll: NPC id %u no longer has the expected name [%s]", npc_id, expected_name.c_str());
        return;
    }

    IKinematics* kinematics = smart_cast<IKinematics*>(entity->Visual());
    if (!kinematics)
    {
        Msg("! ph_dump_ragdoll: [%s] has no valid IKinematics", expected_name.c_str());
        return;
    }

    CPhysicsShell* shell = entity->PPhysicsShell();
    const float healthBefore = entity->GetfHealth();
    const bool aliveBefore = entity->g_Alive() != FALSE;
    Msg("ph_dump_ragdoll: target [%s] before capture health=%g alive=%d shell=%d active=%d",
        expected_name.c_str(), healthBefore, aliveBefore ? 1 : 0, shell ? 1 : 0,
        shell && shell->isActive() ? 1 : 0);
    OgsrTemporaryShell temporary;
    if (!shell)
    {
        if (!ph_world || !ph_world->Exist())
        {
            Msg("! ph_dump_ragdoll: physics world is not available");
            return;
        }
        if (ph_world->Processing() || ph_world->IsFreezed())
        {
            Msg("! ph_dump_ragdoll: physics world is busy; capture skipped");
            return;
        }

        CPhysicsShell* built = P_create_Shell();
        if (!built)
        {
            Msg("! ph_dump_ragdoll: P_create_Shell failed");
            return;
        }
        temporary.shell = built;
        temporary.owned = true;
        built->preBuild_FromKinematics(kinematics);
        built->mXFORM.set(entity->XFORM());
        built->SmoothElementsInertia(0.3f);
        built->set_PhysicsRefObject(entity);
        built->Build();
        shell = built;
        Msg("ph_dump_ragdoll: temporary diagnostic shell built for [%s]", expected_name.c_str());
    }

    Fvector velocity;
    velocity.set(0.0f, 0.0f, 0.0f);
    CCharacterPhysicsSupport* support = entity->character_physics_support();
    if (support && support->movement())
        support->movement()->GetCharacterVelocity(velocity);

    physics_portable::RagdollCaptureDocument document;
    std::string error;
    if (!physics_adapter_ogsr::captureFromOgsr(*kinematics, *shell, entity->XFORM(), velocity, document, &error))
    {
        Msg("! ph_dump_ragdoll: capture failed: %s", error.c_str());
        return;
    }

    const physics_adapter_ogsr::OgsrParityReport parity =
        physics_adapter_ogsr::verifyOgsrParity(*kinematics, *shell, document);
    Msg("ph_dump_ragdoll: ODE parity bodies=%u joints=%u mass_err=%g com_err=%g inertia_err=%g "
        "axis_count_err=%g axis_dir_err=%g axis_space_mismatch=%u inert_axes=%u limit_err=%g spring_err=%g "
        "bone_pose_err=%g ok=%d",
        static_cast<unsigned>(parity.bodies), static_cast<unsigned>(parity.joints), parity.maxMassError,
        parity.maxComError, parity.maxInertiaError, parity.maxAxisCountError,
        parity.maxAxisDirectionError, static_cast<unsigned>(parity.axisSpaceMismatches),
        static_cast<unsigned>(parity.legacyInertAxes), parity.maxLimitError, parity.maxSpringError,
        parity.bonePoseError, parity.ok ? 1 : 0);
    if (!parity.details.empty())
        Msg("ph_dump_ragdoll: ODE parity details: %s", parity.details.c_str());
    if (!parity.reason.empty())
        Msg("ph_dump_ragdoll: Euler axis note: %s", parity.reason.c_str());
    if (!parity.ok)
    {
        Msg("! ph_dump_ragdoll: ODE parity failed; capture not written");
        return;
    }

    document.ogsrCommit = "7c75614ffa1ba30bc45989bc122bec529752c695+dirty";
    document.visual = kinematics->getDebugName().c_str();
    document.modelHash = 0;

    std::string json;
    if (!physics_portable::writeRagdollCapture(document, json, &error))
    {
        Msg("! ph_dump_ragdoll: JSON validation failed: %s", error.c_str());
        return;
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output)
    {
        Msg("! ph_dump_ragdoll: cannot open [%s]", output_path.c_str());
        return;
    }
    output.write(json.data(), static_cast<std::streamsize>(json.size()));
    output.close();
    if (!output.good())
    {
        Msg("! ph_dump_ragdoll: write failed for [%s]", output_path.c_str());
        return;
    }

    Msg("ph_dump_ragdoll: wrote [%s] bones=%zu bodies=%zu joints=%zu", output_path.c_str(),
        document.description.bones.size(), document.description.bodies.size(),
        document.description.joints.size());
    CPhysicsShell* shellAfter = entity->PPhysicsShell();
    Msg("ph_dump_ragdoll: target [%s] after capture health=%g alive=%d shell=%d active=%d",
        expected_name.c_str(), entity->GetfHealth(), entity->g_Alive() ? 1 : 0, shellAfter ? 1 : 0,
        shellAfter && shellAfter->isActive() ? 1 : 0);
}

class CCC_DumpRagdoll final : public IConsole_Command
{
public:
    explicit CCC_DumpRagdoll(LPCSTR name) : IConsole_Command(name)
    {
        bLowerCaseArgs = FALSE;
        bEmptyArgsHandled = FALSE;
    }

    void Execute(LPCSTR arguments) override
    {
        if (!g_pGameLevel)
        {
            Msg("! ph_dump_ragdoll: no level is loaded");
            return;
        }
        if (!arguments || !arguments[0])
        {
            Msg("usage: ph_dump_ragdoll <npc_name>");
            return;
        }

        const std::string requested = arguments;
        if (equals_no_case(requested, "list"))
            list_loaded_stalkers();
        else
            request_capture(requested, true);
    }

    void Info(TInfo& info) override { xr_strcpy(info, "ph_dump_ragdoll <npc_name>"); }
};

} // namespace

void RegisterOgsrCaptureCommand()
{
    static CCC_DumpRagdoll command("ph_dump_ragdoll");
    Console->AddCommand(&command);
}

void OgsrRagdollCaptureOnFrame()
{
    maybe_auto_request();

    u16 npc_id;
    std::string npc_name;
    std::string output_path;
    {
        std::lock_guard<std::mutex> lock(g_capture_mutex);
        if (!g_capture_pending)
            return;
        npc_id = g_pending_id;
        npc_name = g_pending_name;
        output_path = g_pending_path;
        g_capture_pending = false;
    }

    if (!g_pGameLevel)
        return;
    if (!ph_world || !ph_world->Exist())
        return;

    if (ph_world->Processing() || ph_world->IsFreezed())
    {
        std::lock_guard<std::mutex> lock(g_capture_mutex);
        if (!g_capture_pending)
        {
            g_capture_pending = true;
            g_pending_id = npc_id;
            g_pending_name = npc_name;
            g_pending_path = output_path;
        }
        return;
    }

    execute_capture(npc_id, npc_name, output_path);
}

#endif // OGSR_DIAGNOSTIC_CAPTURE
