> **EVIDENCE ARCHIVE — AUTHOR REPORT, NOT AN INDEPENDENTLY VERIFIED CODE REVIEW.**
> Uploaded 2026-11-22; original attachment filename: `dx12-f42-sky-integration-and-corrections.md`.
> SHA-256 of the original uploaded file (before this provenance header): `217faee8596760342d1da0aa25da4a6a61bc6a1ba4ed2db111d3c205adb2a10b`.
> The author reports local branch `feature/f42-sky-integration-and-fixes`, commit `f0b450b3b`; on 2026-11-22 this branch and SHA were **not available on GitHub**.
> This docs-only archival branch does NOT include the author's game code, binaries, PNG artifacts or Windows worktree. Relative artifact links in the report may not resolve in GitHub.
> Do not merge into `prypiat` as evidence of F4.2 code acceptance.

---

# F4.2 Corrections и игровое небо DX12

Ветка `feature/f42-sky-integration-and-fixes` основана на F4.2
`7d5dc1858922ebbbdee845b0967b44b8d679a42f`. На игровом native DX12
проверены Classic 2D, объёмные облака с погодой X-Ray, смена уровня
Jupiter → Zaton и удержание ресурсов после управляемой потери устройства.
На Jupiter устранено мерцание запечённой декали `decal_graz2`.
Долгая подготовка PSO и шейдеров GBV перенесена с первого объёмного
кадра в задачу, выполняемую во время загрузки. В сопоставимом холодном
запуске CPU-работа первого объёмного Present сократилась с 11,65 с
до 49,36 мс.
[Исходный обзор F4.2](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/prypiat/docs/dx12-f42-independent-source-review.md)
· [Задача игрового неба](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/15)

## Источник облаков

Рабочий standalone найден в `D:/OGSR_01C_B/standalone_01c_b`.
В основе `xr_3da/GameDx12Shaders/CloudRayMarch.hlsl` — `shaders/CloudRayMarch.hlsl`
с SHA-256 `103251963da9814e00486c932090239707796343534f326e161154df1a405ac5`.
Этот хеш совпадает с исходным V-SF Rain в извлечении V-Q3, занимавшемся
проверкой эквивалентности standalone. Контракты V-Q3 сохранены в
`D:/Coding Project/OGSR_V_Q3`.

Адаптация сохраняет вычисление периодического 3D Perlin/Worley-шума,
погодное поле, форму Original V-SF Rain и общее семплирование плотности
для лучей камеры и света. Начало луча и направление берутся из игровой
камеры. Детерминированный пространственный сдвиг внутри интервала
из исходного V-SF Rain ослабляет полосы на соседних лучах. Вклад
дождевого фронта меняется непрерывно по `rain_density`. Базовые значения
`clouds_color.a`, `clouds_color.rgb` и `rain_density` преобразуются в
художественные параметры облачности, окраски теней и дождевой массы.
Преобразование универсально для разрешённых игрой конфигураций погоды
и требует дальнейшей визуальной калибровки.

