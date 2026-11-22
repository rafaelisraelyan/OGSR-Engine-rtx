# Независимый аудит текущего HEAD F3.5.6B

Дата: 2026-10-21. Ветка: `prypiat`.
Проверенная ревизия: `5b025aaa97d6ef9aa64ce2a28b9f445583b3a4f9`.
Историческая F3.5.3B: `53b82c5b7`; baseline F3.5.3A: `b523262242ac857e8d6c866a72aafb325c54e910`.
Продолжение `dx12-f3-5-3b-independent-audit-2026-10-21.md`.

## Вердикт и границы доказательств

- **Историческая F3.5.3B: FAIL** — H1 подтверждён предыдущим независимым воспроизведением. Он унаследован от F3.5.3A. Этот вердикт не переносится автоматически на текущий HEAD.
- **Текущий HEAD: CONDITIONAL PASS** для рассматриваемого standalone Smoke, его существующих callers и одного owner thread. Новых Critical/High не подтверждено. H1 исправлен `d38ea6d85a6363739f850c81a9faafe727ba93b0`.
- Цепочка GPU lifetime проверена по исходникам до Reset/Map/Release, подкреплена CPU fault matrix и настоящим D3D12 fence. Это не исчерпывающее экспериментальное доказательство всех отказов драйвера: failed Signal, WAIT_FAILED, ArmFailed и device removal воспроизводились адаптером, не принудительным отказом реального GPU.
- Обнаружен **Low L1**: возможная неправильная привязка GPU timing после Map failure. **Medium M1** — подтверждённый пробел renderer fault harness: оба Submit-сценария проверяют вторую отправку.
- Отсутствие гонок обосновано ownership и happens-before в рассмотренном пути. ASan и GPU validation сами по себе не доказывают отсутствие data races. Race detector и естественный overlap callbacks в этом аудите не измерены.
- Гарантии нельзя переносить на произвольные конкурентные вызовы CHW12, динамическую выгрузку registry или игровой traversal. Эти режимы здесь не реализованы и не проверялись.

Production-код, JobSystem, roadmap, AGENTS.md и пользовательские файлы не менялись. Commit/push/merge/rebase не выполнялись. Добавлен этот отчёт; диагностические harness и журналы лежат в игнорируемом `x64/audit-f353b/`.

## Findings

### M1 — Medium, пробел покрытия первого GR Submit

**Места:** `ogsr_engine/Layers/xrRenderDX12/CHW12.cpp:2844`, `tests/run-parallel-recording.ps1:26`, `:47`.

Условие `faultStage==4 && i==1` всегда выбрасывает исключение перед вторым `group.Submit`. Значения `-recordfault submit 1` и `submit 2` меняют метку, но не номер отказавшей отправки. Скрипт для обоих ожидает `entered=1 exited=1`.

**Воспроизведение:** оба CLI-сценария успешно прошли существующий скрипт, но по коду оба оставляют одну принятую задачу. Это не подтверждение отказа первой отправки и не настоящий backend rejection: исключение создаёт owner до вызова GR.

**Последствие:** ложное впечатление, что оба места отказа Submit покрыты end-to-end. Дефекта drain в самой обработке не найдено: catch выполняет Wait, затем проверяет failure/closedCount и отказывается от Execute.

**Независимое дополнение:** отдельная сборка с `GR_JOBS_TESTING` отвергала именно первый и второй backend Submit, а также первую и вторую регистрацию задачи, на 1/2/4/8 workers. Принятые callbacks исполнены ровно один раз, rejected callback не исполняется, после Wait active=0, группа допускает успешный повторный Submit. Это проверка реального GR, не полная инъекция в D3D12 renderer.

**Рекомендация:** отдельный test-only счётчик номера Submit в renderer harness, с assertions по количеству принятых/завершённых задач, неизменности GPU submission counter и повторному Initialize. Production API расширять не требуется.

### L1 — Low, старый timestampSampleIndex после Map failure

