# PhysicsAdapterOGSR — diagnostic ragdoll capture

This directory contains the OGSR-specific adapter. Its collector is linked into
Debug `xrGame` only when `OgsrDiagnosticCapture=true`; production builds do not
include it. It documents the safe way to obtain a real stalker ragdoll fixture
without changing `CharacterPhysicsSupport`, `CPHWorld` or the renderers.

## Current implementation state

- `src/capture.cpp` — compiled, portable OGSR rule builder (jtRigid merge,
  invisible pass-through, jtNone/FreeBody, bind/current pose, joint frames).
  Tested by `tests/test_capture.cpp`.
- `include/physics_adapter_ogsr/capture.h` — neutral input snapshot.
- `ogsr/ogsr_capture.h` — debug-only `IKinematics`/`CPhysicsShell` collector.
  Compiled and linked into Debug `xrGame`/`xrEngine` only when
  `OgsrDiagnosticCapture=true` is set explicitly.
- The archive test runtime additionally uses
  `OgsrDiagnosticRuntimeWorkaround=true`; this is the only configuration that
  compiles the MainMenu guards and menu/pause handoff. Normal Debug/Release
  builds do not alter the standard menu.
- Runtime capture on real Zaton stalkers: **PASS (multiple dead/live runs)**.
- The collector does not call `RunSimulation()`. The live NPC run built a
  temporary shell, captured it, then deactivated and deleted it; health and
  alive state remained unchanged.
- The installed game assets are packed in `resources.db*`; the test runtime uses
  junctions to the installed archives and the repository `gamedata`.

The portable builder and the Debug engine collector are both tested. Runtime
evidence and the exact parity result are recorded in
`docs/p1-physx-ragdoll-foundation.md`.

## Why a runtime capture

The skeleton data lives in binary `*.object` files and is post-processed by
`CKinematics` and optional `fix_cop_joints` compatibility code. Parsing the file
offline would require reproducing:

- `OGF_S_IKDATA` version handling;
- `SJK` limit sign/order conversion;
- `fix_cop_joints` (`SkeletonCustom.cpp:332-360`);
- visibility masks and `LL_Validate` breakable disabling;
- final `dMass` after `CPHShell::SmoothElementsInertia(0.3)`;
- merged `jtRigid` shapes and final joint frames.

A runtime dump is therefore the authoritative source.

## Proposed debug-only collector

A separate diagnostic branch/tool (not production) should add a console command:

```text
ph_dump_ragdoll <visual_name> <output.json>
```

It must run with a level loaded, because `IKinematics`, `mXFORM` and the ODE shell
need a live object. The collector must not call `CreateShell`, must not change
callbacks and must not step physics. It reads an existing live visual and, when
available, an existing ODE shell.

### Required fields per bone

| Field | Source |
|---|---|
| Bone id/name/parent | `IKinematics::LL_BoneCount`, `LL_GetData(i).GetSelfID/GetParentID`, `LL_BoneName` |
| Bind transform | `IBoneData::get_bind_transform()` |
| Current model pose | `IKinematics::LL_GetTransform(i)` after `CalculateBones(TRUE)` |
| Visibility | `LL_GetBoneVisible(i)` |
| Shape type/local transform | `SBoneShape` (`stNone/stBox/stSphere/stCylinder`, `box/sphere/cylinder`) |
| Shape flags | `SBoneShape::flags` (`sfNoPhysics`, `sfRemoveAfterBreak`, `sfNoPickable`) |
| Bone mass / COM | `IBoneData::get_mass()`, `get_center_of_mass()` |
| Final mass/COM/inertia | existing ODE `CPHElement::getMassTensor()` **after** `SmoothElementsInertia(0.3)` |
| Joint type | `SJointIKData::type` (`jtRigid/jtCloth/jtJoint/jtWheel/jtNone/jtSlider`) |
| Source limits | `SJointIKData::limits[0..2]` |
| Effective limits | `IBoneData::lo_limit(k)/hi_limit(k)`; these are what `AddElementRecursive` consumes |
| Spring/damping/friction | `SJointIKData::spring_factor/damping_factor/friction`, `SJointLimit::spring_factor/damping_factor` |
| Breakable | `SJointIKData::ik_flags.test(flBreakable)`, `break_force`, `break_torque` |
| Game material | `IBoneData::get_game_mtl_idx()` |

