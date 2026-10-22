# OGSR RTX — Weather & Surge Visual Contract (проект, 2026-10-23)

> **Статус: DESIGN / NOT IMPLEMENTED.** Это проект визуализации, а не доказательство работающих погодных эффектов в DX12. V0 прошёл независимый аудит (PASS WITH NOTES), V1 находится в разработке; пользователь видел небольшие фрагменты объёмных облаков, но V1 ещё не принят. **Никакой работы над выбросами/молниями до V1 + V2 PASS.**

## 1. Источники правды и граница ответственности

**Не смешивать три разные системы X-Ray.**

1. **Обычная погода:** `CEnvironment::WeatherCycles` + игровой weather manager выбирают погодный цикл; `CEnvironment` интерполирует почасовые `CEnvDescriptor`. Это источник ветра, солнца, освещения, облаков, тумана и осадков при обычной погоде.
2. **Временный WeatherFX:** `CEnvironment::WeatherFXs`, `SetWeatherFX`, `SetWeatherFXFromTime`, `StopWeatherFX`. Погодные FX подменяют текущие env-descriptors на время эффекта и возвращают в обычный цикл. WeatherFX **сам по себе не означает выброс**.
3. **Игровой выброс:** логика `surge_manager.script` (в применяемой CoP-геймдате, если она используется) отвечает за событие, предупреждения, укрытия, звук, последствия и выбор WeatherFX. Встроенный C++ `CALifeSurgeManager` в `xrGame/alife_surge_manager.cpp` обслуживает ALife spawning; **это не источник погодных фаз выброса**.

**Наш принцип:** игровая система решает **когда и что происходит**, DX12 Volumetric Clouds + Lightning + Surge Visual Adapter решают **как это выглядит**. Ни одна новая визуальная подсистема не вызывает `level.set_weather_fx`, не запускает/прерывает выброс, не меняет дождь, таймеры, AI, урон, задания или поиск укрытия.

### Что подтверждено в текущем исходном коде OGSR

- `ogsr_engine/xr_3da/Environment.cpp`: `CEnvironment::SetWeather` (стр. 220+), `SetWeatherFX` (260+), `SetWeatherFXFromTime` (322+), `StopWeatherFX` (339+), interpolated current env, загрузка weather cycles/effects. Переход к WFX использует `WFX_TRANS_TIME = 5.f` как внутреннюю константу, **не является длительностью предвестников выброса**.
- `ogsr_engine/xr_3da/Environment_misc.cpp`: загрузка `clouds_texture`, `clouds_color`, `sky_color`, `fog_color`, `fog_density`, `fog_distance`, `rain_density`, `wind_velocity`, `wind_direction`, `ambient_color`, `sun_color`, `sun_dir` и других полей; `CEnvDescriptorMixer` интерполирует значения.
- `ogsr_engine/xr_3da/Environment.h`: `WeatherCycles`, `WeatherFXs`, `CurrentEnv`, `IsWeatherFXPlaying`, поля ветра/порывов, weather descriptors.
- `ogsr_engine/xrGame/level_script.cpp`: Lua API `set_weather`, `set_weather_next`, `set_weather_fx`, `start_weather_fx_from_time`, `is_wfx_playing`, `get_wfx_time`, `stop_weather_fx`.
- `ogsr_engine/xr_3da/thunderbolt.cpp`: старая молния уже влияет на `sky_color`, `sun_color`, `fog_color`; при переходе к 3D нужно исключить двойной световой импульс.
- `ogsr_engine/xr_3da/Environment_misc.cpp`: `load_weathers` (839+) грузит `*.ltx` из `fsgame::game_weathers` для CoP-погоды либо секцию `[weathers]` из `pSettings` для SoC; `load_weather_effects` (903+) загружает отдельные WeatherFX.

**Ограничение доказательств:** предоставленный архив исходников и просмотренная ветка не содержат полный актуальный набор игровой `surge_manager.script`, `level_weathers.script` и конкретных `gamedata/configs/environment/weathers/*.ltx`, `weather_effects/*.ltx`. Поэтому **точные имена всех погодных циклов, их реальные временные интервалы и стадии именно нашей установленной игры пока НЕ УСТАНОВЛЕНЫ**. Внешние моды не являются данными OGSR.

