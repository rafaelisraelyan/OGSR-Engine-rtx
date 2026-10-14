# 07 — Найденные проблемы и техдолг

> **Проверено против ветки `main_cop_cs`.** Все номера строк подтверждены.
> Исправлено против SoC-версии: A1 сдвинулся на `:395-397`, флагов `/we####`
> 15 (не 16). Добавлены CoP-специфичные проблемы — раздел H.

Проверено по исходникам. Каждый пункт воспроизводим.

---

## A. Сборка

### A1. `dbghelp.dll` безусловный post-build — ломает отчёт о сборке

**Где:** `ogsr_engine/xr_3da/XR_3DA.vcxproj:395-397`

```xml
:395  <Target Name="PostBuildStep1" AfterTargets="Build">
:396    <Exec Command='copy /Y "$(WindowsSDKDir)Debuggers\$(PlatformShortName)\dbghelp.dll" "$(OutDir)dbghelp.dll"' />
:397  </Target>
```

Нет `IgnoreExitCode`, нет `Condition`. Без компонента «Debugging Tools for Windows»
путь `$(WindowsSDKDir)Debuggers\x64\dbghelp.dll` не существует → `copy` → ненулевой
код → цель падает → **MSBuild reports build FAILED при успешно слинкованном EXE**.

**Это классическая жалоба на сборку OGSR/X-Ray.** Нами проверено на практике:
движок собирался, `xrEngine.exe` появлялся, а `rc=1` приходил ровно отсюда.

### A2. Воспроизводимость сборки отсутствует

`Update_Components.cmd` клонирует 12 репозиториев через `--depth 1`,
**без закрепления коммитов**. 8 из 12 — движущиеся ветки:

| Зависимость | Пин |
|---|---|
| `lz4/lz4` | **`dev`** |
| `facebook/zstd` | **`dev`** |
| `microsoft/DirectXMath` | `main` |
| `OGSR/DirectXMesh` | `main` |
| `cameron314/concurrentqueue` | `master` |
| `AgentD/squashfs-tools-ng` | `master` |
| `tzcnt/cpputils` | `main` |
| `OGSR/discord-rpc` | `master` |

Сборка сегодня и через полгода даст разный бинарник из одного коммита.

### A3. NIS не качается (баг скрипта)

Репозиторий `NVIDIA/DLSS` содержит submodule `NVIDIAImageScaling`.
`Update_Components.cmd:26` клонирует **без `--recurse-submodules`**.
Проверка: папка `.../DLSS/NVIDIAImageScaling/` **есть, но 0 элементов**;
`git submodule status` → `-35e13ba3…` — submodule не инициализирован.
Ничего реализуемо на DX11 не потеряно, но NIS недоступен.

**Подробнее — [06-rtx-feasibility.md](06-rtx-feasibility.md) §3.2.**

### A4. `Update_Components.cmd` не идемпотентен и не проверяет ошибки

`RD /s /q <путь>` **до** `git clone`. При обрыве сети остаётся
half-populated дерево. Нет `|| exit /b 1`, нет `errorlevel`-проверок;
`pause` в конце маскирует ситуацию.

### A5. `ReleaseTracyProfiler` не существует как конфигурация

`Engine.sln` содержит только `Debug|x64` и `Release|x64` (`:108-109`).
Строка `ReleaseTracyProfiler` есть лишь в `.github/workflows/push.yml` и в
`Engine_*_properties.props:30-39`, где сравнивается с
`$(CONFIGURATION_GA)` — переменной **окружения**.

### A6. Tracy-сборка неотличима от обычной Release на диске

Так как `Configuration` остаётся `Release`, обе вариации пишут в
`bin_x64\` и `ogsr_engine\_LIB\Engine\Release\x64\`.
Дополнительный канал: `generate_build_info.bat:17-19` пишет
`#define CONFIGURATION_GA` в `ogsr_build_info.hpp`.

### A7. 15 предупреждений повышены до ошибок

`Engine_properties.props:15` и `Engine_lib_properties.props:15` — **15** флагов:

```
4566, 4828, 4715, 4189, 4595, 4834, 4018, 4101
4552, 4551, 4477, 4390, 4806, 4002, 4302
```

Только движковые проекты. Любое обновление MSVC, добавляющее предупреждение
из этого списка, ломает сборку.

### A8. `stdcpp20` безусловный

`OgsrBuildProps.props:28` — `<LanguageStandard>stdcpp20</LanguageStandard>`.
На SoC-ветке было условно (`stdcpp23` при VS2026). **На CoP условия нет.**
Это упрощает жизнь, но фиксирует C++20 как ceiling.

### A9. `WindowsTargetPlatformVersion` не закреплён

`OgsrBuildProps.props:16-17` берёт **последний установленный** SDK.
Preview-SDK может сдвинуть заголовки D3D11/D3DCompiler.

