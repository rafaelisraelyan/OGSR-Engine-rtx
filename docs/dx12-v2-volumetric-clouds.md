# V2 — Volumetric Cloud Quality, Morphology & Weather

Статус: **V2.1 CHECKPOINT — NOT A FINAL V2 PASS**. V1 зафиксирован коммитом
`42b8124e5`. Weather map, пресеты и переходы реализованы; визуальная
морфология остаётся промежуточной и требует V2.2.

## V2.0 — Baseline

Baseline снят с принятого V1 (до изменений V2), чистая Release-сборка:

- adapter: NVIDIA GeForce RTX 5070 Laptop GPU, 7891 MiB VRAM;
- 1280×720 и 1920×1080, 128³ noise, half-resolution cloud output
  `R16G16B16A16_FLOAT`, 120 кадров, `--no-vsync`;
- Clear Day и Overcast, 32/48/64 шага;
- captures: `artifacts/v2_baseline/capture_720p/`,
  `artifacts/v2_baseline/capture_1080p/`;
- timings: `artifacts/v2_baseline/performance_baseline.txt`.

### V1 baseline timings (GPU timestamps, average)

| Resolution | Preset | Steps | Cloud ms | Composite ms | Total ms |
|---|---|---:|---:|---:|---:|
| 1280×720 | Clear | 32 | 0.359 | 0.031 | 0.389 |
| 1280×720 | Clear | 48 | 0.562 | 0.059 | 0.621 |
| 1280×720 | Clear | 64 | 0.740 | 0.086 | 0.826 |
| 1280×720 | Overcast | 32 | 0.393 | 0.023 | 0.416 |
| 1280×720 | Overcast | 48 | 0.482 | 0.027 | 0.509 |
| 1280×720 | Overcast | 64 | 0.816 | 0.034 | 0.850 |
| 1920×1080 | Clear | 32 | 0.607 | 0.055 | 0.661 |
| 1920×1080 | Clear | 48 | 0.940 | 0.059 | 0.999 |
| 1920×1080 | Clear | 64 | 1.137 | 0.080 | 1.217 |
| 1920×1080 | Overcast | 32 | 0.707 | 0.052 | 0.759 |
| 1920×1080 | Overcast | 48 | 1.098 | 0.058 | 1.156 |
| 1920×1080 | Overcast | 64 | 1.593 | 0.063 | 1.656 |

Это baseline для A/B, а не FPS-бюджет и не обещание производительности на
других GPU. V1-машина остаётся контрольной точкой.

### Наблюдаемые дефекты V1

1. Плотность выглядит как один слой шума; крупные массы не читаются.
2. Clear Day и Overcast различаются в основном яркостью и coverage, а не
   пространственной организацией.
3. Нет weather map: одна и та же плитка шума задаёт всё небо.
4. Мелкая эрозия размывает силуэт и создаёт «мягкую кашу» на half-res.
5. Нет выраженных плотных оснований и крупных просветов.
6. Горизонт обрывается в плоскую полосу атмосферной дымки.
7. Нет различимых cumulus / stratocumulus / overcast типов.
8. Прозрачность и debug-режимы работают, но не объясняют погодную структуру.

## V2.0 — Архитектурное решение

### Weather map

Выбрана **процедурная Texture2D** `256×256`, `R8G8B8A8_UNORM`, в world space.
Она генерируется один раз на GPU compute-шейдером `WeatherGenerate.hlsl` и
обновляется только при смене weather seed/явной регенерации. Полная
per-frame регенерация запрещена и не выполняется.

Контракт каналов:

| Канал | Смысл |
|---|---|
| R | крупномасштабный coverage: плотные зоны и большие просветы |
| G | cloud type: cumulus → stratocumulus → overcast |
| B | локальная плотность / толщина массы |
| A | storm/front потенциал, крупные неоднородности |

Сэмплирование:

```hlsl
float2 weatherUv = (worldXZ - windOffsetWorldXZ) * weatherWorldSizeInv;
float4 weather = WeatherTexture.SampleLevel(LinearWrap, weatherUv, 0);
```

