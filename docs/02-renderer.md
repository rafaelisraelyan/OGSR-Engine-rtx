# 02 — Рендерер

## 0. Главное, что нужно понять сразу

**`ogsr_engine/xrCore/` не содержит рендерера.** Это библиотека низкого уровня
(VFS, математика, память, XML, ini, таймеры, сжатие). Рендерер живёт в трёх других местах:

| Путь | Файлов | Роль |
|---|---|---|
| `ogsr_engine/Layers/xrRender/` | 217 | Бэкенд-независимое ядро рендерера |
| `ogsr_engine/Layers/xrRenderDX10/` | 53 | Реализация на **DirectX 11** (название «DX10» — наследие) |
| `ogsr_engine/Layers/xrRenderPC_R4/` | 52 | Собственно `CRender` — композиция кадра, render targets, фазы |
| `ogsr_engine/Include/xrRender/` | 24 | Публичные абстрактные интерфейсы |

Окно и создание устройства — в `ogsr_engine/xr_3da/`, **не** в `xrRender`.

## 1. Создание графического устройства

Класс устройства — **`CHW`**, глобальный экземпляр `HW`.

```
ogsr_engine/Layers/xrRender/HW.h:13          class CHW
ogsr_engine/Layers/xrRender/HW.h:83          extern ECORE_API CHW HW;
ogsr_engine/Layers/xrRenderDX10/dx10HW.cpp:25   CHW HW;
```

Ключевые функции:

| Что | Файл:строка |
|---|---|
| Выбор адаптера (`EnumAdapterByGpuPreference`, HIGH_PERFORMANCE) | `dx10HW.cpp:52-92` |
| **Создание устройства `D3D11CreateDevice`** | `dx10HW.cpp:148-274`, вызов на `:178` |
| Swapchain (`FLIP_DISCARD`, `ALLOW_TEARING`, waitable) | `dx10HW.cpp:103-146` |
| Уничтожение / сброс | `dx10HW.cpp:276-315` / `:320-338` |
| Полноэкранный / borderless | `dx10HW.cpp:606-648` |
| Дамп использования VRAM (`IDXGIAdapter3`) | `dx10HW.cpp:394-426` |

Особенности:

- Запрашиваются feature levels **`11_1`, затем `11_0`**; жёсткий assert `>= 11_0` (`dx10HW.cpp:190`).
- `D3D_DRIVER_TYPE_UNKNOWN` на явно выбранном адаптере (обязательно при выборе адаптера).
- Создаётся **10 D3D11-контекстов**: 1 immediate + 9 отложенных (`:192-196`).
  Числа заданы в `ogsr_engine/xr_3da/Engine.h:12-15`:
  ```cpp
  #define R__NUM_SUN_CASCADES       (3u)
  #define R__NUM_AUX_CONTEXTS       (6u)
  #define R__NUM_PARALLEL_CONTEXTS  (R__NUM_SUN_CASCADES + R__NUM_AUX_CONTEXTS)  // 9
  #define R__NUM_CONTEXTS           (R__NUM_PARALLEL_CONTEXTS + 1 /* imm */)     // 10
  ```
- Depth сенсор задаётся через `dxgi1_6.h` (`:20`), хотя `HW.h` включает `dxgi1_4.h`.
- Debug-слой — только по ключу `-dxdbg` в командной строке (`:164`).
- Tracy GPU-контекст: `profiler_ctx = TracyD3D11Context(...)` (`:209`).

### Окно

```
ogsr_engine/xr_3da/Device_Initialize.cpp:70-72   CreateWindowEx, класс "_XRAY_1.5", заголовок "OGSR Engine"
ogsr_engine/xr_3da/Device_wndproc.cpp:42-53      WndProc
ogsr_engine/xr_3da/device.h:147                  class CRenderDevice
```

`CApplication` — **не** графический класс. Это синглтон приложения
(`ogsr_engine/xr_3da/x_ray.h:8`, реализация `x_ray.cpp:458-800`): загрузочные
экраны, управление уровнями.

