# 08 — План работ

> **Актуальная ветка документа:** `prypiat` (форк ветки CoP `main_cop_cs`).
> **Дата обновления планов:** 2026-11-08 (J1 post-merge GPU ACCEPTED WITH NOTES; J2A R2 independent Codex **REQUEST CHANGES** — 3 P2 + 3 P3 findings, J2A R3 corrective gate planned; J2A source/J3 HOLD, облака и PhysX — отдельные направления).
> Исторические статусы A/B1/F/V и их даты оставлены как ранее зафиксированные
> отчёты, а не результат новой проверки GPU/CI. Изменение roadmap **не означает**
> реализации или тестирования добавленных этапов.

**Конечная техническая цель:** сохранить стабильный DX11/R4 и поэтапно получить
**нативный DirectX 12**, затем **аппаратный DXR** (сначала RT-тени, затем — при
успешных измерениях — отражения/глобальное освещение).

**Принятый архитектурный ориентир (решение 2026-10-22):**
**X-Ray DX12/DXR — Hybrid Rendering Architecture**. Изучать гибридную
реализацию RTX в *Control* (Remedy / Northlight) как эталон выборочных
RT-эффектов и их интеграции в raster pipeline, а *Metro Exodus PC Enhanced
Edition* (4A Games) — как исследовательский ориентир динамического RTGI для
открытых пространств. **Это проектное решение, а не реализованный RT backend.**
Приоритет F3.6/F4.0 и порядок F4 → G3/G4 → опциональный G5 не меняются;
никакого обязательного path tracing, DDGI или RT для запуска игры.

**Новый приоритет после F3.5.6A–C (решение 2026-10-21):** перейти к раннему
**F4.0 — первому реальному игровому кадру CoP/OGSR на DX12**, не дожидаясь
полного переноса освещения, прозрачности, HUD, постобработки и DXR.
Это **план**, а не утверждение о готовом игровом DX12-рендерере; точные
зависимости реальных ассетов и игрового frame loop сначала проверить аудитом.

**G-CLAS / PTLAS — принятое направление исследований (решение 2026-11-03):**
в будущей DXR-геометрии исследовать **CLAS → Cluster BLAS** и **Partitioned TLAS**
для больших уровней, LOD, растительности и изменяющихся инстансов. Обычные
**BLAS/TLAS остаются обязательным базовым и fallback-путём**; CLAS/PTLAS
не реализованы, их экономия VRAM и ускорение в OGSR не доказаны. Исследование
**не блокирует F4.0-J1.2B, первый игровой Draw и G1–G3**.

**F4-PBR — принятое направление (решение 2026-10-30):** после раннего
F4.0 развивать самостоятельный physically based material/lighting path
для DX12 raster: Legacy/Enhanced, THM/DDS и совместимая модель для будущего
DXR. IX-Ray 1.6 STCoP — референс для архитектуры (см. F4-PBR ниже),
но не готовый DX12 renderer; PBR не блокирует F4.0 и первые G3-тени.

**Актуальный графический статус (2026-11-06):** G1B Game Bridge — source-only в Git (`bb62c00ac`); J1.1 — независимо принят standalone real-asset GPU Draw (`a1c751274`); J1.2A Round 2 — source/owner-lifetime (`c4a95c39`); J1.2B Round 2 — source-only RTV/depth/submit/readback (`3a9b7c926`). **F4.0-J1 Asset Recovery R3 — ACCEPTED как VFS/CPU compatibility gate:** после установки оригинальной CoP в новой проверке обнаружено 41 архивное хранилище, core files Jupiter/Zaton/Pripyat читаются через production VFS, `XRLC_version=14`, `CFORM=4`, реальные `fsL_VISUALS`/`fsL_SECTORS` и `ReadVisual` на drawable `MT_NORMAL` leaf проходят CPU-only checks. Оригинальный runtime не запускался; in-game OGSR DX12 Draw, GPU readback и R4 Present **NOT TESTED / BLOCKED до J1 runtime**. R2 фиксирует прежнее состояние неполного payload и не отменяется; R3 его supersedes. **F4.0-J1 R1 runtime attempt (2026-11-06) — BLOCKED:** изолированный OGSR x64 DX11/R4 достиг Zaton `LevelResources load completed`, но `g_pGameLevel->bReady` не достигнут из-за `bind_stalker.script:125` → `set_callback` (Lua/engine contract mismatch, `callback.actor_before_death` отсутствует в enum и Lua exporter). Caller откатан; DX12 game Draw/readback и R4 Present parity NOT TESTED. **J1 Runtime Compatibility R2 (2026-11-06) — DX11/R4 GAMEPLAY PASS:** уменьшенный compatible loose OGSR overlay (9 scripts/21 configs) и CoP archive fallback устранили R1 Lua fatal без изменения engine; пройдены Zaton New Game, Jupiter save load, level teardown/reload. Clean shutdown не подтверждён, feature-on runtime в R2 не запускался — это исторический статус R2. **F4.0-J1 First In-Game DX12 Readback R2 (2026-11-06): GPU EVIDENCE PASS** — реальный Jupiter sector 129 → root 17431 → leaf 9, игровой ViewProjection, один DX12 DrawIndexedInstanced(1314), fence 3, D32, 10 623 ненулевых пикселя PNG, GBV PASS, R4 Present и clean bridge shutdown; независимый пересчёт PNG и hashes PASS. **Исторически (на момент R2) production patch ещё не был закоммичен; принятый R2B source интегрирован позднее коммитом `656652be7` (2026-11-07).** **F4.0-J1 R2A diagnostic hardening (2026-11-07):** независимые hash/PNG/CPU-oracle и source-level проверки техлида **PASS WITH NOTES**: explicit level/session/epoch, zero-coverage image gate, readback buffer layout, long PNG-path fail-closed; реальный повторный Draw/Jupiter readback и GBV PASS по Windows evidence. **Codex review завершён: REQUEST CHANGES (P2)** — при reset до диагностического Draw `captureEpoch` и `geometry.LevelEpoch` обновляются, а `diagnosticLevelEpoch` остаётся старым, блокируя identity gate в той же сессии. GPU evidence R2/R2A не опровергнуто. **R2B fix и повторный review PENDING; production integration HOLD**; feature-off runtime и TDR в R2A NOT TESTED. F4.0 и GR game parallel recording J2/J3 впереди.

**Сверка PhysX на 2026-11-06 (параллельный трек P):** предыдущий независимый аудит **P3.3-G4.2-R1 — EXPERIMENTAL PASS WITH NOTES** по переданному review-пакету; **G5 Real Gameplay Readiness — PLANNED / NOT ACCEPTED**. В отличие от ранее принятого evidence, GitHub commit `a02eca549b05e057a9fe6c1542bd0d5d9918a7d8` не найден при отдельном запросе к репозиторию; интеграция именно этого SHA в удалённую ветку **UNVERIFIED**. Это не новая независимая проверка архивов, Windows runtime и push. Полная provenance и матрица G5 приведены в разделе P.

**Основные направления:**

- **DX11 — A–D:** DLSS, NIS, контроль качества апскейла, исследовательский SSGI/Hi-Z.
  Это полезно для текущей игры, но не является необходимым условием DX12.
- **DX12/DXR — F–G:** аудит зависимостей, вертикальный прототип нового backend,
  ранний каркас многопоточного CPU-side renderer в **F3.5 перед F4**,
  перенос игрового кадра и аппаратные лучи. Этап F0 можно начинать параллельно B2.
- **E:** технический долг и воспроизводимость сборки, без автоматического
  расширения объёма правок.
- **H:** поздний аудит и адресная оптимизация **всего игрового движка** после
  RT-рубежей; не путать с архитектурным DX12 F3.5.
- **I–L (новые планы, не реализованы):** секционный world streaming,
  высотная видимость, растительность/тени, бесшовные миры и возможность
  повторного использования DX12/DXR командой IX-Ray Platform.
- **P — PhysX Ragdoll (самостоятельный параллельный трек):** первая игровая цель —
  **ragdoll одного NPC в OGSR DX11/R4**, затем укрепление реализации и перенос
  подтверждённого ядра в IX-Ray. ODE/движение NPC не заменять; DX12 F4.0 не блокировать.
- **V — Volumetric Clouds (самостоятельный параллельный трек):** изолированный
  DX12/compute-прототип процедурных 3D-облаков с ray marching; позже —
  необязательная интеграция в игровой DX12 с выбором Classic 2D / Volumetric 3D.
  **V0–V2 разрешено разрабатывать параллельно F4.0; V не является гейтом F4/G.**
  **Актуальный приоритет V-Q (2026-11-07):** стабилизировать и квалифицировать
  original V-SF Rain, а не запускать новые morphology experiments; подготовить
  ранний cloud proof сразу после пригодного игрового DX12 (см. раздел V-Q).

### Актуальная сверка — 2026-11-01: F4.0-B3-B2B Round 6 в `prypiat`

**Статус: ✅ `INTEGRATED — INDEPENDENT CODEX PASS` в пределах standalone
GPU consumer и evidence/verifier/harness.** Код опубликован отдельным коммитом
[`fc3b0617e2a38664c8f8670476eab8a96bfe9029`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/commit/fc3b0617e2a38664c8f8670476eab8a96bfe9029)
поверх `8bbc9f0e0b7602ca5fb7610b7b08bfba79ad050c`. Это актуальный
кодовый HEAD перед данным документационным обновлением. Коммит добавляет
**ровно 14 test/evidence-файлов** исключительно в
`ogsr_engine/Layers/xrRenderDX12/tests/` (**7779 insertions, 0 deletions**).
Он не меняет shipping renderer, `xrGame`, `xr_3da`, DX11/R4,
production B1–B5, named-pipe protocol, PhysX, DXR, PBR и roadmap.
После публикации отдельно сверены remote HEAD, единственный commit,
parent, полный whitelist и 14 Git blobs; различия LF/CRLF между
отдельным ZIP-снимком и Windows worktrees были явно учтены.

**Основание независимой приёмки:** Codex `Round6-independent-review.md`
(2026-11-01), вердикт **`PASS — READY TO COMMIT`**, после чего DeepSeek
выполнил commit/push с post-push проверкой; локальный основной checkout
не менялся. Проверенный full patch:
`B3B2B-Round6-full.patch`, SHA-256
`9EBA72FFF288D3CE642D859AA2955356419F255DED581142204CB099EE0ED7E0`.
Codex независимо проверил **1248/1248** записей evidence manifest,
четыре цепочки применения на историческом `920a3f16`, прежнем `6e2d82dc`
и актуальном на момент review `8bbc9f0e`; **14/14 postimages**
совпали. Manifest и GPU-логи остаются внешними evidence-артефактами, а не
добавленными в Git файлами.

| Проверка / направление | Зафиксированное подтверждение | Граница результата |
|---|---|---|
| **R5-N1, real D3D12 InfoQueue** | Независимый before/after: Round 5 отвергал `INFO: No ERROR occurred`, Round 6 принимает; `ParseInfoQueue` использует structured severity, `ValidInfoQueue` применяет zero-policy к WARNING/ERROR/CORRUPTION | `pDescription` — произвольный текст; совпадения слов больше не подменяют severity; полный coherent rewrite всех mutable evidence без внешнего trust root не обнаруживается |
| **Evidence source/build/CPU** | Codex: четыре MSVC x64 targets exit 0; portable evidence CPU `128/0` (`/W4 /WX`), persistent CPU `97/0`; fault `33/33` | Это test targets и standalone consumer, **не** combined `Engine.sln` / игровой DX12 renderer |
| **Verifier и negative matrices** | Codex: full verifier 24 cases; runners `10/10` и `12/12`, self-tests `7/7` и `26/26`; R4 `21/21`, independent probes 20 negatives + control, extended 40 negatives + 6 controls | Проверены фактические baseline PASS, mutation/reject и fail-closed cases; не утверждать абсолютную защищённость evidence |
| **Jupiter offline GPU** | Codex самостоятельно: RTX 5070 Laptop, D3D12 Debug Layer + GBV `ID1016`, frames `3/3/3`, sampled 3, fences `4/4`, InfoQueue `81/81`, warning/error/corruption `0/0/0`, verifier `0/PASS/PASS`; `format=v2` | Это **standalone DX12**, не кадр внутри игрового CoP/OGSR runtime |
| **Named-pipe → GPU live** | Codex независимо: sample-every=1 `2/2/2/2`; sample-every=10 last `2/3/3/2` (третий `kind=last`); forced-final `2/2/2/2`; producer/consumer/verifier exit 0, protocol/CRC errors 0 | Источник — отдельный standalone producer, **не** реальный `xrGame` producer |
| **B1–B5 Round 2** | Ранее опубликованы и имеют отдельный Codex `CONDITIONAL PASS`; Round 6 их не изменял | Два игровых shutdown crash и незавершённые transport/build gates остаются отслеживаемыми |

**Открытые гейты F4.0:** `CMainMenu::Activate` (`MainMenu.cpp:147`)
и `CGamePersistent::OnFrame` / `Level().name()` до проверки
`g_pGameLevel` — оба **P2 / UNRESOLVED**, baseline A/B ещё нужен;
B3 reconnect/handle stress на финальном игровом binary **NOT TESTED**;
автоматический `-BuildRelease` bootstrap **не принят**.
Не проверены combined current-prypiat game build,
in-game `xrGame` producer, реальный игровой DX12 кадр и
DX12→DX11 fallback, другие GPU/драйверы, ASan, long stress и реальный TDR.
Ни аппаратный standalone PASS, ни принятый test harness не закрывают
**F4.0 native in-game renderer acceptance**.

**Следующие независимые задачи:** (1) отдельное baseline A/B и lifecycle
regression обоих shutdown crashes; (2) read-only аудит игрового frame loop,
встраивание реальной геометрии Jupiter в opt-in DX12 runtime при сохранении
R4 fallback; (3) отдельные reconnect/handles и build/bootstrap проверки.
Любые production исправления требуют собственного patch, тестов и Codex review.

Ниже сохранены **исторические** сверки на 2026-10-31 и 2026-10-30.
Указания в этих старых таблицах, что B3-B2B «не в Git» или ожидает re-review,
не являются текущим статусом после `fc3b0617`.

### Актуальная сверка — 2026-10-31: B1–B5 Round 2 в `prypiat`

**Состояние Git после финальной независимой приёмки:** production-only изменения
Claude B1–B5 Round 2 опубликованы поверх `9b46385540e19c4655c6c8637a0a8c6043faaadd`
двумя последовательными коммитами:

- `54c5ea039be2ccf20a9aa252681804c6667d4a28` —
  `fix(dx12): harden B1-B5 production camera and GPU lifecycle`;
- `c67a04cbed38c72ecb6f1b43482dfb88486361bf` —
  сохранение исходных CRLF в `GameCameraCaptureAdapter.cpp/.h`.
  Второй коммит только восстанавливает побайтовую идентичность принятого
  патча; итоговые **7/7 Git blob SHA совпадают** с независимым production-only
  Round 2 review (включая новый `GameCameraCaptureExportPath.h`).

**Основания приёмки:** Codex `Round2-independent-review.md`:
`CONDITIONAL PASS`; Windows `B1-B5-Round2-Integration-Report.md`:
`CONDITIONAL PASS`; Codex `Final-integration-review.md`
(2026-10-31): **`CONDITIONAL PASS — READY TO COMMIT WITH TRACKED RISKS`**.
Это разрешение на узкий production-коммит, **не** полное принятие игрового
DX12 renderer/F4.0 и **не** закрытие двух crashes.

| Направление | Состояние на 2026-10-31 | Доказательства / условия |
|---|---|---|
| **Claude B1–B5 Round 2 (production-only)** | ✅ **В Git (`prypiat`), Codex CONDITIONAL PASS** | 7 разрешённых файлов, включая новый header; B1 порядок `Execute → Signal → Present`, B2 `GetCommandLineW` и Unicode export, B3 first-instance, B4 throttling, B5 lifecycle; test hooks находятся за compile-time guards и **не включены** в проверенных shipping defines |
| **Build и обычный runtime** | ✅ **Feature-on сборка и игровые тесты по Windows evidence**, независимое delta-review | Ручные `Release|x64` и `ReleaseDX12|x64` MSVC exit 0, post-build 0; новый `xrEngine.exe`: две реальные Jupiter-сессии, epoch `1→2→3→4`, teardown=2, exit 0; game IPC 1201 кадров/0 protocol/CRC errors; Unicode export. Codex проверил compilation logs/артефакты, самостоятельно повторил standalone DX12 и Unicode export, **не** полный Jupiter/IPC runtime |
| **Standalone DX12 smoke** | ✅ **Независимый аппаратный повтор Codex** | Fence 49/0, geometry 245/0, visibility 1519476/0, GPU resize/GBV/InfoQueue/Shutdown; **не** игровой DX12 frame |
| **F4.0-B3-B2B persistent GPU evidence (P1–P4)** | 🔴 **Отдельный патч, НЕ в Git; Codex REQUEST CHANGES** | Исходные single repro и hardware GPU replay проверены, но extended audit нашёл 8 нарушений integrity (включая sampled-set false PASS, false FAIL hash, fence sentinel и Unicode relocation); Round 3 исправления требуют отдельной независимой приёмки |
| **Shutdown: `GamePersistent::OnFrame`** | 🔴 **P2, UNRESOLVED** | Возможный `Level().name()` при `g_pGameLevel==nullptr`; старый crash `0xC0000005` после teardown. Причина и статус pre-existing/regression не доказаны. Отдельный guard fix, baseline A/B и регрессия перед следующим F4.0 acceptance |
| **Shutdown: `CMainMenu::Activate`** | 🔴 **P2, UNRESOLVED** | Direct-start `-start ...` + `quit`: null `m_startDialog`, `MainMenu.cpp:147`, `0xC0000005`; PDB/minidump сопоставлены. Связь с B1–B5 **не подтверждена**, baseline A/B не выполнен; отдельный issue/fix/review |
| **B3 IPC reconnect / handles** | ◻ **NOT TESTED на новом integration binary** | Проверены single connection/1201 кадров и отдельные B3 Win32/transport тесты. Перед последующим transport acceptance: потеря receiver во время overlapped write, reconnect, порядок/CRC, handle counting и teardown |
| **Build bootstrap/automation** | 🟡 **P3, не закрыт** | `prepare-b1b5-integration.ps1 -BuildRelease` завершился exit 1 (нет gitignored vendored dependencies, нехватка места `D:`, ошибка создания корня `D:`); последующая ручная feature-on сборка exit 0. Исправить зависимости и явные feature properties в отдельной задаче, не объявлять script PASS |
| **F4.0 — игровой DX12 frame** | ⬜ **НЕ ПРИНЯТ** | Ещё нет принятой отрисовки реальной геометрии Jupiter внутри OGSR DX12 runtime; standalone consumer и корректная камера не заменяют in-game frame/renderer, DX12→DX11 fallback и full lifecycle acceptance |

**До следующего F4.0 milestone acceptance:** отдельно закрыть либо доказательно
классифицировать оба shutdown crashes (с сохранением GamePersistent cleanup),
протестировать нужные reconnect/handle и интеграционные lifecycle gates и
проверить итоговую игровую DX12-сборку. Реальный TDR, Engine ASan и полный
игровой DX12→DX11 fallback не были проверены в рамках данного B1–B5 review;
не записывать их как PASS. Исправления B3-B2B Round 3, PhysX и PBR **не**
входят в эти два производственных коммита.

Ниже сохранена **историческая сверка на 2026-10-30**: её предыдущие статусы
`B1–B5 Round 2 не в Git` больше не являются актуальными после данной записи.

### Актуальная сверка статусов проекта — 2026-10-30

**Источник истины и уровни доказательств:** ветка `prypiat` после
документального коммита `3e9fe8b7`; последний коммит **с исходным кодом
F4.0** — `920a3f168d8820ffbb065c1027b739eaaac27788` (B3-B2A).
Ниже **«в Git»** означает наличие кода в ветке, **«отчёт DeepSeek»** —
тесты Windows/GPU, заявленные и документированные исполнителем, а
**«ревью Codex»** — независимая проверка в указанном объёме. Эти уровни
не взаимозаменяемы. Необъединённые патчи, локальные worktree и отчёты
не повышают статус кода в `prypiat` автоматически.

| Направление | Состояние на 2026-10-30 | Доказательства / оставшийся гейт |
|---|---|---|
| **DX11/R4: A–C** | ✅ Исторически подтверждён рабочий baseline; нового полного ретеста нет | Старые результаты DLSS/NIS не считать свежей проверкой всех режимов |
| **D — DX11 SSGI/Hi-Z** | 🔎 Исследование, без подтверждённого игрового PASS | D0–D2, отдельное решение о продолжении; не гейт DX12 |
| **F0–F3.5.6A/B** | ✅ Standalone DX12 foundation и геометрия в Git | Исторические smoke/CPU/GPU результаты; **не игровой DX12 renderer**, не заявлять полного runtime/perf PASS |
| **F3.5.6C — material/PSO** | ◻ Отдельный независимый PASS не подтверждён | Проверить перед полным F4 либо формально принять документированный минимальный эквивалент |
| **F3.6A — X-Ray Asset Bridge** | ✅ Закоммичен `ccf0d22d` | Настоящий SoC OGF/DDS в standalone; не полноценная игровая сцена |
| **F4.0-B1 / B2 / B2.3** | ✅ Код в Git | Jupiter geometry reader `0ef5aaf2`; game camera snapshots `77e71e47`; opt-in capture `60f2030e` |
| **F4.0-B3A** | ✅ Код в Git | `4b120ca8`: standalone Jupiter DX12 offscreen validation, не игровой DX12 кадр |
| **F4.0-B3-B1** | ✅ Код в Git | `61a75650`: real-game camera capture → standalone DX12 offscreen replay/validation; не подмена R4 |
| **F4.0-B3-B2A** | ✅ Код в Git; Codex CONDITIONAL PASS в истории этапа | `920a3f16`: camera mailbox и Named Pipe → **standalone CPU receiver**; producer/consumer, не GPU renderer в игре; runE не закрыт |
| **F4.0-B3-B2B — persistent GPU consumer / evidence** | 🟡 **Изолированный patch, НЕ в Git; DeepSeek: `READY FOR CODEX RE-REVIEW`** | После исходного Codex `REQUEST CHANGES` исправлены P1–P4; Windows по отчёту: CPU 97/0, evidence CPU 38/0, fault 33/33, два P4 harness 10/10 и 11/11, offline DX12 3/3 с Debug Layer/GBV и 0 InfoQueue errors/corruption; **повторное независимое Codex review не получено** |
| **Claude B1–B5 Hardening (отдельный patch)** | 🔴 **Codex: `REQUEST CHANGES`; НЕ в Git** | B1 fence-ordering, B3 pipe, B4 logging, B5 duplicate teardown в основном подтверждены; B2 реальный Unicode CLI/quoted-tail не исправлен, B1 fault hooks не изолированы, full feature-on `xrGame` lifecycle не доказан, post-build `dbghelp.dll` завершался ошибкой |
| **Claude B1–B5 Round 2** | 📝 Задание DeepSeek подготовлено, выполнение НЕ подтверждено | Исправить B2/guards B1, пересобрать feature-on `xrGame`, проверить lifecycle, успешный post-build; затем Codex re-review |
| **runE / shutdown** | 🔴 `0xC0000005`, UNRESOLVED | Codex подтвердил null read после unload и опасный доступ к `Level().name()` до null-check в `GamePersistent.cpp:515`; **точный root cause и «pre-existing» не доказаны baseline A/B**. Отдельный worktree/fix |
| **F4.0 — полноценный игровой DX12 frame** | ⬜ **НЕ ПРИНЯТ** | Нужны draw/submit в CoP runtime, реальный уровень/материалы, управляемая камера, validation, resize/reload/shutdown и DX11 fallback; standalone GPU PASS недостаточно |
| **F4-PBR.0–4** | 📝 Архитектурный план в Git (`3e9fe8b7`), **код не реализован** | IX-Ray PBR проанализирован как референс; ещё нужны аудит OGSR THM/DDS, Material/PSO, G-buffer, GGX/IBL, игровой A/B и общий material lookup для DXR. Не гейт F4.0 или G3 |
| **F5/F6 и G0–G5 (DXR)** | ◻ Запланированы; рабочего игрового DXR PASS нет | F5 temporal/F6 perf после игровой базы; G0–G2 можно исследовать отдельно, G3 — RT-тени, G5 — отражения после принятого PBR raster/material path |
| **V — Volumetric Clouds** | 🔴 V2.3h-A03b-R02: независимый CPU audit **BLOCKED / STOP** (2026-11-01); V2 NOT ACCEPTED | HCS1/HCS2 не портировать в HLSL; следующий **R03A architecture review (PLANNED)**; GPU/игровой PASS отсутствуют. V независим от F4/G; см. датированный срез ниже |
| **P — PhysX ragdoll** | 🔎 ODE/скелетная архитектура исследованы; P0–P2 НЕ ПРИНЯТЫ | Один NPC ragdoll сначала в OGSR DX11/R4; ODE fallback; не блокировать F4 |
| **H, I–L, VFX, AUDIO** | 📝 Запланировано или исследуется; новые runtime PASS не предъявлены | Гейты и зависимости указаны ниже; не считать roadmap-решения реализованными возможностями |

