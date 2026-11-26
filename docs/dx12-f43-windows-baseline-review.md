# F4.3 Windows Baseline — independent evidence review

Date: 2026-11-27. **PASS WITH NOTES — B3c2 unblocked.** Scope: technical Windows build/CPU/WARP baseline, not game/hardware acceptance. Auditor inspected the supplied ZIP and published diffs; no independent Windows execution.

## Provenance

- Input baseline: 50be6c652ecc9fc947e9088b0ae61b76e412682b.
- Engine fix/build: ad181f657b472ee33960fa02ae5f87c697d29ff7 (near -> nearCamera, no policy change).
- Final tests: db33bbee9087833d9b180626c1cc2523be06e353 (small -> smallPage; clear InfoQueue storage/retrieval filters, compare cumulative denied count to device-creation baseline).
- ZIP: windows-baseline-f43.zip, SHA-256 784a138780582e5bb31cd3bfcd62ea13ad1fee88a8335249da72d1826c1c1810; CRC verification PASS, 169 entries including directories.
- Archive contains logs, exit codes, fixture executables/objects, extracted shaders and manifest; engine EXEs not included. Engine hashes below are manifest-reported, not independently recomputed. Copied NativeFrame fixture source is adapted for harness (windows.h and Msg declaration); not a direct production blob identity claim.

## Actual evidence

| Gate | Result | Source in ZIP (relative to windows-baseline-f43/) |
|---|---|---|
| DX12 XR_3DA Release x64 | PASS, exit 0, 83 warnings / 0 errors | dx12-engine-retry.log/.exit; manifest pins engine source ad181f657 |
| DX11 feature-off | PASS, exit 0, 257 warnings / 0 errors | dx11.log/.exit; build before DX12-only nearCamera change |
| FXC | 7/7 entry points, 5 VS + 2 PS | fxc.log + fxc-pixel.log/.exit; initial PS extraction failed, corrected raw-literal concatenation succeeded |
| CPU | 12 distinct suites PASS | runs/f43, runs/f43-sampling, runs/f43-allocator-fixed; final allocator 690659 assertions, sampling reference 84834 checks, PCF 240000 checks |
| WARP physical pages | PASS, exit 0 | f43-physical-fixed.exit; runs/f43-physical-fixed/shadow-page-resources-warp.log |
| WARP CSM | PASS, exit 0 | f43-csm-fixed.exit; runs/f43-csm-fixed/warp.log |
| GPU mutations | 6/6 killed with targeted mismatch masks 35,35,51,2,2,8 | final warp.log and final fixture source; compile/PSO/fence failure not accepted |
| GR parity | independent log comparison: 3348 readback records, 1116 keys each present in legacy/multi/parallel, zero differences | final warp.log; six configurations, GR 9 submit / 9 complete, one observed fault each |
| Filtering | modes 1–8 present in each configuration's readback; not all modes use identical probe sets | final warp.log; terminal banner "PCF 1-7" stale |
| Debug Layer/GBV | fixture PASS, no unexpected diagnostics after filter reset | final fixture sources and logs; 5460 expected clear mismatch warnings in CSM |
| Game/hardware | NOT RUN | manifest |

Configurations (sun, local): (1,8), (2,0), (3,8), (4,8), (4,16), (4,24). Physical allocation sizes on WARP 524288/2097152 bytes, DXGI budget query returned success; not hardware VRAM/residency evidence. FrameCB payload 688, stride 768; projection t14 stride 80 per manifest/fixture and FXC assembly.

## Notes and boundaries

1. Initial failures retained: missing local dependencies, long FidelityFX paths, small/near Windows macros, cumulative InfoQueue counter. Final successful runs are selective retries, not a single clean full-suite execution at final test SHA. Production engine fix changes field name; final commit test-only. Local dependency copies and SUBST X: are environment workarounds. Exact full rebuild reproducibility on fresh machine remains unverified.
2. InfoQueue change does not suppress future errors: filters cleared, subsequent denied delta must be zero, discarded zero, retrieval count equal and messages read successfully. Diagnostic run recorded 64 already-denied messages at device creation, contents unknown; cannot claim complete visibility before filter reset. Expected mismatch ID is allowed only at warning severity; no blanket warning waiver. Production optimized clears remain a separate concern.
3. FXC warnings X4000 for SunFilterVisibility / ProjectShadow require follow-up source review if touching related code; no failure of tested probes established. Build warnings not zero.
4. Manifest engine SHA-256: DX12 8BBA8D47FB04E526B8549FB321AC1E1869E22FC4D0271642B3FFD32353943B56; DX11 181CC5EB55FB0CC7D5323E5A36C27223AAF9211DE6465ABAE173969B43F9BAB7. No claim of independently verified engine binary bytes.
5. Physical resource WARP fixture does not sample multiple pages through production lighting. Legacy page zero restriction remains. No native game execution, hardware timing, checkerboard fix or full F4 acceptance.

## Next

B3c2 focused descriptor management and multi-page shader sampling, under modular ADR. Retain legacy path, actual ResourceBindingTier/capacity, CPU descriptor sources, slot/version fences and rollback/quarantine. Use compact targeted CPU/WARP checks, actual nonuniform page sampling and mutations, one affected Release build. No need to repeat all successful baseline tests before implementation. B3d and full adaptive native runtime remain separate. Game launch only on separate agreement.
