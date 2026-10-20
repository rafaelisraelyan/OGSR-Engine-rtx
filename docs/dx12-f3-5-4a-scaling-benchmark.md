# F3.5.4A — Scalable Recording Benchmark

**Дата:** 2026-10-20
**Ветка:** `prypiat`
**Base HEAD:** `53b82c5b7` (F3.5.3B)
**Статус:** реализовано, собрано, проверено; коммит не создавался

---

## 1. Аудит владения

До F3.5.4A `FrameResource` слота владел фиксированным набором:

```
commandAllocator / commandList   — owner Begin
extra[0]                         — Draw 0
extra[1]                         — Draw 1
extra[2]                         — owner End
```

Проблемы для масштабирования:

- число draw lists жёстко равно двум;
- `extra[]` смешивает draw lists и End-список;
- `quarantine` хранит `extraAllocators[3]` / `extraLists[3]`;
- fault harness адресует только list 0–3.

Инварианты, которые требовалось сохранить:

- каждая list пишется ровно одним исполнителем;
- allocator переиспользуется только после fence своего слота;
- owner не трогает allocator/list задачи до `Wait`;
- при неизвестном состоянии GPU ресурсы уходят в карантин.

## 2. Минимальное расширение

Добавлен bounded массив и отдельный End:

```cpp
static constexpr uint32_t MAX_DRAW_LISTS = 8;  // = kMaxDrawLists
RecordingResource drawRecording[MAX_DRAW_LISTS];
RecordingResource ownerEnd;
uint32_t drawListCount = 0;
```

`dx12quarantine.h` получил `drawAllocators[8]`, `drawLists[8]`, `endAllocator`,
`endList`. Архитектура владения не менялась: по-прежнему три слота кадра,
у каждого свой набор пар, общий пул не вводился.

## 3. Параметры

| Параметр | Значения | По умолчанию |
|---|---|---|
| `-draws N` | 1 … 1 000 000 | 2 |
| `-lists L` | 1, 2, 4, 8 | 2 |
| `-commandlists` | `legacy`, `multi`, `parallel` | `legacy` |
| `-nogbv` | отключает GBV до создания device | выключен |

`L` ограничен `N` и `MAX_DRAW_LISTS`. Конфигурация `L > N` отклоняется
явно (`empty draw lists are not allowed`), а не маскируется clamp'ом.
При `L = 1` режим `parallel` вырождается в sequential multi с одним draw
list; лог пишет фактический режим, а не запрошенный. `-depthtest` требует
`N >= 2`, потому что фиксированный oracle проверяет оба куба.

## 4. Реализация

- Draw calls делятся на L непрерывных диапазонов:
  `first = N * i / L`, `last = N * (i + 1) / L`.
- Глобальный порядок всегда `0 … N-1`, поэтому итоговое изображение не
  зависит от режима и числа списков.
- Каждый draw рисует один из двух кубов в существующем vertex buffer:
  `firstVertex = (index % 2) * 36`, `vertexCount = 36`. Это сохраняет
  одинаковый набор команд и одинаковую GPU-работу.
- Legacy: один список (`Begin + N Draw + End`).
- Multi: `Begin`, L draw lists, `End` — запись последовательная.
- Parallel: те же L+2 списка, draw lists пишутся задачами GR JobSystem.
- Порядок `ExecuteCommandLists`: `Begin → Draw 0 … Draw L-1 → End`.
- Все списки закрываются до `ExecuteCommandLists`; при неполном результате
  отправка не выполняется.
- Добавлен счётчик интервала между `Present` (`[frame] interval=…`), отдельно
  от CPU recording и GPU timestamp.
- Диагностика `[recording-draws]` печатает сумму фактически записанных Draw
  на первом кадре; несовпадение с `N` — отказ кадра. Посписковые значения
  печатаются только в test mode.
