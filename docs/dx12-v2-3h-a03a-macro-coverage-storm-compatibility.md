# V2.3h-A03a — Macro Coverage & Storm Compatibility Audit

Статус: **BLOCKED.**
U — PASS WITH NOTES (с оговоркой о confound в A03); C — BLOCKED;
W — PASS; S — BLOCKED; T — PASS для принятых вариантов, Storm-интеграция
исключена; P — measured. **V2 PASS не присваивается.**
Legacy V2.3 остаётся default. S2new остаётся безопасным Storm Reference.
A03 M1 сохранён byte-identical; A03a — отдельные Reference-перестановки,
все новые флаги по умолчанию off.

Ветка: `feature/dx12-volumetric-clouds`, HEAD
`42b8124e538ef4e40229a155203c133ca412756c`.

## A. Git State & Backup

- A03a snapshot: `D:\Coding Project\OGSR_BACKUP_BEFORE_V23H_A03A_2026-10-28`,
  MANIFEST SHA256
  `27C26C5FF45692B851466928A57F00A75C8B6742DA7C185E14FE6A4E8844F9C0`
  (232/232; restore byte-identical; 122 untracked files included).
- A03 package SHA256:
  `3D1524B6D0C84B07AEBB709867999307A7E75FD809FDEEE275CA8169A7E0BCB9`.
- A02c package SHA256:
  `D1935647393EF26C5497EF45B19484769172931C993C73F71E592C847FC17679`.
- Источники A03a и patch vs A03: см. `AUDIT_BASELINE.txt` в пакете.
- Git state: uncommitted, 0 staged; no commit/push/merge/reset/clean.

## B. Baseline Identity

C0 снят до изменений A03a и повторён на финальной сборке. Все девять
контрольных файлов byte-identical:

| Variant | File | Result |
|---|---|---|
| Legacy V2.3 | cumulus below 1280×720 | byte-identical |
| Reference RB | sunset 1280×720 | byte-identical |
| Reference R | sunset 1280×720 | byte-identical |
| Candidate A w0.40 | sunset 1280×720 | byte-identical |
| Candidate B a0.18 | sunset 1280×720 | byte-identical |
| A03 M1 seed 1337 | sunset 1280×720 | byte-identical |
| Storm S2old | storm horizon 640×360 | byte-identical |
| Storm S2new | storm horizon 640×360 | byte-identical |
| M1 OD station | Float32 `.bin` | byte-identical |

A03 shader bytecode не затронут: A03a компилируется в отдельные
`CloudRayMarchReferenceA03a.hlsl`, `CloudRayMarchProbeReferenceA03a.hlsl`,
`CloudDensityVolumeA03a.hlsl`. Control OD station, снятая capture-EXE
`F64B6B10…`, bit-identical на финальной EXE `C3591370…`.

## C. Macro Seed Independence

### Цепочка передачи

```text
--macro-seed N
  -> ReferenceControls::macroSeed (CloudTypes.h)
  -> BuildCloudConstants:
       ReferenceMacro2.w = (N % 8192) / 64          (A03 phase, legacy)
       ReferenceMacro3.y = flags
       ReferenceMacro4.xyz = hash(N) - hash(1337)    (A03a delta)
  -> CloudReferenceDensity.hlsl:
       ReferenceMacroPhaseA/B -> ReferenceMacroField -> ReferenceMacroMass
```

`--seed` (noiseSeed) идёт только в генерацию NoiseVolume и в
`ReferenceDebug.zw` (кандидаты A/B). Macro-путь `ReferenceDebug` не читает.

A03 использовал `(N % 8192)/64`: 8192 различных фаз, точный повтор через
8192; A03a hash-дельта равна нулю при seed 1337 и снимает повтор.
Проверено: macro-only OD A03 seed 1337 и 9529 **bit-identical**
(SHA256 `91452110…`), hash-вариант 1337/9529 различается
(`B6347172…` / `71A39DBE…`). При seed 1337 macro-поля A03 и A03a
**bit-identical** (проверено по `macro_probe` channels 0–3).

### Series A — fixed noiseSeed=1337, macroSeed меняется

- `detail_transfer` bit-identical для всех пяти macroSeed (60 км slices,
  5/5, max diff 0.0).
- `macro_raw` pairwise correlation: −0.204…0.217 (A03 phase) и
  −0.155…0.049 (hash phase) — независимые крупные структуры.