### Внешние ориентиры — не спецификация OGSR

- В сообществе CoP подтверждается пример `level.set_weather_fx("fx_surge_day_3")` и `stop_weather_fx()`: https://ap-pro.ru/forums/topic/92-cop-modostroy-voprosnica/page/456/ . Не считать имя или порядок обязательными без актуальной геймдаты.
- В модифицированном Misery `surge_manager.script` есть пример выбора `fx_blowout_day` / `fx_blowout_night`, стадий предупреждений и сброса эффектов: https://github.com/naaroe/gamedata-misery/blob/master/scripts/surge_manager.script . **Это модификация, не ванильный CoP и не наш проект.**
- Примерная таксономия погодных .ltx из модов (`w_clear1`, `w_cloudy3_dark`, `w_foggy1`, `fx_psi_storm_day`) — лишь внешние примеры названий; фактическую инвентаризацию делать по установленной сборке.

## 2. Универсальный Weather Snapshot: читать ВСЕ фактические циклы X-Ray

**Не создаём второй Weather Manager и не жёстко перечисляем вымышленные .ltx.** Новый мост получает разрешённые `WeatherCycles`, реальные названия `WeatherFXs`, текущие/следующие дескрипторы и уже вычисленный `CurrentEnv`. Каждый установленный погодный цикл автоматически отображается в Volumetric 3D, включая пользовательские/модовые варианты.

Концептуальные входы `XRayWeatherSnapshot` (названия полей и ABI согласовать перед V3):
- `timeOfDay`, `timeFactor`, `weatherCycleId`, `weatherFxId`, `weatherFxActive`, `weatherFxRemainingTime`, `weatherDescriptorId`;
- `skyColor`, `cloudsColor`, `cloudsTextureName`, `sunColor`, `sunDirection`, `ambientColor`, `hemisphere` (если доступно);
- `fogColor`, `fogDensity`, `fogDistance`, `farPlane`, `rainDensity`, `rainColor`;
- `windDirection`, `windVelocity`, `windGust` / `windBlast` при наличии и после верификации единиц;
- `thunderboltCollection`, `thunderboltPeriod`, `thunderboltDuration` при наличии;
- `surgeActive`, `surgeEventId`, `surgePhase`, `surgeProgress`, `surgeTimestamp` **только от проверенного event adapter**, а не по цвету неба или наличию WFX.

Параметры `CurrentEnv` являются авторитетными **уже интерполированными** входами; не делаем вторую неподконтрольную интерполяцию погодных .ltx. Только визуально сглаживаем специфические 3D-параметры (например, coverage и morphology) при смене состояний, сохраняя темп исходного X-Ray.

## 3. Полная матрица визуализации обычных погодных состояний

Это **семантические категории отображения**, а не утверждение о конкретных названиях или количестве .ltx. Реальный `weatherCycleId` остаётся диагностическим идентификатором; решение о категории строится по полям окружения, возможным пресетам мода и ручным override-конфигам без изменения игры.

