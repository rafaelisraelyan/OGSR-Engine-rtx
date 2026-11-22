# DX12 F3.1 — GPU Validation Report

**Date:** 2026-10-16  
**Git HEAD:** 7ae6958d7 (branch: prypiat)  
**Code version:** F3.1 v4 (post-review fixes applied)

## Environment

| Component | Value |
|---|---|
| GPU | NVIDIA GeForce RTX 5070 Laptop GPU |
| Vendor ID | 10DE |
| Device ID | 2D18 |
| Driver | (default Windows 11) |
| OS | Windows 11 |
| Feature Level | 11.0 |
| Debug Layer | Enabled |
| GPU-Based Validation | Enabled |
| DRED | Enabled (auto-breadcrumbs + page fault) |

## Build

| Project | Configuration | Result |
|---|---|---|
| xrRender_R5_DX12 | ReleaseDX12 x64 | 0 errors, 0 warnings |
| xrDX12Smoke | ReleaseDX12 x64 | 0 errors, 0 warnings |
| CPU camera tests | 5/5 tests | PASS, exit 0 |
| Resize lifecycle test | 4 resizes + 25 frames | PASS, exit 0 |

---

## GPU TEST A — Automatic Depth Verification

**Command:** `xrDX12Smoke.exe -depthtest`  
**Result:** PASS  
**Exit code:** 0

### Control Pixels

| Pixel | Expected RGBA | Actual RGBA | Result |
|---|---|---|---|
| (640,360) | (0, 255, 0, 255) | (0, 255, 0, 255) | **PASS** |
| (640,180) | (0, 0, 255, 255) | (0, 0, 255, 255) | **PASS** |
| (640,40) | (18, 41, 82, 255) | (18, 41, 82, 255) | **PASS** |

### Debug Layer / GBV / DRED

| Category | Count |
|---|---|
| ERROR | 0 |
| CORRUPTION | 0 |
| WARNING | 0 |
| INFO (GBV startup message) | 1 |

DRED: no page faults detected.

---

## GPU TEST B — Visual Scene

**Command:** `xrDX12Smoke.exe` (30s timeout)  
**Result:** PASS (visual NOT VERIFIED)  
**Exit code:** 0 (timeout kill)

- 30 seconds of continuous rendering without errors.
- Camera orbits (fixedPose=0 in log).
- No validation errors.
- **Visual appearance NOT VERIFIED** — no screenshot, no visual inspection.
- Camera orbit confirmed by fixedPose=0 in log, NOT by visual observation.
- **WM_CLOSE shutdown NOT VERIFIED** — window not found by PowerShell.

---

## GPU TEST C — Resize & Frame Lifetime

**Method:** CPU harness (`test_resize.exe`) directly calling CHW12 API  
**Result:** PASS (API-level)  
**Exit code:** 0

### Resize Harness Details

The harness creates a real D3D12 device, swapchain, and GPU resources via `CHW12::Initialize`. It is NOT a simulation.

| Resize | From | To | Frames | Result |
|---|---|---|---|---|
| 1 | 1280x720 | 1600x900 | 5 | PASS |
| 2 | 1600x900 | 1279x719 | 5 | PASS |
| 3 | 1279x719 | 1281x721 | 5 | PASS |
| 4 | 1281x721 | 1280x720 | 5 | PASS |

- Total: 25 frames across 4 resizes.
- Depth buffer + DSV recreated correctly per size.
- Aspect ratio updated.
- No validation errors. Shutdown clean.

### Not Verified

- WM_SIZE from OS (interactive resize) — NOT VERIFIED.
- Minimize → Restore — NOT VERIFIED.
- GUI resize — NOT VERIFIED (PowerShell could not locate window).

---

## Negative Control — Depth Test Detection

### Run 1: DepthEnable = FALSE

**Command:** `xrDX12Smoke.exe -depthtest` (with `DepthEnable = FALSE` in PSO)  
**Result:** FAIL (as expected)  
**Exit code:** 1

| Pixel | Expected | Actual | Result |
|---|---|---|---|
| (640,360) | (0,255,0,255) | (0,0,255,255) | FAIL — far cube drawn over near |
| (640,180) | (0,0,255,255) | (0,255,0,255) | FAIL — near cube drawn over far |
| (640,40) | (18,41,82,255) | (18,41,82,255) | PASS — background unchanged |

### Run 2: DepthEnable = TRUE (restored)

**Command:** `xrDX12Smoke.exe -depthtest`  
**Result:** PASS  
**Exit code:** 0

All three pixels match expected values. System correctly restored.

---

## Exit Code Fix

**Problem:** `WinMain` always returned 0, even after `RenderFrame` failure.

**Fix:** Added `int exitCode = 0;` before message loop. Set `exitCode = 1;` on RenderFrame failure. Return `exitCode` instead of hardcoded 0.

