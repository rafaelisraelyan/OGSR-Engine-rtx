# F3.5.5A — Packet-Driven DX12 Recording

**Дата:** 2026-10-20
**Baseline:** `98a1e0fc5` (F3.5.4B)
**Статус:** реализовано и проверено; commit/push не выполнялись
**Новый режим:** `-drawsource packets` (по умолчанию остаётся `synthetic`)

---

## 1. Границы этапа

F3.5.5A подключает `OrderedFrame12` к реальной D3D12-записи Draw в
standalone Smoke:

```
Snapshot → PrepareVisible → PrepareOrdered → packet binding → один owner list
```

Не входят в этап:

- parallel batch recording;
- batch planner;
- per-object constant buffer и world transform;
- material/PSO registry, textures, descriptor tables;
- Transparent/HUD GPU-категории;
- реальный R4 traversal.

## 2. Реализация

### 2.1. Источник Draw

`CHW12::DrawSource`:

- `Synthetic` — прежний путь F3.5.4A/B: `m_drawCount`, `%2`, synthetic cubes;
- `Packets` — `PrepareOrdered()` и `DrawPacket12` как единственный источник
  `firstVertex/vertexCount`.

Packet-путь включается только явно. При `-drawsource packets` запрещены:

- `-commandlists multi/parallel` — используется один owner list;
- `-draws` и `-lists` — synthetic benchmark load не смешивается с packets;
- `-recordfault` — fault harness относится к synthetic recording.

Невалидная комбинация отклоняется до `Initialize`.

### 2.2. Подготовка кадра

Добавлен согласованный захват `view/projection`:

```cpp
SmokeCamera12::CaptureViewProjectionPair(view, projection)
```

`RenderSceneSnapshot12(view, projection, objects, generation)` пересчитывает
VP тем же порядком, что и прежний `CaptureViewProjection()`, поэтому
двухкубовый hash не меняется.

Packet-путь вызывает `PrepareOrdered()` с текущим `m_prepMode`
(`serial/parallel/verify`). Visibility compaction и ordering выполняются до
любых GPU-команд.

### 2.3. Binding двух объектов

Smoke-local binding выводится из существующих `m_mesh.Objects()`; глобального
resource registry нет. Для каждого packet проверяется:

- `frame.generation == m_sceneGeneration`;
- только непустой диапазон `Opaque` и отсутствие Transparent/HUD;
- packet видим;
- object ID существует в текущем mesh binding;
- `firstVertex/vertexCount` совпадают с диапазоном этого объекта;
- `visual/material/pso (id, generation)` совпадают с binding;
- `vertexCount != 0`, `firstVertex <= 72`,
  `vertexCount <= 72 - firstVertex` (без сложения, переполнение исключено);
- flags не содержат `Hud/Transparent`;
- `opacity == 1`;
- world-матрица identity, потому что вершины world-baked, а shader
  использует только ViewProjection.

Любое нарушение — `[packet-draw] FAIL: <reason>` до `Reset/Execute`, кадр
помечается `m_prepFailed/m_recordFailed`, GPU не получает частичную работу.

### 2.4. Draw

В packet-ветке один command list:

```cpp
for (const DrawPacket12& packet : ordered.frame.packets)
    m_mesh.Draw(cmd, cbVA, packet.object.firstVertex, packet.object.vertexCount);
```

`m_drawCount` и `%2` в этой ветке не используются. CB по-прежнему один на
кадр: `ordered.frame.shaderCamera`.

Пустой `OrderedFrame12` — корректный кадр: clear render target/depth, ноль
Draw, обычный Present. Это не ошибка.

### 2.5. Lifetime

`OrderedFrame12` объявлен локально в `RenderFrame` и живёт до завершения
`Reset → record → Close → ExecuteCommandLists`. GPU не читает packet-память:
command lists, allocator, CB и GPU-ресурсы принадлежат `FrameResource` и
освобождаются только по существующему fence-инварианту слота.

### 2.6. Test-only faults

`-packetfault` детерминированно портит копию `m_mesh.Objects()` до snapshot:

`hideall`, `hidesecond`, `cullsecond`, `reverse`, `stalevisual`,
`unsupportedpso`, `unknownobject`, `invalidrange`, `overflowrange`,
`transparent`, `hud`, `worldtransform`.

Это не traversal и не registry; хуки существуют только для проверки
packet validation.

## 3. Проверки

### 3.1. Сборка

Targeted `Engine.sln` ReleaseDX12: **exit 0, 0 ошибок, 0 предупреждений**.

### 3.2. Packet runner

`tests/run-packet-draw.ps1`: **22/22 PASS**.

