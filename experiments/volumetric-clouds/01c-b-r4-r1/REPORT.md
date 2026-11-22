# OGSR RTX — V-WEATHER-01C-B-R4-R1
## Interactive Multi-Frame Diagnostic Safety & Verified stderr Pipe Coverage

**Дата:** 2026-11-21
**Baseline:** rebuilt R4 evidence package
`OGSR_V_WEATHER_01C_B_R4_NonBlocking_Terminal_Corrections.zip`,
SHA-256 `bfc4bdd5d74da39226617a2093981e5819d207f7d6ae67d6aedda69a7d5151c8`.
**Вердикт автора:** **R4-R1 CANDIDATE — P2-A CLOSED (witness-proven),
P2-B CLOSED (per-stream), ИСТОРИЧЕСКИЙ VISUAL GATE 4/16 — NOT MET,
transition shader + GBV — NOT RUN / unresolved (с измерениями).**
**Следующий gate:** независимый read-only review → Tech Lead final decision.

Область работ ограничена двумя P2 из ревью R4, точностью fault activation,
исследованием transition-shader GBV и повторением регрессий. Production OGSR,
HLSL, DX11/R4, V-SF Rain и roadmap не изменялись; commit/push не выполнялись.

---

## 1. P2-A — интерактивная многокадровая диагностическая безопасность

### 1.1 Что было не так

`BeginFrame()` ожидал только fence **переиспользуемого back buffer**. При трёх
back buffer'ах это не доказывает завершение более поздних submissions, а сразу
после ожидания вызывались `FlushPendingPresentNotice()` и
`DrainDebugMessages()` — то есть диагностический sink мог быть вызван, пока
более поздняя GPU-работа ещё не подтверждена. Дополнительно `RunInteractive`
писал сообщения minimize/restore непосредственно после `Present`.

### 1.2 Что сделано

- **`Renderer::GpuIdleConfirmed()`** — неблокирующая проверка «fence догнал
  **последний** submission»: сравнение `GetCompletedValue()` с
  `lastSubmittedFence_`; sentinel `UINT64_MAX` (device removed) трактуется как
  «неизвестно», а не как «idle». `WaitForGpu()` на кадр **не добавлен**, overlap
  и несколько кадров in flight сохранены.
- **`Renderer::FlushDiagnosticsIfIdle()`** — единый гейт для отложенных
  present-notice и InfoQueue-drain. Пока работа не подтверждена, он только
  инкрементирует `diag_defer_count`/`inflight_at_defer` и **ничего не пишет**.
- **`BeginFrame()`** больше не выводит диагностику сразу после ожидания back
  buffer, а вызывает этот гейт.
- **`RunInteractive`**: minimize/restore, occlusion-notice, resize-подсказки,
  frame-limit и warmup-сообщения выводятся через фиксированный буфер
  отложенных сообщений (16×192, без аллокаций) и только при подтверждённом
  полном idle; перед выводом выполняется `TryWaitForGpu()` (терминальный
  fail-closed сохранён).
- **Witness** получил счётчики `diag_defer_count`, `diag_flush_count`,
  `diag_unsafe_count` (обязан оставаться 0), `inflight_at_defer`,
  `inflight_at_check`, `frame_submit_count`, `frame_confirm_count` и события
  `gpu-idle-confirmed` / `diagnostics-deferred` / `diagnostics-flushed`.

### 1.3 Адресный тест нескольких кадров in flight

Новые кейсы: `inflight_no_block`, `inflight_blocked_stdout`,
`inflight_blocked_stderr`, `inflight_blocked_both`.

Сценарий (реализован в `Application::RunInteractive` под
`OGSR_CLOUD_INFLIGHT_TEST=1`, fault-сборка):

1. Кадр 0 отправляется и подтверждается **реальным** full-idle wait.
2. Три реальные frame command list отправляются подряд, без ожиданий между
   ними; если GPU успевает их «съесть», отправляются ещё (до 3 попыток), а если
   очередь всё равно пуста — тест **падает**, а не «проходит» вхолостую.
3. Устанавливаются заполненные pipes (`stdout`/`stderr`/`both`) в точке
   `inflight-diagnostics`.
4. Проверяется, что более поздние submissions ещё не подтверждены
   (`inflight_at_check ≥ 1`), и что гейт отказался флашить.
5. Выполняется реальный цикл minimize/restore (`ShowWindow(SW_MINIMIZE)` →
   `SW_RESTORE`), при этом включается forced present-notice
   (`OGSR_CLOUD_FORCE_PRESENT_NOTICE=1`), чтобы задействовать путь отложенного
   occlusion-уведомления.
