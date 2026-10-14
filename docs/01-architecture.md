# 01 — Архитектура

## 1. Что это за движок

**OGSR-Engine** — форк X-Ray / OpenXRay, движок серии STALKER.
Рендерер переписан на DirectX 11, сверху добавлены DLSS, FSR 3, HDR/ACES,
ReShade, Tracy, RenderDoc, ImGui, Impostor-подобные оптимизации и
мод-ориентированная скриптовая система.

Собран как **набор статических библиотек + один EXE**.

## 2. Слои

```
┌──────────────────────────────────────────────────────────────┐
│  Game/            gamedata, шейдеры HLSL, Lua-расширения      │
├──────────────────────────────────────────────────────────────┤
│  xrGame (2069)   игровая логика, AI, физика, UI (C++)          │
│  COMMON_AI (196) ALife, графы, СКРИПТОВЫЙ ДВИЖОК (C++)         │
├──────────────────────────────────────────────────────────────┤
│  Layers/         рендерер                                      │
│    xrRenderPC_R4  — CRender, кадр, RT-фазы                    │
│    xrRenderDX10   — бэкенд DirectX 11                         │
│    xrRender       — ядро, не зависящее от API                 │
├──────────────────────────────────────────────────────────────┤
│  xr_3da (161)    фронтенд: окно, CRenderDevice, движок цикла    │
│  xrCDB (115)     коллизии (CDB = collision detection) + Opcode │
│  xrSound (33)    звук на OpenAL Soft                          │
│  xrParticles (14) система частиц                              │
│  xrCore (105)    VFS, математика, память, XML, ini, LZ, таймеры│
├──────────────────────────────────────────────────────────────┤
│  Luabind (93) / LuaJIT (187) — Lua VM и биндинги               │
├──────────────────────────────────────────────────────────────┤
│  3rd_party/Src   24 папки (см. 03-build-and-dependencies.md)   │
└──────────────────────────────────────────────────────────────┘
```

## 3. Модули

| Модуль | Назначение |
|---|---|
| `xrCore` | VFS (`FS.*`, `LocatorAPI.*`, squashfs-монтирование `.sqfs`), математика (`_vector3d.h`, `_matrix.h`, …), контейнеры, `CInifile`, XML, LZ77+Huffman, тримминг строк, пул потоков, логер, Tracy |
| `xrCDB` | **CDB = collision detection.** Статическая геометрия: `CDB::MODEL` — AABB-дерево над треугольниками Opcode. Три вида запросов: `ray_query`, `box_query`, `frustum_query` (`xrCDB.h:128-130`). Синглтон `XRC` (`xrXRC.h:13`). Внутри — вендоренный **Opcode** (58 файлов) |
| `xrSound` | `CSoundRender_Core` поверх **OpenAL Soft** + EFX. Два CDB-профиля: `geom_SOM` (окклюзия) и `geom_ENV` (реверберация). Пулы: sources / emitters / targets |
| `xrParticles` | `PAPI::CParticleManager`. Действия грузятся из `.ope`, интерпретируются `particle_actions_collection.cpp`. Флаг `copFormat` читает оба формата (CoP и CS) |
| `xr_3da` | Фронтенд. `CApplication`, `CRenderDevice`, `WinMain`, ввод, консоль, `CGameFont`, ReShade, Discord RPC, **единственное место** где подключается Luabind |
| `xrGame` | Вся игровая логика: ~150 наследников `CGameObject`, уровень, сеть, физика (ODE), инвентарь, торговля, диалоги, задачи, весь C++-UI |
| `COMMON_AI` | Три вещи в одном: (1) серверные сущности `CSE_Abstract`, (2) графы и алгоритмы (`GRAPH/`, `PATH/`, `ALGORITHMS/`), (3) **скриптовый движок** (`CScriptEngine`, экспорт классов) |
| `xrRender*` | См. [02-renderer.md](02-renderer.md) |
| `Luabind` | Вендоренный luabind с двумя патчами OGSR (см. ниже) |
| `LuaJIT` | LuaJIT 2.1.0-beta3, **включая FFI** (`Game/Lua_Extensions/_G_add.lua:5`: `FFI = require('ffi')`) |

## 4. Ключевые решения

### 4.1 Всё статическое, CRT тоже

Все 23 C++-проекта используют `MultiThreaded`/`MultiThreadedDebug` (**/MT**).
`vcruntime140.dll`/`msvcp140.dll` в рантайме не нужны.

`ogsr_engine/xrCore/xrCore.h:17-19` жёстко падает с ошибкой, если `_MT` не определён.

### 4.2 `COMMON_AI` не является отдельным проектом

