# V2.3h-A01 — Noise Field Isolation

Статус: **BLOCKED** (reference density model).
V2 PASS не присваивался. Legacy V2.3 остаётся принятым renderer baseline.
Reference-модель не является default и не merge-ится. H1/H2 visual gate
по-прежнему не пройден.

Ветка: `feature/dx12-volumetric-clouds`, HEAD
`42b8124e538ef4e40229a155203c133ca412756c`.

## A. Scope & state

- Legacy V2.3 не изменялся: pixel identity mean 0, max 0.
- Reference rb/plain H2 и H3 побитово совпадают с V2.3h (mean 0, max 0),
  поэтому V2.3h evidence для этих вариантов остаётся валидным.
- H5/H6, V2.4, молнии, выбросы и игровой DX12 не начинались.
- A01 добавляет только диагностику: raw Float32 noise probe, mix,
  coordinate mode, shape-transfer edges. Coverage/height/lighting не
  перекалибровывались.

## B. Method

Добавлен `--debug-mode=noise-field`: reference-only probe пишет
`256x256x3 float4` в `noise_field_float32.bin`:

- record 0: `noise.r, noise.g, noise.b, shapeField`;
- record 1: `smoothstep, heightProfile, heightFraction, densityField`;
- record 2: `worldX, worldY, worldZ, textureFetchCount`.

`densityField` — это `transfer*profile` для H1/H2 и результат одного
coverage remap для H3+, до density multiplier, ray march и lighting.
`shapeField = 0.70*noise.r + 0.30*noise.b` по умолчанию; mix
`r|b|rb` и transfer edges задаются явно. Raw-срезы сняты для XY/XZ/YZ,
offsets 0.25/0.5/0.75, seeds 1337/4242/9001. Анализ:
`scripts/analyze_noise_field.py` (периоды, автокорреляция, half-life,
mask run-length, Float32 density/OD). Сопоставимые контролы:
`scripts/match_reference_mixes.py` (H2 threshold при фиксированной ширине
0.30; H3 coverage).

## C. Noise volume and world periods

NoiseVolume: 4 Perlin base periods, 8 Worley cells, volume tiles every 1.0
UVW. Reference coordinate: `(worldXZ - wind) * noiseScale * 0.55`,
`worldY * noiseScale * 0.55 * 1.80`.

Для Sunset `noiseScale = 0.0004`:

| Component | Period |
|---|---:|
| XZ noise tile | 4545.45 m |
| Y noise tile | 2525.25 m |
| Perlin base feature (XZ) | 1136.36 m |
| Worley cell (XZ) | 568.18 m |
| Worley cell (Y) | 315.66 m |

Layer 650..2000 m = 1350 m, то есть 0.53 Y-tile. Поэтому Y-tile seam
внутри слоя не наблюдается; вертикальная структура — внутренняя
ячеистость поля (и aspect ratio среза), а не Y-тайлинг.

Измерения:

- 4 km extent (31.25 m/px): горизонтальный пик **145 px** = **4531 m**,
  correlation **0.9985–0.9993** для r/b/rb.
- 10 km extent (78.125 m/px): пик **58 px** = **4531 m**,
  correlation **0.9986**. Это ровно XZ tile period.
- После warp 10 km: пик уходит на **174 px = 13.6 km**,
  correlation **0.849**; короткий 4.5 km пик исчезает, но появляется
  новая длиннопериодная корреляция от самого warp-поля.

Вывод: крупномасштабная повторяемость — это **тайлинг NoiseVolume**, а не
погодная карта и не coverage.

### Cause attribution

| Symptom | Main cause | Evidence |
|---|---|---|
| Repeated 4.5 km cells | NoiseVolume tile period | 145 px @31.25 m and 58 px @78.125 m, corr 0.998–0.999 |
| Apparent vertical curtains in YZ | slice aspect ratio (8 km × 1.35 km in 256²), B fine cells, threshold | pixel V/H 2.9, world V/H 0.49; V half 179 m vs H 500 m |
| B-only small regular cells | B (Perlin–Worley, 8-cell Worley) | B H half 281 m vs R 531 m; B-only visual |
| RB inherits B | 0.70R + 0.30B | world V/H 0.492 (B 0.545, R 0.419) |
| Chevron/regular cells | B + high shape transfer | B-only visual; transfer isolates ridges |
| Warp partial fix | Soft coordinate warp | tile corr 0.79, new 13.6 km period 0.849 |
| Coverage cannot fix tiling | Monotonic remap of the same field | H3 mean OD matched, tile peak unchanged |

## D. Raw field measurements (YZ, seed 1337)

