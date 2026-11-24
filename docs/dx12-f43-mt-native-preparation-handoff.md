# F4.3-MT — native GR preparation skeleton / agent handoff
Base FR2 review PR #22, `35b463272f8a06e0036dbac5c9b4de5e26d3d62f`. Tracker #40, children #33–#39. Isolated from #32 / FR3 raster corrections on purpose. **Source implementation, NOT BUILT or runtime accepted.** Do not merge until independent Windows/owner acceptance.

## Implemented (MT0–2 subset)
- Existing J2A `g_j2aJobs` and `EnsureJobs` reused via synchronous `GameDx12GR_RunRanges`. No extra scheduler, pool, project or third-party dependency. Existing lifecycle and auto up to 4 background workers unchanged. Backend task deltas and actual unique callback participant IDs logged separately from work-item count.
- Full `GameDx12StaticScene12::RecordNativeFrame` calls GR with bounded source-index blocks (256 per block), one independent job item per active sun/local projection. Writes disjoint byte masks for static and captured rigid/skinned visibility and private ordered caster vectors per view/layout, plus dynamic visibility per shadow view. Owner's immutable view of level registry + captured extras is frozen only for this synchronous call. Workers do not touch D3D12, X-Ray objects, CROS, FS, uploads, binding caches or shared counters.
- Preserves original sphere/box predicates, original BVH DFS and layout order, object order, shadow allocation, shaders, draw validation and command recording. Owner consumes prepared indices/masks, keeps all material/pose checks, descriptor writes, barriers, clear and submission in existing sequence. Default serial path is original traversal. Parallel failure discards partial preparation and uses original serial path; Verify failure blocks frame (existing bridge Cancel). Verify compares a serial run of the **same** planner against GR results; it is not independent legacy traversal/image parity. No cached data across frames.
- Launcher `-GR serial|parallel|verify` sets existing OGSR_DX12_GAME_GR. Default serial. `ph_dx12_mt` reports sampled frame work_items, configured workers, actual participants_used, backend submitted/completed/failures/unobserved deltas and phase duration. ParallelFor is one backend task with many work items, not one backend task per object. Existing metrics mode and serial/parallel timings populated for planner; snapshot_ms includes prep, serial_ms is Verify reference only (zero in ordinary serial), record_ms excludes new prep. No claimed speedup.
- Source cap 1,048,576 each static/dynamic, bounded by 9 active views; temporary vectors may still allocate substantially. Verify doubles plan memory; optimize scratch reuse only after correctness. No worker reads live CObject or records command lists.

## Checks done here
Exact source anchor replacements against base, checked 2 shadow layout trees/PSOs, existing GR callback + synchronous wait + stats contracts, existing XR_3DA include/build linkage. Portable JS partition/order model: 47805 checks PASS (empty, grain boundaries, shuffled work order, full Jupiter-sized static workload, 0–9 views). This model does not execute engine code, BVH traversal, actual GR or GPU. MSVC/FXC/PowerShell runtime, Debug Layer/GBV, gameplay NOT RUN. All issues open.

## Agent next: build and establish correctness before extending
1. Read AGENTS.md, #40 and #33–#39; check diff from pinned FR2. Build XR_3DA Release x64 with DX12 experimental bridge. Run existing GR/enkiTS tests, shader compilation, PowerShell AST, diff check. Fix compiler errors first, no speculative lighting changes.
2. Run launcher from this branch (owner manual gameplay), same save/weather/route/resolution:
```powershell
$run = 'C:\path\to\worktree\tools\dx12\run-f43f-jupiter.ps1'
$root = 'C:\OGSR_ENGINE\OGSR_J1_Runtime_R2'
$exe = 'C:\path\to\worktree\bin_x64\xrEngine.exe'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$run" -GameRoot "$root" -Exe "$exe" -GR serial -SceneLog
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$run" -GameRoot "$root" -Exe "$exe" -GR parallel -SceneLog
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$run" -GameRoot "$root" -Exe "$exe" -GR verify -SceneLog
```
Verify not perf. Parallel logs must have submitted=completed, failures/unobserved=0, actual participant counts; one backend task per synchronous ParallelFor is expected. Check identical legacy vs prepared source/mesh/pose IDs, per-pass ordered caster indices, draw counts and matched images. Current Verify compares same algorithm; add independent legacy oracle and engine tests for invalid inputs, exception/rejected submit, frame cancel, reload/shutdown. Run `-Validation` separately. Owner does manual gameplay; do not claim PASS from compilation.

## Remaining transfers, in dependency order
| Issue | Work still required | Existing implementation to reuse |
|---|---|---|
| #33 MT0 | Formalize owner/lifetime contract; configurable worker counts/grain, measured scheduling/Wait separate, fallback reason and stats aggregation; no additional pool | GameDx12J2APreparation.cpp EnsureJobs/Shutdown, JobSystem/JobStats |
| #34 MT1–3 | Extract immutable typed snapshot / native packet planner from local lambda; validated session/epoch/generation and mesh/material/pose handles; deterministic packet order, independent legacy Verify, reusable frame scratch; real C++ tests | dx12renderprep.cpp PrepareVisible/PrepareOrdered, dx12packetplan.h/.cpp, M23 GameDx12FourVisual.cpp; keep full-scene layout/BVH semantics |
| #35 | Owner capture of CObject/skeleton/ROS and collision; parallel safe detail transforms/visibility/grouping, pure light ranking, dynamic mesh packet prep; no live game data on workers | GameDx12GameVisuals.cpp, GameDx12LevelDetails.cpp, prior renderprep |
| #36 MT4 | Sequential multi-list parity first; bounded allocator/list pairs per frame slot and recording task, reset after GPU fence, independent per-list state, ordered owner submit, failure drain/quarantine | CHW12.cpp F3.5.3B/5C, GameDx12NativeFrame.cpp |
| #37 MT5/6 | Parallel recording of independent sun/local shadow batches, then opaque/dynamic batches only if profitable; shared resources planned by owner, keep transparent/water ordered | CHW12.cpp recording pattern, docs/dx12-f3-5-3b-parallel-recording.md, docs/dx12-f3-5-5c-parallel-packet.md |
| #38 | Timeline and safe overlap with TTAPI, no speculative device.cpp reorder | device.cpp, existing TTAPI dependencies |
| #39 MT7 | Real Jupiter Serial/Parallel/Verify matrix, 0/1/2/4 background workers vs auto, phase wall/p95/p99, GPU/Present, fault tests, reload/resize | docs/dx12-f3-2c2-runtime.md, existing metrics/native fence validation |

Do not move whole Capture/CalculateBones/RayTest/LoadPending into worker without proof of thread safety. CPU Wait != GPU fence. Do not share a command allocator/list or mutate descriptor heap/mapped ranges concurrently. One direct queue, exact pass order. No CSM, DXR, lighting policy, water, terrain, R4 or gameplay changes in this branch. Rebase/cherry-pick onto accepted raster branch only after coordination with owner. Future CSM and BLAS/TLAS preparation consume this shared planner; GPU acceleration structure build stays GPU work.
