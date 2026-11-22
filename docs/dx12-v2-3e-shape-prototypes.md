# V2.3e — Cloud Shape Architecture Prototype

Статус: **BLOCKED** (visual gate).
V2 PASS не присваивался. Merge в `prypiat` не выполнялся.
V2.4, V-L, V-S, lightning, surge, game DX12 не начинались.

Ветка: `feature/dx12-volumetric-clouds`, HEAD `42b8124e5`.
База: V2.3 final delta
(`OGSR_V2_3_Final_Delta.zip`, SHA256
`4118AD127570FA69102D55B43967A77D6ED6396929C4631F0BE1C79DA15CAEC5`).
Предыдущий checkpoint: `OGSR_V2_3d_Morphology_Checkpoint.zip`,
SHA256 `F6BA3459D5C69E5088A5746BA9F09329D74F83658F0F59FCA2D011D8F9477A73`.

## A. Аудит V2.3d — исправления

### V23D-A01 — probe buffer initial state

Probe buffer создавался с `D3D12_RESOURCE_STATE_UNORDERED_ACCESS`. Для
buffer’ов это игнорируется (эффективно COMMON), отсюда D3D12 WARNING #1328.
Исправлено: buffer создаётся в `COMMON`, затем явно переводится в
`UNORDERED_ACCESS` через `ExecuteImmediate`.

Проверено:

- `--selftest --gpu-validation`: `initial debug warnings: 0`,
  `debug message totals: 0 errors, 0 warnings`.
- `--frames=120 --resize-test --gpu-validation` (Release и Debug):
  `debug errors=0 warnings=0`, `debug message totals: 0 errors, 0 warnings`.
- То же для `--shape-prototype=A` и `B`: 0 warnings / 0 errors.

Логи: `evidence/a01_fix_selftest.log`,
`evidence/a01_fix_frames120_resize.log`,
`evidence/*_gbv_120_resize_*.log`.

### V23D-A02 — Storm ROI threshold

Порог стандартизирован: 0.275 (70/255). При 0.549 (140/255) sky-gap не
попадает в bright-класс (occupancy 0), поэтому прежняя строка лога была
недействительной. Перемерено по исходным PNG:

| Вариант | occupancy | runMean | straight |
|---|---:|---:|---:|
| baseline | 0.599 | 165.5 | 0.313 |
| type-smoothing 1.0 | 0.438 | 117.5 | 0.017 |
| top-widen 1.0 | 0.598 | 165.4 | 0.313 |

Evidence: `evidence/roi_metrics_corrected.txt`. В
`docs/dx12-v2-3d-morphology-correction.md` добавлено примечание о
стандартизации.

### V23D-A03 — полные Float32 dumps

В пакет включены четыре полных readback:

- `probe_float32/sunset_col0.33.bin`
- `probe_float32/sunset_col0.35.bin`
- `probe_float32/sunset_col0.37.bin`
- `probe_float32/storm_col0.35.bin`

Формат: `uint32 'SGRP', rows, steps, records=3`, затем float32
`(row, step, record, component)`. Layout и параметры — в
`probe_float32/README.txt` и `*.params.txt` (включая SHA256 EXE).

## B. Prototype A — Deep Convective Cloud Shapes

Цель: Cumulus + Storm, крупные округлые массы, вертикальное развитие,
мягкое объединение, без узких гребней и клиньев.

Реализация (отдельный PSO `CloudRayMarchPrototypeA.hlsl`,
`OGSR_SHAPE_PROTOTYPE=1`):

- billow-поля из `macro.r`/`mid.r`: `1 - abs(2n - 1)`;
- soft union двух масштабов;
- height-dependent convective envelope;
- placement из `gatedCoverage`;
- blend с baseline по `convectiveWeight = saturate(typeWeights.x + storm*0.55)`;
- baseline top/bottom cuts ослабляются для prototype-части, чтобы не
  создавать плоские срезы.

Проверялись также Worley-cell и Perlin-Worley варианты. Все итерации дали
**блочные/фасеточные** массы; billow-вариант — лучший из них, но всё равно
неестественный. Sunset ROI: runMean 226.6 → 217.9, straight 0.050 → 0.130;
Storm straight 0.313 → 0.230. Визуальный gate не пройден.

## C. Prototype B — Layered Cloud Deck Shapes

Цель: Stratocumulus + Overcast, непрерывный слой, сложная нижняя
поверхность, вариации оптической толщины, ограниченная эрозия, без пластин
и вертикальных занавесей.

Реализация (отдельный PSO `CloudRayMarchPrototypeB.hlsl`,
`OGSR_SHAPE_PROTOTYPE=2`):

