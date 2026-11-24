# F4.3-MT / FR3. Кандидат для ручной приёмки

24.11.2026. Локальная ветка `experiment/f43-mt-raster-integration`. Merge `7209e7220a58ef8f9222ca7f94fa20b4dba3121c` объединяет MT `74fe26ed3b142b02cd08abf4bf9b6320fe49e574` и FR3 `9e1384370256be7c5522b50df28ce6d6af008254`. Исправления ревью и сборка C++/HLSL проверены на `c6e973b0257df4d80e730fdb22e4f064e5b0c81e`. Последующие изменения launcher и отчёт отделены от исходников бинарника.

## Ревью

- `GameDx12StaticScene.cpp`. Автослияние поместило запись FR3 `DetailSunShadows` в общий теневой recorder. Там оставались запись mapped pose upload, вызов с прежней сигнатурой и ранний `return`, после которого prepared-путь пропускал траву. Владелец теперь строит detail packets и заполняет отдельные 512 матриц fence-слота. Любой путь записи sun получает тот же порядок пакетов. Прежние границы 12 м, frustum test, alpha-test и бюджет FR3 сохранены.
- `GameDx12Bridge.cpp`. Отдельный box probe активен для отсутствующего явно запрошенного ID и работает на владельце. GPU план по-прежнему получает результат штатного отбора. Сохранились журнал кадра, диагностика света и счётчик command lists. Время диагностического probe входит в CPU capture, поэтому сравнение FPS выполняется отдельными прогонами.
- C++/HLSL. Ручной receiver-plane tap использует белую границу, согласованную со статическим аппаратным sampler, и сохраняет сравнение `less_equal`. Флаг SSR помещён в конец `FrameCB`, C++ проверяет смещение поля и 2048-байтовый шаг слота. Проверены t6, интервалы дескрипторов воды и отдельный диапазон матриц травы.
- D3D12. Begin ждёт fence слота до Reset. Первый список устанавливает ресурсные состояния, списки карт записывают глубину, цветовой список переводит карты в SRV. Владелец проверяет результаты Close после GR Wait и подаёт упорядоченный batch на прежнюю очередь. GPU fence защищает resources и allocators. Пары дополнительных списков учитываются при quarantine. [Запись списков](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles) · [ExecuteCommandLists](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists) · [Reset allocator](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandallocator-reset)
- Launcher сохраняет флаги FR2/FR3 и режимы GR. Добавлены точечные A/B для SSR, sky-hemi и flora bounds, а также доступ к существующему журналу кадров.

## Результаты

- XR_3DA Release x64 PASS, 0 ошибок, 86 предупреждений. MSBuild 17.14, MSVC 14.44, SDK 10.0.26100.0, `OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE=true`, `OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE=true`. Выход `artifacts/F4.3-MT-native-gr/integration-release-runtime` в исходной копии: `xrEngine.exe`, PDB, OpenAL32 и NGX. SHA-256 `xrEngine.exe` — `F99983DB4DDDCE2664E7CE1968028CF5DB6E8E131ED0CF55FDF7350819EB6FFC`.
- FXC VS/PS 5.0 7/7, GR 388 и 430 проверок, модели JS 47 805 / 1926 / 345, PowerShell AST и `git diff --check` PASS.
- C++ fixture использует код native prep, culling и GR RunRanges из ветки с синтетическими snapshot/registry. Serial, Parallel и Verify с 0/1/2/4 background workers дали одинаковый хэш `5c3d0529931a775d` на 32 769 sources. Проверены пустой кадр, граница блока, BVH order, устаревшие session/epoch/generation, недействительные mesh/pose, повторное использование памяти и отказ при недоступном GR.
- WARP Debug Layer и GBV. 24 кадра с resize, отменой неполного batch, наблюдаемым GR callback exception и сравнением цвета и двух depth slices. Режимы legacy, multi, parallel 0 и 2 workers дают одинаковые данные. Два GR-прогона завершились 7/7 tasks с одним намеренным наблюдённым отказом и 0 unobserved. WARP test использует production `GameDx12NativeFrame.cpp` с тестовой обвязкой и синтетическими ресурсами.
- Логи, точные команды повторения, тестовые исходники и контрольные суммы сохранены в `artifacts/F4.3-MT-native-gr`. См. `manifest.md`, `integration-*.log` и `integration-release-sha256.csv`.

