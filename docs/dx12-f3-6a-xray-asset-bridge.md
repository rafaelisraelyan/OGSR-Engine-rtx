# F3.6A — X-Ray Asset Bridge

**Статус: implemented and locally verified. Остановлено для независимого
ревью. Commit/push не выполнялись.**

## 1. Implemented

- Реальный SoC static asset `meshes\objects\dynamics\plitka\plitka1.ogf`
  загружается из внешнего loose-файла.
- Реальная DDS `textures\ston\ston_stena_marbl_m_03_back.dds` загружается из
  внешнего loose-файла.
- CPU loader `dx12xrayasset.cpp` проверяет OGF subset: `MT_NORMAL`, format 4,
  FVF `0x112`, inline vertices/indices, uint16 indices.
- Проверяются chunk sizes, overflow, finite values, нормали, counts, bounds и
  index range. UV не ограничиваются диапазоном `[0,1]`.
- Добавлен отдельный render-owned asset handle:
  - visual `{0x5841, 1}`;
  - material `{0x5842, 1}`;
  - pso `{0x5843, 1}`.
- Synthetic `Resource12` handles не используются для asset. CPU preparation
  получает explicit allow-list, глобальный `ValidResource` не ослаблен.
- `GpuMeshRegistry12` получил один fixed asset slot:
  - asset VB/IB и upload buffers;
  - BC1 texture и texture upload buffer;
  - SRV descriptor heap;
  - отдельный textured root signature и PSO.
- Textured pipeline использует `b0`, `b1`, `t0`, `s0`; shader — явно
  обозначенный unlit textured fallback, не полный X-Ray `default`.
- Asset проходит существующий путь:
  `Object12 → OrderedFrame12 → ResolvedFrame12 → FrameBatchPlan12 →
  DrawIndexedInstanced`.
- Добавлены четыре инстанса одного asset handle для multi-batch/parallel
  проверок. Instance 0 центрирован в начале координат для readback oracle.
- `-scene xrayasset` и CLI:
  - `-xrayogf <path>`;
  - `-xraydds <path>` (optional; missing DDS включает fallback);
  - `-xraytest`;
  - `-xrayframes N`;
  - `-xraypostsubmitfault` (TEST ONLY).
- Legacy, transforms и geometry scenes не изменены по поведению.

## 2. Asset provenance and formats

### OGF

Источник: `D:\Games\S.T.A.L.K.E.R. Shadow of Chernobyl\gamedata.db4`,
entry `meshes\objects\dynamics\plitka\plitka1.ogf`.

- 64 392 bytes.
- `ogf_header`: format version 4, `MT_NORMAL` (0).
- `OGF_TEXTURE`: `ston\ston_stena_marbl_m_03_back`, shader `default`.
- `OGF_VERTICES`: FVF `0x112` (position + normal + UV), 1 802 vertices,
  stride 32.
- `OGF_INDICES`: 3 300 uint16 indices.
- Bounds: `(0, 0, -0.010446) .. (2.370610, 2.863260, 0)`.
- Все индексы в `[0, 1801]`, нормали единичные с допуском, UV конечные.

### DDS

Источник: `D:\Games\S.T.A.L.K.E.R. Shadow of Chernobyl\gamedata.db7`,
entry `textures\ston\ston_stena_marbl_m_03_back.dds`.

- BC1/DXT1.
- 512×512.
- 10 mip levels.
- 174 904 bytes.

Лицензированные данные не копируются в Git. Для запуска используются только
внешние локальные файлы.

### Почему SoC, а не CoP

В `D:\Games\S.T.A.L.K.E.R. Call of Pripyat\resources\resources.db0` найдено
879 OGF: 616 `MT_SKELETON_RIGID` (10), 263 `MT_SKELETON_ANIM` (3),
`MT_NORMAL` (0) отсутствует. Поэтому по согласованному решению используется
SoC static asset. CoP runtime/VFS integration не выполнялась.

## 3. CPU/GPU data flow and ownership

1. CLI передаёт пути к внешним OGF/DDS.
2. Owner thread загружает и валидирует CPU `XrayAssetData12` и
   `XrayTextureData12`.
3. `BuildXrayScene` создаёт immutable `Object12` packets с asset handles.
4. Owner создаёт VB/IB, texture, upload buffers, SRV heap, root signature и
   PSO.
