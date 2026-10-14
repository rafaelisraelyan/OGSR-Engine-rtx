# 06 — Возможности RTX в OGSR-Engine

> **Вывод в одном абзаце.** Движок — чистый DirectX 11. Аппаратный ray tracing
> (DXR, RT cores, `TraceRay`) в D3D11 **не существует** — это ограничение API,
> а не движка. Полноценный RTX в этом форке требует порта рендерера на
> DirectX 12, что по объёму сопоставимо с написанием нового рендерера.
> Всё, что можно сделать **без** DX12, перечислено в разделе 3.

---

## 1. Диагноз: почему «просто включить RTX» не работает

### 1.1 Движок не имеет DX12

Проверено по всему дереву:

| Проверка | Результат |
|---|---|
| `D3D12`, `dx12`, `D3D11_3` | 0 реальных вхождений |
| Макрос выбора бэкенда (`GRAPHICS_API`, `USE_DX12`) | отсутствует |
| Линковка | только `d3d11.lib; d3dcompiler.lib; dxgi.lib; dxguid.lib` (`XR_3DA.vcxproj:94`, `:164`) |
| Устройство | `D3D11CreateDevice` (`xrRenderDX10/dx10HW.cpp:178`) |

Потолок шейдерной модели — **SM 5.0** (`vs_5_0`, `ps_5_0`, `gs_5_0`, `cs_5_0`).
Ни SM6, ни mesh shaders, ни bindless, ни async compute — недоступны.

### 1.2 Hardware RT требует DX12

DXR (`ID3D12CommandList5::TraceRay`, `DispatchRays`, `D3D12_RAYTRACING_TIER`) —
это расширение **DirectX 12**. В D3D11 нет:

- ray tracing pipeline state / shader model;
- построения acceleration structures средствами API;
- `RayQuery` для inline ray tracing;
- доступа к RT cores.

**Единственный способ** использовать RT cores — перейти на DX12.

### 1.3 Официальный слой интеграции NVIDIA требует DX12

Проверено по `docs/ProgrammingGuideDLSS_G.md` в репозитории
`NVIDIAGameWorks/StreamLine`:

- строка 83: *«Call `slInit` as early as possible (before any D3D12/Vulkan APIs are invoked)»*;
- строки 185-186: проверка требований идёт через
  `FeatureRequirementFlags::eD3D12Supported` / `eVulkanSupported`.

StreamLine — официальный рекомендуемый путь интеграции DLSS Frame Generation,
DLSS-G и Reflex. **DX11 в списке нет.**

### 1.4 Текущая геометрия данных движка для RT не подходит

G-buffer отложенного рендеринга
(`r4_rendertarget_phase_scene.cpp:32`) — 4 MRT:

| Слот | RT | Формат | Содержимое |
|---|---|---|---|
| 0 | `rt_Position` | `R16G16B16A16_FLOAT` | позиция **в пространстве глаза** |
| 1 | `rt_Color` | `R8G8B8A8_UNORM` | albedo + specular-gloss |
| 2 | `rt_Velocity` | `R16G16_FLOAT` | motion vectors |
| 3 | `rt_heat` | `R8G8B8A8_UNORM` | маска heatvision |

Нет **мировых нормалей**, **roughness/metallic**, **emissive**.
Глубина — `R24G8_TYPELESS` (D24_UNORM_S8_UINT), что для DXR требует
конвертации в R32_FLOAT или подсказки компрессии Z-буфера.

Ни в одном шейдере движка нет слов `TraceRay`, `rayquery`, `RayDesc`,
`AccelerationStructure` (проверено: единственные совпадения — два
нерелевантных комментария в `3DFluid/dx103DFluidRenderer.cpp:520`
и `OpCoDe/OPC_RayCollider.cpp:58`).

---

## 2. Что есть в SDK NVIDIA DLSS для D3D11

Проверено напрямую по заголовкам SDK **v310.9.1**.

### 2.1 Точки входа

`include/nvsdk_ngx.h` объявляет:

