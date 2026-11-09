#include "stdafx.h"
#include "HUDManager.h"
#include "HUDTarget.h"
#include "Actor.h"
#include "../xr_3da/IGame_Level.h"
#include "../xr_3da/xr_input.h"
#include "GamePersistent.h"
#include "MainMenu.h"
#include "Grenade.h"
#include "Spectator.h"
#include "Car.h"
#include "UIGameCustom.h"
#include "UICursor.h"
#include "string_table.h"
#include "game_cl_base.h"
#ifdef DEBUG
#include "PHDebug.h"
#endif
#include "inventory.h"
#include "torch.h"
#include "player_hud.h"

CUIGameCustom* CurrentGameUI() { return HUD().GetGameUI(); }

CFontManager::CFontManager()
{
    Device.seqDeviceReset.Add(this, REG_PRIORITY_HIGH);

    InitializeFonts();
}

void CFontManager::InitializeFonts()
{
    InitializeFont(pFontMedium, "hud_font_medium");
    InitializeFont(pFontDI, "hud_font_di", CGameFont::fsGradient | CGameFont::fsDeviceIndependent);
    InitializeFont(pFontArial14, "ui_font_arial_14");
    InitializeFont(pFontGraffiti19Russian, "ui_font_graffiti19_russian");
    InitializeFont(pFontGraffiti22Russian, "ui_font_graffiti22_russian");
    InitializeFont(pFontLetterica16Russian, "ui_font_letterica16_russian");
    InitializeFont(pFontLetterica18Russian, "ui_font_letterica18_russian");
    InitializeFont(pFontGraffiti32Russian, "ui_font_graff_32");
    InitializeFont(pFontGraffiti50Russian, "ui_font_graff_50");
    InitializeFont(pFontLetterica25, "ui_font_letter_25");
    InitializeFont(pFontStat, "stat_font", CGameFont::fsDeviceIndependent);

    for (auto* font : m_all_fonts)
    {
        if (font->m_bCustom)
            InitializeFont(font, font->m_font_name.c_str());
    }
}

LPCSTR CFontManager::GetFontTexName(LPCSTR section)
{
    constexpr char* tex_names[]{"texture800", "texture", "texture1600"};
    int def_idx = 1; // default 1024x768
    int idx = def_idx;

    u32 h = Device.dwHeight;

    if (h <= 600)
        idx = 0;
    else if (h < 1024)
        idx = 1;
    else
        idx = 2;

    while (idx >= 0)
    {
        if (pSettings->line_exist(section, tex_names[idx]))
            return pSettings->r_string(section, tex_names[idx]);
        --idx;
    }
    return pSettings->r_string(section, tex_names[def_idx]);
}

void CFontManager::InitializeFont(CGameFont*& F, LPCSTR section, u32 flags)
{
    LPCSTR font_tex_name = GetFontTexName(section);
    R_ASSERT(font_tex_name);

    LPCSTR sh_name = pSettings->r_string(section, "shader");
    if (!F)
    {
        F = xr_new<CGameFont>(sh_name, font_tex_name, section, flags);
        m_all_fonts.push_back(F);
    }
    else
        F->Initialize(sh_name, font_tex_name, section);

    F->m_font_name = section;

    if (!(flags & CGameFont::fsDeviceIndependent))
    {
        if (pSettings->line_exist(section, "scale_x"))
        {
            F->SetWidthScale(pSettings->r_float(section, "scale_x"));
        }
        if (pSettings->line_exist(section, "scale_y"))
        {
            F->SetHeightScale(pSettings->r_float(section, "scale_y"));
        }
    }

    if (pSettings->line_exist(section, "size"))
    {
        const float sz = pSettings->r_float(section, "size");
        if (flags & CGameFont::fsDeviceIndependent)
            F->SetHeightI(sz);
        else
            F->SetHeight(sz);
    }
    if (pSettings->line_exist(section, "interval"))
        F->SetInterval(pSettings->r_fvector2(section, "interval"));
}

CGameFont* CFontManager::InitializeCustomFont(const char* section, const u32 flags)
{
    for (auto* font : m_all_fonts)
    {
        if (!_stricmp(font->m_font_name.c_str(), section))
            return font;
    }

    CGameFont* pFontAdd{};
    InitializeFont(pFontAdd, section, flags);
    pFontAdd->m_bCustom = true;
    return pFontAdd;
}

