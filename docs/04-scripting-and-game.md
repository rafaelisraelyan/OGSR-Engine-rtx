# 04 — Скрипты, игровые объекты, AI, UI

> **Проверено против ветки `main_cop_cs`.** Главные отличия от SoC-версии:
> `COMMON_AI` заменён на `xrServerEntities`, `SPAWN_VERSION` вырос до 128,
> `m_netpk.script` отсутствует, `CUICustomItem` удалён,
> `CScriptXmlInit` сменил набор методов.
> **Lua-UI теперь умеет DLSS** — см. §8.

## 1. Подключение Lua

Единственная точка входа для движка —
`ogsr_engine/xr_3da/ai_script_space.h` (проверено: вне `Luabind/` и
вне `LuaJIT/` `<lua.hpp>` тянется ровно из двух мест —
`ai_script_space.h` и `xrServerEntities/script_engine.cpp`).
Комментарий автора: *«KRodin: В этом файле подключается Luabind.
Только здесь и больше нигде.»*

## 2. Автоматическая регистрация классов

### Механизм типового списка

`ogsr_engine/xrServerEntities/script_export_space.h:15-23`:

```cpp
#ifdef SCRIPT_REGISTRATOR
# define script_type_list imdexlib::typelist<>
#  define add_to_type_list(type) using TypeList_##type = imdexlib::ts_prepend_t<type, script_type_list>;
#  define save_type_list(type) TypeList_##type
#else
#  define add_to_type_list(type) ;
#  define save_type_list(type)
#endif
```

Каждый экспортируемый заголовок заканчивается тройкой, после чего
`save_type_list(X)` раскрывается в `TypeList_X`, а это имя `#define`-ится
как `script_type_list` для следующего includer'а. Список накапливается
по цепочке включений.

**Счётчики на CoP:**

| Метрика | CoP | было на SoC |
|---|---|---|
| `add_to_type_list` | **218** | 194 |
| `DECLARE_SCRIPT_REGISTER_FUNCTION` | **153** | 153 |
| определений `script_register(lua_State*` | **224** | 231 |

### Драйвер

`ogsr_engine/xrServerEntities/script_engine_export.cpp`:

```cpp
#define SCRIPT_REGISTRATOR
#include "script_export_space.h"
#include "script_engine_export.h"

void export_classes(lua_State* L) { Register<script_type_list>::_Register(L); }
```

`Register<>::_Register` применяет `ts_reverse_t`, т.е. регистрация идёт
**в порядке объявления**. Мастер-список — `script_engine_export.h`.

## 3. Инициализация Lua

`CScriptEngine::init()` — **`xrServerEntities/script_engine.cpp:110`**
(на SoC было `COMMON_AI/script_engine.cpp:120-156`).

