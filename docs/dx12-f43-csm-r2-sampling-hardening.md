# CSM R2 C — sampling fixture hardening

Baseline: b5eb1a49f5d61b1db4e50e0ce5b0a71b14f9e55f. **Source-only candidate; Windows MSVC/FXC/WARP execution pending.** Earlier PASS reports are historical, not evidence for this revision.

The fixture uses the extracted production SceneFrameConstants and kFrameConstantsSize and reflects FrameCB offsets/sizes. Its probe entry point and PSO remain test-specific. It uses production NativeFrame (PCH replacement) and extracted HLSL.

Control runs modes 1–7 across 1/2/3/4 sun cascades and local 0/8, legacy/multi/parallel (unless sequential-only). It checks primary selection, overlap, next slice, blend/fade, inactive receivers, local layer visibility and spatial PCF. Independent CPU oracle integrates point comparisons and bilinear weights with white borders and an analytical nonzero receiver-plane gradient. Patterned depth slices use valid clear rectangles; all 8x8 depth texels and all probe RGBA pixels enter the readback parity vector. One-LSB UNORM tolerance against reference, exact encoded cascade indices; parity across modes is exact on WARP. Seven control frames plus the observed failing callback change GR counters to 8/8.

Six negative runs compile modified copies of the same extracted HLSL (force actual sampled sun slice 0, swap actual sampled slices 1/2, reverse manual comparison, wrong nextSlice, disable blending, wrong local layer) and execute the same draw/readback/oracle. Only a mismatch in the intended category counts as a killed mutation; shader compile, PSO, GPU/fence and depth readback errors fail the test. The original source is unchanged. InfoQueue requires complete reads, no denied/retrieval-hidden/discarded messages, and only accepts the named clear-value warning at Warning severity. Partial depth clears deliberately use differing values; expected warning count is reported, not suppressed.

Limitations: no production scene VS/PSO/descriptor heap or real geometry draw, no derivative-driven plane normal validation, no hardware NVIDIA execution, no full local projection/atlas test. The local sample helper is tested on uniform 8x8 maps at interior UV, not its 1024-resolution edge behavior. The fixture is a regression test, not proof of the original Jupiter checkerboard fix. Record CSM math and WARP logs, actual mutation outcomes and build SHA after Windows execution.

References: Microsoft ClearDepthStencilView, D3DReflect, ID3D12ShaderReflection, InfoQueue GetMessage and count/retrieval filter documentation.

## Executed portable CPU reference check

`g++ -std=c++20 -Wall -Wextra -Werror -pedantic tools/dx12/test-f43-csm-sampling-reference.cpp -o csm-ref && ./csm-ref`

PASS: 50986 numerical checks; 2 gradient-sensitive probes. Header Git blob: `411a6a111691eabe5a5d8d574166e5e708e0cf71`. Test source blob: `54be142a7db8d28fac081a3dc8635e353dd983a1`. Of the counted checks, 42250 compare the independent CPU bilinear integrator to a four-tap implementation; 8736 check finite normalized expected output channels. Additional assertions require a nonzero gradient effect and two expected overlap/fade values. These tests run on Linux, not on WARP, and are not GPU mutation execution evidence.
