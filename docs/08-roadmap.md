# 08 — План работ

> **Актуальная ветка документа:** `prypiat` (форк ветки CoP `main_cop_cs`).
> **Дата обновления планов:** 2026-10-27. Исторические статусы A/B1/F и их
> даты оставлены как ранее зафиксированные отчёты, а не результат новой
> проверки GPU/CI. Изменение roadmap **не означает** реализации или тестирования
> добавленных этапов.

**Конечная техническая цель:** сохранить стабильный DX11/R4 и поэтапно получить
**нативный DirectX 12**, затем **аппаратный DXR** (сначала RT-тени, затем — при
успешных измерениях — отражения/глобальное освещение).

**Принятый архитектурный ориентир (решение 2026-10-22):**
**X-Ray DX12/DXR — Hybrid Rendering Architecture**. Изучать гибридную
реализацию RTX в *Control* (Remedy / Northlight) как эталон выборочных
RT-эффектов и их интеграции в raster pipeline, а *Metro Exodus PC Enhanced
Edition* (4A Games) — как исследовательский ориентир динамического RTGI для
открытых пространств. **Это проектное решение, а не реализованный RT backend.**
Приоритет F3.6/F4.0 и порядок F4 → G3/G4 → опциональный G5 не меняются;
никакого обязательного path tracing, DDGI или RT для запуска игры.

**Новый приоритет после F3.5.6A–C (решение 2026-10-21):** перейти к раннему
**F4.0 — первому реальному игровому кадру CoP/OGSR на DX12**, не дожидаясь
полного переноса освещения, прозрачности, HUD, постобработки и DXR.
Это **план**, а не утверждение о готовом игровом DX12-рендерере; точные
зависимости реальных ассетов и игрового frame loop сначала проверить аудитом.

**Основные направления:**

- **DX11 — A–D:** DLSS, NIS, контроль качества апскейла, исследовательский SSGI/Hi-Z.
  Это полезно для текущей игры, но не является необходимым условием DX12.
- **DX12/DXR — F–G:** аудит зависимостей, вертикальный прототип нового backend,
  ранний каркас многопоточного CPU-side renderer в **F3.5 перед F4**,
  перенос игрового кадра и аппаратные лучи. Этап F0 можно начинать параллельно B2.
- **E:** технический долг и воспроизводимость сборки, без автоматического
  расширения объёма правок.
- **H:** поздний аудит и адресная оптимизация **всего игрового движка** после
  RT-рубежей; не путать с архитектурным DX12 F3.5.
- **I–L (новые планы, не реализованы):** секционный world streaming,
  высотная видимость, растительность/тени, бесшовные миры и возможность
  повторного использования DX12/DXR командой IX-Ray Platform.
- **P — PhysX Ragdoll (самостоятельный параллельный трек):** первая игровая цель —
  **ragdoll одного NPC в OGSR DX11/R4**, затем укрепление реализации и перенос
  подтверждённого ядра в IX-Ray. ODE/движение NPC не заменять; DX12 F4.0 не блокировать.
- **V — Volumetric Clouds (самостоятельный параллельный трек):** изолированный
  DX12/compute-прототип процедурных 3D-облаков с ray marching; позже —
  необязательная интеграция в игровой DX12 с выбором Classic 2D / Volumetric 3D.
  **V0–V2 разрешено разрабатывать параллельно F4.0; V не является гейтом F4/G.**

### Актуальная сверка выполнения DX12 и смежных направлений — 2026-10-25

**Основание:** коммиты, доступные в `origin/prypiat` на момент сверки
(HEAD `4b120ca81`), отчёты этапов и независимого аудита. Ниже отдельно
различаются **наличие кода в Git**, **PASS по представленным тестам** и
**полноценная приёмка игрового runtime**. Эта сводка исправляет устаревшие
`NEXT / NOT STARTED` в исторических таблицах ниже, но не переписывает
историю прежних решений. Сама правка roadmap не выполняет GPU-тесты.

| Этап | Фактическое состояние на дату сверки | Основание / оставшийся гейт |
|---|---|---|
| **F0–F2** — аудит, ADR, bootstrap | ✅ Реализованы и зафиксированы; standalone DX12 bootstrap проверялся ранее | `b396cfb4`, `786e34f8`, `5520318e`; исторические GPU-тесты F2, не игровой renderer |
| **F3.0–F3.3** — GPU-основа, GR Job System, CPU preparation | ✅ Standalone реализация и тесты по отчётам | `7ae6958d`, `09ca02357`, `5b0e4bbcb`; общий GR pool, Serial/Parallel/Verify, CPU snapshot; не FPS игры |
| **F3.5.0–F3.5.2B** — ADR, representative snapshot, visibility/compaction, ordering | ✅ Код закоммичен, CPU/ASan и regression PASS по отчётам | `8b1c0525`, `84de634e`, `d6592f97`, `3ac497d3`; порядок opaque/transparent/HUD относится к representative CPU-пути |
| **F3.5.3A/B** — sequential multi-list и parallel D3D12 command recording | ✅ Реализовано и проверено в standalone Smoke | `b5232622`, `53b82c5`; исторический H1 fence был **FAIL** для старой ревизии и позднее исправлен `d38ea6d8` |
| **F3.5.4A/B** — scaling/telemetry | ✅ Инструменты и 1–8 draw lists закоммичены; ⚠️ полный performance acceptance не заявлен | `de162d81`, `98a1e0fc`; p99 дополнен в Audit Hardening; синтетические замеры не являются FPS или доказательством ускорения |
| **F3.5.5A/B/C** — packet-driven GPU recording, batch planning, parallel packets | ✅ Реализовано для standalone | `aeb6a674`, `d02e2a44`, `6daa0b44`; игровой traversal по-прежнему не подключён |
| **F3.5.6A/B + Audit Hardening** — object CB, indexed geometry registry, fault/timing fixes | ✅ Код и независимый delta-review PASS; **CONDITIONAL PASS** для всего проверенного standalone HEAD | `0659303f`, `5b025aaa`, `738d143c`; M1/L1 закрыты, H1 исправлен; не тестировались все реальные driver failures/TDR, race detector и полный игровой runtime |
| **F3.5.6C** — material/PSO milestone | ◻ Отдельный PASS не подтверждён в просмотренной истории | Не подменять возможными минимальными ресурсами F3.6A; проверить нужный контракт перед полноценным F4 |
| **F3.6A — X-Ray Asset Bridge** | ✅ Закоммичен реальный standalone X-Ray OGF/DDS path; **не** игровой CoP renderer | `ccf0d22d`, `docs/dx12-f3-6a-xray-asset-bridge.md`; SoC static mesh, external loose files, textured fallback, без публикации игровых ресурсов |
| **F4.0-B1** — Jupiter geometry reader | ✅ CPU-only исходники закоммичены | `0ef5aaf2`; это декодирование данных уровня, не GPU игровой кадр |
| **F4.0-B2 / B2.3** — game camera snapshots и opt-in capture | ✅ CPU-only camera и opt-in интеграционный код закоммичены | `77e71e47`, `60f2030e`; наличие кода не доказывает полноценный runtime DX12 capture |
| **F4.0-B3A** — offscreen Jupiter validation | 🛠 Код standalone offscreen validation добавлен в Git; независимый GPU PASS этой сверкой не подтверждён | `4b120ca81`; отделять test harness от кадра игрового процесса |
| **F4.0 целиком** — реальный игровой DX12-кадр | ⬜ **НЕ ПРИНЯТ** | Нужны CoP/OGSR runtime, управляемая камера, видимая реальная геометрия и текстуры через DX12, depth/GBV, визуальный контроль и DX11 fallback |
| **V — Volumetric Clouds** | 🛠 Отдельная разработка: V0 audit PASS WITH NOTES по roadmap; V2.1 — checkpoint без визуальной приёмки, V2.2 — текущая задача по отчётам | Не считать V1/V2 полноценным PASS без результатов кода, GPU и независимого review; V не блокирует F4/G |
| **P — PhysX ragdoll** | 🔎 Архитектура и исследование ODE locomotion; **ragdoll P0–P2 не приняты** | Аудит ODE не равен интеграции PhysX; целевой первый NPC ragdoll в OGSR DX11/R4, ODE fallback |
| **G — Hybrid DXR** | ✅ Архитектурное направление принято, реализация DXR не подтверждена | `22875a0`; Control/Metro — ориентиры, не готовый RT pipeline |

**Границы:** работающий в Smoke DX12 путь, включая параллельную запись и
поддержку настоящего SoC mesh, **не тождественен** замене R4 в игре.
В F4.0 пока нельзя ставить PASS из-за наличия CPU readers, capture adapters
или standalone GPU harness. Не считать тестовые hashes и p50/p95/p99
доказательством стабильного игрового FPS. Legacy DX11/R4 должен сохраняться.

### Историческая сводка статусов (снимок 2026-10-14/19; актуальная сверка — выше)

| Этап | Статус | Следующее действие |
|---|---|---|
| A · DLSS 310.9.1 | ✅ Подтверждён: сборка и запуск CoP | Сохранить baseline и логи |
| B1 · NIS submodule | ✅ Подтверждён в исходном roadmap | Зафиксировать SHA и проверить CI на чистой машине |
| B2–B4 · NIS в движке | ⬜ Не реализован | Проверить compute-dispatch и ресурсы SDK |
| C · Тюнинг DLSS | ⬜ Не реализован | Качество и семантика флагов |
| D · DX11 SSGI / Hi-Z | ⬜ Исследование | D0 → D1 → D2, решение по результатам |
| E · Техдолг | 🔎 Диагностика | Отдельные согласованные изменения |
| F · Нативный DX12 | 🛠 Standalone F3.6A и частичные F4.0-B1/B2/B3A в Git; игровой F4.0 ещё не принят | Актуальная сверка 2026-10-25 выше; следующий гейт — реальный игровой DX12-кадр |
| F3.2 · GR Job System (DX12 only) | ✅ Коммиты и тесты standalone подтверждены по отчётам | Сохранить GR только для DX12; DX11/R4/physics не подключены |
| F3.5 · Multithreaded Renderer Foundation | ✅ F3.5.1–F3.5.6B в Git; standalone CONDITIONAL PASS по независимому аудиту | Не выдавать за игровой runtime; закрыть непроверенные hardware fault/race/perf гейты |
| F3.5.1 · Representative snapshot prototype | ✅ Закоммичен `84de634e` | Далее выполнены F3.5.2A/B: culling, compaction, draw ordering |
| G · DXR | 🆕 План, зависит от F | G0: проверка возможностей GPU/драйвера; архитектурный ориентир Control + Metro закреплён 2026-10-22, реализация не начата |
| H · Многопоточность | ⬜ План | H0: базовые профили, H1 после RT-теней и отражений |
| I · World Streaming | ⬜ Концепция | I0: аудит X-Ray ресурсов/уровней и ownership |
| J · Высота / LOD / растения / тени | ⬜ Концепция | J0: инвентаризация LOD/detail/CSM |
| K · Seamless World | ⬜ Возможность на базе I/J | K0: формат регионов и A-Life/AI constraints |
| L · DX12/DXR portability / IX-Ray | ⬜ Предложение | L0: лицензии, сравнение API и границ интеграции |
| P · PhysX ragdoll (OGSR DX11 → IX-Ray) | 🆕 Архитектура принята, реализация не начата | P0 → P1: аудит скелета и headless PhysX ragdoll; P2: один игровой NPC в OGSR DX11 |
| V · Volumetric Clouds (DX12, параллельно F4.0) | 🛠 V0 — независимый аудит PASS WITH NOTES; V1 — разработка, PASS не подтверждён | Закончить V1 ray marching и независимое ревью; V2 — качество; V-L молнии и V-S визуализация выброса отложены до V1/V2 PASS |

### Обязательные правила работы

1. **Не ломать рабочий DX11:** каждый эксперимент из F/G изолирован веткой/
   конфигурацией; первоначально DX11 остаётся основным путём и способом отката.
2. **Сборка ≠ работающий рендер:** отдельно фиксировать compile/link, запуск,
   кадр, переключение режимов, визуальные регрессии и производительность.
3. **Никаких оценок FPS без измерений:** сохранять сопоставимые сцены, разрешение,
   настройки, логи, GPU timings и скриншоты (желательно RenderDoc/Tracy).
4. **Текущие записи о выполнении не переносить автоматически:** статус меняется
   только при предъявлении проверяемого результата и коммита.
5. **Путь CoP:** `Game/Resources_CoP_1.6.02/gamedata/`; точные регистры
   имён файлов проверять по реальному дереву, а не по старым описаниям.

---

## Этап A — Обновить DLSS SDK 310.7.0 → 310.9.1

**Приоритет:** высокий · **Риск:** низкий · **Сложность:** дни
**Статус:** ✅ **выполнено 2026-10-14** — правка внесена, совместимость проверена,
сборка подтверждена.

### Что сделано

`Update_Components.cmd:26`:
```diff
-git clone --branch v310.7.0 --depth 1 ... 3rd_party/Src/NVIDIA_DLSS/DLSS
+git clone --branch v310.9.1 --depth 1 ... 3rd_party/Src/NVIDIA_DLSS/DLSS
```

### Проверка совместимости (по исходникам SDK, не предположение)

| Проверка | Результат |
|---|---|
| 42 API-символа `NVSDK_NGX_*` / `NGX_*` из движка в 310.9.1 | 42/42, отсутствующих 0 |
| `nvsdk_ngx_helpers.h` тянет `nvsdk_ngx_helpers_d3d.h` | да, `:116` |
| 4 `.lib` из `AdditionalLibraryDirectories` | все на месте |
| `rel/` + `dev/` DLL для post-build | обе на месте |
| `NVSDK_NGX_DLSS_Hint_Render_Preset_*` | без перенумерации |
| `NVSDK_NGX_PerfQuality_Value` | идентичен побайтно |

Preset L/M включатся автоматически через `DlssDllVersionAtLeast(310,5,0)`
(`RenderTargetPhaseAA.cpp:248`).

### Результат сборки

```
bin_x64\xrEngine.exe     31 463 424 Б (CoP)   валидный PE
bin_x64\nvngx_dlss.dll   310,9,1,0   NVIDIA Deep Learning SuperSampling
bin_x64\OpenAL32.dll
23 проекта, 1315+ объектов, ошибок компиляции — 0
```

### Рантайм-проверка CoP (подтверждено)

Билд установлен в `D:\Games\S.T.A.L.K.E.R. Call of Pripyat`; оригинальные
`xrEngine.exe` и `OpenAL32.dll` сохранены рядом как `.bak`.

| Проверка | Результат |
|---|---|
| `r_aa_dlss_version` | 310.9.1 |
| Апскейлинг Balanced | 1485x928 -> 2560x1600 |
| Пресеты DLSS | переключаются и применяются |
| FSR3 / TAA / SMAA | работают без падения |

### Что осталось

Косметическая ошибка A1 (`XR_3DA.vcxproj:395-397`, post-build `dbghelp.dll`).
EXE линкуется; падает только шаг копирования. Либо поставить
«Debugging Tools for Windows», либо пропатчить в форке (этап E).

### Откат

Вернуть одну строку и перезапустить `Update_Components.cmd`.

---

## Этап B — NVIDIA Image Scaling

**Приоритет (DX11-направление):** высокий · **Риск:** средний до первой
успешной проверки compute-шейдера · **Сложность:** недели
**Статус:** B1 ✅; B2–B4 ⬜.

### Зачем

NIS — официально поддерживаемый NVIDIA пространственный апскейлинг + шарпинг,
идущий **одним шейдером на DX11**. Для движка, у которого DLSS доступен
только на NVIDIA, это логичный fallback для AMD/Intel и старых карт.

`README.md` самого SDK: *«It contains compute shaders that can be integrated
with **DX11**, DX12, and Vulkan.»* — цитата проверена локально.

На момент диагностики submodule `NVIDIAImageScaling` (SHA
`35e13ba3…`) не инициализировался из-за отсутствия
`--recurse-submodules` в `Update_Components.cmd`.

**Статус B1:** ✅ **выполнено 2026-10-14 по исходному roadmap** — скрипт
поправлен, `NVIDIAImageScaling v1.0.3` активирован в рабочем дереве
(333 файла). **Осталось проверить воспроизводимость клона в чистом CI**;
локальная инициализация не доказывает её автоматически.

### B1. Починить скрипт — ✅ СДЕЛАНО

```diff
-  git clone --branch v310.9.1 --depth 1 https://github.com/NVIDIA/DLSS.git 3rd_party/Src/NVIDIA_DLSS/DLSS
+  git clone --branch v310.9.1 --depth 1 --recurse-submodules https://github.com/NVIDIA/DLSS.git 3rd_party/Src/NVIDIA_DLSS/DLSS
```

Активация сабмодуля в существующем дереве (без перекачки 2 ГБ):
```cmd
git -C 3rd_party\Src\NVIDIA_DLSS\DLSS submodule update --init --recursive
```

### B2. Compute-гейт и загрузка NIS HLSL

Текущее состояние: `.cs`-файлов в `gamedata` **ноль**, хотя
`ShaderTypeTraits<SCS>` (`ShaderResourceTraits.h:47-63`) умеет `cs_5_0`,
и `CResourceManager` имеет пул `m_cs` (`ResourceManager.h:39-49`).

