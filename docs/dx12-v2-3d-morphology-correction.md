# V2.3d — Cloud Mass Geometry Correction

Статус: **BLOCKED** (visual gate).
V2 PASS не присваивался. Merge в `prypiat` не выполнялся.
V2.4, lightning, surge, game DX12 не начинались.

Ветка: `feature/dx12-volumetric-clouds`, HEAD `42b8124e5`.
База: V2.3 final delta
(`OGSR_V2_3_Final_Delta.zip`, SHA256
`4118AD127570FA69102D55B43967A77D6ED6396929C4631F0BE1C79DA15CAEC5`).
Предыдущий checkpoint: `OGSR_V2_3c_Morphology_Checkpoint.zip`,
SHA256 `4D7B2C873672A747824A092CA3E398A31F8DECDB7E26FD8FE0D56B767A752ACF`.

## A. Git state

- HEAD `42b8124e5`; V2.1–V2.3d остаются незакоммиченными.
- reset/rebase/force push/merge не выполнялись.
- V2.3d не коммитился и не пушился.

## B. Priority 1 — точная диагностика

### Float32 probe

Добавлен прямой readback структурированного буфера `float4` (3 записи на
шаг). Никакого BMP/8-bit/sRGB-преобразования в измерительном тракте.
Приложение само пишет:

- `probe_float32.bin` — `uint32 magic 'SGRP', rows, steps, records=3`,
  затем float32 `(row, step, record, component)`;
- `probe_profile.csv` — те же значения + восстановленные world XYZ;
- `probe_params.txt` — камера, yaw/pitch/FOV, seed, time, wind offset,
  layer bottom/top/margin, coverage, density, extinction, sun, atmosphere,
  ablation, jitter, lighting, probe column и **SHA256 реально запущенного
  EXE**.

Record 0: `density,height,profile,shaped`.
Record 1: `macroR,midG,weatherR,typeValue`.
Record 2: `coverage,t_normalized,profile*shaped,0`.

Проба снята тремя соседними лучами Sunset (`--probe-column=0.33/0.35/0.37`)
и одним Storm (`0.35`). EXE SHA256 во всех `probe_params.txt` совпадает с
фактическим бинарником:
`662DC25DA6E2E823E7FA8F7F31E2990E2E159B23140C5E804AE3605459CCFDB6`.

### World-space результат

Sunset, центральный луч x=448 (u=0.35), float32:

| row | maxD | t, м | world (x,y,z) | height | type | cov | optical depth |
|---:|---:|---:|---|---:|---:|---:|---:|
| 300 | 0.001 | 2604 | (-244, 1673, 5529) | 0.77 | 0.33 | 0.20 | 0.1 |
| 350 | 0.164 | 2651 | (-260, 1579, 5700) | 0.68 | 0.32 | 0.75 | 33.7 |
| 400 | 0.495 | 2633 | (-253, 1428, 5819) | 0.55 | 0.35 | 0.92 | 165.4 |
| 450 | 0.319 | 2494 | (-210, 1203, 5835) | 0.37 | 0.41 | 0.93 | 163.9 |
| 500 | 0.380 | 2658 | (-247, 1147, 6069) | 0.33 | 0.55 | 1.00 | 232.8 |
| 600 | 0.409 | 2676 | (-225, 846, 6264) | 0.11 | 0.58 | 1.00 | 547.5 |
| 650 | 0.345 | 3483 | (-415, 977, 7062) | 0.25 | 0.41 | 1.00 | 377.8 |

### Local 3D slices

Срезы вокруг X≈−250, Z≈5900 (2 км радиус):

- h=0.2–0.4: компактная масса, несколько ячеек;
- h=0.5–0.6: масса сужается в гребень;
- h=0.7–0.8: остаются отдельные фрагменты.

