# V2.3h-A03 — Non-Tiling World-Space Cloud Macrostructure

Статус: **U PASS WITH NOTES / M PASS WITH NOTES / S BLOCKED (integration)
/ T PASS / P measured; V2 NOT ACCEPTED.**
Legacy V2.3 остаётся принятым baseline. M1 не является default и не
merge-ится. Storm S2new сохранён без изменений.

Ветка: `feature/dx12-volumetric-clouds`, HEAD
`42b8124e538ef4e40229a155203c133ca412756c`.

## A. Git State & Backup

- A02c package: `OGSR_V2_3h_A02c_Density_Diagnostics.zip`, SHA256
  `D1935647393EF26C5497EF45B19484769172931C993C73F71E592C847FC17679`.
- A02c source hash (99 files, LF):
  `70131b8c4193efcfabc91989e52338676bcb885c8b5a5e80975392448afb33b5`.
- A03 snapshot: `D:\Coding Project\OGSR_BACKUP_BEFORE_V23H_A03_2026-10-28`,
  MANIFEST SHA256
  `C261B4DD79BBD634339232D1A95933687A8B7E0DC1C489D6A774EC1F186D6C18`
  (99/99; restore byte-identical).
- A03 source/patch hashes: см. `AUDIT_BASELINE.txt` в пакете.
- Git state at handoff: uncommitted; 0 staged; no
  commit/push/merge/reset/clean/rebase.

## B. Baseline Identity

M0 снят на A02c-сборке до изменений A03. A03-сборка с выключенным
`field-mode=macro` даёт **byte-identical** BMP для всех контрольных
вариантов:

| Variant | Compare | Result |
|---|---|---|
| Legacy V2.3 | humulus below 1280×720 | byte-identical |
| Reference R | sunset station 1280×720 | byte-identical |
| A w0.40 | sunset station 1280×720 | byte-identical |
| B a0.18 | sunset station 1280×720 | byte-identical |
| Storm S2old | storm horizon 640×360 | byte-identical |
| Storm S2new | storm horizon 640×360 | byte-identical |

A02c R/A/B/S2new остаются доступными и не изменяются при выключенном
A03. M0-файлы: `evidence/m0` и `evidence/identity`.

## C. Root Cause of Macro Repetition

Reference base shape — это `NoiseTexture` (128³ Perlin-Worley),
семплированная в world space с wrap. Период:

```text
P = 1 / (noiseScale * kReferenceBaseScale),  kReferenceBaseScale = 0.55
Sunset   0.00040 -> 4545.45 m
Overcast 0.00060 -> 3030.30 m
Storm    0.00035 -> 5194.81 m
Clear    0.00090 -> 2020.20 m
Partly   0.00080 -> 2272.73 m
```

На точном периоде Sunset R-only даёт **IoU 1.000 / OD corr 1.000**
(seed 1337 и 4242) на 4.545/9.091/13.636 км. Overcast R-only даёт
**1.000** на 3030/6061/9091 км. A/B меняют плотность/modulation, но
крупные маски остаются связанными: A 0.790–0.823, B 0.698 (seed 1337)
и **0.942/0.995** (seed 4242). Последнее — почти точная копия: модуляция
плотности не устраняет повторение крупных масс.

A02c использовал округлённые 4530/9060/13600; A03 использует точные
period-aligned позиции из формулы выше.

## D. M1 Architecture & Implementation

M1 — отдельный `ReferenceFieldMode::NonTilingMacro`; он заменяет
**размещение** крупных масс, а не модулирует уже размещённое облако.

Два октава 3D value noise на hash-решётке без modulo:

```text
n1 = ValueNoise((p - wind) / 6000, y / 12000, phase1)
n2 = ValueNoise((p - wind) / 2500, y / 5000,  phase2)
macro = lerp(n1, n2, 0.35)
mass  = smoothstep(0.42, 0.52, macro + (detailT - 0.5) * 0.15)
shape = mass * lerp(0.55, 1.0, detailT)       // detail 0.45
heightScale = lerp(0.65, 1.0, shape)          // non-storm only
```

