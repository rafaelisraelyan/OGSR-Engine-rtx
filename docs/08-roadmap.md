# 08 — План работ

> **Актуальная ветка документа:** `prypiat` (форк ветки CoP `main_cop_cs`).
> **Дата обновления планов:** 2026-10-16. Исторические статусы A/B1/F и их
> даты оставлены как ранее зафиксированные отчёты, а не результат новой
> проверки GPU/CI. Изменение roadmap **не означает** реализации или тестирования
> добавленных этапов.

**Конечная техническая цель:** сохранить стабильный DX11/R4 и поэтапно получить
**нативный DirectX 12**, затем **аппаратный DXR** (сначала RT-тени, затем — при
успешных измерениях — отражения/глобальное освещение).

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

### Сводка статусов (исторические записи 2026-10-14/15; архитектурный план дополнен 2026-10-16)

| Этап | Статус | Следующее действие |
|---|---|---|
| A · DLSS 310.9.1 | ✅ Подтверждён: сборка и запуск CoP | Сохранить baseline и логи |
| B1 · NIS submodule | ✅ Подтверждён в исходном roadmap | Зафиксировать SHA и проверить CI на чистой машине |
| B2–B4 · NIS в движке | ⬜ Не реализован | Проверить compute-dispatch и ресурсы SDK |
| C · Тюнинг DLSS | ⬜ Не реализован | Качество и семантика флагов |
| D · DX11 SSGI / Hi-Z | ⬜ Исследование | D0 → D1 → D2, решение по результатам |
| E · Техдолг | 🔎 Диагностика | Отдельные согласованные изменения |
| F · Нативный DX12 | 🛠 В работе (статусы F ниже — исторический снимок) | После F3: F3.5 CPU-side многопоточная архитектура перед F4 |
| F3.2 · GR Job System (DX12 only) | 🛠 Локальная интеграция Stage 1 по отчёту, не закоммичена | C.1: ownership/lifecycle; затем DX12 Smoke runtime/CPU scene |
| F3.5 · Multithreaded Renderer Foundation | ✅ ADR принят (`docs/dx12-f3-5-architecture-adr.md`), реализация не начата | F3.5.1 — representative snapshot prototype |
| F3.5.1 · Representative snapshot prototype | ⏭ NEXT / NOT STARTED | Расширить snapshot представительными полями X-Ray; синтетические `(id, generation)`; не трогать R4 |
| G · DXR | 🆕 План, зависит от F | G0: проверка возможностей GPU/драйвера |
| H · Многопоточность | ⬜ План | H0: базовые профили, H1 после RT-теней и отражений |
| I · World Streaming | ⬜ Концепция | I0: аудит X-Ray ресурсов/уровней и ownership |
| J · Высота / LOD / растения / тени | ⬜ Концепция | J0: инвентаризация LOD/detail/CSM |
| K · Seamless World | ⬜ Возможность на базе I/J | K0: формат регионов и A-Life/AI constraints |
| L · DX12/DXR portability / IX-Ray | ⬜ Предложение | L0: лицензии, сравнение API и границ интеграции |

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

## Этап F — Нативный DX12 (новое направление; пока НЕ реализован)

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
| F4 — игровой кадр | ⬜ NOT STARTED | — |
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

**Статус на 2026-10-19:** ✅ **F3.5.0 — ADR ПРИНЯТ** (`docs/dx12-f3-5-architecture-adr.md`), реализация не начата. ⏭ **F3.5.1 — NEXT / NOT STARTED**: это **representative snapshot prototype** (раздел 9 ADR), а не интеграция реальных игровых объектов — реальный обход сцены остаётся за F4. Пункт не подтверждает реализацию snapshot или multi-list renderer.

**Ограничения, сохраняемые без изменений:** ручной визуальный осмотр окна, интерактивный minimize/restore и специализированный race detector (TSan) **не выполнены**; отсутствие гонок не доказано. Синтетические замеры F3.2C.2 — не FPS и не игровой рендер.

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

### Исследовательская пометка по архитектуре освещения (2026-10-16)

**Статус: обсуждается, не утверждено к реализации.** В качестве ориентиров для возможной архитектуры непрямого освещения OGSR RTX изучаем опубликованные решения 4A Games в Metro Exodus Enhanced Edition: гибридная raster + ray tracing схема, RTGI, DDGI/временное накопление освещения, упрощённое представление материалов для GI, гибридные SSR/RT-отражения и оптимизацию GPU. Это идеи для оценки после появления полноценного игрового кадра и измерений, **не новый этап, не обязательство внедрить DDGI и не изменение порядка F4 → G3/G4 → опциональный G5**. Сохранить совместимость Legacy Materials и каноничную визуальную атмосферу X-Ray.

**Первоисточники 4A Games для быстрого возврата:**
- [In-Depth Technical Dive into Metro Exodus PC Enhanced Edition](https://www.4a-games.com.mt/4a-dna/in-depth-technical-dive-into-metro-exodus-pc-enhanced-edition) — архитектура RTGI, DDGI, материалы, отражения.
- [Everything Technical about Metro Exodus Upgrade for PlayStation 5 and Xbox Series X|S](https://www.4a-games.com.mt/4a-dna/everything-technical-about-metro-exodus-upgrade-for-playstation-5-and-xbox-series-x-s) — оптимизация рендеринга и производительности.

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
    F4: перенос игрового кадра → F5: temporal/DLSS/FSR3
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
