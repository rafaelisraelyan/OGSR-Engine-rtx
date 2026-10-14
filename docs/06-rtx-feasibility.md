# 06 — Возможности RTX в OGSR-Engine

> **Проверено против ветки `main_cop_cs`.** Исправлено против SoC-версии:
> нормали в G-buffer **есть** (ранее утверждалось обратное);
> `RayQuery` в физике существует и это не DXR;
> этап A — выполнен; консольные токены уточнены.

**Вывод в одном абзаце.** Движок — **чистый DirectX 11**. Аппаратный ray tracing
(DXR, RT cores, `TraceRay`) в D3D11 **не существует** — это ограничение API,
а не движка. Полноценный RTX в этом форке требует порта рендерера на
DirectX 12, что по объёму сопоставимо с написанием нового рендерера.
Всё, что можно сделать **без** DX12, перечислено в разделе 3.

---

## 1. Диагноз: почему «просто включить RTX» не работает

### 1.1 Движок не имеет DX12

| Проверка | Результат |
|---|---|
| `D3D12`, `dx12`, `D3D11_3` | 0 совпадений во всём `ogsr_engine/**` и `Game/**` |
| Макрос выбора бэкенда | `GRAPHICS_API` = 0, `USE_DX12` = 0. **Но есть `#define USE_DX11`** в `xrRenderPC_R4/stdafx.h:30` — это односторонний выбор, DX11 жёстко зашит |
| Линковка | только `d3d11.lib; d3dcompiler.lib; dxgi.lib; dxguid.lib` (`XR_3DA.vcxproj:94`, `:164`). `d3d12.lib`, `dxcompiler.lib`, DXIL — 0 во всех 26 vcxproj |
| Устройство | `D3D11CreateDevice` (`dx10HW.cpp:178`) |
| Потолок шейдерной модели | SM 5.0 (`vs_5_0`, `ps_5_0`, `gs_5_0`, `hs_5_0`, `ds_5_0`, `cs_5_0`) |
| SM6 / mesh / bindless / async compute | 0 совпадений |

### 1.2 Hardware RT требует DX12

DXR — расширение **DirectX 12**. В D3D11 нет ни ray tracing pipeline,
ни ускорений для RT cores.

**Единственный способ** использовать RT cores — перейти на DX12.

### 1.3 Официальный слой интеграции NVIDIA требует DX12

Проверено по `docs/ProgrammingGuideDLSS_G.md` в репозитории
`NVIDIAGameWorks/Streamline`:

- *«Call `slInit` as early as possible (before any D3D12/Vulkan APIs are invoked)»*;
- проверка требований идёт через
  `FeatureRequirementFlags::eD3D12Supported` / `eVulkanSupported`.

StreamLine — официальный рекомендуемый путь для DLSS Frame Generation,
DLSS-G и Reflex. **DX11 в списке нет.**
*(Уточнение: этот файл — внешний источник, его номер строк не проверялся локально.)*

### 1.4 Геометрия данных движка

G-buffer — `r4_rendertarget_phase_scene.cpp:32`, 4 MRT:

```cpp
u_setrt(cmd_list, rt_Position, rt_Color, rt_Velocity, rt_heat, rt_Base_Depth->pZRT[cmd_list.context_id]);
```

Содержимое — `Game/Resources_CoP_1.6.02/gamedata/shaders/r3/common_iostructs.h:287-293`:

```hlsl
struct f_deffer
{
    float4 position : SV_Target0; // xy=encoded normal, z = pz, w = encoded(m-id,hemi)
    float4 C : SV_Target1;        // r, g, b,  gloss
    float2 Velocity : SV_Target2;
    float4 H : SV_Target3;
};
```

| Слот | RT | Что хранит |
|---|---|---|
| 0 | `rt_Position` (R16G16B16A16_FLOAT) | **нормаль в пространстве глаза (в `.xy`, закодирована)**, view-space Z в `.z`, (mat-id, hemi) в `.w` |
| 1 | `rt_Color` (R8G8B8A8_UNORM) | albedo `.rgb` + **gloss** `.a` |
| 2 | `rt_Velocity` (R16G16_FLOAT) | motion vectors |
| 3 | `rt_heat` (R8G8B8A8_UNORM) | маска heatvision |

> ⚠️ **Исправление против SoC-версии.** Ранее утверждалось, что нормалей
> в G-buffer нет. **Это неверно**: нормали есть, в `rt_Position.xy`,
> закодированы. Для RTX это существенно — расшифровывать надо, а не
> городить новый MRT. `rt_Color.a` — gloss, готовый источник roughness.
> Что действительно отсутствует: **metallic, emissive в 4 MRT**
> (emissive идёт отдельной стадией `DEFER_SELF_ILLUM`).

