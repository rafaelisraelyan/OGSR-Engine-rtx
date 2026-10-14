# 03 — Сборка и зависимости

> **Проверено против ветки `main_cop_cs`.** Исправлено против SoC-версии:
> `LanguageStandard` стал безусловным, флагов `/we####` 15 (не 16),
> `XR_3DA.vcxproj` A1-таргет сдвинулся, добавились два проекта.

## 1. Решение

`Engine.sln`, 247 строк, Visual Studio 2026 (18.6), SolutionGuid
`{1F9B04CC-5248-4AE5-97B7-66A0D0E5AA5E}` (`Engine.sln:245`),
`VisualStudioVersion = 18.6.11806.211` (`:4`),
`MinimumVisualStudioVersion = 10.0.40219.1` (`:5`).

**Конфигураций всего две** (`Engine.sln:108-109`):
```
Debug|x64 = Debug|x64
Release|x64 = Release|x64
```

## 2. Проекты — 26 штук

| # | Проект | Путь | Тип |
|---|---|---|---|
| 1 | `XR_3DA` → `xrEngine.exe` | `ogsr_engine\xr_3da\XR_3DA.vcxproj` | **Application** (`Subsystem=Windows`) |
| 2 | `xrCore` | `ogsr_engine\xrCore\` | StaticLib |
| 3 | `xrCDB` | `ogsr_engine\xrCDB\` | StaticLib |
| 4 | `xrSound` | `ogsr_engine\xrSound\` | StaticLib |
| 5 | `xrParticles` | `ogsr_engine\xrParticles\` | StaticLib |
| 6 | `xrRender_R4` | `ogsr_engine\Layers\xrRenderPC_R4\` | StaticLib |
| 7 | `xrGame` | `ogsr_engine\xrGame\` | StaticLib |
| 8 | `Luabind` | `ogsr_engine\Luabind\` | StaticLib |
| 9 | `LuaJIT` | `ogsr_engine\LuaJIT\src\luajit.vcxproj` | **Makefile** (→ `msvcbuild.bat`) |
| **10** | **`xrPhysics`** | `ogsr_engine\xrPhysics\` | StaticLib |
| **11** | **`xrNetServer`** | `ogsr_engine\xrNetServer\` | StaticLib |
| 12 | `DirectXTex` | `3rd_party\Src\DirectXTex\` | StaticLib |
| 13 | `imgui` | `3rd_party\Src\imgui\` | StaticLib |
| 14 | `lzo` | `3rd_party\Src\lzo\` | StaticLib |
| 15 | `ode` | `3rd_party\Src\ode\contrib\msvc7\ode_default\default.vcxproj` | StaticLib |
| 16 | `libsquashfs` | `3rd_party\Src\libsquashfs\` | StaticLib |
| 17 | `liblz4` | `3rd_party\Src\lz4\` | StaticLib |
| 18 | `libzstd` | `3rd_party\Src\zstd\` | StaticLib |
| 19 | `mimalloc-lib` → `mimalloc` | `3rd_party\Src\mimalloc\` | StaticLib |
| 20 | `DiscordRPC` | `3rd_party\Src\DiscordRPC\` | StaticLib |
| 21 | `ffx_backend_dx11_x64` | `3rd_party\Src\FidelityFX-SDK\` | StaticLib |
| 22 | `ffx_fsr3upscaler_x64` | `3rd_party\Src\FidelityFX-SDK\` | StaticLib |
| 23 | `libogg_static` | `3rd_party\Src\Xiph\libogg\...` | StaticLib |
| 24 | `libvorbis_static` | `3rd_party\Src\Xiph\libvorbis\...` | StaticLib |
| 25 | `libvorbisfile_static` | `3rd_party\Src\Xiph\libvorbisfile\...` | StaticLib |
| 26 | `libtheora_static` | `3rd_party\Src\Xiph\libtheora\...` | StaticLib |

**DLL-проектов нет.** DLL в рантайме: `LuaJIT.dll`, `OpenAL32.dll`,
`nvngx_dlss.dll`, `dbghelp.dll` **+ `dplay32.dll` / `dpwsockx.dll`** (DirectPlay8,
загружается системой — новая зависимость CoP).

## 3. Новые модули CoP

### `xrPhysics`

- **152 файла** (150 исходников: 59 `.cpp` + 91 `.h`) + `xrPhysics.vcxproj`
- Подкаталоги: `dcylinder/`, `tri-colliderknoopc/`
- Дефайны: `dSINGLE;XRPHYSICS_STATIC;XRCDB_STATIC;XRCORE_STATIC;XRSOUND_STATIC` (`:61`, `:117`)
- Include-пути: `..`, `$(SolutionDir)\3rd_party\Src\ode` (`:60`, `:116`)
- `ProjectReference` (`:327-340`): `ode`, `xrCDB`, `xrCore`, `xrSound`
- **Это не новая подсистема**, а выделение ODE-слоя из `xrGame`
  (`PHShell.cpp`, `PHElement.cpp`, `PHWorld.cpp`, `Physics.cpp`, …)
- **Мёртвый `DllMain`**: `xrPhysics.cpp` — 43 строки с `DllMain` в StaticLibrary,
  который никогда не вызывается
- `PHNetState.cpp` компилируется здесь, а не в `xrGame`

### `xrNetServer`

- **17 файлов** (16 исходников: 5 `.cpp` + 11 `.h`), включая `DPlay/`
- Протокол **DirectPlay 8** (COM), `#include "DPlay/dplay8.h"` (`stdafx.h:16`)
- Потребители: `CLevel : IPureClient` (`xrGame/Level.h:44`),
  `xrServer : IPureServer` (`xrGame/xrServer.h:43`)
