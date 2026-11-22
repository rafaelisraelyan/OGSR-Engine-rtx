# V2.3h-A02b — Silhouette Uniqueness & Storm Volume

Статус: **U BLOCKED / S PASS WITH NOTES / T PASS**.
V2 PASS не присваивался. Legacy V2.3 остаётся принятым renderer baseline.
Reference-модель не является default и не merge-ится.

Ветка: `feature/dx12-volumetric-clouds`, HEAD
`42b8124e538ef4e40229a155203c133ca412756c`.

## A. Git State & Backup

- A02 package: `OGSR_V2_3h_A02_NonRepeating_Field.zip`,
  SHA256 `EC4C95FD2BD19BA17B3CC4F094E35604EF581066D7F18262BBE5BD17B8D7E5A2`.
- A02b snapshot:
  `D:\Coding Project\OGSR_BACKUP_BEFORE_V23H_A02B_2026-10-27`,
  MANIFEST SHA256
  `44093BF4F881820E5F4CCE24F3197101EBD780F242CDBE0E2CB125F6398F4068`
  (89/89 verified).
- Git state at audit: 120+ entries, no staged changes; no
  commit/push/reset/clean/rebase/merge.
- Legacy identity: mean 0, max 0. Reference direct-path identity vs A02:
  mean 0, max 0.

## B. Baseline Identity

- Legacy V2.3 Sunset/cumulus identity preserved.
- Reference RB/R direct path unchanged; A02b additions are explicit
  switches (`view-optical-depth`, `fly-level`, `storm-volume`,
  `reference-density-scale`, storm height/bottom variation).
- Defaults keep the A02 multiplicative/hash behavior unchanged.

## C. Silhouette Uniqueness

New `--debug-mode=view-optical-depth` writes cloud-only
`OD/(OD+1)` (before atmosphere), so masks are based on ray-integrated
cloud optical depth, not image brightness.

Sunset, seed 1337, low camera, mapped-OD threshold 0.25 (OD ≈ 0.33):

| Variant | 4.53 km IoU | 4.53 OD corr | 4.53 multi-scale | 9.06 IoU | 9.06 OD corr | 13.6 IoU | 13.6 OD corr |
|---|---:|---:|---:|---:|---:|---:|---:|
| R-only | 0.988 | 0.997 | 0.978 | 0.977 | 0.990 | 0.973 | 0.986 |
| A w0.40 | 0.880 | 0.754 | 0.659 | 0.851 | 0.746 | 0.884 | 0.773 |
| B a0.18 | 0.807 | 0.864 | 0.621 | 0.765 | 0.742 | 0.777 | 0.762 |

Boundary Dice at 4.53 km: R 0.854, A 0.479, B 0.364.
Final-render exact-copy test (from A02): R 0.995/1.669 mean abs;
A 10.55/11.69; B 11.56/21.60. The exact copies are broken by A/B, but
the large masks still overlap substantially. The 10 km panorama still
shares a related large-mass layout. **U is therefore BLOCKED, not
self-accepted.**

The 60 km density autocorrelation (A02 matched):
R 0.864/0.967 (4.53/13.6), A 0.487/0.554, B 0.850/0.846.
This confirms A has the strongest field-level decorrelation; B has the
stronger final-render 9.06 break but retains the 4.53 slice identity.

Overcast (multi-threshold): masks are nearly full, so IoU is not a good
discriminator. OD correlation remains the useful metric; R 0.99+,
A/B 0.68–0.91 depending on station/threshold. The same U conclusion
holds: exact copies are broken, large-scale layout is not fully unique.

## D. Horizontal Flight

Added deterministic `--motion=fly-level`: constant height, constant
pitch/FOV, horizontal forward displacement 25 m/frame.

200 frames at 640×360 half/96, Sunset, y=400, pitch 10, wind=0:

| Variant | mean frame diff | max diff | frame 0 vs 181 (4.525 km) |
|---|---:|---:|---:|
| R-only | 1.765 | 2.060 | 14.69 |
| A w0.40 | 1.834 | 2.168 | 17.21 |
| B a0.18 | 1.375 | 1.499 | 10.99 |