Карта не привязана к экрану или камере; при переносе камеры на тот же
world-offset она не «прилипает» к экрану. Сдвиг карты совпадает с V1 wind
contract, чтобы wind translation invariance сохранялась. Размер одной плитки
в world units — `4096`; это даёт несколько weather-зон в одном кадре, но
плитка может повторяться на очень большой дистанции (known issue).

### Descriptor slots

Текущий shader-visible heap — 16 слотов. V1 занимает 0..8.

| Slot | Назначение |
|---:|---|
| 0 | Noise Texture3D UAV |
| 1 | Noise Texture3D SRV |
| 2 | Noise probe UAV |
| 3..5 | Cloud UAV frame 0..2 |
| 6..8 | Cloud SRV frame 0..2 |
| 9 | Weather map UAV (generation only) |
| 10 | Weather map SRV (cloud sampling) |
| 11..15 | reserve |

Слоты 9/10 освобождаются только после `WaitForGpu` при shutdown. Во время
кадров weather map не перезаписывается. Регенерация выполняется через
`ExecuteImmediate` только на startup/явную команду, не каждый кадр.

### Resource lifetime

- Weather map создаётся до `CloudRayMarcher`, живёт всё время работы.
- Состояния: `COMMON → UNORDERED_ACCESS` (generate) `→ ALL_SHADER_RESOURCE`.
- После генерации: UAV barrier, transition, затем SRV используется compute.
- Resize swap chain не пересоздаёт weather map: её размер не зависит от окна.
- Shutdown: GPU idle → cloud passes → weather map → renderer.
- Отдельная compute queue не добавляется; все проходы идут в текущем frame
  command list или в редком immediate-проходе генерации.

### Density model

Логическая цепочка:

```
world XZ  ── wind offset ──► weather map (coverage/type/density/front)
    │
    ├── height warp (bottom/top variation)
    ├── type weights: cumulus / stratocumulus / overcast
    ├── macro 3D shape (Perlin-Worley)
    ├── detail erosion
    ├── fine erosion (limited)
    └── final density
```

- `coverage` — порог/контраст, а не просто множитель.
- `type` выбирает **разные** height profiles и разные macro-каналы.
- `front` добавляет крупную штормовую зону; ширина перехода задаётся в world
  units.
- `windSpeed=0` не двигает weather map и 3D noise.
- Weather evolution отделена от переноса ветром и должна быть медленной.

### Реализованная формула V2.1

```hlsl
weatherUv = (worldXZ - windOffsetWorldXZ) * (1 / 4096)
weather   = WeatherTexture.SampleLevel(LinearWrap, weatherUv, 0)

coverage = saturate(0.15 + globalCoverage * 0.65 + coverageBias
                    + (weather.r - 0.5) * 1.20 + frontBoost)
weatherGate = smoothstep(0.10, 0.55, weather.r + coverageBias * 0.35)

typeValue = saturate(weather.g * 0.90 + typeBias
                     + (weather.b - 0.5) * 0.08)
weights   = smooth bands cumulus / stratocumulus / overcast

height    = localHeightFraction(baseNoise, weather, heightVariation)
profile   = weighted cumulus / stratocumulus / overcast height profile
shape     = weighted macro/detail/mid Perlin-Worley threshold
density   = shape * erosion * textureDetail * profile * densityMultiplier
            * weatherGate
```

`baseNoise`, `macro`, `mid`, `detail` и `fine` — выборки одного и того же
V0 NoiseVolume; генератор V0 не переписывался. `density` неотрицательна,
`rgb` — premultiplied in-scattered radiance, `a` — transmittance.

### Реализация V2.1

- `WeatherMap` + `WeatherGenerate.hlsl`, GPU generation, readback, seam check.
- Слоты 9/10, SRV `t1` в `CloudRayMarch.hlsl`.
- `Weather` и `CloudType` debug-режимы; `M` циклически переключает 7 режимов.
- Шесть пресетов: clear/partly/overcast/rain/storm/sunset.
- Плавный переход `CloudParams` по `F5`/`1/2/3`; `P` останавливает и переход,
  и время.
