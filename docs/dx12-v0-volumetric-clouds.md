# V0 — Standalone DX12 Volumetric Clouds: отчёт для независимого ревью

Дата: 2026-10-22
Ветка: `feature/dx12-volumetric-clouds`
HEAD: `ccf0d22d1` (`prypiat`)
Статус: **READY FOR FINAL DELTA REVIEW**. Финальный PASS не присваивался.

Изменения находятся только в `tools/VolumetricCloudsTest/`. Игровой DX11/DX12,
`xrRenderDX12`, `xrRender_R4`, физика, материалы и `docs/08-roadmap.md` не
изменялись. Коммит и push не выполнялись.

## A. Git State

- Ветка создана от `prypiat`; HEAD не менялся.
- В рабочем дереве уже присутствовали посторонние изменения: `AGENTS.md`
  (4 строки), несколько `docs/*.md`, а также появившиеся во время работы
  untracked-файлы `ogsr_engine/Layers/xrRenderDX12/dx12xraylevelgeometry.*`,
  `tests/*`. Они не трогались.
- Наш результат — untracked-каталог `tools/VolumetricCloudsTest/`.
- Полный снимок состояния: `tools/VolumetricCloudsTest/artifacts/evidence/git_state.txt`.

Файлы V0:

```
tools/VolumetricCloudsTest/
  CMakeLists.txt
  README.md
  .gitignore
  scripts/check_sources.ps1
  scripts/check_sources.py
  shaders/NoiseGenerate.hlsl
  shaders/SliceViewer.hlsl
  src/main.cpp
  src/Application.{h,cpp}
  src/Window.{h,cpp}
  src/Renderer.{h,cpp}
  src/NoiseVolume.{h,cpp}
  src/SliceViewer.{h,cpp}
  src/Validation.{h,cpp}
  src/ImageIO.{h,cpp}
  src/Common.{h,cpp}
  src/NoiseTypes.h
```

## B. Architecture

### Поток кадра

1. `Renderer::BeginFrame` ждёт fence последнего кадра, использовавшего этот
   back buffer, сбрасывает allocator/list и выставляет shader-visible heap.
2. `BeginRenderPass`: `PRESENT → RENDER_TARGET`, clear, RTV, viewport/scissor.
3. `SliceViewer::Draw`: fullscreen triangle, SRV `t0`, static linear-wrap
   sampler `s0`, root constants `b0`.
4. `EndRenderPass`: `RENDER_TARGET → PRESENT`.
5. `Present`: при capture дополнительно `PRESENT → COPY_SOURCE` и
   `CopyTextureRegion` в readback buffer, затем `COPY_SOURCE → PRESENT`.
   После `ExecuteCommandLists` и `Signal` вызывается `Present`.
6. Fence value запоминается для конкретного back buffer. Следующий кадр,
   получивший тот же buffer index, ждёт это значение.

### Владение ресурсами

- `Renderer`: device, queue, swap chain, RTV heap, SRV/UAV heap, 3 back buffer,
  3 command allocator/list, fence, immediate allocator/list, screen readback.
- `NoiseVolume`: `Texture3D`, UAV/SRV дескрипторы, compute root signature,
  `GenerateNoise` PSO, `ProbePeriodicity` PSO, probe buffer.
- `SliceViewer`: graphics root signature и PSO. SRV не принадлежит ему.
- `Window`: HWND и состояние resize/minimize.
- `Application`: владеет перечисленными объектами и порядком shutdown.

`NoiseVolume` не знает о Win32, окне и вводе. `SliceViewer` не знает, как
генерируется текстура. Это граница для V1.

### Состояния объёма

- Создание: `UNORDERED_ACCESS`.
- `Generate`: при необходимости переход в `UNORDERED_ACCESS`, dispatch,
  `UAV` barrier, переход в `ALL_SHADER_RESOURCE`.
- `Readback`/периодический probe: `ALL_SHADER_RESOURCE`/`UAV` →
  `COPY_SOURCE` → обратно.
- Во время интерактивного кадра объём всегда `ALL_SHADER_RESOURCE`, поэтому
  Graphics и Compute могут читать его в V1 без гонок с генерацией.

### Синхронизация

- Генерация и readback идут через `ExecuteImmediate`, который сначала ждёт
  полного GPU idle, затем записывает и выполняет отдельный list, затем снова
  ждёт idle. Это консервативно, зато исключает неоднозначность состояния
  объёма при редкой перегенерации.
