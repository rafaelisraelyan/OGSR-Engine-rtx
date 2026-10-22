# V0 reproduction

Все команды выполняются из корня репозитория OGSR. Нужны Windows 10/11 x64,
Visual Studio 2022 Desktop C++, Windows SDK 10.0.19041+ и CMake 3.21+.

## 1. Build

```powershell
cmake -S tools/VolumetricCloudsTest -B tools/VolumetricCloudsTest/build -G "Visual Studio 17 2022" -A x64
cmake --build tools/VolumetricCloudsTest/build --config Release
cmake --build tools/VolumetricCloudsTest/build --config Debug
ctest --test-dir tools/VolumetricCloudsTest/build -C Release --output-on-failure
```

EXE: `tools/VolumetricCloudsTest/build/Release/OGSRCloudsTest.exe`.
HLSL копируется в `shaders/` рядом с EXE. При наличии `fxc.exe` сборка
дополнительно проверяет `GenerateNoise`, `ProbePeriodicity`,
`ProbeSamplerWrap`, `FullscreenVS`, `SlicePS`.

## 2. Main selftest, 128³, hardware + debug layer

```powershell
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe --selftest --debug --size=128 --seed=1337
```

Ожидаемые ключевые строки:

```text
periodicity probe max |f(p)-f(p+axis)|: X=0 Y=0 Z=0
periodicity probe negative control: 0.45625
sampler WRAP filter error: 0.000340104
sampler WRAP p vs p+axis difference: 0
sampler CLAMP reference difference: 0.0500488
validation verdict: PASS
V0 SELFTEST: PASS
```

Exit code 0. Отчёт: `selftest.txt` рядом с EXE. При успешном shutdown в логе
есть `shutdown: GPU idle`; при потере устройства — `best-effort cleanup` и
ненулевой код.

### 128³ GPU-based validation (полный лог)

```powershell
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe --selftest --debug --gpu-validation --size=128 --seed=1337
```

Ожидается `debug errors: 0`, `debug warnings: 0`, `V0 SELFTEST: PASS`.
Стартовое сообщение GBV с severity `MESSAGE` — информационное.

### 64³, WARP и UNORM

```powershell
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe --selftest --debug --size=64 --seed=1337
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe --selftest --debug --warp --size=64 --seed=1337
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe --selftest --debug --size=64 --seed=1337 --format=unorm
```

WARP не является показателем производительности.

## 2a. Audit hardening negative tests

Unit-тест политики fence sentinel и validation:

```powershell
ctest --test-dir tools/VolumetricCloudsTest/build -C Release --output-on-failure
# или напрямую:
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsAuditTests.exe
```

Ожидается:

```text
FENCE SENTINEL TESTS: PASS
VALIDATION POLICY TESTS: PASS
AUDIT POLICY TESTS: PASS
```

`UINT64_MAX` моделируется как `DeviceRemoved` и не может быть принят за
завершённый fence.

Негативная матрица (все команды должны вернуть exit code **3**, без PASS и без
capture-артефактов):

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

Для selftest-вариантов в `selftest.txt` должно быть
`validation verdict: INCONCLUSIVE`, `V0 SELFTEST: INCONCLUSIVE`; каталог
capture не создаётся. Успешные сценарии без Debug Layer
(`--selftest`, `--capture`, `--frames`) работают с verdict `NOT REQUESTED`.

## 3. Screenshots

```powershell
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe `
  --capture="D:\Coding Project\OGSR ENGINE fork from rtx\tools\VolumetricCloudsTest\artifacts\capture" `
  --debug --size=128 --seed=1337
```

Параметры capture:

```text
resolution      128^3
seed            1337
octaves         4
baseFrequency   4
lacunarity      2
worleyFrequency 8
gain            0.5
blend           0.55
erosion         0.35
format          R16G16B16A16_FLOAT (auto; fallback R8G8B8A8_UNORM)
channel         Perlin / Worley / Combined
Z slices        16, 64, 112  (N/8, N/2, 7N/8)
diagnostic      Combined, Z=64, 3x3 tiling
```

Файлы capture:

| Файл | Канал | Z | Режим |
|---|---|---|---|
| `perlin_z016.png` | Perlin fBm | 16 | обычный |
| `perlin_z064.png` | Perlin fBm | 64 | обычный |
| `perlin_z112.png` | Perlin fBm | 112 | обычный |
| `worley_z016.png` | Worley F1 | 16 | обычный |
| `worley_z064.png` | Worley F1 | 64 | обычный |
| `worley_z112.png` | Worley F1 | 112 | обычный |
| `combined_z016.png` | Perlin-Worley | 16 | обычный |
| `combined_z064.png` | Perlin-Worley | 64 | обычный |
| `combined_z112.png` | Perlin-Worley | 112 | обычный |
| `combined_diag_z064.png` | Perlin-Worley | 64 | 3×3 seam |

Приложение пишет BMP; в пакете лежат PNG-копии с теми же именами. Команда
выше создаёт BMP; для конвертации можно использовать Paint или:

```powershell
Add-Type -AssemblyName System.Drawing
Get-ChildItem <capture-dir> -Filter *.bmp | ForEach-Object {
  $img = [System.Drawing.Image]::FromFile($_.FullName)
  $img.Save((Join-Path $_.DirectoryName ($_.BaseName + ".png")), [System.Drawing.Imaging.ImageFormat]::Png)
  $img.Dispose()
}
```

## 4. Resize / minimize

```powershell
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe --frames=90 --resize-test --debug --size=128
```

Режим дважды меняет размер окна. В логе должны быть `resize:` и
`debug errors=0 warnings=0`.

Minimize/restore и смена параметров проверялись через Win32 `ShowWindow` и
`PostMessage`; лог: `evidence/run_keys_minimize_hw.log`.

## 5. Full evidence script

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  tools\VolumetricCloudsTest\scripts\run_evidence.ps1 `
  -Exe tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe `
  -DebugExe tools\VolumetricCloudsTest\build\Debug\OGSRCloudsTest.exe `
  -EvidenceDir tools\VolumetricCloudsTest\artifacts\evidence `
  -CaptureDir tools\VolumetricCloudsTest\artifacts\capture
```

Скрипт последовательно запускает 128³/64³ hardware, 128³/64³ GPU-based
validation, WARP 64³, UNORM 64³, Debug 64³, capture и resize, копируя
`selftest.txt` и логи в `artifacts/evidence`.

## 6. Interactive controls

`1/2/3` — Perlin/Worley/Combined, стрелки/колесо — Z, `Space` — анимация,
`D` — диагностика швов, `N` — перегенерация, `4` — 64³/128³, `G` — seed,
`F/O/W/B/E/K` — параметры, `H` — справка, `Esc` — выход.