```
:110  void CScriptEngine::init()
:113  luaL_newstate()                          → LSVM
:115  reinit(LSVM)                             → ScriptCrashHandler на "Debug"
:116  luabind::open(LSVM)                      → class_registry + class()
:122  luabind::set_error_callback(lua_error)
:123  luabind::set_cast_failed_callback(lua_cast_failed)
:125  luabind::set_pcall_callback(lua_pcall_failed)   // "НЕ ЗАКОММЕНТИРОВАТЬ НИ В КОЕМ СЛУЧАИ!!!"
:128  extern void export_classes(lua_State * L);
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
`CollectScriptFiles()` — `:174`, `register_script_classes()` — `:270`.

### Ленивая загрузка

`setup_auto_load()` (`:95`) ставит на `_G` метаметод
`XRAY_AutoLoadMetaTable` с `__index = auto_load`:

```cpp
ai().script_engine().process_file_if_exists(lua_tostring(L, 2), false);
lua_rawget(L, 1);
```

**Любое обращение к несуществующему глобалу инициирует поиск на диске.**

`namespace_loaded()` обходит `_G` по точкам, проверяя, что каждый сегмент —
таблица. Отрицательные результаты мемоизируются в `no_files`.

Триггеры автозагрузки:
- `function_object("a.b.c", …)` из C++ — точка разбора в **последней** точке;
- `prefetch("name")` из Lua;
- обычный доступ к глобалу;
- резолв имени класса в `object_factory.register_script_class`.

### Файл → namespace

`CollectScriptFiles` (`:174`) обходит `$game_scripts$` рекурсивно;
для каждого пути, содержащего `.script`, берётся **basename до первой точки,
в нижнем регистре**. `_g.script` → `_g`. Коллизии молча отбрасываются.

> ⚠️ **`*.lua` по-прежнему исключён** (`script_engine.cpp:198`):
> `if ((strstr(fname, ".script") /*|| strstr(fname, ".lua")*/) && FS.exist(fname))`

## 4. Обёртка скрипта

Всё, кроме `_G`, оборачивается в `FILE_HEADER`
(`script_engine.cpp:17`):

```lua
local function script_name() return '{0}' end;
local this;
module('{0}', package.seeall, function(m) this = m end);
{1}
```

Именно `this` ищет пропатченный `Luabind/src/create_class.cpp`,
чтобы зарегистрировать класс в модуле, а не в `_G`.

## 5. Четыре способа расширить игру из Lua

### 5.1 Пассивный автозагрузчик

Всё, что не требует регистрации.

### 5.2 `class_registrator.register` — новый игровой объект

`script_engine.cpp:273`:

```cpp
ASSERT_FMT(functor("class_registrator.register", result), "[%s] Cannot load class_registrator!", __FUNCTION__);
```

Обращение к глобалу `class_registrator.register` подтягивает
`class_registrator.script`, который вызывает
`object_factory():register(client_class, server_class, clsid, script_clsid)`.

Далее объект создаётся из C++ по CLSID:
`MainMenu.cpp:189` → `NEW_INSTANCE(make_clsid("MAIN_MNU"))`
→ `xrFactory_Create` → `object_factory().client_object(clsid)`
→ `CObjectItemScript::client_object()` → Lua-конструктор.

> ⚠️ На CoP `MainMenu.cpp:189` использует `make_clsid("MAIN_MNU")`,
> а не `TEXT2CLSID("MAIN_MNU")` (как было на SoC `:129`).

### 5.3 `script_binding` в `.ltx` — поведение существующего класса

`xrGame/script_binder.cpp:62-68`:

```cpp
if (!pSettings->line_exist(section, "script_binding")) return;
...
if (!ai().script_engine().functor(pSettings->r_string(section, "script_binding"), lua_function)) {
    ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
                                     "function %s is not loaded!", pSettings->r_string(section, "script_binding"));
    return;
}
lua_function(game_object->lua_game_object());
```

### 5.4 Точка входа мода

`ogsr_engine/xrGame/alife_simulator.cpp:65-67`:

```cpp
LPCSTR start_game_callback = pSettings->r_string(alife_section, "start_game_callback");
luabind::functor<void> functor;
R_ASSERT2(ai().script_engine().functor(start_game_callback, functor), "failed to get start game callback");
functor();
```

Значение читается из конфига, функция вызывается при старте ALife.

> ⚠️ **SoC-механизм `m_netpk.script` на CoP отсутствует.** На CoP вместо
> переопределения раскладки сетевого пакета из Lua есть
> `gulag_general.script` (36 КБ). Это принципиальное отличие: на SoC поддержка
> более чем 64 костей достигалась описанием формата пакета на Lua, на CoP
> сделано иначе. Перед портированием мод-логики это надо учитывать.

## 6. `xrServerEntities` — замена `COMMON_AI`

**122 файла: 46 `.cpp` + 76 `.h`. Собственного `.vcxproj` нет.**

Компилируется в `xrGame.vcxproj:1592-1634` (**43 из 46** `.cpp`).
Не компилируются нигде:
- `PHNetState.cpp` — компилируется в `xrPhysics.vcxproj:165`
- **`PHSynchronize.cpp` / `lua_studio.cpp` — мёртвый код**

Include-путь добавлен в `xrGame.vcxproj:82`, `:141`.

Состав (каталог плоский, без подпапок):

| Группа | Файлы |
|---|---|
| Скриптовый движок | `script_engine.{h,cpp}`, `script_engine_export.{h,cpp}`, `script_engine_script.cpp`, `script_export_space.h`, `script_export_macroses.h` |
| Серверные сущности | `xrServer_Objects{,_Abstract,_ALife}{,.h}`, `xrServer_Object_Base.{h,cpp}`, `xrServer_Factory.cpp`, `xrServer_Space.h` |
| ALife | `alife_human_brain.*`, `alife_monster_brain.*`, `alife_movement_manager_holder.h`, `alife_space.{h,cpp}` |
| Фабрика | `object_factory*.{h,cpp}`, `object_item_script.{h,cpp}`, `object_item_abstract*`, `xrServer_Factory.cpp` |
| Монстры (CoP-специфика) | `xrServer_Objects_ALife_Monsters_script{,2,3,4}.cpp` — **4 файла** |
| Смарт-каверы | `xrServer_Objects_Alife_Smartcovers*.{h,cpp}` |

> ⚠️ **`GRAPH/`, `PATH/`, `ALGORITHMS/` исчезли.** На SoC они были в `COMMON_AI/`.
> На CoP их нет как отдельных каталогов нигде — графы уровня, A*, quadtree,
> patrol paths переехали в `xrGame`.

### Формат сохранений

`xrServerEntities/xrServer_Objects.h:166`:
```cpp
#define SPAWN_VERSION u16(128)
```
На SoC было **118** с полной историей 118 изменений в комментарии.
**На CoP версия 128** — сохранения от SoC-сборки несовместимы.

### Переопределение виртуальных методов сервера из Lua

`xrServerEntities/xrServer_script_macroses.h` — всё та же схема:

- **C++-сторона**: `DEFINE_LUA_WRAPPER_METHOD_V0/V1`,
  `DEFINE_LUA_WRAPPER_CONST_METHOD_0/1`, `DEFINE_LUA_WRAPPER_METHOD_R2P1_V1/V2`
  (`script_export_macroses.h:25-55`), сгруппированы по `INHERIT_ABSTRACT`,
  `INHERIT_ALIFE`, `INHERIT_DYNAMIC_ALIFE`, `INHERIT_ZONE`, `INHERIT_CREATURE`,
  `INHERIT_MONSTER`, `INHERIT_ITEM`.
- **Lua-сторона**: `luabind_class_pure0..4`, `abstract0..4`, `alife0..4`,
  `dynamic_alife0..4`, `zone0..4`, `creature0..4`, `monster0..4`, `item0..4`
  (`:238-408`).

Базовый шаблон `CWrapperBase<T> : T, luabind::wrap_base` — при попытке
вызвать чистый виртуаль логирует *«Attempt to call pure virtual method …»*.

## 7. Игровые объекты

### `CGameObject`

`xrGame/GameObject.h:50`:
```cpp
class CGameObject : public CObject, public CUsableScriptObject, public CScriptBinder
```
(на SoC было `:64`).

**Быстрые касты вместо RTTI** — ~21 виртуальный `cast_*()`:
`cast_attachment_owner`, `cast_inventory_owner`, `cast_entity_alive`,
`cast_actor`, `cast_custom_zone`, `cast_physics_shell_holder`,
`cast_weapon`, `cast_stalker`, …
Комментарий: *«functions used for avoiding most of the smart_cast»*.

Lua-коллбэки: ~30 мест вызова (`eHit`, `eDeath`, `eZoneEnter/eZoneExit`,
`eOnActorJump/Land`, `eAttachVehicle/eDetachVehicle/eUseVehicle`, …),
диспетчеризация в `GameObject.cpp`.

### `CScriptGameObject` — фасад

`xrGame/script_game_object.h`. Методы генерируются макросами `BIND_FUNCTION*`
(`script_game_object.cpp`). Поведение при несоответствии типа —
`script_bind_macroses.h:28-37`:

```cpp
#define CAST_OBJECT(Z, A, B) \
    B* l_tpEntity = smart_cast<B*>(Z); \
    if (!l_tpEntity) { ai().script_engine().script_log(eLuaMessageTypeError, \
                        "%s : cannot access class member %s!", #B, #A);
```

То есть возвращается значение по умолчанию + ошибка в лог, **не краш**.

### CoP-специфика

- `Actor_Network.cpp` — **новый файл, +1349 строк**. Основное изменение
  в сетевом коде актора; перед портированием мод-логики надо изучить
- `xrPhysics` (см. [03-build-and-dependencies.md](03-build-and-dependencies.md) §3)
- `dCylinder` — новый цилиндрический коллайдер ODE

## 8. AI

### Структура

```
xrGame/ai/               487 файлов (на SoC было 456)
  stalker/                ai_stalker{,_cover,_debug,_events,_feel,_fire,_impl,...}
  monsters/               basemonster + 17 монстров
                          + states/, group_states/, control_animation*,
                            control_direction*, control_path_builder*, melee_checker
```

> ⚠️ По сравнению с SoC: удалён `ai/monsters/burer`-ряд и часть
> `control_*`, добавлены новые монстры CoP. Полный список — в `xrGame/ai/`.

`CAI_Space` — фасад:

| Что | Файл:строка |
|---|---|
| `CAI_Space::init()` | `xrGame/ai_space.cpp:40` (на SoC `:36`) |
| `CAI_Space::load()` | `xrGame/ai_space.cpp:82` (на SoC `:74`) |

`init()` строит в порядке: `CEF_Storage` → `CGraphEngine(1024)` →
`CCoverManager` → `CPatrolPathStorage` → `CScriptEngine`.

### Источники конфигурации AI

| Источник | Статус на CoP |
|---|---|
| Бинарные графы (`level.ai`, game graph, `level.gct`, `all.spawn`) | ✅ |
| `.ltx` через `pSettings->r_*` | ✅ |
| `.xml` для UI/диалогов | ✅ |
| **`.ini`-переопределение путей** (`[engine_custom_spawn] waypoints_file`) | ❌ **УДАЛЕНО**. В `Config/Resources_CoP_1.6.02/gamedata/Configs/external.ltx` такой секции нет, механизм пропал |

> Это существенное отличие от SoC: там был механизм замены patrol_path
> из `.ini` с логом *«Loaded %d items from custom_waypoints…»*. На CoP его нет.

### Память

`xrGame/memory_space.h` + `visual_memory_manager.cpp` + `sound_memory_manager.cpp`
+ `hit_memory_manager.cpp`; в Lua — `memory_visible_objects`,
`sound_objects`, `hit_objects`, `not_yet_visible_objects`,
`visibility_threshold()`, `enable_vision()`, `set_sound_threshold()`.

## 9. UI

### 9.1 C++-сторона

Ядро — `xrGame/ui_base.h:90` (`is_widescreen()`).

> ⚠️ **На CoP нет `UI_BASE_WIDTH` / `UI_BASE_HEIGHT` и `is_ultra_widescreen()`** —
> на SoC они были (`:3-4`, `:108-109`). Опорное разрешение и предикаты
> соотношения сторон переработаны.

~130 виджетов в `xrGame/ui/`: `UIWindow`, `UIDialogWnd`, `UIStatic`,
`UIButton`/`UI3tButton`/`UICheckButton`/`UIRadioButton`, `UIEditBox(Ex)`,
`UIComboBox`, `UIListWnd(Ex)`, `UITabControl`, `UIScrollView`/`UIScrollBox`/`UIScrollBar`,
`UIFrameWindow`/`UIFrameLineWnd`, `UIProgressBar`/`UISpinNum`/`UISpinText`/`UICustomSpin`,
`UILine(s)`/`UISubLine`, `UILabel`/`UIMultiTextStatic`/`UITextBanner`,
`UIMessageBox(Ex)`, `UIKeyBinding`/`UIEditKeyBind`, `UIMotionIcon`,
`UIInventoryWnd(+2/+3)`, `UIPdaWnd`/`UIPdaListItem`/`UIPdaContactsWnd`,
`UITalkWnd`/`UITalkDialogWnd`, `UITradeWnd`, `UICarPanel`/`UICarBodyWnd`,
`UIMap`/`UIMapWnd`, `UIEncyclopediaWnd`, `UIEventsWnd`/`UITaskItem`,
`UINewsWnd`, `UIActorInfo`/`UICharacterInfo`, `UIOutfitInfo`, `UIItemInfo`,
`UIOptionsManager`, `UIArtefactPanel`, `UIGameLog`, `UIMessagesWindow`,
`UILoadingScreen`.

**CoP-специфика:** `ui/UIActorMenuInventory.cpp` — **новый, 1227 строк**
(меню актора переработано); `ui/UICarBodyWnd.cpp` **удалён (−969 строк)**.

**Раскладка — в XML, не в коде:** `gamedata/Configs/ui/*.xml`
(на SoC было `gamedata/Config/ui/`!):
`ui_mm_opt.xml` (39 КБ), `ui_mm_opt_16.xml`, `ui_mm_loading_screen{,16}.xml`,
`ui_keybinding.xml`, `encyclopedia{,16,_item,_item_16}.xml`,
`textures_descr/ui_mm_loading_screen.xml`.

Тексты — `gamedata/Configs/text/{eng,rus,ukr}/ui_st_mm.xml`
(по 40/40/30 КБ). **Украинский язык — новое на CoP.**

### 9.2 `CUICustomWnd` — действительно отсутствует

Проверено: **0 совпадений** по всему `ogsr_engine/**`. Утверждение верно.

> ⚠️ **На CoP также удалён `CUICustomItem`** (0 совпадений) — на SoC он был
> (`UICustomItem.h`, помощник по атласу текстур с `Frect iVisRect`).
> Остаётся только `CUIDialogWndEx` (18 совпадений) и `CUIScriptWnd` (1).

### 9.3 `CUIScriptWnd` — то, что видит Lua

`xrGame/ui/UIScriptWnd_script.cpp:27` → `export_class instance("CUIScriptWnd")`.

Добавляет `AddCallback(control_id, event, functor[, object])`,
`ClearCallbacks()`, `Register(window[, name])`, шаблонный `GetControl<T>(name)`
и виртуальные `OnKeyboard`/`Update`/`Dispatch`, уходящие в Lua.

### 9.4 `CScriptXmlInit` — фабрика виджетов из XML

`xrGame/ScriptXmlInit.cpp:200` — `void CScriptXmlInit::script_register(lua_State*)`.
Экспортируется **22 метода**: `ParseFile` + 21 `Init*`.

**Изменился набор против SoC:**

| Новое на CoP | Убрано против SoC |
|---|---|
| `InitWindow` | `InitLabel` |
| `InitTextWnd` | `InitButton` |
| `InitSleepStatic` | `InitAutoStaticGroup` |
| `InitListBox` | `InitList` (преобразована) |

Полный список CoP: `InitWindow`, `InitFrame`, `InitFrameLine`, `InitEditBox`,
`InitStatic`, `InitTextWnd`, `InitAnimStatic`, `InitSleepStatic`, `Init3tButton`,
`InitCheck`, `InitSpinNum`, `InitSpinFlt`, `InitSpinText`, `InitComboBox`,
`InitTab`, `InitTrackBar`, `InitKeyBinding`, `InitMMShniaga`, `InitScrollView`,
`InitListBox`, `InitProgressBar`.

### 9.5 Lua-сторона: главное меню и все его диалоги

`xrGame/MainMenu.cpp:189`:
```cpp
DLL_Pure* dlg = NEW_INSTANCE(make_clsid("MAIN_MNU"));   // → Lua-класс main_menu
```

`gamedata/Scripts/ui_main_menu.script`:
```lua
-- :34
local mm = _G.main_menu.get_main_menu()
```

Прочие Lua-UI на CoP: `ui_mm_opt_main.script` (8 КБ),
`ui_mm_opt_gameplay.script` (2 КБ), `ui_mm_opt_video.script` (3 КБ),
**`ui_mm_opt_video_adv.script` (11 КБ)**, `ui_save_dialog.script` (6 КБ),
`ui_load_dialog.script` (9 КБ), `_g.script` (27 КБ),
`gulag_general.script` (36 КБ).

> ⚠️ На CoP **разделены** `ui_save_dialog.script` и `ui_load_dialog.script`
> (на SoC был один `ui_mm_save_load_dialog.script`).
> Нет `task_manager.script`, `level_weathers.script`, `personal_test.lua`,
> `ANSI_scripts/`.

### 9.6 ★ Lua-UI умеет DLSS — это важно

`gamedata/Configs/Scripts/ui_mm_opt_video_adv.script`:

```
:27  { "dlss_quality",  "list", 0, hint_colors.green, "video_settings_desc_70" },
:28  { "dlss_preset",   "list", 0, hint_colors.green, "video_settings_desc_72" },
:29  { "fsr3_quality",  "list", 0, hint_colors.green, "video_settings_desc_71" },
```

```
:92   self.upscaler_rows = {}
:110  --[=[if opt == "dlss_preset" then --SIMP: убран, я так понимаю по умолчанию
:112  local ver = get_console():get_string("r_aa_dlss_version") or ""
:130  elseif opt == "dlss_quality" then
:133  elseif opt == "dlss_preset" then
:174  local mode = (after_click and self.aa_list:GetCurrentValueName()
                   or get_console():get_string("r_aa_mode")) or ""
:187  local ver = get_console():get_string("r_aa_dlss_version") or ""
```

То есть интерфейс опций **уже exposes** `dlss_quality`, `dlss_preset` и
`fsr3_quality` и читает `r_aa_dlss_version` / `r_aa_mode` прямо из консоли.
Любые изменения механизма переключения апскейлеров надо синхронизировать
и с этим скриптом, иначе поле в меню разъедется с реальным состоянием.

## 10. Каталог `Game/`

```
Game/
├── Lua_Extensions/           2 файла
├── Resources_CoP_1.6.02/     641 файл, 73,6 МБ
└── ShadersSln/               4 файла
```

### `Game/Lua_Extensions/`

| Файл | Что |
|---|---|
| `_G_add.lua` | расширения `_G`/`string`/`table` + патч `CUIScriptWnd:Connect` |
| `_luaxml.script` | чисто-Lua XML DOM (зависимость от LuaFileSystem убрана) |

Оба надо **скопировать вручную** в `<gamedata>/scripts/` — сборка их не потребляет.
И `_G_add.lua` лежит как `.lua`, а значит **не** попадает на путь автозагрузки.

### `Game/Resources_CoP_1.6.02/gamedata/`

| Папка | Файлов | Размер |
|---|---|---|
| `anims/` | 6 | 0,04 МБ |
| `Configs/` | 21 | 0,14 МБ |
| `levels/` | 3 | 0,04 МБ |
| `scripts/` | 9 | 0,08 МБ |
| `shaders/` | 552 | 0,65 МБ |
| `textures/` | 50 | 72,5 МБ |

> ⚠️ **На CoP нет папки `sounds/`** (на SoC был `rain.ogg`) и **нет `shaders.xr`.
> `anim/` стал вложенным**: вместо плоского списка —
> `anims/camera_effects/actor_move/` и `anims/movement/`.

**`Configs/` (полностью):**
```
default_controls.ltx          external.ltx (5 КБ)
rspec_{default,extreme,high,low,minimum}.ltx
gameplay/info_jupiter.xml                    ← новое
text/{eng,rus,ukr}/ui_st_mm.xml              ← ukr новое
ui/{encyclopedia,encyclopedia_16,
     encyclopedia_item,encyclopedia_item_16,  ← новое
     ui_keybinding,ui_mm_loading_screen{,16},
     ui_mm_opt{,16}}.xml
textures_descr/ui_mm_loading_screen.xml
```

> `external.ltx` на CoP всего **5 КБ** против 26 КБ на SoC —
> OGSR-специфичных настроек меньше, секция `engine_custom_spawn` отсутствует.

**`Scripts/` (полностью):** `gulag_general.script`, `ui_load_dialog.script`,
`ui_main_menu.script`, `ui_mm_opt_gameplay.script`, `ui_mm_opt_main.script`,
`ui_mm_opt_video.script`, `ui_mm_opt_video_adv.script`,
`ui_save_dialog.script`, `_g.script`.

**`levels/` (полностью):** только `l03_agroprom/level.fog_vol` и
два `.level.puddles` — **это оверлей, а не игровые уровни.**

### `Game/ShadersSln/`

Отдельное решение, только для индексации и clang-format HLSL.
**Жёстко закреплено на v143 + `WindowsTargetPlatformVersion 10.0`**,
не импортирует `OgsrBuildProps.props`, в CI не собирается.
На машине только с VS2026 — не откроется.

> `Game/Shaders` **не существует**. Шейдеры лежат в
> `Game/Resources_CoP_1.6.02/gamedata/shaders/`.

## 11. Выгрузка Lua

`CScriptEngine::unload()` (`script_engine.cpp:43-57`): уничтожает
`destroy_lua_wpn_params()`, `MainMenu()->DestroyInternal(true)`,
`xr_delete(g_object_factory)`, вызывает `close()` (`lua_close`), чистит `no_files`.

`CAI_Space::unload(false)` (`ai_space.cpp:118-130`) делает
`script_engine().unload(); script_engine().init();` — **полный teardown
и rebuild Lua VM при каждой выгрузке уровня без перечитывания**.