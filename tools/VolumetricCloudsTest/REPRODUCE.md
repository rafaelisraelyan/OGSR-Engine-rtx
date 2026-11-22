# V0/V1/V2.3 reproduction

Все команды выполняются из корня репозитория OGSR. Нужны Windows 10/11 x64,
Visual Studio 2022 Desktop C++, Windows SDK 10.0.19041+ и CMake 3.21+.

## 1. Build

```powershell
cmake -S tools/VolumetricCloudsTest -B tools/VolumetricCloudsTest/build `
  -G "Visual Studio 17 2022" -A x64
cmake --build tools/VolumetricCloudsTest/build --config Release
cmake --build tools/VolumetricCloudsTest/build --config Debug
ctest --test-dir tools/VolumetricCloudsTest/build -C Release --output-on-failure
```

EXE: `tools/VolumetricCloudsTest/build/Release/OGSRCloudsTest.exe`.
HLSL копируется в `shaders/` рядом с EXE. При наличии `fxc.exe` сборка
дополнительно проверяет `GenerateNoise`, `ProbePeriodicity`,
`ProbeSamplerWrap`, `FullscreenVS`, `SlicePS`, `CloudRayMarch`,
`CloudCompositePS`, `GenerateWeather`.

## 2. V2.3 cloud selftest, 128³, hardware + debug layer

```powershell
.\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe `
  --mode=clouds --selftest --debug --size=128 --seed=1337
```

Ожидаемые ключевые строки:

```text
V0 SELFTEST: PASS
V2 CLOUD SELFTEST: PASS
OVERALL SELFTEST: PASS
```

Exit code 0. В отчёте `selftest.txt` дополнительно проверяются:

- `final validation after cloud GPU work` с `final debug errors: 0`;
- cloud output finite, без NaN/Inf;
- Clear Day содержит видимый объём;
- `coverage=0` даёт `T=1`, `rgb=0`;
- увеличение extinction уменьшает transmittance;
- фиксированные camera/seed/time детерминированы;
- движение камеры и солнца меняет изображение;
- wind translation invariance: t=0 и t=1 с камерой, сдвинутой на
  `windDirection * windSpeed`, совпадают в пределах half-float;
- windSpeed=0 и противоположное направление ветра;
- Step Count считает executed iterations с early exit, Occupancy — отдельно.

### V2.3 lighting, atmosphere and temporal

```powershell
$exe = ".\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe"

# Sun path / optical depth / shadow consistency debug maps.
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sun_depth_debug --capture=artifacts\v23\depth
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_shadow_diff_debug --capture=artifacts\v23\shadow_diff
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_shadow_path_debug --sun-elevation=5 --capture=artifacts\v23\path_low

# Lighting isolation on identical geometry.
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_cumulus_below --lighting=sun-only --capture=artifacts\v23\sun
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_cumulus_below --lighting=ambient-only --capture=artifacts\v23\ambient
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_cumulus_below --ablation=no-shadow --capture=artifacts\v23\noshadow

# Atmospheric perspective on/off.
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_storm_horizon --atmosphere=on --capture=artifacts\v23\atm_on
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_storm_horizon --atmosphere=off --capture=artifacts\v23\atm_off

# Deterministic temporal sequence: 30 frames, slow pan, wind off.
& $exe --mode=clouds --preset=clear --window=1280x720 --cloud-scale=half `
  --steps=128 --camera=0,100,-1200 --camera-pitch=20 --frames=30 `
  --motion=pan-slow --wind=off --jitter=spatial --no-vsync `
  --sequence=artifacts\v23\seq_pan

# Quality profiles.
& $exe --mode=clouds --profile=performance --frames=120 --warmup=20 --no-vsync
& $exe --mode=clouds --profile=balanced  --frames=120 --warmup=20 --no-vsync
& $exe --mode=clouds --profile=quality   --frames=120 --warmup=20 --no-vsync
& $exe --mode=clouds --profile=reference --frames=120 --warmup=20 --no-vsync
```

`--shadow-steps=1..16`, `--sun-shadow-distance`, `--jitter=off|spatial`,
`--sun-elevation`, `--sun-azimuth` are available for controlled experiments.
`sequence_metrics.txt` contains high-frequency energy and frame-to-frame diff.

### V2.3b morphology diagnostics

```powershell
$exe = ".\tools\VolumetricCloudsTest\build-v22\Release\OGSRCloudsTest.exe"

# User-reported positions (orientation is unknown; this is the nearest
# controlled reproduction, not an exact screenshot match).
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 `
  --camera-yaw=0 --camera-pitch=45 --fov=57 --capture=artifacts\v23b\sunset
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_storm_horizon --camera=-366,-140,2056 `
  --camera-yaw=225 --camera-pitch=45 --fov=72 --capture=artifacts\v23b\storm

# One-factor morphology ablations.
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --ablation=no-base-warp `
  --capture=artifacts\v23b\no_base_warp
# base-3d, no-warp, no-type, no-height-variation, no-mid, no-fine,
# no-anisotropy, flat-bottom, no-shadow are also available.

# Raw density, vertical slice, horizontal slice.
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --debug-mode=density --capture=artifacts\v23b\dens
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --debug-mode=density-slice --capture=artifacts\v23b\vslice
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --debug-mode=density-horizontal --slice-height=0.4 `
  --capture=artifacts\v23b\hslice
```

### V2.3f density formula benchmark

```powershell
$exe = ".\tools\VolumetricCloudsTest\build-v23f-final\Release\OGSRCloudsTest.exe"

# F0 baseline: no formula flag, pixel-identical to V2.3.
# F1 Perlin-Worley primary shape:
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --formula=F1 --capture=artifacts\v23f\F1
# F2 per-type density blending:
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --formula=F2 --capture=artifacts\v23f\F2
# F2 cross-type difference (pre-weather, x4):
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --formula=F2 --debug-mode=cross-type-difference `
  --capture=artifacts\v23f\F2_diff
# F3 coverage separation:
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_storm_horizon --camera=-366,-140,2056 --camera-yaw=225 `
  --camera-pitch=45 --fov=72 --formula=F3 --capture=artifacts\v23f\F3
