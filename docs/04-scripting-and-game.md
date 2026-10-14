# 04 — Скрипты, игровые объекты, AI, UI

## 1. Подключение Lua

**Единственное место во всём движке**, где подключаются `<lua.hpp>` и
`<luabind/*.hpp>` — `ogsr_engine/xr_3da/ai_script_space.h:1-23`.
Комментарий автора: *«KRodin: В этом файле подключается Luabind.
Только здесь и больше нигде.»*

Все остальные модули включают этот заголовок
(`xrGame/stdafx.h:11`: *«пусть луа для рендера и скриптов инитится в одном месте»*).

## 2. Автоматическая регистрация классов

### Механизм типового списка

`COMMON_AI/script_export_space.h:15-23`:

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

Каждый экспортируемый заголовок заканчивается тройкой, например
`xrGame/script_game_object.h:833-837`:

```cpp
    DECLARE_SCRIPT_REGISTER_FUNCTION
};
add_to_type_list(CScriptGameObject)
#undef script_type_list
#define script_type_list save_type_list(CScriptGameObject)
```

Поскольку `save_type_list(X)` раскрывается в `TypeList_X`, а это имя
`#define`-ится как `script_type_list` для следующего includer'а, список
накапливается по цепочке включений.

**Статистика:** 194 заголовка с `add_to_type_list`,
153 с `DECLARE_SCRIPT_REGISTER_FUNCTION`, 231 определение `script_register(lua_State*`.

### Драйвер

`COMMON_AI/script_engine_export.cpp:11-35`:

```cpp
#define SCRIPT_REGISTRATOR
#include "script_export_space.h"
#include "script_engine_export.h"

void export_classes(lua_State* L) { Register<script_type_list>::_Register(L); }
```

`Register<>::_Register` применяет `ts_reverse_t`, т.е. регистрация идёт
**в порядке объявления**. Мастер-список всех заголовков —
`script_engine_export.h` (165 строк); ветка `#ifdef XRGAME_EXPORTS` (`:24`/`:26`)
добавляет игровые классы, UI и биндеры.

## 3. Инициализация

`CScriptEngine::init()` — `COMMON_AI/script_engine.cpp:120-156`.

```
1. luaL_newstate()                              → LSVM
2. reinit(LSVM)                                 → ScriptCrashHandler на "Debug"
3. luabind::open(LSVM)                          → class_registry + глобальная фабрика class()
4. tracy::LuaRegister(LSVM)
5. luabind::set_error_callback / set_cast_failed_callback
   + set_pcall_callback(lua_pcall_failed)       // "НЕ ЗАКОММЕНТИРОВАТЬ НИ В КОЕМ СЛУЧАЕ!!!"
   + lua_atpanic(LSVM, lua_panic)
6. export_classes(LSVM)                         // все движковые функции в скрипты
7. luaL_openlibs(LSVM)
8. setup_auto_load()                            // метаметод __index на самом _G
9. process_file_if_exists("_G", false)          // $game_scripts$/_g.script
10. register_script_classes()                   // class_registrator.script
11. object_factory().register_script()          // clsid._clsid + clsid_table
12. LUA_GCSETSTEPMUL 500, LUA_GCSETPAUSE 100
```

### Ленивая загрузка

`setup_auto_load()` (`:105-118`) ставит на `_G` метаметод
`XRAY_AutoLoadMetaTable` с `__index = auto_load`:

```cpp
// script_engine.cpp:93-103
ai().script_engine().process_file_if_exists(lua_tostring(L, 2), false);
lua_rawget(L, 1);
```

**Любое обращение к несуществующему глобалу инициирует поиск на диске.**

`namespace_loaded()` (`:633-689`) обходит `_G` по точкам, проверяя, что
каждый сегмент — таблица (`R_ASSERT3` при коллизии с не-таблицей, `:671`).
Отрицательные результаты мемоизируются в `no_files` (`script_engine.h:32`).

Триггеры автозагрузки:
- `function_object("a.b.c", …)` из C++ (`:289-313`) — точка разбора в
  **последней** точке (`parse_script_namespace`, `:158-173`);
  именно так `ui_wpn_params.script` подтягивается из
  `xrGame/ui/UIWpnParams.cpp:24`;
