# 02 — Рендерер

> **Проверено против ветки `main_cop_cs`.** Все номера строк подтверждены.
> Главное исправление против SoC-версии этого документа: **в G-buffer ЕСТЬ
> нормали** — `rt_Position.xy` хранит закодированную нормаль в пространстве глаза.

## 0. Главное, что нужно понять сразу

**`ogsr_engine/xrCore/` не содержит рендерера.** Рендерер живёт в трёх местах:

| Путь | Исходников | Роль |
|---|---|---|
| `ogsr_engine/Layers/xrRender/` | 217 | Бэкенд-независимое ядро |
| `ogsr_engine/Layers/xrRenderDX10/` | 53 | Реализация на **DirectX 11** |
| `ogsr_engine/Layers/xrRenderPC_R4/` | 52 | `CRender` — композиция кадра, RT, фазы |
| `ogsr_engine/Include/xrRender/` | 24 | Публичные абстрактные интерфейсы |

Окно и создание устройства — в `ogsr_engine/xr_3da/`, **не** в `xrRender`.

## 1. Создание графического устройства

Класс устройства — **`CHW`**, глобальный экземпляр `HW`.

```
ogsr_engine/Layers/xrRender/HW.h:13          class CHW
ogsr_engine/Layers/xrRender/HW.h:83          extern ECORE_API CHW HW;
ogsr_engine/Layers/xrRender/HW.h:78          ID3D11DeviceContext* d3d_contexts_pool[R__NUM_CONTEXTS]
ogsr_engine/Layers/xrRenderDX10/dx10HW.cpp:25   CHW HW;
```

Ключевые функции:

| Что | Файл:строка |
|---|---|
| Выбор адаптера (`EnumAdapterByGpuPreference` `:76-77`, HIGH_PERFORMANCE) | `dx10HW.cpp:52-92` |
| **Создание устройства `D3D11CreateDevice`** | `dx10HW.cpp:148-274`, вызов `:178` |
| Запрос feature levels 11_1 / 11_0 | `dx10HW.cpp:173-176` |
| `D3D_DRIVER_TYPE_UNKNOWN` | `dx10HW.cpp:180` |
| Жёсткий assert `>= 11_0` | `dx10HW.cpp:190` |
| Создание 9 отложенных контекстов | `dx10HW.cpp:192-196` |
| Swapchain (`FLIP_DISCARD` `:113`, `ALLOW_TEARING` `:124`) | `dx10HW.cpp:103-146` |
| Уничтожение / сброс | `dx10HW.cpp:276-315` / `:320-338` |
| Полноэкранный / borderless / windowed | `dx10HW.cpp:606-619` / `:621-635` / `:637-648` |
| Дамп VRAM (`IDXGIAdapter3`) | `dx10HW.cpp:394-426` |
| Debug-слой по ключу `-dxdbg` | `dx10HW.cpp:164` |
| Tracy GPU-контекст | `dx10HW.cpp:209` |

Числа контекстов — `ogsr_engine/xr_3da/Engine.h:12-15`:
```cpp
#define R__NUM_SUN_CASCADES       (3u)
#define R__NUM_AUX_CONTEXTS       (6u)
#define R__NUM_PARALLEL_CONTEXTS  (R__NUM_SUN_CASCADES + R__NUM_AUX_CONTEXTS)  // 9
#define R__NUM_CONTEXTS           (R__NUM_PARALLEL_CONTEXTS + 1 /* imm */)     // 10
```

### Окно

| Что | Файл:строка |
|---|---|
| `CreateWindowEx` | `ogsr_engine/xr_3da/Device_Initialize.cpp:70-72` |
| **Класс окна `"_XRAY_1.5"`** | `Device_Initialize.cpp:53` |
| Заголовок `"OGSR Engine"` | `Device_Initialize.cpp:71` |
| `WndProc` | `Device_wndproc.cpp:42-53` |
| `class CRenderDevice` | `device.h:147` |
| `CApplication` | `x_ray.h:8`, реализация `x_ray.cpp:463-806+` |

`CApplication` — **не** графический класс, а синглтон приложения
(загрузочные экраны, управление уровнями).

## 2. Переключения API нет

**Нет ни DX12, ни D3D11_3.** Проверено поиском по всему `ogsr_engine/**`
и `Game/**` — **0 совпадений** для `D3D12` / `D3D11_3`.

Единственная песочница — `#define USE_DX11` в
`ogsr_engine/Layers/xrRenderPC_R4/stdafx.h:30` (рядом с `RENDER R_R4` на `:29`).
Это односторонний выбор: DX11 жёстко зашит.

