# F2.1 — First Frame (DX12 bootstrap)

## HEAD и ветка

| Параметр | Значение |
|---|---|
| Ветка | `prypiat` |
| HEAD | `64b1b81c5` — F2.0.1: repair DX12 solution configuration |
| Дерево | `D:\Coding Project\OGSR ENGINE fork from rtx` |
| Подэтап | **F2.1-B — исправления и подготовка bootstrap к запуску** |
| Дата | 2026-10-15 |

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
- [ ] DX11 baseline остаётся рабочим — не проверялось

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
