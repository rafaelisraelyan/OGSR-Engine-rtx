# V2.3g — Formula Validation, WARP Recovery & Coverage-Matched Morphology Review

Статус: **BLOCKED**.
V2 PASS не присваивался. F3 остаётся экспериментальной формулой,
**не default**. Merge в `prypiat` не выполнялся. V2.4, V-L, V-S, lightning,
surge и game DX12 не начинались.

Причина BLOCKED: критерий 5 не выполнен. При сопоставленных coverage и
optical depth Storm wedge не исчезает; F3 лишь умеренно меняет Sunset и
Overcast. Ранний «сильный» выигрыш F3 в V2.3f был в основном следствием
дополнительного coverage/density, а не изменения 3D-контура.

> **A01 correction (2026-10-27):** в V2.3g probe PSO для `DensitySlice`,
> `DensityHorizontal`, `DensityVerticalZ`, `MacroShape` и `RayProfile`
> ошибочно исполнял F0. Поэтому probe-изображения F3 в этом отчёте
> (slices/macro/ray-profile) **недействительны** для сравнения F0/F3 и не
> используются как доказательство. `Final`, `Density`, `Weather`,
> `CloudType`, coverage/OD-метрики и final-ROI не затронуты. Исправление и
> повторные данные: `dx12-v2-3g-a01-probe-pso-correction.md`. Corrected
> probes показывают, что F3 действительно меняет density field; выводы
> V2.3g о «неизменном 3D-контуре» на старых probe-изображениях отменяются.
> Итоговое решение по F3 (отклонена как замена) остаётся в силе по final
> visual gate.

Ветка: `feature/dx12-volumetric-clouds`, HEAD
`42b8124e538ef4e40229a155203c133ca412756c`.
База: V2.3 final delta
(`OGSR_V2_3_Final_Delta.zip`, SHA256
`4118AD127570FA69102D55B43967A77D6ED6396929C4631F0BE1C79DA15CAEC5`).
Предыдущий checkpoint: `OGSR_V2_3f_Formula_Benchmark.zip`,
SHA256 `623C4A5CD45427FA1C2FCCBB880CBAA8FE15A767C4896D8341B4134EEC2DF223`.

## A. Git State

- Branch: `feature/dx12-volumetric-clouds`
- HEAD: `42b8124e538ef4e40229a155203c133ca412756c`
- `git status --porcelain`: **94** entries — 20 tracked modified,
  74 untracked, 0 staged, 0 deleted (including this report and the package
  ZIP/SHA256 at repository root).
- Все V2.1–V2.3g изменения остаются незакоммиченными. Коммит, push, rebase,
  force push, `git reset --hard`, `git clean` и merge в `prypiat` не
  выполнялись.
- V2.3 baseline: `OGSR_V2_3_Final_Delta.zip`,
  SHA256 `4118AD127570FA69102D55B43967A77D6ED6396929C4631F0BE1C79DA15CAEC5`.
- V2.3f checkpoint: `OGSR_V2_3f_Formula_Benchmark.zip`,
  SHA256 `623C4A5CD45427FA1C2FCCBB880CBAA8FE15A767C4896D8341B4134EEC2DF223`.
- V2.3f backup: `D:\Coding Project\OGSR_BACKUP_BEFORE_V23F_2026-10-26`,
  MANIFEST SHA256
  `800C94580C52919208EFC9753E7A60E236803E70EC9EC1A61AD09F68EFDAEA21`.
  Резервная копия доступна; восстановление V2.3f ранее дало manifest diff 0.
- V2.3g snapshot:
  `D:\Coding Project\OGSR_BACKUP_BEFORE_V23G_2026-10-26`,
  MANIFEST SHA256
  `9CF86524D6091ECE9E1F3805C4176A2CF83BEC3FFF7C9A34861BBDA4C10D937F`,
  69 payload-файлов + `MANIFEST.sha256`; проверено 69/69, расхождений 0.
- Default `--formula=none` остаётся V2.3-совместимым: capture
  `clouds_cumulus_below` против V2.3 final даёт mean 0, max 0 (по каналам
  RGB, выборка каждые 4 px).
- F3 selectable only; default PSO не содержит formula-кода.

## B. WARP Root Cause

### Наблюдение V2.3f

