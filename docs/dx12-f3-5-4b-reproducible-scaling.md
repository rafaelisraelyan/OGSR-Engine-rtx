# F3.5.4B — Reproducible Scaling Benchmark

**Дата:** 2026-10-20
**Baseline:** `de162d81d` (F3.5.4A)
**Статус:** инфраструктура и pilot выполнены; commit/push не делались
**Артефакты пилота:** `x64/ReleaseDX12/benchmark/f354b-pilot/`

---

## 1. Цель и границы

Цель — воспроизводимая методика измерения масштабирования D3D12 parallel
command recording, отделяющая:

- CPU-запись команд от GPU-работы;
- вариацию между кадрами от вариации между независимыми процессами;
- correctness-прогоны (Debug Layer + GBV) от performance-прогонов
  (`-nodebug -nogbv`);
- requested parallelism от фактически исполняющего.

Игровой renderer, DX11/R4, JobSystem API, registry и ownership GPU-ресурсов
не изменялись. Production render path не оптимизировался ради benchmark.

## 2. Инфраструктура

### 2.1. Runner

`ogsr_engine/Layers/xrRenderDX12/tests/run-scaling-benchmark.ps1`

- каждая конфигурация — отдельный процесс `xrDX12Smoke.exe`;
- порядок точек ротируется между повторами, чтобы одна и та же точка не
  оказывалась всегда первой или последней;
- correctness-фаза выполняется до performance; при её провале performance
  не запускается;
- все числа пишутся в invariant culture (точка как десятичный разделитель).

Выходные файлы:

| Файл | Содержимое |
|---|---|
| `fingerprint.json` | GPU, драйвер, git SHA, SHA256 exe и скрипта, resolution, CPU, OS |

Fingerprint фиксирует SHA256 самого бинарника, git SHA и `gitDirty`, а также
полный `git status --porcelain`. Для незакоммиченного кода `gitDirty=true`
и список изменений сохраняются в JSON; SHA256 бинарника остаётся точной
идентификацией выполненного кода. Performance-прогон считается валидным
только при совпадении фактического resolution с fingerprint; speedup
сопоставляется по draws и resolution.
| `correctness.csv` | hash, Draw count, validation, Debug/GBV по каждой конфигурации |
| `runs.csv` | один ряд на процесс: параметры, фактические workers, per-run статистика |
| `frames.csv` | все per-frame срезы (long format: metric/value) |
| `between.csv` | статистика по независимым запускам |
| `within.csv` | статистика внутри запусков (median IQR, median p95) |
| `speedup.csv` | speedup и parallel efficiency относительно legacy |
| `summary.json` | сводка, fingerprint, stability, все ряды |
| `logs/`, `raw/` | логи и сырые per-frame CSV каждого процесса |

### 2.2. Per-frame телеметрия

`CHW12` накапливает до 512 срезов в памяти:

```
frame, recording, scheduling, wait, submission, gpu, frame_interval
```

Никакого CSV/JSON и никакого per-frame логирования внутри измеряемого
участка нет. Один CSV пишется после `Shutdown`, когда кадры уже выполнены.
Лог процесса содержит только несколько строк на весь прогон.

GPU-время приходит из query timestamps с задержкой на несколько кадров;
индекс среза сохраняется в `FrameResource`, поэтому значение привязывается
к тому кадру, который его записал. В пилоте заполнены все 300/300 срезов.

### 2.3. Debug Layer и GBV

Добавлен runtime-switch до создания device:

- correctness: Debug Layer + GBV (по умолчанию);
- performance: `-nodebug -nogbv`.

`SetDebugLayer(false)` не создаёт Debug Layer, не создаёт InfoQueue и не
включает DRED. Лог пишет фактический режим, а runner проверяет его в каждом
performance-прогоне.

### 2.4. Worker semantics

- `-jobworkers 0` — автоматическая политика, а не «ноль исполнителей»:
  `backgroundWorkers=0`, `automaticWorkers=true`.
- `JobConfig` и `JobStats` задают `totalParticipants = backgroundWorkers + 1`;
  инициирующий поток всегда присутствует.
