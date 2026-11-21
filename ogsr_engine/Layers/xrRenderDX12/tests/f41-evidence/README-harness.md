# Игровые проверки native DX12 и R4

Инструменты, которыми снимались доказательства этапов F4.1 (полная статическая
сцена Jupiter) и F4.1-E (сверка R4↔DX12). Скрипты лёгкие и переносимые:
пути к игре и данным передаются параметрами, ничего не «вшито» в бинарники.

## Состав

| Файл | Назначение |
|---|---|
| `run-f41.ps1` | готовит run dir (копия exe, dll, оверлей gamedata, appdata, `fsgame.ltx`), запускает игру, штатно закрывает, пишет `manifest.md` |
| `summarize-f41.ps1` | читает игровой лог прогона, пишет `summary.json`/`summary.md` |
| `decode-native-capture.py` | превращает `native-frames.rgba` в PNG-кадры + `manifest.json` с SHA-256 |
| `capture-window.ps1` | снимок окна процесса через `PrintWindow(PW_RENDERFULLCONTENT)`; нужен, потому что у R4 нет скриптуемого дампа кадра |
| `pack-screenshots.ps1` | сборка компактного архива «кадры + отчёты + сводки» с `SHA256SUMS.txt` |

## Типовой прогон

```powershell
$runs = 'C:\OGSR-evidence\F4.2\runs'
$dx12 = 'C:\OGSR-builds\f42-camera\xrEngine.exe'
$r4 = 'C:\OGSR-builds\f42-r4\xrEngine.exe'

.\run-f41.ps1 -Run jupiter-interior-dx12 -RunsRoot $runs -Exe $dx12 `
    -Save b23_jupiter -Capture
python .\decode-native-capture.py (Join-Path $runs 'jupiter-interior-dx12')
.\summarize-f41.ps1 -RunDirectory (Join-Path $runs 'jupiter-interior-dx12')

.\run-f41.ps1 -Run jupiter-interior-r4 -RunsRoot $runs -Exe $r4 -R4 `
    -Save b23_jupiter -WindowShot (Join-Path $runs 'jupiter-interior-r4\window.png') `
    -WindowShotDelay 45
.\run-f41.ps1 -Run zaton-r4 -RunsRoot $runs -Exe $r4 -R4 -Save b23_zaton
```

Пути в примере заменяются путями конкретного запуска из его манифеста.
Скрипт копирует EXE и доступные DLL из одного каталога сборки, готовит
отдельную копию `gamedata`, конфигурацию и сохранения. Архивы игры читаются
из `-ArchRoot`. Каждому прогону нужен новый `-Run` внутри `-RunsRoot`.

Парные кадры интерьера снимаются из одного сохранения и при одинаковом
размере клиентской области. Для движения можно записать путь штатной
командой `demo_record <имя>`, передать файл через `-Demo <файл.xrdemo>`
и воспроизвести его командой `demo_play <имя>` на обеих сборках.
`-Fly` меняет камеру только в DX12. Для снимка R4 при необходимости
задайте `-WindowShotReadyPattern` со строкой игрового лога, после которой
сцена готова. Кадр и его SHA-256 записываются в манифест запуска.

Штатное завершение подтверждается `cleanExit=True` и `closeForced=False`.
Для сравнения R4 также требуется проверить в логе факт загрузки уровня,
а в PNG наличие геометрии и целевой камеры. В DX12 дочитайте итоговую
запись InfoQueue до конца и проверьте наличие `debug=1`, `gbv=1`,
`gbvVerified=1` и нулевые счётчики warning/error/corruption/discarded.
Декодер сверяет 30 последовательных Present, номера слотов, fences и
размер readback. Для движущегося объекта дополнительно нужна привязка
изменения его world transform или bone pose к этому ряду Present.

## Переменные окружения движка, которые использует harness

| Переменная | Смысл |
|---|---|
| `OGSR_DX12_CMP=1` | включает `ph_cmp:`-логи сравнения (работает и в R4-сборке) |
| `OGSR_DX12_GAME_FLY` | свободная камера для доказательных кадров |
| `OGSR_DX12_GAME_SCENE_CULL`, `..._NOTEST`, `..._VIEW` | диагностика отсечения/alpha-test/режима вывода |
| `OGSR_DX12_NATIVE_CAPTURE_DIR` | каталог для `native-frames.rgba` |
| `OGSR_DX12_CLEAR` | цвет «неба» |

## Что не входит в репозиторий

Тяжёлые артефакты прогонов (копии `gamedata`, `native-frames.rgba`, PNG-серии,
бинарники сборки) намеренно не коммитятся — они воспроизводимы этими же
скриптами из тех же входных данных. См. `docs/dx12-f41-full-static-scene.md`.