Глубина — `R24G8_TYPELESS` (D24_UNORM_S8_UINT). Для DXR неоптимальна.

### Шейдеры: трассировки действительно нет

По всем HLSL (`Game/**`: `.ps/.vs/.gs/.hs/.ds/.cs/.s/.h`) и движковым
`.cpp/.h` — **0 совпадений** для `TraceRay`, `rayquery`, `DXR`,
`AccelerationStructure`.

> ⚠️ **Уточнение формулировки.** Слово `RayQuery` в движке встречается
> **около 40 раз**, и большинство — это **физика X-Ray**:
> `xrGame/xr_area_raypick.cpp:159,164`, `xr_collide_form.h:101`,
> `ICollisionForm::RayQuery`, `ai/actors/helpers/actor_movement_helper.cpp`,
> `Car.cpp`, `PHSimpleCharacter.cpp`. Это **не** DXR, а собственный аналог
> «ray query» поверх Opcode/CDB. Формулировать так, иначе заявление неверно
> буквально.

Не относящиеся к делу совпадения — комментарий «raytracing» в
`3DFluid/dx103DFluidRenderer.cpp:520` и `OpCoDe/OPC_RayCollider.cpp:58`.

---

## 2. Что есть в SDK NVIDIA DLSS для D3D11 (v310.9.1)

### 2.1 Точки входа

`include/nvsdk_ngx.h` объявляет полный D3D11-параллельный набор:

```
NVSDK_NGX_D3D11_Init (:150) / Init_Ext (:151) / Init_with_ProjectID (:246)
GetCapabilityParameters (:422) / CreateFeature (:544) / EvaluateFeature (:683)
ReleaseFeature (:573) / Shutdown (:278) / Shutdown1 (:281)
GetFeatureRequirements (:617)
```

Обёртки `include/nvsdk_ngx_helpers_d3d.h`:

| Обёртка | Строка | Создаёт фичу |
|---|---|---|
| `NGX_D3D11_CREATE_DLSS_EXT` | `:85` | `NVSDK_NGX_Feature_SuperSampling` (`:99`) — **DLSS SR, работает** |
| `NGX_D3D11_EVALUATE_DLSS_EXT` | `:103` | — |
| `NGX_D3D11_CREATE_DLISP_EXT` | `:169` | `NVSDK_NGX_Feature_ImageSignalProcessing` (`:180`) |
| `NGX_D3D11_CREATE_DLSSD_EXT` | `dlssd_d3d.h:227` | `NVSDK_NGX_Feature_RayReconstruction` (`:244`) |
| `NGX_D3D11_EVALUATE_DLSSD_EXT` | `:247` | — |

Структура `NVSDK_NGX_D3D11_DLSS_Eval_Params` (`helpers_d3d.h:34-70`) содержит
поля RR-семейства:
`pInRayTracingHitDistance` (`:68`), `pInBiasCurrentColorMask` (`:48`),
`pInIsParticleMask` (`:63`), `pInAnimatedTextureMask` (`:64`),
`pInDepthHighRes` (`:65`), `pInPositionViewSpace` (`:66`).

enum'ы: `NVSDK_NGX_Feature_FrameGeneration = 11`,
`NVSDK_NGX_Feature_RayReconstruction = 13` (`nvsdk_ngx_defs.h:210`, `:214`).

### 2.2 DLSS-G (Frame Generation) — D3D11-обёртки в SDK нет

`nvsdk_ngx_helpers_dlssg_d3d.h` содержит **только**
`NGX_D3D12_CREATE_DLSSG` (`:46`), `NGX_D3D12_EVALUATE_DLSSG` (`:66`),
`NGX_D3D12_ESTIMATE_VRAM_DLSSG` (`:197`). D3D11 — **0 записей**.

### 2.3 Как это читать

**Наличие D3D11-точек входа ≠ поддержка NVIDIA.**

- Официально поддерживаемая конфигурация (StreamLine) — **DX12/Vulkan**.
- Специализированной D3D11-обёртки у Frame Generation нет вовсе — единственный
  путь ручная сборка параметров через
  `NVSDK_NGX_D3D11_CreateFeature(..., NVSDK_NGX_Feature_FrameGeneration, ...)`,
  что почти наверняка будет отклонено DLL с
  `NVSDK_NGX_Result_FAIL_FeatureNotSupported`.
