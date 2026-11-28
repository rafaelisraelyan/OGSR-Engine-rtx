# F4.3-Q — восстановление освещения и теней DX12

28 ноября 2026 г. База — `254fd195ce90decba0b0ee4fb7e3ec7675c0f638` на `origin/experiment/f43-csm-r2-c`. Исправление receiver-plane из `0e62b8f7` присутствует в базе. Рабочая ветка — `codex/f43-quality-recovery` в отдельном checkout. [Трекер #41](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/41)

Статус пакета — **PARTIAL**. Игровая, визуальная и аппаратная приёмка остаются открытыми. WARP подтверждает технические свойства отдельных сценариев.

## Q0 — PARTIAL

Найдены сырые Jupiter B/F. У обоих прогонов одинаковый проверенный EXE от `0e62b8f73e0306e22e5dc01e02f999ad57804a75`, одинаковые входные профиль и сохранение, выход 0, Adaptive включён, Debug Layer и GBV выключены. Состав оптимизаций B/F подтверждён журналом. В B 3632 выборки, в F 470. Первые 24 точки до кадра 193 совпали по позиции с допуском 2 см и вектору направления с допуском 0,004. Записанных численных параметров света для этих точек нет. Оценка ускорения остаётся открытой. `DetailSunShadows` выключен в обоих прогонах.

`03a5bf04c`, `cc091853d`, `1475518a0`. Лаунчер сохраняет сырые логи, фактические режимы и CSV. Режимы сцены включают albedo, shading и geometric normals, baked hemi, результирующий hemi, ambient, sun diffuse и visibility, local contribution и visibility, индивидуальный источник, каскад и blend, материал, physical address и фактическое разрешение. Для Adaptive диагностический burst показывает запрошенное и физическое разрешения, page/generation/slice, dirty-reason mask и наличие выбранного visual или object в подготовленном наборе. Точная адресация пишется в лог, экранный режим кодирует её цветом. Поле `gpu_context_valid` указывает, что метаданные `gpu_*` относятся к тому же `gpu_present` и слоту. [#41](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/41)

## Q1 — PASS, техническая проверка

`c26f0a4be`, `03a5bf04c`. Парсер пропускал десять float после world matrix в TREEDEF2, а detail snapshot терял baked hemi. Восстановлены чтение и проверка обоих блоков, коэффициенты hemi после однократного множителя R4 и явный выбор MT_TREE_ST/PM. Detail placement сохраняет `c_hemi / 15` с clamp R4 и нормаль CForm, включая наклон. Данные проходят через immutable snapshot и fenced instance stream до native VS.

Узкий CPU parser run дал 110 PASS, 0 FAIL, 1 SKIP для реальных входов Jupiter. Production HLSL в WARP дал для деревьев 0,33, 0,41, 0,49, 0,57 и для деталей 0,4 и 0,8. Мутация потери коэффициентов обнаружена. Освещение реальных деревьев и камыша, а также разные интенсивности суток требуют игровых кадров. [#42](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/42)

## Q2 — PARTIAL

`c26f0a4be`, `03a5bf04c`. Detail color использует двустороннюю растеризацию и terrain-aligned normal без инверсии по обратной стороне. Для идентифицированного R4 `CBlender_Tree` признак cutout берётся из `oBlend`. Непереопределённые tree blender используют фактический общий `r_alphatest_threshold` основного и теневого HLSL. Для Lua и неизвестных blender остаётся штатное определение материала.

Compute WARP подтвердил наклонную нормаль (0,6 / 0,8 / 0), раздельные hemi и sun и обнаружение мутации вертикальной нормали. Два растеризованных треугольника с противоположным winding дали одинаковый результат при разных фактических `SV_IsFrontFace`. Мутация инверсии нормали обнаружена. WARP с production `shadowMain` также проверил одинаковые маски цвета и глубины для двух mip, порога 200/255 и альфа-значений 0/199/201/255. Фикстура использует тестовую корневую сигнатуру, заданный mip и point sampler. Открыты варианты winding и mip coverage реальных материалов, а также MAT_FLORA. Общий path shading обычной статики и NPC сохраняет свои флаги. [#43](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/43)

## Q3 — PASS, техническая проверка

`03a5bf04c`. Целочисленный snapped box дополнен непрерывным PCF3/5/7. Каждое сравнение использует поправку глубины в центре текселя, дробные края получают точные веса, вырожденный градиент обнуляется. Максимальная опора равна 4, 6 и 8 центрам на ось. Сохраняются CSM fitting, snapping, overlap, blend и старые режимы. Для нового фильтра резерв 4,5 текселя учитывает широкую опору и половину текселя snapping.

Независимый CPU-эталон дал 100 794 сравнения. Проверены углы каскадов и overlap на CPU. Production WARP со специализированными PSO дал ожидаемый readback на PCF3/5/7, включая субтексельные координаты и наклонный приёмник. Legacy PCF и регрессия receiver correction проверены отдельно. Динамика CSM при игровом изменении камеры, FOV, солнца и на дальней границе остаётся открытой. [#44](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/44)

## Q4 — PARTIAL

`03a5bf04c`, `cc091853d`. Исправленный legacy local PCF сохранён в отдельном режиме. Новый фильтр использует фиксированную равномерную опору с дробными краями независимо от центральной visibility. Сохранены аналитический перспективный градиент `0e62b8f7` и адреса Banked/Adaptive. Поле проекции выводится от 128 px, минимального разрешения Adaptive. В зоне стыка каждая доступная грань использует собственные UV, receiver depth и градиент. Смешивается visibility. Низкоуровневый normal bias сохранён. Для его дальнейшей калибровки пространственная ширина текселя в плоскости проекции равна `2 * clip.w / (resolution * projectionScale)`. Эквивалент на наклонной геометрии и пределы требуют аппаратной проверки.

CPU-проверка охватывает 49 065 случаев, включая локальный clamp, градиент и геометрию guard на 128–1024. WARP дал readback Banked SM5.0 для трёх профилей, Banked SM5.1 и Adaptive для PCF3. Clustered/full прошёл с ограниченным индексом t13. Для отдельной WARP-фикстуры прошли 102 проверки point и 102 проверки spot PCF7 на каждом из разрешений 128/256/512/1024. Point-выборки делятся на 21 двухгранную и 81 трёхгранную точку на карту. Ограничение индекса t14 сняло исходное сообщение GBV при адресации проекции. Осталось проверить на игровом материале стыки с движущимся окклюдером, penumbra у края spot и normal bias на разных поверхностях. [#45](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/45)

## Q5 — PARTIAL

`03a5bf04c`. Детали раньше отбирались из camera extras только для первого каскада, с отсечением 12 м и квотой 512. Новый opt-in путь извлекает потенциальных кастеров из world XZ объёма всех CSM, включая часть объёма в сторону источника света. Размещения имеют `(slot, ordinal)` в поколении, базовую форму до fade/SSA, консервативную сферу и проходят frustum culling каждого каскада. Снимок создаётся на потоке владельца, запись получает неизменяемые пакеты. Upload расширяется по frame slot и использует штатную fence/quarantine схему.

CPU membership проверен независимым эталоном на 1800 источниках и при перестановке. Тест production-подготовки проверяет более 512 записей, пересечение прежней границы 12 м, кастер вне объёма камеры, бюджет и обнаружение пропущенного кастера. Отдельный WARP прошёл 624 caster через production packing, slot upload, Aux VS и D32 в обоих слотах. Перестановка сохранила глубину, мутация пропуска изменила ожидаемый пиксель.

На кадр отводятся 64 МиБ оценки кэша, 32 МиБ выходного снимка и 16 МиБ рабочей памяти запроса. Дополнительный upload ограничен 32 МиБ на слот. Декодирование до 256 новых слотов за вызов ограничивает работу, сохраняя очередь недостающих слотов. Количество кастеров в готовом снимке определяется данными и памятью. При `retained` прежний снимок может покрывать только часть текущего объёма. `pending`, `upload_budget`, `upload_failed` и `host_memory` фиксируются отдельно. Синтетический WARP покрывает подготовку и передачу с валидными mesh, а чтение реального CForm, отсутствующие материалы и расширение/вытеснение кэша на Jupiter остаются открытыми. Новый путь работает для солнца по явному переключателю. Включение локальных detail-теней не оценено. Геометрия деталей остаётся без динамической деформации ветром. [#46](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/46)

## Q6 — PARTIAL

`03a5bf04c`. Исходники подтверждают общую `SkinVertex`, точную pose в SC1, отдельные слоты, hysteresis и публикацию после фактического Signal. Источник скачков у живых NPC по игровым данным не установлен. Адаптивная политика и кеш сохраняют существующую семантику. Добавлены per-light назначение и диагностика membership и причин dirty.

CPU-тест охватывает pose, удаление кастера, два слота, reassignment, неизвестные данные, cancel и quarantine. WARP подтвердил SC1/SC2 cache-off/on с readback глубины и production sampling, static copy с dynamic overlay, полный и Clustered цикл с мутациями. В отдельной фикстуре production `SkinVertex` измеренное D32 изменилось с 0,25 до 0,50 в обоих слотах при изменении второй из двух костей. У полной последовательности остаются открыты общая фикстура production pose с reassignment и меняющимся VRAM-бюджетом, проверка animated bounds, а также симптом скачков в реальной сцене. [#47](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/47)

## Q7 — PARTIAL

`03a5bf04c`, `cc091853d`. Доступны elapsed frame, CSV и P95 для сеанса, время подготовки, проверки кеша, записи и команды отправки. `-ShadowPhaseTiming` добавляет измерения static, dynamic и copy к CSM и общей локальной записи. Данные читаются при штатном повторном использовании слота. Две slot-арены и разрешение пар фаз проверены на WARP с cache-off, SC1 и SC2. Статическая фаза исключает затраты, общие для всей карты, а copy включает переходы состояний вокруг копирования. Сумма фаз и elapsed local time могут различаться. Регистрация детальных пар timestamp имеет собственную стоимость, поэтому режим выключен в стандартном A/B.

По имеющимся B/F недостаточно сопоставимых кадров и отдельного измерения для PERF-01 (поиск физического ключа), PERF-02 (повтор pose) и PERF-03 (retention scratch). Эти оптимизации остаются в техническом долге. Аппаратные результаты RTX по профилям открыты. [#48](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/48)

## Владение данными и источники

Загрузкой OGF, placement и захватом изменяемого игрового состояния владеет поток игры. Геометрия и материалы используют штатные owners и registry. Детали используют отдельный CPU-снимок теневых областей, общий SRV-регистр трансформов и slot-fenced буфер для теневого прохода. FrameCB сохраняет шаг 768 байт, новые поля в хвосте находятся по смещениям 736 и 740. Сохраняются t13–t17 и root signature. Сохранены порядок Prepare → Record → Execute → Signal → CommitSubmitted, отмена до отправки и quarantine.

Границы фильтра и receiver plane сверены по документации и первичным работам. Функциональность GatherRed в legacy проверяется на Texture2DArray, включая сборку FXC. [Microsoft CSM](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps) · [Microsoft shadow depth maps](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps) · [Texture2DArray GatherRed](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/texture2darray-gatherred) · [Reeves, Salesin и Cook](https://graphics.pixar.com/library/ShadowMaps/paper.pdf) · [Valient](https://www.guerrilla-games.com/media/News/Files/GDC09_Valient_Rendering_Technology_Of_Killzone_2_Extended_Presenter_Notes.pdf)

Пары фаз и ресурсы разрешаются на штатной очереди. [Microsoft resource barriers](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12) · [Microsoft fence-based resource management](https://learn.microsoft.com/en-us/windows/win32/direct3d12/fence-based-resource-management)

Исторические задачи использованы как отдельные сценарии риска. Паритет level.details и culling sphere остаются отдельными от восстановления hemi. Для локальных источников различаются admission, покрытие проекции и sampling. Технические проверки F4.3-Q не меняют статус исторического ревью, уличного освещения NPC или GR в игровом сеансе. [#12](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/12) · [#17](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/17) · [#23](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/23) · [#24](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/24) · [#25](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/25) · [#26](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/26) · [#27](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/27) · [#28](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/28) · [#30](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/30) · [#31](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/31) · [#32](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/32) · [#40](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/40)

## Делегирование и ревью

Terra 6.1 High реально использовалась для всех четырёх ролей. Terra A реализовала загрузку данных и подготовила CPU/WARP по растительности. Terra B разработала фильтр и эталон, выявила взаимодействие FXC с динамической тестовой адресацией, проверила специализированные профили и регрессии. Terra C разработала light-space placement и CPU-проверки membership/cache. Terra D независимо проверила преобразование R4, слоты, ресурсный lifetime и причины ограничения гейтов. По её замечаниям исправлены обработка нечислового градиента и связь GPU-измерений с метаданными слота. Codex интегрировал общие файлы, реализацию point-face blending, диагностику и сборки.

## Ручной протокол

Используются один маршрут Jupiter, тот же профиль, сохранение, погода, стартовое направление и одинаковая длительность контрольных сегментов. Сначала сравнить стартовую неподвижную камеру, затем повторить движение и остановиться в одинаковых контрольных точках. Для GPU-сравнения использовать строки с `gpu_context_valid=1` и соответствующие `gpu_*` параметры. Для диагностического сравнения NPC отдельно фиксировать свет, позу и движение камеры. Для обычного A/B Debug Layer и GBV выключены по умолчанию.

Запуск Reference F выполняется предыдущим точным EXE с проверенным legacy sampling. Его идентификаторы записаны в baseline evidence. В примере используются корень восстановленного Jupiter и короткая директория для журналов. Команды выполнять из checkout F4.3-Q. `-Preflight` проверяет входы без запуска игры.

```powershell
$game = 'D:\OGSR_F4B1B5_Round2_Runtime'
$reference = Join-Path $env:USERPROFILE '.codex\worktrees\f43-performance-evolution\OGSR ENGINE fork from rtx\bin_x64\xrEngine.exe'
$candidate = (Resolve-Path '..\runs\11-final-native\bin\xrEngine.exe').Path
$runRoot = (Resolve-Path '..\runs').Path
subst S: $runRoot
$common = @{
    GameRoot = $game
    RunDirectory = 'S:\jupiter-quality'
    PerformanceMode = 'F'
    MetricsStart = 1
    MetricsCount = 0
    MetricsStride = 8
}
.\tools\dx12\run-f43f-jupiter.ps1 @common -Exe $reference -Preflight
.\tools\dx12\run-f43f-jupiter.ps1 @common -Exe $reference
.\tools\dx12\run-f43f-jupiter.ps1 @common -Exe $candidate -SunFilterQuality Performance -LocalFilterQuality Performance -Preflight
.\tools\dx12\run-f43f-jupiter.ps1 @common -Exe $candidate -SunFilterQuality Performance -LocalFilterQuality Performance
```

Профили фильтра независимы. Performance, Balanced и Quality выбирают соответственно PCF3, PCF5 и PCF7. Настройки количества каскадов и разрешения для основного сравнения совпадают с Reference F. Для локализации доступны `-SunShadowFilter LegacyAuto`, `-LocalShadowFilter Legacy`, `-NoShadowCache`, `-NoClusteredLighting`, `-DetailSunShadows`, `-View BakedHemi`, `-View ShadowVisibility -LightId <ID>` и `-LightId <ID> -ProbeFrames 120 -ProbeDelay 60`. У каждого запуска свои логи и копии профиля и сохранения. Завершение по обычному выходу из игры.

## Сборки и доказательства

- Native Release x64 **PASS**, SHA `cc091853d6597bf23ccb6fff2ffbde44e7b0ada1`. `xrEngine.exe` SHA-256 `1E0406F5258C10795A25981754B7A5A9687A5C50F0C2A352B9F0493B385E8E8C`. `xrEngine.pdb` SHA-256 `1AC3CB13CA169A91A3AF0AECA6E4BD096A89B18DA0D061D685BD1AE48DFA0A1E`.
- Feature-off DX11 Release x64 **PASS**, тот же исходный SHA. Параметры Bridge и CameraCapture выключены в отдельном checkout.
- FXC **PASS**, 21 задача, включая production VS и PS, legacy, PCF3/5/7, несколько независимых комбинаций, SM5.0 и SM5.1. Reflection подтверждает поля 736/740 и t13–t17.
- Целевые CPU/WARP-сценарии сохранены в `b26542f39`. Для повторения WARP используйте `tools/dx12/test-f43-csm.ps1 -OutputDirectory <новый каталог> -JobLibrary <gr_job_system.lib> -WarpCase <сценарий>`. Последовательность итоговых сценариев включает `legacy-diagnostic`, `continuous-1`…`continuous-3`, `continuous-banked-1`, `point-face-continuous`, `detail-casters`, `adaptive-runtime-continuous`, `clustered-lighting-continuous` и `shadow-cache-continuous`. Тесты растительности запускаются `tools/dx12/test-f43-vegetation-warp.cmd <существующий пустой каталог>` из окружения MSVC x64.
- Jupiter preflight **PASS** для кандидата и Reference F. Контрольные хэши профиля, сохранения и `fsgame.ltx` до и после совпали. Игровой runtime **NOT RUN**.
- Манифест в `artifacts/f43-quality-recovery/manifest.md` перечисляет логи, EXE/PDB, первичные и повторные результаты, а также ZIP. Технические результаты ограничены описанными сценариями. Вывод о готовности всего пакета и публикация остаются открытыми.

## Final Technical Closure — 28 ноября 2026 г.

Производственный снимок и объект независимого аудита Terra 6.1 High — `95d6f954cb42a960a8d4e393da71c334458afba5`. Terra 6.1 High отдельно проверила streaming. Базовые Q1 и Q3 приняты в техническом объёме. Статусы выше описывают предыдущий пакет.

- REVIEW-Q01. Запустите кандидат через `run-f43f-jupiter.ps1 -PerformanceMode F -DetailShadowValidation` с обычными параметрами `GameRoot`, `Exe` и `RunDirectory`. Для проверки параметров добавьте `-Preflight`. Сверяйте `ph_dx12_shadow_details` в `actual-modes.log`: requested, actual, complete, retained, reason, placements, instances и pending_slots. Поле instances суммирует записи по каскадам, поэтому один объект может учитываться несколько раз.
- REVIEW-Q02. При смене объёма неполные данные оставляют detail-тени временно выключенными и не переиспользуют прежний снимок. Остальные CSM продолжают работу. Бюджет в 256 учитывает даже неуспешные попытки декодирования. CPU-проверка использует production helper и подтверждает pending → complete, вытеснение при смещении области и ограничение бюджета. Полный игровой поток остаётся PARTIAL. Резерв пикового декодирования может остановить прогресс до освобождения места, а повторяющиеся индексы пересекающихся CSM могут исчерпать working budget до дедупликации. Вытеснение охватывает слоты вне текущего запроса. Проверки игрового CForm и переходов generation/лимита GPU upload остаются открыты. Путь opt-in.
- REVIEW-Q03. `ShadowVisibility` отображает результат production `LocalVisibility` для подходящего диагностического источника, включая смешивание граней. `PrimaryFaceVisibility` оставляет отдельную выборку ведущей грани. Режимы удобнее сравнивать с `-LightId`, поскольку результирующий вклад всех источников показывается в других режимах.
- Q6. Динамический draw в WARP SC1/SC2 использует `SkinVertex` с двумя костями, точную позу в ключе и проверяет depth/sampling для двух слотов. Отдельный Adaptive сценарий связывает сжатие и восстановление бюджета с реальными позами на физических страницах. Полный единый сценарий SC1/SC2 + reassignment + production animated bounds остаётся открытым. Cache policy сохранена.
- Q7. При metrics записываются имя цикла погоды, время среды в секундах и параметры солнца. Для GPU используются теги завершившегося present, включая `gpu_weather` и `gpu_time_of_day_s`. Имя цикла не описывает весь переход погоды. PERF-01/02/03 ждут аппаратного профилирования.

Reference F использует прежний подтверждённый EXE. Артефакты closure находятся рядом с checkout в `runs/closure-release`, проверка пакета и точные SHA приведены в манифесте closure. Публикация подготовлена локально. Игра не запускалась.

## F4.3-Q-R1 · исправления независимого ревью

Исходный срез сборки — `1d63f7246d00c5b064d960b6d7009c721e3b7352`. База сравнения — `965b323e71d87883a4f49c5689c29232365ffcfd`. Общая визуальная приёмка остаётся **PARTIAL**.

### Q-Balanced и режим detail shadows

- **CR-Q01 / #46.** Бюджет resident, резерв декодирования и пик placement storage учитываются раздельно. До декодирования освобождается место под transient. После переноса нового объекта в кеш проверяется resident. Запрос 10×1 при resident 10, transient 4, peak 14 и decode 256 дал ровно 10 декодирований за четыре вызова, без pending. При peak 12 девять слотов готовы, десятый имеет явный отказ. Повышение peak возвращает полное покрытие.
- **CR-Q02 / CR-Q05 / #46.** Уникальные slot IDs и маска каскадов формируются до проверки рабочего бюджета. Покрытие каждого каскада проверяется по обязательным слотам текущего поколения. Два слота запаса используются для prefetch и удержания. Очерёдность отдаёт приоритет обязательным слотам. При сравнении двух областей CPU подтвердил сохранение готового каскада, 4 pending в соседнем после перемещения и полное покрытие на следующем шаге. Каскад с недостающими слотами исключается из detail pass до готовности.
- **CR-Q03 / CR-Q04 / #46.** Color и shadow используют одну геометрию placement, масштаб, fade и SSA при одинаковой позиции камеры. Участие в тенях определяется по frustum каскада, в том числе за пределами видимой области камеры. Отдельные стадии подтверждают покрытие, готовность mesh и материала, подготовку пакетов, загрузку, запись и отправку. Отсутствующий выбранный mesh отклоняет каскад. Лог `ph_dx12_shadow_detail_submit` после успешного Present показывает маску фактически отправленных caster packets и отдельную маску доказанно пустых каскадов.
- **CR-Q06 / #46.** При совпадении настроек оба прохода переиспользуют данные уже декодированного слота. Кеши пока хранят отдельные копии, а CForm queries остаются на потоке владельца. Пик CDB и выделения контейнеров не измерены. Ограничение бюджета относится к оценке placement storage. Исчезновение части теней всё ещё возможно при неполном каскаде, отказе бюджета или неполных GPU-пакетах. **Q-Details остаётся PARTIAL и экспериментальным opt-in.**
- **#47.** Один сценарий WARP сравнил cache-off, SC1 и SC2 в двух слотах при смене позы, выходе кастера из light frustum, сокращении бюджета до нуля и восстановлении с новыми page IDs. Сценарий использует production `SkinVertex`, упаковку битов позы, culling по сфере, D32 readback и production `SampleLocalVisibility`. Получено по 26 сопоставленных значений, по два SC1 hit, четыре SC2 hit при активном SC2. Сфера вычисляется из синтетической геометрии и двух матриц позы. Игровой producer animated bounds реальных NPC и визуальные скачки требуют игровой проверки.
- **#48.** Timestamp и политики Adaptive, SC1/SC2, Clustered Lighting сохранены. PERF-01/02/03 остаются открыты до аппаратного профилирования. В ручном A/B сопоставляйте реальное положение камеры, погоду, освещение и время суток. Выводы о времени кадра на RTX по WARP не делаются.

CPU PASS — streaming, unique coverage, независимый переход областей, ошибка GPU mesh, geometry policy, membership и SC1. WARP PASS с Debug Layer + GBV — `animated-adaptive-cache` и `detail-casters`, без validation warnings в этих запусках. Последний сценарий проверил 624 кастера, оба слота, production VS и D32, инвариантность перестановки и мутацию пропущенного кастера. Изменения не затронули общие интерфейсы feature-off, поэтому отдельная DX11-сборка в R1 не требовалась.

Terra A работала над streaming и placement cache, Terra B над геометрией и GPU coverage, Terra C независимо проверила два зафиксированных source SHA. Для R1 повторно использованы агентские роли этапа F4.3-Q, настроенные на Terra 6.1 High. Повторное обращение не раскрывает новый идентификатор исполнения модели. Замечания ревью о девятом слоте и раздельном учёте пустых каскадов исправлены в `1d63f7246`.

### Ручная проверка R1

Два основных запуска используют сохранение Jupiter и один маршрут. Выполнять команды из checkout F4.3-Q. `-Preflight` проверяет входы без запуска. Для игрового прогона владелец повторяет соответствующую команду без `-Preflight` и завершает сессию штатным выходом.

```powershell
$reference = Join-Path $env:USERPROFILE '.codex\worktrees\f43-performance-evolution\OGSR ENGINE fork from rtx\bin_x64\xrEngine.exe'
$candidate = (Resolve-Path '..\runs\r1-20261128\release-reviewed\bin\xrEngine.exe').Path
$runRoot = (Resolve-Path '..\runs').Path
# При необходимости создайте свободный SUBST S: для сокращения пути сеанса.
subst S: $runRoot
$common = @{
    GameRoot = 'D:\OGSR_F4B1B5_Round2_Runtime'
    RunDirectory = 'S:\jupiter-quality-r1'
    PerformanceMode = 'F'
    MetricsStart = 1
    MetricsCount = 0
    MetricsStride = 8
}
.\tools\dx12\run-f43f-jupiter.ps1 @common -Exe $reference -Preflight
.\tools\dx12\run-f43f-jupiter.ps1 @common -Exe $candidate -SunFilterQuality Balanced -LocalFilterQuality Balanced -Preflight
```

Reference F использует исходный EXE Performance Evolution `0e62b8f73e0306e22e5dc01e02f999ad57804a75`, SHA-256 `AD37789E9C32BD9BE3C334196805C562291D7E585A64801F515485D7410ECA1C`. Q-Balanced включает PCF5 для солнца и локальных теней при всех оптимизациях F. У Q-Balanced detail shadows выключены. Q-Details использует тот же набор с `-DetailShadowValidation`; запуск остаётся диагностическим до подтверждения бюджета памяти и стабильности покрытия. `actual-modes.log` сохраняет requested/actual и раздельные сообщения о покрытии, pending/denied, GPU readiness и submit. Игра при подготовке кандидата не запускалась.


### Сборка и доказательства R1

**Q-Balanced — READY FOR MANUAL HARDWARE VALIDATION.** Native Release x64 и оба Jupiter preflight прошли. Adaptive включён. В обычном ручном A/B Debug Layer и GBV выключены. Профиль, сохранение и `fsgame.ltx` сохранили исходные SHA-256.

- `xrEngine.exe` SHA-256 `366A4FE2A1BCFFC66445DD58A14A1B223D81F24F60E65881CD2948E1D74CEE0B`
- `xrEngine.pdb` SHA-256 `D00FB0E536A57116D5CBE4D3DC53D1F94F26D71D2B258802E8F06733D6E2D6C1`
- Локальный пакет `f43-quality-r1-evidence.zip` содержит source diff, независимое ревью, CPU/WARP и build logs, preflight, checksum manifest и точные Git SHA. EXE/PDB хранятся рядом в `release-reviewed/bin`.
- Визуальные статусы #41 и #46–48 остаются открыты.
