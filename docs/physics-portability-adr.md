# ADR — Portable PhysX, OGSR first → IX-Ray next

> **Status: ACCEPTED (architecture decision only)** · 2026-10-22 · branch `prypiat`
>
> No PhysX integration, compilation, runtime validation or IX-Ray collaboration is claimed by this document.
>
> Implementation status is tracked in [08-roadmap.md](08-roadmap.md), track P: P0–P2.2A are FULL PASS WITH NOTES; P3.0 architecture audit is ARCHITECTURE READY (independent review pending); P3.1 is not started. This ADR remains an architecture decision and does not itself claim PASS.

## Intent

Build PhysX **first for OGSR**, with **character ragdoll as the first in-game feature**, then port the proven module to IX-Ray. Both are concrete target engines. Portability influences module boundaries from day one, but the first runtime acceptance target is an **OGSR DX11 stalker NPC whose animated skeleton transitions to PhysX ragdoll and collides with the level**. Standalone tests serve that goal and are not a competing gameplay milestone. This is not a general-purpose game engine.

## Decision and invariants

1. **Keep ODE intact.** ODE remains the default OGSR production solver and safe fallback. PhysX is initially opt-in for explicitly designated objects or test scenes; no wholesale replacement.
2. **Core portability from the start.** Split a standalone-testable `PhysicsCore` API and `PhysicsPhysX` implementation from fork-specific integration. They must not include OGSR, IX-Ray, ODE or rendering headers and must compile without those runtimes. Prioritize a small API based on actual OGSR needs, not a speculative universal abstraction.
3. **Implement `PhysicsAdapterOGSR` first.** It owns conversion from OGSR data (`Fvector`, game materials, collision geometry, gameplay callbacks, save state), scene lifecycle and fixed-step integration. OGSR-specific code is welcome **inside this adapter**, not in portable core.
4. **Implement `PhysicsAdapterIXRay` second.** After the OGSR ragdoll vertical slice is validated, audit actual IX-Ray source interfaces, build systems, licenses and ownership. Reuse the same core revision with **no fork-specific changes**, adjusting adapters/build glue where possible. Portability is accepted only after real IX-Ray build/runtime tests.
5. **Start on OGSR DX11/R4 + CPU PhysX.** No dependence on DX12, DXR, CUDA or GPU features. The DX12/DXR track F and portability track L are independent. Both renderer backends consume game transforms rather than owning simulation.
6. **Single authoritative solver for each dynamic object.** ODE and PhysX never both step the same body. Duplicated static world colliders are acceptable. One-way ODE→PhysX kinematic proxies may be added; two-way impulse coupling requires a later dedicated design and tests.
7. **Preserve game semantics.** Actor/NPC movement, stairs, slopes, crouch, AI hybrid movement, contact rules, animation, ragdoll, networking and save/load are not assumed transferable from ODE API to PhysX API. Migrate individual behaviors with measured ODE regression baselines and safe rollback.
8. **SDK/license/provenance.** Pin a specific PhysX version, verify included components' license requirements and third-party notices, and review redistribution of any X-Ray-derived code separately. No external partnership is assumed until agreed.
9. **Evidence before PASS.** Agent status is not validation: require reproducible build/runtime logs, diff review, lifecycle tests, collision and movement comparisons, plus DX11/ODE regression checks.

## Architecture

```text
                  OGSR Game Runtime                 IX-Ray Game Runtime
                          |                                 |
                  PhysicsAdapterOGSR                 PhysicsAdapterIXRay
                          \                                 /
                           \                               /
                    [small versioned PhysicsCore API]
                                      |
                             [PhysicsPhysX]
                                      |
                           NVIDIA PhysX CPU SDK

      OGSR legacy gameplay ── existing ODE physics (unchanged by default)
      DX11 and DX12 renderers ── consume synchronized transforms only
```

Portable core minimum is driven by the first **ragdoll**: math/coordinate conventions, opaque generation-safe handles, scene/lifetime control, per-bone body/shape definitions, joint frames and limits, pose/velocity handoff, fixed timestep, collision queries, result poses, events and explicit ownership. Advanced locomotion is deferred. Initial delivery uses a C++ source library, not a promised stable binary ABI.

## First OGSR ragdoll vertical slice (priority #1)

The first gameplay target is **one human NPC on OGSR DX11/R4**. Keep normal character locomotion and AI on existing ODE. At the existing animation/physics-shell transition, select an optional PhysX ragdoll path; initialize its bone bodies from the current animated pose and hand off the character linear velocity. PhysX then becomes the sole simulator for that character's physical skeleton, and the adapter writes simulated bone transforms back to the existing kinematics/rendering system. ODE must not activate a duplicate shell.

