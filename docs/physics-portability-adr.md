# ADR — Portable PhysX, OGSR first → IX-Ray next

> **Status: ACCEPTED (architecture decision only)** · 2026-10-22 · branch `prypiat`  
> No PhysX integration, compilation, runtime validation or IX-Ray collaboration is claimed by this document.

## Intent

Build a PhysX integration **first for OGSR** and then **port the validated module to IX-Ray**. Both are concrete target engines. Portability must influence module boundaries from day one, but OGSR functionality and regression testing remain the first implementation gate. The plan is not to build an independent general-purpose game engine.

## Decision and invariants

1. **Keep ODE intact.** ODE remains the default OGSR production solver and safe fallback. PhysX is initially opt-in for explicitly designated objects or test scenes; no wholesale replacement.
2. **Core portability from the start.** Split a standalone-testable `PhysicsCore` API and `PhysicsPhysX` implementation from fork-specific integration. They must not include OGSR, IX-Ray, ODE or rendering headers and must compile without those runtimes. Prioritize a small API based on actual OGSR needs, not a speculative universal abstraction.
3. **Implement `PhysicsAdapterOGSR` first.** It owns conversion from OGSR data (`Fvector`, game materials, collision geometry, gameplay callbacks, save state), scene lifecycle and fixed-step integration. OGSR-specific code is welcome **inside this adapter**, not in portable core.
4. **Implement `PhysicsAdapterIXRay` second.** After OGSR P2/P3 success, audit the actual IX-Ray source interfaces, build systems, licenses and data ownership. Reuse the core with **no fork-specific changes**, adjusting only adapters/build glue when possible. Portability is accepted only after real IX-Ray build/runtime tests, not folder names.
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

Portable core minimum: math/coordinate conventions, opaque generation-safe handles, scene/body lifecycle, shape and material descriptors, fixed-step control, scene queries, event buffers and explicit ownership. Ragdoll constraints and advanced character movement are added only when driven by OGSR test cases. Initial delivery uses a C++ source library, not a promised stable binary ABI.

## Milestones

| Stage | Deliverable | Acceptance gate |
|---|---|---|
| P0 — contract + compatibility audit | Pin SDK, map OGSR ODE call sites and establish ODE gameplay baseline; define small core and two-adapter contract | ADR approved, source findings verified |
| P1 — independent PhysX CPU harness | Build portable core and PhysX implementation outside OGSR | Init/step/shutdown, leak/regression tests |
| P2 — OGSR DX11 opt-in rigid body | Adapter for one PhysX object and OGSR level collision; ODE gameplay unchanged | In-game DX11 proof + reload/fallback |
| P3 — OGSR hybrid bridge | Single-owner registry, event mapping and optional one-way proxies | Collision/lifetime/load-save and performance tests |
| P4 — IX-Ray portability proof | Implement IX-Ray adapter to **the same core revision**, beginning with a minimal rigid body | Independent IX-Ray build and in-game tests |
| P5 — OGSR PhysX ragdoll | NPC animation-to-ragdoll via PhysX while ODE fallback remains | Gameplay, joint stability and save/load validation |
| P6 — advanced character movement | OGSR actor then hybrid NPC movement after focused audit | Slopes, steps, ladders, crouch, jump, AI regression |
| P7 — wider IX-Ray feature parity | Port proven ragdoll/locomotion pieces via IX-Ray adapter | Independent cross-engine regression matrix |

P4 can begin in parallel with later OGSR mechanics once P3 establishes a stable minimum core. Schedules and implementation are not implied by this ADR.

## Non-goals for initial stages

Do not refactor all of `xrPhysics` into a universal interface, replace `CPHWorld`/`CPHIsland`, depend on DX12, promise GPU PhysX, or claim full cross-solver impulse coupling. Do not directly copy OGSR or IX-Ray gameplay code into portable core.

## Reference code

OGSR: `xrPhysics/PHWorld.cpp`, `PHIsland.cpp`, `PHSimpleCharacter.cpp`, `PHAICharacter.cpp`, `PHActorCharacter.cpp`, `xrGame/PHMovementControl.cpp`, `CharacterPhysicsSupport.cpp`, `movement_manager_physic.cpp`. Refer to `docs/08-roadmap.md`, track P, and renderer portability track L.
