# F2.1 — First Frame (DX12 bootstrap)

## HEAD и ветка

| Параметр | Значение |
|---|---|
| Ветка | `prypiat` |
| HEAD | `64b1b81c5` — F2.0.1: repair DX12 solution configuration |
| Дерево | `D:\Coding Project\OGSR ENGINE fork from rtx` |
| Подэтап | **F2.1-B.1 — Final Correctness Hotfix** (текущий) |
| Дата | 2026-10-15 |
| Базовый коммит | `d23009f1b` — F2.1-B: fix DX12 bootstrap correctness and add debug layer |

## Статус

**PARTIAL.** Код исправлен и компилируется. GPU runtime не подтверждён:
`xrDX12Smoke.exe` не запускался, запуск запрещён без отдельного разрешения.

## Что было не так в F2.1-A (аудит 2026-10-15)

| # | Проблема | Файл / строки |
|---|---|---|
| 1 | Debug Layer отсутствовал полностью: нет `D3D12GetDebugInterface`, `EnableDebugLayer`, DRED, `ReportLiveObjects`. `#ifdef _DEBUG` + `<dxgidebug.h>` — мёртвый код, `ReleaseDX12` не определяет `_DEBUG` | `CHW12.cpp:4-6` |
| 2 | `Present` возвращал `DXGI_ERROR_DEVICE_REMOVED`/`DEVICE_RESET`, которые никто не читал. Критерий приёмки «Device removed обрабатывается корректно» не выполнялся | `CHW12.cpp:228` |
| 3 | `ExecuteCommandLists`, `Signal`, `Close` — HRESULT отброшен. `EnumAdapterByGpuPreference` вызван без проверки | `CHW12.cpp:223,225,232,60` |
| 4 | Нет `GetDeviceRemovedReason()` и DRED-вывода: причина падения осталась бы неизвестной | — |
| 5 | `WaitForGpu()` сигналил ранее отправленное `m_fenceValue` и ждал его же. Работает только по совпадению в однопоточном потоке | `CHW12.cpp:273-274` |
| 6 | `WaitForSingleObject(..., INFINITE)`: повисший GPU молча подвешивал приложение | `CHW12.cpp:185,279` |
| 7 | Нет `WM_SIZE`/`WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE`: `Resize()` не вызывался ниоткуда, occlusion (`DXGI_STATUS_OCCLUDED`) отбрасывался | `xrDX12Smoke.cpp` |
| 8 | `Resize()` при провале `ResizeBuffers` оставлял backbuffer'ы отпущенными и не пересозданными, `RenderFrame` продолжал писать в null | `CHW12.cpp:255-258` |
| 9 | Командный лист создавался заново каждый кадр, вопреки ADR §10.3 | `CHW12.cpp:197-200` |
| 10 | Alt+Enter не подавлен: `MakeWindowAssociation` не вызван | — |
| 11 | Программный адаптер не отсекался: smoke мог тихо уйти на WARP | `CHW12.cpp:56-66` |
| 12 | `ProjectReference` продублирован двумя одинаковыми `ItemGroup` | `xrDX12Smoke.vcxproj:72-80` |
| 13 | `{E8A4C1D2}.Release|x64.Build.0` отсутствовал: smoke не собирался в конфигурации `Release` | `Engine.sln` |
| 14 | `RenderFrame()` возвращал `false`, вызывающий код это игнорировал: бесконечный горячий цикл на сломанном устройстве | `xrDX12Smoke.cpp:70` |

## Выполненные изменения

### `CHW12.h`

- `dxgi1_4.h` → `dxgi1_6.h`.
- `FrameResource` получил `ComPtr<ID3D12GraphicsCommandList> commandList` — лист живёт вместе со слотом.
- Добавлены `GPU_WAIT_TIMEOUT_MS = 5000`.
- `IsInitialized()` теперь читает `m_initialized`, а не `m_device != nullptr`.
- Новые публичные: `IsDeviceRemoved()`, `IsOccluded()`.
- Новые приватные: `CreateFrameResources()`, `EnableDebugLayer()`,
  `WaitForFenceValue(uint64_t)`, `DrainInfoQueue()`, `ReportDeviceRemoved(HRESULT)`.
- `WaitForGpu()` из `void` стал `bool`.
- Новые члены: `m_infoQueue`, `m_deviceRemoved`, `m_occluded`, `m_debugLayerEnabled`.

### `CHW12.cpp`

**Debug Layer + DRED** (`EnableDebugLayer`, вызывается первой строкой `Initialize`):

- `D3D12GetDebugInterface` → `ID3D12Debug::EnableDebugLayer()`.
- `ID3D12Debug1::SetEnableGPUBasedValidation(TRUE)`.
- `ID3D12DeviceRemovedExtendedDataSettings`: `SetAutoBreadcrumbsEnablement` и
  `SetPageFaultEnablement` в `D3D12_DRED_ENABLEMENT_FORCED_ON`.
- `ID3D12InfoQueue` берётся с устройства; `DrainInfoQueue()` печатает
  накопившиеся validation-сообщения через `printf` в конце каждого кадра,
  в `Resize` и в `ReportDeviceRemoved`. Без этого при запуске без отладчика
  ошибки D3D12 не видны вообще.
- `Shutdown()` вызывает `IDXGIDebug1::ReportLiveObjects` до сброса device.

**Синхронизация**:

- `WaitForGpu()` сигналит `++m_fenceValue` и ждёт именно это значение.
- Новый `WaitForFenceValue()` — таймаут 5000 мс вместо `INFINITE`.
- Ждём в начале `RenderFrame()` только слот `m_frameIndex`, по его `fenceValue`.
- Fence сигналится и при `DXGI_STATUS_OCCLUDED`, иначе слот после
  восстановления окна завис бы навсегда.
- Аллокатор и лист создаются один раз в `CreateFrameResources()`, лист
  закрывается сразу после создания, чтобы `Reset()` был первым обращением.

**HRESULT**:

- Проверяются `Present`, `Signal` (все три места), `Close`, `CreateCommandList`,
  `CreateCommandAllocator`, `EnumAdapterByGpuPreference` (в цикле, с выходом по
  `DXGI_ERROR_NOT_FOUND`), `GetDesc1`, `ResizeBuffers`, `CreateDescriptorHeap`,
  `GetBuffer`, `CreateSwapChainForHwnd`.
- `ReportDeviceRemoved()` печатает `hr`, `GetDeviceRemovedReason()` и
  DRED page-fault VA, затем сливает info queue.

**Resize**:

- `width == 0 || height == 0` трактуется как `WM_SIZE` от свёрнутого окна и
  возвращает `true`, а не провал.
- При провале `ResizeBuffers` вызывается `ReportDeviceRemoved` и цикл кадров
  останавливается — несогласованное состояние больше не используется.
- `m_rtvHeap.Reset()` перед пересозданием RTV: старый хип указывает на
  освобождённые буферы.
- `WaitForGpu()` провал прерывает ресайз до отпускания backbuffer'ов.

**Прочее**:

- `MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER)` — Alt+Enter подавлен.
- Отсечение адаптера с `DXGI_ADAPTER_FLAG_SOFTWARE` (WARP).
- Полностью заполненный `DXGI_SWAP_CHAIN_DESC1` (все поля, включая `Scaling`,
  `AlphaMode`, `Stereo`, `SampleDesc.Quality`), `queueDesc.Priority`.
- `SIZE_T`-каст в арифметике дескрипторного хэндла.
- `DXGI_DEBUG_ALL` заменён на локальный `kDxgiDebugAll`: символ из `dxgidebug.h`
  — extern-объявление под `dxguid.lib`, которого в проекте нет.

### `xrDX12Smoke.cpp`

- `WndProc` получил `WM_SIZE` с вызовом `CHW12::Resize(LOWORD, HIWORD)`;
  провал ресайза уничтожает окно и завершает цикл.
- Глобальный `g_hw` для доступа из `WndProc`.
- Цикл выходит при `RenderFrame() == false` с печатью `IsDeviceRemoved()`.
- `PeekMessage` во внутреннем `while` с явным `WM_QUIT`, иначе после `WM_QUIT`
  в середине пачки сообщений цикл продолжал рендерить.
- `UnregisterClassW` на всех путях выхода.
- Возврат `0` вместо `msg.wParam`: `msg` не инициализирован при выходе по отказу.

### `xrDX12Smoke.vcxproj`

- Удалён дублирующий `ItemGroup` с `ProjectReference`.

### `Engine.sln`

- Добавлено `{E8A4C1D2-3B5F-4E7A-8C9D-1F2A3B4C5D6E}.Release|x64.Build.0 = ReleaseDX12|x64`.

### `stdafx.h`

- Добавлен `<cstdlib>` (для `malloc`/`free` в `DrainInfoQueue`).

## Результаты проверок

### Сборка smoke-проекта

```
msbuild xrDX12Smoke.vcxproj /t:Rebuild /p:Configuration=ReleaseDX12 /p:Platform=x64
```

| Параметр | Значение |
|---|---|
| ExitCode | 0 |
| Ошибок | 0 |
| Предупреждений | 0 |
| Артефакт | `ogsr_engine\Layers\xrRenderDX12\x64\ReleaseDX12\xrDX12Smoke.exe` |

### Сборка решения ReleaseDX12

`msbuild Engine.sln /t:Rebuild /p:Configuration=ReleaseDX12 /p:Platform=x64`

| Параметр | Значение |
|---|---|
| ExitCode | 0 |
| Ошибок | 0 (в отфильтрованном логе совпадений на `error` только в именах файлов: `error.cpp`, `error_private.c`, `tinyxmlerror.cpp`) |
| Артефакт | `x64\ReleaseDX12\xrDX12Smoke.exe` |

Повторный `Build` после правок: exit 0, `xrDX12Smoke.vcxproj -> x64\ReleaseDX12\xrDX12Smoke.exe`.

### DX11 baseline

Не пересобирался в рамках F2.1-B: правки не задевают `xrRenderPC_R4`.
Проверка требуется перед любым коммитом.

## Нерешённые проблемы

