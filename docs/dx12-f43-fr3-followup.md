# FR3 follow-up — bounds, spatial evidence and bounded detail shadows
Parent `f89922ffa1631356211cfa1c0331127d8a29e241`, baseline PR #22. **Experimental, no Windows build, no FXC or GPU execution.** No issue closed.

## Source comparisons
- Own `Layers/xrRender/light.cpp:170–208` maintains influence spheres for point/spot; `xrCDB/ISpatial_q_frustum.cpp:22–72` uses octree AABB and sphere tests; `ISpatial_q_box.cpp:70–95` shows q_sphere is an AABB query. IX-Ray pinned `7b74303ed0f2d93cf63b65983e3c819f323ebf5c` `light_vis.cpp` and `r4_R_render.cpp` also use light-volume-aware selection. No proof that production spatial query is wrong at the exact disappearance frame.
- Own R4 `Light_DB.cpp:196–238` derives sun direction/color from weather and scale, as DX12 already does. Own R4 `r4_rendertarget_phase_combine.cpp:36–85` derives ambient and hemi parameters from weather and binds environment textures. IX-Ray [combine](https://github.com/ixray-team/ixray-1.6-stcop/blob/7b74303ed0f2d93cf63b65983e3c819f323ebf5c/gamedata/shaders/d3d11/combine_1.ps.hlsl) and [ambient irradiance](https://github.com/ixray-team/ixray-1.6-stcop/blob/7b74303ed0f2d93cf63b65983e3c819f323ebf5c/gamedata/shaders/d3d11/metalic_roughness_ambient.hlsli) use material/environment terms; not equivalent to our scalar model. No arbitrary brightness adjustment.
- Own `FTreeVisual.cpp:27–109` loads geometry and separate TREEDEF2 transform; source header coordinates are not established. The fix below avoids relying on that convention. Microsoft [descriptor contracts](https://learn.microsoft.com/en-us/windows/win32/direct3d12/descriptors-overview) and [shadow map techniques](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps) checked. No IX-Ray code copied; references only.

## Changes
- #17: compute conservative world-space bounding sphere from the actual validated vertex pool range and OGF transform, at level load. Double precision AABB, float center, expanded radius. No second transform of header bounds. Both visible and shadow BVH use result. Malformed/nonfinite source falls back to existing header bounds with log; validate actual Jupiter assets and loading cost. Terrain untouched.
- #24: targeted missing-ID trace uses independent bounded 200m half-extent box query and direct sphere/frustum test, only when a traced ID is missing. Logs broad_found, direct_frustum, source reason, sphere and range. Does not alter lighting admission; absent outside query is not proof of inactivity. Diagnostic can add CPU cost during probe bursts.
- #26: freeze sky-ray sample origin for each 26-ray sweep while retaining previous complete estimate. Avoid mixing unrelated positions; fast movement can temporarily retain stale estimate. Initial and far-object limitations remain.
- #28: opt-in `-DetailSunShadows`, maximum 512 detail instances within 12m and sun frustum; instanced cutout shadow draw using existing auxiliary shadow PSO. Sun only, separate upload region after existing 131072 transform capacity (32 KiB extra per frame slot); normal visible detail budget untouched. No R4 parity claim, no local grass shadows. Default off. Counted as dynamic/rigid shadow draw calls in existing counters, diagnostic burst reports detail instance count. Selection by group order, not globally nearest. Validate visual quality and GPU time.
- #31: new diagnostic views Ambient, SunDirect (after visibility), LocalTotal, GeometricNormals and HDRClip (per-channel excess above 1, saturated for visualization). Preserve production lighting values; HDR clipping and full material/IBL parity still open.
- #30: compile-time reserved descriptor range guards, explicit water slot contract; original t6 fix retained. New normals/sun/lighting diagnostics help isolate intermittent pattern; actual artifact root cause still unknown.
- #23/#25/#27/#29 first commit remains in place. No larger shadow map budget, CSM, GR integration, water cubemap or arbitrary global intensity change.

## Tests executed
Portable JS model (`node tools/dx12/test-f43-fr3-followup-model.js`): 345 assertions passed (transformed points within sphere, descriptor intervals, upload ranges, one-origin hemi sweep). Exact-anchor replacement and raw shader literal length checks. These are model/source checks, not compiled C++ or HLSL. No Windows SDK or GPU runtime available. Run XR_3DA Release x64 and FXC seven entry points before game. Keep all issues open.

## Manual testing
Build branch, use commands in [first FR3 report](dx12-f43-fr3-experimental-corrections.md). New switches/views:
- Default with `-SceneLog` — verify normal scene, trees and FPS.
- `-LightId <id> -ProbeFrames 600 -View ShadowFailureReason` — inspect `ph_dx12_fr3: missing_light` on absent ID; compare logs, do not benchmark burst.
- `-View SunDirect`, `-View Ambient`, `-View Hemi`, `-View GeometricNormals`, `-View HDRClip`, `-View SunVisibility` — same NPC/position/weather; compare `-LegacySunPCF` and `-NoSunShadows` separately.
- `-DetailSunShadows` — compare nearby grass against default off; verify no new self-shadow artifacts, compare dynamic draws/GPU time.
- Tree bounds: compare actual flora and shadows in matching positions, check invalid-bounds log. Restart and reload; `-Validation` separate from FPS.
All runtime acceptance pending. #32 remains OPEN; #40 separate.
