# F3.5.2A — Visibility & Stable Compaction

19 октября 2026 года. Ветка `prypiat`, исходный HEAD
`84de634ea9001bec731f4d244984f4d7cd438f59`. Финализация включена в один локальный коммит F3.5.2A; push не выполняется.

## Границы реализации

Новый `PrepareVisible()` применяется только к representative CPU-сцене через
`-visibilityselftest`. GPU продолжает использовать прежний `Prepare()` и два Draw.
DX11/R4, CHW12, geometry, GPU fences, запись command lists, JobSystem/enkiTS,
roadmap и ADR не изменены. Пользовательские AGENTS.md, DX12_RTX_Analysis.md и два
прежних untracked отчёта F3.1 сохранены.

Один общий kernel проверяет поля/resources и строит консервативные world bounds.
В новом CPU-пути он пропускает старый диагностический clip test; visibility
измеряется и выполняется отдельно. `Prepare()` сохраняет прежнее поведение.

Для row-vector view-projection используются D3D12-неравенства
`-w <= x,y <= w`, `0 <= z <= w`. Для каждой мировой плоскости вычисляется
верхняя оценка support мировой AABB с направленным округлением double.
Отбраковка разрешена только при строго отрицательной верхней оценке.
Деления на w и нормализации плоскостей нет. Касание плоскости, пересечение near/far
и AABB вокруг камеры сохраняются. Invisible/нулевая opacity/нулевая mask скрывают
объект, в том числе HUD; иначе HUD обходит frustum. Все поля валидируются до
visibility, даже у скрытого объекта.

Это консервативный шестиплоскостной тест, а не точный SAT: у рёбер frustum и
из-за расширения world AABB возможны лишние включения. Цель — отсутствие ложного
отсечения. Конечная camera matrix должна представлять нужный D3D12 clip volume;
семантическая корректность произвольной матрицы камеры не проверяется.

## Декомпозиция и владение

После подготовки scratch выполняются отдельные стадии:

1. Visibility: каждый worker записывает только свой элемент scratch.
2. Block counts: блоки по 128 объектов, независимые счётчики видимых элементов.
3. Последовательный exclusive prefix sum с проверкой сложения относительно count.
4. Scatter: блок пишет только в `[offset[b], offset[b+1])`, сохраняя исходный порядок.

Serial использует те же стадии и callback; Parallel — JobGroup с Wait на каждой
границе; Verify сравнивает готовые результаты всех полей через Equivalent.
Порядок определяется исходным snapshot, а не числовым ID и не порядком workers.
Sorting отсутствует. Новый pool внутри PrepareVisible не создаётся.

Счётчик ограничен UINT32_MAX, размеры проверяются контейнерами/max_size,
число блоков вычисляется без `count + blockSize - 1`. Prefix проверяет переполнение,
scatter проверяет границы диапазона и совпадение итогового count. Для x64
произведение номера блока на 128 безопасно при указанном лимите count.
Пустая сцена даёт пустой готовый кадр без задач; все скрытые объекты дают нулевой
выходной размер. Перед scatter выходной вектор получает окончательный размер.

Группа локальна каждой стадии и заканчивает работу до уничтожения захваченных
callback/массивов. При ошибке kernel, Submit, регистрации или allocation кадр
не возвращается; RAII освобождает локальные массивы после drain группы.
На scatter нет shared push_back или mutex per draw. Проверенный контракт
JobSystem требует вызовов с потока-владельца; параллельный внешний Shutdown
не поддерживается. Частичные timings после исключения не считаются результатом.

## Проверки

- ReleaseDX12|x64 Rebuild xrDX12Smoke с зависимостями: успешно, без ошибок/предупреждений.
  Первоначальный конфликт Windows min/max macros в oracle исправлен.
- Обычная visibility-сборка: **1 519 476 проверок, 0 ошибок**.
- Прежние CPU-наборы: snapshot **566 486**, renderprep **510**, без ошибок.
- Отдельный MSVC ASan harness собирает CPU-код, JobSystem и enkiTS из исходников:
  visibility **1 519 828**, snapshot **566 486**, renderprep **510**, 0 ошибок,
  exit 0, сообщений ASan нет. Разница 352 — GR_JOBS_TESTING: 288 проверок поздних отказов (см. финализацию)
  и 64 проверки управляемых отказов
  выделения памяти регистрации и Submit в Parallel/Verify. Проверяется сохранность
  ранее опубликованного кадра; production библиотека не получает fault hooks.
