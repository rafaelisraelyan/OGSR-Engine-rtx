#include "stdafx.h"
#include "Level.h"
#include "Level_Bullet_Manager.h"
#include "xrServer.h"
#include "game_cl_base.h"
#include "xrmessages.h"
#include "../xr_3da/x_ray.h"
#include "../xr_3da/device.h"
#include "../xr_3da/IGame_Persistent.h"
#include "../xr_3da/XR_IOConsole.h"
#include "MainMenu.h"
#include "string_table.h"
#include "UIGameCustom.h"
#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE
#include "../xr_3da/GameCameraCaptureAdapter.h"
#endif

int g_cl_save_demo = 0;

BOOL CLevel::net_Start(LPCSTR op_server, LPCSTR op_client)
{
    net_start_result_total = TRUE;

    pApp->LoadBegin();

    string64 player_name;
    xr_strcpy(player_name, xr_strlen(Core.UserName) ? Core.UserName : Core.CompName);
    VERIFY(xr_strlen(player_name));

    // make Client Name if options doesn't have it
    LPCSTR NameStart = strstr(op_client, "/name=");
    if (!NameStart)
    {
        string512 tmp;
        xr_strcpy(tmp, op_client);
        xr_strcat(tmp, "/name=");
        xr_strcat(tmp, player_name);
        m_caClientOptions = tmp;
    }
    else
    {
        string1024 ret = "";
        LPCSTR begin = NameStart + xr_strlen("/name=");
        sscanf(begin, "%[^/]", ret);
        if (!xr_strlen(ret))
        {
            string1024 tmpstr;
            xr_strcpy(tmpstr, op_client);
            *(strstr(tmpstr, "name=") + 5) = 0;
            xr_strcat(tmpstr, player_name);
            const char* ptmp = strstr(strstr(op_client, "name="), "/");
            if (ptmp)
                xr_strcat(tmpstr, ptmp);
            m_caClientOptions = tmpstr;
        }
        else
        {
            m_caClientOptions = op_client;
        };
    };
    m_caServerOptions = op_server;
    //---------------------------------------------------------------------------
    g_loading_events.push_back(fastdelegate::MakeDelegate(this, &CLevel::net_start1));
    g_loading_events.push_back(fastdelegate::MakeDelegate(this, &CLevel::net_start2));
    g_loading_events.push_back(fastdelegate::MakeDelegate(this, &CLevel::net_start4));
    g_loading_events.push_back(fastdelegate::MakeDelegate(this, &CLevel::net_start5));
    g_loading_events.push_back(fastdelegate::MakeDelegate(this, &CLevel::net_start6));

    return net_start_result_total;
}

shared_str level_version(const shared_str& server_options);
shared_str level_name(const shared_str& server_options);
bool CLevel::net_start1()
{
    // Start client and server if need it
    if (m_caServerOptions.size())
    {
        //g_pGamePersistent->SetLoadStageTitle("st_server_starting");
        //g_pGamePersistent->LoadTitle();
        g_pGamePersistent->LoadTitle("st_server_starting");

        typedef IGame_Persistent::params params;
        params& p = g_pGamePersistent->m_game_params;
        // Connect
        if (!xr_strcmp(p.m_game_type, "single"))
        {
            Server = xr_new<xrServer>();
        }
        else
        {
            Msg("Unsupported game type: ", p.m_game_type);
            net_start_result_total = FALSE;
            return true;
        }

        if (xr_strcmp(p.m_alife, "alife"))
        {
            shared_str l_ver = game_sv_GameState::parse_level_version(m_caServerOptions);

            map_data.m_name = game_sv_GameState::parse_level_name(m_caServerOptions);

            //g_pGamePersistent->LoadTitle(true, map_data.m_name);

            int id = pApp->Level_ID(map_data.m_name.c_str(), l_ver.c_str(), true);

            if (id < 0)
            {
                Msg("Can't find level: ", map_data.m_name.c_str());
                net_start_result_total = FALSE;
                return true;
            }
            //pApp->SetLoadingLevelText(*map_data.m_name);
        }
    }

    return true;
}

bool CLevel::net_start2()
{
    if (net_start_result_total && m_caServerOptions.size())
    {
        GameDescriptionData game_descr;
        if ((m_connect_server_err = Server->Connect(m_caServerOptions, game_descr)) != xrServer::ErrNoError)
        {
            net_start_result_total = false;
            Msg("! Failed to start server.");
            return true;
        }
        Server->SLS_Default();
        map_data.m_name = Server->level_name(m_caServerOptions);
        //g_pGamePersistent->LoadTitle(true, map_data.m_name);
        //pApp->SetLoadingLevelText(*map_data.m_name);
    }
    return true;
}

