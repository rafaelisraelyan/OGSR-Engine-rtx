#include "stdafx.h"
#include "Level.h"
#include "Level_Bullet_Manager.h"
#include "xrServer.h"
#include "xrmessages.h"
#include "game_cl_base.h"
#include "PHCommander.h"
#include "NET_Queue.h"
#include "MainMenu.h"
#include "space_restriction_manager.h"
#include "ai_space.h"
#include "script_engine.h"
#include "stalker_animation_data_storage.h"
#include "client_spawn_manager.h"
#include "seniority_hierarchy_holder.h"
#include "UIGameCustom.h"
#include "string_table.h"
#include "ui/UIGameTutorial.h"
#include "ui/UIPdaWnd.h"
#include "script_vars_storage.h"
#include "../xrPhysics/PhysicsCommon.h"
#ifdef OGSR_PHYSX_RUNTIME
#include "ogsr_physx_runtime.h"
#endif

const int max_objects_size = 2 * 1024;
const int max_objects_size_in_save = 8 * 1024;

extern bool g_b_ClearGameCaptions;

void CLevel::remove_objects()
{
    m_is_removing_objects = true;
    //BOOL b_stored = psDeviceFlags.test(rsDisableObjectsAsCrows);

    u32 m_base, c_base, m_lmaps, c_lmaps;
    Device.m_pRender->ResourcesGetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);

    Msg("~ ObjectResources unload...");
    Msg("~ ObjectResources - base: %d, %d K", c_base, m_base / 1024);
    Msg("~ ObjectResources - lmap: %d, %d K", c_lmaps, m_lmaps / 1024);

//#pragma todo("Разобраться зачем это тут было, мб щас не надо.")
    //Game().reset_ui();

    int loop = 5;
    while (loop)
    {
        R_ASSERT(Server);
        Server->SLS_Clear(); // generate GE_DESTROY for all game objects

        // process destroy queue 
        for (int i = 0; i < 20; ++i)
        {
            snd_Events.clear();
            psNET_Flags.set(NETFLAG_MINIMIZEUPDATES, FALSE);
            // ugly hack for checks that update is twice on frame
            // we need it since we do updates for checking network messages
            ++(Device.dwFrame);
            //psDeviceFlags.set(rsDisableObjectsAsCrows, TRUE);
            ClientReceive();
            ProcessGameEvents();
            Objects.Update(true); //SIMP: !!!
#ifdef DEBUG
            Msg("Update objects list...");
#endif // #ifdef DEBUG
            Objects.dump_all_objects();
        }

        if (Objects.o_count() == 0)
            break;
        else
        {
            --loop;
            Msg("Objects removal next loop. Active objects count=%d", Objects.o_count());
        }
    }

    BulletManager().Clear();
    ph_commander().clear();
    ph_commander_scripts().clear();

    space_restriction_manager().clear();

    ai().script_engine().collect_all_garbage();

    //psDeviceFlags.set(rsDisableObjectsAsCrows, b_stored);
    g_b_ClearGameCaptions = true;

    stalker_animation_data_storage().clear();

    VERIFY(Render);

    if (!g_prefetch)
        Render->models_Clear(TRUE);
    else
        Render->models_Clear(FALSE);

    Render->clear_static_wallmarks();

#ifdef DEBUG
    if (!client_spawn_manager().registry().empty())
        client_spawn_manager().dump();
#endif // DEBUG
    VERIFY(client_spawn_manager().registry().empty());
    client_spawn_manager().clear();

    g_pGamePersistent->destroy_particles(false);

    #pragma todo("Разобраться зачем это тут было, мб щас не надо.")
    ::Sound->stop_emitters();

    g_ScriptVars.clear();

   shader_exports.set_dof_params(0.f, 0.f, 0.f, 0.f);

    // u32 m_base, c_base, m_lmaps, c_lmaps;
    Device.m_pRender->ResourcesGetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);

    Msg("~ ObjectResources unload completed!");
    Msg("~ ObjectResources - base: %d, %d K", c_base, m_base / 1024);
    Msg("~ ObjectResources - lmap: %d, %d K", c_lmaps, m_lmaps / 1024);

    ai().script_engine().collect_all_garbage();

    // clean up scheduler queues
    Engine.Sheduler.Destroy();

    m_is_removing_objects = false;

    void Cleanup_R_occlusion();
    Cleanup_R_occlusion();
}

