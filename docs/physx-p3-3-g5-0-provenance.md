# PhysX P3.3-G5.0 — Source Provenance and Clean-Build Gate

> **Дата:** 2026-11-06. **Статус:** **BLOCKED — G4.2-R1 source provenance is not verified in `origin/prypiat`**. Read-only investigation; **не** G5 gameplay PASS.
>
> Трек: Portable PhysX Ragdoll, OGSR DX11/R4. ODE default/fallback; DX12 F4.0 независим.

## 1. Факты, проверенные через GitHub API

| Проверка | Результат |
|---|---|
| Ветка по умолчанию | `prypiat` |
| HEAD при проверке | `887740e1dbc364b7e234647da9a636bdf91c4607` (roadmap-only) |
| Каталоги `ogsr_engine/Layers/PhysicsPortable`, `PhysicsPhysX`, `PhysicsAdapterOGSR` в `prypiat` | Отсутствуют в перечислении `Layers` |
| SHA из предыдущего G4.2-R1 review `a02eca549b05e057a9fe6c1542bd0d5d9918a7d8` | `GET commit` вернул 422 «No commit found» в этом репозитории; локальные объекты разработчика не исследованы |
| Резервная ветка | `backup/physx-p31c-20261029` на SHA `2253acd743725d1799b2759a8ed6462a099d694f` |
| Содержимое резервной ветки | PhysX Portable/PhysX/OGSR adapter + `xrGame`-интеграция и `docs/p3.1c-ogsr-multi-npc-runtime.md` |
| Статус резервного исходника | P3.1-C-R3, **не** доказанный P3.3-G4.2-R1 |
| Сравнение веток | Ветки разошлись; `compare prypiat...backup` показывает 6 commits ahead / 48 behind, поэтому wholesale merge рискован |

**Интерпретация:** предыдущее решение G4.2-R1 — **EXPERIMENTAL PASS WITH NOTES по предоставленному evidence** — остаётся исторически зафиксированным; нынешняя проверка не устанавливает его source provenance и не воспроизводит Windows runtime. Отсутствие GitHub SHA не означает, что в исходном локальном workspace нет коммита, архива или патча.

Источники внутри репозитория: [roadmap](08-roadmap.md), [PhysX ADR](physics-portability-adr.md), [резервная ветка P3.1-C](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/tree/backup/physx-p31c-20261029).

## 2. Исходная независимая приёмка — что именно принято

По переданному тексту аудита `P3.3-G4.2-R1`: ZIP `p33g42r1-review.zip` (22 записи, CRC PASS), source diff 5 файлов (+489/−79), Release x64 `XR_3DA` build согласно логам, 10/10 CTest, `git diff --check` exit 0 согласно отчёту. В v2 all-human сценарии зарегистрированы/сняты callback на трёх reload, три успешных collider debug draw (`shapes=21, joints=10`), `ReleaseAllV2 remaining=0 live=0`, DLL lease refs → 0. Код snapshot-by-value и registry/bone locks принят для проверенного пути.

**Не доказано этой сверкой:** наличие тех пяти файлов на текущем remote, новые Windows-сборки, непрерывный нормальный gameplay с 4–8 ragdoll, долгий стресс, legacy callback lifecycle и корневая причина одиночного no-vis `0xC0000005` в `CLevel::IR_OnMouseMove` до ragdoll. Не приписывать crash PhysX без baseline A/B.

## 3. Задание исполнителю G5.0 — только READ-ONLY audit

**Среда:** оригинальный Windows workspace разработчика, где запускался принятый G4.2-R1. Сначала прочитать `AGENTS.md` и соответствующие вложенные инструкции. Не менять `xrGame`, PhysX modules, DX11, DX12, `Engine.sln`, конфигурации и gameplay.

### A. Git provenance

Собрать полный вывод команд (без токенов и приватных ключей):