6. Потоки восстанавливаются, подтверждается idle, отложенные сообщения
   выводятся.

Результат по witness (все четыре кейса, `evidence/01c_b_r4_faults.json`):

| Кейс | exit | timeout | frame_submit | diag_defer | diag_flush | diag_unsafe | inflight defer/check | pipes |
|---|---:|---:|---:|---:|---:|---:|---|---|
| `inflight_no_block` | 0 | 0 | 9 | 6 | 3 | **0** | 2 / 2 | — |
| `inflight_blocked_stdout` | 0 | 0 | 9 | 6 | 3 | **0** | 2 / 2 | activated=1 |
| `inflight_blocked_stderr` | 0 | 0 | 9 | 6 | 3 | **0** | 2 / 2 | activated=1 |
| `inflight_blocked_both` | 0 | 0 | 9 | 6 | 3 | **0** | 2 / 2 | activated=1 |

То есть: **реальные submissions были в полёте** (2 неподтверждённых в момент
проверки), **гейт откладывал** диагностику (6 deferrals), **флаши происходили
только после подтверждённого idle** (3), и **ни один sink не был вызван при
незавершённой работе** (`diag_unsafe_count = 0`) — в том числе при полностью
заполненных stdout и/или stderr.

### 1.4 Покрытие путей, перечисленных в задании

| Путь | Как покрыт |
|---|---|
| `BeginFrame` / deferred InfoQueue drain | те же 4 inflight-кейса: drain проходит через гейт; `diag_defer > 0` |
| occlusion | forced present-notice в inflight-кейсах + тот же гейт (`DiagnosticNoticeDeferred`) |
| window minimize / restore | реальные `ShowWindow(SW_MINIMIZE)`/`SW_RESTORE` в inflight-кейсах |
| frame resize | `resize_renderer_post_submit/signal` (терминальные) + `run_01c_b_r4_runtime.ps1` (стадии `10_resize_960x540`, `11_resize_back`) |
| normal interactive shutdown | `inflight_*` завершают цикл и корректно выходят (exit 0); shutdown-пути — `shutdown_*_wait`, `blocked_both_shutdown_*` |

---

## 2. P2-B — доказательство перенаправления stdout и stderr

### 2.1 Что было не так

`SetStdHandle`, `_open_osfhandle`, `_dup2` не проверялись;
`blocked-pipes-activated` писался независимо от результата; единственный
sensitivity control писал только в stdout, поэтому в режиме `both` блокировка
stderr не была доказана.

### 2.2 Что сделано

- **`InstallStream()`** проверяет каждый шаг и возвращает код ошибки:
  1 = ошибка `SetStdHandle`, 2 = `_open_osfhandle`, 3 = `_dup2`.
- Ошибка установки **никогда** не объявляется активацией: пишется
  `blocked-pipes-failed`, а `blocked-pipes-activated` не эмитится.
- **Отдельный pipe-status shared memory** (`OGSR_CLOUD_PIPESTATUS_SHM`),
  создаваемый harness'ом **всегда**, независимо от witness. Он фиксирует
  `stdout/stderr_requested`, `stdout_ok/stderr_ok`, коды ошибок, `activated`,
  `probe_target`, `probe_started`, `deactivated` — и остаётся наблюдаемым даже
  когда witness отключён (`blocked_both_witness_failure`).
- **Отдельный sensitivity control на каждый поток**:
  `control_stdout_probe`, `control_stderr_probe`,
  `control_both_stdout_probe`, `control_both_stderr_probe`
  (`OGSR_CLOUD_BLOCK_PROBE_TARGET=stdout|stderr`). В режиме `both`
  выполняются два контроля, поэтому блокировка доказывается **для каждого**
  потока, а не только для первого записанного.
- **Негативный контроль установки** `pipe_setup_failure_no_activation`:
  harness намеренно передаёт невалидный handle (`--break-handle stdout`).
- **`DeactivateBlockedPipes()`** восстанавливает исходные handle/fd (без
  двойного закрытия), чтобы окно с заблокированными потоками было
  ограниченным.
- Timeout в обычных terminal-кейсах остаётся FAIL; ожидаемый timeout допустим
  только у явно промаркированных positive controls (5 кейсов).

### 2.3 Результаты (raw `pipe_status.json` + witness)

