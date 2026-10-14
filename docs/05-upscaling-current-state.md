# 05 — Апскейлинг: текущее состояние

> **Проверено против ветки `main_cop_cs`.** Файл `RenderTargetPhaseAA.cpp`
> на CoP **идентичен SoC** — все номера строк совпадают. Исправлены только
> ссылки в `r4_rendertarget.h` и пара мелочей.

Документ описывает **как именно** устроены DLSS и FSR 3 в этом форке —
чтобы изменения вносились в правильное место.

## 1. Главный файл

**`ogsr_engine/Layers/xrRender/RenderTargetPhaseAA.cpp`** (990 строк).
Здесь всё: SMAA, TAA, DLSS, FSR 3 (основной + для прицела), CAS,
`PhaseAA()`, `Phase3DSSUpscale()`, выбор render-разрешения.

## 2. Консольные переменные

Объявления — `ogsr_engine/Layers/xrRender/xrRender_console.h`:

```cpp
:6    ps_r_pp_aa_mode
:7    ps_r_dlss_preset
:8    ps_r_dlss_quality
:12   ps_r_dlss_dll_version[64]
:13   ps_r_fsr3_quality
:14   ps_r_dlss_3dss_scale_factor
:16-23   enum DLSS_QUALITY_{DLAA,QUALITY,BALANCED,PERFORMANCE,ULTRA_PERFORMANCE}
:25-32   enum FSR3_QUALITY_{NATIVE_AA,QUALITY,BALANCED,PERFORMANCE,ULTRA_PERFORMANCE}
:264-272  enum { NO_AA, DLSS, FSR3, TAA, SMAA }
```

Значения по умолчанию (`xrRender_console.cpp`):

```cpp
:36  ps_r_pp_aa_mode = DLSS;              // DLSS включён по умолчанию
:58  ps_r_dlss_quality = DLSS_QUALITY_DLAA;     // но без апскейла — только AA
:68  ps_r_fsr3_quality = FSR3_QUALITY_NATIVE_AA;
:78  ps_r_dlss_3dss_scale_factor{1.0f};
```

Команды — `xrRender_console.cpp:892-896`:

| Команда | Строка | Класс |
|---|---|---|
| `r_aa_mode` | `:892` | `CCC_Token` |
| `r_aa_dlss_quality` | `:893` | `CCC_DlssQuality` (`:427`) |
| `r_aa_fsr3_quality` | `:894` | `CCC_Token` |
| `r_aa_dlss_preset` | `:895` | `CCC_DlssRenderPreset` (`:439`, валидирует) |
| `r_aa_dlss_version` | `:896` | `CCC_DlssDllVersion` (`:413`, read-only) |
| `r_3dss_scale_factor` | **`:898`** | `CCC_Float`, диапазон **[1.0 … 2.5]** |

**Важные вспомогательные функции** — `RenderTargetPhaseAA.cpp:441`, `:443`:
```cpp
bool R_dlss_is_preset_available(const u32 preset);
void R_dlss_refresh_available_presets();
```
Объявлены в `xrRender_console.h:10-11`. Именно они реализуют валидацию
пресетов в `CCC_DlssRenderPreset` (его `TokenEnabled()` — `:444-448`,
`Execute()` — `:450-459`).

> Пользователь вводит токены вида `st_opt_dlss` — таблица `xr_token`
> (`xrRender_console.cpp:37-76`) содержит разработческие имена вроде
> `st_opt_dlss`, `st_opt_dlss_quality`, `st_opt_fsr3_quality`.
> Полный список — в файле, строки `:37-45`, `:48-56`, `:59-66`, `:69-76`.

## 3. Выбор render-разрешения

`CRenderTarget::ConfigureTemporalRenderSize()` — `RenderTargetPhaseAA.cpp:451-501`.

```
:453  SetTemporalRenderSize(Device.dwWidth, Device.dwHeight, ...)   натив
:455-481  ветка DLSS
:458       NGXWrapper.Initialize(20082024151405ull)
:461-471   откат в FSR3 если инициализация не удалась
:464       RefreshAvailablePresets()
:466       QueryOptimalSettings(...)
:474       при неудаче — ps_r_dlss_quality = DLSS_QUALITY_DLAA
:483-498  ветка FSR3
:483-498  ffxFsr3UpscalerGetRenderResolutionFromQualityMode(...)
```

Каскад отказоустойчивости: **DLSS → FSR3 → TAA → SMAA**.
Виден и в `ConfigureTemporalRenderSize()`, и в `PhaseAA()` (`:938-948`).

> ⚠️ Смена качества **физически меняет размеры RT**, поэтому требует
> `vid_restart`. Движок об этом предупреждает:
> `--[DLSS] quality changes resize physical render targets; apply video settings or run vid_restart`
> (`RenderTargetPhaseAA.cpp:566`).

## 4. Обёртка DLSS — `NGXWrapper`

`RenderTargetPhaseAA.cpp:157-439`, единственный экземпляр `} NGXWrapper;` на `:439`.

### Определение версии загруженной DLL