### A10. `Game/ShadersSln` жёстко закреплён на v143

`Game/ShadersSln/ShadersSln.vcxproj` — `PlatformToolset v143`,
`WindowsTargetPlatformVersion 10.0`. Не импортирует `OgsrBuildProps.props`.
На машине только с VS2026 не откроется.

### A11. `DISABLE_DBG_ASSERTIONS` включён по умолчанию

`build_config_defines.h:19`. Debug-сборка фактически не assert-тяжёлая.
Плюс `xrCore.h:33-36` принудительно делает `#define NDEBUG` / `#undef DEBUG`
при `_DEBUG` + `DISABLE_DBG_ASSERTIONS`.

### A12. `LuaJIT` не пересобирается при наличии `LuaJIT.lib`

`luajit.vcxproj:37-39` короткозамыкает на `if not exist "..\bin\$(PlatformShortName)\LuaJIT.lib"`.
Без `Rebuild` бинарник останется старым. Плюс Makefile-проект тянет
**EXE**-props (`Engine_properties.props`), получая `/we####` + Tracy.

### A13. Манифест не знает про Windows 11

`xr_3da/manifest.xml` объявляет `supportedOS` только для
Windows 10 / 8.1 / 8 / 7. GUID `{d48ca3fe-…}` отсутствует. DPI в legacy-режиме.

---

## B. Мёртвый код

### B1. `3rd_party_properties.props` не импортируется

Ни один `.vcxproj` его не подключает (импортируют `_lib_` ровно 15 проектов).

### B2. `DirectXMesh` клонируется, но не используется

`Update_Components.cmd:4-5` клонирует `OGSR/DirectXMesh`;
paths добавлены (`xrRender_R4.vcxproj:64`, `:88`), но **нет ни одного
`#include "DirectXMesh.h"`** и нет vcxproj. В клоне есть 11 vcxproj
(Desktop 2019/2022/2026, Meshconvert, GDK) — все не подключены.

### B3. Не компилируемые дубликаты в `xrRender/`

Исключены из `xrRender_R4.vcxproj`:
`ResourceManager_Resources.cpp`, `ResourceManager_Scripting.cpp`,
`SH_RT.cpp`, `SH_Texture.cpp`, `tga.cpp`, `dxParticleCustom.cpp`,
`xrRenderPC_R4/du_sphere_part.cpp`.

> **Ловушка:** правя загрузку шейдеров или RT, редактируйте
> `xrRenderDX10/dx10*`-файлы, а не лежащие рядом `xrRender/*`-аналоги.

### B4. `libsquashfs` собирает write-сторону

Компилирует `block_writer`, `dir_writer`, `meta_writer`,
`write_inode/super/table` — читателю `.sqfs` они не нужны.

### B5. `opcode.lib` в `#pragma comment` под неактивным условием

`xrCDB/OpCoDe/Opcode.h:54-56` ссылается на `Opcode.lib`/`Opcode_D.lib`,
которые никогда не собираются (за `#ifndef BAN_OPCODE_AUTOLINK`).

### B6. Несуществующий include-путь в `xrGame.vcxproj`

`xrGame.vcxproj:82`, `:139` добавляют `…\3rd_party\Src\d3dx\include`,
которого нет. Фактическая раскладка — `3rd_party\Src\d3dx\d3dx\`.

### B7. Неиспользуемая конфигурация imgui

`imgui.vcxproj:8-11` объявляет `Mixed|x64`, который `Engine.sln` не маппит.

### B8. `cleanup.cmd` чистит несуществующие пути

Обрабатывает `.vs\Engine\*` (раскладка **VS2010**) и `bin_x86\`,
который `Engine.sln` уже не может породить (только x64).

### B9. `xrPhysics.cpp` — мёртвый `DllMain`

43 строки с `DllMain` в **StaticLibrary** — никогда не вызывается.
Зато `#pragma comment(lib, "ode.lib")` на `:9` рабочий.

### B10. `PHSynchronize.cpp` и `lua_studio.cpp` не компилируются нигде

Находятся в `xrServerEntities/`, но нет ни в `xrGame.vcxproj`, ни в
`xrPhysics.vcxproj`.

---

## C. Рендерер и графика

### C1. Нет compute-шейдеров, хотя инфраструктура готова

`ShaderTypeTraits<SCS>` (`:47-63`) полностью собирает `cs_5_0` /
`ID3D11ComputeShader`, UAV-таргеты создаются (`r4_rendertarget.cpp:287-292`),
но **`.cs`-файлов в gamedata — ноль**. Путь ни разу не пройден.

### C2. Джиттер DLSS считается функциями FSR3

