# V2.3 — Physically Motivated Cloud Lighting & Atmospheric Perspective

Статус: **READY FOR V2.3 INDEPENDENT REVIEW**.
V2 PASS не присваивался. Ветка `feature/dx12-volumetric-clouds`,
HEAD `42b8124e5`; V2.3-изменения остаются в рабочем дереве.

База: V2.2b checkpoint (`OGSR_V2_2b_Checkpoint.zip`, SHA256
`8CA83A874DFCAF9E1F4DE3337AA1BFD6350F660EA8D594DE5088FA92AA458564`).

## 1. Что изменено и что нет

Изменён только standalone-инструмент `tools/VolumetricCloudsTest`.
Игровой DX11/DX12, `xrCore`, `xrRenderDX12`, `xrRender_R4`, GR Job System,
F4.0, PhysX, ODE, Surge Manager и погодные скрипты не затрагивались.
Merge в `prypiat` не выполнялся, коммита и пуша нет.

Точный delta, stat и SHA256 патча — в `PACKAGE_README.md`,
`AUDIT_BASELINE.txt` и `git_diff_stat_v2_2b_to_v2_3.txt` пакета.

Геометрия облаков (V2.2b density) намеренно не менялась: все A/B освещения
сняты на одной и той же плотности, камере, seed, time и weather seed.

## 2. Архитектура освещения

Один проход `CloudRayMarch.hlsl` на кадр, один constant buffer на back buffer,
существующие UAV/SRV, без новой очереди и без `WaitForGpu()` на кадр.

На каждый occupied sample:

```text
density rho(x)            — одна функция геометрии для камеры и теней
sigma = sigma_ext * rho   — extinction
tau_sun = integral rho ds — вдоль clipped sun segment
T_sun = exp(-sigma_ext * tau_sun)
MS    = a1 exp(-b1 tau) + a2 exp(-b2 tau)
sun   = S * [T + MS (1 - T)] * phase * weather terms
sky   = f(height, density, sky zenith/horizon)
amb   = ambientColor + skyLight, occluded by height and local density
L     = integral T_cam (1 - exp(-sigma ds)) (sun + amb) T_atm(s)
```

### V2.3A — clipped sun optical depth

Было: `stepLength = VolumeThickness() / shadowSteps`, луч не ограничивался
реальным объёмом, а вертикальное солнце интегрировало лишний путь.

Стало:

```hlsl
bool SunRaySegment(position, out t0, out t1)
{
    t1 = distanceCap;                       // Lighting.x, default 6000 m
    if (sunDir.y >  eps) t1 = min((VolumeTop()    - y) / sunDir.y, cap);
    if (sunDir.y < -eps) t1 = min((VolumeBottom() - y) / sunDir.y, cap);
    // near-horizontal sun: cap only
}
```

`t0 = 0`, `t1` всегда ≤ `sunShadowDistance`; шаги равномерно распределены по
`[t0, t1]`, число шагов — `Lighting.y` (1..16, default 6).
Плотность внутри отрезка считается той же функцией, что и primary path.

Проверки:

- нулевая плотность → optical depth = 0 (selftest, `coverage=0`);
- рост extinction не увеличивает transmittance (selftest: 2.5× extinction
  даёт средний T 0.150 против 0.230);
- толстый объём ослабляет сильнее тонкого (тот же тест);
- near-horizontal sun (0.5°) конечен, T ≤ 1;
- нормированный shadow path ≤ 1.0 (selftest: 0.635; при 5° — 1.0,
  при 75° — ~0.31, см. `v23_lighting/sun_path_*`);
- смена азимута солнца меняет картинку (selftest, threshold 1e-4).

Debug: `--debug-mode=sun-depth`, `sun-transmittance`, `shadow-path`,
`atmosphere-transmittance`.

### V2.3B — одна геометрия для камеры и теней

Было: `SampleCloudDensityInternal(position, detailed)`; при `detailed=false`
`baseNoise` заменялся константой, а он участвует в domain warp и в локальной
высоте основания/вершины. Тень видела другую форму, чем камера.

