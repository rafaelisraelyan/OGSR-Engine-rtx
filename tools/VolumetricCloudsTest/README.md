# OGSR Volumetric Clouds — standalone DX12 test

Изолированный Windows-инструмент для V0 NoiseVolume/SliceViewer, V1
volumetric clouds и V2.3 lighting/atmosphere checkpoint. Не использует
X-Ray, `xrCore`, `xrRenderDX12`, `xrRender_R4`, игровую физику или материалы.
Собирается только с системными библиотеками Windows SDK.

Текущий этап — **V2.3**: clipped sun optical depth, единая геометрия плотности
для камеры и теней, skylight ambient, аналитическая атмосферная перспектива,
jitter modes, deterministic frame sequences и quality profiles.
Геометрия облаков V2.2b не менялась.

Статус: **READY FOR V2.3 INDEPENDENT REVIEW**. V2 PASS не присваивается.
Overcast/Storm «шторы» остаются shape-ограничением density; это зафиксировано
в known issues. Игровой DX11/DX12, F4.0 и погодные системы не изменяются.

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
| `--mode=clouds` | volumetric clouds (V2.3) |
| `--preset=clear` | Clear Day: разреженные облака и большие просветы |
| `--preset=partly` | Partly Cloudy: крупные массы и широкие просветы |
| `--preset=overcast` | Overcast: плотный неоднородный слой |
| `--preset=rain` | Rain Approaching: тёмный фронт |
| `--preset=storm` | Storm Clouds: массивные грозовые массы |
| `--preset=sunset` | Sunset / Low Sun: контровое освещение |
| `--steps=N` | шаги ray marching, 8..256 (quality default 128) |
| `--shadow-steps=N` | сэмплы солнечной тени, 1..16 (default 6) |
| `--sun-shadow-distance=M` | предел пути солнечного луча, метры (default 6000) |
| `--jitter=off\|spatial` | детерминированный jitter выборки (default spatial) |
| `--lighting=full\|sun-only\|ambient-only` | изоляция вкладов освещения |
| `--atmosphere=on\|off` | аналитическая атмосферная перспектива |
| `--profile=performance\|balanced\|quality\|reference` | quality/performance профили |
| `--sun-elevation=F` / `--sun-azimuth=F` | диагностический override солнца |
| `--sequence=DIR` | детерминированная последовательность кадров |
| `--motion=static\|pan-slow\|pan-fast\|up\|fly` | движение камеры в sequence |
| `--wind=on\|off` | ветер в sequence |
| `--camera=x,y,z`, `--camera-yaw=F`, `--camera-pitch=F` | стартовая камера sequence |
| `--window=WxH` | клиентский размер окна, например `1920x1080` |
| `--coverage=F` | покрытие неба 0..1 |
| `--density=F` | множитель плотности 0..2 |
| `--debug-mode=final\|density\|transmittance\|steps\|occupancy\|weather\|cloud-type\|density-slice\|sun-depth\|sun-transmittance\|shadow-diff\|atmosphere-transmittance\|shadow-path` | debug-вывод |
| `--time=SECONDS` | фиксированное время ветра для captures |
| `--weather-seed=N` | seed world-space weather map (default 7331) |
| `--ablation=MODE` | profiling/diagnostic density/shadow ablation |
| `--vertical-noise=F` | override vertical noise scale |
| `--scene=NAME` | одна сцена для диагностического capture |
| `--noise-slice-capture=DIR` | XY/XZ/YZ срезы NoiseVolume |
| `--slice-axis=xy\|xz\|yz` | ориентация V0-среза |
| `--cloud-scale=half\|full` | half или full-resolution cloud output |
| `--warmup=N` | отбросить GPU timings до кадра N |
| `--selftest` | V0 + V2.3 проверки, пишет `selftest.txt` |
| `--capture[=DIR]` | снимки; в clouds mode — 38 сцен, в slices — 10 BMP |
| `--frames=N` | ограничить число кадров |
| `--resize-test` | два resize во время `--frames` |
| `--size=64` / `--size=128` / `--size=256` | разрешение 3D NoiseVolume |
| `--seed=N` | seed шума |
| `--format=16f` / `--format=unorm` | формат 3D-объёма |
| `--adapter=N`, `--warp` | выбор адаптера / WARP |
| `--debug` | D3D12 debug layer в Release |
| `--gpu-validation` | GPU-based validation (включает debug layer) |
| `--test-no-validation` | служебный negative-тест недоступной validation |
| `--test-no-info-queue` | служебный negative-тест без `ID3D12InfoQueue` |
| `--test-inject-debug-error` | служебный negative-тест: инъекция D3D12 ERROR после cloud selftest |
| `--no-vsync` | отключить vsync |