**Важная поправка к V2.3h.** YZ/XY-срез отображает **8 км по
горизонтали и 1.35 км по Y** в квадрат 256×256. Это вертикальное
увеличение **5.93:1**: горизонтальный пиксель 31.25 м, вертикальный
5.27 м. Пиксельные run-length и «вертикальные шторы» нельзя читать как
мировую анизотропию. В мировых метрах поле, наоборот, **мельче по Y**,
чем по XZ.

| Mix | Coord | H peak px | H corr | Shape H half m | Shape V half m | World V/H | Mask % | World V/H run | Long V frac |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| b | plain | 145 | 0.9969 | 281 | 132 | 0.469 | 2.15 | 0.556 | 0.611 |
| r | plain | 145 | 0.9987 | 531 | 185 | 0.348 | 7.87 | 0.415 | 0.860 |
| rb | plain | 145 | 0.9985 | 500 | 179 | 0.359 | 1.79 | 0.492 | 0.815 |
| rb | warped | 148 | 0.7860 | 500 | 195 | 0.390 | 0.89 | 0.598 | 0.640 |

`Mask` — transfer > 0.5 при baseline 0.58..0.88; `World V/H run` —
средняя длина вертикального пробега в метрах, делённая на среднюю длину
горизонтального. `Long V frac` — доля занятых пикселей в вертикальных
пробегах >= 16 px (84 м); это пиксельная метрика, приведена только как
дополнение.

Наблюдения:

- B-only имеет самую короткую горизонтальную корреляцию (281 м) и
  самую мелкую ячеистую структуру; R-only — самую широкую (531 м).
- В мировых метрах все варианты шире, чем выше: half-life world V/H
  0.35–0.47, mask run bias 0.42–0.56 (warp до 0.60). Пиксельный bias
  2.5–3.5 — это в основном артефакт aspect ratio 5.93:1.
- «Вертикальные шторы» из V2.3h — в значительной степени артефакт
  диагностического YZ-среза, а не вертикальная экструзия поля. Реальная
  проблема — точный 4.53 км tile-период и мелкая регулярная ячеистость
  B-канала; финальный рендер всё равно требует повторной визуальной
  проверки, но объяснять её вертикальной растяжкой поля нельзя.
- Shape transfer 0.58..0.88 выделяет гребни поля; при matched mean
  transfer R требует более высокий порог (0.614..0.914), B — более
  низкий (0.566..0.866).
- Примечание: комментарий в `CloudReferenceDensity.hlsl` «Y is
  deliberately slower» неверен — умножение Y на 1.8 делает вертикальные
  features **меньше**, а не больше. Комментарий не правился после
  сборки, чтобы не менять evidence-бинарник; исправить в следующем
  checkpoint.

## E. Matched R/B/RB comparison

H2: fixed transfer width 0.30, lower edge подобран так, чтобы
`mean(smoothstep)` совпал с baseline 0.06705.

| Mix | Transfer edges | Mean transfer | Mask % | H run m | V run m | World V/H | Long V | Mean density | Column OD mean |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| r | 0.6138..0.9138 | 0.06704 | 3.24 | 192.5 | 80.6 | 0.419 | 0.698 | 0.0536 | 1.369 |
| b | 0.5663..0.8663 | 0.06702 | 3.25 | 157.2 | 85.6 | 0.545 | 0.715 | 0.0593 | 1.512 |
| rb | 0.5800..0.8800 | 0.06705 | 1.79 | 205.6 | 101.2 | 0.492 | 0.815 | 0.0539 | 1.374 |

H3: coverage подобран так, чтобы `mean(densityField)` совпал с baseline
0.0818.

| Mix | Coverage | Mean density | Nonzero frac | OD mean | OD p50 | OD p95 | Fetch |
|---|---:|---:|---:|---:|---:|---:|---:|
| r | 0.4239 | 0.08178 | 0.471 | 2.087 | 2.205 | 4.119 | 1 |
| b | 0.4682 | 0.08181 | 0.541 | 2.088 | 2.025 | 3.761 | 1 |
| rb | 0.4500 | 0.08180 | 0.561 | 2.087 | 2.110 | 3.843 | 1 |
| rb warp | 0.4630 | 0.08178 | 0.567 | 2.087 | 2.209 | 3.381 | 2 |

При одинаковых mean density и mean column OD:

- R-only: меньше занятого объёма (0.471), но более высокие p50/p95 OD —
  fewer, wider, denser regions; world V/H 0.419 — самый «плоский»
  вариант.