- `prefetch("name")` из Lua;
- обычный доступ к глобалу;
- резолв имени класса в `object_factory.register_script_class`.

### Файл → namespace

`CollectScriptFiles` (`:185-219`) обходит `$game_scripts$` рекурсивно;
для каждого пути, содержащего `.script`, берётся **basename до первой точки,
в нижнем регистре**. `_g.script` → `_g`, `ANSI_scripts/ansi_functions.script` → `ansi_functions`.
Коллизии молча отбрасываются (`:215`).

> ⚠️ **`*.lua` не подхватывается** — `.lua` закомментирован на `:209`.
> Файл `gamedata/scripts/personal_test.lua` существует, но грузится вручную
> через `dofile()` в `ui_main_menu.script:171`.

## 4. Обёртка скрипта

Всё, кроме `_G`, оборачивается в `FILE_HEADER` (`script_engine.cpp:19-26`):

```lua
local function script_name() return '{0}' end;
local this;
module('{0}', package.seeall, function(m) this = m end);
{1}
```

Именно `this` ищет пропатченный `Luabind/src/create_class.cpp:129-166`,
чтобы зарегистрировать класс в модуле, а не в `_G`.

## 5. Четыре способа расширить игру из Lua

### 5.1 Пассивный автозагрузчик

Всё, что не требует регистрации. Базовый уровень.

### 5.2 `class_registrator.register` — новый игровой объект

`script_engine.cpp:282-287`:

```cpp
luabind::functor<void> result;
ASSERT_FMT(functor("class_registrator.register", result), "[%s] Cannot load class_registrator!", __FUNCTION__);
result(const_cast<CObjectFactory*>(&object_factory()));
```

Обращение к глобалу `class_registrator.register` подтягивает
`class_registrator.script`, который вызывает
`object_factory():register(client_class, server_class, clsid, script_clsid)`
(`object_factory_script.cpp:76-81`).

`CObjectFactory::register_script_class` (`object_factory_script.cpp:15-42`)
резолвит имя класса в luabind-`LUA_TUSERDATA` и оборачивает в `CObjectItemScript`.
`CObjectFactory::register_script()` (`:51-73`) публикует CLSID и как
luabind-enum `clsid._clsid`, и как обычную таблицу `clsid_table` (`:72`) —
именно этим пользуется `m_netpk.script` (`clsid.inventory_box`, `clsid.script_heli`).

Далее объект создаётся из C++ по CLSID:
- клиент: `MainMenu.cpp:129` → `NEW_INSTANCE(TEXT2CLSID("MAIN_MNU"))`
  → `xrFactory_Create` → `object_factory().client_object(clsid)`
  → `CObjectItemScript::client_object()` (`object_item_script.cpp:15-26`)
  → `object_cast<CLIENT_SCRIPT_BASE_CLASS*>(m_client_creator(), adopt<result>())`;
- сервер: `CObjectItemScript::server_object(section)` (`:28-64`).

Референс в `gamedata/config/m_netpk.ltx:78`: `main_menu = unknown , MAIN_MNU , 7 ,`.

### 5.3 `script_binding` в `.ltx` — поведение существующего класса

`xrGame/script_binder.cpp:46-74`:

```cpp
if (!pSettings->line_exist(section, "script_binding")) return;
auto script_func_name = pSettings->r_string(section, "script_binding");
luabind::functor<void> lua_function;
if (!ai().script_engine().functor(script_func_name, lua_function)) {
    Msg("!![CScriptBinder::reload] function [%s] not loaded!", …); return;
}
lua_function(game_object->lua_game_object());   // C++ → Lua, возвращает CScriptBinderObject
```

Строка `script_binding = my_mod.my_binder_function` в секции объекта
приводит к вызову Lua-функции, которая возвращает `CScriptBinderObject`.
Биндинг — `script_binder_object_script.cpp:19-33`
(`reinit`/`reload`/`net_spawn`/`net_destroy`/`update`/`save`/`load`/…).