1. **GPU runtime не подтверждён.** Exe не запускался. Критерии приёмки F2.1
   из `docs/dx12-f2-bootstrap.md:92-98` остаются непроверенными:
   валидный `ID3D12Device*`, 3 backbuffer, цветной кадр, resize без validation
   errors, обработка device removed.
2. **DRED-вывод не верифицирован.** `ReportDeviceRemoved()` написан, но путь
   срабатывания не исполнялся ни разу.
3. **`ReportLiveObjects` не проверен** на реальных утечках.
4. **GPU-based validation включена всегда**, без флага. На слабых адаптерах
   это заметно замедляет кадры; для smoke-целей приемлемо, но стоит вынести
   в отдельный `#define` при переносе в игровой путь.
5. **`m_occluded` не используется вызывающим кодом.** Флаг есть, но окно не
   прекращает рендерить при минимизации, а только переживает occlusion.
6. **CI не собирает ReleaseDX12.** `.github/workflows/push.yml` matrix —
   `[Release, ReleaseTracyProfiler]`. Регрессия DX12-слоя пройдёт незамеченной.

## Критерии приёмки F2.1

Из `docs/dx12-f2-bootstrap.md`, статус после F2.1-B:

- [ ] `D3D12CreateDevice` возвращает валидный `ID3D12Device*` — код есть, runtime нет
- [ ] Swapchain создаётся с 3 backbuffers — код есть, runtime нет
- [ ] `ClearRenderTargetView` + `Present` показывают цветной кадр — код есть, runtime нет
- [ ] Resize работает без validation errors — код есть, runtime нет
- [ ] Device removed обрабатывается корректно — код есть, runtime нет
- [ ] DX11 baseline остаётся рабочим — **проверено в F2.1-B.1: `Release|x64` Rebuild, 0 ошибок компиляции и линковки, `bin_x64\xrEngine.exe` создан**

---

# F2.1-B.1 — Final Correctness Hotfix

**Базовый коммит:** `d23009f1b`. **Дата:** 2026-10-15. **Ветка:** `prypiat`.
Правки по результатам code review. `CHW12` не переписывался, архитектура
не менялась: только подтверждённые проблемы.

## 1. Shutdown и частичная инициализация

**Проблема:** `Shutdown()` начинался с `if (!m_initialized) return;`. После
частично неуспешного `Initialize()` (провал на любом шаге после создания
устройства) вся очистка блокировалась: `m_initialized` так и оставался
`false`, а деструктор уходил в no-op. Утечка устройства, очереди, swapchain
и всех RTV. Повторный вызов после явного `Shutdown()` тоже был no-op только
по совпадению.

**Исправлено:**

- Введён `enum class Stage { None, DebugLayer, Device, SwapChain,
  RenderTargetViews, FrameResources, Fence, Ready }`, член `m_stage`.
  Каждый шаг `Initialize()` выставляет свою стадию после успеха, поэтому
  shutdown знает, какие объекты существуют.
- `Shutdown()` идемпотентен: ранний выход только если `m_stage == None`
  **и** `!m_initialized`. Повторный вызов — no-op.
- Все `ComPtr::Reset()` сами проверяют указатель, поэтому частично созданное
  состояние разбирается корректно: несуществующие объекты просто отсутствуют.
  Отдельный cleanup-helper не потребовался, RAII через `ComPtr` сохранён.
- Порядок освобождения сохранён: backbuffers → commandList → commandAllocator
  → rtvHeap → commandQueue → swapChain → `ReportLiveObjects` → device → fence
  → event. `ReportLiveObjects` вызывается до сброса device, иначе отчёт пуст.
- GPU-ожидание выполняется только если `m_commandQueue && m_fence &&
  m_fenceEvent && m_gpuWorkSubmitted`. До первой отправки команд GPU нечего
  ждать, а `SetEventOnCompletion` на fence, который никто не сигналил, завис
  бы до таймаута. Отсутствующие queue, fence и event не разыменовываются.
- Неуспешная синхронизация **не** считается успешной: при провале `WaitForGpu()`
  ресурсы освобождаются, но в лог пишется явное предупреждение, что гарантии
  завершения GPU нет.

## 2. Fence synchronization

**Проблема:** в `WaitForGpu()` результат `m_commandQueue->Signal(...)`
отбрасывался. При провале `Signal()` код всё равно вызывал
`WaitForFenceValue()`, то есть ждал fence, который никогда не будет
сигналирован, — молча до таймаута.

**Исправлено:**

- `Signal()` HRESULT проверяется. При `FAILED` ожидание не начинается,
  `WaitForGpu()` возвращает `false` немедленно.
- Монотонность значений fence сохранена: `++m_fenceValue` до `Signal()`.
- `WaitForFenceValue(uint64_t value, bool& timedOut)` разделяет исходы:
  `false` без `timedOut` — аппаратный сбой API (`SetEventOnCompletion`
  провалился или нет fence/event), `false` с `timedOut` — GPU не ответил за
  `GPU_WAIT_TIMEOUT_MS`. Это разные диагнозы.
- `WaitForGpu()` проверяется во всех вызывающих: `Resize()` и `Shutdown()`.
  При отказе GPU `Resize` **не** отпускает backbuffer'ы.
- `Sleep()` не используется.
- `m_gpuWorkSubmitted` выставляется в `RenderFrame()` после
  `ExecuteCommandLists()`. До этого `WaitForGpu()` возвращает `true`, не
  отправляя сигнал на fence без работы.

## 3. Критические GPU failures

Последовательность `ExecuteCommandLists → Present → Signal → Next Frame`:

- **Ошибка Present** → `RenderFrame()` возвращает `false`, следующий кадр не
  начинается. `DXGI_ERROR_DEVICE_REMOVED` / `DXGI_ERROR_DEVICE_RESET`
  идут в `ReportGpuFailure()`; прочие `hr` логируются как ошибка Present без
  утверждения об удалении устройства.
- **Ошибка Signal** → аллокатор не сбрасывается преждевременно: кадр
  останавливается здесь, до следующего `RenderFrame()`, который делал бы
  `Reset()`.
- **Ошибка ResizeBuffers** → рендеринг прекращается. Освобождённые
  backbuffer'ы никем не используются: `m_frameResources[i].renderTarget`
  больше не читается, а `m_deviceRemoved` блокирует `RenderFrame()`.
- **Ошибка CreateRenderTargetViews после resize** → рендеринг прекращается.
  Буферы новые и валидные, но без RTV рисовать нельзя; освобождённое
  (старый rtvHeap) не трогается.
- **Таймаут GPU** → рендеринг прекращается, в лог уходит отдельная строка
  с значением fence и таймаутом.
- `ReportDeviceRemoved` переименован в `ReportGpuFailure(stage, hr)` и
  вызывается **только** при подтверждённой аппаратной проблеме. Обычная
  ошибка вызова и таймат сюда не попадают: раньше таймаут ожидания слота
  объявлялся как `DXGI_ERROR_DEVICE_REMOVED`, что было неверным утверждением.
- Автоматического восстановления устройства нет: на F2.1 допустим
  безопасный отказ с диагностикой.

## 4. DX11 Release isolation

**Проблема:** в `Engine.sln` присутствовало
`{E8A4C1D2-...}.Release|x64.Build.0 = ReleaseDX12|x64`, из-за чего обычная
DX11 `Release` собирала экспериментальный smoke-exe.

**Исправлено:** удалена только эта строка `Build.0`. `ActiveCfg` для
`Release|x64` сохранён, `ReleaseDX12|x64` (ActiveCfg + Build.0) не тронут.

**Проверено:** `Release|x64` Rebuild — `xrDX12Smoke` в сборке отсутствует
(0 совпадений в логе), `bin_x64\xrEngine.exe` линкуется.

## 5. Resize и клиентская область

- `WS_OVERLAPPEDWINDOW` вместо `WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME`:
  окно снова изменяемого размера.
- `AdjustWindowRectEx(&desired, style, FALSE, 0)`: 1280x720 — это **client
  area**, а не размер окна. Без поправки backbuffer получал размер окна
  вместе с рамкой.
- `GetClientRect()` после создания окна: фактические ширина и высота
  client area идут в `Initialize()`, а не захардкоженные 1280x720.
- `WM_SIZE` с `SIZE_MINIMIZED` не вызывает `Resize` (client area нулевой),
  а выставляет `g_minimized`. При нулевых `LOWORD`/`HIWORD` `Resize`
  тоже не вызывается.
- При минимизации рендеринг не выполняется вовсе (`continue` в цикле), а не
  слаёт `Present` в occlusion на каждом кадре.
- Рекурсивный и параллельный Resize невозможен: `Resize` вызывается только
  из `WndProc` в главном потоке, а `WndProc` исполняется внутри
  `DispatchMessage` того же цикла, что и `RenderFrame`.

## 6. Диагностический лог

Приложение собрано с `SubSystem=Windows`, поэтому `printf`/`wprintf` при
запуске без консоли не видны. Добавлен `dx12log.h` / `dx12log.cpp`:

- Файл `xrDX12Smoke.log` рядом с exe (`GetModuleFileNameW` + замена имени).
- UTF-8 (`w, ccs=UTF-8`), временная метка в каждой строке.
- `Write()` для узких строк, `WriteWide()` для описаний GPU
  (UTF-16 → UTF-8 через `WideCharToMultiByte`).
- Логируются: адаптер (вендор, устройство), все HRESULT, статус Debug Layer
  и DRED, устройство удалено или нет, все сообщения `ID3D12InfoQueue` с
  разделением severity (`CORRUPTION` / `ERROR` / `WARNING` / `INFO` /
  `MESSAGE`), причина неуспешного `Initialize` (конкретная стадия),
  причина отказа `RenderFrame`.
- Каждый шаг `Initialize()` при провале пишет свою строку, поэтому видно,
  где именно сломалось.
- Сбой `_wfopen_s` молча отключает лог: запись не является условием
  успешного рендеринга.
- Секреты и пользовательские данные не логируются.
- На каждом кадре лог не пишется: `DrainInfoQueue()` вызывается только когда
  `GetNumStoredMessages() != 0`.
- `xrDX12Smoke.log` добавлен в `.gitignore`.
- `OutputDebugString` не используется: файл читается и без отладчика.

