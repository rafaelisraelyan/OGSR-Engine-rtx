# 03 — Сборка и зависимости

## 1. Решение

`Engine.sln`, Visual Studio 2026 (18.6), SolutionGuid
`{1F9B04CC-5248-4AE5-97B7-66A0D0E5AA5E}`.

**Конфигураций всего две:** `Debug|x64` и `Release|x64`.
Ни Win32/x86, ни `ReleaseTracyProfiler` как конфигурации решения не существует
(см. §4 — как работает Tracy).

## 2. Проекты

| Проект | Путь | Тип |
|---|---|---|
| `XR_3DA` → `xrEngine.exe` | `ogsr_engine/xr_3da/XR_3DA.vcxproj` | **Приложение**, `Subsystem=Windows`, `CharacterSet=MultiByte` |
| `xrCore` | `ogsr_engine/xrCore/` | StaticLib |
| `xrCDB` | `ogsr_engine/xrCDB/` | StaticLib |
| `xrSound` | `ogsr_engine/xrSound/` | StaticLib |
| `xrParticles` | `ogsr_engine/xrParticles/` | StaticLib |
| `xrRender_R4` | `ogsr_engine/Layers/xrRenderPC_R4/` + `../xrRender` + `../xrRenderDX10` | StaticLib |
| `xrGame` | `ogsr_engine/xrGame/` | StaticLib (включает `COMMON_AI`) |
| `Luabind` | `ogsr_engine/Luabind/` | StaticLib |
| `LuaJIT` | `ogsr_engine/LuaJIT/src/luajit.vcxproj` | **Makefile** (NMake → `msvcbuild.bat`) |
| `DirectXTex` | `3rd_party/Src/DirectXTex/` | StaticLib |
| `imgui` | `3rd_party/Src/imgui/` | StaticLib (`imgui`) |
| `lzo` | `3rd_party/Src/lzo/` | StaticLib |
| `ode` | `3rd_party/Src/ode/contrib/msvc7/ode_default/default.vcxproj` | StaticLib |
| `libsquashfs` | `3rd_party/Src/libsquashfs/` | StaticLib |
| `liblz4` | `3rd_party/Src/lz4/` | StaticLib |
| `libzstd` | `3rd_party/Src/zstd/` | StaticLib |
| `mimalloc-lib` → `mimalloc` | `3rd_party/Src/mimalloc/` | StaticLib |
| `DiscordRPC` | `3rd_party/Src/DiscordRPC/` | StaticLib |
| `ffx_backend_dx11_x64` | `3rd_party/Src/FidelityFX-SDK/` | StaticLib |
| `ffx_fsr3upscaler_x64` | `3rd_party/Src/FidelityFX-SDK/` | StaticLib |
| `libogg_static`, `libvorbis_static`, `libvorbisfile_static`, `libtheora_static` | `3rd_party/Src/Xiph/` | StaticLib ×4 |

**DLL-проектов нет.** Единственная DLL — `LuaJIT.dll` (NMake) + готовые
`OpenAL32.dll`, `nvngx_dlss.dll`, `dbghelp.dll`.

### Граф зависимостей

```
xrCore ──────────┬──> lzo
                 ├──> mimalloc-lib
                 └──> libsquashfs ──> libzstd, liblz4

libsquashfs  ──> libzstd, liblz4

xrCDB        ──ProjectReference──> xrCore
xrParticles  ──ProjectReference──> xrCore
xrSound      ──ProjectReference──> xrCDB, xrCore
Luabind      ──sln deps──────────> LuaJIT, xrCore
LuaJIT       ──NMake: msvcbuild.bat x64 static <Config>

xrRender_R4  ──ProjectReference──> xrCDB, xrCore, xrParticles, imgui,
                                  DirectXTex, ffx_backend_dx11_x64,
                                  ffx_fsr3upscaler_x64
xrGame       ──ProjectReference──> xrCDB, xrCore, xrParticles, xrSound, imgui
             ──sln deps──────────> ode, Luabind

XR_3DA.exe   ──ProjectReference──> xrCDB, xrCore, xrParticles, xrSound,
                                  xrRender_R4, xrGame, imgui
```

