# F4.3-MT. Техническая проверка native GR

24.11.2026. Исходная публикация `0005485e6446c3d0166d12a5bd7bbbf9de2a1d9f`, база FR2 `35b463272f8a06e0036dbac5c9b4de5e26d3d62f`. Проверенная реализация MT `429eeafdb`.

## Ревью и реализация

- `509a45ec7` закрепляет поток-владелец общего J2A/GR и различает auto и явные 0 workers. Отдельно измеряются создание группы/Submit и Wait. Wait включает исполнение на потоке-владельце.
- `4bc814bb1` использует снимок из неизменяемых представлений опубликованных данных. Снимок проверяет level session, epoch, generation, диапазоны поз, индексы мешей и требования скиннинга. GR пишет маски по индексу и независимые упорядоченные списки для отдельных проекций. Verify заново собирает эталон прежним обходом BVH и объектов и сравнивает весь результат. Векторы сохраняют ёмкость между кадрами. Capture, обновление ROS/поз, регистрация ресурсов, запись descriptors и upload остаются на владельце.
- `8cca541bd` добавляет последовательные списки. Одна пара allocator/list для каждой карты, один список начала и один список цвета. Слот ждёт прежний GPU fence перед Reset. Владелец записывает переходы карт, GPU получает начало, карты в порядке проекций и цвет в одном Execute. Close каждого списка проверяется до Execute. Дополнительные пары входят в существующий quarantine.
- `c167e7b9b` исправляет найденные сборкой подключения JobGroup и область видимости null water SRV.
- `429eeafdb` записывает независимые теневые списки через тот же GR pool. Работы получают собственные списки и результаты. Основной и водный проходы по-прежнему следуют после перехода карт в SRV. При недостатке карт или пакетов выбирается последовательная запись. Отказ записи блокирует отправку кадра после Wait.

`GameDx12Bridge_OnFrame` в `device.cpp` по-прежнему вызывается после обновления камеры и перед `TTAPI->submit`. Во время Capture доступ к `IGame_Level`, объектам, skeletal pose и collision выполняется на потоке игры. GR завершается до выхода из кадра моста, TTAPI сохраняет прежний порядок. Для overlap с TTAPI нужны отдельные замеры и доказанный фазовый контракт. Opaque и dynamic geometry используют общие кэши привязок, трансформы слота и deferred/water ordering. Для MT6 необходимо сначала выделить пакеты и эксклюзивные GPU-пары без изменения прозрачной последовательности, затем сравнить critical path на Jupiter.

## Проверки

- XR_3DA Release x64, MSBuild 17.14, MSVC 14.44, SDK 10.0.26100.0, экспериментальные свойства `OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE` и `OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE`. Опубликованный HEAD и MT5 собираются. MT5 завершился с 0 ошибок и 79 предупреждениями. Рабочий DOS-путь W: устраняет ограничение длины заголовков FidelityFX. Профиль сборки `Engine.sln /t:XR_3DA /p:Configuration=Release /p:Platform=x64`.
- GR ReleaseDX12 x64, 31 тест / 388 проверок и 34 теста / 430 проверок с `GRJobTesting=true`, 0 ошибок. JS partition model 47 805 проверок. PowerShell AST launcher проходит. FXC 7/7 точек входа VS/PS 5.0.
- Независимый WARP тест `GameDx12NativeFrame.cpp` с Debug Layer и GPU-based validation. Legacy и последовательный multi дают одинаковый прочитанный цвет и глубину двух теневых срезов. После расширения на GR 0 и 2 background workers все четыре режима дали одинаковый readback. Проверено 24 кадра, resize, отмена открытого списка и отказ от fence частичного набора. В каждой GR конфигурации 7 submit / 7 complete, один намеренный наблюдённый callback fault, 0 unobserved. В info queue нет Warning/Error/Corruption. Это техническая сцена с двумя DSV-срезами и контрольным цветом.
- Результаты и контрольные суммы находятся в `artifacts/F4.3-MT-native-gr` исходной рабочей копии. Манифест указывает абсолютные пути. MT runtime сохранён отдельно под `mt-parallel-runtime`.

## Гейты владельца

Для ручных прогонов запустить `tools/dx12/run-f43f-jupiter.ps1` с `-GameRoot <Jupiter-runtime> -Exe <mt-parallel-runtime\xrEngine.exe> -SceneLog`. Менять по одному параметру:

- `-GR serial -CommandLists legacy`, затем `-GR serial -CommandLists multi`.
- `-GR parallel -CommandLists legacy`, `-GR verify -CommandLists legacy`.
- `-GR serial -CommandLists parallel` и `-GR parallel -CommandLists parallel`.
- Повторить нужные конфигурации с `-Validation`, затем сравнить кадры без `-Validation` при `-GRWorkers 0`, `1`, `2`, `4`, `auto`. `-GRGrain` задаёт размер блока culling, по умолчанию 256.

Использовать один save, маршрут, разрешение и погоду. В журнале сверить `ph_dx12_mt`, `ph_dx12_mt_record`, `ph_dx12_f43f: perf`: effective mode, fallback, маску карт, draw counts, одинаковые submitted/completed, ноль unobserved, реальное участие потоков, overlap и контрольные изображения. Verify предназначен для корректности. Проверить resize, выход из уровня и возврат, переключение native/R4 и Debug Layer. Jupiter runtime, image parity реальной сцены, производительность и MT6 остаются за игровым гейтом. Issues #33–#40 остаются открыты.
