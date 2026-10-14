# 07 — Найденные проблемы и техдолг

Проверено по исходникам форка `OGSR/OGSR-Engine@0a88379c8`.
Каждый пункт воспроизводим.

---

## A. Сборка

### A1. `dbghelp.dll` безусловный post-build — ломает отчёт о сборке

**Где:** `ogsr_engine/xr_3da/XR_3DA.vcxproj:403-405`

```xml
<Target Name="PostBuildStep1" AfterTargets="Build">
  <Exec Command='copy /Y "$(WindowsSDKDir)Debuggers\$(PlatformShortName)\dbghelp.dll" "$(OutDir)dbghelp.dll"' />
</Target>
```

Нет `IgnoreExitCode`, нет `Condition`. Без компонента «Debugging Tools for Windows»
путь `$(WindowsSDKDir)Debuggers\x64\dbghelp.dll` не существует → `copy` → ненулевой
код → цель падает → **MSBuild reports build FAILED при успешно слинкованном EXE**.

**Это классическая жалоба на сборку OGSR/X-Ray.**

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
`Update_Components.cmd:26` клонирует **без `--recurse-submodules`**, поэтому
NIS физически отсутствует, хотя SDK официально поддерживает DX11.

**Подробнее — [06-rtx-feasibility.md](06-rtx-feasibility.md) §3.2.**

### A4. `Update_Components.cmd` не идемпотентен и не проверяет ошибки

Каждая строка: `RD /s /q <путь>` **до** `git clone`. При обрыве сети посреди
скрипта остаётся half-populated дерево. Нет `|| exit /b 1`, нет
`errorlevel`-проверок; `pause` в конце маскирует ситуацию.

### A5. `ReleaseTracyProfiler` не существует как конфигурация

`Engine.sln` содержит только `Debug|x64` и `Release|x64`.
Строка `ReleaseTracyProfiler` есть лишь в `.github/workflows/push.yml` и в
`Engine_*_properties.props:30-39`, где она сравнивается с
`$(CONFIGURATION_GA)` — переменной **окружения**.

### A6. Tracy-сборка неотличима от обычной Release на диске

Так как `Configuration` остаётся `Release`, обе вариации пишут в
`bin_x64\` и `ogsr_engine\_LIB\Engine\Release\x64\`.
`tlog`-инкрементальная проверка пересоберёт, но на диске артефакты
неразличимы.

### A7. 16 предупреждений повышены до ошибок

`ogsr_engine/Engine_properties.props:15` и `Engine_lib_properties.props:15`:

```
/we4566 /we4828 /we4715 /we4189 /we4595 /we4834 /we4018 /we4101
/we4552 /we4551 /we4477 /we4390 /we4806 /we4002 /we4302
```

Применяется **только к движковым проектам**. Любое обновление MSVC,
добавляющее одно из этих предупреждений, ломает сборку.

### A8. Стандарт C++ зависит от версии Visual Studio

`OgsrBuildProps.props:29-31`:

```xml
<LanguageStandard Condition="'$(VisualStudioVersion)' == '18.0'">stdcpp23</LanguageStandard>
<LanguageStandard Condition="'$(VisualStudioVersion)' != '18.0'">stdcpp20</LanguageStandard>
```

Preview/insider VS2026 с версией не ровно `18.0` молча откатывается на `v143`+`stdcpp20`.
Один и тот же исходник компилируется по-разному.

### A9. `WindowsTargetPlatformVersion` не закреплён

`OgsrBuildProps.props:16-17` берёт **последний установленный** SDK.
Preview-SDK может сдвинуть заголовки D3D11/D3DCompiler.

### A10. `Game/ShadersSln` закреплён на v143

`Game/ShadersSln/ShadersSln.vcxproj:577`, `:583-602` —
`PlatformToolset v143`, `WindowsTargetPlatformVersion 10.0`.
Не импортирует `OgsrBuildProps.props`. На машине только с VS2026 не откроется.

### A11. `DISABLE_DBG_ASSERTIONS` включён по умолчанию

`ogsr_engine/build_config_defines.h:19`. Debug-сборка фактически
**не** assert-тяжёлая — легко пропустить инварианты.

### A12. `LuaJIT` не пересобирается при наличии `LuaJIT.lib`

`ogsr_engine/LuaJIT/src/luajit.vcxproj` короткозамыкает на
`if not exist "..\bin\$(PlatformShortName)\LuaJIT.lib"`.
Без `Rebuild` (или `cleanup.cmd`) бинарник останется старым.

### A13. Манифест не знает про Windows 11

`ogsr_engine/xr_3da/manifest.xml` объявляет `supportedOS` только для
Windows 10 / 8.1 / 8 / 7. GUID `{d48ca3fe-…}` отсутствует. DPI в legacy-режиме.

---

## B. Мёртвый код

### B1. `3rd_party/Src/3rd_party_properties.props` не импортируется

Ни один `.vcxproj` его не подключает. Все 3rd-party используют вариант `_lib_`.

### B2. `DirectXMesh` клонируется, но не используется

`Update_Components.cmd:4-5` клонирует `OGSR/DirectXMesh`,
путь добавлен в `AdditionalIncludeDirectories`, но **нет ни одного
`#include "DirectXMesh.h"`** и нет vcxproj.

