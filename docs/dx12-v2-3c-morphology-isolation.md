# V2.3c — Cloud Morphology Root-Cause Isolation

Статус: **BLOCKED** (visual gate).
V2 PASS не присваивался. Merge в `prypiat` не выполнялся.
V2.4, lightning, surge, game DX12 не начинались.

Ветка: `feature/dx12-volumetric-clouds`, HEAD `42b8124e5`.
База: V2.3 final delta
(`OGSR_V2_3_Final_Delta.zip`, SHA256
`4118AD127570FA69102D55B43967A77D6ED6396929C4631F0BE1C79DA15CAEC5`).
Предыдущий checkpoint: `OGSR_V2_3b_Morphology_Checkpoint.zip`,
SHA256 `A8755C4CAEC2A84E2E52533C00654A4388538FA2DCD82CBD0A6BC59FBA70F173`,
статус BLOCKED.

> `docs/dx12-v-cloud-visual-target.md` и
> `docs/dx12-v-scientific-cloud-taxonomy-and-transitions.md` в репозитории
> отсутствуют. Визуальный target взят из постановки V2.3b/V2.3c и V2.3.

## A. Git state

- HEAD `42b8124e5`; V2.1–V2.3c остаются незакоммиченными в рабочем дереве.
- reset/rebase/force push/merge не выполнялись.
- Изменения V2.3c не коммитились и не пушились.

## B. Воспроизведение

Позиции и FOV воспроизведены точно; yaw/pitch/seed/time/wind в отчёте
пользователя отсутствуют, поэтому ориентация подобрана свипом и
**не является exact reproduction**.

| Сцена | Position | FOV | Steps | Ракурс |
|---|---|---:|---:|---|
| Sunset | `(476, -248, 3925)` | 57 | 128 | yaw 0, pitch 45 |
| Storm | `(-366, -140, 2056)` | 72 | 128 | yaw 225, pitch 45 |

Дополнительно сняты Overcast снизу/сверху, Cumulus снизу/сбоку,
Stratocumulus и motion-последовательности 60 кадров.

## C. Факторная изоляция Cloud Type

`no-type` разделён на пять независимых режимов; каждый меняет только один
механизм:

- `no-type-height-profile` — профиль высоты;
- `no-type-thresholds` — пороги cumulus/strat/overcast;
- `no-type-anisotropy` — вертикальная анизотропия;
- `no-type-local-bounds` — `overcastWeight` в локальных границах;
- `no-type-shape-blending` — смешивание форм.

Средняя абсолютная разница к baseline (0..255, выборка каждые 4 px):

| Вариант | Sunset final | Sunset density | Storm final | Storm density |
|---|---:|---:|---:|---:|
| legacy `no-type` | 3.196 | 6.219 | 9.768 | 30.282 |
| `no-type-height-profile` | 2.108 | 3.998 | 4.272 | 11.096 |
| `no-type-thresholds` | 3.271 | 5.646 | 5.282 | 13.793 |
| `no-type-anisotropy` | 2.526 | 4.879 | 1.908 | 7.652 |
| `no-type-local-bounds` | **0.015** | **0.062** | 2.597 | 5.723 |
| `no-type-shape-blending` | 3.470 | 6.051 | 5.729 | 15.519 |

Выводы:

1. **Sunset-столб не зависит от одного type-механизма.** Ни один из пяти
   режимов его не убирает; `local-bounds` практически не меняет кадр
   (mean 0.015), значит локальные границы/`overcastWeight` не причина.
2. **Storm wedge сильнее всего связан с shape blending и thresholds**
   (density 15.5 и 13.8). Это крупная граница форм, а не мелкая density-деталь.
3. Анизотропия и профиль меняют форму, но не устраняют ни столб, ни wedge.

Evidence: `evidence/factor_metrics.txt`, `evidence/factor_*.png`.

## D. Мировые координаты столба

Добавлен `--debug-mode=ray-profile` + `--probe-column`: для каждого экранного
ряда вдоль выбранной колонки экспортируются 128 мировых сэмплов с
компонентами density/height/profile/shaped/macro/weather/type/coverage.
Декодер: `tools/VolumetricCloudsTest/scripts/decode_ray_profile.ps1`.

