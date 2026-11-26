# F4.4-A2b.1 — classic particle source metadata and CPU DDS eligibility

Baseline: 790142d6fd29e27cea41ed9f2a3d9d1b4677aeb7. This is the material-readiness portion of A2b, not native particle rendering.

## Source evidence

- Layers/xrRender/ResourceManager.cpp Create(): Lua shader overrides precede C++ blenders; unknown/Lua/non-particle shaders are rejected by the new read-only adapter, not silently mapped to BLEND.
- Layers/xrRender/Blender_Particle.cpp Compile(): six classic normal-pass modes and texture clamp. blenders/Blender.cpp: IBlender::Compile passes priority and strict sorting. The new nonvirtual getter reads these original properties under the experimental bridge macro.
- GameDx12J2ABatch_ReadTextureNames: existing owner-thread VFS search and DDS decoding, reused with a conservative extensionless texture-name subset. No new VFS or GPU resource manager.

## Behavior

- Builds an owned material inventory from the A1 snapshot, retains original blend ID, priority, clamp and strict-sorting metadata, deduplicates lower-case DDS names, rejects unsupported aliases/paths, caps names at 128 and passes a 32 MiB budget to the existing reader.
- Builds the A2a draw plan only for known classic material IDs with nonempty decoded DDS payloads; unresolved quads are counted. Source order is retained; actual pass sorting, material generation and GPU texture residency remain future gates.
- Existing opt-in -ParticleCapture diagnostic cadence (every 120 frames, original camera only) now emits material_cpu_only, eligible_quads, unresolved_quads, batches, gpu_draws=0 and bounded material_source records. This is a CPU diagnostic, not a texture cache. It may perform VFS reads at diagnostic cadence. Default path, R4 Render and particle simulation are unchanged.
- Added A2a and A2b.1 portable tests to the combined Windows runner; runner NOT executed here.

## Evidence actually executed

Linux GCC C++20, -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g. Both binaries returned exit 0:

- tools/dx12/test-f44-particle-materials.cpp: PASS (name filtering/dedup, real A1/A2a types, missing DDS eligibility, unknown mode, invalid masks/ranges, identity, capacity).
- tools/dx12/test-f44-particle-draw-plan.cpp: PASS (including 200 randomized fixtures).

Tested blob hashes: GameDx12ParticleMaterials.h b8a248ee9f9616741d20131fb56a57319684b285; test-f44-particle-materials.cpp 467209c893b466cdec65f73d4aabad9e7e2fa6de. A1 and A2a headers matched their baseline blob hashes. The native R4 adapter/bridge, VFS access against game assets, Windows build, WARP and hardware GPU were not executed.

## Manual check and remaining work

Build experimental branch on Windows and launch the native bridge via launcher with -ParticleCapture -GR verify -SceneLog. At an active campfire or smoke emitter, inspect material_cpu_only and material_source records. Record actual shader names, blend IDs, priority, DDS readiness and unresolved counts. Zero eligible quads is not a rendering pass. No GPU draws are submitted by this patch.

A2b.2: design GPU material generation/cache and frame-slot upload lifetime with the existing NativeFrame/StaticScene owner, create six PSO states and HLSL (SET needs separate opaque/deferred path), validate texture formats, descriptor lifetime, alpha/color/depth on WARP and hardware; preserve pass order and quarantine semantics. No generic-alpha fallback. A2 and visual parity remain open.

## Follow-up: no independent diagnostic texture count or aggregate byte cutoff

The initial 128-name and 32 MiB aggregate limits above describe the original A2b.1 revision, not the current implementation. The inventory now admits every unique texture represented by the valid A1 snapshot. The diagnostic probes one unique DDS at a time, retaining only the decoded eligibility mask; it does not accumulate CPU texture payloads or enforce a fixed aggregate byte quota. The shared VFS reader's existing 16 MiB per-file limit and A1 snapshot capacities (1024 emitters, 32768 quads, 4096 visuals) are unchanged. This does not imply unlimited production rendering or a GPU texture cache; VFS probes remain sampled owner-thread work and may be expensive in dense scenes.

Production requirement: no arbitrary prototype quotas that silently discard valid scene content. Use existing resource ownership, scalable texture/descriptor registry, hardware/OS memory budgets, checked dynamic allocation, bounded work scheduling, frame-slot fences and explicit backpressure/error reporting. A per-batch size may remain a processing granularity, never a total scene quota. Audit and replace remaining A1 diagnostic caps as part of the native production path, including per-file VFS limits, without disabling malformed asset or overflow protections. No claim of runtime parity.

Follow-up portable test: GCC C++20 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g, exit 0. Full existing 1024-emitter snapshot with 1024 unique textures, no unresolved quads with a synthetic decoded mask; invalid inventory rejected. Tested blobs b4b06a08083ff2429159921256d08732ea1ea282 and 7ec9bf7562738c4390645858091d8619b04fb8c7. This does not test actual DDS reads beyond 32 MiB. Windows and GPU NOT RUN.