| Категория | Берём из X-Ray | Рисуем собственным DX12 | Поведение и ограничения |
|---|---|---|---|
| Ясно/сухо | `clouds_color`, `sky_color`, `rain_density`, солнце | Небольшие разрозненные облака, прозрачные промежутки и естественный свет | `clouds_color` не всегда прямо равен проценту облачного покрытия; нужна документированная калибровка |
| Переменная облачность | Разрешённый погодный цикл, параметры облаков и освещения | Обособленные объёмные массивы, различная высота/размер, дрейф | Стабильные world-space структуры, без приклеивания к камере |
| Облачно/пасмурно | `clouds_color`, `sky_color`, солнце/ambient | Широкие слои, плотные основания, неоднородный свет, атмосферная перспектива | Избегать одинаковых «ватных» островков и резких скачков покрытия |
| Ненастье/дождь | `rain_density`, `rain_color`, туман, ветер | Тяжёлые влажные облака, более тёмная нижняя поверхность, приглушённое солнце | Осадки остаются под управлением существующей системы X-Ray; clouds не начинают дождь |
| Гроза/электрическая активность | WeatherFX/грозовые коллекции при наличии, параметры молний и освещения | Массы грозовых облаков, затем intra/inter-cloud молнии и объёмные вспышки | До V-L PASS используем старый thunder path и не удваиваем вспышки |
| Туман/дымка | `fog_color`, `fog_density`, `fog_distance`, `far_plane` | Световоздушная перспектива и аккуратное поглощение облаков горизонтом | Не использовать fog density как прямую cloud density |
| Ветер/шквалы | `wind_velocity`, `wind_direction`, gust/blast при доступности | World-space перенос облаков, отдельно медленная эволюция формы | Ветер у земли — базовый ориентир, высотный ветер — отдельная визуальная модель, не обещанная физическая симуляция |
| Рассвет/закат | `sun_dir`, `sun_color`, `sky_color` и ambient | Тёплые/холодные края, контровое освещение, затухание солнца | Непрерывный day/night без смены независимых облачных пресетов по часу |
| Ночь | `sun_dir`, `sun_color`, ambient, небесный цвет | Силуэты облаков, слабое естественное освещение, видимые вспышки грозы | Без искусственно светящихся белых облаков |
| Региональные/модовые/внутренние циклы | Фактические имена и дескрипторы из настроек/уровня | Универсальное отображение по параметрам; локальные overrides только после аудита | В помещениях/специальных уровнях можно пропускать облачный проход по проверенным правилам |

### Переходы между погодными циклами

- **Ясно → облачно:** растёт coverage, меняется размер структур; не делаем `NoiseVolume` regeneration каждый кадр.
- **Облачно → дождь/гроза:** нарастает плотность, ухудшается видимость солнца, фронты движутся по ветру; **тайминг дождя берём из X-Ray**.
- **Ненастье → прояснение:** постепенно снижается coverage, свет возвращается вместе с исходными полями окружения.
- **Любое состояние → день/ночь:** свет и scattering меняются независимо от накопленной формы облаков.
- **Любое состояние → временный WeatherFX:** исходный WFX временно становится главным источником текущих env-параметров; по завершении возвращаемся к нужному обычному циклу.
- **Любое состояние → teleport/load/time skip:** безопасно перестраиваем/сбрасываем cloud history по event/time discontinuity; без «догоняющих» резких промежуточных кадров.

**Погодная семантика** не должна быть утрачена из-за beauty presets: цвет, степень облачности и темп переходов задаёт X-Ray, renderer преобразует их в трёхмерную структуру с пользовательской художественной калибровкой.

## 4. Выброс — отдельный event-driven визуальный сценарий

Выброс **не тождественен** обычной грозе, красному WeatherFX или psi-storm из стороннего мода. Игра определяет старт/конец, опасность, укрытия, предупреждения, волны и сохранения. Наш `SurgeVisualAdapter` не повторяет эту механику, а строит визуальные cues по проверенным событиям.

Ниже **ПРЕДЛАГАЕМЫЕ визуальные фазы**, а не обнаруженные названия/timestamps существующего `surge_manager.script`. Настоящую фазовую шкалу и события устанавливаем только после изучения текущей геймдаты.

