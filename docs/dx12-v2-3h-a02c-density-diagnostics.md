# V2.3h-A02c — Density Diagnostics & Storm Height Profile Correction

Статус: **D — PASS / H — PASS / R — PASS / U — BLOCKED / S — PASS WITH NOTES / V2 — NOT ACCEPTED**.
V2 PASS не присваивался. Legacy V2.3 остаётся принятым renderer baseline.
Reference и corrected S2 не являются default и не merge-ятся.

Ветка: `feature/dx12-volumetric-clouds`, HEAD
`42b8124e538ef4e40229a155203c133ca412756c`.

## A. Git State / Backup

- A02b package: `OGSR_V2_3h_A02b_Silhouette_Storm_Volume.zip`,
  SHA256 `CB6D11489C2D607EDCC7B26248A74C06E1CA5D82D14E3E56E245ACE1A41CBF1B`.
- A02b source hash (92 files, LF):
  `c08eff4e481e950074c7f31f3832933da0bc2b15eb41c313ca02912e9cee2d90`.
- A02c snapshot:
  `D:\Coding Project\OGSR_BACKUP_BEFORE_V23H_A02C_2026-10-28`,
  MANIFEST SHA256
  `65CE30401B14F3DAEA51CDA80D62986910324421EFAEF14A74C726A8794C72E3`
  (92/92 verified; restore byte-identical).
- Git state at handoff: working tree uncommitted, no staged changes;
  no commit/push/merge/reset/clean/rebase.

## B. Optical Depth Pipeline

Фактическая цепочка A02b:

1. `CloudRayMarch.hlsl` накапливает `transmittance *= exp(-sigma * step)`,
   `sigma = extinction * density`.
2. Debug mode 19 писал `mappedOD = OD / (OD + 1)`,
   `OD = -log(max(T, 1e-4))` в `CloudOutput` (RGBA16F), `a = 1`.
3. `CloudComposite.hlsl` для режимов 1..19 обходит sky, exposure и tone
   mapping, но **всегда** выполняет `LinearToSrgb(color)`.
4. Swapchain `R8G8B8A8_UNORM`; BMP/PNG содержат sRGB-кодированные 8-бит
   значения.
5. `analyze_a02b_silhouette.py` читал `byte/255` как линейный mappedOD.
6. Порог 0.25 применялся к sRGB-значению, а не к линейному optical depth.
7. IoU/Boundary Dice/OD correlation считались по этой искажённой маске.

Это не предположение: на станции `r_s1337_low_d0` средняя абсолютная
ошибка «sRGB как linear» составила **0.1533** (max **0.3084**), после
корректного `sRGB → linear` — **0.0012** (max 0.0354, квантование 8 бит).
По всем 68 станциям: **0.1335** против **0.00134** средней абсолютной
ошибки.

## C. Linear OD Fix

Добавлен прямой Float32-канал:

- тот же compute shader пишет в `ProbeOutput` (structured float4)
  `(mapped_od, od, 0, 1)`;
- данные не проходят composite, exposure, tone mapping, sRGB, атмосферу
  и 8-битный capture;
- `--debug-mode=view-optical-depth` теперь использует probe PSO;
- чтение — `ReadbackRawRecords(width * height)`;
- анализ — `scripts/analyze_a02c_optical_depth.py`;
- PNG/BMP оставлены только для визуализации.

Аналитический тест: восемь полос OD
`{0, 0.05, 0.1, 0.25, 0.5, 1, 2, 5}`. Проверено **180/180** строк,
worst absolute error **3.97e-8**; обратное преобразование
`OD = mapped / (1 - mapped)` — worst **< 1e-4**. Тест лежит в
`evidence/od_test_pattern/`.

Эффект на метрики (mapped OD 0.25 = raw OD 0.333, 4.53 км, seed 1337):

| Variant | Float32 | sRGB-decoded BMP | A02b raw-byte |
|---|---:|---:|---:|
| R-only | 0.987 | 0.987 | 0.988 |
| A w0.40 | 0.790 | 0.791 | 0.879 |
| B a0.18 | 0.697 | 0.698 | 0.806 |

Float32 и корректно декодированный BMP совпадают; A02b завышал IoU A/B
на ~0.09–0.11. Числа аудита (A 0.792, B 0.698) воспроизводятся.

## D. Float32 XYZ Dump Format

Для S0/S1/S2old/S2new:

- dense grid `128 × 96 × 128`, world origin `(camera.x - 8000, -100,
  camera.z - 8000)`, cell XZ **125 m**, cell Y **44.79 m**;
- 4 float32 на воксель: `raw_shape`, `shaped` (transfer + coverage remap),
  `envelope`, `density` (точное значение renderer-а);