#ifdef DEBUG
extern void show_animation_stats();
#endif // DEBUG

extern CUISequencer* g_tutorial;
extern CUISequencer* g_tutorial2;

void CLevel::net_Stop()
{
    Msg("- Disconnect");
#ifdef OGSR_PHYSX_RUNTIME
    OgsrPhysXRuntimeOnLevelUnload();
#endif

    if (CurrentGameUI())
    {
        CurrentGameUI()->HideShownDialogs();
        CurrentGameUI()->PdaMenu().Reset();
    }

    if (g_tutorial && !g_tutorial->Persistent())
        g_tutorial->Stop();

    if (g_tutorial2 && !g_tutorial->Persistent())
        g_tutorial2->Stop();

    bReady = false;
    m_bGameConfigStarted = FALSE;

    remove_objects();

    // WARNING ! remove_objects() uses this flag, so position of this line must e here ..
    game_configured = FALSE;

    IGame_Level::net_Stop();
    IPureClient::Disconnect();

    if (Server)
    {
        Server->Disconnect();
        xr_delete(Server);
    }

    ai().script_engine().collect_all_garbage();

    Memory.mem_compact();

#ifdef DEBUG
    show_animation_stats();
#endif // DEBUG
}

void CLevel::ClientSend()
{
    NET_Packet P;
    u32 start = 0;
    {
        if (CurrentControlEntity())
        {
            CObject* pObj = CurrentControlEntity();
            if (!pObj->getDestroy() && pObj->net_Relevant())
            {
                P.w_begin(M_CL_UPDATE);

                P.w_u16(u16(pObj->ID()));
                P.w_u32(0); // reserved place for client's ping

                pObj->net_Export(P);
            }
        }
    };
    //-------------------------------------------------
    while (1)
    {
        P.w_begin(M_UPDATE);
        start = Objects.net_Export(&P, start, max_objects_size);

        if (P.B.count > 2)
        {
            Send(P, net_flags(FALSE));
        }
        else
            break;
    }
}

u32 CLevel::Objects_net_Save(NET_Packet* _Packet, u32 start, u32 max_object_size)
{
    NET_Packet& Packet = *_Packet;
    u32 position;
    for (; start < Objects.o_count(); start++)
    {
        CObject* _P = Objects.o_get_by_iterator(start);
        CGameObject* P = smart_cast<CGameObject*>(_P);
        if (P && !P->getDestroy() && P->net_SaveRelevant())
        {
            Packet.w_u16(u16(P->ID()));
            Packet.w_chunk_open16(position);
            P->net_Save(Packet);

            u32 size = u32(Packet.w_tell() - position) - sizeof(u16);
            ASSERT_FMT(size < 65536, "Object [%s][%u] exceed network-data limit: size = [%u], Pend = [%u], Pstart = [%u]", P->cName().c_str(), P->ID(), size, Packet.w_tell(),
                       position);

            Packet.w_chunk_close16(position);
            if (max_object_size >= (NET_PacketSizeLimit - Packet.w_tell()))
                break;
        }
    }
    return ++start;
}

void CLevel::ClientSave()
{
    NET_Packet P;
    u32 start = 0;

    for (;;)
    {
        P.w_begin(M_SAVE_PACKET);

        start = Objects_net_Save(&P, start, max_objects_size_in_save);

        if (P.B.count > 2)
            Send(P, net_flags(FALSE));
        else
            break;
    }
}

void CLevel::Send(NET_Packet& P, u32 dwFlags, u32 dwTimeout)
{
    ClientID _clid;
    _clid.set(1);
    Server->OnMessage(P, _clid);
}

void CLevel::net_Update()
{
    if (game_configured)
    {
        // If we have enought bandwidth - replicate client data on to server
        ClientSend();
    }
    // If server - perform server-update
    if (Server)
    {
        Server->Update();
    }
}

struct _NetworkProcessor : public pureFrame
{
    virtual void OnFrame()
    {
        if (g_pGameLevel && !Device.Paused())
            g_pGameLevel->net_Update();
    }
} NET_processor;

pureFrame* g_pNetProcessor = &NET_processor;

