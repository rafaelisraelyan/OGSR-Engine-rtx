# 08 — План работ

> **Актуальная ветка документа:** `prypiat` (форк ветки CoP `main_cop_cs`).
> **Дата обновления планов:** 2026-11-11 (F4.0-J1 postmerge GPU ACCEPTED WITH NOTES; J2A R7 CPU-only production ACCEPTED WITH NOTES at `b484f8d795cfdbe37ea29e2282c1f4d18035e648`; **F4.0-J2B-M0 author CPU compatibility PASS WITH NOTES / Tech Lead package+source targeted pre-review PASS WITH NOTES**, real Jupiter visuals 9/105/106/107 each `ReadVisual`+`BuildXrayLevelAsset12` author PASS; **independent Codex M0 full review PENDING**, J2B M1 implementation not authorized, J2B GPU implementation/J3 HOLD).
> Исторические статусы A/B1/F/V и их даты оставлены как ранее зафиксированные
> отчёты, а не результат новой проверки GPU/CI. Изменение roadmap **не означает**
> реализации или тестирования добавленных этапов.

### Порядок сопровождения roadmap — решение 2026-11-10

**Ответственный за обновление `docs/08-roadmap.md`: ChatGPT (технический руководитель проекта) при работе с новыми отчётами и результатами в рамках активного обсуждения.** Владелец проекта определяет приоритеты и даёт отдельное разрешение на production integration. **DeepSeek** реализует код, проводит сборки/тесты и передаёт исходники, логи и воспроизводимые доказательства. **Codex** выполняет независимое READ-ONLY ревью крупных и критических изменений; он не обязан и не должен менять roadmap в рамках ревью.

После каждого подтверждённого этапа или нового блокирующего ревью технический руководитель сверяет фактический Git HEAD, статус патча, результаты независимых проверок, ограничения и открытые гейты; затем вносит **отдельное документальное обновление** в `prypiat` с точной датой, статусом, доказательствами, обязательными условиями следующего этапа и ссылкой на коммит. Исторические результаты сохраняются. Нельзя повышать статус по одному заявлению агента, промежуточному PASS или без соответствующего review/evidence; явно различать READ-ONLY review, source/CPU pre-review, авторские Windows/GPU evidence, независимое подтверждение, production commit и post-merge acceptance. Обновление документации само по себе не разрешает production merge и не закрывает HOLD. Без поступления новых результатов самостоятельного фонового мониторинга нет.

**Текущий контроль:** F4.0-J2B-M0 author `PASS WITH NOTES` reported on `prypiat` HEAD `2cc0536f08707724043a518e097c4b10a73f6b2f`. Tech Lead independently checked delivered ZIP SHA/CRC, 53/53 manifest and the full M0 CPU harness/real-visual, negative and architecture evidence; **targeted evidence/source pre-review PASS WITH NOTES, independent actual Jupiter VFS payload re-run NOT PERFORMED** because proprietary level bytes are intentionally not included. Four reported visuals 9/105/106/107 use strict 32-byte declaration and each passes `ReadVisual` plus debug-flat `BuildXrayLevelAsset12`; 106/107 share source VB/IB IDs but have distinct ranges. **Codex STRICT READ-ONLY full M0 review PENDING**; no M1 implementation, GPU registry/upload/4 Draw or J3 approved. M1 requires separate design/owner gate after Codex; GPU M2/M3 and J3 HOLD. One-shot J2A acceptance and DX11/R4 remain unchanged.

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
| **F4.0-J2A R3 — traversal/provenance/resource safety** | 🔴 **Codex REQUEST CHANGES (2026-11-09); J2A/J3 HOLD** | Earlier 6 R2 findings addressed and targeted CPU oracle 55/55 PASS, but full R3 independent review found **P2 false sector lookup through terminal MT_NORMAL children**, **P2 default maxInstances=8 cannot collect Jupiter evidence requiring ≥20** (run used 24), and **P3 incomplete accounting for index build + reverse search initial node**. J2A R4 fix and new negative/default-env tests required; no J2A source integration. |
| **F4.0-J2A R4 — lookup semantics/default capacity/budget accounting** | 🔴 **Independent Codex REQUEST CHANGES (2026-11-09), J2A/J3 HOLD** | R3's 2 P2 + 1 P3 targeted fixes and 72/72 CPU oracle remain confirmed. Full R4 review found **P2**: target visual9 exists in `selected` but `ReadVisual(9)` failure or invalid geometry identity is skipped; with >=2 other valid objects J2A returns PASS and snapshot lacks visual9. Codex reproduced both with unchanged R4 function using injected VFS/ReadVisual. Valid visual9 culled by frustum remains legitimate PASS. Narrow R5 fix + independent review required; no J2A source commit. |
| **F4.0-J2A R5 — Validated Target Survival** | 🔴 **Full independent Codex REQUEST CHANGES (2026-11-10), J2A/J3 HOLD** | Prior R4 target-survival P2 fixed, 72/72 hierarchy and 9/9 target fault-injection PASS. But blocking P3: a late packetIds.reserve/insert allocation failure after success metadata and PASS log leaves false PASS in logs and nonzero hash/counters despite returned false; independently injected bad_alloc confirms. Nonblocking P3: ParseVisuals duplicate ID scan is O(N²), 65536 visual parser-only fixture ~2.31s owner-thread. J2A R6 transactional-result and parser-scaling corrective gate required; no production commit. |
| **F4.0-J2A R6 — transactional result and parser scaling** | 🔴 **Full independent Codex REQUEST CHANGES (2026-11-10); J2A/J3 HOLD** | Prior R5 false-PASS and O(N²) parser P3 fixed; independent MSVC 72/72 hierarchy, 9/9 target, 6/6 transaction and R5/R6 parser scaling PASS. New **blocking P2**: persistent OOM can make `result.reason = error.what()` or `Msg` throw inside RunJ2A catch, escaping GameDx12J2A_Run and bridge frame caller; CPU fault injected two variants (`escaped=1`, no final FAIL). P3 double terminal `result=FAIL` (wrapper+bridge); P3 one-entry transient over-limit in `seenIds` and reverse reachable. R7 fail-closed corrections + Codex review required, no source commit. |
| **F4.0-J2A R7 — no-throw failure boundary / strict budgets** | 🟢 **PRODUCTION ACCEPTED WITH NOTES (2026-11-10), `prypiat` commit `b484f8d79`, opt-in CPU-only one-shot. J2B GPU/J3 HOLD** | G6 published exact 24 files (22 added/2 modified), GitHub SHA/tree/parent and 24 Git blob postimages independently verified. G7 post-push author 3 x64 Release Rebuilds (feature-off has no d3d12.dll import), hierarchy74/74, target9/9, transaction6/6, no-throw12/12, VFS5/5, guard5/5, GR10/10, ASan74/74, Jupiter default/limits/repeat and Zaton R4 PASS WITH NOTES; Tech Lead verified package SHA/CRC/130/130 manifest, binary SHA/imports, log invariants and both-on J1+J2A evidence (J1 one Draw, fence3/GBV PASS, readback PNG 10623 nonclear). **No independent Windows runtime replay by Tech Lead**; VFS OOM/live GR failure/pending reset/J1 pre-draw reset remain notes. **J2B-M0 design/source compatibility NEXT; GPU implementation & J3 HOLD.** |
| **F4.0-J2B-M0 — 4-real-Jupiter-asset CPU compatibility audit** | 🟡 **Author PASS WITH NOTES / Tech Lead package+source pre-review PASS WITH NOTES (2026-11-11); independent Codex PENDING; GPU/J3 HOLD** | Real Jupiter `level` 6,873,629 B, `level.geom` 93,930,032 B (SHA below); author reader+converter pass separately for 9 (545v/1314i), 105 (99v/312i), 106 (120v/192i), 107 (385v/876i), all stride32, finite bounds/sphere and compact base-relative uint16 indices; 106/107 share source buffer IDs but distinct ranges. 103/0/1 synthetic, 114/0/0 real-path, edge5/5, conversion15/15 from author logs; 10/10 deterministic hash lines across 2 runs. Tech Lead verified ZIP SHA/CRC, manifest53/53 and inspected real harness and per-visual logs; **cannot independently recompute source data without original game payload**. Existing GPU registry remains 3 synthetic + 1 X-Ray slot, no four GPU Draw. Author M0 result is not Codex acceptance; **no production modifications or M1/M2/M3 work authorized**. |
| **F4.0-J2B PREP — Jupiter serial 4-visual offscreen architecture** | 📐 **Design ACCEPTED WITH NOTES (2026-11-09); implementation NOT STARTED** | Strict dependency: J2A R5 independent Codex PASS → final source acceptance → separate owner authorization/integration/post-merge regression. Then source-geometry conversion compatibility gate 9/105/106/107 → immutable CPU preload via shared GR pool → bounded owner GPU registry/upload/fence → exactly one direct command list with 4 real Draws and per-visual contribution evidence → lifecycle hardening; R4 Present retained. J3 separate HOLD. |
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

### F4.0-J2A R3 — historical targeted tech-lead pre-review (2026-11-09; SUPERSEDED BY CODEX REQUEST CHANGES)

- **Status:** six preceding R2 Codex findings addressed in the supplied cumulative source patch and targeted CPU tests. **No new confirmed P1/P2/P3** found in this pre-review; this is **NOT independent Codex review or final J2A acceptance**. No J2A source staged/committed/merged/pushed. J3 **HOLD**; original successful J1 postmerge result unchanged.
- **Artifacts:** delivery `F4.0-J2A-R3-Delivery.zip` **17 126 477 bytes**, SHA256 `64FE0C32BD1370509B8EAB96D56C584BA3D47D6EE1FBF2BE7492FC2C186EF953`, 7 entries CRC PASS, manifest **6/6 PASS**. Evidence ZIP **17 082 434 bytes**, SHA256 `E9E0639E5511DA5734DD0E4B40A2E42A4DE3EF9ACDFEABBF891229786B4BBAB0`, 38 entries CRC PASS, manifest **37/37 PASS**. R3 cumulative patch **104 057 bytes**, SHA256 `3746944E3C72C1B1CBB4B59CFC394FE8466C4262E0D1B496548F1B32E28AAC71` (11 paths, 2 modified/9 new). R3 incremental **55 372 bytes**, SHA256 `967DBBF2BAEEC32BEC0BE39610505CD5CDF47BE5C68550849E50CE8C4724051A` (7 paths). Original existing base Git blobs in patch correspond to current `prypiat`; recent changes since R3 baseline `5886b6bf` docs-only.
- **Independent CPU execution:** evidence `dx12levelhierarchy.cpp` compiled under Linux g++ C++20 with shipped hierarchy tests: **55/55 PASS**. Independently recompiled and executed R3 repro: reordered sector chunks `1,0` yield canonical IDs/root `0→100,1→200`; full 100 000 reference collection returns **LimitExceeded**, not Complete; targeted visual 9 lookup returns sector 1 and its single instance; true compressed-bit rejects with exact reason. Additional independent hand-authored shared-DAG CPU test: two legitimate occurrences of same drawable visual survive with distinct ordinals, exact budget Complete, insufficient node/edge/instance budgets LimitExceeded, targeted reverse provenance and scopeguard exception release PASS.
- **Fix review:** `CollectLeaves` / `CollectSectorLeaves` charge visits before expanding paths and expose applied budgets; `FindSectorForVisual` performs bounded reverse reachability and no fallback to arbitrary first sector; `ParseSectors` uses real chunk IDs and rejects duplicate/gaps; MSVC duplicate `dx12packetplan.cpp` item removed under mutually exclusive camera/bridge conditions; compressed oracle uses correct `0x80000000` bit and expected reasons; `ReadVfsFile` uses `dx12scope::ScopeGuard` on all exits including allocation error.
- **Windows evidence only:** bridge-off / bridge-on-camera-off / bridge-on-camera-on MSVC Release x64 rebuild/imports (d3d12 0/1/1), no MSB8027/LNK4042 in supplied logs; unrelated C4756 physics warning still present. Jupiter sector **129/root17431** with five unique instances, **4 visible CPU packets**, one culled visual415, hash `EC3EAADD55DFCD6C`, GR Serial==Parallel==Verify, R4 Present and clean teardown/shutdown per logs. **Independent Windows runtime/GPU was NOT rerun here.**
- **Timing correction and limits:** instrumented J2A one-shot **total 161.010 ms warm / 307.611 ms cold**, with VFS 44.590 ms, hierarchy parse 110.492, target lookup 1.294, collect 0.276, ReadVisual 4.075, GR PrepareOrdered 0.055 (warm). Historical ~3.17 s level-ready-to-PASS interval is NOT the duration of J2A call. One-shot owner-thread stall remains nontrivial and requires preload/cache before scene scale. Material/PSO/Resource12 are **CPU-only placeholders**, not GPU resource handles; `g_j2aJobs` is a separate GR pool instance whose lifetime/ownership policy must be resolved before J3.
- **Next gate:** independent **Codex READ-ONLY full cumulative R3 source review**: budget exact exhaustion, failure output never published, sector continuity/provenance, parser malformed inputs, exceptions/RAII, MSBuild three feature combinations, GR thread pool ownership and timing. Codex verdict PASS/PASS WITH NOTES/REQUEST CHANGES, then separate final tech-lead source acceptance before any production staging/commit/push. **No J3, no multi-object DX12 GPU Draw claim.**

### F4.0-J2A R3 independent Codex review → R4 corrective gate (2026-11-09; REQUEST CHANGES)

- **Verdict:** full cumulative J2A R3 patch **REQUEST CHANGES** due to **2 P2 + 1 P3**. Earlier R2 six findings targeted fixes retained, source/CPU oracle 55/55 and actual Jupiter CPU evidence remain useful; they do **not** establish correctness of the full J2A patch. **J2A production staging/commit/push HOLD; J3 NOT STARTED; J1 post-merge acceptance unchanged.**
- **P2 #1 — reverse lookup vs terminal MT_NORMAL semantics:** `ogsr_engine/Layers/xrRenderDX12/dx12levelhierarchy.cpp`, `FindSectorForVisual` around line **1011** creates reverse parent edges for children of every visual type. `CollectSectorLeaves` at ~line **858** stops descent at `MT_NORMAL`. The parser admits `MT_NORMAL` with `OGF_CHILDREN_L`: independent Codex binary fixture has sector0 root reachable *only through a terminal MT_NORMAL* to target visual9, while sector1 truly emits visual9. Lookup incorrectly selects sector0; collect reports complete with drawable instances **without visual9**. Fix reverse edges to exactly mirror collector traversable node types (or parser reject incompatible MT_NORMAL children), and **after collecting confirm a target drawable instance for selected sector before snapshot publication**. Test decoy lower-index sector followed by true sector, same visual from shared DAG paths, invalid/deep/budget rejection, no arbitrary fallback and no false PASS.
- **P2 #2 — default J2A maxInstances too small:** `ogsr_engine/xr_3da/GameDx12J2APreparation.cpp` ~line **229** uses `EnvU32("OGSR_DX12_J2A_MAX_INSTANCES", 8, 1, 32)`; successful Jupiter logs explicitly set **24**, and sector emitted at least **20 leaves** (15 later rejected by ReadVisual plus 5 successful). Since collect charges before ReadVisual, default8 deterministically returns `LimitExceeded` without later retry in the level session. Set default at least to *verified fixture value 24*, preserve opt-in and the fail-closed bounded max, add **env-unset real Jupiter runtime test** plus explicit override low-budget negative. Do not silently discard unsupported visuals before proven traversal semantics.
- **P3 — partial budget accounting / contract mismatch:** `dx12levelhierarchy.cpp` `CollectLeaves` around line **818**, `FindSectorForVisual` around **993** indexes all visual nodes without charging those scans to node budget; lookup builds parents with budgeted node scans but enqueues the initial `visualId` into reverse BFS without charging that initial node. Codex CPU example with 64,000 visuals: collector maxNodeVisits1 can still build full index then report one visit; singleton reverse lookup maxNodeVisits1 can report Found despite additional initial work. R4 must **count initial reverse node and disclose/bound index work/memory**, either fully charge index and reverse work under named budgets or expose separately enforced pre-index limits. Carefully preserve exact boundary behavior, stats/reasons and consistent sector lookup/collector accounting. Add 1-node and 64k-node tests and memory-bound/adversarial fixtures.
- **Codex independently checked:** cumulative patch clean apply against baseline, 55 MSVC assertions, independent reverse-lookup false-positive fixture, instance cap 8/19 FAIL vs20 PASS, CPU benchmark and scope-guard test, compression / sector-ID / MSBuild evaluation; Windows Jupiter, teardown and feature-off logs were supplied evidence only; no independent full Windows game execution. Supplied measured one-shot J2A `total_ms=161.010` (another ~308ms) includes owner synchronous VFS/parse; no reason to restore earlier incorrect 3.17s interpretation. Synthetic `material/pso/Resource12` GPU IDs and separate `g_j2aJobs` ownership remain limitations before J3 scaling.
- **Next:** narrow **F4.0-J2A R4 — Lookup Semantics & Budget Accounting**, fixing both P2 and P3 with new CPU fixtures, original 55 tests, expected Jupiter sector129/root17431 / 5 processed / 4 visible / hash `EC3EAADD55DFCD6C`, **default-env unset runtime**, low-budget rejection, GR Serial/Parallel/Verify, reset/teardown, build three feature configurations, no loss of R4 Present. Produce incremental + cumulative patches and evidence manifests. Independent Codex READ-ONLY review and separate tech-lead acceptance required before **any production staging/commit/push**. **Do not begin J3.**

