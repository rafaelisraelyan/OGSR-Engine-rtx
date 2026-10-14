# 01 — Архитектура

> **Проверено против ветки `main_cop_cs` (Call of Pripyat).** Документ описывает
> именно эту ветку. Отличия от SoC-ветки (`main`) отмечены в
> [07-known-issues.md](07-known-issues.md) §H.

## 1. Что это за движок

**OGSR-Engine** — форк X-Ray / OpenXRay, движок серии STALKER.
Рендерер переписан на **DirectX 11**, поверх добавлены DLSS, FSR 3, HDR/ACES,
ReShade, Tracy, RenderDoc, ImGui, мод-ориентированная скриптовая система.

Собран как **набор статических библиотек + один EXE**.

## 2. Слои

```
┌──────────────────────────────────────────────────────────────┐
│  Game/            641 файл, 73,6 МБ                         │
│    Resources_CoP_1.6.02/gamedata/ — оверлей OGSR             │
│    Lua_Extensions/          — рукописная Lua-библиотека      │
│    ShadersSln/              — IDE-проект для HLSL            │
├──────────────────────────────────────────────────────────────┤
│  xrGame (~2000+)    игровая логика, AI, физика-обвязка, UI    │
│    ai/              487 файлов                               │
├──────────────────────────────────────────────────────────────┤
│  xrServerEntities (122) — СКРИПТОВЫЙ ДВИЖОК + серверные       │
│    сущности + ALife + фабрика объектов                        │
│    (заменяет COMMON_AI, который был на SoC-ветке)             │
├──────────────────────────────────────────────────────────────┤
│  Layers/           рендерер                                   │
│    xrRenderPC_R4   — CRender, кадр, RT-фазы (52 исходника)   │
│    xrRenderDX10    — бэкенд DirectX 11 (53)                  │
│    xrRender        — ядро, не зависящее от API (217)         │
├──────────────────────────────────────────────────────────────┤
│  xr_3da           фронтенд: окно, CRenderDevice, цикл         │
│  xrPhysics (150)  ← новое: низкоуровневый ODE-слой            │
│  xrNetServer (16) ← новое: сеть на DirectPlay 8              │
│  xrCDB (115)      коллизии (CDB = collision detection)       │
│  xrSound (33)     звук на OpenAL Soft                        │
│  xrParticles (14) система частиц                             │
│  xrCore (105)     VFS, математика, память, XML, ini, таймеры  │
├──────────────────────────────────────────────────────────────┤
│  Luabind (93) / LuaJIT — Lua VM и биндинги                    │
├──────────────────────────────────────────────────────────────┤
│  3rd_party/Src     22 папки                                  │
└──────────────────────────────────────────────────────────────┘
```

## 3. Модули