**File:** `xrDX12Smoke.cpp`, `WinMain()`.

## Test Summary

| Test | Result | Evidence |
|---|---|---|
| A — GPU depth-test | **PASS** | Exit 0, 3/3 pixels match |
| B — Visual scene | **PASS** (visual NOT VERIFIED) | 30s, 0 errors, timeout kill |
| C — Resize/lifetime | **PASS** (API-level) | 4 resizes, 25 frames, exit 0 |
| Negative control | **PASS** | FALSE→FAIL(exit 1), TRUE→PASS(exit 0) |
| Debug Layer | **PASS** | 0 ERROR, 0 CORRUPTION, 0 WARNING |
| GBV | **PASS** | Enabled, no validation failures |
| DRED | **AVAILABLE** | Enabled, no page faults |
| WM_CLOSE shutdown | **NOT VERIFIED** | Window not found by PowerShell |
| GUI resize | **NOT VERIFIED** | WM_SIZE/Minimize not tested |

---

## Files Created / Modified

### Created
- `ogsr_engine/Layers/xrRenderDX12/dx12camera.h`
- `ogsr_engine/Layers/xrRenderDX12/dx12camera.cpp`
- `ogsr_engine/Layers/xrRenderDX12/dx12mesh.h`
- `ogsr_engine/Layers/xrRenderDX12/dx12mesh.cpp`
- `docs/dx12-f3-1-depth-mvp-camera.md`

### Modified
- `ogsr_engine/Layers/xrRenderDX12/CHW12.h`
- `ogsr_engine/Layers/xrRenderDX12/CHW12.cpp`
- `ogsr_engine/Layers/xrRenderDX12/dx12quarantine.h`
- `ogsr_engine/Layers/xrRenderDX12/xrDX12Smoke.cpp`
- `ogsr_engine/Layers/xrRenderDX12/xrDX12Smoke.vcxproj`
- `ogsr_engine/Layers/xrRenderPC_R5_DX12/xrRender_R5_DX12.vcxproj`

### Deleted
- `ogsr_engine/Layers/xrRenderDX12/dx12triangle.h`
- `ogsr_engine/Layers/xrRenderDX12/dx12triangle.cpp`

---

## Unverified Scenarios

- WM_CLOSE shutdown — window not found by FindWindow/EnumWindows
- WM_SIZE from OS (interactive window resize)
- Minimize → Restore
- Visual appearance (no screenshot, no visual inspection)
- Multiple Initialize/Shutdown cycles on same CHW12
- Post-cleanup leak audit (requires specialized tools)
- CI pipeline (no commit performed)
- Interactive camera control (automated test only)

---

## Git Status

```
 M AGENTS.md                          (pre-existing user changes)
 M docs/08-roadmap.md                 (pre-existing user changes)
 M ogsr_engine/Layers/xrRenderDX12/CHW12.cpp
 M ogsr_engine/Layers/xrRenderDX12/CHW12.h
 M ogsr_engine/Layers/xrRenderDX12/dx12quarantine.h
 D ogsr_engine/Layers/xrRenderDX12/dx12triangle.cpp
 D ogsr_engine/Layers/xrRenderDX12/dx12triangle.h
 M ogsr_engine/Layers/xrRenderDX12/xrDX12Smoke.cpp
 M ogsr_engine/Layers/xrRenderDX12/xrDX12Smoke.vcxproj
 M ogsr_engine/Layers/xrRenderPC_R5_DX12/xrRender_R5_DX12.vcxproj
?? docs/dx12-f3-1-depth-mvp-camera.md
?? ogsr_engine/Layers/xrRenderDX12/dx12camera.cpp
?? ogsr_engine/Layers/xrRenderDX12/dx12camera.h
?? ogsr_engine/Layers/xrRenderDX12/dx12mesh.cpp
?? ogsr_engine/Layers/xrRenderDX12/dx12mesh.h
```

`git diff --check`: clean (only CRLF warnings).

---

## Evidence Bundle

`f31_evidence.zip` (86KB) includes: all source files, GPU report + F3.1 spec, all GPU test logs (A/B/C/negative/restore), resize harness source + build script + log, build log with exit code fix, git status + diff check.

---

## Conclusion

F3.1 GPU validation: **PASS** (with noted limitations)

- Depth test: **PASS** — 3/3 control pixels match on real RTX 5070 GPU.
- Negative control: **PASS** — disabled depth test correctly detected.
- Resize lifecycle: **PASS** — 4 resizes, 25 frames, clean shutdown.
- Exit code fix: **DONE** — RenderFrame failure now returns exit code 1.
- WM_CLOSE shutdown: **NOT VERIFIED** — requires interactive window or alternative automation.
- GUI resize: **NOT VERIFIED** — requires interactive window management.
