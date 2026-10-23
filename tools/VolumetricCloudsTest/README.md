# OGSR Volumetric Clouds — standalone DX12 test

Изолированный Windows-инструмент для V0 NoiseVolume/SliceViewer и V1
volumetric clouds. Не использует X-Ray, `xrCore`, `xrRenderDX12`,
`xrRender_R4`, игровую физику или материалы. Собирается только с системными
библиотеками Windows SDK.

Текущий этап — **V1**: ограниченный облачный слой, DX12 compute ray marching,
Beer–Lambert, Henyey–Greenstein, Clear/Overcast пресеты, compositing и GPU
timestamps. V0 срезы шума сохранены и являются режимом по умолчанию для
обратной совместимости. Игровой DX11/DX12 и F4.0 не изменяются.

Статус V1 после audit correction: **READY FOR V1 FINAL DELTA REVIEW**.
PASS не присваивается до независимой приёмки.

## Сборка

Требования: Windows 10/11 x64, Visual Studio 2022 (Desktop C++), Windows SDK
10.0.19041 или новее, CMake 3.21+.

```powershell
cmake -S tools/VolumetricCloudsTest -B tools/VolumetricCloudsTest/build `
  -G "Visual Studio 17 2022" -A x64
cmake --build tools/VolumetricCloudsTest/build --config Release
ctest --test-dir tools/VolumetricCloudsTest/build -C Release --output-on-failure
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe --mode=clouds
```

HLSL-файлы копируются в `shaders/` рядом с EXE и загружаются во время
выполнения. При сборке, если найден `fxc.exe`, все entry points дополнительно
проверяются на этапе build.

## Режимы

| Команда | Назначение |
|---|---|
| `--mode=slices` | V0 просмотр срезов шума; режим по умолчанию |
| `--mode=clouds` | V1 volumetric clouds |
| `--preset=clear` | Clear Day: умеренная облачность, светлые объёмы |
| `--preset=overcast` | Overcast: плотный слой, тёмные основания |
| `--steps=N` | шаги ray marching, 8..128 (обычно 32/64) |
| `--window=WxH` | клиентский размер окна, например `1920x1080` |
| `--coverage=F` | покрытие неба 0..1 |
| `--density=F` | множитель плотности 0..2 |
| `--debug-mode=final\|density\|transmittance\|steps\|occupancy` | debug-вывод |
| `--time=SECONDS` | фиксированное время ветра для captures |
| `--selftest` | V0 + V1 проверки, пишет `selftest.txt` |
| `--capture[=DIR]` | снимки; в clouds mode — 11 сцен, в slices — 10 BMP |
| `--frames=N` | ограничить число кадров |
| `--resize-test` | два resize во время `--frames` |
| `--size=64` / `--size=128` | разрешение 3D NoiseVolume |
| `--seed=N` | seed шума |
| `--format=16f` / `--format=unorm` | формат 3D-объёма |
| `--adapter=N`, `--warp` | выбор адаптера / WARP |
| `--debug` | D3D12 debug layer в Release |
| `--gpu-validation` | GPU-based validation (включает debug layer) |
| `--test-no-validation` | служебный negative-тест недоступной validation |
| `--test-no-info-queue` | служебный negative-тест без `ID3D12InfoQueue` |
| `--no-vsync` | отключить vsync |

`--selftest` возвращает 0 при PASS, 1 при functional FAIL, 3 при
INCONCLUSIVE. `--test-no-validation` и `--test-no-info-queue` предназначены
только для аудита. `--capture` и `--selftest` можно совмещать.

## Управление

### Облака (`--mode=clouds`)

| Клавиша | Действие |
|---|---|
| ЛКМ + мышь | mouse look |
| `W/S/A/D/Q/E` | движение; `Shift` — быстрее |
| колесо, `Z/X` | FOV |
| `J/L`, `I/K` | азимут / высота солнца |
| `1` / `2` | Clear Day / Overcast |
| `5` / `6` / `7` / `8` | final / density / transmittance / executed steps |
| `9` | occupancy: шаги с ненулевой плотностью |
| `M` | циклически сменить debug-режим |
| `+/-` | density |
| `[` / `]` | coverage |
| `,` / `.` | скорость ветра |
| `P` | пауза анимации ветра |
| `R` | сброс к воспроизводимой Clear Day сцене |
| `N` | перегенерация NoiseVolume |
| `4` | переключить 64³ / 128³ |
| `F2` | переключить slices / clouds |
| `H` / `F1` | справка |
| `Esc` | выход |

### Срезы (`--mode=slices`)

`1/2/3` — Perlin/Worley/Combined, стрелки и колесо — Z-срез, `Space` —
анимация, `D` — диагностика швов, `N` — перегенерация, `4` — 64/128,
`G/F/O/W/B/E/K` — параметры шума, `F2` — clouds, `H/F1` — справка.

## Архитектура V1

```
NoiseVolume (Texture3D SRV, WRAP)
        │
        ▼
