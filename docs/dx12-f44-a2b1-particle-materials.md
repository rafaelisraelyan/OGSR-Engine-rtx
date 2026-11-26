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
