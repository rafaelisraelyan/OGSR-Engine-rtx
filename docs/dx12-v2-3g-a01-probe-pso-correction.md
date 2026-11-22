# V2.3g-A01 — Probe PSO Correction & Morphology Continuation

Статус: **READY FOR V2.3g-A01 INDEPENDENT REVIEW**.
V2 PASS не присваивался. F3 **отклонена как замена** стандартного рендера и
остаётся экспериментальной формулой, **не default**.
Merge в `prypiat` не выполнялся. V2.4, lightning, surge и game DX12 не
начинались.

Ветка: `feature/dx12-volumetric-clouds`, HEAD
`42b8124e538ef4e40229a155203c133ca412756c`.

## A. Source Safety and Patch Provenance

- AGENTS.md прочитан; правила по Git, backup и русскому тексту соблюдались.
- V2.3f backup:
  `D:\Coding Project\OGSR_BACKUP_BEFORE_V23F_2026-10-26`,
  MANIFEST SHA256
  `800C94580C52919208EFC9753E7A60E236803E70EC9EC1A61AD09F68EFDAEA21`.
- V2.3g snapshot:
  `D:\Coding Project\OGSR_BACKUP_BEFORE_V23G_2026-10-26`,
  MANIFEST SHA256
  `9CF86524D6091ECE9E1F3805C4176A2CF83BEC3FFF7C9A34861BBDA4C10D937F`.
- A01 snapshot before applying the patch:
  `D:\Coding Project\OGSR_BACKUP_BEFORE_V23G_A01_2026-10-27`,
  71 file, MANIFEST SHA256
  `FE49DD0263EFE150F8F00169591D5114A8C55D1FA4ED6ACCE12078ACEADA3B0D`;
  71/71 verified, 0 mismatches.
- Supplied archive:
  `OGSR_V23G_A01_Probe_PSO_Source_Patch.zip`,
  size 20 688 bytes,
  SHA256 `E3CD7955C3EEBC551F1D18B4532D0FCE3F699EB366AAEBA2DAFD0E97912EB89B`.
- Archive `MANIFEST.sha256`: 13/13 verified, 0 mismatches.
- Patch: `OGSR_V23G_A01_Probe_PSO_Correction.patch`,
  SHA256 `0F4BCA5E3457E980D40CBB25F8820385E409BEA83D9518F267622A1306F66BB3`,
  236 lines, 11 files, +135/−1.
- Patch source basis: `OGSR_V2_3g_Formula_Validation.zip`,
  SHA256 `CF2AD5EBA3AC0EBD76F0E1D42FB229056E14703D7B45FDF4DC15DA59F556900C`.
- Current V2.3g source hash before apply (72 files):
  `f147f743ba190b297d6b22b5e84be5a3ffaddf84ef8d491bcd76057e4fe663ff`,
  совпадает с source basis.
- `git apply --check`: exit 0. `git apply`: exit 0.
- Git state during source freeze: 103 entries — 20 tracked modified,
  83 untracked, 0 staged, 0 deleted. На финальном handoff — 105 entries:
  добавляются `OGSR_V2_3g_A01_Correction.zip` и `.sha256` в корне репозитория.
  Коммит, push, reset, clean, rebase, force push и merge не выполнялись.

## B. Defect

До A01 `CloudRayMarchProbe.hlsl` содержал только:

```hlsl
#define OGSR_PROBE 1
#include "CloudRayMarch.hlsl"
```

То есть probe-пермутация компилировалась без `OGSR_FORMULA` и без
`OGSR_SHAPE_PROTOTYPE`. В `CloudRayMarcher::Dispatch()` ветка `probe`
выбиралась раньше formula/prototype, поэтому все diagnostic probe views
фактически исполняли F0:

- `DensitySlice`
- `DensityHorizontal`
- `DensityVerticalZ`
- `MacroShape`
- `RayProfile` (Float32 readback)

`Final`, `Density`, `Weather` и `CloudType` не входят в probe-ветку и
использовали обычный formula PSO; поэтому V2.3g coverage/meanD метрики,
построенные по `density`, остаются валидными. Старые V2.3g slices/macro/
ray-profile изображения для F0/F3 различий **не используются**.

## C. Applied Correction

Patch добавляет семь probe-пермутаций, которые включают те же
`CloudRayMarch.hlsl` и те же defines, что и render path:

- `CloudRayMarchProbeFormulaF1..F5.hlsl` (`OGSR_PROBE=1`, `OGSR_FORMULA=n`);
- `CloudRayMarchProbePrototypeA/B.hlsl` (`OGSR_PROBE=1`, `OGSR_SHAPE_PROTOTYPE=1/2`).

C++ изменения:

- `pipelineStateProbeFormula_[5]`, `pipelineStateProbeProtoA_`,
  `pipelineStateProbeProtoB_`;
- probe selection по prototype/formula;
- лог фактического PSO:
  `cloud probe PSO selected: <wrapper>`;
- лог один раз на переход PSO;
- освобождение всех probe PSO в `Shutdown()`;
- CMake/fxc проверяет все восемь probe permutations;
- `check_sources.py` проверяет наличие wrapper-файлов и defines.

Default render PSO, formula implementations, resource layout, lighting и
weather model не изменялись.

## D. Probe PSO Verification

PSO matrix, Release build, 640×360 half, 64 steps, `macro-shape`:

| Selection | Actual PSO log |
|---|---|
| F0 | `CloudRayMarchProbe.hlsl (F0)` |
| F1 | `CloudRayMarchProbeFormulaF1.hlsl` |
| F2 | `CloudRayMarchProbeFormulaF2.hlsl` |
| F3 | `CloudRayMarchProbeFormulaF3.hlsl` |
| F4 | `CloudRayMarchProbeFormulaF4.hlsl` |
| F5 | `CloudRayMarchProbeFormulaF5.hlsl` |
| Prototype A | `CloudRayMarchProbePrototypeA.hlsl` |
| Prototype B | `CloudRayMarchProbePrototypeB.hlsl` |

Все восемь probe `.cso` собраны fxc:
`CloudRayMarchProbe.cso` и
`CloudRayMarchProbeFormulaF1..F5.cso`,
`CloudRayMarchProbePrototypeA/B.cso`.

F3 проверен во всех пяти probe-режимах; в каждом логе присутствует
`cloud probe PSO selected: CloudRayMarchProbeFormulaF3.hlsl`.
Под GBV probe F3 также дал этот PSO, 0 errors/0 warnings, exit 0.

Короткий F0/F3 probe smoke test (Storm, 640×360 half, 64 steps) дал
ненулевые различия:

| Mode | mean abs diff (0..255) | max | nonzero |
|---|---:|---:|---:|
| DensitySlice | 8.3155 | 121 | 9975/43200 |
| DensityHorizontal | 14.7749 | 137 | 18927/43200 |
| DensityVerticalZ | 7.8819 | 107 | 10143/43200 |
| MacroShape | 25.7352 | 116 | 24840/43200 |
| RayProfile image | 0.4734 | 69 | 893/43200 |

Float32 ray-profile (Storm, 180×64×3 float4 records):
density meanAbsDiff 0.028466, shaped 0.054081,
profileTimesShaped 0.051169; height/profile/macroR/midG/weatherR/typeValue/
coverage/tNormalized равны 0. Это ожидаемая картина: меняется density-часть,
а weather/type inputs остаются общими.

F0 probe после patch побитово совпадает с pre-A01 F0 probe:
mean 0, max 0. Default `--formula=none` final также побитово совпадает с
V2.3: mean 0, max 0.

## E. DX12 Regression

Build directory:
`D:\Coding Project\OGSR ENGINE fork from rtx\tools\VolumetricCloudsTest\build-v23g-a01`

- Release EXE SHA256:
  `6D3F42D4809DD0499AF19B232F539F8285F9B0A03E52050011CA33C2B30B51F5`
- Debug EXE SHA256:
  `462A7860A9988B3918C7B069E43EF80FA37CC1FD2A50DBCF47EEFCAE170D4D81`
- CTest Release / Debug: **1/1 PASS**.
- Source checks Python / PowerShell: **PASS**.
- Hardware F0/F3 selftest, 64³: **PASS**.
- WARP F0/F3 functional selftest, 64³, без GBV: **PASS**.
- WARP + `--gpu-validation`: exit 2, hardware-only limitation.
- Release/Debug GBV selftest F0/F3: **PASS**, 0 errors/0 warnings.
- Release/Debug GBV 120 frames + resize F0/F3: **0 errors, 0 warnings**,
  resize 469×242 → 629×372, exit 0.
- Probe F3 + GBV: PSO F3, 0 errors/0 warnings, exit 0.
- Injected debug ERROR: `final debug errors: 1`,
  `final validation verdict: FAIL`, `OVERALL SELFTEST: FAIL`, exit 1.