### Required global fields

- `entityWorld` (`CEntityAlive::XFORM()` / `mXFORM`);
- `rootBodyWorld` and `rootToEntity = inverse(rootBodyWorld) * entityWorld`;
- current linear velocity from `CPHMovementControl::GetCharacterVelocity`;
- active weapon bones (`CEntityAlive::g_WeaponBones`);
- `LL_GetBonesVisible()` and `LL_GetBoneRoot()`;
- model name/hash and OGSR commit SHA.

### Merged, invisible and free bodies

The collector must preserve the exact `AddElementRecursive` disposition:

| Source | Contract disposition |
|---|---|
| Visible bone with a physics shape, `jtRigid` with parent | `MergedIntoParent`; shape/mass folded into parent |
| Visible bone without shape, `jtRigid` without shape | `MergedIntoParent`; no body |
| Invisible bone | `SkippedInvisible`; children attach to the nearest visible ancestor body |
| `jtNone` with a shape | `FreeBody`; body without joint |
| Root without shape | `StaticRoot` / fixed body |
| `jtRigid` with a shape and `flBreakable` | shape-level fracture, not only joint break |

### ODE cross-check

The same debug build should build the ODE shell in a test-only path and dump:

- body count and element order;
- final `dMass` (mass, COM, inertia);
- joint type and effective `dParamLoStop/HiStop`;
- initial body pose after activation;
- `m_object_in_root` / root mapping.

Compare the portable capture field-by-field before accepting a fixture.

## Portable capture pipeline

`buildRagdollCapture` accepts a neutral `SkeletonCapture` and produces the
validated `RagdollCaptureDocument`. The OGSR collector in `ogsr/ogsr_capture.h`
fills that snapshot from `IKinematics` and a prepared `CPhysicsShell`, then calls
`writeRagdollCapture`.

Important: for `MergedIntoParent` bones the collector must provide the **final
parent ODE `dMass`** (after `SmoothElementsInertia(0.3)`). The builder adds the
child shape to the parent shape list but deliberately ignores child mass, because
OGSR's merged mass/COM/inertia are already folded into the parent by the ODE
shell. This avoids silently recomputing a different mass distribution.

The portable JSON loader validates:

- top-level schema version;
- required fields and enum values;
- array sizes and finite numbers;
- body/bone/joint references;
- dynamic mass and positive-definite inertia;
- initial pose/velocity counts;
- `rootToEntity` and all initial transforms.

Errors include a JSON path and, for syntax errors, line/column.

## P1 test status

| Check | Result |
|---|---|
| Portable JSON writer/loader round-trip | PASS |
| Malformed JSON line/column error | PASS |
| Semantic error names the invalid field | PASS |
| OGSR rule builder (jtRigid/invisible/jtNone/frames) | PASS (33 checks) |
| Legacy Euler 5/3 source invariant | PASS (26 checks) |
| Builder + JSON + PhysX synthetic fixture | PASS |
| Real OGSR `IKinematics`/ODE collector | PASS (compiled/linked, multiple runs) |
| Real stalker fixture | PASS (47 bones, 11 bodies, 10 joints) |
| ODE parity (mass/COM/inertia/axes/limits/spring) | PASS (all reported errors `0`) |
| Temporary-shell cleanup | PASS (live `sim_default_stalker_26525`) |

The engine-side collector is Debug-only and is wired through
`OgsrDiagnosticCapture=true`. The real run also verifies the
`(axis, lower, upper)` tuple and axis space against the live ODE joint for the
first three exported axes. `CPHJoint` legacy full_control stores 5 raw records
because of a fallthrough, but only entries 0-2 are Euler DOF; the regression test
and diagnostic note record this cause, so it is not treated as a fifth axis.

## Fixture policy

Do not commit extracted game models or textures. A fixture may contain only
numeric skeleton/joint/shape data if the project's asset license permits it;
otherwise keep it outside the repository and reference it by hash in test logs.