WARP cloud selftest возвращал Cloud RGB = 0, transmittance = 1, несколько
функциональных FAIL при debug errors = 0 и exit code 1. Исторический WARP
тест V2.3 также завершался по тайм-ауту, поэтому причина не была очевидна.

### Изоляция

| Конфигурация | F0 | F3 | Результат |
|---|---|---|---|
| Hardware, без GBV | PASS | PASS | корректный cloud output |
| Hardware + `--gpu-validation` | PASS | PASS | GBV работает |
| WARP, без GBV | PASS | PASS | корректный cloud output |
| WARP + `--gpu-validation` | FAIL | FAIL | RGB = 0, T = 1, пустой output |

Дополнительно WARP capture с `--debug-mode=weather` дал непустое изображение,
то есть compute/dispatch/readback path на WARP жив. Пустым был именно
selftest path в конфигурации WARP + GBV.

### Root cause

Это **ошибка конфигурации теста**, а не F3 и не WARP compute path.
`--gpu-validation` включает D3D12 GPU-Based Validation — hardware-only
режим. В этой среде WARP + GBV не даёт пригодного cloud output. F0 и F3
ведут себя одинаково: без GBV оба PASS, с GBV оба FAIL.

### Исправление

1. `CloudRayMarcher::Readback()` принимает `CloudFormula` и передаёт его в
   `Dispatch()`; `CloudReadback` несёт `formula` и `shaderVariant`.
2. `selftest.txt` пишет `Formula requested`, `Formula executed`,
   `Shader variant`, `GPU adapter`, `Validation status`.
3. CLI явно отклоняет `--warp --gpu-validation` с exit code 2 и сообщением,
   что GBV — hardware-only режим; WARP functional selftest запускается без
   GBV.
4. Hardware GBV и WARP functional test разделены; WARP+GBV не выдаётся за
   functional PASS.

### P0 acceptance

WARP 64³ functional selftest **PASS** для F0 и F3 на 640×360 output
(320×180 diagnostic также проходил в процессе разработки). WARP+GBV остаётся
документированным ограничением, а не скрытым PASS.

Evidence: `evidence/selftest_warp_F0.txt`, `selftest_warp_F3.txt`,
`warp_selftest_F0.log`, `warp_selftest_F3.log`,
`warp_gbv_config_error.log`, `warp_root_cause.txt`.

## C. F3 Numerical Selftest

Ранее `Readback()` принудительно использовал `CloudFormula::None`, поэтому
`--formula=F3` фактически проверял F0. В V2.3g формула протянута через
render path, dispatch, readback и selftest. `selftest.txt` для F3 содержит:

```text
V2 CLOUD SELFTEST (640x360)
Formula requested: F3
Formula executed: F3
Shader variant: CloudRayMarchFormulaF3.hlsl
GPU adapter: NVIDIA GeForce RTX 5070 Laptop GPU ... hardware
V2 CLOUD SELFTEST: PASS
OVERALL SELFTEST: PASS
```

Ключевые проверки F3 (hardware, 640×360):

| Проверка | Значение | Результат |
|---|---:|---|
| coverage=0: mean transmittance / max RGB | 1 / 0 | PASS |
| clear preset mean transmittance / RGB | 0.658018 / 0.215633 | PASS |
| 2.5× extinction mean transmittance | 0.581994 | PASS |
| wind translation max abs diff | 0.00195312 (threshold 0.008) | PASS |
| zero wind max abs diff | 0 (threshold 1e-5) | PASS |
| opposite wind max abs diff | 0.000976562 (threshold 0.008) | PASS |
| occupancy low/high coverage mean | 0.0637682 / 0.0740162 | PASS |
| shadow density vs primary density | exact match | PASS |
| sun optical depth / transmittance | 0.169685 / 0.580664 | PASS |
| finite / no NaN / Inf | finite | PASS |
| deterministic fixed camera/seed/time | identical | PASS |
| transmittance inside [0,1] | yes | PASS |
| sun below horizon removes scattering | yes | PASS |

WARP F3 selftest также PASS на том же 64³ volume; F0 проверен отдельно и
проходит независимо. Ложного «F3 PASS на F0 PSO» больше нет.

