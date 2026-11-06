# P3.3-G5.0 — Independent review of source provenance

**Дата:** 2026-11-06. **Вердикт:** **READY FOR INTEGRATION REVIEW — CONDITIONAL**; **НЕ G5.0 PASS**, **НЕ G5.1**.

**Источник для ревью:** пользовательский `G5.0-Provenance-Review.zip` (SHA-256 `CA0620AA972F0F6B4755967909EACF64E045AA43C020F8C90FB139B3D8C1187D`). Проверялись предоставленные bytes; оригинальный локальный Git object store и Windows-сборка в рамках этого ревью не доступны.

## Независимо повторённые проверки

| Объект | Проверено | Итог |
|---|---|---|
| Внешний ZIP | 10 entries, `testzip()` / CRC | PASS |
| `ARCHIVE-MANIFEST.sha256` | 9/9 присутствующих files, SHA-256 | PASS |
| Вложенный `p33g42r1-review.zip` | 1 636 125 bytes, 22 entries, CRC | PASS |
| `G5.0-Evidence-Manifest.sha256` | 19/19 bytes, доступных в двух ZIP | PASS |
| Внешний game-derived fixture | `ragdoll_sim_default_stalker.json`; SHA-256 `2E8AC440...` указан, но bytes не предоставлены | **NOT VERIFIED / EXTERNAL** |
| R1 patch | 29 450 bytes; SHA-256 `5006BFDD7F106811AE9CFBFA6D6B7910E3EF22614C81B85BBFA19CE17E1037C3`; внутри/снаружи вложенного ZIP побайтово совпадают | PASS |
| Diff contents | `5 files, +489/-79` по непосредственному разбору патча | PASS |
| Новый R1 документ | вычисленный Git blob SHA-1 `2e1b77ed785bee737f63bbd6afb7c5bbd87f4e53` совпадает с provenance report и patch | PASS |
| `logs/reload.log` | 3 registered, 3 unregistered, 3 active collider draws, 9 PhysX ragdoll creations, 3 complete `ReleaseAllV2` и 3 `lease_refs=0` | PASS (как содержимое журнала) |
| `logs/ctest.txt` | UTF-16LE, `100% tests passed out of 10`; `v2_multi_tests` присутствует | PASS (historical evidence) |
| `logs/build-engine.txt` | UTF-16LE, финальная строка `XR_3DA.vcxproj -> ...xrEngine.exe`; есть компиляторные warnings | PASS (historical build log only) |
| До/после reload | два оригинальных JPG непосредственно просмотрены; debug overlays видимы на обоих | PASS (визуальный факт, не proof identity) |

Внутренние журналы и изображения подтверждают согласованность R1 evidence, но не доказывают самостоятельно, что их произвёл именно текущий набор бинарников из remote. **Совпадение `source-p3.3g42r1.patch` с локальным `git diff 7778170d..a02eca5`** подтверждено отчётом `G5.0-Command-Log.txt`, но повторить local `git diff` в этой среде нельзя: G5.0 ZIP не содержит Git-объекты и полный source tree.

## Сверка с GitHub

Текущая основная ветка исторически сверялась на `d136c6a77be7f0a4b63790dcf795085619bc5206`: PhysX modules в `ogsr_engine/Layers` отсутствовали; `a02eca5` GitHub commit API не находил. Публичный backup `backup/physx-p31c-20261029`, commit `2253acd743725d1799b2759a8ed6462a099d694f`, содержит исходники P3.1-C, **не** G4.2-R1. Предоставленный G5.0 командный лог сообщает, что локальная ветка `experiment/physx-all-human-npcs` содержит `a02eca5` с parent `7778170d...` и tree `bd235c15...`. Эти локальные Git-результаты сейчас принимаются как **report-backed provenance**, не независимый remote object verification.

### Уточнения и обязательные условия интеграции

1. **Новая review branch от свежего `origin/prypiat`.** Не делать wholesale merge/cherry-pick ветки 2026-10-29. Не публиковать преждевременно оригинальную experiment branch поверх `prypiat`. При необходимости разрешить push новой integration branch **только после** локального clean diff/audit.
2. **Full source integration review.** Патч R1 покрывает лишь 5 файлов; полный перенос состоит из 108 путей (`26 DOC + 57 MODULE + 21 ENGINE + 2 TEST + 2 BUILD`). После переноса выполнить code/ABI/lifecycle ревью *всех* изменённых production files, включая `xrGame` и DX12-косвенные зависимости. Списки пересечений из G5.0 отчёта требуют независимой проверки на свежем target HEAD.
3. **Fixture gate.** Предпочтителен tracked self-authored synthetic fixture для всегда-выполняемого теста, отдельный реальный fixture-test — optional external. `SKIPPED` не равен `PASS`. При отсутствии real fixture итог должен честно показывать `N passed, 1 skipped` либо раздельный тестовый набор, **не** прежнее `10/10 passed`.
4. **Legacy callback:** минимальный candidate fix — регистрировать `registerRagdollColliderDebug()` только для `multiV2`. Обязательны unit/runtime проверки v1-only, v2-only, mixed activation и teardown, чтобы не удалить callback при ещё живом v2. Наличие исходного дефекта поддержано предоставленным G5.0 анализом; интегрированная версия нуждается в отдельном code review.
5. **No-vis `CLevel::IR_OnMouseMove` crash:** в raw crash log нет подтверждённого создания PhysX ragdoll до падения. Нужны stack/minidump и feature-off/on A/B на согласованных бинарниках/save. Повторение на feature-off не доказывает автоматически root cause; нельзя классифицировать как pre-existing без исследования.
6. **Граница диагностической конфигурации:** `OgsrDiagnosticRuntimeWorkaround=true` из примерной G5.0 команды разрешён только в отдельном diagnostic/archive build. Нужен также **обычный feature-on Release** без workaround и **feature-off Release**; успешный диагностический binary не заменяет production-scope испытание.
7. **DX12 coexistence:** сохранить актуальные `XR_3DA.vcxproj`, `OgsrBuildProps.props`, shaders/renderer, camera/level hooks. Доказать feature-off ODE baseline и текущие DX12 smoke после интеграции. Для `XR_3DA.vcxproj` старый PhysX diff не применять вслепую.
8. **Gate separation:** сначала provenance/merge+clean Windows builds/CTest, потом independent source acceptance и только затем G5.1 (4–8 ragdolls, разные уровни, save/load, perf). Ни архитектурные предположения, ни исторический `10/10` не закрывают G5.0.

## Итог

**G5.0 investigation ACCEPTED WITH CONDITIONS.** Exact evidence package целостен; разработчик предоставил убедительную локальную provenance-цепочку. **G5.0 execution/integration не принят**: PhysX source ещё не опубликован в текущем gameplay tree, fixture не воспроизводим в чистом checkout, свежая merged Windows build отсутствует, а no-vis crash не классифицирован. Следующее разрешённое действие — интеграция исключительно в отдельную review branch с точным source whitelist и последующим независимым аудитом.