**Места:** `CHW12.cpp:2154–2159`, `:2165–2169`, `:3182–3199`; `CHW12.h:167` (лимит 512 CPU-сэмплов).

`CollectGpuTiming` сбрасывает pending, но при failed Map выходит до очистки `timestampSampleIndex` и `timestampSample`. Когда массив CPU-сэмплов заполнен, следующие кадры не назначают новый индекс. Если Map следующего кадра успешен, GPU-время этого кадра записывается в старый сэмпл.

**Воспроизведение:** harness `timestamp_repro.cpp` использует фактическое тело `CollectGpuTiming` текущего HEAD и stub readback. Первый Map возвращает E_OUTOFMEMORY. Затем имитируется следующий submitted frame при заполненном массиве; второй Map успешен. Старый сэмпл меняется с 111 на 2000, хотя должен остаться неизменным. Exit 0 означает воспроизведённый дефект, не PASS реализации.

**Последствие:** неправильная привязка метрик/искажение распределения GPU time в редком failure path. Не ранний Map относительно fence и не GPU use-after-free.

**Рекомендация:** безусловно инвалидировать индекс/флаг потребляемого timestamp до любого раннего выхода; назначать валидный индекс только при создании CPU-сэмпла. Добавить регрессию Map-failure + exhausted samples.

### Ограничения покрытия, не объявленные production-дефектами

- Нет настоящей инъекции failed Signal, WAIT_FAILED, removal и Unknown quarantine в полном CHW12. Для этих ветвей есть source trace и независимый mock oracle; реальные разрушительные GPU/TDR-тесты не запускались.
- `JobGroup::Wait` без deadline: зависание драйвера внутри callback способно удерживать owner. Это риск доступности; освобождать ресурсы параллельно зависшему worker было бы небезопасно. Внешний watchdog ограничивает тесты, но не является восстановлением renderer.
- Обычный не-removal failed Present (`CHW12.cpp:3230–3237`) сам по себе не устанавливает fail-stop флаг. Существующие Smoke callers немедленно прекращают цикл и вызывают Shutdown. Повторный RenderFrame после такого false не входит в доказанный контракт; при расширении API это надо укрепить.
- `run-scaling-benchmark.ps1:41–76` считает median/p95, но не p99. Это пробел относительно полного profiling gate, не дефект GPU lifetime.

## Все callers ожидания и последствия

В renderer единственный прямой caller `dx12fence12::WaitForValue` — адаптер `CHW12::WaitForFenceValue`, `CHW12.cpp:1818–1820`. Остальные прямые callers находятся в CPU-тестах.

| Путь | Fence и условие | Действие после успеха | Действие после отказа |
|---|---|---|---|
| RenderFrame, `:2586–2619` | fence текущего FrameResource; ненулевой value | WriteObjectConstants, timestamp Map, Reset allocator/list, запись CB | ReportGpuFailure, false до записи/Reset/Map |
| VerifyDepthTest, `:1055–1074` | текущий m_fenceValue | Map color readback | false до Map |
| VerifyBackgroundOnly, `:1220–1234` | текущий m_fenceValue | Map color/depth readback | false до Map |
| VerifyTransformScene, `:1328–1351` | текущий m_fenceValue | проверка размера, Map | false до Map |
| VerifyGeometryScene, `:1491–1513` | текущий m_fenceValue | проверка размера, Map | false до Map |
| WaitForGpu, `:1874–1899` | новое монотонное value, сначала queue Signal | true только при подтверждённом completion | failed Signal не вызывает wait; остальные отказы возвращают false |
| Initialize → WaitForGpu, `:416–426` | drain после upload | release upload buffers через OnUploadComplete | ресурсы сохранены до Shutdown |
| Resize → WaitForGpu, `:3287–3329` | drain всей direct queue | сброс slot fences, Collect timing, освобождение и пересоздание slot resources | backbuffers/allocators сохранены, ReportGpuFailure |
| Shutdown → DetermineGpuLifetime, `:3479–3589` | новый drain либо подтверждённый removal | release по классификации | Unknown → Detach в process quarantine |

