# F3.5.3A — последовательная запись нескольких command lists

Дата: 2026-10-19. База: `3ac497d3a965269462f3234bf2445295633bbd55`, `prypiat`.
Без commit/push. Изменения ограничены standalone DX12 Smoke.

## Устройство кадра

`-commandlists legacy|multi`, по умолчанию `legacy`. Режим выбирается до
Initialize. В multi каждый из трёх FrameResource slots владеет четырьмя
парами allocator/list. Старый list используется как Owner Begin; дополнительные
три — Draw 0, Draw 1 и Owner End. Ресурсы создаются при Initialize/Resize,
повторно используются в кадрах и не выделяются каждый кадр.

Последовательность на одном owner thread:

1. Прежний CPU Prepare, затем ожидание fence текущего FrameResource slot.
2. Reset четырёх allocator/list; Owner Begin: PRESENT → RENDER_TARGET,
   очистка color/depth и заполнение прежнего per-slot constant buffer.
3. Draw 0 и Draw 1: каждый задаёт RT/DSV, viewport/scissor, root signature,
   PSO, topology, vertex buffer и root CBV. Каждый записывает свой прежний
   диапазон вершин куба. Состояние соседнего list не наследуется.
4. Owner End: прежний readback в depth-test либо переход в PRESENT.
5. После успешного Close всех четырёх — один ExecuteCommandLists в порядке
   Begin → Draw 0 → Draw 1 → End; прежние Present → Signal → смена слота.

GR workers по-прежнему могут выполнять CPU Prepare, но не записывают команды.
CPU ordering, mesh/shaders, камера, DX11/R4 и игровой traversal не менялись.
GPU fences и барьеры сохранены. Shared mesh/PSO/root signature/CB/descriptors
живут до подтверждённого завершения GPU; все дополнительные пары включены
в прежний GPU quarantine через Detach без выделения памяти.

## Ошибки и восстановление

Любой отказ Reset/Close или тестовый отказ Record прекращает запись до
ExecuteCommandLists. Флаг m_recordFailed запрещает следующую попытку кадра:
незакрытые/ошибочные lists повторно не используются. Восстановление — полный
Shutdown с GPU drain и Initialize; автоматического продолжения повреждённого
кадра нет. При неизвестном GPU lifetime действует прежний terminal quarantine,
повторный Initialize запрещён. Частичная инициализация очищается через Shutdown.

Smoke-ключ `-recordfault allocator|reset|record|close 0..3` допускается только
с `-commandlists multi -depthtest`. Тест сначала реально отправляет кадр,
затем вводит отказ: количество Execute остаётся 1, повторный RenderFrame
отказывает, Shutdown дренирует отправленную работу, Initialize и depth-test
снова проходят. Проверены все 16 комбинаций. Record injection находится после
записи команд указанного list, до Close; Close injection — перед вызовом API.
Это проверка границ прерывания, а не принудительная ошибка драйвера.

## Проверки

- ReleaseDX12|x64: Rebuild и финальная инкрементальная сборка успешны.
- Новый `tests/run-multilist.ps1`: 31/31 сценарий плюс 4/4 WM_TIMER.
  Default legacy, оба режима, Serial/Parallel/Verify CPU Prepare, resize
  (включая 1024×768), повторное использование slots, 16 отказов и восстановление.
- Во всех depth-прогонах hash `9dd37d87b8eb202d` и три контрольных пикселя PASS.
  NVIDIA RTX 5070 Laptop GPU; Debug Layer и GPU-based validation включены.
  WARNING/ERROR/CORRUPTION отсутствуют; штатный shutdown подтверждает GPU drain.
- Прежний run-renderprep: 17/17 плюс 4/4 WM_TIMER; F3.2C.2: 32/32.
- CPU selftest: 857 проверок, 0 ошибок. Отдельный CPU ASan harness с
  GR_JOBS_TESTING: Prepare 510, snapshot 566486, visibility 1519828,
  ordering 4892 проверок; везде 0 ошибок, exit 0, сообщений ASan нет.
  Включены прежние тесты поздних отказов CPU Prepare; CPU-код не менялся.