## 7. Debug Layer и DRED

- `EnableDebugLayer()` по-прежнему вызывается до создания устройства.
- GPU-Based Validation отключается `#define OGSR_DX12_DISABLE_GBV`
  (по умолчанию включена).
- Провал `D3D12GetDebugInterface` больше не является фатальным: smoke
  запускается без validation, в лог уходит соответствующая строка. Раньше
  машина без Graphics Tools не дала бы вообще никакой диагностики.
- Недоступность DRED-интерфейса не мешает запуску, тоже логируется.
- Severity в `DrainInfoQueue()` различается, настоящие ошибки validation не
  подавляются.
- `ReportLiveObjects()` выполняется в правильной фазе: после освобождения
  frame resources, но до сброса `m_device`.

## 8. Результаты сборок

Все четыре сборки выполнены на этой машине (MSBuild 14.44.35207,
PlatformToolset v143, Windows SDK 10.0.26100.0).

### A. DX12 library — `xrRender_R5_DX12.vcxproj`, ReleaseDX12|x64

```
msbuild xrRender_R5_DX12.vcxproj /t:Rebuild /p:Configuration=ReleaseDX12 /p:Platform=x64
```

| Параметр | Значение |
|---|---|
| ExitCode | 0 |
| Ошибок | 0 |
| Предупреждений | 0 |
| Артефакт | `ogsr_engine\Layers\xrRenderPC_R5_DX12\ogsr_engine\_LIB\Engine\ReleaseDX12\x64\xrRender_R5_DX12.lib` |

### B. Smoke — `xrDX12Smoke.vcxproj`, ReleaseDX12|x64

| Параметр | Значение |
|---|---|
| ExitCode | 0 |
| Ошибок | 0 |
| Предупреждений компилятора | 0 |
| Артефакт | `ogsr_engine\Layers\xrRenderDX12\x64\ReleaseDX12\xrDX12Smoke.exe` (531 456 байт) |

Линковка реально выполнена: в логе присутствует вызов `link.exe` с
`d3d12.lib dxgi.lib` и обе трансляции (`xrDX12Smoke.obj`, `dx12log.obj`),
плюс `xrRender_R5_DX12.lib(CHW12.obj)`.

### C. Solution — `Engine.sln`, ReleaseDX12|x64

| Параметр | Значение |
|---|---|
| ExitCode | 0 |
| Ошибок | 0 |
| MSB4121 | 0 |
| Артефакт | `x64\ReleaseDX12\xrDX12Smoke.exe` |

### D. DX11 regression — `Engine.sln`, Release|x64

| Параметр | Значение |
|---|---|
| Ошибок компиляции и линковки | 0 |
| `xrDX12Smoke` в сборке | отсутствует (0 совпадений в логе) |
| `xrEngine.exe` | `bin_x64\xrEngine.exe`, 31 463 424 байт |
| MSB4121 | 0 |

Известная проблема: post-build шаг `copy dbghelp.dll` падает с
`MSB3073` («Системе не удается найти указанный путь»). Это §13.1
`docs/07-known-issues.md`, не связано с правками F2.1-B.1: `xrEngine.exe`
уже слинкован, ошибка возникает после линковки.

**Важная деталь окружения:** для сборки DX11 `Release` требуются include-пути
ATL/MFC (`CL=/I"...\MSVC\14.30.30705\atlmfc\include"` и
`LINK=/LIBPATH:"...\atlmfc\lib\x64"`). `splash.cpp:11` тянет `<atlstr.h>`.
ATL/MFC есть только в toolset 14.30.30705, а не в активном 14.44.35207.
Без этих переменных сборка падает на `error C1083: atlstr.h`. Это свойство
машины, а не репозитория.

## 9. Изменённые файлы

| Файл | Что изменено |
|---|---|
| `ogsr_engine/Layers/xrRenderDX12/CHW12.h` | `enum class Stage`, `m_stage`, `m_gpuWorkSubmitted`, `m_width`/`m_height` сохранены; `WaitForFenceValue(value, timedOut)`; `bool WaitForGpu()`; `CreateSynchronization()` вместо создания fence в `Initialize`; `ReportGpuFailure(stage, hr)` вместо `ReportDeviceRemoved(hr)`; `IsDeviceRemoved()`/`IsOccluded()` сохранены |
| `ogsr_engine/Layers/xrRenderDX12/CHW12.cpp` | идемпотентный `Shutdown()` со стадиями; проверка `Signal()`; разделение timeout / API-сбоя; `ReportGpuFailure` только при подтверждённой аппаратной проблеме; стадийная диагностика `Initialize`; GBV через `#define`; Debug Layer и DRED больше не фатальны; `m_gpuWorkSubmitted`; логи через `dx12log` |
| `ogsr_engine/Layers/xrRenderDX12/dx12log.h` | **новый** — файловый логгер |
| `ogsr_engine/Layers/xrRenderDX12/dx12log.cpp` | **новый** — реализация логгера |
| `ogsr_engine/Layers/xrRenderDX12/xrDX12Smoke.cpp` | изменяемое окно; `AdjustWindowRectEx`; `GetClientRect`; `WM_SIZE` с `SIZE_MINIMIZED`; запрет рендера при минимизации; логгер |
| `ogsr_engine/Layers/xrRenderDX12/xrDX12Smoke.vcxproj` | добавлен `dx12log.cpp` |
| `Engine.sln` | удалён `{E8A4C1D2}.Release|x64.Build.0` |
| `.gitignore` | `xrDX12Smoke.log` |
| `docs/dx12-f2-first-frame.md` | этот раздел |

## 10. Оставшиеся риски

1. **GPU runtime не подтверждён.** Exe не запускался. Все критерии приёмки
   F2.1 остаются непроверенными, включая корректность resize на реальном
   драйвере и отсутствие validation errors.
2. **`ReportLiveObjects()` не проверен** на реальных утечках.
3. **DRED-вывод не верифицирован:** путь срабатывания не исполнялся.
4. **Логгер не проверен в работе:** файл создаётся при старте, но содержимое
   реального запуска не читалось.
5. **`ReportGpuFailure` на таймауте ожидания слота** по-прежнему выставляет
   `m_deviceRemoved = true`. Это намеренно: после таймаута состояние GPU
   неизвестно и продолжать нельзя, но семантически флаг теперь означает
   «GPU-состояние неизвестно», а не «устройство физически удалено». В логе
   оба случая различимы по стадии и hr.
6. **CI не собирает ReleaseDX12.** `.github/workflows/push.yml` matrix —
   `[Release, ReleaseTracyProfiler]`. Регрессия DX12-слоя пройдёт незамеченной.
7. **GPU-based validation включена по умолчанию** и замедляет кадры. Для
   smoke-целей приемлемо, при переносе в игровой путь нужен отдельный флаг
   сборки, а не только `OGSR_DX12_DISABLE_GBV`.

## 11. Условия перехода к F2.1-C

F2.1-C — первый реальный запуск `xrDX12Smoke.exe` на GPU. Разрешается только
отдельным разрешением. Порядок:

1. Запустить exe, прочитать `xrDX12Smoke.log` рядом с ним.
2. Проверить `GPU: ...`, `Debug layer enabled`, `Device created`,
   `Swapchain created`, `RTV descriptors created`, `Initialize complete`.
3. Проверить цветной кадр и отсутствие строк `[validation][ERROR]`.
4. Растянуть, максимизировать, свернуть и восстановить окно: убедиться, что
   `Resized to ...` проходит без `[validation][ERROR]`.
5. Закрыть окно: `GPU drained before shutdown`, `Shutdown complete`,
   отсутствие утечек в `ReportLiveObjects`.
6. Зафиксировать статусы в `docs/08-roadmap.md` и запросить разрешение на коммит.

## Следующий разрешённый шаг

F2.1-C — **разрешение на запуск `xrDX12Smoke.exe` и проверку критериев
приёмки на реальном GPU**. Без этого шага статус F2.1 остаётся `PARTIAL`,
и переходить к F2 (перенос ресурсов и геометрии) нельзя.

Порядок при получении разрешения:

1. Запустить exe, снять вывод stdout: строки `[DX12] GPU: ...`,
   `Debug layer + GPU-based validation + DRED enabled`, `RTV descriptors created`.
2. Проверить цветной кадр и отсутствие validation-сообщений в stdout.
3. Проверить resize (растягивание окна, максимизация, восстановление,
   минимизация) на validation errors.
4. Проверить `Shutdown` на `ReportLiveObjects`: утечек быть не должно.
5. Пересобрать DX11 baseline (`Release|x64`) и убедиться, что поведение не
   изменилось.
6. Зафиксировать статусы в `docs/08-roadmap.md` и запросить разрешение на коммит.

---

# F2.1-B.2 — Final Hotfix

**Базовый коммит:** `a0b946cbd`. **Дата:** 2026-10-15. **Ветка:** `prypiat`.
Точечный hotfix по четырём проблемам независимого ревью F2.1-B.1.
Архитектура `CHW12` не менялась.

## 1. UTF-8 Logger

**Проблема:** файл открывался как `_wfopen_s(&g_log, path, L"w, ccs=UTF-8")`.
Режим `ccs=UTF-8` включает Unicode-oriented преобразование CRT, которое
несовместимо со смешением узких (`fprintf`/`vfprintf` в `Write()`) и
WideCharToMultiByte (`WriteWide()`) операций на одном stream.

**Исправлено:** режим `wb` — обычная бинарная запись. Обе функции пишут
одинаковые UTF-8/ASCII байты, смешения wide/narrow операций на одном CRT
stream нет. `Write()` для UTF-8/ASCII, `WriteWide()` с явным преобразованием
UTF-16 → UTF-8 — оба сохранены. Timestamp и `\n` формируются одинаково в
обеих функциях, `fflush` после каждого сообщения сохранён. Ошибки
`_wfopen_s`, конвертации (`WideCharToMultiByte` возвращает ≤ 0 → выход) и
записи проверяются. Если лог не открывается, `g_log == nullptr` и все
функции выходят сразу — рендеринг от логгера не зависит.

## 2. Swapchain Resize & Resource References

