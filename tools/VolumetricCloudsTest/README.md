# OGSR Volumetric Clouds — V0 standalone D3D12 test

Изолированный Windows-инструмент для генерации и визуализации процедурных
3D-шумов. Не является интеграцией в игровой рендерер: не использует X-Ray,
`xrCore`, `xrRenderDX12`, `xrRender_R4`, игровую физику или материалы.
Собирается только с системными библиотеками Windows SDK.

Текущий этап — **V0**. Ray marching, scattering и облачная плотность появятся
в V1. V0 проверяет фундамент: D3D12-каркас, периодические 3D-шумы на compute
shader, срезы объёма, воспроизводимость и отсутствие validation errors.

## Сборка

Требования: Windows 10/11 x64, Visual Studio 2022 (Desktop C++), Windows SDK
10.0.19041 или новее, CMake 3.21+.

```powershell
cmake -S tools/VolumetricCloudsTest -B tools/VolumetricCloudsTest/build -G "Visual Studio 17 2022" -A x64
cmake --build tools/VolumetricCloudsTest/build --config Release
ctest --test-dir tools/VolumetricCloudsTest/build -C Release --output-on-failure
.\tools\VolumetricCloudsTest\build\Release\OGSRCloudsTest.exe
```

EXE использует консольную подсистему, чтобы логи и коды ошибок были видны при
запуске из терминала. HLSL-файлы копируются в `shaders/` рядом с EXE и
загружаются во время выполнения. При сборке, если найден `fxc.exe`, все entry
points дополнительно проверяются на этапе build.

## Режимы запуска

| Команда | Назначение |
|---|---|
| `OGSRCloudsTest.exe` | интерактивный просмотр |
| `--selftest` | readback, периодичность, детерминизм, seed; пишет `selftest.txt` |
| `--capture[=DIR]` | 10 BMP-снимков (3 канала × 3 среза + диагностика швов) |
| `--frames=N` | ограничить число кадров (автоматизация) |
| `--resize-test` | во время `--frames` дважды изменить размер окна |
| `--size=64` / `--size=128` | начальное разрешение объёма |
| `--seed=N` | seed (по умолчанию 1337) |
| `--format=16f` / `--format=unorm` | принудительный формат объёма; по умолчанию 16F, затем UNORM |
| `--adapter=N` | номер DXGI-адаптера из списка при старте |
| `--warp` | принудительно Microsoft WARP |
| `--debug` | включить D3D12 debug layer в Release |
| `--gpu-validation` | GPU-based validation (включает debug layer) |
| `--test-no-validation` | служебный negative-тест: имитировать отсутствие validation |
| `--test-no-info-queue` | служебный negative-тест: скрыть `ID3D12InfoQueue` при включённом debug layer |
| `--no-vsync` | отключить vsync |

`--selftest` возвращает 0 при PASS, 1 при functional FAIL, 3 при
INCONCLUSIVE (запрошенная validation недоступна). `--test-no-validation`
предназначен только для аудита.

`--selftest` и `--capture` можно совместить: сначала выполнятся проверки, затем
снимки. WARP предназначен только для проверки корректности, не для оценки
производительности.

## Управление

| Клавиша | Действие |
|---|---|
| `1` / `2` / `3` | Perlin fBm / Worley F1 / Perlin-Worley |
| стрелки, колесо | срез по Z |
| `Space` | автоматическое движение по Z |
| `R` | сброс среза и остановка |
| `D` | диагностика швов: плитка 3×3 |
| `N` | принудительная перегенерация |
| `4` | переключение 64³ ↔ 128³ |
| `G` / `Shift+G` | seed ±1 |
| `F` / `Shift+F` | базовая частота ±1 |
| `O` / `Shift+O` | число октав ±1 |
| `W` / `Shift+W` | частота Worley ±1 |
| `B` / `Shift+B` | blend ±0.05 |
| `E` / `Shift+E` | erosion ±0.05 |
| `K` / `Shift+K` | gain ±0.05 |
| `H` / `F1` | справка |
| `Esc` | выход |

