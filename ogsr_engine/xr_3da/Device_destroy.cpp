#include "stdafx.h"

#include "../Include/xrRender/DrawUtils.h"
#include "render.h"
#include "IGame_Persistent.h"
#include "xr_IOConsole.h"
#include "xr_input.h"
#include "GameDx12Bridge.h"
#include "defines.h"
#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE
#include "GameCameraCaptureAdapter.h"
#endif

void CRenderDevice::_Destroy(BOOL bKeepTextures)
{
    DU->OnDeviceDestroy();

    // before destroy
    b_is_Ready = FALSE;
    Statistic->OnDeviceDestroy();
    ::Render->destroy();
    m_pRender->OnDeviceDestroy(bKeepTextures);

    Memory.mem_compact();
}

void CRenderDevice::Destroy(void)
{
    GameDx12Bridge_Shutdown();

    if (!b_is_Ready)
        return;

    ZoneScoped;

    Log("Destroying Direct3D...");

    pInput->clip_cursor(false);

    _Destroy(FALSE);

    // real destroy
    m_pRender->Destroy();

    // xr_delete					(Resources);
    // HW.DestroyDevice			();

    seqRender.R.clear();
    seqAppActivate.R.clear();
    seqAppDeactivate.R.clear();
    seqAppStart.R.clear();
    seqAppEnd.R.clear();
    seqFrame.R.clear();
    seqFrameMT.R.clear();
    seqDeviceReset.R.clear();
    seqParallel.clear();

    RenderFactory->DestroyRenderDeviceRender(m_pRender);
    m_pRender = nullptr;
    xr_delete(Statistic);
}

extern bool use_reshade;
extern bool init_reshade();
extern void unregister_reshade();

void CRenderDevice::Reset(bool precache)
{
    ZoneScoped;

    if (GameDx12Bridge_UsesNativeViewport())
    {
        // R4 хранит ресурсы игровой логики, но не владеет выводом.
        // vid_restart меняет только размер нативного viewport.
        GameDx12Bridge_ApplyNativeVideoMode(psCurrentVidMode[0], psCurrentVidMode[1]);
        return;
    }

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE
    B2CameraCapture_OnDeviceResetBegin();
#endif

    if (use_reshade) // отключим решейд если он был
        unregister_reshade();

    u32 dwWidth_before = dwWidth;
    u32 dwHeight_before = dwHeight;

    pInput->clip_cursor(false);

    u32 tm_start = TimerAsync();

    m_pRender->Reset(m_hWnd, dwWidth, dwHeight, fWidth_2, fHeight_2);

    if (g_pGamePersistent)
    {
        g_pGamePersistent->Environment().bNeed_re_create_env = TRUE;
    }

    _SetupStates();

    if (precache)
        PreCache(20, false, false);

    u32 tm_end = TimerAsync();

    Msg("*** RESET [%d ms]", tm_end - tm_start);

    if (use_reshade) // основа попробуем включить решейд если он был
        use_reshade = init_reshade();

    pInput->clip_cursor(true);

    seqDeviceReset.Process(rp_DeviceReset);

    GameDx12Bridge_OnDeviceReset(dwWidth, dwHeight);

    if (dwWidth_before != dwWidth || dwHeight_before != dwHeight)
    {
        seqResolutionChanged.Process(rp_ScreenResolutionChanged);
    }
}