### F4.0-J2A R4 — historical targeted source/CPU pre-review (2026-11-09; SUPERSEDED BY CODEX REQUEST CHANGES)

- **Status:** all three R3 Codex findings are closed in independently executed **targeted CPU scenarios**; R4 cumulative patch **is not independently accepted**. During broader source review an additional **source-level P2 candidate** was found: post-collection target visual 9 can be rejected by `ReadVisual` but not rechecked prior to reporting J2A PASS. It has **not** been reproduced in actual Jupiter (visual9 present and valid in provided run); Codex must evaluate. J2A not staged/committed/pushed; J3 **HOLD**. J1 postmerge accepted status unchanged.
- **Artifact integrity:** `F4.0-J2A-R4-Delivery.zip` **10 750 568 bytes**, SHA256 `F5652851077D08F89304E21700A538FD75EF71A8071B2686A91D39A30C59762C`, 7 entries/CRC PASS/root SHA manifest **6/6 PASS**. `F4.0-J2A-R4-Evidence.zip` **10 713 727 bytes**, SHA256 `70F809AEDB39CE4A2594B2E430F742F39D0F6AF5880CE882052FEC2D83B82815`, 39 entries/CRC PASS/manifest **38/38 PASS**. Full cumulative 11-path patch **119 806 bytes**, SHA256 `4FF4D22520E0676635FC40DD6D46ED10C5CE4D5DACA8C903303F6D246773C7B7`; incremental 3-path patch **31 453 bytes**, SHA256 `E4464AA2876496B552575108BC13E420B2BBFF4ED32092CFD7BAC88FD01D8707`. 9/9 new-file Git blob IDs verified against evidence sources; two existing patch-base Git blob prefixes match current `prypiat` `GameDx12Bridge.cpp` and `XR_3DA.vcxproj` (remote docs-only since baseline).
- **Independent CPU execution:** evidence `dx12levelhierarchy.cpp` compiled with g++ C++20 and exact `j2a_hierarchy_cpu_main.cpp`: **72/72 PASS**. Independent build of evidence `r4_repro_main.cpp`: decoy sector 0 bypassed in favor of sector **1**, collector **two instances including visual9**; maxNodeVisits=1 initial reverse case **LimitExceeded**, 64k visual index with budget1 **LimitExceeded**. Lookup builds reverse edges only through collector-traversable nodes; index scans/edges and reverse initial node charged. No independent Windows GPU/runtime execution performed.
- **Provided Windows logs only:** Jupiter with env maxInstances unset (default **24**) and explicit20/24: sector129/root17431, `CollectSectorLeaves` **20 references** → `ReadVisual` validated **5 instances**, four visible packets (visual9/105/106/107), one culled visual415, stable packet hash `EC3EAADD55DFCD6C`, GR Serial=Parallel=Verify; explicit19/8 → `LimitExceeded`, **no ReadVisual/prepare**. Reset no-repeat, teardown/shutdown and Zaton R4 baseline by Windows logs. Instrumented warm total **182.636ms** (VFS50.090, Parse125.335, lookup1.987, collect0.566, ReadVisual4.196, PrepareOrdered0.111), excludes unrelated level-ready delay. Three feature configurations have no MSB8027/LNK4042 according to logs; no full independent Windows build.
- **P2 candidate for Codex (validated-target survival):** `ogsr_engine/xr_3da/GameDx12J2APreparation.cpp` checks visualId9 in **collected** instances at ~lines **321–344**, then `ReadVisual` failures for individual visuals **continue** at ~lines **351–383**, requiring only >=2 successful visuals and >=2 objects ~lines **386–429**, then sets `result.ok=true` ~line **501**. If visual9 validation fails but >=2 others succeed, a PASS snapshot can omit the claimed target. Source-level control-flow counterexample; not a demonstrated real game regression. Codex should demand fault-injection test and post-validation `visualInfo.find(9)` or equivalent check before snapshot, without requiring visual9 to be visible under frustum.
- **Next gate:** independent **Codex READ-ONLY review** of full R4 cumulative and incremental patches; look for P1/P2/P3, especially post-ReadVisual target guarantee, index/reverse exact budgets, fail-closed publication, 20-reference Jupiter fixture/default-unset, build feature guards, RAII and GR lifetime. If Codex confirms P2, narrow patch correction and rerun tests before final acceptance. Regardless of verdict, **no production J2A staging/commit/push or J3** without separate tech-lead acceptance/authorization. Materials: `F4.0-J2A-R4-TechLead-PreReview.md`.

### F4.0-J2A R4 Codex finding → R5 validated-target survival gate (2026-11-09; REQUEST CHANGES)

- **Verdict:** independent full cumulative R4 Codex review **REQUEST CHANGES, one confirmed P2** in `ogsr_engine/xr_3da/GameDx12J2APreparation.cpp` around lines **321/353/386**. No additional confirmed P1/P3 in reviewer findings. Targeted R4 source/CPU pre-review 72/72, sector lookup, capacity budgets, build configuration fixes and positive Jupiter CPU evidence remain valid in their tested scopes; **they do not imply acceptance of full J2A**. J1 source/GPU postmerge acceptance unchanged. **J2A production/J3 HOLD**.
- **P2 root cause — checked before `ReadVisual`, not after:** R4 `targetPresent` verifies visual 9 among `selected` (collected leaves), then loops over unique visuals using `xraylevel12::ReadVisual` and `continue` for error or `visual_id` mismatch/zero-size geometry. With at least two other valid visuals, `visualInfo.size()>=2` and `objects.size()>=2` can pass; snapshot and `result.ok=true` are published while mandatory visual9 is absent. **Fault-injection repro by Codex on unchanged R4 production function**: `ReadVisual(9)` failure => unexpected PASS with two objects/two packets; visual9 geometry identity mismatch => unexpected PASS with two objects; valid visual9 out of frustum => expected PASS with three objects and one culled; traversal `LimitExceeded` => expected FAIL before ReadVisual/snapshot. Real Jupiter positive run contains valid visual9; this is a *separate CPU injection defect*, not observed real-Jupiter GPU failure.
- **Required R5 correction:** immediately after completed `ReadVisual` loop and before allocating/building objects/immutable snapshot, require successfully validated `visualInfo.find(9) != visualInfo.end()` (or equivalent strongly typed target handle) with precise diagnostic fail-closed reason and **no successful snapshot publication/PrepareOrdered** when absent. Do not require target to survive frustum culling; valid-but-invisible target is allowed. Retain pre-collection target sector/instance identity check, caps and all feature/lifetime guards.
- **Required tests:** independent reproducer or equivalent in repo build on **unchanged R4** should show two false PASS cases then FAIL after R5; geometry read error, invalid visual9 identity / zero vertex or index count, good target culled by frustum => PASS, budget LimitExceeded => FAIL before read/prepare, target missing in collected sector => FAIL. Verify target survives **validated CPU Objects** (not merely selected leaves, and not necessarily visible packet). Retest all 72 existing hierarchy oracle assertions, GR Serial/Parallel/Verify, default environment unset/20 PASS and 19/8 FAIL, Jupiter sector 129/root17431 / 20 references / 5 valid objects / 4 visible / hash `EC3EAADD55DFCD6C`, reset/teardown, three MSVC feature combinations without MSB8027/LNK4042 and DX11/R4.
- **Independent Codex scope:** 72 MSVC parser assertions, 1500 lookup and 1500 collect randomized oracle cases, 524 exact-capacity cases +1029 insufficient-budget rejections, confirmed index/reverse accounting and default24 with env unset. Windows Jupiter/build/teardown evidence **only from supplied logs**, independent game execution not performed. Renderer GPU visual9 no-reset J1 is separate accepted gate; J2A CPU-only packets and GPU material/PSO handles remain placeholders. Owner-thread one-shot J2A ~182.636ms in R4 provided warm run; still requires caching/preload before scene scale. GR pool ownership remains future gate.
- **Next:** **F4.0-J2A R5 — Validated Target Survival**, narrow incremental correction and dedicated CPU fault injection tests plus cumulative patch / evidence SHA manifest, then independent Codex READ-ONLY review and final tech-lead acceptance. **No staging/commit/merge/push** of J2A until separate explicit user authorization. **J3 NOT STARTED.**

### F4.0-J2A R5 — validated-target survival targeted pre-review (2026-11-09; PASS WITH NOTES / CODEX PENDING)

- **Status:** previous R4 independent Codex P2 (false positive J2A PASS with target visual9 rejected by ReadVisual) is **fixed in R5 source** and supported by the submitted MSVC production-function fault-injection logs. Targeted source review **PASS WITH NOTES** and no new confirmed P1/P2/P3 on the targeted delta; this is **NOT** independent full cumulative Codex PASS, NOT J2A final acceptance and NOT permission to stage/commit/merge/push. J2A production/J3 **HOLD**, J1 post-merge acceptance unchanged.
- **Integrity and provenance:** delivery `F4.0-J2A-R5-Delivery.zip` **8,000,139 bytes**, SHA256 `F68C34A66BABD2AAD3B3CBFD07228783F5159B7E43CE6EDEB09200AD3DDC680E`, 7 ZIP members CRC PASS, root manifest **6/6 PASS** independently. Evidence ZIP **7,960,993 bytes**, SHA256 `4F7BF09E37B30D8C993233C33E0699BEBA35025F5F9527DA8E49774E91A9F0DA`, 42 ZIP members CRC PASS, manifest **41/41 PASS** independently. Cumulative patch 17 paths, **140,211 bytes**, SHA256 `82DD870F96B471F2A7C0FD3F6EE1A0E38D23282DB94D2D9D88593C2B2C09BEFE`; R5-only incremental seven paths (one production cpp + six test-only), **20,938 bytes**, SHA256 `904028839200F99C97F84775DE9C9361A94381D14DC6F47CC96B69B262EF5EAD`. R5 production `GameDx12J2APreparation.cpp` SHA256 `747F92EF560C8758D16B0994213D077B35925D7C010143326764890C40FF1962`, Git blob `a97e74a23d37eb3d0271a2f4503d9d45956a7409`, matches incremental patch. Existing baseline patch index Git blob prefixes for `GameDx12Bridge.cpp` (`46677b2e9`) and `XR_3DA.vcxproj` (`56108b14b`) match live `prypiat`; independent full `git apply --check` from fresh remote worktree **NOT PERFORMED** here (provided Windows evidence says PASS). Recent parallel changes concern docs/PhysX and not these two DX12 source files.
- **P2 source fix:** `ogsr_engine/xr_3da/GameDx12J2APreparation.cpp` after complete ReadVisual loop around lines 385-395, check `if (visualInfo.find(9) == visualInfo.end())` and fail with reason `target visual 9 was not validated`. `visualInfo` receives entries only after successful ReadVisual, matching geometry visual ID and nonzero vertex/index counts. Guard is before immutable object construction, GR PrepareOrdered and `result.ok=true`, and **does not require frustum visibility**. Diff versus supplied R4 prefix only adds ten lines to production function.
- **Independent CPU execution:** g++ C++20 built supplied `dx12levelhierarchy.cpp` + hierarchy main and actually ran **72 PASS / 0 FAIL** `J2A_HIERARCHY_RESULT PASS`. R5 target fault-injection logs examined (not re-run independently in this environment): unchanged R4 prefix yields 3 false PASS; R5 nine scenarios **9/9 PASS** including ReadVisual(9) error, invalid identity, zero counts all fail (ok=false, packets=0, hash=0, prepare_ms=0); valid target visible PASS, valid target culled PASS, exact capacity PASS, insufficient capacity FAIL before ReadVisual and absent sector target FAIL. Test harness links actual production `GameDx12J2A_Run` with MSVC C++20, GR, hierarchy and CPU renderer preparation, but injects synthetic VFS/ReadVisual; no unmodified independent Windows execution.
- **Provided runtime/build evidence:** Jupiter default instance cap 24 (env unset) `CollectSectorLeaves` 20 references, `ReadVisual` five valid objects, four visible packets (9/105/106/107) and culled 415, sector **129** / root **17431**, GR Serial=Parallel=Verify hash `EC3EAADD55DFCD6C`, one-shot/no-repeat reset epoch4, Jupiter→Zaton and shutdown. Explicit 20/24 PASS, 19/8 fail-closed LimitExceeded. Warm one-shot total **170.764ms** (VFS48.091, Parse114.420, lookup2.296, collect0.518, ReadVisual4.955, Prepare0.076), not historical 3.17 seconds. MSVC feature-off/on/camera-on Release x64 rebuild and d3d12 imports 0/1/1; no MSB8027/LNK4042 in logs; feature-off R4 Zaton. Executable hashes supplied, binaries themselves not archived/independently run.
- **Architectural constraints/next gate:** J2A is a **CPU-only** Jupiter prototype, NOT multi-object GPU Draw. `material/pso/Resource12` are synthetic placeholders; g_j2aJobs is a separate GR pool; owner-thread parse still synchronous. Independent **Codex READ-ONLY full cumulative 17-path R5 review** must check target fault-injection/valid culled case, failure-metadata publication in post-success exception/logging paths, test shim isolation, current remote patch applicability, ownership/lifetime and all previous corrected P2/P3. Then separate final tech-lead acceptance and explicit user approval before **any** J2A production staging/commit/push. **J3 NOT STARTED.**

### F4.0-J2A R5 — tech-lead full available source/CPU review (2026-11-09; PASS WITH NOTES, FORMAL ACCEPTANCE PENDING CODEX)

- **Evidence inspection:** R5 delivery 8 000 139 B SHA-256 `F68C34A66BABD2AAD3B3CBFD07228783F5159B7E43CE6EDEB09200AD3DDC680E` and evidence 7 960 993 B SHA-256 `4F7BF09E37B30D8C993233C33E0699BEBA35025F5F9527DA8E49774E91A9F0DA` — both CRC PASS, SHA manifests **6/6** and **41/41**. Full cumulative patch 140 211 B, SHA-256 `82DD870F96B471F2A7C0FD3F6EE1A0E38D23282DB94D2D9D88593C2B2C09BEFE`: 17 paths (2 modified + 15 new), 15/15 new Git blob SHA independently verified after isolated `git apply`, evidence `GameDx12J2APreparation.cpp` byte-identical. Base GitHub blobs `GameDx12Bridge.cpp=46677b2e9273a2169e1c0cb3632ac4053576efd1` and `XR_3DA.vcxproj=56108b14b9170abf7bf109c70061b72fb0d09196` match patch; parallel source changes from `5914debc1` to review HEAD do not touch DX12 patch paths. **Whole checkout git apply independently NOT TESTED**, Windows evidence reports PASS.
- **Independent CPU run:** restored parser/test sources directly from cumulative patch, compiled via Linux g++ C++20 -O2 -Wall -Wextra and executed **72/72 hierarchy assertions PASS**. Source review confirms `visualInfo.find(9)` after ReadVisual and before objects/snapshot/PrepareOrdered, so previous R4 false-PASS mechanism is closed; valid target outside frustum remains allowed. Full `GameDx12J2A_Run` fault injection (9/9 PASS R5, three false-PASS R4 prefix) is **provided MSVC log**, not independently executed here.
- **Windows evidence only:** Jupiter sector 129/root17431, 20 collected references → 5 validated instances, four visible CPU packets, hash EC3EAADD55DFCD6C, Serial=Parallel=Verify; 24/default/20 PASS, 19/8 fail before ReadVisual/prepare; one-shot reset/teardown/shutdown and feature-off R4 Present PASS by logs. J2A warm total 170.764ms, mainly VFS+hierarchy parse; not multi-object GPU rendering. Feature off/on/camera-on x64 rebuild and imports 0/1/1 reported in logs, not independently rerun.
- **P3 to be independently classified by Codex:** `GameDx12J2APreparation.cpp` publishes `result.ok=true` and success-only counters/hash around line 511 *before* fallible post-success diagnostic `packetIds.reserve` and logging (lines ~524–569). Allocation failure caught at ~572 sets `result.ok=false` and error reason but leaves prior packets/hash/counts nonzero. Return bool remains false, so no confirmed P2 false success, but contradictory diagnostic payload is possible. Recommend deferred success publication / clearing success-only metadata on failure and an allocation-fault injection test. **Risk from source control flow, not reproduced runtime fault.**
- **Gate verdict:** **TECH-LEAD SOURCE/CPU PASS WITH NOTES**, no new confirmed blocking P1/P2 in available reviewed paths. **FORMAL J2A R5 acceptance PENDING** until an actual independent full cumulative **Codex PASS/PASS WITH NOTES** is supplied and P3 is dispositioned. No J2A source integration, staging, push or J3; owner authorization and postmerge Windows regression required even after final source acceptance. Current J2B PREP remains design only.

### F4.0-J2A R5 independent Codex verdict → R6 corrective gate (2026-11-10; REQUEST CHANGES)