Evidence: `evidence/selftest_hw_F3.txt`, `selftest_hw_none.txt`,
`selftest_warp_F3.txt`, `hw_selftest_F3.log`, `warp_selftest_F3.log`.

## D. Formula Implementation

Финальная F3 в `shaders/CloudRayMarch.hlsl`:

```hlsl
thresholdCumulus  = 0.55f + formulaThresholdBias;
thresholdStrat    = 0.45f + formulaThresholdBias;
thresholdOvercast = 0.38f + formulaThresholdBias;

weatherPlacement = smoothstep(
    0.10f, 0.55f, weather.r + WeatherA.x * 0.35f);
coverageScale = smoothstep(
    0.15f, 0.55f, coverageGlobal);
placement = weatherPlacement * coverageScale;
```

`frontBoost` остаётся только в density/coverage expression и **не входит**
в placement. Это важно для wind translation: перенос камеры на wind offset
не меняет placement, поэтому wind invariance держится на 0.00195.

F3 — отдельный compile-time PSO `CloudRayMarchFormulaF3.hlsl`
(`OGSR_FORMULA 3`). Default PSO `CloudRayMarch.hlsl` не содержит formula-кода
и остаётся побитово совместимым с V2.3.

Runtime HLSL загружается из `<exe>/shaders`; CMake копирует `shaders/` рядом
с EXE. SHA256 source и runtime copy совпадают:

- `CloudRayMarch.hlsl`:
  `2CCD93C7FC503B2DA4C45AB71E7061EB42FC3BBDDA556D18F0BBCB887FC410E3`
- `CloudRayMarchFormulaF3.hlsl`:
  `101740D7A3229F3BE8F91B9C184E7417A71BF6AE6539FBEEF67231454DF940E1`

## E. Coverage-Matched Comparisons

Методика: один заранее объявленный параметр — `--formula-bias=0.02` для F3.
Проверены 0.02 и 0.04; 0.02 даёт более близкое покрытие на пяти сценах.
Lighting, erosion, camera, seed и weather не менялись.

| Scene | F0 coverage / meanD / meanOD | F3cov coverage / meanD / meanOD | Δ coverage |
|---|---:|---:|---:|
| Sunset | 0.5636 / 0.1713 / 1.1179 | 0.5377 / 0.1372 / 0.7698 | −4.6% |
| Storm | 0.9776 / 0.4274 / 4.9277 | 0.9747 / 0.4137 / 4.7216 | −0.3% |
| Overcast | 0.8681 / 0.3805 / 3.7782 | 0.8664 / 0.3521 / 3.4464 | −0.2% |
| Cumulus | 0.5058 / 0.1314 / 1.0607 | 0.5115 / 0.0999 / 0.6832 | +1.1% |
| Strat | 0.5801 / 0.1655 / 1.6306 | 0.5420 / 0.1230 / 1.0640 | −6.6% |

Coverage-matched Storm попадает в coverage почти точно (0.9747 vs 0.9776),
но meanOD у F3 ниже (4.72 vs 4.93). Это уже не позволяет объяснить разницу
«F3 просто плотнее».

ROI (те же пороги, что в V2.3f; Storm bright 0.275,
Sunset/Overcast dark 0.447):

| ROI | F0 occupancy / runMean / straight | F3cov occupancy / runMean / straight |
|---|---:|---:|
| Storm bright | 0.599 / 165.5 / 0.313 | 0.588 / 162.8 / **0.333** |
| Sunset dark | 0.859 / 226.6 / 0.050 | 0.798 / 208.0 / 0.030 |
| Overcast dark | 0.528 / 159.0 / 0.043 | 0.448 / 139.2 / 0.036 |

Главный отрицательный результат: при matched coverage Storm bright occupancy
почти не меняется (0.588 vs 0.599), runMean почти не меняется
(162.8 vs 165.5), а straightness F3 даже выше (0.333 vs 0.313). То есть
F3 не убирает угловатый просвет, а слегка перераспределяет яркость.

Evidence: `evidence/coverage/F0/*`, `coverage/F3/*`, `coverage/F3cov/*`,
`coverage_od_metrics.txt`, `coverage_sheet_final.png`,
`coverage_sheet_density.png`, `density_histograms.csv`.

## F. Optical-Depth Comparisons