- Resize: `WaitForGpu` → освобождение back buffer references →
  `ResizeBuffers` → пересоздание RTV. Minimize: цикл рендера спит; restore
  снова присылает `WM_SIZE`.
- Shutdown: сначала `WaitForGpu`, только потом освобождаются PSO, root
  signature и ресурсы. Это исправление реального падения, описанного в F.
- В debug-режиме после освобождения прикладных объектов вызывается
  `ID3D12DebugDevice::ReportLiveDeviceObjects(DETAIL | IGNORE_INTERNAL)`,
  сообщения читаются из info queue до уничтожения device.

## C. Аудит референсного ZIP

Архив: `OGSR_V0_Standalone_DX12_Source (1).zip`, 5 файлов, source-only.
Референс не собирался и не запускался на Windows; README сам это фиксирует.
Аудит выявил следующее.

| Наблюдение в референсе | Решение в V0 |
|---|---|
| `D3D12CreateDevice(nullptr, ...)` — выбирается адаптер по умолчанию; на гибридном ноутбуке это может быть iGPU | Явное перечисление DXGI-адаптеров, выбор по максимальной dedicated VRAM, `--adapter=N`, лог имени/vendor/device/VRAM |
| Debug layer только под `#ifdef _DEBUG`; в Release не включается | `--debug` работает и в Release; добавлены info queue, GPU-based validation, DRED |
| Нет чтения `ID3D12InfoQueue`, нет подсчёта ошибок | Все сообщения читаются, считаются errors/warnings, печатаются в лог |
| Нет live-object отчёта и проверки утечек | `ReportLiveDeviceObjects` + info queue; non-device live objects = 0 |
| Один allocator/list + `WaitForGpu` после каждого Present; нет per-frame fences | 3 back buffer, allocator/list/fence на buffer; ожидание только нужного fence |
| Нет обработки device removal, DRED, Present failure | Проверка Present, `GetDeviceRemovedReason`, DRED breadcrumbs/page fault |
| `noiseGenerated_` выставляется до завершения GPU; нет повторной генерации | Генерация только по dirty-флагу; генерация/readback синхронны и проверяемы |
| Параметры шума жёстко зашиты: 3 октавы, частоты 4/8/16, blend 1.35/0.5 | Октавы, базовая частота, lacunarity, gain, Worley frequency, blend, erosion, seed — управляемые |
| Нет seed | Seed входит в hash; проверена детерминированность |
| `WorleyF1` и `GradientNoise` используют магические константы и `saturate`; периодичность не доказана | Целочисленные периоды, wrap хеша, seam-метрика и прямой GPU-probe |
| `SlicePS` использует `frac(SliceZ)`: при `SliceZ = 1.0` показывает срез 0 | Срез задаётся в единицах текселя: `z = (SliceIndex + 0.5) / Resolution`, без `frac` |
| Только `R8G8B8A8_UNORM`, проверяется лишь UAV typed store | Приоритет `R16G16B16A16_FLOAT`, fallback UNORM; проверяются Texture3D + typed UAV + shader sample + UAV store |
| Нет диагностики границ | `D` — плитка 3×3; плюс статистические seam-метрики и точный GPU-probe |
| Нет readback, selftest, скриншотов, build-time проверки HLSL | `--selftest`, `--capture`, fxc-проверка entry points при сборке |
| Деструктор закрывает event даже если `WaitForGpu` бросил исключение | Явный порядок shutdown, ожидание GPU перед освобождением потребителей |
| CMake без `/W4`, `/utf-8`, `/permissive-` | `/W4 /permissive- /utf-8 /Zc:__cplusplus`; финальная сборка без предупреждений |

Что сохранено: изоляция от движка, Win32 + D3D12 + CMake, one-time compute
generation, целочисленная периодическая решётка, WARP-режим, fullscreen
triangle и grayscale-срезы.

Что переписано: весь каркас устройства/swapchain/синхронизации, формат и
разрешение объёма, параметры и перегенерация, viewer, диагностика, selftest,
capture, shutdown. Код референса не копировался вслепую.

## D. Реализация

### Модуль A — D3D12 Framework

- Перечисление адаптеров, выбор discrete GPU по VRAM, WARP fallback и
  `--warp`, лог feature level.
