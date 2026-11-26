# F4.3-P0-B3d — independent source/evidence review

2026-11-27. **PASS WITH NOTES for scalable recording infrastructure. B3c2 targeted closure PASS. Adaptive Native Runtime may begin.** Not game/hardware acceptance.

## Provenance

- Base 393325d5a6cfc88f010636b00376c55b99c02724; implementation 4f1cb13013f4d89efc95acbb19b830da7cf833d3; final reviewed HEAD ac987a8bcc94c577e35cb275efe1235ce9cb5616. GitHub comparison: 11 changed files, two commits.
- Supplied b3d-validation.zip: SHA-256 dc1a4b17bb96f6684ae47cb9161bbf152fab9b8a2bdd144d799b2274324778e2; 80 entries; CRC PASS. Final banked/scalable production-shadow.inc and harness-adapted NativeFrame match extracted reviewed source by normalized length and FNV-1a (42013 / bee4b465; 34889 / 06bc1ea7). This is a noncryptographic content comparison, not an independent rebuild.
- Engine EXE not included. Manifest SHA-256 D7B0A9D75F7EB337214696972F584D65AC3BF4625AD7E57FDA07C88366FFECC2 not independently recomputed. No Windows binaries executed by auditor.

## Evidence

Paths relative to b3d-validation/. All four final exit files (engine-final, fxc-audit, scalable-final, banked-final) contain 0.

| Gate | Evidence |
|---|---|
| Release x64 | engine-final.log: 82 warnings / 0 errors; affected source rebuild after earlier build, not fresh clean build on a new machine; MSVC 14.44.35207, SDK 10.0.26100.0 per manifest; SUBST Y: |
| FXC | fxc-audit.log, four CSOs: PS5.0/5.1 main, two VS5.0 shadowMain. X4000 warnings persist; author examined affected initialization paths, no established defect in those paths. No claim of warning-free shaders. |
| CPU schedule | scalable-final/shadow-schedule.log: 48 local views, IDs start at 4, eight complete point groups, packet ranges and negative cases for invalid face count, page, overlap and packet range. |
| WARP | scalable-final/warp.log: 12 local views legacy/multi/parallel; 42 local views serial multi/parallel (seven point groups). Source checks exact center depth per view on three successful iterations, actual draw calls, cancellation on first iteration, page/list reuse after drain, timing IDs/count. Logs summarize results, not raw depth/timing arrays, so auditor did not independently recompute these. |
| B3c2 banked closure | banked-final/warp.log: 456 diagnostic records, 228 matched pairs, 0 mismatches, two cascades and 0/8 local maps, modes 1–8. Fixture compares whole returned vectors including depth, auditor independently compared logged probes only. |
| B3c2 in-flight closure | scalable-final/warp.log: two real draws behind gate, first slot two-page range, second slot three-page range with adequate heap, refusal and rollback, 16 readbacks; source confirms actual queued command lists before gate opens. |
| Debug Layer / GBV | enabled; 912 expected clear mismatch warnings banked, 5 scalable. No unexpected diagnostics reported after filter reset; device-creation filter caveat remains. |
| Native gameplay, hardware timing, device-removal injection | NOT RUN. Prior full CSM mutation suite not repeated at this SHA, as planned. |

Initial scalable failure E_INVALIDARG retained in archive; manifest attributes it to zero policyRevision in fixture plan, final fixture corrected. No hidden erasure of initial failure.

## Review

New GameDx12ShadowSchedule and GameDx12ShadowRecording modules preserve separation, deterministic mapping, point groups and immutable packet ranges. NativeFrame allocates per-slot growable allocator/list pairs and timestamp heaps, waits the slot fence before reuse, submits owner-ordered list batches; no independent scheduler. Physical lookup now uses sorted page and placement ranges. Fixture exercises the actual recording module with synthetic depth packet pipeline and GR jobs. Actual engine scene uses banked pass preparation and has not integrated these adaptive components.

## Next gate — Adaptive Native Runtime

1. Combined CSM/local recording is not proven by separate fixtures: scalable test covers local IDs 4..45, banked test covers CSM and banked locals. In current RecordShadowSchedule the list indices start at zero and BeginColorList is called internally; it cannot simply be appended after existing CSM recording. Integrate one owner schedule/handoff, one deterministic list order, one timing ID set for CSM + locals. Validate >32 local views plus 1–4 actual CSM passes.
2. Bridge currently calls NativeFrame.Begin with default timing capacity (28); allocate capacity based on actual combined view demand at a safe completed slot, or split planning and recording phases without extra waits. StaticScene currently ignores SetShadowTimingViews result: handle FAILED explicitly; S_FALSE for unavailable profiling is distinct from failed validation. Resolve only written queries. Avoid global mask or fixed array in adaptive path; retain legacy fallback.
3. Global budget across both slot arenas and other resource demand; use actual GetResourceAllocationInfo, DXGI LOCAL Budget/CurrentUsage and incremental headroom. Account retained, staged, retiring and in-flight allocations without double-subtracting CurrentUsage. No reservation guarantee. Descriptor capacity and retained allocator/query pool high-water memory are real constraints; avoid per-light unconditional max-resolution allocation, no artificial scene quotas.
4. Reconcile logical and physical denials atomically, never publish partial point faces, stale or mismatched t13/t14/t15. Derive actual demand and priority from world light bounds/camera, maintain stable identities, quality tiers/hysteresis; zero-page/no-light and budget shrink need defined paths. Stage only validated prepared addresses; fallback or cancel on capacity/physical errors. Document explicit direct-light behavior for denied shadow-casting lights (unshadowed leakage vs suppression trade-off), do not silently convert to unshadowed. Non-shadow-casting lights unaffected. Preserve CSM and intentional banked fallback.
5. Wire original static/rigid/details caster preparation and existing PSOs into shared recording, using actual page viewport/DSV and per-subresource barriers, bind t13/t14/t15 and page table before color passes, including water/diagnostics as applicable. NativeFrame publishes after actual Signal and quarantines all affected owners on uncertain completion. Test cancellation and partial failures; device-loss path still needs evidence.
6. Keep opt-in adaptive runtime with legacy default until separate game/hardware gate. Compact WARP mixed frame, two-slot budget/denial/zero-page cases, production HLSL sampling + depth/readback parity, affected CPU tests and one Release x64 build. No repeat of full baseline or long soak. Hardware Jupiter/dense lights, visual artifacts, timing, resize/reload/shutdown and DX11 checks remain distinct. Do not start Clustered Lighting before correct adaptive runtime.

No runtime performance gain, checkerboard fix or full F4 acceptance claimed.