- extinction: `sigma = density * 0.03` (коэффициент в JSON);
- `density_columns_float32.bin`: 48×48 колонок × 1024 сэмпла,
  шаг **4.199 m**, упаковано по 4 Y-сэмпла в float4;
- `storm_boundary_float32.bin`: `(bottom@1000, top@1000, bottom@2000,
  top@2000)` для 128×128 колонок;
- metadata JSON содержит grid, origin, anisotropic cell size, stride,
  index order, seed, weather preset, time/wind, coverage, density,
  stage, model, shader identity и EXE SHA256;
- NaN/Inf: 0 во всех дампах;
- `density` восстанавливается как
  `saturate(shaped * envelope * gap * density_multiplier)`; gap выводится
  из density при ненулевом знаменателе.

## E. S0/S1/S2 Density Statistics

Толщина считается по **настоящим вертикальным колонкам** (4.2 m шаг),
не по 2D-проекции. Порог — плотность, не яркость.

| Variant | Occupied columns | Mean thick (all) | Mean thick (occupied) | Std (occupied) | p50 | p95 |
|---|---:|---:|---:|---:|---:|---:|
| S0, ρ>0.05 | 100% | 1773 m | 1773 m | 26 m | 1776 | 1806 |
| S1, ρ>0.05 | 100% | 1798 m | 1798 m | 21 m | 1802 | 1823 |
| S2old, ρ>0.05 | 60.4% | 1045 m | 1729 m | 760 m | 1844 | 2839 |
| S2new, ρ>0.05 | 60.5% | 1098 m | 1816 m | 818 m | 1906 | 3036 |
| S0, ρ>0.20 | 100% | 1580 m | 1580 m | 154 m | 1625 | 1684 |
| S1, ρ>0.20 | 100% | 1667 m | 1667 m | 87 m | 1688 | 1734 |
| S2old, ρ>0.20 | 55.3% | 783 m | 1416 m | 707 m | 1440 | 2520 |
| S2new, ρ>0.20 | 56.0% | 823 m | 1469 m | 758 m | 1474 | 2692 |

Вывод: **заявленная в A02b примерно стократная разница разброса не
подтверждается**. При ρ>0.2 отношение S2new/S0 = 4.9×, S2new/S1 = 8.7×;
при ρ>0.05 — 31×/38×. A02b-значение `1838 ± 5.8 m` было получено из
2D `density-slice` (display `saturate(density*2)`, порог яркости 0.2,
шаг ~5.3 m) и не является 3D-статистикой. Независимый 3D-пересчёт даёт
`1580 ± 154 m` для S0 при ρ>0.2.

Структура 3D-поля (dense grid, ρ>0.2):

| Variant | Occupied voxels | 3D components | Largest | Vertical corr | Horizontal corr |
|---|---:|---:|---:|---:|---:|
| S0 | 33.9% | 27 | 100% | 533 m | 750 m |
| S1 | 36.9% | 9 | 100% | >537 m | 625 m |
| S2old | 12.3% | 125 | 83.9% | 260 m | 3625 m |
| S2new | 12.7% | 144 | 85.1% | 256 m | 3750 m |

S2 masses горизонтально в 4–5 раз крупнее S0/S1 (correlation 3.6–3.75 км
против 0.63–0.75 км), но вертикально менее связны и разбиты просветами.
Это и есть реальный геометрический выигрыш, а не «100× толщины».

При этом S2 — не extinction-only: у него 44–45% пустых колонок,
p95 толщины 2.5–3.0 км против 1.7 км у S0/S1 и в 4.9–8.7× больший
разброс при ρ>0.2. S остаётся **PASS WITH NOTES** с исправленными
числами, не сильнее.

## F. Height Envelope Audit

Текущий S2 (`stormVolume` flag = 1) вычисляет `bottom`/`top` как
функции `position`, включая **sample Y**:

```hlsl
ReferenceStormTop(position)    // p.y / 5000 входит в value noise
ReferenceStormBottom(position) // то же
t = (position.y - bottom(position)) / (top(position) - bottom(position))
```

Это самоссылающаяся формула: границы колонки зависят от высоты, в
которой её сэмплируют. Замер по 128×128 колонкам:

| | mean abs | max abs |
|---|---:|---:|
| bottom@1000 vs bottom@2000 | 37.2 m | 126.7 m |
| top@1000 vs top@2000 | 207.2 m | 765.5 m |

Коррекция (`--storm-column=1`) убирает Y из поля границ:

```hlsl
bottom = f(worldXZ, weather, seed, time)
top    = g(worldXZ, weather, seed, time)
h      = (worldY - bottom) / max(top - bottom, epsilon)
```

То же 5-км non-periodic поле, те же фазы и амплитуды; убран только
sample Y. Результат: Y-зависимость **0.0 m / 0.0 m** на всех 16 384
колонках. Дополнительно:

- envelope — один непрерывный интервал на колонку (0 лишних слоёв);
- нет инвертированных top/bottom;
- нет NaN/Inf;
- extreme `height=0/bottom=0` и `height=2000/bottom=500` — finite,
  без инверсий и без плоской крышки;
- поле C2 (quintic value noise, без modulo), 5-км границы без скачков;
- level flight 200 кадров (5 км, постоянная высота/pitch/FOV) — без
  popping; кадровые diff ниже.

Влияние коррекции на уже принятый S2-look: корреляция плотности
**0.9876**, изменено >0.05 только **2.6%** вокселей; top-height
меняется в среднем на 37.4 m (p95 179.2 m, max 3314.6 m, редкий
самоссылающийся выброс). Это локальная коррекция, не новая система.

**H — PASS**: дефект доказан и исправлен изолированным переключателем;
старый S2 сохранён как baseline.

## G. Storm Geometry Comparison

S0/S1 — сплошной deck: 100% занятых колонок, один связный объём,
толщина почти постоянна. S1 (density ×1.5) не создаёт геометрию.

S2old — крупные массы с просветами, но границы колонок «плавают» с
высотой сэмпла. S2new — те же крупные массы, но с стабильными
top/bottom; 44% пустых колонок, p95 2.7 км, отдельные плотные ядра.

Сравнение S2old → S2new:
- density correlation 0.9876 (общий look сохранён);
- 2.6% вокселей изменены >0.05;
- top-height mean 37.4 m, p95 179.2 m, max 3314.6 m;
- визуальные ракурсы: `evidence/visual/`.

## H. Corrected Uniqueness Metrics

Primary source: Float32 `optical_depth_float32.bin`, raw cloud-only
`OD = -log(T)`; thresholds in physical OD. The A02b-comparable station
is mapped OD 0.25, i.e. raw `OD = 0.3333`. Sunset, seed 1337, camera
y = −248 m, pitch 45°, FOV 57°, 640×360 cloud output.

| Variant | 4.53 km IoU | 4.53 OD corr | 4.53 bd Dice | 4.53 c4 | 9.06 IoU | 9.06 OD | 13.6 IoU | 13.6 OD |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R-only | 0.987 | 0.998 | 0.885 | 0.987 | 0.973 | 0.994 | 0.969 | 0.992 |
| A w0.40 | 0.790 | 0.624 | 0.374 | 0.624 | 0.783 | 0.588 | 0.821 | 0.739 |
| B a0.18 | 0.697 | 0.882 | 0.291 | 0.611 | 0.630 | 0.769 | 0.641 | 0.788 |

Threshold sweep at 4.53 km (IoU):

| Raw OD | R | A w0.40 | B a0.18 |
|---:|---:|---:|---:|
| 0.05 | 0.988 | 0.880 | 0.808 |
| 0.10 | 0.987 | 0.861 | 0.782 |
| 0.25 | 0.987 | 0.812 | 0.721 |
| 0.333 | 0.987 | 0.790 | 0.697 |
| 0.50 | 0.985 | 0.754 | 0.648 |
| 1.00 | 0.978 | 0.642 | 0.563 |
| 2.00 | 0.968 | 0.505 | 0.444 |

A/B снимают точную копию (OD corr 0.62/0.88 против 0.998 у R), но
крупные маски всё ещё перекрываются: A 0.79, B 0.70 при 4.53 км;
boundary Dice 0.37/0.29; multi-scale 0.62/0.61. B сохраняет поле
плотности лучше (OD 0.88), но силуэт меняет сильнее; A наоборот.
Ни A, ни B не проходят U как пространственно уникальные.

20/30/40 км — **не целые периоды** 4.53 км; там низкая корреляция у
всех вариантов отражает фазовый сдвиг, а не доказанную уникальность:

| Variant | 20 km IoU/OD | 30 km IoU/OD | 40 km IoU/OD |
|---|---:|---:|---:|
| R | 0.656/0.236 | 0.648/0.267 | 0.718/0.248 |
| A | 0.566/0.066 | 0.537/0.162 | 0.634/0.229 |
| B | 0.496/0.247 | 0.437/0.228 | 0.437/0.247 |

3D density autocorrelation (A02 Float32 XZ fields, matched controls):

| Variant | 4.53 km | 13.6 km |
|---|---:|---:|
| R | 0.864 | 0.967 |
| A w0.40 | 0.487 | 0.554 |
| B a0.18 | 0.850 | 0.846 |

S2 отдельно (Storm field; не сравнивается с matched-density A/B):

| Variant | 4.53 IoU/OD | 9.06 IoU/OD | 13.6 IoU/OD |
|---|---:|---:|---:|
| S2old | 0.144/0.241 | 0.068/−0.505 | 0.283/0.293 |
| S2new | 0.148/0.234 | 0.096/−0.468 | 0.295/0.305 |