| Кейс | exit | timeout | pipe_status | вывод |
|---|---:|---:|---|---|
| `control_stdout_probe` | 0xDEAD | **1** | stdout_ok=1, activated=1, probe=1 | запись в полный stdout виснет |
| `control_stderr_probe` | 0xDEAD | **1** | stderr_ok=1, activated=1, probe=2 | **запись в полный stderr виснет** |
| `control_both_stdout_probe` | 0xDEAD | **1** | stdout_ok=1, stderr_ok=1, probe=1 | в режиме both оба потока установлены, проба по stdout |
| `control_both_stderr_probe` | 0xDEAD | **1** | stdout_ok=1, stderr_ok=1, probe=2 | в режиме both проба по **stderr** тоже виснет |
| `pipe_setup_failure_no_activation` | 1 | 0 | stdout_ok=**0**, activated=**0**, error=2 | невалидный handle не считается активацией |
| `control_blocked_pipe_probe` (legacy) | 0xDEAD | **1** | both ok, probe=1 | сохранён как общий контроль |

Дополнительно: у всех 15 blocked-pipe terminal-кейсов парсер требует
`pipe_status` с `activated=1` и `*_ok=1` для каждого запрошенного потока — то
есть свидетельство активации больше не сводится к одному witness-событию.

---

## 3. Точность fault activation

- `PreRetirementFaultPending()` больше **не** считает `LoggerFault::WaitError`
  ожидаемым: этот fault пишется только в уже провалившейся ветке wait, и его
  присутствие ранее включало pipes в вызове, который завершался успешно
  (именно это и вызывало ложный timeout в R4).
- `ShutdownFaultPending()` заменён на `ShutdownFaultPendingFor(point)`:
  каждая площадка активирует pipes только для **своего** fault point.
  `NoiseVolume` и `WeatherMap` своего shutdown-fault не имеют и больше не
  активируют pipes вообще.
- Добавлены `FaultArmedForPoint()` / `LoggerFaultArmedForPoint()` и проверки
  соответствия «fault point ↔ owner ↔ activation label».
- Предикаты **не** считаются доказательством GPU submission или активации
  pipes: submission доказывается `submit_count`/`last_submitted_list`, а
  активация — `pipe_status` каждого потока.

---

## 4. Матрица (58 кейсов, 0 отказов)

Механика: каждый кейс — отдельный дочерний процесс под
`OGSRCloudsTerminalHarness` с жёстким timeout (120 s, для inflight — 300 s).
Родитель заранее создаёт shared-memory witness и pipe-status block, при
необходимости подставляет уже заполненные pipes в координированной дочерней
точке. `timed_out=1` — FAIL, кроме 5 явно промаркированных positive controls.

| Категория | Кол-во | Результат |
|---|---:|---|
| Терминальные — всего (в это число **входят** 15 blocked-pipe) | 37 | все `exit 1`, witness `terminal-action` последним |
| ├ из них blocked-pipe терминальные | 15 | все `exit 1`, timeout 0, `pipe_status.activated=1` |
| └ из них прочие терминальные | 22 | все `exit 1` |
| Recoverable texture/probe | 5 | `exit 0`, состояние сохранено |
| Healthy (L4a/L5/L6/L7/probe-pre-retirement) | 5 | `exit 0` |
| Witness-disabled | 1 | `exit 1`, witness пуст, активация подтверждена pipe-status |
| Legacy pipe control | 1 | timeout (ожидаемый) |
| Per-stream pipe controls | 4 | timeout (ожидаемые), по одному на stdout/stderr ×2 режима |
| Pipe setup failure | 1 | `exit 1`, `activated=0` |
| Interactive multi-frame | 4 | `exit 0`, `unsafe=0` |
| **Итого уникальных кейсов** | **58** | **0 провалов** |

Внимание: строка «терминальные, всего» **включает** blocked-pipe-подмножество,
поэтому сумму строк брать нельзя. Уникальных кейсов ровно 58:
37 терминальных + 5 recoverable + 5 healthy + 1 witness-disabled +
1 legacy control + 4 pipe controls + 1 pipe-setup failure + 4 inflight.

---

## 5. Transition shader + GPU-based validation (отдельное исследование)

**Проведено на неизменённом R4-бинарнике** (developer,
`de32d5bcc7318c8d2851f60d66983da32b448a7c084150e80ff937e4043bfac2`), а не на
кандидате R4-R1: HLSL и код рендера в R4-R1 не менялись, поэтому результат
переносится, но это указано явно.

