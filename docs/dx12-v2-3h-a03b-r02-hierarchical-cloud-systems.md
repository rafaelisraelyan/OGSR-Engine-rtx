# V2.3h-A03b-R02 — Hierarchical Cloud Systems & Multi-Scale Morphology

Статус: **BLOCKED на CPU geometry gate (G/V/U/K)**. GPU/HLSL-вариант не
создавался. Weather и Storm не запускались по stop rules. V2 NOT ACCEPTED.

- Ветка: `feature/dx12-volumetric-clouds`
- HEAD: `42b8124e538ef4e40229a155203c133ca412756c`
- Дата: 2026-11-01
- Legacy V2.3 остаётся default; A03 M1 и A02c S2new не изменялись.

## A. Git State & Backup

- Полный backup: `D:\Coding Project\OGSR_BACKUP_BEFORE_V23H_A03B_R02_2026-11-01`,
  MANIFEST SHA256
  `504886CF0FE62AF1C5751ECCAB2FC8E28C0421EF4E7667AA4A764FBC210E4837`.
- 390/390 файлов, 282 104 499 байт, 110 untracked; restore проверен
  побайтно.
- Git: 129 status entries, 0 staged; commit/push/merge/reset/clean/rebase
  не выполнялись.
- R01 backup:
  `OGSR_BACKUP_BEFORE_V23H_A03B_R01_2026-10-31`, manifest
  `BDD2EB7143F7A77ACA5649551106FD0C7C4E002C0BD26DE6986F4C5A4F26ED36`.

## B. R01 Baseline & Identity

- R01 source, 126 файлов:
  `ae90cce396f425b7e3c38322ade98ec412e7464313f7f2a4503dd10bd0f3bfa6`.
- R01 package:
  `B2C34C24ACF561F5524FD103DF8CD5F26E4F1A788C3DADFE7BA3BD40EAB294B8`.
- A03b package:
  `EDDF478234AD64727575EF23FF0990661624F3768AE23F2373E0F8DD9B1761EA`.
- Baseline identity после R01 metadata-сборки: **9/9 byte-identical**
  (Legacy, RB, R, A, B, M1, S2old, S2new, M1 OD).
- Release EXE:
  `4574A4F69BB3F2C7EC049DCA64B81830FD398F10499B21156161F68B1EA6C498`.
- R02 не меняет `src/`, `shaders/` или runtime; меняются только CPU
  research scripts, tests и документация. Поэтому baseline identity
  остаётся 9/9, полный DX12 regression для R02 не заявляется.

## C. Diagnostic Corrections (D gate)

Исправлены дефекты, найденные после R01:

1. **p90 percentile.** Старый анализатор сортировал площади по убыванию и
   брал `areas[int(0.9*n)]`, то есть значение из хвоста малых компонент.
   Новый код использует ascending linear-interpolation percentile.
   - R01 local: old p90 = 45 cells → corrected 2196.8 cells (100 m grid).
   - R01 regional: old p90 = 32 cells → corrected 298.6 cells (300 m grid).
   Старые JSON сохранены; новые записаны как `*_corrected.json`.
2. **World aspect.** Старый R01 XY-срез 16 км × 2.8 км растягивался в
   квадрат 160×160. Новый renderer сохраняет metres-per-pixel: 800×140.
   R01 evaluator не изменялся; corrected XY/YZ получены отдельным скриптом.
3. **2D vs 3D components.** В статистике теперь явно указаны
   `dimension`, `connectivity`, grid и voxel/cell units. 2D — 4-connected
   XZ column-max; 3D — 6-connected volume.
4. **Metadata/checksum.** Float32 dump содержит grid, origin, cell size,
   axes, index order, seed, algorithm parameters, source SHA256, field name,
   element count и SHA256 данных. Все 53 поля проверены: 7 540 416
   finite values, 0 non-finite, 0 checksum/metadata errors.
5. **Unit tests.** 10/10 PASS: percentile, aspect, resampling, 2D/3D
   components, Float32 checksum roundtrip.

R01 evals/fields не перезаписывались. Исправленные R01 slices:
`evidence/r01_corrected/r01_xy_corrected_s1337.png`,
`r01_yz_corrected_s1337.png`.

## D. Hierarchical Architecture