- `detailT = smoothstep(0.58, 0.88, R)` — существующий R-channel
  Perlin-Worley, один texture fetch.
- `mass` — непериодическое world-space поле; именно оно задаёт
  расположение, размеры и просветы крупных масс.
- `shape` — макро-масса с внутренней detail-модуляцией; detail не
  решает, где находятся крупные облака.
- `heightScale` утоньшает слой к краям макро-массы; Storm S2new
  исключён, его envelope не меняется.
- Seed: фазовый сдвиг `seed % 8192 / 8192 * 128`; детерминированный,
  не per-frame.
- Wind: XZ-смещение вычитается и в macro, и в detail; поле
  транслируется вместе с ветром.
- Нет новой 3D-текстуры, нет CPU random, нет camera-dependent offset.
- Стоимость: 2 октава × 8 hash = 16 hash-вычислений на density sample;
  detail fetch 1. Точная цена — раздел L.

## E. M2 Alternative

M2 (sparse cell placement) **не реализовывался**: M1 на основном
Sunset-периоде даёт IoU 0.003–0.194 по пяти seed, точных копий нет.
M2 остаётся зарезервированным, если независимый reviewer потребует
более сильного разрыва на 9.09 км / Overcast.

## F. Macro / Detail Separation

Сравнение seed 1337 на 4.545 км (mapped OD 0.25):

| Variant | IoU | OD corr | Maxima match |
|---|---:|---:|---:|
| R-only | 1.000 | 1.000 | 0.960 |
| A w0.40 | 0.790 | 0.626 | 0.635 |
| B a0.18 | 0.698 | 0.884 | 0.748 |
| Macro only (detail 0) | 0.286 | −0.346 | 0.047 |
| Macro + detail 0.25 | 0.095 | −0.006 | 0.140 |
| Macro + detail 0.45 (M1) | 0.134 | 0.184 | 0.186 |
| Macro + detail 0.80 | 0.157 | 0.307 | 0.277 |

Macro-only силуэт узнаваем (крупные массы/просветы), detail добавляет
внутреннюю структуру и не возвращает периодическую форму: IoU остаётся
0.095–0.157 при detail 0.25–0.80. Detail 0.45 выбран как баланс
текстуры и уникальности; detail 0.80 усиливает повтор внутренних
ядер (maxima 0.277), detail 0.25 даёт более гладкие массы.

## G. Float32 World-Space Diagnostics

- OD: 70 станций, cloud-only `od = -log(T)`, 320×180 Float32;
  Sunset 0/4545/9091/13636/20000/30000/40000/60000, seeds
  1337/4242/7/99/2026; Overcast 0/3030/6061/9091 (свой период);
  A/B controls; macro-only и detail 0.25/0.80.
- 16 км volume: `128×96×128`, cell 125/44.79 m, 4 канала
  (raw_shape, shaped, envelope, density), 48×48×1024 колонок
  с шагом 4.199 m; R/M1/macro-only.
- 60 км horizontal slice: `512×1×512`, cell **117.1875 m**,
  origin `(-30000, 900, -30000)` и `(-30000, 1800, -30000)`;
  macro-only raw_shape и M1 density; aspect 1:1.
- Metadata: origin, cell, axis order, seed, model, weather, wind,
  precision, PSO, EXE SHA256; NaN/Inf = 0.
- Большие координаты: 60 км panorama и 60 км flight; float32
  hash cell index ~10, detail coordinate ~13; без floating-origin
  скачков.

16 км volume, ρ>0.2 (Sunset camera):

