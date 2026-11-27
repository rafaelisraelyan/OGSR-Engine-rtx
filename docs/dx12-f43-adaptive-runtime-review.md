# F4.3 — Adaptive Native Shadow Runtime: independent evidence review

Date: 2026-11-27. **PASS WITH NOTES for opt-in source integration and technical CPU/WARP scope.** No game/hardware acceptance. Next: targeted hardware preflight and owner-authorized gameplay A/B.

## Provenance

- Base 946d7a0c3efa85726233c312ebd24ac3aa520665; infrastructure b1be2b288606389a826610e33b30b72e3c2c1c5e; production integration ed8ee8f6bf0552a99cba08056bd3135a1368cddb; reviewed final HEAD f0ff2894ff80318fe30b9da2af14c1f3728b03c3. GitHub comparison: 15 changed files, three commits.
- adaptive-runtime-validation.zip SHA-256 f8f4c0d57eaf182662b2d13d321a928397f1476156574606038129f580b28ac7, 50 ZIP entries, CRC PASS. Final extracted production-shadow.inc and harness-adapted NativeFrame match reviewed source by normalized length and FNV-1a (42013 / bee4b465; 37210 / b0a3756c). Noncryptographic content comparison, not independent rebuild.
- Engine EXE not in ZIP. Manifest SHA-256 AFF2DD4D50066729CD796C559B76FA42F5C4D56B9B4899F0103F32CA283A71FA not independently recomputed. Windows execution not repeated by auditor.

## Evidence

Paths relative to adaptive-runtime-validation/. engine-final.exit and mixed-final.exit contain 0. Intermediate runs retained.

| Gate | Evidence |
|---|---|
| Release x64 XR_3DA | engine-final.log: 82 warnings / 0 errors. MSBuild 17.14, MSVC 14.44.35207, SDK 10.0.26100.0, SUBST Y: per manifest. Not independent clean rebuild. |
| CPU | mixed-final/adaptive-runtime.log PASS: six compile-time budget examples and physical capacity, denial reconciliation, empty mapping, rejection without output mutation. |
| WARP | mixed-final/warp.log: true AdaptiveShadowRuntime coordinator and DXGI LOCAL observation, synthetic depth PSO, production sampling functions, NativeFrame + GR. Two full frames (44 passes each), shrink (8 passes), zero locals (2 passes), both slots and initial canceled recording. |
| Parity | 130 records per mode (98 center depth values + 32 color probes over four submitted frames); exact returned vector parity multi vs parallel and per-probe assertions. Raw arrays absent; auditor verified logs/source, not independently recomputed numeric values. |
| Budget | 7 full point groups / 7 pages, shrink to 1 group/1 page, then 0; forced slot allowance in fixture (unlimited / 393221 / 0 bytes), not an OS budget change. Real DXGI query used in coordinator. Each submitted frame drained before readback; not simultaneous two-slot runtime pressure test. |
| Debug Layer / GBV | enabled, final log expected clear-value warnings=0; no unexpected diagnostics reported after filter reset. Earlier device-creation filter caveat remains. |
| Native game/hardware | NOT RUN. No actual world material/geometry/water/details runtime validation, GPU performance, resize/reload/shutdown or device-removal injection. |

## Source review and remaining notes

- Actual caller: GameDx12Bridge.cpp passes NativeFrame owner and IDXGIAdapter3 to GameDx12StaticScene::RecordNativeFrame, which invokes AdaptiveShadowRuntime under the opt-in flag. Dynamic passes/prepared caster vectors, existing production PSOs, combined CSM/local RecordShadowPasses, one color handoff and t13/t14/t15 + descriptor table. Legacy remains default. No independent scheduler/device/queue. Frame timing and shadow query heaps are now separate; actual combined demand can grow after Begin. Banked timing result checked. Empty page plan commits on Signal; typed null SRV and fallback GPU address avoid allocating a dummy shadow page.
- Current candidate estimate uses range/distance/projection scale, not exact screen coverage or spot cone bounds. Runtime resolution tiers 128/256/512/1024 with six layers per page are quality/allocation granularity, not a count quota. Budget observes whole-resource sizes, slot credit and incremental peak; driver accounting can lag. Runtime calls QueryShadowMemoryBudget and PlanAdaptiveSlotBudget with reserve 0; production Prepare uses default UINT64_MAX slotAllowance. Existing banked shadow resources and high-water pools also occupy memory. Add explicit configurable byte allowance and conservative reserve, record actual usage/headroom/retained/new costs, not a count cap. Do not treat DXGI budget as memory reservation or declare whole renderer globally budgeted.
- Physical packing policy in GameDx12ShadowPagePlan.h prioritizes retained compatible placements, then point groups before spots and ascending ID. Logical planner prioritizes importance but physical denial due to page overhead/descriptor capacity can override that priority. Example: high-importance spot and lower-importance point both logically admitted, one available page with six slices; the point can fill it before the spot. This is a source-derived counterexample to end-to-end importance ordering, not a reproduced gameplay bug. Evaluate and test priority-aware physical reconciliation, including mixed light types and pressure hysteresis; do not claim highest-importance physical selection guaranteed.
- Denied shadow casters have direct RGB contribution set to zero. This avoids the specific unshadowed-direct-light leakage path, at cost of darker scenes and possible popping. Non-shadow-casting lights retain contribution. DXGI query unavailability falls back banked; other preparation failures cancel frame. Explicit actual-mode/fallback/denial telemetry is needed; launcher run-f43f-jupiter.ps1 has no explicit AdaptiveShadows switch yet. Current frame metrics count views and general usage, not full adaptive budget/physical page trace. Add short rate-limited metrics and launcher opt-in; do not confuse requested mode with executed mode. Do not claim full native visual parity.
- Remaining technical debt: fixed GR temporary source cap (1<<20) with existing serial fallback, retained high-water allocation/query pool, texture limits. Preserve separately tracked items; no arbitrary new scene quota. No full baseline repeat. Device removal remains distinct.

## NEXT — hardware / gameplay validation

Before first game launch: minimal preflight change, explicit launcher adaptive switch (default off), bounded-by-bytes configurable quality allowance/reserve for safe first run, selected vs requested mode and fallback reason; per-slot retained/staged/peak bytes, DXGI usage/budget, requested/admitted/denied point/spot groups, actual page tier and projection counts, timing IDs/GR stats and HRESULT. Small CPU pressure test with competing spot/point; if a real defect is identified, fix in isolated commit and rerun affected tests. No broad refactor.

Game launch only after owner agreement. Use same isolated Jupiter save, camera/route/weather/settings, banked baseline and adaptive opt-in. One short Debug/GBV adaptive correctness run and one short non-validation A/B pair, capture screenshots of dense indoor light/occlusion, outdoor CSM, alpha-test/rigid/details/water as present. Capture actual mode and nonzero pages, per-frame CPU/GPU/fence timings, budget, diagnostics, normal exit, no silent fallback. Compare only equivalent conditions, no FPS extrapolation. Check resize/reload if feasible in one run; no forced device removal without separate agreement. Hardware/game acceptance and default-mode switch separate decisions. Clustered Lighting follows correct adaptive gameplay.