bool CLevel::net_start4()
{
    if (!net_start_result_total)
        return true;

    g_loading_events.pop_front();

    g_loading_events.push_front(fastdelegate::MakeDelegate(this, &CLevel::net_start_client6));
    g_loading_events.push_front(fastdelegate::MakeDelegate(this, &CLevel::net_start_client5));
    g_loading_events.push_front(fastdelegate::MakeDelegate(this, &CLevel::net_start_client4));
    g_loading_events.push_front(fastdelegate::MakeDelegate(this, &CLevel::net_start_client3));
    g_loading_events.push_front(fastdelegate::MakeDelegate(this, &CLevel::net_start_client2));
    g_loading_events.push_front(fastdelegate::MakeDelegate(this, &CLevel::net_start_client1));

    return false;
}

bool CLevel::net_start5()
{
    if (net_start_result_total)
    {
        NET_Packet NP;
        NP.w_begin(M_CLIENTREADY);
        Game().local_player->net_Export(NP, TRUE);
        Send(NP, net_flags(TRUE, TRUE));
    };
    return true;
}
bool CLevel::net_start6()
{
    // init bullet manager
    BulletManager().Clear();
    BulletManager().Load();

    pApp->LoadEnd();

    if (net_start_result_total)
    {
#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE
        B2CameraCapture_OnLevelReady(name().c_str());
#endif
        if (strstr(Core.Params, "-$"))
        {
            string256 buf, cmd, param;
            sscanf(strstr(Core.Params, "-$") + 2, "%[^ ] %[^ ] ", cmd, param);
            xr_strconcat(buf, cmd, " ", param);
            Console->Execute(buf);
        }
    }
    /*else
    {
        Msg("! Failed to start client. Check the connection or level existance.");

        if (!map_data.m_map_loaded && map_data.m_name.size() && m_bConnectResult) // if (map_data.m_name == "") - level not loaded, see CLevel::net_start_client3
        {
            string256 level_id_string = {};
            string256 dialog_string = {};
            CStringTable st;
            LPCSTR tmp_map_ver = !!map_data.m_map_version ? map_data.m_map_version.c_str() : "";

            xr_strconcat(level_id_string, st.translate("st_level").c_str(), ":", map_data.m_name.c_str(), "(", tmp_map_ver, "). ");
            xr_strconcat(dialog_string, level_id_string, st.translate("ui_st_map_not_found").c_str());

            DEL_INSTANCE(g_pGameLevel);
            Console->Execute("main_menu on");
        }
        else if (map_data.IsInvalidClientChecksum())
        {
            string256 level_id_string = {};
            string256 dialog_string = {};
            CStringTable st;
            LPCSTR tmp_map_ver = !!map_data.m_map_version ? map_data.m_map_version.c_str() : "";

            xr_strconcat(level_id_string, st.translate("st_level").c_str(), ":", map_data.m_name.c_str(), "(", tmp_map_ver, "). ");
            xr_strconcat(dialog_string, level_id_string, st.translate("ui_st_map_data_corrupted").c_str());

            g_pGameLevel->net_Stop();
            DEL_INSTANCE(g_pGameLevel);
            Console->Execute("main_menu on");
        }
        else
        {
            DEL_INSTANCE(g_pGameLevel);
            Console->Execute("main_menu on");
        }

        return true;
    }*/

    if (CurrentGameUI())
        CurrentGameUI()->OnConnected();

    return true;
}

void CLevel::InitializeClientGame(NET_Packet& P)
{
    string256 game_type_name;
    P.r_stringZ(game_type_name);
    if (game && !xr_strcmp(game_type_name, game->type_name()))
        return;

    xr_delete(game);
#ifdef DEBUG
    Msg("- Game configuring : Started ");
#endif // #ifdef DEBUG
    CLASS_ID clsid = game_GameState::getCLASS_ID(game_type_name, false);
    game = smart_cast<game_cl_GameState*>(NEW_INSTANCE(clsid));
    game->set_type_name(game_type_name);
    game->Init();
    m_bGameConfigStarted = TRUE;

    R_ASSERT(Load_GameSpecific_After());
}