```cpp
:53   char ps_r_dlss_dll_version[64]{};
:55-62  struct DlssDllFileVersion { u32 major, minor, patch, revision; bool valid; };
:64   static DlssDllFileVersion g_dlss_dll_file_version{};
:66-75  static bool DlssDllVersionAtLeast(u32, u32, u32);
:77-121 static void QueryLoadedDlssDllVersion();
```

`QueryLoadedDlssDllVersion()` читает ресурс версии `nvngx_dlss.dll`:
`GetModuleHandleW` (`:82`) → `GetModuleFileNameW` (`:87`) →
`GetFileVersionInfoSizeW` (`:91`) → `GetFileVersionInfoW` (`:96`) →
`VerQueryValueW` (`:101`). Результат — в `ps_r_dlss_dll_version`.

Используется для доступа к **Preset L/M**, добавленным в SDK 310.5.0:

```cpp
// :244-252
u32 mask = 1u << NVSDK_NGX_DLSS_Hint_Render_Preset_Default;   // 0
mask |= 1u << ..._F;    // 6
mask |= 1u << ..._J;    // 10
mask |= 1u << ..._K;    // 11
if (DlssDllVersionAtLeast(310, 5, 0)) {
    mask |= 1u << ..._L;   // 12
    mask |= 1u << ..._M;   // 13
}
```

Значения enum'ов пресетов (`nvsdk_ngx_defs.h:72-87`):
`Default 0`, `E 5`, `F 6`, `G 7`, `H_Reserved 8`, `I_Reserved 9`,
`J 10`, `K 11`, `L 12`, `M 13`, `N 14`, `O 15`.

### AppID

Жёстко зашит: `20082024151405ull` — `:235`, `:458`, `:542`.
Нужен `NVSDK_NGX_D3D11_Init_with_ProjectID`, чтобы завести свой.

### Создание

`Create()` — `:286-365`:

```cpp
NVSDK_NGX_Parameter_SetUI(NgxParameters, preset_name, preset);
result = NgxParameters->Get(preset_name, &dlssPreset);

int32_t flags{};
flags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;    // :323  — ВКЛЮЧЁН
flags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;       // :327  — ВКЛЮЧЁН
// flags |= ..._DoSharpening;                      // :332  — ЗАКОММЕНТИРОВАН
// flags |= ..._MVJittered;                        // :337  — ЗАКОММЕНТИРОВАН
// flags |= ..._AlphaUpscaling;                    // :341  — ЗАКОММЕНТИРОВАН

dlssCreateParams.Feature.InWidth        = renderSize.Width;
dlssCreateParams.Feature.InHeight       = renderSize.Height;
dlssCreateParams.Feature.InTargetWidth  = displaySize.Width;
dlssCreateParams.Feature.InTargetHeight = displaySize.Height;
dlssCreateParams.Feature.InPerfQualityValue = quality;
dlssCreateParams.InFeatureCreateFlags   = flags;
NGX_D3D11_CREATE_DLSS_EXT(HW.get_context(CHW::IMM_CTX_ID), &Handle,
                          NgxParameters, &dlssCreateParams);   // :353
```

### Оценка

`Draw()` — `:395-434`:

```cpp
dlssEvalParams.Feature.pInColor        = rt_Generic_0->pSurface;   // :406
dlssEvalParams.Feature.pInOutput       = OutputRT;                 // :409  rt_Generic_combine
dlssEvalParams.pInDepth                = rt_zbuffer->pSurface;     // :412  D24_UNORM_S8_UINT
dlssEvalParams.pInMotionVectors        = rt_Velocity->pSurface;    // :414

dlssEvalParams.InRenderSubrectDimensions = saved_renderSize;       // :416
dlssEvalParams.InJitterOffsetX = ps_r_taa_jitter_full.x;           // :418
dlssEvalParams.InJitterOffsetY = ps_r_taa_jitter_full.y;           // :419
dlssEvalParams.InMVScaleX = -saved_renderSize.Width  * 0.5f;       // :421
dlssEvalParams.InMVScaleY =  saved_renderSize.Height * 0.5f;       // :422
dlssEvalParams.InReset     = resetHistory ? 1 : 0;                // :423

NGX_D3D11_EVALUATE_DLSS_EXT(HW.get_context(CHW::IMM_CTX_ID), Handle,
                            NgxParameters, &dlssEvalParams);      // :425
```

Форматы входов: цвет `R16G16B16A16_FLOAT`, глубина `D24_UNORM_S8_UINT`,
MV `R16G16_FLOAT`.

## 5. Обёртка FSR 3 — `Fsr3Wrapper`

`RenderTargetPhaseAA.cpp:685-851` (класс), функции `:853-929`. **Два экземпляра:**

```cpp
} Fsr3Wrapper , Fsr3WrapperScope;      // :851
```

- `Fsr3Wrapper` — основной, выход `rt_Generic_combine`
- `Fsr3WrapperScope` — для изображения через прицел, выход
  `rt_Generic_combine_scope`, всегда `FSR3_QUALITY_NATIVE_AA` (`:871-872`)