- Flip-discard swap chain, 3 buffers, RTV/SRV-UAV heaps, fullscreen triangle.
- Per-back-buffer fences и allocators; resize/minimize; `DXGI_MWA_NO_ALT_ENTER`.
- Debug layer, GPU-based validation, info queue, DRED, device-removed report.
- Понятные ошибки: `HRESULT` с именем, контекст операции, лог в
  `clouds_v0.log` рядом с EXE.

### Модуль B — Шумы

`NoiseGenerate.hlsl`, `cs_5_1`, `[numthreads(8,8,4)]`:

- `GradientNoise` — 12 градиентов, quintic fade, целочисленный wrap.
- `PerlinFbm` — октавы, целочисленные частоты, нормировка амплитуд.
- `WorleyF1` — 27 ячеек, wrap, feature point относительно текущей ячейки,
  инверсия F1.
- `EvaluateNoise` — единая функция для генерации и probe; каналы
  `Perlin`, `Worley`, `Perlin-Worley`.
- `ProbePeriodicity` — 64 точки, сравнение `f(p)` с `f(p+axis)`, запись
  максимальной разницы по осям.

Начальное разрешение 128³; 64³ доступно через `--size=64` и клавишу `4`.
Генерация не выполняется каждый кадр: только при старте, смене параметров,
`N` или смене разрешения.

### Модуль C — Viewer

- Каналы 1/2/3, Z-срез стрелками/колесом, анимация Space, сброс R.
- Параметры F/O/W/B/E/K и seed G, перегенерация N, разрешение 4.
- Диагностика `D`: 3×3 плитка; красная рамка только по внешнему краю,
  внутренние границы не перекрываются, чтобы разрыв был виден.
- Заголовок окна показывает канал, Z, разрешение, seed и параметры.

### Модуль D — Границы V1

`NoiseVolume::SrvGpuHandle()`, `Resolution()`, `Params()`, `Format()` — всё,
что нужно будущему ray-marching pass. Генерация и визуализация развязаны.

### Валидация

`Validation.cpp`:

- min/max/mean/stddev по трём каналам;
- seam ratio по X/Y/Z: `mean|v(0)-v(N-1)| / mean|v(i+1)-v(i)|`;
- проверка диапазона `[0,1]`, неконстантности, детерминизма, чувствительности
  к seed.

`--selftest` дополнительно выполняет прямой GPU-probe периодичности и пишет
`selftest.txt`. `--capture` проверяет present/copy/readback и пишет BMP.

## E. Build Evidence

Чистая сборка из пустого каталога:

```powershell
cmake -S tools/VolumetricCloudsTest -B tools/VolumetricCloudsTest/build-clean -G "Visual Studio 17 2022" -A x64
cmake --build tools/VolumetricCloudsTest/build-clean --config Release
cmake --build tools/VolumetricCloudsTest/build-clean --config Debug
```

Результат:

- Release: `build-clean/Release/OGSRCloudsTest.exe`, 181 760 bytes,
  SHA256 `E67FD260DC3DE602CBD20F2D8253D5E4AC184439AFF7ADF85C588EDCC82F0065`.
- Debug: `build-clean/Debug/OGSRCloudsTest.exe`.
- Обе конфигурации собраны без предупреждений компилятора.
- Unit-тест `OGSRCloudsAuditTests` собирается вместе с проектом; `ctest`
  проходит (`ctest_release.log`).
- `dumpbin`: `8664 machine (x64)`, subsystem `Windows CUI` (консоль выбрана
  осознанно для логов).
- fxc при сборке проверяет `GenerateNoise/cs_5_1`,
  `ProbePeriodicity/cs_5_1`, `ProbeSamplerWrap/cs_5_1`,
  `FullscreenVS/vs_5_1`, `SlicePS/ps_5_1`.
- Компилятор: MSVC 19.44.35229.0, Windows SDK 10.0.26100.0, CMake 4.4.3.

Логи: `artifacts/evidence/build_release_clean.log`,
`artifacts/evidence/build_debug_clean.log`,
`artifacts/evidence/configure_clean.log`.

Source-level проверки: `scripts/check_sources.ps1` — PASS, в исходниках нет
ссылок на игровой рендерер/runtime, entry points и контракты shader/CPP
совпадают.

## F. GPU Validation

Железо: NVIDIA GeForce RTX 5070 Laptop GPU, vendor `0x10de`, device `0x2d18`,
driver `32.0.16.1742`, 8 GiB dedicated VRAM, feature level 12_2.
OS: Windows 10.0.26200. SDK 10.0.26100.

