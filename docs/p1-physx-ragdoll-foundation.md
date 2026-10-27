# P1.0–P1.2C — Portable PhysX Ragdoll Foundation & Real OGSR Capture

**Status: READY FOR FULL P1 PASS — real OGSR fixtures obtained; ODE parity,
PhysX real-rig validation, temporary diagnostic-shell cleanup, the legacy
Euler-axis regression and the P1.2C integration builds PASS. The 5/3 discrepancy
is confirmed as two inert `SPHAxis` artifacts (indices 3-4), not independent
degrees of freedom. Known P1 approximations (D6, materials, passive joints)
remain documented below.**
No production runtime integration is claimed.
The opt-in P2.0 PhysX CPU runtime and its DX11/R4 evidence are documented
separately in `docs/p2-physx-runtime-integration.md`.
P2.1 extends the bridge with one real NPC ragdoll; its PARTIAL status and
evidence are in `docs/p2.1-physx-npc-ragdoll.md`.

## Scope

- New portable contract and math bridge.
- New standalone PhysX 5.6.1 CPU world.
- Synthetic humanoid and pendulum fixtures.
- P1.2A debug-only OGSR collector and `ph_dump_ragdoll` command.
- Real Zaton story-NPC capture, ODE parity check and PhysX real-rig readback.
- No changes to `CCharacterPhysicsSupport`, ODE movement, `CPHWorld`, `CPHIsland`,
  DX11/R4 or DX12. The only production-side edits are Debug-only
  `OGSR_DIAGNOSTIC_CAPTURE` hooks/guards and project wiring.

## SDK pin

| Item | Value |
|---|---|
| PhysX | `107.3-physx-5.6.1` |
| Commit | `5ca9f472105a90d70d957c243cb0ef36fe251a9f` |
| Build | Windows x64, CPU-only, Release |
| License | BSD 3-Clause |

PhysX is built outside the repository; no third-party binaries are committed.

## Math correction

The P0.2 formula `T(mul43(A,B)) = T(B) * T(A)` was wrong. The actual
`xrCore/_matrix.h::mul_43(A,B)` means «B followed by A» in row-vector storage.
The tested bridge is:

```text
T(M) = [transpose(M.rotation) | M.translation]
T(mul43(A, B)) = T(A) * T(B)
root_to_entity = inverse(root_body_world) * entity_world
```

Tests use non-identity rotations and translations, a three-bone hierarchy and
model-space round-trips. See `physics_portable_tests`.

## Implemented files

```text
ogsr_engine/Layers/PhysicsPortable/
  include/physics_portable/ragdoll.h
  include/physics_portable/row_matrix.h
  include/physics_portable/json.h
  src/ragdoll.cpp
  src/json.cpp
  tests/test_main.cpp
  CMakeLists.txt

ogsr_engine/Layers/PhysicsPhysX/
  include/physics_physx/physx_ragdoll_world.h
  src/physx_ragdoll_world.cpp
  tests/humanoid_fixture.h
  tests/humanoid_fixture.cpp
  tests/test_main.cpp
  CMakeLists.txt
  README.md
  THIRD_PARTY_NOTICES.md

ogsr_engine/Layers/PhysicsAdapterOGSR/
  include/physics_adapter_ogsr/capture.h
  src/capture.cpp
  tests/test_capture.cpp
  ogsr/ogsr_capture.h
  README.md
  capture-schema.json

ogsr_engine/xrGame/
  ogsr_ragdoll_capture.cpp
  Level.cpp                  (Debug-only OnFrame hook)
  console_commands.cpp       (Debug-only ph_dump_ragdoll)
  MainMenu.cpp               (Debug-only null guards for the test archive)
  xrGame.vcxproj             (Debug-only collector sources/includes)
```

## Test results

Commands (external SDK build in a temp directory):

```cmd
cmake -S ogsr_engine/Layers/PhysicsPortable -B build/physics_portable -G "Visual Studio 17 2022" -A x64
cmake --build build/physics_portable --config Release
ctest --test-dir build/physics_portable -C Release

cmake -S ogsr_engine/Layers/PhysicsPhysX -B build/physics_physx ^
  -G "Visual Studio 17 2022" -A x64 ^
  -DPHYSX_ROOT=<physx-sdk>/physx
cmake --build build/physics_physx --config Release
ctest --test-dir build/physics_physx -C Release

cmake -S ogsr_engine/Layers/PhysicsAdapterOGSR -B build/physics_adapter -G "Visual Studio 17 2022" -A x64
cmake --build build/physics_adapter --config Release
ctest --test-dir build/physics_adapter -C Release
```

