# V2.3b — Cloud Morphology & Visual Quality Correction

Статус: **BLOCKED** (visual gate). Техническая диагностика завершена.
V2 PASS не присваивался. Merge в `prypiat` не выполнялся.

Ветка `feature/dx12-volumetric-clouds`, HEAD `42b8124e5`.
База: V2.3 final delta (`OGSR_V2_3_Final_Delta.zip`,
SHA256 `4118AD127570FA69102D55B43967A77D6ED6396929C4631F0BE1C79DA15CAEC5`).

> `docs/dx12-v-cloud-visual-target.md` и
> `docs/dx12-v-scientific-cloud-taxonomy-and-transitions.md` в репозитории
> отсутствуют. Визуальный target взят из постановки V2.3b и V2.3.

## 1. Воспроизведение

Пользовательские позиции воспроизведены точно по X/Y/Z и FOV; yaw/pitch,
seed, time и wind offset в отчёте отсутствуют, поэтому ориентация подобрана
свипом и **не является exact reproduction**. Это явно указано в evidence.

| Сцена | Position | FOV | Steps | Sweep |
|---|---|---:|---:|---|
| Sunset | `(476, -248, 3925)` | 57 | 128 | yaw 0..315 / pitch 20,45 |
| Storm | `(-366, -140, 2056)` | 72 | 128 | yaw 0..315 / pitch 20,45 |

Выбраны ракурсы с наиболее выраженными дефектами:
`sunset yaw 0 pitch 45`, `storm yaw 225 pitch 45`.
Evidence: `evidence/repro_sunset.png`, `evidence/repro_storm.png`.

Наблюдения подтверждены: узкие вертикальные колонны/занавеси в Sunset,
крупные угловатые массы и треугольный разрыв в Storm.

## 2. Диагностическая матрица

Одна и та же камера/seed/time/weather seed, 720p full, 128 steps.
Для каждого варианта: final, raw density, density slice, no-shadow,
ambient-only, sun-only. Evidence: `evidence/diag_*_sheet.png`.

| Вариант | Что изменено | Наблюдение |
|---|---|---|
| Baseline | — | колонна и wedge присутствуют |
| A `no-base-warp` | baseNoise не влияет на local height | плотность меняется, колонна остаётся |
| B `base-3d` | base field получает Y-компоненту | форма меняется, колонна остаётся |
| C `no-warp` | domain warp off | колонна остаётся |
| D `no-type` | все типы = cumulus | **полноразмерная колонна исчезает** |
| E `no-height-variation` | weather bottom/top shift = 0 | небольшое изменение, колонна остаётся |
| F `no-mid` | без low-frequency Worley | колонна остаётся |
| G `no-fine` | fine = 0.5 | **pixel-identical**: fine не участвует в core density |
| H `no-anisotropy` | vertical anisotropy = 1 | колонна остаётся |
| I `flat-bottom` | без ragged lower edge | небольшое изменение |
| J `no-shadow` | self-shadow off | density не меняется, меняется только lighting |

Метрики vertical correlation по density slice: lag-40 correlation
sunset 0.041, storm 0.227; различия между ablation-вариантами < 5%.
Это подтверждает, что screen-space колонна не является простой
вертикальной корреляцией в плоскости camera Z.

Horizontal density slices на h=0.2/0.4/0.6/0.8 показывают broad-массы,
а не тонкие вертикальные колонны на этих высотах. Колонна в кадре —
это узкая cumulus/stratocumulus-башня на другом удалении, а не
экструзия всей плоскости. Evidence: `evidence/hslice_*_sheet.png`.

## 3. Root cause

1. **Тип/профиль, а не один шумовой фактор.** `no-type` (все cumulus)
   убирает полноразмерную колонну. Значит, колонна возникает там, где
   weather type blend выбирает stratocumulus/overcast, чьи height profiles
   тянутся почти через весь слой. Вертикально связанное macro-поле при этом
   становится «занавесью».
2. **Вертикальная связность macro-поля.** Эффективный vertical span
   macro noise ~0.3–0.5 noise units на слой; горизонтальный span больше.
   Один и тот же столб проходит снизу вверх. Повышение vertical frequency
   ломает колонну, но возвращает горизонтальные chevron-полосы
   (`macro-vertical 2.0`).
3. **Faceting overcast.** Порог низкочастотного macro-поля даёт крупные
   пластины с прямыми границами; слабая edge-эрозия их не разбивает.
   Широкий coverage gate (0.08..0.70) усиливает faceting.
4. **Storm wedge.** Это крупный разрыв покрытия/типа, а не мелкая density
   деталь. Ни один density-параметр его не убирает; широкий gate смягчает
   края, но делает overcast пластинчатым.
5. **2D base height warp и weather height variation** дают вклад, но не
   являются единственной причиной: ablation A/E меняют кадр слабо.

## 4. Что проверено и не принято

- Macro two-sample blend 0.5 — уменьшает вертикальную связность, но
  не убирает колонну и стоит +1 texture fetch (+12–17% default).
- Edge erosion 0.8 с floor 0.6 — делает края волнистее, пластины остаются.
- `gate-3d=0.25` — слабое изменение формы.
- `top-cut -0.05..-0.15` — укорачивает колонны, но уплощает overcast и
  не убирает wedge.
- Широкий coverage gate 0.08..0.70 — смягчает storm wedge, но усиливает
  faceting overcast (хуже baseline).
- Medium 3D domain warp 0.18 — не разбивает пластины.
- Boundary-only erosion octave (high X/Y, low Z frequency) — добавляет
  мелкую текстуру, крупные пластины остаются.

Ни один вариант не прошёл visual gate. Поэтому дефолты возвращены к V2.3.

## 5. Состояние рендера

- Default path **побитово совпадает с V2.3** (mean/max diff 0 на
  `clouds_cumulus_below`).
- Диагностические ablation-режимы и horizontal slice добавлены, default
  их не включает.
- Diagnostic hooks стоят ~3–8% на overcast/storm в interleaved A/B
  (`evidence/perf_ab.txt`); это цена диагностического билда, не финального
  рендера.
- Debug/Release GBV selftests PASS; Debug/Release GBV actual 120 frames:
  0 errors/warnings, exit 0.

## 6. Следующий шаг (не реализован)

1. Заменить 2D base height warp на низкочастотное 3D height field.
2. Добавить medium-scale erosion octave, не коррелированный с macro по Y,
   с проверкой на chevrons.
3. Переделать overcast/stratocumulus profiles: 3D top/bottom вместо slab.
4. Для storm — 3D coverage/height sampling weather map, а не только XZ.
5. Повторить reproduction, diagnostic matrix, temporal sequences, perf и
   DX12 regression.

## 7. Ограничения

Sunset/Storm колонны и wedge сохраняются; overcast faceting сохраняется.
Temporal/denoiser, `weatherEvolution`, WMO-роды, lightning/surge/DXR/игровой
DX12 не реализованы. V2 PASS не присваивался; merge не выполнялся.
