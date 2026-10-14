# 08 — План работ

> **Актуально для ветки `main_cop_cs`.** Этап A выполнен и подтверждён
> сборкой. Этап B (NIS) — следующий по окупаемости.

Приоритеты согласованы с вами: **DLSS-обновление + NIS + SSGI + документация**.

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

**Приоритет:** высокий · **Риск:** низкий · **Сложность:** недели

### Зачем

NIS — официально поддерживаемый NVIDIA пространственный апскейлинг + шарпинг,
идущий **одним шейдером на DX11**. Для движка, у которого DLSS доступен
только на NVIDIA, это логичный fallback для AMD/Intel и старых карт.

`README.md` самого SDK: *«It contains compute shaders that can be integrated
with **DX11**, DX12, and Vulkan.»* — цитата проверена локально.

Он **уже лежит в дереве** как submodule `NVIDIAImageScaling` (sha `35e13ba3`),
но `Update_Components.cmd:26` клонирует **без `--recurse-submodules`**.
Проверка: папка `.../DLSS/NVIDIAImageScaling/` **есть, но 0 элементов**;
`git submodule status` → `-35e13ba3…` — submodule не инициализирован.
Это баг скрипта, а не дизайна.

**Статус B1:** ✅ **выполнено 2026-10-14** — скрипт поправлен, сабмодуль
`NVIDIAImageScaling v1.0.3` активирован в существующем дереве (333 файла).

### B1. Починить скрипт — ✅ СДЕЛАНО

```diff
-  git clone --branch v310.9.1 --depth 1 https://github.com/NVIDIA/DLSS.git 3rd_party/Src/NVIDIA_DLSS/DLSS
+  git clone --branch v310.9.1 --depth 1 --recurse-submodules https://github.com/NVIDIA/DLSS.git 3rd_party/Src/NVIDIA_DLSS/DLSS
```

Активация сабмодуля в существующем дереве (без перекачки 2 ГБ):
```cmd
git -C 3rd_party\Src\NVIDIA_DLSS\DLSS submodule update --init --recursive
```

### B2. `.cs`-шейдеры

Текущее состояние: `.cs`-файлов в gamedata **ноль**, хотя
`ShaderTypeTraits<SCS>` (`ShaderResourceTraits.h:47-63`) умеет `cs_5_0`,
и `CResourceManager` имеет пул `m_cs` (`ResourceManager.h:39-49`).

1. Взять `.hlsl` из NIS, скомпилировать в `cs_5_0` (fxc из Windows SDK).
2. Положить в `Game/Resources_CoP_1.6.02/gamedata/shaders/r3/`.
3. Создать через `s_nis.create("nis_upscale")` в конструкторе `CRenderTarget`
   (`r4_rendertarget.cpp:321+`).

> ⚠️ Это будет **первый** compute-шейдер в проекте. Нужно проверить,
> что диск-кэш в `r4.cpp:823-869` корректно обрабатывает `.cs`,
> и что `ShaderResourceTraits.h:137-144` правильно резолвит путь
> через `getShaderPath()`.

### B3. Обёртка

По образцу `Fsr3Wrapper` (`RenderTargetPhaseAA.cpp:685-851`):

```
DispatchInputs  →  color = rt_Generic_0, sharpness
Output          →  rt_Generic_combine
```

NIS — однопроходный, без history. Значит: не нужны dilated-ресурсы и
`RequestHistoryReset`; нужен `u_setrt` на display-разрешение перед вызовом;
каскад в `PhaseAA()` (`:938-948`) расширяется: `DLSS → NIS → FSR3 → TAA`.

### B4. Интерфейс

Добавить в `pp_aa_mode_token` (`xrRender_console.cpp:37-47`) и `enum`
(`xrRender_console.h:264-272`) значение `NIS`.

**Обязательно синхронизировать Lua-UI** —
`Game/Resources_CoP_1.6.02/gamedata/Scripts/ui_mm_opt_video_adv.script`:
строки `:27-29` объявляют `dlss_quality` / `dlss_preset` / `fsr3_quality`;
строка `:174` читает `r_aa_mode`. Новый режим должен появиться и там,
иначе меню разъедется с реальным состоянием.

### Критерии приёмки

- `r_aa_mode <дли новый токен>` → в логе нет ошибок, картинка полноэкранная.
- Переключение между режимами не падает и корректно откатывается.
- Работает на карте без DLSS (проверяется логом `!![DLSS] DLSS NOT AVAILABLE`).
- Опция появляется в меню опций и применяется без ручного `vid_restart`.

### Риски

| Риск | Оценка | Митигация |
|---|---|---|
| Точный тег/структура NIS | Средняя | Взять commit id из `.gitmodules` DLSS-репо |
| Первый `.cs` в проекте — неисследованный путь | Средняя | Готовить покадрово, с ImGui-отладкой |
| Двойной sharpen с CAS | Низкая | Отключить `ps_r_cas` в режиме NIS |
| Меню опций рассинхронизируется | Низкая | Правка `ui_mm_opt_video_adv.script` в том же коммите |

---

## Этап C — Тюнинг текущего DLSS

**Приоритет:** средний · **Риск:** низкий · **Сложность:** дни

### C1. `AlphaUpscaling`

`RenderTargetPhaseAA.cpp:341`. В CoP много травы, дождя и дымки —
прозрачные пиксели сейчас обрабатываются наихудшим образом.

### C2. `MVJittered`

`:337`. Джиттер реально применяется (`:418-419`), флаг лишь уточняет
семантику MV.