- Интервалы не смешиваются: `recording` — wall clock до отправки,
  `scheduling`/`wait` — части этого интервала, `submission` — только
  `ExecuteCommandLists`, GPU — query timestamps, frame — QPC после `Present`.
- GBV включается только при запросе: `SetGpuValidation(false)` до
  `Initialize()`. Лог печатает фактическое значение.

## 5. Проверки

### 5.1. Сборка

Targeted `Engine.sln` ReleaseDX12: **exit 0, 0 ошибок, 0 предупреждений**.

### 5.2. Hash parity

27 конфигураций: draws `128/512/2048` × modes `legacy/multi/parallel` ×
lists `1/2/4/8` (legacy — один раз на draws).

Во всех случаях:

- `exit 0`;
- RGBA hash `9dd37d87b8eb202d`;
- 0 `validation WARNING/ERROR/CORRUPTION`.

### 5.3. Fault injection

`draws=128`, `lists=4`, stages 0–5, lists `0,1,4,5` (begin, draw, draw, end),
workers `1` и `4`: **40/40 pass**.

Для каждого случая: `ABORTED`, `submittedUnchanged=1`,
два `GPU drained before shutdown`, 0 validation-сообщений.

### 5.4. Resize и повторная инициализация

- `-resizetest -cputest 60` с `L=4`, `draws=128`: `resizeSuccess=3`,
  переходы `960x540 → 1280x720 → 1024x768`, 0 validation-сообщений.
- `-prepfault` с `L=4`: `failure → shutdown → initialize → frame`, exit 0.

### 5.5. Регрессия

- `run-parallel-recording.ps1`: 36/36 + 4/4 WM_TIMER.
- `-cpuselftest`: exit 0.

### 5.6. Финальные гейты ревью

**L = 8.**

- `-resizetest -cputest 60`, `draws=128`, `jobworkers=4`: `resizeSuccess=3`,
  каждый rebuild пишет `listsPerSlot=10`, 0 validation, GPU drained.
- Fault на последнем draw list (`list=8`), stages `2` и `3`, workers `1` и
  `4`: **4/4 PASS**, `entered=8 exited=8 overlap=1`, `closed=7/8`,
  `submittedUnchanged=1`, два `GPU drained`.

**Границы конфигурации.**

| Конфигурация | Результат |
|---|---|
| `N=1, L=1` legacy/multi/parallel | exit 0, `[recording-draws] total=1 expected=1` |
| `N=4, L=4` multi/parallel | hash `9dd37d87b8eb202d`, `total=4` |
| `N=8, L=8` multi/parallel | hash `9dd37d87b8eb202d`, `total=8`, по 1 Draw на list |
| `N=4, L=8` | отклонено: `empty draw lists are not allowed` |
| `-depthtest` при `N=1` | отклонено: oracle требует два куба |

**Quarantine.** Путь `Unknown` не форсировался: для этого нужен реальный
отказ GPU-drain, а инъекция device loss в этом этапе не добавлялась.
Проверено структурно: `static_assert(MAX_DRAW_LISTS == kMaxDrawLists)`,
массивы карантина имеют тот же предел, обход ограничен
`j < drawListCount && j < kMaxDrawLists`, End-пара детачится отдельно,
лог печатает `drawListsPerSlot` и `endLists`.

**Повторная финальная регрессия.** 27/27 hash+diag, runner 36/36 + 4/4,
`-cpuselftest` exit 0, targeted build 0 ошибок / 0 предупреждений.

## 6. Первые замеры

200 кадров, 170 измеряемых, GBV выключен (`-nogbv`), окно скрыто.
Величины в мс: recording / scheduling / Wait / submission / GPU / frame.

### draws = 128