### 5.4 Обезвреживание серверной модели прямо из Lua

Яркий пример — `gamedata/scripts/m_netpk.script` (135 КБ, 3200+ строк).
Подключается через `m_netpk.attach()` из `_G.start_game_callback()`
(комментарий `:8-13`) и **заменяет нативные `net_cse_abstract`
read/write на реализации на Lua** (`:1617`, `:1654`, `:1709`),
опираясь на таблицу полей из `gamedata/config/m_netpk.ltx`:

```lua
{ name = 'spawn_id', type = 'u16', default = 65535 }     -- m_netpk.script:1549
```

Так OGSR поддерживает >64 костей: раскладка сетевого пакета описывается на Lua,
а не на C++.

### Точка входа мода

`xrGame/alife_simulator.cpp:47-50`:

```cpp
LPCSTR start_game_callback = pSettings->r_string(alife_section, "start_game_callback");  // "alife"
luabind::functor<void> functor;
R_ASSERT2(ai().script_engine().functor(start_game_callback, functor), "failed to get start game callback");
functor();
```

Значение читается из конфига, функция вызывается при старте ALife.

## 6. Игровые объекты

### `CGameObject`

`xrGame/GameObject.h:64`:

```cpp
class CGameObject : public CObject, public CUsableScriptObject, public CScriptBinder
```

**Быстрые касты вместо RTTI** (`:81-102`) — 21 виртуальный `cast_*()`:
`cast_attachment_owner`, `cast_inventory_owner`, `cast_entity_alive`,
`cast_actor`, `cast_custom_zone`, `cast_physics_shell_holder`,
`cast_weapon`, `cast_stalker`, …
Комментарий: *«functions used for avoiding most of the smart_cast»*.

Прочее: сетевой жизненный цикл (`net_Spawn`/`net_Destroy`/`net_Relcase`),
планировщик (`shedule_Update`), IK-якоря (`CAI_ObjectLocation* m_ai_location`,
`ALife::_STORY_ID m_story_id`), предикаты-эффекторы (`ef_weapon_type()`,
`ef_creature_type()`, …, `:252-257`).

Lua-коллбэки (`xr_map<GameObject::ECallbackType, …> m_callbacks`, `:262`) —
около 30 мест вызова: `eHit` (`Actor.cpp:630`), `eDeath` (`Car.cpp:1793`),
`eZoneEnter/eZoneExit` (`CustomZone.cpp:1521,1527`), `eOnActorJump/Land`,
`eAttachVehicle/eDetachVehicle/eUseVehicle`, …

Feel-touch: `FeelTouchAddon{ Feel::Touch; float radius; … }` (`:56-62`),
`addFeelTouch(...)` (`:278-279`), диспетчеризация `GameObject.cpp:1019-1049`.

### `CScriptGameObject` — фасад на ~700 методов

`xrGame/script_game_object.h:143-834`. Создаётся лениво и кэшируется
(`GameObject.cpp:732-744`):

```cpp
CScriptGameObject* CGameObject::lua_game_object() const
{
    … if (!m_lua_game_object) m_lua_game_object = xr_new<CScriptGameObject>(const_cast<CGameObject*>(this));
    return m_lua_game_object;
}
```

Уничтожается в `net_Destroy` (`:147`). Методы генерируются макросами
`BIND_FUNCTION*` (`script_game_object.cpp:51-92`).

`script_bind_macroses.h:28-37` — поведение при несоответствии типа:

```cpp
#define CAST_OBJECT(Z, A, B) \
    B* l_tpEntity = smart_cast<B*>(Z); \
    if (!l_tpEntity) { ai().script_engine().script_log(eLuaMessageTypeError,
                        "%s : cannot access class member %s!", #B, #A);
```

То есть возвращается значение по умолчанию + ошибка в лог, **не краш**.
Плюс ~42 предиката `IsXxx` (`:739-780`) для полиморфных проверок без `cast_*`.

### Серверная сторона

`SERVER_ENTITY_DECLARE_BEGIN/END` (`COMMON_AI/xrServer_Space.h:13-50`)
создают классы `CSE_*` и методы `UPDATE_Read/Write`, `STATE_Read/Write`.

