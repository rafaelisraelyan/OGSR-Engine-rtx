# F4.3-CSM-R2 — GPU resource and shadow sampling audit

26.11.2026 MSK. Проверен baseline `55023575828fb74cd174a20c6a05338777717b1e`.

## Findings

- Солнечная карта создаётся как `R32_TYPELESS`, `D32_FLOAT` DSV, один mip и массив `m_sunShadowSlices` от 1 до 4. Каждый DSV задаёт `FirstArraySlice=slice`, `ArraySize=1`.
- Local карта создаётся как `R32_TYPELESS`, `D32_FLOAT` DSV, один mip и 8 array layers. DSV loop аналогично выбирает ровно один layer.
- SRV layout использует `R32_FLOAT`, `TEXTURE2DARRAY`, `MipLevels=1`. Sun SRV занимает descriptor `kSunShadowDescriptor` и register `t2`, local SRV занимает `kLocalShadowDescriptor` и register `t6`. Root signature parameters 6 и 9 имеют по одному SRV descriptor.
- SceneFrameConstants содержит четыре sun matrices и восемь local matrices. `kFrameConstantsSize` равен 2304, что сохраняет 256-byte CBV alignment. Shadow layer root constant использует `b6` для vertex projection selection.
- `RecordNativeFrame` переводит активные slices по subresource index, записывает DSV, затем переводит каждый slice в `PIXEL_SHADER_RESOURCE`. Цветной проход получает SRV только после этого перехода. Native frame submits начало, все shadow lists и color list одним `ExecuteCommandLists`, затем сигналит общий fence. Slot allocator reset происходит только после ожидания slot fence.

## Focused fixture

`tools/dx12/test-f43-csm-warp.cpp` компилирует production-shaped sampling PS с извлечёнными из embedded production HLSL функциями и регистрами `t2` и `t6`, SRV dimension `TEXTURE2DARRAY` и `R32_FLOAT`. После DSV clear и depth-to-SRV barriers fixture выполняет sampling через `SelectSun`, `SunFilterVisibility` и `SampleLocalVisibility`, затем проверяет различимые per-slice значения, overlap/inactive cascade selection и local layers. Проверяются конфигурации sun slices 1, 2, 3, 4 и local 8, а также `local=0`.

## Evidence

Команда сборки fixture с production `GameDx12NativeFrame.cpp`, Debug Layer и GBV завершилась успешно. Запуск WARP завершился:

```
PASS: cascades=1 local=8
PASS: cascades=2 local=0
PASS: cascades=3 local=8
PASS: cascades=4 local=8
PASS: WARP Debug Layer + GBV, sun/local slices, readback parity, resize, cancel, GR fault drain
```

InfoQueue содержит только ожидаемые `D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE` ID 821, потому что fixture намеренно использует различимые clear values для разных slices. Других Warning/Error/Corruption нет. Fixture создаёт собственный test root signature/PSO, поэтому evidence подтверждает production-shaped HLSL/register/resource sampling, а не полный production PSO/draw path. Существующие color/depth readbacks и legacy/multi/parallel parity сохранены. Для линковки sampling fixture в launcher добавлен `d3dcompiler.lib`.

## References

- `ogsr_engine/xr_3da/GameDx12StaticScene.cpp`
- `ogsr_engine/xr_3da/GameDx12StaticScene.h`
- `ogsr_engine/xr_3da/GameDx12NativeFrame.cpp`
- `ogsr_engine/xr_3da/GameDx12NativeFrame.h`
- `ogsr_engine/xr_3da/GameDx12ShadowViews.h`
- `tools/dx12/test-f43-csm-warp.cpp`
- `tools/dx12/test-f43-csm.ps1`
- `docs/dx12-f43-csm-gr-audit.md`
- Microsoft D3D12 resource barriers, descriptor views, command-list submission and allocator-fence lifetime guidance

Итог: PASS для resource format/layout, array slices 0–3 и local 0/8, production-shaped HLSL/register sampling, barriers and fence lifetime under WARP Debug Layer + GBV. Это не заменяет production PSO/root-signature verification или runtime NVIDIA sampling, которые остаются отдельным owner gate.