Линкуется в EXE (`XR_3DA.vcxproj:94`, `:164`):

```
ode.lib  libvorbis_static.lib  libvorbisfile_static.lib  OpenAL32.lib
lzo.lib  libogg_static.lib  libtheora_static.lib
vfw32.lib  winmm.lib  dinput8.lib
d3d11.lib  d3dcompiler.lib  dxgi.lib  dxguid.lib
```

Через `#pragma comment(lib, …)` в исходниках:

| Файл | Библиотека |
|---|---|
| `xrCore/xrMemory.cpp:17` | `mimalloc` |
| `xrCore/arc_sqfs.cpp:19-21` | `libsquashfs`, `libzstd`, `liblz4` |
| `xrRender/RenderTargetPhaseAA.cpp:39-51` | `nvsdk_ngx_s_dbg_iterator0` / `nvsdk_ngx_s_dbg` / `nvsdk_ngx_d` / `nvsdk_ngx_s`, `version.lib` |
| `xrCore/xrCore.cpp:9` | `winmm.lib` |
| `xrCore/_math.cpp:6` | `PowrProf.lib` |
| `xrCore/stacktrace_collector.cpp:20`, `xrDebugNew.cpp:288` | `dbghelp.lib` |
| `xr_3da/DiscordRPC.cpp:8` | `DiscordRPC` |
| `xr_3da/ai_script_space.h:4` | `Luabind.lib` |
| `xr_3da/splash.cpp:16` | `Windowscodecs` |

## 3. MSBuild-свойства

### `OgsrBuildProps.props` (корень, импортируется всеми обёртками)

```xml
<PlatformToolset Condition="'$(VisualStudioVersion)' == '18.0'">v145</PlatformToolset>
<PlatformToolset Condition="'$(PlatformToolset)'==''">v143</PlatformToolset>

<WindowsTargetPlatformVersion>
  $[Microsoft.Build.Utilities.ToolLocationHelper]::GetLatestSDKTargetPlatformVersion('Windows','10.0')]
</WindowsTargetPlatformVersion>

<LanguageStandard Condition="'$(VisualStudioVersion)' == '18.0'">stdcpp23</LanguageStandard>
<LanguageStandard Condition="'$(VisualStudioVersion)' != '18.0'">stdcpp20</LanguageStandard>

<BuildStlModules>false</BuildStlModules>
<ConformanceMode>true</ConformanceMode>
<BufferSecurityCheck>true</BufferSecurityCheck>
<LargeAddressAware>true</LargeAddressAware>

<AdditionalOptions Condition="$(Configuration.Contains('Release'))">/Zc:__cplusplus /Ob3 %(AdditionalOptions)</AdditionalOptions>
<AdditionalOptions Condition="$(Configuration.Contains('Debug'))">/Zc:__cplusplus %(AdditionalOptions)</AdditionalOptions>

<PreprocessorDefinitions Condition="$(Configuration.Contains('Release'))">
  _MSVC_STL_USE_ABORT_AS_DOOM_FUNCTION;%(PreprocessorDefinitions)</PreprocessorDefinitions>
<PreprocessorDefinitions Condition="$(Configuration.Contains('Debug'))">
  _MSVC_STL_USE_ABORT_AS_DOOM_FUNCTION;_MSVC_STL_HARDENING=1;_ITERATOR_DEBUG_LEVEL=0;%(PreprocessorDefinitions)</PreprocessorDefinitions>
```

> ⚠️ **`_ITERATOR_DEBUG_LEVEL=0` в Debug сделан намеренно**, чтобы линковаться
> с `nvsdk_ngx_s_dbg_iterator0.lib` (см. `RenderTargetPhaseAA.cpp:37-42`).

### `ogsr_engine/Engine_properties.props` (для EXE)

