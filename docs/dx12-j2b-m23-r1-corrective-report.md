# F4.0-J2B-M23-R1 — corrective report

> **Статус: M23-R1 READY FOR CODEX.**
> Два P1 из независимого review исправлены на существующей feature-ветке.
> Mainline merge в `prypiat` **не выполнялся** и требует отдельного Codex
> READ-ONLY re-review + явного owner/Tech Lead GO.
> M23 остаётся experimental; M1-R2 safety-вердикт и историческое
> `-file_activity` ограничение не пересматривались.

- **Repository:** `rafaelisraelyan/OGSR-Engine-rtx`
- **Branch:** `feature/j2b-m23-four-real-jupiter-draws`
- **Reviewed HEAD:** `751037a5a17884c1ed5a031e1f0fe2cc54d55f2f`
- **Pinned M1-R2 base:** `2a0a35fb969b04a8e5519234ede63b639d908d4a`
- **P1 #1 commit:** `2f8cf3596` — `fix(dx12): quarantine M23 resources on unknown draw completion`
- **P1 #2 commit:** `3241ee5fd` — `fix(dx12): make M23 upload and draw recording exception-safe`
- **Дата evidence:** 2026-11-20

## 1. Границы R1

Исправлены только два блокирующих P1:

1. unknown draw completion не переводил M23 GPU-ресурсы в process-lifetime
   quarantine, из-за чего деструктор мог выполнить `Release` без
   подтверждённого last-draw-use fence;
2. исключение при записи upload command list (persistent OOM при построении
   `std::string` причины) обходило `Close`/poison и позволяло освободить
   записанные ресурсы при открытом списке; аналогичный участок draw-записи
   проверен и закрыт тем же guard.

M1-R2 CPU batch, VFS adapter, J1 single-visual, DX11/R4 renderer, PBR, DXR,
clouds, PhysX и J3 не изменялись.

## 2. P1 #1 — unknown completion quarantine

### Было

`FailPassUnknown` только выставлял `m_gpuUnknown`; `Shutdown` при этом флаге
сразу возвращался, а `~GameDx12FourVisual12` вызывал тот же путь. Поля
`ComPtr` (4 VB/IB, uploads, constants, root signature, PSO и captured
pass fence/device) в итоге освобождались без подтверждения GPU completion.

### Стало

- `FailPassUnknown` сразу вызывает `QuarantineGpu()` — не выделяющую память,
  идемпотентную операцию.
- `QuarantineGpu` переносит **все** M23 GPU-ссылки в process-lifetime
  quarantine: 4×VB/IB + uploads, constants, root signature, PSO, а также
  captured `m_passFence`/`m_passDevice` при неизвестном завершении.
- При уже занятой ячейке карантина используется `Detach` без `Release`;
  повторный unknown не освобождает удерживаемые ссылки.
- `Shutdown`, повторный `Shutdown`, `OnLevelTeardown`, `OnDeviceReset` и
  деструктор безопасны: unknown/poison/quarantined никогда не доходят до
  `ReleaseGpu`.
- Подтверждённое завершение last-draw-use fence по-прежнему освобождает
  ресурсы нормально.
- Owner/offscreen quarantine bridge сохраняется; bridge при poisoned list
  уходит в quarantine, а не в простой `DisableTerminal`.

### Инструментированное доказательство

Добавлен test-only build option `OGSR_M23_LIFETIME_PROBE`: каждый фактический
`ComPtr::Reset` (Release) и `Detach` учитывается, плюс created/captured/live.
`ph_dx12_m23_probe` пишет счётчики на unknown и на shutdown.

Финальная probe-сборка: `xrEngine_m23_r1_probe_final.exe`, 31 771 648 B,
SHA-256 `5E27F2E4F5CA1363C05474A780AE0A457E758A8572F3FFFC2BE01B1FFA81AF05`.
Все сценарии ниже запущены **одним** этим бинарём.

| Сценарий | created | captured | released | detached | live | quarantine calls / occupied |
|---|---:|---:|---:|---:|---:|---:|
| forced-unknown (до capture fence) | 19 | 0 | **0** | 19 | 0 | 2 / 1 |
| signal failure (после capture) | 19 | 2 | **0** | 21 | 0 | 2 / 1 |
| fence event failure | 19 | 2 | **0** | 21 | 0 | 2 / 1 |
| fence wait failure | 19 | 2 | **0** | 21 | 0 | 2 / 1 |
| device removed branch | 19 | 2 | **0** | 21 | 0 | 2 / 1 |
| normal confirmed fence (control) | 19 | 10 | 29 | 0 | 0 | 0 / 0 |
| upload OOM + recovery | 30 | 10 | 40 | 0 | 0 | 0 / 0 |
| upload error OOM + recovery | 30 | 10 | 40 | 0 | 0 | 0 / 0 |
| upload Close failure | 19 | 0 | **0** | 19 | 0 | 2 / 1 |
| draw Close failure | 19 | 0 | **0** | 19 | 0 | 2 / 1 |

