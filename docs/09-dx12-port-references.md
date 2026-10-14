# 09 — Ресурсы для порта на DirectX 12 / DXR

Список официальных источников и их привязка к этапам дорожной карты.

## Официальные ресурсы

| Ресурс | URL | Что даёт |
|---|---|---|
| **Direct3D 12 Programming Guide** | <https://learn.microsoft.com/en-us/windows/win32/direct3d12/directx-12-programming-guide> | Архитектура API: root signatures, descriptor heaps, command lists/queues, resource barriers, PSO, memory management, synchronization |
| **DirectX-Graphics-Samples** | <https://github.com/microsoft/DirectX-Graphics-Samples> | Рабочие примеры: `HelloTriangle`, `D3D12HelloWorld`, `D3D12Multithreading`, `VariableRateShading`, `MeshShaders`, `DirectML` |
| **D3D12Raytracing Samples** | <https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12Raytracing> | BLAS/TLAS построение, `TraceRay`, `RayQuery` (inline RT), Shader Binding Table, `RaytracingPipelineState` |
| **DirectXShaderCompiler (DXC)** | <https://github.com/microsoft/DirectXShaderCompiler> | Компиляция в **DXIL** (SM 6.x): `vs_6_0`/`ps_6_0`/`cs_6_0`/`mesh_6_0`/`raygeneration_6_0`/`intersection_6_0`/`anyhit_6_0`/`closesthit_6_0`/`miss_6_0`/`callable_6_0` |

---

## Привязка к этапам (из 08-roadmap.md)

| Этап | Ресурсы | Конкретные примеры / разделы |
|---|---|---|
| **B — NIS** | — | Чистый DX11 compute, ничего из списка не нужно |
| **C — DLSS тюнинг** | — | DX11 |
| **D0–D2 — SSGI на compute (Hi-Z + DDA)** | DXC, DirectX-Graphics-Samples | `D3D12Raytracing` → **Inline Raytracing (`RayQuery`)** для трассировки по Hi-Z без полного pipeline. Прототип на `cs_5_0` + ручной DDA; `RayQuery` даст аппаратное ускорение на RT cores |
| **D3–D6 — Геометрия, тени, денойз, интеграция** | D3D12Raytracing, DXC | `D3D12Raytracing` → BLAS/TLAS (`BuildRaytracingAccelerationStructure`), Shader Binding Table, `TraceRay` в `raygeneration`/`closesthit`/`miss`, `DispatchRays` |
| **Полный DX12 порт рендерера** | Всё сразу | - `CHW` → `ID3D12Device` + `ID3D12CommandQueue` + `IDXGISwapChain4`<br>- `CBackend` → root signatures, descriptor tables (CBV/SRV/UAV), PSO cache<br>- `dx10StateManager` → explicit barriers (`D3D12_RESOURCE_BARRIER`)<br>- `dx10ConstantBuffer` → descriptor heaps + `SetGraphicsRootConstantBufferView`<br>- 424 HLSL → `vs_6_0`/`ps_6_0`/`cs_6_0`/`raygeneration_6_0` через **DXC** (`dxcompiler.lib`, `dxil.dll`)<br>- Линковка: `d3d12.lib`, `dxgi.lib`, `dxcompiler.lib`, `d3dcompiler.lib` (fallback) |

---

## Ключевые различия D3D11 → D3D12 для нашего кода

| Подсистема | D3D11 (сейчас) | D3D12 (порт) |
|---|---|---|
| Устройство | `D3D11CreateDevice` → `ID3D11Device` | `D3D12CreateDevice` → `ID3D12Device` + `ID3D12CommandQueue` (Direct/Compute/Copy) |
| Контекст | `ID3D11DeviceContext` (deferred/immediate) | `ID3D12GraphicsCommandList` (записывается, потом `ExecuteCommandLists`) |
| Состояния | `dx10StateManager` кэширует `ID3D11BlendState`/`DepthStencilState`/`RasterizerState` | **PSO** = monolithic state object (все состояния в одном `ID3D12PipelineState`) |
| Константы | `dx10ConstantBuffer` → `ID3D11Buffer` + `VSSetConstantBuffers` | **Descriptor Heaps** (`CBV_SRV_UAV`) + `SetGraphicsRootConstantBufferView` / `SetGraphicsRootDescriptorTable` |
| Ресурсы | `ID3D11Buffer`/`Texture2D` + `Map`/`Unmap` | **Resource heaps** + explicit transitions (`D3D12_RESOURCE_BARRIER`) |
| Шейдеры | `D3DCompile` → `fxc` (SM 5.0) | **DXC** (`dxcompiler.lib`) → DXIL (SM 6.x), `lib_6_3` для библиотек |
| Swapchain | `IDXGISwapChain` (FLIP_DISCARD) | `IDXGISwapChain4` + `Present1` + `D3D12_RESOURCE_BARRIER` для `PRESENT` → `RENDER_TARGET` |
| Синхронизация | Неявная (драйвер) | **Fences** (`ID3D12Fence`) + `Wait`/`Signal` на CPU/GPU |

---

## Минимальный набор для старта DX12-прототипа

1. Создать отдельный `.vcxproj` (например `xrRenderDX12`) рядом с `xrRenderDX10`
2. Добавить `d3d12.lib`, `dxgi.lib`, `dxcompiler.lib` в `AdditionalDependencies`
3. Подключить `DirectXShaderCompiler` (NuGet `Microsoft.DirectX.DXC` или самобранный `dxcompiler.lib` + `dxil.dll`)
4. Написать минимальный `CHW12` по образцу `dx10HW.cpp`:
   - `D3D12CreateDevice` + `CreateCommandQueue` (Direct + Compute)
   - `CreateSwapChainForHwnd` → `IDXGISwapChain4`
   - Frame resources: `N` командных аллокаторов + листов + fences
5. Один треугольник через PSO + root signature → проверить, что кадр рисуется
6. Потом — мигрировать `CBackend` → root signature + descriptor tables
7. Потом — шейдерный компилятор `r4.cpp:674-906` → DXC API (`IDxcCompiler3::Compile`)

---

## Когда начинать

**Только после D0–D2**. Если compute-трассировка на DX11 (`cs_5_0` + DDA) покажет:
- стабильные 30+ FPS на 1440p
- качество GI близкое к референсу
- отсутствие артефактов на движущихся объектах

— тогда имеет смысл вкладываться в DX12 для аппаратного `RayQuery`/`TraceRay` и полного pipeline. До этого — риск потратить месяцы на порт, который не даст визуального прироста без DXR.