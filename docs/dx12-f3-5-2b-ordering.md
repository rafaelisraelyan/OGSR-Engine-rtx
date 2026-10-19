# F3.5.2B — Deterministic Draw Ordering

19 октября 2026 года. Ветка prypiat, исходный HEAD
`d6592f973d8ace8383870530babda9f21faae5cb`. Финализация включена в один локальный
коммит F3.5.2B; push не выполняется.

## Scope и API

Только representative CPU-сцена, отдельный `PrepareOrdered()` и
`-orderingselftest`. GPU продолжает вызывать прежний `Prepare()`; два Draw,
geometry, fences, CHW12, DX11/R4 и command recording не изменены.
Нет игрового traversal, registry или нового worker pool внутри кадра.

Snapshot получает новый конструктор `(view, projection, objects, generation)`.
Он копирует immutable view и сам вычисляет VP=view*projection; shader VP —
транспонированная копия. Неконечные view/projection, не-affine view и overflow VP
отклоняются. Старый VP-only конструктор сохраняется для прежних потребителей;
`HasView()==false` запрещает только ordering. Таким образом, ordering не может
случайно использовать identity view с чужим VP. Snapshot не сохраняет ссылки
на исходную камеру. Источник обязан захватывать view/projection на согласованной
границе потока-владельца; concurrent mutation исходной камеры не поддерживается.

`OrderedFrame12` содержит прежний `PreparedFrame12` и три `PacketRange12`
(begin/count): Opaque, Transparent, HUD. Диапазоны непрерывны и покрывают весь
массив; пустая категория имеет count=0 и begin, равный концу предыдущей.
Equivalent сравнивает диапазоны и все поля packets/кадра.

## Алгоритм

1. Выполнить прежнюю CPU visibility/compaction, не публикуя промежуточный кадр.
2. Линейно восстановить исходный индекс snapshot из стабильного списка видимых ID.
3. Построить ключи: Serial последовательно; Parallel через JobGroup/Wait с grain=128.
4. Последовательно выполнить std::sort по строгому компаратору.
5. Последовательно собрать новый массив packets и category ranges; вернуть только
   полностью готовый результат. Verify выполняет оба пути и сравнивает их.

Приоритет категории: HUD (включая Transparent|HUD) определяется первым,
затем Transparent, затем Opaque. Итоговый порядок категорий — Opaque → Transparent → HUD.

Opaque: PSO `(id,generation)` → Material `(id,generation)` → Geometry
`(id,generation)` → object ID → исходный индекс. В этом прототипе geometry handle
представлен `Object12::visual`; vertex ranges остаются неизменными metadata.

Transparent: убывание view-space z центра **мировой AABB**, затем object ID,
затем исходный индекс. Центр и глубина вычисляются в double из конечных float
bounds и affine view. Используется view, не projected/NDC depth. NaN/Inf
отклоняются до comparator; -0 нормализуется в +0. Epsilon не используется.

HUD: только исходный индекс snapshot, без группировки по ресурсам или глубине.
Сортировка не обязана быть stable_sort: для разных допустимых packets ключ
имеет полный tie-breaker. Snapshot уже запрещает повторяющиеся object ID;
fallback исходного индекса отдельно проверен на внутренних ключах.

## Lifetime, ошибки и числовые границы

CPU workers читают immutable snapshot/visible packets, пишут в независимые keys[i].
Память keys выделена до submit. Группа генерации ключей дренируется до sort,
уничтожения keys/callback или возврата исключения. Ошибка любой стадии не меняет
ранее опубликованный OrderedFrame12. После sort/result allocation ошибки также
не публикуют частичный кадр; локальные векторы уничтожаются через RAII.

Количество packets и индексы ограничены UINT32_MAX через общий checked helper.
Проверены UINT32_MAX и UINT32_MAX+1 без выделения гигантской сцены; счётчики
категорий проходят ту же проверку. Сумма range counts вычисляется в size_t x64
и сверяется с количеством packets. Индексы источника валидируются при mapping.

Тестовые hooks расширены только под GR_JOBS_TESTING: исключение после записи
ключа, после std::sort, после первой записи ordered packet, плюс прежние counts/
scatter. 40 сценариев: пять стадий × Parallel/Verify × 1/2/4/8 workers, сцена
1024 объектов. Проверяются факт отказа, drain callback и задач, отсутствие
unobserved errors, неизменность прошлого кадра и успешный повторный запуск.
В production сборке hooks отсутствуют; scheduler/enkiTS не менялись.

## Тесты