```xml
<OutDir>$(SolutionDir)\bin_$(PlatformShortName)\</OutDir>
<IntDir>$(SolutionDir)\ogsr_engine\_TEMP\Engine\$(Configuration)\$(Platform)\$(ProjectName)\</IntDir>

<AdditionalOptions>/source-charset:utf-8
  /we4566 /we4828 /we4715 /we4189 /we4595 /we4834 /we4018 /we4101
  /we4552 /we4551 /we4477 /we4390 /we4806 /we4002 /we4302 %(AdditionalOptions)</AdditionalOptions>

<AdditionalIncludeDirectories>$(SolutionDir)\3rd_party\Src\tracy\public</AdditionalIncludeDirectories>

<PreprocessorDefinitions>_CRT_SECURE_NO_WARNINGS;_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING</PreprocessorDefinitions>

<Link/ImportLibrary>$(SolutionDir)\ogsr_engine\_LIB\Engine\$(Configuration)\$(Platform)\$(TargetName).lib</Link/ImportLibrary>
<Link/AdditionalLibraryDirectories>
  3rd_party\Src\NVIDIA_DLSS\DLSS\lib\Windows_x86_64\x64
  3rd_party\_LIB\$(Configuration)\$(Platform)
  ogsr_engine\_LIB\Engine\$(Configuration)\$(Platform)
  3rd_party\Src\OpenAL_Soft\libs\$(PlatformShortName)
  3rd_party\_LIB\Release\$(Platform)
  ogsr_engine\_LIB\Engine\Release\$(Platform)
</Link/AdditionalLibraryDirectories>
```

> ⚠️ **16 предупреждений повышены до ошибок** — только для движковых проектов.
> Это главный источник «не собирается после обновления MSVC».
> Список: 4566, 4828, 4715, 4189, 4595, 4834, 4018, 4101, 4552, 4551, 4477,
> 4390, 4806, 4002, 4302.

### `ogsr_engine/Engine_lib_properties.props` (для статических библиотек)

Байт-в-байт как предыдущий, кроме:
```xml
<OutDir>$(SolutionDir)\ogsr_engine\_LIB\Engine\$(Configuration)\$(Platform)\</OutDir>
```

### `3rd_party/Src/3rd_party_lib_properties.props`

```xml
<OutDir>$(SolutionDir)\3rd_party\_LIB\$(Configuration)\$(Platform)\</OutDir>
<Lib/OutputFile>$(SolutionDir)\3rd_party\_LIB\$(Configuration)\$(Platform)\$(TargetName).lib</Lib/OutputFile>
```
Без `/we####`, без `SDLCheck`, без include-пути Tracy.

### `3rd_party/Src/3rd_party_properties.props` — мёртвый файл

Не импортируется ни одним `.vcxproj`. Можно удалять.

## 4. Как на самом деле работает Tracy

**`ReleaseTracyProfiler` не является конфигурацией MSBuild.** В `Engine.sln`
такой конфигурации нет. Строка встречается только в:
`.github/workflows/push.yml` и `Engine_*_properties.props:30-39`.

```xml
<ItemDefinitionGroup Condition="'$(CONFIGURATION_GA)'=='ReleaseTracyProfiler'">
  <ClCompile>
    <PreprocessorDefinitions>
      TRACY_ENABLE;TRACY_NO_FRAME_IMAGE;TRACY_ON_DEMAND;%(PreprocessorDefinitions)
    </PreprocessorDefinitions>
  </ClCompile>
</ItemDefinitionGroup>
```

CI делает:
```cmd
set CONFIGURATION_GA=ReleaseTracyProfiler
msbuild Engine.sln /p:Configuration=Release /p:Platform=x64
```

MSBuild подставляет переменную окружения в `$(CONFIGURATION_GA)`, условие
срабатывает, а фактическая конфигурация остаётся `Release`.

