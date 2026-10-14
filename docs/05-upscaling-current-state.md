# 05 — Апскейлинг: текущее состояние

Документ описывает **как именно** устроены DLSS и FSR 3 в этом форке —
чтобы изменения вносились в правильное место.

## 1. Главный файл

**`ogsr_engine/Layers/xrRender/RenderTargetPhaseAA.cpp`** (990 строк).
Здесь всё: SMAA, TAA, DLSS, FSR 3 (основной + для прицела), CAS,
`PhaseAA()`, `Phase3DSSUpscale()`, выбор render-разрешения.

## 2. Консольные переменные

Объявления — `ogsr_engine/Layers/xrRender/xrRender_console.h`:

```cpp
extern ECORE_API u32   ps_r_pp_aa_mode;              // режим AA/апскейла
extern ECORE_API u32   ps_r_dlss_preset;             // Default/F/J/K/L/M
extern ECORE_API u32   ps_r_dlss_quality;            // DLAA/Quality/Balanced/Perf/UltraPerf
extern ECORE_API u32   ps_r_fsr3_quality;            // NATIVE_AA .. ULTRA_PERFORMANCE
extern ECORE_API float ps_r_dlss_3dss_scale_factor; // масштаб 3D-прицела
extern char           ps_r_dlss_dll_version[64];    // заполняется из DLL
```

Перечисления:

```cpp
// xrRender_console.h:17-23
enum { DLSS_QUALITY_DLAA, DLSS_QUALITY_QUALITY, DLSS_QUALITY_BALANCED,
       DLSS_QUALITY_PERFORMANCE, DLSS_QUALITY_ULTRA_PERFORMANCE };

// xrRender_console.h:26-32
enum { FSR3_QUALITY_NATIVE_AA, FSR3_QUALITY_QUALITY, FSR3_QUALITY_BALANCED,
       FSR3_QUALITY_PERFORMANCE, FSR3_QUALITY_ULTRA_PERFORMANCE };

// xrRender_console.h:265-272
enum { NO_AA, DLSS, FSR3, TAA, SMAA };
```

**Значения по умолчанию:**

```cpp
// xrRender_console.cpp:36
u32 ps_r_pp_aa_mode = DLSS;          // DLSS включён по умолчанию
u32 ps_r_dlss_quality = DLSS_QUALITY_DLAA;   // но без апскейла — только AA
u32 ps_r_fsr3_quality = FSR3_QUALITY_NATIVE_AA;
float ps_r_dlss_3dss_scale_factor{1.0f};
```

Команды — `xrRender_console.cpp:892-896`:

| Команда | Тип | Класс |
|---|---|---|
| `r_aa_mode` | токен | `CCC_Token` |
| `r_aa_dlss_quality` | токен | `CCC_DlssQuality` (`:427`) |
| `r_aa_dlss_preset` | токен | `CCC_DlssRenderPreset` (`:439`, валидирует доступность) |
| `r_aa_fsr3_quality` | токен | `CCC_Token` |
| `r_aa_dlss_version` | read-only | `CCC_DlssDllVersion` (`:413`) |
| `r_3dss_scale_factor` | float | `CCC_Token` |

## 3. Выбор render-разрешения

`CRenderTarget::ConfigureTemporalRenderSize()` — `RenderTargetPhaseAA.cpp:451-501`.

```cpp
SetTemporalRenderSize(Device.dwWidth, Device.dwHeight,
                      Device.dwWidth, Device.dwHeight);   // натив по умолчанию

if (ps_r_pp_aa_mode == DLSS)
{
    if (!NGXWrapper.Initialize(20082024151405ull)) { ps_r_pp_aa_mode = FSR3; }  // откат
    else {
        NGXWrapper.RefreshAvailablePresets();
        if (NGXWrapper.QueryOptimalSettings(Device.dwWidth, Device.dwHeight, quality, info))
            SetTemporalRenderSize(info.renderWidth, info.renderHeight,
                                  Device.dwWidth, Device.dwHeight);
        else { ps_r_dlss_quality = DLSS_QUALITY_DLAA; }   // не создавать upscaler
    }
    return;
}

if (ps_r_pp_aa_mode != FSR3) return;

ffxFsr3UpscalerGetRenderResolutionFromQualityMode(&w, &h, Device.dwWidth,
                                                 Device.dwHeight, quality);
SetTemporalRenderSize(w, h, Device.dwWidth, Device.dwHeight);
```

Каскад отказоустойчивости: **DLSS → FSR3 → TAA → SMAA**.
Виден и в `ConfigureTemporalRenderSize()`, и в `PhaseAA()` (`:938-948`).

