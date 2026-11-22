# Issue #2 — Flint read-only audit: early R4 startup access violation

Date: 2026-11-22. Status: **INVESTIGATION ONLY / NOT A CONFIRMED DX12 REGRESSION**.

Canonical issue: https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/2

## Observed evidence (do not overstate)
- An early access violation was reported in F4.0-B-VSLICE R4 **automation**; root cause was not identified.
- Early `-start`, loading-screen waits and emulated input were unstable.
- `final-r4-off-05` had watchdog termination `exit=-1`: **not** proof of a game-engine access violation.
- Later feature-off and feature-on R4 tests traversed Zaton → Jupiter, captured frames and exited 0 (code SHA `4d60ebc5d`). Original local source HEAD was `81956aa1a27d5a5fe4fdd841259afb0fb77f9546`.
- This is **INC-003**, distinct from post-shutdown **INC-001 / Issue #3**.

## Flint — part 1, read-only only

Goal: distinguish an actual R4 startup exception from automation/watchdog/startup-race artifacts, using existing primary evidence. **Do not attempt a broad rendering audit.**

1. Locate and read original `F4.0-B-VSLICE-Implementation-Report.md`, `delivery/independent-review.md`, logs and run manifests for `final-r4-off-05`, `r4-regression-off`, `r4-regression-on` and INC-003. If artifacts are absent, state **NOT AVAILABLE** and stop rather than inventing a call stack.
2. Prepare an evidence matrix: exact source SHA, EXE hash if recorded, GPU/driver, level/save, command line including `-start`, feature flag, automation driver, process exit, actual Windows exception code/dump, last engine log markers; distinguish observed from inferred.
3. Inspect only implicated R4 startup / entry / early loading / automation paths in source and their shutdown interactions *as relevant to INC-003*. Identify concrete null/ownership/race hypotheses with file:line and how to falsify them.
4. Decide whether the currently available evidence establishes (a) reproducible startup AV, (b) automation-only instability, or (c) unresolved insufficient data. Do **not** label it DX12-caused without proof.
5. Propose at most 3 minimal targeted reproductions: ordinary menu, early `-start` with feature-off, early `-start` with feature-on, preserving exact launch inputs; require dump + stack and exit code before any code fix.
6. Deliver a short report to the user: VERIFIED FACTS / UNKNOWN / RANKED HYPOTHESES / NEXT ONE TEST / RECOMMENDATION for #2. STOP.

## Hard boundaries
- No source edits, no HLSL changes, no commits, no push, no build loops, no GPU stress tests, no forced startup automation.
- Do not close #2 solely because later runs exited 0; do not classify watchdog `-1` as an access violation.
- Do not merge this docs branch into `prypiat` or launch unrelated Issues.
- A fix requires a separately approved task with a known repro or crash stack.

## Acceptance
- [ ] Primary source evidence inventory with unavailable artifacts explicitly named
- [ ] Confirm whether dump/exception record exists; watchdog vs AV separately classified
- [ ] Exact relevant file:line observations, no speculative root cause declaration
- [ ] One reproducible, bounded next experiment and STOP