Методика: F3 с `--formula-bias=0.02` и scene-level density compensation.
Компенсация — один множитель на сцену, не per-pixel fit:

- Sunset 1.31
- Storm 1.25
- Overcast 0.88
- Cumulus 1.40
- Strat 1.38

| Scene | F0 meanOD | F3od meanOD | Residual |
|---|---:|---:|---:|
| Sunset | 1.1179 | 0.9745 | −12.83% |
| Storm | 4.9277 | 4.7440 | −3.73% |
| Overcast | 3.7782 | 3.5365 | −6.40% |
| Cumulus | 1.0607 | 0.9003 | −15.12% |
| Strat | 1.6306 | 1.3624 | −16.45% |

Coverage F3od измерен по transmittance-изображению (T < 0.99), потому что
OD-группа снималась только в transmittance view:
Sunset 0.6849, Storm 0.9886, Overcast 0.8712, Cumulus 0.6214, Strat 0.6275.

Точное одновременное совпадение coverage и optical depth одним параметром
невозможно: Storm удаётся приблизить по OD (−3.7%), но Sunset/Cumulus/Strat
остаются заметно прозрачнее (−12.8…−16.5%). Эти residuals не скрываются.
При matched OD Storm wedge также не исчезает.

Evidence: `evidence/coverage/F3od/*`, `trans_stats.txt`,
`coverage_od_metrics.txt`, `coverage_sheet_od.png`, `od_histograms.csv`.

## G. Shape Quality

### Storm wedge

World-space slices (`evidence/slices/storm_xy.png`, `storm_yz.png`,
`storm_horizontal.png`) показывают, что треугольный контур плотности
сохраняется. F3 добавляет массу/заполнение вокруг контура, но не
меняет его геометрию. При matched coverage/OD bright occupancy 0.588 vs
0.599, runMean 162.8 vs 165.5. Это не shape win.

Исторические V2.3f цифры (occupancy 0.319, runMean 69.5) не воспроизводятся
после выравнивания coverage/OD. Они были получены при существенно большем
coverage/density F3 и отражали затемнение/заполнение, а не новый 3D-контур.

Debug views подтверждают это: `evidence/debug/weather.png` (coverage input)
и `cloud-type.png` идентичны F0/F3 при одном seed, а `macro-shape.png`
показывает ту же macro-структуру с добавленной массой вокруг контура. То есть
F3 не меняет weather map и type map; различие возникает в placement/density,
а не в новом macro-контуре.

### Sunset pillar

Тонкий гребень сохраняется. F3 делает его менее контрастным:
dark occupancy 0.798 vs 0.859, runMean 208.0 vs 226.6, straight 0.030 vs
0.050. Дополнительных вертикальных колонн не появилось (occupancy и runMean
не выросли), высотная протяжённость в YZ-slice сохраняется. Это умеренное
изменение контраста, не устранение pillar.

### Overcast plates

Пластинчатость сохраняется. F3cov не создаёт более плотный потолок:
dark occupancy 0.448 vs 0.528, runMean 139.2 vs 159.0; meanOD ниже
(3.45 vs 3.78). Overcast становится тоньше/светлее, но не исправляется.
Деградации в виде нового плотного ceiling не видно; plates остаются.

### Дополнительные сцены

Clear sky control (`--coverage=0`): transmittance ровно 1.0, coverage 0,
OD 0; density debug residual 0.016/0.025 — фон debug-визуализации, не clouds.
Clear aerial / Partly / Cumulus side при original parameters: F3 тоньше F0
(meanD −23…−32%, meanOD −28…−42%), без новых дыр/швов/NaN. Пластины:
`evidence/extra/scenes/missing_scenes_final.png` и
`missing_scenes_density.png`; метрики: `extra_scenes_metrics.txt`. Для этих
трёх сцен matched-группы не снимались; это указано как ограничение.

### Weather seeds

Основное сравнение — seed 1337. Seeds 1338/1339 добавлены на Sunset, Storm,
Overcast. Weather seed меняет пространственное расположение сильнее, чем
формула: при 1338/1339 фиксированный Sunset ROI полностью тёмный, а Storm ROI
не содержит bright pixels. ROI насыщается, поэтому seed sweep не может
доказать улучшение формы. Он показывает лишь, что F3 остаётся немного тоньше
по meanD и не даёт seed-specific регрессии. Для полноценного seed sweep нужны
feature-matched ROI или камера, следящая за той же облачной массой.

