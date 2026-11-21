# OGSR RTX — инженерная дорожная карта

> **Актуализировано 21 ноября 2026.** Это практический план разработки: видение, архитектура, подэтапы, зависимости, тестовые сценарии и критерии приёмки, а также **отобранная история ключевых решений**. Не превращать файл обратно в ленту полных ревью-логов. [Полный исторический roadmap](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/521cde4e685d003a7ee8f4e0cf08a2730fb45ab8/docs/08-roadmap.md) сохранён в Git.
>
> **GitHub на момент сверки:** DX11/R4 остаётся главным рендерером `prypiat`. DX12 F4.1-E уже **опубликован** в [`feature/f41-full-static-jupiter`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/tree/feature/f41-full-static-jupiter), HEAD `b9b3dd2279e3b5de4a970bcfb383b64f1099ad6f`. Merge не проводился. Истории `prypiat` и feature расходятся, поэтому дальнейшая интеграция требует отдельного review и разрешения.

## 1. Видение и принципы

Мы развиваем OGSR/X-Ray для *S.T.A.L.K.E.R.: Call of Pripyat*, сохраняя атмосферу Зоны, масштаб открытых локаций, погодные состояния, время суток, динамику NPC, физику, растительность и игровой сценарий.

**Стратегия в два принципиальных шага.** **Сначала — функциональный перенос R4/DX11 в DX12**: все нужные исходные объекты и игровые визуалы, эффекты, частицы, декали, свет, погодные render paths, HUD и существующая постобработка, с проверкой по [матрице паритета](dx12-r4-render-parity.md). Только **после достижения игрового паритета и устойчивого lifecycle** — второй этап: улучшение красоты (новые текстуры, материалы/PBR, художественный свет, аппаратные RTX/DXR-тени, отражения и при измеренной пользе непрямое освещение). **Единственное раннее визуальное исключение по решению владельца — собственное небо OGSR RTX с нашим GPU volumetric cloud renderer:** его добавляем в игровой DX12 уже в ходе/сразу после F4.2, а не реализуем сначала полное старое небо ради последующей замены. Это **не** отменяет обязательную совместимость с игровой погодой R4 и безопасный Classic 2D fallback. Никакой переход ко второму шагу не заменяет пропущенные функции первого. *Control* показывает потенциал выборочных RT-эффектов, но архитектура больших наружных уровней требует собственных решений для visibility, foliage, GPU memory и суточной смены освещения.

### Принципы и границы

| Принцип | Что это означает для кода |
|---|---|
| **Реальная игра прежде демо** | Принимаем по настоящим игровым кадрам/камерам, gameplay, ресурсам и lifecycle, а не только по standalone |
| **DX11/R4 — эталон** | Сохранять совместимость и обычный R4 gameplay; проверять реальные объекты R4 vs DX12 на одинаковой позе |
| **Один рендерер владеет кадром** | Game-owned device/queue/swapchain/frame, GPU fences; эффекты не создают собственный игровой runtime |
| **Оригинальные ресурсы X-Ray** | `level.geom`, `level.details`, spawn/game visuals, OGF, DDS/THM; не превращать рендерер в ручную коллекцию выбранных моделей |
| **Безопасность и многопоточность** | Immutable snapshots, object/session generations, GPU owner-thread и fence-safe resource retirement |
| **Большие результаты с проверкой** | Субагенты помогают с R4 audit, форматами, fixtures; основной агент реализует критический GPU-код |
| **Честная приёмка** | Code written, author PASS, independent review, clean Windows build и production merge — разные состояния |

### Контрольные игровые сцены

Jupiter — вокзал с дверями, рельсы и вагоны, карьер/terrain, посёлок, улицы с растительностью и большие виды сверху. Zaton — второй уровень для проверки универсальности, reload и отсутствия Jupiter-специальных условий. Для каждого проблемного класса иметь одинаковую R4/DX12 позу и source identity. Проводить проверки движущейся камеры, level unload/reload и нормального выхода.

## 2. Текущее состояние проекта и порядок работ

