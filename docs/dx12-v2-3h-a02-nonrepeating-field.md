# V2.3h-A02 — Non-Repeating Base Cloud Field

Статус: **READY FOR V2.3h-A02 INDEPENDENT REVIEW**.
V2 PASS не присваивался. Legacy V2.3 остаётся принятым renderer baseline.
Reference-модель не является default и не merge-ится. Exact-copy
periodicity улучшена; полный visual gate требует независимой проверки.

Ветка: `feature/dx12-volumetric-clouds`, HEAD
`42b8124e538ef4e40229a155203c133ca412756c`.

## A. Backup / Git State

- A01 backup: `D:\Coding Project\OGSR_BACKUP_BEFORE_V23H_A01_2026-10-27`,
  MANIFEST SHA256
  `36A068065DA79933D94DC9598E2891D5E0E615B8E634F3609F4533D8182CDDFB`.
- A02 snapshot: `D:\Coding Project\OGSR_BACKUP_BEFORE_V23H_A02_2026-10-27`,
  MANIFEST SHA256
  `D76328DA5489C3F0D571807547CF5D1BCB003E5E1A2E57987FAA6797AF1818D1`
  (89/89 entries verified).
- Git state at audit: 120 entries (20 modified, 100 untracked, 0 staged,
  0 deleted); final ZIP/SHA add two untracked entries. No
  commit/push/reset/clean/rebase/merge.
- A01 package: `OGSR_V2_3h_A01_Noise_Field_Isolation.zip`,
  SHA256 `D19F78858A4DFC5E3CA829C647D41D5D3282C63162F242998242524961D41B81`.
- Source/patch/EXE SHA256 are recorded in `AUDIT_BASELINE.txt`; the
  report does not embed its own source hash.

## B. Baseline Periodicity (N0)

Legacy identity: Sunset with `coverage=0.45` — mean 0, max 0.
Direct `rb` H2/H3 vs A01 — mean 0, max 0. The direct path is unchanged.

Direct R-only XZ, 60 km span (234.4 m/px, seed 1337):

| Metric | Value |
|---|---:|
| Raw shape corr at 4.53 / 9.06 / 13.6 km | 0.936 / 0.939 / 0.985 |
| Transfer corr at 4.53 / 13.6 km | 0.864 / 0.967 |
| Block-repeat RMS at 4.53 km | 0.303 |
| Raw shape corr at 20 / 30 / 40 km | 0.343 / 0.313 / 0.146 |

At the fine 8 km YZ slice the raw 4.53 km correlation is **0.998**: the
exact tile repeat is real. The 60 km metric is lower because of coarse
sampling and interpolation, but the 13.6 km (3-tile) peak remains 0.985.
The 40 km panorama confirms that the large-mass arrangement repeats
visibly at 10 km station spacing.

RB direct is similar: 0.932 / 0.935 / 0.984 raw; transfer 0.821 / 0.956.

## C. R-only (N1)

R-only is the cleanest large-shape candidate:

- broad, connected masses; no B-channel fine cells;
- aspect-corrected world half-lives: H **500 m**, V **179 m**,
  world V/H **0.359**;
- at matched density it still retains the exact 4.53 km tile
  (transfer corr 0.864; 13.6 km 0.967);
- visually softer and less detailed than RB, but not blurred.

R-only is a useful building block, but N1 itself cannot pass uniqueness.

## D. Candidate A — Multi-Frequency R

Two R samples, fixed non-integer scale ratio **0.73**, fixed seed-derived
phase, same wind-corrected coordinate. The final formula is
**multiplicative**:

```text
base = R(coord)
second = R(coord * 0.73 + phase)
shapeField = saturate(base * (1 + w * (2 * second - 1)))
```

The combined exact period is 100 base tiles (~454 km), outside the
measured 40 km. The second sample modulates the base density and leaves
its fine structure intact. Texture fetches: **2**.

