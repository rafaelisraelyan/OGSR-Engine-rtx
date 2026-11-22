# V2.3h — Natural Cloud Density Reference Checkpoint

Статус: **BLOCKED**.
V2 PASS не присваивался. Legacy V2.3 остаётся принятым renderer baseline; reference
модель не является default и не merge-ится. H1/H2 не прошли visual gate.

Ветка: `feature/dx12-volumetric-clouds`, HEAD
`42b8124e538ef4e40229a155203c133ca412756c`.

## A. Git State & Backup

- A01 snapshot:
  `D:\Coding Project\OGSR_BACKUP_BEFORE_V23G_A01_2026-10-27`,
  MANIFEST SHA256
  `FE49DD0263EFE150F8F00169591D5114A8C55D1FA4ED6ACCE12078ACEADA3B0D`.
- A01 package: `OGSR_V2_3g_A01_Correction.zip`,
  SHA256 `99A9A7FF5A33E243CBD1C9A5EDC4F3F8846AE4FB43F6C70BA6EDC270B6648CE7`.
- V2.3h snapshot:
  `D:\Coding Project\OGSR_BACKUP_BEFORE_V23H_2026-10-27`,
  MANIFEST SHA256
  `B20FA0E97997EBE2469DB453A8D741D738C0A9C92CB1EE2BD53E88C5CB1680B5`.
- V2.3f backup MANIFEST SHA256
  `800C94580C52919208EFC9753E7A60E236803E70EC9EC1A61AD09F68EFDAEA21`.
- V2.3g snapshot MANIFEST SHA256
  `9CF86524D6091ECE9E1F3805C4176A2CF83BEC3FFF7C9A34861BBDA4C10D937F`.
- Git state during source freeze: 109 entries — 20 modified, 89 untracked,
  0 staged, 0 deleted. At handoff ZIP/SHA add two untracked entries.
  No commit/push/reset/clean/rebase/merge performed.

## B. Legacy Architecture

Цепочка V2.3:

`NoiseVolume (R Perlin / G Worley / B Perlin-Worley)`
`-> baseNoise по XZ (z=0.45)`
`-> LocalHeightFraction (height warp + weather bottom/top)`
`-> CloudHeightProfile (type-specific)`
`-> CloudTypeWeights + type thresholds`
`-> macro/mid noise по warpedCoordinate`
`-> coverage/weatherGate/front`
`-> density multiplier, top/bottom cut`
`-> primary/shadow density`
`-> ray march + lighting + atmosphere`

Классификация механизмов:

- локальная геометрия: `baseNoise` (2D XZ), `LocalHeightFraction`,
  `CloudHeightProfile`, type thresholds, top/bottom cut, macro/mid 3D noise;
- размещение: `SampleWeather`, `FrontMask`, `coverage`, `weatherGate`,
  type weights;
- оптическая плотность: `LayerShape.z`, extinction, weather density bias,
  storm/front multipliers.

Legacy не объявляется ошибочным заранее. В V2.3h он остаётся baseline и
проверяется на pixel identity. A01 probe PSO correction сохраняется:
reference probe не может молча выбрать legacy/F0.

Legacy identity: `--density-model=legacy` capture
`clouds_cumulus_below` 1280×720 full/128 против V2.3 final — mean 0, max 0.
Render PSO в логе: `CloudRayMarch.hlsl`.

## C. Reference Density Mathematics

Reference — отдельная compile-time permutation
`CloudRayMarchReference.hlsl` / probe `CloudRayMarchProbeReference.hlsl`.
Legacy и reference не смешиваются: CLI `--density-model=legacy|reference`,
стадия reference (`base|height|coverage|erosion`) передаётся в
`LightingFlags.w`. Для reference `VolumeBottom/Top` использует точные
границы слоя (`WeatherC.z = 0`), без weather height margin.

H1 — чистая 3D-форма:

```text
coord = ((world.x - wind.x) * scale,
         world.y * scale * 1.80,
         (world.z - wind.z) * scale) * 0.55
shapeField = noise.r * 0.70 + noise.b * 0.30
shape = smoothstep(0.58, 0.88, shapeField)
density = shape * densityMultiplier
```