CFontManager::~CFontManager()
{
    Device.seqDeviceReset.Remove(this);

    pFontMedium = nullptr;
    pFontStat = nullptr;

    pFontDI = nullptr;

    pFontArial14 = nullptr;
    pFontGraffiti19Russian = nullptr;
    pFontGraffiti22Russian = nullptr;
    pFontLetterica16Russian = nullptr;
    pFontLetterica18Russian = nullptr;
    pFontGraffiti32Russian = nullptr;
    pFontGraffiti50Russian = nullptr;
    pFontLetterica25 = nullptr;

    delete_data(m_all_fonts);

    m_all_fonts.clear();
}

void CFontManager::Render()
{
    for (auto* font : m_all_fonts)
    {
        font->OnRender();
    }
}
void CFontManager::OnDeviceReset() { InitializeFonts(); }

//--------------------------------------------------------------------
CHUDManager::CHUDManager() : pUIGame(nullptr), m_pHUDTarget(xr_new<CHUDTarget>()) {}
//--------------------------------------------------------------------
CHUDManager::~CHUDManager()
{
    OnDisconnected();

    if (pUIGame)
        pUIGame->UnLoad();

    xr_delete(pUIGame);
    xr_delete(m_pHUDTarget);
}

//--------------------------------------------------------------------
void CHUDManager::OnFrame()
{
    if (!b_online)
        return;

    if (pUIGame)
        pUIGame->OnFrame();

    m_pHUDTarget->CursorOnFrame();
}
//--------------------------------------------------------------------

bool need_render_hud()
{
    CObject* O = (g_pGameLevel) ? g_pGameLevel->CurrentViewEntity() : nullptr;
    if (!O)
        return false;

    CActor* A = smart_cast<CActor*>(O);
    if (A && (!A->HUDview() || !A->g_Alive()))
        return false;

    if (smart_cast<CCar*>(O))
        return false;

    return true;
}

void CHUDManager::Render_SMAP(u32 context_id) { Render_Actor_Shadow(context_id); }

void CHUDManager::Render_MAIN(u32 context_id)
{
    if (!pUIGame)
        return;

    if (!need_render_hud() || !m_Renderable)
        return;

    ZoneScoped;

    std::scoped_lock slock(render_lock);

    // hud itself
    {
        CObject* O = g_pGameLevel->CurrentViewEntity();
        const auto root = O->H_Root();
        root->renderable_HUD(true);
        O->OnHUDDraw(this, context_id, root);
        root->renderable_HUD(false);
    }

    // Render_Actor_FirstPersonBody(context_id);
}

void CHUDManager::Render_Actor_Shadow(u32 context_id) // added by KD
{
    if (!psActorFlags.test(AF_ACTOR_SHADOW))
        return;

    if (!pUIGame)
        return;

    CObject* O = g_pGameLevel->CurrentViewEntity();
    if (!O)
        return;

    CActor* A = smart_cast<CActor*>(O);
    if (!A) // if (A && !A->HUDview())
        return;

    // KD: we need to render actor shadow only in first eye cam mode because
    // in other modes actor model already in scene graph and renders well
    if (A->active_cam() != eacFirstEye)
        return;
    /*
        const auto flashlight = smart_cast<CFlashlight*>(A->inventory().ItemFromSlot(DETECTOR_SLOT));
        if (flashlight && flashlight->torch_active())
            return;
    */
    const auto torch = smart_cast<CTorch*>(A->inventory().ItemFromSlot(TORCH_SLOT));
    if (torch && torch->torch_active())
        return;

    /* const auto wpn = smart_cast<CWeapon*>(A->inventory().ActiveItem());
    if (wpn && (wpn->IsFlashlightOn()))
        return;*/

    std::scoped_lock slock(render_lock);
    O->renderable_Render(context_id, O->H_Root());
}

bool CHUDManager::RenderActiveItemUIQuery()
{
    if (!need_render_hud() || !m_Renderable)
        return false;

    return (g_player_hud && g_player_hud->render_item_ui_query());
}