- **Verdict:** full cumulative R5 independent Codex review **REQUEST CHANGES: two P3, no confirmed P1/P2**. R5 fixed previously verified R4 target visual9 survival defect, and accepted targeted CPU evidence remains valid. No source staging/commit/merge/push; J2A production and J3 **HOLD**. J1 postmerge remains accepted; J2B PREP remains design only.
- **Blocking P3 — false PASS after late bad_alloc:** GameDx12J2APreparation.cpp **511–569** sets result.ok=true, success counters/hash, and prints result=PASS **before** allocating diagnostic packetIds via unordered_set.reserve and insert. Catch **572–585** resets ok/sets reason but leaves nonzero success-only metadata. Codex independently injected std::bad_alloc immediately before reserve in **test copy of unmodified R5**: log result=PASS then exception/FAIL; return=false, ok=false, instances=3, packets=3, hash=6B4BFF32CCAAE1DB. Bridge checks bool, so no independently confirmed GPU lifetime issue; however PASS-only log consumers can falsely accept the evidence. **R6 requirement:** complete all potentially throwing diagnostic construction before publishing success metadata and the **one final PASS line**; on any failure preserve a consistent, documented Result contract (zero success-only packet/hash counts, no PASS) without hiding error reason. Inject allocation failures at both reserve and insert, including after CPU prepare; assert return false, ok false, zero accepted packet/hash and no misleading PASS. Keep valid success hash/packets unchanged.
- **Nonblocking P3 — quadratic visual-ID duplicate detection:** dx12levelhierarchy.cpp ParseVisuals **201–208** loops over all preceding visual IDs for each new ID, O(N²) up to kMaxVisuals=65536 (~2.15bn comparisons) before traversal budgets. Codex own parser-only ~3,932,196-byte fixture with 65,536 distinct visuals parses in ~2.31s on its machine, **not actual Jupiter full level runtime**. Before scaling, correct now in narrow R6 with memory-bounded worst-case O(N log N) (e.g. std::set or bounded IDs copy/sort/adjacent check), or justified collision-resistant bounded O(N) strategy; retain duplicate rejection, error reasons, kMaxVisuals and allocation/parse fail-closed behavior. Include 1k/4k/16k/65,536 scaling plus duplicate first/middle/last fixtures and malformed data.
- **Evidence:** independent Codex MSVC 72/72 hierarchy and 9/9 target fault injection, 2000 DAG longest paths, 1500 lookup + 1500 collect shared-instance oracles, 524 exact-capacity + 1029 insufficient-budget cases, scopeguard and CPU benchmark PASS; delivery/evidence manifests 6/6 and 41/41, full 17-path R5 cumulative patch checked against baseline and matching current target Git blobs. Jupiter sector129/root17431, 20 collected/5 valid/4 visible, hash EC3EAADD55DFCD6C, reset/teardown, feature builds and DX11/R4 **are provided Windows logs, not Codex game runtime**. Full current HEAD checkout not independently built by reviewer.
- **Next gate:** F4.0-J2A R6 — Transactional Result & Parser Scaling. Supply minimal R6 incremental and full cumulative patch, own fault-injection and parser scaling tests, existing regression oracle, Jupiter runtime / three MSVC build configuration logs, SHA manifests and ZIP. Independent Codex READ-ONLY verdict then separate tech-lead acceptance; only explicit owner authorization permits any production integration. J2B-M0 and J3 remain future gated work.

### F4.0-J2A R6 — transactional result + parser scaling independent targeted pre-review (2026-11-10; PASS WITH NOTES / CODEX PENDING)

- **Status:** R6 candidate for full independent Codex READ-ONLY review, NOT final accepted J2A. Previous R5 Codex blocking P3 (late bad_alloc after false `result=PASS`/dirty counters) and nonblocking P3 (O(N²) duplicate visual-ID parser) are corrected in **source and targeted test evidence**. No new independently verified P1/P2/P3 blocking finding in the reviewed R6 delta. J1 postmerge acceptance remains unchanged; **production J2A/J2B GPU/J3 HOLD**, J2B PREP design-only.
- **Archive verification:** R6 Delivery ZIP **11,541,910 bytes**, SHA256 `D35108A7E5E3FD91CF16AC77E2EB5C0A993646ECF9BEF07C0357DEA4B27689C9`, ZIP CRC PASS/root SHA manifest **6/6 PASS**. Evidence ZIP **11,495,000 bytes**, SHA256 `D965513F4080C3964574039F48E6C34EC3C4F19BA42F6AD18DE9B07563E06579`, ZIP CRC PASS/manifest **55/55 PASS**. Full cumulative patch **170,824 bytes**, SHA256 `E0D4AAC3F3F03CFB7439E6D7C5F244FF20973155E0DB0F01F03A8236B563EEB4`, 22 paths (2 modified/20 added). Incremental R6 patch **39,475 bytes**, SHA256 `2FFD816E1266C0A532362CA2AC90EA76929154CBC5739D392CD4FA2FC5A9F6C7`, seven paths. Isolated full patch application for **20 new files** only: all **20/20** postimage Git blob prefixes verified; new R6 source files byte-match supplied evidence. Reverse incremental apply restores **both R5 prefix source files byte-identically**. GitHub current HEAD at review time `a5c53dbfad97b866c66b791ded5dfa5c85a631df` has expected old two blobs `GameDx12Bridge.cpp=46677b2e9273a2169e1c0cb3632ac4053576efd1`, `XR_3DA.vcxproj=56108b14b9170abf7bf109c70061b72fb0d09196`; later commits were PhysX documentation-only. **A full clean-current-HEAD 22-path git apply and Windows build were NOT independently executed**; author-supplied logs report both.
- **Source contract P3 #1:** `ogsr_engine/xr_3da/GameDx12J2APreparation.cpp` `ClearSuccessMetadata()` resets `ok/instances/uniqueVisuals/packets/visible/culled/batches/lists/hash/serialMs/parallelMs/verifyMs` while keeping useful stage timing/provenance. `packetIds.reserve/insert` and detailed diagnostics complete before scalar-only success commit; wrapper logs one `result=PASS/FAIL` after `RunJ2A` returns. Supplied MSVC real-function injection suite **6/6 PASS R6**: late reserve, insert and before-commit injected bad_alloc yield `return=false`, `result.ok=false`, zero successful metadata and **0 PASS lines**; R5 prefix reproduces old false PASS. **Not re-run independently** because Windows/GR test harness unavailable. Reviewer should additionally assess `prepared=1` pre-commit diagnostic semantics and exception/low-memory behavior of reporting `reason`, neither a confirmed new bug.
- **Source contract P3 #2:** `ogsr_engine/Layers/xrRenderDX12/dx12levelhierarchy.cpp` `ParseVisuals` uses `std::set<uint32_t> seenIds`, worst-case O(N log N) duplicate detection. Published authored order preserved; exact cap, over-cap, duplicates first/middle/last, malformed input and injected bad_alloc leave no published partial hierarchy in tests. **Independently built and executed:** g++14.2 C++20 `-O2 -Wall -Wextra -pedantic` original hierarchy CPU `RunTests` **72/72 PASS**, plus R6 parser scaling suite PASS; own Linux median ms: 1024 0.164; 4096 0.679; 16384 3.146; 65536 **15.562**. Benchmark suite emits test-only `-Wmismatched-new-delete` GCC warnings for custom global new/delete; not claim warning-free. R5 Linux max-N comparison timed out; author-supplied Windows R5 median 2328.174ms vs R6 29.772ms (~78x), **not independently measured by this reviewer**. R6 Jupiter parser timing improvement 114.420ms R5 → **7.221ms** R6 from author-supplied Windows logs.
- **Other supplied Windows-only evidence:** target suite **9/9**; Jupiter default cap24/unset, sector129/root17431, **20 collected references** → five validated instances / four visible CPU packets (9,105,106,107) / culled 415, hash `EC3EAADD55DFCD6C`, GR Serial=Parallel=Verify. Default total warm owner 86.872ms (VFS67.916, Parse7.221, lookup3.141, collect1.074, ReadVisual6.881, Prepare0.128), not historical level-ready delay. Explicit cap20/24 PASS; cap19/8 `LimitExceeded`, zero ReadVisual/Prepare, clean zero success stats and no PASS line. Three x64 feature combinations built in provided Windows logs without MSB8027/LNK4042 but existing unrelated C4756 warnings; reset/teardown/shutdown and feature-off DX11/R4 Zaton reported. GPU multi-object Draw not part of J2A.
- **Acceptance path:** independent full **R6 cumulative 22-path Codex READ-ONLY review** including transaction fault injection, parsers and malicious fixtures, memory/owner/worker lifetime, exclusion of test-only `OGSR_J2A_CPU_FAULT_INJECTION` from production builds, git applicability at fresh HEAD and return-vs-log success semantics. Only after a positive verdict with findings dispositioned: separate final tech-lead acceptance and explicit user authorization to stage/commit/push J2A production source; then postmerge Windows feature matrix/Jupiter/feature-off reset. No J2B GPU or J3 until their separate gates. Full audit file: `J2A-R6-Independent-TechLead-PreReview.md`.

### F4.0-J2A R6 full Codex verdict → R7 no-throw failure & strict budgets (2026-11-10; REQUEST CHANGES)

- **Independent full R6 Codex verdict: REQUEST CHANGES.** R5 blocking P3 false PASS after post-preparation error was **CLOSED** by transactional publication; R5 O(N²) duplicate visual ID was **CLOSED** by std::set. New **one P2 blocking + two P3**; **no confirmed other P1/P2**. J2A production J2B GPU and J3 **HOLD**; J1 and J2B PREP statuses unchanged.
- **P2 blocking — repeated exception during failure handling escapes game frame:** `ogsr_engine/xr_3da/GameDx12J2APreparation.cpp` ~625–637 internal catch clears scalar success metadata then does `result.reason = error.what()` and `Msg`, both potentially allocating. Caller wrapper `GameDx12J2A_Run` calls `RunJ2A` outside a try; `GameDx12Bridge.cpp` ~1498 also calls outside try and logs via allocating `Msg`. Codex injected persistent std::bad_alloc immediately before packetIds.reserve in a test copy of real Run+VFS+GR function. Two independent repro variants (long what causing reason allocation failure; short what followed by logging allocation failure) both **escaped=1**, `result.ok=0 packets=0 hash=0 final_fail_lines=0`. NOT a reproduced game crash, but source/runtime path independently proven. **Required R7:** no-allocation error clearing and durable fail code/fixed fallback without requiring std::string allocation; best-effort error text/logging wrapped in isolated no-throw guards; outermost `GameDx12J2A_Run` and bridge frame boundary prevent any exception propagation; no PASS on failed preparation, no silently assumed success. Under permanent OOM logs may be absent, so truthful requirement is **0 or 1 final FAIL and NEVER PASS**, while bool=false and fail-code always defined. Avoid claiming exact-one logged record when logger itself cannot allocate.
- **P3 double terminal FAIL:** `GameDx12J2APreparation.cpp` ~654 wrapper prints `result=FAIL`; `GameDx12Bridge.cpp` ~1502 prints second `result=FAIL` with reason. One actual explicit-cap19 Jupiter runtime log has duplicates. Preserve only one terminal `result=PASS/FAIL` emission per invocation, assign bridge secondary context `failure_detail` without terminal token, both best effort and with OOM-safe guard. Add integration-level log counting for normal/budget/OOM. Existing `prepared=1` is a nonterminal stage diagnostic; reviewer found no confirmed downstream parser issue.
- **P3 transient one-over-cap allocation:** `dx12levelhierarchy.cpp` ~205 `seenIds.insert(type)` before checking `output.visuals.size() >= kMaxVisuals`. At 65,537th distinct ID may allocate one node then fail; injected OOM changes intended `visual count exceeds limit` to `bad_alloc`, no partial published hierarchy. Reverse lookup ~1138 inserts parent into `reachable` before node budget check, similarly exceeds by one entry. **R7:** check duplicate membership first (preserve duplicate error priority), capacity/budget before introducing novel ID/parent, insert only once capacity available; retain actual counters, traversal semantics, error priority and allocation-fail rollback. Test 65,536 unique + 65,537th unique, duplicate at capacity, reachable exact node budget + next edge, and OOM before potential over-cap insertion.
- **Independent Codex CPU evidence:** MSVC hierarchy 72/72, target 9/9, transaction 6/6; scope-guard and GR prep benchmarks PASS; ASan CPU suite returned zero with 566,486 snapshot, 1,519,828 visibility, 4,892 ordering checks; duplicate visual9 instances preserved 5 packets/4 distinct ReadVisual calls. 2000 DAG and 1500+1500 lookup/collect from prior independent review. Own comparable parser scaling R5 65,536 median 2,385.976ms vs R6 29.692ms (parser-only; not game frame). All 4 package hashes and manifests 6/6,55/55 verified. R5 cumulative+R6 incremental byte equal cumulative R6; 22 paths (2 changed/20 new), isolated apply to old baseline and touched-path reconstruction from `8eb8f20755ad265e24be5d39883defb35c6cea45` PASS; **full current HEAD rebuild not independently demonstrated**.
- **Windows evidence limits:** provided Jupiter logs sector129/root17431, 20 refs, five validated, four visible, hash EC3EAADD55DFCD6C, 20/24 PASS, 19/8 FAIL, reset/teardown and feature-off R4. Independent full MSVC solution build blocked at third-party libvorbis C1902 (environment root unknown); two remaining configs and DX12 imports were NOT independently checked. Own source MSBuild evaluation excludes test injection macros and duplicate ClCompile. J2A remains CPU-only and J3 uses Smoke/GR later.
- **Next gate:** **F4.0-J2A R7 — No-Throw Failure Boundary & Strict Budgets** with minimal incremental + cumulative patches, isolated persistent OOM faults for reason and Msg, one final log integration test, over-cap parser/reverse fixture, all 72/9/6 previous assertions, GR/ASan safety evidence, honest Windows build and game logs, manifests+SHA. Then full independent Codex READ-ONLY and separate tech lead acceptance; separate explicit owner authorization is mandatory for any J2A production staging/commit/push. Do not start GPU J2B or J3.

### F4.0-J2A R7 — historical targeted independent source/CPU pre-review (2026-11-10; PASS WITH NOTES prior to full Codex)

- **Disposition:** no new confirmed blocking P1/P2 in targeted R7 source changes; prior R6 Codex P2 persistent OOM escape from error handler and two P3 (double terminal FAIL; transient allocation beyond parser/reverse lookup budgets) have source-level corrections and relevant supplied MSVC CPU fault regression evidence. **This is NOT an independent full cumulative Codex PASS or J2A acceptance.** J2A production, J2B GPU implementation and J3 **HOLD**. J2B PREP remains accepted design-only.
- **Package integrity / applicability:** `F4.0-J2A-R7-Delivery.zip` **11,422,632 B**, SHA256 `F1E107204DF7DDED692F24D3431EE8A0008060ED655B94988BD3E8736E55E0AB`; ZIP CRC and root manifest **6/6 PASS**. `F4.0-J2A-R7-Evidence.zip` **11,365,820 B**, SHA256 `D2B1EDB1205F1EB8AEF5B03A4C31A91CE7F361E31F6553222E847248067171F1`; CRC and evidence manifest **63/63 PASS**. Incremental seven paths/61,525 B SHA256 `8AFF729C7A09287675E0CA8C7AF6B8813CD1E183BC40FA0C65EACCDF529B85A5`. Full cumulative **24 paths** (2 modified/22 added)/208,742 B SHA256 `A3FA3104E4E265358A6160C0608389E89414B7927E92322CDFEFFAD34A645AD0`. Independently applied **23/23** of cumulative paths in isolated reconstruction, except `XR_3DA.vcxproj` due to lack of unmodified local project bytes; 23 Git blob postimages match patch index; separately live `prypiat` project old blob `56108b14b9170abf7bf109c70061b72fb0d09196` matches patch. Reversed and re-applied R7 incremental; **five R6 source/test files byte-identical** to supplied prefix. Independently built clean-current-HEAD 24-path tree and MSVC compilation **NOT TESTED**. Live HEAD checked `a8c4b2c465e5a113bd1cbb0a5fda00bde21705dc`; later changes relative to R7 documentation baseline are PhysX docs, not DX12 code.
- **Source P2 fix:** `GameDx12J2APreparation.h/.cpp` adds fixed `GameDx12J2AError12` and no-throw scalar `ClearSuccessMetadata`; `SetReasonBestEffort` traps `std::string` allocation failure; `LogBestEffort` formats in a fixed 2048-byte stack buffer and traps exceptions from actual engine `Msg`; both `GameDx12J2A_Run` and `GameDx12J2A_RunForBridge` declared `noexcept`; `GameDx12Bridge::OnFrame` additionally catches and protects its diagnostic `Msg`. Author MSVC R7 actual-production-function-with-VFS/ReadVisual/Msg-shims test **12/12 PASS** for persistent OOM/reason/Msg and bridge adapter; R6 prefix showed three escapes. **The 12/12 fault suite was not run independently here.** No final FAIL log is guaranteed under permanent OOM; correct contract is controlled false + non-None error, zero success metadata, **never a false PASS**.
- **Source P3 single terminal logging:** `GameDx12J2A_Run` owns final timing `result=PASS/FAIL`; bridge uses non-terminal `failure_detail`. Independently inspected supplied game logs: Jupiter default/24/20 each one terminal PASS, 19/8 one FAIL + one failure_detail, no double terminal lines; feature-off Zaton log **zero** `ph_dx12` messages. This is author runtime evidence review, not an independent game run.
- **Source P3 strict capacity + independent CPU:** `ParseVisuals` executes duplicate lookup → `kMaxVisuals` check → `seenIds.insert`; reverse lookup checks seen parent then node budget before insert and charges repeated edges appropriately. Independently GCC C++20 built `dx12levelhierarchy.cpp` + hierarchy oracle and executed **74/74 PASS**; independently GCC C++20 built and executed parser scaling/strict fixtures PASS (incl. 65536 capacity, 65537 fail without extra allocation, duplicate-at-capacity, rollback on bad_alloc, malformed input). Own parser-only Linux median ms 1024: 0.141; 4096: 0.589; 16384: 3.052; 65536: **14.979**. Instrumented allocation harness emits GCC `-Wmismatched-new-delete` warnings due to its custom global new/delete; do not claim warning-free. Supplied MSVC ASan hierarchy 74/74, target9/9, transaction6/6, GR guard/bench are not independently executed.
- **Windows evidence only:** Jupiter sector **129**, root **17431**, default cap24, 20 collected references, 5 validated instances, 4 visible CPU packets, culled visual415, stable hash `EC3EAADD55DFCD6C`, parse4.390ms/total55.933ms, repeat reset epoch4/Jupiter→Zaton teardown/shutdown. 20/24 PASS, 19/8 LimitExceeded with no success counters/hash and one FAIL. Three supplied Release x64 feature builds show output exe without MSB8027/LNK4042; some unrelated C4756/C4723 warnings present; no independent current-HEAD Windows full build or live gameplay.
- **Next gate:** complete independent Codex READ-ONLY **full 24-path R7 cumulative review**, repro resilient OOM and final-log failure semantics, cap boundary and lifetime, build/feature toggles; separately final tech-lead acceptance and explicit user authorization before production staging/commit/push; only afterward Windows postmerge regression and J2B-M0. `J2A-R7-Independent-TechLead-PreReview.md` is the available audit artifact.