- B-only: больше занятого объёма (0.541), ниже p50/p95 — more, finer
  cells; world V/H 0.545 — самый «вертикальный» из трёх, но всё равно
  шире, чем выше.
- RB: между ними, с наибольшим p95 среди plain-вариантов (3.843),
  world V/H 0.492.
- Ни один вариант не даёт вертикальной экструзии в мировых метрах;
  B-only имеет самую мелкую горизонтальную ячейку и самый высокий
  world V/H, R-only — самый низкий.

Ограничение: одним порогом/coverage нельзя одновременно совпасть по
mean density, mean OD и occupied volume; распределения каналов разные.
Mean density и mean OD сопоставлены, occupied volume отличается и
приведён явно.

## F. Candidate: soft 3D coordinate warp

Один ограниченный кандидат: `coordinate += (warpNoise - 0.5) * 0.20`,
warp sample = NoiseTexture(coord * 0.37 + offset), максимум 0.10
noise-unit. Один дополнительный texture fetch.

Результаты:

- 4 km YZ: tile autocorrelation 0.9985 -> 0.7860; пик 145 -> 148 px.
- 10 km XZ: tile autocorrelation 0.9986 -> 0.8490; пик 58 -> 174 px
  (13.6 km). То есть период не исчез, а сместился на масштаб
  warp-поля.
- Vertical coherence почти не изменилась: 0.848/0.578 на lag 16/32
  (plain 0.839/0.532).
- H2 world-space half-lives: H 500 m -> 500 m, V 179 m -> 195 m;
  world V/H 0.359 -> 0.390. Warp не уменьшает вертикальный bias, а
  слегка увеличивает его; пиксельный V/H 2.92 -> 3.54 — тот же
  aspect-ratio среза.
- H2 mask: доля занятой площади 1.79% -> 0.89%, long-V 0.815 -> 0.640;
  world run bias 0.492 -> 0.598.
- H3 matched: mean density/OD совпадают, p95 OD ниже (3.38 vs 3.84) —
  поле более равномерное, ядра размыты.
- Визуально warp добавляет диагональные смазы и искривления; крупные
  массы теряют чёткость, повторяемость не исчезает.

Кандидат **не** проходит H1/H2: он частично декоррелирует короткий
tile-период, но переносит периодичность на ~13.6 км, не улучшает
world-space V/H и размывает массы. Это не улучшение визуального gate.

### Visual A/B

- `sheets/h2_transfer_shape_channels.png`: matched H2 transfer + shape.
  R-only даёт крупные широкие пятна; B-only — множество мелких
  горизонтальных ячеек, которые в YZ-срезе выглядят высокими из-за
  aspect ratio 5.93:1; RB наследует ячейки B; warp искривляет их.
- `sheets/h3_density_channels.png`: matched H3 density. Та же картина:
  B-only — регулярная мелкая ячеистость, R-only — более крупные массы,
  warp — диагональные смазы.
- `sheets/h3_sunset_r_b_rb_warp.png`: R-only визуально спокойнее,
  B-only — самый регулярный из-за мелких ячеек, RB — промежуточный,
  warp размывает массы и добавляет диагональные структуры.
- `sheets/captures_<group>.png`: bottom/side/top для warp/r/b H2/H3
  групп. rb/plain побитово совпадает с V2.3h; для него использовать
  V2.3h sheets, а не отдельные A01-плитки.
- `sheets/warp_h2_top.png`: верхний вид warp-кандидата показывает
  длинные искривлённые волокна, а не естественную ячеистую структуру.

R-only — лучшее направление, но не принятый вариант: он сохраняет
точный 4.53 км tile-период и не проходит полный H1/H2 gate.

## G. Performance & texture fetches

1080p half, 128/6, 120 frames, warmup 20, no GBV. GPU: RTX 5070 Laptop.
Clocks/power 1402 MHz / 23.3 W / 65 °C before; 2377 MHz / 75.1 W /
85 °C after. The matrix ran under thermal drift, so absolute ms are
**indicative** and not directly comparable to the V2.3h run. Warp/plain
ratios are within the same session.

Median cloud ms (n=3–6):

| Scene | Legacy | Ref plain | Ref warp | Warp/plain |
|---|---:|---:|---:|---:|
| Clear | 4.428 | 4.043 | 5.165 | 1.28× |
| Partly | 6.466 | 3.712 | 4.914 | 1.32× |
| Overcast | 6.303 | 2.975 | 4.268 | 1.43× |
| Storm | 3.015 | 3.646 | 7.331 | 2.01×* |