1. **B2.0 — короткий тест:** создать минимальный compute-шейдер, реально
   выполнить `Dispatch`, записать предсказуемый результат в UAV и прочитать/
   показать его в ImGui/RenderDoc. Пройти путь компиляции и дискового кэша
   (`r4.cpp:823-869`) до конца. Без этого NIS не начинать.
2. Изучить `NIS_Main.hlsl`, `NIS_Scaler.h`, `NIS_Config.h` **ровно версии
   подключённого submodule**. Определить необходимые `NIS_*` defines,
   thread-group layout и поддерживаемые форматы.
3. Выбрать способ интеграции: исходный HLSL + рантайм-компиляция через
   действующий `CRender::shader_compile()` либо отдельно управляемый DXBC.
   **Не считать `.cs` в `gamedata` готовым исполняемым шейдером само по себе.**
4. Подключить shader-resource создание (`ShaderResourceTraits.h:137-144`),
   проверить путь `getShaderPath()` и очистку/инвалидацию shader cache.

> Это первый реально используемый compute-проход на данном пути рендера.
> Конкретный вызов создания (`s_nis.create` или отдельный compute handle)
> выбирать по фактическому API ResourceManager после B2.0.

### B3. Обёртка и ресурсы

По месту вызова ориентироваться на `PhaseAA()` и `Fsr3Wrapper`
(`RenderTargetPhaseAA.cpp:685-851`), **но не копировать temporal-контракт FSR3**:

```
Вход:  rt_Generic_0 (render-res, SRV)
NIS:   config/constants + коэффициенты фильтра (SRV) + sampler
Выход: rt_Generic_combine (display-res, UAV)
Далее: BeginPostprocess(..., temporalOutput=true) / проверенный путь resolve
```

- Подать корректные коэффициенты scaler/USM и constant buffer по
  `NIS_Config.h`; задать `NIS_HDR_MODE` в соответствии с фактическим
  цветовым пространством `rt_Generic_0`.
- Проверить формат текстур, bind flags, SRV/UAV, sampler, размер dispatch,
  выход за границы viewport, отсутствие конфликтов одновременных RTV/UAV/SRV.
- NIS пространственный: история, jitter и `RequestHistoryReset` для самого
  NIS не требуются. CAS/другой sharpen не применять дважды.
- **Не изменять автоматически текущую цепочку отказа DLSS → FSR3 → TAA/SMAA.**
  Сначала ввести NIS как явный режим и сравнить качество/скорость. Порядок
  fallback менять только отдельным решением после тестов.
- При смене render-res обеспечить пересоздание зависимых RT или штатный
  `vid_restart`; просто вызов `u_setrt` проблему resize не решает.

### B4. Интерфейс

Добавить в `pp_aa_mode_token` (`xrRender_console.cpp:37-47`) и `enum`
(`xrRender_console.h:264-272`) значение `NIS`.

**Обязательно синхронизировать Lua-UI** —
`Game/Resources_CoP_1.6.02/gamedata/scripts/ui_mm_opt_video_adv.script`:
строки `:27-29` объявляют `dlss_quality` / `dlss_preset` / `fsr3_quality`;
строка `:174` читает `r_aa_mode`. Это **реальный путь в GitHub**
(нижний регистр `scripts`), старое написание `Scripts/` было ошибкой документации.
Новый режим нужно поддержать как в C++ токенах, так и в Lua-UI.

### Критерии приёмки

- B2.0: минимальный `Dispatch` корректно пишет в UAV; RenderDoc/ImGui показывает ожидаемые пиксели.
- `r_aa_mode <зарегистрированный токен NIS>` даёт корректный полный кадр
  на native и на пониженном render-res; CPU/GPU validation не сообщает ошибок.
- Работает без DLSS на совместимой DX11-видеокарте; NIS не должен требовать RT cores.
- Переключение DLSS/FSR3/TAA/SMAA/NIS и resize не приводят к падениям,
  чёрным кадрам, использованию устаревших ресурсов или неправильному aspect ratio.
  `vid_restart` допустим, **если это осознанный контракт UI**; без рестарта
  применять режим только после явной реализации пересоздания ресурсов.
- В Lua-меню правильные доступность/подсказка/качество; сохранённые настройки
  не меняют смысл. Нет двойного CAS/sharpen; HDR-яркость и альфа не ломаются.
- Сравнение NIS/FSR3/DLSS в одинаковых сценах: скриншоты, резкость,
  мерцание травы, задержки GPU (Tracy/RenderDoc), обоснованный выбор fallback.

### Риски

| Риск | Оценка | Митигация |
|---|---|---|
| Тег NIS/повторяемость CI | Средняя | Закрепить проверенный submodule SHA и выполнить clean checkout |
| Первый `.cs` в проекте — неисследованный путь | Средняя | B2.0: отдельный тест `Dispatch` + UAV + RenderDoc |
| HDR/linear/PQ и коэффициенты NIS | Средняя | Следовать версии `NIS_Config.h`, визуальный тест |
| Двойной sharpen с CAS | Средняя | Не включать CAS поверх NIS без явного выбора |
| Смена render/display размеров | Средняя | Рестарт/пересоздание RT по контракту |
| Меню опций рассинхронизируется | Низкая | Правка `ui_mm_opt_video_adv.script` в том же коммите |

---

## Этап C — Тюнинг текущего DLSS

**Приоритет:** средний · **Риск:** низкий · **Сложность:** дни

### C1. `AlphaUpscaling`

`RenderTargetPhaseAA.cpp:341`. Проверить, поддерживается ли флаг выбранной
версией DLSS и соответствует ли он реальной семантике alpha на входе.
**Не включать вслепую:** сравнить траву, дым, дождь и UI до/после.

### C2. `MVJittered`

`:337`. Наличие camera jitter (`:418-419`) само по себе **не доказывает**,
что motion vectors передаются jittered. Сначала сверить генерацию `rt_Velocity`,
знаки `InMVScale*` и контракт NGX, затем решить вопрос с флагом.

### C3. `DoSharpening` — требует решения

`:332`. Без него `InSharpness` игнорируется (`:316-317`), но движок
имеет собственный CAS (`ProcessCAS`, `:611`), вызываемый в `PhaseAA()`
для всех режимов кроме SMAA (`:966-967`).

Варианты: оставить внешний CAS; использовать DLSS sharpening, если режим
поддерживается активной версией SDK; либо предоставить явный выбор
в настройках. **Критерий:** одна активная стадия sharpening, а не
предположение, что включённый флаг обязательно улучшает картинку.

### C4. Задокументировать DLAA как дефолт

`xrRender_console.cpp:58`. Пользователь может не понимать, почему
при «DLSS» нет апскейлинга. Отражено в `ui_mm_opt_video_adv.script`.

### Критерии приёмки C

Флаги проверены по контракту соответствующего SDK; видимые различия
сохранены скриншотами в статике и движении, артефакты не усилились,
производительность не ухудшилась непредсказуемо. Изменения независимы
и могут быть откатаны по одному.

---

## Этап D — SSGI / Hi-Z на compute-шейдерах

**Приоритет:** высокий по ценности, **высокий** по сложности · **Месяцы**

**Это один из путей получить приближение к трассируемому освещению
в DirectX 11.** Hi-Z/DDA — **screen-space ray marching** с ограничениями
вне экрана и за заслоняющими поверхностями; это **не DXR** и не замена
аппаратным acceleration structures. Hardware RT в выбранной D3D-архитектуре
требует перехода на DX12/DXR (направление F–G).

### Что уже есть

| Актив | Где |
|---|---|
| Компиляция `cs_5_0` | `ShaderResourceTraits.h:47-63` (`:54`) |
| Пул CS-шейдеров | `ResourceManager.h:39-49` |
| UAV-таргеты | `r4_rendertarget.cpp:287-292` |
| **Нормали** | `rt_Position.xy` — закодированы, см. `common_iostructs.h:287-293` |
| **Gloss → roughness** | `rt_Color.a` |
| **Material ID** | `rt_Position.w` (закодирован с hemi) |
| Отладка в движке | `r4_imgui.cpp:75-81` — `DrawTexture` показывает `rt_Color`, `rt_Position`, `rt_heat`, `rt_Generic_1/2`, `rt_Bloom_1` |
| Тайминг-инструменты | Tracy с `PIX_EVENT` |

### Чего ещё нет

- Ни одного `.cs`-файла (блокер; снимается этапом B).
- Metallic/emissive в 4 MRT.
- Hi-Z пирамиды.
- Равномерного доступа к треугольникам.
- Денойзера.

### Подэтапы

| # | Подэтап | Содержание | Видимый результат |
|---|---|---|---|
| D0 | Compute-гейт | Минимальный `.cs` + UAV (можно использовать результат B2.0) | Работающий `Dispatch` и запись результата |
| D1 | Hi-Z | Построить и визуализировать mip-пирамиду глубины; выбрать доступный для compute формат и преобразование depth | Корректные mip-уровни в ImGui |
| D2 | Screen-space tracing | Проверить декодирование normal/depth, reconstruction позиции, DDA/Hi-Z и критерии hit/miss | Отладочная маска попаданий, предсказуемые экранные ограничения |
| D3 | SSGI lighting | Применить screen-space hit к освещению, учитывать emissive отдельным входом при необходимости | Измеримый непрямой свет без явных GPU-ошибок |
| D4 | Temporal/filter | Reprojection по motion vectors, rejection, spatial denoise, camera-cut reset | Стабильность в движении, контролируемый ghosting |
| D5 | Интеграция | Добавить независимый runtime-флаг, debug modes, профилирование и возврат к исходному освещению | Toggle в консоли без порчи baseline |
| D6 (опц.) | Off-screen geometry | Отдельно оценить программную структуру геометрии (BVH/SDF/voxel) | **Отдельное решение:** это уже не обычная Hi-Z SSGI, не делать без замеров |

**Первый шаг:** найти используемые шейдерами функции распаковки G-buffer,
переиспользовать их в compute-проходе, сверить преобразования координат
и `rt_Position.xy/.z/.w`. Наличие закодированной normal **не означает**,
что весь вход для GI уже подготовлен.

### Критерий приёмки

Для D0–D2: воспроизводимая маска попаданий на нескольких сценах,
сопоставление с depth и нормалями, известные ограничения на краях экрана.
Для D3–D5: видимое улучшение непрямого освещения без заметного ghosting,
управляемая стоимость в GPU ms и без регрессии исходного DX11 при выключенном
эффекте. Любой шаг может завершиться решением **не продолжать**.

### Риски

| Риск | Оценка |
|---|---|
| Hi-Z требует корректного доступа к глубине и форматов compute/UAV | Средняя |
| Screen-space не видит невидимую геометрию | Высокая, алгоритмический предел |
| Off-screen BVH/гриды (D6) — отдельный большой проект | Высокая |
| Temporal/reprojection, leakage/ghosting | Высокая |
| Reconstruction позиции/нормали и coordinate spaces | Средняя |

### Рекомендация

Начинать только с D0–D2. Если Hi-Z + DDA не покажут полезного качества и
приемлемого GPU time — остановиться. Для достижения DXR **не требуется**
завершать D3–D6. Часть screen-space наработок пригодится позднее, но
программный BVH не является обязательной ступенью перед BLAS/TLAS.

---

## Этап E — Гигиена репозитория

**По договорённости — только диагностика, без правок.**

| Пункт | Файл | Что предложить |
|---|---|---|
| Закрепить коммиты | `Update_Components.cmd` | Заменить 12 веток на SHA |
| Проверить `--recurse-submodules` на чистом CI | `Update_Components.cmd:26` | **B1 уже выполнен локально**; проверить воспроизводимость |
| Условный `dbghelp.dll` | `XR_3DA.vcxproj:395-397` | `IgnoreExitCode="true"` или `Condition` на `Exists(...)` |
| `ReleaseTracyProfiler` как настоящая конфигурация | `Engine.sln` | Добавить в SolutionConfigurationPlatforms |
| Поправить устаревший комментарий | `build_config_defines.h:63-64` | Сказать 128, а не 768 |
| Убрать мёртвый код | `xrRender/*`, `xrPhysics.cpp`, `lua_studio.cpp`, `PHSynchronize.cpp` | см. §B |
| Защитить `Game/` | `.gitignore` | Добавить `Game/Resources_CoP_1.6.02/bin_x64/` |
| Проверить DirectPlay 8 на поддерживаемых Windows | `xrNetServer` / запуск CoP | Документировать runtime-требования; **не копировать системные DLL вслепую** |
| dependabot на зависимости | `.github/dependabot.yml` | изучить ограничения для сторонних git clone |
| Исправить пути `Configs/`/`Scripts/` в документации | `docs/*`, `Game/` | сверить с реальным деревом: `gamedata/scripts` |

---

## Этап F — Нативный DX12 (standalone реализован; игровой DX12 ещё не принят)

**Цель:** запустить CoP с настоящим D3D12-backend и сохранить рабочий DX11.
**Приоритет:** стратегический для DXR · **Риск:** высокий · **Оценка срока:**
только после F0–F2, без обещания «готово за N месяцев».

**Важное архитектурное наблюдение:** `xrRender_R4` собирает файлы из
`xrRenderPC_R4`, `xrRender` и `xrRenderDX10` **в одну статическую библиотеку**;
`RImplementation`, `HW`, `CBackend` и ресурсные классы сильно привязаны к D3D11.
`RenderFactory` — фабрика игровых/render-объектов, **не готовый RHI**.
Решение «новый `xrRender_R5`» принимать **после F0**, не заранее.

| Подэтап | Работа | Гейт / проверяемый результат |
|---|---|---|
| **F0 — аудит** | Карта зависимостей `CHW`, `CBackend`, `CResourceManager`, `CRT`, `CTexture`, `dx10StateManager`, `CRender`, `dxRenderDeviceRender`, оконного цикла и статической линковки. Инвентаризация HLSL, 64 RT, deferred/forward/HUD/particles, DX11 SDK-обёрток. | `docs/dx12-port-audit.md`: таблица классов, lifetime, владельцев и D3D11-вызовов; предлагаемая граница backend; сравнение альтернатив по риску. **Никаких переписываний.** |
| **F1 — проектирование** | Выбрать: отдельная build-конфигурация / временный DX12 harness / поэтапный API separation. Описать интерфейсы device, swapchain, command recording, shader & resources и правила совместного сосуществования. | ADR с явными правилами ABI, ownership, переключения/отката, статической линковки. DX11 baseline собирается без изменений поведения. |
| **F2 — DX12 bootstrap** | D3D12 device, DXGI swapchain, graphics queue, allocators/command list, frames-in-flight, fences, RTV/DSV/SRV/UAV descriptor heaps, resize, debug layer, DRED. | Независимый DX12-путь стабильно выполняет `Clear → Present`, корректно переживает resize/minimize, без validation errors. **Это ещё не игровой рендер.** |
| **F3 — ресурсы и геометрия** | Upload/default heaps, constant/vertex/index buffers, PSO, root signatures, descriptors, resource transitions/barriers, простые текстуры. Раздельно исследовать D3D11-legacy shader state и `D3DVERTEXELEMENT9`. | Текстурированный mesh из реального формата OGSR, depth test, camera movement, без leaks и GPU faults. |
| **F3.5 — multithreaded renderer foundation** | До массового переноса кадра: минимальные immutable render snapshots/draw packets, дизайн frame scheduling, per-worker/per-frame allocators и lifetime; небольшой тестовый parallel command recording path с последовательным fallback. Детали и гейты ниже. | Повторяемые тесты корректности на 1/2/4/8 workers, CPU/GPU timeline, p95/p99, sync overhead; нет data races, GPU errors, регрессий DX11. Сложную job system не внедрять без измерений. |
| **F4.0 — первый игровой DX12 кадр (ранний vertical slice)** | После F3.5.6A–C — ограниченная интеграция реальных ассетов и камеры CoP в игровой DX12-путь, без ожидания полного F4. Сначала геометрия и depth, затем базовые текстуры/материалы и навигация. | Открывается реальная локация OGSR (целевая — Кордон, допустимая альтернатива — Припять) именно через DX12; есть видимые объекты, управляемая камера, depth и базовые текстуры; runtime/debug/GPU validation и DX11 fallback проверены. Не объявлять весь F4 завершённым. |
| **F4 — игровой кадр** | Перенос/de-risk основных проходов: sky, G-buffer, CSM/sun, dynamic lights, прозрачность, HUD/оружие, particles, postprocessing. Сохранение нормалей `rt_Position.xy`, gloss `rt_Color.a`, motion vectors, view depth/material ID. | Одинаковая тестовая локация реально загружается и рендерится на DX12 и DX11. Скриншоты ключевых буферов и визуальный diff. |
| **F5 — temporal и совместимость** | DX12-обёртки для выбранных DLSS/FSR, jitter, history reset, upscale input/output, прицел 3DSS, ImGui, Tracy/PIX/RenderDoc, HDR/ReShade там, где поддерживается. | DLSS/FSR fallback без device loss, нет ошибок resize/async lifetime и серьёзного ghosting. |
| **F6 — стабилизация** | Измеренная настройка/расширение прототипа parallel command recording из F3.5, memory budgeting/residency, CPU/GPU profiling, regression suite. | Без известных GPU validation errors, сравнение GPU ms/CPU ms/VRAM на нескольких сценах, контролируемый rollback DX11. |

