# F2.0 — DX12 Build & Entry Point

## Статус

**PARTIAL** — build integration создана, но сборка не проверена (нет MSBuild
в среде выполнения). DX12 backend не реализован — только минимальная
инфраструктура.

## Структура новых проектов

```
ogsr_engine/Layers/xrRenderDX12/
  CHW12.h          — минимальный интерфейс (пустой класс)
  CHW12.cpp        — пустая реализация

ogsr_engine/Layers/xrRenderPC_R5_DX12/
  xrRender_R5_DX12.vcxproj  — новый проект (StaticLib)
```

## Как работает выбор DX11/DX12

```cpp
// Device_create.cpp (существующий, не изменён)
m_pRender = RenderFactory->CreateRenderDeviceRender();
```

Для DX12 будет добавлена альтернативная фабрика:

```cpp
// Будущее (F2.1+):
#if defined(OGSR_USE_DX12)
    m_pRender = RenderFactory12->CreateRenderDeviceRender();
#else
    m_pRender = RenderFactory->CreateRenderDeviceRender();
#endif
```

**На F2.0 это НЕ реализовано** — только build-time конфигурация.

## Global symbols

| Символ | Библиотека | Конфликт? |
|---|---|---|
| `HW` | `xrRender_R4.lib` | Нет — DX12 использует `HW12` |
| `RImplementation` | `xrRender_R4.lib` | Нет — DX12 использует `R5` |
| `RenderFactoryImpl` | `xrRender_R4.lib` | Нет — DX12 использует `RenderFactory12Impl` |

## Библиотеки

| Проект | Линкуется в |
|---|---|
| `xrRender_R4.lib` | `xrEngine.exe` (Release) |
| `xrRender_R5_DX12.lib` | `xrEngine.exe` (ReleaseDX12) — будущее |

## Как собрать

```cmd
:: DX11 (default, рабочий)
msbuild Engine.sln /p:Configuration=Release /p:Platform=x64

:: DX12 (экспериментальный, F2.0)
msbuild Engine.sln /p:Configuration=ReleaseDX12 /p:Platform=x64
```

## Что НЕ реализовано

- `D3D12CreateDevice` — F2.1
- DXGI swapchain — F2.1
- GPU command queue — F2.1
- Descriptor heaps — F2.1
- Command recording — F2.1
- `ClearRenderTargetView` — F2.1
- `Present` — F2.1
- Полноценный `CRender12` — F4
- `dxRenderDeviceRender12` — F2.1

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