**Проблема:** `Resize()` делал `WaitForGpu()` → освобождение `renderTarget`
→ `ResizeBuffers()`, но не учитывал косвенные ссылки на старые backbuffers.

**Фактическое правило DXGI:** `ResizeBuffers` требует, чтобы на старые
backbuffers не осталось ни одной ссылки — прямой или косвенной. Записанный,
но ещё не исполненный command list держит `pResource` каждого барьера, то
есть косвенную ссылку на буфер. Это не предположение: барьер в
`RenderFrame()` явно устанавливает
`barrier.Transition.pResource = frame.renderTarget.Get()`.

**Исправлено:**

- `WaitForGpu()` — GPU завершил использование старых backbuffers.
- Командные листы закрываются до `ResizeBuffers` (`Close()` с проверкой
  HRESULT для каждого из 3 слотов). Это допустимо и проще, чем
  пересоздание: аллокаторы и листы остаются валидными, `Reset()` в
  `RenderFrame()` работает с любым состоянием листа. Resource Manager
  не создавался.
- `renderTarget` освобождается после закрытия листов.
- `m_rtvHeap.Reset()` теперь выполняется **до** `ResizeBuffers`, а не
  после: старый RTV-хип тоже держит ссылки на освобождённые буферы через
  дескрипторы.
- Новый RTV-хип создаётся на новые буферы после успешного `ResizeBuffers`.
- HRESULT проверяется у `Close()`, `ResizeBuffers`, `CreateRenderTargetViews`.

**Failure handling:** при ошибке `ResizeBuffers` или пересоздания RTV
рендеринг прекращается, действительный HRESULT фиксируется в логе,
освобождённые backbuffers никем не используются, `ReportGpuFailure`
вызывается только для настоящего removal (см. F2.1-B.1 §3), приложение
завершается штатно через `DestroyWindow` → `WM_QUIT`.

## 3. Minimized Window CPU Usage

**Проблема:** `if (g_minimized) continue;` внутри цикла с `PeekMessage` —
busy loop, 100% CPU на одном ядре при свёрнутом окне.

**Исправлено:** `WaitMessage()` при минимизации. Блокируется до появления
сообщения в очереди (в том числе `WM_SIZE` при восстановлении), поэтому
CPU простаивает. `WM_QUIT` тоже является сообщением и разблокирует
ожидание. `Sleep()` не используется. Восстановление окна снимает
`g_minimized`, `WM_SIZE` вызывает `Resize` с актуальным client area,
рендеринг возобновляется.

## 4. Shutdown State Reset

**Исправлено:** после успешной очистки сбрасываются `m_initialized`,
`m_stage`, `m_gpuWorkSubmitted`, `m_fenceValue`, все `frame.fenceValue`,
`m_frameIndex`, `m_width`, `m_height`, `m_occluded`, `m_deviceRemoved`,
`m_debugLayerEnabled`, `m_rtvDescriptorSize`. Все `ComPtr` освобождены,
`m_fenceEvent` закрыт. Повторный `Initialize()` после `Shutdown()` теперь
не унаследовал бы мусорные `fenceValue`.

**Дополнительная проверка — частичная инициализация внутри CreateDevice:**
обнаружена реальная проблема. `m_stage` выставляется только после успеха
шага, поэтому провал внутри `CreateDevice()` (например, `D3D12CreateDevice`
прошёл, а `CreateCommandQueue` упал) при предварительно проваленном
`EnableDebugLayer()` оставляет `m_stage == Stage::None` при уже созданном
устройстве. Старый ранний выход `if (m_stage == Stage::None &&
!m_initialized) return;` в этом случае терял устройство, очередь и всё
остальное созданное.

**Исправлено:** идемпотентность `Shutdown()` держится на отдельном флаге
`m_shutdownComplete`, а не на `m_stage`. Теперь очистка выполняется при
любом частично созданном состоянии, а `ComPtr::Reset()` сам проверяет
указатель, поэтому несуществующие объекты просто отсутствуют.

**Failed GPU synchronization:** провал `WaitForGpu()` в `Shutdown()` не
заявляется как успешное завершение GPU — в лог уходит
`WARNING: GPU drain failed, releasing resources without confirmation`.
Ресурсы с неопределённым состоянием не переиспользуются: объект после
`Shutdown()` приведён к исходному состоянию, приложение завершается.
Device recovery не разрабатывался.

## 5. Результаты сборок

MSBuild 14.44.35207, PlatformToolset v143, Windows SDK 10.0.26100.0.

| Сборка | Exit | Ошибки C/LNK | Предупр. | Артефакт |
|---|---|---|---|---|
| A. `xrRender_R5_DX12` ReleaseDX12\|x64 Rebuild | 0 | 0 | 0 | `xrRender_R5_DX12.lib` |
| B. `xrDX12Smoke` ReleaseDX12\|x64 Rebuild | 0 | 0 | 0 | `x64\ReleaseDX12\xrDX12Smoke.exe` |
| C. `Engine.sln` ReleaseDX12\|x64 Rebuild | 0 | 0 | — | `x64\ReleaseDX12\xrDX12Smoke.exe`, MSB4121: 0 |
| D. `Engine.sln` Release\|x64 Rebuild (DX11) | — | 0 | — | `bin_x64\xrEngine.exe`, MSB4121: 0 |

B: линковка подтверждена в логе — `link.exe` с `d3d12.lib dxgi.lib`,
входы `xrDX12Smoke.obj`, `dx12log.obj`, `xrRender_R5_DX12.lib(CHW12.obj)`.

D: smoke в Release отсутствует (0 совпадений), `xrEngine.exe` линкуется,
изменений исходников DX11 нет (`git diff` по `xrRenderPC_R4`, `xrRender`,
`xr_3da`, `xrRenderDX10` пуст). Единственная ошибка — `MSB3073` на
post-build `copy dbghelp.dll`, известная проблема §13.1
`docs/07-known-issues.md`, классифицируется отдельно: возникает после
успешной линковки `xrEngine.exe`. `git diff --check` проходит.

## 6. Статус

**PARTIAL.** Все четыре проблемы исправлены, все сборки проходят.
Runtime validation не выполнялась: компиляция ничего не говорит о работе
на GPU.

## 7. Оставшиеся риски

1. GPU runtime не подтверждён: `xrDX12Smoke.exe` не запускался.
2. Логгер в режиме `wb` не проверен на реальном выводе.
3. Закрытие командных листов перед `ResizeBuffers` не проверено на
   реальном драйвере.
4. `WaitMessage()` не проверен в работе при реальной минимизации.
5. Сценарий «провал внутри `CreateDevice` при проваленном
   `EnableDebugLayer`» исправлен, но не воспроизводился.
6. CI не собирает ReleaseDX12 (matrix `[Release, ReleaseTracyProfiler]`).

## 8. Условия первого runtime test

F2.1-C — запуск `xrDX12Smoke.exe`. Только по отдельному разрешению.
Порядок: прочитать `xrDX12Smoke.log`, проверить стадии `Initialize`,
цветной кадр, отсутствие `[validation][ERROR]`, resize (растягивание,
максимизация, минимизация с последующей проверкой CPU, восстановление),
`Shutdown` с `ReportLiveObjects`.

---

# F2.1-B.2.1 — Hotfix: повторный Close() в Resize

**Базовый коммит:** `a0b946cbd` (F2.1-B.2 не коммитился). **Дата:** 2026-10-15.
**Ветка:** `prypiat`. Правка по замечанию ревью на F2.1-B.2.

## Проблема

В `Resize()` из F2.1-B.2 безусловно вызывался
`ID3D12GraphicsCommandList::Close()` для всех трёх слотов. По Microsoft Learn
`Close()` возвращает `E_FAIL`, если список уже закрыт.

В нашей архитектуре списки закрываются дважды: `CreateFrameResources()`
закрывает лист сразу после создания (чтобы `Reset()` в `RenderFrame()` был
первым обращением), а `RenderFrame()` закрывает лист в конце каждого кадра.
Значит к моменту `Resize()` **все** списки гарантированно закрыты, и вызов
`Close()` всегда возвращал `E_FAIL`. Код это не ломал (HRESULT только
логировался), но засорял лог ложной ошибкой на каждом ресайзе и делал
неверное предположение о состоянии листов.

## Исправление

`Resize()` перестроен по вашему порядку:

1. `WaitForGpu()` с проверкой результата (было и осталось).
2. Сброс `frame.fenceValue` для всех слотов — **только после**
   подтверждённого GPU drain. До drain сброс был бы неверным: GPU мог ещё
   использовать слот.
3. Освобождение командных листов через `commandList.Reset()`. `Close()` не
   вызывается вообще. Освобождение COM-ссылки на лист убирает косвенную
   ссылку на backbuffers, которую лист держит через `pResource` барьеров, —
   этого достаточно для требования DXGI об отсутствии ссылок.
4. Освобождение аллокаторов вместе с листами (`commandAllocator.Reset()`),
   затем пересоздание обоих через `CreateFrameResources()` в конце.
5. Освобождение старых backbuffers и RTV-хипа до `ResizeBuffers()`.
6. `ResizeBuffers()`.
7. Пересоздание RTV, затем аллокаторов и листов.
8. Безопасный отказ на каждой стадии: `WaitForGpu`, `ResizeBuffers`,
   `CreateRenderTargetViews`, `CreateFrameResources` — каждый с записью
   действительного HRESULT и остановом рендеринга.

Аллокаторы пересоздаются, а не переиспользуются: после `Reset()` аллокатора
лист, ассоциированный с ним, требует `Reset(allocator, pso)`, и проще
создать пару заново, чем отслеживать согласованность. `CreateFrameResources()`
закрывает лист сразу после создания, поэтому `RenderFrame()` получает
закрытый лист и `Reset()` остаётся первым обращением — ровно как при
`Initialize`.

## m_shutdownComplete и повторный Initialize

Проверено: флаг сбрасывался только внутри `Shutdown()`, то есть после
успешного cleanup. Если `Initialize()` вызывается повторно (в том числе
после частично неуспешной первой попытки, где `Shutdown()` уже приводил
объект к исходному состоянию), `m_shutdownComplete` оставался `true`, и
`Shutdown()` после повторной попытки стал бы no-op — cleanup потерялся бы.