- macro-only cross-seed silhouette IoU 0.126…0.892; высокие значения
  соответствуют насыщенным маскам (mean OD 3.9–5.5).
- Итоговый вариант, period IoU при 4545 м (OD 0.333 / quantile-45%):

| macroSeed | IoU 4545 | IoU q45 | mean OD d0 | occupied d0 |
|---|---:|---:|---:|---:|
| 1337 | 0.391 | 0.350 | 2.03 | 47.5% |
| 4242 | 0.763 | 0.441 | 3.60 | 79.8% |
| 99 | 0.316 | 0.327 | 2.32 | 55.1% |
| 2027 | 0.897 | 0.549 | 5.42 | 100.0% |
| 9001 | 0.094 | 0.066 | 3.56 | 71.8% |

Высокий IoU у 2027/4242 — следствие почти полностью закрытого неба
(OD-маска насыщена), а не точного копирования: 60 км macro raw
autocorrelation на 4545 м = 0.30–0.39, без пика на периоде.

### Series B — fixed macroSeed=1337, noiseSeed меняется

Macro-only OD maps при noiseSeed 1337/4242/99/2027/9001 **bit-identical**
на d0 и d4545 (SHA256 совпадает, 5/5). Это доказывает, что macro-поле не
зависит от периодической detail-текстуры.

### Повторяемость, NaN/Inf, сетка

- `rep` vs `rep2` (одинаковые параметры): bit-identical, mean |ΔOD| = 0.
- Все 97 OD-станций и все volume probes: finite, NaN/Inf = 0.
- 60 км macro raw autocorrelation: 0.302 (lag 38.79 cells) для A03 phase,
  0.391 для hash phase; strongest lag = 2 cells, без пика на 4545 м.
- Новый сетки/швов не обнаружено; detail bit-identical across macroSeed.

### U — PASS WITH NOTES (с существенной оговоркой)

A03 U был измерен на пяти **noiseSeed** при фиксированном macroSeed=1337;
это подтверждено аудитом. A03a впервые измеряет пять **macroSeed**:
крупные массы не копируют NoiseVolume (autocorrelation 0.30, нет пика),
но при 6-км macro scale период 4.545 км сравним с корреляционной длиной,
поэтому у части seed silhouette IoU 0.44–0.55 (и 0.90 на насыщенной
OD-маске). Это не точный повтор, но и не полный разрыв. Формально
**U PASS WITH NOTES**, notes сильные: A03-значения 0.003–0.194 были
получены variation noiseSeed, а не macroSeed.

## D. Macro Coverage Diagnosis

Float32 16 км volume, Sunset, coverage 0.45, macroSeed 1337, ρ > 0.2:

| Variant | Occupied cols | Exact-zero cols | Shaped cols | Mean thickness | 3D vox | 3D comp | Largest |
|---|---:|---:|---:|---:|---:|---:|---:|
| A03 M1 | 12.13% | 81.0% | 24.50% | 4.56 | 0.006 | 155 | 17.4% |
| Macro-only | 37.16% | 59.3% | 51.94% | 22.68 | 0.088 | 4 | 43.9% |
| C3-1 | 41.22% | 55.3% | 59.17% | 16.40 | 0.070 | 122 | 38.7% |
| C3-1b (+mean comp) | 42.66% | 55.0% | 60.35% | 16.74 | 0.074 | 108 | 38.7% |

Причины, изолированные в Float32:

1. `detail_transfer = smoothstep(0.58, 0.88, R)` имеет mean **0.047**,
   p90 **0.161**, p99 0.693. Detail-множитель `lerp(0.55, 1, detail)`
   в среднем **0.572** и равен 0.55 в ~90% объёма.
2. A03 применяет множитель **до** coverage remap. Порог remap
   `1 - coverage = 0.55`, поэтому при detail=0 `mass * 0.55 <= 0.55`:
   detail удаляет колонку целиком. 13.2% всех вокселей имеют
   `mass > 0.55`, но `mass * multiplier <= 0.55`; это 75% всех
   macro-occupied вокселей.
3. Edge jitter `(detail - 0.5) * 0.15` имеет mean **−0.068** (detail
   распределён около 0.047, не 0.5). Это 68% ширины macro smoothstep
   (0.10) — систематическое уничтожение массы.