`CollectGpuTiming` имеет три call sites: `:2619` после slot fence, `:3319` после resize drain, `:3512` только при WorkCompleted в Shutdown. Сам метод не проверяет fence: безопасность зависит от этих callers. Новых скрытых callers не найдено.

Readback oracle вызывается только после успешного RenderFrame в существующих Smoke test paths. После failed Present/Signal тест не продолжает Verify/Map.

## Stale wake, повторное ожидание, Signal и removal

`dx12fencewait.h:74–119` проверяет removed marker до сравнения completed >= requested и повторно после event wake. Event не считается доказательством completion. Timeout/WaitFailed/ArmFailed/SpuriousWake не возвращают Completed.

SpuriousWake в CHW12 — fail-stop, не цикл повторного ожидания. RenderFrame помечает неизвестное состояние и выходит. Shutdown затем может выполнить новую попытку drain с большим значением fence. Если stale registration снова разбудит ожидание слишком рано — повторно получается false и ресурсы уходят в quarantine. Если новый completed value действительно достигнут — release безопасен.

Независимый CPU oracle:

- Фактические тела `WaitForGpu`, `IsDeviceRemovedConfirmed`, `DetermineGpuLifetime` извлечены из текущего исходника; D3D12/Win32 заменены адаптерами. Обёртка wait вызывает настоящий `dx12fence12::WaitForValue`.
- 216 комбинаций: три wait status, failed/successful Signal, arm failure/success, подтверждённый/неподтверждённый removal, completed до и после wait (ниже цели, достигнутая цель, UINT64_MAX).
- WorkCompleted проверяется по фактическому counter адаптера и наличию успешного Signal, не по renderer log.
- Дополнительно: timeout value 10 → stale wake value 13 при completed 10 → Unknown → повторный drain value 14 при completed 14 → WorkCompleted.
- Итог вместе с независимыми GR проверками: **795 assertions, PASS**. Встроенные fence tests отдельно: **37 checks, 0 failures**.

Настоящий D3D12 oracle `real_fence.cpp`:

- Direct queue удерживается за CPU-controlled gate fence. В неё отправлены Signal(10), затем второй gate, затем Signal(13). Нет зависших shaders, forced TDR или GPU resource destruction до completion.
- Ожидание 10 получает Timeout; CPU открывает первый gate. Старое event уведомление будит ожидание 13.
- Фактический вывод: `first=Timeout stale=SpuriousWake actual=10 requested=13 retry=Completed final=13`.
- После открытия второго gate повторное ожидание завершается при реальном GetCompletedValue()==13. Exit 0.

Failed Signal: `WaitForGpu` возвращает false до wait. Frame Signal failure в `:3243–3249` помечает состояние неизвестным/removed; следующий RenderFrame и Resize запрещены. Shutdown не доверяет старому slot value, а пытается сигналить новый value. Успех этого drain покрывает прежние Execute; повторный отказ без removal даёт Unknown.

Device removal: UINT64_MAX никогда не становится WorkCompleted. Release по DeviceRemoved возможен после GetDeviceRemovedReason()!=S_OK (`:1902–1947`). При несогласованном marker + S_OK выбирается Unknown. WAIT_FAILED не приравнивается к removal.

## GPU lifetime, quarantine и повторный Initialize