**Локально:**
```cmd
set CONFIGURATION_GA=ReleaseTracyProfiler
msbuild Engine.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

> ⚠️ Tracy-сборка и обычная Release пишут в **одни и те же** `bin_x64\` и
> `_LIB\Engine\Release\x64\`. Неразличимы на диске.

Клиент Tracy компилируется **безусловно** одним TU внутри `xrCore`
(`xrCore.vcxproj:139` → `3rd_party/Src/tracy/public/TracyClient.cpp`),
а тот сам себя гасит по `#ifdef TRACY_ENABLE` (`TracyClient.cpp:16`).

## 5. Переопределение конфигурации

`ogsr_engine/build_config_defines.h` (67 строк) — отслеживаемый файл.
Активно:

| Макрос | Значение |
|---|---|
| `USE_OWN_MINI_DUMP` | minidump при падении |
| `DISABLE_DBG_ASSERTIONS` | asserts выключены **даже в Debug** |
| `FSLTX` | `"fsgame.ltx"` |
| `NEWS_TO_SHOW` | `500` |
| `USE_TEXTURES_LTX` | — |
| `ANIMATED_PHYSICS_OBJECT_SUPPORT` | — |
| `GAMESAVE_SIZE` | `768` |

Закомментированы: `OGSR_TOTAL_DBG`, `CRASH_ON_INVALID_VERTEX_ID`,
`USE_MEMORY_VALIDATOR`, `DUPLET_STATE_SWITCH`, `APPLY_ARMOR_PIERCING_TO_NPC`,
`REMOVE_ARTICLES_ON_DISABLE_INFO`, `USE_STORY_ID_AS_SPAWN_ID`,
`USE_REDUCE_LOD_TEXTURE_LIST`, `DISABLE_DISCORD`.

Переопределить, не трогая git:

```
ogsr_engine/build_config_overrides/build_config_defines.h
ogsr_engine/build_config_overrides/trivial_encryptor_ovr.h
```

Механизм — `ogsr_engine/xrCore/xrCore.h:4-8` через `__has_include`.
Папка в `.gitignore` (`.gitignore` → `build_*.h`).

## 6. 3rd_party

Две категории:

**Вендорены в дереве (коммитятся):**
`d3dx`, `eax`, `imgui`, `lzo`, `ode`, `OpenAL_Soft`, `RenderDoc`, `reshade`, `tracy`, `Xiph`

**Качаются скриптом `Update_Components.cmd` (внутри git-игнорируемых папок):**
`concurrentqueue`, `cpputils`, `DirectXMath`, `DirectXMesh`, `DirectXTex`,
`DiscordRPC`, `FidelityFX-SDK`, `libsquashfs`, `lz4`, `mimalloc`,
`NVIDIA_DLSS`, `zstd`

| Папка | Что | Версия | Сборка |
|---|---|---|---|
| `NVIDIA_DLSS` | NVIDIA DLSS SDK (NGX) | **`v310.9.1`** | готовые `.lib`/`.dll`, `lib/Windows_x86_64/x64` |
| `FidelityFX-SDK` | AMD FidelityFX, **форк OGSR** с нативным DX11-бэкендом | `release-FSR3-3.1.2-DX11-Native-API` (FSR 3.1.2) | из исходников, 2 vcxproj |
| `imgui` | Dear ImGui | **v1.92.9 WIP** | из исходников |
| `tracy` | Tracy Profiler | **0.11.2** | 1 TU внутри `xrCore` |
| `RenderDoc` | In-app API | MIT 2019-2024 | header-only, `USE_RENDERDOC` |
| `reshade` | ReShade addon SDK | `RESHADE_API_VERSION 5` | header-only |
| `OpenAL_Soft` | OpenAL Soft пребилт | **soft_oal.dll 1.25.1**, LGPL-2 | готовые `.lib`+`.dll` |
| `DiscordRPC` | discord-rpc, форк OGSR + rapidjson 1.1.0 | `master` / `v1.1.0` | из исходников |
| `mimalloc` | Microsoft mimalloc | **`v3.5.1`** | из исходников |
| `Xiph` | libogg **1.3.6** / libvorbis **1.3.7** / libtheora **1.2.0** | — | из исходников (4 проекта) |
| `lz4` | LZ4 | ветка **`dev`** ⚠️ | из исходников |
| `zstd` | Zstandard | ветка **`dev`** ⚠️ | из исходников |
| `lzo` | minilzo | **2.10** | из исходников, ~70 `.c` |
| `libsquashfs` | libsqfs поверх `AgentD/squashfs-tools-ng` | `master` ⚠️ | из исходников, ~50 `.c` |
| `ode` | Open Dynamics Engine, модифицированный OGSR | вендорен | из исходников, LGPL-2.1+ |
| `DirectXTex` | Microsoft DirectXTex | ветка `mar2025` | из исходников, 20 `.cpp` |
| `DirectXMath` | Microsoft DirectXMath | `main` ⚠️ | header-only |
| `DirectXMesh` | форк OGSR | `main` ⚠️ | **мёртвый вес** — ноль `#include` |
| `concurrentqueue` | cameron314 | `master` ⚠️ | header-only |
| `cpputils` | tzcnt/cpputils | `main` ⚠️ | header-only |
| `d3dx` | DirectX9 headers | — | header-only; `d3dx9.lib` **не линкуется** |
| `eax` | EAX / DS3D EAX v2.0 (1999) | — | header-only |