| Направление | Что есть | Что ещё не принято |
|---|---|---|
| **DX11/R4** | Полный стандартный рендерер, fallback | Нельзя ломать при общих DX12 изменениях |
| **F4.0-B** | Нативный игровой DX12 vertical slice, реальные DDS, камера/Present, независимое ревью PASS WITH NOTES | Ограниченный участок, не вся игра |
| **F4.1-E** | Jupiter: **20 880** source visual ID совпали с R4, **13 911** рисуемых static visuals, 0 dropped unreachable. Реальные игровые снимки, GBV logs | Отдельные game-owned объекты/двери, `level.details`, blend, свет, HUD; independent code review ещё нужен |
| **Source F4.1** | Ветка [опубликована](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/tree/feature/f41-full-static-jupiter), 49 линейных feature commits, HEAD `b9b3dd227` | Не слита в `prypiat`, интеграционная clean matrix не пройдена |
| **F4.2** | **Следующий большой функциональный пакет** | Подключение игровых skeleton-rigid объектов и оригинальной травы |
| **DXR/RTX** | ADR и техническая концепция | Реальный ray-traced игровой кадр не принят |
| **Volumetric Clouds** | Standalone DX12 ray marcher и погодная ветка | Последний R4 rebuild получил 2×P2 REQUEST CHANGES, игровой HDR composite не принят |
| **PhysX** | Opt-in source integration через [PR #1](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/pull/1), 16-live controlled ragdoll тест | Full ALife/save/loot/performance и default-on HOLD, ODE fallback |

### Ближайшая очередь

| Очередь | Цель | Гейт завершения |
|---|---|---|
| **Сейчас** | F4.1-E независимый source/integration review опубликованного кода | Точный Git/EXE SHA, clean build, R4 clean exit и DX12 GPU smoke |
| **Следующий** | F4.2-A `MT_SKELETON_RIGID` из game-owned объектов | Настоящие двери, лампы и ящики с верной world/bone pose |
| **Следующий** | F4.2-B исходные `level.details` | Трава/детали, instancing, сравнимое распределение с R4 |
| **Ранняя интеграция, параллельно или сразу после F4.2-A/B** | **F4.2-SKY: наше небо и объёмные облака** [#15](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/15) | Рабочий game-owned sky/background, CEnvironment, GPU cloud compositing, корректный indoor occlusion, Classic 2D fallback; пока opt-in |
| **После F4.2** | F4.3 паритет исходных материалов, света и прозрачности R4; интеграция с новым sky pass | Нормальный opaque/cutout/**blended**, legacy shaders, sun/local lights; **без новых PBR-текстур** |
| **Дальше** | F4.4 полный игровой контент и исходные render passes | NPC/оружие/HUD, частицы, декали/wallmarks, погодные эффекты и **существующая R4-постобработка** |
| **Затем** | F4.5 полная матрица R4→DX12, стабильность и производительность | Нет необъяснённых пропусков объектов/эффектов, безопасный lifecycle, DX11 fallback, измеренный GPU budget |
| **Параллельно** | Облака R4-R1, PhysX G5.1-S3R-R2 | Их автономные correction gates без подмены требований DX12 parity |
| **Только после полного переноса** | F5 улучшение качества и масштабирования; DXR G0–G5 | Новые текстуры/PBR и художественная модернизация, temporal/DLSS/FSR, затем BLAS/TLAS и RTX с raster fallback |

## 3. F4.1-E — подтверждение реальной статической сцены

### Что работает и что именно было исправлено

Оба рендерера нумеруют `level` visuals одинаково: индекс `fsL_VISUALS(3)` = R4 `CRender::Visuals` = индекс DX12 geometry inventory. По инструментированному сравнению — **20 880/20 880**, type mismatch 0.

| Visual type | Записей | Роль |
|---|---:|---|
| `MT_NORMAL` | 7 903 | Static geometry |
| `MT_PROGRESSIVE` | 789 | Progressive terrain |
| `MT_TREE_ST` | 3 035 | Static flora |
| `MT_TREE_PM` | 2 184 | Ранее отсутствовал: SWI читается из `level.geom`, не `level` |
| `MT_HIERARHY` | 3 954 | Structural containers |
| `MT_LOD` | 3 015 | Группы, для near R4 traversal нужны все дети |
| **Итого** | **20 880** | 13 911 drawable, 6 969 структурных groups |

Геометрия читает shared `level.geom` pools: 36 VB/11 IB (~93,8 МБ), без массовой переупаковки. В текущем evidence-слое 365 запрошенных DDS без unsupported; это **не** доказательство всех legacy shader/material semantics. R4↔DX12 compared screenshot выявляет реальные динамические двери в R4 и пустые проёмы DX12 — это *не static sector visuals*. [F4.1 report](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/feature/f41-full-static-jupiter/docs/dx12-f41-full-static-scene.md) · [R4 comparison](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/feature/f41-full-static-jupiter/docs/dx12-f41e-r4-dx12-comparison.md).

### Следующий шаг до начала F4.2

1. Сверить remote feature HEAD, source ancestry F4.0-B → F4.1-E и реальные файлы. Ничего повторно пушить не требуется.
2. Прочитать `GameDx12StaticScene`, `GameDx12Bridge`, `level.geom` parser, tree PM, MT_LOD traversal, материалы и legacy R4 instrumented code, проверить input bounds и GPU ownership.
3. Запустить адресную clean camera-on Release сборку на выбранном exact SHA; Jupiter native DX12 Present/camera/GBV. Отдельно R4 feature-off Jupiter/Zaton с **exit 0**, по возможности feature-on. Предыдущий R4 smoke достиг уровня, но normal quit 0 ещё не доказан — [#7](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/7).
4. Ветки feature и `prypiat` разошлись. Для integration candidate использовать отдельный worktree от актуального main, сопоставить shared files и PhysX, протестировать объединённую версию. Не force-push и не blind merge.
5. Независимое code review: concrete source findings, verified tests, scope limitations, рекомендация. **Merge отдельным решением владельца**.

**Гейт F4.1-E:** реальный static ID/type/mesh coverage, GPU-safe lifecycle, R4 clean regression и понятная source/binary provenance; динамическая сцена F4.2 не требуется для статического gate, но без неё «вся игровая карта» ещё не готова.

## 4. F4.2 — подробный план игровых объектов и level details

**Результат:** двери, висящие лампы, ящики и другие игровые rigid props в оригинальных координатах и состоянии; затем трава/малые детали в исходных местах. Не возвращать hardcoded sector IDs и не создавать второй VFS loader.

### F4.2-A — game-owned skeletal rigid visuals

Проверить `CPhysicObject`, `Render->model_Create`, `CKinematics` и реальные `MT_SKELETON_RIGID` OGF: дверь может содержать bone `door`; одной identity/world matrix не всегда достаточно. Поддержка анимированных NPC с несколькими костями — отдельная F4.4 задача.

| Шаг | Реализация | Доказательство |
|---|---|---|
| **A0** | Source audit game entity creation/delete, visual type, bone/current pose, transform, visibility | Схема game→render object ownership |
| **A1** | Game-thread immutable frame snapshot: object handle/generation, visual id, session, world/bone transform, visibility/bounds | Нет GPU worker чтения mutable game objects |
| **A2** | Existing asset/GPU registry cache, real OGF geometry/material, bounded parsing | Не подменять игровые объекты тестовыми кубами |
| **A3** | DX12 draw input/root constants, depth/material, serial owner GPU command recording | Двери, лампы и ящики появляются в сцене |
| **A4** | Spawn/despawn, изменённая физическая поза двери/предмета, unload/epoch/fences | Нет stale draws/UAF |
| **A5** | Source identity/type counters `candidates/visible/drawn/unsupported(reason)` | Понятно, что R4 рисует, а DX12 ещё нет |
| **A6** | Same-camera R4/DX12: закрытая/открытая дверь, лампа, предмет | Реальные кадры, 30+ consecutive native Presents, GBV |

Объекты без visual и multi-bone unsupported типы не скрывать и не давать им фиктивные geometry IDs. [#11](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/11) / [#9](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/9).

### F4.2-B — настоящие детали земли

`level.details` — отдельный path `CDetailManager`; он не входит в 20 880 `level` visual ID. Нужно воспроизвести original instances, а не создать произвольную траву.

| Шаг | Реализация | Проверка |
|---|---|---|
| **B0** | Source audit detail format, slots, object IDs, transforms, R4 draw/LOD | Доказаны структуры и units |
| **B1** | Level/session-owned VFS snapshot, bounded parsing, deterministic errors | Нет чтения/декомпрессии в frame loop |
| **B2** | Cache исходных detail meshes и alpha DDS/THM | Нет дублированных VB/IB на каждый instance |
| **B3** | GPU instancing, camera visibility/distance/LOD/density, per-instance data | Реальное расположение и плотность как у R4 |
| **B4** | Shared game queue/frame/descriptor policy и safe retirement | InfoQueue/GBV; VRAM budget |
| **B5** | Matched улица/трава/лес, camera move, stat counts | Нет систематических пропусков/pop-in |

**Критерии F4.2:** реальные объекты и оригинальные детали, source-backed game/world transforms, корректное создание/удаление, GBV + R4 regression. Wind и advanced foliage LOD возможно отдельно, но нельзя выдавать отсутствие detail vegetation за завершённый этап. [#12](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/12).

### F4.2-SKY — ранняя интеграция нашего неба (решение владельца 2026-11-21)

**Цель:** не тратить отдельный этап на полное повторение старого R4 sky renderer ради его немедленной замены. В **игровой native DX12** переносим **нашу разработанную sky/cloud систему**, опираясь на существующий standalone GPU ray marcher и его визуальную концепцию; объёмное небо становится ранним пользовательским направлением, а не поздним PBR/RTX-дополнением. [Issue #15 — F4.2-SKY](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/15).

**Порядок:** параллельная изолированная ветка или следующий интеграционный пакет непосредственно после F4.2-A/B; главный Codex не бросает unfinished двери/level.details, но **не ждём F4.4/F4.5 и RTX** перед первым игровым небом. Это **opt-in integration R&D**, не производственная приёмка.

| Шаг | Работа | Acceptance |
|---|---|---|
| **SKY-0 — data contract** | Прочитать реальный R4 `CEnvironment`/CurrentEnv и stand-alone sky/cloud source; source of truth — игровое время суток, weather cycle/WeatherFX, солнце, ветер, fog и coverage. Один immutable per-game-frame snapshot | Никакого второго погодного таймера, world weather RNG или чтения mutable CEnvironment из GPU worker |
| **SKY-1 — собственный sky background** | Встраивание sky render pass в существующий D3D12 device/queue/frame и формат color/HDR, camera/projection, horizon, sun/atmospheric context | Небо видно на Jupiter с корректными направлениями/горизонтом; не затирает интерьер, крыши и terrain |
| **SKY-2 — облачный volume pass** | Адаптировать существующий GPU ray marcher/compute shaders и ресурсы под игровой scene/depth/HDR composite, barriers/descriptors/fences, time/wind movement | Облака реально видны над Jupiter, правильно перекрыты геометрией; камера/день-ночь/ветер работают |
| **SKY-3 — fallback & quality** | В DX12 режимы Classic 2D и optional Volumetric 3D; Classic безопасен при отсутствии/ошибке 3D. DX11/R4 не меняем | При выключении 3D корректное обычное небо, без сломанного кадра; без двойного слоя старых и новых облаков |
| **SKY-4 — validation** | Jupiter indoor/outdoor, Zaton, day/night, rain/overcast, camera motion, resize/reload/close; GPU Debug Layer/GBV, 30+ captures, GPU ms/VRAM | Targeted game evidence, прозрачная таблица NOT RUN/known gaps, измеренная цена и нормальная lifetime |

**Текущее ограничение:** standalone V-WEATHER-01C-B всё ещё **HOLD** с нерешёнными R4-R2 диагностическими issues, а V1/V2 formal visual/performance acceptance ещё нет. **Раннее подключение не превращает экспериментальные облака в production-approved**: не переносить опасный standalone control/diagnostic loop в игру, исправить source/ownership issues перед продвижением. В игре нет отдельного cloud-owned device, swapchain, queue, GR scheduler или погоды. Не ломать существующий R4 fallback, не сливать renderer code в `prypiat` без отдельного решения.

---

## 5. Следующие этапы F4.3–F5 и DXR

| Пакет | Работа | Acceptance |
|---|---|---|
| **F4.3 — оригинальные материалы и освещение** | R4 DDS/THM/shaders, opaque/cutout/**blend**, normal/depth, исходный sun/local light/shadows и связка с нашим ранним F4.2-SKY | Исходные R4 материалы/свет совместимы с нашим sky, вода и стекло без неверных opaque patches; [#6](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/6) |
| **F4.4 — полная функциональность R4** | Multi-bone NPC/mutants, оружие/HUD, particle groups, muzzle flashes, следы попаданий, кровь, wallmarks, scripted draw, погодные эффекты, **исходная постобработка R4** | Инвентарь [R4→DX12](dx12-r4-render-parity.md) закрывает реальные gameplay классы и пассы; matched scenes без необъяснённых пропусков |
| **F4.5 — проверка полного переноса** | Матрица эффектов/объектов и визуальные регрессии; `vid_restart`, level change, resize/alt-tab, device loss, repeated teardown, CPU/GPU/VRAM budgets | No unexplained R4-only rendering family, no UAF/GBV errors, normal exit, genuine DX11 fallback |
| **F5 — качество и масштабирование после паритета** | Motion vectors, jitter/history, TAA/DLSS/FSR, upgrade legacy textures/materials в отдельном контролируемом пакете | Исходные сцены не деградируют; качество в движении и GPU/latency budget |
| **DXR G0–G2 — после паритета** | Feature detection, ordinary BLAS/TLAS, geometry IDs, RayQuery/DispatchRays | Correct hit geometry и safe AS lifetime, raster-only fallback |
| **DXR G3–G5 — развитие RTX** | RT shadows → reflections → при необходимости GI, dynamic/foliage AS | Прирост качества при контролируемой стоимости и сохранении функциональности R4 |

**Жёсткая граница между переносом и модернизацией:** пока не обеспечен полный перечень необходимых R4 render paths (объекты, инстансы, эффекты, декали, освещение, существующая постобработка, HUD) и рабочий gameplay на DX12, **новые художественные текстуры/PBR и RTX не являются приоритетом и не заменяют недостающие системы**. Исключение — минимально необходимая реализация старого материала/света/паса, без которой невозможно воспроизвести существующее содержимое. Крупные открытые уровни X-Ray затем получат hybrid raster/RT, а не обязательный full path tracing. Совместимость исходных DDS/THM и R4 shader semantics сохраняется.

### Сквозной принцип: ни одна графическая возможность R4 не должна потеряться

**Обязательный реестр полноты R4 → DX12:** [техническая матрица путей рендера](dx12-r4-render-parity.md) · [Issue #14 — No feature left behind](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/14). **Статическое совпадение 20 880 visual ID не подтверждает полной функциональности игрового рендера.** Кроме геометрии есть независимые GPU paths: динамические игровые объекты, детали, частицы, wallmarks/декали, кровь/следы попаданий, свет и тени, небо/погода, UI/HUD, оружие, постобработка и редкие игровые эффекты. Список продолжают уточнять по **реальному source R4**, а не считать закрытым по этой формулировке.

| Шаг | Обязательный инженерный результат |
|---|---|
| **Инвентаризация R4** | Перечень всех producer→render interfaces→draw/phase функций; source file/function, тип, эффект, нужные материалы и состояние. Каждая строка имеет статус `VERIFIED / PARTIAL / MISSING / DEFERRED / UNKNOWN` |
| **Машинная проверка** | Создать структурированный `render-parity.json/csv`, source/visual/effect IDs и deterministic matched gameplay events; отличать loaded, submitted, visible, culled и skipped с причинами |
| **Игровые сценарии** | Сравнение R4/DX12 в интерьере и на открытой локации, при стрельбе и взрывах, появлении крови/декалей/частиц, в дождь и ночью, с HUD и спецэффектами; transient effects сравнивать с учётом времени и недетерминированности |
| **Контроль новых этапов** | F4.2–F5 обновляют реестр вместе с кодом и адресными тестами; обнаруженный неизвестный R4-only класс не должен silently disappear |
| **Общая приёмка DX12** | **Не принимать «полный игровой DX12»**, пока не классифицированы все обнаруженные R4 render paths, а нужные для игры классы не реализованы, не проверены или явно не отложены с обоснованным fallback |

Это **не новый блокер для текущей разработки F4.2**: read-only аудит может выполнять отдельный субагент, пока основной разработчик пишет DX12-код. Не проводить бесконечные повторы старых проверок: по каждому классу — одна отслеживаемая строка, evidence, ответственный этап и Issue при дефекте. Мерцание декалей уже зафиксировано отдельно в [#13](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/13).

## 6. Сохранённые архитектурные решения F/GR/PBR

> **Инженерная спецификация из истории проекта — Один GR Job System и CPU preparation.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

#### F3.2 — GR Job System: сначала только DX12 (решение 2026-10-18)

**Архитектурный статус:** GR Job System — самостоятельная библиотека уровня Engine Core
(`ogsr_engine/Layers/JobSystem/`), а не часть `xrRender_R5_DX12`. Backend — закреплённый
форк enkiTS (SHA `404a3bf8f855039dfff2052184d6308655286c07`); публичный
`gr/jobs/` API не зависит от enkiTS, DX11, DX12 и Win32. Принцип — один общий
планировщик CPU-задач и Work Stealing; размещение потоков оставлено Windows
(**OS Default**, без hard/soft CPU affinity в runtime). Количество workers
конфигурируется, а последовательный путь сохраняется для отката и измерений.

**Решение об области работ:** на текущем этапе **только собственный DX12 renderer /
standalone `xrDX12Smoke.exe`** становится потребителем GR Job System.
Физика, AI, анимация, streaming и фоновые задачи аудио **не интегрируются сейчас**:
позднее, после измерений DX12 и отдельного аудита зависимостей, в направлении H.
Специализированные realtime-потоки (особенно audio callback) не обязаны
использовать общий пул; запрещено блокировать realtime audio callback ожиданием job.

| Подэтап | Статус на 2026-10-18 по локальным отчётам, без независимого GPU runtime PASS | Следующий гейт |
|---|---|---|
| **F3.2A / A.1 — enkiTS bench + OS scheduler** | Отчёт: 10 раундов, 480 конфигураций; принято **OS Default**, хотя hard affinity дала локальный выигрыш в синтетике | Хранить raw timings; не выдавать синтетический speedup за FPS |
| **F3.2B / B.1 — GR Job System Foundation + Safety Review** | По отчёту агента: автономная библиотека и исправления noexcept destructor, move-assignment, lifetime; требуются проверки интегрируемой версии | Убедиться в корректном ownership, исключениях, shutdown и регрессионных тестах |
| **F3.2C Stage 1 — VS integration** | По отчёту Codex: `xrDX12Smoke → gr_job_system → enkiTS`, ReleaseDX12/x64 без ошибок/предупреждений; окно не запускалось; изменения локальные, не закоммичены | Воспроизводимая сборка из репозитория; не подключать `xrRender_R5_DX12` и другие игровые targets раньше отдельного этапа |
| **F3.2C.1 — ownership/lifecycle hardening** | Требует отдельной проверки: потенциальное освобождение опубликованной задачи при исключении из `tasks.push_back`, а также hook/drain/shutdown | Устранить подтверждённые дефекты, fault-injection и регрессионные тесты |
| **F3.2C.2 — DX12 Smoke CPU scene pipeline** | **PASS**, коммит `09ca02357`: 32/32 runtime, 857 CPU checks, depth readback, resize и GPU validation; ручной визуальный осмотр и race detector не выполнены | Выполнить ручную проверку окна перед полной визуальной приёмкой |
| **F3.3 — DX12 CPU render preparation** | **PASS**, коммиты `09ca02357` + `5b0e4bbcb` (F3.3.0 и F3.3.0a): immutable snapshot, prepared packets, Serial/Parallel/Verify, WM_TIMER fault handling; merge с `origin/prypiat` — `99c9d1a10` | F3.5.0 ADR принят → F3.5.1 |
| **F3.4 — DX12 parallel command recording** | Запланировано; согласовать с существующим разделом F3.5.3, не дублировать работу | Per-worker command lists/allocators, GPU fence lifetime, barriers, validation, profiling |
| **F3.5 — DX11 vs DX12 comparison / решение** | Запланировано; согласовать с существующим F3.5.4 (не переименовывать старый этап) | Одинаковые сцены и настройки, CPU/GPU timings, p50/p95/p99, frametime, 1% low; отделить различия backend от различий качества |

**F3.2C.2 — измерения в standalone Smoke (4 background workers + main, grain=64):**

| CPU-объектов | Serial, мс | Parallel, мс | Serial / Parallel |
|---:|---:|---:|---:|
| 256 | 0.024141 | 0.037532 | 0.64× (parallel медленнее) |
| 1024 | 0.114042 | 0.059029 | 1.93× |
| 4096 | 0.383540 | 0.136484 | 2.81× |
| 16384 | 1.301304 | 0.421946 | 3.08× |

Источник — локальный `docs/dx12-f3-2c2-runtime.md` (30 прогревочных и
200 измеряемых кадров; Serial/Parallel verification, 32 automated runs).
Числа измеряют **только CPU scene preparation** на RTX 5070 Laptop GPU,
не FPS и не реальный игровой scene renderer. Serial остаётся default.
F3.2C.2: runtime и автоматическая корректность по отчёту PASS; ручной осмотр
окна, интерактивный minimize/restore и специализированный race detector
остаются открытыми. Не заявлять завершения этих проверок.

**Следующий этап F3.3:** сначала аудит фактических CPU scene/render preparation
и их владельцев в `xrRender_R5_DX12`, анализ frame lifetime и точек
синхронизации; затем отдельный минимальный serial/parallel path с проверкой
эквивалентности. Не переносить синтетические объекты в игру как «готовый
рендерер», не менять DX11 и другие подсистемы; GPU command list recording
оставить на более поздний гейт F3.5.3.

**Последовательность:** закрыть корректность и lifecycle → подключить GR к
standalone DX12 Smoke → подготовка CPU сцены → реальные CPU-задачи DX12 →
параллельная запись D3D12 command lists только после отдельных GPU lifetime
инвариантов → сравнение с сохранённым DX11 baseline. DX11, gameplay, physics,
AI и audio сейчас не менять. Сборка **не равна** запуску или GPU validation PASS.

**Важно о нумерации:** существующий ниже F3.5 — общий архитектурный рубеж
Multithreaded Renderer Foundation до F4. Ссылки F3.3/F3.4/F3.5 в таблице выше
описывают предложенные рабочие итерации GR/DX12 внутри этого рубежа; они **не
отменяют и не перенумеровывают** F3.5.0–F3.5.4 и требуют синхронизации перед
обновлением основных milestones.

---

> **Инженерная спецификация из истории проекта — Immutable frame snapshots и foundations многопоточности.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

#### F3.5 — Multithreaded Renderer Foundation: архитектура ДО массового переноса F4

**Мотивация:** D3D12 позволяет записывать независимые command lists с нескольких CPU threads, но это не делает устаревший frame loop X-Ray автоматически многопоточным. После F3 достаточно рабочего 3D-пути, чтобы проверить модель владения состоянием; откладывание этих границ до H создаёт риск переписывать F4. **F3.5 — архитектурный минимальный прототип, не обещание прироста FPS и не переписывание simulation scheduler.**

| Подэтап | Работа | Обязательный гейт |
|---|---|---|
| **F3.5.0 — architecture ADR + trace** | Карта main/render threads, кадра, точек синхронизации, потоков GPU и владельцев данных; выбрать границы immutable frame snapshot, draw packets, PSO/material handles, queue submission и fallback. Без предположения, что готовый X-Ray renderer thread-safe. | ✅ ADR принят: `docs/dx12-f3-5-architecture-adr.md` (frame N/N+1, потоковая модель, fencing/lifetime invariants, список опасных mutable pointers, синхронизация с TTAPI, render-owned `(id, generation)`). Реализация не начата. |
| **F3.5.1 — immutable snapshot** | На согласованной границе кадра после update собрать компактный read-only RenderSceneSnapshot (transform/bounds/material/resource references); без записи игровых сущностей из render workers. Owner и lifetime сохраняются до последнего потребителя, ограничить frames-in-flight и backpressure. | Snapshot непротиворечив; проверены изменения/удаления объектов и быстрое движение камеры без dangling references/data races. |
| **F3.5.2 — work decomposition** | Минимальная независимая подготовка visibility/culling, sort/draw packets из snapshot; фиксированный порядок там, где нужна прозрачность или зависимости; исключить global mutex per draw. Мелкие сцены допускают последовательный путь. | Детерминированная либо визуально эквивалентная сборка тестовой сцены в serial и worker режимах, без нарушения порядка проходов. |
| **F3.5.3 — D3D12 multi-list prototype** | Per-thread/per-frame command allocators & lists, per-frame descriptor/upload allocation и synchronization; reuse/reset лишь после завершения fence; command list ownership на одном worker во время записи, упорядоченный submission на direct queue. Ввести явные правила ресурсов/barriers (межлистовые зависимости согласуются при merge/submission). | Тестовые сцены без GPU validation, race, lifetime и barrier errors на 1/2/4/8 workers; resize/shutdown и быстрая смена сцены безопасны. |
| **F3.5.4 — profiling & decision** | Одинаковые сцены/разрешения/сборки; CPU frame time отдельно от GPU time, render-thread time, p50/p95/p99 frametime, количество draw calls, lock/contention, worker overhead и scaling 1/2/4/8. Проверить CPU-bound и GPU-bound сцены. | Таблица результатов, reproducible capture/коммит, выбор разумного default (включая serial), чёткое решение: какой parallel path переносить в F4, что оставить F6/H. |

**Обязательные инварианты:**

- Драйвер/GPU **не гарантируют** ускорения из-за большего числа workers: важны профиль CPU bottleneck и overhead. Переключатель на 1 worker / последовательный путь обязателен для сравнений и диагностики.
- В каждом записывающем потоке — собственный command allocator/list, независимое CPU-владение; GPU fence защищает **повторное использование allocators**, transient upload/descriptor и уничтожение объектов после завершения GPU. Graphics queue submit может оставаться сериализованным.
- Snapshot и draw packets **не являются** самостоятельной копией всего игрового мира; simulation/A-Life/physics не переносятся на workers в F3.5. Разделить lifetime игровых сущностей и стабильных render handles.
- Алгоритмы ресурсных переходов не должны допускать недетерминированные barriers при параллельной записи. Минимальный central pass/resource dependency tracker достаточен; полноценный render graph возможен лишь при подтверждённой необходимости.
- Гарантировать безопасную обработку отказов, остановку worker pool и shutdown, когда кадры ещё in-flight; минимизировать блокировки, но не заменять корректную синхронизацию lock-free оптимизациями по инерции.
- Сохранить существующий DX11 путь неизменным. Новая реализация не может объявляться PASS, пока не пройдены code-review, build, runtime/debug layer, GPU и performance гейты.
- Интеграция с World Streaming I/J в будущем должна передавать worker задачам подготовленные **resource/scene handles**, а не сырой mutable game state; F3.5 не включает реализацию I/J.

**Порядок:** F3 минимальная 3D геометрия/depth/materials → F3.5.0–F3.5.4 → перенос игрового кадра F4 с уже определёнными безопасными границами. При необходимости F4 может стартовать с serial recording поверх подготовленного интерфейса и включать рабочий parallel path постепенно. G0/G1 лабораторные исследования не обязаны ждать полного performance tuning F3.5; полноценная интеграция DXR в игру — после F4.

**Исторический статус на 2026-10-19 (заменён актуальной сверкой 2026-10-25 выше):** ✅ **F3.5.0 — ADR ПРИНЯТ** (`docs/dx12-f3-5-architecture-adr.md`), реализация не начата. ⏭ **F3.5.1 — NEXT / NOT STARTED**: это **representative snapshot prototype** (раздел 9 ADR), а не интеграция реальных игровых объектов — реальный обход сцены остаётся за F4. Пункт не подтверждает реализацию snapshot или multi-list renderer.

**Ограничения, сохраняемые без изменений:** ручной визуальный осмотр окна, интерактивный minimize/restore и специализированный race detector (TSan) **не выполнены**; отсутствие гонок не доказано. Синтетические замеры F3.2C.2 — не FPS и не игровой рендер.

> **Инженерная спецификация из истории проекта — Исторический vertical slice F4.0-B.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

#### F4.0-B-VSLICE — real game DX12 geometry + textures + frame lifecycle (2026-11-20; EXPERIMENTAL REVIEW PASS WITH NOTES / PRODUCTION MERGE HOLD)

**Актуальный результат:** см. сводку F4.0-B выше: B1–B4 подтверждены для ограниченного Jupiter sector/asset subset на локальном feature HEAD `81956aa1a`; 30 игровых native DX12 кадров и реальный BC3 alpha-test, CPU/GBV/GR и R4 проверки имеют указанные ограничения. **Не утверждать, что вся карта загружается или код уже опубликован в `prypiat`.**

**Ниже сохранён первоначальный контракт/план работ F4.0-B как историческое основание, а не текущий список невыполненных задач.**

- **Policy / meaning:** owner selected big integrated work package B after M23-R1 Codex PASS WITH NOTES. F4.0-B-VSLICE bundles **(B1) game world-space real static geometry and M1 CPU→GPU handoff, (B2) minimal OGSR texture/material/UV/normal preview and (B3) owner-owned true DX12 multi-frame camera/depth/swapchain frame runtime**, plus minimal F4.0-J3 foundation if measured/needed. This is **one engineering milestone** with incremental source commits but only **one final independent Codex READ-ONLY review**; no separate review after each B1/B2/B3. Naming is a new umbrella tag, does not rewrite historical F4.0-B/C/F4/J3 statuses.
- **Nonnegotiable actual demonstration:** start OGSR CoP Jupiter real level, move camera and record a **sequence of 30+ genuine DX12 game frames** with non-synthetic level geometry at world transforms, depth and at least a repeatable textured subset using real OGSR DDS and safely interpreted UV/normal/material metadata; two+ distinct camera positions, correct occlusion/visibility, measurable draw count/coverage, GPU frame fences and DX12 Present originating from a game-owned queue/swapchain. Explicitly show DX11/R4 fallback in separate run. If native DX12 swapchain takeover cannot be made safe without broad rewrite, record BLOCKED for final B3 (no false PASS based on offscreen-only images); keep partial B1/B2 on feature branch.
- **Architecture:** derive from accepted M23-R1 feature HEAD `009ed868b431180f6c567c24cbe5444f7f4671b3` and previous M1-R2 source. Directly reuse validated immutable CPU four-asset source identity/session/epoch and M23 bounded GPU uploads/last-use fence/quarantine; no separate VFS→convert duplication in the main new path. Bounded sector/instance static traversal, source reuse and stable per-object constants. Keep one GR Job System only (no new worker pool); owner-serial recording initially, optionally prove limited per-worker allocator/list J3 route after owner frame sound, never move VFS/game mutability/allocator sharing to workers. Genuinely owner-managed device/queue, frame-slot fence before allocator reuse, descriptor/RT/backbuffer generation and completion-safe retire, resize/minimize/windowed Present/teardown.
- **Materials minimum (NOT full PBR):** audit CoP/OGSR OGF/source declaration, DDS/THM and actual normal/UV/material mapping; implement deterministic supported legacy material subset with sRGB decode and linear lighting policy, SRV/sampler and texture upload barriers/fences; clear fallback for missing/unsupported alpha/detail/foliage, no fabricated full PBR/shipping equivalence. Full F4-PBR.1–3, sky/CSM/HUD, DXR, clouds V-Q4 and physically complete lighting are separate future milestones.
- **Development / evidence:** dedicated published GitHub branch `feature/f40-b-game-dx12-vertical-slice` from pinned `009ed868b431180f6c567c24cbe5444f7f4671b3`; no force-push/amend/squash, no source merge into `prypiat`, protect existing worktrees and preserve unrelated cloud/PhysX roadmap. Checkpoint commits for CPU→GPU instances/geometry, textures/materials, owner frame runtime, optional GR J3 foundation, evidence; only local targeted compile/smoke per checkpoint, no repeated full matrix. Once complete one final full clean feature-off/on/camera Release x64, CPU negative/ASan/GR and GPU GBV/InfoQueue/draw/fence/reset/resize/window lifecycle, Jupiter real camera+DX11/R4 Zaton, artifacts raw screen captures/PNG hashes, frame sequence/metrics and one joint Codex review. Fail fast on known memory/GPU UAF rather than postpone to final test. No FPS promises without measurements.
- **Formal gate:** M23-R1 current code is `PASS WITH NOTES` for diagnostic only; production `prypiat` remains unmerged and **choosing B is NOT owner authorization to merge**. Even after successful B result and Codex `PASS/PASS WITH NOTES`, require **distinct explicit owner + Tech Lead GO for controlled non-squash integration** and focused postmerge R4/DX12 smoke. If performance/scaling of worker CL is not convincingly beneficial, preserve owner serial. Keep VFS `-file_activity` concurrency limitation documented and do not imply blanket fix.

> **Инженерная спецификация из истории проекта — Совместимость legacy материалов и целевая PBR модель.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

#### F4-PBR — Physically Based Materials & Lighting (решение 2026-10-30; PLANNED)

**Статус:** архитектурное направление закреплено; **реализация и GPU PASS не
подтверждены**. Это обязательный рубеж качества для полноценного игрового
**DX12 raster lighting**, но **не гейт раннего F4.0** и не требование перед
исследовательскими G0–G2 или первыми G3 RT-тенями. До интеграции игровых
RT-отражений и RTGI (G5) должна быть принята согласованная модель материалов,
общая для raster и ray-hit shading. Само добавление этого раздела не означает
перенос PBR из DX11 или готовность DXR.

**Ориентир:** открытая реализация **IX-Ray 1.6 STCoP**, ветка `default`,
исследованный commit `fb73ad7f3de241bd3e3576f9af9fcd263cd26561`.
Она реализует THM-выбор PBS-материалов, packed `BUMP/BUMP#`, отдельные
legacy/PBS lighting paths, metallic/roughness/AO в G-buffer, GGX/Smith/Fresnel,
IBL и привязку к погоде/небу. Это *референс для анализа*, **не готовый DX12
backend и не основание копировать исходники без анализа лицензии**.

**Контракт совместимости (Asset Compatibility First):**

- `Legacy Materials` используют существующие OGSR/X-Ray OGF, DDS и THM с
  документированными значениями albedo/normal/gloss, roughness, F0, metallic,
  AO, alpha-test и fallback. Массовая переконвертация ассетов **не требуется**;
  режим DX11/R4 остаётся независимым baseline/откатом.
- `Enhanced Materials` по мере появления могут задавать base color,
  normal, metallic, perceptual roughness, AO, emissive и, где необходимо,
  SSS/translucency. Нельзя считать отсутствие карты ошибкой загрузки.
- Для совместимости допускается изучить IX-Ray packing:
  `BUMP` — нормаль в G/A, optional height в R;
  `BUMP#` — metallic/R, roughness/G, vegetation SSS/B, AO/A. Формат,
  соглашения каналов, normal Y, alpha и gamma **сначала проверяются** на
  существующих CoP/OGSR ресурсах; не вводить несовместимое переименование DDS.
- Один типизированный `MaterialID`/material record и проверяемые GPU
  texture handles должны обслуживать deferred raster и будущий DXR
  `ray-hit material lookup` вне экранного G-buffer; не использовать
  временные CPU pointers без lifetime/generation-гарантий.

| Подэтап | Содержание | Гейт / доказательства |
|---|---|---|
| **F4-PBR.0 — Asset/Shader Audit** | Инвентаризировать OGSR THM/DDS, gloss/specular, normals, alpha test, detail/terrain, солнце/sky/hemi, старые HLSL; сопоставить с IX-Ray `tmPBR_Material`, `UsePBRTexures`, `USE_PBR`, `USE_LEGACY_LIGHT` и `BUMP#`. Проверить лицензионные условия перед переносом кода. | ADR: матрица Legacy → Enhanced, карта полей и sRGB/linear, перечень несовместимостей; никакого изменения игрового DX11. |
| **F4-PBR.1 — Material Contract & G-buffer** | Разработать DX12 material/texture binding и explicit layout: albedo, normal, metallic, roughness, specular/F0, AO, material ID, depth, motion vectors; documented alpha/foliage/SSS. Отдельно выбрать RT formats, точность и packed/unpacked политику; реализовать deterministic legacy fallback. | CPU/Shader unit tests, debug views каждого поля, стабильные ID/lifetime при reload, визуально правильный RGB/sRGB и нормали; тесты текстур с отсутствующими каналами. |
| **F4-PBR.2 — PBR Lighting & IBL** | Реализовать и проверить физически обоснованный Cook–Torrance/ GGX BRDF с Fresnel и visibility/Smith, energy handling, диапазонами roughness/F0; direct sun/local lights и атмосферно согласованное IBL от погодного неба. Учесть exposure, linear HDR и смену суток, не копировать эвристические коэффициенты IX-Ray без оценки. | Референсные BRDF/sphere tests (dielectric/metal, rough/smooth), day/night/weather A/B, стабильно работающие raster lighting, GPU Debug Layer/GBV без ошибок. |
| **F4-PBR.3 — Game Integration & Validation** | Применить материал ко всем необходимым игровым классам: статика, terrain/detail, foliage alpha cutout, dynamic/skinned, прозрачность/HUD по отдельному обоснованному контракту. Интегрировать через настоящий CoP/OGSR DX12 runtime, сохраняя DX11 fallback. | Сопоставимые кадры DX11 и DX12: помещение, улица/мокрые поверхности, лес/ветер, ночь/день; G-buffer captures, PSO/SRV validation, resize/reload/shutdown, GPU ms, VRAM, CPU p95/p99. Список unsupported материалов без ложного PASS. |
| **F4-PBR.4 — DXR Material Readiness** | Описать общий material record и доступ к albedo/normal/roughness/metallic/alpha/emissive по ID+UV в ray hit, включая объекты за пределами кадра; проверить descriptor ownership, residency, alpha-tested vegetation и fallback. **Без обязательной реализации DXR в этом подэтапе.** | Контракт и тестовая проверка material lookup готовы для G4/G5; RT-отражения и RTGI не начинают production-интеграцию без этого гейта. |

**Зависимости:** F4.0-C обеспечивает лишь минимальный textured preview.
F4-PBR.0 можно проводить параллельно F4.0, но F4-PBR.1–3 подключаются к
полному F4 по мере готовности игрового geometry/texture path. F5 temporal
опирается на документированные normal/depth/material/motion контракты;
переход F4 → G0–G3 возможен без обязательных PBR-карт. Для игровых G4/G5
материальный контракт F4-PBR.4 обязателен; для RT-отражений/RTGI необходима
принятая базовая PBR-модель. Никаких заявлений о FPS без GPU измерений.

**IX-Ray reference (точные исходники):**

- [IX-Ray PBS/texture packing](https://github.com/ixray-team/ixray-1.6-stcop/blob/fb73ad7f3de241bd3e3576f9af9fcd263cd26561/docs/docs/main/ru/graphics/pbr.md).
- [THM material detection](https://github.com/ixray-team/ixray-1.6-stcop/blob/fb73ad7f3de241bd3e3576f9af9fcd263cd26561/src/Layers/xrRender/TextureDescrManager.cpp) и [shader option binding](https://github.com/ixray-team/ixray-1.6-stcop/blob/fb73ad7f3de241bd3e3576f9af9fcd263cd26561/src/Layers/xrRender/uber_deffer.cpp).
- [Material/G-buffer](https://github.com/ixray-team/ixray-1.6-stcop/blob/fb73ad7f3de241bd3e3576f9af9fcd263cd26561/gamedata/shaders/d3d11/metalic_roughness_base.hlsli), [GGX direct light](https://github.com/ixray-team/ixray-1.6-stcop/blob/fb73ad7f3de241bd3e3576f9af9fcd263cd26561/gamedata/shaders/d3d11/metalic_roughness_light.hlsli), [IBL](https://github.com/ixray-team/ixray-1.6-stcop/blob/fb73ad7f3de241bd3e3576f9af9fcd263cd26561/gamedata/shaders/d3d11/metalic_roughness_ambient.hlsli).
- [IX-Ray license](https://github.com/ixray-team/ixray-1.6-stcop/blob/fb73ad7f3de241bd3e3576f9af9fcd263cd26561/LICENSE.md): проверить совместимость до любого дословного переноса; в этом milestone закреплены только архитектурные решения.


## 7. DXR hybrid architecture и material contracts

Текущий статус аппаратного DXR — **PLANNED**. Первое внедрение обычного BLAS/TLAS и RT shadows, не экспериментальные CLAS/PTLAS. Подробные варианты renderer архитектуры:

> **Инженерная спецификация из истории проекта — Hybrid DXR для X-Ray outdoor.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

#### X-Ray DX12/DXR — Hybrid Rendering Architecture (решение 2026-10-22)

**Статус:** ✅ **принято как направление проектирования; НЕ реализовано и НЕ
протестировано на GPU**. Это архитектурные ограничения и исследовательские
ориентиры, а не обещание конкретного качества, FPS или буквального
воспроизведения Northlight/4A Games. Решения о каждом алгоритме — отдельные
ADR, прототип и гейт с измерениями.

**Главный принцип:** обычный **DX12 raster — самостоятельный полнофункциональный
рендерер** (основная геометрия, G-buffer, CSM/shadow maps, свет и постобработка);
**DXR добавляет выборочные проходы**, устраняющие конкретные ограничения
raster/SSR. Сохранить рабочий **DX11/R4** как независимый baseline и откат.
Отсутствие DXR-capable GPU, ошибка RT-инициализации или отключение RT **не
должны препятствовать** запуску базового игрового DX12-пути. DX12 raster,
DX12 + DXR и DX11 сравниваются и тестируются раздельно.

| Источник | Что изучаем | Что НЕ переносим автоматически |
|---|---|---|
| **Control / Remedy Northlight** | Общие acceleration structures для нескольких RT-эффектов; выборочные opaque/transparent reflections, near-field indirect diffuse и contact shadows поверх raster; облегчённый ray-hit shading, отдельное вычисление света, специализированные spatial/temporal denoisers. | Фиксированные дистанции лучей, лимиты источников света, предположения об интерьерах и готовом baked/voxel GI: параметры определяются **сценами X-Ray и профилями**. |
| **Metro Exodus PC Enhanced Edition / 4A Games** | Динамический RTGI от солнца, неба и локальных/эмиссивных источников; представление сцены для лучей, упрощённые GI-материалы, временное накопление, кандидаты вроде DDGI для дальнего непрямого освещения открытых пространств. | Обязательный DDGI/RTXGI, полный path tracing, перенос конкретной структуры проб или бескомпромиссная замена shadow maps. RTGI — отдельное решение **после** стабильных теней и отражений. |
| **X-Ray / OGSR** | Большие outdoor-локации, динамические сутки/погода, ветер и foliage, сталкерская атмосфера, Legacy Materials, скелетная анимация и совместимость с игровым runtime. | Требование массовой переконвертации моделей и текстур; слом DX11 и «RTX-only» игра. |

**Технические контракты, которые должны пережить F → G:**

1. **Базовые данные кадра (F4/F5):** стабильные depth/normal/material IDs,
   преобразование существующего gloss в документированную roughness/BRDF
   политику; motion vectors, jitter, exposure и сигналы сброса temporal history
   для denoisers/upscalers. Данные и их формат фиксировать в контракте
   deferred lighting; не объявлять все карты PBR обязательными для Legacy.
2. **RT Scene и lifetime (G1/G4):** общий для выбранных RT-проходов
   BLAS/TLAS path на базе существующей геометрии, стабильные render handles,
   explicit GPU lifetime/fences и проверяемые правила build/refit/rebuild.
   Геометрия RT должна учитывать объекты **вне camera frustum**, влияющие
   на отражения/освещение/тени; RT selection, LOD, distance masks и residency
   измерять, а не просто копировать raster visibility или весь игровой мир.
   Динамика, NPC, foliage и streaming имеют отдельные invalidation-гейты.
3. **Ray-hit material lookup (G2/G4/G5):** точка попадания вне экранного
   G-buffer обязана получить material ID, UV, нормаль и нужные ресурсы.
   Изучить компактную запись hit attributes и отдельный compute shading pass
   как **кандидат оптимизации**, не как обязательный API. Alpha-tested
   растительность/заборы и прозрачность не объявлять opaque по умолчанию.
4. **Гибридные эффекты (G3/G5):** G3 — ограниченные RT contact/local shadows
   как дополнение к CSM/shadow maps, со строго определённым fallback.
   В G5 **сначала отражения** с отдельным A/B-гейтом (SSR/environment
   fallback, шероховатость, стекло и мокрые поверхности); **затем** отдельно
   исследовать near-field indirect diffuse и RTGI/DDGI для смены погоды и суток.
   Никакой перенос G5 в F4.0 и никакого «всё RTX сразу».
5. **Denoising/temporal (F5, G3/G5):** для shadows, reflections и diffuse GI
   предусмотреть *разные* фильтры/истории, используя depth, normals, motion и
   confidence/roughness при необходимости. Проверять disocclusion, смену
   камеры/уровня, resize, движение NPC/ветра и смену солнца/погоды; историю
   инвалидировать предсказуемо, без ghosting/light leaking. Сначала baseline
   denoiser с проверяемым поведением; DLSS Ray Reconstruction — **отдельная
   интеграция**, не автоматическая часть DXR.

**Порядок принятия (не меняет номера и зависимости существующих этапов):**

- **F3.6/F4.0:** завершить реальный игровой DX12 geometry frame; никакой
  зависимости от трассировки. **F4/F5:** полноценные raster-проходы и
  готовность G-buffer/temporal данных; только затем RT в штатном игровом кадре.
- **G0–G2:** feature query, BLAS/TLAS, raygen/miss/hit и диагностика без
  объявления игрового RTX. **G3:** первые контактные тени + fallback и
  GPU budget. **G4:** skinned/foliage/material/RT lifetime validation.
- **G5:** независимый milestone RT reflections; после его измерений отдельный
  ADR о целесообразности near-field GI, DDGI или иного outdoor RTGI.
  Path tracing — только самостоятельное долгосрочное исследование без
  обязательства реализации.

**Матрица доказательств и критерии для каждого RT milestone:**

- Сравнивать **DX11/R4**, **DX12 raster (RT off)** и **DX12 + конкретный
  RT-эффект (RT on)** на одной сцене, разрешении, погоде и времени суток;
  фиксировать неизвестные/различающиеся функции renderer.
- Набор сцен: **помещение/подвал**, улица **Припяти** (окна, мокрые поверхности),
  **Кордон/лес** (листва, alpha test, ветер), открытый горизонт/солнце;
  день/ночь и переход освещения/погоды — по мере доступности runtime.
- Сохранять GPU captures/markers, отдельные visual debug outputs
  (shadow/reflection/GI mask, ray hit/miss, history validity), A/B screenshots,
  Debug Layer/GBV/DRED; показатели GPU pass ms, CPU frame, p50/p95/p99
  frametime, VRAM/residency и изменение количества RT primitives/instances.
- Проверять холодный старт/переключение RT off/on, GPU без DXR, resize,
  level reload, teleport/camera cut, массовые dynamic objects, wind/foliage
  и длительный прогон. При ошибке или неподдерживаемом tier — корректный
  fallback, а не device loss. **Нет объявленного FPS-бюджета до baseline**.
  Положительный результат измерений не заменяет проверку артефактов и отката.

**Первоисточники (для выбора алгоритмов, не доказательство их реализации):**

- [Remedy / NVIDIA: Ray Tracing in Control, Ray Tracing Gems II, глава 46](https://link.springer.com/chapter/10.1007/978-1-4842-7185-8_46) — архитектура гибридного RT, reflections, indirect diffuse, contact shadows и denoisers.
- [4A Games: In-Depth Technical Dive into Metro Exodus PC Enhanced Edition](https://www.4a-games.com.mt/4a-dna/in-depth-technical-dive-into-metro-exodus-pc-enhanced-edition) — динамический RTGI/DDGI и материалы.
- [4A Games: Everything Technical about Metro Exodus Upgrade for PS5 and Xbox Series X|S](https://www.4a-games.com.mt/4a-dna/everything-technical-about-metro-exodus-upgrade-for-playstation-5-and-xbox-series-x-s) — аппаратные/производительные ограничения.
- [Microsoft: DirectX Raytracing Functional Specification](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html) — нормативные DXR API, capabilities, BLAS/TLAS и dispatch.
- [Microsoft: Direct3D 12 Programming Guide](https://learn.microsoft.com/en-us/windows/win32/direct3d12/directx-12-programming-guide) — resource lifetime, command lists, descriptor/barrier model.

> **Инженерная спецификация из истории проекта — Legacy/Enhanced материалы.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

#### Совместимость материалов Legacy / Enhanced (проектное правило)

**Asset Compatibility First:** существующие OGSR/X-Ray текстуры, модели и материалы должны отображаться на DX12 и поддерживать начальные DXR-тени **без обязательной конвертации ассетов**. `Legacy Materials` сохраняют стиль старого контента через документированные fallback-параметры; `Enhanced Materials` — опциональные карты/метаданные для более точного PBR, мокрых поверхностей, отражений и расширенных эффектов. Это правило проектирования, а не подтверждённая реализация.

- **F4:** работающий Legacy raster path, сравнение с DX11 в одинаковой локации, при одинаковой погоде и освещении; корректные normal/gloss, alpha cutout и material lookup.
- **G1–G3:** BLAS/TLAS и первые RT-тени используют существующую геометрию, дополнительных roughness/metallic-карт не требуют.
- **G4:** обеспечить доступ к свойствам поверхности в точке ray hit (в том числе вне экранного G-buffer), alpha-tested foliage и динамические материалы; проверить обе модели материалов.
- **G5:** дополнительные карты повышают точность отражений/indirect lighting, но эти эффекты — отдельные необязательные рубежи.
- **Качество:** Legacy не обязан быть физически идентичен Enhanced; при этом отсутствие новых карт само по себе **не повод ухудшать** визуальную совместимость с DX11. Все эффекты проверяются A/B и GPU profiling.

Подробный контракт, ограничения DXR, правила fallback и матрица приёмки: [DX12/DXR Material Compatibility](dx12-dxr-material-compatibility.md).


## 8. Volumetric Clouds — погодная система и интеграция

**Приоритет интеграции (решение владельца 2026-11-21):** наша sky/cloud система должна появиться **рано в игровом native DX12, через F4.2-SKY** ([Issue #15](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/15)); не воспроизводить старое небо R4 полностью лишь для последующей замены. Это **только opt-in integration R&D**, не отменяет следующие standalone source/diagnostic gates и Classic 2D fallback.

**Актуальный блокер (2026-11-21): V-WEATHER-01C-B-R4-R1, независимый Codex STRICT READ-ONLY → REQUEST CHANGES (4×P2, 1×P3); Tech Lead подтвердил source findings, FINAL 01C-B HOLD, узкий R4-R2 REQUIRED.** Последняя точная поставка SHA256 `8c066d84db4195d801582b2b9be6196235ee03a632140cc9541628e8be11635b`: independent ZIP/SHA/patch PASS (1050/1050 payloads, 136/136 source, 34/34 HLSL, 5 EXE), 58/58 author fault rows consistent but тестовые критерии неполны. Safe terminal & last-submitted-fence check source improvements retained; **CloudProfiler/interactive input/transition/timing writes bypass the safe diagnostic gate, `#endif        flushDeferred();` swallows loop flush, blocked pipes restored before minimize/restore, witness-disabled parser omits pipe-status check, CRT pipe fd rollback incomplete**. Historical static **4/16 NOT MET** (188 channel values at max 1 LSB), same-session R2/R4-R1 16/16; transition+GBV **NOT RUN** (author 770–772 s frame; no clean shutdown), require explicit disposition. **01C-C NOT RUN, V1/V2 formal NOT ACCEPTED, V-L/V-S1 NOT STARTED, V-Q4 BLOCKED**; no production/DX11/R4/HLSL changes.

| Очередь | Что реализовать | Гейт |
|---|---|---|
| **R4-R2 narrow diagnostic correctness** | Cover **all** blocking diagnostic sinks (input P/C, CloudProfiler Collect, weather transitions, settings, frame timing file) under last-submission-fence/idle contract; fix separate `#endif` + `flushDeferred()` and bounded 16-event overflow; test real BeginFrame reuse + minimize/restore while both pipes blocked; enforce witness-disabled pipe-status and test-only stream fd/HANDLE transactional rollback | Codex R4-R1 **REQUEST CHANGES 4×P2/1×P3**; require source/fault evidence, developer/shipping runtime and independent Codex. Preserve GPU overlap, no WaitForGpu each frame; historic PNG & transition+GBV separately unresolved, 01C-C HOLD |
| Fault proofs | Точные hooks, handle/CRT return values, witness activation и negative controls | Failed activation=FAIL, не считать timeout PASS |
| 01C-C | Same-session GPU p50/p95/VRAM, rain/clear/cloud profiles, resolutions | Измеренная GPU цена и safe default |
| V1/V2 | Visual acceptance текущего base, R03A перед спорными HCS1/HCS2 | Никаких непроверенных крупных морфологических изменений |
| V-Q4 / **F4.2-SKY early** | Ранняя адаптация нашего sky/cloud к game-owned DX12 HDR/depth composite с CEnvironment sun/weather/wind; начальная интеграция opt-in, полный V-Q4 acceptance зависит от закрытия V-WEATHER и тестов | Real game captures, indoor occlusion, Classic fallback, resize/reload, barriers/fences; V-Q4 пока NOT ACCEPTED |
| V-L/V-S | Lightning, weather transitions, surge после облачной интеграции | Игровая логика и fallback сохраняются |

Наличие game-owned DX12 queue/Present из F4.1 ещё не доказывает доступность HDR sky/color/depth для облаков. В игровом режиме один device/queue/GR, а standalone стенд может иметь свой. `CEnvironment` определяет погоду, shipping пользователь — качество/режим, не debug seed.

> **Инженерная спецификация из истории проекта — Общий weather/surge API и источники игровых событий.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

##### V-PLAN-C — единый контракт X-Ray погоды, молний и выброса

1. **Игра управляет временем и событиями.** CEnvironment::WeatherCycles / CurrentEnv дают уже интерполированные sun/sky/clouds/wind/fog/rain; WeatherFXs — временные погодные FX, **не автоматический выброс**; surge_manager.script и проверенная геймдата задают game-authoritative surge start/phase/end, не C++ CALifeSurgeManager. Никакой визуальный DX12 компонент не управляет дождём, игровой угрозой, укрытиями, Lua triggers, звуком/повреждением или погодным таймером.
2. **Один неизменяемый frame snapshot.** Camera + CurrentEnv/time/weather IDs + validated thunder/surge event + quality generation из **одного game frame**; параметры отправляются основному DX12 через game-owned device/queue/frame/fences, CPU при необходимости использует текущий gr::jobs::JobSystem. Raymarch/density/shadow/illumination — GPU compute; без второго GPU runtime/job system и независимого cloud weather clock.
3. **Два отображения одного состояния.** Classic 2D — **default** (в DX12 ещё НЕ реализован), Volumetric 3D — опция. В игровом Release пользователь выбирает только *графический режим/качество*, а не weather presets, weather seed, duration или dev debug controls; при off/fallback не сбрасываются погода, ветер и время. DX11/R4 без изменений.
4. **Один импульс грозы / Surge.** До V-L приёмки сохранять legacy thunder; после подключения V-L объёмные вспышки визуально синхронизировать с оригинальным thunderbolt/weather event, **не складывать два независимых lightning light impulse/звука**. Для доступности — проверяемые ограничения интенсивности и частоты вспышек, stable seed; в игре вспышки не инициируют геймплейные события.
5. **World lighting только по готовности.** Standalone V-L1/V-L2 и V-S1/V-S2 — визуальные эффекты облачного объёма, **не доказательство scene HDR/fog/depth/terrain lighting**. Зависимые V-L3/V-S3 подключаются лишь после корректной game-owned HDR/depth/lighting и соответствующих quality/fallback gates.

> **Инженерная спецификация из истории проекта — Единый DX12 владелец кадра и GR для cloud pass.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

##### V-Q3/V-Q4 — единый game-owned DX12 runtime и существующий GR JobSystem (архитектурное решение 2026-11-11; CONTRACT / NOT IMPLEMENTED)

**Обязательное правило для будущей игровой интеграции Volumetric Clouds:** игровой cloud pass **не создаёт второй независимый GPU runtime**. Он использует предоставленные основным игровым DX12 renderer **ID3D12Device**, command queue/command lists, frame slots, descriptors, constant-buffer/upload resources, resource-state/barrier policy, GPU fences и общий порядок resize/reload/shutdown. Владение, публикация команд, GPU completion и retirement ресурсов определяются единым игровым frame lifecycle. **GameDx12Bridge** с отдельным device/queue остаётся только diagnostic/test infrastructure и не удовлетворяет этому контракту.

**CPU preparation / GPU execution:** обновление погоды (**CEnvironment**/timeline), камера, солнце, ветер, cloud time, seed, transition state и graphics quality формируют согласованный immutable per-frame cloud snapshot из **той же версии игрового кадра**, что и геометрия/небо. CPU подготавливает константы и Dispatch/composite commands, **но не рассчитывает изображение облаков на CPU**: ray marching, density, lighting и self-shadowing выполняются GPU compute shaders. Нельзя допускать независимый cloud frame clock, stale camera/weather snapshots или сброс weather state при Clouds Off/On/quality switch.

**Единый планировщик CPU:** **не создавать второй JobSystem/thread pool для cloud integration**, дублирующий существующий **gr::jobs::JobSystem**. Если CPU cloud preparation действительно требует распараллеливания, она использует существующий GR scheduler и его жизненный цикл/синхронизацию; при малой стоимости достаточно owner-thread подготовки. Cloud GPU compute не следует ошибочно оформлять как CPU render-preparation job. Командная запись работниками возможна только при соблюдении DX12 allocator/list ownership и общего frame snapshot.

**GPU queue policy:** начальный V-Q4 preview выполнять через game-owned Direct Queue с явными resource-state transitions и корректной синхронизацией. Возможная отдельная async Compute Queue рассматривается **только** как ресурс, принадлежащий общему игровому renderer, после отдельного profiling/overlap/correctness gate; она не даёт разрешения на независимый cloud device/queue или отдельный GPU lifetime.

**Acceptance / boundary:** перед V-Q4 проверить source-level dependency injection без cloud-owned device/queue/JobSystem, одинаковую camera/weather/frame generation, fence-safe retirement/resize/reload, GPU Debug Layer/GBV/InfoQueue, переходы Clouds On/Off и неизменность DX11/R4. Standalone harness вправе владеть собственным device/queue для самостоятельных тестов — **это не game integration PASS**. Пункт является утверждённым **архитектурным контрактом**, не реализованной возможностью: V-Q4 **BLOCKED**, V2/V3 **NOT ACCEPTED**; текущий **V-WEATHER-01C-B** остаётся самостоятельным standalone checkpoint без расширения его scope.

> **Инженерная спецификация из истории проекта — Погодные режимы, переходы и параметры; старые reviews ниже не размножаются.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

#### V-WEATHER — погодные режимы, переходы и настройки облаков (решение 2026-11-08)

**Актуальная задача параллельного облачного трека:** развивать уже работающую standalone DX12 cloud system, не дожидаясь полного F4.0, и затем перенести её в игровой DX12. Новых V-Visual/FOMS/Storm-morphology экспериментов без доказанного блокера не проводить. **Original V-SF Rain — неизменяемый контроль**; Gate C не применять, V-03 оставить контрольным вариантом.

**Режимы первой версии:** `clear` (малооблачно), `partly` (переменная облачность), `overcast` (пасмурно), `rain_sf` (локальная дождевая масса оригинального V-SF Rain); `storm_preview` — только экспериментальный developer preview, визуально **NOT ACCEPTED**. `sunset` считать пресетом освещения/времени суток, а не новой морфологией. Сохранять legacy CLI `--preset=rain`, `--preset=storm` и исходные hashes/PNG при статических endpoints.

**Переходы:** переиспользовать существующие `StartCloudTransition()` и `LerpCloudParams()` для совместимых численных полей. Между обычным полем и V-SF Rain `stormFront` — дискретный bool, поэтому простой toggle вызывает скачок: требуется непрерывный `modelMix` density/extinction **в world-space**, учитывающий то же поле в light/shadow sampling; без screen-space crossfade, пересоздания noise или внезапной смены seed/time. При `mix=0/1` неизменный исходный быстрый path, на mid-transition измерить GPU cost. Контролировать bounds, повторное переключение из текущего состояния, pause/resize/reset и сохранение направления ветра/солнца.

**Release UI / безопасность пользовательских параметров — строгое разграничение:**

- **В обычном игровом Release ручная настройка погоды СКРЫТА И НЕДОСТУПНА.** Игрок не выбирает `clear/partly/overcast/rain_sf/storm_preview`, не меняет длительность погодного перехода, weather seed, density/weather debug values и не получает dev-хоткеев. Недостаточно спрятать кнопку: не регистрировать debug weather CLI/console commands, debug-панель и ручные override пути в shipping build. Игровое состояние погоды формируется штатной системой `CEnvironment`/сценариями. Developer weather override не должен попадать в release user config.
- **Пользовательские настройки ГРАФИКИ в Release доступны:** выбор Classic 2D / Volumetric 3D после V4, проверенные профили качества облаков (Performance / Balanced / Quality / Reference либо безопасный сокращённый набор), on/off при наличии поддержанного fallback. Они управляют только качеством и GPU-ценой рендеринга, **не игровой погодой**. DX11/R4 сохраняется.
- **Standalone и явно включённая developer/test build:** разрешены ручные погодные пресеты, scripted sequences, CLI, хоткеи, `storm_preview`, debug overlay и настройка длительности перехода для visual/GPU validation. Разделить development-only features явным build/config guard с тестом shipping-off; не путать Release-бинарник standalone runner с игровым shipping Release.

**Ближайшие гейты, выполнять последовательно:**

1. **V-WEATHER-01A — статические режимы (ПЕРВЫЙ):** включить существующие `clear/partly/overcast`, отдельно именованный `rain_sf` (immutable original V-SF Rain), `storm_preview` только как debug. 800×450 Front/Ground/Oblique, fixed seed/time/camera/sun; `rain_sf` при равных условиях **byte-identical** Gate B/Gate D; legacy `--preset`/feature-off без регрессий. Build/PNG/GBV/InfoQueue/patch evidence. Без нового density HLSL.
2. **V-WEATHER-01B — плавные модели:** `clear→partly→overcast→rain_sf→partly`, `rain_sf→overcast→rain_sf`, кадры 0/25/50/75/100%, плавность в world-space, независимый контроль endpoints, прерывания перехода, смещение камеры, замеры GPU median/p95 в mid-blend.
3. **V-WEATHER-01C — настройки и release-gating:** развести quality vs weather, реализовать standalone settings persistence и validation/restore defaults, profiles и clouds-off, tests restart/resize/feature-off. Shipping build/CLI tests доказывают **отсутствие** ручных weather controls, а качество и cloud mode остаются настраиваемыми. Не изменять production игрового UI до отдельного разрешения.
4. **V-Q4/V3 позже:** после game-owned DX12 camera/HDR/depth/queue и нормального frame lifecycle — адаптер к `CEnvironment`, HDR compositing, depth, fallback, GPU timings, погодные сценарии. GameDx12Bridge diagnostic device **не** использовать как game-owned renderer. Для первого opt-in preview допустим fixed Rain; полноценная V3 приемка только после погодных и игровых тестов.

**Текущий статус на 2026-11-11 (последние независимые аудиты по evidence ZIP; Windows GPU не запускался повторно аудитором):**

| Подэтап | Текущий результат | Ограничение / следующий гейт |
|---|---|---|
| **V-WEATHER-01A** — static modes | **PASS WITH NOTES**: `clear`, `partly`, `overcast`, immutable original `rain_sf`, developer-only `storm_preview`; контрольные PNG byte-identical. | Storm визуально **NOT ACCEPTED**, никакой новой морфологии. |
| **V-WEATHER-01B / B1-R1** — переходы | **TECHNICAL PASS WITH NOTES**: world-space density blending, forward/reverse symmetry и ε-endpoints исправлены; source/target sampling корректный. | Длинные вертикальные формы в промежуточном Overcast — известное художественное ограничение; release visual gate не принят. |
| **V-WEATHER-01B / B2A-R1** — автоматический timeline | **LIVE INTERRUPT COMPLETION PASS WITH NOTES**: `Clear → Partly → Overcast → Rain SF → Partly`, динамическое прерывание 35%/65% и позднее прерывание с доведением до нового endpoint. | Float-time примечание в обычной 120-frame последовательности не мешает static-exact endpoint. |
| **V-WEATHER-01B / B2B-P0/P1** — стоимость GPU | P0 **ATTRIBUTION PASS WITH NOTES** (тройная оценка density в shadow loop — основной hotspot); P1 **EXPERIMENTAL PASS WITH NOTES**: conservative `shadow OD early-out 1%` уменьшает стоимость примерно на 14–20% в проверенных случаях при ≤1 LSB визуальных различий. | **OD 1% opt-in developer candidate, не default.** OD 5% и inner-only shadows **NOT ACCEPTED**; standalone 1600×900 и three-state всё ещё дороги, raw telemetry/process outlier; **GPU PERFORMANCE NOT ACCEPTED**. |
| **V-WEATHER-01C-A** — конфиг/профили | **CONTRACT PASS WITH NOTES**: schema v1 (`schema_version`, `clouds_enabled`, `cloud_render_mode`, `cloud_quality`), persistence, on/off, Performance 64/2 half, Balanced 96/4 half, Quality 128/6 half, Reference 128/6 full. | Classic 2D DX12 path ещё отсутствует; корректный fallback обязателен. |
| **V-WEATHER-01C-A-R1** — строгая developer/shipping изоляция | **SHIPPING-CONTRACT PROXY PASS WITH NOTES**: закрыты legacy `--seed`, `--size`, `--format`, device/slices overrides, noise hotkeys; 102/102 CLI/config tests и 16/16 static PNG regression по evidence. | **НЕ GAME SHIPPING PASS:** game `CEnvironment` adapter, console/UI, config parser и shipping executable ещё не проверены в OGSR. |
| **V-WEATHER-01C-B** — runtime graphics settings | **R4-R1 independent Codex REQUEST CHANGES (2026-11-21): 4×P2 + 1×P3; Tech Lead SOURCE CONFIRMED, FINAL HOLD.** R4-R1 ZIP `8c066d84…11635b` manifest+patch exact; 58/58 author fault records consistent; previous R4's terminal and last-fence improvements preserved. | **R4-R2 REQUIRED (narrow):** bypass of safe GPU diagnostic gate via CloudProfiler/P,C/transition/settings/timing sinks; swallowed loop flush `Application.cpp:2733`; blocked-pipe test restores streams before actual minimize/restore; parser witness-disabled case omits pipe-status, stream ownership/rollback P3. Historical PNG **4/16 NOT MET**, transition+GBV **NOT RUN** despite author frame completion; independent review and Tech Lead acceptance needed. **01C-C NOT RUN, V-Q4 BLOCKED.** |
| **V-WEATHER-01C-C / final standalone qualification** | **NOT RUN**: повторяемая проверка GPU p50/p95, clocks/thermal/coverage, GPU/VRAM lifetime, настройка безопасного quality default, regression и документация экспорта. | Без общего production/performance PASS и без ложной гарантии 60 FPS. |



> **Инженерная спецификация из истории проекта — Последнее значимое независимое review с реальными блокерами.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

##### V-WEATHER-01C-B-R4 (REBUILT) — Independent Codex STRICT READ-ONLY review + Tech Lead decision (2026-11-21; REQUEST CHANGES 2×P2, R4-R1 REQUIRED)

- **Verified artifact and review:** Codex independently read **the rebuilt R4** ZIP SHA256 **bfc4bdd5d74da39226617a2093981e5819d207f7d6ae67d6aedda69a7d5151c8** (not the superseded c1a47f...); checked 863 SHA manifest entries, five Windows EXE hashes, 13-file patch vs R3 fca08ade..., and read all 49 raw fault records/parsing. Tech Lead re-opened exactly the same R4 ZIP source for this gate. **Independent verdict REQUEST CHANGES; final standalone V-WEATHER-01C-B HOLD.** No Codex Windows/real GPU compilation, CTest/GBV or fault replay; read-only source/evidence proof.
- **P2-A / interactive multi-frame in-flight logger stall:** Renderer.cpp **470–504** BeginFrame() waits **only backBufferFenceValues_[index]** (3 back buffers, Renderer.h:105), then runs **FlushPendingPresentNotice()** (calls LogWarnBestEffort/LogBestEffort → potentially blocking sink) and **DrainDebugMessages("frame")** (also emits synchronous messages). That fence only proves completed work **up to that frame's value**, not any *later* frame submissions still on the same queue. Application.cpp **2522–2563** RunInteractive() directly Log()s window minimize/restore while earlier Present frames can remain pending. This violates documented no-blocking-diagnostic-while-unknown-completion intent in Application.cpp:2530–2534. **Not a proven COM use-after-free**, but a reproduced-by-source **P2 liveness/safety-contract hole**. R4-R1 must remove/defer/block-proof these diagnostic operations during multi-frame in-flight, without performing a full GPU WaitForGpu on every BeginFrame (would serialize CPU/GPU and invalidate timings). Audit other interactive diagnostics reachable while later GPU work is pending, including occlusion/resize.
- **P2-B / stderr proof incomplete:** CloudFaultInjection.cpp **154–169** ActivatePipeHandle ignores return status of SetStdHandle and _dup2 (also check _open_osfhandle, handle ownership/CRT and GetStdHandle/actual FILE fd mapping). **245–250** unconditionally marks BlockedPipesActivated even when one pipe redirect may have failed; **258–262** sole sensitivity probe writes stdout even in both-pipes mode. Therefore timeout proves **stdout** can block but not stderr. Witness-disabled terminal case has no witness activation proof (exit1 alone could be main() exception exit). R4-R1 must expose **verified stdout and stderr activation status** to parent without relying on blocked stdout/stderr, add separate positive stdout-only, stderr-only, both independently exercised sensitivity controls, preserve timeout≠success and ensure failed hook setup=FAIL, and prove terminal completion when logging remains enabled.
- **Coverage note (not new P1):** CloudFaultInjection.cpp **348–381** TerminalFaultPending / PreRetirementFaultPending / ShutdownFaultPending are coarse group predicates; actual activation depends on label/fault pairing. Test disjoint owner-specific faults and mismatch/non-activation negative controls, inspect pre_submit_no_submit which did **not** run with a full pipe (author REPORT §9). Scope statements must distinguish blocked vs not-blocked control and actual Test-only harness instrumentation from complete COM Release tracking.
- **Previous findings retained as fixed for inspected C++ paths:** Common.cpp **176–187** [[noreturn]] noexcept TerminalFailClosed uses mapped-memory atomic WitnessEvent then std::_Exit(1), no stdout/stderr, allocation or ordinary logger. Renderer.cpp TrySignalFence / TryWaitForFence / TryWaitForGpu status/noexcept and ExecuteImmediate submission→fence path prevent C++ stack unwinding before unknown-completion terminal for reviewed paths. R3 immediate terminal P2 closure **not rescinded**; new interactive P2 is separate. Legacy pending-cancel and candidate recoverable rollback tested in earlier scope remain.
- **Visual and validation holds:** Independently R2 current ↔ rebuilt R4 **16/16 exact**, historical **4/16 exact** with 188 differing RGBA channel values max 1 LSB and rain_sf 4/4 exact. Historic gate **NOT MET** until formal separate Tech Lead narrow deviation or strict restoration; no silent baseline rewrite or attribution to Windows hotfix. Transition shader + GPU Based Validation **NOT RUN** after 150s on R2/R3/R4 controls; author Debug Layer-only transition and four regular GBV runs **do not resolve it**. For complete standalone qualification require successful transition+GBV in suitable environment **or explicit Tech Lead gate waiver/modified acceptance with documented risk**; no acceptance now. Overall GPU performance NOT ACCEPTED.
- **Next narrow checkpoint R4-R1 (same 01C-B gate; NOT R5):** test-only fixture 3-buffer multi-frame submission with per-index fence F(k) completed while F(k+1)/F(k+2) not, blocked stdout/stderr/both; test interactive minimize/restore, occlusion, deferred notice and debug InfoQueue; prove absence of synchronous sink during pending later work and continued render progress with bounded watchdog, and no forced full WaitForGpu on every frame. Add independent stdout/stderr sensitivity controls, check pipe replacement syscall success and real CRT stream ownership, parent-observable activation for witness-off case; exact fault-point/owner matching and negative controls. Preserve author source/EXE/patch SHA, static comparisons, 108 CLI, CTest dev/shipping/fault and Debug Layer/GBV/InfoQueue, shipping hook isolation. DeepSeek supplies ZIP; Codex strictly READ-ONLY; Tech Lead separate final decision / docs-only update.
- **Hard gates unchanged:** 01C-C NOT RUN, V2 formal NOT ACCEPTED (V2.3h R02 BLOCKED), V-L/V-S1 runtime NOT STARTED, V-Q4 BLOCKED. No HLSL/morphology/V-SF Rain/OD default, no DX11/R4/game production or duplicate device/queue/JobSystem. No commit/push from DeepSeek.

##### V-WEATHER-01C-B-R4-R1 — Tech Lead artifact/source audit (2026-11-21; targeted REQUEST CHANGES / Codex PENDING / final HOLD)

- **Exact ZIP:** \`OGSR_V_WEATHER_01C_B_R4_R1_Interactive_Diagnostic_Corrections.zip\`, SHA256 \`8c066d84db4195d801582b2b9be6196235ee03a632140cc9541628e8be11635b\`. Independent ZIP CRC PASS **1051 entries** incl manifest; **1050/1050 SHA256SUMS** match; **136/136 source** + **34/34 shader hashes** + **5/5 Windows EXE SHA** verified. Patch \`d101e9b4eff98ac586a0dd77d7fc9c359fb4bf2be47d2f07b1927a4140ccc7d1\`: \`git -c core.autocrlf=false apply --check\` + actual apply on prior rebuilt R4, **136/136 final source paths byte-exact**, 10 changed files, HLSL unchanged. Binary hashes ≠ independent MSVC build provenance.
- **Author fault evidence:** **58/58** rows match per-case exit/timed-out/witness/pipe-status raw files: 37 ordinary terminal exit1, 1 witness-disabled exit1, 1 failure-to-activate negative exit1, 10 healthy/recoverable exit0, 4 interactive inflight exit0, **5 intentional pipe-probe timeouts (exit 0xDEAD)**. Independently checked case counts and failure/no-activation; **author Windows/GPU replay NOT repeated**. Inflight author counters each \`frame_submit_count=9, diag_defer=6, diag_flush=3, diag_unsafe=0, inflight_at_check/defer=2\`; stdout and stderr separate sensitivity controls and failed handle setup negative evidence present. Source \`Renderer::GpuIdleConfirmed()\` compares actual fence with \`lastSubmittedFence_\` including device-removal sentinel, without a full wait on each BeginFrame; \`CloudFaultInjection::InstallStream()\` checks per-stream setup steps; **targeted improvements, not final safety PASS**.
- **New source finding P2 (deferred diagnostics correctness):** \`source/src/Application.cpp:2733\` literally \`#endif        flushDeferred();\`. The tokens after \`#endif\` are **not** an executable call to the deferred-flush lambda (verified with independent minimal C++ preprocess reproduction). Normal interactive developer/shipping builds therefore have no periodic \`flushDeferred()\` call in the loop; only exit-time flush remains, so bounded 16×192 queue may retain/drop events. The special fault-only resume path separately calls the lambda at line ~2668, so author inflight success does not establish ordinary build behavior. **Require source correction to a separate \`#endif\` line and standalone \`flushDeferred();\`, plus developer/shipping runtime regression/overflow test.** Not proven GPU lifetime UAF.
- **Coverage gap (must resolve accurately):** \`Application.cpp:2705–2731\` activates filled pipes, directly calls \`FlushDiagnosticsIfIdle\` under in-flight, then **deactivates pipes at line 2727 before \`ShowWindow(SW_MINIMIZE)\`**. These tests cover the gating function, but **not an actual BeginFrame-reuse + minimize/restore sequence while pipes remain blocked**. Require honest acceptance scope or targeted interactive test that observes real BeginFrame and window cycle under blocked sinks with proper bounded watchdog.
- **Visual and transition gates:** independent R4-R1 vs unchanged same-session R2 PNG **16/16 exact**; vs actual historical R2 01C-A-R1 static PNG **4/16 exact** (12 images, 188 changed decoded RGBA channel values max 1 LSB, rain_sf 4/4 exact). Historical gate **NOT MET** until explicit Tech Lead deviation/restoration. Transition shader+GBV investigation used **unchanged R4 developer EXE**, not R4-R1: two author logs report frame completion ~770.36/771.83s, zero debug errors/warnings in logs; process then fails to terminate before 900/1200s during live-object/shutdown activity. **Whole-run transition+GBV PASS NOT ESTABLISHED; source of stall not independently proven**. 72 min/frame is an **extrapolation** from actual steps=128, shadow=6 (requested steps=8 did not apply), not a measured production benchmark. Standard 5/5 Debug Layer/GBV are author logs, transition under Debug Layer only, no independent GPU replay.
- **Next:** independent Codex STRICT READ-ONLY review of exact R4-R1 ZIP, explicitly evaluating the preprocessor source finding and test-coverage gap, plus GPU no-block fence semantics, pipe fd/HANDLE ownership, shader+GBV shutdown and source-to-EXE provenance. If confirmed, request **only a narrow follow-up correction** (do not auto-start R5 or broaden HLSL). Final Tech Lead 01C-B acceptance **HOLD**, 01C-C NOT RUN, V2 formal NOT ACCEPTED, lightning/surge/game production BLOCKED.

##### V-WEATHER-01C-B-R4-R1 — Independent Codex STRICT READ-ONLY review + Tech Lead decision (2026-11-21; REQUEST CHANGES 4×P2 + 1×P3 / R4-R2 REQUIRED)

- **Scope:** Independent Codex read the exact R4-R1 ZIP SHA256 \`8c066d84db4195d801582b2b9be6196235ee03a632140cc9541628e8be11635b\`, checked CRC, 1050/1050 payload SHA, source 136/136, HLSL 34/34, 5 Windows EXEs, patch \`d101e9b4eff98ac586a0dd77d7fc9c359fb4bf2be47d2f07b1927a4140ccc7d1\` byte-for-byte against rebuilt R4, and all 58 reported raw cases. **REQUEST CHANGES**. Tech Lead independently reopened ZIP and confirmed the cited C++/Python control-flow paths. No independent Windows/MSVC/EXE/GPU/GBV replay, no source→binary build proof.
- **P2-1 — unsafe synchronous diagnostics outside the new renderer gate:** \`Application.cpp:3145–3155\` interactive P/C can invoke normal logger after Present while latest GPU fence pending; \`Application.cpp:2806–2811\` \`RenderOneFrame()\` calls \`CloudProfiler::Collect()\`, and \`CloudProfiler.cpp:135–137,170–174\` logs Map errors and periodic GPU timestamps after per-buffer idle but not necessarily last-submission idle. \`Application.cpp:3495\` transition completion and \`Application.cpp:3822–3845\` \`WriteFrameTiming()\` synchronous file stream/flush also reachable in unsafe window; review all settings/transition/input/resize/exit paths. Not a demonstrated GPU UAF, but violates the no-blocking diagnostic sink before **last** submission completion rule. **R4-R2:** one centrally enforceable diagnostic contract, no per-frame global WaitForGpu that serializes CPU/GPU; profile data collection/metrics should not be silently skipped, only I/O deferred.
- **P2-2 — loop flush incorrectly swallowed by preprocessor:** \`Application.cpp:2733\` literally \`#endif        flushDeferred();\`. Tokens after directive are not an executable C++ statement, so ordinary developer/shipping \`RunInteractive()\` does **not** periodically call \`flushDeferred()\`. Its fixed 16×192 queue silently drops later entries; separate \`pendingPresentNotice_\` has separate overwrite behavior. Fix line break, explicit overflow policy, developer and shipping interactive tests with >16 events and real idle, shutdown and minimize/restore; fault-only \`flushDeferred()\` at line 2668 does not cover this.
- **P2-3 — blocked-pipe in-flight test does not exercise full claimed interactive path:** \`Application.cpp:2681–2731\` does real bursts with pending submissions then activates blocked streams and directly calls \`FlushDiagnosticsIfIdle()\`, but **deactivates streams at line 2727 before real \`ShowWindow(SW_MINIMIZE)\`**. Evidence \`inflight_at_check=2\`, defers, \`diag_unsafe=0\` demonstrates *that direct gate only*, not global sink safety, reuse in \`BeginFrame()\`, or real minimize/restore with pipes full. Harness JSON lacks per-flush \`lastFlushSubmitted/lastFlushCompleted\` although shared memory stores them; \`gate_inserted=0\` in fixture. Add real BeginFrame/3-buffer reuse, input/profiler/transition, minimize/restore/occlusion under blocked stdout and stderr, parent hard watchdog, numeric actual fence snapshots on each defer/flush; no false PASS if test stage not reached.
- **P2-4 — witness-disabled parser accepts invalid pipe activation:** \`reproduction/parse_01c_b_r4_faults.py:260–267,380–407\` \`witness_failure_checks()\` checks exit 1/no witness but does **not** invoke \`pipe_checks(case,3)\`; raw \`blocked_both_witness_failure\` happens to have valid pipe status, but parser could PASS absent/failed activation. Add predicate, mutation/negative tests proving FAIL for missing pipe-status / requested stream not installed.
- **P3 — fault-only stream ownership & rollback:** \`CloudFaultInjection.cpp:197–221,305–345,393–421\` does not check \`_dup\`; temporary fd returned by \`_open_osfhandle\` is not closed after \`_dup2\`; if stdout succeeds but stderr fails, changed stdout is not rolled back because \`g_blockedPipesActive=false\`; restoration status ignores \`_dup2\`/\`SetStdHandle\` failures yet marks deactivated. Correct ownership (including _open_osfhandle transfer) and error-safe partial rollback, verify all return codes, negative cases for failed stderr-after-successful-stdout and restore failures. **Test-only**, no evidence of shipping hook leakage.
- **Retained gains:** \`GpuIdleConfirmed()\` tracks the latest signaled submission and rejects device-removed sentinel, \`BeginFrame()\` still waits its reused buffer fence; direct \`FlushDiagnosticsIfIdle()\` defers with pending latest submissions, and independent stdout+stderr author sensitivity controls time out as expected. \`TerminalFailClosed()\` remains no-throw/no-logger until \`_Exit(1)\`. No new proven premature COM Release in checked paths. These localized source gains **do not** justify full 01C-B acceptance.
- **Separate hard gates:** exact R4-R1 vs current-session unchanged R2 **16/16 PNG exact**, versus historic baseline **4/16 exact**, 12 frames /188 changed RGBA channels / max 1 LSB / \`rain_sf\` 4/4 exact; **historical gate NOT MET**, waiver/restoration decision pending. Transition HLSL + GBV tested using unchanged **R4** developer EXE rather than R4-R1: author reports frames ~770–772s/zero errors then forced timeout during/after live-object reporting; no normal whole-process exit, exact stall cause unproven. **Transition+GBV NOT RUN / full acceptance unresolved**, require successful run or explicit Tech Lead risk-scoped gate disposition. No performance PASS and no 01C-C.
- **Next:** **R4-R2** narrow correction, source+full patch SHA and reproducible developer/shipping/fault build/run, independent validation of all newly exposed sinks and fault/parser failures, visual regression and GBV honest status. DeepSeek no commit/push; Codex STRICT READ-ONLY; Tech Lead final safety, historical visual and transition GBV acceptance **separate**. V2/V3 formal NOT ACCEPTED; lightning V-L, Surge V-S1 and game V-Q4 remain gated.

**Release-грань подтверждена как требование, не как готовность игровой сборки:** обычный игрок может менять **качество облаков и поддерживаемый render mode**; вручную задавать погоду, длительность transitions, cloud density/seed и debug CLI/hotkeys — **не может**. Действительную погоду для игры задаёт `CEnvironment`. Проверка standalone shipping-controls-OFF **не заменяет** проверку игрового Release.

**V-Q3 / V-Q4 готовность:** cloud pass/extraction и contract к будущему renderer подготовлены, но **in-game composite BLOCKED** до появления у F4.0 **game-owned D3D12 device/queue/frame**, корректной игровой камеры, доступного HDR linear sky/scene target, depth/depth SRV по проверенной convention, descriptors/CB/fences и ресурсного lifecycle. Нынешний `GameDx12Bridge` — диагностический отдельный device/queue и RGBA8 readback, не готовый игровой HDR renderer. Первый opt-in preview с фиксированным Rain может предшествовать полной weather/Classic интеграции; полные V3/V4 требуют больше работы. От готовности требуемых игровых DX12 ресурсов до первого ограниченного кадра с V-SF Rain **ориентир 2–5 рабочих дней при отсутствии новых блокеров, не обещание от текущей даты**.

**В целом:** V-WEATHER standalone функционально продвинут; **V2/V3 NOT ACCEPTED**, полнофункциональная игровая погода и GPU efficiency не приняты. До готовности игрового DX12 продолжаем V-WEATHER-01C-B и квалификацию GPU отдельно от F4.0; DX11/R4 и production не затрагивать без отдельного гейта.

> **Инженерная спецификация из истории проекта — Молнии после принятой системы облаков.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

#### V-L — Lightning in Volumetric Clouds (отложенное расширение)

**Решение 2026-10-23:** планируем красивые грозовые молнии: внутриоблачные
вспышки, разряды между различными облачными массами, впоследствии
облако–земля. Они должны восприниматься частью облачного объёма, освещать
его изнутри и соответствовать S.T.A.L.K.E.R. погоде. Это **не текущее
задание DeepSeek**, не часть V1 PASS и не повод замедлять F4.0.

**Жёсткий гейт перед первым V-L кодом:** как минимум **V1 PASS** и **V2 PASS
(самостоятельный DX12 EXE)** после независимого ревью кода, captures, GPU
timings и ресурсоёмкости. Нужны крупные непрерывные облачные массы, рабочая
камера/параллакс, корректное освещение/плотность, устойчивое изображение
в движении, рабочие погодные пресеты и отсутствие блокирующих ошибок
D3D12 Debug Layer/GBV, resize и resource lifetime. **Несколько видимых
фрагментов облаков не выполняют этот гейт.** Если V2 пройден частично,
работы V-L не запускать до отдельного решения о готовности.

| Подэтап | Отложенная задача | Зависимость / критерий |
|---|---|---|
| **V-L0 — lightning design** | Контракт грозовых ячеек/облачной плотности; отдельная visual lightning subsystem; типы intra-cloud, inter-cloud, cloud-to-ground; seed, частота, визуальные ограничения и GPU budget. | Только после V1 + V2 PASS; согласованный ADR, никакой обязательной симуляции реального электрического поля. |
| **V-L1 — intra-cloud flashes** | Кратковременные объёмные вспышки и светорассеяние внутри грозовых облаков; без геометрии разряда и без игровой сцены. | После V-L0; standalone captures, стабильность, reproducible seed, GPU ms и безопасные flash limits. |
| **V-L2 — inter-cloud discharges** | Процедурные ветвящиеся каналы между **пространственно различными** плотными облачными областями; подсветка обоих объёмов, а не случайные линии в пустом небе. | После V-L1; привязка к пространственной структуре плотности, камера и движение, performance/visual tests. |
| **V-L3 — world lightning & thunder** | Опционально: разряды облако–земля, короткое освещение геометрии, туман/осадки, согласование с существующим X-Ray thunder/weather timing; безопасный fallback. | Только после наличия необходимых **V3–V5 и F4/F5** игровых проходов; DX11 и Classic 2D не ломать. |

**Единый источник погодных событий:** `CEnvironment` / существующая система
X-Ray задаёт погодный цикл, ветер, грозу, дождь и игровые события. Cloud
renderer отвечает за форму/освещение облаков, V-L — за **визуальное**
проявление разрядов; не создавать параллельный игровой weather manager.
Подключение к реальным циклам — после V3–V5. Для Classic 2D сохраняется
исходное погодное поведение; Volumetric 3D + lightning — **опционально**.

**Названия:** `V-L0…V-L3` — внутренние подэтапы V, не путать с занятым
самостоятельным направлением `L — DX12/DXR portability / IX-Ray`.

**Актуализация 2026-11-21 (старое предложение ниже было приоритетом 2026-10-23):** standalone ray marching и weather runtime теперь имеют гораздо больше исходников, captures и safety evidence, чем первоначальные видимые V1-фрагменты, **но формальные независимые V1 PASS и V2 PASS всё ещё отсутствуют**. V2.3h-A03b-R02 BLOCKED, R03A READ-ONLY только запланирован. Поэтому жёсткий gate V-L0–V-L2 остаётся прежним: **не начинать реализацию молний до V1+V2 PASS**; после их принятия можно делать V-L1/V-L2 в standalone **до V3 game integration**. Ближайший статус/порядок см. в V-PLAN.

> **Инженерная спецификация из истории проекта — Weather Cycle и Surge визуальные подсистемы.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

#### V-S — Weather Cycle & Surge Visual System (отложенное расширение)

**Решение 2026-10-23:** выброс — важный самостоятельный аномальный
визуальный сценарий S.T.A.L.K.E.R. В будущем используем **ту же технологию
Volumetric 3D + V-L молний** для красно-багрового неба, массивного
аномального облачного ядра, внутренних вспышек и межоблачных разрядов,
согласованных с оригинальными звуком, пси-эффектами и игровыми стадиями.

**Полный контракт, источники кода, подробная матрица обычных погодных
циклов и фаз выброса, приёмочные тесты:** [X-Ray Weather & Surge Visual Contract](dx12-v-weather-surge-visual-contract.md).

**Важно не смешивать:**

- **`CEnvironment::WeatherCycles`** — *обычные* погодные циклы: ясно,
  переменная облачность, пасмурно, дождь, ветер, туман, гроза, смена
  дня/ночи и другие фактически загруженные `.ltx`. Это категории
  визуального отображения, **не выдуманный список имён циклов**. Берём
  интерполированные `CurrentEnv` (sun/sky/clouds/wind/rain/fog) и
  отображаем **все реальные циклы**, включая дополнительные модовые,
  через собственный DX12 cloud pass.
- **`CEnvironment::WeatherFXs`** — временные FX, которые могут подменить
  погодные параметры. Наличие WFX не означает автоматический выброс.
- **Игровой `surge_manager.script`** — источник авторитетных событий
  выброса: начало, предупреждение, пик, завершение и игровая логика
  (фактические названия/тайминги ещё требуют изучения конкретной
  геймдаты). **Не путать с C++ `CALifeSurgeManager`**, который обслуживает
  ALife spawning.

**Как рисуем самостоятельно (проект):**
в обычную погоду — 3D-морфология облаков, солнечное рассеяние, самозатенение,
world-space дрейф от ветра, масштабируемая плотность и покрытие; при
выбросе — гладкий переход к багровой палитре **на основе исходных
`CurrentEnv`**, объёмные грозовые фронты, локальное внутреннее
аномальное свечение и V-L межоблачные молнии. Возвращение к погоде
происходит по сигналам исходной игровой системы, без дополнительного
управления погодой со стороны рендерера.

**Визуальные фазы выброса (проект, не названия игровых скриптов):**
S0 обычное небо → S1 предвестники → S2 нарастание/покраснение →
S3 активный выброс/облачное ядро → S4 затухание → S5 восстановление
обычной погоды. Длительность, реальные WFX IDs, момент предупреждения,
пику и завершения **не придумывать**: их нужно считать из
игрового `surge_manager.script` и актуальных погодных `.ltx`.

| Подэтап | Что делаем | Условие запуска / приёмка |
|---|---|---|
| **V-S0 — source inventory** | Собрать фактическую карту `WeatherCycles`/`WeatherFXs`, Lua-событий выброса, фаз, погодных `.ltx`, звука, постэффектов и camera FX из установленной геймдаты. | Исследование только при наличии файлов; не влияет на текущий V1. |
| **V-S1 — standalone surge sky** | Пресеты погодных циклов, красная объёмная атмосфера, облачное ядро, плавная смена визуальных фаз в DX12 standalone. | **V1 PASS + V2 PASS** и независимые baseline/captures/тайминги. |
| **V-S2 — surge lightning** | Переиспользование **V-L1/V-L2** для электрических разрядов между облаками, объёмной подсветки и аномального свечения. | V-S1 и принятые V-L этапы, без двойного legacy-освещения. |
| **V-S3 — in-game surge adapter** | Связь с настоящими Lua-событиями и `CEnvironment`, sky/depth/HDR/fog/scene lighting, сохранение исходных игровых событий и Classic 2D. | Соответствующие V3–V5/F4/F5 игровые проходы + независимые игровые тесты. |

**Актуализация 2026-11-21:** историческое ограничение «не начинать V-L/V-S1 до V1+V2 PASS» остаётся действующим. Описание лишь небольших фрагментов V1 относится к 2026-10-23 и **не отражает** поставки V-WEATHER-01C-B-R4; тем не менее **V1/V2 официально NOT ACCEPTED**, Storm визуально не принят, а game integration отсутствует. **V-S0 source inventory** разрешается как отдельная READ-ONLY исследовательская задача при наличии фактической геймдаты и без runtime-кода; V-S1/S2 и lightning остаются отложенными. Не изменять игровые surge/weather события до отдельного game adapter gate, см. V-PLAN.

**Game-data gap:** в доступной копии репозитория отсутствует полный
актуальный `surge_manager.script` и соответствующие
`weather_effects/*.ltx` для используемой игры. Названия
`fx_surge_day_3`, `fx_blowout_day` и т.п. встречаются во внешних
CoP/модовых материалах, **не подтверждены в нашей установленной
геймдате**. До V-S0 не объявлять полный перечень/тайминг погодных циклов
установленным.

**Главное правило:** *одна оригинальная погода X-Ray → два отображения
(Classic 2D по умолчанию / Volumetric 3D опционально)*.
Новый Surge Visual Adapter — только потребитель игровых данных, не второй
игровой weather/surge manager.

---


## 9. PhysX ragdoll — основной ODE и opt-in физика

PhysX source integration действительно находится в `prypiat` после [PR #1](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/pull/1). Это не готовый default-on physics replacement: ODE движение/AI и fallback сохраняются. Управляемый 16-live human ragdoll тест имеет узкий PASS WITH NOTES, обычная сохранённая ALife сцена, loot, rig variety и perf всё ещё **FULL G5.1 HOLD**.

### G5.1-S3R-R2 — конкретная текущая работа

| Направление | Действие | Результат |
|---|---|---|
| ALife lifetime | Источник post-load `GE_DESTROY`, corpse ID, одинаковый save PhysX off/on, loot | Event producer и отсутствие потери corpse/inventory |
| Long-frame | Синхронные CPU/worker/GPU traces, dropped ticks и physics step p95/p99 | Причина stalls, не предположение |
| Static collision | Dense/empty/boundary, cache/cooking, unload/revisit | Контакты и bounded memory |
| Review | Targeted independent проверка только нерешённых задач | Решение по исправлению, без повторного широкого 16 NPC теста |

**R2 NOT RUN.** [Подробное ТЗ](p3.3g5.1-s3r-r2-targeted-followup.md); переносимые физические rig и массы NPC остаются отдельными задачами.

> **Инженерная спецификация из истории проекта — Автоматическая адаптация PhysX к реальным skeleton metadata.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

#### P-S — адаптация PhysX к скелету модели (решение 2026-10-25; PLANNED)

**Требование:** PhysX ragdoll должен строиться по фактической иерархии костей и физическим метаданным загруженной модели X-Ray, а не по жёстко зашитому списку костей одного сталкера. Существующие `CCharacterPhysicsSupport::CreateSkeleton/CreateShell`, `PHGeometryOwner`, `PHElement` использовать как исходный материал для аудита и переноса данных. **Это требование к будущему адаптеру, не подтверждение уже работающего PhysX auto-rig.**

- Считывать bone hierarchy, parent-child, bind/current transforms, доступные collision shapes, массы, joint types/limits; проверять валидность и единицы измерения. Дополнительные визуальные/декоративные кости не обязаны становиться отдельными rigid bodies.
- Строить PhysicsRig из пригодных физических костей, сохраняя animation-only кости; для моделей без достаточных физических метаданных требовать явный профиль/маппинг либо безопасный ODE fallback. **Не обещать** корректный ragdoll для произвольной модели только из-за наличия костей.
- Поддерживать замену модели и новые качественные скелеты без правки C++ для каждого заранее известного bone name **при совместимых данных и валидном профиле**; изменение топологии, дополнительные конечности, IK и joints требуют отдельных проверок.
- Учитывать отдельные rigs мутантов после NPC ragdoll P2/P3; не считать humanoid rig универсальным. Контролировать animation→PhysX pose/velocity handoff, распределение масс, коллизии, lifetime, save/load и отсутствие двойной симуляции.
- Гейты: P-S0 read-only аудит текущего model-driven ODE shell; P-S1 один реальный NPC с auto-built PhysX shell по его физическим данным (совместно с P2); P-S2 замена на совместимую модель с иной детализацией костей; P-S3 разные скелеты/мутанты только после стабилизации P3 и профильных тестов. Classic ODE fallback и DX11 остаются обязательными.

**Приёмка:** тестовый набор минимум из стандартной модели NPC, совместимой модели с дополнительными animation-only костями, модели с изменённой физической иерархией и модели с неполными данными. Для каждого случая записать результат (автосборка / нужен профиль / fallback), логи причин отказа, визуальные и collision evidence, регрессии DX11/DX12. **PLANNED / NOT IMPLEMENTED.**

**Переносимость с первого дня:** `PhysicsCore` и `PhysicsPhysX` не зависят от OGSR, IX-Ray, ODE и DirectX. `PhysicsAdapterOGSR` подключает данные костей/коллизий/игровой цикл/анимацию OGSR; после доказанного игрового результата создаётся `PhysicsAdapterIXRay` для того же ядра. Это планируемая вторая интеграция, не заявленная готовая совместимость. Никакого согласованного сотрудничества с IX-Ray Team пока нет.

**Сохранность:** ODE остаётся стандартным backend и fallback; ни одна физическая оболочка не симулируется одновременно ODE и PhysX. Для первого ragdoll PhysX получает статический collision mesh уровня, без обещания полноценных контактов с динамическими ODE-объектами. Не менять глобальные `CPHWorld`/`CPHIsland`, не ломать DX11 и не блокировать DX12 F4.0. Специальные пути save/load, сеть, анимационные переходы, визуальные кости, удаление объектов и смена уровня требуют тестов до production-включения.

| Подэтап | Реализация | Гейт |
|---|---|---|
| **P0 — аудит ragdoll и baseline ODE** | Проверить `CCharacterPhysicsSupport::CreateSkeleton/CreateShell/EndActivateFreeShell`, `CPHShell`, `CPHJoint`, игровые shape/joint data, save/load и pose callbacks. Pin PhysX SDK/лицензии и определить contract. | Статический аудит с путями/функциями и воспроизводимые ODE тесты; без массового рефакторинга. |
| **P1 — переносимый CPU ragdoll harness** | Собрать минимальное `PhysicsCore/PhysicsPhysX` вне OGSR, поднять физический скелет, суставы и столкновение со статической поверхностью. Сравнить reduced-coordinate articulation и D6 joints для X-Ray rig. | Стабильные pose/velocities, лимиты суставов, fixed-step, init/shutdown и lifetime; тестовый ящик не заменяет этот гейт. |
| **P2 — первый PhysX ragdoll в OGSR DX11** | Опционально переключить одного сталкера с анимации на PhysX shell, скопировать начальную позу/скорость и вернуть bone transforms в OGSR; импортировать статический collision mesh уровня. | Воспроизводимое видео/лог реального DX11 runtime; ODE fallback, отсутствие двойной симуляции, отсутствие крашей. |
| **P3 — стабилизация игрового ragdoll** | Настройка масс, суставов, контактов, коллизионных фильтров, корректная очистка, load/reload, save/load и производительность. | Независимая проверка кода и матрица тестов, обнаруженные ограничения записаны; режим off работает как прежде. |
| **P-C0 — аудит существующей физики мутантов** | После подтверждённого P2/P3 NPC ragdoll изучить фактические CBaseMonster, movement/animation controllers, ODE character support, IKLimbsController, видовые анимации, конфигурации и игровые модели. Определить, где реальное проскальзывание лап, ошибки опоры, рельефа и инерции, а где намеренная неестественная пластика. | Матрица по видам с исходными анимациями, видео baseline, кодовыми ссылками и списком подтверждённых дефектов; ничего не переписывать с нуля. |
| **P-C1 — аккуратное улучшение опор и сцепления** | Расширить существующие IK/ODE/animation механизмы только где они применимы: постановка лап, контакт с грунтом, склоны, повороты, синхронизация скорости и анимации. Сохранить оригинальные походки, атаки, ритм и лор каждого вида; начать с кабана, затем проверить слепых псов. | Сравнение Classic/Enhanced в одинаковых сценах; нет проскальзывания, неверных опор, повреждения AI-path, attack timing и hitboxes; feature flag и rollback. |
| **P-C2 — видовая инерция и реакции** | Настроить разные профили массы, баланса, контактов, прыжков и приземлений без универсальной процедурной походки. Физика поддерживает авторские анимации, не заменяет их. | Игровые тесты на рельефе, препятствиях и в бою для кабана, пса, кровососа, снорка, псевдогиганта; измеримые улучшения без потери характерной пластики. |
| **P-C3 — PhysX ragdoll мутантов (позже)** | После стабилизации NPC ragdoll и P-C0–P-C2 адаптировать PhysX shell для подходящих скелетов мутантов; отдельно проверить кости, суставы, массы, контакты и ODE fallback. | Реальный игровой runtime-test по каждому rig, корректный animation→ragdoll handoff, отсутствие двойной симуляции и регрессий DX11/DX12. |
| **P4 — перенос ragdoll в IX-Ray** | Изучить фактический IX-Ray, создать отдельный adapter и собрать ту же версию portable core. | Отдельный build и реальный IX-Ray runtime-test; лишь после них portability PASS. |
| **P5 — расширение ragdoll** | Другие скелеты человека, потом мутанты и особые оболочки; адресные контакты с legacy объектами. | Матрица игровых/визуальных регрессий для каждого rig. |
| **P6 — перенос physics character movement (позже)** | Игрок, затем гибридные NPC, прыжки/лестницы/приседания/склоны. | Независимые gameplay/AI baseline tests и fallback. |


**После NPC ragdoll — направление P-C: Enhanced Creature Physics (PLANNED).** Улучшать уже существующие механизмы X-Ray, а не заменять оригинальную анимацию мутантов новой физической походкой. Сохранять авторскую неестественность, устрашающий характер и лор. Начинать **после игрового NPC ragdoll P2 и его стабилизации P3**; P-C не блокирует DX12 F4.0 и не подменяет текущие P0–P6.

**Первый сквозной критерий успеха:** один физический скелет NPC в OGSR DX11, созданный из исходной модели и игровой позы, симулируется PhysX и правильно отображается в мире; весь остальной gameplay остаётся на ODE. Производственные гарантии P3 не объявлять выполненными после P2.

**Ссылка:** [Portable PhysX ADR](physics-portability-adr.md). **Статус P (2026-11-11):** G5.0 owner-approved PhysX source-only PR #1 MERGED; unchanged engine tree `61f6403ae8997121085c8a602bb11117822ccb7a`. S1 one and S2 two real human PhysX ragdolls + third ODE fallback PASS WITH NOTES. **S2R1 apparent live-2 disconnect hang traced to a test-only harness early-return before `phaseDisconnectWait`; S2R2 independent PASS WITH NOTES**, 0/1/2 live & release/off 2×2 queued level transitions in source-equivalent Windows logs, lease0, no production fix. Final feature-on PDB and binary independently matched; six raw perf CSV independently recomputed, ~30 FPS pacing and true uncapped production perf unverified. **S3R 16-live controlled-gate independent Codex PASS WITH NOTES / TECH LEAD ACCEPTED WITH NOTES; full G5.1 REQUEST CHANGES/HOLD.** [Codex audit](p3.3g5.1-s3r-codex-independent-review.md), [Tech Lead decision](p3.3g5.1-s3r-techlead-decision.md), [targeted R1 follow-up](p3.3g5.1-s3r-r1-targeted-evidence.md). Three P1s: wrong savecheck EXE/no root stack, 178 dropped ticks and 60 raw intervals >50ms, ordinary ALife geometry cap fallback 10/11. PhysX stays default OFF; no production merge/PhysX locomotion/mutants/new 32/40 stress authorized. **R1 (2026-11-11) author targeted evidence delivered, Tech Lead ZIP/preimage/perf/static-matrix pre-reviewed with notes; independent Codex R1 review PENDING.** [R1 detailed tech lead audit](p3.3g5.1-s3r-r1-techlead-prereview.md). Static budget 3-constant proposal remains unmerged/default OFF; regular ALife corpse lifecycle and optimized long-play performance still HOLD. **R1 now independently reviewed by Codex: A REQUEST CHANGES (local invalid graph crash mechanism PROVEN; corpse GE_DESTROY sender unknown); B REQUEST CHANGES (3+3 raw data, 8 drops, long frame source unproven); C NARROW PASS WITH NOTES (11/11 one normal ALife scene using test-only budgets).** [Review](p3.3g5.1-s3r-r1-codex-independent-review.md), [Tech Lead decision](p3.3g5.1-s3r-r1-techlead-decision.md), [R2 focused follow-up](p3.3g5.1-s3r-r2-targeted-followup.md). R1 overall REQUEST CHANGES; 3-constant patch, G5.1 default-on and production rollout HOLD; R2 test-only authorized/not run.

> **Инженерная спецификация из истории проекта — Масса/экипировка и независимая мобильность NPC.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

#### P-M — Индивидуальная масса NPC и подвижность экипировки (решение 2026-10-25; PLANNED)

**Принцип:** *масса определяет реакцию персонажа на физические воздействия, а экипировка и её механизмы — насколько хорошо он может эту массу перемещать*. Не связывать скорость ходьбы напрямую с общей физической массой.

- **Индивидуальная масса тела NPC:** при создании сталкера назначать устойчивое значение в ориентировочном диапазоне **65–80 кг** (проектный пример; уточнять по балансу). Не выбирать случайную массу заново каждый кадр или при загрузке сохранения.
- **Экипировка:** физическая масса = масса тела + физически учитываемая масса надетой брони/снаряжения. Пересчитывать при экипировке/снятии; согласовать с inventory, исключить двойной учёт. Массы костюмов, груз и конкретные игровые коэффициенты определить после проверки баланса.
- **Раздельные параметры:** `physical_mass` отвечает за инерцию, контакты, внешние импульсы и ragdoll; `mobility_profile` — за желаемую скорость, ускорение, торможение и манёвренность. Сама большая масса **не обязана** снижать скорость NPC.
- **Обычная тяжёлая броня:** может *слегка* замедлять ходьбу/ускорение и менять манёвренность как отдельный настраиваемый игровой эффект.
- **Исправный экзоскелет:** собственная масса **увеличивает физическую инерцию**, но приводы **компенсируют нагрузку при передвижении**: обычная скорость ходьбы **не ниже** базовой скорости того же NPC без брони. Ускорение/стабилизацию настраивать отдельно. Износ приводов — необязательное позднее расширение.
- **Толчки и отдача:** при одинаковом реально приложенном импульсе тяжёлый NPC получает меньшее изменение скорости (`Δv = J / m`). Раздельно исследовать удар по живому NPC, его анимационную реакцию, отдачу собственного оружия и расчёт урона. **Не уменьшать урон автоматически пропорционально массе** и не переводить живых NPC безусловно в ragdoll.
- **Ragdoll:** суммарная масса тел скелета согласуется с массой персонажа и экипировкой; отдельно настраиваются массы костей, моменты инерции и суставы. Не симулировать одну оболочку одновременно ODE и PhysX.
- **Совместимость:** по умолчанию сохранять исходное поведение при выключенном feature flag. Проверять AI-навигацию, лестницы, прыжки, приседание, анимации, save/load и сеть; не затрагивать DX11/R4 и DX12 renderer ради механики.

| Подэтап | Реализация | Гейт |
|---|---|---|
| **P-M0 — ODE mass/movement baseline (READ-ONLY)** | Аудит `CPHMovementControl`, `CPHSimpleCharacter`, гибридного AI movement, контактов/импульсов, `CCharacterPhysicsSupport`, shell и конфигураций брони. Установить влияние массы на силу, торможение, hit и состояния. | Карта зависимостей и воспроизводимые измерения исходной ODE; не считать hit reaction NPC реализованной без проверки. |
| **P-M1 — opt-in профиль массы на ODE** | Устойчивый `CharacterMassProfile` (рабочее название): индивидуальная масса, экипировка, отдельная мобильность/компенсация; один тестовый NPC без изменения остальных. | Скорость и управляемость там, где требуется, сохранены; проверены смена брони, save/load и AI. |
| **P-M2 — контролируемые физические реакции** | Проверить/реализовать передачу внешних импульсов живым NPC без смешивания импульса, анимации, урона и ragdoll; сравнить лёгкую/тяжёлую броню и экзоскелет. | Измеряемая разница реакции при одинаковом импульсе, корректные контакты, отсутствие непреднамеренных рывков/падений. |
| **P-M3 — согласование с PhysX ragdoll** | Передавать профиль при переходе к PhysX shell; проверять распределение массы по костям, инерцию, импульсы и pose/velocity handoff. | Реальный NPC в OGSR DX11, корректная масса ragdoll, ODE baseline и fallback; после необходимых гейтов P2/P3. |

**Тест-матрица:** базовый NPC ~70 кг и тяжёлый NPC/экзоскелет ~110–120 кг при одинаковом импульсе; базовая ходьба/ускорение без брони vs исправный экзоскелет (скорость ходьбы не снижается); отдельно обычная тяжёлая броня (умеренный штраф); рельеф/лестницы/AI path, экипировка в runtime, сохранение/загрузка, animation → ragdoll. Значения масс — **тестовые примеры**, не утверждённый баланс. Измерять fixed timestep и фактическую передачу импульсов.

**Порядок и статус:** P-M0 можно исследовать параллельно как независимый ODE baseline; P-M1/P-M2 — только после отдельных тестов; P-M3 не подменяет первый P2 ragdoll. **PLANNED / NOT IMPLEMENTED.** Не блокировать F4.0, DX11/R4 и исходные P0–P6.


## 10. Оптимизация больших миров и архитектура будущего

Отдельные дальние направления не блокируют текущие F4.1/E и F4.2. Прежде чем менять subsystem, измерять реальный bottleneck и сохранять R4 fallback.

> **Инженерная спецификация из истории проекта — Общая многопоточность всего движка.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

### Этап H — Многопоточность и производительность всего X-Ray (после RT-теней и отражений)

**Цель:** после работающих **G3 RT-теней** и **G5 RT-отражений**
проверить, насколько эффективно OGSR использует многоядерный CPU и
готовит кадры для DX12/DXR. Оптимизировать реальные узкие места, а не
распараллеливать код по предположениям. **DX11 baseline сохранить.**

**Различие между F3.5 / F6 / H:** F3.5 заранее определяет безопасные границы
многопоточного **CPU-side DX12 renderer** и испытывает минимальный parallel
recording prototype **до F4**. F6 стабилизирует и оптимизирует готовый игровой
рендер по измерениям. H — **сквозной аудит simulation/A-Life/physics/AI и
игрового движка** после затратных RT-эффектов; не откладывает фундамент F3.5.

| Подэтап | Работа | Гейт / проверяемый результат |
|---|---|---|
| **H0 — исходные профили** | Ещё до DXR сохранить воспроизводимые CPU/GPU frametime, 1% low, загрузку потоков, draw calls и накладные расходы синхронизации в одинаковых сценах/настройках DX11 и DX12. Инструменты — Tracy, Visual Studio Profiler, PIX/ETW по применимости. | Есть baseline, методика и конфигурация замеров, чтобы отделять регрессии DXR от старых ограничений X-Ray. |
| **H1 — аудит многопоточности** | **После G3 и подтверждённых RT-отражений G5** исследовать реальный frame loop, main/render threads, A-Life/AI, физику, анимации, culling, очереди рендера, загрузку ресурсов, создание PSO, worker/job systems, mutex/lock contention, ожидания CPU↔GPU и распределение задач по ядрам. Сначала составить карту существующих потоков и зависимостей, затем снять профили тяжёлых игровых сцен. | Markdown-отчёт с картой потоков, горячими участками, величиной блокировок и перечнем приоритетных проблем **с измеренным вкладом в frametime**; никаких архитектурных переписываний на стадии аудита. |
| **H2 — адресная оптимизация** | По результатам H1 выносить независимые задачи simulation/A-Life/AI/physics в workers, уменьшать блокировки и main-thread stalls, улучшать сквозной streaming и связку игры с рендером. Развивать, но не изобретать заново, F3.5/F6 parallel D3D12 recording; дальнейшее масштабирование только по измеренной выгоде. | Для каждой правки — изолированный diff, до/после CPU ms, GPU ms, frametime/1% low, отсутствие новых race/deadlock, validation errors и потери корректности игрового поведения. |
| **H3 — масштабирование и регрессии** | Проверить несколько сцен (NPC/A-Life, тяжёлая геометрия, RT-тени/отражения), разные нагрузки и число доступных CPU-потоков; сравнить DX11/DX12, проверить стабильность длительных сессий. | Воспроизводимый regression suite и таблица выигрыша/потерь; откат изменений без пользы; DX11 fallback и корректность игровой логики сохранены. |

**Правила H:**
- H1 начинается **после рабочих RT-теней и RT-отражений**, а не после одного
  DX12 Clear/Present. **Это не запрещает ранний F3.5 — архитектуру и
  тестирование многопоточного DX12 до F4.** RTGI и прочие тяжёлые эффекты
  можно планировать отдельно по итогам замеров.
- Низкая загрузка GPU сама по себе не доказывает bottleneck главного потока:
  сопоставлять CPU/GPU timeline, очереди, ожидания и профиль конкретной сцены.
- Не заменять целиком scheduler, физику или AI, не вводить «многопоточность
  везде» и не переключать DX11 на новый backend ради оптимизации.
- Все изменения вносить поэтапно: **профиль → гипотеза → минимальная правка
  → измерения → регрессия → решение оставить/откатить**.

**Статус:** ⬜ **NOT STARTED**. H0 — запланирован как ранний baseline;
H1–H3 — после указанных RT-рубежей. Реализованных оптимизаций этапа H пока нет.



> **Инженерная спецификация из истории проекта — World Partition и асинхронная загрузка.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

### Этап I — World Partition и асинхронный World Streaming (план; не реализован)

**Цель:** по мере движения игрока подгружать и освобождать GPU/CPU-ресурсы секций мира без остановки кадра, сохраняя совместимость с классическими переходами между уровнями. **Это не тождественно frustum culling:** объект может не рисоваться, но оставаться резидентным; игровой AI/A-Life не должен автоматически выгружаться вместе с его рендер-представлением.

| Подэтап | Работа | Гейт / проверяемый результат |
|---|---|---|
| **I0 — аудит и метрики** | Исследовать существующие OGSR level geometry, details/trees, visual/LOD formats, spatial DB, lifetime и владение ресурсами, загрузчик уровней, Game/A-Life. Снять RAM/VRAM/frametime и причины блокировок в нескольких сценах; установить интерфейс секций без допущения о текущем формате данных. | Документ: что уже делится на части, что загружается монолитно, где безопасна выгрузка, матрица рисков. Никакого рефакторинга в I0. |
| **I1 — пространственные секции** | Прототип manifest/index с AABB/иерархическими ячейками и ссылками на ресурсы; поддержать неравномерное деление для леса, зданий, подземелий; reference counting общих ресурсов. | Дебаг-оверлей границ и список ресурсов секции; повторяемые тесты загрузки/выгрузки небольшого тестового участка. |
| **I2 — async IO / upload / eviction** | Рабочие очереди чтения/декомпрессии, CPU staging, D3D12 upload queues/fences, budgets, residency, безопасный deferred destruction после GPU fences; приоритет ближайших/ожидаемых секций, контроль thrashing. | Нет заметных main-thread IO stalls, use-after-free, GPU validation errors; RAM/VRAM и worst-frame измерены до/после. |
| **I3 — predictive streaming** | Приоритет от 3D camera position, frustum, скорости/направления и предполагаемого маршрута; hysteresis, предзагрузка на поворотах/при подъёме и запасные LOD при cache misses. | Тестовые маршруты с резкими поворотами и подъёмами, статистика late loads и видимых pop-in; отсутствие жёстко фиксированного радиуса в качестве единственного критерия. |
| **I4 — compatibility / stress** | Сохранение старого режима загрузки уровней, дебаг-инструменты, memory pressure, частые перемещения, alt-tab/resize, cold/warm cache, DX11 fallback. | Откат одной настройкой/сборкой; длительная сессия без утечек и повреждений ресурса, критерии измеряются на фиксированных маршрутах. |

**Зависимости:** I0 можно исследовать параллельно F3–F4; GPU-resident streaming внедрять после F4 и базовой синхронизации F6 (с использованием правил ownership/snapshot из F3.5). Не делать streaming обязательным блокером G0–G3. Не обещать абсолютного отсутствия stalls до замеров и настройки бюджета.



> **Инженерная спецификация из истории проекта — Visibility/LOD/растительность/тени.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

### Этап J — Height-Aware Visibility, LOD, растительность и тени (план; не реализован)

**Цель:** устранить заметное «появление в пяти метрах» травы, деревьев и теней без безусловного увеличения дальности рисования. J использует менеджер секций I, но **видимость, уровень детализации, резидентность и тенеобразование остаются разными решениями**.

| Подэтап | Работа | Гейт / проверяемый результат |
|---|---|---|
| **J0 — инвентаризация** | Проверить реальные форматы и механизмы progressive/static LOD, деревьев, detail objects, foliage shaders, CSM/sun, существующие shadow LOD и impostors непосредственно по исходникам и ассетам текущей ветки. | Матрица «существует / нуждается в конвертере / отсутствует»; никаких предположений, что все модели уже имеют пригодный LOD. |
| **J1 — система высот** | Определить единый vertical datum: условное **0 м относительно согласованного уровня моря** как метаданные мира; per-level vertical offset из локального X-Ray Y (ось проверить в коде), без перемещения старой геометрии. Хранить altitude камеры, AGL (над поверхностью), 3D camera pose, FOV, скорость. Геодезическую привязку к реальному уровню моря делать только при наличии достоверных исходных данных и datum. | Два тестовых уровня с разными локальными нулями дают непрерывную высоту, корректны на крыше/холме/в подвале; старые уровни не ломаются при отсутствии метаданных. |
| **J2 — visibility и screen-space LOD** | Выбирать LOD по projected screen-space error/размеру, применить hysteresis, dither/cross-fade и стабильные temporal переходы; окклюзию и дальний силуэт считать отдельно; поддержать HLOD/impostors для горизонта, terrain tiles для дальнего рельефа. | Одинаковые маршруты у земли и на высокой точке: дальние силуэты сохраняются, в движении нет заметного popping/мерцания, LOD переключается стабильно. |
| **J3 — vegetation streaming** | Секции деревьев/detail grass, плотность и анимация по бюджетам, density LOD и HLOD леса, предзагрузка растительности до входа в кадр; proxy fallback при запоздавшем детальном ассете. | Нет очевидной растительности, появляющейся непосредственно перед игроком; измерены density, pop events, GPU ms, RAM/VRAM на лесной сцене. |
| **J4 — shadow coverage** | Согласовать растительный LOD и caster LOD с cascaded shadow maps; **shadow casters включать также вне frustum камеры**, если они отбрасывают тени в видимую область. Стабилизация cascades, blend/hysteresis, distance shadow representations / контактные тени — по измерениям. | На солнце и при ходьбе по лесу нет резкого включения теней у ног; проверены motion/camera height, GPU ms и shimmering. |
| **J5 — DXR integration** | Streaming-aware BLAS/TLAS, выбор RT proxies для растительности, alpha-tested foliage, ограничение RT-caster region по влиянию света и памяти, async rebuild/refit по валидным гейтам G. | Отсутствуют missing RT shadows/invalid TLAS при границах секций, корректный raster fallback, DXR cost измерен. |

**Принцип:** высота над морем — **общая координатная ссылка**, а не магическая формула дальности; обзор определяется геометрией/окклюзией, AGL и проектируемым размером объектов. Для distant shadows не просто расширять CSM без бюджета: допустимы HLOD/proxy, impostors, baked/distant occlusion по качеству.

**Зависимости:** J0–J1 проектировать после I0, части J2/J4 можно прототипировать после F4 и до полного streaming; J3 требует I1–I3; J5 — после G1–G4 + I2. DX11-сохранность обязательна.



> **Инженерная спецификация из истории проекта — Seamless World, пока концепция.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

### Этап K — Seamless World / экосистема бесшовных модов (долгосрочно, не реализовано)

**Цель:** после внедрения I/J предоставить техническую возможность создавать бесшовные объединённые регионы без экранов загрузки. Это **не отдельный режим рендерера или движка** и не обязательное автоматическое объединение старых уровней. Существующие level transitions и моды продолжают работать; новые проекты смогут использовать streaming для перехода между регионами без загрузочного экрана.

| Подэтап | Работа | Гейт / проверяемый результат |
|---|---|---|
| **K0 — world composition contract** | Global transform/coordinates (при больших размерах camera-relative GPU positions / precision strategy), per-level offsets, границы, порталы и региональные метаданные, отдельная система абсолютной высоты J1. | Технический формат сцепления двух тестовых уровней и правила миграции/версий, без копирования игровых ассетов в репозиторий. |
| **K1 — render-only crossing** | Бесшовный рендер двух тестовых регионов на I/J, не обещая seamless AI; текстуры, геометрия, небосвод и освещение непрерывны. | Проход через границу без загрузочного экрана и заметного скачка геометрии/экспозиции, фиксированные frametime/VRAM. |
| **K2 — gameplay persistence** | Сериализация/переход владения игровыми сущностями, A-Life offline/online, межрегиональные navmesh/AI paths, физика, звуки, задания, сохранение/загрузка, спавн. | NPC/объекты/квесты не дублируются и не пропадают, долговременный переход туда-обратно и save/load воспроизводимы. |
| **K3 — modding tools и compatibility** | Утилита сборки world manifest, валидатор границ/высоты/LOD, диагностические overlay, версия формата; единая архитектура world streaming для обычных и бесшовных локаций без отдельного переключателя «Classic / Seamless». | Малый пример бесшовного мода и документация для моддеров; существующие переходы CoP и старые моды не регрессируют. |

**Не включено автоматически:** объединение всех канонических уровней CoP, seamless A-Life, единая глобальная навигация или сохранность существующих модов при включённом новом режиме. Каждое направление требует самостоятельного теста. Зависит от I и J; K2 не блокирует K1.



> **Инженерная спецификация из истории проекта — Portable DX12/DXR и возможный IX-Ray adapter.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

### Этап L — Переносимый DX12/DXR модуль и возможное сотрудничество с IX-Ray Platform (предложение, не согласовано)

**Цель:** сделать результат полезным вне OGSR. **IX-Ray Platform — потенциальный партнёр, не заявленный участник проекта и не получатель согласованного кода**. Никаких утверждений о сотрудничестве до контакта и согласия команды.

| Подэтап | Работа | Гейт / проверяемый результат |
|---|---|---|
| **L0 — legal / technical feasibility** | Сравнить лицензии, provenance кода/шейдеров, сторонние SDK, актуальные renderer interfaces OGSR и IX-Ray, форматы ассетов, toolchains и API; определить допустимые условия перераспределения. | Compatibility matrix и архитектурное ADR; без предположения, что весь OGSR backend можно просто скопировать. |
| **L1 — backend boundaries** | Минимизировать зависимости D3D12 device/queues/descriptors/PSO/resources/RT path от OGSR game layer, API-specific adapters и ownership; не переписывать уже работающий рендер без доказанной причины. | Изолируемые компоненты, минимальный integration harness, документированная зависимость на игровые типы. |
| **L2 — reproducible technical preview** | После F4/F6 и достаточной зрелости G3 собрать пример, test scenes без лицензируемых игровых данных, CI build-only + отдельные GPU runtime измерения, feature matrix, известные ограничения и DX11 fallback. | Повторяемый preview с проверяемыми RT shadow результатами и реальными GPU timings; DXR не заявлять готовым раньше G. |
| **L3 — outreach / optional integration** | Подготовить техническое предложение IX-Ray Team с краткими ограничениями, ссылкой на репозиторий, bench evidence и возможным небольшим экспериментальным портом, **только после самостоятельного решения о публикации**. | Заинтересованность/обратная связь команды; дальнейшие действия зависят от их решения. |

**Общая стратегия:** OGSR-Engine-rtx сохраняется самостоятельным экспериментальным форком. Не менять лицензии, авторство и attribution без юридической проверки; не заявлять совместимость с IX-Ray до прототипа.

#### Межэтапные приоритеты и ограничения I–L

1. **Ближайший исторически записанный шаг:** F3 — минимальная DX12 геометрия. **После достижения тестовых F3.x — F3.5 до массового F4**; I–L не сдвигают графический путь F/G. Актуальность F3-статуса проверять по коммитам и GPU-логам, а не по этой записи.
2. **До окончания F4:** допустим только исследовательский I0/J0/L0; не смешивать массовое переписывание загрузчика/игрового мира с DX12 bring-up.
3. **После игрового кадра F4/F6:** секционный streaming I, затем Height-Aware LOD/vegetation/shadows J, поэтапно и с контролем DX11.
4. **DXR G и streaming I/J могут развиваться параллельно:** RT streamer J5 интегрировать только когда базовые BLAS/TLAS и foliage path валидны; полное K не является предпосылкой аппаратных RT-теней.
5. **K (seamless) — долгосрочная возможность, возникающая на базе I/J**, а не отдельный режим или обязательный deliverable первой DX12/DXR версии. Классические переходы остаются поддерживаемыми как контентная механика; L — необязательный путь сотрудничества после проверки лицензий/переносимости.
6. **Контроль качества:** fixed routes — Кордон/лес, Припять/улица и крыша, холм/горизонт, помещение/подвал, переход через границу двух тестовых регионов; capture screenshots/video, sudden pop-in counters, frame-time percentiles, GPU/CPU times, VRAM/RAM peak и resource residency. Численные целевые пороги назначать только после baseline.
7. **Никаких ложных PASS:** каждый milestone требует соответствующий commit, воспроизводимые логи/тесты и указание реальных ограничений. Streaming, RT или seamless не считать реализованными из факта DX12 bootstrap.

**Статус I–L на 2026-10-15:** ⬜ **PLANNED / NOT STARTED**. Вся эта секция — roadmap proposal, не реализованная функциональность.

---


## 11. Дополнительный DX11 renderer и инфраструктура

Детальные ранее принятые технические исследования. Новая интеграция по отдельному приоритету; не подменять F4.2 косметической чисткой.

> **Инженерная спецификация из истории проекта — DLSS SDK 310.9.1 — историческое обновление.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

### Этап A — Обновить DLSS SDK 310.7.0 → 310.9.1

**Приоритет:** высокий · **Риск:** низкий · **Сложность:** дни
**Статус:** ✅ **выполнено 2026-10-14** — правка внесена, совместимость проверена,
сборка подтверждена.

#### Что сделано

`Update_Components.cmd:26`:
```diff
-git clone --branch v310.7.0 --depth 1 ... 3rd_party/Src/NVIDIA_DLSS/DLSS
+git clone --branch v310.9.1 --depth 1 ... 3rd_party/Src/NVIDIA_DLSS/DLSS
```

#### Проверка совместимости (по исходникам SDK, не предположение)

| Проверка | Результат |
|---|---|
| 42 API-символа `NVSDK_NGX_*` / `NGX_*` из движка в 310.9.1 | 42/42, отсутствующих 0 |
| `nvsdk_ngx_helpers.h` тянет `nvsdk_ngx_helpers_d3d.h` | да, `:116` |
| 4 `.lib` из `AdditionalLibraryDirectories` | все на месте |
| `rel/` + `dev/` DLL для post-build | обе на месте |
| `NVSDK_NGX_DLSS_Hint_Render_Preset_*` | без перенумерации |
| `NVSDK_NGX_PerfQuality_Value` | идентичен побайтно |

Preset L/M включатся автоматически через `DlssDllVersionAtLeast(310,5,0)`
(`RenderTargetPhaseAA.cpp:248`).

#### Результат сборки

```
bin_x64\xrEngine.exe     31 463 424 Б (CoP)   валидный PE
bin_x64\nvngx_dlss.dll   310,9,1,0   NVIDIA Deep Learning SuperSampling
bin_x64\OpenAL32.dll
23 проекта, 1315+ объектов, ошибок компиляции — 0
```

#### Рантайм-проверка CoP (подтверждено)

Билд установлен в `D:\Games\S.T.A.L.K.E.R. Call of Pripyat`; оригинальные
`xrEngine.exe` и `OpenAL32.dll` сохранены рядом как `.bak`.

| Проверка | Результат |
|---|---|
| `r_aa_dlss_version` | 310.9.1 |
| Апскейлинг Balanced | 1485x928 -> 2560x1600 |
| Пресеты DLSS | переключаются и применяются |
| FSR3 / TAA / SMAA | работают без падения |

#### Что осталось

Косметическая ошибка A1 (`XR_3DA.vcxproj:395-397`, post-build `dbghelp.dll`).
EXE линкуется; падает только шаг копирования. Либо поставить
«Debugging Tools for Windows», либо пропатчить в форке (этап E).

#### Откат

Вернуть одну строку и перезапустить `Update_Components.cmd`.

---



> **Инженерная спецификация из истории проекта — NVIDIA Image Scaling и DX11.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

### Этап B — NVIDIA Image Scaling

**Приоритет (DX11-направление):** высокий · **Риск:** средний до первой
успешной проверки compute-шейдера · **Сложность:** недели
**Статус:** B1 ✅; B2–B4 ⬜.

#### Зачем

NIS — официально поддерживаемый NVIDIA пространственный апскейлинг + шарпинг,
идущий **одним шейдером на DX11**. Для движка, у которого DLSS доступен
только на NVIDIA, это логичный fallback для AMD/Intel и старых карт.

`README.md` самого SDK: *«It contains compute shaders that can be integrated
with **DX11**, DX12, and Vulkan.»* — цитата проверена локально.

На момент диагностики submodule `NVIDIAImageScaling` (SHA
`35e13ba3…`) не инициализировался из-за отсутствия
`--recurse-submodules` в `Update_Components.cmd`.

**Статус B1:** ✅ **выполнено 2026-10-14 по исходному roadmap** — скрипт
поправлен, `NVIDIAImageScaling v1.0.3` активирован в рабочем дереве
(333 файла). **Осталось проверить воспроизводимость клона в чистом CI**;
локальная инициализация не доказывает её автоматически.

#### B1. Починить скрипт — ✅ СДЕЛАНО

```diff
-  git clone --branch v310.9.1 --depth 1 https://github.com/NVIDIA/DLSS.git 3rd_party/Src/NVIDIA_DLSS/DLSS
+  git clone --branch v310.9.1 --depth 1 --recurse-submodules https://github.com/NVIDIA/DLSS.git 3rd_party/Src/NVIDIA_DLSS/DLSS
```

Активация сабмодуля в существующем дереве (без перекачки 2 ГБ):
```cmd
git -C 3rd_party\Src\NVIDIA_DLSS\DLSS submodule update --init --recursive
```

#### B2. Compute-гейт и загрузка NIS HLSL

Текущее состояние: `.cs`-файлов в `gamedata` **ноль**, хотя
`ShaderTypeTraits<SCS>` (`ShaderResourceTraits.h:47-63`) умеет `cs_5_0`,
и `CResourceManager` имеет пул `m_cs` (`ResourceManager.h:39-49`).

1. **B2.0 — короткий тест:** создать минимальный compute-шейдер, реально
   выполнить `Dispatch`, записать предсказуемый результат в UAV и прочитать/
   показать его в ImGui/RenderDoc. Пройти путь компиляции и дискового кэша
   (`r4.cpp:823-869`) до конца. Без этого NIS не начинать.
2. Изучить `NIS_Main.hlsl`, `NIS_Scaler.h`, `NIS_Config.h` **ровно версии
   подключённого submodule**. Определить необходимые `NIS_*` defines,
   thread-group layout и поддерживаемые форматы.
3. Выбрать способ интеграции: исходный HLSL + рантайм-компиляция через
   действующий `CRender::shader_compile()` либо отдельно управляемый DXBC.
   **Не считать `.cs` в `gamedata` готовым исполняемым шейдером само по себе.**
4. Подключить shader-resource создание (`ShaderResourceTraits.h:137-144`),
   проверить путь `getShaderPath()` и очистку/инвалидацию shader cache.

> Это первый реально используемый compute-проход на данном пути рендера.
> Конкретный вызов создания (`s_nis.create` или отдельный compute handle)
> выбирать по фактическому API ResourceManager после B2.0.

#### B3. Обёртка и ресурсы

По месту вызова ориентироваться на `PhaseAA()` и `Fsr3Wrapper`
(`RenderTargetPhaseAA.cpp:685-851`), **но не копировать temporal-контракт FSR3**:

```
Вход:  rt_Generic_0 (render-res, SRV)
NIS:   config/constants + коэффициенты фильтра (SRV) + sampler
Выход: rt_Generic_combine (display-res, UAV)
Далее: BeginPostprocess(..., temporalOutput=true) / проверенный путь resolve
```

- Подать корректные коэффициенты scaler/USM и constant buffer по
  `NIS_Config.h`; задать `NIS_HDR_MODE` в соответствии с фактическим
  цветовым пространством `rt_Generic_0`.
- Проверить формат текстур, bind flags, SRV/UAV, sampler, размер dispatch,
  выход за границы viewport, отсутствие конфликтов одновременных RTV/UAV/SRV.
- NIS пространственный: история, jitter и `RequestHistoryReset` для самого
  NIS не требуются. CAS/другой sharpen не применять дважды.
- **Не изменять автоматически текущую цепочку отказа DLSS → FSR3 → TAA/SMAA.**
  Сначала ввести NIS как явный режим и сравнить качество/скорость. Порядок
  fallback менять только отдельным решением после тестов.
- При смене render-res обеспечить пересоздание зависимых RT или штатный
  `vid_restart`; просто вызов `u_setrt` проблему resize не решает.

#### B4. Интерфейс

Добавить в `pp_aa_mode_token` (`xrRender_console.cpp:37-47`) и `enum`
(`xrRender_console.h:264-272`) значение `NIS`.

**Обязательно синхронизировать Lua-UI** —
`Game/Resources_CoP_1.6.02/gamedata/scripts/ui_mm_opt_video_adv.script`:
строки `:27-29` объявляют `dlss_quality` / `dlss_preset` / `fsr3_quality`;
строка `:174` читает `r_aa_mode`. Это **реальный путь в GitHub**
(нижний регистр `scripts`), старое написание `Scripts/` было ошибкой документации.
Новый режим нужно поддержать как в C++ токенах, так и в Lua-UI.

#### Критерии приёмки

- B2.0: минимальный `Dispatch` корректно пишет в UAV; RenderDoc/ImGui показывает ожидаемые пиксели.
- `r_aa_mode <зарегистрированный токен NIS>` даёт корректный полный кадр
  на native и на пониженном render-res; CPU/GPU validation не сообщает ошибок.
- Работает без DLSS на совместимой DX11-видеокарте; NIS не должен требовать RT cores.
- Переключение DLSS/FSR3/TAA/SMAA/NIS и resize не приводят к падениям,
  чёрным кадрам, использованию устаревших ресурсов или неправильному aspect ratio.
  `vid_restart` допустим, **если это осознанный контракт UI**; без рестарта
  применять режим только после явной реализации пересоздания ресурсов.
- В Lua-меню правильные доступность/подсказка/качество; сохранённые настройки
  не меняют смысл. Нет двойного CAS/sharpen; HDR-яркость и альфа не ломаются.
- Сравнение NIS/FSR3/DLSS в одинаковых сценах: скриншоты, резкость,
  мерцание травы, задержки GPU (Tracy/RenderDoc), обоснованный выбор fallback.

#### Риски

| Риск | Оценка | Митигация |
|---|---|---|
| Тег NIS/повторяемость CI | Средняя | Закрепить проверенный submodule SHA и выполнить clean checkout |
| Первый `.cs` в проекте — неисследованный путь | Средняя | B2.0: отдельный тест `Dispatch` + UAV + RenderDoc |
| HDR/linear/PQ и коэффициенты NIS | Средняя | Следовать версии `NIS_Config.h`, визуальный тест |
| Двойной sharpen с CAS | Средняя | Не включать CAS поверх NIS без явного выбора |
| Смена render/display размеров | Средняя | Рестарт/пересоздание RT по контракту |
| Меню опций рассинхронизируется | Низкая | Правка `ui_mm_opt_video_adv.script` в том же коммите |

---



> **Инженерная спецификация из истории проекта — DLSS quality/alpha/jitter tests.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

### Этап C — Тюнинг текущего DLSS

**Приоритет:** средний · **Риск:** низкий · **Сложность:** дни

#### C1. `AlphaUpscaling`

`RenderTargetPhaseAA.cpp:341`. Проверить, поддерживается ли флаг выбранной
версией DLSS и соответствует ли он реальной семантике alpha на входе.
**Не включать вслепую:** сравнить траву, дым, дождь и UI до/после.

#### C2. `MVJittered`

`:337`. Наличие camera jitter (`:418-419`) само по себе **не доказывает**,
что motion vectors передаются jittered. Сначала сверить генерацию `rt_Velocity`,
знаки `InMVScale*` и контракт NGX, затем решить вопрос с флагом.

#### C3. `DoSharpening` — требует решения

`:332`. Без него `InSharpness` игнорируется (`:316-317`), но движок
имеет собственный CAS (`ProcessCAS`, `:611`), вызываемый в `PhaseAA()`
для всех режимов кроме SMAA (`:966-967`).

Варианты: оставить внешний CAS; использовать DLSS sharpening, если режим
поддерживается активной версией SDK; либо предоставить явный выбор
в настройках. **Критерий:** одна активная стадия sharpening, а не
предположение, что включённый флаг обязательно улучшает картинку.

#### C4. Задокументировать DLAA как дефолт

`xrRender_console.cpp:58`. Пользователь может не понимать, почему
при «DLSS» нет апскейлинга. Отражено в `ui_mm_opt_video_adv.script`.

#### Критерии приёмки C

Флаги проверены по контракту соответствующего SDK; видимые различия
сохранены скриншотами в статике и движении, артефакты не усилились,
производительность не ухудшилась непредсказуемо. Изменения независимы
и могут быть откатаны по одному.

---



> **Инженерная спецификация из истории проекта — DX11 SSGI/Hi-Z — отложено.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

### Этап D — SSGI / Hi-Z на compute-шейдерах

**Приоритет:** высокий по ценности, **высокий** по сложности · **Месяцы**

**Это один из путей получить приближение к трассируемому освещению
в DirectX 11.** Hi-Z/DDA — **screen-space ray marching** с ограничениями
вне экрана и за заслоняющими поверхностями; это **не DXR** и не замена
аппаратным acceleration structures. Hardware RT в выбранной D3D-архитектуре
требует перехода на DX12/DXR (направление F–G).

#### Что уже есть

| Актив | Где |
|---|---|
| Компиляция `cs_5_0` | `ShaderResourceTraits.h:47-63` (`:54`) |
| Пул CS-шейдеров | `ResourceManager.h:39-49` |
| UAV-таргеты | `r4_rendertarget.cpp:287-292` |
| **Нормали** | `rt_Position.xy` — закодированы, см. `common_iostructs.h:287-293` |
| **Gloss → roughness** | `rt_Color.a` |
| **Material ID** | `rt_Position.w` (закодирован с hemi) |
| Отладка в движке | `r4_imgui.cpp:75-81` — `DrawTexture` показывает `rt_Color`, `rt_Position`, `rt_heat`, `rt_Generic_1/2`, `rt_Bloom_1` |
| Тайминг-инструменты | Tracy с `PIX_EVENT` |

#### Чего ещё нет

- Ни одного `.cs`-файла (блокер; снимается этапом B).
- Metallic/emissive в 4 MRT.
- Hi-Z пирамиды.
- Равномерного доступа к треугольникам.
- Денойзера.

#### Подэтапы

| # | Подэтап | Содержание | Видимый результат |
|---|---|---|---|
| D0 | Compute-гейт | Минимальный `.cs` + UAV (можно использовать результат B2.0) | Работающий `Dispatch` и запись результата |
| D1 | Hi-Z | Построить и визуализировать mip-пирамиду глубины; выбрать доступный для compute формат и преобразование depth | Корректные mip-уровни в ImGui |
| D2 | Screen-space tracing | Проверить декодирование normal/depth, reconstruction позиции, DDA/Hi-Z и критерии hit/miss | Отладочная маска попаданий, предсказуемые экранные ограничения |
| D3 | SSGI lighting | Применить screen-space hit к освещению, учитывать emissive отдельным входом при необходимости | Измеримый непрямой свет без явных GPU-ошибок |
| D4 | Temporal/filter | Reprojection по motion vectors, rejection, spatial denoise, camera-cut reset | Стабильность в движении, контролируемый ghosting |
| D5 | Интеграция | Добавить независимый runtime-флаг, debug modes, профилирование и возврат к исходному освещению | Toggle в консоли без порчи baseline |
| D6 (опц.) | Off-screen geometry | Отдельно оценить программную структуру геометрии (BVH/SDF/voxel) | **Отдельное решение:** это уже не обычная Hi-Z SSGI, не делать без замеров |

**Первый шаг:** найти используемые шейдерами функции распаковки G-buffer,
переиспользовать их в compute-проходе, сверить преобразования координат
и `rt_Position.xy/.z/.w`. Наличие закодированной normal **не означает**,
что весь вход для GI уже подготовлен.

#### Критерий приёмки

Для D0–D2: воспроизводимая маска попаданий на нескольких сценах,
сопоставление с depth и нормалями, известные ограничения на краях экрана.
Для D3–D5: видимое улучшение непрямого освещения без заметного ghosting,
управляемая стоимость в GPU ms и без регрессии исходного DX11 при выключенном
эффекте. Любой шаг может завершиться решением **не продолжать**.

#### Риски

| Риск | Оценка |
|---|---|
| Hi-Z требует корректного доступа к глубине и форматов compute/UAV | Средняя |
| Screen-space не видит невидимую геометрию | Высокая, алгоритмический предел |
| Off-screen BVH/гриды (D6) — отдельный большой проект | Высокая |
| Temporal/reprojection, leakage/ghosting | Высокая |
| Reconstruction позиции/нормали и coordinate spaces | Средняя |

#### Рекомендация

Начинать только с D0–D2. Если Hi-Z + DDA не покажут полезного качества и
приемлемого GPU time — остановиться. Для достижения DXR **не требуется**
завершать D3–D6. Часть screen-space наработок пригодится позднее, но
программный BVH не является обязательной ступенью перед BLAS/TLAS.

---



> **Инженерная спецификация из истории проекта — Адресная инженерная чистка.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

### Этап E — Гигиена репозитория

**По договорённости — только диагностика, без правок.**

| Пункт | Файл | Что предложить |
|---|---|---|
| Закрепить коммиты | `Update_Components.cmd` | Заменить 12 веток на SHA |
| Проверить `--recurse-submodules` на чистом CI | `Update_Components.cmd:26` | **B1 уже выполнен локально**; проверить воспроизводимость |
| Условный `dbghelp.dll` | `XR_3DA.vcxproj:395-397` | `IgnoreExitCode="true"` или `Condition` на `Exists(...)` |
| `ReleaseTracyProfiler` как настоящая конфигурация | `Engine.sln` | Добавить в SolutionConfigurationPlatforms |
| Поправить устаревший комментарий | `build_config_defines.h:63-64` | Сказать 128, а не 768 |
| Убрать мёртвый код | `xrRender/*`, `xrPhysics.cpp`, `lua_studio.cpp`, `PHSynchronize.cpp` | см. §B |
| Защитить `Game/` | `.gitignore` | Добавить `Game/Resources_CoP_1.6.02/bin_x64/` |
| Проверить DirectPlay 8 на поддерживаемых Windows | `xrNetServer` / запуск CoP | Документировать runtime-требования; **не копировать системные DLL вслепую** |
| dependabot на зависимости | `.github/dependabot.yml` | изучить ограничения для сторонних git clone |
| Исправить пути `Configs/`/`Scripts/` в документации | `docs/*`, `Game/` | сверить с реальным деревом: `gamedata/scripts` |

---


## 12. VFX, аномалии и звук

Сначала исходные огонь/дым/вода/частицы и оригинальные gameplay события, затем расширенные аномалии и акустика; AMD/не-NVIDIA GPU должны сохранять корректный fallback.

> **Инженерная спецификация из истории проекта — VFX план и физические взаимодействия.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

### Направление VFX — базовая физика среды → улучшение аномалий (решение 2026-10-25; PLANNED)

**Порядок обязателен:** сначала модернизировать родные эффекты X-Ray (огонь, дым, искры, вода, лужи и реакции на ветер/геометрию), сохраняя игровые события и оригинальные эффекты; **только затем** использовать общую систему для электрических, огненных и гравитационных аномалий. Не писать второй независимый particle engine для аномалий. Это отдельный трек, не блокирующий DX12 F4.0 и PhysX NPC ragdoll.

**Платформенная стратегия:** NVIDIA Advanced — исследовать NVIDIA Flow для локального объёмного огня/дыма и PhysX GPU/PBD там, где он действительно применим и поддерживается SDK/GPU; не считать Flow частью PhysX и не обещать автоматическую совместимость. AMD/другие GPU — **временно** сохранять исходные огонь/воду/частицы и доступные универсальные улучшения без CUDA; DX11 Classic — обязательный рабочий fallback. После NVIDIA-пути отдельно разработать vendor-neutral DX12 compute-альтернативу для AMD и других GPU. Возможности определять по фактическим feature checks, а не только по названию производителя. Все пути должны сохранять одинаковую игровую механику и урон.

**Лужи:** дешёвый отдельный класс водных эффектов — отражение, рябь и локальные брызги при необходимости; **не** запускать полноценную жидкостную симуляцию для каждой лужи, в том числе на NVIDIA. Для больших водоёмов и локальных всплесков предусмотреть разные уровни детализации и бюджеты GPU/VRAM.

| Этап | Работы | Приёмка |
|---|---|---|
| **VFX0 — аудит родных эффектов** | Проверить particle engine, конфиги/геймдату, ZoneCampfire, wind API, огонь, дым, воду, лужи, взаимодействия с объектами, освещение и DX11/DX12 render paths. | Карта кода и эффектов, видео baseline, измерения CPU/GPU/VRAM, список совместимых API и лицензий; не объявлять функции SDK реализованными. |
| **VFX1 — базовый огонь/дым/искры** | Улучшить существующие emitters и реакции на ветер/геометрию. Изолированный NVIDIA Flow DX12 prototype для объёмного огня и дыма; лёгкий исходный/универсальный fallback. | Костёр в игровом окружении, ветер меняет дым/искры; нет регрессий тайминга, света, звука и gameplay, сравнение Classic/Enhanced/Advanced. |
| **VFX2 — вода и упрощённые лужи** | Сохранить базовую воду X-Ray, добавить недорогие рябь/брызги/контакты; исследовать PhysX GPU/PBD только для ограниченных локальных жидкостных эффектов на поддерживаемых NVIDIA. | Раздельные профили луж/водоёмов/всплесков, отсутствие массовой дорогой симуляции, измеренные GPU/VRAM бюджеты и fallback. |
| **VFX3 — универсальная альтернатива AMD** | После первого рабочего NVIDIA Advanced исследовать и внедрить vendor-neutral DX12 compute-эффекты огня/дыма/частиц и локальной воды без CUDA. Старые функции AMD — временное решение, не целевое качество. | Тесты на AMD и NVIDIA, feature detection, сравнение качества/производительности; отсутствие NVIDIA-зависимости в общем API эффектов. |
| **A-VFX0 — аудит аномалий** | Только после доказанного базового VFX1/VFX2 изучить CustomZone, FryupZone, TorridZone, GraviZone, ParticlesObject, idle/awake/blowout/bolt события и реальные игровые ассеты. | Матрица оригинальных аномалий, привязка визуальных эффектов к существующим gameplay событиям и эталонные видео. |
| **A-VFX1 — электрические аномалии** | Процедурные DX12 молнии, ветвления, вспышки и свет; не симулировать дуги как rigid bodies PhysX и не менять игровую зону поражения. | Сохранены тайминги урона/звука/болта, Classic fallback, нет ложных игровых коллизий. |
| **A-VFX2 — огненные/гравитационные аномалии** | Переиспользовать VFX огонь/дым/искры/ветер для огненных; локальные поля частиц/пыли и визуальные вихри для гравитационных. | Оригинальные механики, характер и ритм аномалий сохранены, сравнение Classic/Enhanced/Advanced и замеры производительности. |


#### VFX-AMD — предварительная оценка NVIDIA vs AMD, временный режим и риски (2026-10-25; PLANNED)

**Граница подтверждения:** это вводная инженерная оценка, **не измерение** на реальных GPU и не обещание совместимости. До выбора конкретных версий SDK, тестовых видеокарт и прототипов нельзя утверждать точные FPS, миллисекунды, расход VRAM или сроки. Flow и PhysX GPU — разные технологии; поддержку Flow на AMD проверять по выбранному backend/SDK и реальным тестам, а не выводить из наличия DX12.

| Компонент | NVIDIA: первый этап | AMD: первый этап | Целевое состояние |
|---|---|---|---|
| CPU PhysX rigid bodies / ragdoll | Доступен без CUDA при корректной интеграции | Тот же CPU PhysX без CUDA | Единая игровая физика независимо от GPU |
| PhysX GPU / CUDA PBD fluid | Исследовательский прототип только на поддерживаемых NVIDIA | CUDA backend недоступен напрямую; исходная вода/локальные эффекты | Универсальная DX12 compute альтернатива для локальных жидкостей либо упрощённая визуальная симуляция |
| NVIDIA Flow fire/smoke | Проверить DX12 backend, SDK, совместимость и производительность | **Не объявлять неподдерживаемым без проверки:** возможная переносимость DX12 пути зависит от версии и backend | Предпочесть переносимый backend, если доказан; иначе независимая DX12 compute реализация |
| Частицы, искры, ветер, электрические дуги | Универсальные DX12 shaders плюс опциональные advanced эффекты | На старте старые эффекты + безопасные общие DX12 улучшения | Единый GPU particle/lightning pipeline с профилями качества |
| Лужи | Упрощённые рябь/отражения/брызги | Та же облегчённая система | Не симулировать каждую лужу как PBD-жидкость |

**Вводный расчёт нагрузки (гипотезы для бюджетирования, не бенчмарки):**
- Для 60 FPS весь кадр имеет **16,67 мс**, для 30 FPS — **33,33 мс**. Дополнительный VFX-pass конкурирует с DX12/DXR освещением, тенями, прозрачностью и постобработкой; он не получает весь бюджет кадра.
- Пример проектного лимита **+1,0 мс GPU** на один локальный advanced эффект при 60 FPS составляет **около 6%** от бюджета кадра; **+2,0 мс** — около **12%**. Это **предлагаемые пороги исследования**, а не измеренная стоимость Flow/PhysX. При нескольких эффектах учитывать их суммарную стоимость и пики.
- Для объёмной сетки удвоение разрешения **по каждой из трёх осей** даёт до **8× больше ячеек** при равной плотности и без учёта sparse-оптимизаций; реальные время/память зависят от solver, активных блоков, формата данных и числа итераций. Нельзя напрямую переносить этот коэффициент на FPS.
- Прототипировать уровни качества с фиксированными ограничениями на активные источники, частицы, объёмные области, разрешение, iterations, GPU memory; отключать/упрощать дорогие эффекты при перегрузке, не изменяя игровую логику.

**Временная политика AMD:** на первых этапах сохранять рабочие классические огонь, дым и воду и добавлять лишь общие эффекты, которые реально прошли AMD GPU тест. Отсутствие CUDA означает отсутствие **CUDA backend**, а не отсутствие вычислительных блоков или невозможность GPU симуляции на AMD. Это **временный компромисс**, с отдельным обязательным этапом VFX3 и последующим повышением качества; не обещать сроки до профилирования.

**Возможные последствия и меры:** (1) визуальный разрыв NVIDIA/AMD — показывать режимы Classic/Enhanced/Advanced, документировать ограничения и план сокращения разрыва; (2) рост GPU/VRAM, особенно в сценах с несколькими кострами и аномалиями — per-frame budgets, LOD, culling и лимит одновременных симуляций; (3) vendor lock-in и обслуживание двух backend — единый Environmental VFX API и тесты одинаковых событий; (4) DX12↔CUDA interop, синхронизация ресурсов и драйверные различия — изолированный прототип, измерение копирований/барьеров, отказ от недоказанных предположений; (5) расхождение визуальной и игровой физики — authoritative gameplay на старых системах, advanced VFX только визуальные; (6) поломка DX11 fallback — отдельная regression matrix.

**Гейт перед обещаниями:** зафиксировать версии NVIDIA Flow/PhysX, лицензии, список NVIDIA/AMD GPU, проверить runtime capability detection; замерить p50/p95 GPU ms, VRAM, CPU overhead, стабильность и качество на одинаковых сценах (один костёр, несколько костров, вода/лужи, затем аномалии). Только после этих измерений оценивать реальные преимущества и стоимость универсального AMD пути.

**Статус:** PLANNED / NOT IMPLEMENTED. Реализация, приёмка и независимое GPU evidence потребуются отдельно.




> **Инженерная спецификация из истории проекта — Audio/OpenAL improvements.** Идеи, архитектура и тестовые критерии сохраняются. Датированные статусы внутри не отменяют нынешнего состояния, указанного выше.

### Направление AUDIO — Enhanced X-Ray Audio (решение 2026-10-25; PLANNED)

**Цель:** улучшить существующий звуковой движок OGSR/X-Ray, **не переписывая его с нуля**. Сохранить OpenAL/OpenAL Soft, оригинальные звуковые события, пространственное позиционирование, звуковую память AI, акустическую геометрию SOM и EAX/EFX, если соответствующие пути подтверждены аудитом. Улучшения звука не должны зависеть от DX12, PhysX, CUDA или производителя GPU и не блокируют DX12 F4.0.

**Основание для планирования:** предварительный статический аудит `xrSound/SoundRender_Core.cpp`, `SoundRender_CoreA.cpp`, `SoundRender_TargetA.cpp`, `SoundRender_Core_Processor.cpp`, `SoundRender_Emitter.cpp`, `SoundRender_Source_loader.cpp`, `SoundRender_Environment.cpp`, а также точек интеграции в `xrGame`. В коде присутствуют OpenAL-источники и параметры затухания, `geom_DB.ray_query` / `geom_SOM` для окклюзии, обработка звуковых целей и потоковая OGG-загрузка. **Наличие кода не является runtime PASS**: поведение, конфигурация и фактическое включение функций требуют проверки.

| Этап | Реализация | Критерий приёмки |
|---|---|---|
| **AUDIO0 — read-only аудит и baseline** | Проследить `xrSound` → OpenAL backend → `xrGame` и AI sound memory, приоритеты emitters/targets, `geom_SOM`, EAX/EFX, OGG streaming, параметры `psSoundTargets` и буферов; сверить конфиги, реальные ассеты и версии библиотек. | Карта вызовов с путями/функциями; воспроизводимые тестовые сцены (коридор, улица, дверь/стена, много NPC, дождь/аномалии), CPU/память/latency/voice counts и аудиозаписи Classic baseline. |
| **AUDIO1 — надёжность и численная устойчивость** | Проверить нулевую/малую дистанцию в `get_occlusion_to/get_occlusion`, контракты `Fvector::div`, некорректные OGG/seek/read, lifetime звуков, device reopen при активных потоках и EFX; исправлять только доказанные ошибки. | Unit/integration тесты совпадающих позиций, испорченных файлов, переключения аудиоустройства и остановки/рестарта потоков; без падений и регрессий. |
| **AUDIO2 — улучшенная окклюзия и материалы** | Расширить текущие `geom_SOM`/ray-query механизмы частотно-зависимым ослаблением (например, low-pass через препятствия), с совместимостью со старыми SOM и сохранением legacy scale при отсутствии данных. | A/B тесты бетон/лёгкая преграда/открытая линия видимости; контролируемая разница спектра и громкости, стабильность при перемещении, ограниченный CPU budget. |
| **AUDIO3 — окружения и реверберация** | Исследовать существующие EAX/EFX environment transitions, зоны помещений/тоннелей/подвалов, отражения и плавные переходы; не обещать полноценную wave acoustics без отдельного прототипа. | Сравнение Classic/Enhanced в помещениях и на улице, тесты устройств/OpenAL Soft, без резких скачков и с корректным fallback при отсутствии расширений. |
| **AUDIO4 — масштабирование и профилирование** | Измерить фактическую систему `priority()`, `psSoundTargets`, OGG buffering и стоимость acoustic ray queries; внедрять voice budgeting, query caching и LOD только по профилю. | Сцены с множеством NPC/выстрелов/аномалий/дождём, p50/p95 CPU time, количество активных/виртуальных voices, underruns, отсутствие потерь критичных звуков. |
| **AUDIO5 — расширенное распространение (исследование)** | Оценить ограниченные отражения, дифракцию за углами и акустические материалы, используя существующую геометрию уровня; API не связывать с DX12. | Изолированный прототип, бюджет CPU/памяти, проверка слышимости и AI; решение о внедрении только после сравнения с AUDIO2–AUDIO4. |

**Инварианты:** звук и AI должны получать согласованные игровые события, но визуальная/акустическая детализация не должна скрытно менять поведение NPC; DX11 и Classic fallback обязательны; исходные аудиоресурсы и узнаваемая атмосфера Зоны сохраняются. Использовать feature flags и сравнение Classic/Enhanced. Нельзя считать `psSoundTargets = 256` доказательством 256 одновременно слышимых звуков, а номинальные три блока по 400 мс — задержкой старта 1,2 с без runtime измерений.

**Статус:** PLANNED / NOT IMPLEMENTED. Первый следующий шаг — AUDIO0, без преждевременной замены OpenAL backend.


## 13. Работа агентов, тесты, приёмка и история

### Процесс

1. Изучить `AGENTS.md`, документацию и реальный code path. Проверить `git status`, branch и base SHA.
2. Реализовывать крупный функциональный результат. Субагенты получают независимые R4 audits, test fixtures, scripts, source/visual comparison; главный агент отвечает за интеграцию и GPU.
3. Адресные build/smoke в процессе; итоговая чистая матрица на последнем SHA этапа, независимое ревью source/artefacts. Не превращать разработку в бесконечные однотипные аудиты.
4. Соблюдать game-owned CPU snapshots, VFS/GPU ownership, fences/epochs. Не считать «уровень загрузился» доказательством clean exit; повреждение lifetime/GBV/UAF блокирует гейт.
5. Публиковать логические commits, не выгружать игровые архивы/тяжёлые captures и чужие изменения; remote feature push не даёт разрешения на production merge.

| Класс изменения | Минимальная приёмка |
|---|---|
| CPU parser | Malformed input, budgets, rejected reason diagnostics, fault/ASan |
| Level geometry | R4 visual ID/type/reachability parity и matched game screenshots |
| Game dynamic visual | Spawn/delete, animated transforms, stable object generation, R4 same-pose |
| Материалы | Opaque/cutout/blend/depth, actual DDS/THM semantics |
| GPU frame | InfoQueue Debug Layer/GBV, fences/retire, resize/close/reload |
| Weather/cloud | CEnvironment snapshot, single device/queue/GR, HDR/depth/barriers, fallback |
| PhysX | ODE baseline, save/loot/contacts, lifetime и сравнимые CPU timings |
| Performance | Same-save A/B, p50/p95/p99 CPU/GPU, VRAM, качество и power где нужно |

### История значимых результатов

| Рубеж | Событие |
|---|---|
| Октябрь 2026 | Стратегия DX12 → DXR, PhysX portability и GPU volumetric clouds |
| Начало ноября | GR, J1 real assets/VFS/readback/reset, CPU/GPU foundation |
| 9 ноября | [PhysX production source merge PR #1](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/pull/1) |
| 20 ноября | F4.0-B реальная игровая DX12 камера, DDS и Present |
| 21 ноября | F4.1-E static R4 inventory parity, TREE_PM/LOD geometry restored |
| 21 ноября | [F4.1 feature branch опубликована](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/tree/feature/f41-full-static-jupiter), исходники доступны |
| Следом | F4.2 игровые skeleton-rigid props и `level.details` |

**Обновление этого файла:** вносить только актуальный статус задачи, архитектурный контракт/план, blocker и acceptance. Повторный лог/review по одному и тому же пункту — отдельный отчёт/Issue/PR со ссылкой здесь.

[README](../README.md) · [DX12 ADR](dx12-architecture-adr.md) · [R4 Renderer](02-renderer.md) · [GitHub Issues](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues) · [полная историческая версия](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/521cde4e685d003a7ee8f4e0cf08a2730fb45ab8/docs/08-roadmap.md)
