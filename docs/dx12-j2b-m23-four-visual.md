# F4.0-J2B-M23 — четыре реальных Jupiter visual в одном DX12-кадре

> **Статус:** экспериментальная реализация на feature-ветке, независимый
> Codex review и mainline merge **НЕ ВЫПОЛНЕНЫ**. Production `prypiat` не
> изменялся. M1-R2 остаётся в статусе «опубликованные исправления, полная
> независимая приёмка PENDING».
>
> **Обновление R1 (2026-11-20):** два P1 из независимого review исправлены
> коммитами `2f8cf3596` и `3241ee5fd`; targeted fault/lifetime evidence и
> probe-матрица — в `docs/dx12-j2b-m23-r1-corrective-report.md`. M23
> по-прежнему не принят и не смержен; нужен focused Codex re-review.
>
> **Ветка:** `feature/j2b-m23-four-real-jupiter-draws`
> **База (M1-R2 HEAD):** `2a0a35fb969b04a8e5519234ede63b639d908d4a`
> **Реализация M23:** `88ebf0bef6c3d6072b316e361249f168fd302018`,
> hardening `6cfd8d939c01f7b5026f07b03cd810d1a4beb0ee`, gate/lifetime fix
> `a75b414ef`
> **Дата evidence:** 2026-11-19

## 1. Цель и результат

Цель M23 — не «четыре вызова Draw в логе», а реальный DX12 readback-кадр, в
котором присутствуют четыре настоящих GPU-объекта Jupiter: визуалы
**9, 105, 106, 107**, с подтверждённым вкладом каждого.

Результат: в одном owner command list записаны ровно четыре
`DrawIndexedInstanced` (индексы 1314 / 312 / 192 / 876), кадр выполнен на GPU,
прочитан обратно, сохранён combined PNG и четыре isolated PNG. В combined PNG
каждый визуал имеет уникальный цвет, ненулевое число пикселей и собственный
bbox; isolated PNG каждого визуала содержит только его цвет, а число пикселей
совпадает с combined. Debug Layer / GBV: `warning=0 error=0 corruption=0
lifetime=0`.

Это диагностический debug-flat кадр, а не PBR-материал и не полный игровой
renderer. J3, DXR, облака и PhysX в M23 не входят.

## 2. Что было проверено до начала M23 (M1-R2 smoke)

- `j2b_vfs_registration_fault.exe` без `-file-activity`: PASS.
- `j2b_vfs_registration_fault.exe --file-activity`: PASS, включая
  `bad_alloc` / persistent `bad_alloc` / `runtime_error`, отсутствие
  опубликованного слота, adapter fail-closed и баланс handles.
- `j2b_m1_batch_cpu.exe` на реальных `jupiter_level.bin` +
  `jupiter_level_geom.bin`: PASS 32/32, включая GR parallel, partial submit,
  drain/reuse, worker exception, OOM и session/epoch negatives.
- Runtime Zaton → Jupiter с `-dx12_game_bridge -file_activity`: J1 Draw
  visual 9, fence 3, 10 623 ненулевых пикселя, GBV PASS, clean shutdown.

Эти проверки подтверждают, что критический VFS/GR/runtime путь M1-R2 не
сломан, но **не заменяют** независимую приёмку M1-R2.

## 3. Архитектура M23

### 3.1 Отдельный bounded registry

Новый `GameDx12FourVisual12` не использует и не изменяет:

- J1 single-visual `GameDx12LevelGeometry12`;
- synthetic `GpuMeshRegistry12` (три синтетические mesh-слота);
- J2A CPU preparation.

Registry содержит ровно четыре слота. Для каждого визуала один раз читаются
`$level$/level` и `$level$/level.geom`, выполняется `ReadVisual` и
`BuildXrayLevelAsset12`. Жёстко проверяются M0-счётчики:

- visual 9 — 545 вершин / 1314 индексов;
- visual 105 — 99 / 312;
- visual 106 — 120 / 192;
- visual 107 — 385 / 876.

Создаются четыре независимые пары VB/IB (stride 32, `R16_UINT`) с
upload-буферами. Все четыре пары загружаются одной транзакционной
submission. GPU handles проверяются на ненулевое значение и попарное
различие; каждый handle и transform логируются.

### 3.2 Diagnostic staging

Авторские 105/106/107 — крошечный кластер рядом с visual 9, а сам visual 9 —
тонкая проволочная геометрия. Поэтому для доказательства вклада каждый
авторский asset переносится в свой слот сетки 2×2 в NDC перед реальной игровой
камерой:

- центр слота получается unprojection реального `Device.mFullTransform`;
- объект ставится на расстоянии 12 м по лучу камеры;
- масштаб подбирается так, чтобы максимальный projected AABB extent был
  `0.36` NDC;
