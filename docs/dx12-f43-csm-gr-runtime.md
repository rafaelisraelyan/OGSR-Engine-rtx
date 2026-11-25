# F4.3-CSM-GR — runtime Jupiter

26.11.2026 MSK. Проверен commit `1b2b545f30dafe6f3d774ed9d021dbd355c8be2a` на NVIDIA GeForce RTX 5070 Laptop GPU. Исходные игровые данные и save `b23_jupiter` читались из `C:\OGSR_ENGINE\OGSR_J1_Runtime_R2`. Копия EXE, `gamedata`, `appdata` и журнал находятся в `artifacts/F4.3-CSM-GR/jupiter-csm-quality-run/f43-csm-gr-quality`.

## Запуск

Native DX12 был включён через `-dx12_native_game`. Использовались `GR=parallel`, `COMMAND_LISTS=parallel`, два фоновых worker'а, `GR_GRAIN=256`, `SUN_PROFILE=quality`, четыре каскада 2048² на дальность 160 м, receiver-plane filter, lighting, dynamic sun shadows, local shadows, details и metrics/scene log.

## Результат

- Jupiter загрузился, native DX12 получил собственный HWND и swapchain, уровень `jupiter` перешёл в ready.
- Зафиксировано 16 отсчётов `ph_dx12_mt_record` на кадрах 120–1920. Во всех `mode=parallel`, `fallback=none`, `lists=14`, `views=12`, `mask=0xfff`, `participants=3`, `workers=2`, `submitted=1`, `completed=1`. Пересечение интервалов записи составило 18–19 пар.
- Зафиксировано 16 CSM-снимков. Все четыре каскада активны с одинаковыми split `0.200/15.181`, `15.181/40.150`, `40.150/100.075`, `100.075/160.000`, resolution 2048, filter 4 и receiver bias mask 3. На кадре 120 суммарно 7356 shadow draws, включая 570/916/1594/3083 по каскадам.
- Первый пробный запуск без feature-переменных lighting/sun/shadows дал только native static scene и не использовался как CSM evidence.

## Ограничения

Harness остановил второй процесс по таймауту после 70 секунд и записал `exit=-1`, `closeForced=True`. Это не штатный shutdown PASS. В журнале нет `ph_dx12_metrics`, `ph_dx12_f43f: perf` или GPU timestamp-строк, поэтому GPU-время и InfoQueue/GBV в игровом runtime не подтверждены. WARP Debug Layer + GBV и Release/HLSL проверки остаются отдельными PASS из манифеста F4.3.

Полный журнал: `artifacts/F4.3-CSM-GR/jupiter-csm-quality-run/f43-csm-gr-quality/appdata/logs/xray_codexsandboxoffline_26-11-26_02-20-52.log`.
