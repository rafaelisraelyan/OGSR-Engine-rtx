# F4.1 — полная статическая сцена Jupiter в native DX12

Статус: реализовано и проверено на реальных данных Jupiter (2026-11-21).
База: `F4.0-B-VSLICE` (`81956aa1a`). Ветка: `feature/f41-full-static-jupiter`.
Merge в `prypiat` не выполнялся.

## 1. Что изменилось по сравнению с F4.0-B

F4.0-B рисовал ровно пять объектов: сектор 129 (или 130 по env-пробе), четыре
визуала `9/105/106/107` из `fourvisual12::kPlan` и один диагностический
`415`, разложенных в сетку 2×2 перед камерой (диагностическая постановка,
а не мировые координаты).

F4.1 убирает ручной отбор из обычного DX12 runtime:

| Было (F4.0-B) | Стало (F4.1) |
|---|---|
| `jupiterSector = 129`, `jupiterRootVisual = 17431` | сектор не выбирается вообще |
| список визуалов `9,105,106,107` | визуалы берутся из контейнера уровня |
| `PrepareSceneSector` (один сектор) | обход **всех** секторов уровня |
| NDC-раскладка перед камерой | мировые координаты как есть (identity world) |
| `kSceneCapacity = 64` | 20880 визуалов, 8692 draw |
| 5 текстур | 329 текстур, все разрешены |
| один input layout | два фактических layout уровня |

Диагностический путь M23/J1 сохранён как отдельный opt-in и в обычном
runtime не участвует.

## 2. Архитектура

Ключевой факт, на котором построено решение (подтверждён аудитом R4):
**статическая геометрия уровня уже в мировых координатах**. R4 ставит
identity (`r__dsgraph_render.cpp:18`), `_NormalItem` не содержит матрицы,
а вершины лежат в общих пулах `level.geom`, адресуемых через
`vBase/vCount` и `iBase/iCount` (`FVisual::Render`). Поэтому:

- per-instance трансформации не нужны;
- пулы `level.geom` загружаются в GPU **как есть**, без переупаковки вершин;
- «инстансинг» выражается повторным использованием одних и тех же пулов
  без повторной загрузки геометрии.

Поток данных (без нового параллельного загрузчика):

1. `GameDx12J2ABatch_ReadSnapshot` — штатное чтение `$level$/level` и
   `$level$/level.geom` через VFS.
2. `levelhierarchy12::Parse` + `CollectLeaves` — обход дерева секторов.
3. `xraylevel12::ReadLevelPools` (новое) — **один** разбор контейнера
   визуалов и общих пулов VB/IB. Метаданные и байтовые диапазоны, без копий
   геометрии. Устраняет квадратичную работу `ReadVisual` (тот заново
   разбирает пулы на каждый visual).
4. Обход достижимости от корней секторов (в bridge) — какие визуалы R4
   реально рисует.
5. `GameDx12StaticScene12_ResolveMaterials` — `shader_id` → первая текстура
   записи `fsL_SHADERS`, признак `aref`.
6. `GameDx12J2ABatch_ReadTextureNames` — DDS/THM через VFS, дедупликация.
7. `GameDx12StaticScene12::Load` — PSO, пулы, текстуры, один fence.

## 3. Поддержка форматов по фактическим данным

### 3.1 Раскладки вершин

В `level.geom` Jupiter реально встречаются три объявления (stride 32,
общий префикс POSITION/NORMAL/TANGENT/BINORMAL):

| Раскладка | Последние 8 байт | Визуалов |
|---|---|---|
| `Classic` | COLOR@24 (D3DCOLOR) + TEXCOORD0@28 (SHORT2) | 1844 |
| `UvAt24` | TEXCOORD0@24 (SHORT2), опционально TEXCOORD1@28 | 6848 |
| `UvAt24` (SHORT4) | SHORT4@24 = (u,v,lmu,lmv) | входит в предыдущую |

Первые две различаются только смещением TEXCOORD0, поэтому GPU-путь
использует два input layout / два PSO и переключает PSO один раз на группу.

Декодирование совпадает с R4 (`common_functions.h`, `unpack_tc_base`):

```
normal  = 2 * N.zyx - 1                    // D3DCOLOR -> RGB
uv      = (short2(tc) + float2(T.w, B.w)) * (32 / 32768)
```

### 3.2 Типы визуалов

Реальная гистограмма Jupiter (20880 визуалов):

| Тип | Кол-во | Обработка в F4.1 |
|---|---|---|
| `MT_NORMAL` (0) | 7903 | рисуется |
| `MT_HIERRARHY` (1) | 3954 | группа, раскрывается |
| `MT_PROGRESSIVE` (2) | 789 | **рисуется** по SWI-окну 0 |
| `MT_LOD` (6) | 3015 | первый (детальный) уровень |
| `MT_TREE_ST` (7) | 3035 | **не поддержано**, пропускается |
| `MT_TREE_PM` (11) | 2184 | **не поддержано**, пропускается |

`MT_PROGRESSIVE` — важное открытие: это большие патчи рельефа, которые
обход листьев (`CollectLeaves`) не эмитит. Без них карта выглядела
«продырявленной» (пиксели совпадали с цветом очистки). Окно берётся из
`OGF_SWIDATA` (chunk 6): 4 reserved u32, count, затем `FSlideWindow`
`{u32 offset; u16 num_tris; u16 num_verts}`, и рисуется как в
`FProgressive::Render` — `vBase/vCount`, `iBase+offset`, `num_tris`.

### 3.3 Индексы