| Прогон | Разрешение | Результат |
|---|---|---|
| Release, hardware, debug layer | 128³ | 0 errors, 0 warnings; selftest PASS |
| Release, hardware, debug layer | 64³ | 0 errors, 0 warnings; selftest PASS |
| Release, hardware, GPU-based validation | 128³ | 0 errors, 0 warnings; selftest PASS |
| Release, hardware, GPU-based validation | 64³ | 0 errors, 0 warnings; selftest PASS |
| Release, WARP, debug layer | 64³ | 0 errors, 0 warnings; selftest PASS |
| Release, UNORM fallback, debug layer | 64³ | 0 errors, 0 warnings; selftest PASS |
| Debug build, hardware, debug layer | 64³ | 0 errors, 0 warnings; selftest PASS |
| Release, resize test | 128³ | 3 размера, 0 errors; exit 0 |
| Release, клавиши + minimize/restore | 128³→64³ | seed/blend/regeneration/resolution; paused/resumed; 0 errors |
| Negative validation (`--test-no-validation`) | 64³ | INCONCLUSIVE, exit 3; PASS невозможен |
| Negative info queue при включённом Debug Layer | 64³ | INCONCLUSIVE, exit 3 |
| Negative frames/capture при недоступной validation | 64³ | exit 3, capture не создан |
| Negative combined `--selftest --capture` | 64³ | INCONCLUSIVE, exit 3, capture не создан |
| Negative capture при недоступной GBV | 64³ | отказ, exit 3, PNG не созданы |

Ключевые числа selftest 128³:

- Perlin: min 0.0465, max 0.9204, mean 0.4998, stddev 0.1219.
- Worley: min 0.2250, max 0.9971, mean 0.6513, stddev 0.1160.
- Combined: min 0.1763, max 0.9204, mean 0.5146, stddev 0.0934.
- Seam ratios: Perlin 0.852–1.091; Worley 1.055–1.068; Combined 0.992–1.087.
- Direct periodicity probe: X=0, Y=0, Z=0 (порог 1e-4); negative control 0.45625.
- WRAP sampler probe: filter error 0.000340104; p vs p+1 difference 0;
  CLAMP reference difference 0.0500488.
- Determinism max abs diff: 0 (порог 1e-6).
- Seed sensitivity: mean abs diff 0.111994 (порог 1e-3).

Для 64³: periodicity probe X=0/Y=0/Z=0, negative control 0.45625,
WRAP filter error 0.00049758, p vs p+1 = 0, CLAMP reference 0.0983887,
seam ratios 0.867–1.122.

Live objects при shutdown: единственное сообщение — `Live ID3D12Device`
(сам device ещё жив в момент отчёта, refcount 3); non-device objects = 0.
Это проверено для hardware, WARP, selftest и capture.

### Найденный и исправленный дефект

Первый автоматический resize-прогон завершился падением на shutdown
(исключение 0x87D, KERNELBASE). Причина: `SliceViewer` освобождал PSO/root
signature до `WaitForGpu`, пока GPU ещё мог использовать их в последнем draw.
Исправление: явный `WaitForGpu` в начале `Application::Shutdown`, затем
освобождение viewer/noise, затем renderer. После исправления resize-тест и
все прогоны завершаются с кодом 0. Это зафиксировано в логах и является
причиной, по которой нельзя освобождать потребителей GPU-ресурсов до idle.

### Ответы на вопросы независимого аудита

**A. GPU synchronization.** Ожидание fences: `Renderer::WaitForFence` и
`Renderer::SignalFence` в `src/Renderer.cpp`. У каждого back buffer свой
command allocator/list и своё последнее значение fence; `Renderer::BeginFrame`
ждёт fence именно этого buffer index до `Reset` allocator. `Present`
выставляет новое значение fence после `ExecuteCommandLists` и до `Present`,
а capture дополнительно ждёт его перед `Map`. `Resize` начинается с
`WaitForGpu`, затем освобождаются back buffer references, вызывается
`ResizeBuffers`, пересоздаются RTV. Shutdown: `Application::Shutdown` сначала
ждёт GPU, затем освобождает viewer/noise/renderer; renderer освобождает
ресурсы, PSO/root signatures уже не используются. Повторная генерация:
`Application::RegenerateIfNeeded`/`ChangeResolution` → `NoiseVolume::Generate`
→ `Renderer::ExecuteImmediate`, который делает `WaitForGpu` до и после записи.
Подробный код и порядок — в `src/Renderer.cpp`, `src/NoiseVolume.cpp`,
`src/Application.cpp`.