- `JobGroup::Wait()` исполняет pending jobs на вызывающем потоке, поэтому
  owner участвует в выполнении задач.
- Контрольный прогон `-jobworkers 0`: фактические `backgroundWorkers=6`,
  `totalParticipants=7`; requested `0` не равен фактическому `6`.

В пилоте использованы только явные `1/2/4`; для них фактические значения
совпали с запрошенными: `1→2`, `2→3`, `4→5` участников.

### 2.5. Speedup и efficiency

- Median/IQR/p5/p95 считаются с линейной интерполяцией (PERCENTILE.INC).
- Speedup считается только для `recording` и только против legacy-точки
  с теми же `draws`, resolution и условиями batch.
- `ParallelWidth = min(lists, totalParticipants)`.
- `ParallelEfficiency = speedup / ParallelWidth`; для multi и legacy ширина
  равна 1.
- `frame_interval` не используется как FPS и не входит в speedup.

## 3. Correctness

21 конфигурация: legacy `128/512/2048`; multi и parallel `128/512/2048` ×
lists `2/4/8`.

| Проверка | Результат |
|---|---|
| Debug Layer включён | 21/21 |
| GPU-Based Validation включён | 21/21 |
| RGBA hash равен legacy для того же draws | 21/21, `9dd37d87b8eb202d` |
| `[recording-draws] total == expected` | 21/21 |
| validation WARNING/ERROR/CORRUPTION | 0 |
| exit code | 0 во всех |

Дополнительно performance-конфигурация (`-nodebug -nogbv`, parallel
`d=2048, L=8, W=4`) прошла depth-oracle с тем же hash и 0 validation.

## 4. Pilot

- 19 точек × 5 независимых процессов = 95 запусков;
- 30 warmup + 300 измеряемых кадров;
- 95/95 валидны, 0 underfilled;
- полный прогон (correctness + performance) — 4.2 минуты;
- GPU: RTX 5070 Laptop, driver `32.0.16.1692`, 1280×720;
- performance: `-nodebug -nogbv`, фактические workers = requested.

Точки: legacy для каждого draws; multi `2048×{2,4,8}` и `128×8`; parallel
диагональ `(L,W) = (2,1),(4,2),(8,4)` для каждого draws, плюс срезы
`2048×L4×W{1,4}`, `2048×L2×W4`, `2048×L8×W4`.

## 5. Результаты

### 5.1. Recording, median между запусками (мс)

| Draws | Mode | Lists | W | Median | IQR | IQR/median |
|---:|---|---:|---:|---:|---:|---:|
| 128 | legacy | 1 | 0 | 0.0892 | 0.0018 | 2.0% |
| 128 | multi | 8 | 0 | 0.1832 | 0.0033 | 1.8% |
| 128 | parallel | 2 | 1 | 0.1903 | 0.0047 | 2.5% |
| 128 | parallel | 4 | 2 | 0.1896 | 0.0083 | 4.4% |
| 128 | parallel | 8 | 4 | 0.1924 | 0.0051 | 2.7% |
| 512 | legacy | 1 | 0 | 0.1419 | 0.0020 | 1.4% |
| 512 | parallel | 2 | 1 | 0.2172 | 0.0044 | 2.0% |
| 512 | parallel | 4 | 2 | 0.1978 | 0.0040 | 2.0% |
| 512 | parallel | 8 | 4 | 0.1999 | 0.0032 | 1.6% |
| 2048 | legacy | 1 | 0 | 0.3442 | 0.0008 | 0.2% |
| 2048 | multi | 2 | 0 | 0.3686 | 0.0015 | 0.4% |
| 2048 | multi | 4 | 0 | 0.3926 | 0.0055 | 1.4% |
| 2048 | multi | 8 | 0 | 0.4271 | 0.0031 | 0.7% |
| 2048 | parallel | 2 | 1 | 0.3132 | 0.0016 | 0.5% |
| 2048 | parallel | 2 | 4 | 0.3117 | 0.0080 | 2.6% |
| 2048 | parallel | 4 | 1 | 0.3270 | 0.0035 | 1.1% |
| 2048 | parallel | 4 | 2 | 0.2677 | 0.0031 | 1.2% |
| 2048 | parallel | 4 | 4 | 0.2537 | 0.0078 | 3.1% |
| 2048 | parallel | 8 | 4 | 0.2543 | 0.0047 | 1.8% |