`*` Storm warp has two thermal outliers (13.5 and 20.2 ms); stable
samples are 5.0–6.05 ms, i.e. ~1.4–1.7× plain. The 64/96/128 step sweep
was also contaminated by the same drift and is kept in
`evidence/timing` as raw data, not as a clean benchmark.

Texture fetches per density sample (analytic): Legacy base/macro/mid/fine
up to 4 + weather; reference plain H2/H3 = 1; reference warp = 2;
reference H4 = 2 (plain) / 3 (warp). The warp therefore adds one
dependent 3D fetch per sample. Measured cost is +28–43% on sparse
scenes and higher on dense Storm; this is a controlled but real cost.
The candidate is rejected for visual reasons first, performance second.

## H. DX12 & Legacy identity

- Release/Debug build: PASS, no C/X/LNK warnings.
- CTest Release/Debug: 1/1 PASS.
- Source checks Python/PowerShell: PASS.
- fxc reference render/probe permutations: PASS; the reference probe
  permutation includes the noise-field branch.
- Hardware selftest Legacy / reference plain / reference warp: PASS.
  Logs report `Density model requested/executed: reference`,
  `Reference stage: coverage`, `Render PSO: CloudRayMarchReference.hlsl`,
  `Probe PSO: CloudRayMarchProbeReference.hlsl`.
- WARP functional selftest Legacy / plain / warp: PASS (without GBV).
- WARP + `--gpu-validation`: exit 2, hardware-only limitation.
- Release/Debug GBV selftest Legacy / plain / warp: PASS,
  0 errors / 0 warnings.
- Release/Debug GBV 120 frames + resize, reference warp:
  0 errors / 0 warnings; 1280×720 -> 938×484 -> 1258×744,
  cloud output 469×242 -> 629×372, exit 0.
- Noise-field probe under GBV: PSO
  `CloudRayMarchProbeReference.hlsl`, Float32 readback 256×256×3,
  0 errors / 0 warnings, exit 0.
- Negative injected debug ERROR under reference warp: FAIL, exit 1.
- Legacy V2.3 pixel identity: mean 0, max 0.
- Reference rb/plain H2 and H3 vs V2.3h: mean 0, max 0.
- Shutdown: GPU idle, non-device objects 0; only the device refcount
  warning.

## I. Verdict

**BLOCKED.**

A01 локализовал причину:

1. Крупные повторяющиеся ячейки — тайлинг NoiseVolume с периодом
   **~4.53 км** (autocorrelation 0.998–0.999). Это не coverage, не
   weather map и не lighting.
2. **Поправка к V2.3h:** «вертикальные шторы» в YZ-срезе в основном
   созданы aspect ratio диагностического среза (8 км × 1.35 км в
   256×256, увеличение 5.93:1). В мировых метрах поле мельче по Y:
   V half 179 м против H half 500 м, world V/H 0.49, а не 2.9.
   Вертикальной экструзии поля нет. Реальная проблема B-канала — мелкая
   горизонтальная ячейка (half 281 м против 531 м у R) и регулярный
   мелкий рисунок; финальный рендер требует повторной визуальной
   проверки с aspect-corrected slices, но объяснять дефект вертикальной
   растяжкой нельзя.
3. RB mix наследует мелкую ячеистость B; shape transfer усиливает
   эффект, выделяя гребни. При matched mean transfer world V/H:
   R 0.419, B 0.545, RB 0.492. Пиксельные 2.48/3.23/2.92 — это
   умноженный на 5.93 aspect-ratio среза.
4. Один ограниченный warp-кандидат снижает короткий tile-пик, но
   переносит корреляцию на ~13.6 км, увеличивает world V/H
   (0.492 -> 0.598) и добавляет диагональные смазы. Для H1/H2 этого
   недостаточно.

R-only — самый обещающий из трёх каналов: при matched mean density/OD он
даёт наименьший world V/H (0.419), более широкие массы и меньше мелких
ячеек, чем B-only (0.545) и RB (0.492). Но R-only всё равно сохраняет
точный tile-период 4.53 км (autocorrelation 0.999) и не проходит полный
H1/H2 gate; поэтому он **не** объявляется новым default и не
принимается как замена.

Legacy V2.3 остаётся default. Reference-модель не принимается; H3/H4 не
маскируют дефект. Следующий шаг — не очередной threshold/coverage, а
изменение самого поля: использовать R как основную форму, B — только
для ограниченной erosion, и заменить точный tiled Worley на
decorrelated multi-frequency 3D sampling без периода 4.53 км. H5/H6 не
начинать до успешного H1/H2.

V2 PASS не присваивался. Commit/push/merge не выполнялись.
