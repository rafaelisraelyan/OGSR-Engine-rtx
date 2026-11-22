# V2.3h-A03b-R01 — Region-Based 3D Occupancy Feasibility

Статус: **BLOCKED на R2 (CPU geometry)**. GPU-этап не запускался по stop
rule. Legacy V2.3 и контрольные A03 M1 / S2new не изменялись.
V2 NOT ACCEPTED.

Ветка: `feature/dx12-volumetric-clouds`, HEAD
`42b8124e538ef4e40229a155203c133ca412756c`.

## A. Git State & Backup

- Snapshot: `D:\Coding Project\OGSR_BACKUP_BEFORE_V23H_A03B_R01_2026-10-31`,
  MANIFEST SHA256
  `BDD2EB7143F7A77ACA5649551106FD0C7C4E002C0BD26DE6986F4C5A4F26ED36`
  (295/295, restore byte-identical, 105 untracked).
- A03b baseline: source `3cc798a5…` (122 файла), package
  `EDDF4782…`.
- R01 source/patch hashes: см. `AUDIT_BASELINE.txt` пакета.
- `.gitignore` включён в baseline; patch R01 не должен его добавлять.
- Git: uncommitted, 0 staged; commit/push/merge не выполнялись.

## B. Baseline Identity & Metadata

- C0 после R01-сборки: Legacy, RB, R, A, B, M1, S2old, S2new и M1 OD
  **9/9 byte-identical**.
- Release EXE `4574A4F6…`.
- Исправлен handoff-дефект metadata `occupancy_probe_float32.json`:
  теперь local 0.65·N(2400)+0.35·N(1000), LocalMean4 ±7200 м,
  macro anomaly weight 0.12, `occupancy_cut` вычисляется из coverage
  и gamma 1.2, а не задаётся устаревшей строкой.
  Для coverage 0.45 cut = 0.047375.
- R01 GPU variant отсутствует; DX12 PASS для него не заявляется.

## C. Region Architecture

CPU-модель: jittered sites на XZ-сетке 7 км, два site на ячейку,
3×3 neighborhood (18 sites/sample).

Для site:

```text
cx,cz  = jittered center (0.15..0.85 cell)
cy     = layer bottom + 0.18..0.82 * thickness
rx,rz  = 0.28..0.55 * cell
ryt/ryb= asymmetric top/bottom vertical radii
yaw    = hash orientation
box    = 0.10..0.80 (ellipsoid 0 → rounded box 1)
warp   = a·rx·rz + b·zy·rz (smooth non-ellipsoidal bulge/tilt)
d      = lerp(ellipsoid, max-box, box) + warp
f      = 1 − d
F      = max(f over 18 sites)
occ    = smoothstep(cut−0.05, cut+0.05, F)
```

Deterministic, world-space, no NoiseVolume period, no camera dependence,
no unbounded loop, no new texture. Wind can be applied by translating XZ
before the lattice lookup.

## D. Offline 3D Geometry

CPU prototype: `tools/VolumetricCloudsTest/scripts/proto_r01_regions.py`.
Slices: XZ at y=2500, XY at z=0, and XZ column-max. 16 км при 100 м,
60 км при 300 м.

### 60 км

![R01 60 km column max](evidence/xzmax_60km.png)

На 60 км видны **регулярные ряды** rounded polygon/blobs. Это не
случайное поле, а квази-решётка с шагом ~7 км. Два site на ячейку
уменьшили, но не устранили артефакт.

### 16 км

![R01 16 km column max](evidence/xzmax_16km.png)

16 км содержит 8–9 компонент: одна крупная масса (largest 19–40%)
и несколько мелких фрагментов. Формы — выпуклые rounded ellipsoid/box,
без многомасштабной внутренней структуры. XY-срез показывает простые
вертикальные овалы, а не многоуровневые 3D-массы.

### Quantitative

| Test | Value |
|---|---:|
| 60 км occupied columns | 33.6% |
| 60 км components | 88 |
| 60 км largest component | 4.3% |
| 60 км autocorr at 6.9 км | 0.165 |
| 16 км components, seed 1337 | 8 |
| 16 км largest component | 39.8% |
| 16 км components, seed 9529 | 9 |
| 16 км largest component | 19.2% |

## E. Seed Robustness

Fixed cut 0.45, 16 км:

| seed | occupied cols | occupied vox |
|---|---:|---:|
| 1337 | 36.2% | 7.3% |
| 4242 | 32.9% | 7.2% |
| 99 | 34.9% | 9.2% |
| 2027 | 29.5% | 9.9% |
| 9001 | 36.3% | 10.3% |
| 9529 | 33.8% | 9.1% |

Численный seed-разброс мал (29.5–36.3%), alias 1337/9529 отсутствует.
Но это не спасает геометрию: все seed дают тот же класс rounded islands.

## F. Early GPU Cost Gate

**NOT RUN.** R2 stop rule: модель выглядит как набор rounded blobs и
квази-регулярная решётка на 60 км. Перенос в HLSL не выполнялся, GPU
время R01 не измерялось. Нельзя заявлять cost PASS для несуществующего
варианта.

## G. Weather Occupancy

**NOT RUN.** Нет GPU-варианта; CPU cut 0.35/0.45/0.55 даёт 48/36/25%
колонок, но геометрия уже rejected.

## H. Uniqueness

**NOT RUN на GPU.** CPU 60 км поле показывает квази-решётку; это хуже,
чем отсутствие уникальности — это регулярный артефакт. Raw xzmax Float32
включён.

## I. Storm Feasibility

**NOT RUN.** S2new не изменялся. Region model не дошла до GPU/Storm.

## J. Temporal Stability

**NOT RUN.** Нет кандидата для flight/wind.

## K. DX12 Regression

R01 GPU-вариант не создавался. Выполнен только baseline identity 9/9 и
пересборка metadata fix. Полный DX12 PASS для R01 не заявляется.

## L. Known Issues

1. Регулярная решётка на 60 км: один/два site на ячейку задают шаг ~7 км;
   jitter и два site не устраняют ряды.
2. Формы выпуклые rounded blobs; нет многомасштабной/internal structure.
3. 16 км даёт 8–9 компонент с одной доминирующей массой; распределение
   размеров бедное.
4. XY-срез — простые вертикальные овалы, нет stacked/multi-level 3D.
5. Увеличение числа sites/noise вернёт стоимость к A03b без гарантии
   геометрии; по stop rule GPU не запускался.
6. Metadata A03b исправлена; `.gitignore` включён в baseline.

## M. Verdict

- G — **BLOCKED** (R2): rounded islands + lattice.
- C — NOT RUN (нет GPU-варианта).
- U — NOT RUN (CPU lattice уже rejected).
- P — NOT RUN (нет GPU-варианта).
- S — NOT RUN (S2new сохранён).
- W — NOT RUN.
- T — baseline identity 9/9; R01 full PASS не заявляется.
- **V2 — NOT ACCEPTED.**

Следующий допустимый шаг — отдельная архитектура с иерархическими/
multi-scale region sites или иным способом устранения lattice; текущая
R01 модель не переносится в HLSL.
