# F4.3-P0-B3c2 — independent source/evidence review

2026-11-27. **PASS WITH NOTES for scoped B3c2. B3d authorized to begin with targeted closure.** This is not native multi-page gameplay or hardware acceptance.

## Provenance

- Base 5731a5b1f1292aa8c9e4ccd08f6f3e578fd2e00d; implementation 2d7ad0e13dcc3cf359f8e089f32f1acc9c2979bd; reviewed final HEAD ab701796d7917d5842db98a8fcaa8a8dc4a27ec6. GitHub diff: 11 files, two commits. prypiat unchanged by this review.
- Supplied b3c2-validation.zip SHA-256 52c266df44188367a94ca8d23cf93fe095d462a83ce2e5190de26fe4a8008d02; 130 ZIP entries, CRC PASS. Final descriptor fixture extracted declarations and harness-adapted NativeFrame match reviewed source by normalized length/FNV-1a comparison (42013 / bee4b465; 33678 / ad8be6c1); not a cryptographic provenance proof. Auditor did not execute Windows binaries.
- Engine binary not in ZIP. Manifest-reported SHA256 E68395602C62D86EC555CD892944CFB54575B5723649A27D112ABDB45D7E84EF not independently recomputed.

## Evidence (paths relative to b3c2-validation/)

| Gate | Result |
|---|---|
| Release x64 | engine-final.exit 0, engine-final.log 82 warnings / 0 errors, MSVC 14.44.35207 and SDK 10.0.26100.0 per manifest; SUBST X: environment workaround |
| FXC | fxc-final.exit 0; both production PS profiles compile. SM5.1 assembly has unbounded t0 space1, nonuniform annotations, t13 stride 64, t14 stride 80, t15 stride 16; SM5.0 t6 retained. FrameCB stride 768 remains |
| CPU range policy | descriptors-final/shadow-descriptors.log PASS; checked fit/disjoint placement, capacity refusal and uint32 arithmetic |
| WARP pages | descriptors-final.exit 0; real 128x128x6 / 256x256x9 pages, one draw, divergent indices, 4 valid + 4 invalid probes; actual Stage/Present/Signal/Commit and Cancel |
| New mutations | page/slice/descriptor 3/3; exact mismatch masks 1/1/5, real readback; descriptor mutation copies wrong CPU source descriptor, not a synthetic CPU failure |
| Legacy CSM WARP | CsmWarpOnly-r1.exit 0; six configurations, filters 1–8, 6/6 mutations, GR 9/9 and one observed fault each. Auditor recomputed 3348 logged readback records (1116 per mode), zero differences |
| Debug Layer/GBV | PASS in both fixtures; 16 and 5460 expected clear-value warnings respectively, not zero warnings. Device-creation filter limitation remains as in baseline review |
| Game / hardware | NOT RUN; no native adaptive scene pass or production SM5.1 PSO execution verified |

## Source review

ShadowDescriptors owns slot range bookkeeping and t15 uploads; physical page owner retains CPU SRVs and depth resources. Same material heap, actual heap capacity, Tier 2+ and SM5.1 check, two slot ranges from opposite ends. Stage checks slot/fence and copies actual CPU SRVs in page ordinal order; NativeFrame validates predicted exact fence before Execute and commits only after successful queue Signal. Cancel and quarantine preserve existing owner model. No new scheduler or second shader-visible heap. Runtime still binds legacy page; StageShadowPages is exercised by fixture, not integrated adaptive game frame. t15 is a resource-derived shape table, not guessed metadata; logs show WARP GetDimensions returned unexpected dimensions with divergent page selection, corrected fixture passes. This is an observed fixture behavior, not a universal driver defect.

## Notes / targeted closure for B3d

1. Production CreatePipeline selects PS5.1 on capable devices even when scene uses only banked page; the full CSM regression used SM5.0 and began before the final t15/sort changes (manifest explicitly says so). FXC compile plus multi-page probe do not execute the production banked SM5.1 variant. Add a short banked SM5.1 test with production-shaped LocalShadowConfig.w, t15 binding, CSM/local sampling, and null/no-local case, compare with SM5.0. Audit X4000 warnings around ValidLocalAddress, sun receiver functions; no evidence of actual undefined result in tested probes. Production PSO/material validation remains separate.
2. The delayed-fence test queues Wait/Signal without a real draw referencing that staged range; it verifies refusal on same slot, rejected 3-page growth with only 2 entries free, and disjoint successful 2-page staging/cancel. It does NOT prove successful growth from 2 to 3 pages while GPU reads prior descriptors. Add compact actual GPU draw queued behind gate, stage larger other slot with sufficient heap capacity, then release and verify old/new readbacks. No stress campaign. Include descriptor rejection/no-submit and safe rollback.
3. Current range growth is within a preallocated heap, not resizing it. CreateTextures attempts D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2 (1 million descriptors), falls back to original scene heap on allocation failure. This is legal hardware capacity, not a scene-content quota, but not budget-driven sizing. Microsoft documents that one million 32-byte descriptors can consume 32 MB (https://learn.microsoft.com/en-us/windows/win32/direct3d12/shader-visible-descriptor-heaps). Record actual allocation/usage and revisit capacity policy with shared registry/global budget; no premature heap manager rewrite required for B3d. Actual Tier1 hardware fallback and device-removal quarantine not executed.
4. Test runner DescriptorTestsOnly explicitly executes new CPU/WARP cases; default suite does not include them. Preserve both modes and document actual invoked coverage. Keep short targeted regression in subsequent changes. No need to repeat full baseline.

## B3d next

Extract focused dynamic shadow scheduling/recording and timing contracts. Keep four CSM projection IDs reserved, use variable logical projection records and physical page/slice identities, independent reusable command-list/allocator leases per slot after completed fence, deterministic ordered submission, GR immutable input/disjoint output, checked dynamic timestamp query allocation. Use real page-local DSV transitions; no 28-view/32-bit mask constraint for adaptive path. Add compact >32-view CPU/WARP evidence with more than one physical page, distinct per-view depth results and serial/multi/parallel parity; exercise cancellation and fence-gated reuse. B3d does not itself claim full adaptive gameplay: global budgeting, pressure fallback, real game integration and hardware evidence follow. Preserve legacy paths and prypiat.
