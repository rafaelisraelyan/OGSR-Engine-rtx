# V2.3h-A03b — Local Occupancy & Storm Topology

Статус: **P0 PASS; C BLOCKED (slab-like masses, seed spread);
U BLOCKED (near-saturation, spatial non-stationarity); S PASS
(candidate); W PASS WITH NOTES; T PASS (accepted variants); P measured.
V2 NOT ACCEPTED.**
Legacy V2.3 — default. A03 M1 и Storm S2new сохранены byte-identical.

Ветка: `feature/dx12-volumetric-clouds`, HEAD
`42b8124e538ef4e40229a155203c133ca412756c`.

## A. Git State & Backup

- Snapshot: `D:\Coding Project\OGSR_BACKUP_BEFORE_V23H_A03B_2026-10-29`,
  MANIFEST SHA256
  `98A589967F26E75C6AB88DAD3EAA12509E4A55CC80EA67C2DB237C53732FBD1E`
  (218/218; restore byte-identical; 95 untracked).
- F01 source baseline: `5a80bb4b…` (116 файлов).
- A03b source: 122 файла, LF; точный hash и patch — в
  `AUDIT_BASELINE.txt` пакета.
- Patch F01→A03b: 16 файлов, +910/−62.
- Git: uncommitted, 0 staged; commit/push/merge/reset/clean не выполнялись.

## B. Baseline Identity

C0 после A03b-сборки: Legacy, RB, R, A, B, A03 M1, S2old, S2new и M1 OD
**9/9 byte-identical** к A03a C0. A03b-флаги по умолчанию off;
A03/A03a permutations не изменены.

Release EXE: `7D995A02…`; Debug EXE: `25494B79…`.

## C. P0 Probe Integrity

F01 установил две причины расхождения 50.19% vs 14.58%:

1. `DensityColumns`/`StormBoundary` компилировались из A03-шейдера, а
   `DensityVolume` — из A03a; флаги A03a в CB игнорировались.
2. Аудит сравнил volume c3 (origin −8000/−9200) с columns c4
   (origin −8366/−5944).

P0-фикс сохранён: отдельные A03a/A03b PSO для всех трёх entry points,
metadata содержит `shader_permutation`, `columns_crosscheck`,
`macro_local_occupancy`, `storm_local_occupancy`.
Shared-coordinate F01: S2new/fixed/analytic max abs **0.0**.
A03b shared probe: volume 35.11% vs columns 35.11%, max abs **0.0**;
native 35.11% vs 35.33% (+0.22 pp).

**P0 — PASS.**

## D. Shared-Coordinate Verification

`--columns-crosscheck=1` с A03b: volume и columns на одной сетке дают
max abs **0.0** (оба 35.11%). Native grids: 35.11% vs 35.33% (+0.22 pp),
остаток — XZ-дискретизация.

## E. Dense/Column Consistency

Native grids: c3 fixed 60.09/60.29, c4 fixed 50.19/50.74, S2new
55.99/55.99. A03b shared 35.11/35.11 (0.0), native 35.11/35.33
(+0.22 pp). Остаток — XZ-дискретизация.

## F. Existing Occupancy Diagnosis

A03a C3-1: coverage не управляет mass, detail-множитель и jitter
насыщают поле; W1 seed 9001 Clear 59.6%; W2 seed-robust, но плиты.
F01 — C BLOCKED.

A03b diagnosis: низкочастотный macro mean зависит от seed (4242 0.598,
9001 0.423), поэтому абсолютный macro-threshold даёт seed-зависимый
Clear. Решение: high-pass macro anomaly, local occupancy 2.4/1.0 км,
true local mean по 4 taps и presence на фиксированной плоскости.
Остаточный seed-разброс всё ещё превышает визуальный контракт Clear.

## G. Weather Occupancy Contract

| Слой | Формула | Ответственность |
|---|---|---|
| A. Macro placement | `A = N(p; 6000, φA) − N(p; 12000, φM)` | Слабая крупная организация |
| B. Local occupancy | `L = 0.65·N(p; 2400, ψ1) + 0.35·N(p; 1000, ψ2)`, `F = L − LocalMean4(L) + 0.12·A` | Локальные группы и просветы |
| C. Weather | `t = 0.160·Q(1 − coverage^1.2)` | Порог присутствия |
| D. Detail | `D = smoothstep(shapeLow, shapeHigh, NoiseVolume.R)` | Внутренняя неоднородность |
| E. Density | `shape = smoothstep(t−0.05, t+0.05, F)·lerp(0.85,1,D)` | Толщина/extinction |

`LocalMean4` — 4 taps ±7.2 км. Presence — на фиксированной высоте 2000 м
(column decision), плотность остаётся 3D: lens-профиль 0.22/0.78,
column top/bottom ±1200/±1000 м, Perlin-Worley body. 0.160 — одна
глобальная калибровка; `coverage^1.2` — единая weather-кривая. Никаких
per-seed таблиц.

## H. L1/L2 Results