S2old/S2new не воспроизводят копию на 4.53/9.06/13.6 км; 5-км поле
не периодично. На 20 км у S2 остаётся IoU 0.77/OD 0.87 — это фазовое
совпадение крупномасштабного weather-поля, а не 4.53-км копия; на
30/40 км оно исчезает. Это не делает U PASS для A/B.

Seed 4242 и высокая камера (y = 700 m) дают тот же вывод: R сохраняет
копию, A/B её ломают, но крупные маски A/B остаются связанными.
Полные строки — `evidence/silhouette_metrics.csv`.

## I. DX12 Regression

- Clean Release/Debug build: PASS, no C/X/LNK warnings.
- CTest Release/Debug: 1/1 PASS.
- Source checks Python/PowerShell: PASS.
- fxc: все Reference-перестановки + новые entry points
  `CloudDensityVolume.cso`, `CloudDensityBoundary.cso`,
  `CloudDensityColumns.cso` — PASS.
- Hardware selftest Legacy / R / A / B / S0 / S1 / S2old / S2new: PASS.
- WARP functional R / A / B / S2old / S2new: PASS.
- WARP + GBV: exit 2, документированное hardware-only ограничение.
- Release GBV Legacy / R / A / B / S2old / S2new: PASS, 0/0.
- Debug GBV S2new: PASS, 0/0.
- 120 frames + resize S2new (Release и Debug): 0/0,
  cloud output 469×242 → 629×372.
- GBV volume probe S2new (все три новые compute-перестановки):
  64×48×64, cell 250/89.6 m, columns 48×256×48 — 0/0.
- GBV OD probe R: 320×180 Float32 readback — 0/0.
- Negative injected ERROR: FAIL, exit 1 (ожидаемо).
- F2/F5/Pause S2new: slices↔clouds, storm→sunset, animation
  paused/running, 0 errors/0 warnings, exit 0.
- Shutdown: non-device objects 0 (Release и Debug, после
  добавления сброса трёх новых PSO).
- EXE SHA256:
  - capture EXE (OD/volume/visual): `B3ED1FF5...A515BE`;
  - final EXE: `434CA4F8...F6022D`;
  - final Debug: `0538631D...91D5DF`.
  Разница capture/final — только сброс трёх новых PSO в
  `CloudRayMarch::Shutdown`; контрольная OD-станция даёт
  **bit-identical** `.bin` и BMP, поэтому данные валидны для final EXE.

Pixel identity:
- Legacy V2.3 vs A02b: mean 0, max 0 (каждый пиксель).
- Reference direct vs A02b: mean 0, max 0.
- S2old vs A02b `s2_c055` horizon: mean 0, max 0.

**R — Regression: PASS.**

## J. Known Issues

1. U остаётся BLOCKED: A/B уменьшают, но не устраняют перекрытие крупных
   масс.
2. A02b-значение `1838 ± 5.8 m` и «100×» опровергнуты 3D-пересчётом;
   исправленные ratios 4.9× (ρ>0.2) и 31× (ρ>0.05).
3. 20/30/40 км — не целые периоды 4.53 км; низкая корреляция там
   отражает фазу, а не доказанную уникальность.
4. Dense grid 128×96×128 (125/44.8 m) предназначен для occupancy,
   components и correlation; толщина берётся из 4.2-m колонок.
5. OD probe — structured float4, не R32_FLOAT texture; тем не менее это
   честный Float32 без sRGB/8-бит. Тест-паттерн подтверждает.
6. S2new — isolated switch; default S2old и Legacy не изменены.
7. N4/H5/H6/V2.4/lightning/surge/game DX12 не начинались.

## K. Verdict

- **D — Diagnostic Correctness: PASS.** Линейный Float32 OD pipeline
  проверен аналитическими значениями; sRGB-ошибка A02b доказана и
  устранена; Python-анализ воспроизводим из Float32.
- **H — Height Profile: PASS.** Y-зависимость envelope доказана
  (top до 765 m), corrected column envelope даёт 0.0 m, finite,
  без инверсий; old S2 сохранён.
- **R — Regression: PASS.** Release/Debug, CTest, source checks, fxc,
  HW/WARP/GBV, 120 frames + resize, Float32 volume/OD probes, negative,
  F2/F5/pause, Legacy/Reference/S2old pixel identity 0/0, shutdown
  0 live objects.
- **U — Uniqueness: BLOCKED.** См. раздел H.
- **S — Storm Volume: PASS WITH NOTES.** Геометрия реальна (gaps,
  p95, разброс), но «100×» опровергнут; числа исправлены.
- **V2: NOT ACCEPTED.** Независимая приёмка не присваивается.

No commit/push/merge.