Только `R16_UINT`: формат `level.geom` хранит индексы u16 (`iCount*2`).

### 3.4 DDS

Расширено под реальные данные: legacy `DXT1`/`DXT3`/`DXT5` и заголовок
`DX10` с `BC1/BC2/BC3/BC4/BC5/BC7`. Неподдержанный формат не скрывается:
считается и логируется (`ph_dx12_f41: texture unsupported`).

### 3.5 Материалы

`shader_id` → запись `fsL_SHADERS` вида `shader\texture[,texture...]`.
Базовая текстура — первая в списке. Cutout включается, если имя шейдера
содержит `aref` **или** THM текстуры сообщает alpha (иначе сетки/решётки
рисовались сплошным чёрным).

## 4. Ресурсы и кадр

- Пулы загружаются один раз на уровень/сессию: 36 VB + 11 IB, 93.8 МБ.
- 329 текстур, 95.6 МБ, дескрипторный heap `N+1` (0 — null-SRV).
- Upload идёт через один staging 128 МБ с чанкованием и подтверждением
  fence между порциями; повторных загрузок геометрии в кадре нет.
- Кадр: обновление frame-CB (transposed ViewProjection), frustum culling по
  сферам из `OGF_HEADER` (плоскости из row-vector VP), затем два прохода по
  раскладкам. `record_ms` ≈ 2–6 мс на ~500–1400 видимых draw.
- Освещение первого этапа: `0.55 + 0.45*saturate(N·sun) + 0.22*saturate(N.y)`
  с ручным sRGB-кодированием (цель — `R8G8B8A8_UNORM`).
- Цвет очистки — небо (`0.42,0.50,0.62`), переопределяется
  `OGSR_DX12_CLEAR=r,g,b`.

## 5. Управление и диагностика

| Переменная | Назначение |
|---|---|
| `OGSR_DX12_GAME_STATIC_SCENE=0` | выключить полную сцену (остаётся диагностика) |
| `OGSR_DX12_GAME_FLY=x,y,z,yaw[,pitch[,spin]]` | **только для доказательных кадров**: свободная камера на базе штатных матриц |
| `OGSR_DX12_GAME_SCENE_CULL=0` | отключить frustum culling |
| `OGSR_DX12_GAME_SCENE_CULL=none` | отключить отсечение граней |
| `OGSR_DX12_GAME_SCENE_NOTEST=1` | отключить alpha-test (диагностика) |
| `OGSR_DX12_GAME_SCENE_VIEW=1|2|3` | material-id / UV / textured-mask |
| `OGSR_DX12_GAME_SCENE_LOG=1` | кадровый лог draws/culled/ms |
| `OGSR_DX12_CLEAR=r,g,b` | цвет «неба» |

Логи: `ph_dx12_f41: snapshot|hierarchy|visual_types|sector_root_types|pools|
layout_visuals|reachability|materials|textures|scene loaded|ready|frame`.

## 6. Фактический результат (Jupiter)

```
snapshot          level_bytes=6873629 geom_bytes=93930032
hierarchy         sectors=151 visuals=20880 leaves=7903
visual_types      type0=7903 type1=3954 type2=789 type6=3015 type7=3035 type11=2184
sector_root_types normal=0 hierarchy=151 progressive=0 lod=0 tree=0
pools             vb=36 ib=11 visuals=20880 drawable=8692 rejected=12188
layout_visuals    classic=1844 uv24=6848
reachability      normal=7903 progressive=789 referenced=8692 dropped=0
                  skipped_tree_nodes=5219
materials         drawable=8692 textured=8692 no_texture=0 distinct_shader_ids=...
textures          unique=329 decoded=329 missing=0 unsupported=0 bytes=95604752
scene loaded      draws=8692 geometry_bytes=93817940 alpha_test=1002
кадр (интерьер)   draws=1393 culled=7299 record_ms=5.7
кадр (аэро)       draws=763  culled=7929 record_ms=3.3
кадр (улица)      draws=516  culled=8176 record_ms=2.2
InfoQueue         shutdown verdict=PASS debug=1 gbv=1 gbvVerified=1
                  warning=0 error=0 corruption=0 discarded=0
```

## 7. Что осознанно не сделано

- **Деревья** (`MT_TREE_ST`/`MT_TREE_PM`, 5219 узлов) не рисуются: им нужен
  per-instance `tree_data` и instanced vertex shader. Они учитываются в
  логе как `skipped_tree_nodes`, а не выдаются за готовые.
- **Скайбокс/атмосфера**: небо — цвет очистки.
- **Тени, PBR, RTX, динамические объекты, HUD, эффекты**: вне этапа.
- **Порталы/HOM**: culling только по сферам, без посекторной видимости.
- **LOD по расстоянию**: у `MT_PROGRESSIVE` берётся окно 0 (самый детальный),
  у `MT_LOD` — первый уровень.
- Прозрачные blended-материалы (стекло, вода) рисуются как cutout/opaque.

## 8. Известные артефакты

- Тёмные пятна на аэро-кадре в районе карьера — поверхности с прозрачными
  материалами (вода/грязь), нарисованные без blend. Не «дыры»: проверено
  сэмплированием пикселей.
- Некоторые тонкие элементы (провода, рельсы) могут мерцать из-за
  отсутствия LOD/депт-биаса.

## 9. Правила запуска

См. `artifacts/F4.1-FULL-STATIC-JUPITER/run-f41.ps1` (готовит run dir,
gamedata-оверлей, `fsgame.ltx`, appdata, запускает, штатно закрывает,
пишет манифест) и `summarize-f41.ps1` (сводка по логу).