- Консольные переменные `psNET_*` (`NET_Shared.h:11-18`)
- **Асимметрия конфигурации**: в Debug нет include-пути `..\xrCore` (`:116`),
  в Release есть (`:60`); в Debug есть `DEBUG;WIN32;`, в Release нет `WIN32`
- `DPlay/dplay8.h` и `dpaddr.h` не перечислены в `ClInclude`
- Подключается в `xrGame.vcxproj:2666-2668`

### Граф зависимостей

```
xrCore ──────────┬──> lzo
                 ├──> mimalloc-lib
                 └──> libsquashfs ──> libzstd, liblz4

libsquashfs ──> libzstd, liblz4

xrPhysics  ──ProjectReference──> ode, xrCDB, xrCore, xrSound
xrNetServer ──ProjectReference──> xrCore
xrCDB / xrParticles / xrSound / xrRender_R4
xrGame ──ProjectReference──> xrCDB, xrCore, xrParticles, xrSound,
                              xrPhysics(2662), xrNetServer(2666), imgui
                              + sln deps: ode, Luabind
XR_3DA.exe ──ProjectReference──> xrCDB, xrCore, xrParticles, xrSound,
                                  xrRender_R4, xrGame, imgui
                  + sln deps: xrGame, DiscordRPC, Luabind, libtheora_static
```

> ⚠️ `xrPhysics` и `xrNetServer` **не вложены ни в одну solution folder**
> и **не имеют sln-зависимостей** — порядок обеспечивается только `ProjectReference`.

> ⚠️ **Циклическая линковка**: `xrGame.vcxproj:103` (Release) / `:167` (Debug)
> содержат `xrEngine.lib` в `AdditionalDependencies`. Разрешается тем,
> что библиотеки статические.

### Линковка EXE (`XR_3DA.vcxproj:94`, `:164`)

```
ode.lib;libvorbis_static.lib;libvorbisfile_static.lib;OpenAL32.lib;lzo.lib;
libogg_static.lib;libtheora_static.lib;vfw32.lib;winmm.lib;dinput8.lib;
d3d11.lib;d3dcompiler.lib;dxgi.lib;dxguid.lib
```

`#pragma comment(lib, …)` в исходниках — см. таблицу в
[06-rtx-feasibility.md](06-rtx-feasibility.md). На CoP добавлено
`xrPhysics/xrPhysics.cpp:9` → `ode.lib`.

## 4. MSBuild-свойства

### `OgsrBuildProps.props` (45 строк)