| Фаза визуализации | Оригинальный сигнал/вход, который требуется подтвердить | Новая объёмная визуализация | Молнии / свет | Завершение |
|---|---|---|---|---|
| S0: Idle / обычная погода | `surgeActive=false`, активный обычный цикл | Обычное 3D небо или Classic 2D | Штатные weather lightning | Вход в surge по авторитетному сигналу, не по красному цвету |
| S1: Предвестники | Event-start / warning / фаза в Lua; фактический тайминг TBD | На горизонте появляются крупные тяжёлые фронты, coverage постепенно нарастает | Отдалённые редкие внутриоблачные подсветки, если V-L готов | Игровое предупреждение/разрешённый прогресс |
| S2: Нарастание | WeatherFX descriptors + event progress | Небо багровеет по оригинальным `sky_color`/`fog_color`, видны многослойные ядра | Учащение импульсов, локальное облачное свечение и самозатенение | Переход по игровому сценарию |
| S3: Активный выброс | Авторитетное событие пика/волны от Lua, не угаданное время | Аномальное ядро/разрывы облачных масс, выраженная глубина, красная объёмная подсветка | Intra/inter-cloud разряды, emissive scattering и **один** общий light impulse | Игровое окончание пика/волны |
| S4: Затухание | Завершение выброса, `stop_weather_fx`/переход к обычному циклу | Свечение уменьшается, облачный фронт рассеивается, оттенки нормализуются | Частота и яркость вспышек возвращаются к обычному режиму | Синхронная передача обычной погоде |
| S5: Recovery / normal | `surgeActive=false`, проверенный текущий `weatherCycleId` | Плавное восстановление естественного неба и текущей погодной морфологии | Обычный thunder collection / fallback | Готовность к следующему событию |

**Специальные ветви:** день/ночь; отсутствие солнца; близкая/дальняя точка наблюдения; разные локации; indoor/shelter; ручной запуск; save/load посередине выброса; time skip/anabiotic; прерывание события; смена уровня; обрыв из-за game shutdown. Обработка определяется проверенными входами, а не фиксированным самодельным таймером.

### Как будем «рисовать по-своему»

1. **Объёмная морфология:** общий V cloud density с дополнительным Surge-полем coverage/vertical shape и крупным world-space фронтом. Не подменять резким увеличением red tint всей сцены.
2. **Цвет:** сохранить `CEnvironment` как источник цветового тренда и времени; добавить художественную передачу багрового рассеяния **в линейном HDR**, контролировать tone mapping и затемнение тумана/горизонта. Режим не обязан точно копировать исходную 2D-текстуру.
3. **Внутренняя энергия:** emissive/scattering term в 3D облачном ray marching, локализованный в аномальном ядре; exposure ограничить и обеспечить конфиг снижения интенсивности вспышек.
4. **Lightning reuse:** **одна** система V-L для обычной грозы и Surge, но отдельные художественные presets/распределения каналов. Электрические каналы создаются среди пространственно разных облачных масс, а не в пустом пространстве.
5. **World response:** HDR sky, fog, ground lighting, depth, cloud shadows и surge wave — только после V3–V5/F4/F5; никогда не выдавать standalone visual pass за интегрированную сцену.
6. **Legacy fidelity:** Classic 2D сохраняет оригинальную погоду/эффекты/триггеры; Volumetric 3D опционален с безопасным fallback. При выборе нового пути исключить двойной legacy thunder flash, повторное затемнение и повторную обработку звука.
7. **Timing:** дождь/гром/предупреждения/опасность/постэффекты — по игре, а не по визуальному таймеру. Новый renderer воспроизводит событие по ID и прогрессу, не меняя скрипт управления без отдельного согласования.

## 5. Архитектурный контракт и порядок кадра

```text
game Lua surge_manager + game weather manager
                 |  (game events, time, weather IDs)
          CEnvironment / CurrentEnv
                 |  (already interpolated sky/rain/wind/sun/fog)
       XRayWeatherSnapshot (immutable)
                 |
       WeatherVisualAdapter (normal + WeatherFX)
                 +---- SurgeVisualAdapter (game-authoritative)
                             |
                      CloudVisualParameters
                             |
             DX12 VolumetricClouds / V-L Lightning
                             |
                  HDR Sky + Fog + Lighting
                 (integration only in V3–V5)
```

**Guardrails:** snapshot only, без вызовов Lua из compute passes; неизменяемые данные на frame boundary, versioning и safe defaults; `WeatherFX` сам по себе не запускает `Surge`; shader не читает `surge_manager.script` напрямую; недоступная 3D-опция не ломает оригинальный DX11/R4, Classic 2D и ALife. Не смешивать GPU cloud resource ownership с F4.0.