Стало: единственная `SampleCloudDensityInternal(position)`.
`SampleCloudDensity` и `SampleCloudDensityForLight` — две обёртки над ней,
чтобы контракт был явным. Debug `shadow-density-diff` пишет
`saturate(|rho_primary - rho_light| * 1000)`; selftest читает картинку и
требует max < 1e-6 (фактически 2.38e-7, FP-шум инлайна).

### V2.3C — direct, multiple scattering и skylight

- Direct: `S * T_sun * phase`, HG с `g` из пресета, 30% изотропной доли.
- Multiple scattering: две экспоненты с уменьшенной extinction,
  `0.16 exp(-0.15 tau) + 0.06 exp(-0.04 tau)`, и формула
  `T + MS (1 - T)`, чтобы полностью освещённая точка не становилась ярче 1.
- Skylight: `lerp(SkyHorizon, SkyZenith, 0.15 + 0.85 h)`, смешанный с
  `ambientColor`; occluded по высоте (`0.28..1.0`) и локальной плотности
  (`exp(-1.5 rho)`). Дымка не подменяет плотность: ambient меняется
  максимум в ~2.5 раза, а не в 10.
- `--lighting=sun-only|ambient-only|full` — изоляция вкладов для A/B.
- `--ablation=no-shadow` — контроль, что дефект не прячется светом.

### V2.3D — атмосферная перспектива

Убраны эмпирические `distanceFade` на каждый sample и пост-фактум
`horizonHaze = 1 - exp(-t1 * 0.000035)`.

Аналитическая модель:

```text
tau_atm(d) = beta * exp(-h_avg / H) * d
T_atm(d)   = exp(-tau_atm)
L_atm      = AtmosphereColor(rayDir) * (1 - T_atm)
```

`h_avg` — средняя высота отрезка камеры и точки; `beta` = 1e-4 1/m,
`H` = 6000 m (defaults). `AtmosphereColor` побайтово повторяет `SkyColor`
из `CloudComposite.hlsl` (включая sun glow), поэтому дымка перед облаком
совпадает с небом за ним.

Композит остаётся прежним: `sky * T_total + cloudRGB`.
`T_total = T_cloud * T_atm`, а `cloudRGB` уже содержит
`L_atm_front`; при `T_cloud = 1` сумма даёт ровно `sky`, при `T_cloud = 0` —
дымку. Двойного ослабления нет: при `coverage=0` кадры с `--atmosphere=on`
и `off` побитово совпадают (проверено, mean diff 0, max 0).

Тесты: горизонт без облаков, далёкие/близкие облака, взгляд вдоль слоя,
камера внутри слоя, камера выше облаков, низкое солнце, overcast/storm
horizon — см. `v23_lighting/sheet_atmosphere.png`.

### V2.3E — jitter и временная стабильность

- `--jitter=off|spatial`; `off` — центры шагов, `spatial` — детерминированный
  interleaved-gradient offset по пикселю (V2.2b-поведение).
- `--sequence=DIR --frames=N --motion=static|pan-slow|pan-fast|up|fly
  --wind=on|off --camera=x,y,z --camera-yaw= --camera-pitch=`.
- Для каждой последовательности пишутся BMP-кадры и `sequence_metrics.txt`:
  high-frequency energy (|pixel - 4-neighbour average|) и frame-to-frame diff.

Ограничение: полноценного temporal reprojection/denoiser нет.
Метрики измеряют зерно и остаточную разницу, а не TAA-стабильность.

### V2.3F — quality profiles

`--profile=performance|balanced|quality|reference`:

| Profile | Steps | Shadow steps | Cloud scale |
|---|---:|---:|---|
| performance | 64 | 2 | half |
| balanced | 96 | 4 | half |
| quality | 128 | 6 | half |
| reference | 128 | 6 | full |

Явные `--steps`, `--shadow-steps`, `--cloud-scale` имеют приоритет.