5. Upload commands отправляются в очередь; `m_gpuWorkSubmitted`
   выставляется сразу после успешного `CreateXrayAsset`.
6. Upload buffers удерживаются до подтверждённого fence completion.
7. `ResolveXrayFrame` на owner-side проверяет immutable packet data и
   заполняет VBV/IBV/index range/pipeline.
8. Workers только читают `ResolvedFrame12`/`FrameBatchPlan12` и записывают
   свои command lists.
9. `GpuMeshRegistry12::RecordDraw` для asset выставляет textured PSO,
   root signature, descriptor heap, b0/b1 и SRV table независимо в каждом
   list.

Texture upload использует `GetCopyableFootprints`, корректные mip offsets,
row pitch, `CopyTextureRegion` и переходы
`COMMON → COPY_DEST → PIXEL_SHADER_RESOURCE`.

## 4. GPU lifetime and quarantine

- Asset VB/IB и upload buffers удерживаются до `WaitForGpu`.
- Texture и texture upload buffer удерживаются до того же fence.
- SRV heap, root signature и textured PSO входят в registry ownership.
- `GpuQuarantine` расширен fixed-size полями для asset VB/IB/uploads,
  texture, SRV heap, root signature и PSO.
- Unknown GPU lifetime передаёт asset-ресурсы в quarantine без динамических
  аллокаций.
- `-xraypostsubmitfault` проверяет failure после asset submit, но до
  штатного drain: Shutdown выполнил безопасный drain, `GPU drained before
  shutdown`, без `NoWorkSubmitted` и без Unknown/quarantine.

## 5. Verified

**Исторические результаты ниже относятся к исходной реализации F3.6A до
Audit Hardening. Актуальные результаты hardening — в разделе 10.**

### Build

- `ReleaseDX12|x64` Rebuild: **PASS**, 0 warnings/errors.

### CPU

- X-Ray synthetic loader/resolver: **30/30 PASS**.
- X-Ray real OGF/DDS CPU test: **35/35 PASS**.
- Geometry: **245/245 PASS**.
- Fence: **49/49 PASS**.
- Transform: **540/540 PASS**.
- Packet plan, CPU, snapshot, visibility, ordering, preparation: **PASS**.
- ASan (including X-Ray synthetic): **PASS**.

### GPU X-Ray runner

`tests/run-xray-scene.ps1`: **10/10 PASS**.

- textured L1, L1×8, L2 parallel W2, L4 parallel, batchlimit=1;
- fallback texture;
- invalid OGF rejection before upload;
- post-submit failure/drain;
- resize;
- re-init.

Evidence:

- Textured hash: `829633b7dc067e7a`.
- Fallback hash: `79bff891dbd7af25`.
- Textured center pixel: `(140, 116, 107, 255)`.
- Fallback center pixel: `(255, 128, 32, 255)`.
- Center depth: `0.984311`.
- 4 draws / 4 packets; L2 plan 2 lists / 2 batches; batchlimit=1 plan
  4 batches.
- Debug Layer + GBV: **0 WARNING/ERROR/CORRUPTION**.
- Invalid OGF: exit failure, no `asset uploaded`.
- Post-submit: safe drain, no Unknown/quarantine.

### Existing regressions

- Geometry runner: **16/16 PASS**.
- Transform runner: **10/10 PASS**.
- Packet draw: **22/22 PASS**.
- Packet multilist: **35/35 + WM_TIMER 2/2 PASS**.
- Legacy hash: `9dd37d87b8eb202d`.
- Transform hash: `c13dd65927d9727d`.
- Geometry hash: `0ea8788795a5950d`.
- Все старые validation-логи: 0 WARNING/ERROR/CORRUPTION.

## 6. Not Tested

- Полный VFS/`CLocatorAPI` path: по решению F3.6A используются внешние
  предварительно извлечённые loose-файлы.
- Asset Unknown/quarantine fault injection: код пути реализован, но
  реальный Unknown lifetime для texture/SRV не провоцировался.
- Полный X-Ray `default` shader, lighting, lightmaps и normal maps.
- Skeletal/GCONTAINER/hierarchy/LOD assets.
- Texture formats beyond BC1/DXT1.
- Runtime traversal, R4/DX11 integration, streaming, DXR.
- CI GPU run с реальным asset: CI не содержит лицензированный SoC asset;
  CPU X-Ray selftest в CI использует synthetic fixtures.