**Проверяемые артефакты, не опубликованные в `prypiat`:**

- **B3-B2B P1–P4:** DeepSeek Windows report
  `F4.0-B3B2B-P1P4-Verify`, patch `B3B2B-P1-P4-final.patch`,
  SHA-256 `C0F1982E854E6650117B75605D9AA4C9137ACD89A0A99F1C25315200CC8A4E6D`;
  fresh apply / 11 из 11 файлов заявлены и зафиксированы в evidence.
  **Недостаточно для ACCEPTED без повторного Codex review.**
- **Claude B1–B5:** `B1-B5-full.patch`,
  SHA-256 `A1C01996A1247AAF61A1BE9435A2C0D7BD5FA550CC422F8E2408F649003C10F9`,
  14 файлов; DeepSeek первоначально заявил `CONDITIONAL PASS`, но
  **последний независимый Codex verdict — `REQUEST CHANGES`**.
  Production-only patch тоже не принят целиком из-за B2.
- Факты тестирования всегда разделять на **independently executed**,
  **log/evidence-verified**, **author-reported** и **NOT TESTED**.
  Нет подтверждённых аппаратного TDR, полного current-format live
  Jupiter с финальным `last` sample и штатного feature-on lifecycle.

**Ближайший порядок (не означает разрешение на слияние патчей):**

1. **Codex re-review B3-B2B P1–P4:** независимо повторить исходные
   false-PASS мутации, проверить оба harness и валидный GPU run.
   Только после положительного вердикта планировать интеграцию.
2. **DeepSeek Claude B1–B5 Round 2:** исправить B2 Unicode/CLI, изолировать
   B1 fault hooks, доказать штатный B4/B5 feature-on lifecycle и post-build,
   затем отправить в Codex. Не смешивать с B3-B2B.
3. **runE:** отдельно воспроизвести baseline vs patched, подтвердить root
   cause и провести regression disconnect/reload/quit; не маркировать
   pre-existing без A/B.
4. Продолжить **F4.0** до реального игрового DX12 кадра; после этого
   реализовать **F4-PBR** для полноценного raster/material lighting.
   Сохранять DX11/R4 fallback. B3-B2C/DXR не объявлять начатыми
   лишь из-за наличия планов или подготовленных тестовых патчей.

**Фиксация статуса:** изменения roadmap не выполняют сборку, тесты,
интеграцию, перенос патчей или независимую приёмку сами по себе.
Всё ниже, датированное 2026-10-25 и раньше, — исторический срез,
который следует читать вместе с этой таблицей.

### Историческая сверка выполнения DX12 и смежных направлений — 2026-10-25

**Основание:** коммиты, доступные в `origin/prypiat` на момент сверки
(HEAD `4b120ca81`), отчёты этапов и независимого аудита. Ниже отдельно
различаются **наличие кода в Git**, **PASS по представленным тестам** и
**полноценная приёмка игрового runtime**. Эта сводка исправляет устаревшие
`NEXT / NOT STARTED` в исторических таблицах ниже, но не переписывает
историю прежних решений. Сама правка roadmap не выполняет GPU-тесты.

| Этап | Фактическое состояние на дату сверки | Основание / оставшийся гейт |
|---|---|---|
| **F0–F2** — аудит, ADR, bootstrap | ✅ Реализованы и зафиксированы; standalone DX12 bootstrap проверялся ранее | `b396cfb4`, `786e34f8`, `5520318e`; исторические GPU-тесты F2, не игровой renderer |
| **F3.0–F3.3** — GPU-основа, GR Job System, CPU preparation | ✅ Standalone реализация и тесты по отчётам | `7ae6958d`, `09ca02357`, `5b0e4bbcb`; общий GR pool, Serial/Parallel/Verify, CPU snapshot; не FPS игры |
| **F3.5.0–F3.5.2B** — ADR, representative snapshot, visibility/compaction, ordering | ✅ Код закоммичен, CPU/ASan и regression PASS по отчётам | `8b1c0525`, `84de634e`, `d6592f97`, `3ac497d3`; порядок opaque/transparent/HUD относится к representative CPU-пути |
| **F3.5.3A/B** — sequential multi-list и parallel D3D12 command recording | ✅ Реализовано и проверено в standalone Smoke | `b5232622`, `53b82c5`; исторический H1 fence был **FAIL** для старой ревизии и позднее исправлен `d38ea6d8` |
| **F3.5.4A/B** — scaling/telemetry | ✅ Инструменты и 1–8 draw lists закоммичены; ⚠️ полный performance acceptance не заявлен | `de162d81`, `98a1e0fc`; p99 дополнен в Audit Hardening; синтетические замеры не являются FPS или доказательством ускорения |
| **F3.5.5A/B/C** — packet-driven GPU recording, batch planning, parallel packets | ✅ Реализовано для standalone | `aeb6a674`, `d02e2a44`, `6daa0b44`; игровой traversal по-прежнему не подключён |
| **F3.5.6A/B + Audit Hardening** — object CB, indexed geometry registry, fault/timing fixes | ✅ Код и независимый delta-review PASS; **CONDITIONAL PASS** для всего проверенного standalone HEAD | `0659303f`, `5b025aaa`, `738d143c`; M1/L1 закрыты, H1 исправлен; не тестировались все реальные driver failures/TDR, race detector и полный игровой runtime |
| **F3.5.6C** — material/PSO milestone | ◻ Отдельный PASS не подтверждён в просмотренной истории | Не подменять возможными минимальными ресурсами F3.6A; проверить нужный контракт перед полноценным F4 |
| **F3.6A — X-Ray Asset Bridge** | ✅ Закоммичен реальный standalone X-Ray OGF/DDS path; **не** игровой CoP renderer | `ccf0d22d`, `docs/dx12-f3-6a-xray-asset-bridge.md`; SoC static mesh, external loose files, textured fallback, без публикации игровых ресурсов |
| **F4.0-B1** — Jupiter geometry reader | ✅ CPU-only исходники закоммичены | `0ef5aaf2`; это декодирование данных уровня, не GPU игровой кадр |
| **F4.0-B2 / B2.3** — game camera snapshots и opt-in capture | ✅ CPU-only camera и opt-in интеграционный код закоммичены | `77e71e47`, `60f2030e`; наличие кода не доказывает полноценный runtime DX12 capture |
| **F4.0-B3A** — offscreen Jupiter validation | 🛠 Код standalone offscreen validation добавлен в Git; независимый GPU PASS этой сверкой не подтверждён | `4b120ca81`; отделять test harness от кадра игрового процесса |
| **F4.0 целиком** — реальный игровой DX12-кадр | ⬜ **НЕ ПРИНЯТ** | Нужны CoP/OGSR runtime, управляемая камера, видимая реальная геометрия и текстуры через DX12, depth/GBV, визуальный контроль и DX11 fallback |
| **V — Volumetric Clouds** | 🔴 **2026-11-01:** V2.3h-A03b-R02 — BLOCKED на CPU geometry gate; V2 не принят | Исторически V0 audit PASS WITH NOTES, V2.1 checkpoint, V2.2 — этап отчётов до R02; R03A (READ-ONLY) — следующий предложенный гейт; не блокирует F4/G |
| **P — PhysX ragdoll** | 🔎 Архитектура и исследование ODE locomotion; **ragdoll P0–P2 не приняты** | Аудит ODE не равен интеграции PhysX; целевой первый NPC ragdoll в OGSR DX11/R4, ODE fallback |
| **G — Hybrid DXR** | ✅ Архитектурное направление принято, реализация DXR не подтверждена | `22875a0`; Control/Metro — ориентиры, не готовый RT pipeline |

**Границы:** работающий в Smoke DX12 путь, включая параллельную запись и
поддержку настоящего SoC mesh, **не тождественен** замене R4 в игре.
В F4.0 пока нельзя ставить PASS из-за наличия CPU readers, capture adapters
или standalone GPU harness. Не считать тестовые hashes и p50/p95/p99
доказательством стабильного игрового FPS. Legacy DX11/R4 должен сохраняться.

### Историческая сводка статусов (снимок 2026-10-14/19; актуальная сверка — 2026-10-30 выше)

| Этап | Статус | Следующее действие |
|---|---|---|
| A · DLSS 310.9.1 | ✅ Подтверждён: сборка и запуск CoP | Сохранить baseline и логи |
| B1 · NIS submodule | ✅ Подтверждён в исходном roadmap | Зафиксировать SHA и проверить CI на чистой машине |
| B2–B4 · NIS в движке | ⬜ Не реализован | Проверить compute-dispatch и ресурсы SDK |
| C · Тюнинг DLSS | ⬜ Не реализован | Качество и семантика флагов |
| D · DX11 SSGI / Hi-Z | ⬜ Исследование | D0 → D1 → D2, решение по результатам |
| E · Техдолг | 🔎 Диагностика | Отдельные согласованные изменения |
| F · Нативный DX12 | 🛠 Standalone F3.6A и частичные F4.0-B1/B2/B3A в Git; игровой F4.0 ещё не принят | Актуальная сверка 2026-10-25 выше; следующий гейт — реальный игровой DX12-кадр |
| F3.2 · GR Job System (DX12 only) | ✅ Коммиты и тесты standalone подтверждены по отчётам | Сохранить GR только для DX12; DX11/R4/physics не подключены |
| F3.5 · Multithreaded Renderer Foundation | ✅ F3.5.1–F3.5.6B в Git; standalone CONDITIONAL PASS по независимому аудиту | Не выдавать за игровой runtime; закрыть непроверенные hardware fault/race/perf гейты |
| F3.5.1 · Representative snapshot prototype | ✅ Закоммичен `84de634e` | Далее выполнены F3.5.2A/B: culling, compaction, draw ordering |
| G · DXR | 🆕 План, зависит от F | G0: проверка возможностей GPU/драйвера; архитектурный ориентир Control + Metro закреплён 2026-10-22, реализация не начата |
| H · Многопоточность | ⬜ План | H0: базовые профили, H1 после RT-теней и отражений |
| I · World Streaming | ⬜ Концепция | I0: аудит X-Ray ресурсов/уровней и ownership |
| J · Высота / LOD / растения / тени | ⬜ Концепция | J0: инвентаризация LOD/detail/CSM |
| K · Seamless World | ⬜ Возможность на базе I/J | K0: формат регионов и A-Life/AI constraints |
| L · DX12/DXR portability / IX-Ray | ⬜ Предложение | L0: лицензии, сравнение API и границ интеграции |
| P · PhysX ragdoll (OGSR DX11 → IX-Ray) | 🆕 Архитектура принята, реализация не начата | P0 → P1: аудит скелета и headless PhysX ragdoll; P2: один игровой NPC в OGSR DX11 |
| V · Volumetric Clouds (DX12, параллельно F4.0) | 🔴 V2.3h-A03b-R02 **STOP / NOT ACCEPTED** (CPU audit 2026-11-01) | R03A — следующий READ-ONLY архитектурный этап (не начат); без HLSL-порта HCS1/HCS2; V-L и V-S остаются отложенными, V не блокирует F4.0 |

### Обязательные правила работы

1. **Не ломать рабочий DX11:** каждый эксперимент из F/G изолирован веткой/
   конфигурацией; первоначально DX11 остаётся основным путём и способом отката.
2. **Сборка ≠ работающий рендер:** отдельно фиксировать compile/link, запуск,
   кадр, переключение режимов, визуальные регрессии и производительность.
3. **Никаких оценок FPS без измерений:** сохранять сопоставимые сцены, разрешение,
   настройки, логи, GPU timings и скриншоты (желательно RenderDoc/Tracy).
4. **Текущие записи о выполнении не переносить автоматически:** статус меняется
   только при предъявлении проверяемого результата и коммита.
5. **Путь CoP:** `Game/Resources_CoP_1.6.02/gamedata/`; точные регистры
   имён файлов проверять по реальному дереву, а не по старым описаниям.

---

## Этап A — Обновить DLSS SDK 310.7.0 → 310.9.1

**Приоритет:** высокий · **Риск:** низкий · **Сложность:** дни
**Статус:** ✅ **выполнено 2026-10-14** — правка внесена, совместимость проверена,
сборка подтверждена.

### Что сделано

`Update_Components.cmd:26`:
```diff
-git clone --branch v310.7.0 --depth 1 ... 3rd_party/Src/NVIDIA_DLSS/DLSS
+git clone --branch v310.9.1 --depth 1 ... 3rd_party/Src/NVIDIA_DLSS/DLSS
```

### Проверка совместимости (по исходникам SDK, не предположение)

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

### Результат сборки

```
bin_x64\xrEngine.exe     31 463 424 Б (CoP)   валидный PE
bin_x64\nvngx_dlss.dll   310,9,1,0   NVIDIA Deep Learning SuperSampling
bin_x64\OpenAL32.dll
23 проекта, 1315+ объектов, ошибок компиляции — 0
```

### Рантайм-проверка CoP (подтверждено)

Билд установлен в `D:\Games\S.T.A.L.K.E.R. Call of Pripyat`; оригинальные
`xrEngine.exe` и `OpenAL32.dll` сохранены рядом как `.bak`.

| Проверка | Результат |
|---|---|
| `r_aa_dlss_version` | 310.9.1 |
| Апскейлинг Balanced | 1485x928 -> 2560x1600 |
| Пресеты DLSS | переключаются и применяются |
| FSR3 / TAA / SMAA | работают без падения |

### Что осталось

Косметическая ошибка A1 (`XR_3DA.vcxproj:395-397`, post-build `dbghelp.dll`).
EXE линкуется; падает только шаг копирования. Либо поставить
«Debugging Tools for Windows», либо пропатчить в форке (этап E).

### Откат

Вернуть одну строку и перезапустить `Update_Components.cmd`.

---

## Этап B — NVIDIA Image Scaling

**Приоритет (DX11-направление):** высокий · **Риск:** средний до первой
успешной проверки compute-шейдера · **Сложность:** недели
**Статус:** B1 ✅; B2–B4 ⬜.

### Зачем

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

### B1. Починить скрипт — ✅ СДЕЛАНО

```diff
-  git clone --branch v310.9.1 --depth 1 https://github.com/NVIDIA/DLSS.git 3rd_party/Src/NVIDIA_DLSS/DLSS
+  git clone --branch v310.9.1 --depth 1 --recurse-submodules https://github.com/NVIDIA/DLSS.git 3rd_party/Src/NVIDIA_DLSS/DLSS
```

Активация сабмодуля в существующем дереве (без перекачки 2 ГБ):
```cmd
git -C 3rd_party\Src\NVIDIA_DLSS\DLSS submodule update --init --recursive
```

### B2. Compute-гейт и загрузка NIS HLSL

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

### B3. Обёртка и ресурсы

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

### B4. Интерфейс

Добавить в `pp_aa_mode_token` (`xrRender_console.cpp:37-47`) и `enum`
(`xrRender_console.h:264-272`) значение `NIS`.

**Обязательно синхронизировать Lua-UI** —
`Game/Resources_CoP_1.6.02/gamedata/scripts/ui_mm_opt_video_adv.script`:
строки `:27-29` объявляют `dlss_quality` / `dlss_preset` / `fsr3_quality`;
строка `:174` читает `r_aa_mode`. Это **реальный путь в GitHub**
(нижний регистр `scripts`), старое написание `Scripts/` было ошибкой документации.
Новый режим нужно поддержать как в C++ токенах, так и в Lua-UI.

### Критерии приёмки

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

### Риски

| Риск | Оценка | Митигация |
|---|---|---|
| Тег NIS/повторяемость CI | Средняя | Закрепить проверенный submodule SHA и выполнить clean checkout |
| Первый `.cs` в проекте — неисследованный путь | Средняя | B2.0: отдельный тест `Dispatch` + UAV + RenderDoc |
| HDR/linear/PQ и коэффициенты NIS | Средняя | Следовать версии `NIS_Config.h`, визуальный тест |
| Двойной sharpen с CAS | Средняя | Не включать CAS поверх NIS без явного выбора |
| Смена render/display размеров | Средняя | Рестарт/пересоздание RT по контракту |
| Меню опций рассинхронизируется | Низкая | Правка `ui_mm_opt_video_adv.script` в том же коммите |

---

## Этап C — Тюнинг текущего DLSS

**Приоритет:** средний · **Риск:** низкий · **Сложность:** дни

### C1. `AlphaUpscaling`

`RenderTargetPhaseAA.cpp:341`. Проверить, поддерживается ли флаг выбранной
версией DLSS и соответствует ли он реальной семантике alpha на входе.
**Не включать вслепую:** сравнить траву, дым, дождь и UI до/после.

### C2. `MVJittered`

`:337`. Наличие camera jitter (`:418-419`) само по себе **не доказывает**,
что motion vectors передаются jittered. Сначала сверить генерацию `rt_Velocity`,
знаки `InMVScale*` и контракт NGX, затем решить вопрос с флагом.

### C3. `DoSharpening` — требует решения

`:332`. Без него `InSharpness` игнорируется (`:316-317`), но движок
имеет собственный CAS (`ProcessCAS`, `:611`), вызываемый в `PhaseAA()`
для всех режимов кроме SMAA (`:966-967`).

Варианты: оставить внешний CAS; использовать DLSS sharpening, если режим
поддерживается активной версией SDK; либо предоставить явный выбор
в настройках. **Критерий:** одна активная стадия sharpening, а не
предположение, что включённый флаг обязательно улучшает картинку.

### C4. Задокументировать DLAA как дефолт

`xrRender_console.cpp:58`. Пользователь может не понимать, почему
при «DLSS» нет апскейлинга. Отражено в `ui_mm_opt_video_adv.script`.

### Критерии приёмки C

Флаги проверены по контракту соответствующего SDK; видимые различия
сохранены скриншотами в статике и движении, артефакты не усилились,
производительность не ухудшилась непредсказуемо. Изменения независимы
и могут быть откатаны по одному.

---

## Этап D — SSGI / Hi-Z на compute-шейдерах

**Приоритет:** высокий по ценности, **высокий** по сложности · **Месяцы**

**Это один из путей получить приближение к трассируемому освещению
в DirectX 11.** Hi-Z/DDA — **screen-space ray marching** с ограничениями
вне экрана и за заслоняющими поверхностями; это **не DXR** и не замена
аппаратным acceleration structures. Hardware RT в выбранной D3D-архитектуре
требует перехода на DX12/DXR (направление F–G).

### Что уже есть

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

### Чего ещё нет

- Ни одного `.cs`-файла (блокер; снимается этапом B).
- Metallic/emissive в 4 MRT.
- Hi-Z пирамиды.
- Равномерного доступа к треугольникам.
- Денойзера.

### Подэтапы

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

### Критерий приёмки

Для D0–D2: воспроизводимая маска попаданий на нескольких сценах,
сопоставление с depth и нормалями, известные ограничения на краях экрана.
Для D3–D5: видимое улучшение непрямого освещения без заметного ghosting,
управляемая стоимость в GPU ms и без регрессии исходного DX11 при выключенном
эффекте. Любой шаг может завершиться решением **не продолжать**.

### Риски

| Риск | Оценка |
|---|---|
| Hi-Z требует корректного доступа к глубине и форматов compute/UAV | Средняя |
| Screen-space не видит невидимую геометрию | Высокая, алгоритмический предел |
| Off-screen BVH/гриды (D6) — отдельный большой проект | Высокая |
| Temporal/reprojection, leakage/ghosting | Высокая |
| Reconstruction позиции/нормали и coordinate spaces | Средняя |

### Рекомендация

Начинать только с D0–D2. Если Hi-Z + DDA не покажут полезного качества и
приемлемого GPU time — остановиться. Для достижения DXR **не требуется**
завершать D3–D6. Часть screen-space наработок пригодится позднее, но
программный BVH не является обязательной ступенью перед BLAS/TLAS.

---

## Этап E — Гигиена репозитория

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

## Этап F — Нативный DX12 (standalone реализован; игровой DX12 ещё не принят)

**Цель:** запустить CoP с настоящим D3D12-backend и сохранить рабочий DX11.
**Приоритет:** стратегический для DXR · **Риск:** высокий · **Оценка срока:**
только после F0–F2, без обещания «готово за N месяцев».

**Важное архитектурное наблюдение:** `xrRender_R4` собирает файлы из
`xrRenderPC_R4`, `xrRender` и `xrRenderDX10` **в одну статическую библиотеку**;
`RImplementation`, `HW`, `CBackend` и ресурсные классы сильно привязаны к D3D11.
`RenderFactory` — фабрика игровых/render-объектов, **не готовый RHI**.
Решение «новый `xrRender_R5`» принимать **после F0**, не заранее.

| Подэтап | Работа | Гейт / проверяемый результат |
|---|---|---|
| **F0 — аудит** | Карта зависимостей `CHW`, `CBackend`, `CResourceManager`, `CRT`, `CTexture`, `dx10StateManager`, `CRender`, `dxRenderDeviceRender`, оконного цикла и статической линковки. Инвентаризация HLSL, 64 RT, deferred/forward/HUD/particles, DX11 SDK-обёрток. | `docs/dx12-port-audit.md`: таблица классов, lifetime, владельцев и D3D11-вызовов; предлагаемая граница backend; сравнение альтернатив по риску. **Никаких переписываний.** |
| **F1 — проектирование** | Выбрать: отдельная build-конфигурация / временный DX12 harness / поэтапный API separation. Описать интерфейсы device, swapchain, command recording, shader & resources и правила совместного сосуществования. | ADR с явными правилами ABI, ownership, переключения/отката, статической линковки. DX11 baseline собирается без изменений поведения. |
| **F2 — DX12 bootstrap** | D3D12 device, DXGI swapchain, graphics queue, allocators/command list, frames-in-flight, fences, RTV/DSV/SRV/UAV descriptor heaps, resize, debug layer, DRED. | Независимый DX12-путь стабильно выполняет `Clear → Present`, корректно переживает resize/minimize, без validation errors. **Это ещё не игровой рендер.** |
| **F3 — ресурсы и геометрия** | Upload/default heaps, constant/vertex/index buffers, PSO, root signatures, descriptors, resource transitions/barriers, простые текстуры. Раздельно исследовать D3D11-legacy shader state и `D3DVERTEXELEMENT9`. | Текстурированный mesh из реального формата OGSR, depth test, camera movement, без leaks и GPU faults. |
| **F3.5 — multithreaded renderer foundation** | До массового переноса кадра: минимальные immutable render snapshots/draw packets, дизайн frame scheduling, per-worker/per-frame allocators и lifetime; небольшой тестовый parallel command recording path с последовательным fallback. Детали и гейты ниже. | Повторяемые тесты корректности на 1/2/4/8 workers, CPU/GPU timeline, p95/p99, sync overhead; нет data races, GPU errors, регрессий DX11. Сложную job system не внедрять без измерений. |
| **F4.0 — первый игровой DX12 кадр (ранний vertical slice)** | После F3.5.6A–C — ограниченная интеграция реальных ассетов и камеры CoP в игровой DX12-путь, без ожидания полного F4. Сначала геометрия и depth, затем базовые текстуры/материалы и навигация. | Открывается реальная локация OGSR (целевая — Кордон, допустимая альтернатива — Припять) именно через DX12; есть видимые объекты, управляемая камера, depth и базовые текстуры; runtime/debug/GPU validation и DX11 fallback проверены. Не объявлять весь F4 завершённым. |
| **F4 — игровой кадр** | Перенос/de-risk основных проходов: sky, G-buffer, CSM/sun, dynamic lights, прозрачность, HUD/оружие, particles, postprocessing. Сохранение нормалей `rt_Position.xy`, gloss `rt_Color.a`, motion vectors, view depth/material ID. | Одинаковая тестовая локация реально загружается и рендерится на DX12 и DX11. Скриншоты ключевых буферов и визуальный diff. |
| **F5 — temporal и совместимость** | DX12-обёртки для выбранных DLSS/FSR, jitter, history reset, upscale input/output, прицел 3DSS, ImGui, Tracy/PIX/RenderDoc, HDR/ReShade там, где поддерживается. | DLSS/FSR fallback без device loss, нет ошибок resize/async lifetime и серьёзного ghosting. |
| **F6 — стабилизация** | Измеренная настройка/расширение прототипа parallel command recording из F3.5, memory budgeting/residency, CPU/GPU profiling, regression suite. | Без известных GPU validation errors, сравнение GPU ms/CPU ms/VRAM на нескольких сценах, контролируемый rollback DX11. |