Симптом воспроизведён 1:1: **150.48 с**, убит, 69 строк лога, последняя —
`cloud render PSO selected: CloudRayMarchTransition.hlsl`.

Удалось разделить факторы (10 задокументированных экспериментов,
`evidence/gbv_transition_investigation`):

- **Кадр под GBV завершается**: минимальный конфиг — **770.36 с** и
  **771.83 с** (два независимых прогона), `debug errors=0 warnings=0`,
  `debug message totals: 0 errors, 0 warnings`.
- **Процесс не выходит**: после кадра shutdown застревает на
  `live-object messages emitted` (>223 с тишины), поэтому `code 0` в этой
  среде недостижим даже при таймауте 1200 с.
- **Deadlock опровергнут**: CPU занят на одном ядре практически полностью —
  процесс считает, а не ждёт fence (в пакете нет CPU-сэмплов, поэтому эта
  деталь помечена как авторское наблюдение, а не как воспроизводимый артефакт).
- **Не специфика NVIDIA**: тот же симптом на AMD Radeon 610M
  (`--adapter=1`, vendor 0x1002).
- **WARP + GBV невозможен**: приложение отклоняет комбинацию (`exit 2`),
  а GBV без debug layer не существует (включается через `ID3D12Debug1`).
- **TDR не при чём**: `TdrDelay=10`, событий Display/nvlddmkm/4101/4102/4103
  за 3 часа нет.

**Уточнение стоимости (исправлено после независимого аудита).** Флаги редукции
(`--steps=8 --shadow-steps=1 --jitter=off`) были переданы, но **не применились**:
по логу процесс работал с `steps=128 shadow_steps=6 jitter=spatial`
(см. `09_extreme_min_gbv_900s/run.log`, строки `options:` и
`cloud quality actual:`). Поэтому корректная удельная оценка считается по
фактической конфигурации:

```
160×100 = 16 000 px, steps=128  →  2 048 000 пиксель-шагов за 771.83 с
удельная стоимость ≈ 0.377 мс на пиксель-шаг
боевой конфиг 90 000 px × 128 шагов ≈ 11.52 M пиксель-шагов
  ≈ 4.3·10³ с ≈ 72 минуты на ОДИН кадр
```

То есть «часы на кадр» — завышение: измеренная оценка ≈ **72 мин/кадр**
(несколько кадров дают часы). Ранняя формулировка «≈6.0 мс на пиксель-шаг»
была получена из предположения о 8 шагах и **отозвана** как ошибочная.
Времена компиляции под GBV (300256 B → 15.6 с, 346480 B → 21.4 с) измерены
напрямую и не затронуты этой поправкой.

Debug-layer-only (без GBV) проходит: 47.32 с, `0 errors/0 warnings` — но это
**не** GBV и PASS'ом не считается.

**Статус: NOT RUN / unresolved.** Причина установлена и измерена; PASS по
transition shader под GBV **не** объявляется. План воспроизведения и сигнал
успеха — в `evidence/gbv_transition_investigation/README.md`.

---

## 6. Visual baseline

HLSL и художественные параметры не менялись: 34/34 файла шейдеров побайтово
идентичны baseline.

| Сравнение | Результат |
|---|---|
| R4-R1 против R2 control (одна сессия, те же параметры) | **16/16 побайтово идентичны, 0 mismatches** |
| Исторический 01C-A-R1 baseline (попиксельно) | **4 exact / 12 drifted / 188 changed channels / max 1 LSB / rain_sf 4/4** |

Статусы этих двух артефактов различаются и это не противоречие:
`01c_b_r4_static_identity.json` — побайтовое сравнение с историческим
baseline, его `result` = **FAIL** ( сходится только 4/16);
`01c_b_r4_historical_drift.json` — попиксельный разбор, его `result` = PASS
(разбор выполнен корректно), а `historical_gate` = **NOT MET**.
Итог один: **исторический visual gate НЕ ДОСТИГНУТ (FAIL / NOT MET)**.

Environment drift: наблюдаемый дрейф (12/16 изображений, 188 каналов, max
1 LSB) воспроизводится и на неизменённом R2-контроле в той же сессии, поэтому
он не является следствием R4/R4-R1; при этом дрейф **не** приписан конкретному
обновлению ОС/драйвера — данных для такого вывода нет.

Исторический gate **NOT MET**; дрейф не приписан конкретному обновлению
ОС/драйвера (для такого вывода нет данных). Требуется решение Tech Lead.

---

## 7. Регрессии