## Ручные прогоны Jupiter

Выходы обоих экспериментальных вариантов сохранены раздельно. `fsgame.ltx`, `$app_data_root$` и save `b23_jupiter` проверены для указанного GameRoot. Каждый вызов создаёт новый игровой процесс. Сохраняйте журнал каждого прогона до запуска следующего.

```powershell
$run = 'C:\Users\GR\.codex\worktrees\f43-mt-native-gr\OGSR ENGINE fork from rtx\tools\dx12\run-f43f-jupiter.ps1'
$root = 'C:\OGSR_ENGINE\OGSR_J1_Runtime_R2'
$exe = 'D:\Coding Project\OGSR ENGINE fork from rtx\artifacts\F4.3-MT-native-gr\integration-release-runtime\xrEngine.exe'

powershell.exe -NoProfile -ExecutionPolicy Bypass -File $run -GameRoot $root -Exe $exe -SceneLog -GR serial   -CommandLists legacy
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $run -GameRoot $root -Exe $exe -SceneLog -GR serial   -CommandLists multi
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $run -GameRoot $root -Exe $exe -SceneLog -GR parallel -CommandLists legacy -GRWorkers 2
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $run -GameRoot $root -Exe $exe -SceneLog -GR verify   -CommandLists parallel -GRWorkers 2 -Validation
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $run -GameRoot $root -Exe $exe -SceneLog -GR serial   -CommandLists parallel -GRWorkers 2
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $run -GameRoot $root -Exe $exe -SceneLog -GR parallel -CommandLists parallel -GRWorkers 2
```

Для независимого сравнения растровых изменений сохранить те же `-GR`, `-GRWorkers` и `-CommandLists` в обеих половинах пары. Вызовы одной и той же сборки:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $run -GameRoot $root -Exe $exe -SceneLog -GR verify -CommandLists multi
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $run -GameRoot $root -Exe $exe -SceneLog -GR verify -CommandLists multi `
  -LegacyLocalPriority -LegacySunPCF -LegacySkyHemi -LegacyWaterSSR -LegacyFloraBounds
```

Для локализации повторить с одним legacy-переключателем. `-LegacyLocalPriority` возвращает отбор прежним компаратором, `-LegacySunPCF` — аппаратное сравнение FR2, `-LegacySkyHemi` — прежний порядок и refresh лучей, `-LegacyWaterSSR` — остановку поиска после первого отклонённого crossing, `-LegacyFloraBounds` — исходную OGF header sphere. Для травы повторить пару с `-DetailSunShadows` и без него, включая сочетание с `-NoDynamicSunShadows`. Для диагностики проверить `-View ShadowFailureReason -LightId <id> -ProbeFrames 600`, `SunVisibility`, `SunDepthDelta`, `Hemi`, `WaterReflection`, `Ambient`, `SunDirect`, `LocalTotal`, `GeometricNormals`, `HDRClip` и существующие варианты. `-R4` доступен для отдельного прогона.

Сопоставлять одинаковые save, маршрут, погоду, время и разрешение. Проверять порядок теневых карт, число draw calls, изображение, resize, reload и shutdown. `ph_dx12_mt` даёт work items, workers, participants, Submit/Wait и fallback. `ph_dx12_mt_record` даёт число списков, маску проекций, фактических участников и пересечение интервалов записи. `ph_dx12_f43f: perf` сообщает длительности CPU/GPU, ожидание fence, Present, draw calls и число списков. `-Metrics` включает `ph_dx12_metrics` для первых 480 представленных сцен. Для p50/p95/p99 использовать одинаковое окно после прогрева, сравнивать `frame_ms`, `gpu_ms` с его `gpu_present`, и исключать `Verify` и `-Validation` из FPS-замеров. Сверить 0/1/2/4 фоновых workers и auto, размер блока по `-GRGrain`.

Статус Issues #32–#40 остаётся открытым. Реальный GPU Jupiter, визуальная приёмка, замеры TTAPI overlap и выигрыш MT6 остаются за владельцем. `Serial` и `legacy` остаются исходными режимами запуска, поэтому параллельное выполнение требует явного выбора.