- Достоверные performance measurements: не выполнялись.

## 7. Known Limitations

- Asset source is SoC, not CoP; files are external and not versioned.
- One fixed asset slot and four fixed instances; no dynamic registry,
  residency, eviction or streaming.
- Unlit textured fallback only; X-Ray material semantics are not reproduced.
- Texture loader supports only 2D BC1/DXT1 with a mip chain.
- Missing texture uses a 1×1 orange fallback; it is not a material fallback.
- Readback oracle checks center pixel/depth and full-frame hash, not full
  rasterization or every texel.
- No DB archive reader or `CLocatorAPI` dependency in Smoke.

## 8. Remaining Risks

- External asset path is machine-dependent; a missing file must remain an
  explicit pre-GPU failure.
- The asset quarantine path is not fault-injected; future changes must keep
  texture/SRV/PSO in the same fixed quarantine storage.
- Textured pipeline is intentionally single-material; adding more materials
  would require an explicit registry/material design.
- A future VFS adapter must preserve the same immutable CPU data and owner
  lifetime rules.
- `default` shader differences may make the rendered appearance differ from
  the original game.

## 9. Recommendation for F4.0

1. Add a read-only VFS adapter (or a controlled extraction tool) with
   deterministic asset provenance and CI-safe fixture policy.
2. Introduce a real material descriptor/handle layer and a minimal
   `default`-compatible opaque shader path.
3. Add dynamic registry capacity/residency/eviction with generation reuse.
4. Integrate one runtime visual into the existing traversal only after
   owner-side snapshot/packet integration is designed.
5. Add GPU CI coverage for the asset path using a non-copyright fixture or a
   separately provisioned licensed asset.
6. Keep the existing fence/quarantine contract and extend it for any new
   resource types.

## 10. F3.6A Audit Hardening — M1/M2/L1/L2

> Исторические результаты этого раздела относятся к состоянию до Final Audit
> Corrections. Актуальные числа и проверки — в разделе 11.

### M1 — Unsafe DDS source bounds

**Root cause.** `CreateXrayResources()` compared `XrayTextureMip12` source
offsets/sizes with `textureBytes`, the aligned GPU upload allocation size. That
allocation can be larger than `texture->data`, so malformed metadata could pass
the old check and make `memcpy` read beyond CPU texture data.

**Fix.** `dx12xrayasset.cpp` now owns CPU-side
`ValidateXrayTextureSource()` and `ValidateXrayTextureCopy()`:

- source ranges use `texture.data.size()` and overflow-safe subtraction;
- every BC1 mip checks expected dimensions, row pitch, row count and data size;
- destination footprints check format, dimensions, row count, row size,
  pitch alignment, placement alignment and upload-buffer range;
- validation happens before upload-buffer map/copy and before GPU submission;
- no asset is published on validation failure.

`CreateXrayResources()` validates source metadata before creating the texture
and validates `GetCopyableFootprints` output before mapping/recording. A failed
validation returns false; Shutdown releases the unpublished resources.

**Tests.** Synthetic malformed metadata covers source offset beyond data,
source size beyond data, arithmetic overflow, row-pitch mismatch, mip-dimension
mismatch, truncated data, bad destination offset/range/pitch/dimensions/rows,
and zero upload allocation. ASan run passes.

### M2 — Explicit invalid DDS false positive

**Root cause.** `CHW12` fell back to the orange texture whenever `-xraydds`
failed to load. `VerifyXrayAsset()` only knew that a fallback was active, not
that the user explicitly requested a DDS, so `-xraytest` could PASS on fallback
rendering.

**Fix.** `CHW12` tracks `m_xrayDdsExplicit` and `m_xrayDdsLoadFailed`.
`VerifyXrayAsset()` fails before readback with
`explicit X-Ray DDS failed to load; fallback is not a textured PASS` when an
explicit DDS failed or the registry selected fallback. Missing `-xraydds`
remains a valid separate fallback scenario.

**Tests.** `run-xray-scene.ps1` now includes:
- valid explicit DDS → textured PASS;
- invalid explicit DDS → nonzero exit, explicit diagnostic, no `xray-frame
  PASS`;
- omitted DDS → orange fallback PASS, distinct hash.

### L1 — Strict DDS 2D BC1 validation