В адаптацию включены три compute entry points. FXC собирает
compute, vertex и pixel bytecode в промежуточные заголовки
`XR_3DA.vcxproj`, которые встраиваются в EXE. Компиляция HLSL
в игровом кадре после замера первого запуска снята. Проверка
reflection дала размеры CBV 368, 64 и 16
байт для `CloudRayMarch`, `GenerateNoise` и `GenerateWeather` соответственно.
Пакеты 01C-B-R4, экспериментальные переходы standalone и морфология
V2.3h-R02 остаются вне этой интеграции. Независимые гейты 01C-B и V2
оцениваются отдельно. [Дорожная карта облаков](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/prypiat/docs/08-roadmap.md)
· [MSBuild FXC](https://learn.microsoft.com/en-us/visualstudio/msbuild/fxc-task)

## Погода и проход кадра

`GameDx12Environment12` копируется на игровом потоке после штатного
`CEnvironment::OnFrame`. Источник цветов, направления солнца, тумана,
осадков и ветра — уже интерполированный `CurrentEnv`. Снимок хранит
имя текущей погоды, время, признаки WeatherFX и ссылки на неизменяемые
имена исходных текстур. Движение облаков следует накопленному
`wind_anim`. Положение солнца приходит из модели X-Ray. Снимок не
меняет события, дождь, молнии и выброс.

`GameDx12Sky12` пишет команды в текущий прямой список
`GameDx12NativeFrame12` после статической сцены, rigid и деталей.
Проход использует скорректированную по aspect матрицу текущего кадра,
включая диагностическую fly-камеру. D32_FLOAT очищается в 1, а непрозрачная
геометрия записывает глубину с `LESS`. Полноэкранный треугольник неба
использует `z=w`, `EQUAL` и выключенную запись глубины. Это даёт
аппаратную маску пустых пикселей без отдельной текстуры глубины.

Аналитическое небо использует градиент к цвету тумана, высоту луча и
окрестность солнца. Объёмная текстура кадра хранит линейное рассеяние
`L` и пропускание `T`. Полноэкранный проход вычисляет
`L + T * sky` в линейном пространстве, ограничивает яркость и кодирует
цвет в sRGB для существующей цели `R8G8B8A8_UNORM`. Это локальная
линейная композиция неба в пределах текущего LDR кадра F4.2.
HDR-экспозиция всей сцены и объёмный туман на геометрии требуют
дальнейшей работы.

## Режимы и ресурсы

`r_dx12_sky_quality 0` выбирает Classic 2D по умолчанию. Он рисует
аналитическое небо и один плоский облачный слой в игровой погодной
палитре. Режим `1`–`4` заменяет слой объёмом. Low, Medium, High и Ultra
используют делители разрешения 6, 4, 3 и 2, шаги луча 24, 40, 64 и 96
и шаги к солнцу 2, 3, 4 и 6. Для воспроизводимого старта есть переменная
`OGSR_DX12_SKY_QUALITY`. Classic 2D работает во время фоновой
подготовки volumetric PSO и при ошибке формата или выделения ресурсов.
Подготовка запускается при создании native viewport в существующей
GR Job System. Worker удерживает device, переданную root signature и
собственный результат. Он не записывает кадр и не обращается к погоде.
Готовый результат подхватывается на игровом потоке. Здесь же создаются
текстуры поля и слотов. Shutdown завершает задачу до проверки InfoQueue.

Оба прохода используют игровой D3D12 device и Direct Queue. Поля шума
128³ RGBA16F и погоды 256² RGBA8 создаются при первом volumetric-кадре.
После записи генерации переход `UAV → NON_PIXEL_SHADER_RESOURCE` делает
их доступными ray march. Поля становятся постоянными после отправки
списка. Для каждого из двух слотов есть свои upload constants и
выход RGBA16F. Только слот с завершившимся fence может менять
размер и дескрипторы. Каждый ray march переводит выход
`PIXEL_SHADER_RESOURCE → UAV → PIXEL_SHADER_RESOURCE`.

Запись и повторное использование frame slots, resize и shutdown
следуют уже существующей процедуре native кадра. При неизвестном
завершении ссылки на sky PSO, heap, root signatures и текстуры
передаются в тот же карантин. Temporal history отсутствует.
[Переходы состояний D3D12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)
· [Время жизни ресурсов и fences](https://learn.microsoft.com/en-us/windows/win32/direct3d12/fence-based-resource-management)

## Исправления F4.2

DX12 `LevelDetails` владеет собственным `CDB::COLLIDER`. Результат
`xrXRC::collider()` был общим внутри потока, а `XRC.box_query` обновлял
глобальный таймер. Запрос получает модель текущего уровня непосредственно
в кадре. Инициализация и сброс коллайдера следуют жизни `level.details`.
Цикл размещения, исходный seed и параметры поверхности сохранены.

Метрики `OGSR_DX12_GAME_METRICS=1` в первых 480 сценовых Present
выделяют `detail_collect_ms`, `detail_collision_ms`,
`detail_decompress_ms`, `detail_visible_ms`, `rigid_load_ms`,
`rigid_capture_ms` и `snapshot_total_ms`. Декомпрессия включает запрос
коллизий. Полный снимок включает чтение и загрузку новых моделей,
снятие поз и подготовку экземпляров деталей. `frame_ms` — интервал
между Present с ожиданиями, `render_cpu_work_ms` — интервал подготовки
и записи после Begin и до Present. GPU timestamps читаются после fence
слота. `sky_ms` измеряет Classic 2D, `cloud_ms` — ray march,
`composite_ms` — единый проход volumetric background и композиции.
Число dispatch учитывает первую генерацию полей, общий GPU интервал
также включает эту генерацию. `allocation_bytes` — суммы allocation
size GPU-текстур и upload-буферов. [Issue #16](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/16)

Диагностические native кадры и игровой `level` связали мерцание
пятна Jupiter с запечённым visual 716, MT_NORMAL, shader ID 124,
`effects\wallmarkmult/decal\decal_graz2`. Принимающая плитка —
visual 4369, shader ID 572, `default/floor\floor_tile_09`.
Границы по высоте у мешей перекрываются. При неизменных соседних
матрицах камеры цвет пятна сохранялся. При малом изменении матрицы
максимальная разница внутри ROI достигла 129/255, на контрольном
участке пола 1/255. В цветном режиме ID число видимых пикселей 716
менялось с 4 356 до 1 696 и 0 на движущейся камере.

Для всего класса `effects\wallmarkmult` добавлен проход после
непрозрачных мешей, rigid и деталей. За основу взяты
`effects_wallmarkmult.s`, `wmark.vs` и `shared/wmark.h`
из установленных R4-шейдеров CoP. Проход использует общий
`wmark_shift`, `LESS_EQUAL` без записи глубины, линейный sampler
и RGB смешивание `DEST_COLOR / SRC_COLOR`. Текстура wallmark
подаётся без базового directional light. Для общего typeless ресурса
wallmark читает отдельный UNORM SRV. Обычный материал сохраняет свою
выборку sRGB. Для `decal_graz2.dds` формат DXT5 соответствует
BC3_UNORM. В исходных серых текселях 127–128 коэффициент близок
к 1,0. При принудительном sRGB-декодировании он падал до 0,42–0,43
и образовывал тёмный контур на плитке. Динамический WallmarksEngine
остаётся отдельной задачей полноты R4. [Issue #13](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/13)

`OGSR_DX12_GAME_F42_DISCOVERY=1` выводит ID, поколение, секцию, визуал и
причину отбраковки rigid-объектов. Игровой VFS подтвердил форматы.
Freezer Jupiter ID 3150 (`dynamics\kitchen_room\freezer` из
`$game_meshes$`) использует OGF v4 типа 10 и два дочерних MT 5.
Первый имеет 996 вершин в формате `0x2` с двумя весами, материал
`models\model` и текстуру `prop\prop_freez`. Второй имеет 1 089 вершин
в формате `0x1`, тот же shader и текстуру `mtl\mtl_truba_pod_01`.
На Zaton freezer имеет ID 1348. Все четыре `breakable_object` на
Jupiter с ID 1920–1923 (`meshes\brkbl#22`–`#25` из `$level$`)
используют OGF v4 типа 10 и один MT 5. Это 8 вершин `0x1` на
модель, `models\xdistort` и DDS `pfx\pfx_dist_glass`, который
путь DX12 отклоняет как формат без FOURCC. Weighted skinning и
искажающие материалы относятся к дальнейшим
этапам переноса. [Реестр R4](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/prypiat/docs/dx12-r4-render-parity.md)

## Доказательства

Исходная F4.2 и экспериментальные сборки сравниваются отдельно.
`build-native-04` собран в Release x64 с Game Bridge и Camera Capture,
MSVC 14.44 и Windows SDK 10.0.26100.0. Шесть шейдеров скомпилированы
FXC 5.1, итоговый EXE имеет SHA-256
`8CBB0292D4CE0216B8D732F6B20A5AFD81B3D87F62C37A6BB024E32A8E25F4B5`.
После сборки в CloudWeather убрана пустая строка. Повтор FXC подтвердил
байтовое совпадение полученного CSO с собранным в `build-native-04`.
После фоновой подготовки PSO и отдельного прохода wallmarks
`build-native-07` успешно собран с теми же параметрами. Его
`source.diff` побайтово соответствует изменениям исходников до
`c442d82234142a18b61640cd577768a1a4e1270f` с нормализованными
переносами строк. SHA-256 EXE
`8F849B29E6FF6E622DB21AFBEA91BEE5BB7F108F3043C12B349E50F1ACA17C1E`.
Финальный `build-native-08` успешно собран на тех же MSVC и SDK.
Изменения исходников двигателя в сохранённом `source.diff` совпадают
с состоянием `4e28af8f456d3d850a3095461a1c233256fa640e`.
SHA-256 EXE
`D0E2450144A9E88BD60DB82BD55403316A533454350583D5F1F84B9B38C86A1B`.
Логи, input manifests, PNG и проверочные JSON хранятся в
`artifacts/F4.2-SKY-INTEGRATION/runs` рабочего дерева.

### Серии на сборке 04 и предшествующем прототипе

- `jupiter-volumetric-01` на ранней сборке подтвердил 30 Present
  120–149 и fence 126–155. На той же камере сохранились 222 draw и
  1 566 экземпляров деталей F4.2. В приближённой маске по прежнему
  цвету очистки изменились 427 669 из 427 669 пикселей неба и 574
  из 493 931 остальных пикселей. Участки неба при неподвижной камере
  показывают смещение порядка 1 пикселя между первым и последним
  кадром, накопленный игровой ветер изменился на 0,098969 по второй
  оси. В этом запуске были видны полосы в облаках и зарегистрированы
  13 455 мс CPU работы на первом кадре. По этим данным shader
  compilation перенесена в MSBuild, добавлен ограниченный spatial jitter.
- `jupiter-rain-life-01` на ранней сборке прошёл объёмный режим,
  Classic 2D, возврат в объёмный режим, resize, смену игрового
  времени и загрузку Zaton как новой сессии. Был сохранён исходный
  блок из 30 кадров. Поздние этапы подтверждаются логами.
  Запрошенный дождь в этом запуске не подобрался из VFS.
  `zaton-rain-01` подтвердил разрешённый X-Ray цикл `default_thunder`
  и `rain=1`, но завершился до записи полной серии PNG.
- `jupiter-interior-classic-final-01` на EXE сборки 04 сохранил
  30 PNG интерьера с дверью, светильниками и ящиками. В кадре 120
  отправлены 115 rigid-объектов в 130 draw. `mode=classic`,
  `dispatches=0`, выход 0. Полное выделение памяти неба 131 072 байта.
- `jupiter-r4-runtime-off-final-01` и `zaton-r4-runtime-off-final-01`
  проверили штатный R4 на том же EXE с выключенным native DX12.
  Оба подтвердили загрузку игры, сохранили PNG и штатно завершились с
  кодом 0.
- `jupiter-woodland-final-02` сохранил 30 PNG за
  Present 120–149 на EXE `8CBB…`. В первом кадре
  сопоставления остались 222 draw и 1 566 экземпляров травы.
  После Present 128 повернулась камера. Серию использовали для
  проверки окклюзии и поворота.
- `zaton-rain-final-01` прошёл с фактическим `default_rain`
  и `rain=0,4952…0,4943` из снимков X-Ray. 30 PNG, штатный
  выход 0. Медианы по кадрам 120–149 — ray march 2,714 мс,
  композитинг 0,020 мс, весь GPU кадр 6,687 мс.
- `jup-life-final-01` дал 30 PNG с ясным `default_clear` в 13:00,
  лог переключения на Classic с Present 341 и обратно на Volumetric
  с 441, resize до 1024×576, скачок игрового времени и новый уровень
  Zaton с session 2. PNG относятся к серии до переключения,
  этапы после него подтверждает журнал.
- `jup-night-move-final-01`, `jup-dawn-final-01` и
  `jup-dusk-final-01` сохранили по 30 native PNG ночью, на рассвете
  и на закате по времени X-Ray. Ночной Medium покрывает поворот и
  линейное движение камеры. На неподвижном рассветном Low медианное
  смещение облачной структуры в ROI составило около 0,41 px, при
  изменении поля ветра с 0,516407 до 0,631712. В контрольном
  ROI геометрии 99,792% пикселей сохранили точный RGB. В закатном
  Ultra с `default_cloudy` ray march занял около 15,1 мс
  и выделение ресурсов 21 102 592 байта. Это разные ракурсы и
  погодные условия. Прямой диск солнца не был виден на рассветном
  кадре, который снят под облачным слоем.
- `jup-weather-transition-final-02` вызвал штатную смену
  `default_clear → default_rain`. Снимок зафиксировал
  `rain=0` на первых кадрах и `0,4847` после смены. За 30 PNG
  High rain плавно снизился с 0,4342 до 0,4230. Это подтверждает
  получение изменяющихся интерполированных значений после смены
  цикла. Плавное начало дождя не покрыто серией.

В успешных native проверках InfoQueue на shutdown подтверждает
`debug=1`, `gbv=1`, `gbvVerified=1` и
`warning/error/corruption/discarded=0/0/0/0`. Первый финальный
woodland-прогон с активным Win32 input desktop `Screen-saver`
записал командные списки и штатно завершился. Видимые Present и PNG
появились после выхода Windows из экранной заставки.
[D3D12 Debug Layer и GPU-based validation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-d3d12-debug-layer-gpu-based-validation)

### CPU и GPU

На RTX 5070 Laptop с GBV для одинакового фиксированного ракурса
Jupiter использовались Present 100–470, 371 измерение каждого
показателя. Ниже приведены p50/p95/p99 в мс.

- F4.2 без неба — интервал Present 11,477 / 15,968 / 20,063,
  полный GPU кадр 0,829 / 0,853 / 1,390.
- До переноса shader compilation в сборку — интервал Present
  16,293 / 21,317 / 27,501, CPU подготовки и записи
  4,142 / 5,082 / 5,536, весь snapshot
  2,511 / 3,144 / 3,464. Полный GPU кадр 2,357 / 2,523 / 2,583,
  ray march 1,610 / 1,679 / 1,742, композитинг 0,022 / 0,027 / 0,027.
  После разогрева выделялось 18 219 008 байт и выполнялся один
  dispatch на кадр. В F4.2 нет эквивалентного CPU таймера для этого
  сравнения.
- Classic 2D на EXE сборки 04 — sky GPU в помещении
  0,0052 / 0,0116 / 0,0132.

В `jupiter-woodland-final-02` шейдеры уже собраны заранее.
Тем не менее CPU-работа первого кадра заняла 13 273 мс.
Сопоставимый участок на неподвижной камере Present 70–125 дал
ray march 1,619 / 1,692 / 2,093 мс и `snapshot_total_ms`
2,218 / 2,711 / 2,772 мс. Измерение первого кадра показало
необходимость фоновой подготовки PSO.

На сборке 07 GR worker создал PSO за 884 мс. Первый объёмный
кадр по-прежнему занял 11 653 мс CPU при GBV. В default режиме
GBV инструментирует шейдеры на первом использовании PSO.
Native device теперь включает соответствующий текущему patch mode
front-load флаг до создания командной очереди. Readback проверяет,
что режим GBV сохранился и флаг применён.
[Параметры инструментирования GBV](https://learn.microsoft.com/en-us/windows/win32/api/d3d12sdklayers/ne-d3d12sdklayers-d3d12_gpu_based_validation_pipeline_state_create_flags)

В `jupiter-woodland-prewarm-build08` тот же режим GBV подтвердил
`gbv_front_load=1 patch_mode=2 flag=2`. Первый Present показал
220 draw и три dispatch, включая генерацию полей, в Volumetric
Medium. CPU-работа первого кадра снизилась с 11 653,31 до
49,36 мс, из них snapshot 38,55 мс. Завершившаяся во время загрузки
задача создания и инструментации PSO заняла 12 914,91 мс.
Сохранено 30 кадров Present 120–149.
Идентификаторы fence 126–155. Игра штатно вышла с кодом 0,
InfoQueue на shutdown с верифицированной GBV дала четыре нуля.

`jupiter-lifecycle-prewarm-build08` повторил 30 native кадров
с `default_rain`, переход в Classic и обратно, 1024×576,
скачок времени X-Ray и загрузку Zaton в session 2. Выход 0,
InfoQueue с GBV — ноль warning, error, corruption и discarded.
Переключения после завершения серии подтверждены логом.

В `jup-rem08` на финальном EXE работал Volumetric Medium.
После отправки списка для Present 80 тест вызвал
`ID3D12Device5::RemoveDevice`. Получены 0x887A0005,
остановка отправки native кадров и удержание ресурсов при
неизвестной завершённости. Последний успешный Present — 79.
Процесс дошёл до штатного shutdown с кодом 0. Это тест управляемой потери устройства
в процессе. Системный TDR имеет статус **NOT RUN**.

На финальном EXE использовали 371 измерение в неподвижном ракурсе
Jupiter с 222 draw и Medium. Ниже приведены p50/p95/p99 в мс.

- Интервал Present 13,995 / 18,669 / 25,070. CPU подготовки и
  записи 3,496 / 4,450 / 4,854. Snapshot
  1,973 / 2,765 / 3,159. Обход деталей
  1,383 / 2,026 / 2,242, декомпрессия после заполнения кэша
  0,0000 / 0,0001 / 0,0001.
- Полный GPU кадр 2,404 / 2,562 / 2,825, ray march
  1,617 / 1,675 / 2,036, композитинг
  0,0215 / 0,0270 / 0,0273. Один dispatch в штатном кадре,
  18 219 008 байт выделений облачных ресурсов и upload CB.

Итоговый JSON `timings-p100-p470.json` сохранён в каталоге прогона.

`jup-fast08` на финальном EXE использует диагностическое
перемещение камеры с заданным дрейфом 45 м/с вдоль улицы Jupiter.
Записано 30 native PNG. В кадрах 30–148 подготовки и записи
p50/p95/p99 составили 6,810 / 9,790 / 11,114 мс.
Snapshot — 2,535 / 3,580 / 3,876 мс. Декомпрессия деталей
0,0001 / 0,8872 / 0,9620 мс с максимумом 1,113 мс. Пик
collision query 0,338 мс. В этой серии лимит деталей 256
давал максимум 1 792 ожидающих слота в Present 30. Очередь
опустела после Present 37 и оставалась пустой во всей серии
120–149. В Present 120 видимы и отправлены 3 469 деталей.
Интервал Present с игровым циклом дал p95/p99 30,26 / 75,44 мс.
В двух кадрах с наибольшими интервалами CPU-работа подготовки
и записи заняла 7,52 и 6,71 мс. Причина пиков осталась открыта.

`jup-air08` сохранил 30 последовательных кадров вращающейся
камеры в точке `(-44, 120, 196)`, Present 120–149,
fence 126–155. Выход 0, InfoQueue с GBV — ноль warning, error,
corruption и discarded. В серии p50/p95/p99 CPU подготовки
и записи составили 9,663 / 13,217 / 14,008 мс, полного GPU
кадра — 6,702 / 7,568 / 7,807 мс. На снимке видны рельсы
и край отрисованного рельефа в нижнем ракурсе с высоты.
Серия покрывает высотную панораму с вращением камеры.

При первом появлении объектов `rigid_load_ms` достиг 27,73 и
50,64 мс на первых двух Present раннего Jupiter-прогона. В замер
входят CPU чтение и разбор ассетов, DX12 upload и ожидание fence
общей Direct Queue. К третьему кадру показатель снизился до 0,11 мс.
Декомпрессия в этом тесте заняла максимум 1,07 мс. К Present 41
обработаны все 9 989 слотов рабочего окна. Лимиты 96 OGF и 256
слотов сохранены. Раздельные замеры загрузки и ожидания требуются,
прежде чем менять порядок публикации rigid-ресурсов в кадре.

### Декали и неподвижная камера на сборках 07 и 08

Matched demo `f42_floor_motion` использует 24 исходные view matrices.
`jupiter-decal-id-01` до правки, `jupiter-decal-color-after-01`
и `jupiter-decal-id-after-01` после неё сохранили по 30
последовательных Present. В области 350×130 до правки visual 716
полностью исчезал на пяти кадрах. После правки он виден на всех
30 кадрах. Максимальное изменение площади маски между соседними
кадрами снизилось с 4 575 до 166 пикселей. Оба after-прогона
штатно вышли с кодом 0 и чистой InfoQueue. В
`jupiter-decal-r4-motion-01` сохранён R4 PNG в движении по той же
траектории. Этот matched R4-запуск был завершён принудительно.
Чистый R4 shutdown подтверждают отдельные feature-off прогоны выше.

Повтор `jupiter-decal-color-after-08` на финальном EXE сохранил
30 кадров той же траектории с отдельными camera hashes и
возрастающими fences. Яркость пятна после UNORM исправилась
без заметного изменения принимающей плитки. В мировых точках
ядра и края медианная encoded luma изменилась с 44,18 и 49,00
до 123,81 и 123,74. Контрольная плитка сохранила 143,75 и
143,80 в двух запусках. На R4 и финальном DX12 виден
локальный коричневый след. Штатный выход 0, Debug Layer,
GBV и InfoQueue чистые. Отчёт и скрипт измерения лежат в
`jupiter-decal-color-after-08/comparison.md` рядом с PNG.

`jupiter-woodland-prewarm-build07` сохранил 30 PNG при фиксированной
траектории и 222 draw. Ветер X-Ray изменился с 0,532009 до 0,632632.
Три области неба дали максимум корреляции при сдвиге на 1 px.
Кадры с шатанием камеры в более ранних запусках исключены из этого
расчёта.

### Материалы для проверки

- [Сборка 08 и контрольная сумма EXE](../artifacts/F4.2-SKY-INTEGRATION/runs/build-native-08/exe-sha256.txt)
- [Неподвижная камера Jupiter, 30 кадров](../artifacts/F4.2-SKY-INTEGRATION/runs/jupiter-woodland-prewarm-build08/frames/manifest.json) · [Замеры](../artifacts/F4.2-SKY-INTEGRATION/runs/jupiter-woodland-prewarm-build08/timings-p100-p470.json)
- [Дождь на Zaton, 30 кадров](../artifacts/F4.2-SKY-INTEGRATION/runs/zaton-rain-final-01/frames/manifest.json) · [Смена режимов и уровней на сборке 08](../artifacts/F4.2-SKY-INTEGRATION/runs/jupiter-lifecycle-prewarm-build08/summary.md)
- [Быстрый пролёт Jupiter, 30 кадров](../artifacts/F4.2-SKY-INTEGRATION/runs/jup-fast08/frames/manifest.json) · [Замеры](../artifacts/F4.2-SKY-INTEGRATION/runs/jup-fast08/timings-30-148.json)
- [Панорама Jupiter, 30 кадров](../artifacts/F4.2-SKY-INTEGRATION/runs/jup-air08/frames/manifest.json) · [Замеры](../artifacts/F4.2-SKY-INTEGRATION/runs/jup-air08/timings-120-149.json)
- [Декаль Jupiter до и после](../artifacts/F4.2-SKY-INTEGRATION/runs/jupiter-decal-color-after-08/comparison.md) · [Управляемая потеря устройства](../artifacts/F4.2-SKY-INTEGRATION/runs/jup-rem08/summary.md)
- [Регрессия R4 на Jupiter](../artifacts/F4.2-SKY-INTEGRATION/runs/jupiter-r4-runtime-off-final-01/summary.md) · [Регрессия R4 на Zaton](../artifacts/F4.2-SKY-INTEGRATION/runs/zaton-r4-runtime-off-final-01/summary.md)

## Воспроизведение

`ogsr_engine/Layers/xrRenderDX12/tests/f42-sky/run-sky-case.ps1`
использует существующий native capture harness. Укажите
`-Worktree`, `-Exe`, `-RunsRoot`, уникальный `-Run` и
`-Python` для декодирования PNG. `-Save b23_zaton -Weather Rain
-TimeOfDay 13:00 -Quality 2` выбирает дождь через доступные циклы
VFS. Параметр `-Lifecycle` в Jupiter включает проверку режимов,
resize и загрузки Zaton. `-R4` запускает исходный рендерер.
Нужен активный рабочий стол Windows.

`summarize-f42-timings.py` принимает игровой лог и диапазон
Present. `compare-woodland-frames.py` сравнивает выбранный ракурс
с базовым PNG F4.2. `inspect-cloud-motion.py` измеряет движение
в явно выбранных областях и сверяет последовательность Present/fence
со значениями ветра из журнала.

## Границы приёмки

V2 и общая художественная приёмка volumetric продолжают жить
в своём треке. В текущей игре небо компонуется в линейном
пространстве внутри LDR кадра F4.2. Освещение геометрии,
автоэкспозиция, вода, динамические wallmarks и эффекты выброса
относятся к оставшемуся паритету R4. Сборка без флага Game Bridge
не включает `GameDx12Sky.cpp` и шесть FXC-шейдеров, что проверено
вычислением состава проекта MSBuild. Игровые R4 регрессии
Jupiter и Zaton выполнены с выключенным native runtime.

Для последовательности lifecycle снят один непрерывный блок
кадров. Изменение режима, разрешения и уровня подтверждено
отдельными записями журнала. Выделенные 30-кадровые серии
для каждого промежуточного состояния lifecycle, отдельные
ракурсы у окна и на железной дороге, настоящий скачок позиции
камеры и матрица WeatherFX остаются открытой частью приёмки.
Производительность измерена на RTX 5070 Laptop с включённой GBV.