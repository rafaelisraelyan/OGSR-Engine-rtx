# V2.3h-A03a-F01 — Probe Consistency & Weather Occupancy Contract

Статус: **P0 PASS; C BLOCKED; U BLOCKED; S BLOCKED; T PASS
(принятые варианты); P measured. V2 NOT ACCEPTED.**
Legacy V2.3 — default; A03 M1 и Storm S2new сохранены byte-identical.
F01 — диагностический checkpoint, не новая художественная система.

Ветка: `feature/dx12-volumetric-clouds`, HEAD
`42b8124e538ef4e40229a155203c133ca412756c`.

## A. Git State & Backup

- F01 snapshot: `D:\Coding Project\OGSR_BACKUP_BEFORE_V23H_A03A_F01_2026-10-29`,
  MANIFEST SHA256
  `CF083C498CB67DCE23FAE0F0C9C5B54EEB32E4D7B3926BB04AD17EF39F6E6040`
  (258/258; restore byte-identical; 135 untracked files).
- A03a package SHA256:
  `4D06AA3FA289B5A3D362EFFE0BD070B0B63EC5B5E514C94FF0A9F127E40D7BAE`.
- F01 source/patch hashes: см. `AUDIT_BASELINE.txt` в пакете.
- Git state: uncommitted, 0 staged; no commit/push/merge/reset/clean.

## B. Baseline Identity

C0 повторён на F01-сборке: Legacy, RB, R, A, B, A03 M1, S2old,
S2new и M1 OD **9/9 byte-identical** к A03a C0 (EXE
`9E73E196…`). Все новые флаги (F01) по умолчанию off; A03/A03a PSO
для flags-off не изменены.

## C. P0 Probe Root Cause

Расхождение A03a (S2new+fixed: DensityVolume 50.19% vs
DensityColumns 14.58%) объясняется двумя независимыми ошибками:

1. **PSO permutation mismatch.** `DensityVolume` компилировался из
   `CloudDensityVolumeA03a.hlsl` (с `OGSR_A03A`), а `StormBoundary` и
   `DensityColumns` — из `CloudDensityVolume.hlsl` (A03, без
   `OGSR_A03A`). CB нёс флаги `macro_coverage_preserve=1`,
   `storm_macro_gap_replace=1`, но columns-shader их игнорировал:
   metadata утверждала A03a, исполнялся A03.
2. **Camera/origin mismatch в аудите.** `c3_storm_fixed` снят с
   origin `[-8000,-100,-9200]`, `c4/s2new_fixed` — с
   `[-8366,-100,-5944]`. Таблица 50.19/14.58 смешала volume одной
   камеры и columns другой.

### До / после

| Probe | Volume occupied | Columns occupied | Δ |
|---|---:|---:|---:|
| c3 fixed, old A03 columns | 60.09% | 29.70% | −30.39 |
| c3 fixed, A03a columns | 60.09% | **60.29%** | +0.20 |
| c4 fixed, old A03 columns | 50.19% | 14.58% | −35.61 |
| c4 fixed, A03a columns | 50.19% | **50.74%** | +0.55 |
| c4 S2new, A03 columns | 55.99% | 55.99% | 0.00 |
| c4 S2new, A03a columns | 55.99% | 55.99% | 0.00 |

S2new не был затронут (direct path), поэтому всегда согласовался.

### Исправление

- Новые PSO: `pipelineStateStormBoundaryA03a_`,
  `pipelineStateDensityColumnsA03a_` из
  `CloudDensityVolumeA03a.hlsl`.
- `DispatchVolume` выбирает A03a-PSO для mode 0/1/2 при `a03a`.
- Metadata теперь пишет `shader_permutation: "A03a"|"A03"` и
  `columns_crosscheck`; старые dumps этого поля не имели.

## D. Shared-Coordinate Verification

Новый режим `--columns-crosscheck=1` ставит columns на **точно ту же**
сетку, что volume: `columns=resX`, `packets=resY/4`,
`yStart=origin.y`, `yStep=cellY`, `cellXZ=volume cell`. Оба пути
сэмплируют одни и те же world-space центры.

| Test | Volume vs Columns max abs |
|---|---:|
| S2new c3 shared | 0.0 |
| S2new c4 shared | 0.0 |
| fixed c3 shared | 0.0 |
| fixed c4 shared | 0.0 |
| analytic constant 0.5 | 0.0 |
| analytic X stripe | 0.0 |

Аналитический режим `--density-test=1|2` возвращает известное поле
(0.5 / полоса по X) из общего density evaluator; оба GPU-пути дают
bit-identical значения. Это исключает stale PSO, неверный CBV,
descriptor binding и coordinate mismatch.