**Актуальный статус подэтапов F — 2026-11-06** (ветка `prypiat`; сверка по
исходникам, отдельным независимым review и Git-коммитам до `1c2b5752`;
VFS Readiness R2 — внешний evidence-пакет, а не production-изменение):

| Подэтап | Актуальный статус | Подтверждение и оставшиеся ограничения |
|---|---|---|
| **F0 — аудит** | ✅ PASS | `docs/dx12-port-audit.md`; исходный этап завершён. |
| **F1 — проектирование** | ✅ PASS | `docs/dx12-architecture-adr.md`; DX12 architecture ADR принят. |
| **F2 — bootstrap** | ✅ PASS для standalone | DX12 device/queue/fence, Debug Layer, GBV/DRED, resize/shutdown и smoke CI; исторические аппаратные результаты приведены в `docs/dx12-f2-first-frame.md`. Это **не** игровой renderer; реальный TDR NOT TESTED. |
| **F3 — GPU-ресурсы и геометрия** | ✅ Реализовано в standalone; **не** игровой renderer | DX12 GPU geometry/registry, CB, indexed draw, F3.6A OGF/DDS asset bridge и реальные X-Ray ресурсы; результаты standalone Smoke и J1.1. Массовый игровой traversal и текстурный кадр ещё не приняты. |
| **F3.5 — многопоточная основа** | ✅ Standalone foundation реализована | GR Job System на enkiTS, CPU preparation, immutable snapshots/packets, parallel D3D12 command-list recording и serial fallback. **В игровом GameDx12Bridge GR workers ещё не используются**; подключение запланировано на F4.0-J2/J3. Standalone perf ≠ игровой FPS. |
| **F3.5.6C — material/PSO** | ◻ Отдельный PASS не подтверждён | Перед полным F4 проверить исходный material/PSO contract либо формально принять достаточный минимальный эквивалент. |
| **F4.0-G1B — in-process bridge** | ✅ Source-only PASS, в Git | `bb62c00ac`: opt-in DX12 bridge, служебный single-list submit, без игрового Draw; full device lifetime diagnostics остаётся UNVERIFIED. |
| **F4.0-J1.1 — real-asset GPU Draw** | ✅ Независимый standalone GPU PASS, в Git | `a1c751274`: Visual 9 и 315 из настоящих `level` + `level.geom`, fence/readback/PNG и GBV; используется изолированный GPU adapter и тестовая камера, **не** игровой Draw. |
| **F4.0-J1.2A — игровой geometry adapter** | ✅ Source-only + owner-side lifetime PASS, в Git | `c4a95c39`: VFS/geometry, world constants, lifecycle/epoch и quarantine, feature-off/on Release x64 PASS. Использованы cached sibling libraries: clean `Engine.sln`/Debug x64 NOT TESTED; полноценный игровой runtime не запускался. |
| **F4.0-J1.2B — первый in-game Draw** | ✅ **Source-only PASS, в Git; in-game Draw BLOCKED** | `3a9b7c926`: реальный DX12 RTV RGBA8, D32 depth/DSV, barriers, clear, viewport, owner submit/fence/readback, WIC PNG; fence/device identity, COM lifetime и command-list recovery независимо приняты. На момент source-only milestone отсутствовал полный payload; Asset Recovery R3 (2026-11-06) подтвердил его VFS/CPU-совместимость. **IN-GAME DRAW всё ещё BLOCKED:** не проверены OGSR game-level boot, подтверждённый runtime caller/visual placement и GPU readback. R4 Present, G1-M parity и игровой level lifecycle NOT TESTED. |
| **F4.0-J1.2B — VFS Readiness R2** | ✅ **Evidence-only PASS (2026-11-06)** | Исправлен вызов `CLocatorAPI::exist` через alias/resolved path; в проверенной CoP-конфигурации положительный/отрицательный контроли и `file_list` согласуются, но полноценный игровой payload отсутствует. Нового in-game GPU Draw, production diff и тестов на других установках нет; см. детальный gate ниже. |
| **F4.0-J1 — Asset Recovery R3** | ✅ **PASS (VFS + CPU-only compatibility); runtime gate PENDING** | Новая проверка оригинальной CoP: 41 архив, `level`/`level.geom[x]`/`level.ltx`/`level.cform`/`level.ai`/`level.game`/`level.spawn`/`level.hom` доступны из VFS; `XRLC_version=14`, `CFORM=4`, sector-root linked hierarchy и `ReadVisual` прошли на Jupiter leaf 275, Zaton 405, Pripyat 1002. Настоящий OGSR level boot и GPU readback **NOT TESTED**; J1 Draw не принят. |
| **F4.0-J1 — First In-Game Draw R1 / runtime blocker** | 🔴 **BLOCKED (2026-11-06)** | Feature-off/on baseline Release x64 builds PASS по логам; изолированный Zaton runtime загрузил level resources, но скриптовое исключение в `bind_stalker.script:125`/`set_callback` остановило game boot до `bReady`. Отсутствующий в C++ enum/Lua export `callback.actor_before_death` подтверждён; полный источник/совместимость gamedata не подтверждены. Caller откатан, GPU Draw NOT TESTED; детальный гейт ниже. |
| **F4.0-J1 — Runtime Compatibility R2** | ✅ **DX11/R4 runtime PASS with notes** | Изолированный runtime Zaton UI New Game, Jupiter save load, level reload и реальный first-person HUD; Lua active callbacks 19/19 по evidence, 9 loose scripts/21 configs и оригинальные CoP archive scripts. feature-off clean baseline rebuild PASS; feature-on clean/runtime в R2 NOT TESTED; clean shutdown NOT CONFIRMED; DX12 first GPU Draw NOT TESTED. |
| **F4.0-J1 — First In-Game DX12 Readback R2** | ✅ **GPU evidence PASS / source review PENDING (2026-11-06)** | Внутри OGSR успешно выполнен настоящий GPU Draw статического visual 9 Jupiter (545 verts/1314 indices) с игровым VP, owner fence=3, RTV/DSV и PNG readback; 10 623 non-clear pixels независимо проверены. Код только локальный, 3 production files +282/-5; до Git нужны небольшие поправки и независимый Codex review. |
| **F4.0-J1 — Diagnostic Hardening R2A** | 🔴 **Codex REQUEST CHANGES (P2); GPU evidence PASS (2026-11-07)** | Техлид audit PASS WITH NOTES и ранее пройденные Jupiter Draw/readback, pixel oracle, GBV не аннулируются. Codex доказал source-баг: `OnDeviceReset()` увеличивает bridge и geometry epoch, но не обновляет `diagnosticLevelEpoch`; до первого Draw после reset identity mismatch сохраняется. R2A production patch HOLD, следующий R2B reset-recovery fix + повторный review. |
| **F4.0-J1 — Reset Recovery R2B / production integration** | ✅ **source integrated + post-merge GPU ACCEPTED WITH NOTES (2026-11-08)** | MSVC Release x64 feature-off/on Rebuild and d3d12 imports 0/1, Zaton R4, Jupiter visual 9 1 Draw/fence3/nonclear 10 623 px; reset-before harness 7 430 px, reset-after no repeat, GBV PASS, R4 Present, teardown/shutdown. Fresh zero-coverage postmerge NOT TESTED (carried R2B). |
| **F4.0-J2A — CPU hierarchy/GR packet candidate** | 🔴 **REQUEST CHANGES; J3 HOLD (2026-11-08)** | Positive evidence: Jupiter sector129/root17431, 5 instances / 4 visible packets, stable EC3EAADD55DFCD6C, Serial=Parallel=Verify and 17/17 oracle PASS independently rebuilt. P2 independently reproduced: accepted depth79 despite max64. P3: duplicate empty-first OGF_CHILDREN_L accepted. CPU-only prototype not committed. |
| **F4.0-J2A R2 — hierarchy parser hardening** | 🔴 **Codex REQUEST CHANGES (2026-11-08); J2A/J3 HOLD** | Targeted parser fixes depth DAG / seenChildren and CPU oracle 26/26 remain valid, but full cumulative patch contains 3 independent P2: exponential DAG traversal without visit budget, sectors misidentified when chunks reordered, silent global 100 000 instance truncation causing wrong sector selection; 3 P3: duplicate dx12packetplan.cpp MSBuild (MSB8027/LNK4042), false-positive compressed fixture, VFS reader exception leak. J2A R3 required; no production commit. |
| **F4 — полный игровой renderer** | ⬜ **НЕ ПРИНЯТ** | F4.0 не завершён: нужны настоящий игровой DX12 кадр, управляемая камера, depth/текстуры, затем основные raster-проходы и обязательные runtime/rollback проверки. |
| **F5 — temporal / upscaling DX12** | ◻ Не принят / впереди | Игровой DX12 temporal history, jitter, DLSS/FSR integration и history reset не подтверждены. |
| **F6 — стабилизация** | ◻ Не принят / впереди | Измерение реального игрового CPU/GPU frametime, memory residency и FPS; standalone profiling не является F6 PASS. |

**Ближайшая работа после Asset Recovery R3 PASS:** создать отдельный тестовый OGSR runtime без модификации оригинальной CoP, подтвердить штатную загрузку Jupiter и DX11/R4 baseline, затем выполнить строго opt-in **F4.0-J1 serial in-game DX12 Draw** для provenance `sector 0 → root 16926 → MT_NORMAL leaf 275` с настоящей игровой камерой, identity world, depth, owner submit/fence/readback и R4 Present. Это отдельный GPU gate, не следствие CPU PASS. Затем F4.0-J2/J3 — подключить существующий GR Job System к игровой CPU-подготовке и параллельной записи command lists. R4/DX11 сохраняется; ни F4.0, ни полный F4 пока не приняты.

### F3.2 — GR Job System: сначала только DX12 (решение 2026-10-18)

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

### F3.5 — Multithreaded Renderer Foundation: архитектура ДО массового переноса F4

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

### F4.0 — ранний вертикальный срез: первый игровой кадр на DX12 (PLANNED)

**Новая приоритетная цель после принятия F3.5.6A–C:** как можно раньше
увидеть реальную локацию **S.T.A.L.K.E.R.: Call of Pripyat / OGSR**
в нашем нативном DX12-рендерере. F4.0 — промежуточный, строго ограниченный
гейт **до полного F4**, а не обещание завершённого движка или FPS-прироста.

**Порядок работ и проверяемые результаты:**

1. **F4.0-A — integration audit (READ-ONLY).** Проследить путь от игрового
   level loader, renderable/visual и камеры до R4 traversal и frame submit;
   составить карту форматов реальной геометрии/текстур, ownership, shader
   contracts и точек включения DX12. Решить, как подключить экспериментальный
   путь без подмены DX11 или копирования mutable игрового состояния в workers.
2. **F4.0-B — first real geometry frame.** В контексте запущенной игры
   загрузить и нарисовать на DX12 реальную статическую геометрию выбранного
   уровня OGSR, согласовать игровую камеру, World/View/Projection,
   depth test и базовый raster pass. На этом промежуточном гейте временный
   плоский цвет допустим, **но синтетическая сцена в xrDX12Smoke не считается
   игровым кадром**.
3. **F4.0-C — textured navigable preview.** Добавить минимально необходимый
   путь существующих OGSR текстур и материалов (включая документированную
   цветовую/sRGB-политику), базовый sampler/descriptor binding и управление
   камерой в уровне. Без обязательной конвертации всего набора игровых ассетов.
   Unsupported материалы временно показывать диагностически, а не выдавать
   за корректно отрендеренные.
4. **F4.0-D — evidence and rollback gate.** Проверить DX12 draw/dispatch
   действительно в игровом процессе (command captures / GPU markers),
   корректность геометрии, depth и текстур; записать скриншоты/контрольные
   точки, ошибки Debug Layer/GBV, resize/reload/shutdown, используемые
   ресурсы и список неподдерживаемых проходов. Сохранить рабочий DX11/R4
   и проверяемый переключатель/откат экспериментального DX12.

**Критерий F4.0 PASS:** игра загружает реальную локацию через существующий
игровой data path, камера движется, реальные mesh и базовые текстуры
отрисовываются **через D3D12**, depth работает, нет известных GPU
validation errors, DX11 baseline не сломан. Первый геометрический кадр
F4.0-B можно принимать как частичный milestone, но **не как весь F4.0**.

**Зависимости:** завершить и принять F3.5.6A (object transforms),
F3.5.6B (geometry handles/VB/IB), F3.5.6C (material/PSO state) либо
документировать минимальный эквивалент и причину отклонения. Не блокировать
F4.0 ожиданием полного F3.5.7/F3.5.8: нужный для preview минимальный
texture/SRV/sampler path входит в F4.0-C; развитую texture/material
совместимость вести отдельными этапами. Реальный integration audit может
выявить дополнительные обязательные зависимости, которые нужно явно
согласовать до кодирования.

**Вне F4.0:** sky/полное освещение/CSM, прозрачность, HUD/оружие,
particles, сложный postprocess, temporal upscalers, RT-тени/отражения/GI,
streaming, оптимизация FPS. Их перенос остаётся в полном F4/F5/F6/G.
До прохождения полноценных гейтов F4–F5 DX12 не выдавать пользователям
как штатный завершённый игровой режим.

**Состояние на 2026-10-30:** в `prypiat` после B3A закоммичены также
`F4.0-B3-B1` (offscreen replay игровой камеры) и `F4.0-B3-B2A`
(IPC/CPU receiver). Изолированный B3-B2B GPU consumer ожидает повторного
Codex review после P1–P4; отдельный Claude B1–B5 patch получил
`REQUEST CHANGES`. Ничего из этого не подтверждает завершение F4.0.

### F4.0-G1B / J1.1 — принятые промежуточные результаты

- **F4.0-G1B Round 2.4 (source-only): принят.** Коммит
  `bb62c00ac3109464ff1304d3cfa46c014b10bf63`,
  `feat(dx12): add experimental in-process game bridge`: 10
  production/build-файлов, 2254 insertions. Это source-only milestone:
  in-process bridge не является игровым DX12 renderer.
- **F4.0-J1.1 (standalone real-asset GPU Draw): принят независимым review.**
  Source-only коммит `a1c75127427bc05597adba0711f8092cad5de256` добавляет
  `dx12xraylevelasset.h/.cpp`. Visual 9 (545/1314/438) и Visual 315
  (90/333/111) отрисованы из strict `level` + `level.geom` через
  существующий GPU registry. Подтверждены fence 2, readback/PNG SHA-256,
  Debug Layer/GBV без ERROR/CORRUPTION и
  `GetDeviceRemovedReason() == S_OK`. Это isolated adapter с тестовой
  камерой, **не** игровой кадр.
- **F4.0-J1.2B (исторический статус на 2026-11-03): BLOCKED.** На тот момент полный совместимый Jupiter payload отсутствовал в проверяемой установке; требования к `level.graph`/`level.gct` ещё не были уточнены. **Asset Recovery R3 от 2026-11-06 заменяет этот результат:** core CoP level files обнаружены, `level.ai`/`level.game` доступны, совместимый sector leaf найден. Но реальный игровой `DrawIndexedInstanced` всё ещё не выполнен и J1 runtime остаётся BLOCKED.
- **R4 Present / G1-M parity: NOT TESTED.** F4.0 renderer не завершён;
  standalone GPU evidence не заменяет игровой runtime.

### F4.0-J1.2A Round 2 — статус 2026-11-03

- **F4.0-J1.2A Round 2 (source-only): принят.** Коммит
  `c4a95c39e3e994e21b6768985b1005612ae5ec71`,
  `feat(dx12): integrate Jupiter geometry with game bridge`: 6
  production/build-файлов. Закрыты P1 unknown completion/quarantine,
  visual ID 0, небезопасное Draw confirmation и epoch/generation reset.
  Независимый owner-side GPU lifetime/quarantine — PASS в проверенных
  сценариях, без аппаратного TDR. Feature-off / feature-on Release x64
  project builds — PASS (без / с импортом `d3d12.dll`); CPU adapter 17/17,
  strict reader 114/114, visual ID 0 — 14/14, lifecycle 27/27, bridge
  failure modes — 6/6. Использованы cached sibling libraries с
  неподтверждённой полной ABI/commit-совместимостью: clean `Engine.sln`
  и Debug x64 — NOT TESTED. Это source-only milestone, **не** игровой
  DX12 renderer.
- **F4.0-J1.2B Round 2 (source-only): принят.** Коммит
  `3a9b7c926be4a19e0a3ec579488e1a2d19ed7439`,
  `feat(dx12): add experimental in-game offscreen render pass`: 8
  production/build-файлов. Реализованы offscreen RTV `R8G8B8A8_UNORM`,
  D32 depth/DSV, barriers, clear, viewport, owner command submission,
  fence, readback и WIC PNG под feature guard. Независимо приняты fence/device
  identity, COM lifetime `SavePng`, command-list recovery и owner-side
  GPU quarantine; offscreen GPU clear/readback — PASS, feature-off/on
  Release x64 project builds — PASS. **IN-GAME DRAW остаётся BLOCKED:**
  на момент J1.2B source-only проверки отсутствовали полный Jupiter payload и подтверждённый visual placement/caller; R3 (2026-11-06) доказал доступность payload и CPU drawable leaf, но runtime caller/placement и реальный игровой readback ещё не проверены. R4 Present, G1-M parity
  и полный game level lifecycle — NOT TESTED. Это source-only milestone,
  **не** игровой DX12 renderer. Использованы cached sibling libraries:
  clean `Engine.sln` и Debug x64 — NOT TESTED.
- **J1.2B P3 ownership boundary.** При неизвестном GPU completion текущий
  Bridge обязан удерживать owner fence/device через process-lifetime
  quarantine. Захваченные внутри `GameDx12LevelGeometry` `ComPtr` сами по
  себе не гарантируют сохранение ресурсов после уничтожения geometry; для
  текущего Bridge это не production use-after-free, поскольку geometry
  находится в function-static `BridgeStateData`, а `QuarantineObjects()`
  отдельно удерживает fence/device. Любой новый caller обязан сохранять
  fence/device до подтверждённого completion либо до process exit; его
  ownership contract проверяется отдельно.
- **F4.0-J1.2B runtime — BLOCKED.** Следующий рубеж — полный совместимый
  игровой payload, реальный visual/sector ownership, игровая камера и первый
  настоящий indexed Draw с readback. Без этих компонентов in-game GPU PASS
  не заявляется.

### F4.0-J1.2B VFS Readiness R2 — evidence gate (2026-11-06; ACCEPTED)

- **Причина R2:** первоначальный диагностический probe ошибочно вызывал одноаргументный `CLocatorAPI::exist("$level$/level")`, не разрешая алиас. Старые результаты `MISS` были **TEST INVALID**. R2 применяет `FS.update_path(resolved, "$level$", name)`, `FS.exist("$level$", name)` и `FS.exist(resolved)` с проверкой согласованности.
- **Проверенный runtime:** production `CLocatorAPI` и реальный `fsgame.ltx` проверенной установки CoP, уровни `jupiter`, `zaton`, `pripyat`. Положительный контроль `level.puddles`: **HIT** (4038 / 3591 / 6680 байт соответственно, loose file); отрицательный контроль: **MISS**; `FS.file_list($level$)` — **ровно один файл на уровень**. Обязательные `level`, `level.ltx`, `level.geom[x]`, `level.cform`, graph/AI/HOM и игровые spawn/game/gct — **FILE NOT FOUND IN VFS**.
- **Архивы:** `ARCHIVE_COUNT 0` для проверенного корня установки (`gamedata.db*`/`.xdb`); проверка **не рекурсивная**. Другие установки, внешние архивы и восстановленный полный level payload — **NOT TESTED**; нельзя обобщать отсутствие архивов за пределы проверенной конфигурации. SoC `gamedata.db0..dbd` не использовать как замену CoP.
- **Независимая приёмка evidence:** `F4.0-First-InGame-Draw-Readiness-R2-All-Files.zip`: ZIP CRC 14 файлов PASS; `MANIFEST.sha256` 13/13 PASS; `SHA256SUMS.txt` 10/10 PASS; JSON-manifest 9/9 PASS. SHA-256 R2-отчёта `4C5D7A42057FA8C7A2CFAD7672AEA65E6AC872B2A8C18CF25EB2E6BFE0DF8039`, объединённого VFS-лога `81E74377ABD771F32492568F4D5242ED914B4613C1E4EDBBE420CBA1183B742C`. Отчёты — **evidence-only**; независимый повторный запуск Windows probe и игровой GPU test этим milestone не подтверждаются.
- **Подтверждено только исходниками:** `sector.root_id` может ссылаться на hierarchy, `FHierrarhyVisual::Load` поддерживает `OGF_CHILDREN_L` и `OGF_CHILDREN`, а `xraylevel12::ReadVisual` принимает только `MT_NORMAL`; static sector geometry использует authored world-space placement и identity world. `Device.mFullTransform` — игровой row-vector View × Projection с transpose на границе HLSL. Реальные root/leaf ID, положение во фрустуме и визуальная корректность **NOT TESTED** без уровня.
- **Следующий блокирующий gate (asset recovery):** восстановить/проверить официальные данные **Call of Pripyat** в используемой установке, повторить R2 VFS probe до **HIT** для всех обязательных level components; проверить совместимость `XRLC_version` и штатную загрузку уровня OGSR. Не создавать фиктивные файлы, не подмешивать SoC, не считать обнаружение файла успешным декодированием/загрузкой.
- **Промежуточное состояние (сообщение пользователя, 2026-11-06; UNVERIFIED):** оригинальная Call of Pripyat установлена отдельно от разрабатываемого OGSR-движка; экспериментальный engine в игровую папку не переносился. Предыдущий R2 probe уже использовал путь этой установки, поэтому его старый лог **не может** подтвердить состояние **после** новой установки. Следующий шаг — новый read-only запуск корректного VFS-probe из корня CoP, новые логи с отдельными SHA-256, контроль реальных архивов (не ограничиваться нерекурсивным `ARCHIVE_COUNT`), штатный запуск оригинальной игры и подтверждение совместимости payload перед любой интеграцией. До полученных новых доказательств asset gate — **PENDING / IN-GAME DRAW BLOCKED**. Путь локальной установки и игровые архивы в Git не публиковать.
- **После asset gate — J1 serial-first:** взять **реальный** `fsL_SECTORS → root_id → drawable MT_NORMAL leaf` (с проверкой формата children, границ, geometry container и provenance), игровую камеру и identity world, сделать **один opt-in owner-thread** `GameDx12Bridge_RenderOffscreenDiagnostic` с подтверждённым fence, depth, readback PNG, ненулевым покрытием пикселей и сохранённым R4 Present; отдельно проверить P3 quarantine/lifetime, Debug Layer/GBV и негативные сценарии. Только фактический in-game GPU readback допускает J1 PASS.
- **Текущий вердикт:** **VFS R2 PASS, F4.0-J1 IN-GAME DRAW BLOCKED.** Никаких новых production runtime изменений и доказанного игрового кадра. J2/J3 (GR game parallel prep/recording) остаются после J1, CLAS/PTLAS/PBR/DXR не задерживают J1.

### F4.0-J1 Asset Recovery R3 — восстановление CoP level payload (2026-11-06; ACCEPTED)