## 6. Запланированные этапы V-S и зависимости

| Этап | Работа | Жёсткий гейт |
|---|---|---|
| **V-S0: actual asset/script audit** | На установленной пользователем сборке выгрузить список всех `WeatherCycles`/`WeatherFXs`, прочитать реальный `surge_manager.script`, `level_weathers.script`, `weather_effects/*.ltx`, звуковые/postprocess/camera FX; составить *фактическую* временную диаграмму событие→WeatherFX→освещение→звук/урон/укрытие. | Исходные файлы геймдаты доступны; отчёт не заявляет неподтверждённые времена/имена |
| **V-S1: standalone Surge weather & density** | Набор воспроизводимых mock weather fixtures + red volumetric sky, ядро выброса, phase interpolation; без изменения геймплея | **V1 PASS + V2 PASS** после независимого review |
| **V-S2: Surge lightning & radiance** | Переиспользовать V-L1/V-L2 внутриоблачные и межоблачные вспышки, облачный scattering и устойчивый HDR compositing | V-S1 + соответствующие V-L PASS, замеры GPU и captures |
| **V-S3: in-game Surge adapter** | Подключить авторитетный Lua/engine surge event, CurrentEnv, hdr/fog/depth/world lighting, сохранить оригинальные gameplay scripts и Classic 2D | V3–V5 + нужные F4/F5 data paths, полноценное игровое QA |

V-S0 — **исследование**; никакой новый shader/runtime-код для выброса до gate V1/V2. V-L уже зарезервирован в основной дорожной карте; V-S его **не дублирует**.

## 7. Acceptance tests / evidence

- **Inventory:** в лог сохраняются реальные названия каждого загруженного cycle/FX, их число, источник файла, поля/ключевые кадры; mapping report без пропусков. Любой новый модовый цикл попадает в общий fallback, не создавая отсутствующий пресет.
- **Baseline:** Classic 2D/DX11 визуально и поведенчески неизменны; исходная погодная семантика и скорость событий совпадают при одинаковом save/time.
- **Transitions:** clear→cloudy→rain→storm→clear; 00:00 wrap; sunrise/sunset; смена ветра; fog↔sun; без скачков облаков/цвета и пересоздания volume texture каждый кадр.
- **Surge:** start/prewarning/peak/end/recovery, day/night, shelter, save/load mid-surge, time-skip/anabiotic, manual stop, level change; синхронизация оригинального звука, тревоги и gameplay phase.
- **Lighting:** image-based A/B screenshots старого и нового неба; линейный HDR, корректное transmittance, отсутствие NaN/Inf, отсутствие повторных thunder light pulses, предсказуемая экспозиция.
- **Performance:** GPU timestamp cloud march/lightning/composite, VRAM, 720p/1080p, clear/overcast/surge; сравнение без выдуманных FPS-бюджетов.
- **Safety/robustness:** Debug Layer+GBV без ошибок, resize, device removed, safe fallback, deterministic seed, abrupt event cancellation, pause, history reset; режим снижения интенсивности вспышек.
- **Review:** всегда artifacts + исходный commit + конфиги и точное указание версии game-data; статус PASS назначает независимый аудитор, не разработчик.

## 8. Немедленный приоритет / ограничения

**Сейчас только V1.** Видимые фрагменты облаков ≠ полный объёмный слой. Следом независимый V1 audit, затем V2 cloud quality gate. **Ни молнии V-L, ни новые спецэффекты V-S сейчас не запускать.** Исследование файлов V-S0 разрешается отдельно после появления действительной game-data, без отвлечения работающего над V1 разработчика.

Справка по актуальным кодовым источникам: [Environment.cpp](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/prypiat/ogsr_engine/xr_3da/Environment.cpp), [Environment_misc.cpp](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/prypiat/ogsr_engine/xr_3da/Environment_misc.cpp), [level_script.cpp](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/prypiat/ogsr_engine/xrGame/level_script.cpp), [thunderbolt.cpp](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/prypiat/ogsr_engine/xr_3da/thunderbolt.cpp).
