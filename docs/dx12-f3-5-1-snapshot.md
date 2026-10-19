# F3.5.1 — Representative X-Ray Snapshot

19 октября 2026 года. Ветка `prypiat`, исходный HEAD
`8b1c05256ebfe750fceb337ea7bf805091298aa4`. Финальное ревью включено в локальный
коммит F3.5.1; push не выполняется.
Основание — принятый ADR `dx12-f3-5-architecture-adr.md`, раздел 9.

## Реализация

RenderSceneSnapshot12 владеет неизменяемой копией динамического массива объектов.
Вход принимается через span; snapshot не сохраняет его или ссылки на источник.
Object12 содержит u64 object ID, row-vector transform (XMFLOAT4X4 как значимый
эквивалент Fmatrix без зависимости от xrCore), локальные AABB/sphere,
HOM frame/tested, sector ID, visibility mask, HUD/invisible/transparent flags,
opacity и visual/material/PSO `(id, generation)`. Сырых игровых указателей нет.

Ссылки проверяются исключительно по фиксированной SyntheticResources12.
**Это синтетическая таблица, не настоящий render-owned registry.** Она не
разрешает GPU-ресурсы и не моделирует recycling/streaming. Geometry ranges
ограничены прежними 72 вершинами тестовой геометрии. Большая CPU-сцена не
передаётся в GPU: там остаются два прежних объекта и два Draw.

Общий Kernel поддерживает конечные affine transforms: перенос, поворот,
неравномерный и отрицательный масштаб, shear. Интервальный affine transform
с направленным округлением double-сумм и float-границ даёт мировую AABB.
Центр сферы трансформируется матрицей; радиус умножается на
консервативную верхнюю границу spectral norm `sqrt(norm1 * normInf)` с
округлением радиуса вверх и добавлением верхней границы ошибки хранения центра.
Сфера может быть менее плотной после поворота/shear.
Projective world matrices отклоняются. Исходные bounds считаются локальными,
результирующие worldMinimum/worldMaximum/worldSphere — мировыми.

HUD обходит frustum test; invisible, нулевая opacity или mask=0 дают visible=false.
Неизвестные flags, некорректные bounds/sphere/opacity, неизвестные ресурсы и
неверные поколения вызывают ошибку. Opacity<1 требует Transparent. HOM metadata
копируется, но HOM occlusion не исполняется. Visible пока только диагностика:
packets не удаляются; сортировка прозрачности не вводилась.

Prepare/Kernel сохраняют сигнатуры. Serial/Parallel/Verify используют один
алгоритм. Каждый worker пишет в свой элемент заранее выделенного scratch;
после Wait массив перемещается в PreparedFrame без изменения исходного порядка.
При исключении группа дренируется до освобождения scratch, частичный кадр
не публикуется. Scene generation проверяется отдельно от resource generation.
Один pool используется на приложение; в CPU-тесте — один на конфигурацию workers,
на все размеры и повторения. CPU frames-in-flight не добавлены.

## Проверки

- ReleaseDX12|x64, Rebuild xrDX12Smoke и зависимостей: PASS, 0 ошибок/предупреждений.
  Использован отдельный OutDir `x64/f351`, чтобы не перезаписывать прежний exe.
  Полная сборка игровых targets не выполнялась.
- Новый `-snapshotselftest`: **566486 проверок / 0 ошибок** после final review. Размеры
  256/1024/4096/16384, workers 1/2/4/8, три положения камеры, повторные прогоны.
  Serial/Parallel/Verify совпали. Порядок и представительные metadata сохранены.
- Независимый double oracle AABB использует center/extents и abs(linear),
  а не SIMD-преобразование углов kernel. Проверены 26 направлений сферы.
  Входной источник удаляется после capture; snapshot остаётся рабочим.
- Отрицательные проверки: scene/resource generation, unknown resource,
  перевёрнутый/NaN AABB, отрицательный radius, неизвестные flags, конфликт
  opacity/Transparent, Inf/projective matrix, null/stopped JobSystem.
  Ошибка callback после частичной записи не меняет ранее опубликованный кадр;
  дальнейшая отправка и повторный Initialize/Shutdown проходят.