- **Вердикт:** **ASSET GATE PASS** для доступности CoP-ресурсов через production `CLocatorAPI` и ограниченной CPU-совместимости с `xraylevel12::ReadVisual`. Это **не** PASS игрового OGSR runtime, DX12 renderer или реального GPU Draw.
- **Проверенная оригинальная установка:** отдельная CoP 1.6.02; экспериментальный OGSR туда не копировался. Рекурсивно обнаружено **41 архивное хранилище**, в том числе `levels/levels.db0` и `levels/levels.db1`; core assets реально возвращаются VFS как архивные. Интерактивный запуск оригинальной игры **NOT TESTED**.
- **VFS контроллер:** правильные `FS.exist("$level$", name)`, `FS.update_path` и `FS.exist(resolved)`; положительный `level.puddles` HIT, отрицательный контроль MISS на всех трёх локациях. `FS.file_list($level$)`: Jupiter **114**, Zaton **75**, Pripyat **87** файлов. `level`, `level.ltx`, `level.geom`, `level.geomx`, `level.cform`, `level.ai`, `level.game`, `level.spawn`, `level.hom` — HIT. `level.graph` и `level.gct` — MISS, но не являются обязательными файлами проверенного CoP loader path; не считать это asset blocker.
- **CPU compatibility:** `XRLC_version=14` (expected 14), `CFORM=4` (expected 4), `fsL_VISUALS` и `fsL_SECTORS` присутствуют. Совместимый visual: **Jupiter sector 0 → root 16926 → linked hierarchy → MT_NORMAL leaf 275 (38 vertices, 111 indices, stride 32)**; Zaton sector 0 → root 16564 → leaf 405 (1132/2400/32); Pripyat sector 1 → root 15895 → leaf 1002 (16/42/32). Это реальные ID из level payload, а не синтетические тестовые mesh. Некоторые другие leaf отвергаются как `unsupported vertex declaration`: ограничения reader остаются.
- **Независимая проверка evidence ZIP:** `F4.0-J1-Asset-Recovery-R3-All-Files.zip`, **22 файла**, CRC PASS, `MANIFEST.sha256` **21/21 PASS**, `SHA256SUMS.txt` **19/19 PASS**, JSON manifest **18/18** совпадают по размеру и SHA-256. R3 report SHA-256 `9CA1FBAA0D66F184640C593214750F67D702C33BFE9F9E75C1614CA85F76BF9C`; VFS all log `AD1F66F3A3F124B6CAA627062BE8C362EC93F4557E8FB27FA904F11BDEFE47DF`; compatibility all log `B8A58D2F4F27A3387029B6867296F78274AFB0B86D5AF2D8322077380D5251B5`. Успех отражён в Windows logs и исходниках CPU probes; независимый повторный запуск Windows executable на другой машине **NOT TESTED**, build использовал cached sibling libraries (clean game build **NOT TESTED**).
- **Обязательный следующий runtime gate:** создать изолированную тестовую OGSR среду с легальными CoP ассетами/путями, без замены оригинальных файлов; подтвердить `fsgame.ltx`, обычную загрузку Jupiter в **OGSR R4/DX11**, `g_pGameLevel->bReady`, camera snapshot и совместимость native gameplay. Затем подготовить минимальный opt-in J1 serial-first caller: только **реальная игровая камера**, identity world для статической geometry, предварительно проверенная принадлежность `sector → root → leaf`; owner-thread, один draw, GPU fence, offscreen readback/PNG и ненулевое pixel coverage, проверка depth/GBV/device removal, R4 Present сохранён. Отдельный patch и независимый review до любых production commits.
- **Не менять статус преждевременно:** in-game `DrawIndexedInstanced`, full level lifecycle, GPU capture, R4 Present parity, resize/reload/shutdown, camera frustum и G1-M parity — **NOT TESTED**. F4.0-J1 `DRAW = BLOCKED/PENDING` до настоящего игрового GPU evidence. GR J2/J3, DXR/CLAS/PTLAS/PBR не являются условиями первого игрового Draw.

### F4.0-J1 First In-Game Draw R1 — engine/gamedata runtime blocker (2026-11-06; BLOCKED)

- **Вердикт evidence review:** корректный **BLOCKED**, **не J1 PASS**. `F4.0-J1-First-InGame-Draw-R1-All-Files.zip` — 24 ZIP entries, CRC PASS; `MANIFEST.sha256` 23/23 PASS, `SHA256SUMS.txt` 20/20 PASS. R1 report SHA-256 `46F1AD55459327FF1C9CEE84F6D6615DB46CBEEB66991F998C2AE404F476DCB9`, blocker report `6409F17B8E21B9C197643AE7453D790A5B3222EF07152F3D3CA8F8C845A17F98`, финальный runtime log `503FD614B186994E1446B591228EC8AD29BB01772449A29840D526CCD116E182`. Windows повторный запуск третьей стороной **NOT TESTED**.
- **Сборка:** feature-off и feature-on baseline Release x64 project build PASS по отчёту/логам, import `d3d12.dll` соответственно 0/1. Локально подготовленный one-shot DX12 runtime caller скомпилирован, но **откатан**; production source diff не заявлен и в удалённый `prypiat` не вошёл. Patch — отдельное evidence, не принятый runtime код. Clean solution/полная ABI-проверка не подтверждены этими журналами.
- **Данные/runtime:** отдельная тестовая среда OGSR x64 использует копии конфигурации и `bin`, оригинальные CoP level/resource архивы подключены через NTFS junction; **junction не обеспечивает режим read-only**, поэтому исходные архивы не должны подвергаться записи при дальнейших прогонах (для надёжной изоляции использовать отдельные копии или контролируемый доступ). Оригинальная игра не изменялась по отчёту. Для x64 заменён `OpenAL32.dll` только в тестовой среде; два несовместимых loose `fonts.ltx`/`actor_menu_16.xml` отключены там же. `server(all/single/alife/new)` начал **Zaton**, 6464 spawn points загрузились, `LevelResources load completed` зафиксирован, но `bReady` **не подтверждён**.
- **Непосредственный стоп:** `gamedata\\scripts\\bind_stalker.script:125` → Lua `set_callback` → unhandled luabind exception → `CScriptEngine::lua_pcall_failed` FATAL. Проверка актуальных engine files `ogsr_engine/xrGame/game_object_space.h` и `script_game_object_script.cpp` подтверждает отсутствие `actor_before_death` в `GameObject::ECallbackType` и в Lua `callback_types`. Это **подтверждённая API-несовместимость**, но не доказательство отсутствия других; в логе присутствует также `Player state not created - incorect message sequence!` и ранее возникали отдельные UI/config ошибки.
- **Jupiter:** `-start server(jupiter/single/alife/new)` требует не обнаруженный сценарный `jupiter.spawn` (не равен обязательному `level.spawn` в assets). Не создавать его искусственно и не считать прямой start Jupiter доказанным. Использовать штатную игровую последовательность после исправления DX11/R4 baseline.
- **Следующий обязательный gate — F4.0-J1 Runtime Compatibility Audit R2:** установить provenance/версию `gamedata` overlay и всех активных Lua/config/UI override, сопоставить с engine baseline и штатной CoP; для `bind_stalker.script` показать фактический строковый контекст 110–130 и `set_callback` ABI, перечислить недостающие enum/bindings и связанные engine event dispatch. Найти согласованный пакет данных предпочтительнее случайного C++ callback stub или скрытия Lua ошибок; любые compatibility patches — **отдельный минимальный diff + independent review**, без изменения DX12. Доказательства: обычный OGSR DX11/R4 launch, достижение `g_pGameLevel->bReady`, стабильный игровой кадр, camera snapshots, контролируемые level transitions к Jupiter, reload/shutdown, отсутствие новых callback/UI ошибок.
- **Статус J1:** фактический in-game DX12 `DrawIndexedInstanced`, GPU fence/readback/PNG, depth, GBV, R4 Present parity и полный level lifecycle — **NOT TESTED**. Не объявлять J1 разблокированным до DX11/R4 game boot и отдельной приёмки. R3 ASSET GATE PASS сохраняется; GR J2/J3 и DXR не ускоряют этот блокер.

### F4.0-J1 Runtime Compatibility R2 — DX11/R4 gameplay (2026-11-06; ACCEPTED WITH NOTES)

- **Принят runtime gate, не J1 GPU:** основной R1 blocker `bind_stalker.script:125` устранён подбором совместимого VFS overlay без engine/script modifications. В отдельном тестовом OGSR используются **9 loose OGSR scripts + 21 loose configs**, остальные scripts/configs из **оригинальных архивов CoP 1.6.02**; активный archive `bind_stalker.script` не вызывает отсутствующий `callback.actor_before_death`. По приложенной callback matrix, 19/19 используемых callback имён имеются в C++ enum и Lua export; это не общий compatibility PASS для всего полного OGSR overlay.
- **Подтверждающая трасса:** clean feature-off x64 build `xrEngine_r2_clean_off.exe`, SHA-256 `8259d36bd06643c62fd0d0f5dfae5f81977c19498d884d8c3c7df996c24db8e5`, `d3d12.dll` imports=0, engine libs пересобраны из baseline с build-time junction deps и disabled LTCG/WPO, третьесторонние vendor libs reused. UI New Game Zaton → `LevelResources load completed` → `End of synchronization` → first-person gameplay с HUD; clean-off Jupiter сохранение → `End of synchronization`, 11827 objects; level teardown/reload → `Destroying level` + `LevelResources unload completed` + второй `End of synchronization`. В исходниках `IGame_Level::Load` устанавливает `bReady` до успешного завершения игровой загрузки. Независимый интерактивный повторный запуск Windows **NOT TESTED**.
- **Проверка evidence:** загруженный `F4.0-J1-Runtime-Compatibility-R2-All-Files.zip`: **39 entries, ZIP CRC PASS, `MANIFEST.sha256` 38/38 PASS, `SHA256SUMS.txt` 32/32 PASS, JSON manifest 32/32 PASS**. Фактические size **38 069 092 bytes** и SHA-256 `465c55c8744042bae58a32ee3195f280239d1693d0bc107290e28c064832d502`, report SHA-256 `d52ce0c79fc5c4399cdfe1281bec69035430d91da19e26517c25711f780ef331`; это **другая ревизия упаковки** относительно SHA-256 и 12 044 614 bytes в accompanying message. Внутренние хеши сходятся, но внешнюю опись пересинхронизировать до финального архивирования. В evidence ZIP включены два `.exe` — не коммитить бинарники или игры в Git.
- **Сохраняемые ограничения:** `Player state not created - incorect message sequence!` остаётся nonfatal warning в успешных runs, анализировался отдельно, engine patch не принимался. Clean shutdown через `quit`/WM_CLOSE **NOT CONFIRMED**, процесс завершён test kill; ресурсный shutdown и P3 для DX12 не приняты. Feature-on baseline в R2 не пересобирался и не запускался (R1 exe SHA-256 `aa4c7b89629d966f9ff7246e3f1d8d68d8a81e617b6f640bd414fd39a1aa1359` только reference); B2.3 camera explicit log — другой diagnostic binary, clean-off `bReady` следует из source path и синхронизации.
- **Следующий gate — J1 opt-in real game DX12 GPU:** сначала clean feature-on build + проверка обычного UI/game startup с тем же gamedata, затем один owner-thread диагностический DX12 `DrawIndexedInstanced` при валидных `bReady`/session/camera/frustum. Для Jupiter CPU-доказан `sector 0 → root 16926 → MT_NORMAL leaf 275`; **видимость 275 в игровом frustum ещё не проверена** — не выдавать пустой readback за PASS. Использовать существующий J1.2B RTV/depth/fence/readback/PNG, Debug Layer/GBV, P3 quarantine, сохранение DX11/R4 Present; проверять teardown/shutdown в feature-on отдельно. Самый ранний GPU gate завершается реальным изображением с отличающимися от clear пикселями, без замены игровой камеры тестовой. Только затем J1 PASS и J2/J3 GR.

### F4.0-J1 First In-Game DX12 GPU Readback R2 — 2026-11-06 (EVIDENCE PASS / SOURCE REVIEW PENDING)

- **Главное достижение:** первый настоящий GPU raster `DrawIndexedInstanced` статической геометрии Jupiter внутри игрового процесса OGSR, с игровой камерой через существующий J1.2B offscreen renderer, не standalone smoke. Это **технический in-game single-visual GPU evidence PASS**, а не завершённый F4.0 renderer, не DX12 Present и не текстурная сцена.
- **Верификация:** архив `F4.0-J1-First-InGame-DX12-Readback-R2-All-Files.zip`: 20 782 087 bytes, SHA-256 `5CBAEFD8F57BB016E70AAFDC8B34C44C13BE7DA8988486C7A3C83B8D9092AFAC`, 25 файлов, ZIP CRC PASS, internal `SHA256SUMS.txt` 24/24 PASS. PNG `j1_readback_visual9.png`: SHA-256 `0CB64B6E3D69A29591D5A3C49BE11AE8F6C0BEC5B075CC43B85ADA8720580398`, 2560×1600; независимо рассчитано **10 623** пикселя rgba(255,128,32,255) на clear rgba(8,13,23,255), **0,2594 %**, bbox (1129,1099)–(2108,1599).
- **Real level provenance:** визуал Jupiter leaf 275 загружен, но вне игрового frustum (`visible=0`, ≈823 м); Draw правильно пропущен. Read-only probe установил **sector 129 → root 17431 → linked hierarchy → MT_NORMAL leaf 9**, **545 verts / 1314 indices / stride 32**, 10,383 м от настоящей игровой камеры, `visible=1`. В successful log: `load b23_jupiter`, `recorded DrawIndexedInstanced visual=9`, `fence=3`, `D32_FLOAT`, `GetDeviceRemovedReason()==S_OK`, readback, GBV/InfoQueue `verdict=PASS` warning/error/corruption=0. DX11/R4 Present screenshot и `shutdown complete` после Draw; отдельный негативный SavePng failure — без GPU ошибок, clean bridge shutdown. Feature-off/on Release x64 builds PASS, imports d3d12=0/1; весь Engine.sln/Debug x64 и аппаратный TDR NOT TESTED.
- **Production integration HOLD:** три C++ файла `GameDx12Bridge.cpp`, `GameDx12LevelGeometry.cpp/.h`, diff **+282/-5**, пока не staged/committed/pushed. Исходные Git blob SHA совпадают с нынешним `prypiat`; **независимый Codex patch review НЕ проведён**. Замечания техлида: P2 — привязать one-shot к явно выбранному game level/session (визуал ID 9 также существует в Zaton); P2 — проверять положительный pixel coverage как gate, а не только логировать его при `J1 diagnostic draw completed`, и валидировать длину readback buffer; P3 — не игнорировать слишком длинный заданный PNG path. Отдельно проверить глубину реальным depth oracle в будущем, не считать наличие DSV доказательством окклюзии.
- **Следующий gate:** DeepSeek R2A минимальный diagnostic hardening (никаких новых render abstractions/GR workers), positive/negative game tests, затем Codex independent review и отдельная приёмка production патча. Только после этого production commit. **F4.0 preview с базовыми текстурами / GR game J2/J3 / DXR остаются впереди.**

### F4.0-J1 R2A — Diagnostic Hardening (2026-11-07; TECH-LEAD AUDIT PASS WITH NOTES / CODEX PENDING)

- **Вердикт:** исходное требование R2A выполнено; первый игровой in-process DX12 GPU Draw/readback R2 остаётся **GPU EVIDENCE PASS**, в R2A подтверждён повторным positive Jupiter visual 9 run. R2A **tech-lead source/evidence audit PASS WITH NOTES**, но это **не** независимое Codex review и **не** авторизация production commit. Полный F4.0 и GRJOB J2/J3 ещё не приняты.
- **Артефакт:** `F4.0-J1-First-InGame-DX12-Readback-R2A-All-Files.zip`, **4 037 645 bytes**, SHA-256 `28CAB5F3001087A2C9935B31F9D9FA62DBC9F8768E6608C8A6D994BBF7277C7D`; **20 entries, ZIP CRC PASS, SHA256SUMS.txt 19/19 PASS**. `r2a_readback_visual9.png` SHA-256 `0CB64B6E3D69A29591D5A3C49BE11AE8F6C0BEC5B075CC43B85ADA8720580398`, 2560×1600; независимый пересчёт **10 623 pixels**, bbox (1129,1099)-(2108,1599), clear vs orange. `r2a_r4_present_final.png` SHA-256 `DD3E5BD60B065AC7CE78FB2F92A804E12A86A02AD0776CFC1BA64BFDC456CB60`.
- **Source provenance:** полный patch от commit `1959b4535f7930591695e38a8644df1dfd89f8e1` изменяет **8 файлов, +525/-37**: `GameDx12Bridge.cpp/.h`, `GameDx12LevelGeometry.cpp/.h`, `GameDx12OffscreenPass.cpp/.h`, `XR_3DA.vcxproj`, новый `GameDx12ReadbackCoverage.h`. Все **7/7** исходных Git blob SHA в patch `index` совпадают с названным baseline. Это source review, не независимый локальный `git apply --check`; по Windows evidence `git apply --check --ignore-whitespace` PASS, CRLF в offscreen source. Patch и engine binaries **не** закоммичены.
- **Проверка 4 замечаний:** `OGSR_DX12_GAME_DIAGNOSTIC_LEVEL=jupiter` ограничивает VFS load и one-shot Draw + `levelSession`/`captureEpoch` повторно проверяются перед draw; Zaton `skip`, Jupiter после transition `session=2 epoch=3` + Draw. `GameDx12DiagnosticResult` отделяет `gpuCompleted` от `imageGatePass` и `pngSaved`; `nonclear=0` отказывает после подтверждённого fence без quarantine. `ValidateGameDx12ReadbackLayout` проверяет row pitch/size/vector до Map и WIC; слишком длинный requested PNG path даёт explicit abort до Draw. Отдельный `coverage_oracle.cpp` независимо собран/запущен здесь стандартным C++17 компилятором — **11/11 PASS**.
- **Windows evidence:** feature-off/on Release x64 build PASS и `d3d12.dll` imports off/on 0/1 по журналам; `r2a_positive_visual9.log` — ровно один `DrawIndexedInstanced(1314,1)`, fence=3, removed=S_OK, `debug_layer=1 gbv=1 verdict=PASS`, error/corruption 0 и `shutdown complete`; `r2a_negative_png.log` — GPU/image PASS, `png_saved=0` и корректный diagnostic FAIL без quarantine; `r2a_wrong_level_no_draw` и `r2a_visual275_outside` — Draw отсутствует; long path — explicit FAIL без Draw; R4 Present screenshot сохранён. Независимый повторный Windows runtime/GPU execution **NOT PERFORMED** в этой среде.
- **Замечания на независимый Codex review:** проверить корректность `SphereInFrustum` на не-finite asset bounds и альтернативных проекциях; подтвердить, что one-shot `diagnosticAttempted` с process-lifetime семантикой ожидаем при reload; решить контракт, разрешён ли `imageGatePass` при `pngPath=nullptr` (coverage PASS, `pngSaved=false`), и что для формального evidence gate требуется сохранённый PNG. `zero-coverage` протестирован CPU oracle, отдельный GPU zero-coverage fail-run **NOT TESTED**. Полный clean `Engine.sln`/Debug x64, feature-off R2A runtime и TDR/device loss **NOT TESTED**.
- **Codex independent review (2026-11-07):** **REQUEST CHANGES, P2**, P1/P3 confirmed findings **нет**. Проверено по R2A source patch: в `GameDx12Bridge_OnFrame` identity gate требует `captureEpoch == diagnosticLevelEpoch == geometry.LevelEpoch`. `GameDx12Bridge_OnDeviceReset` при активном уровне увеличивает `captureEpoch`, вызывает `geometry.OnDeviceReset(nextEpoch)` (обновляет epoch geometry), но сохранённый `diagnosticLevelEpoch` не синхронизирует. При reset до первого Draw и поздней видимости visual изначальный one-shot навсегда skip-ится до level lifecycle. Отдельного Windows runtime теста reset-before-draw **не было**. Codex source-only verdict подтверждён дополнительной проверкой R2A patch и baseline bridge; не считать GPU failure или P3 quarantine error.
- **Next:** **F4.0-J1 R2B — reset-before-draw recovery**, минимальный source diff. После `OnDeviceReset` повторно связывать диагностическую epoch **только** при `BridgeState::Ready`, том же active level name/session, загруженной geometry с подтверждёнными identity и GPU safety, без уже выполненной one-shot попытки и без unknown GPU completion. Не сбрасывать `diagnosticAttempted` после успешного Draw, не переиспользовать geometry между уровнями. Иначе fail-closed с явным логом, без silent mismatch. Проверить before-first-draw reset и resize, no-reset positive Draw, reset-after-draw no-repeat, wrong-level skip, Zaton→Jupiter, fence/GBV/R4 Present, teardown, negative PNG и GPU lifetime. При PASS повторить независимый **Codex review**, затем техлид acceptance; до этого **не staging/commit/push production patch**, не GRJOB J2/J3. Оригинальная CoP/DX11/R4 fallback без изменений.

### F4.0-J1 R2A Codex Review → R2B Reset Recovery (2026-11-07; REQUEST CHANGES)

- **Scope:** read-only independent Codex review полного R2A source patch; **P2 confirmed**, без подтверждённых P1/P3. `GameDx12Bridge.cpp` R2A `OnFrame` (около line 1404) проверяет `captureEpoch == diagnosticLevelEpoch == geometry.LevelEpoch()`; при `OnDeviceReset` (около line 1748) `captureEpoch` и epoch геометрии обновляются, сохранённая `diagnosticLevelEpoch` нет. После reset **перед первым Draw** видимый visual не отрисуется в той же level session даже с исправной geometry; GPU-specific reset run пока **NOT TESTED**.
- **Accepted evidence unaffected:** первый игровой Draw Jupiter static `MT_NORMAL` visual 9 (1314 indices), fence 3, 10 623 nonclear PNG pixels, Debug Layer/GBV logs PASS и R4 Present evidence; этот P2 — source-level recovery defect, не доказательство неверного предыдущего GPU результата.
- **R2B contract:** однократно переустановить diagnostic binding epoch **после** успешного вызова geometry reset и **только** когда тот же текущий уровень и session, `geometry.IsLoaded()`, geometry session/epoch совпадают с bridge, `!GpuCompletionUnknown()`, безопасен owner context, `!diagnosticAttempted` и нет submitted/in-flight draw. Отклонить с явным логом невалидный level/geometry state; не делать implicit reupload, не сбрасывать process-once attempt при reload/reset, не создавать вторую систему исполнения. При смене уровня соблюдать принятый quarantine и provenance contract.
- **R2B proof:** controlled reset/resize **до** visual в frustum → ровно один Draw с новым epoch; без reset → один Draw; reset **после** Draw → ноль повторов; wrong-level и teardown → ноль Draw; GBV/fence/readback PNG + R4 Present + clean shutdown; условные failure paths fail-closed. Source-level тесты не подменяют runtime reset-after/ before доказательства.
- **R2B camera-control test strategy (решение 2026-11-07):** использовать **вариант A**, отдельную temporary **test-only** консольную команду `j1_test_look_at` в harness-бинарнике. Она вправе менять только направление игровой камеры через обычный game camera path; не должна изменять frustum output, geometry bounds, diagnostic epochs, ready/attempted flags, reset/rebind, Draw/culling, GPU fences или readback. Сценарий: Jupiter visual за пределами camera frustum → реальный resize/device reset до первого Draw → подтверждённое rebinding текущей epoch → камера направлена на visual из harness → ровно один **production** DX12 indexed Draw, fence/readback/PNG, ненулевой coverage, GBV/InfoQueue PASS, R4 Present и shutdown. Приложить test-only diff, build provenance и SHA-256 harness exe, пометить результат **HARNESS-RUNTIME GPU EVIDENCE**, а не unmodified-production-camera result. Отдельный unmodified feature-on exe должен повторить no-reset positive Draw и остальные production regression tests; тестовый camera hook должен отсутствовать в финальном production cumulative patch и бинарнике. Вариант B (изменение gamedata Lua) не применять; вариант C (сразу видимый visual + reset-before-draw) допустим дополнительно, но не заменяет проверку invisible→visible. При отсутствии независимого доказательства безопасного reset/rebinding сохранять **BLOCKED**.
- **Status на момент решения R2A:** R2A production patch **HOLD**, R2B **PLANNED**. Этот исторический статус заменён ниже фактической поставкой R2B; независимый повторный Codex review + tech-lead acceptance всё ещё обязательны до production commit. F4.0 и игровая GRJOB J2/J3 — PENDING.

### F4.0-J1 R2B — Reset Recovery (2026-11-07; CODEX PASS WITH NOTES / TECH-LEAD ACCEPTED WITH NOTES)

