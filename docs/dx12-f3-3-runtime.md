# F3.3 — DX12 CPU render preparation: runtime report

Дата: 2026-10-19. Ветка: `prypiat`.
База: `09ca0235708a04ed17751354cea03e125e8fcc79` (F3.2C) + незакоммиченная работа
F3.3.0 (Codex) + исправления F3.3.0a (Hermes).

## 1. Что проверялось

`RenderSceneSnapshot12 → CPU Preparation → PreparedFrame12 → DX12 Command Recording → GPU`.

Граница между игровой сценой и многопоточным рендерером — неизменяемый
`RenderSceneSnapshot12`, владеющий копиями камеры и объектов. `Prepare()` принимает
snapshot, живое поколение, режим и заимствованный `gr::jobs::JobSystem*`, возвращает
`PreparedFrame12` по значению: при ошибке частичный кадр не публикуется.

Режимы: `Serial` (по умолчанию), `Parallel`, `Verify`. `Verify` выполняет оба пути и
сравнивает результат через `Equivalent()`; расхождение — исключение.

## 2. Изменения F3.3.0a

### Д1 — merge измеряется и публикуется

`Timings12::merge` вычислялся и отбрасывался. Добавлен `m_mergeMs` в `CHW12` и поле
`merge=%.6f` в итоговый лог. Гейт F3.3.0 требовал измерять merge отдельно — теперь
выполняется.

### Д2 — ожидание GPU fence измеряется отдельно

`RenderFrame` замеряет `WaitForFenceValue` отдельно от CPU `JobGroup::Wait`, поле
`gpuFence=%.6f` усредняется по числу ожиданий (первый кадр слота fence не ждёт).
Lifecycle fence не менялся — добавлен только замер.

### Д3 — WM_TIMER обрабатывает `RenderFrame()==false`

Раньше результат игнорировался: таймер продолжал срабатывать без единого кадра и без
диагностики. Теперь при `false`: диагностика (`device removed=`, `preparation failed=`),
латч `g_timerFrameFailed`, `g_running=false`, `KillTimer`, `PostMessage(WM_CLOSE)`.
Renderer из callback не разрушается — `DestroyWindow` происходит в message loop.
Код возврата процесса становится `1`.

### П1 — resize с реальным изменением аспекта

Прежние два размера (`960x540`, `1280x720`) сохраняли 16:9, поэтому путь
`CHW12::SetAspect` после resize не проверялся ни одним прогоном. Добавлен третий
resize на `1024x768` (aspect 1.3333 против 1.7778). Набор проверяет
`Resized to 1024x768` и `resizeSuccess=3`.

### П2 — WM_TIMER-тест с подтверждением отрисовки

Новый `tests/run-wmtimer.ps1`. Путь `WM_ENTERSIZEMOVE → реальный 16 мс таймер →
WM_EXITSIZEMOVE → серия WM_TIMER → WM_CLOSE`. Проверяет, что кадры **действительно
отрисованы** (`timerFrames >= 40`), что счётчик изолирует таймерный путь
(`timerFrames < rendered`, главный цикл — busy loop), 0 validation-ошибок, 0
`[renderprep] FAIL`, 0 `RenderFrame failed`, exit 0. `timerFrames=` добавлен в строку
`[runtime]`.

### Дополнительно: тест набора не запускался

`tests/run-renderprep.ps1` не парсился через `powershell -File`: файл был UTF-8 без
BOM с кириллицей внутри строкового литерала. Проверено отдельно: содержимое до
правок F3.3.0a тоже не парсилось, то есть это дефект F3.3.0. Исправлено UTF-8 BOM
на обоих скриптах.

### Дополнительно: test-only fault injection для Д3

`CHW12::ArmTimerRenderFailure()` / `CHW12::RenderFrameFromTimer()`. Инъекция видна
только WM_TIMER-пути и реализована отдельной точкой входа, а не проверкой флага внутри
`RenderFrame()`, чтобы главный цикл (busy loop) физически не мог её израсходовать
раньше таймера. При инъекции возвращается только `false`: device, fence, ресурсы и
JobSystem не затрагиваются, реальный device removal/TDR не провоцируется. Production-
путь вызывает `RenderFrame()` и флаг не читает.