- из четырёх yaw (0/45/90/135°) выбирается вариант с максимальной projected
  площадью, чтобы тонкие пластины не были edge-on;
- итоговый transform повторно проецируется и отклоняется, если выходит за
  viewport/ depth range или пересекается с другим слотом.

Масштаб, yaw, NDC-bbox, depth slab, tint и полная world-матрица пишутся в лог.
Это **диагностическая расстановка**, а не утверждение о реальном размещении
объектов в уровне; геометрия, индексы, bounds и камера — настоящие.

### 3.3 Pipeline

- VS: X-Ray FVF `0x112`, stride 32, `position/normal/uv`;
- PS: debug-flat `Tint` (уникальный цвет — identity oracle, не материал);
- RTV `R8G8B8A8_UNORM`, DSV `D32_FLOAT`;
- depth `LESS`, depth write all, `FrontCounterClockwise=FALSE`;
- culling `CULL_BACK` (default; env `OGSR_DX12_GAME_M23_CULL=none` — только
  для диагностического сравнения);
- viewport/scissor — размер игрового кадра (в evidence 2560×1600).

### 3.4 Пять submission и fences

Одна owner command list и один allocator. Все submission используют один
owner fence/device, и каждая фиксирует точное значение:

1. upload всех четырёх VB/IB — `upload_fence=2`;
2. combined pass: ровно 4 DrawIndexedInstanced — `combined_fence=3`;
3. isolated visual 9 — fence 4;
4. isolated visual 105 — fence 5;
5. isolated visual 106 — fence 6;
6. isolated visual 107 — fence 7.

`last_draw_use_fence=7` — после него VB/IB больше не используются. Только
после подтверждения `fence->GetCompletedValue() >= 7` и
`GetDeviceRemovedReason() == S_OK` ресурсы могут быть освобождены. Каждый pass
дополнительно проверяет identity fence/device, захваченные при submission.

Combined pass — это именно четыре draw в одной list. Четыре isolated pass —
отдельные диагностические draw (по одному), они не входят в счёт «четырёх
Draw одного кадра» и явно логируются как `combined=0`.

### 3.5 Readback и per-visual oracle

Combined readback сканируется на четыре ожидаемых UNORM-цвета:

- visual 9 — (255, 64, 64);
- visual 105 — (64, 255, 64);
- visual 106 — (77, 115, 255);
- visual 107 — (255, 230, 64).

Критерии PASS:

- каждый цвет присутствует в combined;
- bbox четырёх цветов попарно не пересекаются;
- в isolated PNG каждого визуала есть только его цвет;
- `combined_pixels == isolated_pixels` для каждого визуала;
- сумма четырёх цветов равна общему non-clear числу.

Последнее равенство — сильная проверка: combined и isolated pass используют
один transform, pipeline и viewport, поэтому вклад объекта не может быть
подменён или замаскирован соседом. Runtime-гейт проверяет это напрямую:
isolated pass считается по tint-маске своего визуала (любой чужой non-clear
пиксель = FAIL), а сумма четырёх combined-масок должна равняться общему
non-clear числу кадра (`mask_sum == nonclear`, `isolated_purity=1`).

### 3.6 Lifetime, reset, unload, quarantine

- `OnLevelTeardown` освобождает registry только при подтверждённом upload и
  отсутствии незавершённого pass; иначе — quarantine.
- `OnDeviceReset` обновляет epoch/generation. Reset **до** первого Draw
  разрешает rebind того же уровня/сессии; reset **после** Draw rebind
  запрещает, повторный кадр не выполняется.
- Unknown completion (timeout, потеря identity, forced-fault) переводит все
  M23 GPU-объекты в process-lifetime quarantine: `Release` и `CloseHandle`
  не вызываются.
- J1 single-visual и DX11/R4 не затрагиваются, если `OGSR_DX12_GAME_M23`
  отсутствует.

## 4. Runtime-флаги

- `-dx12_game_bridge` — обязательный opt-in bridge (как у J1).
- `OGSR_DX12_GAME_M23=1` — включить M23 вместо J1 single-visual.
- `OGSR_DX12_GAME_DIAGNOSTIC_LEVEL=jupiter` — обязательная привязка к уровню.
- `OGSR_DX12_GAME_M23_PNG_DIR=<dir>` — каталог для combined/isolated PNG.
- `OGSR_DX12_GAME_M23_CULL=back|none` — culling (default `back`).
- `OGSR_DX12_GAME_M23_FRAME_DELAY=<frames>` — test-only задержка one-shot
  кадра для reset-before сценария.
- `OGSR_DX12_GAME_M23_FORCE_UNKNOWN=1` — test-only инъекция unknown
  completion для проверки quarantine.