| Проверка | Результат |
|---|---|
| Two-cube baseline | exit 0, hash `9dd37d87b8eb202d`, `visible=2 draws=2` |
| Reverse snapshot order | тот же hash, `visible=2 draws=2` |
| Empty visible frame | exit 0, `visible=0 draws=0`, background, `PASS` |
| Empty background pixel/depth oracle | весь кадр = clear color, depth 4×4 = 1.0, `PASS` |
| One visible (flag) | exit 0, `visible=1 draws=1` |
| One visible (frustum) | exit 0, `visible=1 draws=1` |
| `prep=parallel/verify` | hash и packet counts совпадают |
| Transparent / HUD | reject до submit, `frames=0`, 0 validation |
| Unsupported PSO | reject до submit, `frames=0` |
| World transform | reject: `non-identity world is unsupported` |
| Unknown object ID | reject: `unknown object id` |
| Stale visual generation | reject в `PrepareOrdered`, `frames=0` |
| Invalid vertex range | reject binding, `frames=0` |
| Vertex overflow | reject в `PrepareOrdered`, `frames=0` |
| Resize | `resizeSuccess=3`, 0 validation |
| `-prepfault` recovery | `[prep-recovery] PASS`, packet path работает после re-init |
| Invalid CLI mix | все четыре комбинации отклонены явно |

Во всех успешных packet-прогонах:

- `[recording-draws] total == expected == visiblePackets`;
- `[packet-draw] visible == draws`;
- `[recording] PASS mode=packets lists=1`;
- 0 validation WARNING/ERROR/CORRUPTION.

### 3.3. Регрессия

- `run-parallel-recording.ps1`: **36/36 + 4/4** WM_TIMER;
- известное наблюдение: промежуточный прогон того же runner один раз дал
  transient WM_TIMER count-flake (`WM_ENTERSIZEMOVE` посчитан дважды).
  Это не исправлялось и не маскируется: чистый повторный прогон прошёл
  36/36 + 4/4, packet path WM_TIMER не изменяет;
- F3.5.4B short benchmark: **19/19 valid**;
- `-snapshotselftest`, `-visibilityselftest`, `-orderingselftest`,
  `-prepselftest`, `-cpuselftest`: exit 0;
- synthetic `parallel -depthtest`: hash `9dd37d87b8eb202d`, 0 validation.

## 4. Что подтверждено, а что остаётся прототипом

**Подтверждено:**

- `OrderedFrame12` реально управляет D3D12 Draw-командами;
- packet-путь даёт byte-identical RGBA hash для двух кубов;
- DrawTotal равен числу видимых packets;
- empty/one/two visible frames обрабатываются корректно;
- empty frame проверен pixel-oracle: весь кадр равен clear color, а
  центральный depth 4×4 равен 1.0;
- unknown/stale/unsupported binding, invalid range, overflow и
  неподдерживаемые категории отклоняются до Execute;
- packet-путь переживает resize, shutdown и re-initialize;
- synthetic F3.5.4A/B путь не изменился при default `synthetic`.

**Остаётся прототипом:**

- только два синтетических объекта из `m_mesh.Objects()`, один VB, один PSO,
  один root signature и один CB;
- binding выводится из smoke-local mesh, а не из render-owned registry;
- world-baked вершины; per-object transform/CB не поддерживаются и явно
  отклоняются;
- Transparent/HUD/material/texture пути отсутствуют;
- нет batch planner и parallel packet recording;
- test-only faults не являются реальным traversal/registry.

## 5. Ограничения

- Packet count ограничен текущим mesh; это не representative X-Ray сцена.
- Один PSO/CB означает, что смена материалов и descriptor tables не
  проверена.
- Empty frame проверен полностью по цвету кадра и по 4×4 depth-региону в
  центре; полный depth readback не добавлялся.
- Packet timing (`[renderprep]`) включает visibility+ordering, но не
  разделяет их по фазам в этом этапе.
- GPU timestamps остаются frame-level, не per-packet.

## 6. Изменённые файлы

- `dx12camera.h/.cpp` — `CaptureViewProjectionPair`.
- `CHW12.h/.cpp` — `DrawSource`, packet validation/binding, packet draw,
  test-only faults, packet logging, background pixel/depth oracle.
- `xrDX12Smoke.cpp` — `-drawsource`, `-packetfault`, `-backgroundtest`,
  CLI-совместимость.
- `tests/run-packet-draw.ps1` — 22 packet-path проверки.

DX11/R4, JobSystem API, ownership GPU-ресурсов и реальная игровая интеграция
не изменялись.
