# F3.5.5C — Parallel Packet Recording

**Дата:** 2026-10-20
**Baseline:** `d02e2a444` (F3.5.5B)
**Статус:** реализовано и проверено в standalone Smoke; commit/push не выполнялись
**Режимы:** `-drawsource packets` + `-commandlists legacy|multi|parallel`

---

## 1. Цель и границы

F3.5.5C связывает immutable CPU-план с существующей DX12-записью:

```
Snapshot → Visibility → Ordering → FrameBatchPlan12
         → validation → Begin → PacketList 0..N-1 → End
```

Не изменялись:

- DX11/R4 и реальный traversal;
- JobSystem API;
- shader/material/resource registry;
- ownership GPU-ресурсов, fence-per-slot, quarantine и resize;
- synthetic benchmark path по умолчанию.

## 2. Реализация

### 2.1. Конфигурация и CLI

Добавлены независимые packet-параметры:

- `-packetlists N` — верхняя граница logical packet lists, 1..8;
- `-packetbatchlimit N` — test-only ограничение packets в batch, 0 = без
  ограничения.

`-packetlists` нельзя смешивать с synthetic `-draws/-lists`; synthetic
`-lists` остаётся отдельным benchmark-параметром. `-drawsource packets`
теперь разрешён с `-commandlists multi|parallel`.

`CHW12` получил отдельный `m_packetListCount`; synthetic `m_drawListCount`
не переиспользуется. FrameResource создаёт packet-пары allocator/list по
этому пределу.

### 2.2. План до GPU

После `PrepareOrdered()` и до `FrameResource`, `Reset` и `Execute`:

1. `ValidatePacketFrame(ordered)` проверяет generation, категории, object ID,
   visual/material/pso `(id, generation)`, VB range, flags, opacity и
   identity world.
2. `BuildPacketBatchPlan(ordered, maxLists, packetBatchLimit)` строит план.
3. `ValidatePacketBatchPlan` проверяет coverage, gaps/overlap, list/batch
   ranges, category order и resource-key внутри batch.
4. Проверяется, что plan lists не превышают число выделенных списков.

Любая ошибка выставляет `m_prepFailed/m_recordFailed` и завершает кадр до
первого GPU Reset. Частичный Execute невозможен.

### 2.3. Общая запись PacketList12

`CHW12::RecordPacketList()` — единственная функция обхода плана:

```text
for batch in list.batchCount
    for packet in batch.packetCount
        if (!mesh.Draw(...)) return false
        ++draws
```

Функция:

- не пишет в лог и не меняет renderer state;
- проверяет границы batches/packets;
- возвращает число успешно записанных Draw;
- безопасна для вызова из worker callback;
- используется и owner-only путём, и parallel worker'ами.

### 2.4. Mapping на FrameResource

- `PacketList12[i]` → `FrameResource.drawRecording[i]`;
- `drawRecording[i]` содержит эксклюзивную пару allocator/list;
- непустые lists плана используются по порядку;
- при L=4/8 и двух объектах план содержит только два непустых list;
  неиспользованные пары не Reset'ятся и не попадают в Execute.

### 2.5. L=1 и empty frame

Если `packetPlan.lists.size() <= 1`:

- используется owner `FrameResource.commandList`;
- Begin, все Draw и End записываются в него;
- `JobGroup` не создаётся, `tasks=0`;
- план всё равно строится и валидируется через тот же planner.

Empty frame даёт пустой plan, clear render target/depth, 0 Draw и 0 tasks.

### 2.6. L>=2: sequential и parallel

- `multi`: owner Begin → каждый PacketList последовательно в своём list →
  owner End.
- `parallel`: owner Begin; один task на каждый непустой PacketList; каждый
  task выполняет Reset, устанавливает RT/viewport/scissor, обходит batches,
  вызывает `mesh.Draw` и закрывает list; owner ждёт `JobGroup::Wait`, проверяет
  результаты и только затем пишет End и вызывает `ExecuteCommandLists`.
- `mesh.Draw` на каждом packet устанавливает PSO, root signature, topology,
  VB и CBV; list получает RT/viewport/scissor независимо.
- Порядок Execute: Begin → PacketList 0..N-1 → End. Flatten плана сохраняет
  исходный packet order.

### 2.7. Ошибки и lifetime

- Каждый worker пишет только в свой `results[i]`;
- `m_recordFailed` и диагностику пишет только owner;
- worker throw, Draw failure, Close failure и Submit exception приводят к
  `JobGroup::Wait`/drain и отказу от Execute;
- Execute выполняется только при полном наборе closed results и совпадении
  числа Draw с планом;
- `OrderedFrame12` и `FrameBatchPlan12` — локальные объекты `RenderFrame` и
  живут до Wait/Execute; worker'ы читают их как const;
- fence, resize, shutdown, quarantine и re-Initialize не менялись.

## 3. GPU-проверки

### 3.1. Сборка

Targeted `Engine.sln` ReleaseDX12|x64: **exit 0, 0 ошибок, 0 предупреждений**.

### 3.2. Новый packet runner

`tests/run-packet-multilist.ps1`: **34/34 PASS + WM_TIMER 2/2**.

| Проверка | Результат |
|---|---|
| L=1, parallel запрошен | owner-only, tasks=0, hash `9dd37d87b8eb202d`, 2 Draw |
| L=2, multi | Begin+2 lists+End, tasks=0, hash, 2 Draw |
| L=2, parallel, workers 1/2/4 | tasks=2, hash, 2 Draw, jobs clean |
| L=4/8 при двух объектах | plan lists=2, GPU lists=2, tasks=2, без fake Draw |
| Empty, L=4 | plan lists=0, tasks=0, background/depth oracle PASS, 0 Draw |
| One visible, L=4 | plan lists=1, tasks=0, 1 Draw |
| `-packetbatchlimit 1` | plan lists=1, batches=2, 2 Draw, hash |
| Prep serial/parallel/verify | одинаковый hash и DrawTotal |
| Resize | 3 успешных resize, пакетный путь работает |
| Re-Initialize после prep fault | PASS, packet config сохраняется |

