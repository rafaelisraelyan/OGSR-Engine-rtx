# F4.4-A1 — Classic Particles: owner capture and bounded CPU packets

Baseline: a438fb2c04d3b46c1536e659fbfc14666f87cbd3. Scope: original particle snapshot capture and CPU packet preparation, **NOT GPU drawing**. Portable CPU fixture PASS; Windows compilation and native diagnostic run pending. Do not interpret captured_only as gameplay parity.

## Verified source comparison

OGSR baseline: `Layers/xrRender/ParticleEffect.cpp` OnFrame 113–192, FillSprite 268–376, ParticleRenderStream 397–521, Render 523–575; `ParticleEffectDef.h` SFrame and flags; `ParticleGroup.h` SItem children; `r__dsgraph_build.cpp` 429–445 group expansion; `R_Backend.cpp` QuadIB triangles 0,1,2 / 3,2,1. `xrGame/ParticlesObject.cpp` 142–178 shows schedule-based DoWork; `xr_3da/PS_instance.cpp` owns registration in ps_active/ps_destroy. `device.cpp` 267–313 calls Bridge before TTAPI/seqFrameMT. R4 is a static library referenced by XR_3DA.

IX-Ray pinned source [7b74303e](https://github.com/ixray-team/ixray-1.6-stcop/tree/7b74303ed0f2d93cf63b65983e3c819f323ebf5c): [ParticleEffect.cpp](https://github.com/ixray-team/ixray-1.6-stcop/blob/7b74303ed0f2d93cf63b65983e3c819f323ebf5c/src/Layers/xrRender/ParticleEffect.cpp#L296-L505) caches original quad construction, interpolates simulation fields and copies bounded batches. [Blender_Particle.cpp](https://github.com/ixray-team/ixray-1.6-stcop/blob/7b74303ed0f2d93cf63b65983e3c819f323ebf5c/src/Layers/xrRender/Blender_Particle.cpp#L83-L108) preserves SET, BLEND, ADD, MUL, MUL_2X and ALPHA-ADD. [particle.ps.hlsl](https://github.com/ixray-team/ixray-1.6-stcop/blob/7b74303ed0f2d93cf63b65983e3c819f323ebf5c/gamedata/shaders/d3d11/particle.ps.hlsl) compares view-space particle depth with G-buffer point depth for soft particles; additive and distortion have separate shader paths. No IX-Ray implementation code copied. Preserve OGSR simulation/rotation/velocity scaling/world/face/path alignment and original atlas UVs first; interpolation is a later opt-in experiment.

## Implemented

- `GameDx12ParticlePackets.h` immutable owned quad snapshot, copied effect/shader/texture strings, flags, original packed FVF::LIT color and cull mode, contiguous spans, session/epoch identity, 32768 quad / 1024 emitter / 4096 visual caps. 24-byte vertex; four vertices and six 32-bit indices per valid quad.
- `ParticleEffect.cpp` owner capture adapter invokes the unchanged original ParticleRenderStream in chunks of at most 4096 sprites. Walks effect and related/free group children with dedup and depth cap. Skips HUD, invalid or unsupported definitions and records observed/truncated counts. No D3D11 buffer lock, no simulation update, no R4 render-function change, no vtable changes.
- Native bridge opt-in `-ParticleCapture`: sample every 120 Device frames on the actual game camera before seqFrameMT. Reads ps_active excluding locked, pending destruction, invisible and HUD roots. This is a bounded **active-system inventory**, not R4 visible/submitted parity. No live pointer goes to GR. GR uses existing RunRanges for disjoint byte classification; owner compacts in input order; Verify checks serial vs parallel masks/packets. No new scheduler. Reject failures and discard diagnostic output without GPU submission. No particle texture load or draw yet.
- Bounded material diagnostics (first 12 captured emitters) support next-stage real shader/material mapping. Unknown modes are not guessed or mapped to generic alpha.

## Executed evidence

Linux: `g++ -std=c++20 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread -Iogsr_engine/xr_3da tools/dx12/test-f44-particle-packets.cpp -o particle-test && ./particle-test`.

PASS: 836785 assertions; 200 randomized snapshots, bounded capacity and threaded parity. Covers full capacity, empty and one-quad, invalid float/UV, stale session/epoch, invalid ranges, missing/oversize material names, invalid cull and original R4 triangle winding. Portable std::jthread fixture tests disjoint range execution, **not** actual GR runtime or the R4 capture adapter. Verified blobs: header `0444266811b7a49e9f9b26978844e422b0e2b748`, test `30ee63342807bc394a3037e007a43e42fb2d20cf`. Combined CSM runner builds/runs this fixture before WARP.

## Remaining gates

Windows build and manual diagnostic run with `-ParticleCapture -GR verify -SceneLog` or `-GR parallel`. Compare active/root/effect/quad/packet counters and original R4 material identities at campfire, smoke, anomaly, combat and reload. The capture adapter uses current original R4 camera and active simulation state; skips fly camera; no forced extra OnFrame. Need audit attached/non-ps_active particles, invisible/portal/frustum semantics, exact native simulation cadence, and all material/pass registrations. No claim that all game particle families are captured. Snapshot budget order is diagnostic traversal order, not a production priority policy. GR validation is real integration code but not yet Windows executed.

A2: actual native GPU upload lifetime and six source blend/depth states, texture registry, color packing, bounded draw batches, sorting across original passes; then depth-aware soft particles, fog/HDR, distortion and HUD separately as appropriate. Reuse shared frame/queue/descriptor ownership, no extra runtime. Keep parity gate open (#14). Do not merge prypiat or close issues.