**Fix.** `LoadXrayDds12()` now checks required header flags, `DDSD_MIPMAPCOUNT`
for mip chains, FOURCC pixel format, legacy `DXT1` only (DX10 extended header
rejected), texture caps, cube/volume caps, depth, dimensions, maximum mip
count for the dimensions, overflow-safe BC1 mip sizes and truncated chains.

**Tests.** Synthetic malformed DDS cases: DXT5, DX10, missing required flags,
cube caps, excessive mips, truncated mip chain. The real 512×512/10-mip SoC DDS
still loads and renders.

### L2 — Unicode-safe Windows paths

**Fix.** Asset paths are stored as `std::wstring` from the Windows command
line. `LoadXrayOgf12()`/`LoadXrayDds12()` accept `std::wstring` and open via
`std::filesystem::path`, so the wide Windows path reaches the file stream.
Diagnostics use reason text rather than narrow path interpolation.

**Tests.** CPU loader tests cover ASCII, spaces, Cyrillic path, and missing
Cyrillic path. GPU runner includes `unicode-path` and `space-path` cases; both
load the real OGF/DDS and produce the textured hash.

### Hardening verification results

- ReleaseDX12 Rebuild: **PASS**, 0 warnings/errors.
- X-Ray CPU synthetic suite: **59/59 PASS**.
- X-Ray CPU real OGF/DDS suite: **64/64 PASS**.
- ASan including X-Ray malformed input: **PASS**, X-Ray 59/59.
- X-Ray GPU runner: **13/13 PASS**.
- Valid textured hash remains `829633b7dc067e7a`.
- Fallback hash remains `79bff891dbd7af25`.
- Explicit invalid DDS: **expected FAIL**, no false PASS.
- Debug Layer + GBV: **0 WARNING/ERROR/CORRUPTION** in X-Ray logs.
- Legacy/transform/geometry hashes remain `9dd37d87b8eb202d`,
  `c13dd65927d9727d`, `0ea8788795a5950d`.
- Geometry runner: **16/16 PASS**.
- Transform runner: **10/10 PASS**.
- Packet draw: **22/22 PASS**.
- Packet multilist: **35/35 + WM_TIMER 2/2 PASS**.
- Fence CPU: **49/49 PASS**; transform CPU: **540/540 PASS**;
  geometry CPU: **245/245 PASS**.

### Remaining hardening limitations

- Full VFS/`CLocatorAPI` integration is still out of scope.
- Unknown GPU lifetime/quarantine fault injection for asset texture/SRV/PSO
  is not performed; the fixed quarantine path is implemented but not
  fault-injected.
- Full X-Ray `default` material/lighting remains unsupported.
- Performance measurements are not available.

## 11. F3.6A Final Audit Corrections

Раздел закрывает три Low findings и пробел excessive-mips test coverage,
выявленные после Audit Hardening. Архитектура renderer, GPU lifetime,
кварантин, GR Job System и pipeline не менялись.

### L1 — DDS cubemap face flags

**Причина.** `LoadXrayDds12()` отклонял только основной
`DDSCAPS2_CUBEMAP` (`0x200`). Отдельные face flags (`0x400`…`0x8000`) могли
пройти проверку и попасть в 2D-only importer.

**Исправление.** В `dx12xrayasset.cpp` введена маска
`kDdscaps2CubeMapMask = 0xFE00`; проверка `caps2` теперь отклоняет и основной
cubemap flag, и все отдельные face flags. Отклонение volume textures через
`0x200000` сохранено.

**Новые tests.** Synthetic DDS с одним face flag `0x400`, вторым face flag
`0x800`, полной маской `0xFE00` и volume flag `0x200000` отклоняются.
Корректный BC1 2D DDS по-прежнему загружается.

**Результат:** FIXED, PASS.

### L2 — Empty explicit DDS argument

**Причина.** `-xraydds ""` принимался как пустой путь, после чего
`m_xrayDdsExplicit` становился `false` и explicit invalid DDS мог перейти в
разрешённый orange fallback.

**Исправление.** В `xrDX12Smoke.cpp` при наличии `-xraydds` аргумент
обязателен и не может быть пустым. При пустом значении выводится
`[smoke] -xraydds requires a non-empty path`, выставляется `badArgument`,
приложение завершается с ненулевым exit code до инициализации GPU/textured
path. Отсутствие `-xraydds` по-прежнему разрешает fallback; непустой invalid
DDS по-прежнему даёт FAIL при `-xraytest`.