H2 — независимый профиль по мировому Y:

```text
t = (world.y - layerBottom) / max(layerTop - layerBottom, 1)
profile = smoothstep(0, 0.12, t) * (1 - smoothstep(0.82, 1, t))
density = shape * profile * densityMultiplier
```

H3 — один монотонный coverage remap, без отдельной visibility-маски:

```text
coverage = saturate(LayerShape.y)
if coverage <= 0: density = 0
shape = saturate((shapeField - (1 - coverage)) / max(coverage, 1e-3))
density = shape * profile * densityMultiplier
```

H4 — ограниченная medium-frequency Worley erosion:

```text
detail = NoiseTexture(coord * 2.7)
edge = 1 - smoothstep(0.35, 0.75, shape)
density *= lerp(1, saturate(detail.g * 1.15), edge * 0.55)
```

H4 изменяет преимущественно границу; плотное ядро остаётся. Это один
medium-вариант; дополнительная fine-эрозия не включалась в checkpoint.

## D. Perlin–Worley Implementation

OGSR NoiseVolume:

- R — Perlin fBm;
- G — inverted Worley F1;
- B — Perlin–Worley composite с `blend=0.55`, `erosion=0.35`;
- A — 1.

Reference использует R как крупную массу и B как клеточную компоненту
(`0.70/0.30`). Каналы не интерпретируются как RGBA gd-clouds: в gd-clouds
`.r` — отдельно испечённое shape-поле, `.g/.b/.a` — Worley-октавы.
Поэтому прямая копия `texture(...).r` в OGSR дала бы неверную форму.

Математический референс: gd-clouds (fractalino, MIT):
`cloud_density.gdshaderinc` — shape remap, HeightGradient, detail erosion;
`cloud_noise.cpp` — периодический Perlin/Worley bake.
Код не копировался; использованы принципы: 3D shape, отдельный height
profile, один coverage remap, ограниченная erosion. Лицензия MIT
проверена.

## E. Height / Coverage / Erosion

- H2: профиль использует world Y и точные layer bounds; при нулевой
  толщине возвращает 0. Проверка: XZ/XY/YZ slices и side/top views.
- H3: coverage 0 даёт пустой объём (selftest PASS), coverage 0.5 —
  монотонное расширение; sweep 0/0.25/0.45/0.75/1.0.
- H4: medium erosion, strength 0.55, detail scale 2.7; сохранён A/B
  H3 vs H4. Fine-эрозия не добавлялась.

Численный reference selftest выполнен на H3 (`--reference-stage=coverage`):
`coverage=0` empty, transmittance in range, wind translation, shadow
density, sun below horizon, occupancy scaling — PASS. Для H1/H2
coverage-зависимые проверки не применяются (coverage ещё не включён).

H3 sweep, Sunset 1280×720 full/128 (density debug mean / screen density
fraction > 0.05 / final mean):

| coverage | mean density | density fraction | final mean |
|---:|---:|---:|---:|
| 0.00 | 0.0042 | 0.016 | 0.4217 |
| 0.25 | 0.0064 | 0.095 | 0.4228 |
| 0.45 | 0.0732 | 0.902 | 0.3576 |
| 0.75 | 0.1390 | 1.000 | 0.2327 |
| 1.00 | 0.1394 | 1.000 | 0.2268 |

Coverage 0 пуст; 1.6% residual в 8-bit density — фон/квантование, не
облачный объём. Отклик сильно нелинейный: 0.25 почти чисто, 0.45 уже
плотный слой, 0.75/1.0 насыщены. Это известный H3-дефект: параметр
coverage — control, а не откалиброванная доля shape-field. Нужна
квантильная калибровка; H3 не спас H1/H2.

## F. Individual Cloud Types

Not started in this checkpoint. H5 is deferred until H1–H4 visual review.

## G. Weather Reintroduction

Not started. H6 is deferred; no WeatherMap/type blending in reference H1–H4.