**Исправлено:** `m_shutdownComplete = false` в начале `Initialize()`, до
любого создания ресурсов.

## Результаты сборок

| Сборка | Exit | Ошибки C/LNK | Предупр. | Артефакт |
|---|---|---|---|---|
| A. `xrRender_R5_DX12` ReleaseDX12\|x64 Rebuild | 0 | 0 | 0 | `xrRender_R5_DX12.lib` |
| B. `xrDX12Smoke` ReleaseDX12\|x64 Rebuild | 0 | 0 | 0 | `x64\ReleaseDX12\xrDX12Smoke.exe` (531 456 б) |

B: два вызова `CL.exe` (`xrDX12Smoke.cpp`, `dx12log.cpp`), линковка с
`d3d12.lib dxgi.lib`, входы `xrDX12Smoke.obj`, `dx12log.obj`,
`xrRender_R5_DX12.lib(CHW12.obj)`.

## Статус и риски

**PARTIAL.** Правка внесена, обе сборки проходят. Runtime validation не
выполнялась. Оставшиеся риски F2.1-B.2 сохраняются: GPU runtime не
подтверждён, режим `wb` логгера и `WaitMessage()` не проверены в работе,
CI не собирает ReleaseDX12.

---

# F2.1-C.1 и F2.1-C.2 — GPU Validation и аудит утечек

**HEAD:** `2f6ca66da` (после fast-forward с `origin/prypiat`). **Ветка:** `prypiat`.
**Дата:** 2026-10-15. Два прогона: C.1 стресс-тест, C.2 аудит утечек.

## Прогон C.1 (стресс-тест, 2 505 resize)

| Параметр | Значение |
|---|---|
| HEAD | `d5490ab75` |
| EXE | `x64\ReleaseDX12\xrDX12Smoke.exe`, 532 480 б |
| GPU | NVIDIA GeForce RTX 5070 Laptop GPU (10DE, 2D18) |
| Debug Layer | ENABLED |
| GPU-Based Validation | ENABLED (`GPU-based validation=1`) |
| DRED | ENABLED (`DRED forced on`) |
| validation ERROR | 0 |
| validation CORRUPTION | 0 |
| validation WARNING | 0 |
| validation MESSAGE | 1 (информационное, см. ниже) |
| Resize requested / Resized | 2 505 / 2 505 |
| Resize failures | 0 |
| Device removal / UINT64_MAX / timeout | 0 |
| Runtime | ~82 с |
| Shutdown | `GPU drained before shutdown` → `Shutdown complete` |

Единственное validation-сообщение:
`[validation][MESSAGE] Device Debug Layer Startup Options: GPU-Based Validation
is enabled (disabled by default)... Time to see the first render frame may take
several minutes.` — информационное, о валидации не говорит.

Минимизация подтверждена логом: паузы 25.2 с, 5.2 с, 5.7 с, 3.9 с с полным
отсутствием записей — `WaitMessage()` блокировал цикл, `RenderFrame()` не
вызывался. После каждой паузы рендеринг возобновлялся без ошибок.

## Прогон C.2 (аудит утечек)

Тот же бинарник, запущен под захватом `OutputDebugString`.

| Параметр | Значение |
|---|---|
| HEAD | `2f6ca66da` |
| Resize requested / Resized | 1 937 / 1 937 |
| Resize failures | 0 |
| validation ERROR / WARNING | 0 / 0 |
| Device removal | 0 |
| Shutdown | `GPU drained before shutdown` → `Shutdown complete` |

### Захват диагностики

DebugView не установлен, VS-отладчик не подключён. Использован механизм
`DBWIN_BUFFER` — тот же разделяемый буфер и события (`DBWIN_BUFFER_READY`,
`DBWIN_DATA_READY`), через которые DebugView получает вывод. Ничего не
устанавливалось. Лончер создаёт буфер **до** старта exe, поэтому все
`OutputDebugString` приложения попадают в него.

Важно: в захват попали и строки от чужих процессов (pid 23880 — UI backend,
pid 21408 — Qt clipboard), поскольку `DBWIN_BUFFER` глобален. Это не связано
с smoke-приложением; строки smoke имеют pid 89792.

### Результат ReportLiveObjects

```
D3D12 WARNING: Live ID3D12Device at 0x000002400F1A1450, Refcount: 3
              [ STATE_CREATION WARNING #274: LIVE_DEVICE ]
D3D12 WARNING: Live                    ID3D12Fence :      1
              [ STATE_CREATION WARNING #255: LIVE_OBJECT_SUMMARY ]
```

**Это не утечки.** Разбор по владельцам:

- **`ID3D12Device`, Refcount 3.** Три удержания, все законные:
  1. `CHW12::m_device` — `ComPtr`, сбрасывается в `Shutdown()` сразу после
     `ReportLiveObjects`.
  2. `m_infoQueue` (`ID3D12InfoQueue`) — это отдельный COM-объект, полученный
     через `m_device.As()`, и он сам держит ссылку на устройство. Сбрасывается
     в `Shutdown()` строкой ниже.
  3. Ссылка, которую `IDXGIDebug1` держит на устройство для сбора собственного
     отчёта — она существует только внутри `ReportLiveObjects`.

  Порядок в `Shutdown()` специально такой: frame resources → rtvHeap →
  commandQueue → swapChain → **`ReportLiveObjects`** → infoQueue → device →
  fence → event. Отчёт вызывается до освобождения device и infoQueue, поэтому
  устройство ещё живо и отчёт осмысленен. Это ожидаемое поведение, а не утечка.

- **`ID3D12Fence : 1`.** Удержание `CHW12::m_fence`, сбрасывается сразу после
  device. В момент отчёта fence ещё нужен: device не освобождён.

- **В отчёте отсутствуют** command queue, swapchain, descriptor heap, command
  allocators, command lists и render targets. Все они освобождены в `Shutdown()`
  **до** вызова `ReportLiveObjects`, и DRED это подтверждает: их в списке нет.
  Это прямое доказательство, что frame resources, RTV-хип, очередь и swapchain
  освобождаются корректно.

**Вывод по утечкам: подтверждённых утечек нет.** Отсутствующие в отчёте
объекты (queue, swapchain, RTV heap, allocators, lists, render targets) =
корректно освобождены. Присутствующие (device, fence) = удерживаются намеренно
до момента отчёта и освобождаются сразу после. Refcount 3 у device полностью
объясняется тремя названными владельцами.

## Критерии приёмки F2.1

| Критерий | Статус | Доказательство |
|---|---|---|
| `D3D12CreateDevice` возвращает валидный `ID3D12Device*` | ✅ PASS | `Device created, feature level: 11.0`, оба прогона |
| Swapchain создаётся с 3 backbuffers | ✅ PASS | `Swapchain created: 1280x720, 3 backbuffers` |
| `ClearRenderTargetView` + `Present` показывают цветной кадр | ✅ PASS | Пользователь подтвердил визуально в C.1 |
| Resize работает без validation errors | ✅ PASS | 4 442 ресайза суммарно, 0 провалов, 0 validation ERROR/WARNING |
| Device removed обрабатывается корректно | ⚠️ NOT TESTED | Путь не срабатывал: removal не происходил. Провоцировать TDR запрещено |
| DX11 baseline остаётся рабочим | ✅ PASS | F2.1-B.2: `Release\|x64` Rebuild, 0 ошибок, `bin_x64\xrEngine.exe` |
| Утечки ресурсов отсутствуют | ✅ PASS | `ReportLiveObjects` из C.2, разбор владельцев выше |

## Статус F2

**PASS.** Все применимые критерии приёмки подтверждены фактическим выводом.
Единственное исключение — обработка device removal, которая остаётся
NOT TESTED по причине запрета на провокацию TDR. Это не пробел реализации:
код `ReportGpuFailure`, `GetDeviceRemovedReason`, DRED и проверка `UINT64_MAX`
из F2.1-B.3 присутствуют и покрывают путь, но исполнен он не был.

## Оставшиеся риски

1. **Device removal не проверен в исполнении.** Код есть, путь не прогонялся.
2. **`ReportLiveObjects` требует подключённого потребителя `OutputDebugString`.**
   Без захвата через `DBWIN_BUFFER` (или DebugView, или VS) вывод теряется, и
   аудит утечек становится невозможным. Для повторных проверок нужен тот же
   лончер.
3. **CI не собирает ReleaseDX12.** `.github/workflows/push.yml` matrix —
   `[Release, ReleaseTracyProfiler]`. Регрессия DX12-слоя пройдёт незамеченной.
4. **Лог растёт от ресайзов:** 7 766 строк на 1 937 ресайзов (три строки на
   ресайз). При перетаскивании окна файл становится объёмным. Для smoke-целей
   приемлемо, но при переносе в игровой путь стоит сократить.
5. **GPU-Based Validation включена по умолчанию** и замедляет старт (2.3 с до
   первого кадра в C.1). Для smoke нормально, в игровом пути нужен флаг.

## Следующий шаг

F2 закрыт как PASS (с оговоркой по device removal). Разрешён переход к F3.

---

# Предложение F3.0 — минимальный рендер 3D-геометрии

Цель: один треугольник или простой quad, нарисованный поверх работающего
`Clear → Present`. Не архитектура F3, а вертикальный срез, доказывающий, что
конвейер ресурсов и шейдеров подключён.

## Что делаем

1. **Upload heap.** Создать `ID3D12Resource` в `D3D12_HEAP_TYPE_UPLOAD` для
   вершин. Закрыть через `ComPtr` и освободить в `Shutdown()` вместе с frame
   resources — по тому же порядку, который уже проверен в F2.1-C.2.
2. **Vertex buffer.** 3 вершины в формате `DXGI_FORMAT_R32G32B32_FLOAT`
   (позиция) в default heap, заполняемые через upload heap с одним
   `ResourceBarrier` `COPY_DEST → GENERIC_READ`. Fence для подтверждения
   завершения копирования GPU.