Линкуются только `d3d11.lib; d3dcompiler.lib; dxgi.lib; dxguid.lib`
(`XR_3DA.vcxproj:94`, `:164`; плюс `xrEngine.lib`).
`d3d12.lib`, `dxcompiler.lib`, DXIL — **ни в одном** из 26 `.vcxproj`.

Разделение на `xrRender` / `xrRenderDX10` — это **разделение исходников**
(логика vs. реализация D3D), а не абстракция над API. Типы D3D11 протекают
в почти все заголовки (`r4.h:103-115`, `R_Backend.h:94-144`).

Остатки D3D9 живут только как **формат описания**: `D3DVERTEXELEMENT9`,
`SimulatorStates` (`tss_def.cpp:6,13,32,51`), которые транслирует
`xrRenderDX10/dx10StateUtils.cpp`.

## 3. Слои рендеринга

```
CApplication                       xr_3da/x_ray.cpp
  └─ CRenderDevice Device          xr_3da/device.{h,cpp}
       └─ IRenderDeviceRender* m_pRender
  └─ dxRenderDeviceRender          xrRender/dxRenderDeviceRender.cpp
         └─ CRender RImplementation   xrRenderPC_R4/r4.{h,cpp}
              ├─ R_dsgraph_structure contexts_pool[R__NUM_CONTEXTS]   r4.h:380
              ├─ dx10StateManager SSManager      xrRenderDX10/StateManager/
              ├─ CRenderTarget* Target           xrRenderPC_R4/r4_rendertarget.h
              ├─ render_main / render_sun / render_rain
              └─ CHOM, R_occlusion HWOCC
```

> ⚠️ `contexts_pool[10]` — это **не** `CBackend` и **не** в `R_Backend.h`.
> Массив D3D11-контекстов — `HW.h:78`; массив структур графа сцены — `r4.h:380`.

Абстрактная фабрика объектов — `Include/xrRender/RenderFactory.h`,
**14 пар** `Create*`/`Destroy*` (`:22-51`), реализация
`xrRender/dxRenderFactory.cpp`. Единственная настоящая точка абстракции,
и она отвечает только за время жизни объектов.

**RHI собственного нет.** D3D11 вызывается напрямую через набор тонких фасадов:

| Слой | Класс | Файл:строка |
|---|---|---|
| Устройство | `CHW` | `xrRender/HW.h:13` |
| Возможности | `CHWCaps` | `xrRender/HWCaps.h:5` |
| Командный список / состояния | **`CBackend`** | `xrRender/R_Backend.h:74` (`MaxCBuffers = 14` на `:79`) |
| Массивы констант | `ref_cbuffer m_a*Constants[MaxCBuffers]` | `R_Backend.h:87-92` |
| Кэш состояний | `dx10StateManager`, `dx10State` | `xrRenderDX10/StateManager/*` |
| Константы | `dx10ConstantBuffer` | `xrRenderDX10/dx10ConstantBuffer.{h,cpp}` |
| Ресурсы | `CResourceManager` | `xrRender/ResourceManager.h:15` |
| Render target | `CRT` | `xrRender/SH_RT.h:4`, `dx10SH_RT.cpp:15` |
| Текстура | `CTexture` | `xrRender/SH_Texture.h:9` |
| **GPU-события** | `PIX_EVENT`, `PIX_FRAME_START` | **`xrRender/Debug/dxPixEventWrapper.h:31, 32, 34-35`** (не `dxPixEvents.h` — там только статистика) |
| Профилирование | `TracyD3D11Ctx` | `xrCore/xrCore.h:87`, `xrRender/HW.h:49` |
| Захват кадра | `g_renderdoc_api` | `xrRender/dxRenderDeviceRender.cpp:6-11, 108-145` |
| ReShade | `render_reshade_effects()` | **`xr_3da/ReShadeCompat.cpp:31-35`** |

## 4. Кадр

Вход: `CRenderDevice::Run()` (`device.cpp:417`) → `Begin()` (`:32`) → … → `End()` (`:64`).

`CRender::Render()` — `xrRenderPC_R4/r4_R_render.cpp:266-536`.
Декларативного списка стадий нет, конвейер захардкожен:

