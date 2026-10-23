# V1 — First Volumetric Clouds (DX12 standalone)

Статус: **READY FOR V1 FINAL DELTA REVIEW**. V1 не объявляется PASS без
независимого review.

## Baseline

- Ветка: `feature/dx12-volumetric-clouds`.
- Принятый V0 зафиксирован локальным коммитом `4835c97c2`.
- Визуальный ориентир: `docs/dx12-v-cloud-visual-target.md` из
  `origin/prypiat` (коммит `156335260`); в текущей ветке файл отсутствует.
- Игровой DX11/DX12, `xrRenderDX12`, `xrRender_R4`, физика, материалы и
  `docs/08-roadmap.md` не изменяются.

## Цель V1

Получить самостоятельный DX12 EXE с настоящими 3D-облаками: камера, облачный
слой, ray marching, освещение, compositing, управление и GPU timestamps.
Сохранить V0 NoiseVolume и SliceViewer.

## Поток кадра

```
NoiseVolume (Texture3D, SRV, WRAP)
        │
        ▼
CloudRayMarcher (compute, half resolution)
        │  RGBA16F: rgb = in-scattered radiance, a = transmittance
        ▼
CloudCompositor (fullscreen graphics pass)
        │  sky * T + cloudRGB, exposure, SDR conversion
        ▼
Swap chain (R8G8B8A8_UNORM)
```

## Ресурсы и синхронизация

### Cloud output: per-frame resources

Используются **три cloud texture**, по одной на каждый back buffer:

- `cloudTexture[0..2]`, размер `max(1, swapWidth/2) × max(1, swapHeight/2)`,
  `R16G16B16A16_FLOAT` (fallback `R8G8B8A8_UNORM`, если 16F UAV не
  поддерживается).
- `cloudUav[0..2]` и `cloudSrv[0..2]` — стабильные слоты в общем
  shader-visible heap. Дескрипторы не переписываются между кадрами.
- `constantBuffer[0..2]` — три upload-буфера, по одному на back buffer.
- `queryHeap` — 3 × 3 timestamp queries; `readback[0..2]` — отдельные
  readback-области.

Почему это безопасно:

1. `Renderer::BeginFrame` уже ждёт fence именно того back buffer, который
   будет переиспользован.
2. Следовательно, cloud texture и constant buffer этого back buffer больше
   не читаются GPU предыдущим кадром.
3. Каждый descriptor slot указывает на один стабильный ресурс; менять его
   во время полёта кадра не нужно.
4. `CloudRayMarcher` ведёт явное состояние каждой texture:
   `COMMON/ALL_SHADER_RESOURCE → UNORDERED_ACCESS → ALL_SHADER_RESOURCE`.
5. Composite читает SRV только после UAV barrier и transition.
6. Resize: `Renderer::Resize` делает `WaitForGpu`; после него
   `CloudRayMarcher::Resize` освобождает и пересоздаёт текстуры/дескрипторы.
7. Shutdown: `WaitForGpu` до освобождения PSO/root signature/ресурсов.

`ExecuteImmediate` с полным `WaitForGpu` на каждом облачном кадре **не
используется**. Он остаётся только для редкой генерации/readback шума и для
selftest-проверок облаков.

### Descriptor heap

V0 использовал 8 слотов. V1 расширяет heap до 16:

| Slot | Назначение |
|---:|---|
| 0 | Noise Texture3D UAV |
| 1 | Noise Texture3D SRV |
| 2 | Noise probe UAV |
| 3..5 | Cloud UAV frame 0..2 |
| 6..8 | Cloud SRV frame 0..2 |
| 9..15 | reserve |

Слоты 0–2 не меняются, поэтому V0-пути продолжают работать.

## Облачная модель

- Ограниченный по высоте горизонтальный слой: `bottom..top`.
- В горизонтали — бесконечная плитка; world X/Z заворачиваются через
  периодический 3D NoiseVolume. Дальность луча ограничена `maxDistance`.
- Ветер: `noiseXZ = (worldXZ - windDirectionXZ * windSpeed * time) *
  noiseScale`; смещение вычитается до масштабирования и одинаково для
  macro/detail/fine и light shadow samples.
- Поддерживаются камеры ниже слоя, внутри слоя и выше слоя.
- Луч, параллельный слою: epsilon в знаменателе; снаружи — нет пересечения,
  внутри — марш до `maxDistance`.
- Ранний выход при `T < 0.005`; пустой объём даёт `T = 1`, `rgb = 0`.

Ограничение V1: нет конечного горизонтального объёма и weather map;
повторяемость плитки маскируется масштабом, ветром, coverage и вертикальным
профилем. Это документируется как known issue, а не скрывается.

## Плотность

`SampleCloudDensity(worldPosition)`:

1. `height = saturate((y - bottom) / (top - bottom))`.
2. Вертикальный профиль: плавное появление у нижней границы, полный объём в
   середине, плавное исчезновение у верхней.
3. `coverage` задаёт порог для combined Perlin-Worley канала V0.
4. `density` — множитель; `wind` сдвигает горизонтальные координаты.
5. `erosion` уже входит в V0 combined-канал; отдельная повторная генерация
   шума не выполняется.