# F4 world-space XYZ noise:
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --formula=F4 --capture=artifacts\v23f\F4
# F5 controlled boundary erosion; --formula-bias is the erosion strength (0..1):
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --formula=F5 --formula-bias=0.5 `
  --capture=artifacts\v23f\F5

# Quality profiles map to 64/96/128 primary steps.
& $exe --mode=clouds --window=1920x1080 --cloud-scale=half --profile=performance `
  --preset=storm --formula=F3 --frames=120 --warmup=20 --no-vsync
& $exe --mode=clouds --window=1920x1080 --cloud-scale=half --profile=balanced `
  --preset=storm --formula=F3 --frames=120 --warmup=20 --no-vsync
& $exe --mode=clouds --window=1920x1080 --cloud-scale=half --profile=quality `
  --preset=storm --formula=F3 --frames=120 --warmup=20 --no-vsync
```

F1/F3 use `--formula-bias` as a threshold calibration in -0.20..0.20.
F5 uses it as an erosion strength in 0..1. F2 and F4 ignore it.
All formula variants are separate compile-time PSOs; `--formula=none` leaves
the V2.3 default path untouched. The formula variants reuse the existing
macro/mid/weather samples and add no texture fetch.

### V2.3e shape prototypes

```powershell
$exe = ".\tools\VolumetricCloudsTest\build-v23e-final\Release\OGSRCloudsTest.exe"

# Prototype A (deep convective: cumulus + storm) and B (layered deck:
# stratocumulus + overcast). none = accepted V2.3 baseline.
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --shape-prototype=A --capture=artifacts\v23e\A
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_overcast_base --camera=0,100,-1200 --camera-yaw=0 `
  --camera-pitch=25 --fov=62 --shape-prototype=B --capture=artifacts\v23e\B

# Density, no-shadow, macro-shape and type-transition diagnostics.
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --shape-prototype=A --debug-mode=density `
  --capture=artifacts\v23e\A_density
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --shape-prototype=A --self-shadow=off `
  --capture=artifacts\v23e\A_noshadow
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --debug-mode=macro-shape `
  --capture=artifacts\v23e\macro_shape

# Vertical slices around the Sunset defect.
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=-250,1000,5900 --camera-yaw=0 `
  --camera-pitch=0 --fov=57 --shape-prototype=A --debug-mode=density-slice `
  --slice-extent=2 --capture=artifacts\v23e\A_xy
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=-250,1000,5900 --camera-yaw=0 `
  --camera-pitch=0 --fov=57 --shape-prototype=A --debug-mode=density-vertical-z `
  --slice-extent=2 --capture=artifacts\v23e\A_yz

# 64/96/128 steps via quality profiles.
& $exe --mode=clouds --window=1920x1080 --cloud-scale=half --profile=performance `
  --preset=storm --shape-prototype=A --frames=120 --warmup=20 --no-vsync
& $exe --mode=clouds --window=1920x1080 --cloud-scale=half --profile=balanced `
  --preset=storm --shape-prototype=A --frames=120 --warmup=20 --no-vsync
& $exe --mode=clouds --window=1920x1080 --cloud-scale=half --profile=quality `
  --preset=storm --shape-prototype=A --frames=120 --warmup=20 --no-vsync
```

### V2.3d float32 world probe and local 3D slices

```powershell
$exe = ".\tools\VolumetricCloudsTest\build-v23d-final\Release\OGSRCloudsTest.exe"

# Float32 probe. The app writes probe_float32.bin, probe_profile.csv and
# probe_params.txt (camera, seed, time, wind, layer, sun, EXE SHA256).
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --debug-mode=ray-profile --probe-column=0.35 `
  --capture=artifacts\v23d\probe

# Neighboring rays: rerun with --probe-column=0.33 and 0.37.
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --debug-mode=ray-profile --probe-column=0.33 `
  --capture=artifacts\v23d\probe_left

# Local 3D slices around the defect (X=-250, Z=5900): horizontal at several
# heights, plus vertical X-Y and Y-Z cuts.
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=-250,1000,5900 --camera-yaw=0 `
  --camera-pitch=0 --fov=57 --debug-mode=density-horizontal `
  --slice-height=0.5 --slice-extent=2 --capture=artifacts\v23d\hslice
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=-250,1000,5900 --camera-yaw=0 `
  --camera-pitch=0 --fov=57 --debug-mode=density-slice `
  --slice-extent=2 --capture=artifacts\v23d\xy
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=-250,1000,5900 --camera-yaw=0 `
  --camera-pitch=0 --fov=57 --debug-mode=density-vertical-z `
  --slice-extent=2 --capture=artifacts\v23d\yz
```

`probe_float32.bin` layout: `uint32 magic 'SGRP', uint32 rows,
uint32 steps, uint32 records=3`, then float32
`(row, step, record, component)`. Record 0 is
`density,height,profile,shaped`; record 1 is
`macroR,midG,weatherR,typeValue`; record 2 is
`coverage,t_normalized,profile*shaped,0`.
`probe_profile.csv` contains the same values plus reconstructed world XYZ.
`decode_ray_profile.ps1` is retained for the legacy 8-bit bitmap probe; the
app-generated CSV is authoritative for V2.3d.

The probe and the three slice modes use the dedicated probe shader
permutation (`CloudRayMarchProbe.hlsl`). The default PSO compiles the lean
`CloudRayMarch.hlsl` without the probe UAV or slice branches, so default
rendering keeps the V2.3c cost profile.

### V2.3c factor isolation and world-space probe

```powershell
$exe = ".\tools\VolumetricCloudsTest\build-v23c-final\Release\OGSRCloudsTest.exe"

# The five independent type-factor ablations. Each changes one mechanism only.
# no-type-height-profile, no-type-thresholds, no-type-anisotropy,
# no-type-local-bounds, no-type-shape-blending
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --ablation=no-type-shape-blending `
  --capture=artifacts\v23c\shape