**B. Noise periodicity.** `ProbePeriodicity` вычисляет
`EvaluateNoise(p)` и `EvaluateNoise(p + axis)` для 64 непрерывных точек.
`EvaluateNoise` — та же функция, которую вызывает `GenerateNoise`; сравнение
идёт до записи в текстуру и до сэмплирования. Внутри неё, разумеется,
используются `floor`/`frac` и wrap ячеек — это и есть проверяемая
периодическая функция, а не сравнение координат после `frac`. Чтобы исключить
тавтологию, добавлен негативный контроль `f(p)` vs `f(p + 0.37)`: он равен
0.45625, тогда как X/Y/Z = 0. Результаты:

| Разрешение | X | Y | Z | negative control |
|---|---|---|---|---|
| 64³ | 0 | 0 | 0 | 0.45625 |
| 128³ | 0 | 0 | 0 | 0.45625 |

Отдельно `ProbeSamplerWrap` проверяет именно текстурную фильтрацию:
`Texture3D.SampleLevel` с linear-WRAP сэмплирует объём у швов X/Y/Z, а CPU
считает трилинейную интерполяцию по readback-объёму. Ошибка фильтра 3.4e-4
(128³) и 5.0e-4 (64³), `p` vs `p+1` = 0. Для контроля CLAMP-эталон отличается
на 0.050 (128³) и 0.098 (64³), то есть тест действительно пересекает шов.
Визуально это подтверждает `combined_diag_z064.png` (3×3 tiling без
перекрытия внутренних границ).

**C. GPU validation.** Полный лог 128³ с GPU-based validation:
`artifacts/evidence/run_selftest_gbv128.log`. В нём `debug errors: 0`,
`debug warnings: 0`, `V0 SELFTEST: PASS`; стартовое сообщение GBV имеет
severity `MESSAGE` и является информационным. Лог 64³:
`run_selftest_gbv64.log`.

**D. Resource lifetime.** В `Renderer::Shutdown` сначала освобождаются
screen readback, back buffers, RTV heap, swap chain, frame lists/allocators,
immediate list/allocator, fence event/fence, SRV/UAV heap и queue. Только после
этого вызывается `ID3D12DebugDevice::ReportLiveDeviceObjects`; device и
info queue ещё живы, чтобы отчёт можно было прочитать. В этот момент в логе
есть только `Live ID3D12Device` (refcount 3: device + info queue + debug
device), `non-device objects: 0`. Затем сбрасываются info queue, device и
factory, после чего вызывается DXGI `ReportLiveObjects` (его summary идёт в
`OutputDebugString`). Ресурсы, PSO, root signatures, swap chain и descriptor
heaps к моменту D3D12-отчёта уже освобождены.

**E. Visual evidence.** Команда, seed и параметры:

```powershell
OGSRCloudsTest.exe --capture=<dir> --debug --size=128 --seed=1337
```

```text
128^3, seed 1337, octaves 4, baseFrequency 4, lacunarity 2,
worleyFrequency 8, gain 0.5, blend 0.55, erosion 0.35,
format 16F (auto), channels Perlin/Worley/Combined,
Z = 16, 64, 112, diagnostic = Combined @ Z=64.
```

Полная таблица соответствия файлов и команд — в
`tools/VolumetricCloudsTest/REPRODUCE.md`.

## G. Visual Results

Все снимки — 1280×720, 128³, seed 1337, 16F, сняты с back buffer через
readback. Каталог: `tools/VolumetricCloudsTest/artifacts/capture/`.
Приложение сохраняет BMP; для просмотра они сконвертированы в PNG с теми же
именами (`System.Drawing`). `capture_info.txt` содержит исходный список BMP.

| Файл | Содержимое |
|---|---|
| `perlin_z016.png` | Perlin fBm, Z = 16 |
| `perlin_z064.png` | Perlin fBm, Z = 64 |
| `perlin_z112.png` | Perlin fBm, Z = 112 |
| `worley_z016.png` | Worley F1 (инвертированный), Z = 16 |
| `worley_z064.png` | Worley F1, Z = 64 |
| `worley_z112.png` | Worley F1, Z = 112 |
| `combined_z016.png` | Perlin-Worley + erosion, Z = 16 |
| `combined_z064.png` | Perlin-Worley + erosion, Z = 64 |
| `combined_z112.png` | Perlin-Worley + erosion, Z = 112 |
| `combined_diag_z064.png` | 3×3 плитка combined, диагностика швов |
| `contact_sheet.png` | сводный лист: Perlin / Worley / Combined / диагностика |