## H. Visual A/B

Сняты Legacy V2.3 и H1/H2/H3/H4 для Sunset, Storm, Overcast; H3 дополнительно
для Cumulus below/side и Strat. Все 1280×720, full scale, 128 steps,
seed 7331, одинаковые камеры/время/свет. Contact sheets:
`legacy_vs_ref_sunset.png`, `legacy_vs_ref_storm.png`,
`legacy_vs_ref_overcast.png`, `h12_sunset.png`, `h12_storm.png`,
`h12_overcast.png`, `h3_scenes.png`, `h3_coverage_sweep.png`.

H1/H2:

- final-изображения непрерывны и содержат крупные 3D-массы;
- YZ slices показывают повторяющиеся вертикальные «шторы»/столбы с
  регулярным горизонтальным периодом (примерно клетка Worley на 1/8
  периода шума);
- XZ slices показывают тот же повторяющийся клеточный рисунок сверху;
- H2 profile корректно меняет вертикальную огибающую, но **не устраняет**
  эту периодичность.

Это прямой провал критерия H1 «нет вертикальных колонн одинакового профиля
и регулярной повторяемости». H3 coverage лишь разрежает/закрывает поле, H4
даёт слабую (~4.7/255) граничную эрозию; ни H3, ни H4 не маскируют дефект
базовой формы и не считаются её исправлением.

Legacy сохраняет известные Sunset pillars, Storm wedge и Overcast plates.
Reference убирает часть узких legacy-столбов, но заменяет их регулярными
«шторами»/клетками; это не visual win.

H5 (individual types) и H6 (weather reintroduction) **не начинались**:
базовая H1/H2 форма не принята, поэтому добавлять типы/погоду было бы
маскировкой.

## I. Temporal Stability

Temporal smoke test (60 frames, 640×360 half, 128 steps):

| Sequence | Model | mean frame diff | max diff | mean HF |
|---|---|---:|---:|---:|
| Storm fixed camera + wind | Legacy | 0.01373 | 0.01391 | 0.64521 |
| Storm fixed camera + wind | Reference H3 | 0.01014 | 0.01036 | 0.53256 |
| Sunset slow pan + wind | Legacy | 0.32478 | 0.33535 | 0.41961 |
| Sunset slow pan + wind | Reference H3 | 0.38886 | 0.39913 | 0.49146 |

No popping/discontinuity spike in the smoke sequences; reference H3 is stable
under wind and camera motion. This does not rescue the H1/H2 geometry failure;
it only verifies the reference path is temporally usable. Full 60-frame
sequences for H1/H2 were not produced because those stages already failed the
static geometry gate.

## J. GPU Performance

1080p half, 128 steps / shadow 6, 3 runs per point, same session, no GBV.
Median cloud ms (p95 = max for n=3):

| Scene | Legacy | Reference H3 | Ratio |
|---|---:|---:|---:|
| Clear | 4.465 | 4.032 | 0.90× |
| Partly | 5.236 | 3.517 | 0.67× |
| Overcast | 5.266 | 2.446 | 0.46× |
| Storm | 3.111 | 3.844 | 1.24× |

Storm step sweep, shadow 6:

| Steps | Legacy | Reference H3 | Ratio |
|---:|---:|---:|---:|
| 64 | 1.642 | 1.439 | 0.88× |
| 96 | 2.234 | 2.370 | 1.06× |
| 128 | 3.111 | 3.844 | 1.24× |

Reference H3 uses one base noise sample plus profile/coverage (H4 would add a
second detail sample; H4 was not timed). It does not sample WeatherMap and
does not do type blending, so sparse/clear scenes are cheaper. Dense Storm
at 128/6 is **more expensive** because more samples are occupied and early
exit is lower; the ratio is +24% in this checkpoint. Absolute ms drift with
RTX 5070 Laptop clocks/thermals; same-session ratios only. GPU clocks:
1950 MHz / 27.7 W / 61 °C before, 2520 MHz / 33.6 W / 71 °C after.

