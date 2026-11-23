# F4.3-FR2 — системное освещение и тени Jupiter

## Статус и объём независимого ревью

Gameplay / GPU acceptance: **NOT VERIFIED**. Последняя ручная проверка владельца показала, что локальные тени появились в помещениях, а NPC в интерьерах выглядят корректно. Серьёзные проблемы солнечного освещения и теней остаются на улице у NPC и геометрии. Локальный свет проходит через непрозрачную геометрию на расстоянии, а вблизи затенение работает корректнее. Причины оставшихся симптомов не установлены.

- **F4.3-F** — ограниченный отбор локальных источников и теневых карт, индекс кастеров, измерение стоимости кадра, обновлённые terrain BmmD, baked hemi, вода и материалы.
- **FR1** — near-volume по матрице камеры, упорядочивание источников и слот-гистерезис, sky-hemi CROS для NPC/rigid, правки координат локальных теней и воды, `LightTerms`.
- **FR2** — исправление перезаписи `t6`, адресная диагностика light record, новые debug views и контролируемые A/B режимы.

## A. Аудит до исправлений

| Дефект | Доказательства | Возможные причины | Границы доказательств | Диагностика |
| --- | --- | --- | --- | --- |
| Системное исчезновение локального света | В `01:55:19` ID 548 выбран в 13 из 15 отсчётов, два раза отсутствует в spatial query. В `01:40:31` несколько ID показывают смену GPU и теневых слотов. | Доказано затирание общего SRV локальных карт. Для сырых вкладов остаются spatial admission, лимит 16 и входы пиксельного шейдера. | CPU position совпадает с упакованной. Эти данные не доказывают, что пиксельный шейдер получил правильную shadow SRV или что все исчезновения приходятся на карту. | Default против NoLocalShadows, разложение local terms, фиксированные ID и замороженная запись. |
| Утечка через стену | Стены засвечены по ручному наблюдению, shadow_budget и наличие назначенных карт видны в логах. | Для света с картой нарушение SRV доказано. Источники без карты не имеют геометрической окклюзии. | Посекундные логи не сопоставлены с одним пикселем стены. | ShadowDepth/ShadowVisibility одного ID и локальный вклад до/после карт. |
| Темнота наружных объектов | Ручной FAIL после FR1. В E/F/FR1 различаются hemi и матрица/фильтр солнечной карты. | Солнечная видимость, CROS, материал и отличия R4 shading/exposure. | В переданных логах нет world-space receiver и поканальной солнечной диагностики. Затирание относится к t6, солнечный t2 выделен отдельно. | Раскладка LightTerms, Hemi, Lighting, Albedo, Normals и независимый NoSunShadows. |
| Тени NPC | Есть команды записи dynamic shadow casters. Visible и shadow используют общий skinning. | Затёртый локальный SRV; возможные отдельные caster coverage или bias ошибки. | Причина конкретного силуэта неизвестна без карты и ракурса. | Фактический слой карты, глубина и видимость для point/spot. |
| Пиксельный рисунок | Непостоянный ручной симптом. | Доказанное нарушение типа локального SRV позволяет неопределённый результат чтения. Материал, UV, normal и lifetime требуют отличения. | Причина конкретного рисунка открыта, InfoQueue обычного запуска недоступна. | Повтор на той же мировой поверхности в Albedo, Normals, WorldPosition, ShadowDepth и LocalProbe. |
| Вода | Визуальный PARTIAL. | Ограниченный SSR, fallback и состав opaque depth. | FR1 FXC без X4000. В FR2 нет кадра с новым расхождением глубины. | Аудит порядка проходов и slot-owned t3–t5, ручной WaterNormals/WaterReflection. |

## B. Логи и доказанный путь потери света

Проверены все пять журналов владельца. Ниже приведены сводные результаты. Три записи F4.3-F суммарно содержат 49 отсчётов ID 548 внутри объёма. Источник включён в lighting в каждом отсчёте, в 26 отсчётах ему не выделена карта. В обычном FR1 `01:40:31` ID 489 выбран 22 раза из 22 и затенён 18 раз, ID 548 выбран 21 раз из 21 и затенён 5 раз, ID 549 выбран 17 раз из 17 и затенён 5 раз. ID 494 один раз потерян на CPU. ID 490 четыре раза выбран без карты. Это независимые примеры разных переходов.