# Horizontal world slice with a configurable radius (2..20 km) and height.
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --debug-mode=density-horizontal `
  --slice-height=0.5 --slice-extent=8 --capture=artifacts\v23c\hslice

# Ray profile: for each output row, 128 world samples along the ray at the
# selected screen column. Channel blocks start at x=0/256/512/768.
#   block 0: density, height, profile
#   block 1: macro.r, mid.g, weather.r
#   block 2: coverage, normalized t, profile*shaped
#   block 3: shaped, typeValue
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 --camera-yaw=0 `
  --camera-pitch=45 --fov=57 --debug-mode=ray-profile --probe-column=0.35 `
  --capture=artifacts\v23c\ray

# Decoder (CSV). The exact camera/layer values are printed in the capture log
# as "world-probe params"; the defaults below match the sunset preset.
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\VolumetricCloudsTest\scripts\decode_ray_profile.ps1 `
  -Bmp artifacts\v23c\ray\clouds_sunset_horizon.bmp `
  -OutCsv artifacts\v23c\ray\ray_profile.csv -ProbeColumn 0.35
```

The profile decoder computes ray origin/direction and the conservative slab
intersection with the same formulas as `CloudRayMarch.hlsl`, then writes
`row,step,t,x,y,z,density,height,profile,shaped,macroR,midG,weatherR,typeValue,coverage`.
The capture must be `--cloud-scale=full` and the log must show
`cloud output textures created: 1280x720`; otherwise the 256-column channel
blocks are interpolated by the composite pass.

### V2.2b diagnostics and ablation

```powershell
$exe = ".\tools\VolumetricCloudsTest\build-v22\Release\OGSRCloudsTest.exe"
# XY/XZ/YZ raw NoiseVolume slices
& $exe --mode=slices --size=128 --noise-slice-capture=artifacts\v22b\noise
# same camera/seed/time, one scene at a time
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_cumulus_below --capture=artifacts\v22b\baseline
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_cumulus_below --ablation=no-warp --capture=artifacts\v22b\no_warp
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_cumulus_below --ablation=macro-only --capture=artifacts\v22b\macro
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_cumulus_below --vertical-noise=1.0 --capture=artifacts\v22b\vert1
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_cumulus_below --vertical-noise=6.0 --capture=artifacts\v22b\vert6
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=32 `
  --scene=clouds_cumulus_below --capture=artifacts\v22b\steps32
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_cumulus_below --capture=artifacts\v22b\steps128
```

V2.2b default quality path: 128 steps + deterministic ray jitter, lowered
vertical noise scale, detail/fine excluded from core density. `--ablation`
supports `no-warp`, `macro-only`, `no-shadow` in addition to the earlier modes.

GPU timestamps повторяются 3 раза, в отчёт идёт медиана (laptop GPU не держит
фиксированные частоты):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\VolumetricCloudsTest\scripts\measure_v22b_performance.ps1 `
  -Exe tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe `
  -EvidenceDir tools\VolumetricCloudsTest\artifacts\v22b_final_evidence -Repeats 3
```

```powershell
$exe = ".\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe"
& $exe --mode=clouds --selftest --debug --size=128 --seed=1337
& $exe --mode=clouds --selftest --debug --gpu-validation --size=128 --seed=1337
& $exe --mode=clouds --preset=partly --frames=120 --no-vsync --warmup=20
& $exe --mode=clouds --preset=rain --frames=120 --no-vsync --warmup=20
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --capture="artifacts\v22_capture\720p_full_128"
& $exe --mode=clouds --window=1920x1080 --preset=storm --steps=64 `
  --frames=120 --warmup=20 --no-vsync --ablation=shadow-2
```

V2 selftest дополнительно проверяет:

- weather map 256×256: finite, [0,1], non-constant, deterministic,
  seed-sensitive, seam continuity;
- world-space weather sampling меняется при переносе камеры;
- cloud type меняет type mapping;
- увеличение coverage увеличивает occupied steps;
- `LerpCloudParams` endpoints и промежуточные значения;
- below/inside/above layer и large-world camera;
- V1 wind/step/occupancy/negative validation регрессии.

Weather map не перегенерируется каждый кадр. `--weather-seed=N` задаёт seed.
`--cloud-scale=full` включает full-resolution cloud output; `--cloud-scale=half`
оставляет V1 half-res режим.

### 128³ GPU-based validation

```powershell
.\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe `
  --mode=clouds --selftest --debug --gpu-validation --size=128 --seed=1337
```

Ожидается `debug errors: 0`, `debug warnings: 0`, `V0 SELFTEST: PASS`,
`V2 CLOUD SELFTEST: PASS`.

### 128³ GPU-based validation на фактическом frame path

```powershell
# Именно чистая Debug-сборка V2.3 final.
.\tools\VolumetricCloudsTest\build-v23-final\Debug\OGSRCloudsTest.exe `
  --mode=clouds --size=128 --gpu-validation --frames=120 --no-vsync
```

Ожидается `debug layer enabled`, `gpu requested=1 enabled=1`,
`cloud frame path: CloudRayMarcher compute + CloudCompositor draw active`,
`gpu timestamps average over 120 samples`,
`debug message totals: 0 errors, 0 warnings`, `GPU idle`, live-object report
с `non-device objects: 0`, exit code 0. SHA256 запущенного EXE фиксируется в
`debug_gbv_frames120_sha256.txt`.

### 64³, WARP и no-debug

```powershell
.\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe `
  --mode=clouds --selftest --debug --size=64 --seed=1337
.\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe `
  --mode=clouds --selftest --debug --warp --size=64 --seed=1337
.\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe `
  --mode=clouds --selftest --size=64 --seed=1337
```

WARP не является показателем производительности. No-debug сценарий должен
писать `validation verdict: NOT REQUESTED` и завершаться с кодом 0.

## 3. V0 regression

```powershell
.\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe `
  --mode=slices --selftest --debug --size=128 --seed=1337
.\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe `
  --mode=slices --capture="<dir>" --debug --size=128 --seed=1337
```

V0 selftest должен сохранять `V0 SELFTEST: PASS`, periodicity/sampler probes и
10 BMP срезов. `--mode=slices` — режим по умолчанию.