The first implementation used a linear `lerp(base, second, w)`. It was
rejected: at matched density w0.40/w0.50 lost Storm/Sunset structure and
looked like a uniform overcast. Those blend captures remain in evidence
as `xz30_multi-r_w*_seed*`; the final Candidate A uses the multiplicative
form.

Matched controls (XZ 60 km, seed 1337, mean density 0.0818):

| Variant | Thresholds | Coverage | Transfer corr 4.53 / 13.6 | Density corr 4.53 / 13.6 | Block 4.53 (t/d) |
|---|---|---:|---:|---:|---:|
| A w0.25 | 0.5862..0.8862 | 0.4369 | 0.653 / 0.744 | 0.765 / 0.836 | 0.788 / 0.649 |
| A w0.40 | 0.5954..0.8954 | 0.4292 | 0.487 / 0.554 | 0.610 / 0.677 | 0.971 / 0.850 |

Candidate A materially reduces the numerical tile correlation and
preserves detail better than the blend. But the final visual test fails:
in the 0…40 km panorama, station image differences are not larger than
R-only (mean abs 18.4/20.0/18.3 vs 18.0/18.8/17.9), and the same large
dark mass arrangement is visible at every station. The base 60–75%
still dictates the mass positions; the modulation changes density, not
the silhouette.

## E. Candidate B — World-Space Low-Frequency Modulation

Non-periodic 3D value noise on a sheared lattice, cell **12 km**, quintic
interpolation, seed-derived phase; additive to the R-only shape field:

```text
shapeField = saturate(R(coord) + (valueNoise(world / 12 km + phase) - 0.5) * A)
```

No extra texture fetch. Wind is subtracted on XZ, so pure wind
translation is preserved.

Matched controls:

| Variant | Thresholds | Coverage | Transfer corr 4.53 / 13.6 | Density corr 4.53 / 13.6 | Block 4.53 (t/d) |
|---|---|---:|---:|---:|---:|
| B a0.18 | 0.5890..0.8890 | 0.4320 | 0.850 / 0.846 | 0.888 / 0.886 | 0.525 / 0.445 |
| B a0.25 | 0.5952..0.8952 | 0.4256 | 0.837 / 0.767 | 0.876 / 0.821 | 0.563 / 0.485 |

B reduces the **13.6 km** repeat but leaves the primary **4.53 km**
identity almost unchanged. Visually it preserves R-only detail, but it
does not solve the user-visible 4.53 km copying. Rejected as the primary
fix.

## F. World-Space Field Statistics

- Slice aspect correction: XY/YZ map 8 km horizontal and 1.35 km vertical
  into 256×256 (5.93:1). All world correlations use separate pixel
  scales; no pixel run-length is presented as world anisotropy.
- R-only shape half-lives: H 500 m, V 179 m; world V/H 0.359.
- Candidate A/B histograms remain finite and in [0,1]; no NaN/Inf.
- The 60 km XZ density map still shows a lattice of high-density peaks
  for direct R and both candidates; the peaks are modulated in intensity
  but not removed. This is the same tile/lattice structure measured by
  autocorrelation.

Raw-field vs final-render periodicity are different characteristics:

| Field | 4.53 km | 9.06 km | 13.6 km |
|---|---:|---:|---:|
| Raw NoiseVolume R (fine / 60 km) | 0.998 / 0.936 | 0.939 | 0.985 |
| R-only final render (mean abs) | 0.995 | 1.669 | — |
| A w0.40 final render | 10.55 | 11.69 | — |
| B a0.18 final render | 11.56 | 21.60 | — |

The raw base remains periodic; the final field is decorrelated by A/B.
This distinction is why the slice autocorrelation alone was not accepted
as the verdict.

## G. Visual Quality & Uniqueness

### Final-render period test

Sunset, same camera/view, shifted along world X by exactly the measured
period. Mean abs / max channel difference over the 1280×720 image:

| Variant | 0 → 4.53 km | 0 → 9.06 km |
|---|---:|---:|
| R-only | **0.995 / 15** | **1.669 / 28** |
| A w0.40 | **10.55 / 74** | **11.69 / 72** |
| B a0.18 | **11.56 / 62** | **21.60 / 91** |

R-only is visually identical after 4.53 and 9.06 km: the tile copy is
visible in the final render. A and B break the exact copy. B has the
largest 9.06 km difference and no extra texture fetch; A has the stronger
slice autocorrelation reduction.

The 10 km panorama stations still share a similar large-mass layout
(mean abs 18–26/255 for both A and B). This is a residual uniqueness
risk: the exact copies are gone, but the base mass arrangement is still
related. The independent review must decide whether this residual passes
the V2-O criterion.

Evidence:

- `sheets/panorama_40km.png` — 5 stations at 0/10/20/30/40 km, R-only
  and Candidate A w0.40. The large-mass arrangement is visibly similar
  across stations; A does not increase station-to-station difference.
- `sheets/aerial_8km.png` — 8 km aerial, R-only / A w0.25 / A w0.40.
  A w0.40 adds large-scale density variation but no new cloud morphology.
- `sheets/r_h3_sunset.png`, `w25mod_h3_sunset.png`,
  `w40mod_h3_sunset.png`, `b18_h3_sunset.png`;
  corresponding Storm/Overcast sheets.
- `sheets/r_h3_top.png`, `w25mod_h3_top.png`, `w40mod_h3_top.png`,
  `b18_h3_top.png`.
- `sheets/xz30_matched_transfer_density_v2.png` — 60 km transfer and
  density: direct R vs A w0.25/w0.40 vs B a0.18.

Blend Candidate A (w0.40/w0.50) is visually rejected: matched density
frames lose the Storm wedge and Sunset structure and look overcast.
Multiplicative A and B preserve detail and break the exact 4.53/9.06 km
render copy. The remaining 10 km layout similarity is the open question
for independent review.

**Exact-copy gate: A/B improved by ~10×.** Full V2-O visual gate:
READY FOR INDEPENDENT REVIEW, not self-accepted.

## H. Temporal Stability

200-frame fly sequences at 640×360 half/96, wind off; first 60 frames
before the pitched camera climbs out of the layer:

| Variant | mean HF | mean frame diff | max diff |
|---|---:|---:|---:|
| R-only | 0.365 | 2.88 | 3.68 |
| A w0.40 | 0.388 | 3.12 | 4.81 |
| B a0.18 | 0.351 | 2.14 | 2.78 |

No popping/shimmering spikes in the first 60 frames. Limitation: the
available `fly` motion follows the pitched view and leaves the layer
after ~60 frames, so it is not a clean horizontal 4.53 km tile-crossing
test. The 5-station panorama is the spatial-repeat evidence. A dedicated
level-flight motion remains a known gap.

## I. GPU Performance

RTX 5070 Laptop, 1080p half, 128/6, 120 frames, warmup 20, n=3,
matched H3 coverage (RB/R 0.45, A 0.4292, B 0.4320). Median cloud ms:

| Scene | RB | R | A w0.40 | B a0.18 |
|---|---:|---:|---:|---:|
| Clear | 3.603 | 3.363 | 4.700 | 4.176 |
| Overcast | 2.416 | 2.405 | 3.167 | 3.177 |
| Storm | 3.108 | 3.483 | 2.754 | 3.782 |

A adds one texture fetch: +30% Clear, +31% Overcast; Storm is cheaper
because the density distribution/early exit differs at matched mean
density, not because of a quality win. B adds ALU only: +16% Clear,
+31% Overcast, +22% Storm. R is within ±12%.

Storm step sweep, n=1 (indicative): RB 1.697/2.441/3.108 ms;
R 1.920/2.793/3.483; A 1.532/2.141/2.747; B 2.142/2.997/3.782 at
64/96/128. Full n=3 step matrix not run.

## J. DX12 Regression