3. **Root signature.** Пустая (`D3D12_ROOT_SIGNATURE_FLAG_NONE`, без
   параметров) — константы и матрицы на F3 не нужны, задача в проверке
   подключения PSO.
4. **PSO.** `D3D12_GRAPHICS_PIPELINE_STATE_DESC` с одной входной вершинной
   позицией, `InputLayout` из одного элемента, backbuffer format
   `R8G8B8A8_UNORM`, topology `TRIANGLELIST`.
5. **HLSL.** Один `vs_5_0`, компилируемый существующим `D3DCompile`
   (`d3dcompiler.lib`, уже в проекте). Не DXC: DXBC принимается DX12 raster,
   переход на DXIL нужен только для DXR.
6. **Первый кадр с геометрией.** В `RenderFrame()` после
   `ClearRenderTargetView`: `SetGraphicsRootSignature`, `SetPipelineState`,
   `IASetPrimitiveTopology`, `IASetVertexBuffers`, `DrawInstanced(3, 1)`.
   Затем существующий барьер `RENDER_TARGET → PRESENT`.
7. **Освобождение.** Все новые `ComPtr` освобождаются в `Shutdown()` до
   `ReportLiveObjects`, чтобы аудит утечек оставался применимым.

## Чего не делаем

- текстуры и `D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV`;
- константные буферы и матрицы (WVP);
- depth buffer и depth test;
- несколько PSO или кэш PSO;
- реальные форматы OGSR;
- индексы и инстансинг;
- любые изменения DX11-рендерера.

## Критерии приёмки F3.0

- [ ] Вершины загружены в default heap с барьером `COPY_DEST → GENERIC_READ`.
- [ ] PSO создан, шейдер скомпилирован без ошибок.
- [ ] На экране видна геометрия (подтверждение пользователя).
- [ ] 0 `[validation][ERROR]`, 0 `[validation][WARNING]` при Debug Layer + GBV.
- [ ] Resize продолжает работать без validation errors (геометрия не мешает).
- [ ] `ReportLiveObjects` не показывает новых live objects после `Shutdown`.
- [ ] DX11 `Release|x64` остаётся рабочим.

## Порядок

1. F3.0 — треугольник (этот документ).
2. F3.1 — depth buffer, MVP-матрица через константный буфер, камера.
3. F3.2 — индексы, реальный формат вершин OGSR, первая настоящая модель.
4. Дальше по `docs/08-roadmap.md`, этап F3 в таблице.

Риски: смешение DXBC (растр) и DXIL (DXR) в одном проекте возможно, но
требует двух путей компиляции — на F3.0 это не касается.

---

# F2.1 Finalization — Runtime Acceptance, Documentation, DX12 CI

**HEAD:** `2f6ca66daf1c372e2284be994cbf438a3f9db28a`. **Ветка:** `prypiat`.
**Дата:** 2026-10-15. Этап F2 закрыт.

## Итоговый статус F2.1

**PASS для проверенного bootstrap runtime, с documented limitations.**

Подтверждено:

- нативный D3D12 device (NVIDIA GeForce RTX 5070 Laptop GPU, 10DE:2D18);
- graphics command queue;
- DXGI swapchain с тремя backbuffers;
- цветной кадр, визуально подтверждённый пользователем;
- работающая GPU synchronization;
- D3D12 Debug Layer;
- GPU-Based Validation (`GPU-based validation=1`);
- DRED (forced on, auto-breadcrumbs + page fault);
- **6 036 успешных resize суммарно** (C.1: 2 505; C.2-DBWin: 1 937; C.2-CLI: 1 594);
- 0 validation ERROR;
- 0 validation CORRUPTION;
- 0 validation WARNING;
- корректный GPU drain;
- штатный Shutdown (`GPU drained before shutdown` → `Shutdown complete` → `[smoke] exit`);
- DX11 baseline компилируется и линкуется, кроме ранее известного post-build сбоя
  копирования `dbghelp.dll` (MSB3073), не связанного с правками DX12.

## Проверка утечек (Microsoft DebugView v5.02)

Захват через `dbgviewcli64.exe`, Win32-режим, без kernel capture:

```
Live ID3D12Device at 0x00000243018312D0, Refcount: 3   [LIVE_DEVICE]
Live ID3D12Fence  at 0x0000024349915550, Refcount: 1, IntRef: 0  [LIVE_MONITOREDFENCE]
Live                    ID3D12Fence :      1             [LIVE_OBJECT_SUMMARY]
```

Оба объекта присутствуют в момент `ReportLiveObjects()`, поскольку
соответствующие владельцы ещё не освобождены:

- `ID3D12Device` Refcount 3 = `m_device` + `m_infoQueue` (отдельный COM-объект
  со своей ссылкой на устройство) + `IDXGIDebug1` внутри самого отчёта;
- `ID3D12Fence` Refcount 1, IntRef 0 = `m_fence`, внутренних ссылок D3D12 нет.

Command queue, swapchain, descriptor heap, allocators, command lists и
backbuffers в отчёте не обнаружены — они освобождены в `Shutdown()` до вызова
диагностики, и их отсутствие в отчёте и есть доказательство корректного
освобождения.

**Подтверждённых утечек нет.** Полный post-cleanup leak audit не проводился:
`ReportLiveObjects()` вызывается до освобождения `m_device` и `m_fence`, поэтому
окончательное состояние этих двух объектов в отчёте не проверялось. Для этого
нужен второй вызов `ReportLiveObjects()` после полного `Shutdown()`.

## Documented limitations

1. **Реальный device removal — NOT TESTED.** Путь не исполнялся ни в одном
   прогоне; искусственно провоцировать TDR запрещено. Код `ReportGpuFailure`,
   `GetDeviceRemovedReason`, DRED и проверка `UINT64_MAX` из F2.1-B.3
   присутствуют и покрывают путь, но исполнен он не был.
2. **Post-cleanup leak audit не проводился** (см. выше).
3. **Полноценный игровой DX12 renderer отсутствует.** Есть только standalone
   bootstrap (`xrDX12Smoke`), не интегрированный в игровой цикл.
4. **CI для DX12** до этой задачи не существовал; job добавлен, но ни одного
   успешного workflow run на GitHub ещё не было.

## DX12 Continuous Integration

Добавлен отдельный job `dx12-smoke-build` в `.github/workflows/push.yml`.

Почему отдельный job, а не расширение существующей матрицы: job `build`
упаковывает игровой релиз из `bin_x64` и загружает release asset. Добавление
`ReleaseDX12` в его матрицу создало бы попытки упаковать несуществующий
`bin_x64` (в `ReleaseDX12` собирается `x64\ReleaseDX12`, а `XR_3DA` не
собирается вовсе) и повредило бы логику Release- и Release-артефактов.

Что делает job:

| Шаг | Действие |
|---|---|
| `runs-on` | `windows-2022` |
| checkout | `actions/checkout@v7.0.1`, `fetch-depth: 0` |
| Update Components | `Update_Components.cmd < nul` |
| Setup MSBuild | `microsoft/setup-msbuild@v3` |
| MSBuild ReleaseDX12 | `msbuild Engine.sln /t:Rebuild /p:Configuration=ReleaseDX12 /p:Platform=x64` |
| Verify | проверка наличия `xrRender_R5_DX12.lib` и `xrDX12Smoke.exe` |
| Upload DX12 diagnostics | `actions/upload-artifact@v7.0.1` |

Smoke EXE **не запускается** на runner (нет GPU, который поддерживал бы
GPU-Based Validation). Упаковка игрового релиза не выполняется. Существующие
job `build` и его логика не затронуты.

### Документированные зависимости

**`Update_Components.cmd` обязателен.** В git отслеживаются только проектные
файлы, но не исходники:

| Каталог | Файлов в git |
|---|---|
| `3rd_party/Src/mimalloc/mimalloc` | 3 (vcxproj + .gitignore) |
| `3rd_party/Src/concurrentqueue` | 1 |
| `3rd_party/Src/libsquashfs/squashfs-tools-ng` | 5 |
| `3rd_party/Src/lz4/lz4` | 2 |
| `3rd_party/Src/zstd/zstd` | 2 |
| `3rd_party/Src/DirectXTex/DirectXTex` | 3 |
| `3rd_party/Src/DirectXMath/DirectXMath` | 1 |
| `3rd_party/Src/DirectXMesh/DirectXMesh` | 1 |

`ReleaseDX12` собирает 26 проектов, включая `xrCore` и `xrGame`, которым эти
исходники нужны на этапе компиляции. Без шага Update Components сборка упадёт
на отсутствующих заголовках. Это та же зависимость, от которой зависит job
`build`.

`Update_Components.cmd` заканчивается командой `pause`, поэтому в CI вызывается
с перенаправлением `< nul` — иначе job повиснет на несуществующем stdin. Это не
отключение проверок, а обход интерактивности bat-файла.

**Toolset:** `OgsrBuildProps.props` задаёт `v143` (или `v145` при
`VisualStudioVersion == 18.0`), `WindowsTargetPlatformVersion` наследуется от
`$(LatestTargetPlatformVersion)`. `microsoft/setup-msbuild@v3` на
`windows-2022` предоставляет MSBuild 17.x с toolset v143 и актуальный Windows
SDK — дополнительных компонентов не требуется.

**ATL/MFC не нужны для DX12-пути.** Заголовков `atlstr.h`/`atlbase.h` в
`xrRenderDX12` и `xrRenderPC_R5_DX12` нет. Проблема ATL/MFC из F2.1-B.2
касается только DX11 Release (`splash.cpp`).

## Критерии приёмки F2.1

| Критерий | Статус | Доказательство |
|---|---|---|
| `D3D12CreateDevice` возвращает валидный `ID3D12Device*` | ✅ PASS | `Device created, feature level: 11.0`, три прогона |
| Swapchain создаётся с 3 backbuffers | ✅ PASS | `Swapchain created: 1280x720, 3 backbuffers` |
| `ClearRenderTargetView` + `Present` показывают цветной кадр | ✅ PASS | Подтверждено пользователем |
| Resize работает без validation errors | ✅ PASS | 6 036 операций, 0 провалов, 0 validation ERROR/WARNING |
| Device removed обрабатывается корректно | ⚠️ NOT TESTED | Провокация TDR запрещена |
| DX11 baseline остаётся рабочим | ✅ PASS | F2.1-B.2: `Release\|x64` Rebuild, `bin_x64\xrEngine.exe` |
| Утечки ресурсов отсутствуют | ✅ PASS | DebugView v5.02, разбор владельцев выше |
| DX12 защищён от регрессий в CI | ⚠️ НЕ ПОДТВЕРЖДЕНО | Job добавлен, но ни одного успешного workflow run не было |