- DLSS Ray Reconstruction — нейросетевой денойзер, но «RT» в нём означает
  реконструкцию по заранее рассчитанным картам и hit distance,
  а не трассировку лучей. Порог входа — RT cores.

**Вывод:** заголовки D3D11 для RR/FG присутствуют, но это не делает
фичи рабочими на DX11. Проверка возможна только экспериментом на RTX-видеокарте.

---

## 3. Что реально можно сделать (ранжировано)

### 3.1 ✅ ВЫПОЛНЕНО: обновить DLSS SDK 310.7.0 → 310.9.1

**`Update_Components.cmd:26`** теперь `git clone --branch v310.9.1 …`.
На диске `3rd_party/Src/NVIDIA_DLSS/DLSS` = тег **v310.9.1**,
коммит `374959484e79a640feaba44c93ac8cfb0a03f5b5` (2026-09-08).

**Обратная совместимость проверена, а не предположена:**

| Проверка | Результат |
|---|---|
| Все API-символы, которые использует движок (42 шт. после исключения имён `.lib`) | **42 из 42**, отсутствующих — **0** |
| Include-цепочка | `nvsdk_ngx_helpers.h:116` подключает `nvsdk_ngx_helpers_d3d.h` |
| 4 `.lib` из `AdditionalLibraryDirectories` | все на месте |
| `rel/` + `dev/` DLL для post-build | обе на месте |
| `NVSDK_NGX_DLSS_Hint_Render_Preset_*` (Default 0 … O 15) | **без перенумерации** 310.7.0 → 310.9.1 |
| `NVSDK_NGX_PerfQuality_Value` (MaxPerf … DLAA) | **байт-в-байт идентичен** |

Проверка перенумерации важна: `ps_r_dlss_preset` и `ps_r_dlss_quality` —
`u32`, сохраняемые в конфиге. При сдвиге enum'а настройки молча сменили бы смысл.

**Preset L и M включатся автоматически** — движок читает версию DLL
(`DlssDllVersionAtLeast(310, 5, 0)`, `RenderTargetPhaseAA.cpp:248`).
Своего кода для этого не требуется.

**Что дало:** Preset L/M и более свежие модели. Остальное (FG, RR) упирается
в DX12-барьер независимо от версии SDK.

### 3.2 NIS — официально поддерживает DX11, но недоступен

**Почему это лучший нереализованный вариант.**

1. NIS — compute-шейдер, официально поддерживающий **DX11**
   (`3rd_party/Src/NVIDIA_DLSS/DLSS/README.md:6`, цитата проверена локально).
2. Он **уже лежит в дереве**: `NVIDIA/DLSS` — репозиторий с submodule
   `NVIDIAImageScaling` (`.gitmodules`, sha `35e13ba316c98eeecf16f37eae70ce88019911f6`).
3. Но `Update_Components.cmd:26` клонирует **без `--recurse-submodules`**.
   Проверка: каталог `.../DLSS/NVIDIAImageScaling/` **существует, но содержит
   0 элементов**; `git submodule status` выдаёт `-35e13ba3…` — submodule
   не инициализирован.
4. Движок умеет: CS через `ShaderTypeTraits<SCS>` (`cs_5_0`,
   `ShaderResourceTraits.h:47-63`), UAV-таргеты, TAA-джиттер.

**Что потребуется:** расширить `Update_Components.cmd` флагом
`--recurse-submodules`, добавить `.cs`-шейдеры NIS в `gamedata/shaders/r3/`,
написать обёртку по образцу `Fsr3Wrapper`.

### 3.3 SSGI / Hi-Z на compute-шейдерах — ВЫСОКИЙ ДОХОД, ВЫСОКАЯ СЛОЖНОСТЬ

**Единственный способ получить «RT-look» в DX11.**

Фундамент уже есть:

| Актив | Где |
|---|---|
| Компиляция `cs_5_0` | `ShaderResourceTraits.h:47-63` (`GetCompilationTarget()` `:54`) |
| Пул CS-шейдеров | `ResourceManager.h:39-49` (`m_cs`) |
| UAV-таргеты | `rt_Generic_scene_scratch`, `rt_Generic_combine` (`r4_rendertarget.cpp:287-292`) |
| **Нормали** | `rt_Position.xy` — закодированы, надо только декодировать |
| **Gloss → roughness** | `rt_Color.a` |
| Отладка в движке | `r4_imgui.cpp:75-81` — `DrawTexture` уже умеет показывать `rt_Color`, `rt_Position`, `rt_heat`, `rt_Generic_1/2`, `rt_Bloom_1` |

