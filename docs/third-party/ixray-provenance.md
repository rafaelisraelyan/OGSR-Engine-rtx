# Происхождение решений IX-Ray в F4.3‑E

Источник — [IX-Ray 1.6 STCoP](https://github.com/ixray-team/ixray-1.6-stcop/tree/7b74303ed0f2d93cf63b65983e3c819f323ebf5c). Полный [текст лицензии](../../LICENSES/IX-Ray-LICENSE.md) получен с того же коммита. Совместимость форматов игровых данных определяется исходниками OGSR/R4.

| Наш файл | Исходный файл IX-Ray | Source SHA | Тип адаптации | Лицензия |
| --- | --- | --- | --- | --- |
| GameDx12StaticScene.cpp | gamedata/shaders/d3d11/effects_wallmark.lua, effects_wallmarkblend.lua, effects_wallmarkmult.lua, effects_wallmarkset.lua | 7b74303ed0f2d93cf63b65983e3c819f323ebf5c | reference only. Освещение и смешивание накладок | IX-Ray |
| GameDx12StaticScene.cpp | gamedata/shaders/d3d11/shadow_base.ps.hlsl, shadow_model.vs.hlsl | 7b74303ed0f2d93cf63b65983e3c819f323ebf5c | reference only. Clip базовой текстуры и skinning в теневом проходе | IX-Ray |
| GameDx12StaticScene.cpp | gamedata/shaders/d3d11/shared/watermove.hlsli, shared/waterconfig.hlsli | 7b74303ed0f2d93cf63b65983e3c819f323ebf5c | Адаптация формул высоты волны и двух UV-смещений, также присутствующих в OGSR R4 | IX-Ray |
| GameDx12StaticScene.cpp | gamedata/shaders/d3d11/water.vs.hlsl, water.ps.hlsl, effects_water.lua | 7b74303ed0f2d93cf63b65983e3c819f323ebf5c | reference only. Две выборки normal map, tangent-space normal, Fresnel и soft-water. Реконструкция глубины, SSR и композиция реализованы для камеры и форматов OGSR DX12 | IX-Ray |
| r4_loader.cpp, GameDx12StaticScene.cpp | src/Layers/xrRenderPC_R4/r4_loader.cpp, r4_rendertarget_phase_puddles.cpp; gamedata/shaders/d3d11/water_puddles.vs.hlsl | 7b74303ed0f2d93cf63b65983e3c819f323ebf5c | Адаптация шести процедурных вершин и смещения по влажности. Формат разбирает существующий загрузчик OGSR | IX-Ray |
| GameDx12StaticScene.cpp, Light_DB.cpp | src/Layers/xrRenderPC_R4/r4_rendertarget_accum_spot.cpp, r4_rendertarget_accum_point.cpp, r4_rendertarget_phase_smap_S.cpp, r4_rendertarget_phase_smap_D.cpp | 7b74303ed0f2d93cf63b65983e3c819f323ebf5c | reference only. Проекции, проверка флагов и выборка карты. DX12 хранит отдельные слои глубины | IX-Ray |
| GameDx12Bridge.cpp, GameDx12StaticScene.cpp | src/Layers/xrRender/R_Backend_hemi.cpp, LightTrack.cpp | 7b74303ed0f2d93cf63b65983e3c819f323ebf5c | reference only. Разделение vertex hemi, lightmap hemi и динамического трекинга | IX-Ray |

## F4.3‑F

Дополнительные исходники сверены по тому же SHA. Для коррекций F4.3‑F они использованы как технический референс. Контракты игровых данных и численные параметры взяты из OGSR/R4, адаптация выполнена для существующих DX12 проходов.

| Наш код | Исходники IX-Ray | Использование |
| --- | --- | --- |
| GameDx12Bridge.cpp, Light_DB.cpp | src/Layers/xrRender/light_vis.cpp, light.cpp, Light_Package.cpp, lights_render.cpp; src/Layers/xrRenderPC_R4/r4_R_render.cpp | Камера внутри объёма, отделение кандидатов света от теневых слотов |
| GameDx12StaticScene.cpp | src/Layers/xrRender/R_sun.cpp, r_sun_cascades.h, lights_render.cpp; src/Layers/xrRenderPC_R4/r4_rendertarget_phase_smap_S.cpp; gamedata/shaders/d3d11/shadow.hlsli | Пространства проекции и выборки, плотность солнечной карты, PCF |
| GameDx12StaticScene.cpp | gamedata/shaders/d3d11/shared/watermove.hlsli, shared/waterconfig.hlsli, water.vs.hlsl, water.ps.hlsl | Проверка фазы, координат анимированных нормалей и отражения |
| GameDx12StaticScene.cpp, ResourceManager.cpp | gamedata/shaders/d3d11/deffer_base.vs.hlsl, deffer_base.ps.hlsl, deffer_detail.vs.hlsl; src/Layers/xrRender/TextureDescrManager.cpp, Blender_Recorder_R2.cpp, DetailManager.cpp, LightTrack.cpp | Разделение detail, lightmap и динамического hemi. Конкретные слои и масштаб использованы из OGSR |

Бинарные игровые ассеты IX-Ray в рабочее дерево не добавлялись. Сторонний блок caustics и его авторские фрагменты из водного шейдера не переносились. Использование DX12 описано в локальном отчёте F4.3‑E.