Формат сохранений — `SPAWN_VERSION u16(118)` (`xrServer_Objects.h:154`),
с полной историей 118 изменений в комментарии выше. Сохранение другой версии
отвергается (`alife_spawn_registry.cpp:150`: *"Spawn version mismatch - REBUILD SPAWN!"*).

Сериализация макро-шаблонная: `object_loader.h`, `object_saver.h`,
`object_cloner.h`, `object_destroyer.h`, `object_type_traits.h` —
рекурсивно обходят члены, авто-разворачивая всё, что реализует
`IPureLoadableObject`/`IPureSavableObject`
(`object_loader.h:20` содержит `static_assert(!std::is_polymorphic_v<T>, ...)`).

### Переопределение виртуальных методов сервера из Lua

`COMMON_AI/xrServer_script_macroses.h`:

- **C++-сторона**: `DEFINE_LUA_WRAPPER_METHOD_V0/V1`,
  `DEFINE_LUA_WRAPPER_CONST_METHOD_0/1`, `DEFINE_LUA_WRAPPER_METHOD_R2P1_V1/V2`
  (`script_export_macroses.h:25-55`) генерируют виртуаль, форвардящий в Lua
  через `luabind::call_member<T>(this, "name", …)`, плюс `name_static(inherited*, …)`
  с откатом на C++-базу. Сгруппированы по уровням наследования:
  `INHERIT_ABSTRACT` (`:21`), `INHERIT_ALIFE` (`:26`),
  `INHERIT_DYNAMIC_ALIFE` (`:35`, добавляет `on_spawn`/`on_register`/`on_unregister`),
  `INHERIT_ZONE` (`:47`), `INHERIT_CREATURE` (`:66`), `INHERIT_MONSTER` (`:80`),
  `INHERIT_ITEM` (`:84`).
- **Lua-сторона**: `luabind_class_pure0..4`, `abstract0..4`, `alife0..4`,
  `dynamic_alife0..4`, `zone0..4`, `creature0..4`, `monster0..4`, `item0..4`
  (`:238-408`) — каждый раскрывается в
  `luabind::class_<A, CWrapperX<A>, bases<…>>` + список
  `.def(name, &A::name, &CWrapperX<A>::name_static)`.

Пример — `COMMON_AI/xrServer_Objects_script.cpp:132`:

```cpp
void CSE_Temporary::script_register(lua_State* L) { module(L)[luabind_class_abstract1(CSE_Temporary, "cse_temporary", CSE_Abstract)]; }
```

Базовый шаблон `CWrapperBase<T> : T, luabind::wrap_base`
(`xrServer_Objects_script.cpp:39-72`) показывает путь отказа:
`STATE_Read` у базы логирует *«Attempt to call pure virtual method STATE_Read in CSE_Abstract»*.

## 7. AI

### Структура

```
COMMON_AI/
  ALGORITHMS/           a_star, dijkstra, quadtree, binary_heap, bucket_list, vertex_path
  GRAPH/                graph_engine, level_graph, game_graph, game_level_cross_table
  PATH/                 path_manager_*, patrol_path, patrol_point, cover_point
  xrServer_Objects_ALife*.{h,cpp}   CSE_ALife* (~40 КБ Monsters, ~40 КБ ALife, ~27 КБ Items)
  alife_human_brain / alife_monster_brain
  script_engine*, script_export_*, object_factory*

xrGame/ai/              клиентский AI (456 файлов)
  stalker/              ai_stalker{,_cover,_debug,_events,_feel,_fire,_impl,...}
  trader/ crow/ phantom/
  monsters/             basemonster + 17 монстров
                        + states/, group_states/, control_animation*, control_direction*,
                          control_path_builder*, state_manager, melee_checker
xrGame/ai_space.{h,cpp}  CAI_Space — фасад: графы, cover, ef_storage, patrol, CScriptEngine, ALife
```