## 5. Evidence

### 5.1 Clean build matrix (Release x64, commit `a75b414ef`)

- feature-off: `xrEngine_m23_final3_off.exe`, 31 456 256 bytes,
  SHA-256 `258025420F4A5612F21A13F88736109C67A96C5A5E84DE6D94781152FE0B4595`,
  `d3d12.dll` imports = 0, `d3d11.dll` = 1.
- feature-on: `xrEngine_m23_final3_on.exe`, 31 766 528 bytes,
  SHA-256 `27D401CD94DF8E7A36B31B10F752C9A39D0CB5312FFABDFDCAC72256FA85E0F7`,
  `d3d12.dll` imports = 1, `d3d11.dll` = 1.
- camera-on: `xrEngine_m23_final3_camera_on.exe`, 31 782 400 bytes,
  SHA-256 `CAA0EF6CAAF040A29588F17144A77D7BFB84F0F5CC92ED8DB1444E9423AA740A`,
  `d3d12.dll` imports = 1, `d3d11.dll` = 1.

### 5.2 Основной кадр (`final3-m23`)

```
ph_dx12_m23: four-visual frame PASS level_visuals=9,105,106,107
draws=4 isolated_draws=4 upload_fence=2 combined_fence=3
last_draw_use_fence=7 cull=back rt=R8G8B8A8_UNORM depth=D32_FLOAT
readback=1 image_gate=1 mask_sum=38246 nonclear=38246/4096000
isolated_purity=1 png_saved=1
```

Per-visual (combined == isolated, tint-specific count):

- visual 9: 435 px, bbox (481,430)–(822,476), 1314 индексов;
- visual 105: 30 656 px, bbox (1779,304)–(1962,555), 312 индексов;
- visual 106: 1337 px, bbox (625,1025)–(756,1311), 192 индекса;
- visual 107: 5818 px, bbox (1784,1029)–(1929,1290), 876 индексов.

PNG SHA-256:

- combined: `3A6FE4BE70F8032C134B1E9B821370DAD82136D4565C26768CF80B650D5FD46F`;
- isolated 9: `A85B83BE982D8EBDC640FC5AD0577C03404D7DDC88555D72872A23608CD58F47`;
- isolated 105: `68FCF5A83A6CB50C916E3A0917E5076B8CE0CB8B9B130EB208ACBD63382A994C`;
- isolated 106: `BD59DA3FA4D82A486E0D6AA5A5A45864830C94CCE4D020B96A9BCC12AAB93B8B`;
- isolated 107: `634CEAEE7F0776F95FD268920D516A35BBA52FEFB9E94D475EA64F19A859718E`.

Независимый пересчёт PNG (не по логу) подтвердил размеры 2560×1600, счётчики,
bboxes, отсутствие чужого цвета в isolated PNG, попарную непересекаемость и
равенство суммы цветов non-clear. Отчёт: `png_verification.txt`, результат
`M23_PNG_RESULT PASS failed=0`.

R4 Present того же запуска: `r4_present_final3-m23.png`, 3 775 037 bytes,
SHA-256 `2B994911F06DB4B46EE20794E4414FA76F003F784AE738FC8156CE2E2C494FD4`.

### 5.3 Debug Layer / GBV

```
infoqueue phase=m23 verdict=PASS scope=m23 debug=1 gbv=1 gbvVerified=1
stored=61 retrievable=61 read=61 readFailures=0 consistent=1
warning=0 error=0 corruption=0 unknown=0 malformed=0
completion=1 lifetime=0
```

### 5.4 Reset

- **Reset-before** (`final3-reset-before`): `vid_restart` после upload, но до
  первого кадра. Лог: `rebound after device_reset level='jupiter' session=2
  epoch=4 previous_epoch=3`; кадр PASS на `epoch=4`, `combined_fence=3`,
  `last_draw_use_fence=7`.
- **Reset-after** (`final3-reset-after`): кадр PASS, затем `vid_restart`.
  `DRAW_BEFORE=4 DRAW_AFTER=4`; повторного кадра нет, rebind refused
  `reason='one-shot M23 attempt was already consumed'`, чистый shutdown.

### 5.5 Unload / Zaton

`final3-unload`: Jupiter frame PASS → `level teardown level='jupiter'
session=2 epoch=5` → `level ready level='zaton' session=3 epoch=6` →
`ph_dx12_m23: skipped: level='zaton' requested='jupiter'` → teardown Zaton →
`shutdown complete`. Quarantine не сработал, утечек и повторного M23-кадра нет.

### 5.6 Quarantine