Volume occupied columns (ρ>0.2), 16 км, финальная модель, 6 seed:

| Preset | 1337 | 2027 | 4242 | 9001 | 9529 | 99 |
|---|---:|---:|---:|---:|---:|---:|
| Clear (0.42) | 32.6 | 32.5 | 43.1 | 32.3 | 39.7 | 42.7 |
| Partly (0.62) | 56.0 | 53.5 | 57.8 | 50.4 | 61.2 | 64.9 |
| Overcast (0.68) | 63.7 | 59.2 | 62.4 | 56.7 | 65.9 | 69.6 |
| Sunset (0.45) | 36.1 | 35.8 | 45.8 | 34.8 | 43.7 | 46.1 |

Все проценты — occupied columns. Clear < Partly < Overcast выполняется
для каждого seed. Разброс: Clear 32.3–43.1, Partly 50.4–64.9,
Overcast 56.7–69.6, Sunset 34.8–46.1. Clear seed 4242/99 (43%) визуально
образует крупную плоскую массу; Overcast остаётся плотным слоем.
Поэтому C **BLOCKED**, несмотря на монотонность и отсутствие полного
насыщения.

Coverage sweep (Sunset, seed 1337, fixed camera): occupied columns
13.2 / 22.0 / 31.1 / 41.6 / 53.3 / 67.1 / 80.7% при coverage
0.20 / 0.30 / 0.40 / 0.50 / 0.60 / 0.70 / 0.80 — строго монотонно.

noiseSeed independence: macro, local и occupancy probes при
noiseSeed 1337/4242/9001 **bit-identical** (max abs 0.0); меняется
только detail-канал. Это подтверждает разделение ответственности.

## I. MacroSeed Independence

L3: 1337, 4242, 99, 2027, 9001, 9529. Все шесть seed дают разные
occupancy/density поля. Alias 1337/9529 не возвращается:
occupancy max abs 1.0, local 0.728, macro 0.728.
noiseSeed 1337/4242/9001: occupancy/macro/local bit-identical (0.0),
detail меняется — seed влияет на структуру, но не на placement.

## J. Uniqueness Regression

Float32 OD station pairs (Sunset 3 seed, Overcast 1 seed; d0 против
4545/9090/20/40/60 км и 3030/6060/9090/20/40 км):

| Station | seed | occupied | mean OD | IoU@1/3 | OD corr |
|---|---|---:|---:|---:|---:|
| Sunset d0 | 1337 | 26.7% | 1.01 | — | — |
| Sunset d4545 | 1337 | **93.8%** | **4.74** | 0.220 | −0.470 |
| Sunset d9090 | 1337 | 45.1% | 2.06 | 0.315 | 0.350 |
| Sunset d20000 | 9001 | **91.7%** | **4.54** | 0.757 | −0.294 |
| Sunset d60000 | 2027 | 47.2% | 2.16 | 0.600 | 0.569 |
| Overcast d3030 | 1337 | **99.7%** | **5.46** | 0.624 | −0.060 |
| Overcast d6060 | 1337 | **99.5%** | **5.32** | 0.622 | −0.139 |

3D density d0 vs d4545 (Sunset 1337): density corr 0.115, voxel IoU
0.107, occupancy corr −0.092, occupancy IoU 0.191, max abs 1.0.
Это **не** возврат старого тайла (OD corr отрицательна), но почти
полное заполнение на отдельных станциях и сильная spatial
non-stationarity. High IoU при 99% occupancy не является
доказательством уникальности. **U — BLOCKED.**

0–60 км panorama (16 км tiles, occupancy probe):

| Preset | mean | p10 | p50 | p90 | min | max |
|---|---:|---:|---:|---:|---:|---:|
| Clear 1337 | 40.6% | 34.5 | 40.7 | 46.3 | 27.9 | 54.6 |
| Clear 9001 | 40.5% | 34.2 | 40.2 | 47.1 | 27.9 | 55.7 |
| Partly 1337 | 59.8% | 55.8 | 59.8 | 64.7 | 49.1 | 70.8 |
| Overcast 1337 | 65.7% | 62.0 | 65.3 | 70.0 | 56.7 | 75.4 |
| Overcast 9001 | 64.8% | 57.8 | 64.7 | 70.8 | 53.5 | 79.3 |
| Sunset 1337 | 43.5% | 37.6 | 43.8 | 48.8 | 30.3 | 57.0 |

Large-area Clear mean 40.6% (не 30%), отдельные 16 км tiles до 54.6%.
Это природная неоднородность, но она усиливает C/U BLOCKED.

## J2. Temporal & Wind

Level flight, 200 frames, 640×360, 96 steps:

| Sequence | mean diff | max diff | old period cross | macro cross |
|---|---:|---:|---:|---:|
| X, wind off | 0.4216 | 0.9213 | 0.373 (0.885× mean) | 0.590 (1.40×) |
| X, wind on | 0.4154 | 0.9038 | 0.354 (0.851×) | 0.588 (1.41×) |
| Z, wind off | 0.4615 | 1.4029 | 0.332 (0.720×) | 0.113 (0.244×) |
| static, wind on | 0.0218 | 0.0224 | — | — |
| X at 60 км | 0.4619 | 1.7363 | 0.076 (0.164×) | — |