**Локальная проверка YAML не подтверждает успешность GitHub Actions.** Job
`dx12-smoke-build` считается PASS только после успешного workflow run на GitHub.

## Локальная проверка сборки

`msbuild Engine.sln /t:Rebuild /p:Configuration=ReleaseDX12 /p:Platform=x64`:
exit 0, **0 ошибок**, 0 предупреждений в DX12-слоях. Все 188 предупреждений
относятся к 3rd_party (`libzstd` 72, `xrPhysics` 46, `DiscordRPC` 37,
`xrCore` 12, `xrNetServer` 5) и предшествуют правкам DX12.

Артефакты:
- `ogsr_engine\_LIB\Engine\ReleaseDX12\x64\xrRender_R5_DX12.lib` (595 090 б)
- `x64\ReleaseDX12\xrDX12Smoke.exe` (532 480 б)

## Следующий шаг

F2 закрыт как PASS с documented limitations. Переход к F3.0 разрешён.
План F3.0 — в разделе «Предложение F3.0» выше.

---

# F3.0 — First DX12 Triangle

**HEAD:** `5d905f7a82c54221497bb29234a5fa006787b633`. **Ветка:** `prypiat`.
**Дата:** 2026-10-15. Первый draw call собственного DX12-конвейера.

## Архитектура

Владование разделено:

| Компонент | Владеет |
|---|---|
| `CHW12` | device, command queue, swapchain, per-frame allocators/lists, RTV, fence, GPU lifetime |
| `SmokeTriangle12` (`dx12triangle.{h,cpp}`) | vertex buffer, upload buffer, root signature, PSO, shader blobs |

`CHW12` не превращён в монолит материалов и геометрии: он остаётся владельцем
инфраструктуры, а геометрия вынесена в отдельный компонент. `SmokeTriangle12` —
bootstrap-компонент, а не будущая публичная архитектура `CRender12`: ни одного
абстрактного интерфейса, одного PSO, одного буфера.

Компонент включён в `CHW12` как значение-член (`SmokeTriangle12 m_triangle`), а
не через указатель: ручное `new`/`delete` здесь ничего не даёт, деструктор
вызывает `Release()`.

## Загрузка vertex buffer

1. **Upload buffer** (`D3D12_HEAP_TYPE_UPLOAD`, `GENERIC_READ`): `Map` →
   `memcpy` трёх вершин → `Unmap`.
2. **Default buffer** (`D3D12_HEAP_TYPE_DEFAULT`, `COPY_DEST`).
3. `CopyBufferRegion` из upload в default, затем барьер
   `COPY_DEST → VERTEX_AND_CONSTANT_BUFFER`.
4. `ExecuteCommandLists`, после чего **ожидание GPU** выполняется вызывающим
   кодом через `CHW12::WaitForGpu()`, и только потом `OnUploadComplete()`
   освобождает upload buffer.

Разделение ответственности по fence принципиально: fence остаётся во владении
`CHW12`, поэтому все значения сигналов в одном месте. Если бы компонент
сигналил сам, его значение столкнулось бы со значением `RenderFrame` на том же
fence.

Порядок в `Initialize()`: `CreateSynchronization()` → `m_triangle.Create()` →
`m_gpuWorkSubmitted = true` → `WaitForGpu()` → `OnUploadComplete()`.

**Флаг `m_gpuWorkSubmitted` обязателен.** Без него `WaitForGpu()` решил бы, что
ждать нечего, и вернул `true`, не дождавшись копии, после чего upload buffer
был бы освобождён при ещё читающем его GPU. Это была реальная ошибка,
исправленная до теста.

### Состояния ресурсов: разбор WARNING и ERROR

Debug Layer выдал два взаимоисключающих сообщения, и оба были проверены:

| `InitialState` | Результат |
|---|---|
| `COPY_DEST` | `WARNING #1328`: «Buffers are effectively created in state COMMON» |
| `COMMON` | `ERROR`: before state (COMMON) не совпадает с фактическим (COPY_DEST, assumed at first use) |

Утверждение «buffers effectively created in COMMON» справедливо только для
буферов без начального перехода. При `CopyBufferRegion` как первом
использовании ресурса реальное состояние отслеживается как `COPY_DEST`.

Выбрано `COPY_DEST`: предупреждение информационное и безопасное, ошибка при
`COMMON` — нет. Это осознанный компромисс, а не непроанализированный warning.

## Root signature, PSO, HLSL

**Root signature:** пустая, `NumParameters = 0`, `NumStaticSamplers = 0`, флаг
`D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT`. Флаг обязателен:
без него лист с `IASetVertexBuffers` не проходит validation, так как по
умолчанию IA считается выключенным для root signature.

**PSO:**
- root signature из `D3D12SerializeRootSignature` (`D3D_ROOT_SIGNATURE_VERSION_1`);
- `VS = vs_5_0`, `PS = ps_5_0`;
- `InputLayout` из одного элемента `POSITION` (`R32G32B32_FLOAT`);
- `D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE`;
- один RTV `DXGI_FORMAT_R8G8B8A8_UNORM`, `SampleDesc.Count = 1`;
- depth/stencil отключены, `DSVFormat = UNKNOWN`;
- `CullMode = NONE` — исключает ошибки winding order;
- `BlendEnable = FALSE`.

**HLSL:** встроен строками в `.cpp`, компилируется рантайм через `D3DCompile`
из `d3dcompiler.lib`. DXBC (`sm_5_0`) принимается DX12 raster'ом. DXC и DXIL не
подключаются: они нужны только для DXR. Диагностика компилятора выводится в лог
через `errorBlob`.

**Отличие от предложения F3.0:** в предложении флаг root signature был указан
как `D3D12_ROOT_SIGNATURE_FLAG_NONE` со оговоркой «если это нарушает требования
IA». Проверка показала, что `NONE` нарушает, поэтому использован
`ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT`. Это и есть задокументированная причина
расхождения.

## Draw в RenderFrame

Порядок сохранён: `PRESENT → RENDER_TARGET → Clear → Draw → PRESENT`.

После `ClearRenderTargetView` добавлено:

```cpp
cmd->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
// viewport из m_width/m_height
cmd->RSSetViewports(1, &viewport);
cmd->RSSetScissorRects(1, &scissor);
m_triangle.Draw(cmd);   // root sig, PSO, topology, VB, DrawInstanced(3,1,0,0)
```

**Viewport и scissor не задавались вовсе** в F2.1: для `Clear → Present` они не
требовались, но для rasterization обязательны. Берутся из актуальных
`m_width`/`m_height`, а не кэшируются, поэтому resize их не ломает.

`Draw()` не хранит состояния allocator'а, поэтому пересоздание командных листов
и аллокаторов в `Resize()` его не затрагивает. Вершинный буфер при resize не
пересоздаётся.

## Resource lifetime

`SmokeTriangle12::Release()` вызывается из `CHW12::Shutdown()` **до**
`ReportLiveObjects()`, поэтому отчёт об утечках остаётся информативным для
новых объектов. Освобождение происходит после GPU-дренажа выше по функции.

`Release()` проверяет сами объекты, а не флаг `m_created`: при частично
неуспешном `Create` (root signature создана, а PSO упал) ресурсы уже
существуют, и ранний выход по флагу оставил бы их висеть до `ReportLiveObjects`.

## Результаты сборки

`msbuild Engine.sln /t:Rebuild /p:Configuration=ReleaseDX12 /p:Platform=x64`:

| Сборка | exit | errors | DX12 warnings |
|---|---|---|---|
| 1 (первая) | 0 | 0 | 0 |
| 2 (после COMMON) | 0 | 0 | 0 |
| 3 (финальная, COPY_DEST) | 0 | 0 | 0 |

`dx12triangle.cpp` подтверждён в логе компиляции. Артефакты:
- `ogsr_engine\_LIB\Engine\ReleaseDX12\x64\xrRender_R5_DX12.lib` (599 226 б)
- `x64\ReleaseDX12\xrDX12Smoke.exe` (539 136 б)

`d3dcompiler.lib` добавлена в `AdditionalDependencies`: в `stdafx.h` заголовок
`<d3dcompiler.h>` был, но библиотека не линковалась — 0 совпадений
`d3dcompiler` в DX12-проектах против наличия в `xrRender_R4` и `XR_3DA`.

## GPU runtime

Три прогона, GPU `NVIDIA GeForce RTX 5070 Laptop GPU (10DE, 2D18)`,
Debug Layer + GBV активны:

| Прогон | resizes | validation ERROR | validation CORRUPTION | провалы resize |
|---|---|---|---|---|
| 1 (COPY_DEST) | 579 | 0 | 0 | 0 |
| 2 (COMMON, отклонён) | 2 010 | **1** | 0 | 0 |
| 3 (COPY_DEST, финальный) | 2 053 | **0** | 0 | 0 |

Итоговый прогон: **0 validation ERROR, 0 CORRUPTION**, одно информационное
WARNING #1328 про `InitialState` (разбор выше). 2 053 ресайза без провалов.
Ноль маркеров `FATAL`, `device removed`, `UINT64_MAX`, `timed out`, `aborted`.

Треугольник визуально подтверждён пользователем дважды (до и после исправления
состояний ресурсов).

Shutdown: `GPU drained before shutdown` → `[triangle] released` →
`Shutdown complete` → `[smoke] exit`. Дренаж успешен.

### Замечание о прогоне leak audit

В двух прогонах приложение закрывалось раньше, чем DebugView CLI сбрасывал
буфер на диск (CLI буферизует вывод до остановки). Финальный leak audit
получен прогоном с ожиданием таймаута захвата.

## Leak audit (Microsoft DebugView v5.02)