Его 188 исходников компилируются **напрямую в `xrGame.vcxproj`**
(например `<ClCompile Include="..\COMMON_AI\random32.cpp" />`).

### 4.3 Два независимых Lua VM

| VM | Где | Что грузит |
|---|---|---|
| Игровой | `COMMON_AI/script_engine.cpp:120-156` | `.script` из `$game_scripts$` |
| Шейдерный | `Layers/xrRender/ResourceManager_Scripting.cpp:576-592`, `xrRenderDX10/dx10ResourceManager_Scripting.cpp` | `.s` из `$game_shaders$/r3` |

Они **не видят глобалы друг друга**. Шейдерный компилирует шейдеры в
`gamedata/shaders.xr` (236 КБ, шипится в репозитории).

### 4.4 Luabind пропатчен в двух местах

1. **Регистрация классов в модуль, а не в `_G`** —
   `Luabind/src/create_class.cpp:129-166`: `create_class::stage1` ищет в
   upvalue локальную переменную `this` и, если нашёл, регистрирует класс
   в ней вместо глобалов. Вместе с обёрткой `FILE_HEADER`
   (`script_engine.cpp:19-26`) это даёт `db.game_object` вместо мусора в `_G`.
2. **Режим `LUABIND_NO_EXCEPTIONS`** — ошибки через колбэки
   (`set_error_callback`, `set_pcall_callback`), без C++-исключений.

### 4.5 VFS с модами

`fsgame.ltx` объявляет алиасы (`$game_shaders$`, `$game_scripts$`, …).
Архивы модов `*.xdb*` / `*.xsq*` из `$mod_dir$` монтируются в то же дерево
(`LocatorAPI.cpp:1320-1333`) и **конфликтуют по именам** с базовым `gamedata` —
побеждает порядок монтирования.

### 4.6 Система конфигурационных переопределений

`ogsr_engine/build_config_overrides/` (в репозитории только `.gitignore`).
Механизм в `ogsr_engine/xrCore/xrCore.h:4-8`:

```cpp
#if __has_include("..\build_config_overrides\build_config_defines.h")
#include "..\build_config_overrides\build_config_defines.h"
#else
#include "..\build_config_defines.h"
#endif
```

Положите туда `build_config_defines.h` — и он молча подменит отслеживаемый.
Аналогичный трюк для ключей шифрования: `trivial_encryptor_ovr.h`
(`LocatorAPI.cpp:1132`, `trivial_encryptor.cpp:4-5`).

## 5. Точки входа

| Функция | Файл:строка |
|---|---|
| `WinMain` | `ogsr_engine/xr_3da/x_ray.cpp:447` |
| Инициализация | `x_ray.cpp:202` — `Startup()`: звук, `CApplication`, `g_pGamePersistent`, VFS-базы, Discord, ReShade, `Device.Run()` |
| Создание игровых объектов | `xrGame/xrGame.cpp:16` `xrFactory_Create` / `:27` `xrFactory_Destroy` |
| Регистрация игровых команд | `xrGame/xrGame.cpp:36` `AttachGame()` |
| Инициализация Lua | `COMMON_AI/script_engine.cpp:120-156` `CScriptEngine::init()` |
| Инициализация рендерера | `xrRender/dxRenderDeviceRender.cpp:101-156` → `HW.CreateDevice()` → `RImplementation.create()` |

## 6. Строки подсистемы

### Lua

```
LuaJIT  →  luabind::open()  →  CScriptEngine::init()
                                   ├── export_classes(LSVM)        все C++-классы
                                   ├── luaL_openlibs
                                   ├── setup_auto_load()           __index на _G
                                   ├── process_file_if_exists("_G")
                                   └── register_script_classes()   class_registrator.script
```

Подробно — [04-scripting-and-game.md](04-scripting-and-game.md).

### Рендер

```
CRenderDevice::Run()  →  Begin()  →  CRender::Render()  →  phase_combine()  →  End()  →  Present
```

Подробно — [02-renderer.md](02-renderer.md).

## 7. Метаданные проекта

| Свойство | Значение |
|---|---|
| Решение | `Engine.sln`, Visual Studio 2026 (18.x) |
| Конфигурации | только `Debug|x64` и `Release|x64` |
| Стандарт C++ | `stdcpp20` (VS2022) или `stdcpp23` (VS2026) |
| Toolset | `v143` (по умолчанию) или `v145` при `VisualStudioVersion == 18.0` |
| SDK Windows | автоопределение последнего установленного |
| Ветки | `main` (SoC Edition), `main_cop_cs` (CoP/CS) |
| Артефакт CI | `OGSR_Engine_SHOC_EDITION_x64_3.<run>_<Config>.7z` |
| Размер кодовой базы | ~7 500 C++/HLSL/Lua файлов движка + gamedata |