`--selftest` возвращает 0 при PASS, 1 при functional FAIL, 3 при
INCONCLUSIVE. `--test-no-validation` и `--test-no-info-queue` предназначены
только для аудита. `--capture` и `--selftest` можно совмещать.

Validation verdict пересчитывается **после** cloud GPU work: selftest ждёт
GPU, дренирует `ID3D12InfoQueue` и проверяет накопленные сообщения. Любая
validation ERROR даёт FAIL и exit 1; `--test-inject-debug-error` — служебный
тест этого пути. Финальный отчёт содержит `final validation verdict` и
`final debug errors`.

## Управление

### Облака (`--mode=clouds`)

| Клавиша | Действие |
|---|---|
| ЛКМ + мышь | mouse look |
| `W/S/A/D/Q/E` | движение; `Shift` — быстрее |
| колесо, `Z/X` | FOV |
| `J/L`, `I/K` | азимут / высота солнца |
| `1` / `2` / `3` | Clear / Overcast / Partly Cloudy (smooth transition) |
| `F5` (+Shift) | следующий / предыдущий погодный пресет |
| `5` / `6` / `7` / `8` | final / density / transmittance / executed steps |
| `9` / `0` / `V` | occupancy / weather coverage / cloud type |
| `D` | vertical density slice |
| `M` | циклически сменить debug-режим |
| `+/-` | density |
| `[` / `]` | coverage |
| `,` / `.` | скорость ветра |
| `P` | пауза анимации и перехода погоды |
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

## Архитектура V2.3

```
world XZ ── wind offset ──► WeatherMap 256×256 RGBA8 (WRAP)
                                      │ R coverage
                                      │ G cloud type
                                      │ B local density
                                      │ A periodic storm/front
                                      ▼
                         type weights / local height / top+bottom fields
                                      │
NoiseTexture3D (V0) ──► domain-warped macro + low-frequency mid
                                      │
                                      ▼
                 ONE density geometry (camera and shadow samples)
                                      │
              clipped sun ray ──► tau_sun ──► T_sun + multiple scattering
              sky zenith/horizon ──► skylight ambient
                                      │
                                      ▼
          analytic atmosphere: T_atm, L_atm_front (weighted cloud distance)
                                      │
                                      ▼
                    CloudRayMarcher (half or full)
                                      │
                    CloudComposite (sky * T + cloudRGB)
```

- **V2.3B**: `SampleCloudDensityInternal(position)` — единственная функция
  геометрии. `SampleCloudDensity` и `SampleCloudDensityForLight` — обёртки над
  ней; debug `shadow-density-diff` доказывает совпадение (max 2.38e-7).
- **V2.3A**: солнечный луч обрезается консервативным slab и
  `--sun-shadow-distance`; `tau_sun = ∫ rho ds` на этом отрезке.
  `--debug-mode=sun-depth|sun-transmittance|shadow-path` показывают вклады.
- **V2.3C**: direct `S*T*phase`, multiple scattering
  `T + MS*(1-T)`, skylight ambient с occlusions по высоте и плотности.
  `--lighting=sun-only|ambient-only` изолирует вклады.
- **V2.3D**: `tau_atm = beta*exp(-h_avg/H)*d`, `T_atm = exp(-tau_atm)`;
  `L_atm_front = skyColor*(1-T_atm)`; `T_total = T_cloud*T_atm`.
  `--atmosphere=on|off` для A/B; при `coverage=0` кадры совпадают.