```xml
:8   <PlatformToolset Condition="'$(VisualStudioVersion)' == '18.0'">v145</PlatformToolset>
:10  <PlatformToolset Condition="'$(PlatformToolset)'==''">v143</PlatformToolset>
:12  <!--CopyCppRuntimeToOutputDir>true</...-->   закомментирован
:16  LatestTargetPlatformVersion = GetLatestSDKTargetPlatformVersion('Windows','10.0')
:17  WindowsTargetPlatformVersion   (если не пусто)
:20-22  EnableASAN — закомментирован
:26  <EnableEnhancedInstructionSet Condition="'$(Platform)'=='Win32'">StreamingSIMDExtension2
:28  <LanguageStandard>stdcpp20</LanguageStandard>          ← БЕЗУСЛОВНО!
:30  <BuildStlModules>false</BuildStlModules>
:31  <ConformanceMode>true</ConformanceMode>
:32  AdditionalOptions Release: /Zc:__cplusplus /Ob3
:33  AdditionalOptions Debug:   /Zc:__cplusplus
:35  Release: _MSVC_STL_USE_ABORT_AS_DOOM_FUNCTION
:36  Debug:  _MSVC_STL_USE_ABORT_AS_DOOM_FUNCTION;_MSVC_STL_HARDENING=1;_ITERATOR_DEBUG_LEVEL=0
:37  <BufferSecurityCheck>true</BufferSecurityCheck>
:41  <Link><LargeAddressAware>true</LargeAddressAware>
```

> ⚠️ **Исправление против SoC-версии:** на ветке `main` стандарт был условным
> (`stdcpp23` при `VisualStudioVersion == 18.0`, иначе `stdcpp20`).
> **На CoP-ветке условности нет — всегда `stdcpp20`.**
> `_ITERATOR_DEBUG_LEVEL=0` в Debug сделан намеренно, чтобы линковаться
> с `nvsdk_ngx_s_dbg_iterator0.lib`.

### `ogsr_engine/Engine_properties.props` (40 строк, для EXE)

```
:6    OutDir = $(SolutionDir)\bin_$(PlatformShortName)\
:7    IntDir = $(SolutionDir)\ogsr_engine\_TEMP\Engine\$(Configuration)\$(Platform)\$(ProjectName)\
:12-14 AssemblerListingLocation / ObjectFileName / ProgramDataBaseFileName → _TEMP
:15   /source-charset:utf-8 + 15 флагов /we####
:16   AdditionalIncludeDirectories = 3rd_party\Src\tracy\public
:17   SDLCheck = true
:18-22 _CRT_SECURE_NO_WARNINGS; _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
:25   ImportLibrary = _LIB\Engine\$(Configuration)\$(Platform)\$(TargetName).lib
:26   AdditionalLibraryDirectories = 6 путей (DLSS/x64, _LIB, _LIB\Engine,
       OpenAL_Soft\libs, _LIB\Release, _LIB\Engine\Release)
:30-39 ItemDefinitionGroup Condition="'$(CONFIGURATION_GA)'=='ReleaseTracyProfiler'"
       → PreprocessorDefinitions TRACY_ENABLE; TRACY_NO_FRAME_IMAGE; TRACY_ON_DEMAND
```

### `ogsr_engine/Engine_lib_properties.props`

Байт-в-байт как предыдущий, кроме `:6` → `$(SolutionDir)\ogsr_engine\_LIB\Engine\...`.
Порядок сборки: все статические библиотеки движка кладутся в общий
`_LIB\Engine\<Config>\<Plat>\`.

### `3rd_party/Src/3rd_party_lib_properties.props` (20 строк)

```
:6   OutDir = 3rd_party\_LIB\$(Configuration)\$(Platform)\
:7   IntDir = 3rd_party\_TEMP\...
:12-14 AssemblerListingLocation / ObjectFileName / ProgramDataBaseFileName
:17  Lib/OutputFile = 3rd_party\_LIB\...\$(TargetName).lib
```

Без `/we####`, без `SDLCheck`, без Tracy.

### `3rd_party/Src/3rd_party_properties.props` — мёртвый файл

Не импортируется ни одним `.vcxproj`.

### Флаги `/we####` — ровно 15

```
4566, 4828, 4715, 4189, 4595, 4834, 4018, 4101
4552, 4551, 4477, 4390, 4806, 4002, 4302
```