**Статус подэтапов F на 2026-10-15** (ветка `prypiat`, HEAD `2f6ca66da`):

| Подэтап | Статус | Комментарий |
|---|---|---|
| F0 — аудит | ✅ PASS | `docs/dx12-port-audit.md` |
| F1 — проектирование | ✅ PASS | `docs/dx12-architecture-adr.md` |
| F2 — bootstrap | ✅ PASS | F2.0 scaffold и F2.1-B собираются с 0 ошибок. GPU runtime подтверждён: Debug Layer + GBV + DRED активны, 6 036 ресайзов суммарно с 0 validation errors и 0 провалов, shutdown корректен, утечек нет (`ReportLiveObjects` через Microsoft DebugView v5.02, разбор владельцев). Documented limitations: обработка device removal — NOT TESTED (провокация TDR запрещена); полноценный игровой DX12 renderer отсутствует, есть только standalone bootstrap. CI для DX12 добавлен отдельным job `dx12-smoke-build` (build-only, без GPU runtime). Детали — `docs/dx12-f2-first-frame.md`, разделы F2.1-C.1, F2.1-C.2 и F2.1 Finalization |
| F3 — ресурсы и геометрия | ⬜ NOT STARTED (снимок 2026-10-15) | Предложение по F3.0 — в `docs/dx12-f2-first-frame.md`; текущий прогресс проверять отдельно |
| F3.5 — multithreaded foundation | ⬜ PLANNED (2026-10-16) | После подтверждения F3.1 / достаточного минимального 3D-пути; архитектурный прототип до F4 |
| F4 — полный игровой renderer | ⬜ Не принят; F4.0-B1/B2/B3A — частичные подготовительные шаги | Реальный кадр в CoP на DX12 и затем полноценные raster-проходы |
| F5 — temporal | ⬜ NOT STARTED | — |
| F6 — стабилизация | ⬜ NOT STARTED | — |

**Ближайшая работа по историческому снимку:** F3.0 — минимальный рендер 3D-геометрии. Если с тех пор выполнены F3.x GPU-гейты, свериться с актуальными коммитами/логами перед планированием F3.5. Новая запись не переоценивает уже выполненное.


### F3.2 — GR Job System: сначала только DX12 (решение 2026-10-18)

**Архитектурный статус:** GR Job System — самостоятельная библиотека уровня Engine Core
(`ogsr_engine/Layers/JobSystem/`), а не часть `xrRender_R5_DX12`. Backend — закреплённый
форк enkiTS (SHA `404a3bf8f855039dfff2052184d6308655286c07`); публичный
`gr/jobs/` API не зависит от enkiTS, DX11, DX12 и Win32. Принцип — один общий
планировщик CPU-задач и Work Stealing; размещение потоков оставлено Windows
(**OS Default**, без hard/soft CPU affinity в runtime). Количество workers
конфигурируется, а последовательный путь сохраняется для отката и измерений.

**Решение об области работ:** на текущем этапе **только собственный DX12 renderer /
standalone `xrDX12Smoke.exe`** становится потребителем GR Job System.
Физика, AI, анимация, streaming и фоновые задачи аудио **не интегрируются сейчас**:
позднее, после измерений DX12 и отдельного аудита зависимостей, в направлении H.
Специализированные realtime-потоки (особенно audio callback) не обязаны
использовать общий пул; запрещено блокировать realtime audio callback ожиданием job.

| Подэтап | Статус на 2026-10-18 по локальным отчётам, без независимого GPU runtime PASS | Следующий гейт |
|---|---|---|
| **F3.2A / A.1 — enkiTS bench + OS scheduler** | Отчёт: 10 раундов, 480 конфигураций; принято **OS Default**, хотя hard affinity дала локальный выигрыш в синтетике | Хранить raw timings; не выдавать синтетический speedup за FPS |
| **F3.2B / B.1 — GR Job System Foundation + Safety Review** | По отчёту агента: автономная библиотека и исправления noexcept destructor, move-assignment, lifetime; требуются проверки интегрируемой версии | Убедиться в корректном ownership, исключениях, shutdown и регрессионных тестах |
| **F3.2C Stage 1 — VS integration** | По отчёту Codex: `xrDX12Smoke → gr_job_system → enkiTS`, ReleaseDX12/x64 без ошибок/предупреждений; окно не запускалось; изменения локальные, не закоммичены | Воспроизводимая сборка из репозитория; не подключать `xrRender_R5_DX12` и другие игровые targets раньше отдельного этапа |
| **F3.2C.1 — ownership/lifecycle hardening** | Требует отдельной проверки: потенциальное освобождение опубликованной задачи при исключении из `tasks.push_back`, а также hook/drain/shutdown | Устранить подтверждённые дефекты, fault-injection и регрессионные тесты |
| **F3.2C.2 — DX12 Smoke CPU scene pipeline** | **PASS**, коммит `09ca02357`: 32/32 runtime, 857 CPU checks, depth readback, resize и GPU validation; ручной визуальный осмотр и race detector не выполнены | Выполнить ручную проверку окна перед полной визуальной приёмкой |
| **F3.3 — DX12 CPU render preparation** | **PASS**, коммиты `09ca02357` + `5b0e4bbcb` (F3.3.0 и F3.3.0a): immutable snapshot, prepared packets, Serial/Parallel/Verify, WM_TIMER fault handling; merge с `origin/prypiat` — `99c9d1a10` | F3.5.0 ADR принят → F3.5.1 |
| **F3.4 — DX12 parallel command recording** | Запланировано; согласовать с существующим разделом F3.5.3, не дублировать работу | Per-worker command lists/allocators, GPU fence lifetime, barriers, validation, profiling |
| **F3.5 — DX11 vs DX12 comparison / решение** | Запланировано; согласовать с существующим F3.5.4 (не переименовывать старый этап) | Одинаковые сцены и настройки, CPU/GPU timings, p50/p95/p99, frametime, 1% low; отделить различия backend от различий качества |

**F3.2C.2 — измерения в standalone Smoke (4 background workers + main, grain=64):**

| CPU-объектов | Serial, мс | Parallel, мс | Serial / Parallel |
|---:|---:|---:|---:|
| 256 | 0.024141 | 0.037532 | 0.64× (parallel медленнее) |
| 1024 | 0.114042 | 0.059029 | 1.93× |
| 4096 | 0.383540 | 0.136484 | 2.81× |
| 16384 | 1.301304 | 0.421946 | 3.08× |

Источник — локальный `docs/dx12-f3-2c2-runtime.md` (30 прогревочных и
200 измеряемых кадров; Serial/Parallel verification, 32 automated runs).
Числа измеряют **только CPU scene preparation** на RTX 5070 Laptop GPU,
не FPS и не реальный игровой scene renderer. Serial остаётся default.
F3.2C.2: runtime и автоматическая корректность по отчёту PASS; ручной осмотр
окна, интерактивный minimize/restore и специализированный race detector
остаются открытыми. Не заявлять завершения этих проверок.

**Следующий этап F3.3:** сначала аудит фактических CPU scene/render preparation
и их владельцев в `xrRender_R5_DX12`, анализ frame lifetime и точек
синхронизации; затем отдельный минимальный serial/parallel path с проверкой
эквивалентности. Не переносить синтетические объекты в игру как «готовый
рендерер», не менять DX11 и другие подсистемы; GPU command list recording
оставить на более поздний гейт F3.5.3.

**Последовательность:** закрыть корректность и lifecycle → подключить GR к
standalone DX12 Smoke → подготовка CPU сцены → реальные CPU-задачи DX12 →
параллельная запись D3D12 command lists только после отдельных GPU lifetime
инвариантов → сравнение с сохранённым DX11 baseline. DX11, gameplay, physics,
AI и audio сейчас не менять. Сборка **не равна** запуску или GPU validation PASS.

**Важно о нумерации:** существующий ниже F3.5 — общий архитектурный рубеж
Multithreaded Renderer Foundation до F4. Ссылки F3.3/F3.4/F3.5 в таблице выше
описывают предложенные рабочие итерации GR/DX12 внутри этого рубежа; они **не
отменяют и не перенумеровывают** F3.5.0–F3.5.4 и требуют синхронизации перед
обновлением основных milestones.

---

### F3.5 — Multithreaded Renderer Foundation: архитектура ДО массового переноса F4

**Мотивация:** D3D12 позволяет записывать независимые command lists с нескольких CPU threads, но это не делает устаревший frame loop X-Ray автоматически многопоточным. После F3 достаточно рабочего 3D-пути, чтобы проверить модель владения состоянием; откладывание этих границ до H создаёт риск переписывать F4. **F3.5 — архитектурный минимальный прототип, не обещание прироста FPS и не переписывание simulation scheduler.**

| Подэтап | Работа | Обязательный гейт |
|---|---|---|
| **F3.5.0 — architecture ADR + trace** | Карта main/render threads, кадра, точек синхронизации, потоков GPU и владельцев данных; выбрать границы immutable frame snapshot, draw packets, PSO/material handles, queue submission и fallback. Без предположения, что готовый X-Ray renderer thread-safe. | ✅ ADR принят: `docs/dx12-f3-5-architecture-adr.md` (frame N/N+1, потоковая модель, fencing/lifetime invariants, список опасных mutable pointers, синхронизация с TTAPI, render-owned `(id, generation)`). Реализация не начата. |
| **F3.5.1 — immutable snapshot** | На согласованной границе кадра после update собрать компактный read-only RenderSceneSnapshot (transform/bounds/material/resource references); без записи игровых сущностей из render workers. Owner и lifetime сохраняются до последнего потребителя, ограничить frames-in-flight и backpressure. | Snapshot непротиворечив; проверены изменения/удаления объектов и быстрое движение камеры без dangling references/data races. |
| **F3.5.2 — work decomposition** | Минимальная независимая подготовка visibility/culling, sort/draw packets из snapshot; фиксированный порядок там, где нужна прозрачность или зависимости; исключить global mutex per draw. Мелкие сцены допускают последовательный путь. | Детерминированная либо визуально эквивалентная сборка тестовой сцены в serial и worker режимах, без нарушения порядка проходов. |
| **F3.5.3 — D3D12 multi-list prototype** | Per-thread/per-frame command allocators & lists, per-frame descriptor/upload allocation и synchronization; reuse/reset лишь после завершения fence; command list ownership на одном worker во время записи, упорядоченный submission на direct queue. Ввести явные правила ресурсов/barriers (межлистовые зависимости согласуются при merge/submission). | Тестовые сцены без GPU validation, race, lifetime и barrier errors на 1/2/4/8 workers; resize/shutdown и быстрая смена сцены безопасны. |
| **F3.5.4 — profiling & decision** | Одинаковые сцены/разрешения/сборки; CPU frame time отдельно от GPU time, render-thread time, p50/p95/p99 frametime, количество draw calls, lock/contention, worker overhead и scaling 1/2/4/8. Проверить CPU-bound и GPU-bound сцены. | Таблица результатов, reproducible capture/коммит, выбор разумного default (включая serial), чёткое решение: какой parallel path переносить в F4, что оставить F6/H. |

**Обязательные инварианты:**

- Драйвер/GPU **не гарантируют** ускорения из-за большего числа workers: важны профиль CPU bottleneck и overhead. Переключатель на 1 worker / последовательный путь обязателен для сравнений и диагностики.
- В каждом записывающем потоке — собственный command allocator/list, независимое CPU-владение; GPU fence защищает **повторное использование allocators**, transient upload/descriptor и уничтожение объектов после завершения GPU. Graphics queue submit может оставаться сериализованным.
- Snapshot и draw packets **не являются** самостоятельной копией всего игрового мира; simulation/A-Life/physics не переносятся на workers в F3.5. Разделить lifetime игровых сущностей и стабильных render handles.
- Алгоритмы ресурсных переходов не должны допускать недетерминированные barriers при параллельной записи. Минимальный central pass/resource dependency tracker достаточен; полноценный render graph возможен лишь при подтверждённой необходимости.
- Гарантировать безопасную обработку отказов, остановку worker pool и shutdown, когда кадры ещё in-flight; минимизировать блокировки, но не заменять корректную синхронизацию lock-free оптимизациями по инерции.
- Сохранить существующий DX11 путь неизменным. Новая реализация не может объявляться PASS, пока не пройдены code-review, build, runtime/debug layer, GPU и performance гейты.
- Интеграция с World Streaming I/J в будущем должна передавать worker задачам подготовленные **resource/scene handles**, а не сырой mutable game state; F3.5 не включает реализацию I/J.

**Порядок:** F3 минимальная 3D геометрия/depth/materials → F3.5.0–F3.5.4 → перенос игрового кадра F4 с уже определёнными безопасными границами. При необходимости F4 может стартовать с serial recording поверх подготовленного интерфейса и включать рабочий parallel path постепенно. G0/G1 лабораторные исследования не обязаны ждать полного performance tuning F3.5; полноценная интеграция DXR в игру — после F4.

**Исторический статус на 2026-10-19 (заменён актуальной сверкой 2026-10-25 выше):** ✅ **F3.5.0 — ADR ПРИНЯТ** (`docs/dx12-f3-5-architecture-adr.md`), реализация не начата. ⏭ **F3.5.1 — NEXT / NOT STARTED**: это **representative snapshot prototype** (раздел 9 ADR), а не интеграция реальных игровых объектов — реальный обход сцены остаётся за F4. Пункт не подтверждает реализацию snapshot или multi-list renderer.

**Ограничения, сохраняемые без изменений:** ручной визуальный осмотр окна, интерактивный minimize/restore и специализированный race detector (TSan) **не выполнены**; отсутствие гонок не доказано. Синтетические замеры F3.2C.2 — не FPS и не игровой рендер.

### F4.0 — ранний вертикальный срез: первый игровой кадр на DX12 (PLANNED)

**Новая приоритетная цель после принятия F3.5.6A–C:** как можно раньше
увидеть реальную локацию **S.T.A.L.K.E.R.: Call of Pripyat / OGSR**
в нашем нативном DX12-рендерере. F4.0 — промежуточный, строго ограниченный
гейт **до полного F4**, а не обещание завершённого движка или FPS-прироста.

**Порядок работ и проверяемые результаты:**

1. **F4.0-A — integration audit (READ-ONLY).** Проследить путь от игрового
   level loader, renderable/visual и камеры до R4 traversal и frame submit;
   составить карту форматов реальной геометрии/текстур, ownership, shader
   contracts и точек включения DX12. Решить, как подключить экспериментальный
   путь без подмены DX11 или копирования mutable игрового состояния в workers.
2. **F4.0-B — first real geometry frame.** В контексте запущенной игры
   загрузить и нарисовать на DX12 реальную статическую геометрию выбранного
   уровня OGSR, согласовать игровую камеру, World/View/Projection,
   depth test и базовый raster pass. На этом промежуточном гейте временный
   плоский цвет допустим, **но синтетическая сцена в xrDX12Smoke не считается
   игровым кадром**.
3. **F4.0-C — textured navigable preview.** Добавить минимально необходимый
   путь существующих OGSR текстур и материалов (включая документированную
   цветовую/sRGB-политику), базовый sampler/descriptor binding и управление
   камерой в уровне. Без обязательной конвертации всего набора игровых ассетов.
   Unsupported материалы временно показывать диагностически, а не выдавать
   за корректно отрендеренные.
4. **F4.0-D — evidence and rollback gate.** Проверить DX12 draw/dispatch
   действительно в игровом процессе (command captures / GPU markers),
   корректность геометрии, depth и текстур; записать скриншоты/контрольные
   точки, ошибки Debug Layer/GBV, resize/reload/shutdown, используемые
   ресурсы и список неподдерживаемых проходов. Сохранить рабочий DX11/R4
   и проверяемый переключатель/откат экспериментального DX12.

**Критерий F4.0 PASS:** игра загружает реальную локацию через существующий
игровой data path, камера движется, реальные mesh и базовые текстуры
отрисовываются **через D3D12**, depth работает, нет известных GPU
validation errors, DX11 baseline не сломан. Первый геометрический кадр
F4.0-B можно принимать как частичный milestone, но **не как весь F4.0**.

**Зависимости:** завершить и принять F3.5.6A (object transforms),
F3.5.6B (geometry handles/VB/IB), F3.5.6C (material/PSO state) либо
документировать минимальный эквивалент и причину отклонения. Не блокировать
F4.0 ожиданием полного F3.5.7/F3.5.8: нужный для preview минимальный
texture/SRV/sampler path входит в F4.0-C; развитую texture/material
совместимость вести отдельными этапами. Реальный integration audit может
выявить дополнительные обязательные зависимости, которые нужно явно
согласовать до кодирования.

**Вне F4.0:** sky/полное освещение/CSM, прозрачность, HUD/оружие,
particles, сложный postprocess, temporal upscalers, RT-тени/отражения/GI,
streaming, оптимизация FPS. Их перенос остаётся в полном F4/F5/F6/G.
До прохождения полноценных гейтов F4–F5 DX12 не выдавать пользователям
как штатный завершённый игровой режим.

### Принципы F

