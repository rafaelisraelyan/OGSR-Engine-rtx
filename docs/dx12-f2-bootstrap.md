# F2.0 — DX12 Build & Entry Point

## Статус

**PARTIAL** — build scaffold создан и собирается. Полноценный DX12 backend
не реализован (только минимальный `CHW12` без `D3D12CreateDevice`).

## Структура новых проектов

```
ogsr_engine/Layers/xrRenderDX12/
  CHW12.h          — минимальный интерфейс (пустой класс)
  CHW12.cpp        — пустая реализация
  stdafx.h         — d3d12.h, dxgi1_4.h, d3dcompiler.h

ogsr_engine/Layers/xrRenderPC_R5_DX12/
  xrRender_R5_DX12.vcxproj  — StaticLibrary (ReleaseDX12|x64)
```

## GUID проекта

`{D7F3A2B1-8C4E-4F6A-9D2B-5E1C3A7F8B6D}`

## ReleaseDX12 mapping

| Проект | ActiveCfg | Build.0 |
|---|---|---|
| `xrRender_R5_DX12` | `ReleaseDX12\|x64` | `ReleaseDX12\|x64` |
| Вспомогательные библиотеки (xrCore, xrCDB, и т.д.) | `Release\|x64` | `Release\|x64` |
| `XR_3DA` (EXE) | `Release\|x64` | **нет** |
| `xrRender_R4` (DX11 renderer) | `Release\|x64` | **нет** |

## Результаты сборки

```
msbuild Engine.sln /p:Configuration=ReleaseDX12 /p:Platform=x64
ExitCode: 0
Warnings: MSB4121 (ожидаемы — проекты без ReleaseDX12 mapping)
```

## Что НЕ реализовано (F2.1+)

- `D3D12CreateDevice` — F2.1
- DXGI swapchain — F2.1
- GPU command queue — F2.1
- Descriptor heaps — F2.1
- Command recording — F2.1
- `ClearRenderTargetView` — F2.1
- `Present` — F2.1
- `dxRenderDeviceRender12` — F2.1
- Полноценный `CRender12` — F4

## План F2.1

1. `CHW12::Initialize()` — `D3D12CreateDevice`, `CreateCommandQueue`,
   `CreateSwapChainForHwnd`, `CreateDescriptorHeaps`, `CreateFence`
2. `CHW12::Present()` — `Present1`, `MoveToNextFrame`, `WaitForFence`
3. `dxRenderDeviceRender12` — обёртка вокруг `CHW12`
4. Первый кадр: `Clear → Present` без геометрии

## Критерии приёмки F2.1

- [ ] `D3D12CreateDevice` возвращает валидный `ID3D12Device*`
- [ ] Swapchain создаётся с 3 backbuffers
- [ ] `ClearRenderTargetView` + `Present` показывают цветной кадр
- [ ] Resize работает без validation errors
- [ ] Device removed обрабатывается корректно
- [ ] DX11 baseline остаётся рабочим
