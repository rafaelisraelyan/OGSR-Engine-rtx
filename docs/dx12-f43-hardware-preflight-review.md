# F4.3 — Hardware Preflight: independent evidence review

2026-11-27. **PASS WITH NOTES — prepared for owner-authorized hardware/game A/B.** No game/hardware acceptance.

## Provenance

- Base 4530327f72ccc35a866853b216c6ab22ecffb83d; physical priority 2e591e2b09097a41ba0e3adc5c1d324a3e7fd237; budget/telemetry d022a44ee4d386b0805171d6e2436057ccc439b6; launcher/final reviewed HEAD 3cdad61f5b94b546b29720bf19c15869c3966056. GitHub comparison: 10 files, three commits.
- hardware-preflight-validation.zip SHA-256 5b7fb6bb011bdabcb7490abcbb7ba1f7c5634b47b29fa97a4e7cfa13189f8635, 24 entries incl. directories, CRC PASS. Final extracted production-shadow.inc and harness-adapted NativeFrame match reviewed source by normalized length and FNV-1a (42013 / bee4b465; 37210 / b0a3756c). Noncryptographic content comparison, not independent rebuild.
- Engine EXE absent; manifest SHA-256 FC84250D1106B2D33BF489A2009DA4B494E9C40FF51C9938FE21EE86D78648CB not independently recomputed. Windows binaries not executed by auditor.

## Evidence

Paths relative to hardware-preflight-validation/. engine.exit, targeted.exit and jupiter-preflight.exit contain 0.

| Gate | Evidence |
|---|---|
| Release x64 XR_3DA | engine.log 82 warnings / 0 errors; MSVC 14.44.35207, SDK 10.0.26100.0, SUBST Y: per manifest. |
| CPU | targeted/adaptive-runtime.log: two PASS lines; priority/retention hysteresis, atomic point groups, page/descriptor pressure, cap and two-slot peak arithmetic. Existing broader page-plan suite not rerun in this archive. |
| WARP | targeted/warp.log: two full 44-pass frames, shrink to 8, then 2 CSM only, in multi and parallel; 130 exact readback results per mode by source assertions. Actual values not included; no independent numeric re-comparison. Debug Layer/GBV enabled, expected clear warnings=0; device-creation filter caveat remains. |
| Launcher -Preflight | jupiter-a.log, jupiter-b.log, jupiter-gbv.log: same save SHA256 D1D913E9C8687BC29D1CACA8BB02A8B1A07EDD5AB499491A66A67B7C445FF1A2 and profile SHA256 8A1D678D3EAB3BA2FCB39FB4D46C64B8EA1E84690FD008327F49C8531E4863D0; identical executable path, correct mode/cap/reserve/validation parameters. Files present, contents/load not verified; profile 2560x1600 windowed per manifest. |
| Hardware/game/device loss | NOT RUN. |

## Review and notes

- Physical planner carries hysteresis-adjusted logical priority, sorts descending score and ascending ID only on tie. Compatible placement preserved when it fits and does not conflict. Higher-priority spots can displace lower-priority point groups. This is greedy packing, not an optimal global solution; all admitted point groups remain complete.
- BudgetBytes cap covers adaptive physical pages of both slots including replacement peak, not whole renderer/CSM/banked pages/descriptor heaps/command allocators. Finite cap divides final allowance equally between slots (up to 256 MiB each for proposed 512 MiB), and peak new allocations cannot consume retirement credit. This intentionally conservative policy may defer quality upgrades at saturation and waste capacity with uneven slot demand. Reserve 1 GiB subtracts from observed DXGI headroom; not a reservation. Driver usage can lag. CPU verifies cap arithmetic; WARP uses per-slot allowance for shrink and not explicitly the proposed 512 MiB/1 GiB env settings or OS pressure. First game run should verify actual configured values, peak <= cap and meaningful page counts.
- ph_dx12_shadow_preflight reports first successful scene present and every 120 presents; includes actual mode/fallback, observed usage/budget, adaptive pages/tiers, peak and GR/timing counters. It is a sampled successful-frame trace, not proof that no transient failure occurred. GPU timing relates to gpu_present, not necessarily current present. requested_lights uses capture shadow-candidate count; denied_lights is aggregate requested minus admitted, not specifically memory-denied. Distinguish invalid/disabled/physical denial in diagnosis. Page bytes describe adaptive arenas, not all local shadow memory. Existing full failure logs and normal exit still needed.
- Suppression of denied shadow casters is explicit but may darken scenes; banked A defaults to unsuppressed unmapped light policy, so A/B is a comparison of actual configurations, not a strict identical-lighting parity or isolated performance experiment. If leak/darkness ambiguity arises, optional targeted banked suppression reference only, no extra full campaign.
- User has not authorized game launch in this report. Keep banked default, no game starts from audit. No full baseline repeat. Prior page planner CPU regression is directly affected by ordering changes; run the small existing test-f43-shadow-pages.cpp (and adaptive planner if desired) once before gameplay, not the entire suite.

## Next: Hardware / Gameplay Validation

After explicit owner approval, use docs/dx12-f43-hardware-preflight.md and actual tested engine binary, verify local SHA, save/profile hashes, adapter/driver and all settings. Preserve save/profile/logs, use isolated runtime. Start with short adaptive Debug/GBV correctness run, then separate non-validation A/B pair if no blocker. Same save, camera, route, weather/time, 20-second stationary warmup per protocol. 2560x1600 profile, Balanced CSM 3x2048 at 160m, GR parallel 2 background workers grain 256, parallel lists. Explicit 512 MiB shared page cap and 1 GiB reserve in adaptive. Record actual mode/no unexpected fallback, pages and groups, budget/peak, actual scene render, per-frame CPU/GPU/fence times and matching screenshots (indoor local lights, outdoor CSM, cutout/rigid/details/water as present), full Debug/GBV/InfoQueue evidence and clean normal exit. Missing scene features NOT RUN. One short resize/reload if supported and safe; no forced device loss without separate approval. Avoid repeated launches/stress runs; stop on unexpected GPU errors, device removal, failed load or severe visual issue, preserve evidence. No FPS or production acceptance until comparable hardware results. Clustered Lighting and default mode switch separate gates.
