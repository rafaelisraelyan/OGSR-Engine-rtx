# F4.1 / F4.1-E evidence harness

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
# DX12, свободная камера, захват кадров
.\run-f41.ps1 -Run f41-final-a-actorcam -Capture -Fly '-120,60,120,-30,35,0'

# сводка по логу прогона
.\summarize-f41.ps1 -RunDirectory .\runs\f41-final-a-actorcam

# сравнение R4 <-> DX12 (инвентарь, отправленные draw, спавн-объекты)
.\run-f41.ps1 -Run f41-cmp -Cmp

# R4-половина: та же сборка без DX12-моста + снимок окна
.\run-f41.ps1 -Run f41-r4 -R4 -Cmp -Exe .\bin\xrEngine-f41e-off.exe `
    -WindowShot .\runs\f41-r4-shot.png -WindowShotDelay 45
```

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