Вертикальные X-Y и Y-Z срезы подтверждают: это **трёхмерный сужающийся
вверх объём**, а не 2D-поле, экструдированное по Y. Прямая вертикальная
стенка отсутствует; вершина — узкий гребень, который и проецируется как
«столб».

### ROI-метрики

Sunset ROI (400,100)-(500,380), dark threshold 0.447:

| Вариант | runMean | runMedian | runP95 | straight |
|---|---:|---:|---:|---:|
| baseline | 226.6 | 235 | 256 | 0.050 |
| surface-warp 0.02 | 227.1 | 236 | 256 | 0.060 |
| medium-erosion 0.7 | 228.6 | 236 | 256 | 0.080 |
| vertical-shape 1.8 | 225.0 | 251 | 263 | 0.170 |
| top-widen 1.0 | 219.1 | 232 | 251 | 0.040 |
| type-smoothing 1.0 | 235.9 | 235 | 280 | 0.090 |
| vs1.6 + me0.4 | 218.5 | 244 | 261 | 0.180 |

Storm wedge ROI (150,0)-(450,280), bright threshold 0.275:

| Вариант | occupancy | runMean | straight |
|---|---:|---:|---:|
| baseline | 0.599 | 165.5 | 0.313 |
| type-smoothing 1.0 | 0.438 | 117.5 | 0.017 |
| top-widen 1.0 | 0.598 | 165.4 | 0.313 |

> V2.3d audit correction A02: порог стандартизирован на 0.275 (70/255).
> Более ранняя строка лога с 0.549 (140/255) устарела: при этом пороге
> sky-gap не попадает в bright-класс (occupancy 0). Все значения выше
> перемерены по исходным PNG; см. `evidence/roi_metrics_corrected.txt`.

## C. Root cause

1. **Sunset-столб — не вертикальная экструзия 2D noise.** Это верхний
   гребень компактной трёхмерной массы. Оптическая толща растёт вниз
   (0.1 → 547), верхушка тонкая; сужение начинается на h≈0.55–0.7.
2. **Тип в вершине смешанный** (cumulus 0.32–0.41, strat 0.55–0.60 ниже).
   Поэтому cumulus-only пороги/топ-биасы разбавляются весом типа и почти
   не меняют силуэт.
3. **Self-shadow не создаёт форму:** `no-shadow` сохраняет ту же
   геометрию, меняется только освещение. Тёмный столб — затенённая сторона
   тонкого гребня.
4. **Storm wedge** — крупная граница type/coverage; мелкая эрозия и
   top-widen её не меняют. Пространственное сглаживание weather/type
   уменьшает bright occupancy 0.599→0.438, но это глобальный blur
   переходов, а не локальная геометрическая коррекция.
5. **Overcast plates** остаются следствием порога низкочастотного
   macro-поля; ни один V2.3d кандидат их не устраняет.

## D. Проверенные кандидаты

| Кандидат | Наблюдение | Вердикт |
|---|---|---|
| Bounded 3D macro surface warp 0.005–0.02 | слабое изменение, столб остаётся | отклонён |
| Medium-frequency boundary erosion 0.3–0.7 | края волнистее, столб/wedge остаются | отклонён |
| Medium-frequency top jitter 0.3–1.0 | почти без изменений | отклонён |
| Cumulus vertical shape 1.4–2.2 | укорачивает столб, но создаёт hairline-линии | отклонён |
| Cumulus top bias 0.04–0.16 | разбавляется типом, без эффекта | отклонён |
| Cumulus threshold bias −0.05…−0.20 | расширяет все cumulus, меняет покрытие | отклонён |
| Top-widen 0.5–1.0 | runMean −3%, вершина остаётся | отклонён |
| Weather/type smoothing 0.5–1.0 | wedge мягче (0.599→0.438), но глобальный blur | отклонён |
| vs1.6 + me0.4 | hairline и слабое улучшение | отклонён |

Все параметры возвращены в 0; default-рендер побитово совпадает с V2.3
(mean 0 на `clouds_cumulus_below`).

