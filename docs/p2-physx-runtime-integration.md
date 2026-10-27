# P2.0 - Opt-in PhysX CPU Runtime Integration

**Status: P2.0 diagnostic runtime PASS.** The game can load an isolated PhysX
5.6.1 CPU scene through a dynamically loaded bridge DLL, step it with the real
frame delta, suspend it while paused, honor the engine time factor, unload it
with the level and recreate it after a level reload. ODE remains the only
gameplay physics implementation; the PhysX scene moves one diagnostic test box
and never writes game object transforms.

The scene is not a ragdoll and is not attached to NPCs. Ragdoll replacement is
still out of scope.

## Scope

- New SDK-free runtime context in `Layers/PhysicsPhysX` (no PhysX types in the
  public header).
- New C-ABI bridge DLL `ogsr_physx_runtime.dll`, built separately with the
  PhysX SDK.
- New opt-in game integration in `xrGame` behind the `OgsrPhysXRuntime`
  property and the `-physx_runtime` command-line flag.
- Static level fragment around the actor, one diagnostic test box, fixed-step
  simulation, contact count and CPU timings.
- Deterministic smoke mode for pause, time factor, disconnect and reload.
- No changes to `PHJoint.cpp`, ODE movement, `CCharacterPhysicsSupport`,
  DX11/R4, DX12 or NPC callbacks.

## Build

### Bridge DLL

```powershell
cmake -S ogsr_engine/Layers/PhysicsPhysX -B <build-dir> `
      -G "Visual Studio 17 2022" -A x64 `
      -DPHYSX_ROOT=<physx-sdk>/physx
cmake --build <build-dir> --config Release
```

The target is `physics_physx_runtime`; it produces
`ogsr_physx_runtime.dll` and is compiled with the Release static runtime
(`/MT`) because the pinned PhysX libraries are `/MT` Release builds. It is a
separate module, so the `/MTd` Debug game can load it without mixing CRT
libraries.

Copy to the game `bin_x64` directory:

- `ogsr_physx_runtime.dll`
- `PhysX_64.dll`
- `PhysXCommon_64.dll`
- `PhysXFoundation_64.dll`
- `PhysXCooking_64.dll`

The PhysX DLLs are external runtime artifacts and are not tracked by the
repository.

### Game

`OgsrPhysXRuntime` defaults to `false`. The runtime build requires both
projects to receive the property; a project reference otherwise rebuilds
`xrGame` with defaults and silently drops the feature:

```powershell
msbuild ogsr_engine/xrGame/xrGame.vcxproj /t:Rebuild `
  /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=v143 `
  /p:OgsrPhysXRuntime=true /p:OgsrDiagnosticRuntimeWorkaround=true
msbuild ogsr_engine/xr_3da/XR_3DA.vcxproj /t:Rebuild `
  /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=v143 `
  /p:OgsrPhysXRuntime=true /p:OgsrDiagnosticRuntimeWorkaround=true
```

`OgsrDiagnosticRuntimeWorkaround=true` is only for the archive test runtime,
which has no usable main-menu UI factory. Normal builds leave it `false`.

## Runtime behavior

- Initialization is deferred until `Level().CurrentEntity()` exists.
- A diagnostic static fragment is extracted with `CObjectSpace::BoxQuery`
  around the actor (32 x 16 x 32 m box) and capped at 4096 triangles. The test
  map produced 627 triangles around `(256.24, 19.81, 550.82)`.
- One 0.5 m test cube with density 100 is spawned 2 m above the actor center.
- Fixed step is 0.01 s with at most 4 substeps per frame, gravity `-19.62`.
- `Device.fTimeDelta` is consumed by the context; no simulation runs while
  `Device.Paused()`.
- The scene is destroyed in `CLevel::net_Stop()` and recreated by the next
  level start.
- Nothing is rendered differently and no game transform is written.

## Runtime evidence

Log: `xray_gr_24-10-26_17-06-31.log` (Debug x64, DX11/R4 runtime).

