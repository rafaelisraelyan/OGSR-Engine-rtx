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