## 2. Переключения API нет

**Нет ни DX12, ни D3D11_3, ни макроса выбора бэкенда.** Проверено:

- ноль вхождений `D3D12` во всём движке;
- нет `GRAPHICS_API`, `#if USE_DX12`;
- линкуются только `d3d11.lib; d3dcompiler.lib; dxgi.lib; dxguid.lib`
  (`ogsr_engine/xr_3da/XR_3DA.vcxproj:94`, `:164`).

Разделение на `xrRender` / `xrRenderDX10` — это **разделение исходников**
(логика vs. реализация D3D), а не абстракция над API. Типы D3D11 протекают
в почти все заголовки рендерера (`r4.h:103-115`, `R_Backend.h:94-144`, …).

Остатки D3D9 живут только как **формат описания** fixed-function состояний:
`D3DVERTEXELEMENT9`, `SimulatorStates` (`Layers/xrRender/tss_def.cpp`), которые
`xrRenderDX10/dx10StateUtils.cpp` транслирует в объекты DX11.

## 3. Слои рендеринга

```
CApplication                       xr_3da/x_ray.cpp
  └─ CRenderDevice Device          xr_3da/device.{h,cpp}      кадр, окно, pump сообщений
       └─ IRenderDeviceRender* m_pRender
  └─ dxRenderDeviceRender          xrRender/dxRenderDeviceRender.cpp
         └─ CRender RImplementation   xrRenderPC_R4/r4.{h,cpp}   ← «IRender_interface»
              ├─ CBackend contexts_pool[10]      xrRender/R_Backend.h:74
              ├─ dx10StateManager SSManager      xrRenderDX10/StateManager/
              ├─ CRenderTarget* Target           xrRenderPC_R4/r4_rendertarget.h
              ├─ render_main / render_sun / render_rain
              └─ CHOM, R_occlusion HWOCC
```

Абстрактная фабрика объектов — `ogsr_engine/Include/xrRender/RenderFactory.h`
(17 пар `Create*`/`Destroy*`), реализация `xrRender/dxRenderFactory.cpp`.
Это единственная настоящая точка абстракции, и она отвечает только за время жизни объектов.

**RHI собственного нет.** D3D11 вызывается напрямую через набор тонких фасадов:

| Слой | Класс | Файл:строка |
|---|---|---|
| Устройство | `CHW` | `xrRender/HW.h:13` |
| Возможности | `CHWCaps` | `xrRender/HWCaps.h:5` |
| Командный список / состояния | **`CBackend`** | `xrRender/R_Backend.h:74` |
| Кэш состояний | `dx10StateManager`, `dx10State`, `dx10StateCache` | `xrRenderDX10/StateManager/*` |
| Константы | `dx10ConstantBuffer[14 на стадию]` | `xrRenderDX10/dx10ConstantBuffer.{h,cpp}` |
| Ресурсы | `CResourceManager` | `xrRender/ResourceManager.h:15` |
| Render target | `CRT` | `xrRender/SH_RT.h:4`, `xrRenderDX10/dx10SH_RT.cpp:15` |
| Текстура | `CTexture` | `xrRender/SH_Texture.h:9` |
| GPU-события | `PIX_EVENT`, `PIX_FRAME_START` | `xrRender/Debug/dxPixEvents.{h,cpp}` |
| Профилирование | `TracyD3D11Ctx` | `xrCore/xrCore.h:87`, `xrRender/HW.h:49` |
| Захват кадра | `g_renderdoc_api` | `xrRender/dxRenderDeviceRender.cpp:6-11, 108-145` |
| ReShade | `render_reshade_effects()` | `xr_3da/ReShadeCompat.cpp:23-44` |

## 4. Кадр

Вход: `CRenderDevice::Run()` (`device.cpp:417`) → `Begin()` (`:32`) → … → `End()` (`:64`).

`CRender::Render()` — `ogsr_engine/Layers/xrRenderPC_R4/r4_R_render.cpp:266-536`.
Декларативного списка стадий нет, конвейер захардкожен. Порядок:

| # | Стадия | Строка |
|---|---|---|
| — | Переход на render-разрешение, early-out, детект camera cut | 279-307 |
| — | **TAA-джиттер** через `ffxFsr3UpscalerGetJitterOffset` | 309-324 |
| 1 | `SKY_RENDER` — MRT-биндинг + небо | 326-342 |
| 2 | `phase_scene_prepare()` | 354 |
| 3 | `DEFER_PART0_SPLIT` — расчёт + статика графа сцены | 356-370 |
| 4 | `DEFER_TEST_LIGHT_VIS` — occlusion query, видимость | 377-390 |
| 5 | `DEFER_PART1_SPLIT` — LOD, детали, HUD, копии depth | 393-421 |
| 6 | `copy_zbuffer` | 423-430 |
| 7 | `DEFER_WALLMARKS` | 433-439 |
| 8 | `DEFER_RAIN` | 441-445 |
| 9 | `DEFER_SUN` | 448-456 |
| 10 | `DEFER_SELF_ILLUM` | 458-485 |
| 11 | `DEFER_FLUSH_OCCLUSION` | 488-508 |
| 12 | `DEFER_LIGHT` | 511-521 |
| 13 | **`DEFER_COMBINE`** → `phase_combine()` | 524-527 |

### Пост-обработка

`CRenderTarget::phase_combine()` — `xrRenderPC_R4/r4_rendertarget_phase_combine.cpp:8-378`.
Порядок (30 стадий, сокращённо):

1. LUM-пул (автоэкспозиция) · 2. облака · 3. `combine_1` (тонмаппинг)
4. **SSR** · 5. forward/прозрачные · 6. volumetric · 7. distortion
8. bloom · 9. `combine_tonemap` · 10. heatvision · 11. `Phase3DSSUpscale()`
12. **`PhaseAA()`** ← DLSS / FSR3 / TAA / SMAA · 13. SSSS (солнечные лучи)
14. reticle 3DSS · 15. blur · 16. **DoF** · 17. **LUT/ACES**
18. gasmask DoV · 19. nightvision · 20. капли дождя
21. `combine_2` (motion blur) · 22-30. flares, `phase_pp`, биндинг backbuffer

## 5. Render targets и G-buffer

`CRenderTarget` объявлен в `r4_rendertarget.h` (387 строк), создаётся в
`r4_rendertarget.cpp:185+`. Всего **~70 RT**.

**Ключевая для апскейла двухразречная модель:**

```cpp
// r4_rendertarget.h:14-17
u32 m_renderWidth,  m_renderHeight;    // физический рендер-размер (для DLSS/FSR3)
u32 m_displayWidth, m_displayHeight;   // размер окна
```
API: `GetRenderWidth()`…`GetDisplayHeight()` (`:303-306`),
`SetTemporalRenderSize()` (`:315`), `GetRenderSubrectScaleX/Y()` (`:311-312`),
`BeginTemporalUpscaleInput()/EndTemporalUpscaleInput()` (`:317-318`),
`ResetTemporalHistory()` (`:319`).

Ключевые буферы:

| RT | Формат | Роль |
|---|---|---|
| `rt_base` | `R8G8B8A8_UNORM`, `CreateBase` | алиас swapchain (`r4_rendertarget.cpp:222`) |
| `rt_Base_Depth` | `R24G8_TYPELESS` | MRT-глубина |
| `rt_zbuffer` | **`R24G8_TYPELESS`** | рабочая глубина (**D24_UNORM_S8_UINT**) |
| `rt_Generic_0` | `R16G16B16A16_FLOAT` | цвет сцены / вход апскейлера |
| `rt_Velocity` | `R16G16_FLOAT` | motion vectors |
| `rt_Generic_combine` | `R16G16B16A16_FLOAT` + **UAV** | **выход апскейлера**, display-res |
| `rt_Generic_scene_scratch` | `R16G16B16A16_FLOAT` + **UAV** | UAV-пингпонг, render-res |
| `rt_Postprocess_0` | `R16G16B16A16_FLOAT` | полноразмерный цвет после апскейла |
| `rt_smap_sun_cascade[3]` | `R24G8_TYPELESS` | 3 каскада CSM солнца |

