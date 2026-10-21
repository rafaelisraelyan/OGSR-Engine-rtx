# F3.5.6B — устранение замечаний независимого аудита

Дата: 2026-10-21. База: `5b025aaa97d6ef9aa64ce2a28b9f445583b3a4f9`, ветка `prypiat`.
Commit/push не выполнялись. Существующие пользовательские изменения, включая
`AGENTS.md` и ранее созданные документы, сохранены.

## Изменения

### M1: независимые точки отказа Submit

В существующем диагностическом пути `CHW12::RenderFrame` номер отказа теперь
сравнивается с порядковым номером Submit, а не с константой «второй Submit».
Инъекция остаётся перед вызовом GR; публичный API и реализация JobSystem не меняются.
Owner считает принятые Submit только после успешного возврата. После Wait проверяется
`IsComplete`; диагностика drain требует равенства accepted/entered/completed.

Регрессии проверяют:

- Submit 1: accepted=0, completed=0, groupComplete=1, closed=0/2.
- Submit 2: accepted=1, completed=1, groupComplete=1, closed=1/2.
- В обоих случаях `ABORTED drained=1`, `submittedUnchanged=1`, затем успешные
  Shutdown/Initialize и контрольный depth hash.
- Такие проверки добавлены в parallel и packet multilist runners.

Это инъекция отказа **до** Submit, а не аппаратный отказ scheduler. Backend rejection
и отказ регистрации задачи отдельно покрываются GR-тестами с `GR_JOBS_TESTING`.
При исключении owner выполняет Wait до выхода из области заимствованных resources;
проверка failure/closedCount остаётся перед ExecuteCommandLists.

### L1: timestamp Map failure

Внутренний `dx12timing12::Consume` снимает pending/sample/index до чтения readback.
Отказ Map не публикует GPU sample и не оставляет индекс старого кадра. Каждый новый
submission также начинает с index=-1, включая исчерпание массива CPU samples.
Map/Unmap остаются в CHW12; helper не получает D3D12 pointers и не меняет синхронизацию.

Добавлены 12 проверок в fence/timing suite (37 → 49): управляемый отказ read adapter,
заполненные 512 samples, неизменность всего массива после отказа и следующего
непривязанного результата, корректное обновление отдельного индекса, отсутствие
двойного потребления, нулевая частота и обратный интервал ticks.
Тест использует тот же helper, что production, но не вызывает реальный отказ COM Map.

### Метрики

Benchmark runner экспортирует p99 по кадрам каждого запуска, p99 медиан независимых
запусков и медиану per-run p99 раздельно. Это разные распределения; они не объединены
в якобы общий frame p99. Шесть тестов проверяют фактические функции runner, включая
интерполяцию p99=99.01 на 1..100, пустую выборку, singleton и CSV.
Scheduling/Wait не прибавляются повторно к recording wall-clock.

Отдельная сборка с `GR_RECORDING_TRACE` записывает begin/end каждого callback в его
собственную ячейку; owner читает их после Wait. Нет rendezvous/sleep и новых общих
записей workers. Обычная ReleaseDX12 сборка не содержит этой диагностики и восстановлена
после измерений. Script требует явный путь к диагностическому exe.

## Проверки

- ReleaseDX12|x64: сборка Smoke и его зависимостей успешна; обычный бинарник восстановлен.
  Первоначальный sandbox linker LNK1101 исчез при том же MSBuild вне sandbox;
  параметры проекта ради этого не менялись.
- CPU + ASan: PASS, exit 0, без сообщений ASan. Prep 510, snapshot 566486,
  visibility 1519828, ordering 4892, fence/timing 49, transforms 540,
  geometry 245 проверок; packet planner PASS.
- GR JobSystem: 34 теста, 430 проверок, 0 ошибок.
- Parallel recording: 36 сценариев и 4 WM_TIMER — PASS.
- Packet multilist: 35 сценариев и 2 WM_TIMER — PASS.
- Geometry: 16 сценариев — PASS, включая resize, reinitialize, post-submit failure.
- GPU debug layer/validation включены в correctness runners; проверены Serial,
  Parallel, Verify, ошибки recording и drain. Контрольные hashes сохранены:
  два куба `9dd37d87b8eb202d`, geometry `0ea8788795a5950d`.