CloudRayMarcher (compute, half resolution)
        │  RGBA16F: rgb = in-scattered radiance, a = transmittance
        ▼
CloudCompositor (fullscreen graphics)
        │  sky * T + cloudRGB, exposure, linear → sRGB
        ▼
Swap chain (R8G8B8A8_UNORM)
```

Новые компоненты не зависят от Win32 и игрового runtime:

```
src/
  CloudTypes.h       камера, параметры облаков, CloudFrameConstants
  CloudRayMarcher.*  3 cloud texture, UAV/SRV, compute PSO, readback
  CloudCompositor.*  fullscreen sky/composite PSO
  CloudProfiler.*    timestamp query heap, readback, средние GPU ms
shaders/
  CloudRayMarch.hlsl  density, ray/slab intersection, ray marching, lighting
  CloudComposite.hlsl sky gradient, transmittance composite, tonemap, sRGB
```

### Ресурсы и синхронизация

Используются **три half-res cloud texture**, по одной на каждый back buffer:

| Ресурс | Количество | Владение |
|---|---:|---|
| `cloudTexture[0..2]` | 3 | `CloudRayMarcher` |
| UAV + SRV descriptors | 3 + 3 | слоты 3..8 общего heap |
| upload constants | 3 | `CloudRayMarcher` |
| timestamp queries | 3 × 3 | `CloudProfiler` |
| timestamp readback | 3 области | `CloudProfiler` |

Почему это безопасно:

1. `Renderer::BeginFrame` ждёт fence именно того back buffer, который
   переиспользуется.
2. Cloud texture и upload CB этого back buffer уже не читаются предыдущим
   кадром.
3. Descriptor slots стабильны и не перезаписываются во время полёта кадра.
4. `CloudRayMarcher` ведёт явное состояние каждой texture:
   `COMMON/ALL_SHADER_RESOURCE → UNORDERED_ACCESS → ALL_SHADER_RESOURCE`.
5. Composite читает SRV только после UAV barrier и transition.
6. Resize: `Renderer::Resize` → `WaitForGpu`; затем cloud textures
   пересоздаются под новый half-res.
7. Shutdown: `WaitForGpu` до освобождения PSO/root signature/ресурсов.

`ExecuteImmediate` с полным `WaitForGpu` на каждом кадре **не используется**.
Он остаётся только для редкой генерации/readback шума и для selftest-проверок
облаков. Descriptor heap расширен с 8 до 16 слотов; V0-слоты 0–2 не менялись.

### Облачная модель

- горизонтальный слой `bottom..top`; X/Z — бесконечная периодическая плитка
  NoiseVolume, дальность луча ограничена `maxDistance`.
- Ветер задаётся в мировых единицах и вычитается **до** умножения на
  `noiseScale`:
  `noiseXZ = (worldXZ - windDirectionXZ * windSpeed * time) * noiseScale`.
  Это одинаково для macro/detail/fine и light shadow samples.
- Поддерживаются камеры ниже слоя, внутри и выше.
- Луч, параллельный границе: epsilon; снаружи — нет пересечения, внутри —
  марш до `maxDistance`.
- Ранний выход при `T < 0.005`; пустой объём даёт `T = 1`, `rgb = 0`.

Ограничение: нет конечного горизонтального объёма и weather map; повторяемость
плитки маскируется масштабом, ветром, coverage и вертикальным профилем.

### Плотность

`SampleCloudDensity(worldPosition)` использует:

- вертикальный профиль: плавное появление у нижней границы, полный объём,
  исчезновение у верхней;
- coverage как порог, а не линейное смешивание;
- low-frequency macro sample для крупных масс;
- full-frequency Perlin-Worley для эрозии;
- дополнительный fine sample для разрыва краёв;
- density multiplier, wind offset и масштаб.

Perlin/Worley не переписывались. Повторная генерация 3D-объёма при изменении
вида облаков не требуется.

### Освещение

- Направленное солнце (азимут/высота/цвет/интенсивность).
- Single scattering + Beer–Lambert.
- Henyey–Greenstein phase function.
- 4 шага к солнцу для приблизительного self-shadowing.
- Ambient sky contribution.
- Clear Day и Overcast — два воспроизводимых пресета.
- Multiple scattering, temporal reprojection и denoiser в V1 отсутствуют.

### SDR composite

Cloud texture хранит линейные `rgb` (in-scattered) и `a` (transmittance).
Composite: `L = sky * T + cloudRGB`; alpha blending не используется, двойного
умножения на alpha нет. Exposure и tone mapping выполняются в линейном
пространстве, затем явное linear → sRGB перед записью в `R8G8B8A8_UNORM`.
Нижняя полусфера — холодная атмосферная дымка, а не геометрия уровня; это
известное ограничение standalone V1.

### GPU timestamps

`CloudProfiler` использует `D3D12_QUERY_HEAP_TYPE_TIMESTAMP`, три timestamp
на кадр: start, после cloud compute, после composite. `ResolveQueryData`
пишет в readback-область кадра; CPU читает её только после fence завершения
этого кадра. Логируются cloud ms, composite ms, total ms. WARP используется
для проверки корректности, не для сравнения производительности.

## Валидация

`--selftest` сохраняет V0-проверки:

- значения и периодичность шума, direct GPU periodicity probe;
- WRAP sampler против CPU-трилинейной интерполяции;
- детерминизм, seed sensitivity;
- debug layer errors;
- validation preflight с exit code 3 при недоступной запрошенной validation.

Дополнительно V1 проверяет на GPU readback:

- cloud output finite, без NaN/Inf;
- Clear Day содержит видимый объём;
- `coverage=0` даёт пустой полностью прозрачный объём;
- увеличение extinction уменьшает transmittance;
- фиксированные camera/seed/time дают идентичный результат;
- движение камеры и солнца меняет изображение;
- wind translation invariance: t=0 и t=1 с камерой, сдвинутой на
  `windDirection * windSpeed`, совпадают в пределах half-float;
- windSpeed=0 не меняет поле; противоположное направление ветра проверяется
  отдельно;
- Step Count считает реально выполненные итерации с early exit; Occupancy —
  отдельный режим;
- transmittance debug-режим валиден.

Negative-validation сценарии (`--debug --test-no-validation`, отсутствие
`ID3D12InfoQueue`, combined `--selftest --capture`) возвращают exit code 3,
не дают PASS и не создают capture-артефакты.

GPU-based validation проверяется не только на selftest, но и на фактическом
frame path:

```powershell
OGSRCloudsTest.exe --mode=clouds --size=128 --gpu-validation --frames=120 --no-vsync
```

Ожидается `gpu requested=1 enabled=1`, строка
`cloud frame path: CloudRayMarcher compute + CloudCompositor draw active`,
ненулевые cloud/composite GPU timestamps, `debug message totals: 0 errors,
0 warnings` и exit code 0. Resize и F2 slices/clouds проверяются отдельно с
тем же GBV-флагом.

Временные captures:

```powershell
OGSRCloudsTest.exe --mode=clouds --preset=clear --time=0 --capture=<t0>
OGSRCloudsTest.exe --mode=clouds --preset=clear --time=1 --capture=<t1>
```

При `windSpeed=0` поле не меняется; при ненулевом — меняется. Pause/resume
проверяется клавишей `P`.

## Производительность

Измерения — GPU timestamps, не CPU frame time. Пример на NVIDIA GeForce RTX
5070 Laptop GPU, 128³ noise, half-resolution cloud output, 120 кадров:

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

Это измерения конкретной машины и не являются обещанием FPS-бюджета.

## Evidence

- `artifacts/v1_evidence/` — build/CTest, hardware/WARP/GBV selftests,
  negative-validation matrix, GPU timestamp matrix, performance.txt.
- `artifacts/v1_capture/` — 720p Clear/Overcast/debug/camera captures,
  `contact_sheet.png`.
- `artifacts/v1_evidence/capture_1080p_overcast/` — 1080p Overcast captures.
- `artifacts/evidence_v0_regression/` — повторный V0 regression-прогон.
- `selftest.txt` рядом с EXE — последний отчёт.

## Известные ограничения V1

- Нет temporal reprojection/denoiser; при движении возможны мягкие
  пространственные артефакты half-res upscale.
- Нет weather map, полноценной атмосферы, cloud shadows на геометрии,
  multiple scattering, DXR и интеграции в F4.0.
- Нижняя полусфера — дымка без геометрии; горизонт выглядит условно.
- Бесконечная горизонтальная плитка NoiseVolume может давать повторяемость
  на больших дистанциях.
- WARP не отражает производительность GPU.
- Device removal не инсценируется; sentinel/DRED унаследованы от V0 и
  покрыты unit-тестом.

## V0 обратная совместимость

`--mode=slices` — режим по умолчанию. V0 selftest, captures, resize, sampler
periodicity, negative-validation и unit-тесты сохранены. V1 использует тот же
`NoiseVolume`, не меняя генератор и не добавляя зависимостей от Win32 в
cloud-компоненты.