Но **`.cs`-файлов в gamedata — ноль**, то есть путь ни разу не пройден.

Что предстоит:

| Этап | Содержание |
|---|---|
| 1. Hi-Z | Пирамида мип-уровней из `rt_zbuffer`; нужен R32_FLOAT и UAV-структура |
| 2. DDA | Ray marching по Hi-Z в compute-шейдере |
| 3. Геометрия | Доступ к треугольникам (инлайн-буферы) + material ID из `rt_Position.w` |
| 4. Тени | Трассировка к CSM, запись hit distance |
| 5. Денойз | Временной + пространственный |
| 6. Интеграция | Compile-time флаг в `r4.cpp:674-906`, ветка в `phase_combine()` |

Это большой объём (ориентир — месяцы), но технически всё необходимое есть.

### 3.4 Улучшения в рамках текущего DLSS — НИЗКИЙ РИСК

В `RenderTargetPhaseAA.cpp` закомментированы флаги:

| Флаг | Строка | Зачем |
|---|---|---|
| `NVSDK_NGX_DLSS_Feature_Flags_DoSharpening` | `:332` | Без него `InSharpness` игнорируется (`:316-317`). Но есть собственный CAS (`ProcessCAS`, `:611`) — надо выбрать одно, иначе двойной sharpen |
| `NVSDK_NGX_DLSS_Feature_Flags_MVJittered` | `:337` | Джиттер реально применяется (`:418-419`) |
| `NVSDK_NGX_DLSS_Feature_Flags_AlphaUpscaling` | `:341` | Корректная обработка прозрачных пикселей (трава, дождь, дым) |

Отдельно: **качество по умолчанию — DLAA** (`ps_r_dlss_quality = DLSS_QUALITY_DLAA`,
`xrRender_console.cpp:58`), т.е. апскейлинга нет, только AA.
В Lua-UI это expose'ится как `dlss_quality` / `dlss_preset` / `fsr3_quality`
(`gamedata/Scripts/ui_mm_opt_video_adv.script:27-29`).

### 3.5 Чего делать **нельзя** (или можно, но не сейчас)

| Хотелось бы | Реальность |
|---|---|
| DLSS Ray Reconstruction | Нужен DX12 + RT cores |
| DLSS Frame Generation | Официально только DX12/Vulkan (StreamLine) |
| NVIDIA Reflex | Только через StreamLine → DX12 |
| Multi Frame Generation (DLSS 4) | Требует DLSS-RR → DX12 |
| Аппаратные лучи для отражений/теней | Требует DXR → DX12 |
| Порт рендерера на DX12 | Отдельный проект на год+ |

---

## 4. Оценка стоимости порта на DX12

| Подсистема | Объём | Сложность |
|---|---|---|
| `CHW` — устройство, swapchain, очереди | переписать | средняя |
| `CResourceManager`, `CRT`, `CTexture` | переписать | средняя |
| `CBackend` (`R_Backend.*`, ровно **6 файлов**) | переписать | высокая |
| `dx10StateUtils`, `dx10BufferUtils`, `tss_def` | выбросить | — |
| `dx10StateManager`, `dx10ConstantBuffer` | переписать | высокая |
| Шейдерный компилятор `r4.cpp:674-906` | `*_5_0` → `*_6_x` | средняя |
| **424** HLSL-файла шейдеров | добавить вариативные макросы | очень высокая |
| Отложенные контексты (10 шт.) | переписать на command lists | средняя |
| FidelityFX DX11 backend | заменить на DX12 backend | средняя |

**Итог: это не «добавить RTX», а «написать новый рендерер».**
Практическая рекомендация — не начинать, пока не исчерпан раздел 3.

---

## 5. Рекомендуемая последовательность

```
A (дни)         ── DLSS 310.7.0 → 310.9.1                     ✅ ВЫПОЛНЕНО 2026-10-14
                    1 строка, совместимость проверена,
                    собрано: xrEngine.exe + DLSS 310.9.1 в bin_x64

B1 (день)       ── починить Update_Components.cmd
                    (--recurse-submodules)
                        ↓
B2-B4 (недели)  ── NIS: .cs-шейдеры, обёртка, режим в UI
                    UI надо синхронизировать с ui_mm_opt_video_adv.script:27-29
                        ↓
C (дни)         ── тюнинг DLSS-флагов (DoSharpening / MVJittered / AlphaUpscaling)
                        ↓
D0-D2 (недели-месяцы) ── SSGI: гейт → Hi-Z → DDA
                        ↓
D3-D6 (месяцы)  ── геометрия → тени → денойз → интеграция
```

---