| Набор | Результат |
|---|---|
| CTest developer / shipping / fault | **4/4 PASS** в developer, shipping и fault (`100% tests passed out of 4` ×3) |
| CLI/config (108 сценариев) | **PASS** — `cases=108 failed=0` |
| Runtime state machine (800×450 fixed/animated, 1600×900) | **PASS** — 3 прогона × 13 стадий × 8 requested/applied transitions, 0 failures |
| D3D12 Debug Layer / GBV / InfoQueue | **PASS** — 5/5 кейсов, `d3d12_error_lines=0`; transition + GBV — NOT RUN (см. §5) |
| Shipping isolation (fault hooks) | fault-hook needles отсутствуют в developer/shipping EXE, присутствуют в fault EXE (10 needles) |
| Матрица 58 кейсов | **PASS** — 58 кейсов, 0 отказов, 5 ожидаемых timeout (только positive controls) |

---

## 8. Provenance

| Item | Value |
|---|---|
| R4 baseline zip | `bfc4bdd5d74da39226617a2093981e5819d207f7d6ae67d6aedda69a7d5151c8` |
| R4-R1 patch (R4 → R4-R1) | ``d101e9b4eff98ac586a0dd77d7fc9c359fb4bf2be47d2f07b1927a4140ccc7d1` (1209 строк)` |
| Patch apply check | byte-exact, `10` файлов, 0 mismatches |
| Developer EXE | `16add96bb284979298c05c1305343f4bfb17f5f575eefc71901645517fc56a84` |
| Shipping EXE | `886aabaa131f5fcc7e818b212446ccbdaf05c2bd93c1a623f84a2916b7042252` |
| Fault EXE | `3895a120dc47e34bce1d61b76c31c6bc77e9947fc3a0b3d01fef711793555f04` |
| Harness | `90cc51dc0bd4611e10a103c761e98b5473c8e04b13b4d67e50c5b453ebb5894b` |
| HLSL | 34/34 идентичны baseline |
| CTest | 4/4 × 3 конфигурации |

---

## 9. Evidence status

| Gate | Status |
|---|---|
| **SOURCE PROVEN** | **PASS** — 23 структурные проверки, единственная точка выхода `Common.cpp:187` |
| **CPU TESTED** | **PASS** — CTest 4/4 ×3, CLI 108/108, runtime 3×13×8 |
| **GPU INJECTION TESTED** | **PASS** — 58 кейсов, 0 отказов |
| **AUTHOR WINDOWS/GBV TESTED** | **PASS** для Debug Layer/GBV стандартных сценариев; transition + GBV — **NOT RUN / unresolved** |
| **INDEPENDENTLY VERIFIED** | **NOT RUN** — независимого запуска не было; verifier пакета — авторский |
| **NOT RUN** | transition shader под GBV (reason установлена); реальный driver OOM/device removal/hot-unplug; game DX12 integration; DX11/R4; изменение HLSL; V-SF Rain; weather transitions; OD1%; 01C-C; V-L; V-S1; V-Q4 |

---

## 10. Ограничения и остаточные риски

1. Standalone-контракт: game adapter/UI/CEnvironment отсутствуют.
2. Реальный OOM/hot-unplug не имитировался.
3. Интерактивный тест искусственно создаёт окно «несколько кадров in flight»
   дополнительными submissions, а не реальной нагрузкой пользователя;
   production-путь не содержит тестовых хуков (изоляция проверена сканом EXE).
4. В blocked-pipe окне pipes восстанавливаются сразу после проверки: постоянно
   заполненный pipe законно блокирует post-idle flush, и это не является
   нарушением инварианта.
5. `max_abs_delta` исторического дрейфа = 1 LSB; причина дрейфа не установлена.
6. Transition + GBV: измерена стоимость, но не найден способ получить код 0 в
   этой среде; нужен прогон на другой конфигурации либо решение Tech Lead.
7. В ходе работы были инциденты (конкуренция за GPU, внешний taskkill, ошибка
   путей в скриптах); все они раскрыты в `WORKSPACE_MANIFEST.md`, а затронутые
   прогоны пересняты на свободном GPU.

---

## 11. STOP

R4-R1 остановлен здесь. 01C-C, V-L, V-S1, V-Q4, оптимизация шейдеров и
production integration не начаты. OGSR production, DX11/R4, HLSL, Original
V-SF Rain, OD1% default и weather transitions не изменялись. Git commit/push
не выполнялись; roadmap не менялся. Следующий gate — независимый read-only
review и решение Tech Lead.