### 5.2. Speedup и efficiency

| Draws | Mode | L | W | Speedup | Width | Efficiency |
|---:|---|---:|---:|---:|---:|---:|
| 128 | multi | 8 | 0 | 0.49 | 1 | 0.49 |
| 128 | parallel | 2 | 1 | 0.47 | 2 | 0.23 |
| 128 | parallel | 4 | 2 | 0.47 | 3 | 0.16 |
| 128 | parallel | 8 | 4 | 0.46 | 5 | 0.09 |
| 512 | parallel | 2 | 1 | 0.65 | 2 | 0.33 |
| 512 | parallel | 4 | 2 | 0.72 | 3 | 0.24 |
| 512 | parallel | 8 | 4 | 0.71 | 5 | 0.14 |
| 2048 | multi | 2 | 0 | 0.93 | 1 | 0.93 |
| 2048 | multi | 4 | 0 | 0.88 | 1 | 0.88 |
| 2048 | multi | 8 | 0 | 0.81 | 1 | 0.81 |
| 2048 | parallel | 2 | 1 | 1.10 | 2 | 0.55 |
| 2048 | parallel | 2 | 4 | 1.10 | 2 | 0.55 |
| 2048 | parallel | 4 | 1 | 1.05 | 2 | 0.53 |
| 2048 | parallel | 4 | 2 | 1.29 | 3 | 0.43 |
| 2048 | parallel | 4 | 4 | 1.36 | 4 | 0.34 |
| 2048 | parallel | 8 | 4 | 1.35 | 5 | 0.27 |

При 128 и 512 draw calls parallel-запись медленнее legacy: фиксированные
накладные расходы больше полезной работы. При 2048 появляется выигрыш до
~1.35–1.36×; он достигается на `L=4..8` и `W=2..4`. `L=2` упирается в два
списка, поэтому рост W не помогает.

### 5.3. GPU и постоянный frame interval

Для всех конфигураций `draws=2048`:

- GPU timestamp: `1.192–1.199 мс`, разница между режимами < 0.6%;
- frame interval: `3.332–3.334 мс`, практически одинаковая.

Это подтверждает эквивалентность GPU-работы. Одновременно это означает, что
pilot измеряет **recording**, а не FPS: GPU и Present pacing доминируют,
поэтому ускорение записи не превращается в ускорение кадра.

**Наблюдаемое ограничение среды.** Постоянные ~3.333 мс — это не vsync
(16.7 мс при 60 Гц) и не игровой frametime. Скрытое окно не участвует в
обычном композиционном цикле, а `Present(1,0)` возвращается с собственным
pacing драйвера/DWM. Величина одинакова для legacy, multi и parallel, поэтому
она полезна как индикатор постоянства условий, но не как метрика
производительности. Заявлять по ней FPS игры нельзя.

`recording` для parallel `L=8, W=4` раскладывается как `0.0238 scheduling +
0.1536 wait + ~0.077 residual`; submission `0.1008`; legacy submission
`0.0793`. `Wait` — основная видимая доля parallel-записи.

### 5.4. Вариация: кадры против запусков

`draws=2048`:

| Конфигурация | Between IQR | Within median IQR | Within median p95 |
|---|---:|---:|---:|
| legacy L1 | 0.0008 | 0.0787 | 0.3874 |
| parallel L4 W4 | 0.0078 | 0.0368 | 0.3145 |
| parallel L8 W4 | 0.0047 | 0.0630 | 0.3139 |

Медианы запусков стабильны, но отдельные кадры имеют заметный разброс.
Поэтому агрегировать нужно median по кадрам, а затем median по запускам,
а не среднее по всем кадрам подряд.

### 5.5. Воспроизводимость между сессиями

Три полных pilot-сессии (A, B, C) на одном build:

| Конфигурация | A | B | C | Spread |
|---|---:|---:|---:|---:|
| legacy 2048 | 0.3404 | 0.3422 | 0.3442 | 1.1% |
| parallel 2048 L4 W4 | 0.2471 | 0.2288 | 0.2537 | 10.9% |
| parallel 2048 L8 W4 | 0.2259 | 0.2273 | 0.2543 | 12.6% |
| parallel 128 L2 W1 | 0.1794 | 0.1517 | 0.1903 | 25.5% |

Legacy-базис воспроизводится хорошо; parallel-точки с малой работой
чувствительны к состоянию системы. Внутри одной сессии IQR/median ≤ 4.4%
(19/19 точек), но между сессиями разброс до ~13% на 2048 и до ~25% на
128. Это главный аргумент за несколько сессий, anchor-базис и bootstrap CI
по независимым запускам (не по кадрам) в полной Scaling Study.

### 5.6. Thermal drift

- температура: 50–53 °C;
- SM clock: 1507–2197 MHz;
- монотонного дрейфа нет: legacy 2048 по запускам `0.3367–0.3478 мс`,
  parallel 2048 L8 W4 `0.2115–0.2476 мс`;
- порядок точек ротировался, поэтому «первая точка всегда холодная» не
  воспроизводится.

Диапазон температур узкий, но мобильный GPU меняет SM clock в широком
диапазоне. Для полной серии нужен либо lock clocks, либо явная запись
clock-режима и поправка на anchor.

### 5.7. Статус доказательности pilot

Pilot подтверждает:

- инфраструктура воспроизводима: 95/95 валидных процессов, полный прогон
  повторяется, schema CSV/JSON стабильна;
- внутри одной сессии медианы запусков стабильны (IQR/median ≤ 4.4%);
- при 2048 draw calls parallel-запись быстрее legacy в наблюдаемых точках;
- GPU-работа эквивалентна между режимами (1.192–1.199 мс).

Pilot **не подтверждает**:

- полную масштабируемость на другие draws/lists/workers, другие GPU,
  драйверы и разрешения;
- production FPS или улучшение игрового кадра;
- устойчивость speedup между сессиями: наблюдаемый разброс 11–13% на 2048
  и до 25% на малой нагрузке;
- статистическую значимость: 5 запусков, CI не вычислялся; значения
  `1.35–1.36×` — точечные оценки, а не доказанный эффект.

Эти ограничения снимаются только полной Scaling Study по критериям раздела 8.

## 6. Обнаруженные проблемы

1. **Locale-баг агрегатора.** `Export-Csv` в русской локали писал `8,93`;
   повторное чтение с invariant culture давало `893`. Исправлено
   принудительной invariant culture в скрипте.
2. **Контракт warmup.** CHW12 исключает первые 30 кадров; при warmup < 30
   выборка молча пустая. Runner теперь требует `WarmupFrames >= 30`.
3. **`-jobworkers 0` — auto.** Запрошенный 0 даёт, например, 6 background
   workers и 7 участников. Requested нельзя использовать как фактический
   параллелизм.
4. **Низкая нагрузка шумит.** При 128/512 draw calls parallel медленнее
   legacy, а межсессионный разброс достигает 25%.
5. **Frame interval не равен FPS.** Скрытое окно не ограничено vsync;
   3.33 мс отражают Present pacing/GPU, а не игровой frametime.
6. **Debug Layer несопоставим с performance.** Correctness-прогоны с
   Debug+GBV дают другую стоимость; их нельзя смешивать с `-nodebug`.
7. **Повторная отрисовка кубов — синтетика.** Она не моделирует
   state changes, barriers и реальные draw packets.

## 7. Предложение для полной Scaling Study

1. **Матрица:** draws `{128, 512, 2048, 8192}`, lists `{2, 4, 8}`,
   workers `{1, 2, 4, 8}`; legacy и multi — контрольные точки. `W=0`
   только как диагностика auto-политики, не как точка efficiency.