Нет всплеска на старом периоде 4545 м; X/Z и 60 км стабильны.
На границе 6 км macro lattice есть шаг ~1.4× mean.

Точная wind translation: volume t0 vs t1 с origin, сдвинутым на 24 м
вдоль wind direction. Density max abs **8.34e-4** (mean 9.3e-7),
occupancy max abs **1.18e-4**, detail max 2.44e-3. Контракт выполнен
с точностью float. **W — PASS WITH NOTES** (macro-boundary step 1.4×).

## K. Storm S2new Safety

S2new byte-identical. A03b candidate (`--storm-local=1`) меняет только
оболочку ρ<0.5; ядро ρ≥0.5 не модифицируется.

| Metric (ρ>0.2) | S2new | A03b candidate |
|---|---:|---:|
| Occupied columns | 56.0% | 53.3% |
| Occupied voxels | 12.7% | 10.0% |
| Largest 3D component | 85.1% | 84.9% |
| Components | 144 | 156 |
| Mean thickness | 1469 m | 1276 m |

При ρ>0.5 candidate bit-identical S2new: vox 5.0%, columns 43.2%,
largest 72.8%, thickness 838 m. **S — PASS** (topology preserved;
shell thickness −14%).

## L. GPU Performance

1080p Half, 128/6, 3 прогона, median of run medians:

| Variant | Sunset | Overcast |
|---|---:|---:|
| W0 (C3-1) | 3.844 ms | 3.787 ms |
| W1 (quantile) | 3.910 ms | 3.912 ms |
| A03b 64 steps | 5.701 ms | 5.020 ms |
| A03b 96 steps | 8.525 ms | 7.547 ms |
| A03b 128 steps | **10.723 ms** | **9.752 ms** |

A03b дороже W0 в ~2.6× на 128 steps: 2 local octave + 4 mean taps +
macro anomaly. Стоимость измерена, не замаскирована меньшим coverage.

Контрольные варианты (128 steps, 3 прогона): M1 Sunset 6.636 ms;
S2new Storm 6.342 ms; S2local Storm 9.159 ms. S2local дороже S2new на
~44% из-за local field на оболочке.

## M. DX12 Regression

- Release/Debug build PASS; CTest Release/Debug 1/1 PASS.
- Source checks Python/PowerShell PASS; fxc PASS.
- HW selftest PASS: Legacy, R, A, B, M1, A03b, S2new, S2local.
- WARP PASS: R, M1, A03b, S2new, S2local.
- Release GBV PASS: Legacy, R, M1, A03b, S2new, S2local 0/0.
- GBV shared probes: S2new 55.76/55.76, fixed 49.78/49.78,
  A03b 35.33/35.33, analytic 50.00/50.00 — max abs **0.0**.
- 120 frames + resize A03b (Release): 0/0.
- Negative injected ERROR: exit 1 (expected).
- Baseline identity: **9/9 byte-identical**.
- Shutdown live objects: 0 (selftest logs).

## N. Known Issues

1. Clear seed 99 42.7% и крупная плоская масса; Overcast 57–70%
   остаётся плотным стратиформным слоем. U добавляет near-saturation:
   Sunset d4545 93.8%, Overcast d3030 99.7%.
2. Storm candidate теряет ~14% толщины оболочки при сохранении ядра.
3. W2 не переносился; старые A03a columns (14.58%) непригодны.
4. Никаких N4/H5/H6/V2.4/lightning/surge/game DX12.

## O. Verdict

- P0 — PASS.
- C — **BLOCKED**: weather-монотонность и seed-стабильность улучшены
  (Clear 32–43%, Overcast 57–70%), но Clear seed 4242/99 визуально даёт
  крупную плоскую массу, а Overcast — плотный слой. Нужна отдельная
  region-based 3D архитектура (L1-B), а не дальнейший подбор порогов.
- U — **BLOCKED**: near-saturation и spatial non-stationarity
  (Sunset d4545 93.8%, Overcast d3030 99.7%); raw Float32 pairs
  приложены.
- S — PASS (candidate).
- W — **PASS WITH NOTES**: exact wind pair density 8.34e-4,
  occupancy 1.18e-4; old period seam 0.885× mean; macro boundary
  step 1.4× mean.
- T — **PASS** для принятых вариантов: Release/Debug, CTest,
  source/fxc, HW/WARP/GBV, shared probes, 120+resize, negative, identity
  9/9.
- P — measured: W0 3.84/3.79 ms, W1 3.91/3.91 ms, A03b 10.72/9.75 ms
  (128 steps, Sunset/Overcast); controls M1 6.64, S2new 6.34,
  S2local 9.16 ms.
- **V2 — NOT ACCEPTED.**