## H. Temporal Stability

60-кадровые последовательности, 640×360, half scale, 128 steps, 6 shadow
steps, spatial jitter, full lighting, atmosphere on. F0/F3 с одинаковым
timestep, camera path, wind и time.

| Sequence | Formula | meanDiff | maxDiff | max/mean | meanHF |
|---|---|---:|---:|---:|---:|
| Sunset slow pan + wind | F0 | 0.32478 | 0.33535 | 1.033 | 0.41961 |
| Sunset slow pan + wind | F3 | 0.31465 | 0.32535 | 1.034 | 0.40354 |
| Storm slow pan + wind | F0 | 0.16987 | 0.18260 | 1.075 | 0.65639 |
| Storm slow pan + wind | F3 | 0.17685 | 0.18685 | 1.056 | 0.64306 |
| Storm fixed camera + wind | F0 | 0.01373 | 0.01391 | 1.013 | 0.64521 |
| Storm fixed camera + wind | F3 | 0.01315 | 0.01336 | 1.016 | 0.63247 |
| Storm fly across weather tile, high path | F0 | 0.29277 | 0.33766 | 1.153 | 0.46968 |
| Storm fly across weather tile, high path | F3 | 0.27375 | 0.29861 | 1.091 | 0.46157 |

Пересечение world x = 4096 происходит между кадрами 23 и 24; diff на границе
0.268/0.266 (F0/F3), без seam spike и tile-repeat pop. Максимум в boundary
sequence — на кадре 6, до границы, и он плавный.

F3 не даёт popping/shimmering/discontinuity; high-frequency energy у F3 ниже
(чуть мягче detail), frame-to-frame motion сопоставим. Fixed-camera wind
почти постоянен (max/mean ~1.01).

Stress-test: низколетящий fly (y=700→830, pitch +5) входит в верхнюю часть
storm cloud около кадра 50 и даёт большие diff у обеих формул (max 3.22 F0,
1.85 F3). Это camera-inside-volume artifact, общий для F0/F3, не F3-дефект;
он не используется как stability evidence.

Evidence: `evidence/temporal/*`, `temporal_metrics.txt`.

## I. GPU Performance

### Main scenes

1080p, half, 128 steps / 6 shadow steps, 120 frames, warmup 20, no-vsync,
3 runs, same session, F0/F3 adjacent. Median (p95; n=3, nearest-rank,
p95 = max).

| Preset | F0 median (p95), ms | F3 median (p95), ms | F3/F0 |
|---|---:|---:|---:|
| Clear | 9.016 (9.016) | 9.631 (9.631) | +6.8% |
| Partly | 10.309 (10.309) | 10.799 (10.799) | +4.7% |
| Overcast | 9.336 (9.336) | 9.906 (9.906) | +6.1% |
| Storm | 5.879 (5.879) | 6.944 (6.944) | +18.1% |

### Storm steps / shadow matrix

3 runs per point, same session, 1080p half, 120 frames, warmup 20.
Median cloud ms (F0 → F3, ratio):

| Steps / shadow | F0 | F3 | Ratio |
|---|---:|---:|---:|
| 64 / 2 | 2.240 | 2.779 | +24.1% |
| 64 / 4 | 3.139 | 3.686 | +17.4% |
| 64 / 6 | 3.895 | 4.452 | +14.3% |
| 96 / 2 | 3.151 | 3.533 | +12.1% |
| 96 / 4 | 4.013 | 4.555 | +13.5% |
| 96 / 6 | 4.886 | 5.678 | +16.2% |
| 128 / 2 | 3.609 | 4.268 | +18.3% |
| 128 / 4 | 4.665 | 5.530 | +18.5% |
| 128 / 6 | 5.881 | 6.946 | +18.1% |

### Work counters

Storm, 1280×720 full, 128 steps:
- executed primary steps mean: 0.5452 → 0.5901 (+8.2%);
- occupied samples mean: 0.0685 → 0.0847 (+23.6%).