## 3. Проверки

| Проверка | Результат |
|---|---|
| Сборка `ReleaseDX12\|x64` | 0 ошибок, 0 предупреждений |
| `-prepselftest` | 510 проверок / 0 ошибок |
| `-cpuselftest` | 857 проверок / 0 ошибок |
| Набор `run-renderprep.ps1` | 17/17 PASS |
| Регрессия `run-cpu-runtime.ps1` (F3.2C.2) | 32/32 PASS |
| `[depth-frame] hash` (serial/parallel/verify) | `9dd37d87b8eb202d`, без изменений |
| Resize | `960x540 → 1280x720 → 1024x768`, `resizeSuccess=3`, 0 mismatches |
| WM_TIMER (parallel/verify) | `timerFrames=77/76`, `rendered=953/926` |
| WM_TIMER fault (parallel/verify) | exit `1`, `timerFrames=0`, 1 диагностика сбоя |
| Validation WARNING/ERROR/CORRUPTION | 0 во всех прогонах |

### Путь отказа Д3 (подтверждён инъекцией)

```
[diag] WM_TIMER: RenderFrame failed, device removed=0, preparation failed=0; stopping timer rendering
[diag] WM_CLOSE
[diag] WM_DESTROY
[renderprep] PASS
[DX12] GPU drained before shutdown
[DX12][mesh] released
[DX12] Shutdown complete
[jobs] stopped: ... failures=0 unobserved=0 participants=0
[smoke] exit, code=1
```

Подтверждено: таймер прекращает работу; повторная отрисовка не выполняется
(`timerFrames=0`); процесс завершается с ненулевым кодом; GPU и JobSystem
завершаются безопасно; реальный device removal не вызывался
(`GetDeviceRemovedReason` отсутствует в логе).

### Замеры (средние за кадр, мс)

| Режим | snapshot | serial | parallel | submit | wait | merge | gpuFence |
|---|---|---|---|---|---|---|---|
| Serial | 0.0007 | 0.0014 | — | — | — | 0.00004 | 0.0019 |
| Parallel (4w) | 0.0006 | — | 0.0179 | 0.0138 | 0.0029 | 0.00004 | 0.0009 |
| Verify (4w) | 0.0006 | 0.0013 | 0.0170 | 0.0133 | 0.0025 | 0.00008 | 0.0018 |

Два объекта: parallel медленнее serial, что ожидаемо и за ускорение не выдаётся.
Замеры — синтетическая CPU-нагрузка на тестовой сцене, не FPS.

## 4. Ограничения

- Ручной визуальный осмотр не выполнялся.
- Race detector (TSan) не запускался; отсутствие гонок не доказано. Проверено
  код-ревью и результатами ASan F3.2C.1 для GR Job System.
- Culling диагностический: `visible` не исключает `Draw`.
- Kernel требует world-матрицу с нулевым переносом и `_44 == 1`
  (`dx12renderprep.cpp`). Для двух кубов с identity выполняется; перед игровым
  адаптером ограничение надо снять.
- `m_prepFailed` не сбрасывается в `Shutdown`: повторный `Initialize` после сбоя
  подготовки останется неисправным. В smoke повторная инициализация не используется.
- Проверка stale generation в текущем однопоточном producer'е недостижима: snapshot
  создаётся и потребляется внутри одного `RenderFrame`. Покрыта юнит-тестом.
- Игровой `IRenderable`/material registry не реализованы: это отдельный milestone.

## 5. Следующий этап

F3.5.0 — архитектурный ADR многопоточного DX12 renderer'а (карта потоков кадра,
frame N/N+1, fencing/lifetime инварианты). Затем F3.5.1–F3.5.4 по плану в
`docs/08-roadmap.md`. Игровой scene adapter и параллельная запись command lists на
этом этапе не начинаются.