BOOL CLevel::Connect2Server(LPCSTR options)
{
    m_bConnectResultReceived = true;
    m_bConnectResult = true;

    if (!Connect(options))
        return FALSE;
    //---------------------------------------------------------------------------
    Msg("%c client : connection %s - <%s>", m_bConnectResult ? '*' : '!', m_bConnectResult ? "accepted" : "rejected", m_sConnectResult.c_str());

    net_Syncronised = TRUE;
    if (net_Disconnected)
    {
        OnConnectRejected();
        Disconnect();
        return FALSE;
    }

    //---------------------------------------------------------------------------
    // P.w_begin	(M_CLIENT_REQUEST_CONNECTION_DATA);
    // Send		(P, net_flags(TRUE, TRUE, TRUE, TRUE));
    //---------------------------------------------------------------------------
    return TRUE;
};

void CLevel::OnConnectResult(NET_Packet* P)
{
    // multiple results can be sent during connection they should be "AND-ed"
    m_bConnectResultReceived = true;
    u8 result = P->r_u8();
    P->r_u8();
    string512 ResultStr;
    P->r_stringZ_s(ResultStr);
    ClientID tmp_client_id;
    P->r_clientID(tmp_client_id);
    SetClientID(tmp_client_id);
    if (!result)
    {
        m_bConnectResult = false;
    };
    m_sConnectResult = ResultStr;
};

void CLevel::ClearAllObjects()
{
    u32 CLObjNum = Level().Objects.o_count();

    bool ParentFound = true;

    while (ParentFound)
    {
        ParentFound = false;
        for (u32 i = 0; i < CLObjNum; i++)
        {
            CObject* pObj = Level().Objects.o_get_by_iterator(i);
            if (!pObj->H_Parent())
                continue;
            //-----------------------------------------------------------
            NET_Packet GEN;
            GEN.w_begin(M_EVENT);
            //---------------------------------------------
            GEN.w_u32(Level().timeServer());
            GEN.w_u16(GE_OWNERSHIP_REJECT);
            GEN.w_u16(pObj->H_Parent()->ID());
            GEN.w_u16(u16(pObj->ID()));
            game_events->insert(GEN);
            if (g_bDebugEvents)
                ProcessGameEvents();
            //-------------------------------------------------------------
            ParentFound = true;
            //-------------------------------------------------------------
#ifdef DEBUG
            Msg("Rejection of %s[%d] from %s[%d]", *(pObj->cNameSect()), pObj->ID(), *(pObj->H_Parent()->cNameSect()), pObj->H_Parent()->ID());
#endif
        };
        ProcessGameEvents();
    };

    CLObjNum = Level().Objects.o_count();

    for (u32 i = 0; i < CLObjNum; i++)
    {
        CObject* pObj = Level().Objects.o_get_by_iterator(i);
        if (pObj->H_Parent() != nullptr)
        {
            FATAL("pObj->H_Parent() == nullptr");
        }

        //-----------------------------------------------------------
        NET_Packet GEN;
        GEN.w_begin(M_EVENT);
        //---------------------------------------------
        GEN.w_u32(Level().timeServer());
        GEN.w_u16(GE_DESTROY);
        GEN.w_u16(u16(pObj->ID()));
        game_events->insert(GEN);
        if (g_bDebugEvents)
            ProcessGameEvents();
        //-------------------------------------------------------------
        ParentFound = true;
        //-------------------------------------------------------------
#ifdef DEBUG
        Msg("Destruction of %s[%d]", *(pObj->cNameSect()), pObj->ID());
#endif
    };
    ProcessGameEvents();
};

void CLevel::OnConnectRejected() { IPureClient::OnConnectRejected(); };

void CLevel::net_OnChangeSelfName(NET_Packet* P)
{
    if (!P)
        return;
    string64 NewName;
    P->r_stringZ(NewName);
    if (!strstr(*m_caClientOptions, "/name="))
    {
        string1024 tmpstr;
        xr_strcpy(tmpstr, *m_caClientOptions);
        xr_strcat(tmpstr, "/name=");
        xr_strcat(tmpstr, NewName);
        m_caClientOptions = tmpstr;
    }
    else
    {
        string1024 tmpstr;
        xr_strcpy(tmpstr, *m_caClientOptions);
        *(strstr(tmpstr, "name=") + 5) = 0;
        xr_strcat(tmpstr, NewName);
        const char* ptmp = strstr(strstr(*m_caClientOptions, "name="), "/");
        if (ptmp)
            xr_strcat(tmpstr, ptmp);
        m_caClientOptions = tmpstr;
    }
}
