#pragma once

#include "../xr_3da/CustomHUD.h"
#include "HitMarker.h"

class CHUDTarget;
class CUIGameCustom;

class CHUDManager : public CCustomHUD
{
    friend class CUI;

private:
    //.	CUI*					pUI;
    CUIGameCustom* pUIGame;
    CHitMarker HitMarker;
    CHUDTarget* m_pHUDTarget;
    bool b_online;
    bool m_Renderable{true};
    std::recursive_mutex render_lock;

    void Render_Actor_Shadow(u32 context_id);

public:
    CHUDManager();
    virtual ~CHUDManager();
    virtual void OnEvent(EVENT E, u64 P1, u64 P2);

    void Render_SMAP(u32 context_id) override;
    void Render_MAIN(u32 context_id) override;

    virtual void OnFrame();

    virtual void RenderUI();

    CUIGameCustom* GetGameUI() { return pUIGame; }

    void HitMarked(int idx, float power, const Fvector& dir);
    bool AddGrenade_ForMark(CGrenade* grn);
    void Update_GrenadeView(Fvector& pos_actor);
    void net_Relcase(CObject* obj) override;

    // текущий предмет на который смотрит HUD
    collide::rq_result& GetCurrentRayQuery();

    // устанвка внешнего вида прицела в зависимости от текущей дисперсии
    void SetCrosshairDisp(float dispf, float disps = 0.f);
#ifdef DEBUG
    void SetFirstBulletCrosshairDisp(float fbdispf);
#endif
    void ShowCrosshair(bool show);

    void SetHitmarkType(LPCSTR tex_name);
    void SetGrenadeMarkType(LPCSTR tex_name);

    virtual void OnScreenResolutionChanged() override;
    virtual void Load();
    virtual void OnDisconnected();
    virtual void OnConnected();

    virtual void RenderActiveItemUI();
    virtual bool RenderActiveItemUIQuery();

    CHUDTarget* GetTarget() const { return m_pHUDTarget; }

    // Lain: added
    void SetRenderable(bool renderable) { m_Renderable = renderable; }
    bool IsRenderable() const { return m_Renderable; }
};

IC CHUDManager& HUD() { return *((CHUDManager*)g_hud); }