## 4. V1 visual captures

```powershell
$cap = "D:\...\tools\VolumetricCloudsTest\artifacts\v1_capture"
.\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe `
  --mode=clouds --window=1280x720 --preset=clear --capture="$cap"

$cap1080 = "D:\...\tools\VolumetricCloudsTest\artifacts\v1_evidence\capture_1080p_overcast"
.\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe `
  --mode=clouds --window=1920x1080 --preset=overcast --steps=32 `
  --capture="$cap1080"
```

720p набор содержит Clear/Overcast, density/transmittance/step-count,
occupancy, горизонт, взгляд вверх и тот же объём с другой камеры. BMP можно
открыть напрямую; evidence-скрипт конвертирует их в PNG и собирает contact
sheet.

Временные captures для проверки ветра:

```powershell
.\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe `
  --mode=clouds --preset=clear --time=0 --capture="<t0>"
.\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe `
  --mode=clouds --preset=clear --time=1 --capture="<t1>"
```

При `windSpeed=0` кадры совпадают; при ненулевом — различаются. Pause/resume
проверяется клавишей `P` в интерактивном режиме.

## 5. V1 interactive controls and D3D12 paths

```powershell
# mouse look, WASD/QE, presets, debug modes, F2 slices/clouds, resize
.\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe --mode=clouds --size=64

# automated resize
.\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe `
  --mode=clouds --frames=90 --resize-test --size=64
```

Проверить в логе `cloud output resized`, `window minimized; rendering paused`,
`window restored; resuming rendering`, `render mode switched`, `GPU idle`.

## 6. Negative validation matrix

Все команды должны вернуть exit code **3**, без PASS и без capture-артефактов:

```powershell
# debug недоступен: frames / capture
.\...\OGSRCloudsTest.exe --debug --test-no-validation --frames=1
.\...\OGSRCloudsTest.exe --debug --test-no-validation --capture=<test-dir>

# debug + GBV недоступны: selftest
.\...\OGSRCloudsTest.exe --selftest --debug --gpu-validation --size=64 --test-no-validation

# Debug Layer включён, но ID3D12InfoQueue отсутствует
.\...\OGSRCloudsTest.exe --selftest --debug --test-no-info-queue --size=64
.\...\OGSRCloudsTest.exe --selftest --debug --gpu-validation --test-no-info-queue --size=64

# совместный selftest + capture при недоступной validation
.\...\OGSRCloudsTest.exe --selftest --capture=<test-dir> --debug --test-no-validation
```

V1-варианты добавляют `--mode=clouds` к тем же командам. В selftest-отчёте
должно быть `validation verdict: INCONCLUSIVE`; capture-каталог не создаётся.

Отдельный negative-тест с **работающей** validation: инъекция debug ERROR
после cloud selftest. Ожидается `final validation verdict: FAIL`, exit code
**1** (не PASS и не INCONCLUSIVE), `debug message totals: 1 errors`:

```powershell
.\...\OGSRCloudsTest.exe --mode=clouds --selftest --size=64 --debug `
  --test-inject-debug-error
```

## 7. GPU timestamps

```powershell
$exe = ".\tools\VolumetricCloudsTest\build-v23-final\Release\OGSRCloudsTest.exe"
& $exe --mode=clouds --window=1280x720 --preset=clear --steps=32 `
  --frames=120 --no-vsync
& $exe --mode=clouds --window=1920x1080 --preset=overcast --steps=64 `
  --frames=120 --no-vsync
```

В логе: `gpu timestamps average over 120 samples: cloud ... ms,
composite ... ms, total ... ms`. Это GPU timestamps, не CPU frame time.
WARP не использовать для сравнения производительности.

## 8. Full V2.3 evidence script

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/VolumetricCloudsTest/scripts/run_v23_evidence.ps1 `
  -Exe tools/VolumetricCloudsTest/build-v23-final/Release/OGSRCloudsTest.exe `
  -DebugExe tools/VolumetricCloudsTest/build-v23-final/Debug/OGSRCloudsTest.exe `
  -EvidenceDir tools/VolumetricCloudsTest/artifacts/v23_final_evidence `
  -CaptureDir tools/VolumetricCloudsTest/artifacts/v23_final_capture
```

По умолчанию скрипт также использует `build-v23-final`. Оба EXE обязательны:
при отсутствии любого скрипт падает с ошибкой, а не запускает устаревший
бинарник. В начале пишется `binary_identity.txt` (пути, SHA256, размер,
timestamp), в конце — `debug_gbv_frames120_sha256.txt` для Debug+GBV
frame-path прогона. `-SkipPerformance` и `-SkipVisual` позволяют сделать
частичный evidence-only прогон.

V1 evidence runner (`run_v1_evidence.ps1`) остаётся для истории V1.

## 9. Evidence layout

```text
artifacts/v23_final_evidence/       selftests, GBV/negative, performance, temporal
artifacts/v23_lighting/             lighting/atmosphere/sun-path/sun-horizon sheets
artifacts/v23_sequences/            12 sequences x 30 frames + sequence_metrics.txt
artifacts/v23_final_capture/        38 scenes x 720p half/full, 1080p half
artifacts/v1_evidence/              historical V1 build/CTest/selftests
```

## 10. V2.3g formula validation

Сборка: `tools/VolumetricCloudsTest/build-v23g-final`.
HLSL загружается во время запуска из `<exe>/shaders`; CMake копирует
`shaders/` рядом с EXE. Поэтому SHA256 runtime-shader, а не mtime EXE,
определяет, какая формула реально исполнялась.

### 10.1. Hardware и WARP selftest

```powershell
$exe = "tools\VolumetricCloudsTest\build-v23g-final\Release\OGSRCloudsTest.exe"
& $exe --mode=clouds --selftest --size=64 --formula=none
& $exe --mode=clouds --selftest --size=64 --formula=F3
& $exe --mode=clouds --warp --selftest --size=64 --formula=none
& $exe --mode=clouds --warp --selftest --size=64 --formula=F3
```