> ⚠️ Смена качества **физически меняет размеры RT**, поэтому требует
> `vid_restart`. Движок об этом предупреждает:
> `--[DLSS] quality changes resize physical render targets; apply video settings or run vid_restart`
> (`RenderTargetPhaseAA.cpp:566`).

## 4. Обёртка DLSS — `NGXWrapper`

`RenderTargetPhaseAA.cpp:157-439`, единственный экземпляр `} NGXWrapper;` (`:439`).

### Определение версии загруженной DLL

```cpp
char ps_r_dlss_dll_version[64]{};                     // :53
struct DlssDllFileVersion { u32 major, minor, patch, revision; bool valid; };  // :55-62
static DlssDllFileVersion g_dlss_dl_file_version{};   // :64
static bool DlssDllVersionAtLeast(u32, u32, u32);     // :66-75
static void QueryLoadedDlssDllVersion();               // :77-121
```

`QueryLoadedDlssDllVersion()` читает ресурсы версии
`nvngx_dlss.dll` через `GetModuleHandleW` → `GetModuleFileNameW` →
`GetFileVersionInfoW` → `VerQueryValueW`. Результат — в `ps_r_dlss_dll_version`,
команда `r_aa_dlss_version`.

Используется для определения доступности **Preset L/M**, добавленных в SDK 310.5.0:

```cpp
// :244-252
u32 mask = 1u << NVSDK_NGX_DLSS_Hint_Render_Preset_Default;
mask |= 1u << NVSDK_NGX_DLSS_Hint_Render_Preset_F;
mask |= 1u << NVSDK_NGX_DLSS_Hint_Render_Preset_J;
mask |= 1u << NVSDK_NGX_DLSS_Hint_Render_Preset_K;
if (DlssDllVersionAtLeast(310, 5, 0)) {
    mask |= 1u << NVSDK_NGX_DLSS_Hint_Render_Preset_L;
    mask |= 1u << NVSDK_NGX_DLSS_Hint_Render_Preset_M;
}
```

### AppID

Жёстко зашит: `20082024151405ull` (`:235`, `:458`, `:542`). Это идентификатор
проекта NVIDIA. Для форка, возможно, стоит завести свой через
`NVSDK_NGX_D3D11_Init_with_ProjectID`.

### Создание

`Create()` — `:286-365`:

```cpp
NVSDK_NGX_Parameter_SetUI(NgxParameters, preset_name, preset);

int32_t flags{};
flags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;    // :323  — ВКЛЮЧЁН
flags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;       // :327  — ВКЛЮЧЁН
// flags |= ..._DoSharpening;                      // :332  — ЗАКОММЕНТИРОВАН
// flags |= ..._MVJittered;                        // :337  — ЗАКОММЕНТИРОВАН
// flags |= ..._AlphaUpscaling;                    // :341  — ЗАКОММЕНТИРОВАН

dlssCreateParams.Feature.InWidth            = renderSize.Width;
dlssCreateParams.Feature.InHeight           = renderSize.Height;
dlssCreateParams.Feature.InTargetWidth      = displaySize.Width;
dlssCreateParams.Feature.InTargetHeight     = displaySize.Height;
dlssCreateParams.Feature.InPerfQualityValue = quality;
dlssCreateParams.InFeatureCreateFlags       = flags;
NGX_D3D11_CREATE_DLSS_EXT(HW.get_context(CHW::IMM_CTX_ID), &Handle,
                          NgxParameters, &dlssCreateParams);
```

### Оценка

`Draw()` — `:395-434`:

```cpp
dlssEvalParams.Feature.pInColor         = Target->rt_Generic_0->pSurface;
dlssEvalParams.Feature.pInOutput        = OutputRT;              // rt_Generic_combine
dlssEvalParams.pInDepth                 = Target->rt_zbuffer->pSurface;
dlssEvalParams.pInMotionVectors         = Target->rt_Velocity->pSurface;

dlssEvalParams.InRenderSubrectDimensions = saved_renderSize;
dlssEvalParams.InJitterOffsetX = ps_r_taa_jitter_full.x;
dlssEvalParams.InJitterOffsetY = ps_r_taa_jitter_full.y;
dlssEvalParams.InMVScaleX = -static_cast<float>(saved_renderSize.Width)  * 0.5f;
dlssEvalParams.InMVScaleY =  static_cast<float>(saved_renderSize.Height) * 0.5f;
dlssEvalParams.InReset     = resetHistory ? 1 : 0;

NGX_D3D11_EVALUATE_DLSS_EXT(HW.get_context(CHW::IMM_CTX_ID), Handle,
                            NgxParameters, &dlssEvalParams);
```