`r4_R_render.cpp:309-319` вызывает `ffxFsr3UpscalerGetJitterPhaseCount` /
`ffxFsr3UpscalerGetJitterOffset` даже когда активен DLSS.
Алгоритм общий, но связывает DLSS с присутствием FSR3.

### C3. FSR3 работает без exposure / reactive / transparency

`RenderTargetPhaseAA.cpp:802-805` — все три `nullptr`.
На прозрачных объектах (дым, трава, дождь) возможны артефакты.

### C4. Три закомментированных флага DLSS

`:332`, `:337`, `:341` — `DoSharpening`, `MVJittered`, `AlphaUpscaling`.
Без `DoSharpening` значение `InSharpness` игнорируется, но есть собственный
CAS (`:611`) — включение даст двойное sharpen. Требует осознанного выбора.

### C5. Дефолтный режим — DLAA, а не апскейлинг

`ps_r_dlss_quality = DLSS_QUALITY_DLAA` (`xrRender_console.cpp:58`).
При `ps_r_pp_aa_mode = DLSS` апскейлинга нет — только AA.
Стоит документировать в UI.

### C6. Смена качества апскейлера требует `vid_restart`

DLSS/FSR3 физически меняют размеры RT. Движок только логирует
предупреждение (`:566`, `:899`).

### C7. Глубина — `D24_UNORM_S8_UINT`

`rt_zbuffer` / `rt_Base_Depth` / CSM создаются как `R24G8_TYPELESS`.
Для DXR плохо — требуется R32_FLOAT.

### C8. В G-buffer нет metallic и emissive, но нормали есть

`r4_rendertarget_phase_scene.cpp:32` — 4 MRT:
`rt_Position` (нормаль.xy + view-space Z + mat-id/hemi),
`rt_Color` (albedo + gloss), `rt_Velocity`, `rt_heat`.

Отсутствуют: **metallic, emissive в 4 MRT**.
Нормали и gloss **есть** — это упрощает трассировку относительно SoC-оценки.

### C9. Потолок шейдерной модели — SM 5.0

Ни SM6, ни mesh shaders, ни bindless, ни async compute.

### C10. AppID DLSS зашит

`20082024151405ull` — `RenderTargetPhaseAA.cpp:235`, `:458`, `:542`.
Нужен `NVSDK_NGX_D3D11_Init_with_ProjectID` для своего идентификатора.

---

## D. CI/CD и репозиторий

### D1. Нет кэшей, тестов и линтеров

`.github/workflows/push.yml`: только checkout → update → build → 7z → upload.
Ни `actions/cache`, ни NuGet, ни тестов, ни линта. `fetch-depth: 0`
не используется.

### D2. dependabot не следит за зависимостями

`dependabot.yml` — только `github-actions` (`monthly`).
Для git-клонов экосистемы нет — отсюда ручные пины (A2).

### D3. Путь артефактов не защищён `.gitignore`