```
NVSDK_NGX_D3D11_Init / Init_Ext / Init_with_ProjectID
NVSDK_NGX_D3D11_GetCapabilityParameters
NVSDK_NGX_D3D11_CreateFeature / EvaluateFeature / ReleaseFeature
NVSDK_NGX_D3D11_Shutdown / Shutdown1
NVSDK_NGX_D3D11_GetFeatureRequirements
...полный D3D11-параллельный набор
```

Обёртки `include/nvsdk_ngx_helpers_d3d.h`:

| Обёртка | Создаёт фичу |
|---|---|
| `NGX_D3D11_CREATE_DLSS_EXT` (`:85`) | `NVSDK_NGX_Feature_SuperSampling` — **DLSS SR, работает** |
| `NGX_D3D11_EVALUATE_DLSS_EXT` (`:103`) | — |
| `NGX_D3D11_CREATE_DLISP_EXT` | `NVSDK_NGX_Feature_ImageSignalProcessing` — **DLISS/NIS, работает** |
| `NGX_D3D11_CREATE_DLSSD_EXT` (`nvsdk_ngx_helpers_dlssd_d3d.h:227`) | `NVSDK_NGX_Feature_RayReconstruction` (`:244`) |
| `NGX_D3D11_EVALUATE_DLSSD_EXT` (`:247`) | — |

Структура `NVSDK_NGX_D3D11_DLSS_Eval_Params` (helpers_d3d.h:34-70) содержит
в том числе поля RR-семейства:
`pInRayTracingHitDistance`, `pInBiasCurrentColorMask`, `pInIsParticleMask`,
`pInAnimatedTextureMask`, `pInDepthHighRes`, `pInPositionViewSpace`.

### 2.2 Как это читать

**Наличие D3D11-точек входа ≠ поддержка NVIDIA.**

- Официально поддерживаемая конфигурация (StreamLine) — **DX12/Vulkan**.
- DLSS Frame Generation (`NVSDK_NGX_Feature_FrameGeneration`, enum 11)
  специализированной D3D11-обёртки в SDK **не имеет** вовсе.
  Единственный путь — ручная сборка параметров, что почти наверняка будет
  отклонено DLL с `NVSDK_NGX_Result_FAIL_FeatureNotSupported`.
- DLSS Ray Reconstruction — нейросетевой денойзер, но «RT» в нём означает
  реконструкцию по заранее рассчитанным диффузным/спекулярным картам и
  hit distance, а не трассировку лучей. Порог входа — RT cores.

**Вывод:** заголовки D3D11 для RR/FG присутствуют, но это не делает
фичи рабочими на DX11. Проверка возможна только экспериментом на RTX-видеокарте.

---

## 3. Что реально можно сделать (ранжировано по «доход / риск»)

### 3.1 Обновить DLSS SDK 310.7.0 → 310.9.1 — НИЗКИЙ РИСК, высокий доход

Текущая версия закреплена в `Update_Components.cmd:26`:

```cmd
git clone --branch v310.7.0 --depth 1 https://github.com/NVIDIA/DLSS.git 3rd_party/Src/NVIDIA_DLSS/DLSS
```

Актуальная — **v310.9.1** (коммит `3749594`, 08 сен). Между ними:

| Версия | Что добавили |
|---|---|
| 310.9.1 | **Preset F** для DLSS Ray Reconstruction (Transformer mode) |
| 310.6.0 | **Frame Generation 5x и 6x**, FG UI Recomposition (Preset B) |
| 310.5.3 | CUDA-поддержка для DLSS RR |
| 310.5.0 | **Preset L и M** для DLSS Super Resolution |

**Обратная совместимость проверена по исходникам SDK, а не предположена.**

| Проверка | Результат |
|---|---|
| Все 43 символа `NVSDK_NGX_*` / `NGX_*`, которые использует движок | **43 из 43** присутствуют в 310.9.1, отсутствующих — 0 |
| Include-цепочка umbrella | `nvsdk_ngx_helpers.h:116` подключает `nvsdk_ngx_helpers_d3d.h`, где живут `NGX_D3D11_CREATE_DLSS_EXT` / `NGX_D3D11_EVALUATE_DLSS_EXT` |
| `.lib`, которые линкует движок | `nvsdk_ngx_s.lib`, `nvsdk_ngx_s_dbg.lib`, `nvsdk_ngx_s_dbg_iterator0.lib`, `nvsdk_ngx_d.lib` — все на месте |
| DLL для post-build | `rel/nvngx_dlss.dll` (58 956 912 Б) и `dev/nvngx_dlss.dll` (59 383 920 Б) — обе на месте |
| `NVSDK_NGX_DLSS_Hint_Render_Preset_*` (Default 0 … O 15) | **без перенумерации** 310.7.0 → 310.9.1 |
| `NVSDK_NGX_PerfQuality_Value` (MaxPerf … DLAA) | **байт-в-байт идентичен** |