- Execute кадра в `:3179` предшествует Present и Signal. `m_gpuWorkSubmitted=true` в `:3201`, до Present. Между Execute и флагом только невыбрасывающие счётчики/записи фиксированного массива и chrono; нет потенциально выделяющего контейнера.
- Первый upload использует owner list/allocator frame slot 0. `m_gpuWorkSubmitted` выставляется сразу после успешного Create (`:367`, `:392`); CPU BuildScene выполняется до submission. Geometry test после upload submission реально прошёл и последовал штатный drain.
- Registry сохраняет VB/IB/upload и PSO/root signature. OnUploadComplete освобождает upload только после drain (`dx12geometryregistry.cpp:361–371`). Unknown переносит все эти объекты (`:391–404`); вместимости registry/quarantine связаны static_assert в `.h:75`.
- Quarantine (`CHW12.cpp:1950–2036`) переносит device/queue/swapchain, все slot RT/CB, allocator/list, Draw/End пары, timestamps/readbacks, depth/DSV/RTV, mesh и registry, fence/event. CPU mapped pointers обнуляются. Storage не делает аварийных аллокаций и не вызывает автоматический COM Release.
- Unknown запрещает дальнейший Initialize как того же экземпляра, так и нового через process-global occupied (`:236–259`). Quarantine не предназначен для recovery или многопоточного игрового renderer.
- Normal Shutdown дренирует очередь до Reset/Release и сбрасывает tracking. Повторный Initialize заново создаёт ресурсы; render metrics/failure state сбрасываются. Реальные recovery tests это проверили.
- Shutdown/Resize не вызываются одновременно с worker callbacks в рассмотренных callers. Owner синхронно ждёт группу в RenderFrame; оконные сообщения обрабатываются на том же потоке. Перенос Shutdown на другой поток потребует отдельного протокола.

## Thread safety parallel recording

- `CHW12.h:337–356`: каждая пара allocator/list принадлежит определённому frame slot и list index. `CHW12.cpp:2655–2667` строит раздельные адреса. Owner пропускает Draw Reset в parallel (`:2685`), callbacks сбрасывают только свою пару (`:2877–2879`, `:2929–2931`).
- Worker writes: свой command list/allocator, свой `results[i]`; общие entered/exited/overlap — atomic. Остальные captures — значения либо указатели на неизменяемые локальные ordered/plan/resolved, mesh/registry. Их владельцы живут за пределами области JobGroup.
- `SmokeMesh12::Draw`, `dx12mesh.cpp:683–707`, и DrawTransformed не имеют кэшей, shared mutation, Map/Unmap, COM Release/AddRef или обращения к глобальному renderer. ComPtr::Get читает указатель; остальные вызовы направлены в эксклюзивный command list. VBV, PSO, root signature и CB VA неизменны до Wait.
- `GpuMeshRegistry12::RecordDraw`, `dx12geometryregistry.cpp:451–470`, аналогично читает immutable ресурсы и записывает свой list; VBV/IBV передаются по значению из resolved frame.
- Каждый Draw list устанавливает RTV/DSV, viewport/scissor, root signature, PSO, topology, VBV, CBV; geometry добавляет IBV и per-object CBV. Descriptor/CB writes выполняет только owner после GPU fence и до Submit.
- При любой ошибке Submit/recording/Close внешняя область JobGroup удерживает captures. Owner сначала Wait (`:2959`), затем проверяет failure и closedCount (`:2967`), затем число Draw. Execute доступен только после успешного owner End Close.
- GR регистрирует unique_ptr до публикации (`JobGroup.cpp:43–57`); backend rejection предшествует AddTaskSetToPipe. Callback exceptions перехватываются InternalJobTask. Wait дренирует все tasks до rethrow; destructor имеет DrainNoThrow.
- Публикация результатов обеспечивается release completion в enkiTS `TaskScheduler.cpp:638` и acquire `GetIsComplete`, `TaskScheduler.h:113`. Поэтому последующее чтение results owner не требует atomic для каждого поля.
- Нельзя обосновывать safety одним `const`: она требует неизменности ресурсов, одного владельца каждого list, соблюдения owner-thread lifecycle и Wait до разрушения captures. Эти условия выполнены в рассмотренном Smoke path.
- Reentrancy RenderFrame ограничена m_preparing. JobSystem lifecycle из callback запрещён; recording callbacks его не вызывают. GR groups не используются реентерабельно из собственных callbacks.