Артефакты проверки исключены из Git: `x64/f353a-build.log`,
`x64/f353a-results/`, `x64/f353a-regression.log`, `x64/f330-results/`,
`x64/f32c2/`, `x64/f353a-asan.log`.
Полный CPU test log сохранён в `x64/f353a-asan-tests.log`.

## Измерения

Recording измеряется от Reset до последнего Close, включая заполнение CB,
без CPU Prepare и ожидания GPU fence. Submission — только время вызова
ExecuteCommandLists, без Present/Signal и ожидания GPU. Это CPU latency,
не GPU time и не FPS. 30 кадров прогрева, затем среднее 200 кадров, fixed pose,
Serial CPU Prepare. Включённая GPU validation существенно влияет на результаты.

Отдельный прогон после завершения ASan, без других запущенных нами тестов:

- Legacy: recording **0.320709 мс**, submission **0.051477 мс**.
- Multi: recording **0.735342 мс**, submission **0.013689 мс**.

Логи: `x64/f353a-results/timing-legacy.log` и `timing-multi.log`.
Это один короткий прогон каждого режима; меньшая длительность вызова submission
не означает ускорения кадра. Суммарная измеренная CPU-работа здесь выше в multi.
Для выводов о производительности потребуются повторные серии без validation.

## Изменённые файлы

- `ogsr_engine/Layers/xrRenderDX12/CHW12.h`, `CHW12.cpp`: ресурсы, запись,
  переключение режима, обработка отказов и раздельные замеры.
- `ogsr_engine/Layers/xrRenderDX12/dx12quarantine.h`: дополнительные пары.
- `ogsr_engine/Layers/xrRenderDX12/xrDX12Smoke.cpp`: CLI и recovery harness.
- `ogsr_engine/Layers/xrRenderDX12/tests/run-multilist.ps1`: новый GPU-набор.
- `ogsr_engine/Layers/xrRenderDX12/tests/run-wmtimer.ps1`: выбор режима списков.
- Этот отчёт. HEAD остаётся исходным; staging и коммиты не выполнялись.

## Воспроизведение

Из корня репозитория, после ReleaseDX12 build (путь можно заменить на обычный
`x64/ReleaseDX12/xrDX12Smoke.exe`):

```powershell
& .\ogsr_engine\Layers\xrRenderDX12\tests\run-multilist.ps1 -Executable .\x64\f353a\xrDX12Smoke.exe
& .\x64\f353a\xrDX12Smoke.exe -commandlists multi -depthtest -renderprep verify -jobworkers 4
& .\x64\f353a\xrDX12Smoke.exe -commandlists multi -depthtest -recordfault close 3
# Для ручного осмотра окна:
& .\x64\f353a\xrDX12Smoke.exe -commandlists multi -fixedpose
```

## Ограничения

Ручной визуальный осмотр, race detector и отдельный leak detector не выполнены.
Реальный device removal/TDR, отказ выделения каждого D3D12-ресурса и Unknown
quarantine не форсировались; включение новых ресурсов в quarantine проверено
по коду. ASan CPU harness не инструментирует D3D12 runtime/драйвер.
DX11/R4 runtime не запускался, их файлы не изменены. Multi-list пока жёстко
ограничен двумя прежними packets; representative CPU ordering на GPU не подаётся.
Ускорение не заявляется. Параллельная запись — отдельный следующий milestone.

Пользовательские AGENTS.md и три посторонних untracked документа сохранены.
Roadmap и ADR не изменялись. Работа останавливается на F3.5.3A для ревью.

Финализация: повторно проверены Shutdown, Detach всех дополнительных пар в quarantine и запрет Initialize при terminal/occupied quarantine. Штатный Shutdown сбрасывает fence values; Initialize создаёт новые пары. Блокеров lifetime не найдено. Коммит включает только перечисленные файлы F3.5.3A.