```powershell
git status --short
git branch -avv
git rev-parse HEAD
git remote -v
git cat-file -t a02eca549b05e057a9fe6c1542bd0d5d9918a7d8
git show --no-patch --format=fuller a02eca549b05e057a9fe6c1542bd0d5d9918a7d8
git ls-remote origin refs/heads/prypiat
```

Не маскировать ошибку `cat-file` и `show`, если объект не найден. Если локальный commit доступен, установить его parents и ancestry относительно current remote. Проверить реальные source blob SHA и пути, а не только commit message.

### B. Найти точную R1 source snapshot

Найти исходный `p33g42r1-review.zip`, сам патч, manifest и raw evidence (если существуют). Проверить ZIP SHA-256 относительно первичного manifest, `git diff --check`, пять изменённых файлов и установленное базовое состояние; отделить R1 от R0/backup P3.1-C. Если артефактов нет, записать `NOT FOUND`, не строить «эквивалент» методом догадок.

### C. Проверить границы интеграции

Составить карту зависимостей `PhysicsPortable` → `PhysicsPhysX` → `PhysicsAdapterOGSR` → `xrGame` / `XR_3DA`. Отдельно audit:

- `Device.seqRender` diagnostic callback: registration/unregistration v2 и legacy, level/visual destruction;
- `g_v2StatesMutex`, `state.boneMutex`, `lastBodyWorld`, lock ordering и value snapshots;
- `ReleaseAllV2`, SceneDestroy, wrapper/game ownership, DLL lease refs, повторная загрузка;
- feature-off ODE path, список defines, связь с текущими DX12 изменениями в `xrGame`;
- source-of-truth для тестов, игровых DLL/EXE и PhysX SDK pin (в историческом P1: `107.3-physx-5.6.1`, `5ca9f472...`; заново сверить с G4.2-R1).

### D. Подготовить минимальный план переноса, **не выполнять**

Предложить отдельную integration branch **от текущего `origin/prypiat`** и whitelist необходимых PhysX module/engine files, включая непринятые промежуточные коммиты при наличии зависимостей. Обозначить конфликты с R4/DX12. Не переносить backup целиком, не выполнять автоматический merge/cherry-pick, не делать commit/push в рамках аудита. Любая production-правка — отдельный проверяемый review.

### E. Clean build & evidence plan

После отдельного согласования source restoration проверять fresh worktree с pinned SDK: Release x64 `XR_3DA` feature-on/feature-off, CTest, `git diff --check`, идентичность DLL/EXE и исходников, затем реальный OGSR DX11 game startup. Без достоверного дерева исходников нельзя объявлять этот gate PASS. Windows build/gameplay из текущего удалённого read-only аудита **NOT RUN**.

## 4. Выходные материалы и вердикт

Обязательные результаты:

1. `G5.0-Provenance-Report.md` — commits, branches, file hashes, provenance, отличия G4.2-R1 и backup.
2. `G5.0-Integration-Plan.md` — минимальный whitelist, зависимости, owner/ABI/lifetime риски, проверка fallback.
3. `G5.0-Evidence-Manifest.sha256` — только реально существующие evidence, без выдуманных hashes.
4. Таблица `PASS / FAIL / NOT RUN` по source, clean build, CTest, game startup и unresolved crash.
5. Независимый финальный verdict: `READY FOR INTEGRATION REVIEW` (доказанный source, безопасный план) либо `BLOCKED — SOURCE PROVENANCE`.

**Дальнейшие этапы:** отдельно принять source integration + clean Windows runtime, затем переходить к `P3.3-G5.1` (4–8 ragdolls, несколько территорий, save/load, perf). G5.1 до этих проверок — **NOT ACCEPTED**.

## 5. Safety / scope

Никаких force push, reset/clean/rebase, массового cherry-pick, удаления локальных файлов, изменения существующего рабочего дерева или включения PhysX всем NPC. Оригинальный ODE и DX11 обязательны; DX12 текущего `prypiat` не трогать. Новую физику не добавлять, только provenance и тестовые критерии.