- Повторно запущен независимый настоящий fence oracle:
  `first=Timeout stale=SpuriousWake actual=10 requested=13 retry=Completed final=13`.
  Ранее собранный CPU oracle: matrix=216, checks=795, PASS; его исходники и код fence/GR
  в этой правке не менялись. Это повторный запуск существующего harness, не новая сборка.
- Статистика runner: 6 проверок PASS; PowerShell parse и scoped git diff --check PASS.
- End-to-end экспорт p99: 19/19 валидных запусков, 0 invalid/underfilled; проверены
  поля per-run, between-run и within-run JSON. Параметры: Runs=1, WarmupFrames=30,
  MeasuredFrames=12, SkipCorrectness (correctness выполнен отдельными runners выше).
  Это проверка pipeline/экспорта, не статистически достаточное исследование tail latency.
  Для CIM fingerprint потребовался запуск runner вне sandbox.
- После восстановления обычной сборки повторены legacy/multi/parallel depth и parallel
  geometry: 4/4 PASS, оба hash совпали, GBV включён, trace-маркеры отсутствуют.

Артефакты: `x64/f356b-hardening/`, `x64/f353b-results/`,
`x64/f352b-asan/xrDX12Smoke.log`; каталоги результатов игнорируются Git.

## Natural overlap

Два списка, 2048 синтетических Draw, 60 кадров; первые 30 отброшены. На каждой настройке
1/2/4/8 requested workers наблюдалось перекрытие callbacks в 30/30 измеренных кадров.
Средняя длительность пересечения соответственно 451.343 / 308.550 / 225.390 / 297.620 мкс;
максимальная 1255.400 / 1398.200 / 1071.600 / 1170.500 мкс.

Это интервалы жизни callbacks, а не сумма worker durations и не доказательство
одновременного исполнения инструкций на разных ядрах. Owner может участвовать в GR;
один requested worker не означает одного участника. Измерение содержит накладные
расходы часов/диагностики; ускорение или прирост FPS по нему не заявляются.

## GPU lifetime и ограничения

Callers CollectGpuTiming остаются прежними: RenderFrame после подтверждённого slot
fence, Resize после успешного drain, Shutdown только при WorkCompleted. Helper не
превращает неудачное ожидание в WorkCompleted и не выполняет Map без разрешения caller.
Пути failed Signal, Unknown, Release и quarantine не менялись. Новые интервалы trace
живут на стеке owner до Wait; callbacks пишут разные элементы results.

Ручной визуальный осмотр и race detector не выполнялись. ASan и GPU validation не
доказывают отсутствие всех гонок. Не форсировались TDR/device removal, реальный failed
Signal/WAIT_FAILED или отказ драйверного Map; для таких ошибок сохраняются ограничения
независимого аудита. Произвольные конкурентные вызовы CHW12 не поддерживаются данным
owner-thread контрактом. DX11/R4, игровой traversal, registry и GPU fences не изменены.

Исправлены только M1/L1 и запрошенная диагностика. Изменения оставлены для ревью,
без расширения на следующий milestone.

## Состав diff

- `CHW12.cpp`: M1, L1, диагностическая запись интервалов под `GR_RECORDING_TRACE`.
- `dx12timing.h`: внутренний helper снятия timestamp binding.
- `dx12fencewait_tests.cpp`: регрессия L1.
- `xrDX12Smoke.vcxproj`: включение нового header.
- `tests/run-parallel-recording.ps1`, `tests/run-packet-multilist.ps1`: assertions M1.
- `tests/run-scaling-benchmark.ps1`, `tests/run-benchmark-stats.ps1`: p99 и тесты.
- `tests/run-recording-overlap.ps1`: воспроизводимый диагностический запуск.
- Этот отчёт. Полный patch: `x64/f356b-hardening/hardening.patch` (включает новые файлы,
  не включает пользовательские изменения и build artifacts).