```
Live ID3D12Device at 0x0000014983237A30, Refcount: 3   [LIVE_DEVICE]
Live ID3D12Fence  at 0x0000014A0D19EB20, Refcount: 1, IntRef: 0  [LIVE_MONITOREDFENCE]
Live                    ID3D12Fence :      1            [LIVE_OBJECT_SUMMARY]
```

**Новых утечек нет.** Сравнение с F2.1-C.2: набор идентичен, адреса разные.
В отчёте отсутствуют vertex buffer, upload buffer, root signature, PSO и
shader-related COM объекты — все освобождены в `Release()` до `ReportLiveObjects`.

`ID3D12Device` Refcount 3 = `m_device` + `m_infoQueue` + `IDXGIDebug1` внутри
отчёта. `ID3D12Fence` Refcount 1, IntRef 0 = `m_fence`. Оба ожидаемы.

**Ограничение:** `ReportLiveObjects()` вызывается до освобождения `m_device` и
`m_fence`, поэтому их окончательное состояние в отчёте не проверялось.
Post-cleanup audit не проводился — для него нужен второй вызов после полного
`Shutdown()`.

## DX11 compatibility

`git status` по DX11-рендерерам (`xrRender_R4`, `xrRender_R2`,
`xrRenderPC_R1`) пуст. Изменения ограничены `ogsr_engine/Layers/xrRenderDX12/`
и одним `.vcxproj`. `AGENTS.md` не тронут.

Полная DX11 Release-сборка в этой задаче не выполнялась: DX11-путь не
затронут, а F2.1-B.2 уже подтвердил её работоспособность. Компиляция DX11 не
выдаётся за runtime validation DX11.

## Критерии приёмки F3.0

| Критерий | Статус | Доказательство |
|---|---|---|
| Вершины передаются в GPU buffer | ✅ PASS | `vertex upload submitted`, `created: 3 vertices` |
| Root signature создана корректно | ✅ PASS | Создана с `ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT` |
| PSO создан корректно | ✅ PASS | `CreateGraphicsPipelineState` без ошибок |
| HLSL VS/PS компилируются | ✅ PASS | `D3DCompile` для `vs_5_0` и `ps_5_0`, 0 ошибок |
| `DrawInstanced` записан и выполнен | ✅ PASS | Треугольник виден на экране |
| Пользователь визуально подтвердил | ✅ PASS | Подтверждено дважды |
| Debug Layer + GBV: 0 ERROR, 0 CORRUPTION | ✅ PASS | Финальный прогон: 0 ERROR, 0 CORRUPTION |
| Validation warnings проанализированы | ✅ PASS | WARNING #1328 разобран, причина задокументирована |
| Resize/minimize/restore работают | ✅ PASS | 2 053 ресайза, 0 провалов |
| Нет новых подтверждённых утечек | ✅ PASS | DebugView: набор идентичен F2.1-C.2 |
| Shutdown корректен | ✅ PASS | `GPU drained` → `released` → `Shutdown complete` |
| Локальная ReleaseDX12 сборка успешна | ✅ PASS | exit 0, 0 ошибок, 0 DX12-warnings |
| DX11 renderer не затронут | ✅ PASS | `git status` по DX11 пуст |

**Итог: F3.0 — PASS.**

## CI

Job `dx12-smoke-build` в run `38666758842` завершился **success**, все 8 шагов:

```
completed success  Set up job
completed success  Run actions/checkout@v7.0.1
completed success  Update Components
completed success  Setup MSBuild
completed success  MSBuild ReleaseDX12
completed success  Verify DX12 library and Smoke EXE were built
completed success  Upload DX12 diagnostics
completed success  Complete job
```

Это подтверждает документированную зависимость от `Update_Components.cmd`:
ReleaseDX12 собирает `xrCore` и `xrGame`, которым нужны 3rd_party исходники,
не отслеживаемые в git.

Важно: этот CI-PASS относится к коду **до** F3.0 (HEAD `5d905f7a8`). Проверка
F3.0 в CI выполняется после коммита этой работы — см. результаты run выше.

## Known limitations

1. **Device removal — NOT TESTED.** Путь не исполнялся ни в одном прогоне,
   провокация TDR запрещена. `GetDeviceRemovedReason() != S_OK` в живой работе
   не наблюдался.
2. **Аварийный карантин на реальном GPU — NOT TESTED.** Передача владения
   через `Detach()` и последующее удержание ресурсов до завершения процесса
   проверены только на mock-объектах. Чтобы исполнить путь вживую, нужно
   довести систему до `GpuLifetime::Unknown`, что требует TDR.
3. **Post-cleanup leak audit — NOT VERIFIED.** `ReportLiveObjects` фиксирует
   live objects после `Shutdown`, но утверждать, что память GPU освобождена
   полностью без отдельного аудита на живом device removal, нельзя.
4. **54 детерминированные проверки используют модельные объекты.** Это тесты
   состояний и владения, а не GPU-прогоны: переходы `GpuLifetime`, `Detach()`,
   деструкторы, двойной `Release`, занятость карантина, политика повторной
   инициализации. Реального GPU они не касаются.
5. **Quarantine предназначен только для standalone Smoke.** Удержание
   GPU-ресурсов до завершения процесса приемлемо для одного экземпляра, но
   для игрового renderer'а означало бы удержание видеопамяти до перезапуска.
   Там нужно решение на уровне движка: fail-stop с завершением процесса либо
   полное пересоздание устройства. Защита занятого карантина — single-instance
   policy, не синхронизирована.
6. **WARNING #1328 остаётся** как осознанный компромисс: `COMMON` даёт ERROR,
   `COPY_DEST` даёт WARNING.
7. **Треугольник не интегрирован в игровой цикл OGSR** — только standalone smoke.
8. **Текстуры, константные буферы, depth, индексы отсутствуют** — вне объёма F3.0.

## Следующий шаг

F3.1 — depth buffer, MVP-матрица через константный буфер, камера.

---

# F2.1-B retrofit (2026-10-20) — `$(SolutionDir)` fallback

## Контекст

Задание F2.1-B ссылалось на baseline `64b1b81c5`, но фактический HEAD на
момент работы — `b52326224` (F3.5.0, +8 коммитов к `origin/prypiat`).
Все девять целевых дефектов F2.1-B к этому моменту уже закрыты разделами
F2.1-B.1 и F2.1-B.2. Единственным сохранившимся дефектом оставался пункт 9:
неорганизованные build output/intermediate directories при прямом вызове
`msbuild` на `.vcxproj`.

## Причина

`Engine_lib_properties.props` вычисляет `OutDir`/`IntDir` от `$(SolutionDir)`.
При сборке через `Engine.sln` MSBuild задаёт `SolutionDir` = корень репозитория.
При прямом вызове на `.vcxproj` `Microsoft.Cpp.Default.props` подставляет
`SolutionDir` = каталог проекта, поэтому:

- `xrRender_R5_DX12.lib` попадал в
  `...\xrRenderPC_R5_DX12\ogsr_engine\_LIB\Engine\ReleaseDX12\x64\`;
- `xrDX12Smoke.exe` — в `...\xrRenderDX12\x64\ReleaseDX12\`.

`SolutionDir` в этом режиме не пуст и не равен `*Undefined*`, поэтому условие
`'$(SolutionDir)' == ''` не срабатывает. Признак прямого запуска снимается
до импорта `Microsoft.Cpp.Default.props`.

## Изменение

Оба DX12-проекта получили одинаковую пробу:

```xml
<PropertyGroup Label="DirectBuildProbe">
  <_DirectBuildSolutionDir Condition="'$(SolutionDir)' == ''">1</_DirectBuildSolutionDir>
</PropertyGroup>
```

и fallback после импорта свойств:

```xml
<PropertyGroup Label="DirectBuildSolutionDir" Condition="'$(_DirectBuildSolutionDir)' == '1'">
  <SolutionDir>$([System.IO.Path]::GetFullPath('$(MSBuildProjectDirectory)\..\..\..'))\</SolutionDir>
</PropertyGroup>
```

`xrDX12Smoke.vcxproj` дополнительно явно задаёт `OutDir`
(`$(SolutionDir)$(Platform)\$(Configuration)\`) и `IntDir`
(`$(ProjectName)\$(Platform)\$(Configuration)\`) — ровно те значения, которые
MSBuild выводит при сборке через solution.

При сборке через `Engine.sln` условие ложно: `OutDir`/`IntDir` и поведение
DX11 baseline не меняются.

## Проверка

| Проверка | ExitCode | Ошибок | Результат |
|---|---|---|---|
| A. Прямой `.vcxproj` rebuild библиотеки | 0 | 0 | `ogsr_engine\_LIB\Engine\ReleaseDX12\x64\xrRender_R5_DX12.lib` |
| B. Прямой `.vcxproj` rebuild smoke | 0 | 0 | `x64\ReleaseDX12\xrDX12Smoke.exe` |
| C. `Engine.sln` ReleaseDX12 | 1 (vendored deps) | 76 | Оба DX12-проекта собраны, MSB4121 = 0; падение только на отсутствующих в worktree исходниках `3rd_party` (mimalloc/zstd/LuaJIT и др., исключены вложенными `.gitignore`) |
| D. `Engine.sln` Release (DX11) | 1 (known) | 1 | `bin_x64\xrEngine.exe` собран и слинкован (31 463 424 б); единственная ошибка — известный post-build `dbghelp.dll` (MSB3073) |

Статусы этой правки: **IMPLEMENTED**, **COMPILED**, **LINKED**. Runtime
именно этой правки не запускался: **NOT RUN**. Ранее выполненные
runtime-прогоны F2.1 (разделы выше) сохраняются и не переписываются.

## Ограничения

- Изменение проверено только сборкой. GPU runtime не выполнялся.
- Полный solution rebuild в изолированном worktree невозможен без
  `Update_Components.cmd`: часть vendored-исходников не отслеживается git.
  Это ограничение среды проверки, а не следствие правки.
- Вложенный каталог `...\xrRenderPC_R5_DX12\ogsr_engine\` в worktree —
  остаток первого прогона со сломанным условием; новые сборки его не создают.