**P0 — PASS.**

## E. Dense/Column Consistency

Native grids (128×96×128 vs 48×48×1024) после P0-фикса согласуются:
c3 fixed 60.09% vs 60.29% (+0.20 pp), c4 fixed 50.19% vs 50.74%
(+0.55 pp), S2new 0.00/0.00. Остаточная разница — дискретизация
XZ 125 м vs 333 м, не shader. На shared grid разница ровно 0.

## F. Existing Occupancy Diagnosis

A03a C3-1 (seed 1337, Sunset) 12.13%→41.22% колонок; причина —
detail-множитель до coverage-cut + jitter bias (A03a §D).
Но по пресетам C3-1 даёт почти одинаковые 56–62% и на seed
2027/9001 — серую плиту. Это исходная проблема F01.

## G. Weather Occupancy Contract

```text
macroValue = ContinuousMacro(worldPosition, macroSeed)   // 0..1
weatherCut = LocalMeanOrQuantile(coverage)               // before saturation
occupancy  = smoothstep(weatherCut, weatherCut + softness,
                        macroValue + edgeJitter)
shape      = occupancy * detailModulation
density    = shape * heightEnvelope * stormGap * densityScale
```

- Macro — размещение; weather — какая часть непрерывного macro становится
  облаком; detail — локальная неоднородность; density/extinction — толщина.
- W1: `weatherCut = 0.5 + σ·Q(1-coverage) − 0.33·softness`, σ=0.15.
- W2: `weatherCut = meanN1(±6 км X/Z) + σ·Q(1-coverage) − 0.33·softness`.
- Coverage remap после smoothstep в W1/W2 **не применяется**.
- Tolerance/seed/камера не подстраиваются.

## H. W1/W2 Results

Volume occupied columns (ρ>0.2), 16 км, три seed:

| Preset | W1 1337 | W1 2027 | W1 9001 | W2 1337 | W2 2027 | W2 9001 |
|---|---:|---:|---:|---:|---:|---:|
| Clear | 46.0% | 33.5% | 59.6% | 54.5% | 52.4% | 47.1% |
| Partly | 65.9% | 55.0% | 80.1% | 73.2% | 72.3% | 73.3% |
| Overcast | 71.0% | 60.8% | 83.9% | 77.8% | 77.4% | 78.8% |
| Sunset | 48.0% | 34.4% | 62.0% | 55.0% | 53.0% | 49.6% |
| Storm | 41.9% | 36.6% | 52.5% | 45.2% | — | — |

W1: монотонность Clear < Partly < Overcast выполнена, но seed 9001
Clear 59.6% и Sunset 62% — слишком облачно; визуально крупные массы
ещё читаются, но Clear не «преимущественно ясный».
W2: seed-разброс резко снижен (Clear 47–55%, Overcast 77–79%), но
визуальный гейт **провален**: Clear/Overcast превращаются в крупные
однородные плиты (см. `evidence/visual/w2_*`). Порог локального
среднего насыщает массу в 1 на больших площадях.

**C — BLOCKED.** Контракт сформулирован; W1 даёт погодную
дифференциацию, W2 — seed-robust occupancy, но без нового слоя
локальной структуры/occupancy не удаётся одновременно получить
ясный Clear и не-плиточный Overcast.

## I. MacroSeed Independence

P0 fix не меняет seed chain. A03a: detail bit-identical across
macroSeed, macro-only bit-identical across noiseSeed; alias
1337/9529 устранён hash-вариантом. W1/W2 проверены на трёх seed
(1337/2027/9001); W2 occupancy почти не зависит от seed, W1 зависит
(что и было источником плиты).

## J. Uniqueness Regression

Float32 OD, Sunset coverage 0.45, d0 vs 4545/9090, OD threshold 0.333:

| Variant | Seed | d0 occupied | mean OD | IoU 4545 | IoU 9090 |
|---|---|---:|---:|---:|---:|
| W1 quantile | 1337 | 26.0% | 0.83 | 0.176 | 0.348 |
| W1 quantile | 2027 | 82.1% | 3.83 | 0.009 | 0.790 |
| W1 quantile | 9001 | **98.6%** | **5.10** | **0.986** | **0.986** |
| W2 local | 1337 | 77.6% | 3.81 | 0.767 | 0.646 |
| W2 local | 2027 | 94.0% | 4.94 | 0.099 | 0.940 |
| W2 local | 9001 | 38.2% | 1.32 | 0.357 | 0.381 |

Для seed 9001 W1 и seed 2027 W2 маска почти полностью насыщена
(mean OD 5.1/4.9), поэтому IoU 0.94–0.99 отражает не копирование
тайла, а отсутствие просветов. Это всё равно провал визуального
U-контракта: Sunset становится сплошным. **U — BLOCKED.**

