# F4.5-A — ordered native UI command core and frame seam

Baseline 6ade437208413e91a3f3ae0c8beed82e5e44a362; IX-Ray comparison pinned to 7b74303ed0f2d93cf63b65983e3c819f323ebf5c.

IX-Ray evidence (source reviewed, no code copied):
- https://github.com/ixray-team/ixray-1.6-stcop/blob/7b74303ed0f2d93cf63b65983e3c819f323ebf5c/src/Include/xrRender/UIRender.h : separate L/TL/LIT, scissor getter, view/project, depth and topology. OGSR has TL/LIT and a different StartPrimitive ABI; no vtable changes.
- https://github.com/ixray-team/ixray-1.6-stcop/blob/7b74303ed0f2d93cf63b65983e3c819f323ebf5c/src/Layers/xrRender/dxUIRender.cpp : scissor copy, topology selection and depth state; strip primitive count subtracts 1/2 without guard. New core guards degenerate strips. IX-Ray is a reference for separation, not a DX12 implementation.
- https://github.com/ixray-team/ixray-1.6-stcop/blob/7b74303ed0f2d93cf63b65983e3c819f323ebf5c/src/Layers/xrRender/dxFontRender.cpp : separate font atlas creation and render path; dynamic atlas provider remains future work.
- Original OGSR interfaces and source maps in dx12-f45-hud-ui-architecture.md.
- Microsoft PSO and dynamic scissor/topology contracts: https://learn.microsoft.com/en-us/windows/win32/direct3d12/managing-graphics-pipeline-state-in-direct3d-12 and https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-rssetscissorrects .

Implemented:
- GameDx12UiCommands.h: owned vertices and ordered draw state, material ID/generation, screen/world domains, cull/depth/alpha/scissor/matrix, epoch/serial, checked ranges and finite vertices. Original order; no fixed scene quota; representability guard on 32-bit vertex addressing. Caller supplies separate vertex input, not a span aliasing the frame storage. Incomplete list tails are ignored as in legacy primitive counts; invalid state/range rejected. Allocation exceptions propagate to owner; draw allocation rollback. Material registry, GPU packing and full screen/world projection still pending. Frame is immutable by consumer convention; no sealing API yet.
- GameDx12NativeFrame12::BeginScreenOverlay: owner borrows actual active color list (including shadow-list handoff), rebinds output RTV with no DSV and full viewport/scissor. Requires restored RENDER_TARGET state, before Present/Cancel. Not invoked by runtime yet.
- CMainMenu::RenderMenuUiOnly: extracted original dialogs/font draw section; R4 OnRender still calls it after the same Calculate/Render. Does not select backend or enable native menu. No UI vtable or game input change.
- test-f45-ui-commands.cpp wired into existing Windows test runner.

Executed on Linux GCC C++20 with -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g: PASS (100003 ordered draws, 600019 vertices, clip/strip/identity/state/malformed validation). Exact tested blobs 2e0a6de7696af79e8f19075a7859b27d464928a9 and a9563cc1e08d486a745e46a33c3ed92297f05537. These are synthetic CPU inputs, not game UI draws. Windows build, native overlay hook, menu regression, WARP and GPU NOT RUN. Current runtime still lacks native UI producers, material registry/PSOs/upload, font capture, startup/loading dispatch and HUD3D. Next integrate original IUIRender/font producers with real material identity, actual DX12 draw recording and lifecycle; do not mark A complete until interactive game UI.
