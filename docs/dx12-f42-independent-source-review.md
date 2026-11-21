# OGSR RTX — F4.2 Independent Source Review

> **22 ноября 2026 · read-only focused code inspection · partial, not a production acceptance.**  
> Reviewed published branch: [`feature/f42-game-visuals-details`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/tree/feature/f42-game-visuals-details) at exact HEAD `7d5dc1858922ebbbdee845b0967b44b8d679a42f`. Baseline: `feature/f41-full-static-jupiter` `b9b3dd2279e3b5de4a970bcfb383b64f1099ad6f`.
>
> **Verdict:** **FOCUSED SOURCE REVIEW WITH NOTES; full independent acceptance still PENDING; production merge HOLD.** No confirmed critical defect found in the inspected GPU snapshot/transform/frame-slot/normal-shutdown source paths. This is not proof that no P1/P0 exists elsewhere; it does not include an independent Windows build or execution of the author's test artifacts.

## 1. Scope and provenance

GitHub compare reports **11 F4.2 commits**, +11/−0 relative to published F4.1-E, with 24 changed files. [F4.2 author's technical report](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/docs/dx12-f42-game-owned-visuals-and-details.md) gives Jupiter/Zaton captures, 30-Present sequences, InfoQueue/GBV and a separate R4 feature-off test. **The report and screenshots do not substitute for independently running the binaries.** The local `C:\...\artifacts\F4.2-GAME-VISUALS-DETAILS\runs` evidence tree is not included in Git. In this review we read the actual committed source from GitHub, not that local tree.

Source-inspection focus:
- [`GameDx12GameVisuals.cpp`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12GameVisuals.cpp): per-frame game-owned objects, lifetime IDs, bone poses and material/mesh lookup.
- [`GameDx12RigidModel.cpp`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12RigidModel.cpp): bounded OGF v4 parsing, child meshes, skeleton and texture names.
- [`GameDx12LevelDetails.cpp`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12LevelDetails.cpp): V3 detail parsing, CForm surface queries, deterministic slot cache and per-frame placement.
- [`GameDx12StaticScene.cpp`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12StaticScene.cpp): auxiliary geometry/textures, bone/instance GPU buffers, draw recording, frame-slot ownership and quarantine.
- [`GameDx12Bridge.cpp`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12Bridge.cpp): frame preparation, reset, level teardown, fence/drain.
- [`GameDx12NativeFrame.cpp`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12NativeFrame.cpp): swapchain frame slot synchronization, resource lifetime and injected device removal.
- Shared R4 interfaces: `Kinematics.h`, `SkeletonCustom.cpp/.h`, `SkeletonX.h`, `xr_object.cpp/.h`.

## 2. Source-backed findings

| Priority | Finding | Status |
|---|---|---|
| **P2 / instrumentation** | Main-thread CPU preparation can be substantial: [up to 96 OGF reads/frame](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12GameVisuals.cpp#L96-L132), [up to 256 new detail slots/frame](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12LevelDetails.cpp#L491-L511), each using [CDB collision queries and per-placement ray tests](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12LevelDetails.cpp#L339-L421). In [Bridge](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12Bridge.cpp#L1621-L1646), `snapshotMs` is set to `sceneStats.cull_ms` and **does not time** `gameVisuals.Prepare`. Profiling under movement/teleport is necessary; actual FPS regression **not measured**. | [Issue #16](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/16), open. |
| **P2 / coverage, known** | [OGF parser](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12RigidModel.cpp#L215-L243) deliberately rejects 2/3/4-weight vertices. Distortion and certain unsupported source materials are not rendered. The author reports five rejected game objects; independent raw evidence not reviewed. | Existing F4.2 scope limit, schedule under broader R4 parity [#14](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/14). |
| **P2 / parity, known** | [`level.details`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12LevelDetails.cpp#L356-L421) uses its own deterministic placement RNG, not R4's exact sequence, and samples CForm for surface height. Distribution/density can be compared, but per-instance exact placement equivalence **is not established**. | Keep [#12](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/12) open until targeted comparison. |
| **P2 / GPU device recovery, known** | [Native device-loss path](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12NativeFrame.cpp#L432-L516) can inject `RemoveDevice`; the author's report documents fail-closed/quarantine and exit, **not** reinitialization after a system TDR. | Recovery not claimed; test before production F4.5. |
| **Integration risk** | GitHub comparison of `prypiat` → F4.2 reports **60 commits ahead / 67 behind** at review. R4/PhysX and renderer sources have diverged. Successful isolated feature-off R4 regression does not prove that a future combined mainline tree compiles and behaves correctly. | **Merge HOLD**, separate integration candidate + regression with owner approval. |

### Positive code observations

- [`CObject::Dx12Generation`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/xr_object.cpp#L141-L150) uses a globally incremented object generation, and [`GameVisuals::Capture`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12GameVisuals.cpp#L175-L310) builds a per-frame copy of object world and bone poses, rejecting invalid transforms and excessive bone counts.
- Source child-visibility mapping relies on the [original OGF child index](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/Layers/xrRender/SkeletonCustom.cpp#L9-L24), not the possibly reordered R4 `children` vector.
- Rigid OGF input is bounded by file size, vertex/indices, bone count and child count. Unknown vertex formats/references are rejected with named reasons rather than silently faked.
- [`RecordNativeFrame`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12StaticScene.cpp#L1424-L1623) copies the current pose/instances into a per-slot mapped GPU buffer, and [NativeFrame `Begin`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12NativeFrame.cpp#L254-L309) waits for that slot's fence before reuse. This is the appropriate baseline for avoiding writes into matrices still read by the GPU.
- [`OnDeviceReset`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12Bridge.cpp#L3504-L3552) drains outstanding work, advances the epoch and **invalidates frame data rather than resetting every game model/cache**; the scene's GPU resources remain for a resize. [Level teardown](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12Bridge.cpp#L2837-L2879) drains then resets the game visual snapshot and static scene.
- [`QuarantineGpu`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12StaticScene.cpp#L1684-L1755) intentionally retains GPU resources when completion cannot be established; this is a conservative fail-closed strategy, not proof of device recovery or leak-free repeated loss.

## 3. Checks not independently performed

| Test / gate | Status |
|---|---|
| Actual Windows/MSVC build from this exact SHA | **NOT RUN** |
| Replaying `artifacts/F4.2-GAME-VISUALS-DETAILS/runs` and verifying PNG/exe manifests | **NOT AVAILABLE** (local paths not accessible via GitHub) |
| GPU Debug Layer/GBV and 30+ consecutive Presents on a second machine | **NOT RUN**; author reports success |
| Feature-off R4 Jupiter and Zaton clean exit with actual artifacts | **NOT RUN**; author reports `exit=0` |
| Rigid bone matrix correctness vs the same R4 physical pose | **PARTIAL**: source contract inspected; author has a door/crate test, independent replay pending |
| Fast camera/sprint/teleport CPU p95/p99 and device lifetime | **NOT RUN**, see [#16](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/16) |
| System TDR and device recovery | **NOT RUN** (author explicitly disclosed) |
| Merge conflict resolution with latest `prypiat`/PhysX | **NOT RUN**, requires separate integration worktree |

## 4. Outcome / next action

**Accept the code as a substantive implementation worthy of the next milestone, not as production-merged F4.2.** It is now sufficiently published to support continued work and independent review. Preserve the existing 11-commit history, do not refactor working F4.1 static code without evidence.

To close the independent acceptance: retrieve a bounded F4.2 evidence package with exact run manifests + matching EXE SHA, run focused Windows/R4/DX12 regression at HEAD, validate bone/instance parity and measured prep timing. Address any confirmed source faults with small follow-up commits. **Do not merge to `prypiat` without owner authorization.**

The immediate experimental visual priority remains [F4.2-SKY #15](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/15) — integration of our own sky/cloud system **on the game-owned DX12 frame**, behind opt-in and separate from mainline approval. This review does not certify standalone V-WEATHER cloud readiness.


## 5. Дополнительная прямая сверка с исходным R4 (22 ноября 2026)

Эта проверка **добавляет к первичному focused review** прямое сопоставление с upstream-кодом существующего OGSR R4 **в том же Git SHA**, без запуска Windows/GPU.

| Контракт | R4/X-Ray implementation | F4.2 DX12 implementation | Вывод |
|---|---|---|---|
| Скелетная матрица | [`CSkeletonX::_Render`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/Layers/xrRender/SkeletonX.cpp#L62-L82): для `RM_SINGLE` формирует `W.mul_43(world, LL_GetTransform_R(bone))` | [`GameVisuals::Capture`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12GameVisuals.cpp#L240-L267) использует `pose.mul_43(object->XFORM(), LL_GetTransform_R(bone))` | **Структура композиции совпадает для 1-link bone pose**; имена/индексы и actual GPU image всё ещё требуют адресного parity-прогона |
| Rigid vertex formats | [`CSkeletonX::_Load`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/Layers/xrRender/SkeletonX.cpp#L147-L195) поддерживает single-bone/1-link и 2+/weighted cases | [`ReadVertices`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12RigidModel.cpp#L215-L243) явно отклоняет weighted formats | **Осознанный пробел coverage**, не считать F4.2 полной динамикой |
| Отдельный collider | [`CDetailManager`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/Layers/xrRender/DetailManager.h#L114-L121) хранит member `xrXRC xrc`; [`cache_Decompress`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/Layers/xrRender/DetailManager_Decompress.cpp#L76-L84) вызывает member `xrc.box_query` | [`LevelDetails::Decompress`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12LevelDetails.cpp#L339-L355) использует глобальный `XRC` | **Проверяемый риск shared mutable collider**; не доказан data race; рекомендовано dedicated collider или документированная синхронизация [#16](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/16) |
| Бюджет подготовки деталей | [R4](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/Layers/xrRender/DetailManager.h#L10) держит `dm_max_decompress=7` для обычного incremental cache; [`MT_CALC`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/Layers/xrRender/DetailManager.cpp#L408-L451) вызывается через `TTAPI->submit` | [F4.2](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/xr_3da/GameDx12LevelDetails.cpp#L491-L511) имеет 256 new slots/frame и синхронный `Collect` перед записью кадра | **Сильно отличаются бюджеты и исполнение**: измерить реальный worst-frame, не делать вывод о FPS по коду alone |
| Feature-gated ABI | [`OgsrBuildProps.props`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/OgsrBuildProps.props) глобально объявляет макрос; его импортируют [`Engine_properties.props`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/Engine_properties.props) и [`Engine_lib_properties.props`](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/blob/7d5dc1858922ebbbdee845b0967b44b8d679a42f/ogsr_engine/Engine_lib_properties.props) | `IKinematics::RigidVisibleChildMask` и `CObject::Dx12Generation` gated одним макросом | **Путь единообразной конфигурации имеется**, но проверка фактических аргументов сборки каждого MSBuild-проекта при feature-on/off всё ещё нужна |

### GitHub-native CI для этого HEAD

Проверка `get_commit_combined_status` и `fetch_commit_workflow_runs` на exact SHA **не вернула ни status checks, ни PR-triggered workflow runs**. Это не доказывает, что других запусков не было, но **GitHub в данном запросе не предоставил независимых CI evidence** для приёмки. Результаты Windows/GBV/R4 в авторском отчёте остаются author-reported, пока не воспроизведены.

**Обновлённый статус:** сохранён **FOCUSED SOURCE REVIEW WITH NOTES / INDEPENDENT WINDOWS+GPU ACCEPTANCE PENDING / MERGE HOLD**. Детальная техническая задача — [#16](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/16), предметная parity/coverage — [#11](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/11), [#12](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/12) и [#14](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/14).
