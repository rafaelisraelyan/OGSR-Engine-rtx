# 01C-B-R4-R1 workspace / run manifest

**Task ID / Run ID:** `V-WEATHER-01C-B-R4-R1` / `01c-b-r4-r1-interactive-diagnostics`
**Агент и владелец:** DeepSeek V4.1 Flash (OpenCode); owner: user
**Дата и время запуска:** 2026-11-21 12:00 +03:00
**Baseline:** rebuilt R4 evidence package
`OGSR_V_WEATHER_01C_B_R4_NonBlocking_Terminal_Corrections.zip`,
SHA-256 `bfc4bdd5d74da39226617a2093981e5819d207f7d6ae67d6aedda69a7d5151c8`;
baseline source tree `01c_b_r4_work\standalone_01c_b_r4` не изменялся.
**Git:** commit/push не выполнялись; roadmap не менялся.

## Рабочий каталог и изоляция

- Task root: `C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work`
- R4-R1 candidate source: `01c_b_r4_r1_work\standalone_01c_b_r4_r1`
  (isolated copy of the rebuilt R4 tree + R4-R1 corrections)
- R4 baseline tree и его evidence package: только чтение.
- Production/repository sources не менялись.

## Build paths

- Fault-injection + harness: `01c_b_r4_r1_work\build_fault`
- Developer: `01c_b_r4_r1_work\build_developer`
- Shipping-controls: `01c_b_r4_r1_work\build_shipping`
- Generator: Visual Studio 17 2022, x64, Release.
- Toolchain: MSVC 14.44.35207, CMake 4.4.3, fxc 10.0.26100.7705.
- Команда: `build_canonical.ps1` (пишет `cmake_*.log`, `build_*.log`,
  `ctest_*.log`).

## Что изменилось относительно R4 (только два P2 + точность fault activation)

### P2-A — интерактивная многокадровая диагностическая безопасность

Проблема: `BeginFrame()` ждал fence только переиспользуемого back buffer.
С тремя back buffer'ами это не доказывает завершение более поздних submissions,
а сразу после ожидания вызывались `FlushPendingPresentNotice()` и
`DrainDebugMessages()`. Также `RunInteractive` писал сообщения minimize/restore
сразу после `Present`.

Исправлено:

- `Renderer::GpuIdleConfirmed()` — неблокирующая проверка «очередь догнала
  **последний** submission» (сравнение `GetCompletedValue()` с
  `lastSubmittedFence_`, sentinel `UINT64_MAX` трактуется как «неизвестно»);
- `Renderer::FlushDiagnosticsIfIdle()` — единый гейт для отложенных
  present-notice и InfoQueue-drain: пока работа не подтверждена, он **только
  регистрирует deferral** и ничего не пишет; никакого `WaitForGpu()` на кадр
  не добавлено, overlap сохранён;
- `BeginFrame()` больше не выводит диагностику сразу после ожидания back buffer,
  а вызывает этот гейт;
- сообщения minimize/restore, occlusion-notice, resize-подсказки и frame-limit
  идут через фиксированный буфер отложенных сообщений (16×192, без аллокаций)
  и выводятся только при подтверждённом полном idle;
- сохранён terminal fail-closed при неизвестном completion;
- witness получил счётчики `diag_defer_count`, `diag_flush_count`,
  `diag_unsafe_count` (обязан остаться 0), `inflight_at_defer`,
  `inflight_at_check`, `frame_submit_count` и события
  `gpu-idle-confirmed` / `diagnostics-deferred` / `diagnostics-flushed`.

### P2-B — доказательство перенаправления каждого потока

Проблема: `SetStdHandle`/`_open_osfhandle`/`_dup2` не проверялись,
`blocked-pipes-activated` писался независимо от результата, а единственный
sensitivity control писал только в stdout.

Исправлено:

- `InstallStream()` проверяет каждый шаг (`SetStdHandle`, `_open_osfhandle`,
  `_dup2`) и возвращает код ошибки; ошибка установки пишется в pipe-status и в
  witness (`blocked-pipes-failed`), а **не** в `blocked-pipes-activated`;
- отдельный, всегда создаваемый harness'ом **pipe-status shared memory**
  (`OGSR_CLOUD_PIPESTATUS_SHM`), независимый от witness: фиксирует
  `stdout/stderr_requested`, `stdout_ok/stderr_ok`, коды ошибок, `activated`,
  `probe_target`, `probe_started`, `deactivated`. Это даёт
  parent-observable подтверждение активации и для witness-disabled кейса;