## D3D12 ordering и timestamps

Созданные lists закрываются перед первым Reset. Frame slot fence защищает allocator reset и CPU overwrite CB. Begin записывает PRESENT→RENDER_TARGET и clear; Draw lists полны по bindings; End делает readback/barriers, timestamp end/resolve и Close. Один Execute получает фиксированный массив Begin→Draw 0..L-1→End. Owner выполняет Present→Signal.

Важное уточнение: один Execute с несколькими lists не означает полного GPU idle между lists; драйвер может перекрывать работу. Корректность текущих RT/depth операций опирается на порядок команд, D3D12 render-target/depth ordering и barriers, а не на предположение о завершении каждого list перед началом следующего. Произвольные UAV/copy dependencies сюда добавлять без анализа нельзя.

Timestamp heap/readback собственные у каждого frame slot. Query 0 записывается в Begin (`:2700`), query 1 и Resolve — в End (`:3169–3171`). Readback потребляется после fence того же submission либо общего drain. Query/readback освобождаются вместе со слотом или карантинируются. Failed recording не ставит timestampPending; failed Present после Execute оставляет pending до Shutdown drain. На Unknown/DeviceRemoved timestamp Map не выполняется.

## Метрики и ограничения performance

- `recordingMs`: wall-clock от начала recording до Close End; включает scheduling, Wait, owner recording. Нельзя прибавлять scheduling+Wait ещё раз.
- `scheduleMs`: CreateGroup и цикл Submit, включая возможное синхронное исполнение задачи через scheduler. Это не чистая стоимость постановки в очередь.
- `recordWaitMs`: wall-clock JobGroup::Wait; owner может помогать исполнять callbacks. Это не только блокировка.
- `submissionMs`: только CPU ExecuteCommandLists. Не включает Present, Signal, GPU execution.
- GPU timestamp interval: диапазон GPU-команд данного кадра, не CPU submission и не FPS. Slot fence ожидания выполняются отдельно и учитываются в gpuFenceMs.
- Фактический natural overlap двух callbacks без rendezvous в данном аудите не измерен. Счётчик overlap в fault harness достигается тестовым барьером и не является доказательством естественного масштабирования.
- Полный performance sweep и независимая PIX/ETW timeline не повторялись. ASan timings и результаты одновременно запущенных correctness tests не используются для вывода об ускорении.
- Новых заявлений о приросте FPS нет. Vsync Present и синтетическое повторение двух кубов не являются representative игровым FPS.

## Карта рисков F3.5.4–F3.5.6B

1. **F3.5.4A, 1–8 списков и рост Draw count.** Риск выхода за массив/повторного allocator ownership. Текущий bound=8, slot drawListCount, независимые пары, uint64 range arithmetic и проверки количества Draw ограничивают риск. Фиксированный submission order сохраняется. Registry/CPU scene сложности не покрываются повторением тех же двух кубов.
2. **F3.5.4B, telemetry.** Риск неверной агрегации, цензурирования после 512 samples, отсутствующего p99 и L1 при Map failure. Срезы хранятся заранее, I/O после Shutdown; scheduling/wait вложены в recording. Для performance acceptance нужны исправление L1, валидность каждого sample и реальные повторные p50/p95/p99 измерения.
3. **F3.5.5A/B, packets и visibility.** Риск изменения порядка/удаления Draw и передачи изменяемых данных. Текущие ordered/resolved значения локальны на кадр; Validate до Reset; empty frame имеет отдельный путь. Не переносить synthetic IDs на настоящий mutable registry без ownership.
4. **F3.5.5C, batches/lists.** Риск пропуска/повторного Draw и dangling plan. План строится и валидируется owner; непрерывные диапазоны, локальный lifetime до Wait, проверка recorded draw counts; L<=1 не запускает workers. CPU tests покрывают planner.
5. **H1 hardening d38ea6d85.** Event перестал означать completion. Остаётся отсутствие полной end-to-end hardware fault injection, но source trace, 216-case CPU matrix и реальный stale-event oracle согласованы.
6. **F3.5.6A, object CB.** Риск GPU/CPU overwrite и out-of-bounds. Запись после slot fence; 256-byte slices, максимум 256 объектов, owner-only writes; workers получают VA и не пишут CB. Это фиксированная capacity, не масштабируемый transient allocator.
7. **F3.5.6B, indexed registry.** Риск раннего освобождения VB/IB/upload, stale generation и неверных ranges. Три фиксированных mesh, registry живёт до Shutdown, CPU resolution транзакционный, range/handle/transform проверки до recording, upload drain и quarantine включают registry. Динамической выгрузки, residency/eviction и generation reuse пока нет; нужен новый lifetime design перед добавлением.

