# OGSR RTX — V-WEATHER-01C-B-R4-R1 (standalone source snapshot)

This is an **experimental, unaccepted** snapshot of the cloud renderer from the user-uploaded evidence package. It is NOT part of the OGSR game runtime and has NOT been merged into production.

- Full original evidence ZIP: `OGSR_V_WEATHER_01C_B_R4_R1_Interactive_Diagnostic_Corrections.zip` (not committed to GitHub because it contains large prebuilt EXEs, GPU logs and screenshots).
- Original ZIP SHA-256: `8c066d84db4195d801582b2b9be6196235ee03a632140cc9541628e8be11635b`.
- Published here: the complete 136-file `source/` snapshot, incremental patch from rebuilt R4, reproduction scripts, reports and text validation manifests.
- Independent Codex verdict: **REQUEST CHANGES (4×P2, 1×P3)**. Remaining findings include blocking diagnostic sinks outside the gate, swallowed `flushDeferred()` after `#endif`, incomplete blocked-pipe interactive tests, parser pipe-status validation and test-only stream ownership/rollback.
- Historical static PNG gate **NOT MET (4/16)**; transition shader with GBV **NOT RUN / unresolved**.
- Next planned corrective checkpoint: R4-R2. This R4-R1 snapshot is for archival, testing and review only.
- User requested publication to GitHub. No executable builds, evidence PNG or gameplay engine source are modified in this snapshot.

Do not merge this directory into a game-owned DX12 renderer without explicit later integration approval and independent acceptance.