- непрерывный deck по `height` с 3D-вариацией `deckBottom/deckTop`;
- толщина/плотность из `macro.b` и `mid.g`;
- limited large-scale erosion `mid.r` в boundary band;
- stratocumulus gaps из `weather.r`, overcast без gaps;
- blend по `layeredWeight = saturate(typeWeights.y + typeWeights.z)`.

Результат: Sunset runMean 226.6 → 194.9 (−14%), но Overcast стал заметно
темнее и плотнее (occupancy 0.528 → 0.695, runMean 159.0 → 218.8), а
straightness вырос 0.050 → 0.220. Визуальный gate не пройден.

## D. Cloud Type Transitions

Type weights, coverage, macro shape и density показаны отдельно:

- `--debug-mode=cloud-type` — type weights;
- `--debug-mode=weather` — coverage;
- `--debug-mode=macro-shape` — shaped до profile/weather;
- `--debug-mode=density` — итоговая density.

Смешивание прототипов сделано по **density**, а не по threshold, чтобы не
создавать резкие разрывы изоповерхности. Weather map не размывается
целиком. Evidence: `evidence/type_sunset_sheet.png`,
`evidence/type_storm_sheet.png`.

## E. Обязательные сцены

Сняты для `none`, `A`, `B`:

- Sunset pillar (476,-248,3925), FOV 57;
- Storm wedge (-366,-140,2056), FOV 72;
- Overcast below;
- Cumulus side/below;
- Stratocumulus below.

Для каждой: final, density, no-shadow, вертикальные X-Y и Y-Z срезы,
60-кадровая pan-slow последовательность. Evidence:

- `evidence/prototype_final_sheet.png`
- `evidence/prototype_density_sheet.png`
- `evidence/prototype_noshadow_sheet.png`
- `evidence/matrix/<scene>/<prototype>/{final,density,noshadow}.png`
- `evidence/slice_sunset_xy.png`, `slice_sunset_yz.png`,
  `slice_storm_xy.png`, `slice_storm_yz.png`
- `evidence/motion_<scene>_<prototype>.png`

## F. GPU timings

1080p, half, 120 frames, storm preset, 2 повтора, 64/96/128 steps через
профили performance/balanced/quality:

| steps | none | A | B | A overhead | B overhead |
|---:|---:|---:|---:|---:|---:|
| 64 | 0.733 | 0.794 | 0.800 | +8.3% | +9.1% |
| 96 | 1.677 | 1.763 | 1.756 | +5.1% | +4.7% |
| 128 | 2.741 | 2.996 | 3.002 | +9.3% | +9.5% |

Прототипы не добавляют texture fetches: используются уже сэмплируемые
`macro`, `mid`, `weather`. Стоимость — ALU/register pressure.
Evidence: `evidence/prototype_timings.txt`, `evidence/timings_profiles/`.

## G. DX12 validation

Build directory:
`D:\Coding Project\OGSR ENGINE fork from rtx\tools\VolumetricCloudsTest\build-v23e-final`

- CTest Release/Debug: 1/1 PASS.
- Release/Debug GBV selftest: errors 0, warnings 0, validation PASS.
- Release/Debug + GBV 120 frames + resize, none/A/B: 0 errors, 0 warnings.
- Injected debug ERROR: FAIL, exit 1.
- Shutdown clean.
- Release EXE `7DEE37879B9937BA1EBDE6B3468DE3809BEFF16BC06DDE45A01ABFB51C99D602`
- Debug EXE `63E1FA1F6BAC5B0A14DA491417D1A93AEB33F3338C22009B87E41082A863FAE3`

Source/build proof: `evidence/v23e_binary_identity.txt`.

## H. Default preservation

`--shape-prototype=none` побитово совпадает с V2.3: mean 0, max 0 на
`clouds_cumulus_below`. Default PSO не содержит prototype-кода; A/B
компилируются отдельными PSO.

## I. Вывод

Ни один прототип не доказывает, что новая модель формы лучше старой:

- A даёт блочные/фасеточные массы и не устраняет столб/wedge;
- B улучшает Sunset ROI, но ухудшает Overcast (темнее, плотнее,
  больше vertical alignment);
- Storm wedge и Overcast plates не устранены ни одним вариантом.

Поэтому статус — **BLOCKED**, без ложного PASS. V2.3 остаётся принятым
baseline; прототипы остаются экспериментальными PSO, default их не
включает.

## J. Следующий шаг

1. Заменить thresholded low-frequency field на **continuous distance/
   density field** с ограниченной многочастотной эрозией, а не на billow
   или Worley threshold.
2. Для Storm — отдельная 3D-модель фронта, не зависящая от type threshold.
3. Для Overcast — менять не только density, но и self-shadow/оптическую
   толщу нижней поверхности, иначе пластины остаются в освещении.
4. Повторить Float32 probe, 3D slices, ROI, motion, perf и DX12 regression.