| # | Стадия | Строка |
|---|---|---|
| — | Переход на render-разрешение, early-out, детект camera cut | 279-307 |
| — | **TAA-джиттер** через `ffxFsr3UpscalerGetJitterOffset` | 309-324 |
| 1 | `SKY_RENDER` — MRT-биндинг + небо | 326-342 |
| 2 | `phase_scene_prepare()` | 354 |
| 3 | `DEFER_PART0_SPLIT` | 356-370 |
| 4 | `DEFER_TEST_LIGHT_VIS` | 377-390 |
| 5 | `DEFER_PART1_SPLIT` | 393-421 |
| 6 | `copy_zbuffer` | 423-430 |
| 7 | `DEFER_WALLMARKS` | 433-439 |
| 8 | `DEFER_RAIN` | 441-445 |
| 9 | `DEFER_SUN` | 448-456 |
| 10 | `DEFER_SELF_ILLUM` | 458-485 |
| 11 | `DEFER_FLUSH_OCCLUSION` | 488-508 |
| 12 | `DEFER_LIGHT` | 511-521 |
| 13 | **`DEFER_COMBINE`** → `phase_combine()` | 524-527 |

### Пост-обработка

`CRenderTarget::phase_combine()` — `r4_rendertarget_phase_combine.cpp:8-378`.

Фактический порядок (~30 блоков):
```
:13-18    LUM-пул (автоэкспозиция)          :177-181  combine_tonemap
:24-28    облака                            :187-188  heatvision
:33-93    combine_1 (тонмаппинг)           :190      Phase3DSSUpscale()
:96       copy в rt_Generic_0_temp         :193-202  PhaseAA()  ← DLSS/FSR3/TAA/SMAA
:98-126   SSR                               :205-208  fallback-копия 3DSS
:129-144  forward + HUD UI                 :211-212  heatvision_overlay  ← новое
:148-149  volumetric                       :214-219  SSSS (солнечные лучи)
:152-169  distortion                       :221-235  reticle 3DSS
:171-175  bloom                            :238      blur
                                             :241-242  ssfx_bloom           ← новое
                                             :244-245  DoF
                                             :247-248  LUT / ACES
                                             :257-262  gasmask DoV
                                             :264-265  nightvision
                                             :268-269  капли дождя
                                             :271-297  combine_2 (motion blur)
                                             :299-303  flares
                                             :306-309  phase_pp
                                             :311-322  сброс LUM
                                             :324      phase_flares
                                             :326-327  биндинг backbuffer
                                             :329-377  DEBUG-блоки
```

**Отдельно на CoP:** `Environment::USED_COP_WEATHER`
(`xr_3da/environment.h:314`, используется на `:44`) — ветка погоды CoP,
`hemi_color_unmodded` вместо `sky_color`.

## 5. Render targets и G-buffer

`CRenderTarget` объявлен в `r4_rendertarget.h` (387 строк), создаётся в
`r4_rendertarget.cpp:185-547` (создание RT заканчивается на `:311`,
далее блумы/шейдеры/SMAA/SSSS/треугольник).

Двухразмерная модель:
```cpp
// r4_rendertarget.h:14-17
u32 m_renderWidth,  m_renderHeight;    // физический размер (для DLSS/FSR3)
u32 m_displayWidth, m_displayHeight;   // размер окна
```

**Всего ~64 RT** (60 членов `ref_rt` + `xr_vector<ref_rt> rt_base`,
с раскрытием массивов — 64).

Ключевые буферы:

| RT | Формат | Роль | Строка |
|---|---|---|---|
| `rt_base` | `R8G8B8A8_UNORM`, `CreateBase` | алиас swapchain | `r4_rendertarget.cpp:222` |
| `rt_Base_Depth` | `R24G8_TYPELESS` | MRT-глубина | `:224` |
| `rt_zbuffer` | **`R24G8_TYPELESS`** | рабочая глубина | `:282` |
| `rt_Position` | `R16G16B16A16_FLOAT` | **нормаль.xy + глубина + mat-id** | `:226` |
| `rt_Color` | `R8G8B8A8_UNORM` | albedo + **gloss** | `:230` |
| `rt_Velocity` | `R16G16_FLOAT` | motion vectors | `:281` |
| `rt_heat` | `R8G8B8A8_UNORM` | маска heatvision | `:273` |
| `rt_Generic_0` | `R16G16B16A16_FLOAT` | цвет сцены / вход апскейлера | `:233` |
| `rt_Generic_combine` | `R16G16B16A16_FLOAT` + **UAV** | **выход апскейлера**, display-res | `:292` |
| `rt_Generic_scene_scratch` | `R16G16B16A16_FLOAT` + **UAV** | UAV-пингпонг, render-res | `:291` |
| `rt_Postprocess_0` | `R16G16B16A16_FLOAT` | цвет после апскейла | `:293` |
| `rt_smap_sun_cascade[3]` | `R24G8_TYPELESS` | 3 каскада CSM | `:340-345` |

