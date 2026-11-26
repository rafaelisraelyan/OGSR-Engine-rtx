# ADR — Modular native DX12 production architecture

**Status: ACCEPTED architecture policy (owner approved 2026-11-27).**
**Scope:** experimental native DX12 renderer, branch `experiment/f43-csm-r2-c`.
**Baseline:** `2b04d3619bc6558f6ad8d257fa01e51e72102e34` (F4.3-P0-B3c1).
Documentation only; no implementation or runtime acceptance implied.

## Context and decision

`ogsr_engine/xr_3da/GameDx12Bridge.cpp` (4444 lines at baseline) and `GameDx12StaticScene.cpp` (5653 lines) concentrate integration, resource management and rendering work. The problem is mixed responsibilities, not an arbitrary line-count threshold. New rendering capabilities must have coherent module boundaries, explicit ownership and testable contracts. Production First remains mandatory: no arbitrary scene-content quotas or silent truncation; retain checked arithmetic, API/hardware limits, memory budgeting and explicit backpressure.

## Target responsibilities

Names below are illustrative, not a requirement to create empty classes or a claim of existing implementation. Preserve existing useful modules and naming conventions.

| Boundary | Responsibility |
| --- | --- |
| GameDx12Bridge | Game lifecycle, owner-thread capture and renderer orchestration; not implementation of individual GPU passes |
| GameDx12NativeFrame | Swapchain, frame slots, command recording/submission and synchronization |
| Geometry / Texture Registry | Resource identity, generation, cache, upload, streaming and retirement |
| Descriptor Allocator | CPU descriptor sources, shader-visible ranges, capacity/tier checks, slot/version lifetime |
| Material System | Material interpretation, bindings and PSO selection/cache |
| Shadow Renderer | CSM, adaptive local pages, caster preparation and shadow recording |
| Lighting Renderer | Light data and clustered shading |
| Particle / Water / Atmosphere Renderers | Feature-specific preparation, state and passes |
| HUD Renderer / UI Renderer | First-person scene geometry/depth and ordered UI/font passes |
| Ray Tracing | Future DXR acceleration structures, hit resources and ray passes |

## Mandatory rules for implementation

1. Inspect actual callers, owners, existing abstractions and build targets before choosing a boundary. Reuse shared infrastructure; do not introduce a second device/queue/fence timeline, independent scheduler or duplicate registry for convenience.
2. Keep orchestration separate from implementation. New substantial feature-specific code belongs in a focused module, not another unrelated block in Bridge or StaticScene. Small integration seams may remain there. No fixed line-count limit, mandatory one-class-per-file rule, speculative framework or empty modules.
3. State which object owns each CPU/GPU resource, descriptors and command lists; document borrowed handles, session/epoch/generation, thread affinity, resource states and last-use fence. No live GPU descriptor overwrite, early reuse or release. Preserve cancellation and terminal quarantine contracts for uncertain submission/completion.
4. Owner thread captures mutable game state. GR workers consume immutable snapshots and write disjoint outputs or exclusive command-list/allocator pairs. Preserve deterministic ordering and existing serial/parallel/verify semantics; no unsynchronized game/VFS access.
5. Prefer narrow interfaces, acyclic dependencies and portable CPU-testable policy where useful. Do not hide shared mutable state behind globals, service locators or a replacement monolith. Keep shader/CPU ABI explicit and verify reflection when changed.
6. Extract incrementally along actual feature work, not a wholesale rewrite. Separate behavior-preserving extraction from functional changes into independently reviewable commits where practical; document and test unavoidable coupled changes. Maintain original R4/DX11 behavior, feature-off builds, asset/mod compatibility and rollback path. Do not merge or rebase prypiat as part of this policy.
7. Each change must identify module boundaries, dependency direction, owner/lifetime contracts, test coverage and remaining limitations. Validate affected builds, CPU tests, GPU readback/negative cases, Debug Layer/GBV, GR parity and runtime/performance as applicable. Record exact SHA and actual execution status; source-only or synthetic PASS is not GPU/gameplay acceptance.

## Immediate application: F4.3-P0-B3c2

Introduce a focused descriptor-management component for the fenced shader-visible shadow-page table; keep the legacy binding fallback and existing frame owner. Audit the material heap and root layout before selecting range/version strategy. Use actual ResourceBindingTier/capacity and non-uniform indexing contracts where supported. Coordinate descriptor lifetime with actual slot fences and submission outcome. No parallel descriptor manager competing with existing material bindings; no premature full renderer rewrite. Any shared allocator extension should serve demonstrated consumers and preserve existing material bindings. B3d will address variable views, command-list pools and timing separately.

The existing [hybrid shadow plan](dx12-f43-hybrid-shadow-evolution.md), [native frame/GR ADR](dx12-f3-5-architecture-adr.md), [HUD/UI architecture](dx12-f45-hud-ui-architecture.md) and [original DX12 ADR](dx12-architecture-adr.md) remain applicable. This decision adds a cross-cutting modularity requirement without changing their implementation status.

## Review / acceptance

Review each future native DX12 patch against these rules. If a temporary exception is necessary, document its reason, scope, owner and removal condition; do not silently make it permanent. No new runtime, build or GPU results are claimed by this documentation-only decision.