| Check | Observed |
|---|---|
| Level start | `level context initialized center=(256.24,19.81,550.82) static_tris=627 source_tris=627` |
| Test body spawn | `test body at y=21.812 dynamic_bodies=1` |
| Settling | `y=20.061 contacts=1`; steps advanced 299 -> 2028 across the run |
| Fixed-step CPU | `avg_ms=0.21`–`0.32`, `max_ms=0.29`–`0.79` |
| Pause | `pause enabled` -> `paused; fixed step suspended` at 17:07:22.9; resume at 17:07:25.1. Steps stayed at the pre-pause count for the 2.2 s window |
| Time factor 0.5 | 105 steps in 2.08 s (~50 steps/s) while the 0.5 factor was active, versus ~99 steps/s before it |
| Time factor 1.0 | restored; normal step rate resumed |
| Unload | `requesting level disconnect` -> `- Disconnect` -> `level context destroyed` at 17:07:32.26 |
| Reload armed | `reload receiver armed` in the same disconnect frame |
| Reload | `reloading level` at 17:07:37.04 -> `archive runtime workaround deactivated the main menu` -> `level context initialized` with 627 triangles at 17:08:03.86 |
| Reloaded scene | steps advanced 760 -> 2028, `y=20.061`, `contacts=1`, process responsive |

The reload is exercised by `-physx_runtime_smoke`. It disconnects at frame 720.
The unload hook installs a one-shot frame receiver at priority
`REG_PRIORITY_HIGH + 500` and resorts `Device.seqFrame`, so the receiver runs
after the disconnect handler (which frees the level) and before
`CGamePersistent::OnFrame` in the same pass. The receiver then signals
`KERNEL:start` synchronously with the same server configuration. This avoids the
archive harness crash where `CGamePersistent::OnFrame` dereferences a null
`Level()` after a bare disconnect. It is workaround-only code; a production
runtime only needs the unload hook.

Reproduction:

```text
-multi_instances -start "start server(all/single/alife/new) client(localhost)" `
-physx_runtime -physx_runtime_smoke
```

## P2-off verification

- Default Debug and Release builds contain none of the strings
  `ph_runtime`, `ogsr_physx_runtime` or `ph_dump_ragdoll`; `dumpbin /dependents`
  reports no PhysX or bridge DLL imports.
- A P2-off build with the P1 collector (`OgsrDiagnosticCapture=true`,
  `OgsrDiagnosticRuntimeWorkaround=true`, `OgsrPhysXRuntime=false`) still
  captured a live ODE ragdoll:
  `ph_dump_ragdoll: ODE parity ... ok=1`, 47 bones, 11 bodies, 10 joints,
  health/alive `0/0`, shell `1`, 33,400-byte JSON. This is the unchanged ODE
  baseline.
- The P2 runtime sources are absent from the P2-off compile: the executable
  has no `OGSR_PHYSX_RUNTIME` symbols and no PhysX import table entries.

## Regression suites

| Suite | Result |
|---|---|
| `physics_portable_tests` | 49 checks, 0 failures |
| `physics_adapter_ogsr_tests` | 33 checks, 0 failures |
| legacy Euler axis test | 26 checks, 0 failures |
| base PhysX suite | 198 checks, 0 failures |
| PhysX suite with runtime context | 208 checks, 0 failures |

## Known limitations

- This is a diagnostic scene, not gameplay physics: no NPC, ragdoll, movement
  or rendering integration is claimed.
- The archive test runtime cannot use the normal main-menu UI factory. The
  menu/pause handoff and the smoke reload helper are compiled only with
  `OgsrDiagnosticRuntimeWorkaround=true`.
- The static fragment is a bounded box, not the whole level; triangle count is
  capped at 4096.
- The bridge DLL is loaded lazily on the first frame with an actor. If the DLL
  or one of the PhysX DLLs is missing, the game logs
  `cannot load ogsr_physx_runtime.dll` and continues with ODE only.
- PhysX DLLs are deployment artifacts. No SDK binary, private path or captured
  game JSON is part of this repository change.

## Rollback

Rebuild with `OgsrPhysXRuntime=false` (the default) and omit `-physx_runtime`.
No ODE, renderer or gameplay source depends on the new files at compile time,
so the baseline build remains the original path plus the Debug-only P1 hooks.