## 3. DX12

Архитектура V1/V2 сохранена: per-frame cloud texture на back buffer,
per-frame constants, fences, существующий command list, стабильные
descriptors, UAV/SRV barriers, timestamp queries, resize, shutdown.
Новых текстур, readback-ресурсов и очередей нет. Constant buffer вырос с
16 до 19 float4 (добавлены `Lighting`, `Atmosphere`, `LightingFlags`);
размер по-прежнему кратен 256 байтам.

## 4. Производительность

### Методика

- 1920×1080, half- или full-resolution cloud output, 120 кадров,
  `--warmup=20` → ровно 100 timestamp samples на прогон.
- Burn-in 30 с, 3 повтора на конфигурацию, конфигурации чередуются.
- Перед/после каждого прогона пишутся SM clock, power и temperature
  (`nvidia-smi`).
- Laptop RTX 5070 чередует power state в диапазоне 25–112 W; разброс внутри
  одной конфигурации достигает 5.8×. Для относительных сравнений используется
  best-case (наименее throttled) прогон; сравнивать числа между сессиями
  нельзя.

### Quality profiles (1080p, best-case cloud ms)

| Profile | Steps | Shadow | Scale | Clear | Overcast | Storm |
|---|---:|---:|---|---:|---:|---:|
| performance | 64 | 2 | half | 1.931 | 1.561 | 0.685 |
| balanced | 96 | 4 | half | 2.655 | 3.054 | 1.509 |
| quality | 128 | 6 | half | 4.178 | 4.790 | 2.642 |
| reference | 128 | 6 | full | 12.597 | 16.849 | 9.528 |

Quality — рекомендуемый режим. Reference (full-res) в 3–3.5× дороже и не
является обязательным игровым режимом.

### Step sweep (1080p half, shadow 6)

| Steps | Clear | Overcast |
|---:|---:|---:|
| 32 | 1.524 | 1.707 |
| 64 | 2.545 | 2.794 |
| 96 | 3.437 | 3.837 |
| 128 | 4.213 | 4.770 |

### Shadow sweep (1080p half, 128 steps)

| Shadow steps | Clear | Overcast |
|---:|---:|---:|
| 1 | 2.907 | 2.343 |
| 2 | 3.205 | 2.844 |
| 4 | 3.684 | 3.861 |
| 6 | 4.149 | 4.627 |

Тень стоит ~40–50% кадра при 6 шагах; 2–4 шага — рабочий компромисс для
performance/balanced.

### V2.2b vs V2.3 (interleaved, одна сессия)

V2.2b собран из checkpoint-исходника и запускался сразу после V2.3 на той же
сцене; 1080p half, 128 steps, 120 frames, warmup 20, best-case:

| Preset | V2.2b cloud ms | V2.3 cloud ms | Ratio |
|---|---:|---:|---:|
| Clear | 3.489 | 3.926 | 1.13 |
| Overcast | 4.322 | 4.337 | 1.00 |
| Storm | 2.161 | 2.520 | 1.17 |

Стоимость нового освещения и атмосферы — **+0–17%**, а не кратное увеличение.
Прежние 7.7/9.3/5.2 ms из V2.2b-сессии измерены в другом power state и для
сравнения не используются.

Полные данные: `performance_v23.txt` (все прогоны + clocks),
`performance_v23_summary.txt` (min/median/max/spread),
`performance_v22b_vs_v23.txt`.

## 5. Temporal stability

12 последовательностей по 30 кадров (1280×720 half, кроме `pan_slow_full`),
`sequence_metrics.txt` + `sheet_temporal.png`:

| Scenario | HF energy | Frame diff |
|---|---:|---:|
| static, jitter spatial | 0.3048 | 0 |
| static, jitter off | 0.2961 | 0 |
| pan slow, jitter spatial | 0.3049 | 0.922 |
| pan slow, jitter off | 0.2957 | 0.926 |
| pan fast | 0.3049 | 5.645 |
| camera up | 0.3094 | 0.497 |
| fly through | 0.3114 | 0.787 |
| pan slow + wind | 0.3049 | 0.903 |
| 32 steps | 0.7601 | 0.959 |
| 64 steps | 0.4158 | 0.935 |
| 96 steps | 0.3293 | 0.927 |
| 128 steps (half) | 0.3049 | 0.922 |
| 128 steps (full) | 0.5407 | 0.952 |

- Spatial jitter добавляет ~3% high-frequency energy на 128 шагах; разница
  static on/off — 0.172/255 в среднем (max 12). Это не источник заметного
  зерна.
- Доминирует число шагов: 32 шага дают 2.5× HF относительно 128 и видимые
  полосы на `sheet_temporal.png`; 96/128 практически совпадают.
- Full-res HF выше из-за разрешённых деталей, а не шума; прямое сравнение
  half/full по HF некорректно.
- Fast pan / up / fly / wind не дают скачков; frame diff определяется
  движением, а не sampling.
- Temporal reprojection/denoiser не реализован.

## 6. Final audit corrections (V2.3 final delta)

### V23-A01 — clean Debug build

Заблокированный прогон использовал Debug EXE, собранный до фикса
constant buffer (256 байт). GBV показывал CBV byte 287 / 256. Выполнена
полностью чистая сборка Release+Debug в `build-v23-final` с пересозданием
shader artifacts.

- Debug EXE SHA256:
  `09EAB0794E83618E54F1D04CB84A38DDF04E3604A471B5ABACF74204A9373B64`
- Release EXE SHA256:
  `CA9000C66218253BB73C01E752D595B98AEBB42F074D924B6D5FF57D9017234B`
- Debug+GBV selftest 64³: `final debug errors: 0`,
  `final validation verdict: PASS`, exit 0.
- Debug+GBV actual frames 120: 0 errors/warnings, exit 0.
- Release+GBV actual frames 120: 0 errors/warnings, exit 0.

### V23-A02 — final validation verdict

`RunSelfTest` теперь:

1. ждёт завершения GPU после cloud selftest;
2. дренирует `ID3D12InfoQueue` (`PollDebugMessages`);
3. пересчитывает `EvaluateValidationStatus` по накопленным сообщениям;
4. возвращает FAIL (exit 1) при любой validation ERROR;
5. `Run()` проверяет `DebugErrorCount()` после shutdown;
6. evidence scripts требуют `final debug errors: 0` и
   `final validation verdict: PASS` для validation-прогонов.

Негативный тест `--test-inject-debug-error` с работающим debug layer
инъецирует один ERROR после cloud selftest: итоговый verdict FAIL, exit 1,
shutdown totals 1 error. Это не PASS и не INCONCLUSIVE.

### V23-A03 — sun below horizon

`multiScatterVisibility` и sun glow в атмосфере умножаются на тот же
`sunAboveHorizon`, что и direct sunlight; ambient skylight независим.

Selftest: SunOnly при -10° с `--atmosphere=off` → max rgb 0;
SunOnly при +5° → mean rgb 0.1305; AmbientOnly при -10° → mean rgb 0.0189.
Серия `lighting/sheet_sun_horizon.png` покрывает -10/-5/0/5/34/75.

## 7. Известные ограничения

1. Overcast/Storm underside всё ещё содержит крупные вертикальные «шторы» —
   это форма density (V2.2b), не lighting. Свет теперь читает её объём,
   но не перестраивает.
2. Полноценного temporal reprojection/denoiser нет; jitter остаётся
   детерминированным.
3. `weatherEvolution` не реализован (V2.4).
4. Нижняя полусфера — атмосферная дымка без геометрии уровня; это не
   интегрированная атмосфера X-Ray.
5. Weather tile 4096 может повторяться на дистанции.
6. Нет cloud shadows на игровой геометрии, молний, выбросов, DXR.

V2 PASS не присваивался. Merge в `prypiat` не выполнялся.
