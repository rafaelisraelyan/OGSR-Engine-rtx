# V2.2b — Noise Artifact Isolation & Cloud Quality

Статус: **READY FOR V2.2b INDEPENDENT REVIEW**.  
V2 PASS не присваивался. Ветка `feature/dx12-volumetric-clouds`, V1 baseline
`42b8124e5`; V2.2 checkpoint — `OGSR_V2_2_Checkpoint.zip`.

## 1. Diagnostics — где появляется регулярная структура

Одинаковые seed `1337`, weather seed `7331`, camera, time и presets.

### Raw NoiseVolume

Добавлен `--noise-slice-capture=DIR` и `--slice-axis=xy|xz|yz`. Сырые
XY/XZ/YZ-срезы 128³ показывают нормальные Perlin/Worley-ячейки; регулярных
chevron-полос в исходном томе нет.

### Density и параметры

- `--ablation=no-warp` — полосы заметно уменьшаются.
- `--ablation=macro-only` — density становится гладкой; detail/fine являются
  источником значительной части полос.
- `--ablation=no-base` и `no-mid` — полосы остаются; base/mid не корень.
- `--ablation=no-fine` — полосы остаются; один fine не корень.
- `--vertical-noise=1.0` vs `6.0` — при высоком vertical scale полосы резко
  усиливаются. Это главный параметрический триггер.
- `--steps=32/64/128` — при 32/64 step-aliasing даёт полосы/зерно; при 128 с
  детерминированным jitter картинка стабильна.
- half/full — full убирает часть upscale-мягкости, но не источник полос.
- 64³/128³/256³ — 256³ улучшает детали, но не устраняет overcast-полосы;
  проблема не только в разрешении.
- `--ablation=no-shadow` — полосы остаются; lighting не является источником.
- Density vertical slice (`--debug-mode=density-slice`) при высоком vertical
  scale показывает слоистую структуру; после снижения vertical scale и
  отключения detail/fine в плотной сердцевине — гладкую.

### Вывод и минимальное исправление

Источник — комбинация **высокой вертикальной частоты 3D-текстуры** и
**дискретного ray marching**. Исправление:

1. `verticalNoiseScale` снижен: cumulus/partly/sunset 1.2, overcast/rain/storm
   2.0–2.2 (вместо 5–7.5).
2. Detail/fine убраны из основного density core; low-frequency Worley `mid`
   даёт ограниченную эрозию границ. High-frequency пути сохранены в шейдере
   как diagnostic-only для будущих Cirrus/Altocumulus.
3. Domain warp уменьшен до 0.12 noise units; rotation удалён.
4. Добавлен детерминированный 1-spp ray jitter; default cloud steps = 128.
5. 256³ остаётся опцией, но не обязательным исправлением.

## 2. GPU profiling warmup

`--frames=120 --warmup=20` теперь даёт **ровно 100** samples. Reset выполняется
до первого измеряемого кадра, pending warmup samples очищаются через
`ResetStatistics()`/`pending_.fill(false)`. Проверено assertion в evidence
script.

## 3. Weather map validation

- A-канал периодичен (integer front frequency).
- `WeatherMapSeamOk` считает seam/neighbor/gradient metrics по каждому RGBA
  каналу.
- GPU `ProbeWeatherSampler` проверяет sampling при `+1` tile shift.
- Selftest добавляет camera pan через world-space boundary и печатает
  per-channel seam ratios.
- Hardware probe: 0; WARP: `0.000122547` при threshold `5e-4`.

## 4. Lighting

- `--ablation=no-shadow` используется для проверки: полосы не исчезают,
  значит lighting не маскирует density defect.
- Overcast/storm ambient and self-shadow проверены отдельно; overcast base
  остаётся тёмным, но геометрия читается.
- Атмосферная перспектива: distance fade сохранён, без нового postprocess.

## 5. Evidence

- `artifacts/v22b_final_evidence/diagnostics/` — noise slices, no-warp,
  macro-only, no-base/fine/shadow, vertical 1/6, steps 32/64/128, half,
  64/128/256, density slices.
- `artifacts/v22b_final_capture/720p_half/`, `720p_full_128/`, `1080p_half/`
  — по 33 PNG.
- `ab_v1_v21_v22.png` — V1/V2.1/V2.2b с одинаковыми камерами, seed, time,
  128 steps.
- `contact_sheet.png`.
- `v22b_final_evidence/ablation.txt`, `performance.txt`,
  `performance_full.txt` (медиана 3 прогонов), `selftest*.txt`,
  GBV/resize/negative logs.
- `noise_slices` содержит XY/XZ/YZ по каналам Perlin/Worley/Combined.

## 6. Performance (GPU timestamps, median of 3 runs)