4. Height thinning снижает толщину, но не является причиной нулевых
   колонок: exact-zero колонок 81.0% у A03 и 59.3% у macro-only.

`macro_raw` mean в этом 16 км volume = 0.418; на 60 км mean = 0.517.
То есть 12.2% A03 — комбинация transfer-эффекта и локальной
низкочастотной реализации, а не только transfer.

## E. Coverage Correction

C3-1 (Reference-only, default off):

```text
jitter = (detail - 0.05) * edgeJitter          // zero-mean
mass   = smoothstep(0.42, 0.52, macro + jitter)
massC  = saturate((mass - (1-coverage)) / coverage)   // before detail
shape  = massC * lerp(0.55, 1, detail)
```

Coverage remap перенесён на macro-массу **до** detail; detail больше не
решает, существует ли колонка. C3-1b дополнительно делит массу на
средний detail-множитель `1 - w*0.95`.

Результат seed 1337: occupied 12.1% → 41.2% (C3-1) / 42.7% (C3-1b);
exact-zero 81.0% → 55.3%; R-only в том же volume 50.5%.
Но по пресетам (объёмная occupied при ρ>0.2):

| Preset | R | A03 | C3-1 |
|---|---:|---:|---:|
| Clear | 50.4% | 19.1% | **56.9%** |
| Partly | 98.5% | 40.7% | **61.4%** |
| Overcast | 99.6% | 41.4% | **61.6%** |
| Sunset | 49.4% | 18.2% | **55.6%** |
| Storm | 67.7% (S2new) | 29.4% | **60.1%** (fixed) |

Storm-строка C3 снята с scene-камеры `clouds_storm_horizon`; C4 ниже
использует A03-storm-камеру `(-366,-2056)`, поэтому 60.1% и 14.6% —
разные ракурсы, а не противоречие. Гейт S — именно C4-камера.

C3-1 даёт почти одинаковые 56–62% для Clear/Partly/Overcast/Sunset:
погодный coverage перестаёт управлять заполнением. Визуально seed
2027 и 9001 превращаются в сплошную серую плиту (mean OD 5.4/3.6,
occupied 100%/72%) — это запрещённая замена тайлинга плитой.

C3-2 (локальная калибровка порога) реализована как coarse-октава
(48 км, вес 0.8; затем 12 км, вес 0.5) + `sigma*Q(1-coverage)`:
- 48 км: seed 1337/99/9001 → 100% occupancy; coarse-октава оказалась
  независимым случайным сдвигом, не локальным средним.
- 12 км: occupancy 70–96%, mean OD 3.5–4.8; лучше, но всё ещё плита.
Калибровка по глобальному quantile без coarse-октавы не успела быть
доведена; текущие результаты **не проходят C**.

**C — BLOCKED.** Минимальный C3-1 восстанавливает заполнение на seed
1337, но не сохраняет погодную дифференциацию и превращает часть seed
в серую плиту. Для исправления нужен новый слой локального occupancy
(погодная карта / многоточечная оценка среднего), а не ограниченная
настройка transfer.

## F. Storm Integration Diagnosis

16 км storm volume, coverage 0.55, macroSeed 1337, ρ > 0.2:

| Variant | Occupied cols | Mean thick | Std | p95 | 3D vox | Comp | Largest |
|---|---:|---:|---:|---:|---:|---:|---:|
| S2new | 56.0% | 1469 m | 758 m | 2692 m | 0.127 | 144 | 85.1% |
| S2new + A03 macro | 14.6% | 1236 m | 884 m | 2461 m | 0.039 | 76 | 91.8% |
| S2new + fixed (gap=macro) | 14.6% | 1236 m | 884 m | 2461 m | 0.147 | 146 | 38.0% |

Причина потери — **двойной sparse gate**: macro mass и 5-км storm gap
умножаются. Замена gap на macro (`--storm-gap=macro`) убирает второе
умножение (voxel fraction 0.039→0.147), но **не создаёт колонки**:
macro-поле в камере Storm само находится в просвете (14.6% колонок),
поэтому occupied columns не меняются, а 3D-масса дробится на 146
компонент (было 76) с largest 38% (было 91.8%). Это не сохранение
S2new; это другой, более фрагментированный Storm.

**S — BLOCKED.** S2new baseline не изменён (M0 byte-identical); интеграция
macro в Storm не принята.