- Shutdown: `GPU idle`, `non-device objects: 0`; только device refcount
  warning.
- Actual cloud compute + composite: `CloudRayMarcher compute +
  CloudCompositor draw active`, GPU timestamps присутствуют.
- Default F0 final vs V2.3: mean 0, max 0.
- F0 probe vs pre-A01: mean 0, max 0.

## F. Corrected F0/F3 Comparisons

### F.1. Matrix

Снято 96 захватов на seed 1337 (6 сцен × F0/F3 × 8 режимов) и 48 core-
захватов на seed 7331 (Sunset/Storm/Overcast × F0/F3 × 8 режимов).
Дополнительно сняты 15 coverage-matched F3-захватов на seed 7331
(`--formula-bias=0.02`). Все A/B использовали одинаковые камеры, время,
ветер и освещение; менялась только formula.

Для прямого сравнения с V2.3g используется seed 7331: именно он был
default в V2.3g. Набор 1337 остаётся дополнительной проверкой probe PSO
на шести сценах; он не используется для финального visual gate.

### F.2. Final / density / ROI, seed 7331

| Scene | F0 coverage / meanD | F3 coverage / meanD | F0 ROI (occ/run/straight) | F3 ROI |
|---|---:|---:|---:|---:|
| Sunset | 0.5636 / 0.1713 | 0.5769 / 0.1544 | 0.859 / 226.6 / 0.050 | 0.808 / 203.7 / 0.030 |
| Storm | 0.9776 / 0.4274 | 0.9784 / 0.4193 | 0.599 / 165.5 / 0.313 | 0.580 / 160.2 / 0.287 |
| Overcast | 0.8681 / 0.3805 | 0.8672 / 0.3638 | 0.528 / 159.0 / 0.043 | 0.507 / 155.0 / 0.042 |

Эти значения **точно совпадают с V2.3g**: A01 build не изменил final,
density и ROI path. F3 по-прежнему не улучшает Storm wedge и делает
Sunset/Overcast менее контрастными.

### F.3. Coverage-matched F3, seed 7331

| Scene | F3cov coverage / meanD | F3cov ROI (occ/run/straight) |
|---|---:|---:|
| Sunset | 0.5377 / 0.1372 | 0.798 / 208.0 / 0.030 |
| Storm | 0.9747 / 0.4137 | **0.588 / 162.8 / 0.333** |
| Overcast | 0.8664 / 0.3521 | 0.448 / 139.2 / 0.036 |

Storm coverage-matched: bright occupancy 0.588 vs F0 0.599, runMean
162.8 vs 165.5, straight 0.333 vs 0.313. Wedge остаётся; straightness даже
выше. Sunset/Overcast становятся светлее/менее контрастными, но pillar и
plates не исчезают.

### F.4. Corrected probe evidence

PSO во всех A01 probe-захватах соответствует выбранной формуле; старые
V2.3g probe-изображения не использовались.

Float32 ray-profile, seed 7331 (720×128 samples; meanA → meanB,
meanAbsDiff, maxAbsDiff):

| Scene | density | shaped | profile × shaped | weather/type/coverage |
|---|---:|---:|---:|---:|
| Sunset | 0.012518 → 0.002987, 0.010504, 0.138517 | 0.031883 → 0.011103, 0.025034, 0.204752 | 0.018627 → 0.005276, 0.016244, 0.186434 | 0 (все) |
| Storm | 0.055364 → 0.040598, 0.017372, 0.256020 | 0.083360 → 0.145028, 0.084866, 0.238391 | 0.080127 → 0.138666, 0.079780, 0.238391 | 0 (все) |
| Overcast | 0.075974 → 0.062328, 0.017619, 0.110390 | 0.141205 → 0.151652, 0.065573, 0.239513 | 0.133613 → 0.142507, 0.060756, 0.238391 | 0 (все) |

`height`, `profile`, `macroR`, `midG`, `weatherR`, `typeValue`, `coverage` и
`tNormalized` совпадают побитово: probe теперь честно использует ту же
density-формулу, а weather/type inputs остаются общими.

Corrected image differences F0 vs F3 (mean abs, 0..255):

| Scene | final | density | XY | YZ | H0.50 | macro |
|---|---:|---:|---:|---:|---:|---:|
| Sunset | 4.59 | 12.18 | 8.38 | 14.60 | 17.25 | 33.59 |
| Storm | 1.82 | 5.27 | 6.91 | 12.08 | 19.13 | 42.01 |
| Overcast | 6.51 | 5.57 | 9.61 | 9.11 | 16.71 | 35.75 |