`CAI_Space::init()` (`ai_space.cpp:36-53`) строит в порядке:
`CEF_Storage` → `CGraphEngine(1024)` → `CCoverManager` →
`CPatrolPathStorage` → `CScriptEngine`.

`CAI_Space::load(level_name)` (`:74-116`) грузит `$level$/level.ai`
(`LEVEL_GRAPH_NAME`, `COMMON_AI/GRAPH/level_graph.h:20`), валидирует тройку GUID
level↔game↔crosstable (`:97-98`, `:102`), затем
`m_cover_manager->compute_static_cover()`.

### Четыре источника конфигурации AI

**(a) Бинарные графы** — `level.ai`, игровой граф, `level.gct` — собираются
редактором уровней, лежат в `all.spawn` вместе со спавнами и патрульными путями.

**(b) `.ltx`** — читается напрямую через `pSettings->r_*`. Пример:
`COMMON_AI/alife_monster_brain.cpp:51`

```cpp
sscanf(pSettings->r_string(this->object().name(), "smart_terrain_choose_interval"), "%d:%d:%d", …);
```

`xrGame/ai/monsters/basemonster/base_monster_script.cpp:36-37`:
`[monsters_common] script_move_min_offset_from_leader` и т.п.

**(c) `.ini`-переопределение путей** — `xrGame/alife_spawn_registry.cpp:154-168`:

```cpp
if (pSettings->line_exist("engine_custom_spawn", "waypoints_file")) {
    FS.update_path(fname, "$game_config$", pSettings->r_string("engine_custom_spawn", "waypoints_file"));
    if (FS.exist(fname)) {
        CInifile way_inifile = CInifile(fname);
        ai().patrol_path_storage_ini(way_inifile);
    }
}
```

`CPatrolPathStorage::append_from_ini` (`COMMON_AI/PATH/patrol_path_storage.cpp:45-68`)
разбирает секции в `CPatrolPath`, **заменяя** одноимённые пути из `all.spawn`.
Лог: `«Loaded %d items from custom_waypoints, %d from all.spawn was replaced!»`.
Документировано в `gamedata/config/external.ltx:318-323`.

**(d) `.xml`** — для UI/текстов диалогов, не для AI.

### Планировщик сталкеров

`xrGame/stalker_*` (~90 файлов) — классический планировщик в духе UTGO:

```
CActionPlanner<CScriptGameObject, …>      script_game_object.h:124-126
CActionBase / CSActionPlannerActionScript
CPropertyEvaluator
CScriptWorldState / CScriptWorldProperty
```

`xrGame/ai/stalker/ai_stalker_script.cpp:27,65` экспортирует в Lua
`StalkerDecisionSpace::eWorldPropertySmartTerrainTask` и
`eWorldOperatorSmartTerrainTask` — Lua-планировщик даёт высокоуровневые решения,
C++ поставляет действия.

### Память

`xrGame/memory_space.h` + `visual_memory_manager.cpp` + `sound_memory_manager.cpp` +
`hit_memory_manager.cpp`. В Lua:
`memory_visible_objects`, `sound_objects`, `hit_objects`,
`not_yet_visible_objects`, `visibility_threshold()`, `enable_vision()`,
`set_sound_threshold()` (`script_game_object.h:542-550`).

## 8. UI

### 8.1 C++-сторона

Ядро — `xrGame/ui_base.h:59-110`: `ui_core` владеет `C2DFrustum`, `CFontManager`,
`CUICursor`, масштабированием (`m_pp_scale_`, `m_scale_`), стеком ножниц.
Опорное разрешение — **1024×768** (`UI_BASE_WIDTH/HEIGHT`, `:3-4`),
предикаты `is_widescreen()` / `is_ultra_widescreen()` (`:108-109`).

**~130 виджетов** в `xrGame/ui/`: `UIWindow`, `UIDialogWnd`, `UIStatic`,
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
`UILoadingScreen`, `xrUIXmlParser`/`UIXmlInit`.