- GR JobSystem regression с GRJobTesting=true: **34 теста / 430 проверок**, PASS.
- GPU runner: **17/17**, depth во всех режимах **9dd37d87b8eb202d**; resize включает
  1024x768. GPU validation warning/error/corruption не обнаружены.
- WM_TIMER: **4/4**, успешные parallel/verify — 79/77 timer frames; fault-сценарии
  завершаются ожидаемым exit 1, GPU drained, submitted=completed, participants=0.
- Прежняя F3.2C.2 regression: **32/32**.

Независимый oracle не извлекает плоскости из матрицы: для ortho проверяет
пересечение интервалов с известным прямоугольным объёмом, для perspective —
пересечение AABB с вложенными квадратными сечениями `[-z,z]²`, z в [1,2].
На точных граничных примерах проверяется равенство решения; на 1000 AABB бинарной
сетки — отсутствие ложного отсечения (консервативные лишние включения разрешены).
Есть ULP-соседи near/far и боковых границ, объём вокруг камеры, пересечение near,
behind-camera, HUD/invisible/mask/opacity, обратный порядок ID, пустые и полностью
скрытые сцены, размеры 127/128/129/255/256/257. Независимая фильтрация исходных
packets проверяет сохранность metadata и порядка. Сравнения всех режимов и
больших сцен выполняются на 1/2/4/8 workers. Ошибки bounds, flags, world matrix,
resource generation, opacity, scene generation, режима и неработающего JobSystem
не меняют ранее опубликованный кадр. Shutdown/reinitialize повторяются.

## Измерения

Одна локальная серия, миллисекунды, 20 прогревочных + 100 измеряемых повторений.
Порядок Serial/Parallel чередуется; неизменный snapshot захвачен перед серией.
Workers — фоновые workers; вызывающий поток также участвует в работе.
`preparation` — валидация и подготовка bounds; затем отдельные `visibility`,
`counts`, `prefix`, `scatter`. Интервалы не перекрываются. Parallel-интервалы
включают submit/Wait каждой стадии; повторного прибавления этих расходов нет.
`total` измеряется до готовности результата и дополнительно включает выделение
scratch/counts/offsets/output; поэтому сумма фаз может быть меньше total.
Snapshot capture, Verify comparison, уничтожение scratch/возвращённого кадра
и GPU fence не входят в total. ASan-замеры не использованы.

Для 16384 объектов видимыми признаны 2335. При 8 workers total S/P
**8.209882 / 2.046013 ms**. При 256 объектах и 8 workers —
**0.101815 / 0.114717 ms**: parallel не является универсальным ускорением.
Это throughput representative CPU-подготовки, не FPS игрового renderer.

Все комбинации (ms):