### Состав `NVIDIA_DLSS` после `Update_Components.cmd`

Заголовки (`DLSS/include/`, 16 файлов):

| Файл | Размер | Используется движком |
|---|---|---|
| `nvsdk_ngx.h` | 40 108 | ★ `RenderTargetPhaseAA.cpp:33`, `xrRender_console.cpp:5` |
| `nvsdk_ngx_helpers.h` | 39 321 (в 310.9.1 — 5 317) | ★ `RenderTargetPhaseAA.cpp:34` |
| `nvsdk_ngx_defs.h` | 44 313 | транзитивно |
| `nvsdk_ngx_defs_dlssd.h` | 14 725 | нет |
| `nvsdk_ngx_defs_dlssg.h` | 23 156 | нет |
| `nvsdk_ngx_params.h` | 7 744 | нет |
| остальные | — | нет |

Библиотеки (`DLSS/lib/Windows_x86_64/x64/`) — **ровно эта папка** в
`AdditionalLibraryDirectories`:

| Файл | Размер | Кто линкует |
|---|---|---|
| `nvsdk_ngx_s.lib` | 4 740 514 | `RenderTargetPhaseAA.cpp:47` |
| `nvsdk_ngx_s_dbg.lib` | 4 967 260 | `RenderTargetPhaseAA.cpp:41` |
| `nvsdk_ngx_s_dbg_iterator0.lib` | 4 768 676 | `RenderTargetPhaseAA.cpp:39` (**Debug**) |
| `nvsdk_ngx_d.lib` | 5 222 618 | `RenderTargetPhaseAA.cpp:45` (при `_DLL`) |
| ещё 6 вариантов | — | не используются |

DLL в рантайм:

| Файл | Размер | Копируется |
|---|---|---|
| `rel/nvngx_dlss.dll` | 58 977 904 | `XR_3DA.vcxproj:111` (Release) |
| `dev/nvngx_dlss.dll` | 69 725 808 | `XR_3DA.vcxproj:183` (Debug) |
| `nvngx_dlssd.dll` (FG), `nvngx_dlssg.dll` (StreamLine) | ~48 / ~7 МБ | **не копируются** |

### Состав `FidelityFX-SDK`

Компилируются только **2 проекта**:

**`ffx_fsr3upscaler_x64.vcxproj`** (3 `.cpp`):
- `sdk/src/shared/ffx_assert.cpp`
- `sdk/src/shared/ffx_object_management.cpp`
- `sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp`