Форматы входов:
- цвет — `R16G16B16A16_FLOAT`
- глубина — `D24_UNORM_S8_UINT` (комментарий в коде `:410-411` отмечает это)
- motion vectors — `R16G16_FLOAT`

## 5. Обёртка FSR 3 — `Fsr3Wrapper`

`RenderTargetPhaseAA.cpp:685-851`. **Два экземпляра:**

```cpp
} Fsr3Wrapper , Fsr3WrapperScope;      // :851
```

- `Fsr3Wrapper` — основной, выход `rt_Generic_combine`
- `Fsr3WrapperScope` — для изображения через прицел,
  выход `rt_Generic_combine_scope`, всегда `FSR3_QUALITY_NATIVE_AA`

### Особенности реализации

- Используется **нативный DX11-бэкенд** FidelityFX, добавленный в форк
  `OGSR/FidelityFX-SDK` (ветка `release-FSR3-3.1.2-DX11-Native-API`).
- Шейдеры FSR3 поставляются как готовые DX11-блобы — в `gamedata` их нет.
- Общие ресурсы создаются вручную: `dilatedDepth`, `dilatedMotionVectors`,
  `reconstructedPrevNearestDepth` (`:757-760`) через `CreateFsrSharedTexture`
  (`:656-683`), умеющей выставлять `D3D11_BIND_UNORDERED_ACCESS` /
  `RENDERTARGET` / `DEPTHTARGET`.
- В `Draw()` (`:786-846`) **не передаются** `exposure`, `reactive`,
  `transparencyAndComposition` — остаются `nullptr` (`:802-805`).
  Это упрощает интеграцию, но может давать артефакты на прозрачных объектах
  (дым, трава, дождь).
- `frameTimeDelta` берётся из `Device.fTimeDeltaRealMS` (`:821`),
  `cameraFar` — из `g_pGamePersistent->Environment().CurrentEnv->far_plane` (`:828`).
- `reset` через `RequestHistoryReset()`; `CRenderTarget::ResetTemporalHistory()`
  (`:882-888`) дёргает все три обёртки (NGX + 2× FSR3).

## 6. Точки вызова

```cpp
// r4_rendertarget.h:354-366
void ConfigureTemporalRenderSize();
bool reset_3dss_rendertarget(bool need_reset);
void InitDLSS();  void DestroyDLSS();  bool ProcessDLSS();
void InitFSR();   void DestroyFSR();   bool ProcessFSR();
bool ProcessFSR_3DSS(bool need_reset);
void BeginTemporalUpscaleInput();
void EndTemporalUpscaleInput();
void ResetTemporalHistory();
bool PhaseAA(CBackend&);
bool Phase3DSSUpscale(CBackend&);
```

Создание — `r4_rendertarget.cpp:314-316`:

```cpp
reset_3dss_rendertarget(true);
InitDLSS();
InitFSR();
```

Диспетчер — `RenderTargetPhaseAA.cpp:932-970`:

```cpp
bool CRenderTarget::PhaseAA(CBackend& cmd_list)
{
    bool temporalOutput = false;
    switch (ps_r_pp_aa_mode)
    {
        case DLSS: temporalOutput = ProcessDLSS();
                    if (!temporalOutput) { ps_r_pp_aa_mode = FSR3;
                                           temporalOutput = ProcessFSR();
                                           if (!temporalOutput) ps_r_pp_aa_mode = TAA; }
                    break;
        case FSR3:  u_setrt(cmd_list, get_width(cmd_list), get_height(cmd_list), ...);
                    RImplementation.rmNormal(cmd_list);
                    temporalOutput = ProcessFSR();
                    if (!temporalOutput) ps_r_pp_aa_mode = TAA;
                    break;
        case TAA:   ProcessTAA(cmd_list);   break;
        case SMAA:  ProcessSMAA(cmd_list);  break;
    }
    EndTemporalUpscaleInput();
    RImplementation.rmNormal(cmd_list);
    BeginPostprocess(cmd_list, temporalOutput);
    if (ps_r_pp_aa_mode != SMAA) ProcessCAS(cmd_list);
    return temporalOutput;
}
```

## 7. Джиттер

Генерируется в кадре, `r4_R_render.cpp:309-324`:

```cpp
if (ps_r_pp_aa_mode == DLSS || ps_r_pp_aa_mode == FSR3 ||
    ps_r_pp_aa_mode == TAA  || ps_r2_ls_flags.test(R2FLAG_DBG_TAA_JITTER_ENABLE))
{
    const u32 renderWidth  = Target->GetRenderWidth();
    const u32 renderHeight = Target->GetRenderHeight();
    auto jitterPhaseCount = ffxFsr3UpscalerGetJitterPhaseCount(
                                static_cast<int32_t>(renderWidth),
                                static_cast<int32_t>(Target->GetDisplayWidth()));
    ffxFsr3UpscalerGetJitterOffset(&ps_r_taa_jitter_full.x, &ps_r_taa_jitter_full.y,
                                   Device.dwFrame, jitterPhaseCount);

    ps_r_taa_jitter.x =  2.0f * ps_r_taa_jitter_full.x / renderWidth;
    ps_r_taa_jitter.y = -2.0f * ps_r_taa_jitter_full.y / renderHeight;
    ps_r_taa_jitter.z = static_cast<float>(Device.dwFrame % jitterPhaseCount)
                      / static_cast<float>(jitterPhaseCount) + EPS;
}
```

Обратите внимание: джиттер считается **функциями FSR3** (`ffxFsr3Upscaler*`)
даже когда активен DLSS. Это работает (алгоритм Хамбертона общий),
но связывает DLSS-режим с присутствием FSR3.

Сброс истории по camera cut — `r4_R_render.cpp:301-307`:

```cpp
constexpr float temporalCameraCutDistance  = 5.f;
constexpr float temporalCameraCutDirectionDot = 0.25f;
if (Device.vCameraPosition.distance_to_sqr(Device.vCameraPositionSaved) > _sqr(temporalCameraCutDistance) ||
    Device.vCameraDirection.dotproduct(Device.vCameraDirectionSaved) < temporalCameraCutDirectionDot)
{
    Target->ResetTemporalHistory();
}
```

## 8. Переход render-res → display-res

`BeginPostprocess()` — `RenderTargetPhaseAA.cpp:586-607`.

Ключевая тонкость: `CopyResource` **не масштабирует**.

```cpp
u_setrt(cmd_list, GetDisplayWidth(), GetDisplayHeight(), nullptr, nullptr, nullptr, nullptr);
RImplementation.rmNormal(cmd_list);

if (temporalOutput)
    CopyResource(rt_Postprocess_0, rt_Generic_combine);      // апскейлер уже выдал display-res
else if (GetRenderWidth() == GetDisplayWidth() && GetRenderHeight() == GetDisplayHeight())
    CopyResource(rt_Postprocess_0, rt_Generic_0);            // рендер был в нативе
else
    RenderScreenTriangle(cmd_list, rt_Postprocess_0, s_temporal_resolve->E[0]);  // растянуть
```

Т.е. если временный апскейлер упал, движок всё равно показывает полноэкранную
картинку, растянув render-res через `temporal_resolve`.

## 9. Прицел 3DSS

`reset_3dss_rendertarget()` — `:504-534`: RT `rt_Generic_combine_scope`
формата `R16G16B16A16_FLOAT` c UAV, размер = `ceil(Device.dwWidth * scale)`,
где `scale` уменьшается от `ps_r_dlss_3dss_scale_factor` с шагом 0.1 до 1.0.

`Phase3DSSUpscale()` — `:974-988`: требует, чтобы режим AA был
`DLSS`, `FSR3` или `TAA` (иначе нет джиттера, а он необходим).

Используется только FSR3 (`Fsr3WrapperScope`) — DLSS для прицела не задействован.

## 10. Сброс истории

```cpp
void CRenderTarget::ResetTemporalHistory()      // :882-888
{
    m_resetTemporalHistory = true;
    NGXWrapper.RequestHistoryReset();
    Fsr3Wrapper.RequestHistoryReset();
    Fsr3WrapperScope.RequestHistoryReset();
}
```

## 11. Сводка по файлам

| Что | Где |
|---|---|
| Обёртка DLSS | `RenderTargetPhaseAA.cpp:157-443` |
| Обёртка FSR3 | `RenderTargetPhaseAA.cpp:685-929` |
| Выбор render-разрешения | `RenderTargetPhaseAA.cpp:451-501` |
| Диспетчер `PhaseAA` | `RenderTargetPhaseAA.cpp:932-970` |
| Прицел 3DSS | `RenderTargetPhaseAA.cpp:504-534`, `:974-988` |
| CAS | `RenderTargetPhaseAA.cpp:611-621` |
| SMAA | `RenderTargetPhaseAA.cpp:5-14` |
| TAA | `RenderTargetPhaseAA.cpp:16-30` |
| Переход разрешений | `RenderTargetPhaseAA.cpp:586-607` |
| Джиттер | `r4_R_render.cpp:309-324` |
| Консольные переменные | `xrRender_console.h:6-48`, `xrRender_console.cpp:36-78`, `:413-459`, `:892-896` |
| Создание обёрток | `r4_rendertarget.cpp:314-316` |
| Объявления | `r4_rendertarget.h:354-381` |