## E. Изменения в диагностике

- `CloudProbeReadback`, `ReadbackProbe`, структурированный UAV `u1`,
  readback-буфер и `probe_float32.bin`.
- Отдельная shader-перестановка `CloudRayMarchProbe.hlsl` (`OGSR_PROBE=1`)
  для probe и slice-режимов. Default PSO компилирует lean
  `CloudRayMarch.hlsl` без probe UAV и slice-веток.
- `--debug-mode=density-vertical-z` (Y-Z срез вокруг камеры).
- `--slice-extent` (2–20 км), `--probe-column`, автоматический экспорт
  параметров и SHA256.
- `--top-fade` остаётся диагностикой (default 0.06 = исходная ширина).

## F. GPU performance

Interleaved, 1080p half, 128 steps, 120 frames, warmup 20, 3 повтора,
одна сессия:

| Preset | V2.3 | V2.3c | V2.3d | d/c |
|---|---:|---:|---:|---:|
| Clear | 3.884 | 4.059 | 4.000 | 0.985 |
| Overcast | 4.337 | 4.515 | 4.504 | 0.998 |
| Storm | 2.479 | 2.759 | 2.751 | 0.997 |

V2.3d не добавляет значимой стоимости к V2.3c: 0.985–0.998. Разница
V2.3d/V2.3 (+3–11%) — это факторные ablation-ветви и компонентный capture
V2.3c, а не Float32 probe. Обязательных дополнительных шумовых сэмплов в
default-пути нет.

## G. DX12 validation

Build directory:
`D:\Coding Project\OGSR ENGINE fork from rtx\tools\VolumetricCloudsTest\build-v23d-final`

| Проверка | Результат |
|---|---|
| Clean configure Release/Debug | OK |
| CTest Release / Debug | 1/1 PASS |
| Release + GBV selftest | errors 0, validation PASS, exit 0 |
| Debug + GBV selftest | errors 0, validation PASS, exit 0 |
| Release/Debug + GBV 120 frames + resize | 0 errors/warnings, exit 0 |
| Injected debug ERROR | FAIL, exit 1 |
| Shutdown | clean, exit 0 |

EXE SHA256:

- Release `662DC25DA6E2E823E7FA8F7F31E2990E2E159B23140C5E804AE3605459CCFDB6`
- Debug — см. `evidence/v23d_binary_identity.txt`.

## H. Temporal stability

60-кадровые pan-slow Sunset/Storm: 0 errors/warnings, exit 0; HF energy
стабильна, frame-to-frame diff растёт плавно с панорамированием.
Столб и wedge присутствуют на всех кадрах, без popping; новых chevron-
паттернов нет. V2.3d кандидаты не включены в default, поэтому motion
снят для принятого V2.3-рендера.

## I. Известные проблемы

- Sunset-столб сохраняется; кандидаты дают только частичное изменение.
- Storm wedge сохраняется; type smoothing улучшает ROI-метрику, но это
  глобальное размытие weather/type переходов.
- Overcast plates сохраняются.
- Диагностический build стоит +3–11% к V2.3; V2.3d-часть ~0–1.5%.
- PASS не присваивается.

## J. Следующий шаг

1. Локальная геометрическая коррекция верхнего гребня: ограниченное
   расширение изоповерхности в мире (не по типу), с проверкой на hairline.
2. Отдельная storm-морфология: разрыв покрытия формировать 3D-полем, а не
   границей type/coverage.
3. Overcast: многочастотная эрозия нижней границы без глобального blur.
4. Повторить Float32 probe, 3D slices, ROI, motion, perf и DX12 regression.

## K. Вердикт

**BLOCKED.** Причина: визуальный gate не пройден — столб, storm wedge и
overcast-пластины сохраняются. Технические гарантии V2.3 (lighting, DX12
resource model, validation, default pixel identity) сохранены.
V2 PASS не присваивался; merge не выполнялся.