Методика: `--frames=120 --warmup=20` → ровно 100 samples на запуск,
`scripts/measure_v22b_performance.ps1`, 3 повтора, в таблице медиана.
Laptop GPU (RTX 5070 Laptop) не держит фиксированные частоты: разброс между
сессиями достигает 2× на 720p и ~15–40% на 1080p. Сравнивать имеет смысл
только строки внутри одного прогона матрицы; это индикативные числа, не
бенчмарк.

Half-resolution cloud output:

| Resolution | Preset | Steps | Cloud ms | Total ms |
|---|---|---:|---:|---:|
| 1280×720 | Clear | 32 | 1.192 | 1.284 |
| 1280×720 | Clear | 64 | 1.836 | 1.898 |
| 1280×720 | Clear | 128 | 2.923 | 2.967 |
| 1280×720 | Partly | 32 | 1.970 | 2.026 |
| 1280×720 | Partly | 64 | 2.877 | 2.937 |
| 1280×720 | Partly | 128 | 4.862 | 4.963 |
| 1280×720 | Overcast | 32 | 1.568 | 1.740 |
| 1280×720 | Overcast | 64 | 2.431 | 2.731 |
| 1280×720 | Overcast | 128 | 3.695 | 3.878 |
| 1280×720 | Storm | 32 | 0.688 | 0.749 |
| 1280×720 | Storm | 64 | 1.306 | 1.505 |
| 1280×720 | Storm | 128 | 2.514 | 2.836 |
| 1920×1080 | Clear | 32 | 3.394 | 3.566 |
| 1920×1080 | Clear | 64 | 5.247 | 5.385 |
| 1920×1080 | Clear | 128 | 7.723 | 7.846 |
| 1920×1080 | Partly | 32 | 4.077 | 4.219 |
| 1920×1080 | Partly | 64 | 6.250 | 6.377 |
| 1920×1080 | Partly | 128 | 9.290 | 9.413 |
| 1920×1080 | Overcast | 32 | 3.734 | 3.918 |
| 1920×1080 | Overcast | 64 | 5.890 | 6.089 |
| 1920×1080 | Overcast | 128 | 9.327 | 9.521 |
| 1920×1080 | Storm | 32 | 1.710 | 1.946 |
| 1920×1080 | Storm | 64 | 3.153 | 3.424 |
| 1920×1080 | Storm | 128 | 5.191 | 5.625 |

Full-resolution cloud output (тот же прогон, 3 повтора):

| Resolution | Preset | Steps | Cloud ms | Total ms |
|---|---|---:|---:|---:|
| 1280×720 | Clear | 32 | 4.699 | 4.763 |
| 1280×720 | Clear | 128 | 11.396 | 11.492 |
| 1280×720 | Overcast | 32 | 5.706 | 5.776 |
| 1280×720 | Overcast | 128 | 15.889 | 15.995 |
| 1280×720 | Storm | 32 | 2.847 | 2.963 |
| 1280×720 | Storm | 128 | 7.855 | 7.961 |
| 1920×1080 | Clear | 32 | 8.932 | 9.051 |
| 1920×1080 | Clear | 128 | 22.316 | 22.445 |
| 1920×1080 | Overcast | 32 | 10.636 | 10.756 |
| 1920×1080 | Overcast | 128 | 31.632 | 31.762 |
| 1920×1080 | Storm | 32 | 5.868 | 6.073 |
| 1920×1080 | Storm | 128 | 16.641 | 16.826 |

Все три прогона на конфигурацию — в `performance.txt` / `performance_full.txt`
(`runs cloud ms`). 128 steps — quality path: 32/64 дают заметный step-aliasing.
Ablation (`ablation.txt`) предназначена для изоляции вклада density/lighting,
а не для сравнения производительности: early-exit делает timings
нестабильными (в одном прогоне clear `none` 2.844 ms, а `no-type` 5.312 ms —
это разница состояний GPU, а не стоимости feature).

## 7. Проверки

- Clean Release/Debug — 0 warnings/errors.
- CTest Release/Debug — PASS.
- V0 tests/captures/negative matrix — PASS.
- V2.2b hardware 128 Debug/GBV, WARP 64, no-debug — PASS.
- GBV actual frame path 120 frames — 0 errors/warnings, exit 0.
- Resize с GBV — PASS.
- F5 transition / pause / F2 / resize / minimize/restore — PASS.
- Weather map RGBA seam + GPU sampler + boundary pan — PASS.
- Negative validation — exit 3, no captures.

## 8. Known issues

1. Overcast/Storm underside местами слишком тёмный и содержит крупные
   вертикальные «шторы»; это shape/lighting issue, не регулярная полосатость.
2. 128-step jitter даёт небольшую статичную зернистость; temporal pipeline
   (V5) не добавлялся.
3. 256³ повышает качество, но memory/GPU cost; default 128³.
4. `weatherEvolution` не реализован (V2.4).
5. Weather tile 4096 может повторяться на дистанции.
6. Нет cloud shadows, полной атмосферы, игрового DX12, lightning/surge.

Не V2 PASS. Merge в `prypiat` не выполнялся.