| Модуль | Назначение |
|---|---|
| `xrCore` | VFS (`FS.*`, `LocatorAPI.*`, squashfs-монтирование `.sqfs`), математика (`_vector3d.h`, `_matrix.h`, …), контейнеры, `CInifile`, XML, LZ77+Huffman, тримминг строк, пул потоков, логер, Tracy |
| `xrCDB` | **CDB = collision detection.** Статическая геометрия: `CDB::MODEL` — AABB-дерево Opcode над `CDB::TRI`. Три вида запросов: `ray_query`, `box_query`, `frustum_query` (`xrCDB.h:128-130`). Синглтон `XRC`. Внутри — вендоренный **Opcode** |
| `xrSound` | `CSoundRender_Core` поверх **OpenAL Soft** + EFX. Два CDB-профиля: `geom_SOM` (окклюзия) и `geom_ENV` (реверберация) |
| `xrParticles` | `PAPI::CParticleManager`. Действия грузятся из `.ope`, интерпретируются `particle_actions_collection.cpp` |
| `xr_3da` | Фронтенд. `CApplication`, `CRenderDevice`, `WinMain`, ввод, консоль, `CGameFont`, ReShade, Discord RPC |
| `xrGame` | Вся игровая логика: сеть, физическая обвязка, инвентарь, торговля, диалоги, задачи, планировщик сталкеров, весь C++-UI |
| **`xrServerEntities`** | **Заменяет `COMMON_AI`.** Три вещи: (1) скриптовый движок (`script_engine*`), (2) серверные сущности `xrServer_Objects_ALife_*`, (3) фабрика объектов. **46 `.cpp` + 76 `.h`, без собственного `.vcxproj`** — компилируется прямо в `xrGame.vcxproj:1592-1634` |
| **`xrPhysics`** | **Новое на CoP.** Низкоуровневый ODE-слой: `CPHWorld`, `CPHElement`, `CPHShell`, `CPHSimpleCharacter`, `CPHJoint`, `CPHCapture`, `CPhysicsShell`. 150 исходников, дефайн `dSINGLE` (ODE в float). **Это не новая подсистема**: та же часть была в `xrGame/PH*.cpp` на SoC-ветке. Нового: `dcylinder/dCylinder`, `ExtendedGeom`, `PhysicsExternalCommon`, `MovementBoxDynamicActivate` |
| **`xrNetServer`** | **Новое на CoP.** Сеть на **DirectPlay 8** (COM): `IDirectPlay8Client`/`IDirectPlay8Server` (`NET_Client.h:46`, `NET_Server.h:80`), `DPNSEND_GUARANTEED`. Консольные переменные `psNET_*` (`NET_Shared.h:11-18`). Потребители: `CLevel : IPureClient` (`Level.h:44`), `xrServer : IPureServer` (`xrServer.h:43`) |
| `xrRender*` | См. [02-renderer.md](02-renderer.md) |
| `Luabind` | Вендоренный luabind с патчами OGSR (см. ниже) |
| `LuaJIT` | LuaJIT 2.1.0-beta3, **включая FFI** |

> ⚠️ **`GRAPH/`, `PATH/`, `ALGORITHMS/` исчезли.** На SoC-ветке они были в
> `COMMON_AI/`. На CoP их нет нигде как отдельных каталогов — графы уровня
> и A* переехали в `xrGame`. Это проверено поиском по имени каталога.

## 4. Ключевые решения

### 4.1 Всё статическое, CRT тоже

Все проекты используют `MultiThreaded`/`MultiThreadedDebug` (**/MT**).
`vcruntime140.dll`/`msvcp140.dll` в рантайме не нужны.

`ogsr_engine/xrCore/xrCore.h:17-19` жёстко падает, если `_MT` не определён.
Дополнительно `xrCore.h:10-12` — `#error` при включённом RTC + ASAN.

### 4.2 `xrServerEntities` и `COMMON_AI` — не отдельные проекты

Компилируются **напрямую в `xrGame.vcxproj`**. Обратная зависимость:
`xrServerEntities/PHNetState.cpp` компилируется **в `xrPhysics.vcxproj:165`**,
а `PHSynchronize.cpp` и `lua_studio.cpp` **не компилируются нигде** (мёртвый код).

### 4.3 Два независимых Lua VM

| VM | Где | Что грузит |
|---|---|---|
| Игровой | `xrServerEntities/script_engine.cpp:110` `CScriptEngine::init()` | `.script` из `$game_scripts$` |
| Шейдерный | `xrRender/ResourceManager_Scripting.cpp` | `.s` из `$game_shaders$/r3` |

Они **не видят глобалы друг друга**. На CoP **`shaders.xr` в репозитории отсутствует**
(на SoC был, 236 КБ).

### 4.4 Luabind пропатчен в двух местах

1. **Регистрация классов в модуль, а не в `_G`** —
   `Luabind/src/create_class.cpp`: `create_class::stage1` ищет в upvalue
   локальную переменную `this` и, если нашёл, регистрирует класс в ней.
   Вместе с обёрткой `FILE_HEADER` (`xrServerEntities/script_engine.cpp:17`)
   это даёт `db.game_object` вместо мусора в `_G`.
2. **Режим `LUABIND_NO_EXCEPTIONS`** — ошибки через колбэки
   (`set_error_callback` `:122`, `set_cast_failed_callback` `:123`,
   `set_pcall_callback` `:125`), без C++-исключений.

### 4.5 VFS с модами

`fsgame.ltx` объявляет алиасы (`$game_shaders$`, `$game_scripts$`, …).
В репозитории его нет — он приходит из игровых данных.
Архивы модов `*.xdb*` / `*.xsq*` из `$mod_dir$` монтируются в то же дерево
(`LocatorAPI.cpp:1320-1333`) и **конфликтуют по именам** с базовым gamedata.

