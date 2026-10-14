# 08 — План работ

Приоритеты согласованы: **DLSS-обновление + NIS + SSGI + документация**.
Этот документ фиксирует порядок, критерии приёмки и риски.

---

## Этап A — Обновить DLSS SDK 310.7.0 → 310.9.1

**Приоритет:** высокий · **Риск:** низкий · **Сложность:** дни
**Статус:** ✅ **выполнено 2026-10-14** — правка внесена, совместимость проверена (см. ниже)

### Что меняется

`Update_Components.cmd:26`:

```diff
-git clone --branch v310.7.0 --depth 1 https://github.com/NVIDIA/DLSS.git 3rd_party/Src/NVIDIA_DLSS/DLSS
+git clone --branch v310.9.1 --depth 1 https://github.com/NVIDIA/DLSS.git 3rd_party/Src/NVIDIA_DLSS/DLSS
```

**Одна строка. Правок в C++ не требуется** — обратная совместимость
**проверена**, а не предположена:

| Проверка | Результат |
|---|---|
| 43 символа `NVSDK_NGX_*` / `NGX_*` из движка в заголовках 310.9.1 | 43/43, отсутствующих 0 |
| `nvsdk_ngx_helpers.h` тянет `nvsdk_ngx_helpers_d3d.h` | да, `:116` |
| 4 `.lib` из `AdditionalLibraryDirectories` | все на месте |
| `rel/` + `dev/` DLL для post-build | обе на месте |
| `NVSDK_NGX_DLSS_Hint_Render_Preset_*` | без перенумерации |
| `NVSDK_NGX_PerfQuality_Value` | идентичен побайтно |

### Что это даёт

| Версия | Фича |
|---|---|
| 310.9.1 | **Preset F** для DLSS Ray Reconstruction (Transformer mode) |
| 310.6.0 | **Frame Generation 5x/6x**, FG UI Recomposition (Preset B) |
| 310.5.3 | CUDA-поддержка для DLSS RR |
| 310.5.0 | **Preset L и M** для DLSS Super Resolution |

Preset L/M включатся **автоматически**: движок определяет их доступность
через `DlssDllVersionAtLeast(310, 5, 0)` (`RenderTargetPhaseAA.cpp:248`).

### План

1. ✅ Править `Update_Components.cmd` — **сделано**
2. ⬜ Запустить `Update_Components.cmd` (или только строку 26)
3. ⬜ Пересобрать `Release|x64`
4. ⬜ Запустить, проверить лог

### Критерии приёмки

```
--[DLSS] loaded DLL version: [310.9.1]        ← не 310.7.0
--[DLSS] available presets for DLL [310.9.1]: Default/F/J/K/L/M
--[DLSS] current preset: [...]
--[DLSS] quality: [...], output: [...], recommended render: [...], sharpness: [...]
--[DLSS] physical render domain: [W x H] -> [W x H]
```

Плюс в консоли:

```
r_aa_dlss_version      → 310.9.1
r_aa_dlss_preset l     → принимается без ручной настройки
r_aa_dlss_preset m     → принимается
```

### Риски

| Риск | Оценка | Митигация |
|---|---|---|
| Смена поведения пресетов по умолчанию | Низкая | Сравнить скриншоты `r_aa_dlss_preset default` до/после |
| Новые 59-МБ DLL в артефакте | Нет | Размер не критичен |
| Регресс в `QueryOptimalSettings` | Низкая | Логируемые значения render-разрешения |

### Откат

Вернуть одну строку в `Update_Components.cmd` и перезапустить скрипт.

---

## Этап B — NVIDIA Image Scaling

**Приоритет:** высокий · **Риск:** низкий · **Сложность:** недели

### Зачем

NIS — официально поддерживаемый NVIDIA пространственный апскейлинг + шарпинг,
идущий **одним шейдером на DX11**. Для движка, у которого DLSS доступен
только на NVIDIA, это логичный fallback для AMD/Intel и для старых карт.

README `NVIDIA/DLSS`: *«It contains compute shaders that can be integrated
with **DX11**, DX12, and Vulkan.»*

Он **уже лежит в дереве** как submodule `NVIDIAImageScaling`
(sha `35e13ba3`), но `Update_Components.cmd` клонирует без
`--recurse-submodules`.

### B1. Починить скрипт

```diff
-  git clone --branch v310.9.1 --depth 1 https://github.com/NVIDIA/DLSS.git 3rd_party/Src/NVIDIA_DLSS/DLSS
+  git clone --branch v310.9.1 --depth 1 --recurse-submodules https://github.com/NVIDIA/DLSS.git 3rd_party/Src/NVIDIA_DLSS/DLSS
```

Альтернатива (если не хочется тянуть лишнее) — отдельный клон:

```cmd
RD /s /q 3rd_party\Src\NVIDIAImageScaling
git clone --branch v3.x --depth 1 https://github.com/NVIDIAGameWorks/NVIDIAImageScaling.git 3rd_party/Src/NVIDIAImageScaling
```