### B3. Не компилируемые дубликаты в `xrRender/`

Исключены из `xrRender_R4.vcxproj`:
`ResourceManager_Resources.cpp`, `ResourceManager_Scripting.cpp`,
`SH_RT.cpp`, `SH_Texture.cpp`, `tga.cpp`, `dxParticleCustom.cpp`,
а также `xrRenderPC_R4/du_sphere_part.cpp`.

> **Ловушка:** правя загрузку шейдеров или RT, нужно редактировать
> `xrRenderDX10/dx10*-файлы`, а не лежащие рядом `xrRender/*`-аналоги.

### B4. `libsquashfs` собирает write-сторону

`libsquashfs.vcxproj` компилирует `block_writer`, `dir_writer`, `meta_writer`,
`write_inode/super/table` — читателю `.pAK`/`.sqfs` они не нужны.

### B5. `opcode.lib` в `#pragma comment` под неактивным `#ifdef`

`ogsr_engine/xrCDB/OpCoDe/Opcode.h:54-56` ссылается на `Opcode.lib` /
`Opcode_D.lib`, которые никогда не собираются.

### B6. Несуществующий include-путь в `xrGame.vcxproj`

`ogsr_engine/xrGame/xrGame.vcxproj:82`, `:139` добавляют
`$(SolutionDir)\3rd_party\Src\d3dx\include`, которого нет.
Фактическая раскладка — `3rd_party\Src\d3dx\d3dx\`, и подключается как
`<d3dx/d3dx9.h>`.

### B7. Неиспользуемая конфигурация imgui

`3rd_party/Src/imgui/imgui.vcxproj:8-11` объявляет `Mixed|x64`,
который `Engine.sln` не маппит.

### B8. `cleanup.cmd` чистит несуществующие пути

Обрабатывает `.vs\Engine\*` (раскладка **VS2010**) и `bin_x86\`,
который `Engine.sln` уже не может породить (только x64).

---

## C. Рендерер и графика

### C1. Нет compute-шейдеров, хотя инфраструктура готова

`ShaderTypeTraits<SCS>` (`xrRender/ShaderResourceTraits.h:47-63`)
полностью собирает `cs_5_0` / `ID3D11ComputeShader`, и UAV-таргеты создаются
(`r4_rendertarget.cpp:287-292`), но **`.cs`-файлов в gamedata — ноль**.
Путь ни разу не пройден.

### C2. Джиттер DLSS считается функциями FSR3

`r4_R_render.cpp:309-319` вызывает `ffxFsr3UpscalerGetJitterPhaseCount` /
`ffxFsr3UpscalerGetJitterOffset` даже когда активен DLSS.
Алгоритм общий, но DLSS-режим жёстко зависит от присутствия FSR3.

### C3. FSR3 работает без exposure / reactive / transparency

`RenderTargetPhaseAA.cpp:802-805` — все три передаются как `nullptr`.
На прозрачных объектах (дым, трава, дождь) возможны артефакты.

### C4. Три закомментированных флага DLSS

`RenderTargetPhaseAA.cpp:332`, `:337`, `:341`:
`DoSharpening`, `MVJittered`, `AlphaUpscaling`.
Без `DoSharpening` значение `InSharpness` игнорируется —
но есть собственный CAS (`ProcessCAS`, `:611`), так что включение
даст двойное sharpen. Требует осознанного выбора.

### C5. Дефолтный режим — DLAA, а не апскейлинг

`ps_r_dlss_quality = DLSS_QUALITY_DLAA` (`xrRender_console.h:58`).
При `ps_r_pp_aa_mode = DLSS` апскейлинга нет — только anti-aliasing.
Стоит документировать в UI.

### C6. Смена качества апскейлера требует `vid_restart`

DLSS/FSR3 физически меняют размеры RT, поэтому переключение режима
на лету не пересоздаёт ресурсы. Движок только логирует предупреждение
(`RenderTargetPhaseAA.cpp:566`, `:899`).

### C7. Глубина — `D24_UNORM_S8_UINT`

`rt_zbuffer` / `rt_Base_Depth` / `rt_tempzb` / CSM создаются как
`DXGI_FORMAT_R24G8_TYPELESS` (`r4_rendertarget.cpp:224`, `:282-284`, `:344-347`).
Для DXR это плохо — требуется R32_FLOAT.

### C8. В G-buffer нет мировых нормалей и roughness

`r4_rendertarget_phase_scene.cpp:32`:

```cpp
u_setrt(cmd_list, rt_Position, rt_Color, rt_Velocity, rt_heat, rt_Base_Depth->pZRT[...]);
```

| Слот | RT | Формат | Содержимое |
|---|---|---|---|
| 0 | `rt_Position` | `R16G16B16A16_FLOAT` | позиция **в пространстве глаза** |
| 1 | `rt_Color` | `R8G8B8A8_UNORM` | albedo + specular-gloss |
| 2 | `rt_Velocity` | `R16G16_FLOAT` | motion vectors |
| 3 | `rt_heat` | `R8G8B8A8_UNORM` | маска heatvision |

Нет world normal, roughness/metallic, emissive.
Любая трассировка потребует их добавить.

### C9. Потолок шейдерной модели — SM 5.0

`vs_5_0`, `ps_5_0`, `gs_5_0`, `cs_5_0`. Ни SM6, ни mesh shaders,
ни bindless, ни async compute.

### C10. AppID DLSS зашит

`20082024151405ull` — `RenderTargetPhaseAA.cpp:235`, `:458`, `:542`.
Нужен `NVSDK_NGX_D3D11_Init_with_ProjectID`, чтобы завести свой
идентификатор проекта для форка.

---

## D. CI/CD и репозиторий

### D1. Нет кэшей, тестов и линтеров

`.github/workflows/push.yml`: только checkout → update → build → 7z → upload.
Ни `actions/cache`, ни NuGet, ни тестов, ни линта.
`fetch-depth: 0` используется, но результат никем не потребляется.

### D2. dependabot не следит за 12 зависимостями

`.github/dependabot.yml` объявляет только экосистему `github-actions`.
Для git-клонов зависимостей экосистемы нет — отсюда ручные пины (см. A2).

### D3. Имя артефакта жёстко содержит `SHOC_EDITION`

`OGSR_Engine_SHOC_EDITION_x64_3.<run>_<Config>.7z` — несмотря на наличие
ветки `main_cop_cs`.

### D4. `Game/` не игнорируется

`.gitignore` покрывает `/bin*`, но `Game\Resources_SoC_1.0006\bin_x64\`
(куда CI кладёт артефакты) не защищён. Локальная сборка пакета
закоммитит эту папку.

### D5. Git LFS отсутствует

`.gitattributes` содержит только `*.script`, `*.ltx`, `*.puddles`.
`soft_oal.dll` (3.7 МБ) лежит обычным блобом. DLSS-DLL (59–70 МБ)
не коммитятся — качаются, и это правильно.

Нет `binary`-атрибута для `.png`, `.ico`, `.ttf`, `.xr`, `.tlb`.

### D6. `*.lua` не на пути автозагрузки

`script_engine.cpp:209` — `.lua` закомментирован. Файл
`gamedata/scripts/personal_test.lua` существует, но грузится вручную
через `dofile()` в `ui_main_menu.script:171`.

### D7. Полный teardown Lua VM при загрузке сохранения

`CAI_Space::unload(false)` (`ai_space.cpp:118-130`) вызывает
`script_engine().unload(); script_engine().init();` — полный teardown
и rebuild VM при каждой выгрузке уровня без перечитывания.

---

## E. Лицензия

**`LICENSE.md` — модифицированная BSD-3 с некоммерческим ограничением:**

> *«All source code included with this distribution, unless declared otherwise,
> is commercial GSC Game World proprietary code.»*
> *«Neither binary nor source code redistributions may be used for any
> commercial purposes.»*

Это **не** OSI-approved лицензия и **не** MIT/Apache.
Любое коммерческое использование запрещено.
Компоненты сторонние имеют отдельные лицензии — LGPL-2 (ODE, OpenAL Soft),
MIT (ImGui, FidelityFX, RenderDoc), BSD-3 (Xiph), NVIDIA DLSS SDK License.

---

## F. Сводка по приоритету

| Приоритет | Пункт | Почему |
|---|---|---|
| **Высокий** | A1 | Ломает отчёт о сборке у всех без Debugging Tools |
| **Высокий** | A2 | Сборка невоспроизводима во времени |
| **Высокий** | A3 | NIS официально поддерживает DX11 и уже в дереве, но недоступен |
| **Высокий** | C1 | Инфраструктура compute-шейдеров готова и не используется — блокер для SSGI |
| Средний | A7, A8 | Хрупкость при обновлении toolchain |
| Средний | C4, C5 | Осознанная, но неосвещённая конфигурация DLSS |
| Средний | D7 | Полный teardown VM на каждую загрузку |
| Низкий | B1–B8 | Мёртвый код |
| Низкий | D1, D2 | Инфраструктура CI |

---

## G. Что проверено и оказалось НЕ проблемой

Чтобы не тратить время повторно:

| Проверка | Результат |
|---|---|
| Сабмодули git | **отсутствуют** (`.gitmodules` нет). Зависимости — клоны внутри игнорируемых папок |
| Прямой `d3dx9.lib` | **не линкуется**, только заголовки (математика/структуры) |
| Legacy DirectX SDK (June 2010) | **не требуется** |
| `vcruntime140.dll` / `msvcp140.dll` | **не требуются** — везде `/MT` |
| RenderDoc | заголовок вендорен, in-process hook **поднят** (`USE_RENDERDOC`) |
| Tracy | вендорен и подключён, работает через `CONFIGURATION_GA` |
| `COMMON_AI` как отдельный проект | **нет** — компилируется внутри `xrGame.vcxproj` |
| `Game/Shaders` | **не существует**, шейдеры в `Game/Resources_SoC_1.0006/gamedata/shaders/` |
| `CUICustomWnd` | **не существует**, есть `CUICustomItem` и `CUIScriptWnd` |