**Новый test.** GPU runner case `empty-explicit-dds`:
`-scene xrayasset -drawsource packets -xrayogf <valid.ogf> -xraydds "" -xraytest`.
Проверяются: expected failure, diagnostic, отсутствие GPU upload, отсутствие
`xray-frame PASS` и отсутствие ложного `-xraytest: PASS`.

**Результат:** FIXED, PASS.

### L3 — Strict D3D12 footprint row size

**Причина.** `ValidateXrayTextureCopy()` принимал
`rowSizes[mip] >= source.rowPitch`. Для поддерживаемого BC1/RGBA8
unpadded row size известен точно, поэтому завышенный row size мог скрыть
несоответствие source metadata и D3D12 layout.

**Исправление.** В `dx12xrayasset.cpp` добавлена строгая проверка:

`rowSizes[mip] == expectedRowPitch`

Проверка destination `layout.Footprint.RowPitch` остаётся отдельной и
требует достаточного 256-byte aligned pitch. Source row pitch и data size
по-прежнему сверяются с вычисленной BC1/RGBA8 геометрией.

**Новый test.** BC1 4×4: source row pitch `8`, подставленный unpadded row size
`16`, destination aligned RowPitch `256`; такой footprint отклоняется.
Существующий valid D3D12 footprint test и реальный production path
`GetCopyableFootprints` остаются PASS.

**Результат:** FIXED, PASS.

### Excessive-mips test coverage

**Причина.** Excessive-mips fixture выставлял `mipCount = 4` без
`DDSD_MIPMAPCOUNT`. Parser отклонял fixture раньше по причине отсутствия
mipmap-флага, поэтому ветка проверки максимального mip count не проверялась.

**Исправление.** Fixture выставляет согласованные flags
`0x21007` (`DDSD_CAPS|HEIGHT|WIDTH|PIXELFORMAT|MIPMAPCOUNT`) и
`mipCount = 4` для 4×4 texture. Тест дополнительно проверяет, что причина
отказа содержит `invalid DDS dimensions/mips`, то есть отказ произошёл по
ветке проверки mip count, а не по flags. Ограничения importer не менялись.

**Результат:** FIXED, PASS.

### Final corrections verification results

- ReleaseDX12|x64 Build: **PASS**, 0 warnings/errors.
- X-Ray CPU synthetic suite: **69/69 PASS**.
- X-Ray CPU real OGF/DDS suite: **74/74 PASS**.
- ASan malformed DDS/metadata and CPU suites: **PASS**,
  X-Ray **69/69**.
- X-Ray GPU runner: **14/14 PASS**.
- Geometry runner: **16/16 PASS**.
- Transform runner: **10/10 PASS**.
- Fence CPU: **49/49 PASS**; geometry CPU: **245/245 PASS**;
  transform CPU: **540/540 PASS**.
- X-Ray textured hash: `829633b7dc067e7a`.
- X-Ray fallback hash: `79bff891dbd7af25`.
- Legacy depth hash: `9dd37d87b8eb202d`.
- Transform hash: `c13dd65927d9727d`.
- Geometry hash: `0ea8788795a5950d`.
- Debug Layer + GPU-Based Validation: **0 WARNING/ERROR/CORRUPTION**
  в затронутых X-Ray, geometry и transform logs.
- `empty-explicit-dds` завершился с exit code **1** до GPU upload;
  ложного textured PASS нет.
- CI и новый прогон Unknown-quarantine fault injection: **NOT TESTED**;
  commit/push не выполнялись.

## 12. Final CLI Argument Hardening

### Root cause

`-xraydds` принимал любой непустой следующий токен как путь. Поэтому в строке

`-scene xrayasset -drawsource packets -xrayogf valid.ogf -xraydds -xraytest`

парсер записывал `-xraytest` в `xrayDdsPath`, не включал `xrayTest` и затем
завершался ошибкой загрузки файла вместо ошибки аргументов. Требуемая GPU
verification при этом не выполнялась.

### Fixed parser logic

В `xrDX12Smoke.cpp` добавлен helper `IsKnownCliOption()`, который
распознаёт только реальные опции существующего parser. При разборе
`-xraydds` следующий токен теперь отклоняется, если он:

- отсутствует;
- пустой (`""`);
- совпадает с известной CLI-опцией, включая `-xraytest`.