`final3-quarantine` с `OGSR_DX12_GAME_M23_FORCE_UNKNOWN=1`: четыре combined
draw реально записаны, completion намеренно не подтверждён. Логи:
`GPU completion unknown stage='forced-fault'; M23 resources are retained for
process lifetime`, `GPU completion NOT confirmed`, bridge quarantine
`DX12 ownership is quarantined until process exit (no Release, no
CloseHandle)`, на shutdown — `quarantined objects are not released`,
`shutdown complete`. Краша/повторного использования нет.

### 5.7 Сохранение J1 и J2A

- J1 (`final3-j1`, M23 off): ровно `recorded DrawIndexedInstanced visual=9
  indices=1314`, `fence=3`, `nonclear=10623/4096000`, bbox
  (1129,1099)–(2108,1599), `infoqueue phase=diagnostic verdict=PASS`,
  clean shutdown.
- J1+J2A (`final3-j1j2a`, M23 off): J1 один Draw 10 623 px; J2A
  `sector=129 root=17431 instances=5 unique=5 packets=4 visible=4 culled=1`
  `hash=EC3EAADD55DFCD6C prepared=1`, `J2A_FAIL_COUNT=0`.

### 5.8 DX11/R4

- feature-off, Zaton New Game: `LevelResources load completed`,
  `End of synchronization A[1] R[1]`, `ph_dx12_count=0`, R4 Present capture.
- feature-on без `-dx12_game_bridge`: `disabled: runtime flag absent`, DX11/R4
  Zaton без создания DX12-устройства, clean shutdown.
- M23 runtime использует тот же DX11/R4 swapchain и Present; combined/
  isolated изображения — отдельный offscreen readback, не подмена Present.

### 5.9 CPU-тесты

- `j2b_m23_plan_cpu.exe`: PASS 59/59 — контракт плана, уникальность tint,
  staging для четырёх реальных bounds, disjoint slots, depth range, отказ
  singular VP / нулевых bounds / NaN-камеры, colour masks.
- `j2b_m1_batch_cpu.exe` на реальных блобах: PASS 32/32.
- `j2b_m1_accept_cpu.exe`: PASS.
- VFS registration fault off/on: PASS (см. §2).

## 6. Ограничения и notes

- Visual 9 — тонкая проволочная геометрия: 440 px в кадре 2560×1600. Это
  реальный вклад (не ноль, isolated PNG, равенство счётчиков), но не
  «красивый» силуэт. Visual 106 — четыре небольшие площадки (1335 px).
- Combined PNG — debug-flat identity image, не материал: текстуры, normal/
  PBR, освещение и полный X-Ray shader в M23 не реализованы.
- Диагностическая расстановка в сетке 2×2 не является авторским placement
  уровня. Для реального размещения нужен отдельный milestone.
- Четыре isolated pass добавляют четыре однопиксельных по смыслу draw; они не
  входят в combined-четвёрку и предназначены только для oracle.
- M1-R2 остаётся непринятым независимо; M23 не закрывает его P2 и не
  разрешает mainline merge.
- Полный `Engine.sln` Debug x64, TDR, resize/reload во время in-flight кадра
  и независимый Windows replay не выполнялись.

## 7. Файлы реализации

- `ogsr_engine/Layers/xrRenderDX12/dx12fourvisualplan.h` — CPU-контракт плана,
  staging, проекции, masks (header-only, покрыт CPU-тестом).
- `ogsr_engine/xr_3da/GameDx12FourVisual.h/.cpp` — bounded registry, upload,
  pipeline, пять pass, fences, lifetime/quarantine, readback PNG.
- `ogsr_engine/xr_3da/GameDx12VisualMask.h` — per-colour mask oracle.
- `ogsr_engine/xr_3da/GameDx12Bridge.cpp` — opt-in M23 caller, reset rebind,
  teardown/shutdown integration; J1 путь не изменён при M23 off.
- `ogsr_engine/xr_3da/XR_3DA.vcxproj` — новый translation unit только под
  `OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE`.
- `ogsr_engine/Layers/xrRenderDX12/tests/j2b_m23_plan_cpu_main.cpp`,
  `build-j2b-m23-plan-cpu.cmd` — CPU acceptance.

## 8. Воспроизведение

```
msbuild Engine.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 ^
  /p:OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE=true ^
  /p:OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE=false
```

Запуск: `xrEngine.exe -dx12_game_bridge -log` с окружением
`OGSR_DX12_GAME_M23=1`, `OGSR_DX12_GAME_DIAGNOSTIC_LEVEL=jupiter`,
`OGSR_DX12_GAME_M23_PNG_DIR=<каталог>`; New Game → `load b23_jupiter`.

Ожидаемые строки: `ph_dx12_m23: four-visual frame PASS ... draws=4
isolated_draws=4 ... image_gate=1`, `infoqueue phase=m23 verdict=PASS`, затем
`shutdown complete`.