**`ffx_backend_dx11_x64.vcxproj`** (8 `.cpp`) — **добавленный в форк DX11-бэкенд**:
- `sdk/src/backends/dx11/{DXBCChecksum.c, ffx_dx11.cpp, md5.c}`
- `sdk/src/backends/shared/ffx_shader_blobs.cpp`
- `sdk/src/shared/ffx_assert.cpp`
- `sdk/src/backends/shared/blob_accessors/ffx_{fsr1,fsr2,fsr3upscaler,frameinterpolation,opticalflow}_shaderblobs.cpp`
- + 9 HLSL-пассов FSR3 как `ClInclude`

Дефайны второго: `FFX_FSR;FFX_FSR3`.

Подключение — `xrRenderPC_R4/xrRender_R4.vcxproj:477-482` (`ProjectReference`)
и `:64`, `:88` (include-путь `sdk\include`).

## 7. Скрипты

### `Update_Components.cmd`

12 деревьев, каждое через `RD /s /q` + `git clone --depth 1` (**без подмодулей**):

| # | Цель | Репозиторий | Ветка/тег |
|---|---|---|---|
| 1 | `3rd_party/Src/DirectXTex/DirectXTex` | microsoft/DirectXTex | `mar2025` |
| 2 | `3rd_party/Src/DirectXMesh/DirectXMesh` | **OGSR**/DirectXMesh | `main` ⚠️ |
| 3 | `3rd_party/Src/DirectXMath/DirectXMath` | microsoft/DirectXMath | `main` ⚠️ |
| 4 | `3rd_party/Src/concurrentqueue/concurrentqueue` | cameron314/concurrentqueue | `master` ⚠️ |
| 5 | `3rd_party/Src/libsquashfs/squashfs-tools-ng` | **AgentD**/squashfs-tools-ng | `master` ⚠️ |
| 6 | `3rd_party/Src/lz4/lz4` | lz4/lz4 | **`dev`** ⚠️ |
| 7 | `3rd_party/Src/zstd/zstd` | facebook/zstd | **`dev`** ⚠️ |
| 8 | `3rd_party/Src/mimalloc/mimalloc` | microsoft/mimalloc | `v3.5.1` |
| 9 | `3rd_party/Src/NVIDIA_DLSS/DLSS` | NVIDIA/DLSS | **`v310.9.1`** |
| 10 | `3rd_party/Src/cpputils/cpputils` | tzcnt/cpputils | `main` ⚠️ |
| 11 | `3rd_party/Src/DiscordRPC/DiscordRPC` + rapidjson | **OGSR**/discord-rpc, Tencent/rapidjson | `master`, `v1.1.0` |
| 12 | `3rd_party/Src/FidelityFX-SDK/FidelityFX-SDK` | **OGSR**/FidelityFX-SDK | `release-FSR3-3.1.2-DX11-Native-API` |

**Проблемы скрипта:**

1. **8 из 12 пин — движущиеся ветки.** Сборка невоспроизводима во времени.
2. **Нет `--recurse-submodules`.** В репозитории `NVIDIA/DLSS` есть
   submodule **`NVIDIAImageScaling`** (NIS) — он **не качается**.
   Это баг, из-за которого NIS недоступен (см. [06-rtx-feasibility.md](06-rtx-feasibility.md)).
3. **Нет проверки целостности и нет `|| exit /b 1`.** Обрыв сети посреди
   скрипта оставит half-populated дерево, а `pause` это не покажет.
4. **`RD /s /q` уничтожает предыдущий чекаут** до проверки успеха клона.

### `cleanup.cmd`

Удаляет: `3rd_party\_LIB`, `3rd_party\_TEMP`, `ogsr_engine\_LIB`,
`ogsr_engine\_TEMP`, `ogsr_engine\LuaJIT\bin`, `bin_x86\`, `bin_x64\`,
а также мусор `.vs\Engine\*` (раскладка **VS2010**, сейчас бесполезна)
и `.ipch` / `*.VC.*` / `*.bin*` внутри `.vs`.

Полезен тем, что сносит `LuaJIT\bin` — иначе залипшие бинарники LuaJIT
никогда не пересобираются (см. §8, п. 10).

## 8. Грабли при сборке

### 1. Windows SDK Debugging Tools — обязательны

`ogsr_engine/xr_3da/XR_3DA.vcxproj:403-405`:

```xml
<Target Name="PostBuildStep1" AfterTargets="Build">
  <Exec Command='copy /Y "$(WindowsSDKDir)Debuggers\$(PlatformShortName)\dbghelp.dll" "$(OutDir)dbghelp.dll"' />