| Check | Result |
|---|---|
| Portable core compiles without OGSR/ODE/DX/PhysX | PASS (49 checks) |
| Non-identity row/column bridge and hierarchy | PASS |
| `rootToEntity` convention | PASS |
| Portable JSON writer/loader + schema errors | PASS |
| OGSR rule builder (jtRigid/invisible/jtNone/frames) | PASS (33 checks) |
| Legacy Euler 5/3 axis source invariant | PASS (26 checks) |
| PhysX create/step/destroy | PASS |
| JSON-loaded synthetic humanoid into PhysX | PASS |
| 16-body humanoid, 15 D6 joints, floor contact | PASS |
| Fixed-step accumulator | PASS |
| Initial velocity handoff | PASS |
| External impulse | PASS |
| Invalid contract rejection | PASS |
| Repeated create/destroy, tracked allocations return to 0 | PASS |
| Cone vs pyramid asymmetric Z limit | PASS (`cone -0.6616`, `pyramid -0.1996` rad) |
| Cylinder margin 0/0.005/0.02, penetration | PASS (max 0.0294 m at margin 0) |
| Cylinder axis orientation support height | PASS (horizontal 0.15, vertical 0.30 at margin 0) |
| D6 captured Z-axis hinge | PASS (z-angle 0.2185 rad) |
| Debug OGSR collector compiles/links into `xrGame`/`xrEngine` | PASS |
| Real stalker `IKinematics`/ODE capture | PASS (five dead + two live runs) |
| Real fixture JSON validation | PASS (47 bones, 11 bodies, 10 joints) |
| ODE parity: mass/COM/inertia/axes/limits/spring | PASS (all reported errors `0`; `inert_axes=20`) |
| PhysX real-rig create/step/bone-space readback | PASS (276 checks total with fixture) |
| Target health/death state across capture | PASS (dead: `health=0/alive=0`; live: `health=1/alive=1` before and after) |
| Temporary diagnostic-shell cleanup | PASS (live `sim_default_stalker_26525`: built, captured, destroyed; `shell=0` before/after) |
| Full P1 acceptance | **READY FOR FULL P1 PASS** (no P1.2C blocker; known P1 approximations documented) |

Observed humanoid drop: root `y=0.080`, minimum body `y=0.045` after 1.3 s at
`0.01 s` steps, no NaN and no body below the floor threshold.

Current check counts: portable `49`, adapter `33`, legacy Euler source invariant
`26`, PhysX `198` without the real fixture and `276` with it; all zero failures.

### P1.2C integration isolation

| Configuration | Result |
|---|---|
| Debug x64, `OgsrDiagnosticCapture=false` | build PASS; no collector/command symbols |
| Debug x64, capture=true, workaround=false | build PASS; collector present; no menu/pause workaround string |
| Debug x64, capture=true, workaround=true | build PASS; archive runtime capture PASS |
| Release x64, diagnostics absent | build PASS; no collector/command symbols |

`OgsrDiagnosticRuntimeWorkaround` is independent of `OgsrDiagnosticCapture` and
defaults to `false`. The MainMenu guards are compiled only when the archive
workaround flag is explicitly enabled. `PHJoint.cpp`, ODE movement and DX11/DX12
renderers are unchanged.

Default property evaluation with no flags:

```json
{"OgsrDiagnosticCapture":"false","OgsrDiagnosticRuntimeWorkaround":"false"}
```

The archive runtime smoke test used the explicitly enabled combination:
`OgsrDiagnosticCapture=true` + `OgsrDiagnosticRuntimeWorkaround=true`. It queued
and wrote a real capture (`zat_b42_mayron10604`, 33,404 bytes), parity `ok=1`,
health/alive `0/0` before and after. The standard Debug build with capture
disabled contains no collector or `ph_dump_ragdoll` symbols. A capture-enabled
build without the workaround contains the collector but not the archive
menu/pause workaround string.

### Real capture evidence

Build (Debug x64, VS 2022 BuildTools v143). The collector is **opt-in**:
`OgsrDiagnosticCapture` defaults to `false`; only the explicit value `true`
includes the collector and defines `OGSR_DIAGNOSTIC_CAPTURE`.

```powershell
msbuild Engine.sln /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=v143 /p:OgsrDiagnosticCapture=true /t:xrGame
msbuild Engine.sln /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=v143 /p:OgsrDiagnosticCapture=true /t:XR_3DA
```