Limitation: full 4 scenes × 3 steps × shadow 2/4/6 matrix was not run; the
checkpoint used 4 scenes at 128/6 plus Storm 64/96/128 at shadow 6.

## K. DX12 Validation

Build directory:
`D:\Coding Project\OGSR ENGINE fork from rtx\tools\VolumetricCloudsTest\build-v23h-h1`

- Release/Debug build: PASS, no C/X/LNK warnings.
- CTest Release/Debug: 1/1 PASS.
- Source checks Python/PowerShell: PASS.
- fxc: all cloud shader permutations, including reference render/probe, compiled.
- Hardware selftest Legacy: PASS.
- Hardware selftest Reference H3 (coverage): PASS; logs
  `Density model requested/executed: reference`, `Reference stage: coverage`,
  `Render PSO: CloudRayMarchReference.hlsl`,
  `Probe PSO: CloudRayMarchProbeReference.hlsl`.
- WARP Legacy/Reference H3 functional selftest без GBV: PASS.
- WARP + `--gpu-validation`: exit 2, hardware-only limitation.
- Release/Debug GBV selftest Legacy/Reference: PASS, 0 errors/0 warnings.
- Release/Debug GBV 120 frames + resize Legacy/Reference: 0 errors/0 warnings,
  resize 469×242 → 629×372, exit 0. Reference 120 runs were re-run explicitly
  after a script argument bug; fixed logs are included.
- Reference probe + GBV: PSO `CloudRayMarchProbeReference.hlsl`,
  0 errors/0 warnings, exit 0.
- F2/F5 Debug+GBV smoke, Legacy and Reference H3: render mode
  slices→clouds, weather transition clear→partly complete,
  0 errors/0 warnings, exit 0.
- Negative injected debug ERROR under Reference: FAIL, exit 1.
- Shutdown: GPU idle, non-device objects 0; only device refcount warning.
- Legacy final vs V2.3: mean 0, max 0.
- H1/H2 coverage-dependent selftest checks are not applicable because H1/H2
  do not enable coverage; H3 is the numerical reference selftest stage.

A01 probe rule preserved: reference probe cannot silently select legacy/F0;
all reference probe logs show `CloudRayMarchProbeReference.hlsl`.

## L. Known Issues

1. **H1/H2 visual gate FAILED.** YZ slices show repeated vertical
   curtains/columns and XZ slices show periodic cellular repetition. H2
   profile does not remove it.
2. **H3 coverage is non-linear and not calibrated.** 0.25 is nearly clear,
   0.45 is already dense, 0.75/1.0 saturate. Coverage 0 is empty in selftest,
   but the control is not a fraction of the shape-field distribution.
3. **H4 erosion is too weak to rescue the base.** Mean XY change ~4.7/255;
   it does not create natural detail or remove curtains.
4. **H5/H6 not started.** No individual cloud types or weather reintroduction;
   adding them would mask the base failure.
5. **Performance matrix reduced** (4 scenes at 128/6 + Storm 64/96/128 at
   shadow 6, n=3). Full 3×3 shadow matrix and H4 timing are deferred.
6. **Temporal only smoke-tested** for H3; H1/H2 static geometry failed first.
7. **WARP+GBV remains unsupported**; WARP functional test without GBV only.
8. A01 supplied patch and V2.3 legacy remain unchanged; no commit/push/merge.

## M. Verdict

**BLOCKED.**

H1/H2 не проходят visual gate: базовая 3D-форма содержит регулярные
вертикальные шторы/столбы и повторяющийся клеточный рисунок. H2 height
profile, H3 coverage и H4 erosion реализованы и численно проверены, но не
устраняют причину; маскировать её H5/H6 запрещено заданием. Legacy V2.3
остаётся принятым baseline и pixel-identical.

Следующий шаг — не H5/H6, а исправление базового поля: устранить
периодическую вертикальную корреляцию (другой noise channel/layout,
многомасштабная выборка или контролируемый domain warp), затем повторить
H1/H2 slices. H3 coverage calibration — после успешного H1/H2. V2 PASS не
присваивался.