Четыре перегрузки `u_setrt` (до 4 MRT + depth) — `r4_rendertarget.h:236-239`,
реализация `r4_rendertarget.cpp:7/45/82/117`.

### G-buffer отложенного рендеринга

`ogsr_engine/Layers/xrRenderPC_R4/r4_rendertarget_phase_scene.cpp:32`:

```cpp
u_setrt(cmd_list, rt_Position, rt_Color, rt_Velocity, rt_heat, rt_Base_Depth->pZRT[cmd_list.context_id]);
```

| Слот | RT | Формат | Содержимое |
|---|---|---|---|
| 0 | `rt_Position` | `R16G16B16A16_FLOAT` | позиция **в пространстве глаза** |
| 1 | `rt_Color` | `R8G8B8A8_UNORM` | albedo + specular-gloss |
| 2 | `rt_Velocity` | `R16G16_FLOAT` | motion vectors |
| 3 | `rt_heat` | `R8G8B8A8_UNORM` | маска heatvision |

> **Важно для RTX.** В G-buffer **нет** мировых нормалей, roughness/metallic и
> emissive. Любая трассировка потребует их добавить — либо переиспользовать
> `rt_Color.a` (specular-gloss) как источник roughness.

## 6. Шейдеры

### Формат и расположение

**Обычный текст HLSL, компилируется в рантайме через `D3DCompile`.**
Предкомпилированных `.bin`/`.cso` нет.

```
Game/Resources_SoC_1.0006/gamedata/shaders/
├── r3/      — основной набор
└── common/  — общие заголовки (smaa.h, models_detector_cfg.h)
```

Состав:

| Расширение | Количество |
|---|---|
| `.ps` | 206 |
| `.vs` | 120 |
| `.gs` | 3 (`fluid_array*.gs`) |
| `.hs` / `.ds` | 1 / 1 (`dx11/tess.*`) |
| **`.cs`** | **0** |
| `.s` | 125 (Lua-скрипты шейдеров) |
| `.h` | 93 (include) |

Путь разрешается как: `CRender::getShaderPath()` возвращает `"r3\\"`
(`r4.h:258`), а `$game_shaders$` — VFS-алиас на `gamedata\shaders`
(`ogsr_engine/xrCore/LocatorAPI_defs.h:15`, резолв в `LocatorAPI.cpp:660-720`).

### Загрузка и компиляция

