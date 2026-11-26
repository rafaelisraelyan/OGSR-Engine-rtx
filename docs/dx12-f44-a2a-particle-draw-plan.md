# F4.4-A2a — classic particle draw-plan contract

Baseline: 9ce6ab9f697cdd5327605b88779b47a9234c2d24. Scope: portable CPU-only precursor to native GPU drawing. Not connected to the bridge runtime; A2 and parity remain open.

## Source evidence
- Layers/xrRender/Blender_Particle.cpp: six normal-pass blend IDs and depth-write policies; SET uses deferred particle shader, reference 200. Game/Resources_CoP_1.6.02/gamedata/shaders/r3/deffer_particle.ps calls calc_alpha_test and writes G-buffer/motion.
- Layers/xrRender/blenders/Blender_Recorder.cpp:161–203: depth comparison LESSEQUAL and RGB/alpha blend factors. Layers/xrRenderDX10/dx10StateUtils.cpp:462–479 converts color factors to corresponding alpha factors.
- xrCore/_color.h:5,19–22: packed ARGB, 0xAARRGGBB.
- A1 Prepared compaction uses canonical indices and emitter order. Draw plan retains this order, which is NOT yet original render-pass sorting.

## Implemented
GameDx12ParticleDrawPlan.h: six classic blend descriptors, alpha factor conversion, explicit packed color decoding, 4096-quad draw batch limit, canonical index/range/identity validation, unresolved-quad counter. Caller provides resolved material IDs aligned to A1 emitter slots; no material resolver or texture ownership yet. A2a does not modify A1 capture or DX11.

## Executed tests
Linux GCC C++20 with -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g:
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Iogsr_engine/xr_3da tools/dx12/test-f44-particle-draw-plan.cpp -o particle-draw-plan-test && ./particle-draw-plan-test

PASS: boundary, malformed/stale inputs, packed color, basic blend state checks, 200 randomized fixtures and actual A1 BuildPackets -> BuildDrawPlan test. A1 header verified by git blob hash 0444266811b7a49e9f9b26978844e422b0e2b748. Tested new source blobs f1ccbe14159c027968d08a9306c92735ef5ca81e and 448d5ff625099c414ed11d28f3c0b18c16db6b6b. Windows, WARP, native GR and hardware GPU NOT RUN. Test not yet in combined Windows runner.

## Next gate (A2b)
Resolve actual shader/material metadata and texture identity on owner thread; bind plan to same snapshot and material generation; capture every rendered frame when explicitly enabled instead of every 120 frames; preserve original cull/sampler/pass/visibility semantics. Use existing native frame queue and fence/quarantine ownership for bounded upload and descriptor lifetime. Build PSOs and HLSL (deferred SET separately), verify WARP/readback blend/depth/color and failure paths before native Jupiter comparison. No generic-alpha fallback. Do not claim particle rendering or visual parity from A2a.

Microsoft contracts: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_render_target_blend_desc and https://learn.microsoft.com/en-us/windows/win32/direct3d12/fence-based-resource-management .
