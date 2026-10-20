# F3.5.5B — Deterministic Packet Batch Planner

**Дата:** 2026-10-20
**Baseline:** `aeb6a674e` (F3.5.5A)
**Статус:** CPU-only planner и selftests реализованы; commit/push не выполнялись
**Интеграция в CHW12 RenderFrame:** отсутствует (по границам этапа)

---

## 1. Назначение

Планировщик превращает immutable `OrderedFrame12` в детерминированный план
непрерывных packet-диапазонов:

```
OrderedFrame12 (владеет packets)
        ↓ BuildPacketBatchPlan
FrameBatchPlan12 (владеет только индексами/диапазонами)
```

План не копирует packets, не владеет GPU-ресурсами и не зависит от D3D12,
GPU registry, JobSystem или потокового scheduler.

## 2. API

`dx12packetplan.h`:

```cpp
struct PacketBatch12 {
    uint32_t firstPacket;
    uint32_t packetCount;
    DrawCategory12 category;
};

struct PacketList12 {
    uint32_t firstBatch;
    uint32_t batchCount;
};

struct FrameBatchPlan12 {
    std::vector<PacketBatch12> batches;
    std::vector<PacketList12> lists;
    uint32_t packetCount;
};

FrameBatchPlan12 BuildPacketBatchPlan(
    const OrderedFrame12& frame,
    uint32_t maxLists,
    uint32_t maxPacketsPerBatch);
```

- `maxLists` — 1..8; `0` и `>8` отклоняются.
- `maxPacketsPerBatch=0` — без дополнительного ограничения размера.
- `Build` принимает `const OrderedFrame12&` и не изменяет вход.
- При ошибке конфигурации/category ranges — `std::invalid_argument`,
  при превышении `uint32` — `std::length_error`. Allocation failure
  распространяется как исключение; частичный план не возвращается.
- `ValidatePacketBatchPlan` проверяет инварианты, `Equivalent` сравнивает
  поля (без `memcmp`).

## 3. Алгоритм

1. Проверить category ranges:
   - `begin/count` в пределах `packetCount`;
   - ranges contiguous и покрывают `0..N`;
   - empty ranges допустимы.
2. `L = min(maxLists, N)`; при `N=0` план пустой.
3. Для каждого list:
   - границы `first = floor(N*i/L)`, `last = floor(N*(i+1)/L)`;
   - использовать 64-bit intermediate;
   - пустые lists не создавать.
4. Внутри list резать batches:
   - на смене `DrawCategory12`;
   - на смене resource key `(pso, material, visual)`;
   - при достижении `maxPacketsPerBatch` (если не 0).
5. Один list может содержать несколько batches; batches и lists идут в
   глобальном порядке packets.

## 4. Инварианты

- `plan.packetCount == frame.packets.size()`.
- `sum(batch.packetCount) == N`; `sum(list.batchCount) == batches.size()`.
- Batch-диапазоны contiguous, без gaps/overlap.
- List-диапазоны contiguous и покрывают все batches.
- Category не убывает: Opaque → Transparent → HUD.
- Внутри batch одинаковы category и resource key.
- `N=0` → пустые `lists`/`batches`.
- `N>0` → нет пустых lists/batches; `lists.size() <= 8`.
- Overflow проверяется до аллокации/публикации.

## 5. Тесты

### 5.1. CPU selftest

`xrDX12Smoke.exe -packetplanselftest`:

- sizes `0,1,2,7,8,9,17,256,4097`;
- maxLists `1,2,4,8`; invalid `0,9`;
- maxPacketsPerBatch `0,1,2,64,1024`;
- category sets: only opaque/transparent/HUD, opaque+transparent, all three,
  empty middle;
- resource-key transitions;
- coverage/no gaps/no overlap/flatten `0..N-1`;
- validation of every generated plan;
- determinism: 100 повторов;
- immutability of input IDs/resources/category ranges;
- invalid category gaps/overflow;
- intentionally corrupted plans: packet gap, overlap, invalid packet index,
  wrong category, invalid list range, empty list/batch, list overflow,
  missing batch; `ValidatePacketBatchPlan` reject each;
- allocation/overflow: plan is returned only by value after successful
  construction; invalid ranges and overflow throw before publication, and
  allocation failure propagates without a partial plan;
- single-list vs multi-list flattened sequence;
- empty plan.

Результат: **PASS**.

### 5.2. ASan

Отдельная CPU-only ASan-сборка planner + tests, без D3D12/JobSystem:

```
cl /std:c++20 /EHsc /Zi /fsanitize=address ...
```

Результат: **PASS**, ASan errors не обнаружены. ASan не подключён к
production-проекту; это отдельная проверка модуля.

### 5.3. Сборка и регрессия

- targeted ReleaseDX12: **0 ошибок, 0 предупреждений**;
- `-packetplanselftest`: exit 0;
- `run-packet-draw.ps1`: **22/22 PASS**;
- `run-parallel-recording.ps1`: **36/36 + 4/4**;
- F3.5.4B short benchmark: **19/19 valid**;
- CPU/snapshot/visibility/ordering/prep selftests: exit 0.

## 6. Что сознательно не делалось

- planner не подключён к `CHW12::RenderFrame`;
- нет GPU parallel recording и batch→command-list mapping;
- нет load balancing по стоимости packets;
- нет нового PSO/material/resource registry;
- Transparent/HUD остаются CPU-категориями; действующий packet path
  по-прежнему отклоняет их;
- DX11/R4, JobSystem API, shader, root signature, renderer ownership не
  изменялись.

## 7. Ограничения и риски

- Разбиение по количеству packets детерминировано, но не учитывает
  стоимость Draw/state changes.
- Границы list могут разрезать category; batches внутри list всё равно
  режутся по category, порядок сохраняется.
- Resource-key cuts используют только `(pso, material, visual)` IDs;
  генерация/валидность этих ID остаётся вне planner.
- `maxPacketsPerBatch` — верхняя граница, а не цель балансировки.
- Для будущей GPU-интеграции нужно отобразить `PacketList12` на
  `MAX_DRAW_LISTS=8`; это F3.5.5C, не этот этап.
- ASan-проверка покрывает planner/tests, но не GPU/resource lifetime.

## 8. Изменённые файлы

- `ogsr_engine/Layers/xrRenderDX12/dx12packetplan.h`
- `ogsr_engine/Layers/xrRenderDX12/dx12packetplan.cpp`
- `ogsr_engine/Layers/xrRenderDX12/dx12packetplan_tests.cpp`
- `xrRender_R5_DX12.vcxproj` — добавлены planner/tests/header.
- `xrDX12Smoke.cpp` — `-packetplanselftest`.

Commit/push не выполнялись.