Проверка перенумерации важна, потому что `ps_r_dlss_preset` и
`ps_r_dlss_quality` — `u32`, которые сохраняются в конфиге пользователя:
при сдвиге enum'а сохранённые настройки молча сменили бы смысл.

**Preset L и M включатся автоматически.** Движок уже читает версию
загруженной DLL (`DlssDllVersionAtLeast(310, 5, 0)`,
`RenderTargetPhaseAA.cpp:248`) и добавляет L/M в маску доступных пресетов —
отдельного кода не требуется.

Единственная правка — одна строка в `Update_Components.cmd`.
**Выполнено 2026-10-14.**

### 3.2 Включить NVIDIA Image Scaling (NIS) — НИЗКИЙ РИСК

**Почему это лучший нереализованный вариант.**

1. NIS официально поддерживает **DX11**. Из `README.md` репозитория NVIDIA/DLSS:
   > *«It contains compute shaders that can be integrated with **DX11**, DX12, and Vulkan.»*
2. Он **уже лежит в дереве**: `NVIDIA/DLSS` — репозиторий с submodule
   `NVIDIAImageScaling` (sha `35e13ba3`).
3. Но `Update_Components.cmd` клонирует **без `--recurse-submodules`**, поэтому
   NIS физически отсутствует. Это баг скрипта, а не дизайна.
4. Движок уже умеет всё необходимое: CS-шейдеры через
   `ShaderTypeTraits<SCS>` (`cs_5_0`), UAV-таргеты (`rt_Generic_combine`,
   `rt_Generic_scene_scratch`), TAA-джиттер.

NIS даёт качественную пространственную апскейлинг + шарпинг, конкурентоспособную
с FSR 3, и это естественный fallback-режим, когда DLSS недоступен.

**Что потребуется:** расширить `Update_Components.cmd` флагом
`--recurse-submodules` (или отдельным клоном NIS), добавить `.cs`-шейдеры NIS
в `gamedata/shaders/r3/`, написать обёртку по образцу `Fsr3Wrapper`.

### 3.3 SSGI / Hi-Z на compute-шейдерах — ВЫСОКИЙ ДОХИН, ВЫСОКАЯ СЛОЖНОСТЬ

**Это единственный способ получить «RT-look» в DX11.**

Фундамент уже есть:
- `ShaderTypeTraits<SCS>` собирает `cs_5_0` / `ID3D11ComputeShader`
  (`xrRender/ShaderResourceTraits.h:47-63`) — загрузчик работает;
- UAV-таргеты создаются (`r4_rendertarget.cpp:287-292`);
- но **`.cs`-файлов в gamedata — ноль**, то есть путь ни разу не пройден.

Что предстоит сделать:

| Этап | Содержание |
|---|---|
| 1. Hi-Z | Построение пирамиды мип-уровней из `rt_zbuffer`; нужен R32_FLOAT и UAV-структура |
| 2. DDA-трассировка | Ray marching по Hi-Z в compute-шейдере |
| 3. Трассировка геометрии | Доступ к треугольникам через буфер индексов/позиций + material ID |
| 4. Тени | Трассировка к CSM, запись hit distance |
| 5. Денойз | Временной + пространственный (à la NRD, но DX11-реализация) |
| 6. Интеграция | Условный compile-time флаг в `r4.cpp:674-906`, ветка в `phase_combine()` |

**Это большой объём работы** (ориентир — месяцы), но технически всё необходимое
в движке есть, и это единственный путь, дающий настоящие
трассируемые тени и косое освещение без DX12.