Приоритетный запуск `01:55:19` использовал `-SceneLog -LightId 548` и `LightTerms` (view 13). Для ID 548 получено 13 выбранных записей из 15. Например, при `01:55:32/35/41/44` CPU фиксировал слоты 11/8/15/14 и теневые слои 2/−1/2/−1. У двух отсутствующих источников (кадры 2084 и 2851) причины `not_in_spatial_query`. Позиции у выбранных записей совпадают между источником и упакованной константой. GPU index может измениться при изменении состава отсортированного массива. Журнал отражает данные упаковки на CPU, без считывания GPU.

**Доказанный сбой локальных теней.** `CreateTextures` записывал live `Texture2DArray<R32_FLOAT>` для восьми граней/карт в heap slot 16395. Цикл заполнения null-слотов стартовал на 16386 и шёл вплоть до `kSceneDescriptorCount`. При достижении 16395 он перезаписывал t6 null `Texture2D<R8G8B8A8_UNORM_SRGB>`. После этого даже корректные матрицы, DSV, барьеры и команды записи тени не давали пиксельному шейдеру доступа к карте. FXC подтверждает, что PS объявляет t6 именно `texture2darray`. При обращении к null SRV другого типа D3D12 оставляет результат неопределённым. Это общий дефект для point и spot, который становится видимым при назначенном слое. Источники с sentinel 0 в GPU поле слоя возвращают локальную видимость 1 до чтения t6. [Контракт Microsoft](https://learn.microsoft.com/en-us/windows/win32/direct3d12/descriptors-overview#null-descriptors)

**Границы доказательства.** Из журналов нельзя связать каждый момент полного исчезновения с неправильным t6. Два отсутствия ID 548 относятся к ранней стадии, до упаковки. Пространственный запрос и лимит 16 меняют состав набора в зависимости от камеры. Это альтернативная причина для невидимых источников при отключённых локальных картах. FR2 сохраняет live-отбор и даёт эксперимент для его проверки. Для локального света X-Ray frustum и near query проверены отдельно. ID 548 внутри выбранных в оба запроса кадров сохраняет активность. Световой цикл получает world-space позицию из интерполированного результата VS и world-space нормаль. Спектральный вклад определяется расстоянием, конусом, косинусом и shadow visibility. В формулах local attenuation отсутствует положение камеры.

**Тени и sunlight.** Для солнечной карты t2 слот 16385 не попадает в ошибочный цикл. В F4.3-E и F4.3-F прямой член имеет форму `SunColor × N·L × visibility`. F изменил охват и фильтрацию карты и добавил baked/terrain hemi. FR1 подал отдельный sky-hemi для aux VS. R4 пропускает irradiance через environment кубы, материал, BRDF и экспозицию, тогда как DX12 сохраняет текущую упрощённую композицию. В Native DX12 нет установленных данных о причине именно уличной темноты после FR1. Для её проверки `-NoSunShadows` выключает карту t2, сохраняя солнечный источник, hemi и локальные тени. Математика солнечного света и материалы остаются прежними.

**NPC и артефакт.** Шесть point-матриц соответствуют порядку `+X,-X,+Y,-Y,+Z,-Z`. Raster и sampling используют одну матрицу слоя, одинаковую convention UV и D32 сравнение LESS_EQUAL. CPU собирает карты из индекса opaque/cutout и существующих rigid/skinned draw calls. `SkinVertex`, pose buffer, child mask и cutout-порог используются в соответствующих проходах. После исправления t6 отдельные контуры NPC и воспроизводимость пиксельного рисунка остаются неподтверждёнными.

## C. IX-Ray и D3D12

- На закреплённом SHA `7b74303ed0f2d93cf63b65983e3c819f323ebf5c` сверены `light::vis_prepare/vis_update`, сбор `LIGHTSOURCE` и `Lights.add_light` в `CRender::render_main`, `light_Package::sort`, `light::set_active/set_shadow/spatial_move` и создание/обновление в Light_DB. IX-Ray различает near-volume, HOM/LOD и игровые флаги. Его stable ordering сохраняет порядок равного приоритета. Native DX12 использует собственный ограниченный отбор. [Исходники IX-Ray xrRender](https://github.com/ixray-team/ixray-1.6-stcop/tree/7b74303ed0f2d93cf63b65983e3c819f323ebf5c/src/Layers/xrRender)
- Проверены `lights_render`, `accum_point`, `accum_spot`, spot raster/sampling viewports, `shadow_local`, `shadow_sun`, `pcf_3x3` и `shadow_model`. IX-Ray разделяет shadowed и unshadowed packages. Atlas borders относятся к его размещению spot-карт. Наша причина находится в границах диапазонов SRV. Референс использован для сверки, source code IX-Ray в FR2 не переносился. [Исходники R4](https://github.com/ixray-team/ixray-1.6-stcop/tree/7b74303ed0f2d93cf63b65983e3c819f323ebf5c/src/Layers/xrRenderPC_R4) · [Лицензия и существующая атрибуция](../../docs/third-party/ixray-provenance.md)
- Сверены `R_sun`, `accum_direct`, `phase_smap_D`, `CROS_impl::update/calc_sun_value/calc_sky_hemi_value/prepare_lights`, `deffer_base` и `forward_base`. Static vertex, CoP `s_hemi`/lightmap UV, terrain BmmD и object CROS остаются отдельными входами. Свет в DX12 берётся из погодного снимка в world space. Для решения расхождений нужна ручная съёмка совпадающих ракурсов в E/F/FR1/FR2 и, при доступном runtime, IX-Ray R4. Launcher `-R4` запускает текущий OGSR R4. [Солнце IX-Ray](https://github.com/ixray-team/ixray-1.6-stcop/blob/7b74303ed0f2d93cf63b65983e3c819f323ebf5c/src/Layers/xrRender/R_sun.cpp) · [Шейдеры IX-Ray](https://github.com/ixray-team/ixray-1.6-stcop/tree/7b74303ed0f2d93cf63b65983e3c819f323ebf5c/gamedata/shaders/d3d11)
- Root parameter 0 задаёт b0. Reflection показывает `LightingFlags` offset 192, `LocalLights` offset 208, 16 записей по 64 байта. C++ проверяет offsets 0/16/32/48 в записи. FrameCB округлён до 2048 байт и записывается в выдержавший fence слот. Локальный DSV/SRV используют R32_TYPELESS ресурс с D32_FLOAT и R32_FLOAT views. Одна direct queue упорядочивает DEPTH_WRITE и PIXEL_SHADER_RESOURCE. Для обычных запусков InfoQueue недоступна. [Создание дескрипторов Microsoft](https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-descriptors) · [Барьеры Microsoft](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)
- Вода проверена по `water.ps.hlsl` и `shared/watermove.hlsli`. DX12 копирует opaque color после непрозрачных static/rigid/skinned и использует их глубину через read-only DSV/SRV. SSR FR1 использует центр того же depth texel, восемь шагов, погодный fallback и существующий Fresnel. FR2 фиксирует границы heap t3–t5. Отдельный дефект Fresnel, refraction или смешивания с NPC по имеющимся данным не установлен. [Вода IX-Ray](https://github.com/ixray-team/ixray-1.6-stcop/blob/7b74303ed0f2d93cf63b65983e3c819f323ebf5c/gamedata/shaders/d3d11/water.ps.hlsl)

## D. Дельта и стоимость

Снимок DX12 перед серией F/FR1/FR2 — опубликованный коммит `b8dca38665d4bc24b1cca6a63d94be36a2ad3972`. Каждый этап представлен отдельным коммитом. F включает накопленные предпосылки F4.3-B/E.

Изменены четыре файла и этот отчёт.

- `ogsr_engine/xr_3da/GameDx12StaticScene.cpp` — разные границы инициализации для воды, массива t6 и terrain. LocalShadow получает ровно одну запись подходящего типа, включая null при отключённых картах. Для диагностики добавлены `LocalRaw`, `LocalProbe`, `LightIndex`, `WorldPosition`, реальный слой в `ShadowSlot` и изолированное отключение солнечной карты.
- `ogsr_engine/xr_3da/GameDx12Bridge.cpp` и `GameDx12Lighting.h` — bounded trace с camera/slot/packed record, A/B фиксированного набора живых ID и одного замороженного record. Source selection и цель в переданном GPU-буфере выводятся отдельно. Состояние probe сбрасывается по session/epoch; указатели источников в снимке отсутствуют.
- `tools/dx12/run-f43f-jupiter.ps1` — имена режимов, проверка аргументов и поддержка `-NoSunShadows`, `-FixedLightIds`, `-FreezeLightRecord`, `-ProbeFrames`, `-ProbeDelay` и `-ProbeOrigin`.

Сохранены бюджеты 16 lights, две spot-карты, шесть point-граней и одна solar map. Ресурсы визуализации, root tables и выровненный размер frame slot сохранены. В FrameCB занято ещё 16 байт внутри прежних 2048 байт. Штатное вычисление attenuation и visibility использует те же выборки и условия. Изменение SRV делает эти выборки фактическими, поэтому GPU-время FR1 с ошибочным t6 не измеряет стоимость корректного чтения карт. Число геометрических проходов и обход индекса кастеров сохранены. Fixed-selection A/B выполняет один дополнительный пространственный запрос на кадр в окрестности стартовой позиции. Frozen mode прекращает этот запрос после захвата выбранного источника. Диагностика ограничена окном до 600 кадров, явный trace печатает один ID на кадр.

По 133 окнам `01:40:31` при 31 375 кадрах пересчитано 78,45 FPS, 4,709 мс GPU, 4,260 мс CPU и 0,122 мс ожидания. `01:55:19` с LightTerms и без копий воды является отдельным диагностическим запуском. Стоимость и стабильность FR2 на той же сцене ещё не измерены. Отдельно перепроверить границу 10% при одинаковом ракурсе, наборе карт, погоде, разрешении и настройках.

## E. Техническая приёмка

- **FIXED IN CODE** — перезапись SRV локальных теней. CPU replay компилирует блок инициализации FR1/FR2 с mock D3D12 и точными индексами. Проверены две записи FR1, одна правильная запись FR2, typed null, границы sun/water/terrain и независимые ID 489/490/548 в режимах A/B.
- **HLSL PASS** — семь модулей SM5 с фактическим FrameCB, zero warnings. PowerShell 5.1 AST/argv PASS для live, fixed, frozen, WorldPosition, NoSunShadows и R4. `git diff --check` PASS для целевого worktree и дельты до/после FR1.
- **BUILD PASS** — один XR_3DA Release x64, 1 мин 41,98 с, 0 ошибок, 79 предупреждений. SHA-256 `FBDDCC1432E03DC6BF782E10C03CA82F7AF1A02F9A91701FA90B0AD84E90F380`. EXE и PDB FR1 сохранены.
- **GAMEPLAY / GPU ACCEPTANCE: NOT VERIFIED.** Наблюдения владельца уточняют симптомы. Локальный свет, утечка через стены, наружное солнце, контур NPC, стабильность кадра и производительность требуют независимого подтверждения. Технический результат подтверждает нарушение GPU binding, визуальная связь с каждым симптомом требует кадра.
- **OPEN** — выключение отдельных источников до GPU при неподтверждённой причине отказа пространственной выборки или лимита, утечка от unshadowed lights, причина наружной темноты, непостоянный рисунок и качество воды. Для unshadowed lights отдельный этап может проверить receiver-aware priority существующих слотов или sector/portal gates на реальных стенах и проходах. Видимость секторов доступен на уровне R4 и не даёт попиксельной окклюзии.
- **F8 NOT TESTED** — лужи. **F9 BLOCKED** — нужен visual/object ID смещённого меша. Геометрия, вода и terrain сохраняют существующие проходы.

## F. Сценарии владельца

~~~powershell
$run = '.\tools\dx12\run-f43f-jupiter.ps1'
$gameRoot = Read-Host 'Путь к установленному runtime Jupiter'

# Базовый trace. Двигать камеру после указанной задержки в кадрах.
& $run -GameRoot $gameRoot -SceneLog -LightId 548 -View LocalProbe -ProbeDelay 120 -ProbeFrames 600
& $run -GameRoot $gameRoot -SceneLog -LightId 548 -View LocalProbe -ProbeDelay 120 -ProbeFrames 600 -NoLocalShadows

# B. Те же живые ID вокруг стартовой позиции комнаты.
& $run -GameRoot $gameRoot -FixedLightIds '489,490,548' -LightId 548 -View LocalRaw -NoLocalShadows -ProbeFrames 600

# C. Одна захваченная запись, без карты.
& $run -GameRoot $gameRoot -FreezeLightRecord -LightId 548 -View LocalRaw -ProbeFrames 600

# Диагностика совпадения приёмника, frame CBV и видимости солнца.
& $run -GameRoot $gameRoot -SceneLog -LightId 548 -View LightIndex -ProbeFrames 600
& $run -GameRoot $gameRoot -SceneLog -View WorldPosition -ProbeOrigin '-46,5,196' -ProbeFrames 600
& $run -GameRoot $gameRoot -SceneLog -View LightTerms
& $run -GameRoot $gameRoot -SceneLog -View LightTerms -NoSunShadows
~~~

1. В стартовом помещении выберите конкретную непрозрачную поверхность и установите её visual ID при возможности. Зафиксируйте положение камеры и медленно повернитесь так, чтобы та же мировая поверхность оставалась в кадре. Возьмите моменты до, во время и после пропадания. Используйте `-SceneLog` без ID для первичного поиска и повторите с несколькими независимыми ID. Числа 489/490/548 — примеры из журналов, ID следует сверять в каждом процессе.
2. По `ph_dx12_f43fr2: frame` зафиксируйте frame, camera, forward, slot, offset CBV и количество кандидатов. В строках `light_record` и `ph_dx12_f43fr1: light_id` проверьте source/packing, production slot, реально упакованный slot и shadow layer. `LightIndex` читает из GPU FrameCB красный `(index+1)/16`, зелёный `(frame mod 256)/255`, синий `frameSlot/2`. Фиолетовый означает отсутствие выбранного ID в этом режиме. В WorldPosition RGB кодирует координаты как `0.5 + (world - origin)/16` с насыщением. Меняйте origin под выбранную стену, если компонент вышел за ±8 м.
3. `LocalRaw` показывает исходный RGB-вклад без карты, `LocalProbe` показывает в RGB соответственно q-linear falloff, локальную видимость и итоговую линейную яркость от ID до применения albedo. Сравните с `Lighting`, `Albedo`, `Normals` и `Default` для проверки композиции. При `-NoLocalShadows` зелёный в LocalProbe равен единице на поверхности с вкладом. Если выбрана другая поверхность, сверяйте мировой приёмник, а не экранные координаты. Если только B стабилизирует результат, сравните admission и budget. Если только C стабилизирует результат, сопоставьте изменяемые параметры и кадровые слоты. Для получения точного GPU-значения сопоставьте запись/снимок владельца с `LightIndex`; из CPU-лога сами пиксели не выводятся.
4. Для стены с назначенной картой снимите ShadowDepth, ShadowVisibility и ShadowSlot одного ID. Проверьте point по обе стороны границы граней, spot по краю конуса и тень движущегося NPC. Отдельно повторите для источника без карты, чтобы оценить остаточный проток. При повторении треугольного рисунка добавьте Albedo, Normals, WorldPosition и LocalProbe на том же объекте.
5. Для улицы сравните E, F, FR1 и FR2 с одинаковым save, экспозицией, временем и камерой, затем штатный OGSR R4 и IX-Ray R4 при доступной сборке. На static, NPC, rigid и terrain снимите Albedo/Normals, прямой sun (R в LightTerms), sun visibility (G), Hemi и Lighting. Повторите FR2 с `-NoSunShadows` и `-NoHemi`. Проверьте вагон и отдельные lightmap материалы. Для воды используйте существующие WaterNormals/WaterReflection, берега и NPC у кромки.
6. Для FPS используйте Default, тот же маршрут F/FR1, одинаковые настройки и GBV выключенную. Запишите несколько окон метрик после прогрева и проверьте static/dynamic shadow draws. Отдельный диагностический прогон с `-Validation` пригодится для подтверждения корректности t6 и исследования багов binding. Проверьте также выгрузку/загрузку уровня. Окончательные Gates 1–5 закрывает владелец.