### F4.0-J2A R7 — full independent Codex verdict (2026-11-10; BLOCKED — INSUFFICIENT EVIDENCE)

- **Verdict/authority:** independent Codex full cumulative READ-ONLY review reports **BLOCKED — INSUFFICIENT EVIDENCE**, **not REQUEST CHANGES**. No new confirmed Critical/P1/P2/P3/P4 defects with production reproduction in the reviewed scope. This supersedes the earlier `FULL CODEX PENDING` checkpoint, not the historical targeted PASS WITH NOTES. R7 is **NOT ACCEPTED**. J2A production staging/commit/push, J2B GPU implementation and J3 **HOLD**; J2B PREP stays design-only. Source review has not authorized GPU work or merge.
- **Provenance:** review report `F4.0-J2A-R7-Independent-Codex-Review.md` (2026-11-10), uploaded review copy SHA256 `C02E12A5C57CDAC7BB853741611C0476BF7A7E3C831E47A5BAF6FC0A5935DE10`. Author R7 Delivery/Evidence SHA256 and 6/6 + 63/63 manifests independently validated; `git apply --check` and cumulative 24/24 postimage Git blob identities checked in an isolated reconstruction. Reviewer's local remote-tracking ref was stale relative to GitHub; reviewer verified 4 subsequent documentation-only commits up to `fd0d4894`, with no engine-file diff. Later docs-only tech-lead ownership commit `63a006a` does not constitute J2A integration. Re-check actual GitHub HEAD immediately before integration.
- **Independently reproduced:** on Windows x64/MSVC, hierarchy **74/74**, target **9/9**, transaction **6/6**, no-throw **12/12**, parser scaling/strict-budget fixtures, GR scope/preparation, hierarchy ASan **74/74**, no reported ASan faults. Checked R7 no-throw under persistent OOM/reason/throwing logger in production functions with test shims; no escaped exception or false PASS in tested paths. Historical R1–R6 issues **CLOSED only in tested fixtures/scope**; full real-engine/VFS/GR contract not established. Independent `feature-off/on/camera-on` full engine builds and actual game runtime **NOT RUN**; their logs are **author evidence only**.
- **Open evidence gate E1 — real VFS cleanup:** audit all reachable `CLocatorAPI::r_close`/reader destructor implementations and `dx12scopeguard.h` destructor during exception unwind. Determine from production callers/contracts and/or isolated real-VFS fault test whether cleanup can throw and cause `std::terminate` inside noexcept cleanup. Do not invent a P2 absent a reachable failing case. Any confirmed defect requires a separate minimal corrective patch/review.
- **Open evidence gate E2 — GR and lifecycle:** verify actual owner-thread-only execution of `GameDx12J2A_Run`/`RunForBridge`, GR `Submit/Wait/Drain` and `g_j2aJobs` init/shutdown; trace `OnFrame`, level teardown, reset, session/epoch and repeated calls. Prove accepted jobs finish before destroying snapshots, job group, level or pool, including submission failure. Shim-only results are insufficient for final lifecycle claim; independent source/call-graph audit and focused real-backend tests required.
- **Open evidence gate E3 — full Windows game/build:** independently rebuild feature-off, feature-on and camera-on from exact R7 24-path postimages; record command, toolchain, build exit, output hashes, import/toggle boundaries, MSB8027/LNK4042 and test-only macro isolation. Independently check Jupiter default/20/19, reset-before/after, Jupiter→Zaton teardown/shutdown and feature-off DX11/R4 Zaton Present; if game access unavailable, an explicitly scoped equivalent instrumented trace must be reviewed and approved independently, with remaining limitations stated. Author-provided screenshots/logs are not independent execution evidence.
- **Open evidence gate E4 — production dependencies:** finish call-chain and exceptional-path review, malformed-input and budget checks relevant to production, and source/Git current-HEAD applicability. No requirement to prove all theoretical faults impossible; acceptance demands sufficient evidence for documented reachable engine behavior and failure contract.
- **Performance note:** author one-shot Jupiter CPU preparation `parse=4.390ms/total=55.933ms` is not GPU frametime or FPS. The synchronous VFS/parse work in `OnFrame` is a nonblocking J2A CPU-prototype risk and must be budgeted/removed from steady game frame path before J2B/J3 scaling.
- **Next decision:** request a narrow **R7 Evidence Closure** package, not a new feature or speculative R8 rewrite: VFS/GR/lifecycle trace + real-engine/build/runtime checks, exact logs, SHA manifests and no production edits unless a reachable confirmed defect is found. Then Codex independent READ-ONLY re-review of those gates and explicit final tech-lead acceptance. Only afterward ask project owner for separate permission to integrate; post-merge Windows regression precedes J2B-M0 and J3 stays HOLD.

### F4.0-J2A R7 — Evidence Closure delivery and Tech Lead artifact audit (2026-11-10; READY FOR CODEX RE-REVIEW / NOT ACCEPTED)

- **Disposition:** DeepSeek supplied `F4.0-J2A-R7-EvidenceClosure-Delivery.zip` plus `F4.0-J2A-R7-EvidenceClosure-RawEvidence.zip`, with recommendation **READY FOR INDEPENDENT CODEX RE-REVIEW**. Tech Lead independently checked delivery/raw ZIP SHA-256, ZIP CRC, combined `MANIFEST.sha256` **99/99 PASS, 0 missing, 0 mismatch**, and reviewed representative VFS/GR/build/runtime logs. These validate package integrity and internal evidence consistency; **Windows builds/game runs are author execution, not independently replayed by Tech Lead or Codex**. Previous full Codex **BLOCKED — INSUFFICIENT EVIDENCE** verdict stands until a new independent written verdict. No J2A code/PhysX/GR implementation was changed for this closure.
- **Artifacts:** Delivery **498,081 B**, SHA256 `F55496803AFAF32EB5748A02EBF753F3985180D3E2091349264B8D565C2CB4B4`; RawEvidence **65,734,876 B**, SHA256 `B50B517AC37E0E68A2B39FFF91F637A07B6B183581011DF4F3C6CF3537F79037`; combined manifest SHA256 `E3AEB7936F117FFF8D83B8F61BBC54E0183F85A293AD3B85E6AF19F379B8DDBE`, 99/99. R7 cumulative **24 paths** SHA256 `A3FA3104E4E265358A6160C0608389E89414B7927E92322CDFEFFAD34A645AD0`, author postimages 24/24, source snapshot `E29522404A21ADA0A3415DDE6E0733F2F4FA0FF611CD1F02B28621247418C1ED`. Worktree reconstruction was at `3be661d1a` before later docs-only changes; revalidate current HEAD before any integration. Main checkout `42b8124e5`, staged=0 per author unchanged status hash.
- **E1 — author PASS with explicit limits:** audited production `ReadVfsFile`/scope guard/`CLocatorAPI::r_close` and Release reader destructors; scope guard test PASS; production `GameDx12J2A_Run` with **VFS shim** tested missing `level`/`level.geom`, zero/oversize and allocation failure without false PASS; real engine `-file_activity` J2A success and no `level`/`level.geom` in open-file shutdown dump. Independent runtime logs reviewed but these observations alone do not prove all allocation failures in actual engine `r_open`. Standalone `xrCore.lib` `FS._initialize` probe failed outside engine bootstrap before reader open: **UNVERIFIED**, not a confirmed J2A bug. Potential engine `-file_activity` open-side allocation risk noted by author, not reproduced.
- **E2 — author PASS WITH LIMITATIONS:** owner-thread `OnFrame`→`RunForBridge`→`Run`/GR `Wait` and owner-thread Shutdown source graph; public GR backend probe **10/10**, `callback_threads=3`, worker exception rethrow, 24/24 accepted tasks drained on shutdown, re-initialize/reuse; actual runtime default/reset/repeated Jupiter sessions and teardown logs show 1/2 terminal PASS as expected, matching stable hash. **Live-engine GR Submit/Wait fault injection UNVERIFIED**, no safe hook; Codex must determine whether production call graph, public backend probe and runtime trace satisfy the contract.
- **E3 — author PASS, log/file spot-check:** three full Windows Release x64 MSVC Rebuilds (feature-off/feature-on/camera-on), reported exit 0 and `MSB8027=0`/`LNK4042=0`; actual three `xrEngine.exe` in raw evidence matched SHA256 respectively `E87F456B87C318C4F134440D829E6A0BEE98841612A552DCA2AB712327BB5694`, `2263683733E11725B80CF40B6CAB6C159AE7494938A89492B4B723B829AE1E20`, `3C1546CAD9E1F229EB306BFA2602EB0EEAE6462C40705735AF863D436B3D90DA`. Author Zaton feature-off R4 Present and `ph_dx12=0`, Jupiter default/24/20 PASS, cap19/8 LimitExceeded one terminal FAIL, reset/teardown, Jupiter→Zaton and repeat two-session same hash `EC3EAADD55DFCD6C`. Log spot-check confirms default/repeat terminal counts and cap19 failure detail. Isolated worktree used 12 read-only third-party junctions; verify reproducibility/environment and binary-source association in Codex re-review.
- **E4 — author PASS WITH NOTES:** CPU-only synthetic `Resource12{visualId,1}`, PSO/material placeholders `{1,1}`; no GPU resource/Draw/residency or postmerge native D3D12 acceptance claimed. Independently maintained DX11/R4 fallback evidence. No confirmed new reachable Critical/P1–P4 in author scope. CPU one-shot Jupiter preparation now ~59ms, with VFS ~48ms (~82%): **diagnostic frame stall risk** for future production; loading-phase immutable snapshot/caching remains future J2B design work, not an R7 change.
- **Independent next gate:** Codex **STRICT READ-ONLY focused E1–E4 evidence re-review**, examine the actual bundled source/GR probes and their representativeness, check `-file_activity` semantics, verify build snapshot and MSVC outputs, independently confirm runtime log assertions, and explicitly decide whether remaining UNVERIFIED live GR/standalone VFS coverage is acceptable for J2A one-shot or must remain BLOCKED. Demand focused reproducible evidence, not speculative defect reports or unwarranted new R8. Produce `PASS` / `PASS WITH NOTES` / `REQUEST CHANGES` / `BLOCKED — INSUFFICIENT EVIDENCE` with E1–E4 per-gate decisions and exact references. Until then J2A **NOT ACCEPTED**, production staging/commit/push **HOLD**, J2B GPU/J3 **HOLD**, J2B PREP design-only; separate final Tech Lead decision/owner authorization required after favorable verdict.

### F4.0-J2A R7 — independent Evidence Closure re-review + final Tech Lead pre-integration acceptance (2026-11-10; PASS WITH NOTES / ACCEPTED WITH NOTES; PRODUCTION HOLD)

- **Verdict and limits:** independent Codex `F4.0-J2A-R7-EvidenceClosure-Independent-ReReview.md` (SHA-256 `778A03C3AB18E063E812BDDDC2897A06FD5B963D79B7FED79D93C970320ED131`) returned **PASS WITH NOTES**, superseding its earlier `BLOCKED — INSUFFICIENT EVIDENCE` **only for the opt-in, owner-thread, synchronous CPU-only one-shot J2A R7 diagnostic**. After review of the full report, Tech Lead records **FINAL PRE-INTEGRATION ACCEPTED WITH NOTES** for this narrow scope. This is **not a production implementation acceptance, not a GPU acceptance, and not authorization to modify, stage, commit, merge, or push J2A production code**. There were no new confirmed Critical/P1/P2/P3 faults in the independently reviewed scope. Earlier R6 REQUEST CHANGES and R7 evidence BLOCKED remain historical records, with their successor decision stated here.
- **Independent evidence:** earlier cumulative-source/MSVC CPU tests hierarchy **74/74**, target **9/9**, transaction **6/6**, persistent no-throw **12/12**, parser scaling/capacity and GR prep **PASS**, hierarchy ASan **74/74**. Current re-review independently rebuilt and ran VFS failure probe **5/5**, scope-guard **5/5**, production **GR backend** lifecycle probe **10/10** with drain, callback/rethrow and reinit. Verified Evidence Closure Delivery SHA256 `F55496803AFAF32EB5748A02EBF753F3985180D3E2091349264B8D565C2CB4B4`, RawEvidence `B50B517AC37E0E68A2B39FFF91F637A07B6B183581011DF4F3C6CF3537F79037`, combined SHA manifest **99/99 PASS**, Git patch postimages **24/24**, three executable SHA256s and PE dependencies. Three full MSVC Release builds and live Zaton/Jupiter game runs were **performed by DeepSeek, not independently rerun by Codex**; Codex inspected original logs, binary bytes, imports, screenshots and source identities.
- **E1 — ACCEPTED WITH NOTES:** source audit of `ReadVfsFile`→`FS.r_open`→scope guard→`FS.r_close`, real reachable reader destructors and guard; VFS failure probe 5/5, guard 5/5. Real `-file_activity` shutdown registry lacks active Jupiter `level`/`level.geom` entries in the author success run, not proof against all failure-path leaks. **Unverified nonblocking risk:** `LocatorAPI::_register_open_file` may allocate after reader ownership is obtained but before `r_open` returns; hypothetical OOM at this engine-wide registration boundary could bypass J2A's post-open guard. No reachable confirmed leak established; to claim general engine-wide OOM safety, test registration-failure ownership separately. Standalone `xrCore` probe failed before VFS open due to missing engine bootstrap, not treated as J2A bug.
- **E2 — ACCEPTED WITH NOTES:** actual owner-thread OnFrame/Run/Shutdown routes and GR JobGroup Submit/Wait/Drain call graph reviewed; genuine GR backend probe 10/10 and author repeated session/epoch/reset/teardown logs align. **Not tested:** forced live-engine GR backend Submit/Wait failure and reset *while a Jupiter J2A attempt is pending*. The supplied `reset-before` actually reset Zaton before Jupiter level-ready and does **not** demonstrate pending Jupiter cancellation. This is an explicit future regression scenario before widening lifecycle contract; no confirmed data race in supported owner-thread one-shot.
- **E3 — ACCEPTED WITH NOTES:** DeepSeek's feature-off/feature-on/camera-on x64 Release Rebuild logs (MSBuild reported exit 0, no MSB8027 or LNK4042), binary hashes, PE imports independently verified by Codex, but full rebuilds and game runtime remain **author execution evidence**. Zaton feature-off DX11/R4, Jupiter default/24/20 PASS, 19/8 LimitExceeded with zero success metadata, repeated sessions (same hash `EC3EAADD55DFCD6C`) and teardown were inspected in supplied logs; no independently launched gameplay or full independent rebuild.
- **Evidence corrections:** `-file_activity` `----total count = [14550]` is `g_open_files` **registry vector size, not active open-reader count**; report notes **3,635 `fname:` lines** in the printed dump. Fixture `PARSER_SIZE 1024/65536` indicates **visual entry counts, not bytes/KiB**. The two-run same-process Jupiter times are **61.508/59.071 ms**; `59.279 ms` belongs to a different default run. Correct these metadata labels in subsequent evidence publications, without changing original historical logs.
- **E4 — ACCEPTED WITH NOTES:** J2A `Resource12`/material/PSO identities are synthetic CPU placeholders, not GPU VB/IB registry handles, residency or Draw. One-shot synchronous VFS/read/parse/GR on `OnFrame` is accepted only as an opt-in diagnostic with measured total wall time roughly **59 ms** and VFS portion roughly **48 ms** in author's default run; **not a gameplay frame-time budget** and not suitable as a recurring update path. Before J2B-M0 specify cold/warm/p95 budget and preloading outside the gameplay critical frame, resource/session/device-generation ownership, actual geometry/material/PSO identities, GPU upload/fence completion and reset/removal.
- **Next controlled gate:** (1) Tech Lead records this pre-integration verdict (this entry); (2) **explicit project-owner authorization** is required for a narrowly scoped integration of the exact 24-path cumulative R7 snapshot; do not auto-commit/push; (3) prepare and review isolated merge/apply diff against the *then-current* remote `prypiat` and PhysX changes, verify toggles/feature-off compatibility, production/test-only macro isolation and source hashes; (4) after approved staged integration, independently evaluate postmerge Windows full builds, Jupiter default/limit/repeat/reset/teardown and Zaton DX11/R4 GPU Present; (5) only then issue a separate **J2A production acceptance** decision and consider J2B-M0 compatibility gate. **J2B GPU implementation and J3 remain HOLD**.