- **Current status / scope:** независимый Codex review **PASS WITH NOTES**, финальная техлид приёмка **ACCEPTED WITH NOTES** по cumulative source и представленным игровым GPU evidence. Предыдущий P2 reset-before-first-Draw устранён; новых P1/P2/P3 findings в review нет. **Историческое HOLD по source integration снято владельцем проекта 2026-11-07, source интегрирован в GitHub (`656652be7`).** Даже с опубликованным source это не F4.0 PASS и не означает пройденный post-merge Windows GPU regression gate.
- **Integrity/source provenance:** архив `F4.0-J1-Reset-Recovery-R2B-All-Files.zip`: **4 982 200 bytes**, SHA-256 `1CBA30E880F03BA722CA606C7E9BF6D99C42E29AA99478AC49148445F7D4DECB`; ZIP 20 entries, CRC PASS, `SHA256SUMS.txt` **19/19 PASS**. Полный 8-file cumulative patch (37 541 bytes) SHA-256 `0E6B343117EAB8AD236CF065F77F8AD0329510939CDCDD4169E82BE816729E1C`, R2B-only incremental `GameDx12Bridge.cpp` patch (4 213 bytes) SHA-256 `28A29394AC1D406D52E7FE4DDF4A5D01BF7F582CA2FCD5FC0C7671587A4489FD`. Все 7/7 исходных Git blob SHA соответствуют `1959b4535f7930591695e38a8644df1dfd89f8e1` (по GitHub). Отдельный независимый запуск `git apply --check` в этой среде **NOT PERFORMED**; Windows evidence утверждает apply PASS (при необходимости `--ignore-whitespace` для CRLF).
- **P2 fix:** `RebindDiagnosticAfterDeviceReset("device_reset")` вызывается после `geometry.OnDeviceReset` и проверки quarantine. Проверяет bridge Ready, active level/name и одинаковую session, bound diagnostic, `geometry.IsLoaded()`, совпадение geometry session/epoch с bridge, отсутствие unknown/recorded/submitted GPU, отсутствие quarantine/released и `diagnosticAttempted==false`; при успехе обновляет **только** `diagnosticLevelEpoch`. При отказе явный лог и никакого Draw; process-once попытка не сбрасывается.
- **Главный controlled harness GPU run:** Zaton diagnostic skipped → Jupiter `session=2 epoch=3`, visual 9 `visible=0` → `vid_restart` и bridge `epoch=4` → `diagnostic rebound after device_reset ... previous_epoch=3` → harness меняет только направление camera → ровно один `DrawIndexedInstanced(1314,1)`, fence=3, `removed=S_OK`, `gpu_completed=1 image_gate=1 png_saved=1`, InfoQueue/GBV `verdict=PASS` warnings/error/corruption 0, shutdown complete без quarantine. GPU PNG `r2b_reset_before_draw.png`: **2560×1600**, SHA-256 `DE0775B0AF54E378AED04D1AF59F2AED1BF69D0AECE7FFF9E24C3EDB5306A214`, самостоятельно пересчитано **7430** non-clear pixels (bbox `703,454–1617,1599`). R4 Present screenshot проверен по SHA-256 `CD6F5821FF144BDB8B4D067BA095FCF9CE78744410B514D2B19169BBCE56F447`.
- **Regression evidence:** unmodified production R2B (без harness) no-reset: **1 Draw / 10623 pixels / fence=3**, clean shutdown. Reset-after-Draw: две reset epoch и два fail-closed refusal `one-shot diagnostic attempt was already consumed`, повторного Draw нет. Реальный GPU zero-coverage: **1 Draw / 0 pixels / gpu_completed=1 / image_gate=0 / png_saved=1**; независимый PNG pixel count 0. Negative PNG save: GPU/image PASS + `png_saved=0` → expected diagnostic FAIL, clean shutdown; wrong-level Zaton skip. Feature-on/off MSVC Release x64 build logs PASS, imports `d3d12.dll` on/off = 1/0.
- **Test-only isolation:** два camera harness binaries и `j1_test_look_at` source включены как evidence. Имена `j1_test_look_at` и `console_commands.cpp` **отсутствуют** в cumulative и incremental production diffs. Harness меняет camera для controllable invisible→reset→visible test, не production DX12 logic; этот прогон обозначать **harness-runtime GPU evidence**, а не неизменённую игру с обычным input.
- **Ограничения:** независимого запуска Windows exe/GPU здесь нет; feature-off runtime после R2B, полный clean `Engine.sln`, Debug x64, настоящий TDR/device loss и long soak **NOT TESTED**. Статический source review R2B incremental не равен независимому Codex аудиту полного R2A+R2B. На момент подготовки R2B patch был вне Git по отчёту автора; впоследствии он перенесён непосредственно в ветку `prypiat` через GitHub с проверкой каждого target blob SHA. Состояние внешней Windows рабочей папки независимо не проверялось.
- **Codex independent review (2026-11-07):** **PASS WITH NOTES**, новых подтверждённых P1/P2/P3 **нет**. Проверены полный cumulative R2B patch и baseline apply (включая CRLF), 17 CPU state tests для извлечённого `RebindDiagnosticAfterDeviceReset`, 29 106 frustum clip inequality samples, PNG 7430 nonclear pixels и GPU zero-coverage 0. Windows GPU/runtime логи — **внешнее evidence**, новый аппаратный запуск самим Codex **не выполнялся**. Бесконечная far plane не поддерживается; полноценный TDR/device-loss, fault-injection, feature-off R2B runtime, clean `Engine.sln`/Debug x64 остаются **NOT TESTED**. Для формального сохранённого PNG evidence требовать одновременно `imageGatePass==true` **и** `pngSaved==true` (без PNG-path image gate может быть true при pngSaved=false).
- **Source integration gate (2026-11-07): COMPLETED.** Владелец явно разрешил применение и коммит. Подтверждены неизменность всех 7 source blob SHA относительно patch baseline, корректное применение всех hunks, совпадение **8/8 target Git blob SHA**, отсутствие camera harness, новый tree из ровно 8 ожидаемых файлов; опубликован commit **`656652be776098ce0e1ef583a2d7745e171705fe`** в `prypiat`, parent `1f81b9984da893acc74b84bab9667e641662e67f`. GitHub compare подтвердил только эти 8 production files. Это **source integration**, не аппаратный post-merge test. Версия исходников, участвовавшая в Windows R2B GPU logs, побайтово идентична опубликованным blob hashes, но повторная сборка/запуск нового HEAD **NOT TESTED**.
- **Следующий обязательный gate — post-merge verification:** выполнить на Windows fresh feature-off и feature-on Release x64 builds **из текущего `prypiat`**, проверить imports d3d12 off=0/on=1, изолированный UI→Zaton/Jupiter runtime, opt-in J1 visual 9 no-reset Draw + PNG nonclear pixels, reset-before-Draw controlled harness (только в отдельном тестовом бинарнике), reset-after-Draw no repeat, negative zero coverage/PNG, DX11/R4 Present, Debug Layer/GBV/InfoQueue, fence/device/teardown/shutdown, `git status` и exact exe SHA-256. Показать evidence с commit SHA `656652be7` (или новее при неизменных production blobs). До фактического GPU/run подтверждения **post-merge verification PENDING**. После него начинать **F4.0-J2 — GR game-frame preparation**, а затем J3 parallel recording, без создания нового scheduler; F4.0 textured scene и DXR по-прежнему PLANNED.

### F4.0-J1 production source integration — 2026-11-07 (COMMITTED; POST-MERGE GPU ACCEPTED WITH NOTES 2026-11-08)

- **Latest post-merge gate (2026-11-08):** independently inspected delivered Windows evidence and SHA manifests; feature-on/off Rebuild + imports, Zaton/R4, Jupiter DX12 Draw 10 623 PNG pixels, reset-before harness 7 430 pixels, after reset no repeat, negative PNG, GBV and shutdown all PASS by logs. Independent PNG counts and nested manifest 18/18 PASS. **ACCEPTED WITH NOTES**, supersedes historical PENDING lines below. Fresh postmerge zero-coverage GPU NOT TESTED (carried R2B only); independent Windows execution, clean Engine.sln/Debug/TDR NOT TESTED.

- **GitHub commit:** `656652be776098ce0e1ef583a2d7745e171705fe` on `prypiat` (parent `1f81b9984da893acc74b84bab9667e641662e67f`), message `feat(dx12): integrate accepted J1 in-game diagnostic draw and reset recovery`. **8 production paths only:** `GameDx12Bridge.cpp/.h`, `GameDx12LevelGeometry.cpp/.h`, `GameDx12OffscreenPass.cpp/.h`, `GameDx12ReadbackCoverage.h` and `XR_3DA.vcxproj`. Указанные 8 Git blob SHA после применения source hunks совпали с полным R2B patch; CI Windows rebuild здесь не проводился. `console_commands.cpp` и `j1_test_look_at` не добавлялись.
- **Принятый technical evidence:** in-game Jupiter raster `DrawIndexedInstanced(1314,1)`, реальный gameplay camera, offscreen RTV/D32, fence=3, GPU PNG 10 623 pixels без reset; harness reset epoch 3→4 затем 7 430 pixels, GBV/InfoQueue PASS по Windows evidence. Это не DX12 swapchain Present и не полностью портированная геометрия уровня.
- **Post-merge gate:** повторить feature-off/on сборки и игровой runtime на опубликованном коде; проверить R4 parity, GPU debug/GBV, readback image+PNG, negative tests, reset and shutdown. Пока этих журналов нет — **POST-MERGE RUNTIME NOT TESTED**, несмотря на предыдущий PASS и идентичность исходников.
- **Дальше по roadmap:** после post-merge verification — F4.0-J2 immutable frame/geometry packets через существующий GR Job System, затем J3 parallel command lists. Не объявлять полный F4.0 PASS до сцены с базовыми текстурами/материалами и navigable camera, описанной в F4.0-C/D.

### F4.0-J2A geometry CPU prototype — independent review 2026-11-08 (REQUEST CHANGES)

- **Integrity:** combined delivery 14 909 547 bytes SHA256 3E59EA9B44963AAE84394A54E6E12E4B5B8907AB5C0F041BFAAAEC5819B0C20, 9 entries CRC PASS, 8/8 root manifest PASS. Nested J1 18/18 PASS; nested J2A 131 293 bytes SHA256 99635F25A48829DE9FF7A09F47392C7C46D14BEAEAAF731029C02D9EC5B8B008, 16/16 PASS. J2A 10-file patch 61 359 bytes SHA256 12AB8AF505D308AF5590BA052FE4A171EC64E01EAC73F68A0D1E4218930D25D5; baseline GameDx12Bridge.cpp and XR_3DA.vcxproj blob SHA match remote branch. No J2A production commit.
- **Positive CPU evidence:** Jupiter sector 129/root 17431, 5 real instances, four visible MT_NORMAL packets (9/105/106/107), one culled visual 415; deterministic hash EC3EAADD55DFCD6C across 3 runs; GR Serial/Parallel/Verify; hierarchy CPU oracle **17/17 independently rebuilt and executed** under Linux C++20. J2A does not GPU Draw/upload.
- **P2 confirmed:** dx12levelhierarchy.cpp ValidateGraph near 397–433 skips globally visited child before enforcing frame.depth + 1 > kMaxDepth. Independent synthetic 80-node leaf-first acyclic chain returns Parse=true and leaf depth **79** although kMaxDepth=64. Fix order-independent maximum depth over full DAG; regression tests 64 pass, 65/79 fail and shared-subgraph paths.
- **P3 confirmed:** ParseVisuals near 252–259 detects duplicate OGF_CHILDREN_L using nonempty children vector rather than presence flag. First chunk count=0 then second chunk nonempty is incorrectly accepted; independently reproduced. Add seenChildren boolean and negative tests.
- **Cautions:** first owner-side Jupiter VFS/parse/ReadVisual takes approximately **2.51 s**, requiring explicit amortization/preload before wider use; J3 parallel command recording alone will not fix that stall. material/pso and Resource12 handles currently use **synthetic CPU-only IDs**, not real GPU-resident handles; must resolve prior to J3 GPU drawing. Single GR pool ownership still needs review before multi-renderer use. MSVC rebuild and runtime GPU remain provided Windows evidence, not independent Windows execution.
- **Next gate:** narrow J2A R2 parser hardening (P2/P3), expanded CPU oracle, Jupiter same hash/GR Verify regression, x64 off/on build and teardown/reset evidence, independent Codex review and tech-lead reacceptance. **J2A production source HOLD, J3 NOT STARTED.**

### F4.0-J2A R2 parser hardening — independent pre-review (2026-11-08; P2/P3 TARGETED PASS / CODEX PENDING)

- **Status:** R1 P2/P3 findings independently reproduced before fix and **closed in targeted parser tests** for J2A R2. This is a **tech-lead pre-review PASS**, NOT full independent Codex acceptance, NOT a production commit authorization and NOT multi-object GPU rendering. Historic R1 REQUEST CHANGES section above remains a snapshot of the pre-fix state. J2A production/J3 still **HOLD**.
- **Evidence integrity:** delivery `F4.0-J2A-R2-Parser-Hardening-Delivery.zip` **11 401 466 bytes**, SHA-256 `B73C9E1A2B03E33DEB48AA53A8F348F4F57582CE963EA49238477C519D78A488`, 7 entries, CRC PASS, root manifest 6/6 PASS. Nested `F4.0-J2A-R2-Parser-Hardening-Evidence.zip` **11 373 059 bytes**, SHA-256 `44993BA424AAD0B0959C17400D42FB47A6FA39DF82B98B958639439A77B22D3A`, 28 entries, CRC PASS, 27/27 manifest PASS. Cumulative patch 68 161 bytes SHA-256 `9FE7EFFD97391BAB19D62EE5ADDD0461AC14F0CEF7A29B1D999D383401ADF354`; incremental patch 10 653 bytes SHA-256 `B546D620705659B301A00D11C776E29F4DC4CD0217BE415086548F84D6224695` (only `dx12levelhierarchy.cpp`).
- **Independent CPU compile/execution:** source `dx12levelhierarchy.cpp` reconstructed **directly from cumulative patch**, SHA-256 `A142422400315EC55BD761D1D38D4F09692C087CEB0C79821C5CEA04443245AC`, byte-identical to nested evidence source. Linux g++ C++20 build (parser + official oracle) **26/26 PASS**. Reproduction harness confirms depth 64 valid, leaf-first depth 65/79 and shared DAG overflow rejected; duplicate children empty→nonempty, nonempty→empty and empty→empty rejected. Additional independently authored seeded random DAG property fixture **800 cases / 0 mismatches** against separate longest-path calculation.
- **P2 closure:** `ValidateGraph` computes memoized maximum subtree height with cycle detection independently of visual storage order and rejects `height[sectorRoot] > kMaxDepth (64)`. No global visited-state path-length bypass in tests. **P3 closure:** dedicated local `seenChildren` independent of whether first `OGF_CHILDREN_L` has count zero.
- **Windows runtime evidence reviewed, not independently rerun:** Jupiter sector 129/root 17431, five real CPU instances, four visible ordered packets (visuals 9/105/106/107), one culled visual 415, stable hash `EC3EAADD55DFCD6C`, GR Serial==Parallel==Verify; after J2A, reset epoch 4 does not repeat, Jupiter→Zaton teardown and shutdown recorded. Feature-off/on MSVC Release x64 Rebuild/imports 0/1 and DX11/R4 Zaton New Game PASS according to submitted logs. Full clean Engine.sln/Debug/GPU multi-object Draw NOT TESTED.
- **Known limits:** first owner thread level VFS/parse/ReadVisual operation **3.17 s in R2** (2.51 s prior); J3 command parallelism will not fix this stall. `material`, `pso`, and `Resource12` remain CPU-only placeholder IDs (not verified GPU bindings). Before scene-scale rendering, introduce amortized loading and verified material/geometry residency, and review single GR worker pool ownership.
- **Required next gate:** **Codex READ-ONLY review** of cumulative J2A R2 patch and incremental parser fix, verifying lifetime, CPU safety, bounds/cycles/duplicates, map/packet identity, feature guards, test evidence and failure paths. Exact P1/P2/P3 findings and verdict PASS / PASS WITH NOTES / REQUEST CHANGES. Then separate tech-lead source acceptance before any staging/commit/push. J3 NOT STARTED.

### F4.0-J2A R2 — independent Codex audit (2026-11-08; REQUEST CHANGES / J2A R3 REQUIRED)

- **Verdict:** full cumulative J2A R2 production patch **REQUEST CHANGES**, **3 P2 + 3 P3**, not approved for staging/commit/push; J3 **NOT STARTED**. Targeted pre-review of earlier parser issues (DAG longest height and seenChildren) remains valid, but must NOT be conflated with acceptance of whole source patch. J1 post-merge remains ACCEPTED WITH NOTES.
- **P2 #1 / unbounded valid-DAG traversal:** `ogsr_engine/Layers/xrRenderDX12/dx12levelhierarchy.cpp` `CollectLeaves` around line 682, caller `ogsr_engine/xr_3da/GameDx12J2APreparation.cpp` around line 157. Only MT_NORMAL leaves count toward `maxInstances`; shared non-drawable subgraphs can be expanded exponentially. Codex reproduced 27-node, 2180-byte valid DAG with depth 24 taking ~0.30 s before first drawable; +3 depth ~8x cost. Fix **strict bounded node/edge traversal budget** checked before expansion, fail-closed with explicit reason. Preserve legitimate repeated drawable instances along distinct DAG paths; global visited dedupe is **not** correct.
- **P2 #2 / wrong sector provenance:** `dx12levelhierarchy.cpp` `ParseSectors` around line 295 sets `Sector.index = sectorIndex++` instead of chunk type/ID. In reordered chunks 1,0 parser swaps sector-root identity. R4 `r4_loader.cpp` reads `S->open_chunk(i)` by numeric sector ID. Validate sector chunk IDs (duplicates, zero-based continuity and bounds consistent with R4), preserve actual ID, then order sectors by ID before traversal. Required tests reorder, duplicate, missing ID, Jupiter 129→17431.
- **P2 #3 / silent truncation:** `CollectLeaves(hierarchy,100000u,...)` returns **success** on hitting cap without communicating incompleteness. Caller selects sector by visual 9 if found, else silent first-sector fallback, possibly against incomplete traversal. Repro 100 000 early instances + later visual 9 returned exactly cap and missed target. Fix explicit `Complete/LimitExceeded` status (or fail-closed) for global collection, and deterministic bounded targeted-sector discovery without requiring full arbitrary DAG materialization. Never call absence/provenance confirmed from incomplete search; no fallback on cap, no partial snapshot committed. Test exact boundary 100000 and 100001+, target after cap, target missing, controlled multi-sector ordering.
- **P3 #1 / duplicate MSBuild compilation:** `ogsr_engine/xr_3da/XR_3DA.vcxproj`, `dx12packetplan.cpp` ClCompile twice in `GAME_BRIDGE=true`, `GAME_CAMERA_CAPTURE=false`. Evidence log **MSB8027/LNK4042**. Make build ItemGroups mutually exclusive / single compile item; evaluate feature-off, bridge-on/camera-off, bridge-on/camera-on and build so no duplicate Compile/obj.
- **P3 #2 / false-positive compressed fixture:** `dx12levelhierarchy.cpp` around line 798 uses `compressed[0] |= 0x80`, incorrectly mutating low byte of chunk ID instead of 0x80000000 marker; passes with unrelated missing-mandatory-chunk failure. Test correct bit `compressed[3] |= 0x80` and **assert correct failure reason** for top-level chunk, visual and OGF subchunk.
- **P3 #3 / VFS exception safety:** `GameDx12J2APreparation.cpp` `ReadVfsFile` around line 51 opens reader then `bytes.resize(size)`; exception escapes before `FS.r_close(reader)`. Use RAII scope guard / custom deleter bound to `FS.r_close`; fault injection for allocation failure or equivalent scoped lifetime test, no leaks even when parsing fails.
- **Independent Codex evidence:** patch applies to isolated baseline; 26 MSVC oracle assertions PASS **with noted false-positive**, separate 2000 randomized shuffled DAG longest-path comparisons (1651 accept/349 depth reject) show no discrepancy for `ValidateGraph`; separate malformed/compression fixtures rejected; CPU 512-object Serial/Parallel/Verify equivalent; MSBuild evaluated and identified duplicate compile. **No independent Windows GPU/game runtime execution**. Jupiter 5 instances / 4 visible packets and teardown remain **provided Windows logs**, not source-only proof or multi-object GPU Draw.
- **Timing correction:** previously cited **3.17 s** from `level ready` to J2A PASS is **not** a direct measurement of `ReadVisual/Parse/PrepareOrdered` and includes level loading/synchronization. Adjacent pre-J2A camera log to PASS covers ~**178 ms**, but also is not an instrumented duration. R3 must measure timed scopes around VFS/read, hierarchy parse, `CollectLeaves`, `ReadVisual`, GR PrepareOrdered, and total owner critical section; record p50/p95 and budgets. Do not assume J3 fixes owner-side synchronous I/O.
- **Next:** narrow **F4.0-J2A R3 — Traversal Budget, Sector Provenance, Truncation Contract & Resource Safety** implementing all 6 findings with expanded adversarial CPU tests, deterministic Jupiter sector 129/root17431, stable hash `EC3EAADD55DFCD6C` (or explained change), GR Serial/Parallel/Verify and reset/teardown, feature-off/on MSVC builds and no duplicate warning, DX11/R4. Produce R3 incremental + cumulative patches, per-file SHAs and evidence archive. Independent Codex READ-ONLY review and tech-lead acceptance mandatory before any J2A source staging/commit/push. GPU material/PSO handles still placeholders, J3 HOLD.

### F4.0-JOBS — GR Job System в игровом DX12 renderer (решение 2026-11-02; PLANNED)

**Решение:** повторно использовать **существующий GR Job System** (`ogsr_engine/Layers/JobSystem/`,
адаптированный enkiTS) как **обязательную архитектурную основу** для CPU-подготовки
игрового DX12-кадра и параллельной записи D3D12 command lists **до масштабирования
полноценного игрового renderer на множество объектов**. Не внедрять второй
scheduler / второй несвязанный worker pool и не переписывать GR/enkiTS с нуля.
Это требование к будущему игровому renderer, а **не утверждение, что текущий
F4.0-G1B уже работает через GR**.

**Фактическая база:** в standalone `xrDX12Smoke` уже имеются GR-driven CPU
preparation и реальная параллельная запись через `CHW12` (`JobGroup`,
`Submit`/`Wait`, индивидуальные D3D12 allocator/list, owner
`ExecuteCommandLists`; F3.5.3B и F3.5.5C). В проекте
`xrRender_R5_DX12.vcxproj` есть `ProjectReference` на `gr_job_system`,
но наличие build-зависимости **не доказывает игровое runtime-использование**.
Изолированный in-process `GameDx12Bridge` G1B выполняет только
служебный single-list submit без игрового draw и без GR workers. G1B
Round 2.4 source-only принят (`bb62c00ac`), J1.1 standalone GPU принят
отдельно (`a1c751274`); на момент принятия G1B/J1.1 игровой Jupiter runtime был заблокирован
недостающим совместимым level payload; Asset Recovery R3 (2026-11-06) закрыл
VFS/CPU gate, но J1 in-game Draw и GPU readback ещё не проверены. **GR parallel recording не объявлен
принятым; принятые G1B/J1.1 не являются in-game renderer.**

| Гейт | Содержание и порядок | Доказательство |
|---|---|---|
| **F4.0-J0 — source/runtime integration audit** | Сопоставить `CHW12`, `FrameBatchPlan12`, API GR и реальные точки G1B / игрового render lifecycle; выбрать единственного владельца GR pool, безопасную инициализацию/остановку и границы модулей. | Схема ownership/threading, минимальный diff-план; отсутствие второй системы задач и зависимости DX11/R4 от GR. |
| **F4.0-J1 — первый игровой Draw (serial fallback)** | Сначала вывести один настоящий static visual Jupiter внутри игрового процесса с игровой камерой, depth и DX12 readback. Одна command list на owner thread; GR не является блокером первого геометрического кадра. | Реальные игровые VB/IB и GPU draw, подтверждённый fence, кадр/изображение, работающий R4 fallback; не выдавать standalone draw за игровой. |
| **F4.0-J2 — GR game-frame preparation** | После J1 передавать в GR только immutable frame snapshots, проверенные geometry/material handles и готовые packet plans; worker'ы не читают mutable X-Ray level/game state. Встроить serial/parallel/verify. | Побайтовое/смысловое равенство packet order и camera/object data, тесты level teardown/reset и lifetime. |
| **F4.0-J3 — parallel game command recording** | До массового переноса статической/динамической сцены подключить GR `JobGroup` для 2+ непустых draw lists; у каждого worker собственные per-frame allocator/list и результат. Owner делает Begin/End, `Wait`/drain, barriers/queue submission/fence; никакого конкурентного Reset общего allocator/list. | GPU capture реальной геометрии, одинаковые draw order/image oracle с serial, отказ одного worker запрещает частичный `ExecuteCommandLists`, все задачи завершены до reset/reload/shutdown. |
| **F4.0-J4 — correctness, fallback, scaling gate** | Прогнать serial vs GR parallel на реальных сценах/объектах, с 1/2/4 workers и разным числом списков; измерить CPU recording/scheduling/Wait, GPU time, VRAM, p50/p95/p99 и validation. Выбирать parallel default только при воспроизводимом выигрыше. | Одинаковые кадры и ресурсный lifetime, нет новых D3D12 WARNING/ERROR/CORRUPTION, корректный serial fallback; результат по FPS не выводить из synthetic Smoke. |

**Инварианты:** одна согласованная GR lifetime/ownership политика на время работы
DX12, без создания пула на каждый кадр/level; исключения и отказ `Submit`
дренируются до любого GPU Execute; GPU resource creation/residency и изменение
общего состояния остаются под контролем owner. Настоящая GPU работа может
начинаться только после подтверждённого CPU job completion и в рамках
существующих fence/quarantine правил. `JobGroup::Wait` не заменяет GPU fence.
При малом числе packets использовать owner-only последовательный путь, без
фиктивных задач. DX11/R4, physics, AI и audio не подключать к GR в этих
милестоунах.