- `--cloud-scale=half|full`, `--weather-seed`, `--warmup`.
- `CloudProfiler`: average + median + p95 + min/max.
- Расширенный selftest: weather map, world-space camera, cloud type, coverage
  monotonicity, transition endpoints, above/inside/below/large-world camera.

### Визуальный статус V2.1

Weather map и type mapping работают, но silhouette на ряде ракурсов остаётся
слишком «перистым»: крупные cumulus-массы, плотные основания и чистое
разделение трёх типов ещё не достигнуты. Это **не** финальный V2 PASS;
V2.2 должен заняться cumulus-профилем, эрозией, вертикальными массами и
lighting. Ограничение зафиксировано, а не скрыто.

### Presets

Целевой набор: `clear`, `partly`, `overcast`, `rain`, `storm`, `sunset`.
Переходы интерполируют `CloudParams` и weather biases; 3D NoiseVolume не
перегенерируется на каждый переход.

## V2.1 — План реализации

1. `WeatherMap` класс + `WeatherGenerate.hlsl` + fxc validation.
2. Новые слоты 9/10, root signature cloud compute, SRV sampling.
3. Weather debug mode и cloud-type debug mode.
4. Density: weather coverage, type weights, height warp, limited fine erosion.
5. Presets/CLI, captures шаблонов, selftest weather map.
6. Baseline A/B и GPU timings после изменений.

## V2.1 — Evidence

### Build and validation

- Clean Release build — 0 warnings/errors.
- Clean Debug build — 0 warnings/errors.
- CTest Release/Debug — PASS.
- V0 unit/selftest/captures/negative matrix — PASS.
- V2 selftest hardware 128 Debug Layer — PASS.
- V2 selftest hardware 128 GPU-based validation — PASS.
- V2 WARP 64³ — PASS.
- V2 no-debug 64³ — PASS.
- Actual frame path
  `--mode=clouds --size=128 --gpu-validation --frames=120 --no-vsync` —
  PASS: weather map generated, cloud compute + composite active, 0 debug
  errors/warnings, exit 0.
- Resize с GBV — PASS; 0 errors/warnings.
- V2 negative validation matrix — все exit 3, PASS отсутствует, capture
  не создаются.
- Interactive: F5 weather transition, pause/resume, F2 slices/clouds,
  resize, minimize/restore — PASS; 0 debug errors.

### Weather map

- `256×256 R8G8B8A8_UNORM`, deterministic seed `7331`.
- Normal generation: `0.2–1.0 ms` wall (startup, not per frame).
- Under GPU-based validation: ~`764 ms` (validation overhead, startup only).
- Readback: finite, `[0,1]`, non-constant R/G/B, seam check PASS,
  identical seed diff `0`, different seed mean diff `~0.277`.
- Resize swap chain не пересоздаёт карту.

### GPU timings — half-resolution

GPU timestamps, 128³ noise, half-resolution cloud output, 120 frames,
`--warmup=20`; NVIDIA RTX 5070 Laptop GPU. Average cloud/composite/total:

| Resolution | Preset | Steps | Cloud ms | Composite ms | Total ms |
|---|---|---:|---:|---:|---:|
| 1280×720 | Clear | 32 | 1.863 | 0.127 | 1.990 |
| 1280×720 | Clear | 48 | 2.849 | 0.084 | 2.933 |
| 1280×720 | Clear | 64 | 3.687 | 0.133 | 3.821 |
| 1280×720 | Partly | 32 | 2.512 | 0.082 | 2.594 |
| 1280×720 | Partly | 48 | 3.603 | 0.149 | 3.752 |
| 1280×720 | Partly | 64 | 4.961 | 0.099 | 5.060 |
| 1280×720 | Overcast | 32 | 2.437 | 0.096 | 2.533 |
| 1280×720 | Overcast | 48 | 4.618 | 0.066 | 4.684 |
| 1280×720 | Overcast | 64 | 5.389 | 0.061 | 5.450 |
| 1280×720 | Storm | 32 | 2.764 | 0.062 | 2.826 |
| 1280×720 | Storm | 48 | 3.742 | 0.047 | 3.789 |
| 1280×720 | Storm | 64 | 5.510 | 0.059 | 5.569 |
| 1920×1080 | Clear | 32 | 4.575 | 0.111 | 4.686 |
| 1920×1080 | Clear | 48 | 6.523 | 0.102 | 6.625 |
| 1920×1080 | Clear | 64 | 8.802 | 0.073 | 8.875 |
| 1920×1080 | Partly | 32 | 5.880 | 0.085 | 5.965 |
| 1920×1080 | Partly | 48 | 7.702 | 0.080 | 7.781 |
| 1920×1080 | Partly | 64 | 11.638 | 0.056 | 11.693 |
| 1920×1080 | Overcast | 32 | 6.121 | 0.089 | 6.210 |
| 1920×1080 | Overcast | 48 | 8.840 | 0.070 | 8.910 |
| 1920×1080 | Overcast | 64 | 11.530 | 0.055 | 11.584 |
| 1920×1080 | Storm | 32 | 5.940 | 0.080 | 6.020 |
| 1920×1080 | Storm | 48 | 8.786 | 0.076 | 8.861 |
| 1920×1080 | Storm | 64 | 11.477 | 0.062 | 11.540 |