- Clean Release/Debug build: PASS, no C/X/LNK warnings.
- CTest Release/Debug: 1/1 PASS.
- Source checks Python/PowerShell: PASS.
- fxc reference render/probe permutations: PASS (A02 branch included).
- Hardware selftest Legacy / RB / R / A w0.40 / B a0.18: PASS.
  Logs report `Reference field: multi-r` / `hash-mod`, reference render
  and probe PSOs.
- WARP functional selftest RB / A / B without GBV: PASS.
- WARP + `--gpu-validation`: exit 2, hardware-only limitation.
- Release GBV selftest Legacy / RB / R / A / B: PASS, 0 errors/0 warnings.
- Debug GBV selftest A / B: PASS, 0 errors/0 warnings.
- Release/Debug GBV 120 frames + resize A w0.40: 0/0;
  cloud output 469×242 -> 629×372, exit 0.
- Noise-field probe under GBV, A w0.40, 60 km XZ: 0/0, exit 0.
- Negative injected debug ERROR: FAIL, exit 1 (expected).
- F2/F5/P smoke A w0.40: slices↔clouds, clear→partly complete,
  animation paused/running, 0 errors/0 warnings, exit 0.
- Legacy V2.3 pixel identity: mean 0, max 0.
- Reference rb/plain H3 vs A01: mean 0, max 0.
- Shutdown: GPU idle, non-device objects 0; only device refcount warning.

## K. Known Issues

1. Exact 4.53/9.06 km render copies are broken, but the 10 km station
   panorama still shares a similar large-mass layout. Independent review
   must decide whether this residual passes V2-O.
2. Candidate A has the stronger slice decorrelation (transfer 0.487/0.554)
   but costs one extra texture fetch and +30% on Clear/Overcast.
3. Candidate B keeps the 4.53 km slice correlation at 0.85 but gives the
   largest 9.06 km final-render difference (21.6/255) and adds no fetch;
   its ALU cost is +16–31%.
4. Blend Candidate A loses Storm/Sunset structure; rejected.
5. The 60 km orthographic density map still shows a lattice of peaks;
   the final-render exact copy is nevertheless broken.
6. No clean horizontal tile-crossing sequence; `fly` follows the pitched
   view and leaves the layer. The 4.53/9.06 station render test is the
   direct period evidence. Add a level-flight motion next.
7. Performance step sweep is n=1; 128/6 is n=3.
8. N4 erosion was not started; it is the next phase after candidate
   acceptance. H5/H6, V2.4, lightning, surge and game DX12 not started.
9. No V2 PASS; no commit/push/merge.

## L. Verdict

**READY FOR V2.3h-A02 INDEPENDENT REVIEW.**

A02 tested the two allowed candidates against the R-only baseline:

- **R-only final render repeats exactly** at 4.53/9.06 km
  (0.995/1.669 mean abs, max 15/28).
- **Candidate A w0.40** breaks the exact copy to 10.55/11.69 mean abs
  (max 74/72), with the strongest slice decorrelation
  (transfer 0.487/0.554); it preserves detail better than the rejected
  blend, but adds one fetch and +30% on Clear/Overcast.
- **Candidate B a0.18** breaks the exact copy to 11.56/21.60 mean abs
  (max 62/91), keeps R-only detail, uses no extra fetch, and costs
  +16–31% ALU. It has the largest 9.06 km final-render difference.
- **Blend Candidate A** is rejected for loss of cloud structure.

Recommended primary for independent review: **B a0.18** — no extra
texture fetch, strongest 9.06 km break, preserves R-only morphology.
Candidate A w0.40 remains the alternative if the auditor weights the
slice autocorrelation more heavily.

Residual risk: the 10 km panorama still shows related large-mass layouts.
The exact 4.53/9.06 copies are gone, but full visual uniqueness is not
self-certified. N4 erosion is deferred until the base candidate is
accepted. V2 PASS не присваивался. Commit/push/merge не выполнялись.