**Раскладка — в XML, не в коде:** `gamedata/config/ui/*.xml`
(`ui_mm_main.xml`, `ui_mm_opt.xml` — **41 КБ**, `ui_mm_save_dlg.xml`,
`ui_mm_load_dlg.xml`, `ui_mm_loading_screen.xml`, `ui_keybinding.xml`,
`pda_spot.xml`). Тексты — в `gamedata/config/text/{eng,rus}/ui_st_mm.xml` (46 КБ).

Игровая оболочка: `UIGameCustom` (`UIGameCustom.h:31-100`) — корень
отрисовываемого и планируемого UI с `AddCustomMessage`/`AddCustomStatic`
(`xr_vector<SDrawStaticStruct>` с приоритетной сортировкой, `:14-29`, `:59-60`),
`AddDialogToRender` и полным набором `IR_On*`. Наследник для синглплеера —
`UIGameSP` (`UIGameSP.h:20-52`).

### 8.2 `CUICustomWnd` не существует

Поиск по всему репозиторию — ноль совпадений. Что есть:

- **`CUICustomItem`** (`xrGame/UICustomItem.h:13-73`) — помощник по
  атласу текстур: `Frect iVisRect` + `Frect iOriginalRect` + пивот заголовка
  + `EUIItemAlign`, `Render(pos, color[, …])`.
  Наследники: `CUIStaticItem`, `CUIFrameLine`, `CUIFrameRect` (закомментирован).
- **`CUIDialogWndEx`**, экспортируемое в Lua как **`"CUIScriptWnd"`**
  (`xrGame/ui/UIScriptWnd.h:8-45`, регистрация `UIScriptWnd_script.cpp:27`).
  Добавляет `AddCallback(control_id, event, functor[, object])`,
  `ClearCallbacks()`, `Register(window[, name])`,
  шаблонный `GetControl<T>(name)`, и виртуальные `OnKeyboard`/`Update`/`Dispatch`,
  уходящие в Lua.

### 8.3 Lua-сторона

**Всё главное меню и все его диалоги — чистый Lua.**

`xrGame/MainMenu.cpp:129`:

```cpp
DLL_Pure* dlg = NEW_INSTANCE(TEXT2CLSID("MAIN_MNU"));   // → CObjectItemScript → класс Lua "main_menu"
```

`gamedata/scripts/ui_main_menu.script:7`:

```lua
class "main_menu" (CUIScriptWnd)
function main_menu:__init() super()
    self.mbox_mode = 0
    self:InitControls(); self:InitCallBacks()
end
```

`:19-34`:

```lua
self:Init(0, 0, 1024, 768)
local xml = CScriptXmlInit(); xml:ParseFile("ui_mm_main.xml")
xml:InitStatic("back_movie", self); xml:InitStatic("background", self); xml:InitStatic("fire_movie", self)
self.shniaga = xml:InitMMShniaga("shniaga_wnd", self)
self.message_box = CUIMessageBoxEx(); self:Register(self.message_box, "msg_box")
```

`CScriptXmlInit` (`xrGame/ScriptXmlInit.cpp:265-294`) экспортирует в Lua
**23 фабричных метода**: `InitFrame`, `InitFrameLine`, `InitLabel`,
`InitEditBox(Ex)`, `InitStatic`, `InitAnimStatic`, `InitCheck`,
`InitSpinNum/Flt/Text`, `InitComboBox`, `InitButton`, `Init3tButton`,
`InitList`, `InitTab`, `InitTrackBar`, `InitKeyBinding`, `InitMMShniaga`,
`InitScrollView`, `InitAutoStaticGroup`, `InitProgressBar`.

> **Ключевой паттерн форка:** C++ владеет набором виджетов, XML — раскладкой,
> Lua — всей логикой главного меню, опций и диалогов сохранения/загрузки.
> HUD/PDA/инвентарь в игре остаются чистым C++.

`Game/Lua_Extensions/_G_add.lua:126-129` добавляет varargs-обёртку `Connect`:

```lua
function CUIScriptWnd:Connect(name, event, func, ...)
    local args = {...}
    return self:AddCallback( name, event, function() func(unpack(args)) end )
end
```

Активно используется в `ui_main_menu.script:42-45`.