### G-buffer отложенного рендеринга

`r4_rendertarget_phase_scene.cpp:32`:
```cpp
u_setrt(cmd_list, rt_Position, rt_Color, rt_Velocity, rt_heat, rt_Base_Depth->pZRT[cmd_list.context_id]);
```

Содержимое слотов — из `Game/Resources_CoP_1.6.02/gamedata/shaders/r3/common_iostructs.h:287-293`:

```hlsl
struct f_deffer
{
    float4 position : SV_Target0; // xy=encoded normal, z = pz, w = encoded(m-id, hemi)
    float4 C : SV_Target1;        // r, g, b,  gloss
    float2 Velocity : SV_Target2; // XY - motion vectors
    float4 H : SV_Target3;
};
```

| Слот | RT | Что хранит |
|---|---|---|
| 0 | `rt_Position` | **`.xy` — закодированная нормаль в пространстве глаза**, `.z` — view-space Z, `.w` — закодированные (material-id, hemi) |
| 1 | `rt_Color` | albedo в `.rgb`, **gloss** в `.a` |
| 2 | `rt_Velocity` | motion vectors |
| 3 | `rt_heat` | маска heatvision |

> ⚠️ **Важное исправление.** В SoC-версии этого документа утверждалось, что
> нормалей в G-buffer нет. **Это неверно**: нормали есть, в `rt_Position.xy`,
> закодированы. Для RTX это существенно — декодировать надо, а не городить
> новый MRT. Что действительно отсутствует: **roughness, metallic, emissive
> в 4 MRT** (emissive рендерится отдельно в `DEFER_SELF_ILLUM`).

Глубина — `D24_UNORM_S8_UINT` (маппинг `R24G8_TYPELESS → D24_UNORM_S8_UINT`
в `xrRenderDX10/dx10SH_RT.cpp:153-155`). Для DXR неоптимальна.

## 6. Шейдеры

### Формат и расположение

**Обычный текст HLSL, компилируется в рантайме через `D3DCompile`.**
Предкомпилированных `.bin`/`.cso` нет.

```
Game/Resources_CoP_1.6.02/gamedata/shaders/
├── r3/      — 552 файла
└── common/  — models_detector_cfg.h, smaa.h
```

Состав: `.ps` **206**, `.vs` **120**, `.gs` **3** (`fluid_array*.gs`),
`.hs`/`.ds` **1/1** (`r3/dx11/tess.*`), **`.cs` **0**, `.s` **125**, `.h` **93**.
Итого **424 файла**.

Путь: `CRender::getShaderPath()` возвращает `"r3\\"`
(`xrRenderPC_R4/r4.h:258`), `$game_shaders$` — VFS-алиас
(`xrCore/LocatorAPI_defs.h:15`, резолв `LocatorAPI.cpp:660-720`).

### Загрузка и компиляция