- ReleaseDX12|x64 Rebuild xrDX12Smoke с зависимостями и финальный incremental build — PASS.
- Ordering CPU: **4612 проверок, 0 ошибок**.
- Прежние CPU: visibility **1519476**, snapshot **566486**, renderprep **510**, PASS.
- ASan (включая JobSystem/enkiTS из исходников): ordering **4892**, visibility
  **1519828**, snapshot **566486**, renderprep **510**, 0 ошибок, exit 0.
  Дополнительные 280 ordering-проверок — 40 сценариев fault injection по семь проверок.
- GPU runner: **17/17**, WM_TIMER **4/4**, validation без warning/error/corruption.
  Depth Serial/Parallel/Verify: **9dd37d87b8eb202d**. Resize включает 1024x768.
  WM_TIMER: по 77 успешных timer frames; fault-сценарии ожидаемо exit 1,
  GPU drained, submitted=completed (106/108), participants=0.
- F3.2C.2: **32/32**, PASS.

Ожидаемые порядки маленькой сцены заданы списками ID вручную, без production
компаратора. Проверяются все metadata через сравнение с исходными packets.
Базовый порядок: 5,30,40,50,70 / 20,60,80 / 90,10. После разворота камеры
transparent становится 80,20,60; при взгляде вдоль другой оси равные глубины
снова разрешаются по ID. Проверены пустая сцена/категории, все hidden,
transparent HUD, смещённые local bounds с negative scale, signed zero, stale
поколения всех трёх ресурсов, отсутствующая view, неверные camera/depth,
scene generation, режим и неработающий JobSystem. Shutdown/reinitialize покрыты.

Чистый comparator отдельно проверяет поколения каждой resource pair, object ID/
source tie-break, иррефлексивность, асимметрию и транзитивность. Таблица synthetic
resources остаётся прежней; два поколения одного ID одновременно проверяются
на внутренних ключах, а stale resource в реальном snapshot вызывает ошибку.
Некорректная double depth проверяется непосредственно в production validation
helper; при конечных float bounds/view промежуточное double вычисление не может
достичь double overflow. Полноценный registry не моделируется.

## Замеры

Одна локальная серия: 20 прогревочных + 100 измеряемых повторений; порядок S/P
чередуется. Snapshot захвачен заранее и неизменен. Workers — фоновые потоки,
вызывающий поток также может помогать. Все значения — средние ms.

`visibility` — полный вызов PrepareVisible с cleanup его временных массивов.
`keyGeneration` — выделение keys, линейное восстановление исходных индексов,
генерация ключей и submit/Wait в Parallel. `ordering` — последовательный sort,
выделение и заполнение результата, category ranges. `total` — от начала run до
готового результата. Интервалы не перекрываются; submit/Wait повторно не суммируются.
Snapshot capture, Verify comparison, освобождение локальных keys/visible после
замера и уничтожение возвращённого кадра не входят в total. ASan/GPU-регрессии
запускались после этой серии; их timings для оценки производительности не используются.

На 16384 объектах при 8 workers: total Serial/Parallel **20.260240 / 6.069471 ms**,
keyGeneration **0.119246 / 0.176412 ms**, ordering **2.154277 / 2.252670 ms**.
Ускорение total относится прежде всего к предшествующей visibility-подготовке;
параллельная генерация дешёвых ключей в этой серии не ускорилась. Это синтетическая
CPU-сцена, не прирост FPS. Сцену нельзя прямо сравнивать с F3.5.2A: здесь другая
геометрия/камера и намного большая доля видимых packets.

Все комбинации:

```text
objects=256 workers=1 mode=serial samples=100 visibility=0.303874 keyGeneration=0.003665 ordering=0.026335 total=0.333935 ms
objects=256 workers=1 mode=parallel samples=100 visibility=0.280842 keyGeneration=0.008564 ordering=0.033880 total=0.323396 ms
objects=1024 workers=1 mode=serial samples=100 visibility=1.154878 keyGeneration=0.018216 ordering=0.083669 total=1.256829 ms
objects=1024 workers=1 mode=parallel samples=100 visibility=0.913683 keyGeneration=0.031001 ordering=0.126179 total=1.070975 ms
objects=4096 workers=1 mode=serial samples=100 visibility=4.490407 keyGeneration=0.056112 ordering=0.435148 total=4.981743 ms
objects=4096 workers=1 mode=parallel samples=100 visibility=2.923899 keyGeneration=0.099559 ordering=0.640771 total=3.664412 ms
objects=16384 workers=1 mode=serial samples=100 visibility=18.015196 keyGeneration=0.153985 ordering=2.159154 total=20.328433 ms
objects=16384 workers=1 mode=parallel samples=100 visibility=10.682564 keyGeneration=0.287633 ordering=2.379544 total=13.349935 ms
objects=256 workers=2 mode=serial samples=100 visibility=0.315888 keyGeneration=0.005639 ordering=0.015954 total=0.337552 ms
objects=256 workers=2 mode=parallel samples=100 visibility=0.278123 keyGeneration=0.014790 ordering=0.028601 total=0.321632 ms
objects=1024 workers=2 mode=serial samples=100 visibility=1.143493 keyGeneration=0.015654 ordering=0.077427 total=1.236646 ms
objects=1024 workers=2 mode=parallel samples=100 visibility=0.658453 keyGeneration=0.028470 ordering=0.116529 total=0.803584 ms
objects=4096 workers=2 mode=serial samples=100 visibility=4.476628 keyGeneration=0.044580 ordering=0.430347 total=4.951636 ms
objects=4096 workers=2 mode=parallel samples=100 visibility=2.137684 keyGeneration=0.084996 ordering=0.647138 total=2.869992 ms
objects=16384 workers=2 mode=serial samples=100 visibility=18.016429 keyGeneration=0.153368 ordering=2.154008 total=20.323920 ms
objects=16384 workers=2 mode=parallel samples=100 visibility=7.893377 keyGeneration=0.226676 ordering=2.303427 total=10.423663 ms
objects=256 workers=4 mode=serial samples=100 visibility=0.290052 keyGeneration=0.003602 ordering=0.012969 total=0.306698 ms
objects=256 workers=4 mode=parallel samples=100 visibility=0.213494 keyGeneration=0.008904 ordering=0.018751 total=0.241249 ms
objects=1024 workers=4 mode=serial samples=100 visibility=1.102027 keyGeneration=0.011134 ordering=0.068926 total=1.182169 ms
objects=1024 workers=4 mode=parallel samples=100 visibility=0.492977 keyGeneration=0.021093 ordering=0.113520 total=0.627697 ms
objects=4096 workers=4 mode=serial samples=100 visibility=4.460367 keyGeneration=0.028021 ordering=0.419873 total=4.908344 ms
objects=4096 workers=4 mode=parallel samples=100 visibility=1.396479 keyGeneration=0.049434 ordering=0.533148 total=1.979192 ms
objects=16384 workers=4 mode=serial samples=100 visibility=18.007835 keyGeneration=0.155209 ordering=2.170950 total=20.334107 ms
objects=16384 workers=4 mode=parallel samples=100 visibility=5.394521 keyGeneration=0.171557 ordering=2.245378 total=7.811609 ms
objects=256 workers=8 mode=serial samples=100 visibility=0.280310 keyGeneration=0.001713 ordering=0.012783 total=0.294892 ms
objects=256 workers=8 mode=parallel samples=100 visibility=0.202865 keyGeneration=0.012677 ordering=0.019956 total=0.235594 ms
objects=1024 workers=8 mode=serial samples=100 visibility=1.096154 keyGeneration=0.007472 ordering=0.072844 total=1.176545 ms
objects=1024 workers=8 mode=parallel samples=100 visibility=0.415921 keyGeneration=0.028100 ordering=0.129174 total=0.573326 ms
objects=4096 workers=8 mode=serial samples=100 visibility=4.490700 keyGeneration=0.029298 ordering=0.420676 total=4.940757 ms
objects=4096 workers=8 mode=parallel samples=100 visibility=1.098517 keyGeneration=0.071526 ordering=0.588911 total=1.759125 ms
objects=16384 workers=8 mode=serial samples=100 visibility=17.986618 keyGeneration=0.119246 ordering=2.154277 total=20.260240 ms
objects=16384 workers=8 mode=parallel samples=100 visibility=3.640224 keyGeneration=0.176412 ordering=2.252670 total=6.069471 ms
```

## Воспроизведение

Из x64 Native Tools Command Prompt VS 2022 в корне:

```bat
MSBuild.exe Engine.sln /t:xrDX12Smoke:Rebuild /p:Configuration=ReleaseDX12 /p:Platform=x64 /m:1 /nr:false
ogsr_engine\Layers\xrRenderDX12\tests\run-visibility-asan.cmd
```

ASan runner теперь включает ordering и пишет в `x64/f352b-asan/`.
Из PowerShell после стандартной сборки:

```powershell
$p = Start-Process .\x64\ReleaseDX12\xrDX12Smoke.exe -ArgumentList '-orderingselftest' -PassThru -Wait
$p.ExitCode
& .\ogsr_engine\Layers\xrRenderDX12\tests\run-renderprep.ps1
& .\ogsr_engine\Layers\xrRenderDX12\tests\run-cpu-runtime.ps1 -Frames 200
```

Проверенная сборка использовала OutDir `x64/f352b/`; runner получали
`-Executable .\x64\f352b\xrDX12Smoke.exe`. Локальные evidence в ignored x64:
f352b-build.log, f352b-final-build.log, f352b/ordering.log, visibility.log,
snapshot.log, prep.log, f352b-asan/xrDX12Smoke.log,
f330-results/results.json, f32c2/runtime-results.json. WM_TIMER — stdout runner.

## Файлы и ограничения

Изменены dx12renderprep.h/.cpp, xrDX12Smoke.cpp/.vcxproj, ASan runner,
CPU harness и test hooks. Добавлены dx12draworder_internal.h,
dx12ordering_tests.cpp и этот отчёт. AGENTS.md, roadmap, ADR, JobSystem и enkiTS
не изменены; пользовательские изменения/документы сохранены.

Ручной визуальный осмотр, race detector и отдельный leak detector не выполнены.
ASan не доказывает отсутствие всех гонок/утечек. DX11/R4 runtime не запускался.
Сортировка по центру bounds реализует заданный порядок объектов, но не решает
корректное смешивание пересекающихся прозрачных поверхностей. GPU-проверка
касается прежних двух кубов: новая CPU-сортировка на GPU не подаётся.
Не проверялась сцена UINT32_MAX объектов и отказ каждого выделения памяти.
Контракт камеры — LH с положительным z вперёд, как в Smoke.
Детерминизм проверен в текущей сборке/архитектуре; битовая идентичность глубины
между разными toolchain/ISA не заявляется. Невалидные физически, но конечные
view/projection не распознаются автоматически как некорректная камера.

Реализация ограничена F3.5.2B. F3.5.3 пока не реализуется.


## Финальное ревью F3.5.2B

Подтверждённых дефектов и блокеров не найдено; исходники при финализации не менялись.

- Восстановление исходных индексов имеет O(N+M) время, где N — snapshot size,
  M — visible count (M<=N). `source` объявлен до внешнего цикла, только растёт
  в while и после совпадения; каждый исходный элемент посещается не более одного
  раза. При нарушении порядка compaction поиск завершится ошибкой, а не новым
  проходом. Сортировка ключей — O(M log M), материализация — O(M), память — O(M).
- Worker callback захватывает живые snapshot/visible/keys; Wait и деструктор группы
  завершаются до выхода из их области жизни. Сортируются небольшие ключи, не
  DrawPacket12. Выходной массив выделяется один раз и получает одну копию каждого
  выбранного packet. Это намеренная материализация отдельного результата,
  сохраняющая исходный visible frame до завершения операции, а не скрытая копия
  в цикле поиска. Возвращаемые локальные кадры не const: NRVO или implicit move
  не требуют глубокого копирования vector. Verify намеренно хранит два результата.
- Порядок категорий задан enum Opaque/Transparent/Hud. HUD проверяется раньше
  Transparent при классификации. Все сравниваемые transparent depth конечны;
  signed zero нормализован, epsilon отсутствует. Ключи opaque включают поколения
  всех трёх ресурсов, затем ID/source; HUD сравнивает только source. Диапазоны
  строятся по упорядоченным категориям, их prefix и общий размер проверены.
- Проверены 40 fault-сценариев на 1024 объектах в ASan-прогоне: counts/scatter,
  keys, после sort и после записи ordered output. При отказе active=0,
  entered=exited, tasksSubmitted=tasksCompleted, unobserved не растёт. Ранее
  опубликованный кадр не меняется; следующий запуск успешен. Ссылок на keys или
  промежуточный visible в OrderedFrame12 нет. RAII освобождает временные массивы;
  ASan не выявил ошибок памяти, но отдельного leak detector нет.

Повторный CPU ordering перед коммитом: 4612 проверок, 0 ошибок
(`x64/f352b/final-review-ordering.log`). Подтверждены результаты неизменённого
кода: ASan ordering 4892/0 и прежние наборы, ReleaseDX12 build, GPU 17/17,
WM_TIMER 4/4, F3.2C.2 32/32; depth hash 9dd37d87b8eb202d.
Пользовательские AGENTS.md и три посторонних untracked документа исключаются
из коммита; build artifacts и временные логи также не включаются.