**Связь с общим планом:** J1 относится к F4.0-B (первый игровой geometry
frame), J2/J3 — к последующему росту F4.0/F4 renderer, J4 — к гейтам
приёмки/оптимизации F4/F6. F4.0-C текстуры и F4-PBR могут исследоваться
параллельно, но их игровая запись команд должна наследовать этот же
GR/packet/allocator/lifetime контракт. Обозначения **F4.0-J0–J4** не
переименовывают **DXR-G0–G5** (где G3 — RT-тени). Не принимать native
DX12 Presentation, PBR и DXR на основании одного GR теста.

### F4-PBR — Physically Based Materials & Lighting (решение 2026-10-30; PLANNED)

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

### Принципы F

- Не заменять существующий `CHW` на D3D12 в одном большом коммите и не
  подменять `ID3D11DeviceContext` ссылками на command list «по названию»:
  **D3D12 требует явной синхронизации, resource barriers, descriptors и PSO**.
- Не переносить весь HLSL на SM6 одним шагом. Сначала исследовать
  совместимость существующих raster shaders с DX12, выделить DXC/SM6
  как отдельную задачу для DXR и новых функций.
- Не требовать появления универсального render graph на F0. При необходимости
  добавить минимальный state/lifetime tracker, обоснованный профилями.
- **До массового переноса F4** завершить минимальный архитектурный гейт F3.5:
  immutable snapshot, ownership/lifetime и проверенный вариант multi-list CPU
  recording. Параллельность по умолчанию включать только после профилирования;
- Не добавлять поддержку DX12 в пользовательские опции до реального
  прохождения F4–F5; экспериментальный режим переключать через сборку/флаг.
- F0 можно выполнять **параллельно** B2/D0; F3+ — только после F2.

### Условия остановки F

Если аудит F0 выявит необходимость необоснованно переписать системный код,
либо на F2/F3 не удаётся обеспечить стабильную синхронизацию, сначала
пересмотреть границу backend и технические требования; не переносить дальше
всё подряд по инерции.

---

## Этап G — Аппаратный DXR (новое направление; зависит от F)

**Цель:** реальный hardware ray tracing, начиная с **одной ограниченной
функции**. Не путать с DX11 DDA/SSGI, не обещать GI/reflections до
стабильного RT prototype.

| Подэтап | Работа | Гейт / результат |
|---|---|---|
| **G0 — capabilities** | На DX12-устройстве запросить `D3D12_FEATURE_D3D12_OPTIONS5` / `RaytracingTier`; проверить `ID3D12Device5` / `ID3D12GraphicsCommandList4` и fallback на GPU без DXR. | Диагностика точного tier и понятное отключение RT при отсутствии поддержки. |
| **G1 — геометрия для RT** | Сопоставить реальный поток vertex/index с RT geometry. Построить обычный BLAS для **небольшого статического** набора, TLAS для инстансов. Проверить ресурсы, lifetime, scratch buffers, transforms; этот путь сохраняется базой и fallback для G-CLAS. | Видимая тестовая сцена с валидными BLAS/TLAS; RenderDoc/PIX & validation. |
| **G2 — минимальный трассировочный проход** | DXC/SM6, raygen/miss/closest-hit, shader tables/RTPSO, `DispatchRays` в простую UAV-маску. | Предсказуемые hits/misses и стабильная работа без серьёзных ошибок. |
| **G3 — RT-тени** | Начать с одного света/ограниченной геометрии, интеграция с существующим deferred lighting. Временная/пространственная фильтрация при необходимости. | Включаемые RT-тени рядом с текущим CSM, сопоставимые кадры и GPU ms. |
| **G4 — динамика и материалы** | Skinned/dynamic objects, обновление/rebuild BLAS/TLAS, alpha-tested foliage, invalidation, emissive и material lookup. | Нет существенных артефактов при анимации NPC/растительности и движении камеры. |
| **G5 — дополнительные эффекты (опц.)** | Только после G3/G4: **RT-отражения как отдельный проверяемый рубеж**, затем по отдельному решению diffuse GI и denoise; DLSS Ray Reconstruction — отдельная интеграция при проверенной поддержке SDK/API. | Для RT-отражений отдельно зафиксированы качество, GPU ms, CPU ms и frametime. После работающих RT-теней и отражений — переход к H1 (аудит многопоточности всего движка). Каждый дополнительный эффект имеет собственный performance budget. |

### G-CLAS — Clustered Geometry & Partitioned TLAS (решение 2026-11-03; PLANNED)

**Статус:** архитектурное исследование / будущий эксперимент, **NOT IMPLEMENTED /
NOT TESTED**. Нельзя подменять принятый обычный DXR BLAS/TLAS экспериментальным
путём. Приоритеты остаются: реальный игровой DX12-кадр F4.0, обычные G0–G2,
затем RT-тени G3; G-CLAS не является условием принятия G1/G3 или запуска игры.

**Две независимые оптимизации:**

- **CLAS (Cluster Level Acceleration Structure)** хранит компактные кластеры
  треугольников. Из нескольких CLAS строится **Cluster BLAS**; отдельный CLAS
  **нельзя помещать прямо в TLAS**. По спецификации DXR2 один CLAS ограничен
  256 треугольниками и 256 уникальными индексами вершин. Обычные BLAS и
  Cluster BLAS могут сосуществовать в одном наборе RT-инстансов.
- **PTLAS (Partitioned TLAS)** — альтернативная верхнеуровневая структура:
  частичное обновление пространственных групп инстансов вместо перестроения
  обычного TLAS целиком. PTLAS не является обязательной частью CLAS и
  исследуется отдельно, в том числе на обычных BLAS.
- **Ожидаемый эффект — гипотеза:** сокращение AS build/rebuild, повторное
  использование кластеров и экономия части памяти. Требуются реальные
  профили: scratch и дополнительные метаданные могут вырасти, ray traversal
  может замедлиться; нельзя обещать проценты экономии VRAM/FPS без замеров.

| Подэтап | Содержание | Acceptance gate |
|---|---|---|
| **G-CLAS.0 — Capability/SDK Audit** | Проверить текущую официальную DXR2 спецификацию и статус preview, Agility SDK/драйвер/DXC, `ClustersAndPTLASSupported`, tier/SM, поддерживаемые DXR pipeline/RayQuery flags. Выделить отдельный opt-in эксперимент: не обновлять основной shipping SDK ради preview. | Матрица GPU/драйвер/API и feature guards; unsupported устройства и обычный BLAS/TLAS гарантированно работают; никаких новых зависимостей DX11/R4. |
| **G-CLAS.1 — Реальная геометрия OGSR** | После работающего обычного G1 BLAS/TLAS сравнить на доступных совместимых реальных static/terrain мешах: classic BLAS+TLAS против CLAS→Cluster BLAS+TLAS; сохранить geometry/material IDs и hit-результат. | Сопоставимые ray hits, изображения, GPU Debug Layer/GBV; build/update GPU ms, trace GPU ms, VRAM/AS size, peak scratch и CPU submission; без допущения об ускорении. |
| **G-CLAS.2 — LOD, foliage и dynamic** | Отдельно исследовать крупные outdoor-секции, vegetation LOD, wind/animation, streaming и reuse кластеров. Skinned NPC сравнивать с классическим BLAS update/rebuild; не считать CLAS автоматически оптимальным для всех объектов. | Одни сцены, камеры и бюджет: cold/warm build, rebuild frequency, VRAM/residency, GPU time, визуальная и hit-корректность, teardown/reset/epoch. |
| **G-CLAS.3 — PTLAS partitions** | Отдельно сравнить TLAS rebuild/update с PTLAS на статичных секторах и меняющихся инстансах (NPC, foliage, LOD, streaming); оценить гранулярность partitions и lifetime. | Количество перестроенных partitions, build/update/trace GPU ms, память/scratch, корректность spatial transforms, partial updates, level reload; fallback на TLAS. |
| **G-CLAS.4 — Hybrid decision / optional integration** | В RT Geometry Manager выбирать Classic BLAS/TLAS или Cluster BLAS и/или PTLAS по capability, типу геометрии и профилю. Сохранить единые material/geometry IDs, owner fence/quarantine и GR Job System для CPU preparation без второго scheduler. | ADR с порогами на основании измерений и независимым review; стабильные RT-тени G3 и DX12 raster, отсутствие новых DX11/R4 регрессий. Если преимущества нет — оставить classic. |

**Риски и инварианты:** lifetime каждого CLAS должен переживать все Cluster BLAS,
которые на него ссылаются, и незавершённую GPU-работу; соблюдать AS barriers,
scratch allocation, epoch/generation, residency и owner fence/quarantine.
Разделить CPU-планирование существующим GR Job System и GPU-driven indirect
RTAS operations; **не** создавать второй scheduler. Не требовать массовой
переконвертации оригинальных OGF/DDS/THM. Отладка и perf должны охватывать
включение/отключение RT, game level teardown/reload и unsupported GPU.

**Официальные источники (документация, а не результат реализации):**