Плотность не является прямым отображением одного канала: профиль, порог
coverage, контраст и масштаб формируют объём.

## Освещение

- Направленный солнцевой источник: азимут, высота, цвет, интенсивность.
- Single scattering + Beer–Lambert.
- Henyey–Greenstein phase function.
- 4 шага к солнцу для приблизительного self-shadowing.
- Ambient sky contribution.
- Clear Day и Overcast — два воспроизводимых пресета.
- Никакого multiple scattering / temporal reprojection в V1.

## SDR composite

- Cloud texture хранит линейные `rgb` (in-scattered) и `a` (transmittance).
- Composite: `L = sky * T + cloudRGB`; без alpha blending и без двойного
  умножения на alpha.
- Exposure и tone mapping в линейном пространстве.
- Явное преобразование linear → sRGB перед записью в
  `R8G8B8A8_UNORM` swap chain.

## GPU profiling

- `D3D12_QUERY_HEAP_TYPE_TIMESTAMP`, 3 timestamp на кадр:
  start, после cloud compute, после composite.
- `ResolveQueryData` пишет в readback-область кадра.
- CPU читает timestamps только после fence завершения соответствующего
  кадра (в начале следующего использования back buffer или после capture
  wait).
- Логируются cloud ms, composite ms, total ms; WARP не используется для
  сравнения производительности.

## Режимы

- `--mode=slices` — сохранённый V0.
- `--mode=clouds` — V1.
- `--preset=clear|overcast`.
- `--steps=32|64`.
- `--window=WxH`.
- Debug: final, density, transmittance, executed step count, occupancy.

Default остаётся `slices` для обратной совместимости V0; V1 запускается явно
через `--mode=clouds`.

## Результаты реализации

### Baseline и Git

- Принятый V0 зафиксирован локальным коммитом `4835c97c2`; в него вошли
  только `tools/VolumetricCloudsTest/` и `docs/dx12-v0-volumetric-clouds.md`.
- Посторонние `AGENTS.md`, engine-файлы, старые ZIP и audit-документы не
  включались.
- V1-изменения оставлены в рабочем дереве; commit/push и merge в `prypiat`
  не выполнялись.
- Визуальный target `docs/dx12-v-cloud-visual-target.md` прочитан из
  `origin/prypiat` (коммит `156335260`); в текущей ветке файла нет.

### Реализованные файлы

```
src/CloudTypes.h        Camera, CloudParams, CloudFrameConstants, presets
src/CloudRayMarcher.*   per-frame cloud textures, compute PSO, readback
src/CloudCompositor.*   sky + cloud composite PSO
src/CloudProfiler.*     timestamp queries и readback
shaders/CloudRayMarch.hlsl
shaders/CloudComposite.hlsl
```

`Application` получил режимы `slices/clouds`, камеру, пресеты, debug-режимы,
управление солнцем/ветром/плотностью/coverage/FOV, resize cloud textures,
V1 selftest и V1 captures. `Renderer` расширил descriptor heap до 16 слотов.
V0 NoiseVolume, SliceViewer и AuditPolicy сохранены.

### Build Evidence

- Clean Release build — 0 warnings.
- Clean Debug build — 0 warnings.
- CTest Release — `OGSRCloudsAuditTests` PASS.
- CTest Debug — `OGSRCloudsAuditTests` PASS.
- fxc build-time validation: `CloudRayMarch` cs_5_1, `CloudComposite` VS/PS,
  V0 entry points — PASS.

Логи: `artifacts/v1_evidence/build_release_clean.log`,
`build_debug_clean.log`, `ctest_release.log`, `ctest_debug.log`.

### Validation Evidence

- Hardware 128³ V1 selftest + Debug Layer — PASS.
- Hardware 128³ V1 selftest + GPU-based validation — PASS, 0 errors/warnings.
- WARP 64³ V1 selftest — PASS.
- No-debug 64³ — PASS, verdict `NOT REQUESTED`.
- V0 slices selftest и 10 BMP captures — PASS.
- Resize 1280×720 → 938×484 → 1258×744 с пересозданием cloud textures — PASS.
- Keys/minimize/restore, preset/debug/mode switch, noise regeneration — PASS.
- Shutdown: `GPU idle`, non-device live objects 0.
- Negative validation matrix: все сценарии exit 3, PASS отсутствует, capture
  не создаётся.
- Device removal не инсценировался; sentinel/DRED унаследованы от V0 и
  покрыты unit-тестом.

Логи: `artifacts/v1_evidence/selftest_v1_hw128_debug.txt`,
`selftest_v1_hw128_gbv.txt`, `selftest_v1_warp64.txt`,
`selftest_v1_nodbg64.txt`, `v1_resize.log`, `v1_negative_*.log`.
V0 regression: `artifacts/evidence_v0_regression/`.

### V1 Final Audit Correction