void CHUDManager::RenderActiveItemUI() { g_player_hud->render_item_ui(); }

extern ENGINE_API BOOL bShowPauseString;

// отрисовка элементов интерфейса
void CHUDManager::RenderUI()
{
    if (!b_online)
        return;
    if (!need_render_hud() || !m_Renderable)
        return;

    {
        HitMarker.Render();
        if (pUIGame)
            pUIGame->Render();

        UI().RenderFont();
    }

    m_pHUDTarget->Render();

    if (Device.Paused() && bShowPauseString)
    {
        CGameFont* pFont = UI().Font().pFontGraffiti50Russian;
        pFont->SetColor(0x80FF0000);
        LPCSTR _str = CStringTable().translate("st_game_paused").c_str();

        Fvector2 _pos;
        _pos.set(UI_BASE_WIDTH / 2.0f, UI_BASE_HEIGHT / 2.0f);
        UI().ClientToScreenScaled(_pos);
        pFont->SetAligment(CGameFont::alCenter);
        pFont->Out(_pos.x, _pos.y, _str);
        pFont->OnRender();
    }
}

void CHUDManager::OnEvent(EVENT E, u64 P1, u64 P2) {}

collide::rq_result& CHUDManager::GetCurrentRayQuery() { return m_pHUDTarget->GetRQ(); }

void CHUDManager::SetCrosshairDisp(float dispf, float disps) { m_pHUDTarget->GetHUDCrosshair().SetDispersion(psHUD_Flags.test(HUD_CROSSHAIR_DYNAMIC) ? dispf : disps); }

#ifdef DEBUG
void CHUDManager::SetFirstBulletCrosshairDisp(float fbdispf) { m_pHUDTarget->GetHUDCrosshair().SetFirstBulletDispertion(fbdispf); }
#endif

void CHUDManager::ShowCrosshair(bool show) { m_pHUDTarget->ShowCrosshair(show); }

void CHUDManager::HitMarked(int idx, float power, const Fvector& dir)
{
    HitMarker.Hit(dir);
    //clamp(power, 0.0f, 1.0f);
    //pInput->feedback(u16(iFloor(u16(-1) * power)), u16(iFloor(u16(-1) * power)), 0.5f);
}

bool CHUDManager::AddGrenade_ForMark(CGrenade* grn) { return HitMarker.AddGrenade_ForMark(grn); }

void CHUDManager::Update_GrenadeView(Fvector& pos_actor) { HitMarker.Update_GrenadeView(pos_actor); }

void CHUDManager::SetHitmarkType(LPCSTR tex_name) { HitMarker.InitShader(tex_name); }

void CHUDManager::SetGrenadeMarkType(LPCSTR tex_name) { HitMarker.InitShader_Grenade(tex_name); }

// ------------------------------------------------------------------------------------

#include "ui/UIMainIngameWnd.h"
extern CUIXml* pWpnScopeXml;

void CHUDManager::Load()
{
    if (!pUIGame)
    {
        pUIGame = Game().createGameUI();
    }
    else
    {
        pUIGame->SetClGame(&Game());
    }
}

void CHUDManager::OnScreenResolutionChanged()
{
    pUIGame->HideShownDialogs();

    xr_delete(pWpnScopeXml);

    pUIGame->UnLoad();
    pUIGame->Load();

    pUIGame->OnConnected();
}

void CHUDManager::OnDisconnected()
{
    b_online = false;
    if (pUIGame)
        Device.seqFrame.Remove(pUIGame);
}

void CHUDManager::OnConnected()
{
    if (b_online)
        return;
    b_online = true;
    if (pUIGame)
        Device.seqFrame.Add(pUIGame, REG_PRIORITY_LOW - 1000);
}

void CHUDManager::net_Relcase(CObject* obj)
{
    HitMarker.net_Relcase(obj);

    VERIFY(m_pHUDTarget);
    m_pHUDTarget->net_Relcase(obj);
#ifdef DEBUG
    DBG_PH_NetRelcase(obj);
#endif
}

CDialogHolder* CurrentDialogHolder()
{
    if (MainMenu()->IsActive())
        return MainMenu();
    else
        return HUD().GetGameUI();
}