Full-resolution cloud output is in `performance_full.txt`; at 1080p it reaches
`7.4–38.5 ms` cloud time for 32–64 steps. Half-resolution is the default.

Стоимость V2.1 относительно V1 (720p Clear 32: `0.389 → 1.990 ms`) выросла
из-за weather map sample, дополнительного mid/base density sampling, шести
shadow samples и type/profile blending. Это измеренный компромисс, а не
скрытая оптимизация. VRAM отдельно не измерялась; доступен только адаптерный
`vram` из DXGI.

### Visual Evidence

- `artifacts/v2_capture/720p_half/` — 23 PNG, half-res.
- `artifacts/v2_capture/720p_full/` — 23 PNG, full-res.
- `artifacts/v2_capture/1080p_half/` — 23 PNG, 1080p.
- `artifacts/v2_capture/contact_sheet.png` — 6 ключевых сцен.
- `artifacts/v2_baseline/capture_720p/`, `capture_1080p/` — V1 A/B baseline.
- Пресеты: clear, partly, overcast, rain, storm, sunset; debug weather/type/
  density/transmittance/step/occupancy; aerial и moved-camera сцены.

### Known Issues V2.1

1. Силуэты местами «перистые»/полосовые; cumulus-массы и плотные основания
   ещё не достигнуты. Это главный блокер V2.2.
2. Weather map tile 4096 world units может повторяться на большой дистанции.
3. Полноценный temporal reprojection/denoiser отсутствует; half-res upscale
   остаётся мягким.
4. Нет cloud shadows на геометрии, полной атмосферы, дождя и молний.
5. Weather map не подключена к игровому `CEnvironment`; это mock field.
6. Full-resolution вывод существенно дороже; default остаётся half-res.

## V2.1 — Verdict

**V2.1 ARCHITECTURE CHECKPOINT: PASS.**  
**V2.1 VISUAL GATE: NOT PASSED.**

Weather map, resource lifetime, presets, transitions, debug views, full/half
output, tests, GBV and profiling работают. Визуальная цель V2.1 — «в одном
кадре действительно разные области облачности, стабильные при движении
камеры» — достигнута только частично: зоны различаются, но silhouettes
слишком перистые, а cumulus/стратовые массы недостаточно объёмны. Поэтому
V2 не объявляется PASS и не отправляется как финальный `OGSR_V2_Audit.zip`;
подготовлен промежуточный V2.1 checkpoint.

Следующий шаг — V2.2: cumulus/stratocumulus профили, эрозия, плотные
основания, lighting и A/B против V1 с той же камерой. Игровой DX12/DX11,
F4.0, погодные скрипты, lightning и surge не изменялись. Merge в `prypiat` не
выполнялся.

## Границы

Не изменяются игровой DX12/DX11, `xrRenderDX12`, F4.0, GR Job System, PhysX/ODE,
погодные скрипты, lightning, surge и roadmap. Merge в `prypiat` запрещён.
V2 не объявляется PASS без независимого review.