**V1-A01 — wind coordinates.** Исправлен контракт:
`noiseXZ = (worldXZ - windDirectionXZ * windSpeed * elapsedSeconds) *
noiseScale`. Смещение теперь вычитается до умножения на `noiseScale`, а не
прибавляется после. Одинаковый `windOffset` используется для macro/detail/fine
и для четырёх light shadow samples. GPU-тест сравнивает t=0 с t=1 при камере,
сдвинутой на `windDirection * windSpeed`; max abs diff на half-float составил
`0.000976562` при пороге `2e-3`. Отдельно проверены `windSpeed=0`
(diff 0) и противоположное направление (diff `0.000976562`). Временные
captures t=0/t=1 сохранены, pause/resume проверен.

**V1-A02 — GBV actual frame coverage.** Выполнена команда
`--mode=clouds --size=128 --gpu-validation --frames=120 --no-vsync`.
Лог содержит `gpu requested=1 enabled=1`, `cloud frame path:
CloudRayMarcher compute + CloudCompositor draw active`, GPU timestamps
cloud/composite, `debug message totals: 0 errors, 0 warnings`, exit code 0.
Resize и F2 slices/clouds повторены с GBV; live non-device objects 0.

**V1-A03 — Step Count.** `debugMode=StepCount` теперь показывает
`executedSteps / stepCount`, где `executedSteps` увеличивается на каждой
реально выполненной итерации и учитывает early exit. Occupancy (шаги с
ненулевой плотностью) вынесена в отдельный режим `Occupancy` (клавиша `9`,
`--debug-mode=occupancy`). Selftest проверяет `executed >= occupied` и early
exit (min executed fraction `0.0208` при плотном тестовом объёме).

Логи: `v1_gbv_frames120.log`, `v1_gbv_resize_switch2.log`,
`v1delta_pause_resume.log`, `v1_temporal_t0.log`, `v1_temporal_t1.log`,
`selftest_v1_*.txt`, `temporal_t0/`, `temporal_t1/`.

### GPU Performance

GPU timestamps, 128³ noise, half-resolution cloud output, 120 кадров,
NVIDIA GeForce RTX 5070 Laptop GPU:

| Разрешение | Пресет | Шаги | Cloud ms | Composite ms | Total ms |
|---|---|---:|---:|---:|---:|
| 1280×720 | Clear | 32 | 0.362 | 0.043 | 0.405 |
| 1280×720 | Clear | 64 | 0.738 | 0.091 | 0.829 |
| 1280×720 | Overcast | 32 | 0.362 | 0.024 | 0.386 |
| 1280×720 | Overcast | 64 | 0.831 | 0.025 | 0.856 |
| 1920×1080 | Clear | 32 | 0.554 | 0.058 | 0.612 |
| 1920×1080 | Clear | 64 | 1.250 | 0.097 | 1.347 |
| 1920×1080 | Overcast | 32 | 0.761 | 0.058 | 0.818 |
| 1920×1080 | Overcast | 64 | 1.554 | 0.054 | 1.608 |

Это не FPS-бюджет и не сравнение с игровым кадром; измерения относятся к
конкретной машине. Полная таблица: `artifacts/v1_evidence/performance.txt`.

### Visual Evidence

- 720p: `artifacts/v1_capture/` — Clear Day horizon/upward, Overcast
  horizon/upward, density/transmittance/step-count debug, moved camera.
- Contact sheet: `artifacts/v1_capture/contact_sheet.png`.
- 1080p Overcast: `artifacts/v1_evidence/capture_1080p_overcast/`.
- Clear Day и Overcast различаются палитрой, плотностью, освещением и
  видимостью солнца.

### Known Issues

1. Half-resolution cloud texture с bilinear upscale даёт мягкие края; temporal
   reprojection/denoiser в V1 отсутствуют.
2. Нет weather map, cloud shadows на геометрии, полной атмосферы и multiple
   scattering.
3. Нижняя полусфера — атмосферная дымка без геометрии; горизонт условный.
4. Бесконечная горизонтальная плитка NoiseVolume может повторяться на больших
   дистанциях.
5. Нет интеграции в игровой DX12/DX11/F4.0 и нет меню/fallback игры.
6. WARP проверяет корректность, но не производительность.

## Что не входит в V1

Temporal reprojection, denoiser, weather map, cloud shadows на геометрии,
полная атмосфера, multiple scattering, DXR, интеграция в F4.0, замена 2D
облаков и меню игры.

## J. Final Verdict

**READY FOR V1 FINAL DELTA REVIEW.**

V1.1–V1.4 реализованы в standalone DX12 EXE: камера, ограниченный облачный
слой, density shaping, compute ray marching, Beer–Lambert + HG lighting,
Clear/Overcast пресеты, debug-режимы, compositing, GPU timestamps и
regression/negative evidence. Audit corrections V1-A01/A02/A03 внесены
точечно; V1 не объявляется PASS, финальное решение — за независимым
аудитором.

Ограничения перечислены выше. Игровой DX11/DX12, `xrRenderDX12`,
`xrRender_R4`, F4.0 и физика не изменялись. Merge в `prypiat`, commit и push
V1-изменений не выполнялись.