## Выполненные проверки

- Фактический Git HEAD/status/diff; production diff отсутствует, пользовательские изменения сохранены.
- ReleaseDX12|x64: `MSBuild ogsr_engine/Layers/xrRenderDX12/xrDX12Smoke.vcxproj /p:Configuration=ReleaseDX12 /p:Platform=x64 /m:1 /nr:false` — exit 0; зависимости GR и xrRender_R5_DX12 собраны/проверены MSBuild. Это incremental build. Первоначальный solution invocation не дал результата и был прерван; ему PASS не приписывается.
- Независимый current oracle: 216 combinations, 795 assertions PASS; 37 встроенных fence checks PASS.
- Настоящий D3D12 stale wake/retry oracle: exit 0, фактические значения 10/13.
- GR testing regression: 34 tests, 430 checks, 0 failures.
- Parallel recording script: 36 сценариев PASS плюс четыре WM_TIMER прогона (parallel/verify, normal/fault). Expected fault exit=1 учитывается отдельно.
- Отдельные legacy/multi/parallel depth runs: exit 0, hash `9dd37d87b8eb202d`, без validation WARNING/ERROR/CORRUPTION.
- Geometry regression: 16 cases PASS, включая lists 1/2/4/8, resize, reinitialize, post-submit initialization failure и invalid indexed range. Hash geometry `0ea8788795a5950d` — отдельная сцена, не замена legacy hash.
- ASan: итог подтверждающего запуска приведён в дополнении ниже.
- L1 reproduced в CPU stub с фактическим телом CollectGpuTiming.

## Не выполнено / не доказано экспериментом

- Forced TDR, физический device removal, настоящий failed queue Signal, WAIT_FAILED и failed SetEventOnCompletion в CHW12. Они проверены через adapters/source trace, а не опасным воздействием на GPU.
- Полная аппаратная проверка Unknown quarantine с незавершённым GPU workload и попыткой нового Initialize.
- Renderer-level отказ именно первого GR Submit. Проверен независимый реальный GR и source control flow owner; текущий CLI harness имеет M1.
- Race detector, ручной визуальный осмотр, естественный overlap и profiler timeline.
- Полный аудит всего R4/DX11, игрового traversal и всех CPU scene algorithms. Эти области не изменялись аудитом; данный вердикт не является их сертификацией.

## Решение

H1 больше не блокирует текущий standalone HEAD. В пределах существующего owner-thread контракта не найден путь, принимающий stale wake/failed Signal/WAIT_FAILED за завершение и освобождающий ещё используемые GPU-ресурсы. Quarantine остаётся консервативным terminal fallback.

Продолжать ограниченное развитие standalone допустимо с CONDITIONAL PASS. Безусловный end-to-end fault-safety signoff и performance acceptance пока преждевременны: закрыть M1, добавить проверяемые postconditions на границе Reset/Map/Release для failure harness, исправить L1 и измерить естественный overlap/профили. Это рекомендации для отдельного согласованного изменения; в этом аудите исправления не вносились.