| Стадия | Файл:строка | Цель |
|---|---|---|
| Vertex | `xrRenderDX10/dx10ResourceManager_Resources.cpp:110-177` | `vs_5_0`, entry `main`; откат `vs_4_1`/`main_vs_4_1` (`:156-160`) |
| Pixel | `dx10ResourceManager_Resources.cpp:206-268` | `ps_5_0`/`main`, откат `ps_4_1` |
| GS/HS/DS/**CS** | `xrRender/ShaderResourceTraits.h:109-166` | `gs_5_0` (`:74`), `hs_5_0` (`:16`), `ds_5_0` (`:35`), **`cs_5_0` (`:54`)** |

### Точка входа шейдерной системы

**`CRender::shader_compile()` — `r4.cpp:674-906`.**

1. Собирает `xr_vector<D3D_SHADER_MACRO>` из `m_ShaderOptions` (`r4.h:356`).
2. Добавляет **35 дефайнов** (`:680-821`): `SMAP_size`, `USE_HWSMAP`,
   `USE_GTAO`, `SKIN_*`, `ALLOW_STEEPPARALLAX`, `GBUFFER_OPTIMIZATION`,
   `SM_5` (`:775`), `SSLR_ENABLED`, `SSFX_*`, **`USE_ACES`** (`:789`),
   **`USE_CS_COP_LMAP`** (`:817-818`, через `CApplication::CheckCsCopMode()`).
3. Дисковый кэш (`:823-869`) — `$app_data_root$/shaders_cache/`, CRC32,
   `D3DPreprocess` при `R2FLAGEXT_SHADER_CACHE`.
4. `D3DCompile(...)` на **`:873`**, запись кэша `:876-888`.
5. `create_shader()` (`:512-638`) → `D3DReflect` (`:537` PS / `:574` VS)
   → `R_constant_table::parse()` (`:544` / `:593`).
6. Дизассемблер в `<name>.txt` (`:624-635`).

**Потолок шейдерной модели — SM 5.0.** Ни SM6, ни mesh shaders, ни bindless
(0 совпадений по `vs_6_*`, `cs_6_*`, `SV_DispatchThreadID`, `RAYGEN`,
`mesh_shader`, `bindless`).

### PSO / state caching

`CResourceManager` (`xrRender/ResourceManager.h:15`). Детерминирующие пулы:
`m_textures` (`:40`), `m_rtargets` (`:42`), `m_vs`–`m_cs` (`:44-49`),
`v_states` (`:51`), `v_declarations` (`:52`), `v_geoms` (`:53`),
`v_constant_tables` (`:54`), `v_constant_buffer[R__NUM_CONTEXTS]` (`:56`).
Лимиты `SHADER_PASSES_MAX 2`, `SHADER_ELEMENTS_MAX 16` (`Shader.h:22-23`).

### ACES

```
gamedata/shaders/r3/ACES.H                    12 КБ  sRGB_to_ACES, ACES_to_sRGB, ACEScg_*
gamedata/shaders/r3/ACES_Color_Grading.h      2 КБ   ← новое на CoP
gamedata/shaders/r3/ACES_LMT.h                1 КБ   ← новое на CoP
gamedata/shaders/r3/ACES_settings.h          1 КБ   ← новое на CoP
gamedata/shaders/r3/ACES_LMTs/                LMT_Technicolor, LMT_Contrast_Reduction,
                                                LMT_Blue_Fix, LMT_Bleach_Bypass
```

Флаг `R2FLAGEXT_USE_ACES = 1ull << 56` (`xrRender_console.h:254`),
команда `ssfx_use_aces` (`xrRender_console.cpp:1003`), дефайн `USE_ACES` (`r4.cpp:789`).
AgX отсутствует.

## 7. Файлы, которые придётся трогать

1. `ogsr_engine/Layers/xrRender/RenderTargetPhaseAA.cpp` — **главный** файл по апскейлу
2. `ogsr_engine/Layers/xrRender/xrRender_console.{h,cpp}` — консольные переменные
3. `ogsr_engine/Layers/xrRenderPC_R4/r4_R_render.cpp:301-324` — джиттер и сброс истории
4. `ogsr_engine/Layers/xrRenderPC_R4/r4_rendertarget.{h,cpp}` — RT
5. `ogsr_engine/Layers/xrRenderPC_R4/r4_rendertarget_phase_combine.cpp` — порядок пост-обработки
6. `ogsr_engine/Layers/xrRenderPC_R4/r4.cpp:674-906` — компилятор шейдеров
7. `ogsr_engine/Layers/xrRenderDX10/dx10ResourceManager_Resources.cpp` — загрузка VS/PS
8. `ogsr_engine/Layers/xrRender/ShaderResourceTraits.h:109-166` — загрузка GS/HS/DS/CS
9. `ogsr_engine/Layers/xrRenderDX10/dx10HW.cpp` — устройство, swapchain
10. `ogsr_engine/Layers/xrRender/R_Backend.h` + `R_Backend*.cpp` — `CBackend`
11. `ogsr_engine/Layers/xrRenderDX10/StateManager/*` — кэш состояний
12. `ogsr_engine/Layers/xrRenderPC_R4/stdafx.h:29-30` — макросы `RENDER` и `USE_DX11`

## 8. Мёртвый код (важно)

В `xrRender/` лежат файлы, **исключённые из `xrRender_R4.vcxproj`**:
`ResourceManager_Resources.cpp`, `ResourceManager_Scripting.cpp`,
`SH_RT.cpp`, `SH_Texture.cpp`, `tga.cpp`, `dxParticleCustom.cpp`,
а также `xrRenderPC_R4/du_sphere_part.cpp`.

Реально компилируемые аналоги — `xrRenderDX10/dx10ResourceManager_*.cpp`,
`dx10SH_RT.cpp`, `dx10SH_Texture.cpp`.

> Правя загрузку шейдеров или RT, редактируйте `dx10*`-файлы,
> а не `xrRender/*`-аналоги.