Прочие Lua-UI: `ui_mm_opt_main.script`, `ui_mm_opt_controls.script`,
`ui_mm_opt_gameplay.script`, `ui_mm_opt_sound.script`, `ui_mm_opt_video.script`,
`ui_mm_opt_video_adv.script`, `ui_mm_save_load_dialog.script`.

## 9. Каталог `Game/`

```
Game/
├── Lua_Extensions/         рукописная Lua-библиотека (шейпится в gamedata/scripts)
├── Resources_SoC_1.0006/   референсная копия gamedata SoC 1.0006
└── ShadersSln/             IDE-проект для HLSL (в Engine.sln НЕ входит)
```

### `Game/Lua_Extensions/`

| Файл | Размер | Что |
|---|---|---|
| `_G_add.lua` | 8 094 Б | расширения `_G`/`string`/`table` + патч `CUIScriptWnd:Connect` |
| `_luaxml.script` | 17 318 Б | чисто-Lua XML DOM |

Заголовок `_G_add.lua:1-11` содержит инструкции по внедрению:

```lua
--Добавить в _G.script. В самый верх!
log1("--LUA version: ["..(jit.version or _VERSION).."]")
FFI = require('ffi') --Делать только здесь и только один раз!
math.randomseed(os.time())
--_luaxml.script должен быть в папке scripts.
prefetch("_luaxml")
```

Содержимое: `string.trim_w/trim_l/trim_r/trim/explode/clear/width` (`:13-107`),
`table.size/random/keys/values/copy` (`:22-46`, `:109-123`),
`tostring_all` + `log3` (`:131-168`; `log3` принимает **любой** тип как `%s` —
на этом держится `m_netpk.script:10`).

`_luaxml.script:9`: *«KRodin: убрал зависимость от библиотеки LuaFileSystem,
исправил ошибку в xml.find()»* — нативная зависимость заменена на чисто-Lua
реализацию (`:12-13`).

> ⚠️ Оба файла надо **скопировать вручную** в `<gamedata>/scripts/` — сборка их
> нигде не потребляет. И `_G_add.lua` лежит как `.lua`, а значит **не** попадает
> на путь автозагрузки (см. §3).

### `Game/Resources_SoC_1.0006/`

Урезанная референсная копия заменяемых ресурсов SoC 1.0006 — те самые
OGSR-дельты, с которыми сверяется мод-автор.

| Что | Содержимое |
|---|---|
| `shaders.xr` | 236 310 Б — скомпилированные шейдеры (продукт шейдерного Lua-VM) |
| `shaders/r3/` | ~400 `.ps/.vs/.gs/.hs/.ds/.s/.h` — `ogsr_bloom*`, `ogsr_nightvision`, `ogsr_gasmask*`, `ogsr_ssr`, `pbr_brdf_ggx.h`, `common_functions.h`, `screenspace_*`, `fluid_*`, `deffer_*`, `water*`, `ACES*`, `ACES_LMTs/`, `dx11/tess.*`, `shared/`, `_bumped_grass/` |
| `shaders/common/` | `models_detector_cfg.h`, `smaa.h` (55 КБ) |

### `Game/ShadersSln/`

Отдельное решение `ShadersSln.sln` с одним проектом. Только для индексации
и clang-format HLSL. **Жёстко закреплён на v143 + WindowsTargetPlatform 10.0**,
не импортирует `OgsrBuildProps.props`, в CI не собирается.

> `Game/Shaders` **не существует**. Шейдеры лежат в
> `Game/Resources_SoC_1.0006/gamedata/shaders/`.

## 10. Выгрузка Lua

`CScriptEngine::unload()` (`script_engine.cpp:43-57`): уничтожает
`destroy_lua_wpn_params()`, `MainMenu()->DestroyInternal(true)`,
`xr_delete(g_object_factory)`, вызывает `close()` (`lua_close`), чистит `no_files`.

`CAI_Space::unload(false)` (`ai_space.cpp:118-130`):

```cpp
script_engine().unload();
script_engine().init();
```

**Полный teardown + rebuild Lua VM при каждой выгрузке уровня без перечитывания**
(т.е. при загрузке сохранения).