# V2.3f — Cloud Density Formula Benchmark

Статус: **READY FOR INDEPENDENT FORMULA REVIEW**.
V2 PASS не присваивался. F3 — экспериментальный кандидат, **не default**.
Merge в `prypiat` не выполнялся. V2.4, V-L, V-S, lightning, surge,
game DX12 не начинались.

Ветка: `feature/dx12-volumetric-clouds`, HEAD `42b8124e5`.
База: V2.3 final delta
(`OGSR_V2_3_Final_Delta.zip`, SHA256
`4118AD127570FA69102D55B43967A77D6ED6396929C4631F0BE1C79DA15CAEC5`).
Предыдущий checkpoint: `OGSR_V2_3e_Shape_Prototype.zip`,
SHA256 `DC2EAEF618DD75A576C9B1AEA517EB0748922E8ED826E4693490D96DD807E6CE`.

> `OGSR_V2_3e_Open_Source_Cloud_Density_Research_2026-10-26.md` в репозитории
> отсутствует. Формулы реализованы самостоятельно по принципам, описанным
> в постановке (Nubis, Godot Volumetric Clouds V2, Unity HDRP cloud map/LUT,
> Tyler Dodds); чужой shader source не копировался.

## A. Backup before modification

Backup создан и проверен **до** любых изменений исходников.

- Path: `D:\Coding Project\OGSR_BACKUP_BEFORE_V23F_2026-10-26`
- Files: **13 572**
- Size: **4.36 GB**
- Manifest SHA256:
  `800C94580C52919208EFC9753E7A60E236803E70EC9EC1A61AD09F68EFDAEA21`
- Git bundle SHA256:
  `BA59968E061A89759FCFC5C9C33991D30DE646675C95113C909F24587D81D714`
- Tracked patch SHA256:
  `A67B8D84B63BFD6EF52F448832403D27B3DBE0C7CABF8019A72FF9FC5DB0D34F`
- Restore verification:
  `D:\Coding Project\OGSR_RESTORE_VERIFY_V23F`,
  13 572 files, backup/restore manifest diff = **0**.
- Git: branch `feature/dx12-volumetric-clouds`, HEAD
  `42b8124e538ef4e40229a155203c133ca412756c`;
  83 status entries: 20 tracked modified, 0 staged, 63 untracked, 0 deleted.
- Exclusions: `.git`, `x64`, `bin_x64`, `ogsr_engine/_TEMP`,
  `ogsr_engine/_LIB`, `3rd_party/_TEMP`, `3rd_party/_LIB`,
  `tools/VolumetricCloudsTest/artifacts`, `tools/.../build-*`, `.vs`,
  `*.pdb`, `*.ipch`, `*.cso`, `*.ilk`, `*.tmp`.
  All source, scripts, docs, configs, untracked files and release ZIPs are
  included. Full list: `evidence/backup/EXCLUDED.txt`.

## B. F0 — immutable baseline

`--formula=none` воспроизводит V2.3 побитово: mean 0, max 0 на
`clouds_cumulus_below`. Все формулы — отдельные compile-time PSO
(`CloudRayMarchFormulaF1..F5.hlsl`), default PSO не содержит formula-кода.

## C. F2 — order of type density blending

Формула F2:
`density = Σ w_i * (Transfer(shape_i) * profile_i)`.
Baseline: `Transfer(Σ w_i * shape_i) * (Σ w_i * profile_i)`.

Результат: **гипотеза не подтверждена**. Средняя разница final:
Sunset 0.538/255, Storm 0.002, Overcast 0.054, Cumulus 0.13, Strat 1.22.
`cross-type-difference` (pre-weather, ×4) почти чёрный. Причина: в этих
сценах type weights доминируются одним типом, поэтому порядок операций
почти не влияет. F2 отклонена как fix.

## D. F1 — Perlin–Worley as primary shape

`f1Base = macro.b * 0.85 + mid.g * 0.15`, те же height profiles/thresholds.

Coverage bias 0: Sunset coverage 0.564→0.634, meanD 0.171→0.183;
Storm coverage 0.978→0.975. Но Sunset ROI ухудшился:
runMean 226.6→263.6, occupancy 0.859→0.962, straight 0.050→0.240.
Положительный bias +0.05/+0.10 уменьшает coverage, но делает столб ещё
более вертикальным (straight 0.54/0.75) и увеличивает Storm gap
(occupancy 0.704/0.742). F1 **отклонена**.

## E. F3 — weather coverage separation (candidate)

Изменение: shape thresholds фиксированы по типу (0.55/0.45/0.38),
coverage применяется **один раз** как spatial placement после shape;
двойной `weatherGate` убран.

Метрики:

| Scene | Metric | F0 | F3 |
|---|---|---:|---:|
| Sunset | coverage | 0.564 | 0.567 |
| Sunset | meanD | 0.171 | 0.149 |
| Sunset | dark runMean | 226.6 | **211.4** |
| Sunset | dark straight | 0.050 | **0.020** |
| Storm | coverage | 0.978 | 1.000 |
| Storm | meanD | 0.427 | 0.462 |
| Storm | bright occupancy | 0.599 | **0.319** |
| Storm | bright runMean | 165.5 | **69.5** |
| Storm | bright straight | 0.313 | **0.027** |
| Overcast | coverage | 0.868 | 0.867 |
| Overcast | meanD | 0.381 | 0.367 |
| Overcast | dark runMean | 159.0 | 163.2 |

F3 **не уменьшает coverage**: Storm coverage даже растёт (0.978→1.000),
Sunset остаётся тем же. Storm V-gap сокращается почти вдвое по occupancy и
в 2.4 раза по runMean; Sunset pillar заметно слабее и менее вертикален.
Overcast пластины остаются; изменение только в мягкости границ.
F3 — единственный кандидат, который стоит передать на независимый review.

## F. F4 — world-space XYZ noise

`coordinate = (worldX, worldY * 2.2 * anisotropy, worldZ) * scale`;
height используется только для profile. Coverage Sunset 0.564→0.601,
Storm 0.978→0.977. Sunset ROI становится полностью тёмным:
occupancy 1.000, runMean 280, straight 1.000. Storm bright occupancy
0.599→0.618, wedge не уменьшается. F4 **отклонена**.

## G. F5 — controlled edge erosion

`macro.g` boundary erosion, strength 0.5, existing samples, no new fetch.
Sunset ROI runMean 226.6→235.3, straight 0.050→0.070; Storm/Overcast
визуально без изменений. F5 **отклонена**: эффект ниже измеримого порога.

## H. Type transitions and debug views

Раздельно сняты: coverage (`weather`), type weights (`cloud-type`),
shape before coverage (`macro-shape`), F3 density (`density`) и final.
Прототипы и формулы смешиваются по density, weather map не размывается.
Evidence: `evidence/F3_debug_sunset.png`, `F3_debug_storm.png`,
`F2_cross_*.png`.

## I. GPU timings

1080p, half, 120 frames, storm, same session, 2 повтора (128 steps):

| Formula | cloud ms | note |
|---|---:|---|
| none | 7.303 | baseline |
| F1 | 6.240 | lower coverage/early exit |
| F2 | 7.810 | near baseline |
| F3 | 9.170 | higher coverage/density, less early exit |
| F4 | 4.442 | more transparent volume |
| F5 | 7.279 | near baseline |

Coverage-matched Sunset (128): none 5.964, F3 5.672 (**0.95×**).
64/96/128 Storm profile points, none vs F3:
1.613/2.036, 2.635/3.316, 7.303/9.170 ms.
Absolute values shifted vs the V2.3e session (2.7 ms) due to RTX 5070 Laptop
clock/thermal state. No formula adds a mandatory texture fetch; F3 adds ALU
and changes coverage, which explains the Storm cost. Evidence:
`evidence/timings.txt`, `evidence/timings/`.

## J. DX12 / validation

Build directory:
`D:\Coding Project\OGSR ENGINE fork from rtx\tools\VolumetricCloudsTest\build-v23f-final`

- Release EXE SHA256:
  `842177E667FDB84EB94CB414DDD7BA30844566BEB3D1031C9C0F590247635167`
- Debug EXE SHA256:
  `7618F647390306A8F9BE1C27EA5F0174F985649A70F6239E5B2FC9B7012C94EB`
- CTest Release/Debug: 1/1 PASS.
- Release/Debug GBV selftest: 0 errors, 0 warnings, validation PASS.
- Release/Debug + GBV 120 frames + resize, none/F3: 0 errors, 0 warnings.
- Injected debug ERROR: FAIL, exit 1.
- Shutdown clean.
- **WARP limitation:** `--warp --selftest` exits 1 on functional cloud
  checks (clear scene, extinction, camera/weather movement, sun direction,
  coverage, bounds, sun optical depth). `final validation verdict: PASS`,
  debug errors 0. V2.3 WARP selftest timed out. This is an open technical
  limitation, not a hidden DX12 error.

Source/build proof: `evidence/v23f_binary_identity.txt`.

## K. Verdict

**READY FOR INDEPENDENT FORMULA REVIEW.**

- F3 is the only formula with a measurable, coverage-preserving improvement
  of Storm wedge and Sunset pillar; it is not default.
- F1, F2, F4, F5 rejected; F5 below visible threshold.
- Overcast plates remain; WARP functional selftest remains open.
- V2 PASS is not claimed. No commit, push, or merge into `prypiat`.

## L. Next step

1. Independent review of F3 threshold/placement calibration and coverage.
2. Overcast-specific formula: variability of optical thickness and bottom
   surface, not only density shape.
3. F3 + overcast candidate combination only after both pass independently.
4. Resolve WARP functional selftest / document intentional WARP limitation.