В `selftest.txt` обязательны `Formula requested`, `Formula executed`,
`Shader variant`, `GPU adapter`, `V2 CLOUD SELFTEST: PASS`.
WARP запускать **без** `--gpu-validation`: комбинация
`--warp --gpu-validation` намеренно отклоняется с exit code 2, потому что
GBV — hardware-only режим и на WARP не даёт облачный output.

### 10.2. Negative validation

```powershell
& $exe --mode=clouds --selftest --size=64 --gpu-validation `
  --test-inject-debug-error --formula=F3
```

Ожидается `final debug errors: 1`, `final validation verdict: FAIL`,
exit code 1, `OVERALL SELFTEST: FAIL`.

### 10.3. F0 / F3 captures

```powershell
$cap = "tools\VolumetricCloudsTest\artifacts\v23g_final_f3\storm\final"
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_storm_horizon --camera=-366,-140,2056 `
  --camera-yaw=225 --camera-pitch=45 --fov=72 `
  --formula=F3 --capture=$cap
```

Для F0 используется `--formula=none`; для coverage-matched —
`--formula=F3 --formula-bias=0.02`; для optical-depth-matched —
`--formula=F3 --formula-bias=0.02 --density=<scene value>`
(Sunset 1.31, Storm 1.25, Overcast 0.88, Cumulus 1.40, Strat 1.38).
Debug views: `--debug-mode=density`, `transmittance`, `steps`, `occupancy`.
Срезы: `--debug-mode=density-slice` или `density-vertical-z`,
`--slice-extent=2`; горизонтальные срезы: `density-horizontal`
с `--slice-height=0.2/0.4/0.6/0.8`.

Clear sky control: `--scene=clouds_clear_aerial --coverage=0`.
Дополнительные сцены: `clouds_partly_horizon`, `clouds_cumulus_side`.
Weather seeds: `--weather-seed=1337|1338|1339`.

### 10.4. Motion

```powershell
& $exe --mode=clouds --window=640x360 --cloud-scale=half --steps=128 `
  --preset=storm --camera=-366,-140,2056 --camera-yaw=225 `
  --camera-pitch=45 --fov=72 --motion=pan-slow --wind=on `
  --formula=F3 --frames=60 --sequence=<dir>
```

Для пересечения границы weather tile:
`--camera=3500,3200,-1200 --camera-yaw=90 --camera-pitch=-15 --fov=60
--motion=fly --wind=on --frames=60`.

### 10.5. GPU timings

```powershell
& $exe --mode=clouds --window=1920x1080 --cloud-scale=half `
  --profile=quality --preset=storm --formula=F3 `
  --frames=120 --warmup=20 --no-vsync
```

Повторить три раза, чередуя F0/F3; брать `gpu timestamps average over
120 samples: cloud ... ms`. Матрица: `--steps=64|96|128`,
`--shadow-steps=2|4|6`, профили performance/balanced/quality.
WARP для производительности не используется.

## 11. V2.3g-A01 probe PSO correction

### 11.1. Apply and verify the supplied patch

```powershell
$patch = "C:\path\to\OGSR_V23G_A01_Probe_PSO_Correction.patch"
git apply --check $patch
git apply $patch
python tools/VolumetricCloudsTest/scripts/check_sources.py
```

Patch source basis: `OGSR_V2_3g_Formula_Validation.zip`,
SHA256 `CF2AD5EBA3AC0EBD76F0E1D42FB229056E14703D7B45FDF4DC15DA59F556900C`.
Current source hash before apply: `f147f743ba190b297d6b22b5e84be5a3ffaddf84ef8d491bcd76057e4fe663ff`.

### 11.2. Clean build

```powershell
cmake -S tools/VolumetricCloudsTest -B tools/VolumetricCloudsTest/build-v23g-a01 `
  -G "Visual Studio 17 2022" -A x64
cmake --build tools/VolumetricCloudsTest/build-v23g-a01 --config Release
cmake --build tools/VolumetricCloudsTest/build-v23g-a01 --config Debug
ctest --test-dir tools/VolumetricCloudsTest/build-v23g-a01 -C Release --output-on-failure
ctest --test-dir tools/VolumetricCloudsTest/build-v23g-a01 -C Debug --output-on-failure
```

CMake with fxc compiles all eight probe permutations; expected `.cso`:
`CloudRayMarchProbe.cso` and `CloudRayMarchProbeFormulaF1..F5.cso`,
`CloudRayMarchProbePrototypeA/B.cso`.

### 11.3. PSO matrix

```powershell
$exe = "tools\VolumetricCloudsTest\build-v23g-a01\Release\OGSRCloudsTest.exe"
foreach ($f in @("none","F1","F2","F3","F4","F5")) {
  & $exe --mode=clouds --window=640x360 --cloud-scale=half --steps=64 `
    --scene=clouds_storm_horizon --formula=$f --debug-mode=macro-shape `
    --capture="<dir>\$f"
}
& $exe --mode=clouds --window=640x360 --cloud-scale=half --steps=64 `
  --scene=clouds_storm_horizon --shape-prototype=A --debug-mode=macro-shape `
  --capture="<dir>\ProtoA"
```

Expected log for F3:
`cloud probe PSO selected: CloudRayMarchProbeFormulaF3.hlsl`.
F0: `CloudRayMarchProbe.hlsl (F0)`; prototypes:
`CloudRayMarchProbePrototypeA/B.hlsl`.

### 11.4. Corrected A/B captures

```powershell
# Final, density, vertical/horizontal slices, macro shape, weather.
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --shadow-steps=6 --scene=clouds_storm_horizon `
  --camera=-366,-140,2056 --camera-yaw=225 --camera-pitch=45 --fov=72 `
  --formula=F3 --debug-mode=density-slice --slice-extent=2 --capture="<dir>"