### Особенности

- Используется **нативный DX11-бэкенд** FidelityFX из форка
  `OGSR/FidelityFX-SDK` (`git branch` = `release-FSR3-3.1.2-DX11-Native-API`)
- Версия: `ffx_fsr3.h:36,41,46` → **FSR 3.1.2**
- Шейдеры поставляются как готовые DX11-блобы в `ffx_dx11.cpp` — в `gamedata` нет
- Общие ресурсы: `dilatedDepth`, `dilatedMotionVectors`,
  `reconstructedPrevNearestDepth` (`:757-760`) через
  `CreateFsrSharedTexture` (`:656-683`, флаги UAV/RT/DEPTHTARGET `:672-677`)
- В `Draw()` (`:786-846`) **не передаются** `exposure`, `reactive`,
  `transparencyAndComposition` — остаются `nullptr` (`:802-805`).
  На прозрачных объектах (дым, трава, дождь) возможны артефакты.
- `frameTimeDelta` из `Device.fTimeDeltaRealMS` (`:821`),
  `cameraFar` из `g_pGamePersistent->Environment().CurrentEnv->far_plane` (`:828`)
- `ResetTemporalHistory()` (`:882-888`) дёргает все три обёртки

## 6. Точки вызова

```cpp
// r4_rendertarget.h
:316          ConfigureTemporalRenderSize()
:353          bool PhaseAA(CBackend&)
:354          bool Phase3DSSUpscale(CBackend&)
:356          ProcessSMAA / :357 processTAA
:359-361      InitDLSS / ProcessDLSS / DestroyDLSS
:363-366      InitFSR / ProcessFSR / ProcessFSR_3DSS / DestroyFSR
:368          reset_3dss_rendertarget
:370          ProcessCAS / :371 BeginPostprocess
:373-381      phase_sss / rt_sunshafts / s_ssss_*   (не относится к AA)
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
        case FSR3: u_setrt(cmd_list, get_width(cmd_list), get_height(cmd_list), ...);
                   RImplementation.rmNormal(cmd_list);
                   temporalOutput = ProcessFSR();
                   if (!temporalOutput) ps_r_pp_aa_mode = TAA;
                   break;
        case TAA:  ProcessTAA(cmd_list);   break;
        case SMAA: ProcessSMAA(cmd_list);  break;
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

> Джиттер считается **функциями FSR3** даже когда активен DLSS.
> Алгоритм общий, но связывает DLSS-режим с присутствием FSR3.

Сброс истории по camera cut — `r4_R_render.cpp:301-307` (порог 5.f и dot 0.25).

## 8. Переход render-res → display-res

`BeginPostprocess()` — `RenderTargetPhaseAA.cpp:586-607`.

Ключевая тонкость: `CopyResource` **не масштабирует**.

```cpp
u_setrt(cmd_list, GetDisplayWidth(), GetDisplayHeight(), nullptr, nullptr, nullptr, nullptr);
RImplementation.rmNormal(cmd_list);

if (temporalOutput)
    CopyResource(rt_Postprocess_0, rt_Generic_combine);      // апскейлер выдал display-res
else if (GetRenderWidth() == GetDisplayWidth() && GetRenderHeight() == GetDisplayHeight())
    CopyResource(rt_Postprocess_0, rt_Generic_0);            // рендер был в нативе
else
    RenderScreenTriangle(cmd_list, rt_Postprocess_0, s_temporal_resolve->E[0]);  // растянуть
```

## 9. Прицел 3DSS

`reset_3dss_rendertarget()` — `:504-534`: RT `rt_Generic_combine_scope`
формата `R16G16B16A16_FLOAT` c UAV, размер `ceil(Device.dwWidth * scale)`,
где `scale` уменьшается от `ps_r_dlss_3dss_scale_factor` с шагом 0.1 до 1.0.

`Phase3DSSUpscale()` — `:974-988`: требует режим `DLSS`, `FSR3` или `TAA`
(иначе нет джиттера).

Используется только FSR3 (`ProcessFSR_3DSS`, `:913-929`) — DLSS для прицела
не задействован.

## 10. Сводка по файлам

| Что | Где |
|---|---|
| Обёртка DLSS | `RenderTargetPhaseAA.cpp:157-439` (+ функции `:441`, `:443`) |
| Обёртка FSR3 | `:685-851` (класс) + `:853-929` (функции) |
| Выбор render-разрешения | `:451-501` |
| Диспетчер `PhaseAA` | `:932-970` |
| Прицел 3DSS | `:504-534`, `:974-988` |
| CAS | `:611-621` |
| SMAA | `:5-14` |
| TAA | `:16-30` |
| Переход разрешений | `:586-607` |
| Джиттер | `r4_R_render.cpp:309-324` |
| Консольные переменные | `xrRender_console.h:6-48`, `.cpp:36-78`, `:413-459`, `:892-898` |
| Создание обёрток | `r4_rendertarget.cpp:314-316` |
| Объявления | `r4_rendertarget.h:316` + `:353-371` |