F3.5.3B и последующие этапы уже находятся в истории Git. Новый коммит этим аудитом не создавался и не требуется для фиксации production-кода. Исторический FAIL сохраняется как описание старой ревизии.

## Первичные источники API

- Microsoft, [Creating and recording command lists and bundles](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles): ограничения одновременного использования allocator/list и правила Reset.
- Microsoft, [ExecuteCommandLists](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists): возможность перекрытия работы нескольких lists внутри одного вызова и validation ограничений submission.
- Microsoft, [GetCompletedValue](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue): фактическое значение fence и removed marker.

Ссылки на строки относятся к HEAD 5b025aaa97d6ef9aa64ce2a28b9f445583b3a4f9, если явно не обозначена другая ревизия.

## Подтверждение ASan и краткий timing sanity check

Подтверждающий отдельный запуск завершился **exit 0** (`x64/audit-f353b/asan-exit.txt`). ASan diagnostic reports не обнаружены. CPU test результаты:

- render preparation: 510 checks;
- representative snapshot: 566486 checks;
- visibility/compaction: 1519828 checks;
- ordering: 4892 checks;
- packet planner: PASS;
- fence helper: 37 checks;
- transforms: 540 checks;
- geometry: 245 checks.

Все перечисленные проверки — без failures. ASan harness не исполняет D3D12 recording callbacks и не заменяет race detector. Длительный прогон содержит benchmark loops; отсутствие раннего stdout первоначально было ошибочно похоже на зависание. Итоговый успешный запуск и его код выхода сохранены отдельно. Ранние перекрывающиеся диагностические запуски не используются для performance выводов.

После завершения ASan выполнен отдельный краткий timing sanity check на NVIDIA GeForce RTX 5070 Laptop GPU: 256 синтетических Draw (повторение двух кубов), 120 кадров, первые 30 исключены, 90 samples на режим, 2 background workers, default 2 Draw lists для multi/parallel, один процесс на режим. Debug Layer/GBV выключены только в этом timing-прогоне; correctness проверялся отдельно с GBV.

Команда каждого режима:

`xrDX12Smoke.exe -commandlists MODE -fixedpose -draws 256 -prepframes 120 -jobworkers 2 -nodebug -nogbv -metrics PATH`

Сводка, миллисекунды, значения в порядке p50 / p95 / p99; quantile — линейная интерполяция сортированного ряда, отдельно для каждой метрики:

- **legacy:** recording 0.101400 / 0.116355 / 0.183993; submission 0.107450 / 0.143485 / 0.161125; GPU 0.110160 / 0.116360 / 0.126697. Scheduling/Wait отсутствуют (0).
- **multi:** recording 0.130150 / 0.173990 / 0.178994; submission 0.096350 / 0.138515 / 0.171448; GPU 0.108608 / 0.115811 / 0.124186. Scheduling/Wait отсутствуют (0).
- **parallel:** recording 0.139850 / 0.223970 / 0.238091; scheduling 0.016050 / 0.019965 / 0.023023; Wait 0.047300 / 0.116340 / 0.137718; submission 0.104350 / 0.151060 / 0.160205; GPU 0.108816 / 0.116562 / 0.126174.

Во всех трёх прогонах exit 0 и 90 валидных GPU samples. Это проверка наличия и согласованности метрик, **не полный scalability benchmark**: один короткий запуск, мало tail samples, нет повторов, рандомизации порядка и контроля clocks. В данном запуске parallel recording не быстрее legacy; обобщать это на другие нагрузки нельзя. Суммы percentile разных колонок не вычислялись. Scheduling/Wait повторно к recording не прибавлялись. Natural callback overlap эти данные не доказывают.

Артефакты: `x64/audit-f353b/timing-{legacy,multi,parallel}.csv`, соответствующие `.log`, `timing-summary.json`; CPU oracle `current_oracle.cpp/.cmd`, реальный fence oracle `real_fence.cpp/.cmd`, L1 repro `timestamp_repro.cpp/.cmd`.
