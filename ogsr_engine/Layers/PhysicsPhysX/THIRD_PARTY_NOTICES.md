# Third-party notices — NVIDIA PhysX

P1.0/P1.1 uses the NVIDIA PhysX SDK, tag `107.3-physx-5.6.1`, commit
`5ca9f472105a90d70d957c243cb0ef36fe251a9f`.

No PhysX source or binaries are committed to this repository. A local SDK build
is referenced through `PHYSX_ROOT`.

## Linked components

CPU-only, Windows x64, Release:

- `PhysX_64.dll` / `PhysX_64.lib`
- `PhysXCommon_64.dll` / `PhysXCommon_64.lib`
- `PhysXFoundation_64.dll` / `PhysXFoundation_64.lib`
- `PhysXExtensions_static_64.lib`
- `PhysXCooking_64.dll` / `PhysXCooking_64.lib` (imported only because
  `PhysXExtensions_static` references cooking symbols)
- `PhysXPvdSDK_static_64.lib`
- `PhysXTask_static_64.lib`

No CUDA, GPU, DX11, DX12, Flow, Blast, vehicle or snippet components are linked
by this module.

## License

BSD 3-Clause License

Copyright (c) 2008-2025, NVIDIA Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of NVIDIA CORPORATION nor the names of its contributors may
  be used to endorse or promote products derived from this software without
  specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Distribution strategy

- Keep the SDK outside the source tree.
- Ship exactly the DLLs above with the game build; do not ship debug PDBs or
  unneeded SDK components.
- Re-run `dumpbin /dependents` on the final executable and update this notice
  if the linked component set changes.
- Confirm the final product's third-party notices with the project owner before
  redistribution.