Normal Debug and Release builds have no collector symbols. The archive runtime
is a special case: it additionally requires
`/p:OgsrDiagnosticRuntimeWorkaround=true`, which is the only configuration that
compiles the MainMenu null-guards and the menu/pause handoff in
`ogsr_ragdoll_capture.cpp`. A normal diagnostic build has the collector and
command but leaves the standard menu untouched.

Runtime trigger (archive workaround build; collector runs at `CLevel::OnFrame`,
after the previous physics step):

```text
-ph_dump_ragdoll zat_b42_mayron
```

The section name is used instead of the full object name because the engine
appends a per-run object id (`...10591`, `...10648`, ...). A Debug-only `scan`
mode listed loaded stalkers every 30 seconds so the live shell-less target could
be selected explicitly by section; the named capture itself has no
"first NPC" fallback.

Seven successful captures:

| Run | Object | JSON size | Result |
|---|---|---|---|
| 1 | `zat_b42_mayron10591` | 33,405 bytes | parity `ok=1`, written |
| 2 | `zat_b42_mayron10648` | 33,393 bytes | parity `ok=1`, state unchanged |
| 3 | `zat_b42_mayron10609` | 33,391 bytes | parity `ok=1`, state unchanged (final binary) |
| 4 | `sim_default_stalker_26525` | 33,470 bytes | live NPC, temp shell built/destroyed, parity `ok=1` |
| 5 | `zat_b42_mayron10611` | 33,408 bytes | P1.2B final binary, `inert_axes=20` |
| 6 | `sim_default_stalker_26525` | 33,536 bytes | P1.2B live re-run, temp shell built/destroyed |
| 7 | `zat_b42_mayron10604` | 33,404 bytes | P1.2C archive-workaround runtime smoke |

Canonical dead-NPC fixture (outside the repository, run 5):

```text
<external-fixture-dir>\ragdoll_zat_b42_mayron.json
SHA-256 0A5D2A5026D7F79DC9FA459E415F03065AE26EFB03EE4A5266D4E4FB30B81BE6
```

Live temporary-shell fixture (outside the repository, run 6):

```text
<external-fixture-dir>\ragdoll_sim_default_stalker.json
SHA-256 2E8AC440F370D15984B2426E09EBCDF6B66D1BAB041B5D44354213AFF010F02B
```

The fixture is not committed and is not published: it is derived from installed
game assets. Reproduce with `OGSR_RAGDOLL_FIXTURE=<path>`; otherwise the optional
PhysX test prints a skip line.

Parity report from the live ODE shell:

```text
bodies=11 joints=10
mass_err=0 com_err=0 inertia_err=0
axis_count_err=0 axis_dir_err=0 axis_space_mismatch=0
inert_axes=20
limit_err=0 spring_err=0 bone_pose_err=0
```

The raw ODE joint reports 5 axes for each Euler joint; the P1 contract stores
the first 3. All exported axes and limits match. The two extra entries per joint
are the inert `SPHAxis` fallthrough artifacts, not independent DOF; see the
P1.2B section.

State check for run 5 (dead NPC, final binary):

```text
before capture health=0 alive=0 shell=1 active=1
after  capture health=0 alive=0 shell=1 active=1
```

The existing shell was not deactivated or replaced.

Temporary-shell cleanup check (run 6, live NPC `sim_default_stalker_26525`):

```text
before capture health=1 alive=1 shell=0 active=0
temporary diagnostic shell built
ODE parity ok=1
after  capture health=1 alive=1 shell=0 active=0
temporary diagnostic shell destroyed
```

The live NPC had no prebuilt shell. The collector built a temporary diagnostic
shell, captured it without calling `RunSimulation()`, then deactivated and
deleted it. Health, alive state and the absence of a production shell are
unchanged. All eight dead story NPCs in the same session also kept their
existing active shells.

### P1.2B Euler axis verification

The legacy source was inspected without modifying `PHJoint.cpp`; public ODE
behavior and Lua bindings are untouched:

- `CPHJoint::CPHJoint` has no `break` after `case full_control`; it falls
  through to `case slider` and appends two more `SPHAxis` entries.
- The resulting vector has **5 entries** (`GetAxesNumber()` returns
  `axes.size()`).
- `CreateFullControl()` configures `dAMotorEuler` with **3 axes** and calls
  `CalcAxis(0)`, `CalcAxis(1)`, `CalcAxis(2)` only.
- `LimitAxisNum()` clamps public full-control indices to `2`; `SetForce()` and
  `SetVelocity()` update only entries 0-2.