No popping/shimmering spikes; y is constant in the metrics.
Limitation: 25 m/frame does not land exactly on the 4545 m tile, and the
grazing view amplifies the small phase offset. The static 4.53/9.06
station test remains the exact-copy evidence. A future level-flight mode
should use a 4545/181 ≈ 25.11 m/frame or a distance-locked step.

## E. Storm Geometry Diagnosis

Storm preset: layer 400–2300 m, coverage 0.80, density 1.20,
extinction 0.030. S0 is a **continuous flat deck**: vertical thickness
mean **1838 m**, std **5.8 m**, horizontal bright fraction **98.6%**,
one connected component. S1 (density ×1.5) is the same geometry:
1850 ± 5.3 m, 98.7% bright, one component. Increasing extinction only
darkens/densifies; it does not create volume.

The limiting factor is the combination of high coverage, fixed height
envelope, and a base shape that fills the layer. S2 therefore changes
**geometry**:

- coverage 0.55 (lower, to create gaps — not raised to fill the sky);
- non-periodic world-space Storm field, 5 km cell;
- local top + n·1600 m, local bottom − n2·350 m;
- gap mask `smoothstep(0.45, 0.60, stormField)`;
- density scale 1.5 to keep heavy cores.

S2 vertical thickness: mean **997 m**, std **606 m**, p95 **1983 m**;
horizontal bright fraction **36.6%**; **34** connected components,
largest 85.7%. The thickness variance is ~100× S0/S1. This is a real
geometric change, not just extinction.

## F. S0 / S1 / S2 Results

Parameters (all Reference R-only, Storm preset, same camera/lighting):

| Variant | Coverage | Density scale | Storm volume | Height var | Bottom var |
|---|---:|---:|---:|---:|---:|
| S0 | 0.80 | 1.0 | 0 | 0 | 0 |
| S1 | 0.80 | 1.5 | 0 | 0 | 0 |
| S2 | 0.55 | 1.5 | 1 | 1600 m | 350 m |

Contact sheet: `evidence/storm_smoke/storm_s0_s1_s2_views.png`
(horizon, outside side, top, toward sun, sun side, far horizon).
Density/OD: `s0|s1|s2/density-horizontal`, `density-slice`.

S0/S1 are flat plates in every view. S2 shows large separated masses
with sky gaps and strongly varying vertical thickness. The mandatory
Storm horizon remains dark, but the outside-side/top/sunward views show
the volume and gaps. No repeated identical towers are introduced by the
5 km non-periodic field.

## G. Storm Visual Gate

**S PASS (geometry) WITH NOTES.**

- Large connected masses: yes.
- Real thickness/variable height: yes (606 m std, p95 1983 m).
- Gaps between masses: yes (36.6% bright vs 98.6%).
- Density/OD evidence: yes; geometry change precedes lighting.
- Lighting unchanged V2.3; no exposure/black-layer substitution.
- Notes: cloud tops are soft/dark; the 128³ base noise and current
  self-shadowing do not produce photo-real cauliflower detail. This is a
  base-resolution/lighting limitation, not a reason to call S2 a density
  win. S1 confirms density alone does not pass.

## H. Density vs Lighting

S1 increases density ×1.5 but leaves thickness std at 5.3 m and one
component. S2 changes thickness std to 606 m and components to 34
**before** any lighting change. Therefore the S2 improvement is
geometric. The final images remain dark because the Storm preset
(sun 15°, sun intensity 2.2, ambient 0.72, exposure 1.0) and the
self-shadowing are unchanged; no lighting algorithm change was made.

## I. GPU Performance

RTX 5070 Laptop, 1080p half, shadow 6, 60 frames, warmup 20, n=3 at
128 steps; Storm step sweep n=1. Clocks before 1410 MHz / 21 W / 55 °C;
after 1942 MHz / 108 W / 76 °C. Absolute values are indicative.

Median cloud ms, 128/6:

| Model | Clear | Overcast | Storm |
|---|---:|---:|---:|
| Legacy | 4.311 | 5.152 | 2.359 |
| Reference R | 4.223 | 2.902 | 4.480 |
| A w0.40 | 5.452 | 3.303 | 3.164 |
| B a0.18 | 4.717 | 3.820 | 4.514 |
| S0 flat | — | — | 4.082 |
| S1 denser | — | — | 3.573 |
| S2 volumetric | — | — | 3.441 |

Storm step sweep, n=1 (64/96/128): Legacy 1.348/2.032/2.359;
R 2.292/3.319/4.480; A 1.660/2.414/3.164; B 2.422/3.576/4.514;
S0 1.932/2.901/4.082; S1 1.747/2.721/3.573;
S2 1.934/2.647/3.441.

S2 is cheaper than S0/S1 at 128 because coverage is 0.55 instead of
0.80; the comparison is a deliberate geometry/density trade, not a free
speedup. S2 still has one texture fetch per sample (the Storm height/gap
field is ALU only). A has two fetches; B has one. Legacy uses up to four
plus weather. No performance win is claimed for the S2 art change; the
cost is acceptable for the demonstrated volume gain.

## J. DX12 Regression

- Clean Release/Debug build: PASS, no C/X/LNK warnings.
- CTest Release/Debug: 1/1 PASS.
- Source checks Python/PowerShell: PASS.
- fxc reference permutations (including OD/Storm branches): PASS.
- Hardware selftest Legacy / R / A / B / S0 / S1 / S2: PASS.
- WARP functional R / A / B / S2 without GBV: PASS.
- WARP + GBV: exit 2, documented hardware-only limitation.
- Release GBV Legacy / R / A / B / S2: PASS, 0 errors/0 warnings.
- Debug GBV S2: PASS, 0 errors/0 warnings.
- Release/Debug GBV 120 frames + resize S2: 0/0;
  cloud output 469×242 -> 629×372, exit 0.
- Noise-field probe S2 under GBV: 0/0, reference probe PSO.
- Negative injected debug ERROR: FAIL, exit 1 (expected).
- F2/F5/Pause S2: slices↔clouds, storm→sunset complete,
  animation paused/running, 0 errors/0 warnings, exit 0.
- Legacy V2.3 identity: mean 0, max 0.
- Reference direct identity vs A02: mean 0, max 0.
- Shutdown: GPU idle, non-device objects 0; only device refcount warning.

T — Technical Gate: **PASS**.

## K. Known Limitations

1. U remains BLOCKED: exact copies are broken, but silhouette IoU
   0.81–0.88, multi-scale 0.62–0.66, and the 10 km panorama still show
   related large-mass layouts.
2. Level flight is not a distance-locked tile crossing (25 m/frame vs
   4545 m period); static station tests are the exact-copy evidence.
3. S2 tops are soft/dark; no photo-real cauliflower detail.
4. S2 uses a Storm field cell of 5 km and a gap mask; it is
   non-periodic but not a full weather system.
5. H5/H6, N4 erosion, V2.4, lightning, surge and game DX12 not started.
6. No V2 PASS; no commit/push/merge.

## L. Verdict

- **U — Uniqueness Gate: BLOCKED.** A/B materially reduce the exact
  4.53/9.06 copies and OD/boundary correlation, but the large dense
  masks still overlap and the 10 km layout remains related. Exact-copy
  improvement is not full spatial uniqueness.
- **S — Storm Volume Gate: PASS WITH NOTES.** S2 is geometrically more
  massive: thickness std 606 m vs 5–6 m, 34 components vs 1, bright
  fraction 36.6% vs 98.6%, with visible gaps and varied height. Lighting
  unchanged; tops remain soft/dark.
- **T — Technical Gate: PASS.** Release/Debug, CTest, source checks,
  fxc, HW/WARP/GBV, 120-frame resize, probe GBV, negative, F2/F5/pause,
  Legacy and direct identity all pass.

V2 PASS не присваивался. Commit/push/merge не выполнялись.