### HCS1 — основной кандидат

Три уровня, вычисляемые в world space:

1. **System influence, S.** Domain-warped FBM 52 км + ridge 30 км.
   Warp: 6.2 км @ 38 км и 1.5 км @ 12.5 км. S — непрерывное поле [0,1],
   не бинарная маска.
2. **Mass organization, M.** Warped FBM 3.2 км + ridge 6 км.
   M — непрерывное поле [0,1].
3. **Vertical development.** Для колонки:
   - bottom = 1300 ± 450 (28 км) ± 300 (4 км) ± 180 (11 км);
   - thickness = 2400 ± 800 ± 600 ± 350 + 400·S;
   - top = bottom + thickness; top softness 0.54–0.88 на 7 км;
   - 3D mass M3 = fbm3 2.5 км (XZ) × 700 м (Y), 3 октавы;
   - height-dependent shear ±650 м на 1.5 км / 3.2 км.

Формула:

```text
org = S · (0.20 + 0.80·M)
v   = (y − bottom) / thickness
W   = smoothstep(0.015, 0.095, v)
      · (1 − smoothstep(top_soft, 0.995, v))
F   = org · W · (0.10 + 0.90·M3)
occ = smoothstep(cut−0.035, cut+0.035, F)
```

`cut = 0.30`, occupancy threshold 0.2. M3 и shear зависят от Y, поэтому
горизонтальный рисунок на разных высотах различается; это прямая проверка
против XZ-экструзии.

### HCS2 — negative control

Чтобы проверить, не является ли проблема следствием непрерывного поля,
собран второй CPU-вариант: system field + две несоизмеримые сетки масс
5.3 км (axis-aligned) и 7.3 км (rotated 31°), по одному 3D-site на ячейку,
3×3 neighborhood = 18 sites/sample. Site имеет собственные XZ-радиусы,
Y-центр 1550–3850 м, асимметричные vertical radii 360–1420 м, yaw, box и
smooth warp. HCS2 не является production-кандидатом; это проверка того,
устраняет ли явная объектная модель лабиринт HCS1.

## E. CPU Evaluator & Evidence Grid

- `scripts/proto_r02_systems.py` — HCS1.
- `scripts/proto_r02_sites.py` — HCS2.
- `scripts/r02_diagnostics.py` — общие diagnostics.
- Local: 16 км, 96×96×40, cell 166.7×135×166.7 м, Y 400–5800 м.
- Regional: 60 км; seed 1337 — 160×160×20 (375×270×375 м), остальные —
  120×120×16 (500×337.5×500 м). Разное разрешение влияет на component
  counts; прямые сравнения counts между seed 1337 и остальными не
  абсолютны.
- Seeds: 1337, 4242, 99, 2027, 9001, 9529.
- Raw поля: `system`, `mass`, `vertical`, `occupancy`, `density`
  (Float32 LE) для representative seeds; `xzmax` для всех.
- Determinism: 6/6 Float32 files byte-identical при повторном запуске.

## F. Multi-Scale Geometry (G gate)

### HCS1 local, 16 км, cut 0.30

| seed | columns | voxels | 3D comp | largest 3D | thick p50/p90, м |
|---:|---:|---:|---:|---:|---:|
| 1337 | 30.2% | 7.5% | 13 | 46.5% | 1755 / 2430 |
| 4242 | 0.0% | 0.0% | 0 | — | — |
| 99 | 4.4% | 0.4% | 6 | 82.0% | 405 / 1485 |
| 2027 | 55.9% | 8.7% | 34 | 83.4% | 945 / 2025 |
| 9001 | 69.2% | 18.0% | 7 | 99.5% | 1620 / 2160 |
| 9529 | 0.0% | 0.0% | 0 | — | — |

### HCS1 regional, 60 км, cut 0.30

| seed | columns | voxels | 3D comp | largest 3D | thick p50/p90, м |
|---:|---:|---:|---:|---:|---:|
| 1337 | 42.7% | 12.5% | 74 | 97.6% | 1890 / 2700 |
| 4242 | 24.7% | 6.9% | 39 | 67.0% | 1687 / 2362 |
| 99 | 36.6% | 9.2% | 112 | 79.9% | 1687 / 2362 |
| 2027 | 46.5% | 12.3% | 99 | 93.1% | 1687 / 2362 |
| 9001 | 52.9% | 13.3% | 77 | 96.0% | 1687 / 2362 |
| 9529 | 2.25% | 0.56% | 10 | 81.4% | 1687 / 2362 |

