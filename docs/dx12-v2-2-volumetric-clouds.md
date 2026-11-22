# V2.2 — Volumetric Morphology Correction

Статус: **V2.2 CHECKPOINT — VISUAL GATE NOT PASSED**.
V2 PASS не присваивался. V1 baseline: `42b8124e5`. V2.1 checkpoint:
`OGSR_V2_1_Checkpoint.zip`, SHA256
`9975558E44B67DA59629525CF1B08A97C1B2A4180B369052B149CB17A9E2DD10`.

## Что исправлено по замечаниям аудита

### V2C-A01 — conservative volume bounds

`IntersectLayer()` больше не использует номинальные плоские
`LayerBottom/LayerTop`. В `CloudFrameConstants.weatherC.z` CPU вычисляет
консервативный margin:

```
volumeMargin = 0.5 * weatherHeightVariation + 0.35 * layerThickness
densityBottom = layerBottom - volumeMargin
densityTop    = layerTop + volumeMargin
```

`LocalHeightFraction()` больше не делает `saturate` до профиля: профиль сам
возвращает 0 вне локального объёма, поэтому локальное основание ниже
номинального и вершина выше номинальной не обрезаются. Selftest проверяет
`weatherHeightVariation=1200`; captures `clouds_extreme_height` и
`clouds_cumulus_below`/`clouds_cumulus_side`/`clouds_overcast_base` проверяют
нижнюю поверхность.

### V2C-A02 — periodic weather map, включая A

- Storm/front синусоида теперь использует целочисленный частотный вектор
  `float2(3,2)`, поэтому A-канал периодичен на uv=1.
- `WeatherMapSeamOk()` проверяет RGBA, не только RGB.
- Добавлен GPU `ProbeWeatherSampler`: сэмплирование карты и её копии после
  точного `+1` tile shift; сравниваются все 4 канала.
- Selftest: seam PASS, `weather sampler wrap probe max RGBA diff = 0` на
  hardware; на WARP `0.000122547` при пороге `5e-4`.

### V2C-A03 — ablation and cost

Добавлен test-only `--ablation`:
`none`, `no-weather`, `no-mid`, `no-base`, `no-fine`, `shadow-2`,
`shadow-4`, `no-type`.

В light path убраны base/detail 3D samples; shadow density использует
упрощённый weather+macro+mid путь. Ablation 1080p, 64 steps, half-res:

| Preset | Mode | Cloud ms | Total ms |
|---|---|---:|---:|
| Clear | none | 3.048 | 3.110 |
| Clear | no-weather | 2.931 | 3.007 |
| Clear | no-mid | 2.842 | 2.905 |
| Clear | no-base | 2.707 | 2.765 |
| Clear | shadow-2 | 3.041 | 3.113 |
| Clear | shadow-4 | 3.160 | 3.222 |
| Storm | none | 1.051 | 1.137 |
| Storm | no-weather | 0.937 | 1.005 |
| Storm | no-mid | 0.993 | 1.089 |
| Storm | no-base | 0.920 | 1.006 |
| Storm | shadow-2 | 0.449 | 0.507 |
| Storm | shadow-4 | 0.756 | 0.811 |

Ablation flags меняют density/ray termination, поэтому это не чистые
instruction-level микробенчмарки; они показывают вклад shadow marching и
texture fetches в реальный frame path. Итоговая V2.2 half-res стоимость
**ниже** V2.1 в тех же сценариях (1080p Storm 64: `11.477 → 1.075 ms`
cloud; 1080p Clear 64: `8.802 → 3.320 ms`). Причина — более ранний
transmittance early-exit, упрощённый light path и удаление лишних 3D
samples из shadow.

### V2C-A04 — weather evolution

`weatherEvolution` **не реализован** в V2.2. Поле оставлено в структуре как
reserved для V2.4; при старте пишется
`weather evolution: NOT IMPLEMENTED (reserved for V2.4)`. Оно не выдаётся
за рабочую функцию.

## Morphology V2.2

- Density coordinate: world XZ + local height, с type-specific vertical
  anisotropy (cumulus 1.0, stratocumulus/overcast 0.55).
- Domain warp + XZ rotation 0.37 rad разрывают регулярную lattice-подпись V0
  noise; warp использует уже нужный base sample, без дополнительного fetch.
- Mass field: macro Perlin-Worley + low-frequency Worley `mid` + detail.
- Top surface: 3D height field `topThreshold` от macro/mid, а не плоская
  граница.