- Не заменять существующий `CHW` на D3D12 в одном большом коммите и не
  подменять `ID3D11DeviceContext` ссылками на command list «по названию»:
  **D3D12 требует явной синхронизации, resource barriers, descriptors и PSO**.
- Не переносить весь HLSL на SM6 одним шагом. Сначала исследовать
  совместимость существующих raster shaders с DX12, выделить DXC/SM6
  как отдельную задачу для DXR и новых функций.
- Не требовать появления универсального render graph на F0. При необходимости
  добавить минимальный state/lifetime tracker, обоснованный профилями.
- **До массового переноса F4** завершить минимальный архитектурный гейт F3.5:
  immutable snapshot, ownership/lifetime и проверенный вариант multi-list CPU
  recording. Параллельность по умолчанию включать только после профилирования;
- Не добавлять поддержку DX12 в пользовательские опции до реального
  прохождения F4–F5; экспериментальный режим переключать через сборку/флаг.
- F0 можно выполнять **параллельно** B2/D0; F3+ — только после F2.

### Условия остановки F

Если аудит F0 выявит необходимость необоснованно переписать системный код,
либо на F2/F3 не удаётся обеспечить стабильную синхронизацию, сначала
пересмотреть границу backend и технические требования; не переносить дальше
всё подряд по инерции.

---

## Этап G — Аппаратный DXR (новое направление; зависит от F)

**Цель:** реальный hardware ray tracing, начиная с **одной ограниченной
функции**. Не путать с DX11 DDA/SSGI, не обещать GI/reflections до
стабильного RT prototype.

| Подэтап | Работа | Гейт / результат |
|---|---|---|
| **G0 — capabilities** | На DX12-устройстве запросить `D3D12_FEATURE_D3D12_OPTIONS5` / `RaytracingTier`; проверить `ID3D12Device5` / `ID3D12GraphicsCommandList4` и fallback на GPU без DXR. | Диагностика точного tier и понятное отключение RT при отсутствии поддержки. |
| **G1 — геометрия для RT** | Сопоставить реальный поток vertex/index с RT geometry. Построить BLAS для **небольшого статического** набора, TLAS для инстансов. Проверить ресурсы, lifetime, scratch buffers, transforms. | Видимая тестовая сцена с валидными BLAS/TLAS; RenderDoc/PIX & validation. |
| **G2 — минимальный трассировочный проход** | DXC/SM6, raygen/miss/closest-hit, shader tables/RTPSO, `DispatchRays` в простую UAV-маску. | Предсказуемые hits/misses и стабильная работа без серьёзных ошибок. |
| **G3 — RT-тени** | Начать с одного света/ограниченной геометрии, интеграция с существующим deferred lighting. Временная/пространственная фильтрация при необходимости. | Включаемые RT-тени рядом с текущим CSM, сопоставимые кадры и GPU ms. |
| **G4 — динамика и материалы** | Skinned/dynamic objects, обновление/rebuild BLAS/TLAS, alpha-tested foliage, invalidation, emissive и material lookup. | Нет существенных артефактов при анимации NPC/растительности и движении камеры. |
| **G5 — дополнительные эффекты (опц.)** | Только после G3/G4: **RT-отражения как отдельный проверяемый рубеж**, затем по отдельному решению diffuse GI и denoise; DLSS Ray Reconstruction — отдельная интеграция при проверенной поддержке SDK/API. | Для RT-отражений отдельно зафиксированы качество, GPU ms, CPU ms и frametime. После работающих RT-теней и отражений — переход к H1 (аудит многопоточности всего движка). Каждый дополнительный эффект имеет собственный performance budget. |

### X-Ray DX12/DXR — Hybrid Rendering Architecture (решение 2026-10-22)

**Статус:** ✅ **принято как направление проектирования; НЕ реализовано и НЕ
протестировано на GPU**. Это архитектурные ограничения и исследовательские
ориентиры, а не обещание конкретного качества, FPS или буквального
воспроизведения Northlight/4A Games. Решения о каждом алгоритме — отдельные
ADR, прототип и гейт с измерениями.

**Главный принцип:** обычный **DX12 raster — самостоятельный полнофункциональный
рендерер** (основная геометрия, G-buffer, CSM/shadow maps, свет и постобработка);
**DXR добавляет выборочные проходы**, устраняющие конкретные ограничения
raster/SSR. Сохранить рабочий **DX11/R4** как независимый baseline и откат.
Отсутствие DXR-capable GPU, ошибка RT-инициализации или отключение RT **не
должны препятствовать** запуску базового игрового DX12-пути. DX12 raster,
DX12 + DXR и DX11 сравниваются и тестируются раздельно.

| Источник | Что изучаем | Что НЕ переносим автоматически |
|---|---|---|
| **Control / Remedy Northlight** | Общие acceleration structures для нескольких RT-эффектов; выборочные opaque/transparent reflections, near-field indirect diffuse и contact shadows поверх raster; облегчённый ray-hit shading, отдельное вычисление света, специализированные spatial/temporal denoisers. | Фиксированные дистанции лучей, лимиты источников света, предположения об интерьерах и готовом baked/voxel GI: параметры определяются **сценами X-Ray и профилями**. |
| **Metro Exodus PC Enhanced Edition / 4A Games** | Динамический RTGI от солнца, неба и локальных/эмиссивных источников; представление сцены для лучей, упрощённые GI-материалы, временное накопление, кандидаты вроде DDGI для дальнего непрямого освещения открытых пространств. | Обязательный DDGI/RTXGI, полный path tracing, перенос конкретной структуры проб или бескомпромиссная замена shadow maps. RTGI — отдельное решение **после** стабильных теней и отражений. |
| **X-Ray / OGSR** | Большие outdoor-локации, динамические сутки/погода, ветер и foliage, сталкерская атмосфера, Legacy Materials, скелетная анимация и совместимость с игровым runtime. | Требование массовой переконвертации моделей и текстур; слом DX11 и «RTX-only» игра. |

**Технические контракты, которые должны пережить F → G:**

1. **Базовые данные кадра (F4/F5):** стабильные depth/normal/material IDs,
   преобразование существующего gloss в документированную roughness/BRDF
   политику; motion vectors, jitter, exposure и сигналы сброса temporal history
   для denoisers/upscalers. Данные и их формат фиксировать в контракте
   deferred lighting; не объявлять все карты PBR обязательными для Legacy.
2. **RT Scene и lifetime (G1/G4):** общий для выбранных RT-проходов
   BLAS/TLAS path на базе существующей геометрии, стабильные render handles,
   explicit GPU lifetime/fences и проверяемые правила build/refit/rebuild.
   Геометрия RT должна учитывать объекты **вне camera frustum**, влияющие
   на отражения/освещение/тени; RT selection, LOD, distance masks и residency
   измерять, а не просто копировать raster visibility или весь игровой мир.
   Динамика, NPC, foliage и streaming имеют отдельные invalidation-гейты.
3. **Ray-hit material lookup (G2/G4/G5):** точка попадания вне экранного
   G-buffer обязана получить material ID, UV, нормаль и нужные ресурсы.
   Изучить компактную запись hit attributes и отдельный compute shading pass
   как **кандидат оптимизации**, не как обязательный API. Alpha-tested
   растительность/заборы и прозрачность не объявлять opaque по умолчанию.
4. **Гибридные эффекты (G3/G5):** G3 — ограниченные RT contact/local shadows
   как дополнение к CSM/shadow maps, со строго определённым fallback.
   В G5 **сначала отражения** с отдельным A/B-гейтом (SSR/environment
   fallback, шероховатость, стекло и мокрые поверхности); **затем** отдельно
   исследовать near-field indirect diffuse и RTGI/DDGI для смены погоды и суток.
   Никакой перенос G5 в F4.0 и никакого «всё RTX сразу».
5. **Denoising/temporal (F5, G3/G5):** для shadows, reflections и diffuse GI
   предусмотреть *разные* фильтры/истории, используя depth, normals, motion и
   confidence/roughness при необходимости. Проверять disocclusion, смену
   камеры/уровня, resize, движение NPC/ветра и смену солнца/погоды; историю
   инвалидировать предсказуемо, без ghosting/light leaking. Сначала baseline
   denoiser с проверяемым поведением; DLSS Ray Reconstruction — **отдельная
   интеграция**, не автоматическая часть DXR.

**Порядок принятия (не меняет номера и зависимости существующих этапов):**

- **F3.6/F4.0:** завершить реальный игровой DX12 geometry frame; никакой
  зависимости от трассировки. **F4/F5:** полноценные raster-проходы и
  готовность G-buffer/temporal данных; только затем RT в штатном игровом кадре.
- **G0–G2:** feature query, BLAS/TLAS, raygen/miss/hit и диагностика без
  объявления игрового RTX. **G3:** первые контактные тени + fallback и
  GPU budget. **G4:** skinned/foliage/material/RT lifetime validation.
- **G5:** независимый milestone RT reflections; после его измерений отдельный
  ADR о целесообразности near-field GI, DDGI или иного outdoor RTGI.
  Path tracing — только самостоятельное долгосрочное исследование без
  обязательства реализации.

**Матрица доказательств и критерии для каждого RT milestone:**

- Сравнивать **DX11/R4**, **DX12 raster (RT off)** и **DX12 + конкретный
  RT-эффект (RT on)** на одной сцене, разрешении, погоде и времени суток;
  фиксировать неизвестные/различающиеся функции renderer.
- Набор сцен: **помещение/подвал**, улица **Припяти** (окна, мокрые поверхности),
  **Кордон/лес** (листва, alpha test, ветер), открытый горизонт/солнце;
  день/ночь и переход освещения/погоды — по мере доступности runtime.
- Сохранять GPU captures/markers, отдельные visual debug outputs
  (shadow/reflection/GI mask, ray hit/miss, history validity), A/B screenshots,
  Debug Layer/GBV/DRED; показатели GPU pass ms, CPU frame, p50/p95/p99
  frametime, VRAM/residency и изменение количества RT primitives/instances.
- Проверять холодный старт/переключение RT off/on, GPU без DXR, resize,
  level reload, teleport/camera cut, массовые dynamic objects, wind/foliage
  и длительный прогон. При ошибке или неподдерживаемом tier — корректный
  fallback, а не device loss. **Нет объявленного FPS-бюджета до baseline**.
  Положительный результат измерений не заменяет проверку артефактов и отката.

**Первоисточники (для выбора алгоритмов, не доказательство их реализации):**