### F4.0-J2A R7 — project-owner authorization to begin controlled production integration (2026-11-10; START AUTHORIZED / NOT INTEGRATED)

- **Authorization:** project owner explicitly approved starting controlled R7 integration into `prypiat` after the independent `PASS WITH NOTES` and final tech-lead pre-integration `ACCEPTED WITH NOTES`. This authorizes preparatory work and verification on the exact **24-path R7 cumulative patch** (SHA256 `A3FA3104E4E265358A6160C0608389E89414B7927E92322CDFEFFAD34A645AD0`) for the **opt-in, synchronous owner-thread CPU-only J2A one-shot diagnostic**. It is not an unconditional acceptance of a pushed revision, not an authorization to start J2B/J3, and not a claim that any production integration has already occurred.
- **Baseline:** GitHub `prypiat` HEAD at authorization checked as `64bec12b7dc7e54bb6741dcb240576b759e4e04b` (roadmap-only last update), but DeepSeek must fetch the actual remote HEAD immediately before isolated reconstruction and compare engine blobs/patch compatibility. Do not overwrite other existing local worktrees, PhysX changes, or WIP; preserve `AGENTS.md` scoped-read rule.
- **Integration gates:** (G0) clean isolated worktree/branch from real remote HEAD, patch SHA, exact 24 paths/postimages and cumulative diff audit; (G1) feature-off/on/camera-on x64 MSVC builds and test-only macro exclusion; (G2) independently rerun CPU hierarchy 74/74, target 9/9, transaction 6/6, no-throw 12/12, parser/strict-budget/GR and ASan; (G3) isolated real-game Jupiter default/24/20 and 19/8, repeat/reset/teardown/shutdown and Zaton feature-off DX11/R4 Present, error logs and session/epoch; (G4) independent Codex READ-ONLY integration-diff review and tech-lead decision; (G5) only after G0–G4 PASS/accepted notes, authorized controlled staged commit/push and verify remote SHA, with a separate post-push Windows/R4 regression and final **J2A production acceptance** review.
- **Hard stop:** on patch conflict, unaccounted engine changes, mismatch in source hashes, new reproducible Critical/P1/P2, failed builds or regressions, publish nothing; prepare minimal finding and evidence for tech-lead review. Do not silently rebase/drop PhysX changes; no unreviewed refactor, no second GR scheduler, no GPU J2B or J3 tasks, no roadmap edits by DeepSeek/Codex.
- **Status at time of this documentation commit:** **START AUTHORIZED, IMPLEMENTATION/INTEGRATION PENDING.** No J2A production files staged, committed or pushed as part of this document update. J2B PREP remains design-only; J2B GPU and J3 remain HOLD.

### F4.0-J2A R7 — Controlled Integration PrePublish G0–G4 delivery (2026-11-10; READY FOR INDEPENDENT G5 / G6 HOLD)

- **State and scope:** after project-owner permission, DeepSeek prepared `integration/f4-j2a-r7` in isolated `D:\OGSR_J2A_R7_Integration` from GitHub `prypiat` HEAD `72657ca539316a2080de047b8a1f70e76ec8b5e6`, applied exact R7 cumulative patch into working tree **without staging, committing or pushing any J2A code**. External local main checkout later switched to `02d2a003d` by a different process per author; original tracked/untracked status hash reportedly unchanged. Remote `prypiat` verified by Tech Lead at the time of this documentation update: `72657ca...`. All worktree and source Git claims for Windows environment are author evidence, not Tech Lead remote staging access. **G0–G4 author execution complete; G5 means pending independent Codex integration review, not completed G5 approval.** G6 remains HOLD.
- **Package integrity verified by Tech Lead:** Delivery ZIP **688,386 bytes**, SHA256 `23B0E6E54C741D37AF90BAEBA2DED27BEC179F8DD84786AC4E1B5A0FE78F629D`; RawEvidence ZIP **67,645,383 bytes**, SHA256 `572617A045513CA79100A3597CAD3EB4C758056A4FF745F7D598213B64B79E97`; ZIP CRC PASS and combined `MANIFEST.sha256` SHA256 `F02A7F10982EE1982B35747A20C2CC7C5E0C0A21E79D70D656794228BE4CEAF7`, **121/121 PASS**, 0 missing/mismatched. Both bundles contain the full prepublish report, source identity, patch/diff, build/test logs, 3 binaries, original J1 runtime evidence and preserved first harness error.
- **Source identity verification:** original `F4.0-J2A-R7-Cumulative.patch` SHA256 `A3FA3104E4E265358A6160C0608389E89414B7927E92322CDFEFFAD34A645AD0` and integrated `git/integration.diff` SHA256 `1DA90AA1727EB093D004C5D2BEB7E60D8A992A70FAE917EBE57A3DD0B73C7ECF`; 24 paths exactly identical across two patches, and **all 24 unified-diff hunk bodies identical** in independent archive audit, 22 new + 2 modified, `+5451/-4`. Integration diff is **UTF-16LE** because of the export method; decode/convert to UTF-8 before passing to Git or reviewing as text. Author confirms 24/24 Git blob postimages and per-file SHA matches; historical aggregate `E2952240...` cannot be reproduced by the documented method; recalculated aggregate `DD5DB4F8...`. This is a provenance metadata issue, NOT a confirmed source divergence.
- **Build/CPU author tests with validated supplied artifacts:** 3 complete x64 Release MSVC Rebuilds (feature-off/on/camera-on), author exit=0 and no `MSB8027`/`LNK4042`; Tech Lead recomputed PE EXE SHA256 and verified 64-bit PE headers, supplied build logs have no searched fatal errors. EXE SHA256: off `7A3F083200F9F67DB19C2009BD944E11D008E9B7D0CD14AF52C3EE357047D1FB`; on `567A7BA9423F58F8CC66AC7A66167DD43BBA4A7DA687CF73A1EEEF41CDFE0C53`; camera `A228007685D166FC71D48AE68327DAFA06F1CE7F12CC525241C2CC21FF76864D`. Feature-off supplied PE import report excludes `d3d12.dll`; CPU hierarchy **74/74**, target **9/9**, transaction **6/6**, no-throw **12/12**, parser/GR, GR backend **10/10**, VFS **5/5**, guard **5/5**, ASan hierarchy **74/74** author logs. Independent Windows rebuild and runtime execution by Codex **not yet performed for this integration snapshot**.
- **Game/runtime and J1 preservation in author logs:** feature-off Zaton DX11/R4 Present with no `ph_dx12`; Jupiter default cap24 collected20/validated5/visible4/culled1, packet hash `EC3EAADD55DFCD6C`, author total **71.744ms** in default sample (VFS57.859ms); cap24/20 PASS, cap19/8 LimitExceeded code6 and success metadata/hash zero, one terminal FAIL and one detail; two Jupiter sessions stable hash/session epoch, reset after J2A and level teardown; J1 camera capture visual9 actual `DrawIndexedInstanced(1314,1)` to offscreen target, fence3, `gpu_completed=1`, nonclear `10623/4096000`, Debug Layer/GBV InfoQueue error/corruption/lifetime=0, R4 Present, reset-after-Draw refused second draw. First J1 harness test prematurely matched Zaton `diagnostic skipped`, preserved as **test-only harness issue** with successful corrected rerun.
- **Still NOT verified and not in one-shot acceptance:** reset while Jupiter J2A pending; J1 reset-before-Draw camera harness; live-engine GR Submit/Wait fault injection; engine-wide VFS `_register_open_file` OOM. J2A is CPU-only with synthetic material/PSO/resource IDs; no 4-object native GPU draw or GPU residency. Synchronous diagnostic preparation ~50–72ms is not an acceptable recurring gameplay frame budget; requires separate loading-phase/budget design for J2B.
- **Next strict gate:** Codex independent **READ-ONLY G5 review** of the exact integration diff and build/runtime provenance from these archives, including UTF-16LE decoding, 24 source postimages, no dropped branch changes, feature-off and J1 preservation, source aggregate discrepancy, and documented unverifiables. Codex verdict `PASS` / `PASS WITH NOTES` / `REQUEST CHANGES` / `BLOCKED`, with precise findings; then Tech Lead GO/NO-GO. **Do not stage/commit/push automatically.** On positive GO, compare new remote HEAD, create exactly the approved 24-path commit, non-force push, then post-push Windows/CPU/GPU R4 regressions before separate J2A production acceptance. J2B GPU/J3 **HOLD**; roadmap maintenance by ChatGPT only.

### F4.0-J2A R7 — independent Codex G5 result and Tech Lead publication decision (2026-11-10; G5 ACCEPTED WITH NOTES / G6 CONDITIONAL GO, NOT AUTHORIZED TO PUSH)

- **Decision:** independent `F4.0-J2A-R7-Integration-Independent-Codex-G5-Review.md` full report, SHA256 `DB533484CB68D0A4D348B624F39839E818437DA892B2A689E68DE787CE51B0AD`, concludes **PASS WITH NOTES**, `YES WITH CONDITIONS` for G6. Tech Lead reviewed the report and **ACCEPTS G5 WITH NOTES**, issues a **technical CONDITIONAL GO** for preparing to publish the *same exact 24-path R7 source*, subject to all pre-push gates. **This is not the owner's separate authorization to commit/push.** No J2A code is staged, committed or pushed by this documentation update. G6 is currently **AWAITING OWNER APPROVAL**; J2B GPU/J3 remain HOLD.
- **G0/G1 — independently verified:** `integration/f4-j2a-r7` isolated branch at base `72657ca539316a2080de047b8a1f70e76ec8b5e6`, stage=0, engine changes 24 expected paths (22 added/2 modified), `git diff --check` PASS. Actual 24 Git blob postimages, 24 raw per-file SHA256, 24 decoded UTF-16LE diff hunks and original cumulative patch all MATCH. Historical `E295...` aggregate not reproducible because original aggregation algorithm not documented; deterministic verified alternative `DD5DB4F8AA856475492F05EC651AFA0713F1BC223EA57688E86F916BFF6FF44B`; no source bytes mismatch. Prepublish combined archive manifest 121/121, CRC/SHA verified. GitHub remote `prypiat` at decision `2254931932faf43bc3eebf14da9e6cc81c03a673` is one **docs/08-roadmap.md-only** commit ahead of integration base, no engine changes. DeepSeek must re-fetch HEAD immediately before any eventual commit/push and preserve all new documentation.
- **G2/G3 — evidence classification:** Codex read original author MSVC x64 Release feature-off/on/camera-on Rebuild evidence and independently verified three PE32+ hashes/imports (feature-off `d3d11/dxgi`, no `d3d12.dll`; feature-on/camera-on D3D12 imports). Builds/gameplay **NOT independently rerun by Codex at G5**. CPU hierarchy74/74, target9/9, transaction6/6, no-throw12/12, hierarchy ASan74/74, parser/budget/GR prep, VFS5/5, scope guard5/5, GR backend10/10 had been independently reproduced by Codex **previously on byte-identical 24 source files**; G5 confirmed matching bytes, did not rerun all suites in this worktree.
- **G4/J1:** Codex verified author Jupiter CPU packets visual 9/105/106/107, 20 collected, 5 validated, 4 visible, 1 culled, deterministic hash `EC3EAADD55DFCD6C`; cap24/20 PASS, cap19/8 LimitExceeded code6 and zero success hash; Zaton feature-off DX11/R4 evidence and repeated Jupiter sessions/reset-after/teardown. Codex independently decoded the supplied J1 readback PNG clear RGBA (8,13,23,255), 2560x1600, finding **10623/4096000** nonclear pixels, same as log; J1 Draw/fence3/Debug Layer GBV InfoQueue errors=0 came from **author GPU run**, not independent GPU execution. **Combined J1 + J2A in one session NOT TESTED:** J1 harness explicitly removes `OGSR_DX12_J2A_GEOMETRY` while running; do not claim joint compatibility from separate passes. Include combined opt-in regression in post-push acceptance before declaring coexistence.
- **Remaining accepted narrow-contract notes:** VFS `_register_open_file` open-time OOM ownership; live engine GR Submit/Wait forced failure; reset while Jupiter J2A is pending; camera J1 reset-before-Draw; full Codex MSVC/game independently not rerun; diagnostic one-shot 50–72ms CPU cost is not a steady-state game budget. No confirmed new Critical/P1/P2/P3/P4 defects blocking the **opt-in owner-thread synchronous CPU-only one-shot J2A** integration. Do not extend acceptance to async J2A, GPU residency, J2B GPU or J3.
- **Conditional G6 pre-push gate:** require the project owner's explicit **commit/push** approval and final actual remote HEAD check; preserve doc commits; inspect any intervening engine diffs, STOP if overlap/conflict; construct one clean reviewed 24-file source tree based on current remote, verify staged Git blob/per-file hashes 24/24 and `git diff --cached --check`, production flags and test-only macro isolation, no PhysX/clouds/generated/extra files. Reject force push or unexpected divergence; re-run affected tests/source review if source changed. Only when every gate remains satisfied may DeepSeek make one atomic non-force commit/push and verify remote SHA. If gates fail, STOP and report; Tech Lead must reconsider GO.
- **Post-push acceptance gate:** independently verify published commit/tree, full feature-off/on/camera-on MSVC rebuild, CPU hierarchy/target/transaction/no-throw/parser/budget/VFS/GR/ASan, Jupiter default/20/19 and repeat/reset/teardown, Zaton DX11/R4 Present without J2A, J1 visual9 GPU Draw/fence/readback PNG/InfoQueue GBV and R4 Present. Specifically exercise J1+J2A both opt-in in same session before claiming joint support, and maintain explicit status of untested pre-draw/pending resets. Archive hashes/logs/exit codes. **J2A production ACCEPTANCE requires a separate Tech Lead decision after post-push validation**. J2B GPU/J3 remain HOLD.

### F4.0-J2A R7 — project-owner G6 controlled commit/push approval (2026-11-10; APPROVED / IMPLEMENTATION PENDING)

- **Explicit permission:** after independent Codex G5 `PASS WITH NOTES` and Tech Lead conditional GO, the project owner approved executing **G6: exactly one atomic production commit and ordinary non-force push** to `prypiat` of the pre-reviewed J2A R7 **24-path cumulative patch** SHA256 `A3FA3104E4E265358A6160C0608389E89414B7927E92322CDFEFFAD34A645AD0` (22 additions, 2 modifications). This permission does **not** authorize unrelated source edits, force pushes, rewriting branch history, PhysX/cloud changes, J2B GPU, J3, or automatic production acceptance.
- **Mandatory pre-push stop gates:** verify actual remote `prypiat` HEAD after this docs-only approval commit, compare against isolated worktree base `72657ca539316a2080de047b8a1f70e76ec8b5e6`; retain all later docs and detect any intervening engine changes; if overlap, unknown diff, or stale remote semantics, STOP instead of auto merge/rebase; stage only exact 24 approved files; verify `git diff --cached --check`, 24 Git postimage identities, hashes and production macro gating; ensure no staged roadmap, PhysX or other files; check fast-forward branch and avoid `--force`; collect source identity and commit/tree SHA.
- **Publication and next gate:** if all gates stay PASS, DeepSeek may commit/push once, then independently query remote GitHub HEAD and ensure the published commit and exact 24-file source patch match approval. Immediately run post-push full Windows x64 Release feature-off/on/camera-on builds, CPU/ASan/VFS/GR, Jupiter default/cap limits/repeat/reset/teardown, DX11/R4 Zaton Present and existing J1 offscreen DX12 diagnostic. **J1-only and J2A-only tests are not equivalent to a J1+J2A both-on combined runtime test**; require joint test before claiming coexistence. DeepSeek must report post-push evidence and cannot declare J2A production accepted; Tech Lead records separate production acceptance and updates roadmap.
- **State at approval:** G6 not yet executed by the documentation change. Existing G5 source/evidence verified; **J2B GPU and J3 HOLD**, J2B PREP design-only.

### F4.0-J2A R7 — G6 publication + G7 post-push acceptance (2026-11-10; PRODUCTION ACCEPTED WITH NOTES — CPU-only owner-thread one-shot)

