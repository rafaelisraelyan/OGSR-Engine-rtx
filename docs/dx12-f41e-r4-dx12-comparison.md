# F4.1-E — сравнение R4/DX11 и DX12 на Jupiter

Ветка `feature/f41-full-static-jupiter`, база F4.0-B-VSLICE (`81956aa1a`).
Коммиты этапа: `a2cfaec18`, `de1d2661f`, `5c26e8442`.

## 1. Задача

Выяснить, что оригинальный R4/DX11 загружает и отображает на Jupiter такого,
чего не поддерживает native DX12, и устранить пропуски. Не ограничиваться
известными `MT_TREE_PM` и `MT_PROGRESSIVE`. Не смешивать пропуски с обычным
frustum/portal/LOD culling.

## 2. Как сравнивалось

Оба рендерера индексируют статику уровня одинаково: индекс записи в контейнере
`fsL_VISUALS(3)` файла `level` = id в `CRender::Visuals` = индекс в пулах DX12.
Поэтому сравнение идёт прямым join по id, без эвристик и без карт
«указатель → id».

Инструментация (`OGSR_DX12_CMP=1`, обратима, на R4-рендеринг не влияет):

| Строка лога | Что даёт |
|---|---|
| `ph_cmp: r4inv` | инвентарь статики из `CRender::LoadVisuals`: id, тип, сфера, бокс, ptr |
| `ph_cmp: inv role=r4` | тот же список, прочитанный через `IRender_interface` (`getVisual(id)`) |
| `ph_cmp: inv role=dx12` | статус DX12 по каждому id: `drawable`, причина отказа, раскладка |
| `ph_cmp: hist` | гистограмма типов по обеим сторонам |
| `ph_cmp: r4draw` | что R4 **отправил** в кадре (`kind=static|tree`, ptr, ssa, lod) |
| `ph_cmp: dx12draw` | что DX12 отправил или отсекает (`state=drawn|culled`) |
| `ph_cmp: dyn bucket` | спавн-объекты: тип визуала, секция, количество, видимость |
| `ph_cmp: details` | наличие `$level$/level.details` |

`state=culled` обязателен: у DX12 отсечение — только сфера о фрустум, у R4 —
порталы, HOM, SSA-discard, «valuable»-фильтр. Без этого «не нарисовано» нельзя
отличить от «отсечено».

R4-половина кадра снята захватом окна (`capture-window.ps1`,
`PrintWindow` с `PW_RENDERFULLCONTENT`): штатного скриптуемого дампа кадра у R4
нет.

## 3. Результат сверки

```
ph_cmp: inv role=r4    count=20880 pools_visuals=20880 hierarchy_visuals=20880
ph_cmp: hist role=r4   type0=7903 type1=3954 type2=789 type6=3015 type7=3035 type11=2184
ph_cmp: hist role=dx12 type0=7903 type1=3954 type2=789 type6=3015 type7=3035 type11=2184
ph_cmp: inv summary    r4_count=20880 compared=20880 type_mismatch=0
                       r4_normal_unsupported=0
```

Контейнер визуалов читается один в один: совпадает количество, совпадает тип
каждой записи, ни одна `MT_NORMAL` запись не потеряна. То есть пропусков
«R4 загрузил, DX12 не загрузил» внутри контейнера нет — расхождение было
в том, что из загруженного реально отрисовывалось.

## 4. Найденные и устранённые пропуски

| Причина | Объектов | Диагноз |
|---|---|---|
| `MT_TREE_PM` | 2184 | окно берётся из таблицы SWI, а R4 читает её из **`level.geom`** (`LoadSWIs(geom)`), не из `level`. Чтение из `level` давало пустую таблицу, и тип отвергался как «неподдержанный» |
| дети `MT_LOD` | +1213 | R4 добавляет **всех** детей FLOD (`r__dsgraph_build.cpp:552-557`), DX12 брал первого. Замер семантики: `lod_spread=3006`, `lod_ladder=9` — это разные объекты, агрегированные в импостор, а не лестница детализации |
| диагностика | — | отказ «по типу» маскировал неизвестный OGF-подчанок; добавлена гистограмма `rejected_chunk` (тип, подчанок, количество) |

Итог по покрытию:

```
pools           vb=36 ib=11 visuals=20880 drawable=13911 rejected=6969
reachability    normal=7903 progressive=789 tree=5219 referenced=13911
                dropped_unreachable=0 lod_nodes=3015 lod_ladder=9 lod_spread=3006
rejected_chunks type=1 chunk=10 count=3954   (MT_HIERARHY)
                type=6 chunk=10 count=3015   (MT_LOD)
scene loaded    draws=13911 geometry_bytes=93817940 alpha_test=4412
textures        unique=365 decoded=365 missing=0 unsupported=0 bytes=112401208
InfoQueue       verdict=PASS warning=0 error=0 corruption=0 discarded=0
```

`13911 = 7903 MT_NORMAL + 789 MT_PROGRESSIVE + 3035 MT_TREE_ST + 2184 MT_TREE_PM`.
`6969 = 3954 MT_HIERARHY + 3015 MT_LOD` — это группы-контейнеры, у них нет
собственной геометрии для отрисовки. Было 10514 отрисовываемых и 1213
недостижимых, стало 13911 и 0.

## 5. Что осталось неподдержанным

### 5.1 Двери и прочие спавн-объекты

Проверено по коду: дверь в CoP — **не** статическая геометрия уровня.
Это `CPhysicObject` со скелетным визуалом и костью `door`
(`PhysicObject.cpp:25-97`), попадает в мир спавн-объектом, визуал создаётся
через `Render->model_Create` (`xr_object.cpp:38`), трансформ — из спавна
(`GameObject.cpp:262-263`), рисуется динамическим путём
(`r4_R_render.cpp:158-250`).

Замер того же прогона:

```
ph_cmp: dyn summary objects=2540 with_visual=1112 without_visual=1428 visible=648
ph_cmp: dyn bucket type=10 count=65 visible=65 section='breakable_object'
ph_cmp: dyn bucket type=10 count=51 visible=51 section='lights_hanging_lamp'
ph_cmp: dyn bucket type=10 count=49 visible=49 section='physic_object'
ph_cmp: dyn bucket type=10 count=46 visible=46 section='physic_destroyable_object'
```

648 видимых объектов, все с типом 10 (`MT_SKELETON_RIGID`). Секции с
отсутствующим визуалом (`smart_cover`, `space_restrictor`, `zone_*`) визуала
не имеют вообще — это логика, а не графика.

Причина неподдержки: статический конвейер принимает `MT_NORMAL`,
`MT_PROGRESSIVE`, `MT_TREE_*` и мировые координаты «запечённые» в OGF.
Скелетные визуалы требуют загрузки отдельного OGF, бонов и матрицы объекта.

### 5.2 Детали уровня

```
ph_cmp: details present=1 path='$level$/level.details'
```

Файл на Jupiter есть. R4 грузит его `CDetailManager` (инлайн-геометрия в самом
файле, инстансная отрисовка в главном проходе, `DetailManager.cpp:93-135`,
`dx10DetailManager_VS.cpp:77-171`). DX12 его не читает. Это отдельный
статический путь, не входящий в контейнер 20880.

### 5.3 Прочее (вне этапа)

Скайбокс и атмосфера, тени, PBR/RTX, HUD, эффекты. Culling только по сферам,
без порталов и HOM. Прозрачные blended-материалы рисуются как cutout/opaque.
`MT_TREE_PM` — без ветровой анимации и инстансных vertex-атрибутов.

## 6. Визуальное сравнение

`screenshots/05-compare-r4-vs-dx12-interior.png` — одна и та же камера
(акторская камера сохранения `b23_jupiter`), R4 слева, DX12 справа.

Совпадает: арочный проём, рама окна, синяя панель, плитка пола, столы,
перила, стойка, надпись на стене.

Отличается по известным причинам: у R4 закрытые двери в проёме (спавн-объект
`MT_SKELETON_RIGID`), три NPC за столом (динамика), HUD и миникарта,
освещение и тени вместо плоского unlit.

## 7. Честный вывод

F4.1 **не объявляется** полной статической картой по критерию «нет
необъяснённых пропусков»: внутри контейнера уровня пропусков не осталось
(`type_mismatch=0`, `dropped_unreachable=0`), но 648 видимых спавн-объектов
(включая двери) и `level.details` не отрисовываются. Это отдельные подсистемы
со своей геометрией вне `level.geom`, и обе измерены, а не предположены.

Следующий шаг по приоритету — путь для скелетных визуалов
(`MT_SKELETON_RIGID`: у жёстких пропов один бон, поэтому матрица объекта
закрывает двери, лампы и ящики одним механизмом).
