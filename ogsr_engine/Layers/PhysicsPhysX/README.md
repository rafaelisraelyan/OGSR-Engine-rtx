# PhysicsPhysX — P1.0 portable ragdoll foundation

This directory contains the first portable PhysX CPU ragdoll slice. It is intentionally
not linked into `xrEngine`, `xrPhysics`, `CPHWorld`, `CPHIsland` or either renderer.

## SDK pin

- Project: NVIDIA Omniverse PhysX
- Tag: `107.3-physx-5.6.1`
- Commit: `5ca9f472105a90d70d957c243cb0ef36fe251a9f`
- Platform: Windows x64, CPU-only
- No DX11/DX12, CUDA or GPU components are linked.

License: BSD 3-Clause (`LICENSE.md` in the SDK). This repository does not vendor
PhysX binaries. Point `PHYSX_ROOT` at a locally built SDK.

## Build the SDK

```cmd
git clone --branch 107.3-physx-5.6.1 --depth 1 https://github.com/NVIDIA-Omniverse/PhysX.git physx
cd physx\physx
generate_projects.bat vc17win64-cpu-only
msbuild compiler\vc17win64-cpu-only\sdk_source_bin\PhysX.sln /m /p:Configuration=release /p:Platform=x64
```

The resulting DLL/import libraries are under:

```text
physx\bin\win.x86_64.vc143.mt\release
```

## Build and test this module

```cmd
cmake -S ogsr_engine/Layers/PhysicsPhysX -B build/physics_physx ^
  -G "Visual Studio 17 2022" -A x64 ^
  -DPHYSX_ROOT=C:/path/to/physx/physx
cmake --build build/physics_physx --config Release --parallel
ctest --test-dir build/physics_physx -C Release --output-on-failure
```

The test executable copies `PhysX_64.dll`, `PhysXCommon_64.dll` and
`PhysXFoundation_64.dll` beside itself.

## Design invariants

- `physics_portable` contains no OGSR, ODE, DirectX or PhysX types.
- `physics_portable/json.h` loads/writes the diagnostic capture format; the
  loader validates the schema and the portable contract and reports JSON paths.
- Contract transforms are column-vector: `p' = R * p + t`.
- `RagdollPose::bodyWorld` is the **bone frame**, not the center of mass.
- PhysX actor frame is the bone frame; COM is set through `setCMassLocalPose`.
- `rootToEntity` follows `entity = rootBody * rootToEntity`, therefore
  `rootToEntity = inverse(rootBody) * entity`.
- `RagdollShapeDesc` cylinder axis is local **+X**. The OGSR adapter must rotate
  the source cylinder's local +Y axis to +X.
- Geometric cylinder margin is `0` for OGSR parity. `PxShape::setContactOffset` is
  a separate contact-generation parameter.
- D6 `Auto` chooses pyramid swing when both swing axes are limited, or when one is
  limited and the other is locked. Cone is used otherwise. This remains an
  approximation of ODE `dAMotorEuler`; exact parity needs a custom constraint.
- Fixed timestep defaults to `0.01 s` and gravity to `-19.62 m/s²` to match OGSR.

## Real capture fixture

The synthetic humanoid and pendulum remain technical P1 fixtures. The optional
`OGSR_RAGDOLL_FIXTURE=<path>` test additionally loads a real Zaton story-stalker
capture (47 bones, 11 bodies, 10 joints), creates it in PhysX, checks the initial
body and bone-space readback, steps 120 fixed ticks, and verifies all allocations
are released. The fixture itself is intentionally kept outside the repository
because it is derived from installed game assets.

The real OGSR collector and ODE parity procedure are specified in
`../PhysicsAdapterOGSR/README.md`.

PhysX Foundation is a process singleton; only one `PhysXRagdollWorld` can be
created per process in this implementation. The task's repeated create/destroy
path is tested; concurrent worlds are not supported.