### 4.6 Система конфигурационных переопределений

`ogsr_engine/build_config_overrides/` (в репозитории только `.gitignore`).
Механизм в `ogsr_engine/xrCore/xrCore.h:4-8` через `__has_include`.
Положите туда `build_config_defines.h` — он молча подменит отслеживаемый.
Аналогичный трюк для ключей шифрования: `trivial_encryptor_ovr.h`.

## 5. Точки входа

| Функция | Файл:строка |
|---|---|
| `WinMain` | `ogsr_engine/xr_3da/x_ray.cpp:447` |
| `Startup()` | `x_ray.cpp:202` |
| **`CApplication::CheckCsCopMode()`** | `x_ray.cpp:806` — **новое на CoP**: ветка «CoP lightmap'ы», управляет дефайном `USE_CS_COP_LMAP` в `r4.cpp:817-818` |
| **`CORE_FEATURE_SET(...)`** | `x_ray.cpp` — **новый механизм флагов фич**: `strict_infoportions_control`, `monsters_inventory`, `cop_rounded_minimap`, `cop_scale_bounds` |
| Создание игровых объектов | `xrGame/xrGame.cpp:16` `xrFactory_Create` |
| Регистрация игровых команд | `xrGame/xrGame.cpp:36` `AttachGame()` |
| **Инициализация Lua** | `xrServerEntities/script_engine.cpp:110` (на SoC было `:120`) |
| Инициализация рендерера | `xrRender/dxRenderDeviceRender.cpp` → `HW.CreateDevice()` → `RImplementation.create()` |

## 6. Lua: инициализация по шагам

`CScriptEngine::init()` — `xrServerEntities/script_engine.cpp:110`:

```
:113  luaL_newstate()                          → LSVM
:115  reinit(LSVM)                             → ScriptCrashHandler на "Debug"
:116  luabind::open(LSVM)                      → class_registry + class()
:122  luabind::set_error_callback
:123  luabind::set_cast_failed_callback
:125  luabind::set_pcall_callback              // "НЕ ЗАКОММЕНТИРОВАТЬ НИ В КОЕМ СЛУЧАИ!!!"
:129  export_classes(LSVM)                     → все C++-классы в скрипты
:130  luaL_openlibs(LSVM)
:131  setup_auto_load()                        // метаметод __index на самом _G
:134  process_file_if_exists(GlobalNamespace)  → $game_scripts$/_g.script
:139  register_script_classes()                → class_registrator.script
:140  object_factory().register_script()       → clsid._clsid + clsid_table
:142  lua_gc(..., LUA_GCSETSTEPMUL, 500)
:144  lua_gc(..., LUA_GCSETPAUSE, 100)
```

Вспомогательное: `setup_auto_load()` — `:95`,
`CollectScriptFiles()` — `:174`,
`register_script_classes()` — `:270`,
`class_registrator.register` — `:273`.

`*.lua` по-прежнему **исключён** из автозагрузки
(`script_engine.cpp:198`: `if ((strstr(fname, ".script") /*|| strstr(fname, ".lua")*/) && …)`).

## 7. Рендер

```
CRenderDevice::Run()  →  Begin()  →  CRender::Render()  →  phase_combine()  →  End()  →  Present
```

Подробно — [02-renderer.md](02-renderer.md).

## 8. Метаданные проекта

| Свойство | Значение |
|---|---|
| Решение | `Engine.sln`, Visual Studio 2026 (18.6) |
| Конфигурации | только `Debug|x64` и `Release|x64` (`Engine.sln:108-109`) |
| **Проектов** | **26** (на SoC было 23) |
| Toolset | `v143` по умолчанию, `v145` при `VisualStudioVersion == 18.0` |
| **Стандарт C++** | **`stdcpp20` безусловно** (`OgsrBuildProps.props:28`) — на SoC было условно! |
| SDK Windows | автоопределение последнего установленного |
| Ветки | `main` (SoC), `main_cop_cs` (CoP + CS) |
| Артефакт CI | `OGSR_Engine_COP_EDITION_x64_3.<run>_<Config>.7z` (`push.yml:64`) |
| Размер кодовой базы | ~7 500 файлов |