& $exe ... --debug-mode=density-vertical-z --slice-extent=2
& $exe ... --debug-mode=density-horizontal --slice-height=0.50 --slice-extent=4
& $exe ... --debug-mode=macro-shape
& $exe ... --debug-mode=weather
& $exe ... --debug-mode=ray-profile   # writes probe_float32.bin
```

Run the same command with `--formula=none` for F0. Do not use pre-A01 probe
captures for F0/F3 differences: they silently used the F0 probe PSO.

### 11.5. DX12 probe/GBV

```powershell
& $exe --mode=clouds --window=1280x720 --cloud-scale=half --steps=64 `
  --scene=clouds_storm_horizon --formula=F3 --debug-mode=macro-shape `
  --gpu-validation --capture="<dir>"
```

Expected `cloud probe PSO selected: CloudRayMarchProbeFormulaF3.hlsl`,
`debug message totals: 0 errors, 0 warnings`, exit code 0.
WARP+probe run without `--gpu-validation`; WARP+GBV remains unsupported
and is rejected with exit code 2.

## 12. V2.3h reference density checkpoint

Legacy remains the default and must stay pixel-identical to V2.3:

```powershell
$exe = "tools\VolumetricCloudsTest\build-v23h-h1\Release\OGSRCloudsTest.exe"
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_cumulus_below --density-model=legacy --capture="<dir>"
```

Reference stages:

```powershell
# H1: pure 3D shape
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --density-model=reference --reference-stage=base --capture="<dir>"

# H2: shape * world-Y height profile
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --density-model=reference --reference-stage=height --capture="<dir>"

# H3: one coverage remap
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --density-model=reference --reference-stage=coverage --coverage=0.45 `
  --capture="<dir>"

# H4: bounded medium Worley erosion
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --density-model=reference --reference-stage=erosion --coverage=0.45 `
  --capture="<dir>"
```

Reference numerical selftest (H3; coverage=0 empty and coverage scaling):

```powershell
& $exe --mode=clouds --selftest --size=64 --density-model=reference `
  --reference-stage=coverage --coverage=0.5
```

Expected `Density model requested: reference`,
`Density model executed: reference`, `Reference stage: coverage`,
`Render PSO: CloudRayMarchReference.hlsl`,
`Probe PSO: CloudRayMarchProbeReference.hlsl`, `OVERALL SELFTEST: PASS`.

H1/H2 do not include coverage, so coverage-dependent selftest checks are not
applicable to those stages. A01 rule applies: reference probe must never fall
back to legacy/F0; check `cloud probe PSO selected:
CloudRayMarchProbeReference.hlsl`.

H1/H2 visual checkpoint FAILED in V2.3h. V2.3h-A01 showed that the YZ
"curtains" were largely a slice aspect-ratio artifact: XY/YZ map 8 km
horizontally and 1.35 km vertically into 256x256 (5.93:1). The confirmed
issue is the 4.53 km NoiseVolume tile period and the B-channel fine-cell
regularity, not vertical extrusion. Do not use H3/H4 coverage/erosion to
hide it. H5/H6 remain deferred. See
`docs/dx12-v2-3h-a01-noise-field-isolation.md`.

## 13. V2.3h-A01 noise-field isolation

Raw Float32 field (reference model only; the probe never falls back to
legacy/F0):

```powershell
$exe = "tools\VolumetricCloudsTest\build-v23h-h1\Release\OGSRCloudsTest.exe"
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=64 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 `
  --camera-yaw=0 --camera-pitch=45 --fov=57 `
  --density-model=reference --reference-stage=height `
  --noise-mix=rb --coordinate-mode=plain `
  --debug-mode=noise-field --noise-field-axis=yz `
  --noise-field-offset=0.5 --slice-extent=4 --capture="<dir>"
```

`<dir>` must have `noise_field_float32.bin` (256x256x3 float4),
`noise_field_meta.txt`, `noise_field_sample.csv` and
`noise_field_stats.csv`. Record 0 is `noise.r, noise.g, noise.b,
shapeField`; record 1 is `smoothstep, profile, height_fraction,
density_field`; record 2 is `world_x, world_y, world_z, fetch_count`.
The `density_field` is `transfer*profile` for H1/H2 and the single coverage
remap result for H3+; it is before the density multiplier, ray march and
lighting.

Analyse periods and vertical correlation:

```powershell
python tools\VolumetricCloudsTest\scripts\analyze_noise_field.py `
  "tools\VolumetricCloudsTest\artifacts\v23h_a01\noise_fields" `
  --out "tools\VolumetricCloudsTest\artifacts\v23h_a01\noise_field_analysis.csv"

python tools\VolumetricCloudsTest\scripts\match_reference_mixes.py `
  "tools\VolumetricCloudsTest\artifacts\v23h_a01\noise_fields\plain" `
  --out "tools\VolumetricCloudsTest\artifacts\v23h_a01\matched_controls.csv"
```

The second script solves matched H2 transfer edges and H3 coverage for
R-only, B-only and R+B so mean transfer and mean density match the current
0.70R+0.30B baseline.

Candidate (one bounded soft 3D warp, two texture fetches per density
sample):

```powershell
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 `
  --camera-yaw=0 --camera-pitch=45 --fov=57 `
  --density-model=reference --reference-stage=coverage --coverage=0.463 `
  --noise-mix=rb --coordinate-mode=warped --capture="<dir>"
```

Fix the old PowerShell `System.Object[]` CSV export and regenerate numeric
image diffs:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  tools\VolumetricCloudsTest\scripts\compare_capture_diffs.ps1 `
  -CaptureRoot "tools\VolumetricCloudsTest\artifacts\v23h_capture"
```

The script writes invariant decimals and returns numeric `MeanAbs`/`Max`
fields; it must not be replaced by an analysis that exports object arrays.

## 14. V2.3h-A02 non-repeating base field

N0/N1 R-only baseline (same camera as A01 Sunset):

```powershell
$exe = "tools\VolumetricCloudsTest\build-v23h-h1\Release\OGSRCloudsTest.exe"
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 `
  --camera-yaw=0 --camera-pitch=45 --fov=57 `
  --density-model=reference --reference-stage=coverage --coverage=0.45 `
  --noise-mix=r --field-mode=direct --capture="<dir>"