`released=0` во всех unknown/poison сценариях; `live=0` означает, что
деструктору нечего освобождать. Второй вызов `QuarantineGpu` при shutdown
идёт в занятую ячейку (`quarantine_occupied_last=1`) и не освобождает
ресурсы. В normal control `released=29`, `detached=0`, `live=0`.

## 3. P1 #2 — exception-safe command-list recording

### Было

После успешного `list->Reset()` вызов `CreateGeometryBuffers` мог вернуть
`Fail(...)`, который пишет две `std::string`. При persistent OOM второе
исключение обходило `Close`/`m_listPoisoned`; внешний catch освобождал
GPU-объекты, а owner list оставался открытым и мог быть повторно
использован.

### Стало

- Добавлен `M23RecordingGuard12` — RAII guard для upload и draw recording.
- Guard активируется сразу после успешного `Reset`, не выделяет память и
  не трогает `std::string`.
- Любой обычный return или C++ exception до успешного `Close` вызывает
  `Abandon`/`Close`; при ошибке Close shared list терминально poisoned.
- До GPU submission незавершённые команды отбрасываются только после
  Close/poison. После submission ресурсы не освобождаются без fence.
- `LoadFromVfs`/`RenderDiagnostic` получили exception boundaries: unknown
  pass или poisoned list → quarantine; безопасный pre-submit сбой →
  нормальное освобождение.
- Runtime fault injection вынесен под test-only
  `OGSR_M23_FAULT_INJECTION`; в обычной feature-on сборке больше нет
  env-пути `OGSR_DX12_GAME_M23_FORCE_UNKNOWN`.

### Адресные fault-тесты (final probe binary)

- **upload OOM** после двух слотов: `upload exception boundary
  recording_closed=1 poisoned=0`; первый load fail; повторный Jupiter load
  в той же сессии → `four-visual frame PASS`, probes 30/10/40/0.
- **persistent OOM при построении ошибки**: тот же результат
  `recording_closed=1 poisoned=0` + recovery frame PASS.
- **upload Close failure**: `upload list close failed poisoned=1`;
  `released=0`, `detached=19`, bridge quarantine, no frame, no false PASS.
- **draw Close failure**: 4 draw записаны, Close failed, submission не было;
  `GPU completion NOT confirmed`, `released=0`, `detached=19`,
  poisoned=1, no false PASS.
- Debug Layer: `infoqueue phase=m23 verdict=PASS` до отказа; новых
  invalid command-list/lifetime ошибок в логах нет; poisoned list не
  освобождается и не переиспользуется.

## 4. Нормальный GPU control

Финальная ordinary feature-on сборка (без probe/fault):
`xrEngine_m23_r1_final_on.exe`, 31 768 576 B, SHA-256
`F044B5F53B80FEB6F10ECBFDDE7F7B6CB3DCE1C55D7063A1C09ADC0CF60C7264`.

```text
ph_dx12_m23: four-visual frame PASS level_visuals=9,105,106,107
draws=4 isolated_draws=4 upload_fence=2 combined_fence=3
last_draw_use_fence=7 cull=back rt=R8G8B8A8_UNORM depth=D32_FLOAT
readback=1 image_gate=1 mask_sum=38238 nonclear=38238/4096000
isolated_purity=1 png_saved=1
```

- visual 9: 440 px, bbox (483,430)–(822,476);
- visual 105: 30 657 px, bbox (1779,304)–(1962,555);
- visual 106: 1335 px, bbox (625,1025)–(756,1311);
- visual 107: 5806 px, bbox (1784,1029)–(1929,1290).

Combined PNG SHA-256:
`7300E4983DB2A769A982E754CD30516546D8D5A982386AE65477ABAFC0663429`.
Independent `verify_png_colors.ps1`: `M23_PNG_RESULT PASS failed=0`.
Debug Layer/GBV: `PASS`, warning=0 error=0 corruption=0 lifetime=0,
`consistent=1`. R4 Present capture сохранён.