- Entries 3-4 are never motor axes and are not independent degrees of freedom.

Regression guard: `physics_adapter_ogsr_legacy_axes_tests` reads the legacy
source and collector source, checks the five-entry fallthrough, the three-axis
motor, index clamping, absence of `CalcAxis(3/4)`, and the exporter's
`std::min<int>(rawOdeAxes, 3)` rule. Result: **26 checks, 0 failures**.

The live parity report now records the cause explicitly:

```text
inert_axes=20
Euler axis note: legacy CPHJoint full_control constructor falls through to
slider and appends two inert SPHAxis entries (indices 3-4); the Euler motor
uses only 0-2
```

Therefore the 5/3 count is a **legacy bookkeeping artifact**, not five physical
DOF and not a blocker for ragdoll transfer. All three exported axes, their
spaces, limits and spring/damping values match ODE (`axis_count_err=0`,
`axis_dir_err=0`, `axis_space_mismatch=0`, `limit_err=0`, `spring_err=0`).

## Corrections after independent review

- Bone descriptors are now strictly indexed by `BoneId`; the adapter stores
  bones by id after a DFS, so branching skeletons no longer shift IDs.
- Inertia eigenvector determinant correction now flips the whole third column.
- The debug collector uses a right-handed cylinder basis.
- ODE `dMatrix3` inertia is now read with the padded row-major stride:
  `xx=I[0]`, `xy=I[1]`, `xz=I[2]`, `yy=I[5]`, `yz=I[6]`, `zz=I[10]`.
  The earlier `I[4]/I[8]` read produced a non-positive-definite tensor.
- Center of mass is taken directly from `dMass::c`, the authoritative body-local
  COM, instead of mixing the world-space `mass_Center()` with parent-space
  `CPHElement::mXFORM`.
- PhysX D6 frames are oriented from the captured hinge/Euler axis and axis
  space; a Z-axis hinge test verifies the mapping.
- Move assignment clears the previous world before taking ownership.
- JSON parsing checks integer/range values, unit quaternions, nesting depth and
  `rootToEntity` consistency.
- Partial PhysX creation now registers actors for cleanup before shapes/mass
  setup; the world still supports one foundation per process by SDK design.

## Known limitations

- The temporary-shell path is Debug-only. It was exercised on a live stalker in
  run 4: a shell was built for a shell-less NPC and destroyed after capture; it
  is not part of production gameplay.
- ODE's legacy full_control vector reports 5 raw axis records per Euler joint.
  Entries 0-2 are the Euler motor; entries 3-4 are inert fallthrough artifacts.
  The P1 contract exports the 3 active axes, and the regression test confirms
  the cause. This is no longer considered an acceptance blocker.
- The portable builder expects the parent ODE `dMass` to already include merged
  `jtRigid` contributions; it folds the shape but not child mass.
- Contract joint fields `erp`, `cfm`, `friction`, `maxForce`, `targetVelocity`,
  `axes[].spring/damping/zero` are preserved but not yet applied by the PhysX
  world; P1 treats captured joints as passive limits. `collisionGroup` is also
  currently unused.
- D6 pyramid is still not ODE `dAMotorEuler`; it only preserves asymmetric
  swing ranges better than a cone.
- `Hinge2` is deliberately rejected in P1.
- No collision-filter material mapping beyond one diagnostic material.
- No save/load, networking, weapon or death-animation integration.
- One PhysX world per process is the tested configuration; PhysX Foundation is a
  process singleton and concurrent worlds are not supported.
- No DX11/R4 or ODE regression build was run; source diff is empty for those
  directories, which is the current evidence.
- `RagdollShapeDesc` cylinders use local +X in the contract; the OGSR adapter
  still has to rotate the source local +Y axis to +X.
- The test archive runtime has no usable main-menu UI factory. Its null guards
  and menu/pause handoff are compiled only with the explicit
  `OgsrDiagnosticRuntimeWorkaround=true` archive flag, never in normal Debug or
  Release builds.
- `dbghelp.dll` from Debugging Tools was not installed; the conditional
  post-build step warned and `xrEngine.exe` uses the system `dbghelp.dll`. No
  unverified DLL was copied.

## Next tasks

1. Decide whether P1 needs 5-axis Euler parity or whether the 3-axis export is
   the final contract; document the ODE `dAMotorEuler` mapping.
2. Add material/filter mapping and an ODE baseline regression run.
3. Only then consider P2 transition work.