- отдельный sensitivity control на **каждый** поток:
  `control_stdout_probe`, `control_stderr_probe`,
  `control_both_stdout_probe`, `control_both_stderr_probe`
  (`OGSR_CLOUD_BLOCK_PROBE_TARGET=stdout|stderr`), поэтому в режиме `both`
  блокировка доказывается для обоих потоков, а не только для первого;
- негативный контроль `pipe_setup_failure_no_activation`: harness намеренно
  передаёт невалидный handle (`--break-handle stdout`), установка обязана
  провалиться, активация не должна быть объявлена;
- `DeactivateBlockedPipes()` восстанавливает исходные handle/fd, чтобы окно с
  заблокированными потоками было ограниченным, без двойного закрытия.

### P2-A тест (новые кейсы)

`inflight_no_block`, `inflight_blocked_stdout`, `inflight_blocked_stderr`,
`inflight_blocked_both`: кадр 0 подтверждается реальным full-idle wait, затем
**три реальные frame command list** отправляются подряд без ожиданий, после
чего проверяется, что более поздние submissions ещё не подтверждены
(`inflight_at_check ≥ 1`), что гейт отказался флашить (`diag_defer_count ≥ 1`,
`diag_unsafe_count = 0`), что реальный minimize/restore прошёл, и что после
подтверждённого idle отложенные сообщения выведены (`diag_flush_count ≥ 1`).

### Точность fault activation

- `PreRetirementFaultPending()` больше **не** считает `LoggerFault::WaitError`
  «ожидаемым»: этот fault пишется только в уже провалившейся ветке wait, и его
  присутствие раньше включало pipe в вызове, который завершался успешно.
- `ShutdownFaultPending()` заменён на `ShutdownFaultPendingFor(point)`:
  каждая площадка shutdown активирует pipe только для **своего** fault point.
  `NoiseVolume`/`WeatherMap` не имеют своего shutdown-fault и больше не
  активируют pipe вообще.
- Добавлены `FaultArmedForPoint()` / `LoggerFaultArmedForPoint()` и проверки
  соответствия «fault point ↔ owner ↔ activation label».

### Transition shader + GBV

Проведено отдельное исследование (10 задокументированных экспериментов):
симптом воспроизведён 1:1 (150.48 с), но **кадр под GBV завершается** —
770.36 с и 771.83 с (повтор), `debug errors=0 warnings=0`. Процесс не выходит
из-за стоимости shutdown-фазы (live-object report), поэтому код 0 в этой среде
недостижим. Deadlock опровергнут (CPU 96–100 %, кадр завершается), TDR не
срабатывал, WARP+GBV запрещён приложением, на AMD-адаптере симптом тот же.
Итог: **NOT RUN — unresolved**, с измерениями и планом воспроизведения
(`evidence/gbv_transition_investigation`).

## Инциденты прогонов (раскрытие)

1. **Конкуренция за GPU.** Первый запуск матрицы и два параллельных
   GPU-набора выполнялись одновременно на одном ноутбуке. Из-за этого
   `texture_2` завершился с кодом 1, harness не смог запустить `probe_buffer`,
   а static-набор дал `capture exit -1`. Оба «сбойных» кейса при повторном
   одиночном прогоне дали `exit 0 / harness 0` (`evidence/recheck` (включён в пакет)). Поэтому
   **все итоговые прогоны выполнены на свободном GPU, последовательно**;
   параллельные результаты отброшены.
2. **Внешний `taskkill`.** Один из субагентов по ошибке выполнил
   `taskkill /F /IM OGSRCloudsTest.exe`, что могло завершить чужие дочерние
   процессы. Все кейсы, выполненные до этого момента, пересняты в финальном
   прогоне; в отчётности используются только финальные данные.
3. **Ошибка путей.** Первая версия скриптов после копирования всё ещё
   указывала на `01c_b_r4_work`, из-за чего один прогон матрицы и один
   static-набор фактически работали на R4-бинарниках. Это обнаружено,
   пути исправлены (`01c_b_r4_r1_work`, `standalone_01c_b_r4_r1`), после чего
   **все** наборы пересняты заново. Данные, полученные до исправления,
   отброшены.

## Артефакты

- Evidence: `01c_b_r4_r1_work\evidence`
- Patch (byte-exact R4 → R4-R1): `01c_b_r4_r1_work\patch`
- Package: `01c_b_r4_r1_work\package`
- Report: `01c_b_r4_r1_work\report`
- ZIP: `01c_b_r4_r1_work\OGSR_V_WEATHER_01C_B_R4_R1_Interactive_Diagnostic_Corrections.zip`;
  авторитетный хэш — в sidecar `.zip.sha256`.

## Статус хранения

**Active → Permanent после приёмки R4-R1.** Не удалять без согласования
владельца.