### C3. `DoSharpening` — требует решения

`:332`. Без него `InSharpness` игнорируется (`:316-317`), но движок
имеет собственный CAS (`ProcessCAS`, `:611`), вызываемый в `PhaseAA()`
для всех режимов кроме SMAA (`:966-967`).

Варианты: включить флаг и обнулить `ps_r_cas` в режиме DLSS; либо
оставить как есть; либо сделать выбор через консольную переменную.
Рекомендация — вариант 3, со значением по умолчанию по результатам теста.

### C4. Задокументировать DLAA как дефолт

`xrRender_console.cpp:58`. Пользователь может не понимать, почему
при «DLSS» нет апскейлинга. Отражено в `ui_mm_opt_video_adv.script`.

---

## Этап D — SSGI / Hi-Z на compute-шейдерах

**Приоритет:** высокий по ценности, **высокий** по сложности · **Месяцы**

**Это единственный способ получить трассируемые тени и косое освещение
в DirectX 11.** Аппаратные лучи требуют DX12.

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
| D0 | Гейт | Трассировка-прототип отдельным `.cs` + UAV, вывод через ImGui | Работающий compute в проекте |
| D1 | Hi-Z | Пирамида мип-уровней из `rt_zbuffer`; нужна копия в R32_FLOAT | Визуализация пирамиды в ImGui |
| D2 | DDA | Ray marching по Hi-Z в compute | Лучи «стоят», но упираются в экран |
| D3 | Геометрия | Инлайн-буферы треугольников + material ID из `rt_Position.w` | Лучи упираются в реальную геометрию |
| D4 | Тени | Трассировка к CSM, вывод hit distance | Тени от точечных источников |
| D5 | Денойз | Временной + пространственный | Чистая картинка |
| D6 | Интеграция | Compile-time флаг в `r4.cpp:674-906`, ветка в `phase_combine()` | Флаг в консоли |

**Начальный толчок:** использовать `GL_SampleF`/декодирование нормали
из `rt_Position.xy` — готового кода на CoP нет, писать с нуля.

### Критерий приёмки

Видимое отличие: корректные попадания в геометрию, отсутствие «мыла»
на границах (денойзер), стабильность при движении камеры.

### Риски

| Риск | Оценка |
|---|---|
| Hi-Z требует конвертации depth | Средняя |
| Неравномерная геометрия усложняет BVH/гриды | Высокая |
| Денойзер в DX11 — писать с нуля (NRD требует DX12) | Средняя |
| Позиция в пространстве глаза — нужно обратное преобразование | Средняя |

### Рекомендация

Начинать только с D0–D2. Если Hi-Z + DDA заработают и покажут приемлемое
качество — продолжать. Иначе остановиться.

---

## Этап E — Гигиена репозитория

**По договорённости — только диагностика, без правок.**

| Пункт | Файл | Что предложить |
|---|---|---|
| Закрепить коммиты | `Update_Components.cmd` | Заменить 12 веток на SHA |
| Добавить `--recurse-submodules` | `Update_Components.cmd:26` | Довытащить NIS (этап B1) |
| Условный `dbghelp.dll` | `XR_3DA.vcxproj:395-397` | `IgnoreExitCode="true"` или `Condition` на `Exists(...)` |
| `ReleaseTracyProfiler` как настоящая конфигурация | `Engine.sln` | Добавить в SolutionConfigurationPlatforms |
| Поправить устаревший комментарий | `build_config_defines.h:63-64` | Сказать 128, а не 768 |
| Убрать мёртвый код | `xrRender/*`, `xrPhysics.cpp`, `lua_studio.cpp`, `PHSynchronize.cpp` | см. §B |
| Защитить `Game/` | `.gitignore` | Добавить `Game/Resources_CoP_1.6.02/bin_x64/` |
| Добавить `dplay32.dll`/`dpwsockx.dll` в перечень рантайм-DLL | `XR_3DA.vcxproj` | документировать зависимость |
| dependabot на зависимости | `.github/dependabot.yml` | — |
| Синхронизировать `Configs/` и `Scripts` с регистром | `Game/` | регистр критичен вне Windows |

---

## Порядок выполнения

```
 A ✅ ВЫПОЛНЕНО  ── DLSS 310.7.0 → 310.9.1
                     + сборка CoP подтверждена
                          │
                          ▼
 B1 (день)      ── Update_Components.cmd + --recurse-submodules
                          │
                          ▼
 B2-B4 (недели) ── NIS: шейдеры, обёртка, режим в C++ и в Lua-UI
                          │
                          ├──▶ C (дни) ── тюнинг DLSS-флагов
                          │
                          ▼
 D0-D2 (недели-месяцы) ── SSGI: гейт, Hi-Z, DDA
                          │
                          ▼
 D3-D6 (месяцы) ── геометрия, тени, денойз, интеграция
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

---

## Игровые данные (блокёр для рантайм-тестов)

Для CoP нужна **распакованная** `gamedata` именно Call of Pripyat
(не SoC). Состав оверлея форка — `Game/Resources_CoP_1.6.02/gamedata/`:

```
anims/   6    levels/   3    Configs/ 21
scripts/ 9    shaders/  552  textures/ 50 (72,5 МБ)
```

Чего в репозитории **нет** и что приходит из игры: `fsgame.ltx`,
`system.ltx`, `game.ltx`, все конфиги CoP, сотни оригинальных скриптов,
геометрия уровней, звуки, основной текстурный объём.