Weather coverage F0/F3 идентичен (0/0): формула не подменяет weather map.
Corrected macro/slices показывают, что **F3 действительно меняет density
field** — прежнее утверждение V2.3g «контур не изменён» было основано на
недействительном probe и снимается.

При matched coverage F0 vs F3cov различия не исчезают: macro
Sunset 30.73, Storm 41.74, Overcast 34.06; final 5.46/2.42/8.72. То есть
F3 меняет поле не только количеством coverage, но и распределением
плотности. Однако это изменение не даёт выигрыша в final-ROI: wedge,
pillar и plates остаются. F3 отклоняется как замена не потому, что «ничего
не меняет», а потому что меняет поле в сторону, не проходящую visual gate.

### F.5. Дополнительные сцены, seed 1337

Cumulus below / Cumulus side / Strat: F3 тоньше F0
(coverage −0.035…−0.037, meanD −0.066…−0.075), corrected macro/slices
отличаются, weather coverage идентичен. Новых дыр, NaN или type-артефактов
не видно. Эти сцены подтверждают перенос probe-исправления на все шесть
сцен, но не используются для финального visual gate.

### F.6. Вывод по A/B

- Probe PSO correction работает: все F3 probe-режимы дают F3, а не F0.
- Corrected probes показывают реальное изменение density field у F3.
- При matched coverage final Storm wedge, Sunset pillar и Overcast plates
  не устраняются; ROI остаётся на уровне V2.3g или хуже.
- F3 отклоняется как замена стандартного рендера; исходники сохраняются
  как исследовательский результат.

## G. F3 Decision

F3 **отклоняется как замена стандартного рендера**. Причина — не дефект
probe PSO (он исправлен), а сохраняющаяся визуальная несостоятельность:
при сопоставимом покрытии F3 не устраняет Storm wedge, Sunset pillar и
Overcast plates; corrected probes подтверждают, что это не артефакт старой
диагностики.

F3 сохраняется в исходниках как результат исследования. Default остаётся
V2.3 `--formula=none`. Повторная настройка coverage не является следующим
этапом.

## H. Known Issues

1. F3 visual gate не пройден; F3 не default и не merge-ится.
2. WARP+GBV остаётся hardware-only ограничением; WARP functional test
   выполняется только без GBV.
3. Full A/B matrix использует seed 1337 и 1280×720 full; weather seed
   robustness остаётся ограниченной (V2.3g seeds 1338/1339 насыщают ROI).
4. F3 cost и Overcast plates остаются известными ограничениями V2.3g.
5. Patch подготовлен внешним аудитором; в этом этапе он впервые собран,
   проверен на Windows/D3D12 и покрыт GPU validation.
6. Никаких commit/push/merge не выполнено.

## I. Verdict

**Статус: READY FOR V2.3g-A01 INDEPENDENT REVIEW.**

A01 correction выполнена и проверена: probe-пермутации F0/F1–F5 и
Prototype A/B используют те же формулы, что и render path; все probe-режимы
дают корректный PSO; Float32 readback показывает ожидаемые различия;
DX12/GBV/WARP/negative validation проходят; default F0 и F0 probe
пиксельно не изменились.

| # | Проверка A01 | Статус |
|---|---|---|
| 1 | Архив, manifest, source basis, `git apply --check` | **PASS** |
| 2 | F0/F1–F5/Prototype A/B probe PSO mapping | **PASS** |
| 3 | DensitySlice/Horizontal/VerticalZ/Macro/RayProfile под F3 | **PASS** |
| 4 | Float32 probe readback и weather/type invariance | **PASS** |
| 5 | Clean Release/Debug, CTest, fxc permutations | **PASS** |
| 6 | Debug Layer, GBV, 120 frames + resize, shutdown | **PASS** |
| 7 | WARP без GBV; WARP+GBV exit 2 | **PASS с ограничением** |
| 8 | Negative injected ERROR → FAIL exit 1 | **PASS** |
| 9 | Default F0 и F0 probe pixel-identical | **PASS** |
| 10 | Corrected F0/F3 A/B и решение по F3 | **PASS** (F3 отклонена) |

F3 **не включать по умолчанию**, не merge-ить в `prypiat` и не объявлять
V2 PASS. Следующий этап — изолированный эксперимент естественной геометрии
облачных масс на базе V2.3, без продолжения настройки F3 coverage. Начинать
его только после независимого аудита A01.