```

Candidate A (multi-frequency R, multiplicative modulation):

```powershell
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 `
  --camera-yaw=0 --camera-pitch=45 --fov=57 `
  --density-model=reference --reference-stage=coverage --coverage=0.4292 `
  --noise-mix=r --field-mode=multi-r --multi-weight=0.40 `
  --capture="<dir>"
```

Candidate B (world-space low-frequency hash modulation):

```powershell
& $exe --mode=clouds --window=1280x720 --cloud-scale=full --steps=128 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 `
  --camera-yaw=0 --camera-pitch=45 --fov=57 `
  --density-model=reference --reference-stage=coverage --coverage=0.4320 `
  --noise-mix=r --field-mode=hash-mod --modulation-amplitude=0.18 `
  --capture="<dir>"
```

The direct 4.53/9.06 km final-render test shifts the camera X by 4530
and 9060 m with the same view. R-only repeats (mean abs 0.995/1.669);
A w0.40 gives 10.55/11.69; B a0.18 gives 11.56/21.60.

Raw field analysis and matched controls:

```powershell
python tools\VolumetricCloudsTest\scripts\analyze_noise_field.py `
  "tools\VolumetricCloudsTest\artifacts\v23h_a02\noise_fields" `
  --out "tools\VolumetricCloudsTest\artifacts\v23h_a02\noise_field_analysis.csv"

python tools\VolumetricCloudsTest\scripts\match_candidate_controls.py `
  "tools\VolumetricCloudsTest\artifacts\v23h_a02\noise_fields\xz30_multi-r_mod_w40_seed1337\noise_field_float32.bin" `
  --target-transfer 0.07899 --target-density 0.0818 `
  --out "tools\VolumetricCloudsTest\artifacts\v23h_a02\matched_w40.csv"
```

Status: READY FOR V2.3h-A02 INDEPENDENT REVIEW. Candidate B a0.18 is the
recommended primary; Candidate A w0.40 is the slice-decorrelation
alternative. Blend Candidate A was rejected for loss of structure.
N4 erosion is deferred until candidate acceptance. Do not use
coverage/threshold tuning as a substitute for a non-tiling base.

## 15. V2.3h-A02b silhouette / Storm volume

Cloud-only optical-depth map (OD/(OD+1), before atmosphere):

```powershell
$exe = "tools\VolumetricCloudsTest\build-v23h-h1\Release\OGSRCloudsTest.exe"
& $exe --mode=clouds --window=640x360 --cloud-scale=half --steps=96 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 `
  --camera-yaw=0 --camera-pitch=45 --fov=57 `
  --density-model=reference --reference-stage=coverage --coverage=0.45 `
  --noise-mix=r --field-mode=direct --debug-mode=view-optical-depth `
  --capture="<dir>"
```

Level flight (constant height/pitch, 25 m/frame, 200 frames):

```powershell
& $exe --mode=clouds --window=640x360 --cloud-scale=half --steps=96 `
  --scene=clouds_sunset_horizon --camera=476,400,3925 `
  --camera-yaw=0 --camera-pitch=10 --fov=57 `
  --density-model=reference --reference-stage=coverage --coverage=0.45 `
  --noise-mix=r --field-mode=direct --motion=fly-level --frames=200 `
  --wind=off --no-vsync --sequence="<dir>"
```

Storm S0 / S1 / S2:

```powershell
# S0 flat deck
& $exe ... --preset=storm --coverage=0.80 --reference-stage=coverage
# S1 denser control
& $exe ... --preset=storm --coverage=0.80 --reference-density-scale=1.5
# S2 volumetric: lower coverage + non-periodic 5 km height/gap envelope
& $exe ... --preset=storm --coverage=0.55 --reference-density-scale=1.5 `
  --storm-volume=1 --storm-height-variation=1600 `
  --storm-bottom-variation=350
```

Silhouette metrics:

```powershell
python tools\VolumetricCloudsTest\scripts\analyze_a02b_silhouette.py `
  "tools\VolumetricCloudsTest\artifacts\v23h_a02b\optical_depth" `
  --out "tools\VolumetricCloudsTest\artifacts\v23h_a02b\silhouette_metrics.csv"
```

S0/S1: vertical thickness std 5-6 m, one component. S2: 606 m std,
p95 1983 m, 34 components, bright fraction 36.6%. S2 is a geometry
change, not an extinction-only change. U remains BLOCKED; S PASS WITH
NOTES. No V2 PASS; no commit/push/merge.

## 16. V2.3h-A02c linear OD and Float32 density volume

Linear cloud-only optical-depth probe. The shader writes both mapped
OD/(OD+1) and raw -log(T) into a Float32 structured buffer; the capture
path writes `optical_depth_float32.bin` + `.json`. No composite,
exposure, tone mapping, sRGB or 8-bit quantisation is involved.

```powershell
$exe = "tools\VolumetricCloudsTest\build-v23h-h1\Release\OGSRCloudsTest.exe"
& $exe --mode=clouds --window=1280x720 --cloud-scale=half --steps=96 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 `
  --camera-yaw=0 --camera-pitch=45 --fov=57 `
  --density-model=reference --reference-stage=coverage --coverage=0.45 `
  --noise-mix=r --field-mode=direct --debug-mode=view-optical-depth `
  --capture="<dir>"
```

Analytic encode/decode test (eight OD bands, Float32 path):

```powershell
& $exe ... --debug-mode=view-optical-depth --od-test-pattern=1 --capture="<dir>"
python tools\VolumetricCloudsTest\scripts\validate_a02c_od_pipeline.py `
  --pattern "<dir>" --out "<dir>\od_pipeline_validation.json"
```

Linear uniqueness metrics:

```powershell
python tools\VolumetricCloudsTest\scripts\analyze_a02c_optical_depth.py `
  "tools\VolumetricCloudsTest\artifacts\v23h_a02c\optical_depth" `
  --out "tools\VolumetricCloudsTest\artifacts\v23h_a02c\silhouette_metrics.csv" `
  --srgb-out "tools\VolumetricCloudsTest\artifacts\v23h_a02c\srgb_comparison.csv"