## 6. Как проверять

**Консольные команды** (`xrRender_console.cpp:892-898`):

```
r_aa_mode             off|dlss|fsr3|taa|smaa            (токены st_opt_*)
r_aa_dlss_quality     dlaa|quality|balanced|performance|ultra_performance
r_aa_dlss_preset      default|f|j|k|l|m
r_aa_dlss_version     (read-only) печатает версию загруженного nvngx_dlss.dll
r_aa_fsr3_quality     native_aa|quality|balanced|performance|ultra_performance
r_3dss_scale_factor   1.0 … 2.5
```

**Диагностика по логу** — движок логирует всё нужное:

```
:120  --[DLSS] loaded DLL version: [...]
:281  --[DLSS] quality: [%d], output: [%ux%u], recommended render: [%ux%u], ...
:316  --[DLSS] current preset: [%u]
:478  --[DLSS] physical render domain: [%ux%u] -> [%ux%u]
:204  !![DLSS] PLEASE UPDATE YOUR DRIVER
:210  !![DLSS] DLSS NOT AVAILABLE
```

**Инструменты:**
- **Tracy** — сборка `Release` + env `CONFIGURATION_GA=ReleaseTracyProfiler`
  (`Engine_properties.props:30-39`, `push.yml:40-44`).
  Метки `PIX_EVENT(DLSS)` (`:556`), `PIX_EVENT(FSR)` (`:892`),
  `PIX_EVENT(3D_SCOPE_FSR)` (`:915`) уже расставлены.
- **RenderDoc** — `USE_RENDERDOC` включён
  (`xrRender/dxRenderDeviceRender.cpp:6-11`, инициализация `:108-145`).
- **ImGui** — `r4_imgui.cpp:75-81` уже показывает G-buffer.

---

## 7. Итоговая таблица

| Технология | Статус | Нужен DX12 | Реалистично здесь |
|---|---|---|---|
| DLSS Super Resolution | ✅ работает, **дефолт** | нет | да |
| FSR 3 | ✅ работает | нет | да |
| TAA / SMAA / CAS | ✅ работают | нет | да |
| ACES tonemapping | ✅ работает | нет | да |
| **DLSS SDK обновление** | ✅ **выполнено 310.7.0 → 310.9.1** | нет | да |
| NVIDIA Image Scaling | ❌ не подключён, SDK поддерживает DX11 | нет | **да, рекомендую** |
| DLSS-флаги sharpening/jitter/alpha | ❌ закомментированы | нет | да |
| DLSS Ray Reconstruction | ❌ | **да** + RT cores | нет |
| DLSS Frame Generation | ❌ | **да** | нет |
| Reflex | ❌ | **да** | нет |
| Аппаратная трассировка | ❌ | **да** | нет |
| SSGI на compute | ❌ (инфраструктура есть, нормали есть) | нет | да, но дорого |

---

## 8. Источники

- `ogsr_engine/Layers/xrRender/RenderTargetPhaseAA.cpp` — вся интеграция DLSS/FSR3
- `ogsr_engine/Layers/xrRenderDX10/dx10HW.cpp:148-274` — создание D3D11-устройства
- `ogsr_engine/Layers/xrRenderPC_R4/r4_R_render.cpp:266-536` — кадр
- `ogsr_engine/Layers/xrRenderPC_R4/r4_rendertarget_phase_scene.cpp:32` — G-buffer
- `ogsr_engine/Layers/xrRenderPC_R4/r4_rendertarget.cpp:185-547` — RT (форматы до `:311`)
- `Game/Resources_CoP_1.6.02/gamedata/shaders/r3/common_iostructs.h:287-293` — раскладка G-buffer с нормалями
- `ogsr_engine/Layers/xrRender/ShaderResourceTraits.h:47-63` — поддержка `cs_5_0`
- `ogsr_engine/Layers/xrRenderPC_R4/r4.cpp:674-906` — компилятор шейдеров
- `Update_Components.cmd:25-26` — пин DLSS (v310.9.1)
- `3rd_party/Src/NVIDIA_DLSS/DLSS/` — заголовки и библиотеки SDK v310.9.1
- `Game/Resources_CoP_1.6.02/gamedata/Scripts/ui_mm_opt_video_adv.script:27-29` — DLSS-опции в Lua-UI
- `https://github.com/NVIDIA/DLSS` — `v310.9.1`, `README.md`, `include/*`
- `https://github.com/NVIDIAGameWorks/StreamLine` — `docs/ProgrammingGuideDLSS_G.md` *(внешняя ссылка, номера строк не проверены)*