- **Final tech-lead decision:** F4.0-J2A R7 **PRODUCTION ACCEPTED WITH NOTES** **strictly for the opt-in synchronous owner-thread CPU-only one-shot diagnostic preparation**. Existing G5 independent Codex `PASS WITH NOTES` and Tech Lead pre-integration `ACCEPTED WITH NOTES` remain historical predecessors. DeepSeek performed G6 controlled ordinary non-force commit/push and G7 Windows regression. Tech Lead independently inspected the actual GitHub commit, all 24 postimage Git blobs, both delivered archives and original logs/executables/PNGs before recording this decision. This is **not** an acceptance of full native game DX12, J2B four real GPU draws, GPU J2A residency or J3 parallel command recording.
- **Publication independently confirmed:** current GitHub `prypiat` HEAD at audit `b484f8d795cfdbe37ea29e2282c1f4d18035e648`, exact parent `3f5e00d2306d14869988514ad59845ead4971650`, Git tree `d3bcf1bb4331e9bcf69c9d7d9a35c2f0c31364ef`. One integration commit changed **24 paths exactly (22 added, 2 modified)**, **+5451/-4**, no roadmap/PhysX/cloud files. All **24/24 published Git blob SHAs** fetched from GitHub and independently matched the R7 package source-identity table. Canonical cumulative patch SHA256 `A3FA3104E4E265358A6160C0608389E89414B7927E92322CDFEFFAD34A645AD0`, staged/commit diff SHA256 `1DA90AA1727EB093D004C5D2BEB7E60D8A992A70FAE917EBE57A3DD0B73C7ECF` (UTF-16LE export). Ordinary push exit 0 and no force per author log, GitHub remote HEAD confirmed independently. **This documentation update is a subsequent docs-only commit and does not change the accepted production source SHA.**
- **Evidence package integrity independently confirmed:** `F4.0-J2A-R7-G6G7-PostPush-Delivery.zip`, 689,612 B SHA256 `A35D9B93C1DD5806AC941071F3707F29091111213F877C11D91D4832F06B02EF`; `F4.0-J2A-R7-G6G7-PostPush-RawEvidence.zip`, 69,482,347 B SHA256 `C9AA16287C565E64F6A1067698C64E7AD64355358B5D51AB2A5E8E5EDFDFC11D`. Both ZIP CRC PASS. Combined `MANIFEST.sha256` SHA256 `F60193A7FFE842F2103078FA127D44D8EB0B29FDF75806050E9FE4CD150692C2`; **130/130 file SHA256 matches, 0 missing, 0 mismatched**.
- **Three author post-push full MSVC Release x64 Rebuilds (logs inspected, NOT independently rerun):** feature-off binary SHA256 `E426AFDF6E80036A467ECDFCB0839325AF6143AB3A0C648AEBE6CB83EF134C8B` (31,456,768 B; real PE import directory contains `d3d11.dll`/`dxgi.dll`, not `d3d12.dll`); feature-on `B9383BE6798CCC9B1BD461F0A5FA3A17E21D9E0B03921C40DC64E6647354F5F1` (31,719,424 B; `d3d12.dll` imported); camera-on `9773764B89185BCBA063C3E47BD34F167CE4562E05719FA6863F7FEDBEEC20AC` (31,734,784 B; `d3d12.dll` imported). Tech Lead independently recomputed all binary SHA and parsed PE import directories; logs/exit files report 0 errors, `MSB8027=0`, `LNK4042=0`, exits 0, MSVC 19.44 / Windows SDK 10.0.26100.0.
- **Author G7 CPU/regression evidence checked:** hierarchy **74/74** and MSVC ASan hierarchy **74/74**, target **9/9**, transaction **6/6**, persistent no-throw **12/12**, parser/capacity PASS, GR preparation PASS, genuine GR backend probe **10/10** with 3 worker callback threads, VFS failure **5/5**, scope guard **5/5**, all author test exit codes 0. Independent Codex earlier reran these CPU probes on byte-identical R7 sources; Tech Lead did **not** independently rebuild or execute Windows tests in this post-push environment.
- **Author G7 in-game evidence checked from original logs:** Jupiter default sector129/root17431, 20 collected, 5 validated, 4 visible CPU packets visual **9/105/106/107**, 1 culled, packet hash `EC3EAADD55DFCD6C`; default/24/20 PASS, 19/8 `LimitExceeded` code6 with success metadata/hash zero, exactly one terminal FAIL and one detail; repeated Jupiter session two PASS with matching hash, reset-after, Jupiter→Zaton teardown/shutdown; feature-off Zaton scene/HUD R4 Present and `ph_dx12=0`. CPU wall time on default **58.601ms** (VFS **45.656ms**); across author runs ~49–66ms is an allowed one-shot diagnostic stall, NOT a production per-frame budget.
- **J1 and J2A coexistence specifically verified from supplied game evidence:** camera-on with both opt-in flags enabled in the **same Jupiter session2/epoch3**, J2A exactly one begin/terminal PASS and hash `EC3EAADD55DFCD6C`, four visible CPU packets, J1 precisely one `DrawIndexedInstanced(1314,1)` visual9, fence3, `removed=0x00000000`, `gpu_completed=1`, image gate PASS, Debug Layer/GBV InfoQueue `error=0/corruption=0/lifetime=0`, R4 Present, reset-after no second Draw, teardown/shutdown. Tech Lead decoded the separate and combined 2560x1600 readback PNGs: both **10,623/4,096,000 nonclear pixels**, byte-identical PNG SHA256 `0CB64B6E3D69A29591D5A3C49BE11AE8F6C0BEC5B075CC43B85ADA8720580398`; this independently confirms image content **not** an independently executed GPU draw. No full native DX12 gameplay Present claimed.
- **Open nonblocking notes / scope restriction:** engine-wide VFS `_register_open_file` registration-time OOM ownership **UNVERIFIED**; forced live-engine GR Submit/Wait failure **UNVERIFIED** (public backend probe available); reset while pending Jupiter J2A **NOT TESTED** (current one-shot synchronous path), J1 camera reset-before-draw harness **NOT TESTED**, one-shot synchronous CPU preparation ~49–66ms may stall that frame, historic aggregate `E295...` mismatch is a metadata ambiguity superseded by 24/24 independent blob verification. These do not invalidate the documented opt-in one-shot contract; they must not be misrepresented as universally solved or extended to asynchronous/steady-state execution.
- **Next milestone:** do **J2B-M0 CPU/source/asset compatibility audit first**, validating the exact Jupiter visuals **9, 105, 106, 107** via existing `ReadVisual` + `BuildXrayLevelAsset12` semantics (vertex declarations/stride/index/base offsets, unsupported formats, bounds) and plan genuine GR registry handles / session+epoch+device-generation, D3D12 owner-thread resource uploads/fence-driven lifetime and preloading outside `OnFrame`. Define a cold/warm/p95 CPU stall budget and per-visual evidence plan. **J2B PREP design only; J2B GPU implementation and J3 remain HOLD** until independent M0 results and an explicit new technical/project-owner authorization. Preserve DX11/R4 and J1 fallback; four J2A CPU packets are **not** four GPU draws.

### F4.0-J2B-M0 — launch of CPU/source compatibility and ownership audit (2026-11-10; AUDIT AUTHORIZED, GPU IMPLEMENTATION HOLD)

- **Gate authorization:** after J2A R7 source was published (`b484f8d795cfdbe37ea29e2282c1f4d18035e648`) and narrowly production accepted with notes, project owner requested continuation; Tech Lead opens **M0 audit/isolated CPU verification ONLY**. DeepSeek may inspect source, read actual Jupiter `level`/`level.geom` through a controlled isolated test, build CPU-only harnesses and prepare a reviewable report/evidence pack. **Do not modify/stage/commit/push production; do not implement J2B native GPU Draw/upload, new registry or J3 parallel command recording.** ChatGPT remains roadmap editor.
- **Code-based compatibility basis (verified live `prypiat`):** `ogsr_engine/Layers/xrRenderDX12/dx12xraylevelgeometry.h/.cpp` `xraylevel12::ReadVisual` exposes one strict MT_NORMAL shared-container visual with copied vertex bytes, uint16 base-relative indices, original VB/IB offsets and computed bounds/sphere. `dx12xraylevelasset.cpp` `BuildXrayLevelAsset12` **requires exactly `vertex_stride == 32`**, finite position float3 at offset0, index count nonzero/multiple of3 and all indices within the compact vertex range; it checks bounds/sphere and replaces normals/UV with flat-fallback placeholders. Existing `GpuMeshRegistry12` handles 3 synthetic catalog meshes plus **one** X-Ray asset slot; **not** a ready four-mesh game registry. Previous J2A 4 CPU packets are **not** proof that all four source visuals can be converted to real GPU assets or jointly visible in one camera sample.
- **M0 strict acceptance:** for **each** IDs 9,105,106,107 measure exact input file hashes, `ReadVisual` return/error, OGF type/declaration/stride, count/offset/byte ranges, index topology, `BuildXrayLevelAsset12` return/reason, output vertex/index count, bounds/sphere and positions; prove no out-of-bounds/overflow, deterministic replay, fail-closed result for unsupported formats, no silent visual dropping or placeholders interpreted as decoded actual material. Negative fixtures and reasonable memory/capacity budgets required. Where compatibility fails, report original reason + bounded minimal design proposal, not direct production changes.
- **Ownership/design deliverable:** identify immutable VFS input snapshot owner, source vs instance identity, reuse of existing **single GR pool**, bounded job handoff/lifetime and transaction rules; propose typed geometry handle and `session/epoch/deviceGeneration` invalidation, real VB/IB upload owner-thread and exact GPU fence/allocator/quarantine contract for *future* M1–M4, without implementing it now. Explain world transform/camera joint-visibility proof prerequisites and p50/p95/p99 latency/cold-warm budget; guard R4 Present/J1. GPU Draw, upload timings and actual four-visual image evidence remain **NOT TESTED**.
- **Next decision:** deliver `F4.0-J2B-M0-Compatibility-Report.md` with per-visual PASS/FAIL/UNVERIFIED table, source anchors, CPU logs, toolchain/version, artifacts SHA256 manifest, design/risks and exact M1/M2 gating proposal. Independent Codex READ-ONLY review and separate Tech Lead/owner authorization before any production J2B GPU code or staged integration. **J2B GPU implementation and J3 HOLD.**

### F4.0-J2B-M0 — real Jupiter four-visual compatibility evidence (2026-11-11; AUTHOR PASS WITH NOTES / TARGETED TECH LEAD PASS WITH NOTES / FULL CODEX PENDING)

- **Scope/disposition:** DeepSeek delivered isolated CPU audit at `2cc0536f08707724043a518e097c4b10a73f6b2f`; **no production changes, GPU resources, Draw or push**. Author verdict `PASS WITH NOTES`; Tech Lead independently checked original ZIP SHA-256, ZIP CRC, its 53/53 SHA manifest, real-visual source harness, four per-visual logs, determinism hashes, negative fixtures and design reports, returning **targeted source/evidence pre-review PASS WITH NOTES**. Actual `level`/`level.geom` bytes are intentionally excluded (game assets), so Tech Lead has **not** independently reproduced the true Jupiter parser/conversion from source payload. **Independent Codex READ-ONLY full M0 review is NEXT and PENDING**. Do not mark GPU readiness or M1/M2 implementation accepted.
- **Artifact provenance:** `F4.0-J2B-M0-Compatibility-Delivery.zip` **1,064,498 bytes**, SHA256 `73F0EE9EB4829D530B0F37000E580316666F768D68BD96F774FE6661D496082A`; `MANIFEST.sha256` SHA256 `65ACD02EB3F3F8771510AD345E85285F83CDFE39882E7839CBBE81F13F264B40`, **53/53 PASS**, no missing/mismatch, ZIP CRC PASS. Real VFS input per author: `level` **6,873,629 B** SHA256 `D91998F5B906F2C92F443033F4E9D7F096E307696F4C1FACB51FDEDA4A54EE9E`; `level.geom` **93,930,032 B** SHA256 `BBF30871726CEF8311239CFC0A3CD155667F9E527F8701ACDE1C0A8FDB6178A8`; input SHA identity **author measured**, not recomputed by Tech Lead without game payload.
- **Actual four real visuals (author logs, source-linked test harness):** 9 = vertex **545**, uint16 indices **1314**, source VB9/vBase42158 and IB4/iBase782316; 105 = vertex **99**, indices **312**, VB24/vBase22188 and IB7/iBase81021; 106 = vertex **120**, indices **192**, VB15/vBase28516 and IB5/iBase221727; 107 = vertex **385**, indices **876**, VB15/vBase42683 and IB5/iBase267702. All `ReadVisual` **PASS**, `BuildXrayLevelAsset12` debug-flat **PASS**, `vertex_stride=32`, canonical 7-entry declaration with position float3 at byte0, compact base-relative min/max indices `0..vertexCount-1`, `first_index=base_vertex=0`, and valid recomputed finite bounds/sphere. Source VB/IB IDs shared by 106/107 **are not equivalent SourceKey identity**: ranges and visual IDs differ. Four asset output index SHA match compact input; converted vertex SHA differ because normals/UV placeholders. Determinism replay **10/10 hash lines match** across two author log captures. Actual source asset conversion is CPU-only, not a GPU upload or 4 Draw.
- **Negative/test evidence:** `xraylevel_cpu_main` synthetic **103 PASS / 0 FAIL / 1 SKIP**, with real Jupiter source file paths **114 PASS / 0 FAIL / 0 SKIP**, standalone M0 reader edge **5/5 PASS**, conversion negative **15/15 PASS**, author logs supplied. Real CPU harness builds and calls production `dx12xraylevelgeometry.cpp` and `dx12xraylevelasset.cpp`; Tech Lead read runner/source. Author VFS `FS.r_open` reading **cold49.594ms/warm46.983ms**; four `ReadVisual` calls **~1.21–1.25ms**, four conversions **~0.026–0.032ms**. Source bytes **100,803,661 B (~96.13MiB)**; `~101MiB` peak transient is **estimate**, not an instrumented peak measurement. Proposed p50≤60/p95≤80/p99≤100ms and ≤128MiB memory are **future targets, not achieved percentiles**.
- **Architecture notes:** actual `BuildXrayLevelAsset12` checks fixed stride and ranges but relies on trusted `ReadVisual` declaration validation (future direct caller defense-in-depth). Normal/UV and flat-color shader/texture identifiers are debug placeholders. Current `GpuMeshRegistry12` = **3 synthetic slots plus 1 X-Ray asset slot**, `GpuQuarantine` matching limited emergency slots; four X-Ray GPU meshes require a separately designed bounded four-slot registry and atomic upload/fence-ready publication. Future: distinct immutable `SourceKey` (visual+VB/IB ranges/hashes), per-instance `InstanceKey`, generation-safe typed `GeometryHandle` and `LiveToken(session,epoch,deviceGeneration)`; single existing GR pool, owner VFS/owner GPU with workers consuming immutable snapshots; `Wait` draining of accepted tasks is not GPU fence completion. Per Microsoft D3D12 command allocator contract, owner must not Reset a command allocator while GPU still executes commands recorded with it; proper queue/fence synchronization must precede reuse. Four GPU Draws, GPU residency, PSO/material decode and tiny-visual per-object visibility/occlusion remain **NOT TESTED**. Do not infer four Draws from J2A four visible CPU packets.
- **Next gate:** request independent Codex **STRICT READ-ONLY M0 source/CPU evidence review**, verifying file/hash inventory, real VFS-to-file provenance, runner calls to production sources, each visual/declaration/base offset/compact hash, synthetic negative cases and two-process repeat, no silent skip, design scalability and correct ownership/fence assumptions, explicit unverified source payload. Codex verdict `PASS`/`PASS WITH NOTES`/`REQUEST CHANGES`/`BLOCKED` with per-visual outcomes. After a positive verdict, Tech Lead/owner separately decide whether to authorize **M1 CPU four-asset immutable batch/loading-phase implementation**; M2 four-slot registry remains design-only until its own review; **GPU J2B and J3 HOLD**, no automatic source modifications/staging/commit/push.

### F4.0-J2B PREP — first real Jupiter multi-object GPU Draw architecture (2026-11-09; DESIGN ACCEPTED WITH NOTES, NOT IMPLEMENTED)

**Input:** owner-delivered `J2B-PREP-architecture.md` (architecture/design evidence, not a source patch). First GPU milestone: one controlled in-game camera sample for Jupiter source visuals `9, 105, 106, 107`, **one owner-thread direct D3D12 list**, four separately validated real indexed Draws, owner fence/device confirmation, offscreen R8G8B8A8/D32, readback/PNG, verified per-visual contribution. DX11/R4 continues owning swapchain and Present. **Neither 4 GPU meshes nor 4 GPU Draws nor their joint visibility is currently proven.**