- Base: ragged lower mask от macro/height.
- Erosion: detail/fine применяются в основном на границе (`edgeFactor`);
  плотная сердцевина не разрушается мелкой частотой.
- Density slice debug: `--debug-mode=density-slice`, клавиша `D`.
- Новые сцены: cumulus below/side/above, stratocumulus below/side,
  overcast base/aerial, storm base/aerial, extreme height.

## Проверки

- Clean Release/Debug build — 0 warnings/errors.
- CTest Release/Debug — PASS.
- V0 unit/selftest/captures/negative matrix — PASS.
- V2.2 hardware 128 Debug/GBV, WARP 64, no-debug — PASS.
- GBV actual frame path 120 frames — PASS, 0 errors/warnings.
- Resize с GBV — PASS.
- Negative validation — exit 3, PASS/captures отсутствуют.
- Weather map RGBA seam + GPU sampler probe — PASS.
- F5 transition / pause / F2 / resize / minimize/restore — PASS.
- Ablation matrix — PASS.
- A/B V1/V2.1/V2.2 с одинаковыми clear cameras/seed/time — PASS.

## GPU timings — half-resolution

120 frames, warmup 20, RTX 5070 Laptop. Average cloud/total:

| Resolution | Preset | Steps | Cloud ms | Total ms |
|---|---|---:|---:|---:|
| 1280×720 | Clear | 32 | 0.880 | 0.913 |
| 1280×720 | Clear | 64 | 1.933 | 1.974 |
| 1280×720 | Partly | 32 | 1.030 | 1.053 |
| 1280×720 | Partly | 64 | 2.069 | 2.132 |
| 1280×720 | Overcast | 32 | 0.762 | 0.793 |
| 1280×720 | Overcast | 64 | 1.553 | 1.621 |
| 1280×720 | Storm | 32 | 0.245 | 0.264 |
| 1280×720 | Storm | 64 | 0.440 | 0.470 |
| 1920×1080 | Clear | 32 | 1.698 | 1.764 |
| 1920×1080 | Clear | 64 | 3.320 | 3.384 |
| 1920×1080 | Partly | 32 | 1.893 | 1.954 |
| 1920×1080 | Partly | 64 | 3.363 | 3.438 |
| 1920×1080 | Overcast | 32 | 1.612 | 1.707 |
| 1920×1080 | Overcast | 64 | 3.204 | 3.285 |
| 1920×1080 | Storm | 32 | 0.504 | 0.564 |
| 1920×1080 | Storm | 64 | 1.075 | 1.159 |

Full-resolution 1080p Clear 32: 4.123 ms cloud; Storm 64: 3.793 ms cloud.
Полные логи: `performance.txt`, `performance_full.txt`, `ablation.txt`.

## Visual Evidence

- `artifacts/v22_final_capture/720p_full_128/` — 33 PNG, 128 steps.
- `artifacts/v22_final_capture/720p_half/` — 33 PNG, default A/B.
- `artifacts/v22_final_capture/1080p_half/` — 33 PNG.
- `ab_v1_v21_v22.png` — общие clear cameras, V1/V2.1/V2.2.
- `contact_sheet.png` — cumulus below/side, aerial, overcast base, storm,
  density slice.
- `clouds_density_slice_clear/overcast/storm.png` — vertical density slices.

## Visual status / Known Issues

V2.2 заметно улучшил крупные массы, вертикальное развитие cumulus,
нижнюю поверхность overcast и A/B относительно V1/V2.1. Однако:

1. На density-срезах и части ракурсов остаётся регулярная полосчатость
   (lattice/texture slice signature V0 128³). Это главный visual blocker.
2. Overcast underside всё ещё слишком тёмный и местами напоминает шторы;
   нужна дальнейшая работа с low-frequency vertical shape и lighting.
3. Half-res upscale мягкий; temporal/denoiser отсутствует.
4. Weather map tile 4096 может повторяться.
5. `weatherEvolution` не реализован (V2.4).
6. Нет cloud shadows, полной атмосферы, дождя/молний, игрового DX12.

**V2.2 visual gate: NOT PASSED.** V2 не объявляется PASS; merge в `prypiat`
не выполнялся. Следующая итерация — V2.2b/V2.3: убрать полосчатость
(предфильтрация/более высокое разрешение 3D density, вертикальная
фильтрация), затем lighting и атмосферная перспектива.