`Engine_properties.props:15` и `Engine_lib_properties.props:15`.
Только движковые проекты — главный источник «не собирается после обновления MSVC».

## 5. Переопределение конфигурации

`ogsr_engine/build_config_defines.h` (67 строк). Активно:

| Строка | Макрос | Значение |
|---|---|---|
| 7 | `USE_OWN_MINI_DUMP` | minidump при падении |
| 19 | `DISABLE_DBG_ASSERTIONS` | asserts выключены **даже в Debug** |
| 25 | `FSLTX` | `"fsgame.ltx"` |
| 28 | `NEWS_TO_SHOW` | `500` |
| 53 | `USE_TEXTURES_LTX` | — |
| 61 | `ANIMATED_PHYSICS_OBJECT_SUPPORT` | — |
| 64 | **`GAMESAVE_SIZE`** | **`128`** (на SoC было `768`) |

> ⚠️ **Расхождение внутри файла.** Комментарий на `:63` гласит
> `//По умолчанию было 128х128 вроде б. У нас 768х768` — но значение
> в коде **128**. Комментарий устарел.

Закомментированы (9): `OGSR_TOTAL_DBG` (`:10`), `CRASH_ON_INVALID_VERTEX_ID` (`:13`),
`USE_MEMORY_VALIDATOR` (`:22`), `DUPLET_STATE_SWITCH` (`:36`),
`APPLY_ARMOR_PIERCING_TO_NPC` (`:41`), `REMOVE_ARTICLES_ON_DISABLE_INFO` (`:45`),
`USE_STORY_ID_AS_SPAWN_ID` (`:49`), `USE_REDUCE_LOD_TEXTURE_LIST` (`:56`),
`DISABLE_DISCORD` (`:67`).

### `xrCore.h` — жёсткие проверки

```cpp
:4-8    __has_include("..\build_config_overrides\build_config_defines.h")
        иначе "..\build_config_defines.h"
:10-12  #error при RTC + ASAN
:17-19  #error при неопределённом _MT ("Please enable multi-threading library...")
:33-36  _DEBUG + DISABLE_DBG_ASSERTIONS → принудительно #define NDEBUG / #undef DEBUG
:38-40  MASTER_GOLD
```

Переопределить, не трогая git:
```
ogsr_engine/build_config_overrides/build_config_defines.h
ogsr_engine/build_config_overrides/trivial_encryptor_ovr.h   (LocatorAPI.cpp:1132)
```

## 6. Как работает Tracy

**`ReleaseTracyProfiler` не является конфигурацией MSBuild.** В `Engine.sln`
её нет. Она встречается только в `push.yml` и в
`Engine_*_properties.props:30-39`, где сравнивается с `$(CONFIGURATION_GA)`
— переменной **окружения**.

CI делает:
```cmd
set CONFIGURATION_GA=ReleaseTracyProfiler
msbuild Engine.sln /p:Configuration=Release /p:Platform=x64
```

MSBuild подставляет переменную окружения в `$(CONFIGURATION_GA)`, условие
срабатывает, а фактическая конфигурация остаётся `Release`.

> Дополнительно: `generate_build_info.bat:17-19` тоже пишет
> `#define CONFIGURATION_GA "..."` в `ogsr_build_info.hpp` — макро доступен
> и как C-препроцессорный дефайн.

> ⚠️ Tracy-сборка и обычная Release пишут в **одни и те же** `bin_x64\` и
> `_LIB\Engine\Release\x64\`. Неразличимы на диске.

Клиент Tracy компилируется **безусловно** одним TU внутри `xrCore`
(`xrCore.vcxproj:139` → `TracyClient.cpp:16`), который гасит себя по
`#ifdef TRACY_ENABLE`.

## 7. Пост-события XR_3DA.vcxproj