F3 не добавляет обязательный texture fetch (macro/mid/weather samples
переиспользуются). Рост стоимости объясняется большим числом occupied
samples, shadow-ray работой и меньшим early exit. Поэтому стоимость выше
там, где объём плотнее: Storm +18.1% на 128/6, максимум матрицы +24.1%
на 64/2; минимум +4.7% на Partly.

Absolute ms зависят от clocks RTX 5070 Laptop; сравнимы только same-session
ratios. GPU clocks: 180 MHz / 11.95 W / 44 °C before, 877 MHz / 33.62 W /
49 °C after. Coverage-matched и optical-depth-matched группы отдельным
3-run timing-матриксом не измерялись; это ограничение, а work counters и
разница density объясняют направление стоимости.

Evidence: `evidence/timings3/` (raw logs, CSV, summary, GPU clocks),
`evidence/timings.txt` (historical 2-run session), `evidence/counters/`.

## J. DX12 Validation

Build directory:
`D:\Coding Project\OGSR ENGINE fork from rtx\tools\VolumetricCloudsTest\build-v23g-final`

- Release EXE SHA256:
  `E46C5B1457F93A21D9252D091AC2B2FAE3E7BB208F88D3E4D477F9D4C7CC53FE`
- Debug EXE SHA256:
  `1A6909B4A616DAD4A4DD7F57CCE6A39E20A148D02809BA2CC6990B7B35DF5FE0`
- CTest Release / Debug: **1/1 PASS**.
- Hardware F0 / F3 selftest, 64³: **PASS**.
- WARP F0 / F3 functional selftest, 64³, 640×360: **PASS**.
- WARP + `--gpu-validation`: отклонён с exit code 2; это ожидаемое
  ограничение, не functional PASS.
- Release/Debug + GBV, 120 frames + resize, F0/F3: **0 errors, 0 warnings**;
  resize 469×242 → 629×372; shutdown code 0.
- Negative validation: injected debug ERROR → `final debug errors: 1`,
  `final validation verdict: FAIL`, `OVERALL SELFTEST: FAIL`, exit code 1.
- Shutdown: `GPU idle`, `non-device objects: 0`; в live-object report остаётся
  только сам `ID3D12Device` (refcount 3), что является device refcount
  warning, а не утечкой resource-объектов.
- Default `--formula=none` vs V2.3 final: mean 0, max 0.
- F2/F5: source-level paths (`VK_F2` render-mode switch, `VK_F5` weather
  preset transition) и оба formula shader присутствуют; `check_sources.ps1`
  PASS. Дополнительно выполнен Debug+GBV keypress smoke test:
  `f2_f5_transition.log` фиксирует `render mode switched to slices` и обратно
  в clouds, 0 errors/0 warnings, exit code 0; `f5_transition_complete.log`
  фиксирует полный переход `partly -> overcast` и
  `weather transition complete: overcast`. F5-complete прогон был завершён
  kill после перехода, поэтому чистота shutdown берётся из отдельных
  120-frame GBV прогонов, а не из этого smoke test.

### Binary identity

EXE mtime старше финального `CloudRayMarch.hlsl`, потому что HLSL **не
встроен в EXE**: приложение компилирует shader из `<exe>/shaders` при запуске,
а CMake копирует `shaders/` рядом с EXE. C++ файлы после финальной правки
shader не менялись, поэтому EXE не перелинковывался и его SHA256 остался
прежним. Это не stale binary:

- source `CloudRayMarch.hlsl` SHA256 =
  `2CCD93C7FC503B2DA4C45AB71E7061EB42FC3BBDDA556D18F0BBCB887FC410E3`
- runtime `build-v23g-final/Release/shaders/CloudRayMarch.hlsl` SHA256 =
  тот же;
- source F3 shader SHA256 =
  `101740D7A3229F3BE8F91B9C184E7417A71BF6AE6539FBEEF67231454DF940E1`,
  runtime copy совпадает;
- все validation/capture прогоны выполнены после копирования финального
  shader (runtime copy mtime 06:42, прогоны 09:34+).

Compiled-source hash (src + shaders + CMakeLists):
`fbd98de9207e0decb8b0e9ebc5fe261020c9481ba6a660b072ea0bba8b1f31a0`
(42 files). Package-source hash и SHA256 файлов — в `MANIFEST.sha256`.