### 3.4 Улучшения в рамках текущего DLSS — НИЗКИЙ РИСК

В `RenderTargetPhaseAA.cpp` закомментированы флаги, которые стоит включить:

| Флаг | Строка | Зачем |
|---|---|---|
| `NVSDK_NGX_DLSS_Feature_Flags_DoSharpening` | `:332` | Без него `InSharpness` игнорируется, пост-резкость не применяется. Уже есть собственный CAS (`ProcessCAS`, `:611`) — нужно выбрать одно из двух, иначе будет двойное sharpen |
| `NVSDK_NGX_DLSS_Feature_Flags_MVJittered` | `:337` | Джиттер применяется (`:418-419`), флаг флаг — семантика MV |
| `NVSDK_NGX_DLSS_Feature_Flags_AlphaUpscaling` | `:341` | Корректная обработка прозрачных пикселей — в движке много травы, дождя и дымки |

Отдельно: **качество DLSS по умолчанию — DLAA**
(`ps_r_dlss_quality = DLSS_QUALITY_DLAA`, `xrRender_console.h:58`), то есть
апскейлинга нет, только anti-aliasing. Это разумный дефолт, но стоит
задокументировать в UI.

### 3.5 Чего делать **нельзя** (или можно, но не сейчас)

| Хотелось бы | Реальность |
|---|---|
| DLSS Ray Reconstruction | Нужен DX12 + RT cores |
| DLSS Frame Generation | Официально только DX12/Vulkan (StreamLine) |
| NVIDIA Reflex (low latency) | Только через StreamLine → DX12 |
| Multi Frame Generation (DLSS 4) | Требует DLSS-RR → DX12 |
| Аппаратные лучи для отражений/теней | Требует DXR → DX12 |
| Port рендерера на DX12 | Реалистично, но это отдельный проект на год+ |

---

## 4. Оценка стоимости порта на DX12

Если задача сформулирована как «сделать настоящий RTX», честная оценка:

| Подсистема | Объём | Сложность |
|---|---|---|
| `CHW` — устройство, swapchain, очереди | переписать | средняя |
| `CResourceManager`, `CRT`, `CTexture` | переписать | средняя |
| `CBackend` (`R_Backend.*`, ~6 файлов) | переписать | высокая |
| `dx10StateUtils`, `dx10BufferUtils`, `tss_def` | выбросить (D3D12 не нуждается в эмуляции fixed-function) | — |
| `dx10StateManager`, `dx10ConstantBuffer` | переписать | высокая |
| Шейдерный компилятор `r4.cpp:674-906` | `*_5_0` → `*_6_x`, добавить варьирование defines | средняя |
| ~450 HLSL-шейдеров | добавить вариативные макросы (SM6 нужен для DXR libs) | очень высокая |
| Отложенные контексты (10 шт.) | переписать на command lists | средняя |
| FidelityFX DX11 backend | заменить на DX12 backend | средняя |

**Итог: это не «добавить RTX», а «написать новый рендерер».**
Реалистично только как отдельный долгий проект с веткой и сравнительным
бенчмарком. Практическая рекомендация — не начинать, пока не исчерпан
раздел 3.

---

## 5. Рекомендуемая последовательность

```
Этап A (дни)    ── DLSS 310.7.0 → 310.9.1
                 1 строка в Update_Components.cmd + пересборка + проверка лога
                 Предупреждение: nvngx_dlss.dll 59 МБ, версию проверить в логе
                 через r_aa_dlss_version

Этап B (недели) ── NVIDIA Image Scaling
                 1. Починить Update_Components.cmd (--recurse-submodules)
                 2. Заложить .cs-шейдеры NIS
                 3. Обёртка по образцу Fsr3Wrapper + режим в pp_aa_mode_token

Этап C (дни)    ── Тюнинг текущего DLSS
                 DoSharpening / MVJittered / AlphaUpscaling
                 + проверить конфликт с CAS

Этап D (месяцы) ── SSGI / Hi-Z на cs_5_0
                 Первый шаг: Hi-Z пирамида как отдельный этап с визуальной
                 отладкой через ImGui (DrawTexture уже есть в r4_imgui.cpp:75-77)
```