Sunset, колонка x=448 (u=0.35), камера `(476,-248,3925)`, слой 650..2000,
margin 527.5:

| row | maxD | t, м | world (x,y,z) | height | type | cov | shaped | occupied t, м | optical depth |
|---:|---:|---:|---|---:|---:|---:|---:|---|---:|
| 180 | 0 | — | — | — | — | — | — | — | — |
| 260 | 0 | — | — | — | — | — | — | — | — |
| 340 | 0.117 | 2640 | (-256, 1597, 5665) | 0.70 | 0.32 | 0.55 | 0.35 | 2586..2828 | 19.1 |
| 420 | 0.418 | 2604 | (-244, 1353, 5848) | 0.49 | 0.38 | 0.92 | 0.49 | 2359..2970 | 158.3 |
| 500 | 0.381 | 2694 | (-256, 1165, 6098) | 0.35 | 0.55 | 1.00 | 0.34 | 2300..3625 | 232.6 |
| 580 | 0.418 | 2663 | (-228, 902, 6222) | 0.15 | 0.60 | 1.00 | 0.38 | 2359..4360 | 490.4 |
| 660 | 0.319 | 3708 | (-467, 1015, 7282) | 0.25 | 0.39 | 1.00 | 0.54 | 2771..4426 | 334.2 |

Контрольная колонка x=896 (u=0.70) столба не содержит: только дальний
overcast на t≈4.9–5.0 км.

Интерпретация:

- Видимый «столб» — **верхняя кромка одной крупной массы** в мире
  X≈[-470,-230], Y≈[900,1600], Z≈[5665,7285], а не вертикальная экструзия
  через весь слой.
- На верхушке (row 340) плотность занимает только 242 м луча; ниже
  накапливается 1.3–2.0 км облака. Это тонкий силуэт вершины на фоне неба.
- Type на верхушке cumulus (0.32), ниже/дальше stratocumulus/overcast
  (0.55–0.60). Это следует за XZ weather map вдоль луча, а не за высотой.
- Coverage 0.55–1.00 и weather.r 0.58–1.00: дыры покрытия и шва карты нет.
- Shaped macro density на верхушке 0.35–0.54 — гладкая пороговая
  изоповерхность, а не плотное ядро. Именно она даёт прямые «выдавленные»
  очертания.
- `no-shadow` сохраняет ту же форму (меняется только освещение), значит
  столб не создаётся self-shadowing.

Evidence: `evidence/world_probe_summary.txt`, `evidence/ray/`,
`evidence/hslice_sheet.png`.

## E. Что изменено в диагностике

- Пять независимых type-ablation режимов.
- `--debug-mode=density-horizontal` + `--slice-height` + `--slice-extent`
  (радиус 2..20 км).
- `--debug-mode=ray-profile` + `--probe-column` + CSV-декодер.
- `--top-fade` (0.02..0.30) — диагностика формы вершины.
- `--self-shadow=on|off`, camera/FOV overrides для captures.
- `CloudDensityDebug` — экспорт компонентов density из той же функции,
  что используется для рендера.

Default-путь не изменён: `topFade=0.06` равно прежней ширине
`smoothstep(topThreshold-0.06, ...)`; новые параметры используются только
в debug-ветках.

## F. Проверенные и отклонённые коррекции

| Кандидат | Результат | Стоимость/причина отказа |
|---|---|---|
| Top fade 0.12–0.24 | почти без изменений: верхушка обрывается ниже порога | 0 |
| Boundary erosion octave (high X/Y, low Z) | смягчает верх Sunset, но wedge и overcast-пластины остаются | +1 texture fetch |
| Macro two-sample blend 0.5 | не убирает столб | +1 fetch, +12–17% |
| Edge erosion 0.8 + floor 0.6 | края волнистее, пластины остаются | 0 |
| `gate-3d=0.25` | слабое изменение формы | 0 |
| Top-cut −0.05..−0.15 | укорачивает столб, но уплощает overcast | 0 |
| Широкий coverage gate 0.08..0.70 | смягчает wedge, усиливает overcast faceting | 0 |
| Medium 3D domain warp 0.18 | пластины и wedge остаются | +1 fetch |
| 3D weather warp 0.35–0.70 | визуального выигрыша нет; +8–16% | отклонён |
| Base-3d / no-base-warp / no-height-variation / no-anisotropy | столб остаётся | 0 |