```text
objects=256 visible=37 workers=1 mode=serial samples=100 preparation=0.054837 visibility=0.051752 counts=0.000156 prefix=0.000029 scatter=0.000532 total=0.126594 ms
objects=256 visible=37 workers=1 mode=parallel samples=100 preparation=0.043443 visibility=0.045015 counts=0.003201 prefix=0.000034 scatter=0.003239 total=0.114051 ms
objects=1024 visible=151 workers=1 mode=serial samples=100 preparation=0.221447 visibility=0.210529 counts=0.000519 prefix=0.000023 scatter=0.002127 total=0.486843 ms
objects=1024 visible=151 workers=1 mode=parallel samples=100 preparation=0.177862 visibility=0.182047 counts=0.004835 prefix=0.000059 scatter=0.006811 total=0.424676 ms
objects=4096 visible=584 workers=1 mode=serial samples=100 preparation=0.963753 visibility=0.838747 counts=0.002013 prefix=0.000071 scatter=0.010097 total=1.911186 ms
objects=4096 visible=584 workers=1 mode=parallel samples=100 preparation=0.627249 visibility=0.588968 counts=0.012016 prefix=0.000151 scatter=0.025075 total=1.342089 ms
objects=16384 visible=2335 workers=1 mode=serial samples=100 preparation=3.762302 visibility=3.367801 counts=0.013724 prefix=0.000153 scatter=0.049818 total=7.788521 ms
objects=16384 visible=2335 workers=1 mode=parallel samples=100 preparation=2.081620 visibility=1.968026 counts=0.037630 prefix=0.000338 scatter=0.066665 total=4.709886 ms
objects=256 visible=37 workers=2 mode=serial samples=100 preparation=0.062696 visibility=0.061131 counts=0.000204 prefix=0.000031 scatter=0.000751 total=0.134743 ms
objects=256 visible=37 workers=2 mode=parallel samples=100 preparation=0.052777 visibility=0.053707 counts=0.006339 prefix=0.000043 scatter=0.004403 total=0.128991 ms
objects=1024 visible=151 workers=2 mode=serial samples=100 preparation=0.245418 visibility=0.218816 counts=0.000528 prefix=0.000025 scatter=0.002311 total=0.495864 ms
objects=1024 visible=151 workers=2 mode=parallel samples=100 preparation=0.130254 visibility=0.133105 counts=0.009108 prefix=0.000076 scatter=0.009941 total=0.313264 ms
objects=4096 visible=584 workers=2 mode=serial samples=100 preparation=0.969443 visibility=0.854408 counts=0.002186 prefix=0.000071 scatter=0.010381 total=1.932578 ms
objects=4096 visible=584 workers=2 mode=parallel samples=100 preparation=0.461439 visibility=0.435854 counts=0.014599 prefix=0.000149 scatter=0.021788 total=1.021033 ms
objects=16384 visible=2335 workers=2 mode=serial samples=100 preparation=4.375460 visibility=3.578346 counts=0.027997 prefix=0.000288 scatter=0.073694 total=8.729129 ms
objects=16384 visible=2335 workers=2 mode=parallel samples=100 preparation=1.970968 visibility=1.536035 counts=0.041968 prefix=0.000477 scatter=0.071791 total=4.289876 ms
objects=256 visible=37 workers=4 mode=serial samples=100 preparation=0.051719 visibility=0.049215 counts=0.000165 prefix=0.000023 scatter=0.000563 total=0.106081 ms
objects=256 visible=37 workers=4 mode=parallel samples=100 preparation=0.044972 visibility=0.037291 counts=0.006544 prefix=0.000028 scatter=0.003605 total=0.097103 ms
objects=1024 visible=151 workers=4 mode=serial samples=100 preparation=0.250558 visibility=0.219123 counts=0.000564 prefix=0.000035 scatter=0.002385 total=0.491611 ms
objects=1024 visible=151 workers=4 mode=parallel samples=100 preparation=0.100100 visibility=0.082574 counts=0.010754 prefix=0.000078 scatter=0.010517 total=0.221825 ms
objects=4096 visible=584 workers=4 mode=serial samples=100 preparation=1.006607 visibility=0.855843 counts=0.002859 prefix=0.000087 scatter=0.011309 total=1.944974 ms
objects=4096 visible=584 workers=4 mode=parallel samples=100 preparation=0.364567 visibility=0.267681 counts=0.016993 prefix=0.000173 scatter=0.020254 total=0.748423 ms
objects=16384 visible=2335 workers=4 mode=serial samples=100 preparation=3.955912 visibility=3.422222 counts=0.014072 prefix=0.000226 scatter=0.049762 total=8.028936 ms
objects=16384 visible=2335 workers=4 mode=parallel samples=100 preparation=1.240159 visibility=0.929300 counts=0.026034 prefix=0.000382 scatter=0.042523 total=2.821281 ms
objects=256 visible=37 workers=8 mode=serial samples=100 preparation=0.049926 visibility=0.046124 counts=0.000151 prefix=0.000019 scatter=0.000529 total=0.101815 ms
objects=256 visible=37 workers=8 mode=parallel samples=100 preparation=0.051143 visibility=0.041553 counts=0.011210 prefix=0.000031 scatter=0.005744 total=0.114717 ms
objects=1024 visible=151 workers=8 mode=serial samples=100 preparation=0.292813 visibility=0.224762 counts=0.000580 prefix=0.000032 scatter=0.002474 total=0.544945 ms
objects=1024 visible=151 workers=8 mode=parallel samples=100 preparation=0.102327 visibility=0.088145 counts=0.020442 prefix=0.000109 scatter=0.019698 total=0.258088 ms
objects=4096 visible=584 workers=8 mode=serial samples=100 preparation=0.983251 visibility=0.852507 counts=0.002664 prefix=0.000087 scatter=0.010991 total=1.939592 ms
objects=4096 visible=584 workers=8 mode=parallel samples=100 preparation=0.276884 visibility=0.198101 counts=0.024909 prefix=0.000220 scatter=0.026003 total=0.632724 ms
objects=16384 visible=2335 workers=8 mode=serial samples=100 preparation=4.071565 visibility=3.452027 counts=0.018680 prefix=0.000277 scatter=0.056164 total=8.209882 ms
objects=16384 visible=2335 workers=8 mode=parallel samples=100 preparation=0.799701 visibility=0.551334 counts=0.036060 prefix=0.000476 scatter=0.044448 total=2.046013 ms
```

## Воспроизведение

Из x64 Native Tools Command Prompt VS 2022 в корне репозитория:

```bat
MSBuild.exe Engine.sln /t:xrDX12Smoke:Rebuild /p:Configuration=ReleaseDX12 /p:Platform=x64 /m:1 /nr:false
ogsr_engine\Layers\xrRenderDX12\tests\run-visibility-asan.cmd
```

Из PowerShell после обычной сборки:

```powershell
$p = Start-Process .\x64\ReleaseDX12\xrDX12Smoke.exe -ArgumentList '-visibilityselftest' -PassThru -Wait
$p.ExitCode
& .\ogsr_engine\Layers\xrRenderDX12\tests\run-renderprep.ps1
& .\ogsr_engine\Layers\xrRenderDX12\tests\run-cpu-runtime.ps1 -Frames 200
```

Проверенная сборка использовала отдельный OutDir `x64/f352a/`; runner принимали
`-Executable .\x64\f352a\xrDX12Smoke.exe`. Evidence остаются в ignored x64:
`f352a-build.log`, `f352a/visibility.log`, `snapshot.log`, `prep.log`,
`f352a-asan/xrDX12Smoke.log`, `f330-results/results.json`,
`f32c2/runtime-results.json`. WM_TIMER результаты напечатаны в stdout.

## Ограничения

Ручной визуальный осмотр, интерактивный drag-resize/minimize/restore и race detector
не выполнены. ASan не является race detector или отдельным leak detector.
Отсутствие всех гонок/утечек не доказано. Runtime DX11/R4 не запускался.
Не выделялась сцена UINT32_MAX объектов, не инъецировались повреждённые внутренние
counts/offsets, не форсировался bad_alloc каждого scratch/output allocation;
эти ветки защищены проверками и RAII, но не имеют прямого fault-покрытия.
Контролируемый отказ Submit/регистрации проверен в первой parallel-стадии;
поздние counts/scatter покрыты отдельной инъекцией исключения после записи.
Oracle точного пересечения ограничен двумя известными frustum; произвольные
камеры из Smoke покрыты сравнением режимов, а не универсальным геометрическим oracle.

Реальный registry, игровой traversal, sorting и parallel command recording
не реализованы. F3.5.2A завершён в указанных границах, работа остановлена для ревью.


## Финализация: поздние исключения

`tests/visibility_test_hooks.h` доступен только под GR_JOBS_TESTING и не меняет
production API. Однократное исключение возникает в block counts после записи
счётчика блока 0 либо в scatter после первой записи packet блока 0. В Verify
Serial-часть завершается штатно; отказ происходит именно в Parallel-части.
Атомарные счётчики callback измеряют вход/выход и число активных callback.

32 сценария: две стадии × Parallel/Verify × два frustum × 1/2/4/8 workers.
Каждый проверяет факт единственного отказа, active=0, entered=exited после
возврата исключения, tasksSubmitted=tasksCompleted и завершение ранее начатых
стадий (три задачи для counts, четыре для scatter), одну наблюдённую ошибку без
роста unobservedFailures. Присваивание ранее опубликованному кадру не происходит;
Equivalent подтверждает все его поля и порядок. Следующий вызов на том же
JobSystem успешно возвращает ожидаемый кадр, callback снова дренированы.
Всего добавлено 288 проверок.

Локальные scratch/counts/offsets и частичный result уничтожаются при раскрутке
стека после Wait/drain. Повторные отказ/успех под ASan прошли без ошибок доступа,
use-after-free или double-free; отдельного leak detector нет, поэтому отсутствие
всех утечек не заявляется. Производственная логика стадий не изменена.

Повторная ReleaseDX12|x64 сборка — PASS. CPU visibility — 1519476/0.
ASan — visibility 1519828/0, snapshot 566486/0, renderprep 510/0, exit 0.
GPU runner с Frames=40: 17/17; WM_TIMER 4/4, успешные режимы по 77 timer frames.
Depth во всех режимах 9dd37d87b8eb202d; validation warning/error отсутствуют,
resize и shutdown прошли. Для fault WM_TIMER ожидаемый exit 1, submitted=completed=109,
participants=0, GPU drained. Замеры производительности выше относятся к исходной
изолированной серии; повторные прогоны финализации не заменяют их.

Дополнительные evidence: x64/f352a-finalization-build.log,
x64/f352a/finalization-cpu.log и x64/f352a-asan/xrDX12Smoke.log.
В коммит входят dx12renderprep.h/.cpp, dx12snapshot_tests.cpp, xrDX12Smoke.cpp,
тестовый CPU main, ASan runner, visibility_test_hooks.h и этот отчёт.
Пользовательские файлы, roadmap, ADR и артефакты сборки в коммит не включаются.