## G. Wind Translation Contract

Контракт по полям:

| Field | Advection | Evidence |
|---|---|---|
| macro noise | XZ − windOffset | field probe |
| detail noise | XZ − windOffset | bit-identical detail |
| storm top/bottom | XZ − windOffset | field probe |
| storm gap | XZ − windOffset | field probe (A03) |
| height/coverage | world Y / constant | no XZ |

Volume probe: origin O at t=0 vs origin O+d at t=1, d = 24 m/s ×
normalize(1,0,0.22), res 64×96×64, explicit world origin. Max |Δ|:

| Variant | Density channels | Macro channels | Detail |
|---|---:|---:|---:|
| M1 | 7.4e−6 | 1.0e−5 | 0.0 |
| M1c 1 s | 3.6e−5 | 1.9e−5 | 0.0 |
| M1c 0.5 s | 2.0e−5 | 1.8e−5 | 0.0 |
| M1c 2 s | 3.1e−5 | 1.4e−5 | 0.0 |
| S2new | 8.3e−6 | — | — |
| S2fixed | 2.9e−5 | 1.3e−5 | 0.0 |

Допуск 0.008 **не менялся**. Selftest: S2new 0.0078/0.0059 PASS;
A03 S2new+macro 0.0107/0.0156 FAIL; fixed 0.000122/0.000122 PASS.
Исходный FAIL — не рассинхронизация координат (поля инвариантны до
float32 roundoff), а чувствительность финального изображения к
шагам ray march на разреженных краях; после устранения двойного gate
поле стало плотнее и разница упала до 0.000122.

**W — PASS** (контракт адвекции; tolerance не повышен).

## H. Storm Visual Compatibility

C4/C5 не дали принятой интеграции (S BLOCKED), поэтому C6 — только
диагностика. S2new baseline и A03 storm views остаются в A03-пакете;
fixed-вариант даёт ту же занятость 14.6%, но более фрагментированные
3D-массы. Отдельный art-direction pass не выполнялся.

## I. Corrected GPU Timings

A03 использовал **maximum из трёх средних** как «median». Правильные
значения из `v23h_a03/timing/timings_raw.csv` (type-7 median):

| A03 group | reported | corrected median |
|---|---:|---:|
| M1 Clear | 3.485 | **3.458** |
| M1 Storm | 3.984 | **3.930** |
| S2new+macro Storm | 6.633 | **6.562** |
| Legacy Clear | 4.459 | **4.289** |

Renderer уже логирует per-frame `median/p95/min/max`; A03a исправил
`CloudProfiler::Percentile` на линейную интерполяцию и парсит
распределение, а не среднее. Новые 1080p Half 128/6 timings
(3 независимых прогона, median of run medians; не maximum):

| Model | Sunset 128 | Overcast 128 | Storm 128 | Storm p95 |
|---|---:|---:|---:|---:|
| Legacy | 5.352 | 5.022 | 2.497 | 3.085 |
| R-only | 4.064 | 3.147 | 4.627 | 5.403 |
| M1 (A03) | 3.183 | 2.841 | 3.828 | 4.360 |
| M1c (C3-1) | 3.591 | 3.350 | 4.202 | 4.898 |
| S2new | — | — | 0.767 | 0.973 |
| S2new+fixed | — | — | 5.650 | 6.182 |

Облачность (Sunset 16 км volume, ρ>0.2): R 49.4%, M1 18.2%,
M1c 55.6%. Поэтому M1 дешевле R не из-за алгоритма, а из-за меньшей
облачности; M1c при большей облачности стоит 3.59/3.35/4.20 мс.
S2new 0.767 мс — сцена с просветом (scene-камера), не сравнимая
с 5.65 мс fixed (60.1% cloud); для сравнения использовать A03
S2new+macro 6.562 мс при том же A03-ракурсе. Clear/Partly не
перезапускались: гейт уже BLOCKED, а C7 cross-check выполнен.

## J. DX12 Regression

- Release/Debug build: PASS; CTest Release/Debug 1/1 PASS.
- Source checks Python/PowerShell: PASS; fxc: PASS.
- HW selftest PASS: Legacy / R / A w0.40 / B a0.18 / A03 M1 /
  S2old / S2new.