| Что | Release | Debug |
|---|---|---|
| **A1: копирование `dbghelp.dll`** | **`:395-397`** | `:395-397` |
| Post-build DLL (`soft_oal.dll`→`OpenAL32.dll`, `rel/nvngx_dlss.dll`) | `:108-113` | `:180-185` (использует `dev\`) |
| Manifest (`manifest.xml`) | `:114-116` | `:186-188` |
| **PreBuildEvent `generate_build_info.bat`** | `:117-119` | `:189-191` |

**Проблема A1:**
```xml
:395  <Target Name="PostBuildStep1" AfterTargets="Build">
:396    <Exec Command='copy /Y "$(WindowsSDKDir)Debuggers\$(PlatformShortName)\dbghelp.dll" "$(OutDir)dbghelp.dll"' />
:397  </Target>
```
Нет ни `IgnoreExitCode`, ни `Condition`. Без компонента «Debugging Tools for
Windows» `$(WindowsSDKDir)Debuggers\x64\dbghelp.dll` не существует → `copy`
возвращает ненулевой код → цель падает → **MSBuild reports build FAILED,
хотя EXE давно слинкован**. Это классическая жалоба на сборку OGSR/X-Ray.

## 8. LuaJIT

`ogsr_engine/LuaJIT/src/luajit.vcxproj` — Makefile, но импортирует
**`Engine_properties.props`** (EXE-вариант), а не `_lib_`. Значит получает
`OutDir = bin_x64` и `/we####`+Tracy-дефайны (которые для C бесполезны).

```
:34   NMakeOutput = ..\bin\$(PlatformShortName)\LuaJIT.dll
:37-39 короткое замыкание: if not exist "..\bin\$(PlatformShortName)\LuaJIT.lib"
:41-47 копирует LuaJIT.dll + .pdb в OutDir, если ещё нет
:49-56 создаёт _LIB, копирует .lib / .exp
:58-79 NMakeReBuildCommandLine — сначала RD /S/Q ..\bin, потом msvcbuild.bat
:80   NMakeCleanCommandLine = RD /S/Q ..\bin
```

Короткое замыкание означает: **без `Rebuild` старый `LuaJIT.lib` уже
не пересоберётся**.

## 9. 3rd_party — 22 папки

**Вендорены в дереве (10):**
`d3dx`, `eax`, `imgui`, `lzo`, `ode`, `OpenAL_Soft`, `RenderDoc`, `reshade`,
`tracy`, `Xiph`

**Качаются скриптом (12):**
`concurrentqueue`, `cpputils`, `DirectXMath`, `DirectXMesh`, `DirectXTex`,
`DiscordRPC`, `FidelityFX-SDK`, `libsquashfs`, `lz4`, `mimalloc`,
`NVIDIA_DLSS`, `zstd`

| Папка | Что | Версия | Сборка |
|---|---|---|---|
| `NVIDIA_DLSS` | NVIDIA DLSS SDK (NGX) | **`v310.9.1`** | готовые `.lib`/`.dll` |
| `FidelityFX-SDK` | AMD FidelityFX, **форк OGSR** с DX11-бэкендом | `release-FSR3-3.1.2-DX11-Native-API` (FSR **3.1.2**) | из исходников, 2 проекта |
| `imgui` | Dear ImGui | **v1.92.9 WIP** (`imgui.h:32-33`) | из исходников |
| `tracy` | Tracy | 0.11.2 на SoC; на CoP **не проверяемо** (нет макроса версии) | 1 TU внутри `xrCore` |
| `RenderDoc` | In-app API | MIT | header-only |
| `reshade` | ReShade addon SDK | API 5 | header-only |
| `OpenAL_Soft` | OpenAL Soft пребилт | 1.25.1 (размер совпадает) | готовые |
| `DiscordRPC` | discord-rpc, форк OGSR + rapidjson `v1.1.0` | | из исходников |
| `mimalloc` | Microsoft mimalloc | **`v3.5.1`** | из исходников |
| `Xiph` | libogg / libvorbis / libtheora | | из исходников, 4 проекта |
| `lz4` | LZ4 | ветка **`dev`** ⚠️ | из исходников |
| `zstd` | Zstandard | ветка **`dev`** ⚠️ | из исходников |
| `lzo` | **полная библиотека LZO** (не minilzo!) | `lzo_conf.h:1,5`, ~67 `.c` | из исходников |
| `libsquashfs` | libsqfs на `AgentD/squashfs-tools-ng` | `master` ⚠️ | из исходников |
| `ode` | Open Dynamics Engine | LGPL-2.1+ | из исходников |
| `DirectXTex` | Microsoft DirectXTex | `mar2025`, **20 `.cpp`** | из исходников |
| `DirectXMath` | Microsoft DirectXMath | `main` ⚠️ | header-only |
| `DirectXMesh` | форк OGSR | `main` ⚠️ | **мёртвый вес**: в клоне 11 vcxproj, в решении нет, 0 `#include` |
| `concurrentqueue` | cameron314 | `master` ⚠️ | header-only |
| `cpputils` | tzcnt/cpputils | `main` ⚠️ | header-only |
| `d3dx` | DirectX9 headers | — | header-only; `d3dx9.lib` не линкуется |
| `eax` | EAX / DS3D EAХ 2.0 | — | header-only |

### NVIDIA_DLSS после `Update_Components.cmd`

Заголовки — 23 файла (не 16). Размеры для **310.9.1**:

| Файл | Размер |
|---|---|
| `nvsdk_ngx.h` | 41 001 |
| `nvsdk_ngx_helpers.h` | 5 436 |
| `nvsdk_ngx_defs.h` | 45 172 |
| `nvsdk_ngx_defs_dlssd.h` | 14 859 |
| `nvsdk_ngx_defs_dlssg.h` | 23 257 |

Библиотеки `lib/Windows_x86_64/x64/` — **10 файлов**, из них движок линкует 4:

| Файл | Размер | Кто линкует |
|---|---|---|
| `nvsdk_ngx_s.lib` | 3 782 558 | `RenderTargetPhaseAA.cpp:47` |
| `nvsdk_ngx_s_dbg.lib` | 3 949 484 | `:41` |
| `nvsdk_ngx_s_dbg_iterator0.lib` | 3 803 914 | `:39` (Debug) |
| `nvsdk_ngx_d.lib` | 4 090 730 | `:45` |

DLL в рантайм:

| Файл | Размер | Копируется |
|---|---|---|
| `rel/nvngx_dlss.dll` | **58 956 912** | `XR_3DA.vcxproj:111` |
| `dev/nvngx_dlss.dll` | **59 383 920** | `XR_3DA.vcxproj:183` |
| `rel/nvngx_dlssd.dll` (FG) | 48 343 664 | **не копируется** |
| `rel/nvngx_dlssg.dll` (StreamLine) | 7 460 976 | **не копируется** |

### FidelityFX-SDK

Компилируются **2 проекта**:

**`ffx_fsr3upscaler_x64.vcxproj`** — 3 `.cpp`
(`ffx_assert.cpp:101`, `ffx_object_management.cpp:102`, `ffx_fsr3upscaler.cpp:104`)

**`ffx_backend_dx11_x64.vcxproj`** — **10 `.cpp`** (не 8):
```
:100 DXBCChecksum.c            :108 ffx_fsr1_shaderblobs.cpp
:101 ffx_dx11.cpp              :110 ffx_fsr2_shaderblobs.cpp
:102 md5.c                     :112 ffx_fsr3upscaler_shaderblobs.cpp
:104 ffx_shader_blobs.cpp       :114 ffx_frameinterpolation_shaderblobs.cpp
:106 ffx_assert.cpp            :116 ffx_opticalflow_shaderblobs.cpp
```
+ **10** HLSL-пассов FSR3 как `ClInclude` (`:118-127`):
`accumulate`, `autogen_reactive`, `debug_view`, `luma_instability`,
`luma_pyramid`, `prepare_inputs`, `prepare_reactivity`, `rcas`,
`shading_change`, `shading_change_pyramid`

Дефайны `FFX_FSR;FFX_FSR3` (`:43`, `:74`) — только в backend-проекте,
`ffx_fsr3upscaler_x64` их **не имеет**.

Подключение: `xrRender_R4.vcxproj:477`, `:480` (`ProjectReference`),
include `sdk\include` (`:64`, `:88`).

Версия: `sdk/include/FidelityFX/host/ffx_fsr3.h:36,41,46` →
**мажор 3, минор 1, патч 2**.

## 10. Скрипты

### `Update_Components.cmd` — 38 строк, 12 деревьев

| # | Строки | Цель | Репозиторий | Ветка/тег |
|---|---|---|---|---|
| 1 | 1-2 | `DirectXTex` | microsoft/DirectXTex | `mar2025` |
| 2 | 4-5 | `DirectXMesh` | **OGSR**/DirectXMesh | `main` ⚠️ |
| 3 | 7-8 | `DirectXMath` | microsoft/DirectXMath | `main` ⚠️ |
| 4 | 10-11 | `concurrentqueue` | cameron314 | `master` ⚠️ |
| 5 | 13-14 | `squashfs-tools-ng` | **AgentD** | `master` ⚠️ |
| 6 | 16-17 | `lz4` | lz4/lz4 | **`dev`** ⚠️ |
| 7 | 19-20 | `zstd` | facebook/zstd | **`dev`** ⚠️ |
| 8 | 22-23 | `mimalloc` | microsoft/mimalloc | `v3.5.1` |
| 9 | 25-26 | **`NVIDIA_DLSS`** | NVIDIA/DLSS | **`v310.9.1`** |
| 10 | 28-29 | `cpputils` | tzcnt/cpputils | `main` ⚠️ |
| 11 | 31-33 | `DiscordRPC` + rapidjson | **OGSR**/discord-rpc, Tencent | `master`, `v1.1.0` |
| 12 | 35-36 | `FidelityFX-SDK` | **OGSR** | `release-FSR3-3.1.2-DX11-Native-API` |

**Проблемы скрипта (все четыре подтверждены):**
1. **8 из 12 пин — движущиеся ветки.** Сборка невоспроизводима во времени
2. **Нет `--recurse-submodules`** → submodule `NVIDIAImageScaling` (NIS)
   не инициализируется (см. [06-rtx-feasibility.md](06-rtx-feasibility.md) §3.2)
3. **Нет `|| exit /b 1`** — обрыв сети оставит half-populated дерево
4. `RD /s /q` **до** клона, `pause` в конце маскирует ситуацию

### `cleanup.cmd` — 18 строк

Удаляет `3rd_party\_LIB`, `3rd_party\_TEMP`, `ogsr_engine\_LIB`,
`ogsr_engine\_TEMP`, `LuaJIT\bin`, `bin_x86\`, `bin_x64\`, плюс мусор
`.vs\Engine\*` (раскладка **VS2010** — сейчас бесполезна).

## 11. CI/CD

`.github/workflows/push.yml` — 76 строк.

| Параметр | Значение |
|---|---|
| Имя | `Main Workflow` (`:1`) |
| Триггеры | `push`, `pull_request`, `release: [created]` (`:3-8`) |
| Runner | `windows-2022` (`:12`) |
| `fail-fast` | `false` (`:14`) |
| Матрица | `[Release, ReleaseTracyProfiler] × [x64]` (`:17-18`) |
| `ConfigurationReal` | обе → `Release` (`:22-25`) |
| `Update_Components` | `:31-33` — `run: ${{ github.workspace }}/Update_Components.cmd` |
| MSBuild | `:40-44`, env `CONFIGURATION_GA`, `APPVEYOR_*` |
| **Имя артефакта** | `OGSR_Engine_COP_EDITION_${{ matrix.Platform }}_3.${{ github.run_number }}_${{ matrix.Configuration }}.7z` (`:64`) |
| Упаковка | `7z a -t7z -m0=LZMA2:d=96m:fb=273 -mx=9 -mmt=2` (`:60`) |
| Upload artifact | `:62` — **`archive: false` отсутствует** в `with:` |
| Release-upload | `:68` только на `release`, `shogo82148/actions-upload-release-asset@v1.10.2` |

**Путь к артефактам:** `push.yml:54`, `:56`, `:58`:
```
md ..\Game\Resources_CoP_1.6.02\bin_%PLATFORM%
copy *.* ..\Game\Resources_CoP_1.6.02\bin_%PLATFORM%
cd ..\Game\Resources_CoP_1.6.02
```

Секреты: только `secrets.GITHUB_TOKEN`. Кэшей, тестов, линтеров, NuGet — **нет**.

`dependabot.yml` — 8 строк, только `github-actions`, `monthly`.
Для git-клонов зависимостей экосистемы нет — отсюда ручные пины.

## 12. Git и лицензии

| Файл | Содержимое |
|---|---|
| `.gitmodules` | **отсутствует** |
| `.gitattributes` | 4 строки: `*.script`, `*.ltx`, `*.puddles` → `text=crlf linguist-language=…`. **Git LFS отсутствует.** Нет `binary` для `.png`/`.ico`/`.ttf`/`.xr` |
| `.gitignore` | `/bin*`, `_LIB`, `_TEMP`, `*.ipch`, `/gamedata*`, `ogsr_build_info.hpp`, … |

> ⚠️ `Game\` **не** игнорируется — `Game\Resources_CoP_1.6.02\bin_x64\`
> (куда CI кладёт артефакты) не защищён
> (`git check-ignore` → exit 1). Локальная сборка пакета закоммитит папку.

### Лицензии

| Файл | Тип |
|---|---|
| `LICENSE.md` | модифицированная **BSD-3 с некоммерческим ограничением** |
| `OpenAL_Soft/COPYING` | LGPL-2 |
| `FidelityFX-SDK/LICENSE.txt` | MIT (AMD, 2023) |
| `imgui/LICENSE.txt` | MIT |
| `tracy/LICENSE` | BSD-3-Clause |
| `RenderDoc/renderdoc_app.h` | MIT |
| `reshade/*.hpp` | BSD-3-Clause OR MIT |
| `Xiph/*/COPYING` | BSD / Xiph |
| `ode/include/ode/*.h` | LGPL-2.1+ |
| `NVIDIA_DLSS/DLSS/LICENSE.txt` | NVIDIA DLSS SDK |

> Лицензия форка — **не** OSI-approved и **не** MIT/Apache.
> Любое коммерческое использование запрещено.

## 13. Грабли при сборке

### 13.1. ATL — обязательна (у нас так и было)

`splash.cpp:11-12` включает `<atlstr.h>` и `<atlimage.h>`:

```cpp
#include <atlstr.h>
#include <atlimage.h>
...
CImage img;   // :73
```

ATL-заголовки линкуют библиотеки сами, через `#pragma comment(lib, …)`
(`atlbase.h:114` → `atls.lib`, `atlimage.h:51-54` → `gdi32/shlwapi/gdiplus/msimg32`),
поэтому в `XR_3DA.vcxproj` ATL-библиотеки не указаны.

**Грабель, на которую мы наткнулись:** MSBuild выбрал тулсет **14.44.35207**,
а компонент ATL установился в **14.30.30705**. Причина: в
`MSBuild\Microsoft\VC` есть только `v150`, `v160`, `v170` — `v143` там нет,
поэтому `PlatformToolset=v143` разрешается в самый свежий тулсет.

Обход, подтверждённый на практике, через временный build-скрипт
(репозиторий и установку VS не трогаем):
```cmd
set "CL=/I"%ATLROOT%\include""
set "LINK=/LIBPATH:"%ATLROOT%\lib\x64""
```
Обе переменные — **после** `vcvars64.bat`:
`vcvars64` перезаписывает `LIB`, а MSBuild перетирает `LIB` из
`AdditionalLibraryDirectories`. `LINK` не перетирается.
Попытка `set LIB=…` **не работает**.

### 13.2. A1 — `dbghelp.dll`

См. §7. Косметическое падение post-build при линкованном EXE.

### 13.3. Остальное

- `/we####` уронит сборку при обновлении MSVC
- `stdcpp20` безусловный — кодовая база не использует C++23
- `WindowsTargetPlatformVersion` не закреплён — preview-SDK может сдвинуть
  заголовки D3D11/D3DCompiler
- `Game/ShadersSln` жёстко закреплён на v143, в CI не собирается
- `DISABLE_DBG_ASSERTIONS` включён по умолчанию
- `directx` (June 2010) не требуется
- `/MT` везде → CRT DLL не нужны
- Манифест `xr_3da/manifest.xml` не знает про Windows 11 GUID
- **Новая зависимость в рантайме (CoP):** `dplay32.dll` / `dpwsockx.dll` (DirectPlay8)