</Target>
```

Нет ни `IgnoreExitCode`, ни `Condition`. Если компонент
**«Debugging Tools for Windows»** в SDK не установлен, `$(WindowsSDKDir)Debuggers\x64\dbghelp.dll`
не существует → `copy` возвращает ненулевой код → цель падает →
**MSBuild reports build FAILED**, хотя EXE давно слинкован.

> Это классическая жалоба на сборку OGSR/X-Ray.
> Решение: установить «Debugging Tools for Windows» в Visual Studio Installer
> или Windows SDK.

### 2. `Update_Components.cmd` обязателен до первой сборки

Без него 8 из 24 проектов не могут даже вычислить список файлов
(DirectXTex, zstd, lz4, mimalloc, libsquashfs, DiscordRPC, ffx_*, DirectXMath/DirectXMesh),
а `xrCore`/`xrRender_R4` падают на отсутствующих include-папках.

### 3. Post-build копирования DLL безусловные

```
XR_3DA.vcxproj:108-113 (Release), :180-185 (Debug)
```

Копируют `soft_oal.dll` и `nvngx_dlss.dll` (`rel` для Release, `dev` для Debug).
Без скрипта из п.2 — падение.

### 4. Toolset / стандарт C++ зависят от версии VS

| `VisualStudioVersion` | Toolset | Стандарт |
|---|---|---|
| `18.0` (VS2026) | `v145` | `stdcpp23` |
| иначе (VS2022) | `v143` | `stdcpp20` |

Тот же исходник компилируется по-разному на разных студиях.
Preview/insider VS2026, у которых версия не ровно `18.0`, молча откатятся на `v143`.

### 5. `Game/ShadersSln` жёстко закреплён на v143

`Game/ShadersSln/ShadersSln.vcxproj:577`, `:583-602`:
`PlatformToolset v143`, `WindowsTargetPlatformVersion 10.0`.
Не импортирует `OgsrBuildProps.props`, в CI не собирается.
На машине только с VS2026 — не откроется.

### 6. `/we####` уронит сборку при обновлении MSVC

16 предупреждений повышены до ошибок, только в движковых проектах
(см. §3). Любое новое предупреждение MSVC из этого списка ломает сборку.

### 7. `DISABLE_DBG_ASSERTIONS` включён по умолчанию

Debug-сборка фактически **не** assert-тяжёлая.

### 8. `WindowsTargetPlatformVersion` не закреплён

Берётся самый новый установленный SDK. Preview-SDK может сдвинуть
заголовки D3D11/D3DCompiler.

### 9. `/MT` везде — CRT DLL не нужны

`CopyCppRuntimeToOutputDir` закомментирован, что согласуется со статическим CRT.
В рантайме нужны только системные DLL + `OpenAL32.dll`, `nvngx_dlss.dll`,
`LuaJIT.dll`, `dbghelp.dll`.

### 10. LuaJIT не пересобирается при наличии `LuaJIT.lib`

`luajit.vcxproj` короткозамыкает на `if not exist "..\bin\$(PlatformShortName)\LuaJIT.lib"`.
Без `Rebuild` (или `cleanup.cmd`) бинарник останется старым.

### 11. Манифест устарел

`ogsr_engine/xr_3da/manifest.xml` объявляет `supportedOS` только для
Windows 10 / 8.1 / 8 / 7. GUID Windows 11 (`{d48ca3fe-…}`) отсутствует.
DPI — в legacy-режиме.

### 12. Legacy DirectX SDK (June 2010) НЕ нужен