Параметры меняются без пересоздания окна. Перегенерация выполняется один раз
после изменения, а не каждый кадр.

## Архитектура

```
src/
  main.cpp          CLI, DPI, сообщения об ошибках
  Application.*     окно, ввод, режимы selftest/capture, оркестрация
  Window.*          Win32 HWND, resize/minimize, message pump
  Renderer.*        device/queue/swapchain/RTV/descriptors/fences/present/resize
  NoiseVolume.*     Texture3D, compute PSO, периодичность, readback, periodicity probe
  SliceViewer.*     graphics PSO и fullscreen-срез (не знает о генерации)
  Validation.*      статистика, seam-метрики, сравнение объёмов
  ImageIO.*         BMP-вывод
  Common.*          логирование, HRESULT, transitions, компиляция HLSL
shaders/
  NoiseGenerate.hlsl  GenerateNoise + ProbePeriodicity + ProbeSamplerWrap
  SliceViewer.hlsl    FullscreenVS + SlicePS
```

`NoiseVolume` не зависит от окна и UI; `SliceViewer` получает SRV и рисует
полноэкранный треугольник. В V1 `Texture3D` можно передать в ray-marching pass
без переписывания генератора.

## Математика шума

Объём — кубическая `Texture3D`, по умолчанию `R16G16B16A16_FLOAT`, fallback
`R8G8B8A8_UNORM`.

- **Perlin fBm.** Классический gradient noise на целочисленной решётке,
  квинтичная интерполяция, 12 градиентов. Частоты каждой октавы —
  целые (`baseFrequency * lacunarity^i`), поэтому поле периодично по X/Y/Z.
  Амплитуды нормируются суммой, затем значение переводится в `[0,1]`.
- **Worley F1.** Поиск ближайшей feature point в 27 соседних ячейках.
  Координаты ячеек заворачиваются по модулю `worleyFrequency`; расстояние
  считается относительно текущей ячейки, поэтому функция корректно
  продолжается через границу. Результат инвертирован: 1 у feature point,
  0 у границы ячейки.
- **Perlin-Worley.** `base = lerp(perlin, worley, blend)`,
  `combined = saturate(base * (1 - erosion * (1 - worley)))`.
  `blend` и `erosion` настраиваются с клавиатуры.

Выходные каналы: `.r` — Perlin, `.g` — Worley, `.b` — combined, `.a` — 1.
Seed входит в целочисленный hash, поэтому одинаковые параметры и seed дают
побитово одинаковый объём.

## Синхронизация и владение

- 3 back buffer, отдельные command allocator и command list на каждый buffer.
- Fence на каждый back buffer; allocator переиспользуется только после
  завершения кадра, который его использовал.
- `ID3D12Fence::GetCompletedValue() == UINT64_MAX` — это device removal, а не
  завершение. Sentinel проверяется до сравнения с ожидаемым значением; после
  `WaitForSingleObject` fence проверяется повторно. Sentinel/timeout не
  приводят к переиспользованию allocator.
- Генерация и readback выполняются через `ExecuteImmediate`: перед записью
  queue дожидается полного GPU idle. Это осознанно консервативно для V0:
  перегенерация редкая, зато состояние объёма всегда однозначно.
- Объём создаётся в `UNORDERED_ACCESS`, после dispatch ставится UAV barrier и
  переход в `ALL_SHADER_RESOURCE`; readback идёт через `COPY_SOURCE`.
- Resize: `WaitForGpu`, освобождение back buffer references, `ResizeBuffers`,
  пересоздание RTV. Minimize: рендер приостанавливается, restore возобновляет.
- Shutdown: сначала `WaitForGpu`, затем освобождение PSO/root signature/
  ресурсов. Это исправляет реальную ошибку, найденную при resize-тесте:
  освобождение PSO до GPU idle приводило к падению на shutdown. Нормальное
  завершение (`GPU idle`) отделено от best-effort cleanup после device removal
  или неуспешного ожидания.