Визуально 16 км — крупные rounded islands; 60 км — связный лабиринт,
заполняющий большую часть области. System influence на 16 км почти
однороден, на 60 км насыщен; mass field даёт лабиринт, а не отдельные
системы с естественными промежутками. 3D-компоненты HCS1 regional 1337:
p50 0.076 км³, p90 1.424 км³, p95 5.988 км³, но один компонент занимает
97.6% объёма.

### HCS2 negative control

- Local 16 км: 18.0–50.7% columns, 13–18 3D components, largest 16.2–23.7%,
  thickness p50 540–1080 м, p90 810–1485 м.
- Regional 60 км: 21.2–39.0% columns, 129–177 components, largest 2.6–9.1%,
  thickness p50 ~1012 м, p90 1012–1687 м.
- Визуально — регулярное поле почти одинаковых rounded blobs; две сетки
  не устранили lattice, а сделали его более очевидным.
- Regional 1337 component volumes: p50 5.53 км³, p90 19.17 км³,
  p95 24.84 км³ — много похожих по размеру объектов.

**G — BLOCKED.** HCS1: rounded islands + лабиринт, HCS2: регулярные blob'ы.
Ни один вариант не даёт убедительных сложных 3D-масс.

## G. Vertical Structure (V gate)

HCS1 local, seed 1337:

- top std 757 м; bottom std 487 м;
- thickness p50 1755 м, p90 2430 м;
- neighbor correlation top X/Z: **0.943 / 0.930**;
- unique profile signature fraction: 30.2% local; 16.5–29.5% regional;
- columns with >1 interval: 29–39%, но это в основном шумовые разрывы,
  а не выраженные multi-layer структуры.

HCS2:

- unique profile fraction 4.3–7.2% local, 6.9–9.1% regional;
- neighbor correlation top X/Z: **0.965–0.989 / 0.966–0.978** local;
- вертикальные профили — почти одинаковые thin lenses; толщина меняется,
  но форма и top/bottom коррелируют почти полностью.

Профили HCS1 на heatmap — узкие горизонтальные полосы на близких высотах.
Разнообразие толщины есть, но оно не превращается в разные вертикальные
формы. **V — BLOCKED.**

## H. Spatial Statistics (U gate)

- HCS1 regional autocorrelation at 7 км: от −0.01 до +0.38; выраженного
  периодического пика нет. Это не спасает геометрию: один 3D-компонент
  занимает 67–97.6% объёма, а 60 км поле визуально является лабиринтом.
- HCS2 autocorrelation at 7 км: −0.13…+0.19; две сетки маскируют
  автокорреляционный пик, но визуальная решётка остаётся.
- Directional 1D spectrum: доминирующие длины 5–8 км, без устойчивого
  единого периода; спектр не является доказательством отсутствия lattice.
- HCS2 alias 1337/9529 xzmax correlation: **−0.0437** — точного alias нет,
  но оба seed дают один и тот же класс регулярных blob'ов.

**U — BLOCKED.** Механическое повторение крупных масс подтверждено
визуально и через доминирование одного компонента (HCS1) / регулярную
сетку (HCS2).

## I. Seed Robustness (H5)

Coverage при одном и том же cut 0.30:

- HCS1 local: **0.0–69.2%** columns; 4242 и 9529 пусты, 9001 — 69.2%.
- HCS1 regional: **2.25–52.9%**; 9529 — 2.25%, 9001 — 52.9%.
- HCS2 local: 18.0–50.7%; regional: 21.2–39.0%.

Разброс HCS1 несовместим с требованием «модель не должна выглядеть
естественно только на seed 1337» и с будущим weather control. Это не
мелкая настройка: один seed полностью пуст, другой почти полностью занят.

Large world coordinates (CPU, seed 1337, center 1 000 000 / −500 000 м):
поля finite, NaN/Inf нет; occupied columns 29.5% → 65.1% относительно
начала координат. Это ожидаемое различие непериодического поля, но
показывает, что фиксированный cut не нормализует coverage по seed/месту.