- Прежние CPU-тесты: **510** renderprep и **857** CPU scene проверок, без ошибок.
- Runner renderprep: **17/17 запусков**. Depth Serial/Parallel/Verify сохраняет
  полный RGBA hash **`9dd37d87b8eb202d`**. Три resize, включая 1024×768, проходят.
- WM_TIMER: **4/4 сценария** — Parallel/Verify и оба одноразовых отказа.
  Финальные успешные прогоны реально отрисовали по 77 timer frames. При fault ожидаемый
  exit=1, дальнейший render остановлен, GPU drained, submitted=completed,
  participants=0; validation warnings/errors отсутствуют.
- F3.2C.2: **32/32 запусков**, по 200 измеряемых кадров в основной матрице.
- Доступные GR regression exe: **388** production и **430** instrumented
  проверок, без ошибок. Исходники GR и enkiTS не менялись.

CHW12, GPU fences/command recording, DX11/R4, игровая логика, roadmap и ADR
не изменены. Прежние пользовательские изменения, включая AGENTS.md, сохранены.
В dx12mesh добавлено только вычисление sphere из уже существующих bounds;
вершины, draw ranges, материалы и shader-код сохранены.

## CPU timings

Одна локальная серия, 20 прогревочных + 100 измеряемых повторений, порядок
Serial/Parallel чередуется. Средние в ms для **4 workers + вызывающий поток**:

- 256: snapshot **0.024193**; prepare S/P **0.101314 / 0.065665**;
  submit **0.006957**, wait **0.047702**; merge S/P **0.000022 / 0.000051**.
- 1024: snapshot **0.098313**; prepare S/P **0.366793 / 0.144053**;
  submit **0.007022**, wait **0.107958**; merge S/P **0.000022 / 0.000048**.
- 4096: snapshot **0.220893**; prepare S/P **1.452956 / 0.495124**;
  submit **0.008848**, wait **0.446613**; merge S/P **0.000024 / 0.000073**.
- 16384: snapshot **1.107365**; prepare S/P **5.857304 / 1.924809**;
  submit **0.009624**, wait **1.438308**; merge S/P **0.000026 / 0.000076**.

Для 16384 объектов Parallel total при 1/2/4/8 workers:
**3.526842 / 2.710432 / 1.924962 / 1.476037 ms**. Все 16 комбинаций размеров
и workers напечатаны в `x64/f351/final-review-cpu.log`. Эти числа заменяют
предыдущие замеры: подтверждённое исправление округления увеличило работу kernel.

Snapshot включает capture камеры, копирование объектов и проверку уникальности ID
с выделением памяти. Prepare включает выделение/инициализацию scratch и работу
kernel, а для Parallel также группу, submit и Wait. Submit/Wait — вложенные
интервалы, их нельзя прибавлять к prepare; вызывающий поток может выполнять
задачи в обоих интервалах. Merge здесь — только передача владения массивом и
заполнение заголовка, без compaction/sort; столь малые значения близки к цене
таймера. Total включает prepare/merge и измерительную обвязку, но не snapshot,
Verify comparison и уничтожение возвращённого кадра. Проверка camera/generation
перед запуском run не включена в prepare. GPU fence время в эти CPU-замеры не входит.

Это синтетический throughput CPU preparation, **не FPS**. Для настоящих двух
кубов в повторном GPU-прогоне Serial total около 0.0030 ms, Parallel с 4 workers
около 0.0171 ms: Serial остаётся default. Стоимость snapshot для большой сцены
существенна; ускорение одного kernel не равно ускорению всего кадра.

## Воспроизведение и файлы

Из корня в Developer Command Prompt:

```bat
MSBuild.exe Engine.sln /t:xrDX12Smoke:Rebuild /p:Configuration=ReleaseDX12 /p:Platform=x64 /m:1 /nr:false
```

Из PowerShell после обычной сборки:

```powershell
& .\x64\ReleaseDX12\xrDX12Smoke.exe -snapshotselftest
& .\ogsr_engine\Layers\xrRenderDX12\tests\run-renderprep.ps1
& .\ogsr_engine\Layers\xrRenderDX12\tests\run-cpu-runtime.ps1 -Frames 200
```

Для проверенной изолированной сборки заменить exe на `x64/f351/xrDX12Smoke.exe`;
оба runner принимают `-Executable .\x64\f351\xrDX12Smoke.exe`.
Локальные evidence: `x64/f351/final-review-cpu.log`, `x64/f330-results/*.log`
и `results.json`, `x64/f32c2/*.log` и `runtime-results.json`. WM_TIMER runner
печатает результат в stdout; его текущий exe-log последующие прогоны заменяют.

Изменения внутри `ogsr_engine/Layers/xrRenderDX12`: `dx12renderprep.h/.cpp`,
`dx12renderprep_tests.cpp`, новый `dx12snapshot_tests.cpp`, metadata в
`dx12mesh.cpp`, CLI в `xrDX12Smoke.cpp`, compile entry в `xrDX12Smoke.vcxproj`,
`tests/run-renderprep.ps1` (фиксированный depth hash) и `tests/run-cpu-runtime.ps1`
(выбор проверяемого exe). Этот отчёт — единственное новое изменение в docs.

**Ограничения:** ручной визуальный осмотр, интерактивный minimize/restore и
специализированный race detector не выполнены. Автотест WM_TIMER не является
ручной проверкой drag-resize. Реальный registry, игровой capture, TTAPI join,
GPU resource lifetime через новые handles и устойчивость к частичному отказу
запуска enkiTS не реализованы/не проверены. Перечень писателей representative
source ограничен функцией Scene на тестовом потоке; перенос этого утверждения
на игровой мир запрещён без аудита писателей из ADR §3.1.

## Финальное ревью перед коммитом

Подтверждён и исправлен дефект консервативности при округлении: перенос точки
на 2²⁴ с локальной координатой 0.5 сужал AABB и оставлял радиус нулевой сферы
равным нулю при смещённом сохранённом центре. Новый тест до исправления дал
две ошибки (`x64/f351/review-before.log`). Теперь направленное округление
границ и учёт ошибки центра сохраняют включение. Добавлены строгие проверки
без epsilon для поворота, отрицательного масштаба, shear и cancellation.
Арифметические предпосылки: обычное round-to-nearest, `/fp:precise`.

Scene generation проверяется отдельно от каждой пары resource `(id, generation)`;
поколение сцены не подставляется в поколения ресурсов. Snapshot владеет копией
данных, workers пишут в непересекающиеся элементы заранее выделенного scratch.
Wait дожидается всех задач до передачи исключения; группа уничтожается раньше
scratch. Результат публикуется только после успешного ожидания. Подтверждённых
дефектов владения или публикации при ревью не найдено.

Повторный ReleaseDX12|x64 Rebuild завершился успешно. Финальные CPU-прогоны:
510 прежних и 566486 representative проверок, ноль ошибок. Отдельный CPU harness
под MSVC AddressSanitizer повторил оба набора с теми же результатами, exit 0,
без сообщений ASan (`x64/f351-asan/xrDX12Smoke.log`). Это проверка ошибок памяти,
не leak detector и не race detector; отсутствие всех утечек/гонок не доказано.
Регрессия F3.2C.2 повторена после исправления: 32/32 сценария прошли.
GPU-путь, fences, command recording и DX11/R4 не изменены; runtime DX11/R4
не запускался. Диагностический frustum test пока не удаляет Draw; перед реальным
culling необходим отдельный oracle для пограничных случаев clip-space.

Двойного учёта timings нет: snapshot измеряется отдельно, submit/wait вложены
в prepare, totals не складывают их повторно. Измеренный merge — передача владения,
а не будущая compaction/sort. Блокеров для локального коммита этого representative
прототипа не найдено; ограничения выше сохраняются.

F3.5.1 завершён в этих границах. Реализация F3.5.2 не начата.