> Точный тег NIS нужно уточнить на момент выполнения — submodule в `DLSS`
> указывает на конкретный коммит.

### B2. Заложить `.cs`-шейдеры

Текущее состояние: `.cs`-файлов в gamedata **ноль**, хотя
`ShaderTypeTraits<SCS>` (`ShaderResourceTraits.h:47-63`) умеет
собирать `cs_5_0`, и `CResourceManager` имеет пул `m_cs` (`ResourceManager.h:39-49`).

Нужно:
1. Взять `.hlsl` из NIS и скомпилировать в `cs_5_0` (fxc от Windows SDK).
2. Положить результат в `Game/Resources_SoC_1.0006/gamedata/shaders/r3/`.
3. Создать через `s_nis.create("nis_upscale")` в конструкторе
   `CRenderTarget` (`r4_rendertarget.cpp:321-338`).

> ⚠️ Это будет **первый** compute-шейдер в проекте. Нужно проверить,
> что диск-кэш в `r4.cpp:823-869` корректно обрабатывает `.cs`
> (он общий для всех типов) и что `ShaderResourceTraits.h:137-144`
> правильно резолвит путь через `getShaderPath()`.

### B3. Обёртка

По образцу `Fsr3Wrapper` (`RenderTargetPhaseAA.cpp:685-851`):

```
DispatchInputs  →  color = rt_Generic_0, sharpness
Output          →  rt_Generic_combine
```

NIS — однопроходный, без history. Значит:
- не нужны dilated-ресурсы и `RequestHistoryReset`;
- нужен `u_setrt` на display-разрешение перед вызовом;
- каскад отказоустойчивости в `PhaseAA()` (`:938-948`) расширяется:
  `DLSS → NIS → FSR3 → TAA`.

### B4. Интерфейс

Добавить в `pp_aa_mode_token` (`xrRender_console.cpp:37-47`) и
`enum` (`xrRender_console.h:265-272`) значение `NIS`.
Соответствующий Lua-UI — `gamedata/scripts/ui_mm_opt_video*.script`.

### Критерии приёмки

- `r_aa_mode nis` → в логе нет ошибок, картинка полноэкранная, не мыльная.
- Переключение `r_aa_mode` между `dlss`/`nis`/`fsr3`/`taa`/`smaa`
  не падает и корректно откатывается.
- Работает на карте без DLSS (проверяется логом `!![DLSS] DLSS NOT AVAILABLE`).

### Риски

| Риск | Оценка | Митигация |
|---|---|---|
| Точный тег/структура NIS неизвестна | Средняя | Изучить submodule на момент выполнения |
| Первый `.cs` в проекте — неисследованный путь | Средняя | Готовить покадрово, с ImGui-отладкой |
| Двойной sharpen с CAS | Низкая | NIS сам даёт шарпинг — отключить `ps_r_cas` в режиме NIS |

---

## Этап C — Тюнинг текущего DLSS

**Приоритет:** средний · **Риск:** низкий · **Сложность:** дни

### C1. `AlphaUpscaling`

`RenderTargetPhaseAA.cpp:341`. В движке много травы, дождя и дымки —
прозрачные пиксели сейчас обрабатываются наихудшим образом.

### C2. `MVJittered`

`RenderTargetPhaseAA.cpp:337`. Джиттер реально применяется (`:418-419`),
флаг лишь уточняет семантику MV.

### C3. `DoSharpening` — **требует решения**

`RenderTargetPhaseAA.cpp:332`. Без него `InSharpness` игнорируется.
Но в движке есть собственный CAS (`ProcessCAS`, `:611-621`), вызываемый
в `PhaseAA()` для всех режимов кроме SMAA (`:966-967`).

**Варианты:**
1. Включить флаг и обнулить `ps_r_cas` в режиме DLSS.
2. Оставить как есть, полагаясь на CAS.
3. Сделать выбор через консольную переменную.

Рекомендация — **вариант 3**, с значением по умолчанию, которое даёт
заметно лучший результат на практике.

### C4. Задокументировать DLAA как дефолт

`ps_r_dlss_quality = DLSS_QUALITY_DLAA` (`xrRender_console.h:58`).
Пользователь может не понимать, почему при «DLSS» нет апскейлинга.
Стоит отразить в UI-подсказке.

---

## Этап D — SSGI / Hi-Z на compute-шейдерах

**Приоритет:** высокий по ценности, **высокий** по сложности · **Месяцы**

**Это единственный способ получить трассируемые тени и косое освещение
в DirectX 11.** Аппаратные лучи требуют DX12 — см.
[06-rtx-feasibility.md](06-rtx-feasibility.md).

### Что уже есть

| Актив | Где |
|---|---|
| Компиляция `cs_5_0` | `ShaderResourceTraits.h:47-63` |
| Пул CS-шейдеров | `ResourceManager.h:39-49` |
| UAV-таргеты | `rt_Generic_scene_scratch`, `rt_Generic_combine` (`r4_rendertarget.cpp:287-292`) |
| MRT-биндинг | `u_setrt` до 4 слотов (`r4_rendertarget.h:236-239`) |
| Отладка буферов в движке | `r4_imgui.cpp:75-77` уже показывает `rt_Color`, `rt_Position`, `rt_Accumulator` |
| Тайминг-инструменты | Tracy с `PIX_EVENT` |

