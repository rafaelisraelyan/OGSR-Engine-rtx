# F3.5.3B — Parallel Command Recording

Дата: 2026-10-19. Ветка: prypiat.

## Финализация F3.5.3A

После ревью lifetime создан единственный локальный коммит
`b523262242ac857e8d6c866a72aafb325c54e910`:
`feat(dx12): add sequential multi-list recording foundation`.
Новые allocator/list освобождаются после GPU drain; при Unknown все пары
передаются через Detach в process-lifetime quarantine. Terminal/occupied
quarantine запрещает Initialize. Штатный Shutdown сбрасывает fence tracking,
повторный Initialize создаёт новые пары. Блокеров не обнаружено.
Пользовательские AGENTS.md и три посторонних документа исключены из коммита.
Push не выполнялся. Все изменения F3.5.3B остаются незакоммиченными.

## Реализация F3.5.3B

`-commandlists legacy|multi|parallel`; default остаётся legacy.
Parallel использует тот же JobSystem, что CPU Prepare: один Initialize на
приложение. Даже восстановление CHW12 после отказа не пересоздаёт пул.

Owner сначала ждёт прежний fence слота. Он записывает Begin, барьер и clear,
заполняет CB. Две задачи получают эксклюзивные Draw allocator/list этого слота
и выполняют Reset → собственные bindings → Draw → Close. Owner выполняет
JobGroup::Wait, проверяет оба результата, записывает End/readback/barrier и
подаёт один ExecuteCommandLists в порядке Begin, Draw 0, Draw 1, End.
Present → Signal и прежние ожидания GPU не изменены.

Задачи получают копии viewport/scissor/descriptor handles, диапазона вершин и
CB GPU address. Они читают const mesh и пишут только в собственную пару и свой
элемент результата. Draw сделан const, возвращает bool и не пишет в общий лог;
проверка диапазона не допускает unsigned overflow. PSO/root signature/VB/CB и
heaps живут до GPU completion; Create/Release/Quarantine не перекрываются с
записью. Owner не изменяет эти данные до Wait. References на результаты и
тестовые atomics переживают группу и любой путь её дренирования.