2. **Повторы:** 3 сессии × 9 запусков на точку; 60 warmup + 600 кадров.
   Оценка: ~64 точки × 9 × 3 ≈ 1700 процессов, порядка 1–1.5 часа на
   текущей машине без correctness; correctness — отдельно, только
   Debug+GBV.
3. **Условия:** питание от сети, фиксированный power plan, отсутствие
   фоновой нагрузки, фиксированное разрешение, запись температуры и SM
   clock до/после каждого запуска, anchor legacy в начале и конце блока.
4. **Порядок:** блочная рандомизация с детерминированным seed; каждая
   сессия перемешивает точки, anchor повторяется.
5. **Статистика:** median/IQR по кадрам внутри запуска, median/IQR по
   запускам, bootstrap 95% CI для speedup. Единица ресемплинга — независимый
   запуск (его median), не кадр: кадры внутри запуска коррелированы и служат
   только описательной выборкой. Сравнивать с legacy той же сессии, а не с
   глобальным средним.
6. **Метрики:** recording, scheduling, wait, submission, GPU timestamp,
   frame interval; дополнительно PIX/ETW для GPU queue и CPU sampling.
7. **Нагрузка:** перейти от повторной отрисовки двух кубов к набору
   representative draw packets с разными PSO/descriptor/barrier patterns;
   иначе выводы ограничены command-list overhead.
8. **GPU-эквивалентность:** для каждой точки проверять, что GPU median
   совпадает между legacy/multi/parallel в пределах 5%.

## 8. Критерии приёмки полной серии

1. Targeted ReleaseDX12 build: 0 ошибок, 0 предупреждений.
2. Correctness: все выбранные конфигурации с Debug Layer + GBV; hash равен
   legacy, `DrawTotal == N`, 0 validation WARNING/ERROR/CORRUPTION.
3. Performance: каждый запуск `exit 0`, `debug=0 gbv=0`, фактический
   workers равен requested при `W>0`, ровно `MeasuredFrames` срезов.
4. Валидность: 9/9 запусков на точку; иначе точка помечается invalid.
5. Стабильность: between-run IQR/median ≤ 10% для `recording`; при
   превышении — повторить сессию, не «улучшать» результат.
6. Speedup: только против matched legacy; для утверждения «быстрее»
   нижняя граница bootstrap CI должна быть > 1.0.
7. GPU-эквивалентность: max/min GPU median для одного draws ≤ 1.05.
8. Thermal: anchor-точки в начале и конце сессии различаются ≤ 5% или
   результат нормализуется на anchor; температура/clock в допустимом
   диапазоне.
9. `frame_interval` не используется как FPS игры.
10. Артефакты полные: fingerprint, correctness, runs, frames, between,
    within, speedup, summary; сырые логи сохранены.

## 9. Изменённые файлы

- `ogsr_engine/Layers/xrRenderDX12/CHW12.h`, `CHW12.cpp`:
  per-frame `FrameTimingSample`, привязка GPU timestamp к срезу,
  `SetDebugLayer`, сброс/учёт срезов.
- `ogsr_engine/Layers/xrRenderDX12/xrDX12Smoke.cpp`:
  `-nodebug`, `-metrics <path>`, запись per-frame CSV после Shutdown.
- `ogsr_engine/Layers/xrRenderDX12/tests/run-scaling-benchmark.ps1`:
  новый runner, correctness/pilot, CSV/JSON, статистика, fingerprint.

DX11/R4, JobSystem API, игровой renderer и ownership GPU-ресурсов не
изменялись.

## 10. Ограничения

- Сцена — повторная отрисовка двух кубов; синтетическая нагрузка.
- 5 запусков на точку и 300 кадров — pilot, а не финальная статистика;
  p5/p95 по пяти медианам имеют широкую неопределённость.
- Скрытое окно: Present pacing ~3.33 мс, vsync не контролируется.
- Мобильный GPU без lock clocks; SM clock менялся в диапазоне
  1507–2197 MHz.
- Полная декартова матрица не запускалась.
- TDR/device removal, resize и fault injection в этот этап не входили;
  они покрыты регрессией F3.5.4A.
- Debug Layer + GBV использовались только для correctness.