- [Remedy / NVIDIA: Ray Tracing in Control, Ray Tracing Gems II, глава 46](https://link.springer.com/chapter/10.1007/978-1-4842-7185-8_46) — архитектура гибридного RT, reflections, indirect diffuse, contact shadows и denoisers.
- [4A Games: In-Depth Technical Dive into Metro Exodus PC Enhanced Edition](https://www.4a-games.com.mt/4a-dna/in-depth-technical-dive-into-metro-exodus-pc-enhanced-edition) — динамический RTGI/DDGI и материалы.
- [4A Games: Everything Technical about Metro Exodus Upgrade for PS5 and Xbox Series X|S](https://www.4a-games.com.mt/4a-dna/everything-technical-about-metro-exodus-upgrade-for-playstation-5-and-xbox-series-x-s) — аппаратные/производительные ограничения.
- [Microsoft: DirectX Raytracing Functional Specification](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html) — нормативные DXR API, capabilities, BLAS/TLAS и dispatch.
- [Microsoft: Direct3D 12 Programming Guide](https://learn.microsoft.com/en-us/windows/win32/direct3d12/directx-12-programming-guide) — resource lifetime, command lists, descriptor/barrier model.

### Совместимость материалов Legacy / Enhanced (проектное правило)

**Asset Compatibility First:** существующие OGSR/X-Ray текстуры, модели и материалы должны отображаться на DX12 и поддерживать начальные DXR-тени **без обязательной конвертации ассетов**. `Legacy Materials` сохраняют стиль старого контента через документированные fallback-параметры; `Enhanced Materials` — опциональные карты/метаданные для более точного PBR, мокрых поверхностей, отражений и расширенных эффектов. Это правило проектирования, а не подтверждённая реализация.

- **F4:** работающий Legacy raster path, сравнение с DX11 в одинаковой локации, при одинаковой погоде и освещении; корректные normal/gloss, alpha cutout и material lookup.
- **G1–G3:** BLAS/TLAS и первые RT-тени используют существующую геометрию, дополнительных roughness/metallic-карт не требуют.
- **G4:** обеспечить доступ к свойствам поверхности в точке ray hit (в том числе вне экранного G-buffer), alpha-tested foliage и динамические материалы; проверить обе модели материалов.
- **G5:** дополнительные карты повышают точность отражений/indirect lighting, но эти эффекты — отдельные необязательные рубежи.
- **Качество:** Legacy не обязан быть физически идентичен Enhanced; при этом отсутствие новых карт само по себе **не повод ухудшать** визуальную совместимость с DX11. Все эффекты проверяются A/B и GPU profiling.

Подробный контракт, ограничения DXR, правила fallback и матрица приёмки: [DX12/DXR Material Compatibility](dx12-dxr-material-compatibility.md).

### Условия G

- **Начинать G0/G1 после F3**, но встраивать RT в игровой кадр после F4.
- Не считать `D24_UNORM_S8_UINT` самостоятельным запретом DXR:
  acceleration structures строятся из геометрии. Формат depth и
  reconstruction выбирать исходя из конкретных RT/denoise-проходов.
- `metallic/emissive` не лежат в текущих четырёх MRT, а нормали и gloss
  уже есть: `common_iostructs.h:287-293`. Для качественных ray hits нужен
  способ получать свойства **попавшей** поверхности, в том числе вне экрана.
- NVIDIA **RTX** — линейка оборудования/фич; аппаратная трассировка в
  движке — через **Microsoft DXR**. DLSS Super Resolution не означает DXR.
- DLSS Frame Generation, Reflex и Ray Reconstruction — **отдельные
  интеграции** с собственными требованиями и тестами. Они не появляются
  автоматически при включении DX12.

### F3.5-IX — IX-Ray Multithreading Reference (deferred)

**Статус:** PLANNED / DEFERRED. **Приоритет:** medium. **Начало:** только после принятия текущего DX12 milestone и открытия F3.5 (после F3, до F4). **Сейчас никаких изменений runtime, Job System или DX11.**

**Цель:** использовать проверенные паттерны IX-Ray 1.6 как архитектурные референсы, не переносить его планировщик целиком. Исходники: <https://github.com/ixray-team/ixray-1.6-stcop>.

**Кандидаты на изучение:** `xr_task_group` (PPL/oneTBB), `xr_parallel_for`, задачи кадра `PreRenderThread` / `GameThread`, асинхронная подготовка деталей с двойной буферизацией (`DetailManager.cpp`), HOM/culling (`HOM.cpp`), синхронизация и жизненный цикл данных. Сначала проверить, какие эквиваленты уже существуют в OGSR, чтобы не дублировать код.

| Подэтап | Содержание | Гейт |
|---|---|---|
| **F3.5-IX.0 — аудит** | Сопоставить существующий OGSR Job System, frame loop, thread ownership и решения IX-Ray; проверить лицензии до копирования кода. | Карта потоков, сравнительная таблица, список точек интеграции и рисков. |
| **F3.5-IX.1 — решение** | Выбрать минимальные полезные изменения: task groups, ParallelFor, frame snapshots, зависимости и lifetime; оценить готовые schedulers вместо преждевременного собственного work stealing. | ADR с принятыми/отклонёнными решениями, без изменения production code. |
| **F3.5-IX.2 — прототип** | Только после отдельного ревью: изолированный CPU task prototype и, при необходимости, минимальная DX12 parallel command recording проверка с per-worker/per-frame allocators и sequential fallback. | Тесты race/deadlock/shutdown/resize, безопасный GPU fence lifetime. |
| **F3.5-IX.3 — измерения** | Сравнить 1/2/4/8 workers (по доступности), CPU frame time, p95/p99, task wait, scheduler overhead, GPU timeline. | Воспроизводимые замеры; оставить изменения только при доказанной пользе. |

**Оценка трудоёмкости (гипотеза до аудита):** IX.0 — 1–2 рабочих дня; IX.1 — 1–3; IX.2 — 3–7; IX.3 — 3–7. Всего **8–19 рабочих дней** при отсутствии серьёзной переделки существующей системы. Это не календарный срок и не обязательство реализовать все подпункты.

**Дальнейшее развитие:** F6 — интеграция/оптимизация по реальным профилям игрового DX12 кадра; H0 — ранние baseline-замеры; H1–H3 — сквозной аудит и адресная оптимизация AI/A-Life/physics/simulation **после G3 RT-теней и G5 RT-отражений**.

**Ограничения:** DX11 baseline не менять; не заменять рабочий scheduler без измерений; не вводить универсальный RHI или полный Render Graph; не менять текущий milestone; код IX-Ray напрямую не переносить без лицензионной проверки. Изменение этой записи не означает начала реализации.

---

## Этап H — Многопоточность и производительность всего X-Ray (после RT-теней и отражений)

**Цель:** после работающих **G3 RT-теней** и **G5 RT-отражений**
проверить, насколько эффективно OGSR использует многоядерный CPU и
готовит кадры для DX12/DXR. Оптимизировать реальные узкие места, а не
распараллеливать код по предположениям. **DX11 baseline сохранить.**

**Различие между F3.5 / F6 / H:** F3.5 заранее определяет безопасные границы
многопоточного **CPU-side DX12 renderer** и испытывает минимальный parallel
recording prototype **до F4**. F6 стабилизирует и оптимизирует готовый игровой
рендер по измерениям. H — **сквозной аудит simulation/A-Life/physics/AI и
игрового движка** после затратных RT-эффектов; не откладывает фундамент F3.5.

| Подэтап | Работа | Гейт / проверяемый результат |
|---|---|---|
| **H0 — исходные профили** | Ещё до DXR сохранить воспроизводимые CPU/GPU frametime, 1% low, загрузку потоков, draw calls и накладные расходы синхронизации в одинаковых сценах/настройках DX11 и DX12. Инструменты — Tracy, Visual Studio Profiler, PIX/ETW по применимости. | Есть baseline, методика и конфигурация замеров, чтобы отделять регрессии DXR от старых ограничений X-Ray. |
| **H1 — аудит многопоточности** | **После G3 и подтверждённых RT-отражений G5** исследовать реальный frame loop, main/render threads, A-Life/AI, физику, анимации, culling, очереди рендера, загрузку ресурсов, создание PSO, worker/job systems, mutex/lock contention, ожидания CPU↔GPU и распределение задач по ядрам. Сначала составить карту существующих потоков и зависимостей, затем снять профили тяжёлых игровых сцен. | Markdown-отчёт с картой потоков, горячими участками, величиной блокировок и перечнем приоритетных проблем **с измеренным вкладом в frametime**; никаких архитектурных переписываний на стадии аудита. |
| **H2 — адресная оптимизация** | По результатам H1 выносить независимые задачи simulation/A-Life/AI/physics в workers, уменьшать блокировки и main-thread stalls, улучшать сквозной streaming и связку игры с рендером. Развивать, но не изобретать заново, F3.5/F6 parallel D3D12 recording; дальнейшее масштабирование только по измеренной выгоде. | Для каждой правки — изолированный diff, до/после CPU ms, GPU ms, frametime/1% low, отсутствие новых race/deadlock, validation errors и потери корректности игрового поведения. |
| **H3 — масштабирование и регрессии** | Проверить несколько сцен (NPC/A-Life, тяжёлая геометрия, RT-тени/отражения), разные нагрузки и число доступных CPU-потоков; сравнить DX11/DX12, проверить стабильность длительных сессий. | Воспроизводимый regression suite и таблица выигрыша/потерь; откат изменений без пользы; DX11 fallback и корректность игровой логики сохранены. |

**Правила H:**
- H1 начинается **после рабочих RT-теней и RT-отражений**, а не после одного
  DX12 Clear/Present. **Это не запрещает ранний F3.5 — архитектуру и
  тестирование многопоточного DX12 до F4.** RTGI и прочие тяжёлые эффекты
  можно планировать отдельно по итогам замеров.
- Низкая загрузка GPU сама по себе не доказывает bottleneck главного потока:
  сопоставлять CPU/GPU timeline, очереди, ожидания и профиль конкретной сцены.
- Не заменять целиком scheduler, физику или AI, не вводить «многопоточность
  везде» и не переключать DX11 на новый backend ради оптимизации.
- Все изменения вносить поэтапно: **профиль → гипотеза → минимальная правка
  → измерения → регрессия → решение оставить/откатить**.

**Статус:** ⬜ **NOT STARTED**. H0 — запланирован как ранний baseline;
H1–H3 — после указанных RT-рубежей. Реализованных оптимизаций этапа H пока нет.

## Порядок выполнения и зависимости

```text
База CoP / DX11:
    A ✅ DLSS 310.9.1, CoP runtime проверен
    B1 ✅ NIS submodule активирован; clean CI ещё проверить
         │
         ├──▶ B2.0/D0: общий compute smoke-test
         │       ├──▶ B2–B4: NIS SDK → DX11 интеграция → Lua UI
         │       └──▶ D1–D2: Hi-Z / DDA → STOP/GO → D3–D5 SSGI
         └──▶ C: DLSS quality / flags (независимо)

Параллельная стратегическая ветка:
    F0: аудит зависимостей DX11/DX12
         ↓
    F1: решение по границе backend и план миграции
         ↓
    F2: DX12 Clear/Present → F3: ресурсы/mesh/depth
         ↓
    F3.5: snapshot + frame scheduling + per-worker/per-frame allocators
         └──▶ serial/parallel recording и 1/2/4/8-worker гейты
         ↓
    F4.0: первый реальный DX12 кадр уровня → текстурированный preview
          ↓
    F4: полный перенос игрового кадра → F5: temporal/DLSS/FSR3
         ↓
    F6: стабильность и измеренное расширение parallel path
         ↓
    G0: DXR tier → G1: BLAS/TLAS → G2: DispatchRays
         ↓
    G3: RT-тени → G4: динамика → G5: RT-отражения (отдельный гейт)
          ↓
    H1: аудит многопоточности ВСЕГО X-Ray → H2: точечные оптимизации
          ↓
    H3: масштабирование/регрессии; G5 (опц.): diffuse GI/denoise — отдельно

    H0: ранние baseline-замеры CPU/GPU до G3/G5 (параллельно F4–F6)

E: отдельные согласованные исправления техдолга, без bulk-cleanup.
```

**Необязательные зависимости:** NIS и SSGI на DX11 **не являются
блокерами** для F0–F4. F3.5 — обязательный **архитектурный** гейт перед массовым
переносом F4, но не требование ускорения на 8 workers. Прототип DXR G0–G2 может развиваться на небольшом
тестовом render path до завершения F5, но RT-функции в полноценном CoP
проходят интеграцию и приёмку только после F4/F6.

---

## Что сознательно не делаем без отдельного решения

| Идея | Причина |
|---|---|
| Полная замена DX11 в текущем R4 одним коммитом | Нет RHI; большой радиус регрессий, нечем откатиться |
| Обязательный программный BVH для DX11 SSGI перед DXR | Это иной проект; DXR BLAS/TLAS строятся отдельно |
| Автоматическое включение всех DLSS-флагов | Требуется соответствие motion vector / alpha / sharpening контрактам |
| Безусловная замена FSR3 на NIS как DLSS fallback | Нужны сравнительные тесты качества и latency |
| Полный RTX GI, path tracing и FG одновременно | Сначала RT shadows и GPU performance budget |
| Тотальная миграция всех шейдеров на SM6 сразу | Не является обязательным первым шагом DX12 raster |
| Перенос всей simulation/A-Life/job system в F3.5 | В F3.5 только рендер-границы и минимальный parallel prototype; оптимизация игрового движка остаётся H |

### Внешние технические ссылки для новых F/G (не результат локальной сборки)

- [Microsoft: Direct3D 12 Programming Guide](https://learn.microsoft.com/en-us/windows/win32/direct3d12/directx-12-programming-guide)
- [Microsoft: Changes from D3D11 to D3D12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/important-changes-from-directx-11-to-directx-12)
- [Microsoft: D3D12 raytracing feature query](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_d3d12_options5)
- [NVIDIA: Image Scaling integration](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling)

---

## Игровые данные — требование для повторения рантайм-тестов

**Рантайм baseline уже проверен в установленной CoP** (см. этап A).
При этом чистого клона репозитория недостаточно, чтобы повторить такой тест.
Для CoP требуется соответствующая версия игровых ресурсов/распакованная
`gamedata`, а не данные Shadow of Chernobyl. Оверлей форка:
`Game/Resources_CoP_1.6.02/gamedata/`.

В исходном roadmap записан состав оверлея (числа не перепроверялись при
правке этого файла):

```text
anims/ 6; levels/ 3; configs/ ~21; scripts/ 9;
shaders/ 552; textures/ 50 (~72,5 MB)
```

В GitHub реально присутствует путь `gamedata/scripts/` (нижний регистр).
Остальные точные имена каталогов проверять по дереву перед автоматизацией.

В репозитории отсутствует часть оригинальных данных CoP: конфигурации,
геометрия уровней, звуки, значительная часть текстур и скриптов.
**Для CI:** разделить smoke-test сборки без игровых данных и графический
runtime-test на доступной, легально установленной CoP с проверяемым набором
сцен. Не публиковать оригинальные игровые ресурсы вместе с артефактами CI.

---

## Примечание к редакции 2026-10-14

Эта редакция основана на **прикреплённом исходном `08-roadmap.md`** и
исследовании документов `cop-rtx/docs`. Подтверждённые владельцем результаты
A/B1 не переоценивались; добавлены **планируемые**, а не выполненные
F (DX12) и G (DXR). Принципиальные правки: NIS-входы SDK/compute-gate,
корректный путь Lua-UI, разделение SSGI и DXR, реалистичные проверки
resize/fallback, а также исправление устаревшего вывода «DX12 не делать».

---

## Этап I — World Partition и асинхронный World Streaming (план; не реализован)

**Цель:** по мере движения игрока подгружать и освобождать GPU/CPU-ресурсы секций мира без остановки кадра, сохраняя совместимость с классическими переходами между уровнями. **Это не тождественно frustum culling:** объект может не рисоваться, но оставаться резидентным; игровой AI/A-Life не должен автоматически выгружаться вместе с его рендер-представлением.

| Подэтап | Работа | Гейт / проверяемый результат |
|---|---|---|
| **I0 — аудит и метрики** | Исследовать существующие OGSR level geometry, details/trees, visual/LOD formats, spatial DB, lifetime и владение ресурсами, загрузчик уровней, Game/A-Life. Снять RAM/VRAM/frametime и причины блокировок в нескольких сценах; установить интерфейс секций без допущения о текущем формате данных. | Документ: что уже делится на части, что загружается монолитно, где безопасна выгрузка, матрица рисков. Никакого рефакторинга в I0. |
| **I1 — пространственные секции** | Прототип manifest/index с AABB/иерархическими ячейками и ссылками на ресурсы; поддержать неравномерное деление для леса, зданий, подземелий; reference counting общих ресурсов. | Дебаг-оверлей границ и список ресурсов секции; повторяемые тесты загрузки/выгрузки небольшого тестового участка. |
| **I2 — async IO / upload / eviction** | Рабочие очереди чтения/декомпрессии, CPU staging, D3D12 upload queues/fences, budgets, residency, безопасный deferred destruction после GPU fences; приоритет ближайших/ожидаемых секций, контроль thrashing. | Нет заметных main-thread IO stalls, use-after-free, GPU validation errors; RAM/VRAM и worst-frame измерены до/после. |
| **I3 — predictive streaming** | Приоритет от 3D camera position, frustum, скорости/направления и предполагаемого маршрута; hysteresis, предзагрузка на поворотах/при подъёме и запасные LOD при cache misses. | Тестовые маршруты с резкими поворотами и подъёмами, статистика late loads и видимых pop-in; отсутствие жёстко фиксированного радиуса в качестве единственного критерия. |
| **I4 — compatibility / stress** | Сохранение старого режима загрузки уровней, дебаг-инструменты, memory pressure, частые перемещения, alt-tab/resize, cold/warm cache, DX11 fallback. | Откат одной настройкой/сборкой; длительная сессия без утечек и повреждений ресурса, критерии измеряются на фиксированных маршрутах. |

**Зависимости:** I0 можно исследовать параллельно F3–F4; GPU-resident streaming внедрять после F4 и базовой синхронизации F6 (с использованием правил ownership/snapshot из F3.5). Не делать streaming обязательным блокером G0–G3. Не обещать абсолютного отсутствия stalls до замеров и настройки бюджета.

## Этап J — Height-Aware Visibility, LOD, растительность и тени (план; не реализован)

**Цель:** устранить заметное «появление в пяти метрах» травы, деревьев и теней без безусловного увеличения дальности рисования. J использует менеджер секций I, но **видимость, уровень детализации, резидентность и тенеобразование остаются разными решениями**.

| Подэтап | Работа | Гейт / проверяемый результат |
|---|---|---|
| **J0 — инвентаризация** | Проверить реальные форматы и механизмы progressive/static LOD, деревьев, detail objects, foliage shaders, CSM/sun, существующие shadow LOD и impostors непосредственно по исходникам и ассетам текущей ветки. | Матрица «существует / нуждается в конвертере / отсутствует»; никаких предположений, что все модели уже имеют пригодный LOD. |
| **J1 — система высот** | Определить единый vertical datum: условное **0 м относительно согласованного уровня моря** как метаданные мира; per-level vertical offset из локального X-Ray Y (ось проверить в коде), без перемещения старой геометрии. Хранить altitude камеры, AGL (над поверхностью), 3D camera pose, FOV, скорость. Геодезическую привязку к реальному уровню моря делать только при наличии достоверных исходных данных и datum. | Два тестовых уровня с разными локальными нулями дают непрерывную высоту, корректны на крыше/холме/в подвале; старые уровни не ломаются при отсутствии метаданных. |
| **J2 — visibility и screen-space LOD** | Выбирать LOD по projected screen-space error/размеру, применить hysteresis, dither/cross-fade и стабильные temporal переходы; окклюзию и дальний силуэт считать отдельно; поддержать HLOD/impostors для горизонта, terrain tiles для дальнего рельефа. | Одинаковые маршруты у земли и на высокой точке: дальние силуэты сохраняются, в движении нет заметного popping/мерцания, LOD переключается стабильно. |
| **J3 — vegetation streaming** | Секции деревьев/detail grass, плотность и анимация по бюджетам, density LOD и HLOD леса, предзагрузка растительности до входа в кадр; proxy fallback при запоздавшем детальном ассете. | Нет очевидной растительности, появляющейся непосредственно перед игроком; измерены density, pop events, GPU ms, RAM/VRAM на лесной сцене. |
| **J4 — shadow coverage** | Согласовать растительный LOD и caster LOD с cascaded shadow maps; **shadow casters включать также вне frustum камеры**, если они отбрасывают тени в видимую область. Стабилизация cascades, blend/hysteresis, distance shadow representations / контактные тени — по измерениям. | На солнце и при ходьбе по лесу нет резкого включения теней у ног; проверены motion/camera height, GPU ms и shimmering. |
| **J5 — DXR integration** | Streaming-aware BLAS/TLAS, выбор RT proxies для растительности, alpha-tested foliage, ограничение RT-caster region по влиянию света и памяти, async rebuild/refit по валидным гейтам G. | Отсутствуют missing RT shadows/invalid TLAS при границах секций, корректный raster fallback, DXR cost измерен. |

**Принцип:** высота над морем — **общая координатная ссылка**, а не магическая формула дальности; обзор определяется геометрией/окклюзией, AGL и проектируемым размером объектов. Для distant shadows не просто расширять CSM без бюджета: допустимы HLOD/proxy, impostors, baked/distant occlusion по качеству.

**Зависимости:** J0–J1 проектировать после I0, части J2/J4 можно прототипировать после F4 и до полного streaming; J3 требует I1–I3; J5 — после G1–G4 + I2. DX11-сохранность обязательна.

## Этап K — Seamless World / экосистема бесшовных модов (долгосрочно, не реализовано)

**Цель:** после внедрения I/J предоставить техническую возможность создавать бесшовные объединённые регионы без экранов загрузки. Это **не отдельный режим рендерера или движка** и не обязательное автоматическое объединение старых уровней. Существующие level transitions и моды продолжают работать; новые проекты смогут использовать streaming для перехода между регионами без загрузочного экрана.

| Подэтап | Работа | Гейт / проверяемый результат |
|---|---|---|
| **K0 — world composition contract** | Global transform/coordinates (при больших размерах camera-relative GPU positions / precision strategy), per-level offsets, границы, порталы и региональные метаданные, отдельная система абсолютной высоты J1. | Технический формат сцепления двух тестовых уровней и правила миграции/версий, без копирования игровых ассетов в репозиторий. |
| **K1 — render-only crossing** | Бесшовный рендер двух тестовых регионов на I/J, не обещая seamless AI; текстуры, геометрия, небосвод и освещение непрерывны. | Проход через границу без загрузочного экрана и заметного скачка геометрии/экспозиции, фиксированные frametime/VRAM. |
| **K2 — gameplay persistence** | Сериализация/переход владения игровыми сущностями, A-Life offline/online, межрегиональные navmesh/AI paths, физика, звуки, задания, сохранение/загрузка, спавн. | NPC/объекты/квесты не дублируются и не пропадают, долговременный переход туда-обратно и save/load воспроизводимы. |
| **K3 — modding tools и compatibility** | Утилита сборки world manifest, валидатор границ/высоты/LOD, диагностические overlay, версия формата; единая архитектура world streaming для обычных и бесшовных локаций без отдельного переключателя «Classic / Seamless». | Малый пример бесшовного мода и документация для моддеров; существующие переходы CoP и старые моды не регрессируют. |

**Не включено автоматически:** объединение всех канонических уровней CoP, seamless A-Life, единая глобальная навигация или сохранность существующих модов при включённом новом режиме. Каждое направление требует самостоятельного теста. Зависит от I и J; K2 не блокирует K1.

## Этап L — Переносимый DX12/DXR модуль и возможное сотрудничество с IX-Ray Platform (предложение, не согласовано)

**Цель:** сделать результат полезным вне OGSR. **IX-Ray Platform — потенциальный партнёр, не заявленный участник проекта и не получатель согласованного кода**. Никаких утверждений о сотрудничестве до контакта и согласия команды.

| Подэтап | Работа | Гейт / проверяемый результат |
|---|---|---|
| **L0 — legal / technical feasibility** | Сравнить лицензии, provenance кода/шейдеров, сторонние SDK, актуальные renderer interfaces OGSR и IX-Ray, форматы ассетов, toolchains и API; определить допустимые условия перераспределения. | Compatibility matrix и архитектурное ADR; без предположения, что весь OGSR backend можно просто скопировать. |
| **L1 — backend boundaries** | Минимизировать зависимости D3D12 device/queues/descriptors/PSO/resources/RT path от OGSR game layer, API-specific adapters и ownership; не переписывать уже работающий рендер без доказанной причины. | Изолируемые компоненты, минимальный integration harness, документированная зависимость на игровые типы. |
| **L2 — reproducible technical preview** | После F4/F6 и достаточной зрелости G3 собрать пример, test scenes без лицензируемых игровых данных, CI build-only + отдельные GPU runtime измерения, feature matrix, известные ограничения и DX11 fallback. | Повторяемый preview с проверяемыми RT shadow результатами и реальными GPU timings; DXR не заявлять готовым раньше G. |
| **L3 — outreach / optional integration** | Подготовить техническое предложение IX-Ray Team с краткими ограничениями, ссылкой на репозиторий, bench evidence и возможным небольшим экспериментальным портом, **только после самостоятельного решения о публикации**. | Заинтересованность/обратная связь команды; дальнейшие действия зависят от их решения. |

**Общая стратегия:** OGSR-Engine-rtx сохраняется самостоятельным экспериментальным форком. Не менять лицензии, авторство и attribution без юридической проверки; не заявлять совместимость с IX-Ray до прототипа.

### Межэтапные приоритеты и ограничения I–L

1. **Ближайший исторически записанный шаг:** F3 — минимальная DX12 геометрия. **После достижения тестовых F3.x — F3.5 до массового F4**; I–L не сдвигают графический путь F/G. Актуальность F3-статуса проверять по коммитам и GPU-логам, а не по этой записи.
2. **До окончания F4:** допустим только исследовательский I0/J0/L0; не смешивать массовое переписывание загрузчика/игрового мира с DX12 bring-up.
3. **После игрового кадра F4/F6:** секционный streaming I, затем Height-Aware LOD/vegetation/shadows J, поэтапно и с контролем DX11.
4. **DXR G и streaming I/J могут развиваться параллельно:** RT streamer J5 интегрировать только когда базовые BLAS/TLAS и foliage path валидны; полное K не является предпосылкой аппаратных RT-теней.
5. **K (seamless) — долгосрочная возможность, возникающая на базе I/J**, а не отдельный режим или обязательный deliverable первой DX12/DXR версии. Классические переходы остаются поддерживаемыми как контентная механика; L — необязательный путь сотрудничества после проверки лицензий/переносимости.
6. **Контроль качества:** fixed routes — Кордон/лес, Припять/улица и крыша, холм/горизонт, помещение/подвал, переход через границу двух тестовых регионов; capture screenshots/video, sudden pop-in counters, frame-time percentiles, GPU/CPU times, VRAM/RAM peak и resource residency. Численные целевые пороги назначать только после baseline.
7. **Никаких ложных PASS:** каждый milestone требует соответствующий commit, воспроизводимые логи/тесты и указание реальных ограничений. Streaming, RT или seamless не считать реализованными из факта DX12 bootstrap.

**Статус I–L на 2026-10-15:** ⬜ **PLANNED / NOT STARTED**. Вся эта секция — roadmap proposal, не реализованная функциональность.

---

## Этап V — Volumetric Clouds (самостоятельный параллельный DX12-трек; план)

**Решение 2026-10-22:** выделить процедурные объёмные облака из дорожной
карты F в независимое направление **V** (Volumetric Clouds). Работу над V0–V2
можно вести **параллельно F4.0** в отдельном чате/рабочей ветке с общими
техническими стандартами DX12. **Ни F4.0 PASS, ни F4/F5/G не зависят от V**;
готовность standalone-прототипа не означает его интеграции в игру.

**Визуальный ориентир V (решение 2026-10-22):** облачная система
*Red Dead Redemption 2* — основной **референс по визуальному качеству**
(масштаб, трёхмерная морфология, внутреннее освещение, самозатенение,
погодные переходы, стабильная картинка), но **художественная идентичность
S.T.A.L.K.E.R. сохраняется** (холодное пасмурное небо, тёмные дождевые фронты,
атмосфера Зоны). Это цель для V1–V5, **не готовая реализация, не копирование
закрытых технологий Rockstar и не новый гейт V0**. Критерии качества,
погодные сценарии и ограничения описаны в
[Volumetric Clouds — Visual Target](dx12-v-cloud-visual-target.md).

**Научная морфология и переходы (решение 2026-10-23):** долгосрочно
поддержать **10 основных родов облаков WMO**, в том числе **тонкие
перистые/волокнистые формы**, а также логические high/middle/low и
вертикально развитые облачные слои, совместное существование нескольких
форм, независимую адвекцию ветром на высотах и постепенное образование,
распад и смену пространственных погодных фронтов. **Не принуждать все
облака выглядеть как Cumulus; не считать отдельный похожий фрактальный
узор автоматически физически достоверным Cirrus.** Визуальные переходы
опираются на `CEnvironment`, но не пытаются заменить игровую систему
погоды физической CFD-симуляцией. Архитектура и требования к тестированию:
[Scientific Cloud Taxonomy & Transitions](dx12-v-scientific-cloud-taxonomy-and-transitions.md).

**Гейт и приоритет:** после V2.1 технического checkpoint **визуальная
морфология ещё не принята**; текущая задача DeepSeek — **V2.2** (высотная
геометрия, крупные объёмы, erosion, seam и performance), не полная
реализация 10 родов. Multi-layer расширение — **отдельный последующий
этап после V2 PASS и архитектурного review**. Вводить новые слои только
с учётом стоимости GPU и возможности дешёвых альтернатив ray marching.


**Требование совместимости и выбора:** DX12 поддерживает два альтернативных
варианта облаков в пользовательских настройках графики:

- **Classic 2D — по умолчанию:** сохранить оригинальные облачные текстуры,
  погодную семантику, внешний вид и анимацию X-Ray. Реализацию классического
  пути потребуется адаптировать для DX12; наличие работающего DX11-пути само по
  себе не доказывает готовность DX12-варианта.
- **Volumetric 3D — опционально:** процедурный 3D noise, ray marching,
  освещение облачного объёма и последующие оптимизации.
- В один момент активен **один** облачный путь; режим выбирается через
  настройку графики и сохраняется в пользовательском конфиге. Предусмотреть
  безопасный возврат к Classic 2D при недоступности/ошибке Volumetric 3D,
  корректные resource lifetime/barriers и сброс temporal history при смене
  режима. DX11/R4 **не изменять**.

**Архитектурная граница:** отдельный модуль внутри DX12 renderer/standalone
harness с общим интерфейсом cloud pass. Зависимости вводить через параметры
камеры, солнца, ветра, погоды, цели HDR и глубины (последние два входа нужны
только на соответствующем интеграционном этапе). Не подключать модуль к
игровому frame loop на ранних V0–V2; не копировать игровой renderer внутрь
облачного модуля. Реализация ray marching — compute shaders D3D12, **без
обязательной зависимости от DXR**.

| Подэтап | Работа | Гейт / проверяемый результат | Зависимости |
|---|---|---|---|
| **V0 — module bootstrap и noise** | Изолированный DX12 cloud harness; compute PSO, SRV/UAV, 3D Perlin-Worley/base noise, detail noise и debug-визуализация. | Воспроизводимая генерация/чтение 3D-поля, работающая отладочная визуализация; Debug Layer без ошибок; DX11 baseline неизменен. | DX12 device/queue и минимальные compute primitives; **параллельно F4.0**. |
| **V1 — ray marching** | Облачный высотный слой, density sampling, светопропускание, освещение солнцем, движение от тестового ветра, корректное ограничение шагов. | Видимые объёмные облака в standalone DX12; управляемые параметры, captures и GPU pass timing; нет обязательных игровых зависимостей. | V0; **параллельно F4.0**. |
| **V2 — standalone quality gate** | Карта покрытия/погоды с mock-данными, базовый self-shadowing, debug modes, тесты крайних параметров, оптимизация шагов и профилирование. | Зафиксированы A/B captures, GPU ms/VRAM на доступных GPU и известные ограничения; автономный cloud renderer не меняет F4 runtime. | V1; **параллельно F4.0**. |
| **V3 — DX12 sky integration** | Подключить облачный выход к игровым camera/depth/HDR/sky-проходам, согласовать цветовое пространство и порядок композиции; тест на реальной локации. | Облака отображаются внутри игрового DX12 кадра без нарушения depth, exposure, horizon и DX11 fallback. | Нужны соответствующие проходы **полного F4**, не часть F4.0 PASS. |
| **V4 — Classic/Volumetric switch** | Адаптировать оригинальные 2D-облака в DX12, реализовать cloud mode в настройках графики и конфиге; два взаимоисключающих render paths, safe fallback. | Classic 2D визуально сверены с DX11; выбор сохраняется и не нарушает reload/resize; отсутствие 3D не ломает игру. | V3 и legacy DX12 sky path в F4. |
| **V5 — temporal, weather и shadows** | Temporal reprojection/history reset, upscale и spatial filtering, интеграция с `CEnvironment` (ветер, солнце, осадки/циклы), опциональная карта теней облаков и performance tuning. | Стабильное изображение в движении, day/night/weather transition без критичных артефактов, A/B Classic/Volumetric, документированные GPU ms, VRAM и ограничения. | V4; соответствующие temporal/lighting возможности **F4/F5**. |

### V2-O — оптимизация без потери уникальности облаков (решение 2026-10-27)

**Неприкосновенный принцип: `Cloud uniqueness first — reuse computation, not cloud identity`.**
Оптимизация ray marching должна **переиспользовать только те данные и
вычисления, которые уже совпадали бы без оптимизации**, и не должна
искусственно увеличивать количество одинаковых облаков, повторять готовые
силуэты или превращать небо в равномерно заполненный клонами тайл.
**План / NOT IMPLEMENTED:** запись в roadmap не означает наличия кеша,
пропуска пустого пространства или принятого V2 visual gate.

**Разделение естественного повторения и тайлинга:**

- **Допустимо:** метеорологически обусловленные волны, ряды и ячейки
  (*undulatus* / слоистые поля), где повторяется **закономерность**,
  но отдельные элементы имеют естественные различия размера, высоты,
  плотности, промежутков и формы. Их ритм задаёт погодная морфология,
  а не необходимость многократно использовать один и тот же рендер-результат.
- **Недопустимо:** очевидное клонирование одной облачной массы по границам
  NoiseVolume/WeatherMap, одинаковые силуэты через фиксированное расстояние,
  искусственная синхронность отдельных облаков и упрощение weather/type
  распределения ради высокого cache hit rate.
- Периодический 3D noise tile с `WRAP` **может оставаться общим входом**,
  но не должен быть единственным источником уникальности окончательной
  облачной геометрии. Крупномасштабные world-space weather/shape поля,
  локальные параметры и развитие во времени сохраняют разнообразие
  облаков. Никаких новых крупных повторений ради оптимизации.

**Допустимые кандидаты на ускорение (после принятой базовой морфологии):**

1. **Conservative empty-space skipping / occupancy hierarchy:** пропускать
   только области с доказанной нулевой итоговой плотностью; границы
   должны учитывать coverage, type/height profile, warp, erosion,
   анимацию и используемые пороги. Дешёвая проба без доказанной
   верхней оценки плотности **не даёт права** удалять тонкие края и
   слабые облачные элементы.
2. **Adaptive/coarse-to-fine ray marching:** увеличивать шаг там,
   где это не искажает структуру, и уменьшать возле видимых границ;
   измерять пропущенные тонкие детали, banding и temporal popping.
3. **Переиспользование существующих noise/density данных:** повторять
   результат только при совпадении **всех влияющих входов** (world-space
   координаты, seed, форма, погодный тип, coverage, высота, ветер/время,
   параметры модели). При изменении входов — пересчёт либо корректная
   инвалидация. Совпадение периодических координат базового шума
   **не означает** совпадения итоговой плотности.
4. **Lighting/temporal caches — отдельный гейт:** конечное освещение
   дополнительно зависит от солнца, затенения соседними массами,
   атмосферы и вида камеры; нельзя переиспользовать его только потому,
   что совпал базовый noise. Temporal reuse требует корректной
   reprojection, rejection и history reset.

**Гейт качества и доказательства до включения оптимизации по умолчанию:**

- A/B **optimization OFF vs ON** при одинаковых камере, seed, мировых
  координатах, погоде, времени, ветре, освещении и пресете; заранее
  фиксировать допустимые погрешности изображения и плотности.
- Многокилометровые camera sweeps и разные погодные циклы: проверять
  соседние и удалённые области на **искусственные совпадения** силуэтов,
  повтор тайла, утрату локального разнообразия, исчезновение тонких
  просветов/волокон и чрезмерно однородное заполнение неба.
- Отдельно проверять ожидаемые **естественные волновые серии** и
  нежелательные идентичные клоны: регулярность ритма сама по себе
  не является дефектом; точная повторяемость готовой массы — кандидат
  на визуальную регрессию.
- Зафиксировать GPU median/p95, число фактических primary/shadow
  samples, ранние выходы и VRAM; проверять стабильность в движении
  (ghosting, popping, shimmer) и DX12 Debug Layer/GBV.
- **Если оптимизация ускоряет GPU ценой утраты облачной уникальности,
  её visual gate — FAIL независимо от выигрыша в миллисекундах.**

**Порядок:** сначала закрыть визуальные дефекты и принять базовую
3D-морфологию V2; затем испытывать V2-O отдельными переключаемыми
кандидатами. Не задерживать F4.0 и не подключать V2-O к игровому
DX12, V3–V5, молниям или выбросам преждевременно.

**Правила параллельной работы и слияния:**

1. Работы V0–V2 вести отдельно от критического пути F4.0 в
   `feature/dx12-volumetric-clouds` (ветка создана; рабочие изменения V0/V1
   не считаются опубликованными без отдельной проверки коммита и push).
   Повторно использовать общие DX12-инструменты, но не изменять без
   согласования ownership, frame scheduling и базовые ресурсы F4.0.
2. Интерфейс cloud pass и соглашения по shader compiler, root signatures,
   ресурсным состояниям, descriptor lifetime и fences документировать до
   интеграции; проверять соответствие
   [Microsoft Direct3D 12 Programming Guide](https://learn.microsoft.com/en-us/windows/win32/direct3d12/directx-12-programming-guide).
3. Интеграцию V3–V5 делать отдельными проверяемыми изменениями **после**
   появления необходимых F4/F5 data paths; прототип не считать production
   готовым по факту отдельного окна или успешной сборки.
4. **Classic 2D остаётся вариантом по умолчанию**, в том числе для игроков,
   предпочитающих оригинальный стиль. Новый режим должен быть **опцией**, а
   не принудительным визуальным изменением всего проекта.
5. Каждому PASS нужны исходный commit, воспроизводимые build/runtime steps,
   GPU captures/тайминги, визуальные сравнения, тесты init/shutdown,
   resize/reload и явные известные ограничения. Не объявлять GPU performance
   без измерений.

**Статус V на 2026-10-23 (раздельно подтверждение и слова разработчика):**

- **V0 — PASS WITH NOTES по независимому source/delta audit** предоставленного
  финального пакета: 3D-шумы, самостоятельный EXE, negative validation tests.
  Подтверждение Windows GPU-испытаний основано на приложенных логах разработчика,
  а не на повторном запуске аудитором. Локальная Git-фиксация/публикация исходников
  V0 не подтверждена; решение об интеграции в игровой DX12 не принималось.
- **V1 — IN PROGRESS / NOT YET ACCEPTED:** разработчик тестирует ray marching;
  пользователь сообщил, что увидел **небольшие фрагменты облаков** в тестовом
  приложении. Это не доказывает завершения V1.1, пригодности освещения,
  непрерывности облачного слоя, производительности или корректного поведения
  камеры. **Нет предоставленного завершённого V1 audit package, GPU timings
  и независимого V1 PASS**. До получения отчёта и исходников не объявлять
  ни V1.1, ни V1 завершёнными.
- **V2 — PLANNED / NOT ACCEPTED:** визуальная связность, крупномасштабные
  структуры, морфология, погодные пресеты, стабильность в движении,
  GPU-производительность и устранение артефактов ещё предстоят.
- **V3–V5 — PLANNED:** игровая интеграция, настройка Classic 2D / Volumetric 3D,
  подключение реальных погодных циклов и окружения отсутствуют.
- **V-L — DEFERRED / NOT STARTED:** молнии являются опциональным направлением
  внутри V; реализация запрещена до cloud quality gate ниже.

### V-L — Lightning in Volumetric Clouds (отложенное расширение)

**Решение 2026-10-23:** планируем красивые грозовые молнии: внутриоблачные
вспышки, разряды между различными облачными массами, впоследствии
облако–земля. Они должны восприниматься частью облачного объёма, освещать
его изнутри и соответствовать S.T.A.L.K.E.R. погоде. Это **не текущее
задание DeepSeek**, не часть V1 PASS и не повод замедлять F4.0.

**Жёсткий гейт перед первым V-L кодом:** как минимум **V1 PASS** и **V2 PASS
(самостоятельный DX12 EXE)** после независимого ревью кода, captures, GPU
timings и ресурсоёмкости. Нужны крупные непрерывные облачные массы, рабочая
камера/параллакс, корректное освещение/плотность, устойчивое изображение
в движении, рабочие погодные пресеты и отсутствие блокирующих ошибок
D3D12 Debug Layer/GBV, resize и resource lifetime. **Несколько видимых
фрагментов облаков не выполняют этот гейт.** Если V2 пройден частично,
работы V-L не запускать до отдельного решения о готовности.

| Подэтап | Отложенная задача | Зависимость / критерий |
|---|---|---|
| **V-L0 — lightning design** | Контракт грозовых ячеек/облачной плотности; отдельная visual lightning subsystem; типы intra-cloud, inter-cloud, cloud-to-ground; seed, частота, визуальные ограничения и GPU budget. | Только после V1 + V2 PASS; согласованный ADR, никакой обязательной симуляции реального электрического поля. |
| **V-L1 — intra-cloud flashes** | Кратковременные объёмные вспышки и светорассеяние внутри грозовых облаков; без геометрии разряда и без игровой сцены. | После V-L0; standalone captures, стабильность, reproducible seed, GPU ms и безопасные flash limits. |
| **V-L2 — inter-cloud discharges** | Процедурные ветвящиеся каналы между **пространственно различными** плотными облачными областями; подсветка обоих объёмов, а не случайные линии в пустом небе. | После V-L1; привязка к пространственной структуре плотности, камера и движение, performance/visual tests. |
| **V-L3 — world lightning & thunder** | Опционально: разряды облако–земля, короткое освещение геометрии, туман/осадки, согласование с существующим X-Ray thunder/weather timing; безопасный fallback. | Только после наличия необходимых **V3–V5 и F4/F5** игровых проходов; DX11 и Classic 2D не ломать. |

**Единый источник погодных событий:** `CEnvironment` / существующая система
X-Ray задаёт погодный цикл, ветер, грозу, дождь и игровые события. Cloud
renderer отвечает за форму/освещение облаков, V-L — за **визуальное**
проявление разрядов; не создавать параллельный игровой weather manager.
Подключение к реальным циклам — после V3–V5. Для Classic 2D сохраняется
исходное погодное поведение; Volumetric 3D + lightning — **опционально**.

**Названия:** `V-L0…V-L3` — внутренние подэтапы V, не путать с занятым
самостоятельным направлением `L — DX12/DXR portability / IX-Ray`.

**Приоритет сейчас:** закончить V1, получить полный отчёт + исходники,
независимо проверить, затем стабилизировать V2. **Не начинать молнии сейчас.**

### V-S — Weather Cycle & Surge Visual System (отложенное расширение)

**Решение 2026-10-23:** выброс — важный самостоятельный аномальный
визуальный сценарий S.T.A.L.K.E.R. В будущем используем **ту же технологию
Volumetric 3D + V-L молний** для красно-багрового неба, массивного
аномального облачного ядра, внутренних вспышек и межоблачных разрядов,
согласованных с оригинальными звуком, пси-эффектами и игровыми стадиями.

**Полный контракт, источники кода, подробная матрица обычных погодных
циклов и фаз выброса, приёмочные тесты:** [X-Ray Weather & Surge Visual Contract](dx12-v-weather-surge-visual-contract.md).

**Важно не смешивать:**

- **`CEnvironment::WeatherCycles`** — *обычные* погодные циклы: ясно,
  переменная облачность, пасмурно, дождь, ветер, туман, гроза, смена
  дня/ночи и другие фактически загруженные `.ltx`. Это категории
  визуального отображения, **не выдуманный список имён циклов**. Берём
  интерполированные `CurrentEnv` (sun/sky/clouds/wind/rain/fog) и
  отображаем **все реальные циклы**, включая дополнительные модовые,
  через собственный DX12 cloud pass.
- **`CEnvironment::WeatherFXs`** — временные FX, которые могут подменить
  погодные параметры. Наличие WFX не означает автоматический выброс.
- **Игровой `surge_manager.script`** — источник авторитетных событий
  выброса: начало, предупреждение, пик, завершение и игровая логика
  (фактические названия/тайминги ещё требуют изучения конкретной
  геймдаты). **Не путать с C++ `CALifeSurgeManager`**, который обслуживает
  ALife spawning.

**Как рисуем самостоятельно (проект):**
в обычную погоду — 3D-морфология облаков, солнечное рассеяние, самозатенение,
world-space дрейф от ветра, масштабируемая плотность и покрытие; при
выбросе — гладкий переход к багровой палитре **на основе исходных
`CurrentEnv`**, объёмные грозовые фронты, локальное внутреннее
аномальное свечение и V-L межоблачные молнии. Возвращение к погоде
происходит по сигналам исходной игровой системы, без дополнительного
управления погодой со стороны рендерера.

**Визуальные фазы выброса (проект, не названия игровых скриптов):**
S0 обычное небо → S1 предвестники → S2 нарастание/покраснение →
S3 активный выброс/облачное ядро → S4 затухание → S5 восстановление
обычной погоды. Длительность, реальные WFX IDs, момент предупреждения,
пику и завершения **не придумывать**: их нужно считать из
игрового `surge_manager.script` и актуальных погодных `.ltx`.

| Подэтап | Что делаем | Условие запуска / приёмка |
|---|---|---|
| **V-S0 — source inventory** | Собрать фактическую карту `WeatherCycles`/`WeatherFXs`, Lua-событий выброса, фаз, погодных `.ltx`, звука, постэффектов и camera FX из установленной геймдаты. | Исследование только при наличии файлов; не влияет на текущий V1. |
| **V-S1 — standalone surge sky** | Пресеты погодных циклов, красная объёмная атмосфера, облачное ядро, плавная смена визуальных фаз в DX12 standalone. | **V1 PASS + V2 PASS** и независимые baseline/captures/тайминги. |
| **V-S2 — surge lightning** | Переиспользование **V-L1/V-L2** для электрических разрядов между облаками, объёмной подсветки и аномального свечения. | V-S1 и принятые V-L этапы, без двойного legacy-освещения. |
| **V-S3 — in-game surge adapter** | Связь с настоящими Lua-событиями и `CEnvironment`, sky/depth/HDR/fog/scene lighting, сохранение исходных игровых событий и Classic 2D. | Соответствующие V3–V5/F4/F5 игровые проходы + независимые игровые тесты. |

**Жёсткий запрет:** пока не работает и не принят полноценный облачный
рендерер (V1 + V2), **DeepSeek не начинает молнии, аномальное небо или
интеграцию выброса**. Сейчас пользователь видел только небольшие
фрагменты V1 облаков; текущий прогресс неизвестен без полного
отчёта, исходников, GPU timings и captures.

**Game-data gap:** в доступной копии репозитория отсутствует полный
актуальный `surge_manager.script` и соответствующие
`weather_effects/*.ltx` для используемой игры. Названия
`fx_surge_day_3`, `fx_blowout_day` и т.п. встречаются во внешних
CoP/модовых материалах, **не подтверждены в нашей установленной
геймдате**. До V-S0 не объявлять полный перечень/тайминг погодных циклов
установленным.

**Главное правило:** *одна оригинальная погода X-Ray → два отображения
(Classic 2D по умолчанию / Volumetric 3D опционально)*.
Новый Surge Visual Adapter — только потребитель игровых данных, не второй
игровой weather/surge manager.

---

## Этап P — Portable PhysX Ragdoll: OGSR DX11 → IX-Ray (архитектура принята, реализация не начата)

**Первый приоритет направления P (решение 2026-10-22): ragdoll-физика персонажа.** Самый первый игровой результат — **один NPC-сталкер, у которого анимированный скелет переключается на PhysX CPU ragdoll и сталкивается со статической геометрией настоящего игрового уровня на OGSR DX11/R4**. Оригинальное передвижение и AI остаются на ODE. Дополнительные PhysX-ящики — только инструмент тестирования, не самостоятельный игровой milestone. Полная замена character movement и GPU PhysX отложены.

### P-S — адаптация PhysX к скелету модели (решение 2026-10-25; PLANNED)

**Требование:** PhysX ragdoll должен строиться по фактической иерархии костей и физическим метаданным загруженной модели X-Ray, а не по жёстко зашитому списку костей одного сталкера. Существующие `CCharacterPhysicsSupport::CreateSkeleton/CreateShell`, `PHGeometryOwner`, `PHElement` использовать как исходный материал для аудита и переноса данных. **Это требование к будущему адаптеру, не подтверждение уже работающего PhysX auto-rig.**

- Считывать bone hierarchy, parent-child, bind/current transforms, доступные collision shapes, массы, joint types/limits; проверять валидность и единицы измерения. Дополнительные визуальные/декоративные кости не обязаны становиться отдельными rigid bodies.
- Строить PhysicsRig из пригодных физических костей, сохраняя animation-only кости; для моделей без достаточных физических метаданных требовать явный профиль/маппинг либо безопасный ODE fallback. **Не обещать** корректный ragdoll для произвольной модели только из-за наличия костей.
- Поддерживать замену модели и новые качественные скелеты без правки C++ для каждого заранее известного bone name **при совместимых данных и валидном профиле**; изменение топологии, дополнительные конечности, IK и joints требуют отдельных проверок.
- Учитывать отдельные rigs мутантов после NPC ragdoll P2/P3; не считать humanoid rig универсальным. Контролировать animation→PhysX pose/velocity handoff, распределение масс, коллизии, lifetime, save/load и отсутствие двойной симуляции.
- Гейты: P-S0 read-only аудит текущего model-driven ODE shell; P-S1 один реальный NPC с auto-built PhysX shell по его физическим данным (совместно с P2); P-S2 замена на совместимую модель с иной детализацией костей; P-S3 разные скелеты/мутанты только после стабилизации P3 и профильных тестов. Classic ODE fallback и DX11 остаются обязательными.

**Приёмка:** тестовый набор минимум из стандартной модели NPC, совместимой модели с дополнительными animation-only костями, модели с изменённой физической иерархией и модели с неполными данными. Для каждого случая записать результат (автосборка / нужен профиль / fallback), логи причин отказа, визуальные и collision evidence, регрессии DX11/DX12. **PLANNED / NOT IMPLEMENTED.**

**Переносимость с первого дня:** `PhysicsCore` и `PhysicsPhysX` не зависят от OGSR, IX-Ray, ODE и DirectX. `PhysicsAdapterOGSR` подключает данные костей/коллизий/игровой цикл/анимацию OGSR; после доказанного игрового результата создаётся `PhysicsAdapterIXRay` для того же ядра. Это планируемая вторая интеграция, не заявленная готовая совместимость. Никакого согласованного сотрудничества с IX-Ray Team пока нет.

**Сохранность:** ODE остаётся стандартным backend и fallback; ни одна физическая оболочка не симулируется одновременно ODE и PhysX. Для первого ragdoll PhysX получает статический collision mesh уровня, без обещания полноценных контактов с динамическими ODE-объектами. Не менять глобальные `CPHWorld`/`CPHIsland`, не ломать DX11 и не блокировать DX12 F4.0. Специальные пути save/load, сеть, анимационные переходы, визуальные кости, удаление объектов и смена уровня требуют тестов до production-включения.

| Подэтап | Реализация | Гейт |
|---|---|---|
| **P0 — аудит ragdoll и baseline ODE** | Проверить `CCharacterPhysicsSupport::CreateSkeleton/CreateShell/EndActivateFreeShell`, `CPHShell`, `CPHJoint`, игровые shape/joint data, save/load и pose callbacks. Pin PhysX SDK/лицензии и определить contract. | Статический аудит с путями/функциями и воспроизводимые ODE тесты; без массового рефакторинга. |
| **P1 — переносимый CPU ragdoll harness** | Собрать минимальное `PhysicsCore/PhysicsPhysX` вне OGSR, поднять физический скелет, суставы и столкновение со статической поверхностью. Сравнить reduced-coordinate articulation и D6 joints для X-Ray rig. | Стабильные pose/velocities, лимиты суставов, fixed-step, init/shutdown и lifetime; тестовый ящик не заменяет этот гейт. |
| **P2 — первый PhysX ragdoll в OGSR DX11** | Опционально переключить одного сталкера с анимации на PhysX shell, скопировать начальную позу/скорость и вернуть bone transforms в OGSR; импортировать статический collision mesh уровня. | Воспроизводимое видео/лог реального DX11 runtime; ODE fallback, отсутствие двойной симуляции, отсутствие крашей. |
| **P3 — стабилизация игрового ragdoll** | Настройка масс, суставов, контактов, коллизионных фильтров, корректная очистка, load/reload, save/load и производительность. | Независимая проверка кода и матрица тестов, обнаруженные ограничения записаны; режим off работает как прежде. |
| **P-C0 — аудит существующей физики мутантов** | После подтверждённого P2/P3 NPC ragdoll изучить фактические CBaseMonster, movement/animation controllers, ODE character support, IKLimbsController, видовые анимации, конфигурации и игровые модели. Определить, где реальное проскальзывание лап, ошибки опоры, рельефа и инерции, а где намеренная неестественная пластика. | Матрица по видам с исходными анимациями, видео baseline, кодовыми ссылками и списком подтверждённых дефектов; ничего не переписывать с нуля. |
| **P-C1 — аккуратное улучшение опор и сцепления** | Расширить существующие IK/ODE/animation механизмы только где они применимы: постановка лап, контакт с грунтом, склоны, повороты, синхронизация скорости и анимации. Сохранить оригинальные походки, атаки, ритм и лор каждого вида; начать с кабана, затем проверить слепых псов. | Сравнение Classic/Enhanced в одинаковых сценах; нет проскальзывания, неверных опор, повреждения AI-path, attack timing и hitboxes; feature flag и rollback. |
| **P-C2 — видовая инерция и реакции** | Настроить разные профили массы, баланса, контактов, прыжков и приземлений без универсальной процедурной походки. Физика поддерживает авторские анимации, не заменяет их. | Игровые тесты на рельефе, препятствиях и в бою для кабана, пса, кровососа, снорка, псевдогиганта; измеримые улучшения без потери характерной пластики. |
| **P-C3 — PhysX ragdoll мутантов (позже)** | После стабилизации NPC ragdoll и P-C0–P-C2 адаптировать PhysX shell для подходящих скелетов мутантов; отдельно проверить кости, суставы, массы, контакты и ODE fallback. | Реальный игровой runtime-test по каждому rig, корректный animation→ragdoll handoff, отсутствие двойной симуляции и регрессий DX11/DX12. |
| **P4 — перенос ragdoll в IX-Ray** | Изучить фактический IX-Ray, создать отдельный adapter и собрать ту же версию portable core. | Отдельный build и реальный IX-Ray runtime-test; лишь после них portability PASS. |
| **P5 — расширение ragdoll** | Другие скелеты человека, потом мутанты и особые оболочки; адресные контакты с legacy объектами. | Матрица игровых/визуальных регрессий для каждого rig. |
| **P6 — перенос physics character movement (позже)** | Игрок, затем гибридные NPC, прыжки/лестницы/приседания/склоны. | Независимые gameplay/AI baseline tests и fallback. |


**После NPC ragdoll — направление P-C: Enhanced Creature Physics (PLANNED).** Улучшать уже существующие механизмы X-Ray, а не заменять оригинальную анимацию мутантов новой физической походкой. Сохранять авторскую неестественность, устрашающий характер и лор. Начинать **после игрового NPC ragdoll P2 и его стабилизации P3**; P-C не блокирует DX12 F4.0 и не подменяет текущие P0–P6.

**Первый сквозной критерий успеха:** один физический скелет NPC в OGSR DX11, созданный из исходной модели и игровой позы, симулируется PhysX и правильно отображается в мире; весь остальной gameplay остаётся на ODE. Производственные гарантии P3 не объявлять выполненными после P2.

**Ссылка:** [Portable PhysX ADR](physics-portability-adr.md). **Статус P:** PLANNED / NOT IMPLEMENTED. PASS — только по коду, тестам, логам, evidence и независимому review.

### P-M — Индивидуальная масса NPC и подвижность экипировки (решение 2026-10-25; PLANNED)

**Принцип:** *масса определяет реакцию персонажа на физические воздействия, а экипировка и её механизмы — насколько хорошо он может эту массу перемещать*. Не связывать скорость ходьбы напрямую с общей физической массой.

- **Индивидуальная масса тела NPC:** при создании сталкера назначать устойчивое значение в ориентировочном диапазоне **65–80 кг** (проектный пример; уточнять по балансу). Не выбирать случайную массу заново каждый кадр или при загрузке сохранения.
- **Экипировка:** физическая масса = масса тела + физически учитываемая масса надетой брони/снаряжения. Пересчитывать при экипировке/снятии; согласовать с inventory, исключить двойной учёт. Массы костюмов, груз и конкретные игровые коэффициенты определить после проверки баланса.
- **Раздельные параметры:** `physical_mass` отвечает за инерцию, контакты, внешние импульсы и ragdoll; `mobility_profile` — за желаемую скорость, ускорение, торможение и манёвренность. Сама большая масса **не обязана** снижать скорость NPC.
- **Обычная тяжёлая броня:** может *слегка* замедлять ходьбу/ускорение и менять манёвренность как отдельный настраиваемый игровой эффект.
- **Исправный экзоскелет:** собственная масса **увеличивает физическую инерцию**, но приводы **компенсируют нагрузку при передвижении**: обычная скорость ходьбы **не ниже** базовой скорости того же NPC без брони. Ускорение/стабилизацию настраивать отдельно. Износ приводов — необязательное позднее расширение.
- **Толчки и отдача:** при одинаковом реально приложенном импульсе тяжёлый NPC получает меньшее изменение скорости (`Δv = J / m`). Раздельно исследовать удар по живому NPC, его анимационную реакцию, отдачу собственного оружия и расчёт урона. **Не уменьшать урон автоматически пропорционально массе** и не переводить живых NPC безусловно в ragdoll.
- **Ragdoll:** суммарная масса тел скелета согласуется с массой персонажа и экипировкой; отдельно настраиваются массы костей, моменты инерции и суставы. Не симулировать одну оболочку одновременно ODE и PhysX.
- **Совместимость:** по умолчанию сохранять исходное поведение при выключенном feature flag. Проверять AI-навигацию, лестницы, прыжки, приседание, анимации, save/load и сеть; не затрагивать DX11/R4 и DX12 renderer ради механики.

| Подэтап | Реализация | Гейт |
|---|---|---|
| **P-M0 — ODE mass/movement baseline (READ-ONLY)** | Аудит `CPHMovementControl`, `CPHSimpleCharacter`, гибридного AI movement, контактов/импульсов, `CCharacterPhysicsSupport`, shell и конфигураций брони. Установить влияние массы на силу, торможение, hit и состояния. | Карта зависимостей и воспроизводимые измерения исходной ODE; не считать hit reaction NPC реализованной без проверки. |
| **P-M1 — opt-in профиль массы на ODE** | Устойчивый `CharacterMassProfile` (рабочее название): индивидуальная масса, экипировка, отдельная мобильность/компенсация; один тестовый NPC без изменения остальных. | Скорость и управляемость там, где требуется, сохранены; проверены смена брони, save/load и AI. |
| **P-M2 — контролируемые физические реакции** | Проверить/реализовать передачу внешних импульсов живым NPC без смешивания импульса, анимации, урона и ragdoll; сравнить лёгкую/тяжёлую броню и экзоскелет. | Измеряемая разница реакции при одинаковом импульсе, корректные контакты, отсутствие непреднамеренных рывков/падений. |
| **P-M3 — согласование с PhysX ragdoll** | Передавать профиль при переходе к PhysX shell; проверять распределение массы по костям, инерцию, импульсы и pose/velocity handoff. | Реальный NPC в OGSR DX11, корректная масса ragdoll, ODE baseline и fallback; после необходимых гейтов P2/P3. |

**Тест-матрица:** базовый NPC ~70 кг и тяжёлый NPC/экзоскелет ~110–120 кг при одинаковом импульсе; базовая ходьба/ускорение без брони vs исправный экзоскелет (скорость ходьбы не снижается); отдельно обычная тяжёлая броня (умеренный штраф); рельеф/лестницы/AI path, экипировка в runtime, сохранение/загрузка, animation → ragdoll. Значения масс — **тестовые примеры**, не утверждённый баланс. Измерять fixed timestep и фактическую передачу импульсов.

**Порядок и статус:** P-M0 можно исследовать параллельно как независимый ODE baseline; P-M1/P-M2 — только после отдельных тестов; P-M3 не подменяет первый P2 ragdoll. **PLANNED / NOT IMPLEMENTED.** Не блокировать F4.0, DX11/R4 и исходные P0–P6.



## Направление VFX — базовая физика среды → улучшение аномалий (решение 2026-10-25; PLANNED)

**Порядок обязателен:** сначала модернизировать родные эффекты X-Ray (огонь, дым, искры, вода, лужи и реакции на ветер/геометрию), сохраняя игровые события и оригинальные эффекты; **только затем** использовать общую систему для электрических, огненных и гравитационных аномалий. Не писать второй независимый particle engine для аномалий. Это отдельный трек, не блокирующий DX12 F4.0 и PhysX NPC ragdoll.

**Платформенная стратегия:** NVIDIA Advanced — исследовать NVIDIA Flow для локального объёмного огня/дыма и PhysX GPU/PBD там, где он действительно применим и поддерживается SDK/GPU; не считать Flow частью PhysX и не обещать автоматическую совместимость. AMD/другие GPU — **временно** сохранять исходные огонь/воду/частицы и доступные универсальные улучшения без CUDA; DX11 Classic — обязательный рабочий fallback. После NVIDIA-пути отдельно разработать vendor-neutral DX12 compute-альтернативу для AMD и других GPU. Возможности определять по фактическим feature checks, а не только по названию производителя. Все пути должны сохранять одинаковую игровую механику и урон.

**Лужи:** дешёвый отдельный класс водных эффектов — отражение, рябь и локальные брызги при необходимости; **не** запускать полноценную жидкостную симуляцию для каждой лужи, в том числе на NVIDIA. Для больших водоёмов и локальных всплесков предусмотреть разные уровни детализации и бюджеты GPU/VRAM.

| Этап | Работы | Приёмка |
|---|---|---|
| **VFX0 — аудит родных эффектов** | Проверить particle engine, конфиги/геймдату, ZoneCampfire, wind API, огонь, дым, воду, лужи, взаимодействия с объектами, освещение и DX11/DX12 render paths. | Карта кода и эффектов, видео baseline, измерения CPU/GPU/VRAM, список совместимых API и лицензий; не объявлять функции SDK реализованными. |
| **VFX1 — базовый огонь/дым/искры** | Улучшить существующие emitters и реакции на ветер/геометрию. Изолированный NVIDIA Flow DX12 prototype для объёмного огня и дыма; лёгкий исходный/универсальный fallback. | Костёр в игровом окружении, ветер меняет дым/искры; нет регрессий тайминга, света, звука и gameplay, сравнение Classic/Enhanced/Advanced. |
| **VFX2 — вода и упрощённые лужи** | Сохранить базовую воду X-Ray, добавить недорогие рябь/брызги/контакты; исследовать PhysX GPU/PBD только для ограниченных локальных жидкостных эффектов на поддерживаемых NVIDIA. | Раздельные профили луж/водоёмов/всплесков, отсутствие массовой дорогой симуляции, измеренные GPU/VRAM бюджеты и fallback. |
| **VFX3 — универсальная альтернатива AMD** | После первого рабочего NVIDIA Advanced исследовать и внедрить vendor-neutral DX12 compute-эффекты огня/дыма/частиц и локальной воды без CUDA. Старые функции AMD — временное решение, не целевое качество. | Тесты на AMD и NVIDIA, feature detection, сравнение качества/производительности; отсутствие NVIDIA-зависимости в общем API эффектов. |
| **A-VFX0 — аудит аномалий** | Только после доказанного базового VFX1/VFX2 изучить CustomZone, FryupZone, TorridZone, GraviZone, ParticlesObject, idle/awake/blowout/bolt события и реальные игровые ассеты. | Матрица оригинальных аномалий, привязка визуальных эффектов к существующим gameplay событиям и эталонные видео. |
| **A-VFX1 — электрические аномалии** | Процедурные DX12 молнии, ветвления, вспышки и свет; не симулировать дуги как rigid bodies PhysX и не менять игровую зону поражения. | Сохранены тайминги урона/звука/болта, Classic fallback, нет ложных игровых коллизий. |
| **A-VFX2 — огненные/гравитационные аномалии** | Переиспользовать VFX огонь/дым/искры/ветер для огненных; локальные поля частиц/пыли и визуальные вихри для гравитационных. | Оригинальные механики, характер и ритм аномалий сохранены, сравнение Classic/Enhanced/Advanced и замеры производительности. |


### VFX-AMD — предварительная оценка NVIDIA vs AMD, временный режим и риски (2026-10-25; PLANNED)

**Граница подтверждения:** это вводная инженерная оценка, **не измерение** на реальных GPU и не обещание совместимости. До выбора конкретных версий SDK, тестовых видеокарт и прототипов нельзя утверждать точные FPS, миллисекунды, расход VRAM или сроки. Flow и PhysX GPU — разные технологии; поддержку Flow на AMD проверять по выбранному backend/SDK и реальным тестам, а не выводить из наличия DX12.

| Компонент | NVIDIA: первый этап | AMD: первый этап | Целевое состояние |
|---|---|---|---|
| CPU PhysX rigid bodies / ragdoll | Доступен без CUDA при корректной интеграции | Тот же CPU PhysX без CUDA | Единая игровая физика независимо от GPU |
| PhysX GPU / CUDA PBD fluid | Исследовательский прототип только на поддерживаемых NVIDIA | CUDA backend недоступен напрямую; исходная вода/локальные эффекты | Универсальная DX12 compute альтернатива для локальных жидкостей либо упрощённая визуальная симуляция |
| NVIDIA Flow fire/smoke | Проверить DX12 backend, SDK, совместимость и производительность | **Не объявлять неподдерживаемым без проверки:** возможная переносимость DX12 пути зависит от версии и backend | Предпочесть переносимый backend, если доказан; иначе независимая DX12 compute реализация |
| Частицы, искры, ветер, электрические дуги | Универсальные DX12 shaders плюс опциональные advanced эффекты | На старте старые эффекты + безопасные общие DX12 улучшения | Единый GPU particle/lightning pipeline с профилями качества |
| Лужи | Упрощённые рябь/отражения/брызги | Та же облегчённая система | Не симулировать каждую лужу как PBD-жидкость |

**Вводный расчёт нагрузки (гипотезы для бюджетирования, не бенчмарки):**
- Для 60 FPS весь кадр имеет **16,67 мс**, для 30 FPS — **33,33 мс**. Дополнительный VFX-pass конкурирует с DX12/DXR освещением, тенями, прозрачностью и постобработкой; он не получает весь бюджет кадра.
- Пример проектного лимита **+1,0 мс GPU** на один локальный advanced эффект при 60 FPS составляет **около 6%** от бюджета кадра; **+2,0 мс** — около **12%**. Это **предлагаемые пороги исследования**, а не измеренная стоимость Flow/PhysX. При нескольких эффектах учитывать их суммарную стоимость и пики.
- Для объёмной сетки удвоение разрешения **по каждой из трёх осей** даёт до **8× больше ячеек** при равной плотности и без учёта sparse-оптимизаций; реальные время/память зависят от solver, активных блоков, формата данных и числа итераций. Нельзя напрямую переносить этот коэффициент на FPS.
- Прототипировать уровни качества с фиксированными ограничениями на активные источники, частицы, объёмные области, разрешение, iterations, GPU memory; отключать/упрощать дорогие эффекты при перегрузке, не изменяя игровую логику.

**Временная политика AMD:** на первых этапах сохранять рабочие классические огонь, дым и воду и добавлять лишь общие эффекты, которые реально прошли AMD GPU тест. Отсутствие CUDA означает отсутствие **CUDA backend**, а не отсутствие вычислительных блоков или невозможность GPU симуляции на AMD. Это **временный компромисс**, с отдельным обязательным этапом VFX3 и последующим повышением качества; не обещать сроки до профилирования.

**Возможные последствия и меры:** (1) визуальный разрыв NVIDIA/AMD — показывать режимы Classic/Enhanced/Advanced, документировать ограничения и план сокращения разрыва; (2) рост GPU/VRAM, особенно в сценах с несколькими кострами и аномалиями — per-frame budgets, LOD, culling и лимит одновременных симуляций; (3) vendor lock-in и обслуживание двух backend — единый Environmental VFX API и тесты одинаковых событий; (4) DX12↔CUDA interop, синхронизация ресурсов и драйверные различия — изолированный прототип, измерение копирований/барьеров, отказ от недоказанных предположений; (5) расхождение визуальной и игровой физики — authoritative gameplay на старых системах, advanced VFX только визуальные; (6) поломка DX11 fallback — отдельная regression matrix.

**Гейт перед обещаниями:** зафиксировать версии NVIDIA Flow/PhysX, лицензии, список NVIDIA/AMD GPU, проверить runtime capability detection; замерить p50/p95 GPU ms, VRAM, CPU overhead, стабильность и качество на одинаковых сценах (один костёр, несколько костров, вода/лужи, затем аномалии). Только после этих измерений оценивать реальные преимущества и стоимость универсального AMD пути.

**Статус:** PLANNED / NOT IMPLEMENTED. Реализация, приёмка и независимое GPU evidence потребуются отдельно.


## Направление AUDIO — Enhanced X-Ray Audio (решение 2026-10-25; PLANNED)

**Цель:** улучшить существующий звуковой движок OGSR/X-Ray, **не переписывая его с нуля**. Сохранить OpenAL/OpenAL Soft, оригинальные звуковые события, пространственное позиционирование, звуковую память AI, акустическую геометрию SOM и EAX/EFX, если соответствующие пути подтверждены аудитом. Улучшения звука не должны зависеть от DX12, PhysX, CUDA или производителя GPU и не блокируют DX12 F4.0.

**Основание для планирования:** предварительный статический аудит `xrSound/SoundRender_Core.cpp`, `SoundRender_CoreA.cpp`, `SoundRender_TargetA.cpp`, `SoundRender_Core_Processor.cpp`, `SoundRender_Emitter.cpp`, `SoundRender_Source_loader.cpp`, `SoundRender_Environment.cpp`, а также точек интеграции в `xrGame`. В коде присутствуют OpenAL-источники и параметры затухания, `geom_DB.ray_query` / `geom_SOM` для окклюзии, обработка звуковых целей и потоковая OGG-загрузка. **Наличие кода не является runtime PASS**: поведение, конфигурация и фактическое включение функций требуют проверки.

| Этап | Реализация | Критерий приёмки |
|---|---|---|
| **AUDIO0 — read-only аудит и baseline** | Проследить `xrSound` → OpenAL backend → `xrGame` и AI sound memory, приоритеты emitters/targets, `geom_SOM`, EAX/EFX, OGG streaming, параметры `psSoundTargets` и буферов; сверить конфиги, реальные ассеты и версии библиотек. | Карта вызовов с путями/функциями; воспроизводимые тестовые сцены (коридор, улица, дверь/стена, много NPC, дождь/аномалии), CPU/память/latency/voice counts и аудиозаписи Classic baseline. |
| **AUDIO1 — надёжность и численная устойчивость** | Проверить нулевую/малую дистанцию в `get_occlusion_to/get_occlusion`, контракты `Fvector::div`, некорректные OGG/seek/read, lifetime звуков, device reopen при активных потоках и EFX; исправлять только доказанные ошибки. | Unit/integration тесты совпадающих позиций, испорченных файлов, переключения аудиоустройства и остановки/рестарта потоков; без падений и регрессий. |
| **AUDIO2 — улучшенная окклюзия и материалы** | Расширить текущие `geom_SOM`/ray-query механизмы частотно-зависимым ослаблением (например, low-pass через препятствия), с совместимостью со старыми SOM и сохранением legacy scale при отсутствии данных. | A/B тесты бетон/лёгкая преграда/открытая линия видимости; контролируемая разница спектра и громкости, стабильность при перемещении, ограниченный CPU budget. |
| **AUDIO3 — окружения и реверберация** | Исследовать существующие EAX/EFX environment transitions, зоны помещений/тоннелей/подвалов, отражения и плавные переходы; не обещать полноценную wave acoustics без отдельного прототипа. | Сравнение Classic/Enhanced в помещениях и на улице, тесты устройств/OpenAL Soft, без резких скачков и с корректным fallback при отсутствии расширений. |
| **AUDIO4 — масштабирование и профилирование** | Измерить фактическую систему `priority()`, `psSoundTargets`, OGG buffering и стоимость acoustic ray queries; внедрять voice budgeting, query caching и LOD только по профилю. | Сцены с множеством NPC/выстрелов/аномалий/дождём, p50/p95 CPU time, количество активных/виртуальных voices, underruns, отсутствие потерь критичных звуков. |
| **AUDIO5 — расширенное распространение (исследование)** | Оценить ограниченные отражения, дифракцию за углами и акустические материалы, используя существующую геометрию уровня; API не связывать с DX12. | Изолированный прототип, бюджет CPU/памяти, проверка слышимости и AI; решение о внедрении только после сравнения с AUDIO2–AUDIO4. |

**Инварианты:** звук и AI должны получать согласованные игровые события, но визуальная/акустическая детализация не должна скрытно менять поведение NPC; DX11 и Classic fallback обязательны; исходные аудиоресурсы и узнаваемая атмосфера Зоны сохраняются. Использовать feature flags и сравнение Classic/Enhanced. Нельзя считать `psSoundTargets = 256` доказательством 256 одновременно слышимых звуков, а номинальные три блока по 400 мс — задержкой старта 1,2 с без runtime измерений.

**Статус:** PLANNED / NOT IMPLEMENTED. Первый следующий шаг — AUDIO0, без преждевременной замены OpenAL backend.