Perlin визуально плавный, Worley даёт выраженные ячейки, Combined — их
erosion-смесь. На 3×3 диагностике внутренние границы не видны; это
согласуется с seam ratios и probe = 0. SHA256 снимков — в
`artifacts/evidence/manifest.txt`.

## H. Known Issues

| Уровень | Проблема | Комментарий |
|---|---|---|
| Ограничение V0 | Нет ray marching, scattering, density profile | Явно вне V0; `Texture3D` готова для V1 |
| Ограничение V0 | Нет device-recreation после removal | DRED/причина логируются, но приложение завершается |
| Ограничение | DXGI live-object summary выводится через `OutputDebugString` | Device-level live-object report читается через info queue; DXGI summary нужно смотреть в debugger |
| Низкий | `--frames`/`--resize-test` не benchmark | Present может не ограничиваться vsync для фонового окна |
| Низкий | WARP медленный | Используется только для корректности |
| Низкий | fxc ищется в стандартном Windows SDK | Если SDK в нестандартном месте, build-time проверка отключается с warning, runtime-компиляция работает |
| Низкий | Артефакты в `artifacts/` не в git | Скриншоты и логи доступны в рабочем дереве; коммит не выполнялся |
| Информационно | Консольная подсистема EXE | Сделано для диагностики; окно Win32 создаётся отдельно |

Критических известных validation errors, утечек app-объектов или
неисправленных lifetime-дефектов на момент отчёта нет. Независимый аудит
обязан проверить это самостоятельно.

### V0 Audit Hardening (A01–A03)

**A01 — device removal sentinel.** В `src/Renderer.cpp` значение
`GetCompletedValue()` классифицируется через `audit::ClassifyFenceCompletion`
до любого сравнения: `UINT64_MAX` — это `DeviceRemoved`, а не завершение.
После `WaitForSingleObject` fence проверяется повторно (цикл с дедлайном и
повторным `SetEventOnCompletion`); timeout также перепроверяет sentinel.
`sentinel` вызывает `ReportDeviceRemoved(..., true)`, сохраняет
`deviceRemoved_` и `lastDeviceRemovedReason_`, печатает DRED и завершает
ожидание исключением. `BeginFrame`/`ExecuteImmediate` не доходят до
`allocator->Reset` при ложном «завершении». Unit-тест:
`tests/audit_policy_test.cpp` → `FENCE SENTINEL TESTS: PASS`.

**A02 — validation PASS policy.** `audit::EvaluateValidationStatus` в
`src/AuditPolicy.h` различает `NotRequested`, `Pass`, `Fail`, `Inconclusive`.
Если `--debug`/`--gpu-validation` запрошены, но debug layer или GBV не
активировались, selftest пишет `validation verdict: INCONCLUSIVE` и
`V0 SELFTEST: INCONCLUSIVE`, возвращает exit code **3**. Capture/frames без
selftest при недоступной GBV завершаются с кодом 3, не создавая снимков.
Служебный `--test-no-validation` имитирует недоступность инфраструктуры без
потери реального GPU; лог: `run_negative_validation.log`,
`run_negative_validation_capture.log`. PASS в этом сценарии невозможен.

**A03 — shutdown diagnostics.** `Application::Shutdown` теперь возвращает
`bool`; `NoiseVolume::Shutdown` и `Renderer::Shutdown` различают успешное
`GPU idle` и best-effort cleanup после device removal/неуспешного ожидания.
При `DeviceRemoved()` повторные ожидания пропускаются, причина и DRED
логируются, а `Run` повышает код результата до 2, если нормальный shutdown
не подтверждён.