```

Float32 density volume, fine columns and analytic boundaries:

```powershell
& $exe ... --preset=storm --coverage=0.55 --reference-density-scale=1.5 `
  --storm-volume=1 --storm-height-variation=1600 `
  --storm-bottom-variation=350 --density-volume=1 `
  --volume-res=128,96,128 --capture="<dir>"
```

Corrected S2 column envelope (old S2 remains the default baseline):

```powershell
& $exe ... --storm-volume=1 --storm-column=1 ...
```

Independent 3D analysis and slices:

```powershell
python tools\VolumetricCloudsTest\scripts\analyze_a02c_volume.py `
  "tools\VolumetricCloudsTest\artifacts\v23h_a02c\volume" `
  --out "tools\VolumetricCloudsTest\artifacts\v23h_a02c\volume_metrics.json" `
  --columns-csv "tools\VolumetricCloudsTest\artifacts\v23h_a02c\volume_columns.csv"
python tools\VolumetricCloudsTest\scripts\render_a02c_volume_slices.py `
  "tools\VolumetricCloudsTest\artifacts\v23h_a02c\volume" `
  --out "tools\VolumetricCloudsTest\artifacts\v23h_a02c\slices"
```

The .bin files are little-endian Float32. `optical_depth_float32.json`
documents the record layout; `density_volume_float32.json` documents the
world origin, anisotropic cell sizes, index order and channel meaning.

## 17. V2.3h-A03 non-tiling macro field

Exact NoiseVolume period for a preset:

```text
P = 1 / (noiseScale * 0.55)
Sunset 4545.4545 m, Overcast 3030.3030 m, Storm 5194.8052 m
```

M1 macro field (Sunset, 4.545 km period alignment):

```powershell
& $exe --mode=clouds --window=640x360 --cloud-scale=half --steps=96 `
  --scene=clouds_sunset_horizon --camera=5021.45,-248,3925 `
  --camera-yaw=0 --camera-pitch=45 --fov=57 `
  --density-model=reference --reference-stage=coverage --coverage=0.45 `
  --noise-mix=r --field-mode=macro --macro-seed=1337 `
  --debug-mode=view-optical-depth --capture="<dir>"
```

Macro controls: `--macro-scale` (m), `--macro-secondary-scale`,
`--macro-secondary-weight`, `--macro-threshold`, `--macro-softness`,
`--macro-detail`, `--macro-edge-jitter`, `--macro-height`, `--macro-seed`.
Macro-only: `--macro-detail=0 --macro-edge-jitter=0 --macro-height=0`.

60 km horizontal Float32 slice:

```powershell
& $exe ... --density-volume=1 --volume-res=512,1,512 `
  --volume-cell=117.1875 --volume-origin=-30000,900,-30000 `
  --capture="<dir>"
python tools\VolumetricCloudsTest\scripts\render_a03_macro_slices.py `
  "<dir>" --out "<png>"
```

Level flight X/Z with the exact period crossing:

```powershell
& $exe ... --camera=2025,700,-1200 --camera-yaw=90 --camera-pitch=10 `
  --fov=72 --motion=fly-level --frames=200 --wind=off --no-vsync `
  --sequence="<dir>"
```

Analysis:

```powershell
python tools\VolumetricCloudsTest\scripts\analyze_a03_macro.py `
  "tools\VolumetricCloudsTest\artifacts\v23h_a03\optical_depth_float32" `
  --out-csv macro_components.csv --out-json macro_components.json
python tools\VolumetricCloudsTest\scripts\analyze_a03_flight.py `
  "tools\VolumetricCloudsTest\artifacts\v23h_a03\flight" `
  --out flight_metrics.json
```

M1 is a placement field: it gates the large masses before the periodic
R detail. It is not a density-only modulation. Storm S2new is unchanged;
`S2new + macro` is an isolated experiment and is reported BLOCKED for the
integration (occupied columns 56% -> 14.6%).

## 18. V2.3h-A03a coverage and Storm audit

A03a keeps the A03 bytecode untouched. The new controls compile into
separate `*A03a.hlsl` permutations and default off:

```text
--macro-coverage=0|1         coverage remap on macro mass before detail
--macro-detail-compensate=0|1 compensate the mean detail attenuation
--macro-seed-hash=0|1        full-precision seed delta (zero at seed 1337)
--macro-coverage-calibrate=0|1 quantile occupancy calibration (C3-2)
--storm-gap=keep|macro       replace the redundant 5 km Storm gap
--macro-probe=0|1            Float32 macro-stage volume channels
```

Sunset C3-1 station (same camera as A03):

```powershell
& $exe --mode=clouds --window=640x360 --cloud-scale=half --steps=96 `
  --scene=clouds_sunset_horizon --camera=476,-248,3925 `
  --camera-yaw=0 --camera-pitch=45 --fov=57 `
  --density-model=reference --reference-stage=coverage --coverage=0.45 `
  --noise-mix=r --field-mode=macro --macro-seed=1337 `
  --macro-coverage=1 --macro-detail-compensate=1 --macro-seed-hash=1 `
  --density-volume=1 --volume-res=128,96,128 --macro-probe=1 `
  --capture="<dir>"
```

The macro probe writes `macro_probe_float32.{bin,json}` with four
channels: `macro_raw`, `macro_mass`, `detail_transfer`,
`macro_mass_covered`. Render the 60 km slice with
`render_a03a_macro_probe.py`.

Coverage attribution:

```powershell
python tools\VolumetricCloudsTest\scripts\analyze_a03a_coverage.py `
  "tools\VolumetricCloudsTest\artifacts\v23h_a03a\c2" --out coverage.json
python tools\VolumetricCloudsTest\scripts\compare_a03a_seeds.py `
  "tools\VolumetricCloudsTest\artifacts\v23h_a03a\c1_od" --out seeds.json
python tools\VolumetricCloudsTest\scripts\summarize_a03a_timings.py `
  timings_raw.csv --out timing_summary.json
```

Wind contract probe: run the same volume at `--time=0` with the original
origin and at `--time=1` with the origin shifted by the exact wind offset
(24 m along the normalised direction). `compare_a03a_wind_fields.py`
compares all density and macro channels; the coordinate contract is exact
only when every channel matches.