- **V2.3E**: `--jitter=off|spatial`; `--sequence` + `--motion` +
  `--camera` пишут кадры и `sequence_metrics.txt`.
- **V2.3F**: `--profile=performance|balanced|quality|reference`.
- Weather map генерируется GPU compute один раз; слоты `9/10/11` заняты,
  `0..8` сохранены за V0/V1.
- Ветер вычитается из world XZ до сэмплирования карты и 3D noise.
- `IntersectLayer` использует консервативные bounds:
  `volumeMargin = 0.5*heightVariation + 0.35*layerThickness`.
- `--debug-mode=density-slice` / `D` — вертикальный срез плотности.
- `--noise-slice-capture` + `--slice-axis` — XY/XZ/YZ срезы NoiseVolume.
- `--ablation` — density/shadow/no-warp/macro-only диагностика.
- `CloudProfiler` пишет median/p95/min/max; `--warmup=N` отбрасывает прогрев
  ровно (120 frames + warmup 20 = 100 samples).

### Ограничения V2.3

- Overcast/Storm underside содержит крупные вертикальные «шторы» — это форма
  density (V2.2b), lighting её читает, но не перестраивает.
- Полноценный temporal reprojection/denoiser не добавлялся; jitter
  детерминированный, sequences измеряют зерно, а не TAA.
- 32/64 шага дают step-aliasing; quality path — 128 steps + jitter.
- Detail/fine убраны из core density (диагностические пути сохранены).
- `weatherEvolution` reserved for V2.4, не работает.
- Нижняя полусфера остаётся атмосферной дымкой без геометрии.
- Weather tile 4096 может повторяться на дистанции.
- Нет cloud shadows, молний, выбросов, DXR, игрового DX12.

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

GPU timestamps, 1920×1080, 120 кадров, warmup 20 (100 samples), burn-in 30 s,
3 повтора, конфигурации чередуются; clocks/power/temp пишутся через
`nvidia-smi` (`scripts/measure_v23_performance.ps1`). Laptop RTX 5070
чередует power state 25–112 W, разброс внутри конфигурации до 5.8×.
В таблице — best-case (наименее throttled) cloud ms; сравнивать можно только
строки одного прогона.

| Profile | Steps | Shadow | Scale | Clear | Overcast | Storm |
|---|---:|---:|---|---:|---:|---:|
| performance | 64 | 2 | half | 1.931 | 1.561 | 0.685 |
| balanced | 96 | 4 | half | 2.655 | 3.054 | 1.509 |
| quality | 128 | 6 | half | 4.178 | 4.790 | 2.642 |
| reference | 128 | 6 | full | 12.597 | 16.849 | 9.528 |

Quality — рекомендуемый режим; reference (full-res) в 3–3.5× дороже.
V2.2b vs V2.3, interleaved в одной сессии, 1080p half 128:
Clear 3.489 → 3.926 ms (+13%), Overcast 4.322 → 4.337 (+0.3%),
Storm 2.161 → 2.520 (+17%). Стоимость нового освещения — +0–17%.

Step sweep (1080p half, shadow 6): 32/64/96/128 = 1.524/2.545/3.437/4.213 ms
(Clear), 1.707/2.794/3.837/4.770 ms (Overcast).
Shadow sweep (128 steps): 1/2/4/6 = 2.907/3.205/3.684/4.149 ms (Clear),
2.343/2.844/3.861/4.627 ms (Overcast).

Полные данные: `artifacts/v23_final_evidence/performance_v23.txt`,
`performance_v23_summary.txt`, `performance_v22b_vs_v23.txt`.

## Evidence

- `artifacts/v23_final_evidence/` — V2.3 selftests, GBV/negative logs,
  `performance_v23*.txt`, `performance_v22b_vs_v23.txt`,
  `temporal_metrics.txt`, `lighting_ab.txt`.
- `artifacts/v23_lighting/` — lighting/atmosphere/sun-path A/B sheets.
- `artifacts/v23_sequences/` — 12 последовательностей по 30 кадров + metrics.
- `artifacts/v23_final_capture/` — 38 сцен × 720p half, 720p full, 1080p half.
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