| Variant | Occupied cols | Mean thick | Std | 3D comp | Largest |
|---|---:|---:|---:|---:|---:|
| R (periodic) | 50.5% | 329 m | 239 m | 403 | 6.6% |
| M1 (macro+detail) | 12.2% | 234 m | 180 m | 155 | 17.4% |
| M1 macro-only | 37.2% | 1025 m | 340 m | 4 | 43.9% |

Detail 0.45 режет макро-массу на мелкие 3D-компоненты; macro-only
показывает саму макро-массу (4 крупных компоненты). Это разделение
ответственности: макро задаёт объём/размещение, detail — внутреннюю
структуру. Визуальная масса остаётся связной.

## H. Silhouette Uniqueness

Точные периоды, mapped OD 0.25, низкая камера, seed 1337/4242:

| Variant | 4.545 IoU/OD | 9.091 IoU/OD | 13.636 IoU/OD |
|---|---:|---:|---:|
| R-only | 1.000/1.000 | 1.000/1.000 | 1.000/1.000 |
| A w0.40 | 0.790/0.626 | 0.783/0.595 | — |
| B a0.18 | 0.698/0.884 | 0.633/0.773 | — |
| M1 (0.45) | 0.134/0.184 | 0.298/0.528 | 0.022/−0.061 |
| M1 seed 4242 | 0.194/0.301 | 0.523/0.601 | 0.002/−0.054 |
| M1 seed 7 | 0.115/0.035 | — | — |
| M1 seed 99 | 0.003/−0.084 | — | — |
| M1 seed 2026 | 0.047/−0.049 | — | — |
| Macro only | 0.286/−0.346 | 0.460/0.489 | 0.066/−0.682 |
| Overcast R | 1.000 (3030) | 1.000 (6061) | 1.000 (9091) |
| Overcast M1 | 0.387/0.488 (3030) | 0.211/0.234 (6061) | 0.245/0.315 (9091) |

**U — PASS WITH NOTES.** На основном 4.545-км периоде M1 ломает копию
у всех пяти seed (IoU 0.003–0.194); R даёт 1.000, B seed 4242 — 0.942.
На 9.09 км seed 4242 остаётся 0.523/0.601 (частичное совпадение
крупных масс); на 13.6 км — 0.002. На 20/30/40/60 км M1 даёт
IoU 0.010–0.184 и OD −0.15…0.40, R — 0.611–0.717. Overcast даёт
0.387/0.488 на своём периоде. Dense-core maxima частично совпадают
(0.62 на 4.545 seed 4242, 0.76–0.80 Overcast) из-за сохранённого
периодического detail; macro-only maxima 0.047 показывает, что само
размещение уникально. Это ограничение detail, а не macro; оно вынесено
в Notes.

## I. Large-Distance Panoramas

0–60 км, Sunset, 1280×720 half. R-ряд визуально сохраняет одну и ту же
композицию крупных масс на всех дистанциях (periodic field + weather
map); M1-ряд меняет массы, размеры и просветы на каждой дистанции.
Sheet: `evidence/sheets/panorama_r_m1_0_60km.png`.

## J. Temporal Stability

6 детерминированных 200-кадровых level-flight последовательностей:
M1 X, M1 Z, M1 static+wind, M1 X+wind, M1 X на 60 км, R X control.
Постоянные height/pitch/FOV; wind=0 кроме wind-тестов.

| Sequence | Mean diff | Max diff | Period cross | Macro cross |
|---|---:|---:|---:|---:|
| M1 X | 1.203 | 2.189 | 1.533 (1.27×) | 1.368 (1.14×) |
| M1 Z | 0.701 | 1.961 | 1.917 (2.73×) | 0.748 (1.07×) |
| M1 static + wind | 0.0075 | 0.0081 | — | — |
| M1 X + wind | 1.184 | 1.984 | 1.488 (1.26×) | 1.533 (1.30×) |
| M1 X 60 km | 2.464 | 3.338 | 2.432 (0.99×) | — |
| R X control | 2.104 | 2.650 | 2.305 (1.10×) | 2.207 (1.05×) |

