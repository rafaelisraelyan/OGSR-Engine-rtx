# OGSR-Engine — документация форка

Документация написана по результатам полного разбора исходников `OGSR/OGSR-Engine`
(ветка `main`, commit `0a88379c8`).

| Документ | О чём |
|---|---|
| [01-architecture.md](01-architecture.md) | Общая архитектура: модули, слои, зависимости, точки входа |
| [02-renderer.md](02-renderer.md) | Рендерер: DX11-устройство, кадр, render stages, G-buffer, шейдеры |
| [03-build-and-dependencies.md](03-build-and-dependencies.md) | Сборка, MSBuild, 3rd_party, CI, грабли при билде |
| [04-scripting-and-game.md](04-scripting-and-game.md) | Lua/Luabind, игровые объекты, AI, UI, Game/ |
| [05-upscaling-current-state.md](05-upscaling-current-state.md) | Как устроены DLSS и FSR 3 сейчас, точкой интеграции |
| [06-rtx-feasibility.md](06-rtx-feasibility.md) | **Главный документ: что можно и что нельзя сделать с RTX в DX11** |
| [07-known-issues.md](07-known-issues.md) | Найденные проблемы и техдолг |
| [08-roadmap.md](08-roadmap.md) | План работ |

## TL;DR по RTX

Движок — **чистый DirectX 11**. Аппаратный ray tracing (DXR/RT cores) в D3D11
не существует в принципе, поэтому «просто включить RTX» невозможно без порта
рендерера на DirectX 12.

При этом в SDK NVIDIA DLSS **310.9.1** присутствуют точки входа `NVSDK_NGX_D3D11_*`
для семейства DLSS-G/RR, а официальный слой интеграции **StreamLine требует
строго D3D12 или Vulkan**.

Что реально доступно уже сегодня и не требует DX12 — разобрано в
[06-rtx-feasibility.md](06-rtx-feasibility.md). Коротко:

1. Обновить DLSS SDK `310.7.0 → 310.9.1` — низкий риск, обратная совместимость подтверждена.
2. Включить NVIDIA Image Scaling (NIS) — официально поддерживает DX11, уже лежит в дереве как submodule.
3. Реализовать «RT-look» средствами DX11: Hi-Z + compute-шейдеры (`cs_5_0`) + DDA/raymarching. Инфраструктура в движке есть, но не используется.

## Метаданные

- **Апстрим:** `https://github.com/OGSR/OGSR-Engine`
- **Форк:** `https://github.com/rafaelisraelyan/OGSR-Engine-rtx` (public)
- **Ветка:** `main`
- **Лицензия:** модифицированная BSD-3 с некоммерческим ограничением (см. [LICENSE.md](../LICENSE.md)).
  Компоненты сторонние — с отдельными лицензиями (LGPL-2 у ODE/OpenAL, MIT у ImGui/FidelityFX/RenderDoc, BSD у Xiph).