**Source integration references (archived `prypiat` snapshot, recheck against current HEAD before implementation):**
- `xrGame/CharacterPhysicsSupport.cpp::CreateSkeleton`: prebuild shell from kinematics and IK data.
- `xrGame/CharacterPhysicsSupport.cpp::CreateShell`: transfers controller velocity, destroys movement controller, switches skeleton and initializes shell callbacks/state.
- `xrGame/CharacterPhysicsSupport.cpp::EndActivateFreeShell`: corrects spawn position and applies initial velocity.
- `xrPhysics/PHShell.cpp::AddElementRecursive` and `xrPhysics/PHJoint.cpp`: bone-derived shapes, elements and joint limits.

**Solver design must be tested, not assumed.** Compare `PxArticulationReducedCoordinate` (floating articulation, tree topology; higher joint fidelity) against rigid bodies with PhysX D6 joints for imported X-Ray rigs. Initial per-link pose, joint limits, root mapping, optional breaks, collision filtering, contact with the level, and callbacks may govern the final choice. Reduced-coordinate articulation links do not behave like independently settable rigid bodies.

**Minimum scope:** fixed-step CPU PhysX; one humanoid model; bone/shape/joint conversion; coherent pose and velocity transfer; static level collider; readback to visual skeleton; engine lifecycle; feature-off ODE fallback. A basic isolated ragdoll fixture can precede game integration, but a box-drop demo is **not** a project deliverable.

**Explicitly deferred:** replacing player or NPC locomotion, PhysX particles/GPU, active ragdoll, deformable bodies, dynamic PhysX↔ODE impulse exchange, all mutants, full IX-Ray feature parity. Preserve ordinary ODE ragdoll and existing gameplay when the feature is disabled.

## Milestones

| Stage | Deliverable | Acceptance gate |
|---|---|---|
| **P0 — ragdoll contract + ODE baseline** | Audit current shell transition, kinematics, shapes/joints, pose callbacks, fixed-step, level collision and save/network paths; pin SDK/licenses | Source-backed architecture, ODE captures/tests, boundary and fallback design |
| **P1 — portable PhysX ragdoll harness** | Minimal standalone portable PhysX CPU skeleton (not generic rigid body showcase): bone shapes, joints, poses, lifetime | Rig drops and contacts static floor, stable limits, init/step/shutdown and leak tests |
| **P2 — OGSR DX11 ragdoll proof** | `PhysicsAdapterOGSR`, one opt-in NPC, animated-pose → PhysX handoff, static X-Ray level collider and bone visual synchronization | One visible character ragdoll in real OGSR DX11, no double ownership, ODE fallback unchanged |
| **P3 — OGSR ragdoll hardening** | Tune mass/limits/contact filters; exercise repeated spawn/remove, reload, high/low FPS, save/load, transitions and performance | Repeatable gameplay tests + independent review; limitations documented |
| **P4 — IX-Ray first port** | Audit IX-Ray and connect same core revision via a dedicated adapter, beginning with this ragdoll feature | Clean independent IX-Ray build and gameplay proof; not assumed in advance |
| **P5 — wider ragdoll coverage** | More human rigs, later mutants/exceptional models and selective legacy interactions | Per-rig and cross-engine regression matrix |
| **P6 — advanced character locomotion (future)** | Evaluate actor and hybrid AI controllers only after proven ragdoll | Slopes, steps, ladders, crouch, jump and AI regression |

P0–P3 are the immediate focus of track P. IX-Ray remains a planned second implementation target, not an existing integration or partnership. The separate DX12 F4.0 renderer track is not blocked by P. This ADR alone does not prove implementation, build or runtime PASS.

## Non-goals for initial stages

Do not refactor all of `xrPhysics` into a universal interface, replace `CPHWorld`/`CPHIsland`, begin with unrelated rigid-body demos as gameplay goals, depend on DX12, promise GPU PhysX, or claim full cross-solver impulse coupling. Do not directly copy OGSR or IX-Ray gameplay code into portable core.

## Reference code

OGSR: `xrPhysics/PHWorld.cpp`, `PHIsland.cpp`, `PHSimpleCharacter.cpp`, `PHAICharacter.cpp`, `PHActorCharacter.cpp`, `xrGame/PHMovementControl.cpp`, `CharacterPhysicsSupport.cpp`, `movement_manager_physic.cpp`. Refer to `docs/08-roadmap.md`, track P, and renderer portability track L.