- HW selftest FAIL: M1c / final / S2new+fixed — те же три проверки
  (`increasing extinction reduces transmittance`,
  `increasing coverage increases occupied ray-march steps`,
  `sun below horizon removes direct/multiple scattering`).
  Причина — dense macro override, который selftest использует для
  early-exit: при `macroThreshold=0` масса равна 1 во всей колонке,
  поэтому coverage 0.20/0.72 и extinction не меняют поле. Это
  ограничение harness для C3-1, а не proof of renderer regression;
  C3-1 и Storm-интеграция не входят в accepted set.
- WARP: R / A / B / M1 PASS; M1c/final/s2fixed FAIL по тем же трём
  harness-проверкам. WARP+GBV exit 2 — задокументированное ограничение.
- Release GBV: Legacy / R / A / B / M1 / S2old / S2new 0/0;
  M1c/final/s2fixed — selftest FAIL (те же три проверки).
- 120 frames + resize final (Release/Debug): 0/0.
- GBV final Float32 volume probe: 0/0; GBV final OD probe: 0/0.
- Negative injected ERROR: exit 1 (expected).
- Shutdown: non-device objects 0.
- Pixel identity: Legacy/RB/R/A/B/M1/S2old/S2new/M1 OD **9/9
  byte-identical** к C0 на финальной EXE.
- F2/F5/Pause final: slices↔clouds, clear→partly, pause/resume,
  0 errors/0 warnings, exit 0.

**T — Technical: PASS для принятых вариантов** (Legacy/R/A/B/M1/
S2old/S2new). C3-1/final и Storm-интеграция имеют документированный
selftest FAIL и не приняты.

## K. Known Issues

1. A03 U был confounded: пять «seed» в A03 — это noiseSeed, не macroSeed.
   При реальной variation macroSeed period IoU 0.35–0.55 (q45), у
   насыщенных seed 2027/9001 — до 0.90 на OD-маске.
2. Macro scale 6 км сравним с периодом 4.545 км; 60 км autocorrelation
   на периоде 0.30–0.39. Scale 3 км снижает IoU, но визуально даёт
   «стены» и крупные бесструктурные массы — отвергнут.
3. C3-1 восстанавливает coverage seed 1337 (12→41%), но делает
   Clear/Partly/Overcast/Sunset почти одинаковыми (56–62%) и превращает
   seed 2027/9001 в серую плиту.
4. C3-2 coarse-октава (48/12 км) не является локальным средним;
   калибровка нестабильна. Нужен новый occupancy-слой, а не minimal fix.
5. S2new+macro: 14.6% колонок даже после замены storm gap; macro-поле
   в камере Storm находится в просвете. S2new сохранён.
6. Оригинальный wind FAIL S2new+macro — step-edge на разреженном поле;
   поля инвариантны, tolerance не менялся.
7. Scale 3 км, hash-вариант и C3-2 — Reference-only эксперименты, не
   default и не принятая модель.
8. N4/H5/H6/V2.4/lightning/surge/game DX12 не начинались.

## L. Verdict

- **U — World-Space Uniqueness: PASS WITH NOTES (сильные notes).**
  MacroSeed действительно управляет независимым полем; detail от него
  не зависит; точного периода нет. Но A03-числа 0.003–0.194 были
  noiseSeed-variation; при macroSeed-variation period IoU 0.35–0.55,
  у насыщенных seed — выше. Требуется отдельная переоценка U.
- **C — Coverage: BLOCKED.** C3-1 минимален и математически верен, но
  не сохраняет погодную дифференциацию и даёт серую плиту на части seed.
- **W — Wind Invariance: PASS.** Полевой контракт exact; tolerance 0.008
  не изменён.
- **S — Storm Compatibility: BLOCKED.** S2new сохранён; fixed-интеграция
  не восстанавливает 56% и фрагментирует массу.
- **T — Technical: PASS для принятых вариантов.** Release/Debug, CTest,
  source checks, fxc, HW/WARP, GBV accepted, 120+resize, OD/volume probes,
  negative, F2/F5, shutdown, identity. Storm-интеграция исключена.
- **P — Performance: measured.** Корректные медианы; стоимость показана
  вместе с облачностью.
- **V2: NOT ACCEPTED.** No commit/push/merge.

Итог: **BLOCKED.** Для прохождения C/S нужен новый слой локального
occupancy/weather placement; в рамках A03a он не строился.