`push.yml:54-58` собирает артефакты в
`Game\Resources_CoP_1.6.02\bin_%PLATFORM%\`.
`git check-ignore` даёт exit 1 — папка **не** игнорируется.
Локальная сборка пакета закоммитит её.

### D4. Git LFS отсутствует

`.gitattributes` — только `*.script`, `*.ltx`, `*.puddles`.
`soft_oal.dll` (3.8 МБ) лежит обычным блобом. Нет `binary`-атрибута
для `.png`/`.ico`/`.ttf`/`.xr`.

### D5. `*.lua` не на пути автозагрузки

`xrServerEntities/script_engine.cpp:198` — `.lua` закомментирован.
Файл `gamedata/scripts/personal_test.lua` на CoP отсутствует,
но механизм остаётся прежним.

### D6. Полный teardown Lua VM при загрузке сохранения

`ai_space.cpp:118-130` вызывает
`script_engine().unload(); script_engine().init();`.

---

## E. Лицензия

**`LICENSE.md` — модифицированная BSD-3 с некоммерческим ограничением:**

> *«All source code included with this distribution, unless declared otherwise,
> is commercial GSC Game World proprietary code.»*
> *«Neither binary nor source code redistributions may be used for any
> commercial purposes.»*

**Не** OSI-approved, **не** MIT/Apache. Коммерческое использование запрещено.
Компоненты сторонние — с отдельными лицензиями.

---

## F. Сводка по приоритету

| Приоритет | Пункт | Почему |
|---|---|---|
| **Высокий** | A1 | Ломает отчёт о сборке у всех без Debugging Tools |
| **Высокий** | A2 | Сборка невоспроизводима во времени |
| **Высокий** | A3 | NIS официально поддерживает DX11 и уже в дереве |
| **Высокий** | C1 | Инфраструктура compute-шейдеров готова и не используется |
| **Высокий** | H1 | Устаревший комментарий противоречит коду (`GAMESAVE_SIZE`) |
| Средний | A7, A8 | Хрупкость при обновлении toolchain |
| Средний | C4, C5 | Осознанная, но неосвещённая конфигурация DLSS |
| Средний | H2 | Новая рантайм-зависимость на DirectPlay |
| Низкий | B1–B10 | Мёртвый код |
| Низкий | D1, D2 | Инфраструктура CI |

---

## G. Что проверено и оказалось НЕ проблемой

| Проверка | Результат |
|---|---|
| Сабмодули git | **отсутствуют** (`.gitmodules` нет) |
| `d3dx9.lib` | **не линкуется**, только заголовки |
| Legacy DirectX SDK (June 2010) | **не требуется** |
| `vcruntime140.dll` / `msvcp140.dll` | **не требуются** — везде `/MT` |
| RenderDoc | заголовок вендорен, in-process hook **поднят** |
| Tracy | вендорен и подключён через `CONFIGURATION_GA` |
| `xrServerEntities` / `COMMON_AI` как проект | **нет** — компилируются внутри `xrGame.vcxproj` |
| `Game/Shaders` | **не существует** — шейдеры в `Game/Resources_CoP_1.6.02/gamedata/shaders/` |
| `CUICustomWnd` | **не существует** — 0 совпадений |
| Поддержка форка CoP в UI | ✅ `ui_mm_opt_video_adv.script:27-29` exposes `dlss_quality`/`dlss_preset`/`fsr3_quality` |

---

## H. Отличия CoP-ветки от SoC, влияющие на разработку

Эти находки сделаны при перепроверке под `main_cop_cs` и требуют внимания.

### H1. Устаревший комментарий противоречит коду

`build_config_defines.h:63-64`:
```cpp
//По умолчанию было 128х128 вроде б. У нас 768х768
constexpr size_t GAMESAVE_SIZE{128};
```
Комментарий говорит «у нас 768», код даёт **128**. Комментарий устарел,
значение соответствует «оригинальному» CoP.

### H2. Новая рантайм-зависимость: DirectPlay 8

`xrNetServer` использует COM DirectPlay (`IDirectPlay8Client`/`Server`).
Нужны `dplay32.dll` и `dpwsockx.dll` — системные, ставятся вместе с Windows,
но их нельзя считать гарантированно присутствующими. В копирование post-build
**не добавлены**.

### H3. `SPAWN_VERSION` = 128 вместо 118

`xrServerEntities/xrServer_Objects.h:166`.
Сохранения от SoC-сборки несовместимы. При портировании мода это
придётся учитывать (не только формат сейвов, но и сайд-эффекты мода).

### H4. `.ini`-переопределение patrol_path удалено

На SoC был `[engine_custom_spawn] waypoints_file` в `external.ltx`.
На CoP этой секции **нет** ни в `external.ltx`, ни в коде.
Механизм замены путей из `.ini` пропал.

### H5. `m_netpk.script` отсутствует

Мод-логика SoC переопределяла раскладку сетевого пакета на Lua.
На CoP такого файла нет, поддержка >64 костей сделана иначе.
Порт SoC-мод-логики потребует переписывания этого слоя.

### H6. `common_iostructs.h` — источник правды по G-buffer

См. [02-renderer.md](02-renderer.md) §5. Если планируется трассировка,
декодирование нормалей из `rt_Position.xy` — первый шаг.

### H7. `CORE_FEATURE_SET` — новый механизм флагов фич

`x_ray.cpp`: `strict_infoportions_control`, `monsters_inventory`,
`cop_rounded_minimap`, `cop_scale_bounds`. Подключение фич через конфиг.
В документации SoC не отражён.

### H8. `CApplication::CheckCsCopMode()` — ветка CoP-lightmap'ов

`x_ray.cpp:806`, используется в `r4.cpp:817-818` для дефайна
`USE_CS_COP_LMAP`. Влияет на компиляцию шейдеров освещения.

### H9. Configs/ и Scripts/ с заглавной буквы на SoC-овских путях

На SoC в документации фигурировали `gamedata/config` и `gamedata/scripts`.
**На CoP это `gamedata/Configs` и `gamedata/Scripts`** — с заглавной.
Регистр важен на Linux-сборках.

### H10. `xrNetServer` Debug-конфигурация неполна

`xrNetServer.vcxproj:116` — в Debug нет include-пути `..\xrCore`
(в Release `:60` есть). Плюс в Debug есть `DEBUG;WIN32;`, в Release нет `WIN32`.

### H11. Циклическая линковка `xrEngine.lib` ↔ `xrGame.lib`

`xrGame.vcxproj:103` / `:167` содержат `xrEngine.lib` в
`AdditionalDependencies`. Работает только потому, что статические библиотеки,
но ломает инкрементность и запутывает граф.