J1 baseline с тем же бинарём: ровно один
`DrawIndexedInstanced(1314)`, fence 3, `nonclear=10623/4096000`,
`infoqueue phase=diagnostic verdict=PASS`, clean shutdown.

## 5. Пересобранные конфигурации

- **feature-on, clean `/t:Rebuild`** — `xrEngine_m23_r1_final_on.exe`,
  SHA-256 `F044B5F5...`, d3d12 imports = 1, d3d11 = 1.
- **feature-on + lifetime probe + fault injection, clean `/t:Rebuild`** —
  `xrEngine_m23_r1_probe_final.exe`, SHA-256 `5E27F2E4...`,
  d3d12 imports = 1, d3d11 = 1.
- **camera-on, `XR_3DA` rebuild** — `xrEngine_m23_r1_camera_on.exe`,
  31 784 448 B, SHA-256
  `FABE087DEFE9EF915B5617CA87BE0897C23A74BEEE695344AD0A49F5A3D6F44B`,
  d3d12 imports = 1.
- **feature-off — НЕ пересобиралась.** Изменённые translation units входят
  только в `OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE` ItemGroup; feature-off
  их не компилирует. Последняя clean feature-off сборка остаётся
  `xrEngine_m23_final3_off.exe`, 31 456 256 B, SHA-256
  `258025420F4A5612F21A13F88736109C67A96C5A5E84DE6D94781152FE0B4595`,
  d3d12 imports = 0. Она не затронута R1 и в R1 не пересобиралась.

## 6. Остаточные ограничения

- Independent Codex READ-ONLY re-review **ещё не выполнен**; этот отчёт
  описывает авторскую реализацию и evidence, а не независимую приёмку.
- M23 остаётся opt-in experimental diagnostic; DX11/R4 Present не заменён.
- M23 по-прежнему читает `$level$/level` + `level.geom` напрямую через
  `ReadVisual`/`BuildXrayLevelAsset12`; прямой M1 `BatchResult12`→GPU
  handoff не входит в R1 и остаётся будущим integration requirement.
- Историческое ограничение `-file_activity` (отдельные per-call locks на
  общем `g_open_files`) не менялось.
- Полный Windows независимый replay, TDR, resize во время in-flight кадра
  и M1/M23 full matrix в R1 не повторялись; выполнены только затронутые
  проверки.
- J3, PBR/текстуры, DXR, clouds, PhysX и mainline merge не затрагивались.

## 7. Evidence package

Каталог: `D:\OGSR_J2B_M23_R1_Evidence\`
Архив: `OGSR-J2B-M23-R1-Artifacts-2026-11-20.zip` (см. SHA-256 рядом).

Содержимое:

- `runs/r1c-*` — финальные fault/unknown/recovery/normal прогоны probe-сборки;
- `runs/r1c-normal` — probe normal control с PNG и `png_verification.txt`;
- `runs/r1-final-m23` — обычный four-visual кадр, PNG, verification, R4 capture;
- `j1/` — `preflight_r1-j1.log` + J1 readback PNG;
- `builds/` — clean feature-on, clean probe/fault, camera-on rebuild;
- `source/` — два патча, combined diff, git log, bundle;
- `binaries/` — три R1 EXE, hashes, imports;
- `scripts/` — run_r1, verify_png_colors, preflight;
- `F4.0-J2B-M23-R1-Corrective-Report.md`, `MANIFEST.sha256`,
  `verify_manifest.ps1`.

Проверка целостности — `verify_manifest.ps1` из корня архива.

## 8. Воспроизведение

```text
msbuild Engine.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 ^
  /p:OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE=true ^
  /p:OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE=false
```

Probe/fault build дополнительно:

```text
/p:OGSR_M23_LIFETIME_PROBE=true /p:OGSR_M23_FAULT_INJECTION=true
```

Запуск M23: `-dx12_game_bridge -log`, env
`OGSR_DX12_GAME_M23=1`,
`OGSR_DX12_GAME_DIAGNOSTIC_LEVEL=jupiter`,
`OGSR_DX12_GAME_M23_PNG_DIR=<dir>`.
Fault-сценарии: `OGSR_DX12_GAME_M23_FAULT=<name>` (только в probe/fault
сборке), для upload recovery — `OGSR_DX12_GAME_M23_FAULT_ONCE=1`, для
persistent error OOM — `OGSR_DX12_GAME_M23_ERROR_OOM=1`.