Evidence: `evidence/v23g_binary_identity.txt`,
`v23g_build_release_final.log`, `v23g_build_debug_final.log`,
`v23g_ctest_release_final.log`, `v23g_ctest_debug_final.log`,
`rel_gbv_120_resize_*.log`, `dbg_gbv_120_resize_*.log`,
`selftest_negative.txt`, `negative_inject_debug_error.log`,
`source_checks.log`.

## K. Known Issues

1. **Storm shape gate fails.** При matched coverage/OD wedge остаётся;
   bright occupancy 0.588 vs 0.599, runMean 162.8 vs 165.5, straight 0.333 vs
   0.313. Это главная причина BLOCKED.
2. **F3 не default и не проходит полный visual gate.** Default остаётся
   `--formula=none`; F3 только selectable experimental.
3. **WARP+GBV не поддерживается.** WARP functional selftest проходит только
   без GBV; GBV — hardware-only. Это документировано и явно отклоняется CLI.
4. **Optical-depth residuals.** Sunset/Cumulus/Strat остаются на
   −12.8…−16.5% OD; Storm ближе (−3.7%), Overcast −6.4%. Одним параметром
   coverage и OD одновременно не выравниваются.
5. **Overcast plates persist.** F3 делает Overcast тоньше/светлее, но не
   устраняет пластинчатость; нового плотного ceiling нет.
6. **Стоимость F3 +4.7…+24.1%.** Причина — больше executed primary steps
   (+8.2%) и occupied samples (+23.6%), то есть больше shadow work и меньше
   early exit, а не новый texture fetch.
7. **Seed sweep ограничен.** 3 seeds проверены только на Sunset/Storm/
   Overcast; фиксированный ROI насыщается при 1338/1339. Дополнительные
   Clear/Partly/Cumulus side сняты только в original-параметрах, без
   matched-групп.
8. **F2/F5 transitions — smoke test, не полная матрица.** F2 mode switch и
   F5 `partly -> overcast` прошли под Debug+GBV; F5-complete прогон был
   завершён kill после перехода, поэтому чистый shutdown подтверждён
   отдельными 120-frame GBV прогонами.
9. **Camera-inside-cloud stress.** Низколетящий fly даёт popping у F0 и F3
   при входе в облако; это общий camera-volume artifact, не F3-specific.
10. **Research document отсутствует.** `OGSR_V2_3e_Open_Source_Cloud_Density_
    Research_2026-10-26.md` в репозитории нет; формулы реализованы
    самостоятельно, чужой shader source не копировался.

## L. Verdict

**BLOCKED.** V2.3g нельзя передавать как `READY FOR V2.3g INDEPENDENT REVIEW`,
потому что критерий 5 не выполнен.

| # | Критерий | Статус |
|---|---|---|
| 1 | WARP functional selftest или доказанное ограничение | **PASS** (WARP без GBV; WARP+GBV документировано) |
| 2 | Реальный численный GPU selftest F3 | **PASS** (hardware + WARP, formula/variant/adapter в логе) |
| 3 | Нет ложных validation PASS | **PASS** (negative FAIL exit 1; WARP+GBV exit 2) |
| 4 | Coverage/OD-matched сравнения | **PASS** (три группы, residuals видимы) |
| 5 | Storm shape действительно улучшается, а не темнеет | **FAIL** (wedge остаётся при matched coverage/OD) |
| 6 | Sunset без дополнительных вертикальных колонн | **PASS** (occupancy/runMean не выросли) |
| 7 | Overcast не деградирует | **PASS с оговоркой** (нет нового ceiling; plates остаются) |
| 8 | Temporal stability на последовательностях | **PASS** (4 чистые последовательности; in-cloud stress — общий artifact) |
| 9 | Производительность измерена корректно | **PASS** (same-session, work counters; см. I) |
| 10 | Принятый V2.3 не изменён | **PASS** (default none mean 0/max 0) |

Итог: 9 из 10 критериев выполнены, критерий 5 — нет. F3 не включать по
умолчанию, не merge-ить в `prypiat` и не объявлять V2 PASS. Следующий
эксперимент должен менять именно 3D macro contour Storm wedge (а не coverage
scaling) и после отдельного архитектурного review; при необходимости —
ограниченное применение F3 только для Storm с контролируемым переходом.