`d3dx` — только заголовки, `d3dx9.lib` не линкуется.
`dxguid.lib`, `d3dcompiler.lib`, `dinput8.lib` берутся из Windows 10/11 SDK.

## 9. CI/CD

`.github/workflows/push.yml` — один workflow «Main Workflow».
Триггеры: `push`, `pull_request`, `release: [created]`.

| Параметр | Значение |
|---|---|
| Runner | `windows-2022` |
| `fail-fast` | `false` |
| Матрица | `Configuration: [Release, ReleaseTracyProfiler]` × `Platform: [x64]` |
| Реальная конфигурация | `ConfigurationReal` — **обе** мапятся на `Release` |

Шаги:
1. `actions/checkout@v7.0.1` с `fetch-depth: 0`
2. `Update_Components.cmd`
3. `microsoft/setup-msbuild@v3`
4. `msbuild Engine.sln /t:Rebuild /p:Configuration=… /p:Platform=x64`
   при `set APPVEYOR_BUILD_VERSION=…`, `set APPVEYOR_REPO_NAME=…`,
   `set CONFIGURATION_GA=…`
5. Подготовка: `copy *.* ..\Game\Resources_SoC_1.0006\bin_x64`,
   затем `7z a -t7z -m0=LZMA2:d=96m:fb=273 -mx=9 -mmt=2`
6. `actions/upload-artifact@v7.0.1` (`archive: false`)
7. `shogo82148/actions-upload-release-asset@v1.10.2` — **только** на `release`

Секреты: только `secrets.GITHUB_TOKEN`.
Кэшей, тестов, линтеров, NuGet — **нет**.

Артефакт: `OGSR_Engine_SHOC_EDITION_x64_3.<run_number>_<Configuration>.7z`

> Название артефакта жёстко содержит `SHOC_EDITION`, хотя в репозитории есть
> и ветка `main_cop_cs`.

`.github/dependabot.yml` отслеживает **только GitHub Actions**
(`github-actions`, ежемесячно). Для 12 git-клонов зависимостей
экосистемы dependabot нет — отсюда ручные пины.

## 10. Git и лицензии

| Файл | Содержимое |
|---|---|
| `.gitmodules` | **отсутствует** (сабмодулей нет) |
| `.gitattributes` | только `*.script`, `*.ltx`, `*.puddles` → `text=crlf linguist-language=…`. **Git LFS отсутствует** |
| `.gitignore` | `/bin*`, `_LIB`, `_TEMP`, `*.ipch`, `/gamedata*`, `ogsr_build_info.hpp`, … |

> ⚠️ `Game\` **не** игнорируется. CI кладёт артефакты в
> `Game\Resources_SoC_1.0006\bin_x64\` — если собрать пакет локально,
> эта папка попадёт в коммит.

Архитектура зависимостей: клоны **внутри** git-игнорируемых папок
(по `.gitignore` в каждой папке `3rd_party/Src/*`).

### Лицензии

| Файл | Тип |
|---|---|
| `LICENSE.md` | модифицированный **BSD-3 с некоммерческим ограничением**: *«Neither binary nor source code redistributions may be used for any commercial purposes.»* Код помечен как проприетарный GSC Game World |
| `OpenAL_Soft/COPYING` | LGPL-2 |
| `FidelityFX-SDK/LICENSE.txt` | MIT (AMD, 2023) |
| `imgui/LICENSE.txt` | MIT |
| `tracy/LICENSE` | BSD-3-Clause |
| `tracy/public/libbacktrace/LICENSE` | LGPL-3 |
| `RenderDoc/renderdoc_app.h` | MIT |
| `reshade/*.hpp` | BSD-3-Clause OR MIT |
| `Xiph/*/COPYING` | BSD / Xiph |
| `ode/include/ode/*.h` | LGPL-2.1+ |
| `NVIDIA_DLSS/DLSS/LICENSE.txt` | NVIDIA DLSS SDK |

> Лицензия форка — **не** OSI-approved и **не** MIT/Apache.
> Это ограничивает любое коммерческое использование.