Ни один кандидат не прошёл visual gate, поэтому дефолты V2.3 сохранены.

## G. Temporal stability

60-кадровые pan-slow последовательности на Sunset и Storm, фиксированный
timestep, seed 1337, weather seed 7331:

- 0 errors / 0 warnings, exit 0;
- HF energy стабильна (Sunset ≈0.35), frame-to-frame diff растёт плавно
  вместе с панорамированием, без скачков;
- столб и wedge присутствуют на всех кадрах, не появляются/исчезают
  внезапно;
- новых chevron-паттернов не возникло.

Evidence: `evidence/motion_sunset.png`, `evidence/motion_storm.png`,
`evidence/motion_metrics_*.txt`.

## H. Lighting regression

- Default-рендер побитово совпадает с V2.3: mean 0 на `clouds_cumulus_below`,
  mean 0 / max 1 из 255 на Sunset-камере (округление 8-bit).
- V2.3 lighting/atmosphere не переписывались.
- Sun-below-horizon selftest PASS.

## I. GPU performance

Interleaved A/B, 1080p, half-resolution cloud output, 128 steps, 120 frames,
warmup 20, три повтора, одна сессия:

| Preset | V2.3 median | V2.3c median | Ratio |
|---|---:|---:|---:|
| Clear | 3.933 | 4.098 | 1.042 |
| Overcast | 4.402 | 4.584 | 1.041 |
| Storm | 2.448 | 2.577 | 1.053 |

+4–5% — стоимость диагностического контура (пять наборов type-весов,
ablation-ветки, ray-profile capture, slice extent). Обязательных
дополнительных шумовых сэмплов в default-пути нет. Абсолютные ms зависят от
частот RTX 5070 Laptop; значим interleaved ratio.

Evidence: `evidence/perf_ab.txt`.

## J. DX12 validation

Явно указанная build directory:
`D:\Coding Project\OGSR ENGINE fork from rtx\tools\VolumetricCloudsTest\build-v23c-final`.

| Проверка | Результат |
|---|---|
| Clean configure Release/Debug | OK |
| CTest Release | 1/1 PASS |
| CTest Debug | 1/1 PASS |
| Release + GBV selftest | final debug errors 0, validation PASS, exit 0 |
| Debug + GBV selftest | final debug errors 0, validation PASS, exit 0 |
| Release + GBV, 120 frames + resize | 0 errors/warnings, exit 0 |
| Debug + GBV, 120 frames + resize | 0 errors/warnings, exit 0 |
| Injected debug ERROR | FAIL, exit 1 (negative test работает) |
| Shutdown/live objects | clean, exit 0 |

SHA256:

- Release EXE `BB0FFBDA97AB36518A82A75997640F37E3C6FB8020EF3BF7532B94B58CF6033B`
- Debug EXE `21B0972D4F2F2067197437CAD19F45EF6E3A9DFB4DD892E98A2915F7BA3056D8`

Source tree hash и абсолютные пути: `evidence/binary_identity.txt`.
Логи: `evidence/*.log`, `evidence/selftest_*.txt`.

## K. Известные проблемы

- Sunset: вертикальные тонкие вершины сохраняются.
- Storm: угловатый V-разрыв сохраняется.
- Overcast: крупные пластины/фаски сохраняются.
- Диагностический build стоит +4–5%.
- Полный visual gate не пройден; PASS не присваивается.
- Два target-документа из постановки отсутствуют в репозитории.

## L. Следующий шаг

1. Заменить гладкую пороговую macro-изоповерхность на многочастотную
   эрозию границы с ограничением амплитуды (не возвращая chevrons).
2. Ввести 3D height shaping с ограниченной амплитудой для вершины, отдельно
   для cumulus и storm, не уплощая overcast.
3. Сглаживать spatial type transitions на уровне weather map, а не
   постфактум смешивать density.
4. Повторить reproduction, factor matrix, world probe, motion, perf и DX12
   regression.

## M. Вердикт

**BLOCKED.** Причина: визуальный gate не пройден — столб, storm wedge и
overcast-пластины сохраняются. Технические гарантии V2.3 (lighting,
DX12 resource model, validation, default pixel identity) сохранены.
V2 PASS не присваивался; merge не выполнялся.