- **Prerequisite gating:** J2A R5 remains candidate with independent full Codex READ-ONLY review pending. Technical pre-review PASS WITH NOTES is not production acceptance. After Codex PASS, separate tech-lead final acceptance and explicit owner authorization required before J2A integration; post-merge Windows verification required before real J2B production work. J3 parallel command recording is not part of J2B PREP.
- **Source alignment:** `GpuMeshRegistry12` + `GeometryCatalog12` own only three synthetic meshes and one fixed X-Ray asset slot. `GameDx12LevelGeometry12` is single-visual/single draw; current J2A R5 CPU path returns summary and discards VB/IB payload; its Resource12 material/PSO IDs are placeholders. `BuildXrayLevelAsset12` supports a strict 32-byte source vertex declaration and substitutes normals/UV; it has not been proved for all of 9/105/106/107. **Required M0 compatibility gate:** against actual immutable Jupiter level/level.geom bytes, independently validate each of four `ReadVisual` geometries, `BuildXrayLevelAsset12` success, source VB/IB format, index ranges, bounds and position layout. Fail closed if even one cannot be represented. Do not infer readiness from J2A packets.
- **CPU/identity contract (M1):** `SourceKey(levelSession, visualId, overlay/source identity)`; distinct `InstanceKey` with sector, root, occurrence; typed `GeometryHandle(slot, slotGeneration)`, checked against captureEpoch/deviceGeneration and live session. One source VB/IB shared across legitimate multiple instances, but independent per-object constants/transforms; no collisions with synthetic catalog handle namespace. Workers get only copies/immutable bytes and return complete bounded CPU package; VFS `FS` and game state stay on owner thread. Use **one owner-managed GR pool**, no second scheduler/pool. `JobGroup::IsComplete` may be polled owner-side; then explicit `Wait` to drain and propagate exceptions before publishing. No blocking job completion wait in `OnFrame`; sync VFS copy during loading still has to be measured.
- **GPU owner contract (M2):** real bounded multi-asset registry, four GPU VB/IB, one batch upload on existing bridge DIRECT queue, explicit states/barriers and monotonically assigned exact upload fence. Prepared handles are not published for Draw until **all four** uploads have proven completion on the expected queue/device/fence and live token; no partial success. Staging/allocator kept until exact completion and never reset while GPU uses it (Microsoft D3D12 allocator/fence rule). Explicit atomic frame lease/reference to protect registry, PSO, constants, targets and readback over submit and completion; unknown completion → preallocated quarantine/fail-stop. `GameDx12BridgeQuarantine.h` and standalone `dx12quarantine.h` are **experimental process-lifetime fail-safe**, not general native renderer recovery policy; plan engine-level solution before scaling.
- **Serial Draw (M3):** debug-only POSITION layout and PSO (no PBR assertions); confirm source position, world transforms and transpose convention against game camera; 256-byte aligned frame/object constants; four indexed draws in one owner-recorded list with one Execute, exact fence, D3D12 Debug Layer/GBV/InfoQueue and `GetDeviceRemovedReason`. Check `GetCompletedValue()==UINT64_MAX` as device removed, not completion. Correct RTV/depth/copy source/readback states; GPU contribution evidence must identify each visual through markers/ID-pass or isolated controls—four recorded draws and total non-clear pixels alone do not prove four visible GPU objects.
- **Lifecycle/performance (M4):** pre-submit errors do not create GPU work; post-submit completion unknown never releases GPU-dependent resources. Reset / level unload / late CPU job / device re-creation invalidates session/epoch/handle generation; confirmed fences govern retire and allocator reuse. Explicitly measure CPU VFS copy, parse, conversion, GR prep, GPU upload/fence, record/GPU time/readback; eliminate repeated VFS+parse from game frame path. Preserve R4 parity with feature-off/on/camera-on builds, DX11 Present and reset/shutdown.
- **Review notes:** control game-camera sample with all four visuals jointly visible is a **test prerequisite to prove**, not known from current Jupiter CPU evidence. Choosing fixed world=identity is permitted only if source coordinates establish pretransformed static geometry. Physical GPU residency beyond `ID3D12Resource` ownership/upload-ready is not implied by arbitrary handle validity; do not implement eviction without a new explicit contract. `dx12packetplan` is only a CPU coverage/range planner and does not itself own GPU object lifetimes. Full `R5` independent review and actual J2B GPU run NOT TESTED. No production J2B changes, no source commit, no J3.

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
Исторический G1B был только служебным single-list bridge; с тех пор **F4.0-J1 R2B source интегрирован** (`656652be7`), а post-merge Windows Jupiter visual9 DX12 Draw/readback **ACCEPTED WITH NOTES** (2026-11-08). Это один диагностический Draw, **не** game GR multi-list recording и не полная геометрия уровня. J2A R5 CPU-prep остаётся **только targeted pre-review PASS WITH NOTES, full Codex verdict PENDING**, production source HOLD. Standalone GR parallel recording имеет самостоятельную ограниченную / conditional acceptance из F3.5; его игровой GPU runtime пока **NOT TESTED**.

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

### F4.0-J3 portability audit — reuse implemented DX12 Smoke multithreading (2026-11-09; READ-ONLY / DESIGN)

**Source-checked in current `prypiat`:** `ogsr_engine/Layers/xrRenderDX12/CHW12.cpp/.h`, `dx12renderprep.cpp/.h`, `dx12packetplan.cpp/.h`, `dx12geometrycatalog.cpp/.h`, `dx12geometryregistry.cpp/.h`, `ogsr_engine/Layers/JobSystem/include/gr/jobs/JobGroup.h`, `JobSystem.h`. Existing standalone `xrDX12Smoke.cpp` has serial/multi/parallel recording modes and fault injection. F3.5 Foundation is implemented and earlier independently **CONDITIONAL PASS** for standalone; synthetic perf does not establish game FPS. This is **reuse of verified architecture**, not need for a new scheduler.

- **Reusable CPU handoff:** `RenderSceneSnapshot12` immutable camera/object copies → GR `PrepareOrdered(..., Mode::Serial/Parallel/Verify)` → deterministic `OrderedFrame12` → `BuildPacketBatchPlan` + `ValidatePacketBatchPlan`, **before the first D3D12 Reset**. The plan owns ranges only and must not outlive the ordered packets it references.
- **Reusable recording pattern in `CHW12::RenderFrame`:** owner first confirms per-frame-slot GPU fence; writes mapped object constants once; creates/retains unique **allocator/list per worker and frame slot**; owner Begin list manages state/transitions/RTV/depth and owner End list handles final state. When `packetPlan.lists.size() <= 1`, owner-only serial recording (no synthetic worker submissions); otherwise `JobGroup::Submit` records nonempty draw list ranges, `Wait` drains *all accepted work* even if later `Submit` throws. Validate every list `Close` and per-worker outcome before owner-only `ExecuteCommandLists` and exact owner fence. Never submit partially recorded lists.
- **Reuse utilities/contracts, not standalone object lifetime:** `CHW12` owns standalone swapchain/backbuffers, `FrameResource`, `GpuQuarantine` and synthetic/X-Ray fixed slots. Gameplay `GameDx12Bridge` owns a **different** device/queue/fence/offscreen target and P3 experimental `BridgeQuarantineStorage`; transplant the algorithm with an adapter and explicit game-session/epoch/geometry generations, not `CHW12` swapchain or its process-lifetime quarantine. First ensure real source 9/105/106/107 `ReadVisual` and `BuildXrayLevelAsset12` compatibility, GPU resident mesh handles/CB slices and **one serial four-draw offscreen frame**, verified per-visual contribution; J3 parallel recording follows that game GPU gate.
- **Hard failure and thread guarantees:** workers never read mutable game/VFS state or create/reset shared owner resources; groups are bound to an authorized GR thread, callbacks' captures live through `Wait/DrainNoThrow`. `JobGroup::Wait` has **no built-in timeout** (risk documented in `CHW12.cpp`), unlike GPU fence waiting; add design/soak/watchdog strategy before runtime parallel default. CPU job completion is not GPU fence completion. Per Microsoft D3D12 docs, allocator `Reset` before GPU completion or concurrently on a shared allocator is invalid; only the owner submits command lists and manages queue fences, resource barriers, quarantine and late GPU failures.
- **Gate order:** J2A R5 Codex PASS → independent J2A source acceptance → explicit authorized source integration → post-merge Windows tests → **J2B-M0 real four-visual conversion test** → J2B-M1/M2 serial four-mesh GPU upload and one-list Draw/image oracle → **J3-M0 reuse `CHW12` CPU plan / per-worker recorder with 2+ nonempty lists** → J3 correctness/failure/lifetime & serial equivalence → only then adaptive parallel defaults / scaling measurements. No J2A source or J3 code change is authorized by this documentation entry.

References: Microsoft [Recording command lists and bundles](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles), [ID3D12CommandAllocator::Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandallocator-reset), [ExecuteCommandLists](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists).

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

**G5.0 fast-track directive / H0 all-human prep (2026-11-08):** по прямому запросу ускорена дорога к **PhysX CPU ragdoll для всех подходящих человеческих NPC после смерти** (живое передвижение/AI на ODE; мутанты отдельным этапом). Код I3 уже содержит `-physx_all_humans`, V2 multi-instance и `-physx_max_ragdolls` (диапазон **2–40**, default 2), eligibility в `CCharacterPhysicsSupport::TryActivatePhysXRagdoll()` и fallback ODE при лимите/отсутствии static coverage/неподдержанной модели. **Наличие кода ≠ принятый массовый gameplay.** Утверждён [M0 Fast Merge + H0 all-human rollout task](p3.3-g5-m0-fast-merge-all-human-rollout.md): M0 закрывает **только** проверку точных исходников/бинарников и integrated J1 R2B GPU A/B без переписывания PhysX; H0 *параллельно, read-only* готовит реальную human-rig coverage, fallback metrics, матрицу 1→2→4→8→16 (32/40 только после performance review), DLC/quest NPC, save/load/loot и rollback. Тестовый H0 код в отдельной ветке, не менять замороженный I3. **M0 NOT RUN, H0 TASK DEFINED/NOT ACCEPTED, G5.0 MERGE HOLD, G5.1 NOT STARTED.**

**G5.0-M0 independent review (2026-11-09): `M0 BLOCKED / integrated J1 R2B GPU NOT RUN`.** Архив `G5.0-M0-Review.zip`: **22 870 294 bytes**, SHA-256 `8CC42F9704CB730D362F76CC61B2D0B838467784C13A8465BA5D5B17523B2232`; ZIP **55 / CRC PASS**, `SHA256SUMS` **54/54 PASS**. Frozen I3 commit/tree **`760f487...`/`872118f...`** unchanged; 91/91 replay from exact `5cf20ada3` and `GamePersistent.cpp 08ec5cd→2bfddb` PASS **по replay logs** (I3 root Git tree independently verified previously). **Впервые reviewer независимо пересчитал SHA-256 из реальных байтов** обоих Release EXE, `ogsr_physx_runtime.dll` и четырёх PhysX DLL: **7/7 MATCH**. PE RSDS обоих EXE GUID/age **MATCH**, однако PDB bytes отсутствуют и PDB SHA report-backed; OS exit 0 дан в summary PowerShell receipt, не независимо replayed процессом. Feature-on/off startup/reload, 70 UI tests, PhysX V1/V2 3 collider draws + 3 `ReleaseAllV2` и 3 `lease_refs=0`, CTest **10 PASS + 1 SKIPPED**, DX12 standalone CPU 857/0 + jobs 130/130 failures=0 по первичным логам; callback Python validator reviewer повторно выполнил, exit 0. **GPU blocker independently verified:** `m0-j1-jupiter-bridge.log` падает на **отсутствии `jupiter.spawn` в `CALifeSpawnRegistry::load()`** до реального DX12 Draw; альтернативная сохранённая игра не найдена/не загружена, reviewed **test-only** `j1_test_look_at` camera/reset harness отсутствует в среде. **GPU A/B = NOT RUN (test-data/harness unavailable, не доказанный regression)**. H0 CSV: **223** stalker sections, **414** profiles, но source `configs.db` в ZIP отсутствует; V2 diagnostic подтверждает лишь две человеческие NPC-секции и **cap=2 → 2 PhysX + 1 ODE fallback** (3 cycles); 4/8/16 NOT RUN. **Next: M0-GPU1 — восстановить имеющиеся легальные CoP Jupiter game/spawn assets и ранее проверенный test-only J1 harness, исполнить GPU readback/reset A/B на неизменном I3 source, R4 Present/GBV/InfoQueue/shutdown. Не переписывать PhysX; G5.0 MERGE HOLD; G5.1 NOT STARTED.** [Полный независимый M0 review](p3.3g5.0m0-independent-review.md).

**G5.0-M0-GPU1 independent review (2026-11-09): `GPU1 PASS WITH NOTES`, cumulative `M0 PASS WITH NOTES`, PhysX SOURCE INTEGRATION READY FOR CONTROLLED MERGE (NOT YET MERGED).** Archive `G5.0-M0-GPU1-Review.zip` **31 174 117 bytes**, SHA-256 `3B064113B942646FC06A91C2CE719DD9FF79EF967A1202E37003515F25CF731E`; ZIP **48/CRC PASS**, SHA256SUMS **47/47 PASS**. Independently recomputed SHA-256 of both GPU test-only EXEs and parsed their PE RSDS GUID/age (off/on same GUID, ages 12/13); actual PDB bytes absent. All **14 GPU logs** inspected: 7 scenarios per PhysX off/on, correct one-shot Draw vs skip, reset-before Draw rebinding epoch 3→4, reset-after consumed attempt no-repeat, zero-coverage and negative PNG expected FAIL, InfoQueue Debug Layer/GBV verdict PASS, shutdown complete in all runs (OS exit 0 report-backed). Independently decoded **12 PNGs**: 2560×1600 readback no-reset **10 623** non-clear pixels (bbox 1129,1099–2108,1599), reset-before **7430** (703,454–1617,1599), zero-coverage **0**. PhysX off/on **byte-identical** readback PNG for all relevant scenarios, matching prior R2B PNG SHA. **Two distinct** R4 Present screenshots (1707×1067) visually inspected, contain actual game scene + HUD. Test-only `j1_test_look_at`/`CJ1Harness` touches only `console_commands.cpp` and `x_ray.cpp`, absent from accepted frozen production commit `760f4872584137d2db822d027420d735f09e4f2f`. A/B means **same frozen integrated source PhysX toggle off/on**, not working preintegration `5cf20ada3` baseline: that separately reproduced old UI `0xC0000005` before GPU. Asset/save hashes report-backed, not included. Real TDR/long soak/clean Debug NOT RUN and not conflated with passed GPU1 gate. **G5.0-M0 functional last blocker CLOSED**; approved **local source-only merge-candidate** subject to review of exactly 91 approved files and no camera harness/binaries/assets. Actual push/merge into `prypiat` requires explicit owner authorization. G5.1 normal human NPC ragdoll stress **NOT STARTED**, default PhysX stays opt-in and ODE remains fallback. [Full independent GPU1 review](p3.3g5.0m0gpu1-independent-review.md).

**G5.0-M1 independent review (2026-11-09): `PASS WITH NOTES / OWNER MERGE DECISION PENDING`.** Package `G5.0-M1-Merge-Candidate.zip` **233 009 bytes**, SHA-256 `2407CCEC72605D763B847B17448CC2CD232ADA133F68F57C9B97059BFA867E14`; ZIP **13/CRC PASS**, `SHA256SUMS` **12/12 PASS**. Full production patch **byte-for-byte identical to independently accepted G5.0-I3**, SHA `BCC95F1C...`; independently parsed **91 paths = 68 added + 23 modified, 26 038 insertions/61 deletions**, whitelist+Git postimage manifest **91/91** agree with patch; no GPU test-only camera/temporary hooks, no file additions outside `ogsr_engine/`. GitHub public first parent `9aa93fc50eeaaaba64f527a83a62106243a8a3ea`, tree `faf9e434cfd1f2827cd1d8a185370b7fb7eb14c3` independently confirmed. Replacing only its `ogsr_engine` subtree with accepted I3 engine subtree `61f6403a...` produces exactly **M1 root tree `1b19e19a1dba8f7e09f9cdc60cffe6a125098388`**. Independently reconstructed canonical Git commit hash from supplied metadata, parents/tree and exact commit message → **`fe0f7347d2dbbf3dc9b75d5f3ca2b1bf12b9535e`**, MATCH. Raw M1 Git object not packaged and should be checked on published review branch; local worktree clean and source-only claims additionally supported by submitted logs. No new Windows/GPU runs on M1 (source equivalent to tested frozen I3; prior M0+GPU1 accepted with notes). **M1 PASSED WITH NOTES: controlled source-only merge accepted for explicit owner decision, not yet published/merged.** Since audit docs commit advances `prypiat`, refresh and bring in docs-only commits before actual PR merge; stop if engine/DX12 differs. PhysX default OFF/ODE remains fallback; G5.1 all-human stress NOT STARTED. [Independent review M1](p3.3g5.0m1-independent-review.md).