Такое владение соответствует требованию D3D12 не обращаться конкурентно к
одному allocator/list: [Microsoft multithreading sample](https://learn.microsoft.com/en-us/samples/microsoft/directx-graphics-samples/d3d12-multithreading-sample-win32/).
Wait может помогать исполнению задач на owner thread — это контракт GR, а не
привязка task к конкретному background worker. CPU affinity не используется.

## Ошибки

Worker возвращает HRESULT/closed в отдельный результат; только owner изменяет
m_recordFailed и пишет диагностику. Исключение второго Submit не обходит Wait
ранее принятой задачи. Исключения callbacks наблюдаются через Wait после
дренирования всей группы. CreateGroup/Submit/Wait исключения перехватываются;
деструктор группы остаётся дополнительной страховкой lifetime.

При любом неполном результате Execute не вызывается; повторный RenderFrame
отказывает. Для восстановления выполняется Shutdown/Initialize, затем
ConfigurePreparation с прежним живым JobSystem. Unknown quarantine остаётся
терминальным состоянием, а не способом восстановить живое устройство.

Fault harness: allocator/reset/record/close для всех четырёх lists;
submit — исключение перед вторым Submit после успешного первого;
throw — исключение worker callback. Для Draw faults используется ограниченный
по времени rendezvous двух callbacks. Тест подтверждает entered=exited=2,
overlap=1 и неизменный счётчик Execute. Для submit: entered=exited=1.
Затем повторный RenderFrame отказывает, прежняя GPU-работа дренируется,
Initialize/depth-test успешны. Проверен также один background worker + owner.
Вне fault harness rendezvous отсутствует.

## Проверки

- ReleaseDX12|x64: сборка успешна.
- Новый parallel runner: 36/36 случаев + 4/4 WM_TIMER, включая callback
  exceptions, Submit interruption, resize и повторный Initialize.
- Legacy/multi runner: 31/31 + 4/4 WM_TIMER.
- Прежние регрессии: run-renderprep 17/17 + 4/4 WM_TIMER; F3.2C.2 32/32.
- CPU selftest: 857 проверок, 0 ошибок. CPU ASan с GR_JOBS_TESTING:
  Prepare 510, snapshot 566486, visibility 1519828, ordering 4892;
  везде 0 ошибок, exit 0, сообщений ASan нет. Лог f353b-asan-tests.log.
- Во всех трёх режимах RGBA readback hash: `9dd37d87b8eb202d`.
- GPU Debug Layer и GPU-based validation включены, RTX 5070 Laptop GPU.
  WARNING/ERROR/CORRUPTION отсутствуют. JobSystem starts=1,
  submitted=completed, unobserved=0, participants=0 после остановки.
- Тест throw намеренно даёт failures=1; остальные новые случаи failures=0.

## Границы метрик

- Wall-clock recording: Reset → последний Close, включая owner/worker работу,
  scheduling, Wait и запись timestamp resolve, без CPU Prepare и GPU fence wait.
- Scheduling: CreateGroup + два Submit. Это наблюдаемая длительность API,
  включая возможную синхронную работу scheduler; не чистая стоимость очереди.
- JobGroup wait: длительность Wait, включая помощь owner в исполнении задач.
  Scheduling и Wait входят в recording: повторно суммировать их нельзя.
- Submission: только ExecuteCommandLists; без Present, Signal или GPU ожиданий.
- GPU: разность двух timestamp queries в owner Begin/End, делённая на частоту
  direct queue. Включает GPU clear/draw/barriers (и readback в depth-test),
  исключает Present и resolve после второй метки. Это не сумма worker durations.

Query heap и 16-byte readback создаются на каждый FrameResource slot, читаются
после существующего fence либо полного drain; новых ожиданий не добавлено.
Resize собирает pending samples перед release, Shutdown — после успешного
GPU drain. При Unknown оба ресурса передаются quarantine. Если frequency
недоступна, метрика явно помечается available=0.
Методика: [Microsoft D3D12 timing](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing).

## Результат измерений

Одна серия на режим, 30 кадров прогрева + 200 измеряемых, fixed pose,
Serial CPU Prepare. Остальные запущенные нами тесты завершены перед замером.
Debug Layer/GBV включены. Величины в мс: recording / scheduling / Wait /
submission / GPU. Scheduling и Wait уже включены в recording.

- Legacy (без пула): 0.308640 / 0 / 0 / 0.049543 / 0.022085.
- Multi (без пула): 0.730095 / 0 / 0 / 0.013565 / 0.054384.
- Parallel, 1 background worker: 0.699779 / 0.008279 / 0.253267 / 0.016838 / 0.054333.
- Parallel, 2: 0.680204 / 0.010996 / 0.237598 / 0.015896 / 0.054341.
- Parallel, 4: 0.711911 / 0.013462 / 0.262987 / 0.017751 / 0.054488.
- Parallel, 8: 0.741371 / 0.020961 / 0.267572 / 0.016069 / 0.054498.

Все шесть прогонов подтверждают 230 CPU frames / 200 samples и 200 GPU samples.
Это диагностические числа одного короткого запуска, не доказательство
устойчивого ускорения. Legacy остаётся дешевле на двух кубах. Multi/parallel
GPU время практически одинаково в этом измерении; CPU worker durations не
суммировались и не подменяют GPU time. Для performance-решений нужны повторные
серии без validation и более репрезентативная нагрузка.

## Воспроизведение

Из корня репозитория:

```powershell
& .\ogsr_engine\Layers\xrRenderDX12\tests\run-parallel-recording.ps1 -Executable .\x64\f353b\xrDX12Smoke.exe
& .\ogsr_engine\Layers\xrRenderDX12\tests\run-recording-benchmark.ps1 -Executable .\x64\f353b\xrDX12Smoke.exe
& .\x64\f353b\xrDX12Smoke.exe -commandlists parallel -depthtest -jobworkers 2 -recordfault throw 1
# Ручной осмотр:
& .\x64\f353b\xrDX12Smoke.exe -commandlists parallel -fixedpose -jobworkers 2
```

Логи/бинарники остаются в игнорируемом x64: f353b-build.log, f353b-results,
f353b-regression.log, f353b-legacy-multi.log, f353b-renderprep.log,
f353b-runtime.log, f353b-asan.log, f353b-timings.

## Ограничения

Ручной визуальный осмотр, race detector и отдельный leak detector не выполнены.
CPU ASan harness не инструментирует D3D12 runtime и новые recording callbacks.
Реальный driver failure/TDR/Unknown quarantine и отказ каждого GPU allocation
не форсировались. Submit injection — контролируемое исключение на owner до
второй отправки, а не перехват внутреннего allocator enkiTS. Отказы библиотеки
проверяются её существующими CPU regression tests.

GPU validation влияет на CPU и GPU timings. Два куба — слишком маленькая сцена
для вывода о пользе parallel recording; ускорение FPS не заявляется. Нет
параллельного игрового traversal, registry или изменения DX11/R4; их runtime
не запускался. Representative CPU ordering не переносился на GPU.

Изменены CHW12.h/.cpp, dx12mesh.h/.cpp, dx12quarantine.h, xrDX12Smoke.cpp,
tests/run-wmtimer.ps1; добавлены tests/run-parallel-recording.ps1,
tests/run-recording-benchmark.ps1 и этот отчёт. Roadmap/ADR не изменялись,
пользовательские изменения сохранены. После F3.5.3B — остановка для ревью.


## Финальное ревью перед коммитом

Подтверждённых дефектов и блокеров не найдено; production-исходники при
финализации не менялись.

- Draw: m_created, m_vertexBufferView, root signature и PSO только читаются.
  ComPtr::Get возвращает указатель без копирования ComPtr/AddRef/Release.
  Все вызываемые методы записи принадлежат эксклюзивному command list задачи;
  на shared PSO/root signature методы изменения не вызываются. VB view копируется
  в команду; CB address и RTV/DSV handles переданы значениями. Нет shared caches,
  lazy initialization, счётчиков или записи в лог. Create/OnUploadComplete
  завершаются до RenderFrame; Release/Quarantine/Resize выполняются owner после
  возврата RenderFrame, когда все recording tasks уже завершены. Это контракт
  standalone owner thread, не разрешение конкурентного Shutdown извне.
- Timestamp heap/readback принадлежат конкретному FrameResource. Pending ставится
  только после Execute, Resolve записан в End того же кадра. Повторное чтение
  слота происходит после WaitForFenceValue(frame.fenceValue), до Reset и новой
  записи; pending снимается после сбора. Resize ждёт полный drain и собирает
  pending перед release. Shutdown читает результаты только при WorkCompleted.
- Отказ GR Submit/recording происходит до Execute, поэтому новые query данные
  не публикуются и pending нового кадра не устанавливается. При failed Signal
  рендеринг останавливается; Shutdown не доверяет несигналенному frame value,
  а пытается Signal нового значения и полный drain. При неуспехе и живом
  устройстве query heap/readback вместе с allocator/list передаются quarantine;
  Map не вызывается. DeviceRemoved также не читает timestamp данные.
- Внешние results/atomics живут дольше JobGroup. Исключение Submit перехватывается
  внутри области группы, затем вызывается Wait. Callback exceptions выдаются
  DrainImpl только после ожидания всех tasks. Даже при исключении из внешнего
  блока деструктор группы предшествует уничтожению captured locals. После
  завершения RenderFrame незавершённых recording tasks не остаётся.

### Естественный overlap

Во временной диагностической сборке измерены steady_clock интервалы непосредственно
перед allocator Reset и после list Close, с thread ID каждого task. Fault injection
не включалась: нет rendezvous, sleep или дополнительного межпоточного барьера.
Логирование выполнялось owner после Wait. По 30 warmup + 200 измеряемых кадров:

- 2 background workers: overlap 200/200, разные thread IDs 200/200;
  среднее пересечение 0.1853735 мс; task durations 0.2215295 / 0.2074245 мс.
- 4 background workers: overlap 200/200, разные thread IDs 200/200;
  среднее пересечение 0.2015240 мс; task durations 0.2443610 / 0.2229910 мс.

Пересечение вычислено как max(0, min(end0,end1)-max(begin0,begin1)). Это
перекрытие wall-clock интервалов записи на разных потоках, не доказательство
одновременной загрузки двух CPU cores каждую наносекунду и не оценка speedup.
Диагностические часы/логирование и GBV влияют на наблюдение. Временная
инструментация не входит в коммит: CHW12.cpp восстановлен побайтово, SHA-256
ECFAC0F110DD11A2789A4819C2E41FDAF0D130E0F300AF676F016C1922868DD0 совпал с резервной
копией. Локальные логи: x64/f353b-review/overlap-w2.log, overlap-w4.log, overlap.json.

После восстановления повторены обычная ReleaseDX12|x64 сборка и 5 запусков:
depth legacy/multi/parallel, parallel callback throw и interrupted Submit с
восстановлением. Все PASS, hash 9dd37d87b8eb202d, validation warnings/errors=0,
диагностических overlap меток в обычной сборке нет. Прежние 36+4, 31+4,
17+4, 32 и CPU/ASan результаты относятся к тем же неизменённым исходникам.
Реальный failed Signal/TDR/quarantine в этом ревью не форсировался; эти ветки
проверены по коду. Остальные ограничения предыдущего раздела сохраняются.

---

# Final validation (2026-10-20)

## Изменения перед проверкой

- **A1**: `Initialize()` сбрасывает `m_prepFailed` рядом с `m_recordFailed`.
  Добавлен test-only `ArmPreparationFailure()` и smoke-режим `-prepfault`,
  который проверяет цепочку `fault → Shutdown → Initialize → кадр`.
- **A4**: перед rendezvous проверяется `GetStats().totalParticipants >= 2`.
  При полностью последовательной системе тест помечается FAIL, а не выдаёт
  таймаут за подтверждённое перекрытие. Один background worker (участников 2)
  продолжает работать штатно.
- **A2**: API `JobGroup` не изменялся. Отсутствие таймаута у `Wait()` записано
  комментарием в месте вызова.
- **A3**: проверено, изменений нет. `resource.list.Reset()` — это
  `ComPtr::Reset()` (освобождение ссылки), а не
  `ID3D12GraphicsCommandList::Reset(allocator, pso)`. `Close` в аварийную
  ветку не добавлялся.
- **A5**: не изменялось. `run-wmtimer.ps1` — 9 429 байт, 0 байт > 127,
  ASCII-only, поэтому отсутствие BOM функционально безопасно.

## 1. Три режима записи

Один и тот же fixed-pose кадр, `-depthtest`, Debug Layer + GBV:

| Режим | RGBA hash | Контрольные точки | validation BAD |
|---|---|---|---|
| legacy | `9dd37d87b8eb202d` | 3/3 PASS | 0 |
| multi (sequential) | `9dd37d87b8eb202d` | 3/3 PASS | 0 |
| parallel, 4 workers | `9dd37d87b8eb202d` | 3/3 PASS | 0 |

Контрольные точки (ожидание = факт):

- `[640,360]` near cube back face: `(0.000,1.000,0.000,1.000)`;
- `[640,180]` far cube front face: `(0.000,0.000,1.000,1.000)`;
- `[640,40]` background: `(0.071,0.161,0.322,1.000)`.

Порядок исполнения задан массивом `ExecuteCommandLists`:
`owner Begin → Draw 0 → Draw 1 → owner End`. Все списки закрываются до
вызова: `closeList(0)`, `draw->Close()` в задачах, `closeList(3)`, затем
`JobGroup::Wait` и только после него Execute.

## 2. Fault matrix

`run-parallel-recording.ps1`: **36/36** случаев и **4/4** WM_TIMER.

Дополнительная матрица для workers 1 и 4 (stages 0–5, lists 0–3 для
allocator/reset/record/close и 1–2 для submit/throw): **40/40 pass**.

Для каждого случая проверено:

- `[recording] ABORTED mode=parallel lists=4 frames=1` — частичной отправки нет;
- `[recording-recovery] PASS stage=N list=M submittedUnchanged=1`;
- два `GPU drained before shutdown` (аварийный и финальный);
- 0 `validation WARNING/ERROR/CORRUPTION`.

Rendezvous-маркеры:

| Stage | Маркер |
|---|---|
| 0,1,2,3,5 (lists 1/2) | `entered=2 exited=2 overlap=1` |
| 4 (submit) | `entered=1 exited=1 overlap=0` |
| 5 + `-jobworkers 1` | `entered=2 exited=2 overlap=1` |

## 3. Resize, WM_TIMER, shutdown, повторная Initialize

- Resize: 3 успешных перехода (`960x540`, `1280x720`, `1024x768`),
  `resizeSuccess=3`, после каждого — `WaitForGpu` и полное пересоздание
  RTV/frame resources/depth/readback. 0 validation сообщений.
- WM_TIMER: 4/4 PASS (`parallel` и `verify`, обычный и `-timerfault`);
  при инъекции `timerFrames=0`, `renderFrameFailed=1`, устройство не удалено,
  задания завершены (`failures=0`, `unobserved=0`).
- Shutdown: при каждом прогоне `GPU drained before shutdown`; при Unknown
  ветка передаёт ресурсы карантину (проверено по коду, TDR не форсировался).
- Повторная Initialize: `-prepfault` → `[prep-recovery] PASS:
  failure -> shutdown -> initialize -> frame`, exit 0.

## 4. Перекрытие CPU-записи

Fault harness подтверждает реальное перекрытие двух callback'ов:
`overlap=1`, `entered=2`, `exited=2` для stages 0–3 и 5, lists 1 и 2,
workers 1 и 4. Каждый Draw использует собственный allocator/list слота;
owner только читает mesh и CB, пишет в owner-список.

Fault stage 4 (отказ второго Submit) даёт `entered=1`, то есть второй
callback не стартовал — это и есть проверяемое отсутствие частичной работы.

## 5. Раздельные тайминги

200 кадров, 170 измеряемых, fixed pose, Serial CPU Prepare, Debug Layer/GBV
включены. Величины в мс: recording / scheduling / Wait / submission / GPU.

| Режим | recording | scheduling | JobGroup Wait | submission | GPU |
|---|---|---|---|---|---|
| legacy | 0.319716 | — | — | 0.065786 | 0.026546 |
| multi | 0.856537 | — | — | 0.016719 | 0.059213 |
| parallel (4) | 0.749056 | 0.013566 | 0.259941 | 0.022129 | 0.059096 |

Остаточный wall-clock интервал parallel ≈ 0.749056 − 0.013566 − 0.259941 =
**0.475549 мс**. Это не чистое CPU-время worker'ов: интервал включает
планировщик, owner-часть записи и накладные расходы, а worker-задачи
выполняются параллельно. Scheduling и Wait уже входят в recording и повторно
не суммируются. GPU измерен отдельно двумя timestamp queries и не смешивается
с CPU-числами.

## 6. Test-only hooks в обычном режиме

На 200-кадровых прогонах всех трёх режимов: `faultHooksActive=0`
(нет `parallel-fault` и `fault injection`), `validation BAD=0`,
`renderprep PASS`, `recording PASS`.

## Статус

**PASS** для F3.5.3B в пределах smoke-сценария: три режима дают идентичный
RGBA-результат, fault-матрица не оставляет частичных отправок, resize и
WM_TIMER проходят без validation-сообщений, метрики разделены.

## Ограничения

- Сцена — два куба; числа не доказывают ускорение на игровой нагрузке.
- Debug Layer и GBV включены и влияют на CPU/GPU timings.
- TDR, `DXGI_ERROR_DEVICE_REMOVED` и Unknown-quarantine не форсировались;
  эти ветки проверены по коду.
- Ветка `totalParticipants < 2` недостижима в текущем smoke
  (`automaticWorkers=true` гарантирует ≥1 worker), проверена логически.
- `JobGroup::Wait()` не имеет таймаута — принятый риск, API не менялся.
- Визуальный осмотр и race detector не выполнялись.