Устанавливается `badArgument`, выводится существующая диагностика
`[smoke] -xraydds requires a non-empty path`, приложение завершается с
ненулевым exit code до GPU initialization. Путь, начинающийся с дефиса, но
не совпадающий с известной опцией, остаётся допустимым.

### Test

Добавлен GPU runner case `missing-dds-path-before-option`:

`-scene xrayasset -drawsource packets -xrayogf <valid.ogf> -xraydds -xraytest`

Проверяются: ненулевой exit code, ожидаемая диагностика, отсутствие
`[DX12][xray] asset uploaded`, отсутствие `[xray-frame] PASS`, отсутствие
`[smoke] -xraytest: PASS` и отсутствие validation errors.

### Results

- `ReleaseDX12|x64` Build: **PASS**, 0 warnings/errors.
- X-Ray GPU runner: **15/15 PASS**.
- X-Ray CPU synthetic: **69/69 PASS**; real OGF/DDS: **74/74 PASS**.
- `missing-dds-path-before-option`: exit code **1**, diagnostic present,
  GPU upload отсутствует, ложного PASS нет.
- Сохранены: valid textured path, orange fallback, empty explicit DDS,
  explicit invalid DDS, space-containing path и Unicode path.
- `-xraydds` в конце аргументов вручную: exit code **1**, та же диагностика.
- Путь `-custom.dds`, не совпадающий с известной опцией, вручную: textured
  PASS, hash `829633b7dc067e7a`.
- X-Ray textured hash: `829633b7dc067e7a`; fallback hash:
  `79bff891dbd7af25`.
- Legacy/transform/geometry hashes не менялись:
  `9dd37d87b8eb202d`, `c13dd65927d9727d`, `0ea8788795a5950d`.
- Debug Layer + GPU-Based Validation на затронутых X-Ray прогонах:
  **0 WARNING/ERROR/CORRUPTION**.
- ASan, geometry и transform suites не повторялись: изменение ограничено
  CLI parser, runner и документацией; DDS/OGF loaders, GPU lifetime и
  command recording не затрагивались. Предыдущий ASan baseline остаётся
  **69/69 PASS**.
- CI: **NOT TESTED** — commit/push не выполнялись.

## 13. Final CLI Option Completeness

### Root cause

`IsKnownCliOption()` содержал 47 параметров, тогда как основной CLI parser
распознаёт 48. Единственный пропуск — `-cpubench`, который parser разбирает
совместно с `-cputest`. Поэтому `-xraydds -cpubench 60` мог принять
`-cpubench` за DDS path.

### Fix

В `xrDX12Smoke.cpp` в `IsKnownCliOption()` добавлен `L"-cpubench"`; семантика
самого `-cpubench` не менялась. Полная сверка наборов опций после изменения:

- parser: **48**;
- helper: **48**;
- parser-only: **0**;
- helper-only: **0**.

Других пропусков не обнаружено; parser больше не изменялся.

### New negative test

Добавлен `missing-dds-path-before-cpubench`:

`-scene xrayasset -drawsource packets -xrayogf <valid.ogf> -xraydds -cpubench 60 -xraytest`

Проверяются: exit code **1**, диагностика
`[smoke] -xraydds requires a non-empty path`, отсутствие
`[DX12][xray] asset uploaded`, отсутствие `[xray-frame] PASS`, отсутствие
`[smoke] -xraytest: PASS` и отсутствие validation errors.

### Results

- `ReleaseDX12|x64` Rebuild: **PASS**, 0 warnings/errors.
- X-Ray GPU runner: **16/16 PASS** (предыдущие 15 cases сохранены).
- Standalone `-cpubench 60`: **PASS**, `[cputest] PASS`, exit code **0**;
  существующая семантика benchmark сохранена.
- X-Ray textured hash: `829633b7dc067e7a`; fallback:
  `79bff891dbd7af25`.
- Legacy/transform/geometry hashes не менялись:
  `9dd37d87b8eb202d`, `c13dd65927d9727d`, `0ea8788795a5950d`.
- Debug Layer + GPU-Based Validation: **0 WARNING/ERROR/CORRUPTION**.
- ASan/geometry/transform suites не повторялись: изменение ограничено
  CLI helper, runner и документацией; GPU/CPU lifetime и loaders не
  затрагивались.
- CI: **NOT TESTED** — commit/push не выполнялись.

F3.6A после этой корректировки готов к финальному Codex delta review.
