# F3.2C — финализация перед коммитом

19 октября 2026 года. Ветка `prypiat`, исходный HEAD
`7ae6958d7b6acde77e5cd65cd73eca3cfe4743fa`.

## Состав атомарного изменения

Stage 1: отдельная static library `Layers/JobSystem`, публичный API без
DirectX/enkiTS, приватный backend, закреплённая копия enkiTS
`404a3bf8f855039dfff2052184d6308655286c07` с лицензией.
`Engine.sln` и ProjectReference подключают GR только к `xrDX12Smoke`.

F3.2C.1: владение закрепляется до публикации, отказ Submit до публикации,
drain/shutdown и поколения состояния, защита диагностического hook;
production и fault-injection тесты. Подробности и ограничения —
[README](../ogsr_engine/Layers/JobSystem/README.md).

F3.2C.2: один pool на приложение, Serial/Parallel CPU-сцена, проверка
эквивалентности, измерения, CPU-самотест и runtime runner. Подробности —
[отчёт F3.2C.2](dx12-f3-2c2-runtime.md).

В исходном HEAD ещё был треугольник. Поэтому для самодостаточного коммита
включены уже существовавшие необходимые исходники F3.1: CHW12, камера,
двухкубовый mesh, дополнения quarantine, замена triangle и их build entries
в `xrRender_R5_DX12.vcxproj`. Это зависимости текущего Smoke, не новая
интеграция Job System в renderer. Эти исходники при финализации не менялись.

Вне коммита оставлены пользовательские AGENTS.md, docs/08-roadmap.md,
DX12_RTX_Analysis.md и два прежних отчёта docs/dx12-f3-1-*.md.
Build artifacts, локальные snapshots, временные runners и логи исключены.

## Повторная проверка

- Rebuild `Engine.sln /t:xrDX12Smoke:Rebuild`, ReleaseDX12|x64: успешно,
  без предупреждений и ошибок. v143, C++20; GR/Smoke используют /MT и /EHsc.
- Обычный выходной exe был занят открытым пользовательским Smoke.
  Две попытки обычной пересборки получили access denied/LNK1104.
  Пользовательский процесс не закрывался. Успешная пересборка использовала
  `/p:OutDir="D:\Coding Project\OGSR ENGINE fork from rtx\x64\f32-finalization\"`.
  Изменены только пути артефактов, не compile flags или исходники.
- Production GR: 31 тест / 388 проверок / 0 ошибок.
- GRJobTesting: 34 теста / 430 проверок / 0 ошибок, включая контролируемые
  bad_alloc и отказ Submit до публикации.
- Свежий exe: 32/32 runtime-запуска, все exit 0. Runner F3.2C.2 использован
  с локальной копией путей exe/log и `-Frames 30`; resize — 60 кадров.
  CPU selftest: 857 проверок. Serial/Parallel проверены на четырёх размерах
  и 1/2/4/8 workers; depth без/с Parallel и resize прошли.
- В логах нет GPU validation WARNING/ERROR/CORRUPTION. Аппаратный GPU,
  прежние два куба и завершение GPU проверены автоматическими инвариантами.
- Проверка whitespace собственного кода прошла. После staging `git diff
  --cached --check` обнаружил только 10 строк с исходными trailing spaces
  в README.md и LockLessMultiReadPipe.h вендоренного enkiTS. Они сохранены,
  чтобы не изменять закреплённую зависимость. Полная сборка всех игровых
  targets не выполнялась.

Локальные доказательства: `x64/f32-finalization/*.log`,
`runtime-results.json`; build log: `x64/f32c2/finalization-isolated-build.log`.
Они намеренно не входят в коммит. Предыдущие измерения производительности
сохранены в отчёте F3.2C.2; повторная короткая серия — регрессия, не новый benchmark.

**Ручной визуальный осмотр и race detector ещё не выполнены.** ASan в предыдущем
этапе не заменяет race detector. Не заявляется полная свобода от гонок или
проверка gameplay. Сохраняются ограничения lifecycle/backend из README.

Push не выполняется. После коммита разрешён только архитектурный аудит F3.3,
без реализации, изменения DX11/gameplay и parallel command list recording.