- При `--debug` в конце вызывается `ID3D12DebugDevice::ReportLiveDeviceObjects`;
  сообщения попадают в info queue. В отчёте виден только сам device,
  non-device live objects = 0.

## Валидация

`--selftest` проверяет:

- все каналы в `[0,1]` и не константны;
- seam continuity по X/Y/Z: средняя разница через шов / средняя разница внутри
  объёма < 2.5 (на практике ≈ 0.85–1.12);
- **direct GPU periodicity probe**: 64 низкодисперсных точки, сравнение
  `f(p)` и `f(p+axis)` теми же shader-функциями; максимум должен быть < 1e-4
  (на практике 0). Негативный контроль `f(p)` vs `f(p + 0.37)` должен быть
  заметно больше нуля (на практике 0.456);
- **WRAP sampler probe**: 64 пробы у швов X/Y/Z, `SampleLevel` с linear-wrap
  сравнивается с CPU-трилинейной интерполяцией по readback-объёму; ошибка
  фильтра < 1e-2 (на практике ≈ 3e-4), `p` vs `p+axis` = 0. Для контроля
  считается CLAMP-эталон, он заметно отличается (≈ 0.05–0.10);
- детерминизм: повторная генерация с теми же параметрами даёт max abs diff 0;
- чувствительность к seed: combined-поле меняется (mean abs diff > 1e-3);
- отсутствие D3D12 debug errors при включённом debug layer.

Явный запрос `--debug`/`--gpu-validation` не может дать PASS, если
инфраструктура не активировалась. Единый preflight перед `--selftest`,
`--capture`, `--frames` и interactive проверяет отдельно Debug Layer,
`ID3D12InfoQueue` и GPU-based validation. При недоступности: `INCONCLUSIVE`,
exit code 3, capture/frames не создают артефактов. Для негативных тестов есть
служебные `--test-no-validation` и `--test-no-info-queue`; реальный GPU при
этом не теряется.

`scripts/run_evidence.ps1` повторяет весь набор: Release 128/64, GPU-based
validation 128/64, WARP 64, UNORM 64, Debug 64, unit-тесты политики fence и
validation, полную negative-матрицу (frames/capture/combined/info queue),
capture и resize.

`--capture` дополнительно проверяет present/copy/readback path и сохраняет
BMP. Диагностический кадр `combined_diag_z*.bmp` показывает combined-поле
плиткой 3×3: если периодичность нарушена, на внутренних границах будут видны
разрывы. Красная рамка рисуется только по внешнему краю и не перекрывает
внутренние швы.

## Известные ограничения V0

- Нет ray marching, scattering, cloud density profile и temporal reprojection.
- Нет загрузки/сохранения объёма на диск.
- Нет автоматического восстановления после device removal; DRED собирается,
  но полный device-recreation path не реализован.
- DXGI live-object summary выводится через `OutputDebugString`; device-level
  live-object report захватывается через info queue, а DXGI summary нужно
  смотреть в debugger. Это не влияет на корректность shutdown.
- `--frames`/`--resize-test` — вспомогательные режимы, не benchmark.
- WARP не отражает производительность GPU.
- Скриншоты и логи лежат в `artifacts/` (каталог исключён из git).

## Evidence

- `artifacts/evidence/` — build logs, selftest-отчёты hardware/WARP/GBV/UNORM,
  unit/negative-тесты, resize/minimize, git state и GPU/driver info.
- `artifacts/capture/` — PNG-снимки, `capture_info.txt` и `contact_sheet.png`.
- `selftest.txt` рядом с EXE — последний отчёт self-test.

## V1 interface

`NoiseVolume` предоставляет `SrvGpuHandle()`, `Resolution()`, `Params()` и
`Format()`. Этого достаточно, чтобы подключить объём к отдельному
ray-marching compute/pixel pass, не меняя генерацию и не добавляя зависимости
от Win32.