---

## 6. Как проверять

В движок встроен ImGui + Tracy + RenderDoc, этого достаточно для верификации.

**Консольные команды:**
```
r_aa_mode                 none|dlss|fsr3|taa|smaa
r_aa_dlss_quality         dlss_dlss|d3d_quality|d3d_balanced|d3d_performance|d3d_ultraperformance
r_aa_dlss_preset          default|f|j|k|l|m
r_aa_dlss_version         (read-only) печатает версию загруженного nvngx_dlss.dll
r_aa_fsr3_quality         fsr3_natalias|fsr3_quality|fsr3_balanced|fsr3_performance|fsr3_ultraperformance
r_3dss_scale_factor
```
Регистрация — `xrRender_console.cpp:892-896`.

**Диагностика по логу** — движок уже логирует всё нужное:
```
--[DLSS] loaded DLL version: [...]
--[DLSS] quality: [%d], output: [%ux%u], recommended render: [%ux%u], ...
--[DLSS] physical render domain: [%ux%u] -> [%ux%u]
--[DLSS] current preset: [%u]
!![DLSS] PLEASE UPDATE YOUR DRIVER
!![DLSS] DLSS NOT AVAILABLE
```

**Инструменты:**
- **Tracy** — сборка `Release` + env `CONFIGURATION_GA=ReleaseTracyProfiler`
  (см. [03-build-and-dependencies.md](03-build-and-dependencies.md)).
  Метки `PIX_EVENT(DLSS)`, `PIX_EVENT(FSR)`, `PIX_EVENT(3D_SCOPE_FSR)` уже расставлены.
- **RenderDoc** — `USE_RENDERDOC` уже включён
  (`xrRender/dxRenderDeviceRender.cpp:6-11`), in-process API hook поднят.
- **ImGui** — `r4_imgui.cpp:75-77` уже умеет показывать
  `rt_Color`, `rt_Position`, `rt_Accumulator` прямо в движке.
  Незаменимо при отладке Hi-Z.

---

## 7. Итоговая таблица

| Технология | Статус в движке | Нужен DX12 | Реалистично здесь |
|---|---|---|---|
| DLSS Super Resolution | ✅ работает, **дефолт** | нет | да |
| FSR 3 | ✅ работает | нет | да |
| TAA / SMAA / CAS | ✅ работают | нет | да |
| ACES tonemapping | ✅ работает | нет | да |
| NVIDIA Image Scaling | ❌ не подключён, но SDK поддерживает DX11 | нет | **да, рекомендую** |
| DLSS-флаги sharpening/jitter/alpha | ❌ закомментированы | нет | да |
| DLSS Ray Reconstruction | ❌ | **да** + RT cores | нет |
| DLSS Frame Generation | ❌ | **да** | нет |
| Reflex | ❌ | **да** | нет |
| Аппаратная трассировка | ❌ | **да** | нет |
| SSGI на compute | ❌ (инфраструктура есть) | нет | да, но дорого |

---

## 8. Источники

- `ogsr_engine/Layers/xrRender/RenderTargetPhaseAA.cpp` — вся интеграция DLSS/FSR3
- `ogsr_engine/Layers/xrRenderDX10/dx10HW.cpp:148-274` — создание D3D11-устройства
- `ogsr_engine/Layers/xrRenderPC_R4/r4_R_render.cpp:266-536` — кадр
- `ogsr_engine/Layers/xrRenderPC_R4/r4_rendertarget_phase_scene.cpp:32` — G-buffer
- `ogsr_engine/Layers/xrRenderPC_R4/r4_rendertarget.cpp:185-311` — создание RT
- `ogsr_engine/Layers/xrRender/ShaderResourceTraits.h:47-63` — поддержка `cs_5_0`
- `ogsr_engine/Layers/xrRenderPC_R4/r4.cpp:674-906` — компилятор шейдеров
- `Update_Components.cmd:25-26` — пин DLSS SDK
- `https://github.com/NVIDIA/DLSS` — `v310.9.1`, `README.md`, `include/*`
- `https://github.com/NVIDIAGameWorks/StreamLine` — `docs/ProgrammingGuideDLSS_G.md`