### Что отсутствует

- Ни одного `.cs`-файла (блокер; снимается этапом B).
- Мировых нормалей в G-buffer (`r4_rendertarget_phase_scene.cpp:32`).
- Roughness/metallic — есть только `rt_Color.a` = specular-gloss.
- Равномерной сетки треугольников: геометрия приходит из `IKinematics`
  в формате X-Ray, а не как единый инлайн-буфер.
- Hi-Z пирамиды.
- Денойзера.

### Подэтапы

| # | Подэтап | Содержание | Результат, который видно |
|---|---|---|---|
| D0 | Гейт | Трассировка-прототип отдельным `.cs` + UAV, вывод через ImGui | Работающий compute в проекте |
| D1 | Hi-Z | Пирамида мип-уровней из `rt_zbuffer`; нужна копия в R32_FLOAT | Визуализация пирамиды в ImGui |
| D2 | DDA | Ray marching по Hi-Z в compute | Лучи «стоят», но упираются в экран |
| D3 | Геометрия | Доступ к треугольникам через инлайн-буферы + material ID | Лучи упираются в реальную геометрию |
| D4 | Тени | Трассировка к CSM, вывод hit distance | Тени от точечных источников |
| D5 | Денойз | Временной + пространственный | Чистая картинка |
| D6 | Интеграция | Compile-time флаг в `r4.cpp:674-906`, ветка в `phase_combine()` | Флаг `ssfx_rtgi` в консоли |

### Критерий приёмки этапа в целом

```
r_ssr_enabled 1
ssfx_rtgi 1
```
дают видимое отличие: корректные попадания в геометрию, отсутствие
«мыла» на границах (денойзер), стабильность при движении камеры.

### Риски

| Риск | Оценка |
|---|---|
| Отсутствие нормалей в G-buffer требует изменения MRT или отдельного прохода | Высокая |
| Неравномерная геометрия усложняет BVH/гриды | Высокая |
| Денойзер в DX11 — писать с нуля (NRD требует DX12) | Средняя |
| Смещение позиции (в пространстве глаза, а не мира) | Средняя |

### Рекомендация

Начинать **только** с D0–D2. Если Hi-Z + DDA заработают и покажут
приемлемое качество — продолжать. Иначе остановиться и не тратить
месяцы.

---

## Этап E — Гигиена репозитория

**По договорённости — только диагностика, без правок.**

| Пункт | Файл | Что предложить |
|---|---|---|
| Закрепить коммиты | `Update_Components.cmd` | Заменить 12 веток на SHA |
| Убрать `--depth 1` или добавить проверки | `Update_Components.cmd` | `errorlevel`-проверка после каждого клона |
| Условный `dbghelp.dll` | `XR_3DA.vcxproj:403-405` | `<Exec ... IgnoreExitCode="true">` или `Condition` на `Exists(...)` |
| `ReleaseTracyProfiler` как настоящая конфигурация | `Engine.sln`, props | Добавить в SolutionConfigurationPlatforms |
| Убрать мёртвый код | `xrRender/*`, `DirectXMesh`, `3rd_party_properties.props` | см. [07-known-issues.md](07-known-issues.md) §B |
| Защитить `Game/` | `.gitignore` | Добавить `Game/Resources_SoC_1.0006/bin_x64/` |
| dependabot на зависимости | `.github/dependabot.yml` | Добавить экосистему `github-actions` для каждого клона |

---

## Порядок выполнения

```
 A (дни)    ── DLSS 310.7.0 → 310.9.1
              1 строка, пересборка, проверка лога
                   │
                   ▼
 B1 (день)   ── починить Update_Components.cmd (--recurse-submodules)
                   │
                   ▼
 B2–B4 (недели) ── NIS: шейдеры, обёртка, режим в UI
                   │
                   ├──▶ C (дни) ── тюнинг DLSS-флагов
                   │
                   ▼
 D0–D2 (недели–месяцы) ── SSGI: гейт, Hi-Z, DDA
                   │
                   ▼
 D3–D6 (месяцы) ── геометрия, тени, денойз, интеграция
```

---

## Что делать не надо (пока)

| Идея | Почему нет |
|---|---|
| Порт рендерера на DirectX 12 | Отдельный проект на год+; не окупается |
| DLSS Frame Generation | Официально только DX12/Vulkan (StreamLine) |
| DLSS Ray Reconstruction | Нужен DX12 + RT cores |
| NVIDIA Reflex | Только через StreamLine → DX12 |
| Аппаратная трассировка | DXR не существует в D3D11 |
| «Просто включить RTX» | Не существует API для этого |

Обоснование — [06-rtx-feasibility.md](06-rtx-feasibility.md).