**G5.0 production source merge COMPLETED (2026-11-09):** владелец явно разрешил публикацию review branch и controlled source-only merge. В GitHub создан [PR #1](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/pull/1), merged=true, **merge commit `4e97ba9f6b4af5c419498de32cef0b82ba77e002`**, PR source commit `91bd1248368d31f2d696b40ebdc73c963f9e7f2b`. Подтверждено GitHub: **ровно 91 file path, 68 added/23 modified/0 deleted; только `ogsr_engine/`**. После merge дерево `ogsr_engine` **`61f6403ae8997121085c8a602bb11117822ccb7a` EXACT MATCH** независимо принятому frozen I3; `GamePersistent.cpp` blob `2bfddb749542ddc5ea6a32b18e6201cbd4385051`, `console_commands.cpp` `c1811931...`, `xrGame.vcxproj` `9cbe1f8...` и defaults `OgsrPhysXRuntime=false`, `OgsrPhysXRagdoll=false` подтверждены. Никаких GPU1 camera/test-only hooks, binaries, DB/game assets. Современные DX12 J1 R2B, R4 renderer и ODE baseline сохранены. **Source merge DONE; производственные Windows post-merge smoke/build/soak на новом GitHub HEAD всё ещё NOT RUN** (I3/M0/GPU1 ранее приняли источник-побайтово идентичного движка; не выдавать за свежие прогоны). **G5.1-HUMAN task теперь определён** в [postmerge acceptance spec](p3.3g5.1-human-npc-ragdoll-stress.md), но **1→2→4→8→16 реальных человеческих NPC ragdoll gameplay NOT RUN**; all-human flag не включён по умолчанию, движение живых NPC на ODE, мутанты отдельно.

**G5.1 start directive (2026-11-09):** После owner-approved G5.0 source merge подготовлен узкий [S0–S2 postmerge execution gate](p3.3g5.1-s0-s2-postmerge-human-ragdoll-validation.md) с конкретными условиями: fresh normal Release feature-off/on без workaround, legacy ODE и две reload + clean exit, UI 70/70, CTest 10 PASS + 1 SKIPPED при отсутствии fixture, затем настоящий `CAI_Stalker` 1→2 PhysX и 3-й ODE capacity fallback при cap=2, rig/pose/static tiles, save/load/loot, DLL lease=0 и CPU/frame p50/p95/p99 относительно ODE. Код уже автоматически включает V2/multi/free при `-physx_all_humans`, `-physx_ragdoll_multi` конфликтует с V2; compile/runtime flags должны быть сверены. Это **только ТЗ: S0–S2 NOT RUN, G5.1 stress NOT ACCEPTED**, никакого default-on или мутантов.

**G5.1 S0–S2 independent review (2026-11-09):** uploaded `G5.1-S0-S2-Review.zip` verified **114 255 054 bytes**, SHA-256 `A03BF097FCF515053E898C5CFAA62694BC451E85EAB0021ECFE94B57F6C25796`; ZIP **81 entries/CRC PASS**, `SHA256SUMS` **80/80 PASS**. Reviewer independently recalculated actual SHA-256 of four supplied EXEs, `ogsr_physx_runtime.dll`, all four PhysX SDK DLLs and included S2 save `g51_s2_after.scop`; EXE PE RSDS ages **1/2/12/13**, PDB SHA remains report-backed. Git source `48e50bd9915f7db3a2540d14257edddfc11487e9`, engine subtree `61f6403a...` — unchanged; newer `prypiat` docs only. S0: ten cold startups, UI **70/70**, CTest **10 PASS+1 SKIPPED**, V1-only callback 0, V2 **3 register/3 unregister/3 draw**, 9 instances, 3 ReleaseAll/lease0; DX12 CPU **857/0**, jobs **130/130 zero fails**, R4 startup PASS within raw evidence. **S0 normal reload matrix PARTIAL:** единственный S0 reload log использует test-only `bin_g51_ui` с diagnostic workaround=true, а не два отдельных обычных Release feature-on/off reload теста. S1 реальный `CAI_Stalker`: PhysX ownership committed **11 bodies/10 joints**, bone continuity `0.00015m`, 47 callbacks, ODE shell null, loot/inventory functioning, V2 lease zero, ODE comparison; **PASS WITH NOTES** (сырой S1 PhysX журнал имеет 5 игровых событий активации вместо 6 в отчёте, ODE показывает 6). S2: два V2 активных одновременно, третий **ODE capacity fallback at 2/2**, две разные sections, все 3 inventory interactions, скриншоты реальной сцены; at disconnect **two live V2 instances destroyed** before V2 scene lease0. **S2 core PASS WITH NOTES**, но **нет same-process disconnect→new level load**, сохранение загружается в другом процессе без post-load NPC owner/pose evidence (**save load PASS, ragdoll persistence NOT VERIFIED**), visual-model diversity unresolved, coverage-negative NOT RUN. S2 p99 **39.9911ms vs ODE 33.4027ms (+6.5884ms)** на ~30FPS cap и неодинаковых frame windows; per-frame raw samples/CPU physics timing отсутствуют, причинность не доказана. **G5.1 S0–S2 CONDITIONAL PASS; S3/S4/S5 (4/8/16) NOT ACCEPTED**. Required narrow [S2R1 evidence closure](p3.3g5.1-s2r1-lifecycle-perf-closure.md): normal no-workaround reload both modes, same-process active-ragdoll reload, post-save NPC verification and uncapped repeat p99/CPU physics benchmark. [Independent review](p3.3g5.1-s0s2-independent-review.md). No rewriting PhysX; defaults remain OFF.

**G5.1-S2R1 independent review (2026-11-10): BLOCKED on same-process live-2 V2 ragdolls → deferred disconnect → new level load.** `G5.1-S2R1-Review.zip`: **21 310 439 bytes**, SHA-256 `62DFBFDE81867BFA6D6496C3F538CC4DB5BF187F0A922A8D5EDBF720AAE92553`, **48 ZIP entries CRC PASS**, `SHA256SUMS` **47/47 PASS**. Feature-off/on EXE + five runtime/SDK DLL SHA recomputed independently, EXE RSDS **`{22997a6e-916d-413b-bdde-f01d7470f33a}` age 7 off / 6 on**; PDB bytes not provided, final feature-on PDB hash unavailable. R1-A **four separate test-only UI driver runs** (two per build with `OgsrDiagnosticRuntimeWorkaround=false` by report): each 2 load/2 disconnect, OS exit 0 by runner JSON. R1-B saved `g51_s2_after.scop` loads, NPCs 7198/7213/7225 dead ODE owner/shell, 47 bones finite, inventories 11/9/8; **PhysX rigid state is not serialized**, ODE corpse restoration is expected. In the valid live-2 hang run, `live=2 scene=(0,1) lease_refs=3`, then V2 teardown remaining/live 0, lease refs 0, both wrappers released and last `S2R1_TEARDOWN: after hud` at 13:01:34.376; **no level down/start**. This marker is **before optional camera teardown callback and destructor return**, so root call is NOT proven; no thread stacks attached; existing 4.6GB full dump is local only. **NEW evidence F1:** build log compiles `ogsr_s2r1_harness.cpp` and test-only patch references it, but implementation source is **missing from ZIP and patch** (patch UTF-16LE); test driver not reproducible, possible lifecycle cause not auditable. R1-C six raw CSV independently recalculated: 2401 samples per CSV (incl markers); frame p99 median **PhysX 33.4894 vs ODE 33.4718ms**; **PhysX-3 p99 60.8378ms, 217 frames >50ms, PhysX CPU step max 35.1611ms** (also 32.7122/31.6157), so no blanket claim that all hitches are physics-unrelated. Real visual both `sim_default_killer_0/1` = `stalker_merc_2`, 47 bones; diversity still untested. **S2R1 BLOCKED, 4/8/16 S3–S5 HOLD**. Next [G5.1-S2R2 narrow root-cause task](p3.3g5.1-s2r2-live2-disconnect-hang.md): recover missing test CPP, WinDbg text thread stacks from existing dump, bracket final destructor/Console/menu/persistent/EventAPI, 0/1/2-ragdoll A/B and only then minimal fix; never commit harness to production. [Full independent S2R1 review](p3.3g5.1-s2r1-independent-review.md). Source engine unchanged; PhysX remains default OFF.

**G5.1-S2R2 independent review (2026-11-10): `PASS WITH NOTES` — S2R1 hang = TEST-ONLY harness early-return bug, не production PhysX deadlock.** Evidence `G5.1-S2R2-Review.zip` **73 861 417 bytes**, SHA-256 `27C09FB9D00EBA3AADCD24D661423E345A4E85022336E0813ED397E59E7EE037`; 121 entries CRC PASS, `SHA256SUMS` **120/120 PASS**. Независимо реконструированы **старый и новый test-only `ogsr_s2r1_harness.cpp`** из UTF-8 patches: 373/498 lines and exact Git blob SHA. В S2R1 глобальный `if (!g_pGameLevel || !...bReady) return;` расположен **перед** `phaseDisconnectWait`, поэтому после уничтожения уровня никогда не ставился `KERNEL:start`; в S2R2 эта фаза вынесена выше guard. Новый valid mini-dump имеет `MDMP` + module RSDS dump-build age 1, WinDbg text shows `CMainMenu::OnRender`/Theora normal menu rendering, не PhysX wait; **dump-build PDB не приложен**, old 4.6GB dump был невалиден **по текстовому отчёту разработчика**, его bytes reviewer не видел. Final feature-on EXE `8CB1F481...` and included 175MB PDB `4BA72B60...` **independently SHA-256 MATCH + PE/MSF7 PDB GUID `D557CBDA...` age 5 exact match**; feature-off EXE age 2 PASS, feature-off PDB bytes absent. Контроли real-game event reload **0/1/2 live, released V2 scene, ODE off ×2 runs ×2 cycles**, normal menu, callback/destructor/end-of-event markers, new-level ready, exit=0 по runner JSON; additionally final feature-on live-2 ×2 cycles PASS. Два live перед каждым disconnect, V2 teardown `lease_refs=0`; **production fix patch EMPTY (0 bytes)**, все изменения instrumentation/harness test-only, source `ogsr_engine` `61f6403a...` unchanged. Шесть новых raw CSV независимо пересчитаны: frame p99 ODE **33.404–33.464 ms**, PhysX **33.404–33.460 ms**, PhysX wall step mean **0.312–0.327 ms**, CPU mean **0.049–0.060 ms**, CPU p99 **0.087–0.106 ms**, max **0.572 ms**, **zero steps >5ms**; прежние 31–35ms outliers в новых прогонах не повторились, но FPS ~30 pacing и тестовый WPO/LTCG off, definitive uncapped production perf **NOT VERIFIED**. **S2R2 PASS WITH NOTES; S3 4-real-human-ragdoll opt-in TESTING AUTHORIZED, S3 NOT RUN**; S4/S5 8/16 и PhysX default-on HOLD. [S2R2 independent review](p3.3g5.1-s2r2-independent-review.md), [S3 task](p3.3g5.1-s3-four-human-ragdolls.md).

**Historical high-count PhysX rediscovered (2026-11-10):** В P3.3 экспериментальном [all-human smoke 4/8/32/40](p3.3-historical-all-human-32-40-smoke.md) уже сообщалось о **4, 8, 32, 40 одновременно активных PhysX ragdolls**, capacity ODE fallback, повторном использовании слота, 3 сессиях/2 reload и lease_refs=0. Для 32 записано **55 dropped physics steps**, для 40 **57**, FPS 53.9–111.2. Исторический исходный документ `docs/p3.3-experimental-all-human-ragdolls.md` (отдельно отсутствует в текущем prypiat) восстановлен из `G5.0-I1-Review.zip` → `G5.0-I1-Source.patch`; его Git blob `6fc48fbc22653be68ed14803ce007c47515b5410` независимо пересчитан. **Сырые 32/40 логи в доступном I1 review ZIP отсутствуют; предыдущая G4.2-R1 review отдельно исключала эти stress runs из своего объёма.** Это historical reported diagnostic smoke, не независимая production G5.1-приёмка разных visual rigs, static collisions и performance p99. **S3/S4/S5 NOT RUN означает «ещё не принято на нынешней mainline», а не «никогда не тестировали свыше 2 NPC».** Повторный 32/40 smoke без новой диагностической цели не требуется.

**Исторические наблюдения/мониторинг:** (1) legacy v1 registration исправлен в I1, V1-only/V2 валидатор повторно PASS на I3, но mixed transitions отдельно не проверены; (2) исторический no-vis `0xC0000005` в `CLevel::IR_OnMouseMove` не воспроизведён в поздних коротких reload, однако root cause остаётся **UNRESOLVED/MONITOR**. Не приписывать автоматически PhysX.

**P3.3-G5 — Real Gameplay Readiness.** Scope freeze: не добавлять новые физические возможности. Цель — доказать пригодность существующего PhysX ragdoll для обычной игры OGSR DX11/R4 при ODE movement/AI, ODE fallback и отсутствии регрессий.

| Гейт | Требуемые доказательства | Статус |
|---|---|---|
| G5.0 — provenance/build | I3/M0/GPU1 independently accepted source/binary/UI/PhysX/DX12 GPU evidence; **PR #1 MERGED** with 91 source paths and exact accepted engine tree; DX12/R4/ODE kept, feature default OFF; fresh Windows postmerge smoke NOT RUN | **SOURCE MERGED / G5.0 CLOSED AS SOURCE INTEGRATION, postmerge smoke PENDING** |
| G5.0-I2 — baseline UI startup | [Независимый review](p3.3g5.0i2-independent-review.md): UAF root cause доказан; UI fix + 5× cold baseline + feature-on/off startup; reload/exit crash in frame-callback harness требует независимого воспроизведения | **I2-A PASS, I2-B startup PASS (limited), reload/exit BLOCKED; I2 overall BLOCKED** |
| G5.0-I2-R1 — reload/exit isolation | [Независимый review](p3.3g5.0i2r1-independent-review.md): 2 queued-event reload, shutdown, PhysX v1/v2 lease/callback, CTest и DX12 smoke; **новый F1: m_intro Stop→callback self-delete / etsPlayEachItem early return** | **EVIDENCE PASS; SOURCE LIFETIME BLOCKED / MERGE HOLD** |
| G5.0-I2-R2 — intro teardown ownership | [Независимый review](p3.3g5.0i2r2-independent-review.md): F1 forced teardown принят, 48/48 selftests ×2, reload/exit, PhysX V1/V2 и DX12 smoke; обычный Stop idempotence F2 note, 5/6 patch replay | **F1 PASS WITH NOTES; R2 CONDITIONAL PASS, MERGE HOLD** |
| G5.0-I3 — immutable source integration | [Independent review](p3.3g5.0i3-independent-review.md): 91-file source, Git commit/tree exact SHA, F2, builds, 2 reload+exit, PhysX CTest and V1/V2, DX12 CPU/jobs; in-game GPU NOT RUN | **SOURCE/EVIDENCE PASS WITH NOTES** |
| G5.0-M0 — premerge compatibility | [M0 audit](p3.3g5.0m0-independent-review.md) + [GPU1 acceptance](p3.3g5.0m0gpu1-independent-review.md): exact 91-file source replay, independent real EXE/DLL PE SHA, UI/PhysX CTest logs and successfully executed Jupiter J1 in-game GPU readback/reset A/B | **PASS WITH NOTES / READY FOR OWNER-APPROVED MERGE** |
| G5.0-M0-GPU1 — in-game GPU A/B | [Independent GPU1 review](p3.3g5.0m0gpu1-independent-review.md): 14 raw GPU logs, 12 image pixel oracle, off/on byte-identical readbacks, reset-before/after, negative cases, Debug Layer/GBV, R4 Present | **PASS WITH NOTES** |
| G5.0-M1 — controlled PhysX merge candidate | [PR #1](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/pull/1) successfully **merged** after explicit owner consent. Mainline merge commit `4e97ba9f6b4af5c419498de32cef0b82ba77e002`; 91/91 reviewed paths, resulting `ogsr_engine` tree matches accepted I3. No camera harness, SDK binaries or game assets | **MERGED / SOURCE PASS; new Windows smoke pending** |
| H0 — all-human NPC rollout preparation (parallel) | [Independent M0 review](p3.3g5.0m0-independent-review.md): CSV 223 stalker sections/414 profiles, cap=2 diagnostic 2 PhysX + 1 ODE fallback ×3; only two NPC sections runtime-tested. 4/8/16 and model coverage pending | **COVERAGE MAP COMPLETE AS REPORT; WIDE GAMEPLAY NOT ACCEPTED** |
| G5.1 — normal gameplay | [Full G5.1 human plan](p3.3g5.1-human-npc-ragdoll-stress.md): S1/S2 real human 1/2 PhysX ownership and fallback confirmed; S2R2 0/1/2 live reload cleared with test-only fix, new wall/CPU perf baseline; S3 4, S4 8, S5 16 must each be independently reviewed | **S2R2 PASS WITH NOTES; S3 TEST AUTHORIZED/NOT RUN; FULL G5.1 NOT ACCEPTED** |
| G5.1-S0/S1/S2 — postmerge rollout gate | [Independent source/log review](p3.3g5.1-s0s2-independent-review.md): S1 1 real PhysX 11 bodies/10 joints, S2 2 simultaneous PhysX + 3rd ODE at capacity, teardown/lease0; 80/80 manifest, real binaries; S0 normal reload workaround-only, no same-process S2 reload, post-load ragdoll state/perf unresolved | **CONDITIONAL PASS; lifecycle/perf closure before S3 acceptance** |
| G5.1-S2R1 — historical blocked test-only driver | [Historical independent review](p3.3g5.1-s2r1-independent-review.md): reported hang after teardown, missing CPP and 31–35ms CPU step tail; **root cause now identified** as test-only phase/level-null early return, resolved for subsequent tests in S2R2; no production fix | **HISTORICAL BLOCKED, superseded by S2R2** |
| G5.1-S2R2 — live2 lifecycle source/hang root-cause | [Independent S2R2 review](p3.3g5.1-s2r2-independent-review.md): reconstructed old/new test-only harness, matched final EXE/PDB, mini-dump menu render, 0/1/2/release/ODE ×2 runs ×2 cycles; V2 leases=0; 6 CSV new perf recomputed, no recurring spike; production unchanged | **PASS WITH NOTES; S3 TESTING UNBLOCKED** |
| G5.1-S3 — 4 real concurrent human ragdolls | [S3 postmerge acceptance](p3.3g5.1-s3-four-human-ragdolls.md); [historical P3.3 smoke 4/8/32/40](p3.3-historical-all-human-32-40-smoke.md) already reported. Current goal: source-equivalent prypiat, 2 visual rigs, real static collisions, 4-live reload, ODE fallback, raw p99/CPU timing | **POSTMERGE G5.1 NOT RUN; S4/S5 HOLD** |
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

**Ссылка:** [Portable PhysX ADR](physics-portability-adr.md). **Статус P (2026-11-10):** G5.0 owner-approved PhysX source-only PR #1 MERGED; unchanged engine tree `61f6403ae8997121085c8a602bb11117822ccb7a`. S1 one and S2 two real human PhysX ragdolls + third ODE fallback PASS WITH NOTES. **S2R1 apparent live-2 disconnect hang now traced to missing `phaseDisconnectWait` in test-only harness after null-level guard; S2R2 independent PASS WITH NOTES**, 0/1/2 live & release/off 2×2 actual queued level transitions in source-equivalent instrumented Windows logs, lease 0, no production fix. Final feature-on PDB and binary independently matched, raw perf new 3× ODE/3× PhysX CSV recomputed; FPS ~30 pacing, long soak/true uncapped production perf not proved. [S3 four-human test](p3.3g5.1-s3-four-human-ragdolls.md) **AUTHORIZED TO RUN, NOT YET PASSED**; S4/S5 8/16, PhysX default-on, mutants, live locomotion **NOT AUTHORIZED**. Test-only changes remain outside production.

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