- [Microsoft: DXR Functional Spec, Part 2 — Clustered Geometry / PTLAS](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html).
- [Microsoft: DXR Functional Spec — tiers, capabilities, BLAS/TLAS](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html).
- [NVIDIA RTX Mega Geometry SDK — исследовательский референс](https://github.com/NVIDIA-RTX/RTXMG).

---
### X-Ray DX12/DXR — Hybrid Rendering Architecture (решение 2026-10-22)

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

### Совместимость материалов Legacy / Enhanced (проектное правило)

**Asset Compatibility First:** существующие OGSR/X-Ray текстуры, модели и материалы должны отображаться на DX12 и поддерживать начальные DXR-тени **без обязательной конвертации ассетов**. `Legacy Materials` сохраняют стиль старого контента через документированные fallback-параметры; `Enhanced Materials` — опциональные карты/метаданные для более точного PBR, мокрых поверхностей, отражений и расширенных эффектов. Это правило проектирования, а не подтверждённая реализация.

- **F4:** работающий Legacy raster path, сравнение с DX11 в одинаковой локации, при одинаковой погоде и освещении; корректные normal/gloss, alpha cutout и material lookup.
- **G1–G3:** BLAS/TLAS и первые RT-тени используют существующую геометрию, дополнительных roughness/metallic-карт не требуют.
- **G4:** обеспечить доступ к свойствам поверхности в точке ray hit (в том числе вне экранного G-buffer), alpha-tested foliage и динамические материалы; проверить обе модели материалов.
- **G5:** дополнительные карты повышают точность отражений/indirect lighting, но эти эффекты — отдельные необязательные рубежи.
- **Качество:** Legacy не обязан быть физически идентичен Enhanced; при этом отсутствие новых карт само по себе **не повод ухудшать** визуальную совместимость с DX11. Все эффекты проверяются A/B и GPU profiling.

Подробный контракт, ограничения DXR, правила fallback и матрица приёмки: [DX12/DXR Material Compatibility](dx12-dxr-material-compatibility.md).

### Условия G

- **Начинать G0/G1 после F3**, но встраивать RT в игровой кадр после F4.
- Не считать `D24_UNORM_S8_UINT` самостоятельным запретом DXR:
  acceleration structures строятся из геометрии. Формат depth и
  reconstruction выбирать исходя из конкретных RT/denoise-проходов.
- `metallic/emissive` не лежат в текущих четырёх MRT, а нормали и gloss
  уже есть: `common_iostructs.h:287-293`. Для качественных ray hits нужен
  способ получать свойства **попавшей** поверхности, в том числе вне экрана.
- NVIDIA **RTX** — линейка оборудования/фич; аппаратная трассировка в
  движке — через **Microsoft DXR**. DLSS Super Resolution не означает DXR.
- DLSS Frame Generation, Reflex и Ray Reconstruction — **отдельные
  интеграции** с собственными требованиями и тестами. Они не появляются
  автоматически при включении DX12.

### F3.5-IX — IX-Ray Multithreading Reference (deferred)

**Статус:** PLANNED / DEFERRED. **Приоритет:** medium. **Начало:** только после принятия текущего DX12 milestone и открытия F3.5 (после F3, до F4). **Сейчас никаких изменений runtime, Job System или DX11.**

**Цель:** использовать проверенные паттерны IX-Ray 1.6 как архитектурные референсы, не переносить его планировщик целиком. Исходники: <https://github.com/ixray-team/ixray-1.6-stcop>.

**Кандидаты на изучение:** `xr_task_group` (PPL/oneTBB), `xr_parallel_for`, задачи кадра `PreRenderThread` / `GameThread`, асинхронная подготовка деталей с двойной буферизацией (`DetailManager.cpp`), HOM/culling (`HOM.cpp`), синхронизация и жизненный цикл данных. Сначала проверить, какие эквиваленты уже существуют в OGSR, чтобы не дублировать код.

| Подэтап | Содержание | Гейт |
|---|---|---|
| **F3.5-IX.0 — аудит** | Сопоставить существующий OGSR Job System, frame loop, thread ownership и решения IX-Ray; проверить лицензии до копирования кода. | Карта потоков, сравнительная таблица, список точек интеграции и рисков. |
| **F3.5-IX.1 — решение** | Выбрать минимальные полезные изменения: task groups, ParallelFor, frame snapshots, зависимости и lifetime; оценить готовые schedulers вместо преждевременного собственного work stealing. | ADR с принятыми/отклонёнными решениями, без изменения production code. |
| **F3.5-IX.2 — прототип** | Только после отдельного ревью: изолированный CPU task prototype и, при необходимости, минимальная DX12 parallel command recording проверка с per-worker/per-frame allocators и sequential fallback. | Тесты race/deadlock/shutdown/resize, безопасный GPU fence lifetime. |
| **F3.5-IX.3 — измерения** | Сравнить 1/2/4/8 workers (по доступности), CPU frame time, p95/p99, task wait, scheduler overhead, GPU timeline. | Воспроизводимые замеры; оставить изменения только при доказанной пользе. |

**Оценка трудоёмкости (гипотеза до аудита):** IX.0 — 1–2 рабочих дня; IX.1 — 1–3; IX.2 — 3–7; IX.3 — 3–7. Всего **8–19 рабочих дней** при отсутствии серьёзной переделки существующей системы. Это не календарный срок и не обязательство реализовать все подпункты.

**Дальнейшее развитие:** F6 — интеграция/оптимизация по реальным профилям игрового DX12 кадра; H0 — ранние baseline-замеры; H1–H3 — сквозной аудит и адресная оптимизация AI/A-Life/physics/simulation **после G3 RT-теней и G5 RT-отражений**.

**Ограничения:** DX11 baseline не менять; не заменять рабочий scheduler без измерений; не вводить универсальный RHI или полный Render Graph; не менять текущий milestone; код IX-Ray напрямую не переносить без лицензионной проверки. Изменение этой записи не означает начала реализации.

---

## Этап H — Многопоточность и производительность всего X-Ray (после RT-теней и отражений)

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

## Порядок выполнения и зависимости

```text
База CoP / DX11:
    A ✅ DLSS 310.9.1, CoP runtime проверен
    B1 ✅ NIS submodule активирован; clean CI ещё проверить
         │
         ├──▶ B2.0/D0: общий compute smoke-test
         │       ├──▶ B2–B4: NIS SDK → DX11 интеграция → Lua UI
         │       └──▶ D1–D2: Hi-Z / DDA → STOP/GO → D3–D5 SSGI
         └──▶ C: DLSS quality / flags (независимо)

Параллельная стратегическая ветка:
    F0: аудит зависимостей DX11/DX12
         ↓
    F1: решение по границе backend и план миграции
         ↓
    F2: DX12 Clear/Present → F3: ресурсы/mesh/depth
         ↓
    F3.5: snapshot + frame scheduling + per-worker/per-frame allocators
         └──▶ serial/parallel recording и 1/2/4/8-worker гейты
         ↓
    F4.0: первый реальный DX12 кадр уровня → текстурированный preview
          ↓
    F4: полный перенос игрового кадра; F4-PBR.0–3 — параллельный PBR-трек
         ↓
    F5: temporal/DLSS/FSR3 + согласованные G-buffer/motion data
         ↓
    F6: стабильность и измеренное расширение parallel path
         ↓
    G0: DXR tier → G1: BLAS/TLAS → G2: DispatchRays
         ↓
    G3: RT-тени (без гейта новых PBR-карт)
         ↓
    G4: динамика + F4-PBR.4: material lookup readiness
         ↓
    G5: RT-отражения (PBR PASS); RTGI позднее по отдельному ADR
          ↓
    H1: аудит многопоточности ВСЕГО X-Ray → H2: точечные оптимизации
          ↓
    H3: масштабирование/регрессии; G5 (опц.): diffuse GI/denoise — отдельно

    H0: ранние baseline-замеры CPU/GPU до G3/G5 (параллельно F4–F6)

E: отдельные согласованные исправления техдолга, без bulk-cleanup.
```

**Необязательные зависимости:** NIS и SSGI на DX11 **не являются
блокерами** для F0–F4. F3.5 — обязательный **архитектурный** гейт перед массовым
переносом F4, но не требование ускорения на 8 workers. Прототип DXR G0–G2 может развиваться на небольшом
тестовом render path до завершения F5, но RT-функции в полноценном CoP
проходят интеграцию и приёмку только после F4/F6.

---

## Что сознательно не делаем без отдельного решения

| Идея | Причина |
|---|---|
| Полная замена DX11 в текущем R4 одним коммитом | Нет RHI; большой радиус регрессий, нечем откатиться |
| Обязательный программный BVH для DX11 SSGI перед DXR | Это иной проект; DXR BLAS/TLAS строятся отдельно |
| Автоматическое включение всех DLSS-флагов | Требуется соответствие motion vector / alpha / sharpening контрактам |
| Безусловная замена FSR3 на NIS как DLSS fallback | Нужны сравнительные тесты качества и latency |
| Полный RTX GI, path tracing и FG одновременно | Сначала RT shadows и GPU performance budget |
| Тотальная миграция всех шейдеров на SM6 сразу | Не является обязательным первым шагом DX12 raster |
| Перенос всей simulation/A-Life/job system в F3.5 | В F3.5 только рендер-границы и минимальный parallel prototype; оптимизация игрового движка остаётся H |

### Внешние технические ссылки для новых F/G (не результат локальной сборки)

- [Microsoft: Direct3D 12 Programming Guide](https://learn.microsoft.com/en-us/windows/win32/direct3d12/directx-12-programming-guide)
- [Microsoft: Changes from D3D11 to D3D12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/important-changes-from-directx-11-to-directx-12)
- [Microsoft: D3D12 raytracing feature query](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_d3d12_options5)
- [NVIDIA: Image Scaling integration](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling)

---

## Игровые данные — требование для повторения рантайм-тестов

**Рантайм baseline уже проверен в установленной CoP** (см. этап A).
При этом чистого клона репозитория недостаточно, чтобы повторить такой тест.
Для CoP требуется соответствующая версия игровых ресурсов/распакованная
`gamedata`, а не данные Shadow of Chernobyl. Оверлей форка:
`Game/Resources_CoP_1.6.02/gamedata/`.

В исходном roadmap записан состав оверлея (числа не перепроверялись при
правке этого файла):

```text
anims/ 6; levels/ 3; configs/ ~21; scripts/ 9;
shaders/ 552; textures/ 50 (~72,5 MB)
```

В GitHub реально присутствует путь `gamedata/scripts/` (нижний регистр).
Остальные точные имена каталогов проверять по дереву перед автоматизацией.

В репозитории отсутствует часть оригинальных данных CoP: конфигурации,
геометрия уровней, звуки, значительная часть текстур и скриптов.
**Для CI:** разделить smoke-test сборки без игровых данных и графический
runtime-test на доступной, легально установленной CoP с проверяемым набором
сцен. Не публиковать оригинальные игровые ресурсы вместе с артефактами CI.

---

## Примечание к редакции 2026-10-14

Эта редакция основана на **прикреплённом исходном `08-roadmap.md`** и
исследовании документов `cop-rtx/docs`. Подтверждённые владельцем результаты
A/B1 не переоценивались; добавлены **планируемые**, а не выполненные
F (DX12) и G (DXR). Принципиальные правки: NIS-входы SDK/compute-gate,
корректный путь Lua-UI, разделение SSGI и DXR, реалистичные проверки
resize/fallback, а также исправление устаревшего вывода «DX12 не делать».

---

## Этап I — World Partition и асинхронный World Streaming (план; не реализован)

**Цель:** по мере движения игрока подгружать и освобождать GPU/CPU-ресурсы секций мира без остановки кадра, сохраняя совместимость с классическими переходами между уровнями. **Это не тождественно frustum culling:** объект может не рисоваться, но оставаться резидентным; игровой AI/A-Life не должен автоматически выгружаться вместе с его рендер-представлением.

| Подэтап | Работа | Гейт / проверяемый результат |
|---|---|---|
| **I0 — аудит и метрики** | Исследовать существующие OGSR level geometry, details/trees, visual/LOD formats, spatial DB, lifetime и владение ресурсами, загрузчик уровней, Game/A-Life. Снять RAM/VRAM/frametime и причины блокировок в нескольких сценах; установить интерфейс секций без допущения о текущем формате данных. | Документ: что уже делится на части, что загружается монолитно, где безопасна выгрузка, матрица рисков. Никакого рефакторинга в I0. |
| **I1 — пространственные секции** | Прототип manifest/index с AABB/иерархическими ячейками и ссылками на ресурсы; поддержать неравномерное деление для леса, зданий, подземелий; reference counting общих ресурсов. | Дебаг-оверлей границ и список ресурсов секции; повторяемые тесты загрузки/выгрузки небольшого тестового участка. |
| **I2 — async IO / upload / eviction** | Рабочие очереди чтения/декомпрессии, CPU staging, D3D12 upload queues/fences, budgets, residency, безопасный deferred destruction после GPU fences; приоритет ближайших/ожидаемых секций, контроль thrashing. | Нет заметных main-thread IO stalls, use-after-free, GPU validation errors; RAM/VRAM и worst-frame измерены до/после. |
| **I3 — predictive streaming** | Приоритет от 3D camera position, frustum, скорости/направления и предполагаемого маршрута; hysteresis, предзагрузка на поворотах/при подъёме и запасные LOD при cache misses. | Тестовые маршруты с резкими поворотами и подъёмами, статистика late loads и видимых pop-in; отсутствие жёстко фиксированного радиуса в качестве единственного критерия. |
| **I4 — compatibility / stress** | Сохранение старого режима загрузки уровней, дебаг-инструменты, memory pressure, частые перемещения, alt-tab/resize, cold/warm cache, DX11 fallback. | Откат одной настройкой/сборкой; длительная сессия без утечек и повреждений ресурса, критерии измеряются на фиксированных маршрутах. |

**Зависимости:** I0 можно исследовать параллельно F3–F4; GPU-resident streaming внедрять после F4 и базовой синхронизации F6 (с использованием правил ownership/snapshot из F3.5). Не делать streaming обязательным блокером G0–G3. Не обещать абсолютного отсутствия stalls до замеров и настройки бюджета.

## Этап J — Height-Aware Visibility, LOD, растительность и тени (план; не реализован)

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

## Этап K — Seamless World / экосистема бесшовных модов (долгосрочно, не реализовано)

**Цель:** после внедрения I/J предоставить техническую возможность создавать бесшовные объединённые регионы без экранов загрузки. Это **не отдельный режим рендерера или движка** и не обязательное автоматическое объединение старых уровней. Существующие level transitions и моды продолжают работать; новые проекты смогут использовать streaming для перехода между регионами без загрузочного экрана.

| Подэтап | Работа | Гейт / проверяемый результат |
|---|---|---|
| **K0 — world composition contract** | Global transform/coordinates (при больших размерах camera-relative GPU positions / precision strategy), per-level offsets, границы, порталы и региональные метаданные, отдельная система абсолютной высоты J1. | Технический формат сцепления двух тестовых уровней и правила миграции/версий, без копирования игровых ассетов в репозиторий. |
| **K1 — render-only crossing** | Бесшовный рендер двух тестовых регионов на I/J, не обещая seamless AI; текстуры, геометрия, небосвод и освещение непрерывны. | Проход через границу без загрузочного экрана и заметного скачка геометрии/экспозиции, фиксированные frametime/VRAM. |
| **K2 — gameplay persistence** | Сериализация/переход владения игровыми сущностями, A-Life offline/online, межрегиональные navmesh/AI paths, физика, звуки, задания, сохранение/загрузка, спавн. | NPC/объекты/квесты не дублируются и не пропадают, долговременный переход туда-обратно и save/load воспроизводимы. |
| **K3 — modding tools и compatibility** | Утилита сборки world manifest, валидатор границ/высоты/LOD, диагностические overlay, версия формата; единая архитектура world streaming для обычных и бесшовных локаций без отдельного переключателя «Classic / Seamless». | Малый пример бесшовного мода и документация для моддеров; существующие переходы CoP и старые моды не регрессируют. |

**Не включено автоматически:** объединение всех канонических уровней CoP, seamless A-Life, единая глобальная навигация или сохранность существующих модов при включённом новом режиме. Каждое направление требует самостоятельного теста. Зависит от I и J; K2 не блокирует K1.

## Этап L — Переносимый DX12/DXR модуль и возможное сотрудничество с IX-Ray Platform (предложение, не согласовано)

**Цель:** сделать результат полезным вне OGSR. **IX-Ray Platform — потенциальный партнёр, не заявленный участник проекта и не получатель согласованного кода**. Никаких утверждений о сотрудничестве до контакта и согласия команды.

| Подэтап | Работа | Гейт / проверяемый результат |
|---|---|---|
| **L0 — legal / technical feasibility** | Сравнить лицензии, provenance кода/шейдеров, сторонние SDK, актуальные renderer interfaces OGSR и IX-Ray, форматы ассетов, toolchains и API; определить допустимые условия перераспределения. | Compatibility matrix и архитектурное ADR; без предположения, что весь OGSR backend можно просто скопировать. |
| **L1 — backend boundaries** | Минимизировать зависимости D3D12 device/queues/descriptors/PSO/resources/RT path от OGSR game layer, API-specific adapters и ownership; не переписывать уже работающий рендер без доказанной причины. | Изолируемые компоненты, минимальный integration harness, документированная зависимость на игровые типы. |
| **L2 — reproducible technical preview** | После F4/F6 и достаточной зрелости G3 собрать пример, test scenes без лицензируемых игровых данных, CI build-only + отдельные GPU runtime измерения, feature matrix, известные ограничения и DX11 fallback. | Повторяемый preview с проверяемыми RT shadow результатами и реальными GPU timings; DXR не заявлять готовым раньше G. |
| **L3 — outreach / optional integration** | Подготовить техническое предложение IX-Ray Team с краткими ограничениями, ссылкой на репозиторий, bench evidence и возможным небольшим экспериментальным портом, **только после самостоятельного решения о публикации**. | Заинтересованность/обратная связь команды; дальнейшие действия зависят от их решения. |

**Общая стратегия:** OGSR-Engine-rtx сохраняется самостоятельным экспериментальным форком. Не менять лицензии, авторство и attribution без юридической проверки; не заявлять совместимость с IX-Ray до прототипа.

### Межэтапные приоритеты и ограничения I–L

1. **Ближайший исторически записанный шаг:** F3 — минимальная DX12 геометрия. **После достижения тестовых F3.x — F3.5 до массового F4**; I–L не сдвигают графический путь F/G. Актуальность F3-статуса проверять по коммитам и GPU-логам, а не по этой записи.
2. **До окончания F4:** допустим только исследовательский I0/J0/L0; не смешивать массовое переписывание загрузчика/игрового мира с DX12 bring-up.
3. **После игрового кадра F4/F6:** секционный streaming I, затем Height-Aware LOD/vegetation/shadows J, поэтапно и с контролем DX11.
4. **DXR G и streaming I/J могут развиваться параллельно:** RT streamer J5 интегрировать только когда базовые BLAS/TLAS и foliage path валидны; полное K не является предпосылкой аппаратных RT-теней.
5. **K (seamless) — долгосрочная возможность, возникающая на базе I/J**, а не отдельный режим или обязательный deliverable первой DX12/DXR версии. Классические переходы остаются поддерживаемыми как контентная механика; L — необязательный путь сотрудничества после проверки лицензий/переносимости.
6. **Контроль качества:** fixed routes — Кордон/лес, Припять/улица и крыша, холм/горизонт, помещение/подвал, переход через границу двух тестовых регионов; capture screenshots/video, sudden pop-in counters, frame-time percentiles, GPU/CPU times, VRAM/RAM peak и resource residency. Численные целевые пороги назначать только после baseline.
7. **Никаких ложных PASS:** каждый milestone требует соответствующий commit, воспроизводимые логи/тесты и указание реальных ограничений. Streaming, RT или seamless не считать реализованными из факта DX12 bootstrap.

**Статус I–L на 2026-10-15:** ⬜ **PLANNED / NOT STARTED**. Вся эта секция — roadmap proposal, не реализованная функциональность.

---

## Этап V — Volumetric Clouds (самостоятельный параллельный DX12-трек; план)

**Решение 2026-10-22:** выделить процедурные объёмные облака из дорожной
карты F в независимое направление **V** (Volumetric Clouds). Работу над V0–V2
можно вести **параллельно F4.0** в отдельном чате/рабочей ветке с общими
техническими стандартами DX12. **Ни F4.0 PASS, ни F4/F5/G не зависят от V**;
готовность standalone-прототипа не означает его интеграции в игру.

**Визуальный ориентир V (решение 2026-10-22):** облачная система
*Red Dead Redemption 2* — основной **референс по визуальному качеству**
(масштаб, трёхмерная морфология, внутреннее освещение, самозатенение,
погодные переходы, стабильная картинка), но **художественная идентичность
S.T.A.L.K.E.R. сохраняется** (холодное пасмурное небо, тёмные дождевые фронты,
атмосфера Зоны). Это цель для V1–V5, **не готовая реализация, не копирование
закрытых технологий Rockstar и не новый гейт V0**. Критерии качества,
погодные сценарии и ограничения описаны в
[Volumetric Clouds — Visual Target](dx12-v-cloud-visual-target.md).

**Научная морфология и переходы (решение 2026-10-23):** долгосрочно
поддержать **10 основных родов облаков WMO**, в том числе **тонкие
перистые/волокнистые формы**, а также логические high/middle/low и
вертикально развитые облачные слои, совместное существование нескольких
форм, независимую адвекцию ветром на высотах и постепенное образование,
распад и смену пространственных погодных фронтов. **Не принуждать все
облака выглядеть как Cumulus; не считать отдельный похожий фрактальный
узор автоматически физически достоверным Cirrus.** Визуальные переходы
опираются на `CEnvironment`, но не пытаются заменить игровую систему
погоды физической CFD-симуляцией. Архитектура и требования к тестированию:
[Scientific Cloud Taxonomy & Transitions](dx12-v-scientific-cloud-taxonomy-and-transitions.md).

**Исторический гейт/приоритет на 2026-10-23:** после V2.1 технического
checkpoint **визуальная морфология ещё не была принята**; задача DeepSeek
тогда — **V2.2** (высотная геометрия, крупные объёмы, erosion, seam и
performance), не полная реализация 10 родов. На 2026-11-01 это уже не
текущая задача: см. ниже независимый аудит V2.3h-A03b-R02 и план R03A.
Multi-layer расширение — **отдельный последующий этап после V2 PASS и
архитектурного review**. Вводить новые слои только с учётом стоимости GPU
и возможности дешёвых альтернатив ray marching.


**Требование совместимости и выбора:** DX12 поддерживает два альтернативных
варианта облаков в пользовательских настройках графики:

- **Classic 2D — по умолчанию:** сохранить оригинальные облачные текстуры,
  погодную семантику, внешний вид и анимацию X-Ray. Реализацию классического
  пути потребуется адаптировать для DX12; наличие работающего DX11-пути само по
  себе не доказывает готовность DX12-варианта.
- **Volumetric 3D — опционально:** процедурный 3D noise, ray marching,
  освещение облачного объёма и последующие оптимизации.
- В один момент активен **один** облачный путь; режим выбирается через
  настройку графики и сохраняется в пользовательском конфиге. Предусмотреть
  безопасный возврат к Classic 2D при недоступности/ошибке Volumetric 3D,
  корректные resource lifetime/barriers и сброс temporal history при смене
  режима. DX11/R4 **не изменять**.

**Архитектурная граница:** отдельный модуль внутри DX12 renderer/standalone
harness с общим интерфейсом cloud pass. Зависимости вводить через параметры
камеры, солнца, ветра, погоды, цели HDR и глубины (последние два входа нужны
только на соответствующем интеграционном этапе). Не подключать модуль к
игровому frame loop на ранних V0–V2; не копировать игровой renderer внутрь
облачного модуля. Реализация ray marching — compute shaders D3D12, **без
обязательной зависимости от DXR**.

| Подэтап | Работа | Гейт / проверяемый результат | Зависимости |
|---|---|---|---|
| **V0 — module bootstrap и noise** | Изолированный DX12 cloud harness; compute PSO, SRV/UAV, 3D Perlin-Worley/base noise, detail noise и debug-визуализация. | Воспроизводимая генерация/чтение 3D-поля, работающая отладочная визуализация; Debug Layer без ошибок; DX11 baseline неизменен. | DX12 device/queue и минимальные compute primitives; **параллельно F4.0**. |
| **V1 — ray marching** | Облачный высотный слой, density sampling, светопропускание, освещение солнцем, движение от тестового ветра, корректное ограничение шагов. | Видимые объёмные облака в standalone DX12; управляемые параметры, captures и GPU pass timing; нет обязательных игровых зависимостей. | V0; **параллельно F4.0**. |
| **V2 — standalone quality gate** | Карта покрытия/погоды с mock-данными, базовый self-shadowing, debug modes, тесты крайних параметров, оптимизация шагов и профилирование. | Зафиксированы A/B captures, GPU ms/VRAM на доступных GPU и известные ограничения; автономный cloud renderer не меняет F4 runtime. | V1; **параллельно F4.0**. |
| **V3 — DX12 sky integration** | Подключить облачный выход к игровым camera/depth/HDR/sky-проходам, согласовать цветовое пространство и порядок композиции; тест на реальной локации. | Облака отображаются внутри игрового DX12 кадра без нарушения depth, exposure, horizon и DX11 fallback. | Нужны соответствующие проходы **полного F4**, не часть F4.0 PASS. |
| **V4 — Classic/Volumetric switch** | Адаптировать оригинальные 2D-облака в DX12, реализовать cloud mode в настройках графики и конфиге; два взаимоисключающих render paths, safe fallback. | Classic 2D визуально сверены с DX11; выбор сохраняется и не нарушает reload/resize; отсутствие 3D не ломает игру. | V3 и legacy DX12 sky path в F4. |
| **V5 — temporal, weather и shadows** | Temporal reprojection/history reset, upscale и spatial filtering, интеграция с `CEnvironment` (ветер, солнце, осадки/циклы), опциональная карта теней облаков и performance tuning. | Стабильное изображение в движении, day/night/weather transition без критичных артефактов, A/B Classic/Volumetric, документированные GPU ms, VRAM и ограничения. | V4; соответствующие temporal/lighting возможности **F4/F5**. |

### V-Q — ближайший приоритет: стабильное качество и интеграция существующих облаков (решение 2026-11-07)

**Решение / приоритет:** прекратить серию новых визуально-морфологических
экспериментов ради самих экспериментов. Довести уже существующий **DX12
volumetric cloud pass** до **стабильного, производительного и визуально
приемлемого** состояния, сохранив художественную идентичность Зоны.
Новые архитектуры/поля плотности, FOMS, parameter sweeps и очередные
V-Visual-итерации **не запускать по инерции**; возвращаться к ним лишь
при конкретном подтверждённом блокере после отдельного решения.
Не требовать идеальной симуляции всех типов облаков для первых игровых кадров.

**Закреплённая художественная база и доказанные границы:**

- **Original V-SF Rain** — основной неизменяемый художественный кандидат
  **локальных плотных дождевых облаков**: Gate A `PROMISING`,
  Gate B (8°/18°/30°) `VISUAL PASS WITH NOTES`. Нравящуюся мягкую форму,
  боковую глубину и локальный просвет сохранить; небольшая плоскость
  основания — известный визуальный компромисс на текущем этапе.
- **V-SF Rain Gate C** (локальная вариация нижнего fade/baseY) —
  `NO IMPROVEMENT`; патч не переносить в основной вариант.
  **V-03** остаётся независимым визуальным/техническим контролем.
- **Storm/Cumulonimbus** не считать решённым: V-Visual-04–07 и
  V-SF-GPU-01 как грозовой фронт не приняты; не возвращать V-04 с
  угловатыми формами в качестве художественного default.
- **V2/V3 = NOT ACCEPTED**. V-Q1 / Gate D: `VISUAL PASS — NO HIGH-RESOLUTION DEGRADATION` (1600×900); V-Q2 / Gate E: `REPORT COMPLETE WITH NOTES` (standalone GPU, unequal cloud coverage/clocks, **не equal-cloud claim**). V-Q3: standalone extraction `PASS WITH NOTES`, game-side integration `BLOCKED` до наличия game-owned DX12 camera/HDR/depth/descriptor/frame. Игровой cloud runtime `NOT INTEGRATED`.

**Ближайший порядок без повторного морфологического исследования:**

1. **V-Q1 — закрепить baseline:** original V-SF Rain, исходники, команды,
   seed/камеры, PNG, shader hashes, 800×450 и 1600×900 A/B;
   проверить сохранение внешнего вида на высоком разрешении.
2. **V-Q2 — измерить стоимость:** same-session GPU timestamp timings,
   median/p95, частоты/мощность/температуру и долю покрытия кадра;
   отдельно сравнить фиксированные 128 шагов и сопоставимую физическую
   длину шага при разных bounds V-SF/V-03. Не выдавать разницу покрытия
   облаками за ускорение равного объёма работы.
3. **V-Q3 — подготовить перенос существующего cloud pass:** контракт
   camera/sun/weather/wind, HDR sky color, scene depth, cloud output
   (scattered RGB + transmittance, не обычная alpha opacity),
   compositing, SRV/UAV barriers, resource lifetime и resize/reload.
   Не переписывать шейдер и не переносить отложенные экспериментальные
   патчи без обоснования.
4. **V-Q4 — ранний in-game proof:** **после появления пригодного стабильного
   игрового DX12 color/depth/камеры в F4.0** подключить original
   V-SF Rain и получить реальные игровые кадры; сначала узкий opt-in
   preview, затем тесты камеры, горизонта, экспозиции, GPU time,
   level reload и DX11/R4 fallback. Это **не** production V3/V2 PASS;
   полноценная V3-интеграция по таблице выше остаётся отдельным гейтом.

**Синхронизация с F4.0:** подготовку V-Q1–V-Q3 выполнять параллельно
развитию игрового DX12. Ориентир «базовый DX12 за ~две недели» является
планом команды, **не гарантией и не доказательством готовности**.
Не задерживать ранний игровой cloud proof до полного DXR/PBR,
но и не подключать облака до корректных camera/target/depth/barriers.
Не менять DX11/R4 и production без отдельной приёмки/разрешения;
любые Git-изменения — отдельным согласованным scope.


### V-WEATHER — погодные режимы, переходы и настройки облаков (решение 2026-11-08)

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

**Статус на 2026-11-08 (по independently audited evidence ZIP):** V-WEATHER-01A static modes **`PASS WITH NOTES`**: ZIP SHA/CRC, manifest 105/105, original/patched PNG 33/33, Gate B/Gate D original V-SF Rain 6/6, source patch после нормализации CRLF применим, представленные Windows Debug Layer/GBV логи 9/9 (0 InfoQueue errors/warnings). Это только статические standalone режимы; **не** отдельная hardware GPU переиспытательная приёмка аудитором. Не проверен shipping-off game build; developer CLI пока guard-ится в standalone, V-WEATHER-01C остаётся обязательным. V-WEATHER-01B transitions `NEXT / NOT RUN`, V-WEATHER-01C settings/shipping gating `NOT RUN`; V-Q1 visual `PASS`, V-Q2 standalone perf `COMPLETE WITH NOTES`, V-Q3 standalone extraction `PASS WITH NOTES`, game-side `BLOCKED`; V2/V3 `NOT ACCEPTED`.

### V2-O — оптимизация без потери уникальности облаков (решение 2026-10-27)

**Неприкосновенный принцип: `Cloud uniqueness first — reuse computation, not cloud identity`.**
Оптимизация ray marching должна **переиспользовать только те данные и
вычисления, которые уже совпадали бы без оптимизации**, и не должна
искусственно увеличивать количество одинаковых облаков, повторять готовые
силуэты или превращать небо в равномерно заполненный клонами тайл.
**План / NOT IMPLEMENTED:** запись в roadmap не означает наличия кеша,
пропуска пустого пространства или принятого V2 visual gate.

**Разделение естественного повторения и тайлинга:**

- **Допустимо:** метеорологически обусловленные волны, ряды и ячейки
  (*undulatus* / слоистые поля), где повторяется **закономерность**,
  но отдельные элементы имеют естественные различия размера, высоты,
  плотности, промежутков и формы. Их ритм задаёт погодная морфология,
  а не необходимость многократно использовать один и тот же рендер-результат.
- **Недопустимо:** очевидное клонирование одной облачной массы по границам
  NoiseVolume/WeatherMap, одинаковые силуэты через фиксированное расстояние,
  искусственная синхронность отдельных облаков и упрощение weather/type
  распределения ради высокого cache hit rate.
- Периодический 3D noise tile с `WRAP` **может оставаться общим входом**,
  но не должен быть единственным источником уникальности окончательной
  облачной геометрии. Крупномасштабные world-space weather/shape поля,
  локальные параметры и развитие во времени сохраняют разнообразие
  облаков. Никаких новых крупных повторений ради оптимизации.

**Допустимые кандидаты на ускорение (после принятой базовой морфологии):**

1. **Conservative empty-space skipping / occupancy hierarchy:** пропускать
   только области с доказанной нулевой итоговой плотностью; границы
   должны учитывать coverage, type/height profile, warp, erosion,
   анимацию и используемые пороги. Дешёвая проба без доказанной
   верхней оценки плотности **не даёт права** удалять тонкие края и
   слабые облачные элементы.
2. **Adaptive/coarse-to-fine ray marching:** увеличивать шаг там,
   где это не искажает структуру, и уменьшать возле видимых границ;
   измерять пропущенные тонкие детали, banding и temporal popping.
3. **Переиспользование существующих noise/density данных:** повторять
   результат только при совпадении **всех влияющих входов** (world-space
   координаты, seed, форма, погодный тип, coverage, высота, ветер/время,
   параметры модели). При изменении входов — пересчёт либо корректная
   инвалидация. Совпадение периодических координат базового шума
   **не означает** совпадения итоговой плотности.
4. **Lighting/temporal caches — отдельный гейт:** конечное освещение
   дополнительно зависит от солнца, затенения соседними массами,
   атмосферы и вида камеры; нельзя переиспользовать его только потому,
   что совпал базовый noise. Temporal reuse требует корректной
   reprojection, rejection и history reset.

**Гейт качества и доказательства до включения оптимизации по умолчанию:**

- A/B **optimization OFF vs ON** при одинаковых камере, seed, мировых
  координатах, погоде, времени, ветре, освещении и пресете; заранее
  фиксировать допустимые погрешности изображения и плотности.
- Многокилометровые camera sweeps и разные погодные циклы: проверять
  соседние и удалённые области на **искусственные совпадения** силуэтов,
  повтор тайла, утрату локального разнообразия, исчезновение тонких
  просветов/волокон и чрезмерно однородное заполнение неба.
- Отдельно проверять ожидаемые **естественные волновые серии** и
  нежелательные идентичные клоны: регулярность ритма сама по себе
  не является дефектом; точная повторяемость готовой массы — кандидат
  на визуальную регрессию.
- Зафиксировать GPU median/p95, число фактических primary/shadow
  samples, ранние выходы и VRAM; проверять стабильность в движении
  (ghosting, popping, shimmer) и DX12 Debug Layer/GBV.
- **Если оптимизация ускоряет GPU ценой утраты облачной уникальности,
  её visual gate — FAIL независимо от выигрыша в миллисекундах.**

**Порядок:** сначала закрыть визуальные дефекты и принять базовую
3D-морфологию V2; затем испытывать V2-O отдельными переключаемыми
кандидатами. Не задерживать F4.0 и не подключать V2-O к игровому
DX12, V3–V5, молниям или выбросам преждевременно.

**Правила параллельной работы и слияния:**

1. Работы V0–V2 вести отдельно от критического пути F4.0 в
   `feature/dx12-volumetric-clouds` (ветка создана; рабочие изменения V0/V1
   не считаются опубликованными без отдельной проверки коммита и push).
   Повторно использовать общие DX12-инструменты, но не изменять без
   согласования ownership, frame scheduling и базовые ресурсы F4.0.
2. Интерфейс cloud pass и соглашения по shader compiler, root signatures,
   ресурсным состояниям, descriptor lifetime и fences документировать до
   интеграции; проверять соответствие
   [Microsoft Direct3D 12 Programming Guide](https://learn.microsoft.com/en-us/windows/win32/direct3d12/directx-12-programming-guide).
3. Интеграцию V3–V5 делать отдельными проверяемыми изменениями **после**
   появления необходимых F4/F5 data paths; прототип не считать production
   готовым по факту отдельного окна или успешной сборки.
4. **Classic 2D остаётся вариантом по умолчанию**, в том числе для игроков,
   предпочитающих оригинальный стиль. Новый режим должен быть **опцией**, а
   не принудительным визуальным изменением всего проекта.
5. Каждому PASS нужны исходный commit, воспроизводимые build/runtime steps,
   GPU captures/тайминги, визуальные сравнения, тесты init/shutdown,
   resize/reload и явные известные ограничения. Не объявлять GPU performance
   без измерений.

**Статус V на 2026-10-23 (раздельно подтверждение и слова разработчика):**

- **V0 — PASS WITH NOTES по независимому source/delta audit** предоставленного
  финального пакета: 3D-шумы, самостоятельный EXE, negative validation tests.
  Подтверждение Windows GPU-испытаний основано на приложенных логах разработчика,
  а не на повторном запуске аудитором. Локальная Git-фиксация/публикация исходников
  V0 не подтверждена; решение об интеграции в игровой DX12 не принималось.
- **V1 — IN PROGRESS / NOT YET ACCEPTED:** разработчик тестирует ray marching;
  пользователь сообщил, что увидел **небольшие фрагменты облаков** в тестовом
  приложении. Это не доказывает завершения V1.1, пригодности освещения,
  непрерывности облачного слоя, производительности или корректного поведения
  камеры. **Нет предоставленного завершённого V1 audit package, GPU timings
  и независимого V1 PASS**. До получения отчёта и исходников не объявлять
  ни V1.1, ни V1 завершёнными.
- **V2 — PLANNED / NOT ACCEPTED:** визуальная связность, крупномасштабные
  структуры, морфология, погодные пресеты, стабильность в движении,
  GPU-производительность и устранение артефактов ещё предстоят.
- **V3–V5 — PLANNED:** игровая интеграция, настройка Classic 2D / Volumetric 3D,
  подключение реальных погодных циклов и окружения отсутствуют.
- **V-L — DEFERRED / NOT STARTED:** молнии являются опциональным направлением
  внутри V; реализация запрещена до cloud quality gate ниже.

### V2.3h-A03b-R02 — независимый аудит и следующий гейт (2026-11-01)

**Источник/пределы доказательств:** полученный отдельно от репозитория
`OGSR_V2_3h_A03b_R02_Independent_Audit_2026-11-01.md` и CSV с независимыми
метриками (24 строки). Аудит — offline-инспекция предоставленного пакета,
его CPU-геометрии и evidence. **R02 HLSL, DX12/GPU, weather/Storm runtime и
temporal GPU tests не выполнялись.** Ни новые GPU PASS, ни публикация
исходников R02 этим обновлением не подтверждаются. Сообщения разработчика
о Legacy V2.3 / Storm S2new pixel identity 9/9 не являются независимым
Windows replay.

**Решение: R02 BLOCKED на CPU geometry gate, STOP до GPU/HLSL; V2 NOT
ACCEPTED.** Это корректно остановленный исследовательский эксперимент,
а не успешная реализация новой облачной морфологии.

| Гейт R02 | Итог независимого аудита |
|---|---|
| Целостность и воспроизводимость | PASS: ZIP CRC, manifest 345/345; R01→R02 patch, 136/136 source snapshot |
| D — diagnostics | PASS WITH NOTES: 10/10 unit tests |
| G — CPU 3D geometry | BLOCKED |
| V — vertical morphology | BLOCKED |
| U — spatial uniqueness / morphology | BLOCKED |
| K — computational feasibility | BLOCKED как риск по CPU, **не** GPU benchmark |
| C — weather; S — Storm | NOT RUN |
| R02 HLSL/DX12/GPU/temporal | NOT RUN |

**Числовые результаты, не смешивать каналы:** в HCS1 при seed 1337,
threshold 0.2, 16 км occupancy XZ = 30.154%, крупнейшая компонента
occupancy = 46.515%; на 60 км occupancy XZ = 42.703%, крупнейшая компонента
occupancy = 97.581%, но **по final density > 0.2 — 84.045%**.
Среди шести локальных seed HCS1 occupancy XZ при cut 0.30 варьирует
от 0 до 69.195%. HCS2 сохраняет однотипную округлую морфологию;
заявленные 129 3D-компонент HCS2 независимым пересчётом не подтверждены,
поскольку полный поддерживающий 3D binary volume отсутствовал.

**Evidence gap:** независимая проверка охватила **41/41 приложенных Float32
полей, 6 549 184 значений, 0 NaN/Inf**. Производитель заявлял 53 поля и
7 540 416 значений; **12 полей / 991 232 значения отсутствовали в
переданном ZIP**. Публиковать 53/53 как независимо подтверждённые нельзя.
Статистику components, рассчитанную по промежуточному `occupancy > 0.2`,
нельзя подменять статистикой `final density > 0.2`.

**Цена модели (сведения из CPU/Python producer environment):** HCS1 —
55.8 с/млн evaluations, 92 hash evaluations/sample; HCS2 —
26.7 с/млн, 18 site evaluations/sample. **Это не GPU ms и не прогноз для
RTX 5070.** Большая связность сама по себе не дефект Overcast/Storm;
BLOCKED основан на совокупности морфологических проблем, нестабильности
между seed/регионами, слабого вертикального развития и риска стоимости.

**Сохранить:** Legacy V2.3 — default; Storm S2new — независимая
контрольная модель. Не заменять рабочие варианты HCS1/HCS2.
Отдельные прежние PASS не повышаются до нового V2/GPU PASS.

**Следующее действие — R03A: Cloud Morphology Reference & Architecture
Decision (PLANNED / READ-ONLY, NOT STARTED).** Сначала выбрать
геометрически осмысленный подход по WMO/визуальным референсам и сравнению
архитектур; зафиксировать матрицу нескольких seed, world regions,
16/60 км, XZ/XY/YZ и occupancy/final-density, отдельные целевые погодные
статистики и предварительный work-per-sample budget. **Не начинать
очередную крупную шумовую систему, HLSL-port или R03B автоматически.**
R03B — только малый CPU reference после отдельного R03A review.
Никаких изменений DX11/R4, F4.0 runtime, существующего облачного освещения
или игрового weather manager.

**Git provenance (сверка 2026-11-01):** опубликованная ветка
`feature/dx12-volumetric-clouds` указывала на `b38ff438` (2026-10-22);
публикация R02 source в этой ветке не подтверждена. Этот roadmap-коммит
фиксирует только документарное решение, без переноса источников или
выполнения тестов.

### V-L — Lightning in Volumetric Clouds (отложенное расширение)

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

**Приоритет сейчас:** закончить V1, получить полный отчёт + исходники,
независимо проверить, затем стабилизировать V2. **Не начинать молнии сейчас.**

### V-S — Weather Cycle & Surge Visual System (отложенное расширение)

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

**Жёсткий запрет:** пока не работает и не принят полноценный облачный
рендерер (V1 + V2), **DeepSeek не начинает молнии, аномальное небо или
интеграцию выброса**. Сейчас пользователь видел только небольшие
фрагменты V1 облаков; текущий прогресс неизвестен без полного
отчёта, исходников, GPU timings и captures.

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

## Этап P — Portable PhysX Ragdoll: OGSR DX11 → IX-Ray (G4.2-R1 experimental review accepted; G5 planned)

**Первый приоритет направления P (решение 2026-10-22): ragdoll-физика персонажа.** Самый первый игровой результат — **один NPC-сталкер, у которого анимированный скелет переключается на PhysX CPU ragdoll и сталкивается со статической геометрией настоящего игрового уровня на OGSR DX11/R4**. Оригинальное передвижение и AI остаются на ODE. Дополнительные PhysX-ящики — только инструмент тестирования, не самостоятельный игровой milestone. Полная замена character movement и GPU PhysX отложены.

### Актуальный checkpoint P3.3 — G4.2-R1 → G5 (2026-11-06)

**Ранее принятый milestone:** `P3.3-G4.2-R1` — **EXPERIMENTAL PASS WITH NOTES** по результатам предыдущей независимой проверки `p33g42r1-review.zip`. Из переданного текста review: 22 ZIP-записи (CRC PASS); source diff 5 файлов (+489/−79); Release x64 `XR_3DA` сборка успешно по журналу; CTest 10/10; `git diff --check` exit 0 по отчёту. При трёх загрузках уровня подтверждены три регистрации/снятия diagnostic callback и три успешных visual debug draws (`shapes=21 joints=10`); после unload `ReleaseAllV2 remaining=0 live=0`, DLL lease refs возвращаются к 0. В all-human v2 устранены несинхронизированные обращения к registry/lastBodyWorld; snapshot-by-value отрисовывается после освобождения mutex, callback снимается до уничтожения scene/level. **Эти свидетельства не перепроверялись здесь по оригинальному ZIP или новому игровому запуску.**

**Git provenance:** SHA `a02eca549b05e057a9fe6c1542bd0d5d9918a7d8` указан в принятом отчёте, но не найден через GitHub commit API для `rafaelisraelyan/OGSR-Engine-rtx` в ходе текущей сверки. Не считать R1 **опубликованным / INTEGRATED**, пока не проверены фактический remote HEAD, ancestry и Git blobs. Roadmap обновляется как учёт достигнутого *по evidence*, а не подтверждение наличия этого исходного кода в `prypiat`.

**G5.0 remote-source audit (2026-11-06):** в текущем `prypiat` каталоги `ogsr_engine/Layers/PhysicsPortable`, `PhysicsPhysX`, `PhysicsAdapterOGSR` **не обнаружены**. Проверена резервная ветка [`backup/physx-p31c-20261029`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/tree/backup/physx-p31c-20261029) (`2253acd743725d1799b2759a8ed6462a099d694f`): код и документация соответствуют историческому **P3.1-C-R3**, не принятому **G4.2-R1**. Ветки разошлись (backup относительно `prypiat`: +6/−48 commits); перенос backup целиком запрещён без отдельного source/ABI review. **G5.0 = BLOCKED — R1 source provenance**, clean Windows build/CTest в рамках текущей сверки NOT RUN; G5.1 NOT ACCEPTED. Подробное read-only задание: [PhysX G5.0 provenance audit](physx-p3-3-g5-0-provenance.md). GitHub Issues в этом репозитории отключены; задача отслеживается документацией/roadmap.

**G5.0 независимая проверка evidence (2026-11-06): `READY FOR INTEGRATION REVIEW — CONDITIONAL`, не `G5.0 PASS`.** Принят архив `G5.0-Provenance-Review.zip`: внешний ZIP 10 записей, CRC PASS; `ARCHIVE-MANIFEST` 9/9; вложенный R1 ZIP 22 записи, CRC PASS; `G5.0-Evidence-Manifest` 19/19 предоставленных файлов совпали, один внешний game-derived fixture **не предоставлен**; R1 patch 29 450 bytes, diff 5 файлов (+489/−79); восстановлены 3/3/3 collider registration/unregistration/draw и исторические CTest 10/10 по логам. Командный отчёт подтверждает **локальное** наличие коммита `a02eca5`, parent `7778170d`, tree `bd235c15`, но напрямую проверить локальные Git objects по данному ZIP невозможно: предоставлен R1 delta patch, а не полный R1 source tree. Legacy callback остаётся отдельным fix; внешний fixture — обязательный clean-CTest gate; no-vis crash — UNRESOLVED; fresh build вместе с DX12 — NOT RUN. **Следующее действие:** review branch от актуального `origin/prypiat`, только whitelist интеграции и отдельная независимая проверка, **не** push старого PhysX целиком в `prypiat`. [Независимый G5.0 review](physx-p3-3-g5-0-independent-review.md); [G5.0 audit plan](physx-p3-3-g5-0-provenance.md).

**G5.0-I1 — Source Integration Review (2026-11-06): `CONDITIONAL PASS / MERGE HOLD`.** По предоставленному `G5.0-I1-Review.zip` SHA-256 `64C5984733AE17543B3A830D8E6E3C2E85D77B7D0C455F5FA89DA79EA44924CD`: 36 ZIP entries, CRC PASS, evidence SHA 35/35; patch 110 file sections (109/110 воспроизведено через `git apply --check` на реконструированной базе, один ADR не проверялся из-за отсутствия файла в старом локальном ZIP); 16/16 engine old Git blobs match; PhysX R1 runtime blob и узкий v1→v2 callback fix сопоставлены с original R1 Git blobs. По журналам CTest 11/11 с внешним fixture, **10 PASS + 1 SKIPPED** без него; feature-on/off/diagnostic/feature-off-workaround builds succeed; v2 3 register/3 unregister/3 draw, 9 ragdolls, 3 successful `ReleaseAllV2`, DLL leases→0; v1 callback не регистрирует; DX12 standalone smoke `failures=0`. **Runtime blocker:** нормальный запуск без diagnostic workaround падает `0xC0000005` в `CDialogHolder::OnFrame` → `CMainMenu::OnFrame` и на чистом `prypiat` feature-off baseline; 5 MiniDumps/логи подтверждают одинаковую логическую точку, точная root cause неизвестна. Внешние EXE/DLL не представлены; remote integration commit `16b88226b96315f00a1cd098f8dcbd189619b75e` не опубликован, локальный commit reviewer не читал напрямую. **Гейт на merge: HOLD**, без утверждения normal gameplay. Следующее действие `G5.0-I2`: отдельный baseline UI startup fix + обычные feature-on/off gameplay startup без workaround + повторная независимая регрессия PhysX/ODE/DX12. [Полный независимый G5.0-I1 review](p3.3g5.0i1-independent-review.md). **G5.0 overall NOT PASSED; G5.1 NOT STARTED.**

**G5.0-I2 execution task (2026-11-06):** опубликовано [техническое задание на исследование baseline UI crash](p3.3g5.0i2-ui-startup-recovery.md). Ветка baseline fix создаётся **от актуального `origin/prypiat`**, а не от исторической I1 базы; fix должен доказать root cause по dump/PDB и пройти обычный feature-off startup до переноса в PhysX integration. Минимум 5 холодных прогонов, source/binary SHA, полноценная feature-on/off матрица без `OgsrDiagnosticRuntimeWorkaround`, повтор CTest и DX12 smoke. **Задание опубликовано, реализация/тесты не запускались; merge HOLD, G5.1 NOT RUN.**

**G5.0-I2 independent review (2026-11-07):** архив `G5.0-I2-Review.zip` (SHA-256 `16B810388DD6E587F17CA613BE07C36850A45C25A6E67720B7A850D2486037AC`) — **39 ZIP entries, CRC PASS, 38/38 manifest PASS**, 3 валидных minidump `0xC0000005`; исходный startup crash — доказанный **use-after-free**: `CMainMenu::DestroyInternal(true)` удаляет `m_startDialog`, оставляя raw pointer в `CDialogHolder::m_input_receivers`. Диагностические логи показывают одинаковый адрес уничтоженного диалога в receiver на следующем кадре. UI patch применяется к оригинальному Git blob `MainMenu.cpp`, исправленная последовательность `HideDialog`/`CleanInternals` очищает receiver; 5 baseline cold startups и normal integration feature-on/off без workaround прошли **по представленным evidence**, однако подтверждён только startup, не полноценный gameplay. Отдельный `CGamePersistent` guard устраняет наблюдавшийся null-level `Level().name()` crash на disconnect. **I2-A PASS; I2-B startup PASS (evidence-limited); I2-C UI fix CONDITIONAL PASS; I2-B reload/exit BLOCKED; I2-D collider/lease/reload NOT RUN; CTest 10 PASS + 1 SKIPPED; DX12 standalone CPU/jobs smoke PASS.** Новый crash в `CUISequenceSimpleItem::Update` при `load` подтверждён dump, но воспроизведён из **временного `pureFrame::OnFrame` harness**, поэтому «pre-existing tutorial/reload root cause» **NOT PROVEN** без обычного replay и dump/PDB triage. Дополнительно проверить `m_dialogsToRender_new` (не очищается `CleanInternals()`) и reentrant cleanup. Локальные I2 Git commits `74400fa...`/`281cdab...` не опубликованы; старый I2 target `dc80a3...` требует revalidation против нового DX12 `prypiat`. **G5.0-I2 overall BLOCKED; MERGE HOLD; G5.1 NOT STARTED.** [Полный независимый I2 review](p3.3g5.0i2-independent-review.md).

**G5.0-I2-R1 independent review (2026-11-07):** архив `G5.0-I2-R1-Review.zip` SHA-256 `874333699EE9FCF0F77075269AA6479000135C3BAD521A3F663D64FC62D98FBA` — **31 ZIP entries, CRC PASS, 30/30 manifest PASS**. Доказательства подтвердили tutorial/HUD crash: `CGamePersistent::m_intro` продолжал обновляться после `disconnect` при `g_hud == nullptr`; queued-event driver без прямого `Console->Execute("load")` из `pureFrame` успешно выполнил **2 disconnect / 2 start / normal quit**, нормальная разгрузка в raw log; OS exit code 0 указан в отчёте, не отдельно в raw game log. По raw evidence PhysX v2 3 draws, 9 ragdolls, 3 `ReleaseAllV2 complete=1`/`lease_refs=0`; CTest **10 PASS + 1 SKIPPED**, DX12 standalone smoke `failures=0`. **Но найден новый блокер F1 в R1 source:** `CGamePersistent::Disconnect()` вызывает `m_intro->Stop(); xr_delete(m_intro);`, при этом `CUISequencer::Stop()` может завершиться через `etsPlayEachItem → Next()` без `Destroy()`/снятия `Device.seqFrame`, а обычный `Destroy()` вызывает `m_on_destroy_event`, чьи `CGamePersistent::update_game_loaded()/update_logo_intro()` сами удаляют `m_intro`. В R1 log при disconnect 3 раза сработал `intro_delete ::update_game_loaded`; отсутствие crash **не доказывает безопасный single-owner teardown**. R1 evidence/observed reload — PASS, **R1 production lifetime — BLOCKED / MERGE HOLD**, требуется узкий **G5.0-I2-R2**: гарантировать один путь удаления, отсутствие callback self-delete/reentrant destruction, seqFrame/seqRender deregistration и проверить обе ветви `etsPlayEachItem`. Pending dialog queue требует детерминированного теста. Remote `prypiat` продвинулся на DX12, локальная интеграция не опубликована. [Независимый I2-R1 review](p3.3g5.0i2r1-independent-review.md). **G5.0 overall NOT PASSED, G5.1 NOT STARTED.**

**G5.0-I2-R2 independent review (2026-11-08):** `G5.0-I2-R2-Review.zip` 318 481 bytes, SHA-256 `249C04F82C5CC6C12345163C98A5EF5475E63C2125DB6C50E0DBE8A2697DAEF6`; **38 ZIP entries/CRC PASS, manifest 37/37 PASS**. Новая forced-teardown архитектура `m_intro`: один владелец `CGamePersistent`, callback больше не удаляет сам объект, `StopForced()` обходит `etsPlayEachItem → Next`, `Destroy(false)` снимает seqFrame/seqRender/input, item/sequencer Lua stop callbacks на forced path подавлены; обычный путь сохраняет side effects. **F1 PASS WITH NOTES** по исходникам и тестам. Детерминированные тесты **48/48 PASS** на baseline и integration; 5 baseline cold runs, 2 queued-event reload + shutdown, PhysX V1-only/V2 3/3/3 с `ReleaseAllV2` и lease zero; CTest **10 PASS + 1 SKIPPED**, DX12 standalone CPU `857/0`, jobs `130/130` и 80 frames без ошибок по raw logs. **Minor F2:** обычный `CUISequencer::Stop()` не проверяет `m_bDestroyed` на входе и при повторном вызове может затронуть pause; это требует narrow hardening/contract proof перед merge. Патч охватывает шесть production paths; **5/6** восстановлены `git apply --check` и совпадающими post Git blob SHA, а `GamePersistent.cpp` на старом OGSR ZIP полностью не воспроизведён из-за отсутствующего DX12 OnEvent context. R2 в двух рабочих деревьях **uncommitted**, EXE/PDB/DLL bytes не предоставлены, локальный R2 source commit отсутствует, актуальный удалённый `prypiat` продвинулся на DX12. **G5.0-I2-R2 targeted CONDITIONAL PASS; G5.0 overall NOT PASSED / MERGE HOLD; G5.1 NOT STARTED.** Дальше **G5.0-I3**: фиксация immutable code, проверка 6/6 на точной базе, fresh integration на актуальный `prypiat`, build/CTest/ODE/PhysX/DX12 runtime evidence, independent final review. [Независимый аудит G5.0-I2-R2](p3.3g5.0i2r2-independent-review.md).

**G5.0-I3 execution directive (2026-11-08):** утверждено [точное задание на immutable source integration](p3.3g5.0i3-immutable-integration-gate.md). Актуальный remote HEAD на момент публикации: `7620b98672919c26e4f7d6fd349cc04b4750ccc6` (сверить заново при начале). Требуется локально зафиксировать R2 и отдельный F2 normal `Stop()` regression, подтвердить 6/6 production files на **точной** I2-R1 базе, перенести принятую PhysX + UI source integration на новый worktree от свежего `origin/prypiat` **без перезаписи DX12 J1 R2B и R4**. После controlled local commits — clean Release feature-on/off, UI/ODE/PhysX lifecycle, 2 queued reload + clean OS exit, synthetic CTest, текущий DX12 CPU/jobs и актуальные J1 R2B compatibility gates; проверить hashes Git blob/EXE/PDB/DLL и, по возможности, приложить git bundle для независимого аудита неопубликованных commits. **Это только опубликованное задание: выполнение NOT RUN; full G5.0 NOT PASSED; merge HOLD; G5.1 NOT STARTED.**

**G5.0-I3 independent review (2026-11-08):** загруженный `G5.0-I3-Review.zip` — **965 985 bytes, SHA-256 `0FF389EF30FAEAA7A3C90872ADA91A89829F75819DEDFAA347867891CF25A78E`, ZIP 46/CRC PASS, SHA256SUMS 45/45 PASS**. Git bundle независимо разобран: commit SHA **`760f4872584137d2db822d027420d735f09e4f2f`**, parent `5cf20ada3`, root tree **`872118f256d10d234f64802546f6fcae5487bd98`** совпали. Для 91 путей (68 новых, 23 изменённых) Git postimage blobs проверены: 90/91 через `git apply` на старом OGSR архиве и точные blob hashes; для `GamePersistent.cpp` старый архив отличается по DX12, но old blob актуального GitHub совпадает с patch index, а новый root tree пересчитан по GitHub base + manifest — **EXACT MATCH**. Поддеревья `Layers`, `xrGame`, `xr_3da` и root SHA в точности совпадают с bundle. F2/UI selftest **70/70**, 5 cold starts, 2 queued reload/exit feature-off/on по логам PASS; PhysX V1/V2 callback Python validator повторно исполнен reviewer (exit 0), 3 draws/3 releases/3 lease0; CTest **10 PASS + 1 SKIPPED**; DX12 CPU **857 checks / 0 failures**, jobs **130/130, rendered=80, failures=0**. Изменения current `prypiat` после I3 базы — три **docs-only** коммита. **I3 SOURCE/EVIDENCE PASS WITH NOTES**; **J1 R2B integrated GPU readback/reset NOT RUN**, EXE/PDB/DLL SHA source-backed only (binary bytes/CI not provided), Windows tests не запускались reviewer. **G5.0 overall READY FOR PRE-MERGE VALIDATION / MERGE HOLD; G5.1 NOT STARTED**. [Независимый I3 review](p3.3g5.0i3-independent-review.md). Следующий узкий этап **G5.0-M0**: review branch/CI source & binary provenance + integrated J1 in-game GPU A/B, затем отдельное решение о merge.

**Исторические наблюдения/мониторинг:** (1) legacy v1 registration исправлен в I1, V1-only/V2 валидатор повторно PASS на I3, но mixed transitions отдельно не проверены; (2) исторический no-vis `0xC0000005` в `CLevel::IR_OnMouseMove` не воспроизведён в поздних коротких reload, однако root cause остаётся **UNRESOLVED/MONITOR**. Не приписывать автоматически PhysX.

**P3.3-G5 — Real Gameplay Readiness.** Scope freeze: не добавлять новые физические возможности. Цель — доказать пригодность существующего PhysX ragdoll для обычной игры OGSR DX11/R4 при ODE movement/AI, ODE fallback и отсутствии регрессий.

| Гейт | Требуемые доказательства | Статус |
|---|---|---|
| G5.0 — provenance/build | I3 Git commit/root tree source PASS; Windows Release feature-on/off, CTest, UI, PhysX lifecycle и DX12 standalone — PASS по raw logs. Integrated DX12 J1 GPU readback/reset и binary/CI provenance не подтверждены | **I3 PASS WITH NOTES; G5.0 PRE-MERGE VALIDATION PENDING / HOLD** |
| G5.0-I2 — baseline UI startup | [Независимый review](p3.3g5.0i2-independent-review.md): UAF root cause доказан; UI fix + 5× cold baseline + feature-on/off startup; reload/exit crash in frame-callback harness требует независимого воспроизведения | **I2-A PASS, I2-B startup PASS (limited), reload/exit BLOCKED; I2 overall BLOCKED** |
| G5.0-I2-R1 — reload/exit isolation | [Независимый review](p3.3g5.0i2r1-independent-review.md): 2 queued-event reload, shutdown, PhysX v1/v2 lease/callback, CTest и DX12 smoke; **новый F1: m_intro Stop→callback self-delete / etsPlayEachItem early return** | **EVIDENCE PASS; SOURCE LIFETIME BLOCKED / MERGE HOLD** |
| G5.0-I2-R2 — intro teardown ownership | [Независимый review](p3.3g5.0i2r2-independent-review.md): F1 forced teardown принят, 48/48 selftests ×2, reload/exit, PhysX V1/V2 и DX12 smoke; обычный Stop idempotence F2 note, 5/6 patch replay | **F1 PASS WITH NOTES; R2 CONDITIONAL PASS, MERGE HOLD** |
| G5.0-I3 — immutable source integration | [Independent review](p3.3g5.0i3-independent-review.md): 91-file source, Git commit/tree exact SHA, F2, builds, 2 reload+exit, PhysX CTest and V1/V2, DX12 CPU/jobs; in-game GPU NOT RUN | **SOURCE/EVIDENCE PASS WITH NOTES** |
| G5.0-M0 — premerge compatibility | Review-branch source/CI and binary/PDB/OS-exit evidence; independent GamePersistent patch replay; **integrated** J1 R2B GPU readback/reset A/B against unchanged R4/DX12 base | **NOT RUN / MERGE HOLD** |
| G5.1 — normal gameplay | Одновременно 4–8 PhysX v2 ragdolls, несколько разных игровых территорий/сцен; реальный AI/actor loop, непрерывная gameplay-сессия; видео+run-id+логи | NOT RUN |
| G5.2 — save/load/lifecycle | Повторные сохранения/загрузки и level reload с ragdolls, выход из игры; до/после counts registry/callbacks/scene leases; нет stale snapshots, двойной симуляции, UAF | NOT RUN |
| G5.3 — stability | Overlay on/off, повторные запуски и длительная игровая сессия, раздельная диагностика legacy callback и `IR_OnMouseMove` no-vis crash; fail-closed при ошибках | NOT RUN |
| G5.4 — measured performance | На одном save/железе ODE baseline vs PhysX feature-on: physics CPU p50/p95/p99, кадр p50/p95/p99, память и рост live bodies; без подмены игровых замеров smoke/harness | NOT RUN |
| G5.5 — independent review | Raw evidence manifest, commit hashes, воспроизводимые команды, source diff, runtime-логи, визуальная проверка, независимый verdict | NOT RUN |

**Приёмка G5:** PASS только если реальный gameplay, save/load/reload, CPU/memory budget (согласованный по baseline), provenance и независимый review проходят все критичные гейты. Любой необъяснённый crash/lifetime violation — блокер соответствующего гейта. G4.2-R1 PASS не равен G5 PASS. Данный трек не блокирует F4.0/DX12.

### P-S — адаптация PhysX к скелету модели (решение 2026-10-25; PLANNED)

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

**Ссылка:** [Portable PhysX ADR](physics-portability-adr.md). **Статус P:** I1 SOURCE CONDITIONAL PASS; I2 startup UAF root cause PASS; I2-R2 forced teardown F1 PASS WITH NOTES; **I3 immutable source/root tree and targeted build/UI/PhysX/DX12-standalone evidence INDEPENDENT PASS WITH NOTES**; G5.0-M0 (DX12 integrated J1 GPU reset/readback and independently auditable binary/CI provenance) NOT RUN; **G5.0 overall PRE-MERGE VALIDATION PENDING / MERGE HOLD, G5.1 NOT STARTED**. Other tracks independent; do not count diagnostic PhysX or DX12 CPU-only as normal in-game GPU acceptance.

### P-M — Индивидуальная масса NPC и подвижность экипировки (решение 2026-10-25; PLANNED)

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



## Направление VFX — базовая физика среды → улучшение аномалий (решение 2026-10-25; PLANNED)

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


### VFX-AMD — предварительная оценка NVIDIA vs AMD, временный режим и риски (2026-10-25; PLANNED)

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


## Направление AUDIO — Enhanced X-Ray Audio (решение 2026-10-25; PLANNED)

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