## K. Storm S2new Safety

После P0-фикса Storm-метрики пересчитаны (native volume + columns,
камера c4):

| Variant | Occupied cols | Mean thick | Std | p95 | Largest comp |
|---|---:|---:|---:|---:|---:|
| S2new | 56.0% | 1469 m | 758 m | 2692 m | 85.1% |
| S2new+fixed | 50.7% | 1306 m | 699 m | 2633 m | **38.0%** |

Потеря occupancy — 5.3 pp, толщина −11%, вертикальный разброс сохранён.
Но **связность**: крупнейшая 3D-компонента 85.1%→38.0% (146 компонент).
S2new — один связанный фронт; fixed — несколько крупных масс.
Это геометрическая регрессия, а не потеря coverage.

- S2new baseline: byte-identical, сохранён.
- S2new+fixed: **S BLOCKED** (connectivity).
- Wind: S2new 0.0078/0.0059 PASS; S2fixed 0.000122 PASS; A03
  S2new+macro 0.0107/0.0156 FAIL (step-edge). Tolerance не менялся.

## L. GPU Performance

1080p Half, 128/6, 3 независимых прогона, per-frame median/p95
(median of run medians), EXE `9E73E196…`:

| Variant | Sunset median | Sunset p95 | Overcast median | Overcast p95 | Sunset occ |
|---|---:|---:|---:|---:|---:|
| W0 (C3-1) | 3.643 | 4.014 | 3.552 | 4.205 | 55.6% |
| W1 (quantile) | 3.725 | 3.970 | 3.644 | 4.133 | 48.0% |
| W2 (local) | **6.387** | 7.370 | **7.389** | 8.499 | 55.0% |

W1 добавляет ~2–3% к W0; W2 (4×n1 taps) стоит ~1.8–2.1× и
визуально хуже (плиты). Сравнение приведено с occupied fraction;
W2 не рекомендуется.

## M. DX12 Regression

- Release/Debug build: PASS; CTest Release/Debug 1/1 PASS.
- Source checks Python/PowerShell: PASS; fxc: PASS.
- HW selftest PASS: Legacy / R / A / B / M1 / S2new.
- WARP: R / M1 / S2new PASS.
- Release GBV: Legacy / R / M1 / S2new 0/0.
- P0 shared-coordinate probe under GBV: S2new 0/0, fixed 0/0,
  analytic stripe 0/0.
- 120 frames + resize W1 (Release): 0/0.
- Negative injected ERROR: exit 1 (expected).
- Pixel identity: **9/9 byte-identical** к A03a C0.
- Shutdown: non-device objects 0.
- Экспериментальные W1/W2 не входят в accepted set (C/U BLOCKED);
  их selftest harness не является гейтом.

**T — PASS для принятых вариантов и P0-диагностики.**

## N. Known Issues

1. P0 fixed the diagnostic PSO permutation; old A03a columns data
   (14.58%) must not be used for coverage conclusions.
2. c3/c4 audit table mixed two cameras; provenance must include origin.
3. W1 monotonic but Clear seed 9001 59.6%; W2 seed-robust but visual
   slabs. C BLOCKED.
4. Storm fixed retains 50.7% columns but connectivity collapses
   (largest 85.1%→38.0%); S BLOCKED.
5. A03 U confound (noiseSeed vs macroSeed) remains from A03a.
6. No new architecture was built; no N4/H5/H6/V2.4/lightning/surge.

## O. Verdict

- **P0 — Probe Consistency: PASS.** Root cause established (PSO
  permutation + camera mismatch), fixed, shared-coordinate max abs 0.0,
  analytic const/stripe 0.0, native grids agree ≤0.55 pp.
- **C — Weather Occupancy: BLOCKED.** W1/W2 contract implemented and
  measured; seed-robust occupancy (W2) still produces slabs.
- **U — World-Space Uniqueness: BLOCKED.** W1 seed 9001 Sunset
  saturated (occ 98.6%, mean OD 5.10, IoU 0.986), W2 seed 2027
  occ 94.0%, IoU 0.94 at 9090; coverage fixes return near-overcast and
  high period IoU. P0/P0 probes themselves are correct.
- **S — Storm Compatibility: BLOCKED.** Coverage preserved (50.7% vs
  56.0%), but connected mass 85.1%→38.0%.
- **T — Technical: PASS (accepted variants).**
- **P — Performance: measured.** W1 +2–3%, W2 ~2× and visually worse.
- **V2: NOT ACCEPTED.** No commit/push/merge.
