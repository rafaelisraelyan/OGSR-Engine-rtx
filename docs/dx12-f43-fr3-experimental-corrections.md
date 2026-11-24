# F4.3-FR3 — experimental raster corrections (#32)
Baseline `35b463272f8a06e0036dbac5c9b4de5e26d3d62f` (review PR #22). **Experimental source only; Windows Release x64 / FXC / PowerShell runtime / GPU / gameplay NOT RUN.** Do not merge or close issues based on this commit.

## Changes
- #23: optional soft near-volume shadow priority (25% near weighting instead of absolute precedence; existing 25% retained-light bonus). Launcher enables experimental policy, `-LegacyLocalPriority` restores FR2 comparator. Same two spot + one point budget, same admission. This is an A/B candidate, not a guarantee against leaks when maps are unavailable. #24 admission is not changed; use existing FixedLightIds/FreezeLightRecord/LocalRaw/LocalProbe.
- #25: optional orthographic receiver-plane PCF uses geometric normal from screen derivatives, computes depth gradient from the light projection columns and compares at each of four bilinear texel centers. Existing normal/constant bias, extent, raster PSO and shadow resources are unchanged. Degenerate/grazing planes fall back to FR2. Launcher enables candidate; `-LegacySunPCF` restores FR2 sampling. This addresses fixed-depth PCF error on sloped planar receivers; the actual NPC artifact and quality/performance remain unverified. CSM deferred.
- #27: explicit LocalShadow projection reasons and finite/first-layer checks. Production fallback still visibility=1 (not a black light). `ShadowFailureReason` requires LightId: red=no map, yellow=outside coverage, cyan=invalid layer, magenta=invalid projection or missing target ID, black=no contribution, green=valid sample (dark green occluded, bright green visible). Compare with CPU trace to distinguish missing ID from invalid projection. `SunVisibility` shows grayscale visibility, magenta no coverage/map; `SunDepthDelta` red=receiver deeper than central depth, green=shallower, magenta unavailable; delta is uncorrected central comparison, not final PCF.
- #26: retain last complete sky-hemi while refreshing, do not reset an in-progress sweep on each movement, interleave original 26 directions and respect TRACE_HEMI. Same four rays per one scheduled object per frame; initial partial estimate and far/never-sampled objects remain limitations. Mixed-position sweep is an approximation; rapid interior/exterior movement can temporarily retain stale estimate. Do not declare outdoor lighting fixed.
- #29: continue bounded SSR search after rejected depth crossing instead of immediately exiting. Same eight steps and existing hit tolerance/sky fallback; no refinement, new cubemaps or composition rewrite. GPU cost/false positives require comparison.
- #17: unchanged; actual OGF coordinate convention and transformed asset evidence must precede a bounds fix. #28 details shadows deferred; #30 intermittent artifact not established; #31 color-space/material/sun term regression remains open. #24, all remaining issue acceptance gates and GR tracker #40 remain open.
- No changes to terrain, GPU ownership/fences, shadow budget, t6 descriptor layout, DX11/R4, GR or global lighting intensity.

## Checks actually performed
- Unique exact source-anchor replacements at pinned HEAD; raw C++ HLSL literal segments checked (<15,000 characters); shader caller count and permutation checked.
- Portable numerical model test (`tools/dx12/test-f43-fr3-model.js`): 1926 assertions passed in JavaScript execution, including rotated orthographic plane gradient vs direct world-plane projection and occluder comparison, retention model, ray permutation/completion and rejected SSR crossing. This is an independent formula/control-flow model, NOT actual HLSL execution or C++ engine unit tests.
- Linux environment does not have Windows SDK/MSVC/FXC or PowerShell. Compile and run locally before gameplay; no binaries published.

## Build and manual A/B
Use existing XR_3DA Release x64 project with DX12 bridge enabled and all seven FXC entry points. Confirm compile, launcher AST, diff check; if any fail, stop and capture exact errors. Use portable launcher from this branch:
```powershell
$run = 'C:\path\to\worktree\tools\dx12\run-f43f-jupiter.ps1'
$root = 'C:\OGSR_ENGINE\OGSR_J1_Runtime_R2'
$exe = 'C:\path\to\worktree\bin_x64\xrEngine.exe'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$run" -GameRoot "$root" -Exe "$exe" -SceneLog
```
Repeat same command with these additional switches, separately (restart the process between A/B):
- `-LegacyLocalPriority`: compare wall leakage at near/far on several lamps.
- `-LightId <actual-id> -View ShadowFailureReason -ProbeFrames 600`: compare map absence/coverage with scene logs; IDs process-local.
- `-NoLocalShadows`: isolate raw light admission.
- `-LegacySunPCF`: compare NPC/static sun artifacts; `-View SunVisibility` and `-View SunDepthDelta` for exact localization.
- `-NoDynamicSunShadows`, `-NoSunShadows`: isolate dynamic caster/self-shadow.
- `-View Hemi`: compare moving NPC, interior/exterior, far/near. `-View WaterReflection`: shoreline and NPC reflection.
- `-Validation` only separately. Normal FPS without validation; compare matched scenes, timings, draw counts. Do not close #32 until owner manual acceptance.