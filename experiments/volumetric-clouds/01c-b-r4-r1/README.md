# OGSR RTX — V-WEATHER-01C-B-R4-R1 package

Пакет содержит кандидат **R4-R1**: исправления двух P2 из независимого ревью
R4 (интерактивная многокадровая диагностическая безопасность и доказанное
перенаправление stdout/stderr), уточнение точности fault activation,
исследование transition-shader под GPU-based validation и повторные регрессии.

## Что читать в первую очередь

| Файл | Содержание |
|---|---|
| `REPORT.md` | основной отчёт: что было не так, что сделано, доказательства, статусы gate'ов, ограничения |
| `WORKSPACE_MANIFEST.md` | манифест workspace, история прогонов и раскрытие инцидентов |
| `exact_commands.txt` | точные команды воспроизведения всех наборов |
| `environment.txt` | ОС, CPU/GPU, драйверы, toolchain |
| `NO_PRODUCTION_CHANGE.txt` | подтверждение, что production/HLSL/roadmap не менялись |
| `SHA256SUMS.txt` | хэши всех файлов пакета |

## Структура

```
REPORT.md, WORKSPACE_MANIFEST.md, README.md, NO_PRODUCTION_CHANGE.txt,
exact_commands.txt, environment.txt, SHA256SUMS.txt, package_verification.txt
source/                     полный набор исходников кандидата (standalone_01c_b_r4_r1)
source_required/            ключевые файлы, на которые ссылается отчёт
patch/                      инкрементальный патч R4 → R4-R1 + манифест патча
evidence/                   сырые доказательства: матрица 58 кейсов (с witness.json
                            и pipe_status.json на кейс), static/PNG, R2-контроль,
                            runtime, CLI, GBV, исследование transition+GBV,
                            recheck/ (пересдача кейсов при инциденте с GPU)
validation/                 source evidence, хэши шейдеров и EXE, CTest-логи,
                            hook isolation, binaries/ (5 собранных EXE)
reproduction/               скрипты сборки, прогонов, парсинга, патча и пакета
```

## Как проверять

```powershell
python verify_01c_b_r4_package.py OGSR_V_WEATHER_01C_B_R4_R1_Interactive_Diagnostic_Corrections.zip
```

Верификатор проверяет: целостность ZIP, наличие и содержание обязательных
файлов, суммы SHA256SUMS, результаты всех наборов против сырых доказательств
(включая `pipe_status.json` на каждый blocked-кейс), 23 структурные
source-проверки, изоляцию fault-хуков в EXE, побайтовую применимость патча к
чистому baseline R4, а также отсутствие в отчёте заявлений, не подкреплённых
доказательствами.

## Важные ограничения

- Это standalone-контракт: game adapter/UI/CEnvironment не входят.
- Исторический visual gate **NOT MET**: 4 exact / 12 drifted / 188 каналов /
  max 1 LSB при `rain_sf` 4/4. Baseline не переписывался.
- Transition shader под GPU-based validation: **NOT RUN / unresolved** —
  кадр валидируется (770/772 с, 0 errors/0 warnings), но процесс не выходит.
  Причина измерена, план воспроизведения — в
  `evidence/gbv_transition_investigation/`.
- Независимого прогона не было: все результаты авторские.