### 3.3. Фактические workers/tasks

Для L=2 parallel:

| `-jobworkers` | backgroundWorkers | totalParticipants | packet tasks | DrawTotal |
|---:|---:|---:|---:|---:|
| 1 | 1 | 2 | 2 | 2 |
| 2 | 2 | 3 | 2 | 2 |
| 4 | 4 | 5 | 2 | 2 |

Для L=1 и empty frame: `tasks=0`, независимо от запрошенного parallel mode.
Это не performance-измерение: сцена содержит два объекта.

### 3.4. Fault matrix

`run-packet-multilist.ps1`: **11/11 fault cases PASS**.

| Stage | Lists | Проверено |
|---|---|---|
| allocator | 1,2 | оба task завершены, Execute не было |
| Reset | 1,2 | list не закрыт, кадр ABORTED |
| Draw/record | 1,2 | Draw не записан, кадр ABORTED |
| Close | 1,2 | Close failure, кадр ABORTED |
| interrupted Submit | 1 | `entered=1 exited=1`, второй task не стартовал |
| worker throw | 1,2 | `entered=2 exited=2`, failures=1, unobserved=0 |

Для каждого случая:

- `[recording] ABORTED mode=packets lists=4 frames=1`;
- `[recording-recovery] PASS ... submittedUnchanged=1`;
- все tasks завершены (`submitted=completed`);
- `unobserved=0`;
- повторный кадр после Shutdown → Initialize успешен;
- 0 validation WARNING/ERROR/CORRUPTION.

Дополнительно вручную проверены serial multi fault `record 1` и `close 2`:
оба дают ABORTED, recovery PASS и неизменный submission count.

### 3.5. Отклонение binding до submit

8/8 случаев с `-commandlists parallel -packetlists 4`:

- stale visual generation;
- unknown object;
- invalid vertex range;
- vertex range overflow;
- Transparent;
- HUD;
- unsupported PSO;
- non-identity world.

Все завершаются до `Reset/Execute` (`frames=0`), без validation
WARNING/ERROR/CORRUPTION.

### 3.6. Регрессии

- `run-packet-draw.ps1`: **22/22 PASS**;
- synthetic `run-multilist.ps1` (F3.5.4A): **31/31 + 4/4 WM_TIMER**;
- synthetic `run-parallel-recording.ps1` (F3.5.3B): **36/36 + 4/4 WM_TIMER**;
- F3.5.4B short benchmark: **19/19 valid**;
- CPU selftests: snapshot, visibility, ordering, prep, packet plan, CPU —
  **6/6 exit 0**;
- packet WM_TIMER: success и timer-fault paths — **2/2 PASS**;
- packet resize: `resizeSuccess=3`, 0 validation;
- packet re-Initialize после prep fault: PASS.

## 4. Ограничения и остаточные риски

1. Сцена содержит только два объекта. L=4/8 проверяют mapping/allocation и
   отсутствие fake lists, но **не** масштабирование. Максимум два непустых
   GPU draw lists.
2. Несколько batches в одном list проверены на owner L=1
   (`-packetbatchlimit 1`). С двумя объектами parallel list всегда содержит
   один packet, поэтому parallel-ветка с несколькими batches в одном list
   покрыта кодом и planner-тестами, но не отдельным GPU-сценарием.
3. Один PSO/root signature/CB и world-baked vertices. Смена PSO/material
   между batches и per-object CB не проверялись.
4. Transparent/HUD остаются CPU-категориями и отклоняются GPU-путём.
5. `JobGroup::Wait()` не имеет таймаута — прежний принятый риск.
6. При L=1 с `-commandlists parallel` JobSystem может быть инициализирован
   smoke-оболочкой, но packet-задач нет (`tasks=0`).
7. При plan lists меньше `-packetlists` неиспользуемые пары allocator/list
   выделены, но не пишутся и не попадают в Execute.
8. Debug Layer/GBV включены по умолчанию и влияют на timings; performance
   packet path не измерялся и не заявляется.
9. Реальный failed Signal/TDR/Unknown-quarantine не форсировался; ветки
   проверены по коду, как и в предыдущих этапах.
10. Это standalone Smoke, не реальный R4 traversal и не registry.

## 5. Изменённые файлы

- `ogsr_engine/Layers/xrRenderDX12/CHW12.h/.cpp` — packet lists, plan
  validation, `RecordPacketList`, L=1/empty owner path, parallel tasks,
  packet logging;
- `ogsr_engine/Layers/xrRenderDX12/xrDX12Smoke.cpp` — `-packetlists`,
  `-packetbatchlimit`, packet + multi/parallel, fault harness compatibility;
- `ogsr_engine/Layers/xrRenderDX12/tests/run-packet-draw.ps1` — обновлён
  invalid CLI case;
- `ogsr_engine/Layers/xrRenderDX12/tests/run-packet-multilist.ps1` — новый
  runner F3.5.5C;
- `ogsr_engine/Layers/xrRenderDX12/tests/run-wmtimer.ps1` — optional packet
  source для WM_TIMER.

Commit/push не выполнялись. AGENTS.md и пользовательские документы этой
правкой не изменялись; существовавшее пользовательское изменение AGENTS.md
сохранено.