**A02-FINAL — единый preflight.** Перед `--selftest`, `--capture`, `--frames`
и interactive mode вызывается `Application::PreflightValidation()`. Он отдельно
учитывает `--debug`, `--gpu-validation`, наличие Debug Layer и доступность
`ID3D12InfoQueue`. Если запрошенная диагностика недоступна, приложение пишет
`INCONCLUSIVE` (для selftest — в `selftest.txt`), возвращает exit code 3 и не
создаёт capture-артефакты. Обязательная матрица проверена: `--debug
--test-no-validation --frames=1`, `--debug --test-no-validation --capture=...`,
`--debug --gpu-validation --test-no-validation --selftest`, отсутствие
`ID3D12InfoQueue` при включённом Debug Layer (включая `--gpu-validation`) и
совместный `--selftest --capture`. Все дают exit 3 и не дают PASS. Успешные
сценарии без обязательного Debug Layer (`--selftest`, `--capture`, `--frames`)
по-прежнему работают с verdict `NOT REQUESTED`.

Минимальный набор изменённых файлов:

```
src/AuditPolicy.h                  new: fence/validation policy + unit tests contract
tests/audit_policy_test.cpp        new: negative sentinel/validation tests
src/Renderer.h/.cpp                A01, A03, DRED/HRESULT state
src/Application.h/.cpp             A02, A03, exit codes/verdicts
src/NoiseVolume.cpp                A03 best-effort cleanup
src/main.cpp                       --test-no-validation, option logging
CMakeLists.txt                     test target + ctest
scripts/check_sources.*            policy/negative-test checks
scripts/run_evidence.ps1           unit/negative tests in evidence run
README.md, REPRODUCE.md, docs/...  hardening documentation
```

Повторные тесты: Release/Debug — без предупреждений; `ctest` —
`OGSRCloudsAuditTests` PASS; hardware 128³ selftest PASS; hardware 128³ GBV
PASS (0 errors/warnings); WARP 64³ PASS; resize/keys/minimize — 0 errors;
determinism и WRAP sampler — PASS; negative validation — INCONCLUSIVE (exit 3);
negative capture — отказано (exit 3, PNG не созданы). Полные логи:
`artifacts/evidence/run_selftest_hw128.log`, `run_selftest_gbv128.log`,
`run_selftest_warp64.log`, `run_resize_hw128.log`, `audit_policy_tests.log`,
`run_negative_validation.log`, `run_negative_validation_capture.log`,
`ctest_release.log`.

## I. Final Verdict

**READY FOR FINAL DELTA REVIEW**

Локальные критерии V0 выполнены: EXE собирается в x64, запускается на реальном
D3D12 GPU, все три шума визуализируются, срезы переключаются, периодичность
подтверждена прямым GPU-probe и seam-метриками, resize/close/minimize
работают, debug layer и GPU-based validation не дают ошибок, live-object
отчёт не показывает app-утечек, игровой рендерер не изменён.

Финальный PASS не присваивается: результат передаётся техническому
руководителю для независимого аудита. Corrective pass A01–A03 выполнен;
дальнейшая работа — только после delta audit. V1 не начинать.

## Evidence index

```
tools/VolumetricCloudsTest/artifacts/evidence/
  configure_clean.log
  build_release_clean.log
  build_debug_clean.log
  gpu_info.txt
  git_state.txt
  run_selftest_hw128.log
  selftest_hw128.txt
  run_selftest_hw64.log
  selftest_hw64.txt
  run_selftest_gbv128.log
  selftest_gbv128.txt
  run_selftest_gbv64.log
  selftest_gbv64.txt
  run_selftest_warp64.log
  selftest_warp64.txt
  run_selftest_unorm64.log
  selftest_unorm64.txt
  run_selftest_debug64.log
  selftest_debug64.txt
  audit_policy_tests.log
  run_negative_validation.log
  selftest_negative_no_validation.txt
  run_negative_validation_capture.log
  run_negative_validation_capture_gpu.log
  run_negative_frames.log
  run_negative_no_info_queue.log
  selftest_negative_no_infoqueue.txt
  run_negative_gpu_no_info_queue.log
  selftest_negative_gpu_no_infoqueue.txt
  run_negative_combined.log
  selftest_negative_combined.txt
  ctest_release.log
  HARDENING_CHANGES.md
  A02_FINAL_CHANGES.md
  git_diff_hardening.patch
  git_diff_stat_hardening.txt
  git_changed_files_hardening.txt
  git_diff_a02_final.patch
  git_diff_stat_a02_final.txt
  git_changed_files_a02_final.txt
  run_capture_hw128.log
  capture_info.txt
  run_evidence.log
  run_resize_hw128.log
  run_keys_minimize_hw.log
  title_check.log
  window_title_utf8.txt
  manifest.txt
  source_checks.log
```
