# OGSR-Engine — документация форка

Документация написана по результатам полного разбора исходников
`OGSR/OGSR-Engine` и **перепроверена против ветки `main_cop_cs`
(Call of Pripyat)** на коммите `9d0021ce6`.

Все номера строк в документах проверены файл за файлом против кода.
Пометки ⚠️ markют места, где наша SoC-оценка была неверна или устарела.

| Документ | О чём |
|---|---|
| [01-architecture.md](01-architecture.md) | Архитектура: модули, слои, два Lua VM, точки входа. Включает `xrPhysics` и `xrNetServer` |
| [02-renderer.md](02-renderer.md) | Рендерер: DX11-устройство, кадр (13 стадий), G-buffer **с нормалями**, шейдеры |
| [03-build-and-dependencies.md](03-build-and-dependencies.md) | Сборка, MSBuild, 22 папки 3rd_party, 12 граблей сборки, ATL-обход, CI |
| [04-scripting-and-game.md](04-scripting-and-game.md) | Lua/Luabind, `xrServerEntities`, игровые объекты, AI, UI, **DLSS-опции в Lua** |
| [05-upscaling-current-state.md](05-upscaling-current-state.md) | DLSS и FSR 3 построчно, с цитатами кода |
| [06-rtx-feasibility.md](06-rtx-feasibility.md) | **Главный документ: что можно и что нельзя сделать с RTX в DX11** |
| [07-known-issues.md](07-known-issues.md) | Найденные проблемы (A–H) + «что проверено и не проблема» |
| [08-roadmap.md](08-roadmap.md) | Этапы A–E с критериями приёмки и откатом |
| [09-dx12-port-references.md](09-dx12-port-references.md) | Официальные ресурсы DX12/DXR и привязка к этапам дорожной карты |

## TL;DR по RTX

Движок — **чистый DirectX 11**. Аппаратный ray tracing (DXR/RT cores) в D3D11
не существует в принципе, поэтому «просто включить RTX» невозможно без порта
рендерера на DirectX 12. Порт — это по сути новый рендерер (424 HLSL-файла,
`CBackend`, `StateManager`, FidelityFX DX12 backend).

При этом в SDK NVIDIA DLSS **310.9.1** присутствуют точки входа `NVSDK_NGX_D3D11_*`
для семейства DLSS-G/RR, а официальный слой интеграции **StreamLine требует
строго D3D12 или Vulkan**.

Что реально доступно уже сегодня и не требует DX12:

1. **✅ Обновить DLSS SDK `310.7.0 → 310.9.1`** — выполнено,
   совместимость проверена (42/42 символа, enum'ы не перенумерованы),
   сборка подтверждена
2. **Включить NVIDIA Image Scaling (NIS)** — официально поддерживает DX11,
   уже лежит в дереве как submodule, но `Update_Components.cmd` его не тянет
3. **Реализовать «RT-look» средствами DX11** — Hi-Z + compute (`cs_5_0`) + DDA.
   Инфраструктура есть, и в отличие от SoC-оценки **нормали в G-buffer уже есть**
   (`rt_Position.xy`), gloss — в `rt_Color.a`
4. **Тюнинг текущего DLSS** — три закомментированных флага
   (`DoSharpening`, `MVJittered`, `AlphaUpscaling`)

## Главное исправление против SoC-версии документации

Раньше мы утверждали, что в G-buffer нет нормалей и там
`R16G16B16A16_FLOAT` позиция в пространстве глаза.
**Это было неверно** — `common_iostructs.h:287-293`:

```hlsl
struct f_deffer
{
    float4 position : SV_Target0; // xy=encoded normal, z = pz, w = encoded(m-id,hemi)
    float4 C : SV_Target1;        // r, g, b,  gloss
};
```

Нормали есть (закодированы), gloss есть. Для трассировки это существенно снижает
входной план: декодировать, а не городить новый MRT.

## Метаданные

- **Апстрим:** `https://github.com/OGSR/OGSR-Engine`
- **Форк:** `https://github.com/rafaelisraelyan/OGSR-Engine-rtx` (public)
- **Рабочая ветка для CoP:** `prypiat` (от `main_cop_cs`, default branch форка)
- **Базовый коммит:** `9d0021ce6` («Merge branch 'main' into main_cop_cs»)
- **Флаг стандарта:** ветка `main` (SoC) — старая база с другой документацией
- **Стандарт C++:** `stdcpp20` безусловно (`OgsrBuildProps.props:28`)
- **Проектов в решении:** 26 (на SoC было 23)
- **Лицензия:** модифицированная BSD-3 с некоммерческим ограничением.
  Компоненты сторонние — с отдельными лицензиями
  (LGPL-2 у ODE/OpenAL, MIT у ImGui/FidelityFX/RenderDoc, BSD у Xiph)