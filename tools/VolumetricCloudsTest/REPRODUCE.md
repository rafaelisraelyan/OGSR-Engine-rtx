# V0/V1 reproduction

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
`CloudCompositePS`.

## 2. V1 cloud selftest, 128³, hardware + debug layer

```powershell
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe `
  --mode=clouds --selftest --debug --size=128 --seed=1337
```

Ожидаемые ключевые строки:

```text
V0 SELFTEST: PASS
V1 CLOUD SELFTEST: PASS
OVERALL SELFTEST: PASS
```

Exit code 0. В отчёте `selftest.txt` дополнительно проверяются:

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

### 128³ GPU-based validation

```powershell
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe `
  --mode=clouds --selftest --debug --gpu-validation --size=128 --seed=1337
```

Ожидается `debug errors: 0`, `debug warnings: 0`, `V0 SELFTEST: PASS`,
`V1 CLOUD SELFTEST: PASS`.

### 128³ GPU-based validation на фактическом frame path

```powershell
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe `
  --mode=clouds --size=128 --gpu-validation --frames=120 --no-vsync
```

Ожидается `gpu requested=1 enabled=1`, строка
`cloud frame path: CloudRayMarcher compute + CloudCompositor draw active`,
`gpu timestamps average`, `debug message totals: 0 errors, 0 warnings`,
exit code 0.

### 64³, WARP и no-debug

```powershell
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe `
  --mode=clouds --selftest --debug --size=64 --seed=1337
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe `
  --mode=clouds --selftest --debug --warp --size=64 --seed=1337
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe `
  --mode=clouds --selftest --size=64 --seed=1337
```

WARP не является показателем производительности. No-debug сценарий должен
писать `validation verdict: NOT REQUESTED` и завершаться с кодом 0.

## 3. V0 regression

```powershell
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe `
  --mode=slices --selftest --debug --size=128 --seed=1337
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe `
  --mode=slices --capture="<dir>" --debug --size=128 --seed=1337
```

V0 selftest должен сохранять `V0 SELFTEST: PASS`, periodicity/sampler probes и
10 BMP срезов. `--mode=slices` — режим по умолчанию.

## 4. V1 visual captures

```powershell
$cap = "D:\...\tools\VolumetricCloudsTest\artifacts\v1_capture"
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe `
  --mode=clouds --window=1280x720 --preset=clear --capture="$cap"

$cap1080 = "D:\...\tools\VolumetricCloudsTest\artifacts\v1_evidence\capture_1080p_overcast"
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe `
  --mode=clouds --window=1920x1080 --preset=overcast --steps=32 `
  --capture="$cap1080"
```

720p набор содержит Clear/Overcast, density/transmittance/step-count,
occupancy, горизонт, взгляд вверх и тот же объём с другой камеры. BMP можно
открыть напрямую; evidence-скрипт конвертирует их в PNG и собирает contact
sheet.

Временные captures для проверки ветра:

```powershell
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe `
  --mode=clouds --preset=clear --time=0 --capture="<t0>"
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe `
  --mode=clouds --preset=clear --time=1 --capture="<t1>"
```

При `windSpeed=0` кадры совпадают; при ненулевом — различаются. Pause/resume
проверяется клавишей `P` в интерактивном режиме.

## 5. V1 interactive controls and D3D12 paths

```powershell
# mouse look, WASD/QE, presets, debug modes, F2 slices/clouds, resize
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe --mode=clouds --size=64

# automated resize
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe `
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

## 7. GPU timestamps

```powershell
$exe = ".\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe"
& $exe --mode=clouds --window=1280x720 --preset=clear --steps=32 `
  --frames=120 --no-vsync
& $exe --mode=clouds --window=1920x1080 --preset=overcast --steps=64 `
  --frames=120 --no-vsync
```

В логе: `gpu timestamps average over 120 samples: cloud ... ms,
composite ... ms, total ... ms`. Это GPU timestamps, не CPU frame time.
WARP не использовать для сравнения производительности.

## 8. Full evidence script

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/VolumetricCloudsTest/scripts/run_v1_evidence.ps1 `
  -Exe tools/VolumetricCloudsTest/build-clean/Release/OGSRCloudsTest.exe `
  -DebugExe tools/VolumetricCloudsTest/build-clean/Debug/OGSRCloudsTest.exe `
  -EvidenceDir tools/VolumetricCloudsTest/artifacts/v1_evidence `
  -CaptureDir tools/VolumetricCloudsTest/artifacts/v1_capture
```

Скрипт прогоняет V0 regression, V1 hardware/GBV/WARP/no-debug selftests,
resize, captures, negative matrix и 8-точечную GPU timestamp-матрицу.

## 9. Evidence layout

```text
artifacts/v1_evidence/                 build, CTest, selftests, negative, perf
artifacts/v1_capture/                  720p PNG + contact_sheet.png
artifacts/v1_evidence/capture_1080p_overcast/
artifacts/evidence_v0_regression/      повторный V0 regression
```