| Стадия | Файл:строка | Цель компиляции |
|---|---|---|
| Vertex | `xrRenderDX10/dx10ResourceManager_Resources.cpp:110-177` | `vs_5_0`, entry `main`; откат на `vs_4_1`/`main_vs_4_1` по поиску токена в тексте (`:156-160`) |
| Pixel | `dx10ResourceManager_Resources.cpp:206-268` | `ps_5_0`/`main`, откат `ps_4_1` |
| GS/HS/DS/**CS** | `xrRender/ShaderResourceTraits.h:110-166` | `gs_5_0`, `hs_5_0`, `ds_5_0`, **`cs_5_0`** |

### Точка входа шейдерной системы

**`CRender::shader_compile()` — `ogsr_engine/Layers/xrRenderPC_R4/r4.cpp:674-906`.**

1. Собирает `xr_vector<D3D_SHADER_MACRO>` из `m_ShaderOptions` (`r4.h:356`).
2. Добавляет ~35 дефайнов (`:680-821`): `SMAP_size`, `FP16_FILTER`, `USE_HWSMAP`,
   `USE_GTAO`, `SSAO_QUALITY`, `SKIN_0..4`, `ALLOW_STEEPPARALLAX`,
   `GBUFFER_OPTIMIZATION`, `SM_5` (по feature level, `:775`), `SSLR_ENABLED`,
   `SSFX_*`, **`USE_ACES`** (`:789`), `USE_CS_COP_LMAP`.
3. **Дисковый кэш** (`:823-869`): `$app_data_root$/shaders_cache/<имя>.<ext>\`
   с CRC32 препроцессированного исходника; при `R2FLAGEXT_SHADER_CACHE`
   используется `D3DPreprocess`.
4. `D3DCompile(...)` на **`:873`**, запись кэша `:876-888`.
5. `create_shader()` (`:512-638`) → `D3DReflect` → `R_constant_table::parse()`.
6. Опциональный дизассемблер в `<name>.txt` (`:624-635`).

**Потолок шейдерной модели — SM 5.0.** Ни SM6, ни mesh shaders, ни bindless.

### Кэш состояний / PSO

`CResourceManager` (`xrRender/ResourceManager.h`) — это и есть дедуплицирующий
PSO-кэш. Пул ресурсов: `m_textures, m_rtargets, m_vs, m_ps, m_gs, m_ds, m_hs, m_cs`
(`:39-49`), состояния `v_states, v_declarations, v_constant_tables,
v_constant_buffer[10]` (`:51-57`). Лимиты `SHADER_PASSES_MAX 2`,
`SHADER_ELEMENTS_MAX 16` (`Shader.h:22-23`).

### Тонмаппинг

ACES реализован в HLSL: `Game/Resources_SoC_1.0006/gamedata/shaders/r3/ACES.H`
(`sRGB_to_ACES`, `ACES_to_sRGB`, `ACEScg_*`), плюс LMT-хелперы в `ACES_LMTs/`.
Включается дефайном `USE_ACES` (`r4.cpp:789`), флагом `R2FLAGEXT_USE_ACES`
(`xrRender_console.h:254`), командой `ssfx_use_aces`.
AgX / Khronos PBR Neutral отсутствуют.

## 7. Файлы, которые придётся трогать

По убыванию вероятности:

1. `ogsr_engine/Layers/xrRender/RenderTargetPhaseAA.cpp` — **главный** файл по апскейлу/AA
2. `ogsr_engine/Layers/xrRender/xrRender_console.{h,cpp}` — консольные переменные
3. `ogsr_engine/Layers/xrRenderPC_R4/r4_R_render.cpp:301-324` — джиттер и сброс истории
4. `ogsr_engine/Layers/xrRenderPC_R4/r4_rendertarget.{h,cpp}` — создание RT
5. `ogsr_engine/Layers/xrRenderPC_R4/r4_rendertarget_phase_combine.cpp` — порядок пост-обработки
6. `ogsr_engine/Layers/xrRenderPC_R4/r4.cpp:674-906` — компиляция шейдеров
7. `ogsr_engine/Layers/xrRenderDX10/dx10ResourceManager_Resources.cpp` — загрузка VS/PS
8. `ogsr_engine/Layers/xrRender/ShaderResourceTraits.h` — загрузка GS/HS/DS/CS
9. `ogsr_engine/Layers/xrRenderDX10/dx10HW.cpp` — устройство, swapchain
10. `ogsr_engine/Layers/xrRender/R_Backend.h` + `R_Backend*.cpp` — `CBackend`
11. `ogsr_engine/Layers/xrRenderDX10/StateManager/*` — кэш состояний

## 8. Мёртвый код (важно)

В `xrRender/` лежат файлы, **исключённые из `xrRender_R4.vcxproj`**:
`ResourceManager_Resources.cpp`, `ResourceManager_Scripting.cpp`, `SH_RT.cpp`,
`SH_Texture.cpp`, `tga.cpp`, `dxParticleCustom.cpp`, а также
`xrRenderPC_R4/du_sphere_part.cpp`.

Реально компилируемые аналоги — `xrRenderDX10/dx10ResourceManager_*.cpp`,
`dx10SH_RT.cpp`, `dx10SH_Texture.cpp`.

> Правя загрузку шейдеров или render targets, редактируйте `dx10*`-файлы,
> а не `xrRender/*`-аналоги.