| Режим | lists | recording | sched | Wait | subm | GPU | frame |
|---|---|---|---|---|---|---|---|
| legacy | 1 | 0.640 | — | — | 0.012 | 0.091 | 3.29 |
| multi | 2 | 0.769 | — | — | 0.013 | 0.091 | 3.29 |
| multi | 4 | 0.853 | — | — | 0.015 | 0.091 | 3.28 |
| multi | 8 | 0.917 | — | — | 0.015 | 0.090 | 3.29 |
| parallel | 2 | 0.673 | 0.016 | 0.438 | 0.017 | 0.091 | 3.30 |
| parallel | 4 | 0.623 | 0.017 | 0.384 | 0.019 | 0.091 | 3.29 |
| parallel | 8 | 0.705 | 0.020 | 0.447 | 0.021 | 0.091 | 3.29 |

### draws = 2048

| Режим | lists | recording | sched | Wait | subm | GPU | frame |
|---|---|---|---|---|---|---|---|
| legacy | 1 | 5.326 | — | — | 0.175 | 1.214 | 5.66 |
| multi | 2 | 5.339 | — | — | 0.180 | 1.221 | 5.68 |
| multi | 4 | 5.393 | — | — | 0.181 | 1.222 | 5.73 |
| multi | 8 | 5.517 | — | — | 0.204 | 1.228 | 5.93 |
| parallel | 2 | 3.690 | 0.011 | 3.589 | 0.230 | 1.197 | 4.06 |
| parallel | 4 | 2.874 | 0.014 | 2.663 | 0.099 | 1.199 | 3.33 |
| parallel | 8 | 2.979 | 0.015 | 2.790 | 0.110 | 1.202 | 3.31 |

Остаточный wall-clock интервал (recording − scheduling − Wait) для
`draws=2048, L=4`: `2.874 − 0.014 − 2.663 = 0.197 мс`. Это не чистое
CPU-время worker'ов: в интервал входят owner-часть записи и накладные
расходы, а задачи выполняются параллельно.

Контрольный повтор после финальных правок (`draws=2048`, GBV off):
legacy `recording=5.355`, `submission=0.166`, `GPU=1.212`, `frame=5.693`;
parallel `L=8` `recording=2.736`, `sched=0.015`, `Wait=2.485`,
`submission=0.078`, `GPU=1.180`, `frame=3.293`. Сумма Draw — `2048/2048`
в обоих прогонах.

Наблюдения:

- При 128 draw calls parallel не быстрее legacy: накладные расходы
  сопоставимы с полезной работой.
- При 2048 draw calls parallel `L=4` даёт ~1.9× меньше recording, чем
  multi `L=4`; основная доля — `JobGroup::Wait`.
- GPU время почти не зависит от режима (1.20–1.23 мс), что подтверждает
  эквивалентность GPU-работы.
- `frame interval` при скрытом окне не ограничен vsync и отражает темп
  CPU-отправки, а не FPS игры.

## 7. Ограничения

- Сцена — повторная отрисовка двух кубов; это синтетическая нагрузка, а не
  игровой кадр.
- Один прогон на конфигурацию, без медианы/IQR и повторов.
- Окно скрыто, Present не ждёт vsync; `frame interval` нельзя сравнивать с
  игровым frametime.
- Debug Layer включён всегда; GBV выключен только в замерах.
- Повторная отрисовка того же куба может отсекаться ранним depth-тестом;
  GPU-время поэтому растёт слабее, чем число draw calls.
- TDR/device removal не форсировались; quarantine-путь проверен структурно,
  а не исполнением.
- `-depthtest` применим только к `N >= 2`; для `N = 1` проверяется счётчик
  записанных Draw, а не фиксированный двухкубовый oracle.
- `JobGroup::Wait()` без таймаута — прежний принятый риск.

## 8. Изменённые файлы

- `CHW12.h`, `CHW12.cpp` — bounded draw lists, параметры N/L, frame interval,
  GBV switch.
- `dx12quarantine.h` — массивы draw lists + End.
- `xrDX12Smoke.cpp` — `-draws`, `-lists`, `-nogbv`, расширенный fault-диапазон.

DX11/R4, JobSystem API и ownership GPU-ресурсов не изменялись.