## J. Weather Feasibility (C gate)

**NOT RUN.** По stop rule после провала G/V расширенные weather-сценарии
не запускались. Фиксированный cut 0.30 уже даёт 0–69% coverage на разных
seed; Clear/Partly/Overcast/Sunset нельзя честно оценивать без
нормализации coverage. Ручная подгонка cut под каждый seed запрещена.
Никаких weather PASS/FAIL для R02 не заявляется.

## K. Storm Geometry Feasibility (S gate)

**NOT RUN.** S2new не изменялся. Region model не дошла до Storm; никаких
заявлений о связности, толщине или front geometry нет. S2new остаётся
независимым reference.

## L. Computational Complexity (K gate)

CPU benchmark, 200 000 полных samples, Python, эта машина:

| Variant | CPU s / 1M samples | samples/s | per-sample work |
|---|---:|---:|---|
| HCS1 | 55.8 | 17 927 | 17×noise2 + 3×noise3, 92 hash evals, 0 sites |
| HCS2 | 26.7 | 37 427 | 7×noise2 + 18 site evals, 28 hash2 + 18 site hashes |

HCS1 дорог из-за 3D noise и повторного column context на каждый sample;
HCS2 дешевле по CPU, но требует 18 site evaluations и всё равно даёт
регулярную решётку. GPU cost gate не проводился; перспективность для
raymarch не доказана. **K — BLOCKED (риск).**

## M. Visual Gate

Contact sheets (package paths):

- `evidence/sheets/SHEET_hcs1_local_s1337.png` — system / mass / vertical /
  occupancy / XZ / XY / YZ / profiles / top surface.
- `evidence/sheets/SHEET_hcs1_60km_s1337.png` — 60 км system, mass,
  vertical, column-max, XY, YZ.
- `evidence/sheets/SHEET_hcs1_seeds_local.png` — six local seeds.
- `evidence/sheets/SHEET_hcs1_seeds_regional.png` — six regional seeds.
- `evidence/sheets/SHEET_hcs2_60km_s1337.png` — negative control.
- `evidence/sheets/SHEET_r01_aspect_correction.png` — corrected R01 aspect.

Причины отказа визуального гейта:

1. rounded islands и лабиринт вместо отдельных систем и групп масс;
2. почти одинаковые вертикальные профили/полосы;
3. видимая регулярная сетка у HCS2;
4. пустые/переполненные seed при одном cut;
5. нет естественных больших промежутков внутри системы при сохранении
   связных масс.

## N. Known Limitations

- GPU/HLSL, PSO, DX12, weather, storm, temporal и flight не запускались.
- CPU evaluator использует double; raw fields сохранены как Float32.
- HCS2 — только negative control, не кандидат для production.
- Regional seeds, кроме 1337, сняты при 120×120×16; component counts
  зависят от разрешения.
- Спектр — 1D directional diagnostic, не полный 2D PSD.
- Large-coordinate проверка только на 1 000 000 м и только CPU.
- Фиксированный cut не является weather control и не нормализует coverage.
- Полный DX12 regression не выполнялся; identity 9/9 относится к
  неизменённым control-вариантам.

## O. Verdict

| Gate | Result |
|---|---|
| G — Hierarchical 3D Geometry | **BLOCKED** |
| V — Vertical Morphology | **BLOCKED** |
| U — CPU Spatial Uniqueness | **BLOCKED** |
| C — CPU Weather Feasibility | **NOT RUN** |
| S — Storm Geometry | **NOT RUN** |
| K — Computational Complexity | **BLOCKED (risk)** |
| D — Diagnostic Correctness | **PASS** |
| GPU Performance | **NOT RUN** |
| DX12 R02 | **NOT RUN** |
| Temporal GPU | **NOT RUN** |
| V2 Overall | **NOT ACCEPTED** |

**Итог: BLOCKED.** HCS1 и HCS2 не переносятся в HLSL. Следующий шаг —
не GPU-порт текущей модели, а новая архитектура с истинно трёхмерным
размещением масс, нормализацией coverage по seed и без регулярной
генераторной сетки; либо возврат к контрольным A03b-вариантам.