M1 Z имеет 2.7× mean на 4.545-км пересечении, но это **плавный**
профиль 135–142 (1.70→1.96→1.67→1.15), а не одиночный скачок: через
кадр проходит крупная масса. Ни на одном пересечении нет
однокадрового разрыва/смены PSO. 60-км полёт стабилен; статическая
камера + ветер даёт медленный (79 м за 200 кадров) непрерывный сдвиг.
Temporal PASS с оговоркой о малом ходе wind-теста.

## K. Storm S2new Compatibility

S2new baseline (A02c) не изменён. Отдельный экспериментальный
`S2new + macro`:

| Variant | Occupied cols (ρ>0.2) | Mean thick | Std | p95 | Components | Largest |
|---|---:|---:|---:|---:|---:|---:|
| S2new baseline | 56.0% | 1469 m | 758 m | 2692 m | 144 | 85.1% |
| S2new + macro | **14.6%** | 1236 m | 884 m | 2461 m | 76 | 91.8% |

Macro гейтит уже разреженное Storm-поле вместе с 5-км storm gap и
coverage 0.55: occupied columns падают с 56% до 14.6%. Визуально
S2new+macro — крупная масса + большой просвет + редкие обрывки; это
**геометрическая регрессия**, а не сохранение S2new. Поэтому:

- **S2new baseline: сохранён, PASS WITH NOTES (A02c), M0 byte-identical.**
- **S2new + macro: BLOCKED (integration).** В принятый Reference не
  включается. Проблема зафиксирована отдельно: coverage 56%→14.6%,
  а selftest `wind translation invariance` для этой комбинации
  **FAIL** (M1/m1d0 проходят). Это дополнительный симптом того, что
  macro-гейт нельзя механически накладывать на уже разреженное
  Storm-поле.

## L. GPU Performance

RTX 5070 Laptop, 1080p half, shadow 6, 128 steps, n=3, 60 frames,
warmup 20. Median cloud ms (Clear / Partly / Overcast / Storm / Sunset):

| Model | Clear | Partly | Overcast | Storm | Sunset |
|---|---:|---:|---:|---:|---:|
| Legacy | 4.459 | 5.415 | 5.309 | 2.740 | 5.688 |
| R-only | 5.033 | 4.281 | 3.289 | 5.602 | 4.212 |
| A w0.40 | 5.900 | 4.787 | 3.875 | 3.770 | 4.311 |
| B a0.18 | 5.400 | 5.052 | 4.373 | 5.278 | 4.614 |
| M1 | 3.485 | 3.406 | 2.782 | 3.984 | 2.779 |
| M1 macro-only | 2.960 | 3.010 | 2.675 | 3.874 | 2.899 |
| S2new | — | — | — | 3.074 | — |
| S2new + macro | — | — | — | **6.633** | — |

Storm step sweep, n=1 (64/96/128): Legacy 1.456/2.064/2.740;
R 2.852/4.130/5.602; M1 1.964/2.919/3.984; M1d0 1.934/2.863/3.874.

**M1 дешевле R в этих прогонах не потому, что macro быстрее.**
Macro-гейт удаляет крупные объёмы, ray march раньше пропускает
shadow-работу для пустых шагов; это меньшая облачность, а не
алгоритмическое ускорение. Честная оценка ALU-стоимости macro:
S2new+macro = **6.633 ms** против S2new = **3.074 ms** при **меньшем**
coverage (56%→14.6%) — macro+storm envelope стоит ~3.5 ms и не
оправдан. M1 на 16 hash/sample остаётся ALU-heavy; потенциальные
точки оптимизации — 2D macro texture / 1 octave / coarser shadow
macro. Стоимость зафиксирована; смешивать её с visual gate нельзя.

## M. DX12 Regression

- Release/Debug build: PASS, no C/X/LNK warnings.
- CTest Release/Debug: 1/1 PASS.
- Source checks Python/PowerShell: PASS.
- fxc: all reference permutations (macro branch included): PASS.
- Hardware selftest: Legacy / R / A / B / S2old / S2new /
  M1 / M1 macro-only: **PASS**. `S2new + macro` FAILS
  `wind translation invariance` (rejected integration; see K) —
  same failure under WARP; not counted as an accepted variant.
- WARP functional accepted variants: PASS.
- WARP + GBV: exit 2 (documented hardware-only limitation).
- Release GBV Legacy / R / A / B / S2old / S2new / M1 / M1d0: 0/0.
- Debug GBV M1: 0/0.
- 120 frames + resize M1 (Release/Debug): 0/0.
- GBV M1 Float32 volume probe: 0/0; GBV M1 OD probe: 0/0.
- Negative injected ERROR: FAIL exit 1 (expected).
- F2/F5/Pause M1: slices↔clouds, clear→partly, pause/resume,
  0 errors/0 warnings, exit 0.
- Shutdown: non-device objects 0.
- Pixel identity: Legacy/R/A/B/S2old/S2new byte-identical to M0/A02c.
- Final EXE 6E593B6E vs capture EXE 6AFBEAF9: selftest-only changes;
  a control OD station is **bit-identical** (.bin SHA256
  `B4383F26...`). Data captured with 6AFBEAF9 is valid for the final
  build.

**T — Technical: PASS** for the accepted variants; the rejected
S2new+macro variant is documented separately.

## N. Known Limitations

1. M1 detail 0.45 retained the periodic fine structure; dense cores
   partially repeat where macro masses overlap (maxima 0.62 at 4.545
   seed 4242; 0.76–0.80 Overcast). Macro-only placement is unique
   (0.047); this is a detail-vs-macro trade, documented.
2. 9.09 km seed 4242 M1 IoU 0.523/OD 0.601 — partial large-mass
   overlap; 13.6 km is 0.002. The 4.545 primary period is strong.
3. M1 is soft/blurry at the current 128³ base + lighting; no
   photo-real cauliflower. Not a uniqueness result.
4. M1 16 km volume at 0.2 is fragmented (155 components) because the
   periodic detail cuts the macro mass; macro-only has 4 components.
   The visual mass is connected at lower density.
5. S2new+macro is a failed integration (56%→14.6% columns; selftest
   wind invariance FAIL); S2new baseline remains the Storm reference.
6. No M2, N4, H5/H6, V2.4, lightning, surge or game DX12.

## O. Verdict

- **U — World-Space Uniqueness: PASS WITH NOTES.** Exact R copy 1.000
  is broken to 0.003–0.194 at 4.545 km across five seeds; A/B remain
  at 0.70–0.94 (B seed 4242 near-copy). Macro-only 0.047. Notes:
  9.09 km seed 4242 0.523 and Overcast dense-core maxima 0.8.
- **M — Macrostructure Quality: PASS WITH NOTES.** Large masses with
  varied sizes/gaps; no new grid; 0–60 km panorama diverse; detail
  soft.
- **S — Storm Compatibility: BLOCKED (integration).** S2new baseline
  preserved; S2new+macro regresses occupied columns 56%→14.6%.
- **T — Technical: PASS (accepted variants).** Release/Debug,
  CTest, source checks, fxc, HW/WARP/GBV, 120 frames + resize,
  M1 volume/OD probes, negative, F2/F5/pause, shutdown, pixel
  identity all pass. `S2new+macro` fails wind invariance and is
  excluded (S BLOCKED).
- **P — Performance: measured; см. L.** M1 net timings are lower
  because the macro removes cloud volume; the ALU cost is visible in
  the rejected S2new+macro (6.63 ms vs 3.07 ms). No free speedup is
  claimed.
- **V2: NOT ACCEPTED.** No self-assigned PASS.

No commit/push/merge.
