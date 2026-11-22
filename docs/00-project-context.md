# Контекст проекта OGSR-Engine RTX (снапшот)

> Сводный контекст для входа в проект. Обновлён: 2026-11-20.
> Срез рабочей копии: ветка `feature/dx12-volumetric-clouds`, HEAD `02d2a003d`
> («docs: introduce agent workspace and storage policy»).
> Назначение: быстрый вход без повторного сканирования репозитория.
> Правила работы: корневой `AGENTS.md` + `docs/agent-workspace-storage-policy.md`.
> Репозиторий: <https://github.com/rafaelisraelyan/OGSR-Engine-rtx> (fork от
> `OGSR/OGSR-Engine`, default branch `prypiat`).
> Актуальная дорожная карта: `docs/08-roadmap.md` в `origin/prypiat` (GitHub);
> локальная копия может отставать от удалённой.

---

## 1. Актуальные директории проекта

### 1.1 Верхний уровень

```text
OGSR ENGINE fork from rtx/
├─ .github/                 CI: push.yml, dependabot.yml
├─ 3rd_party/               вендоренные и скачиваемые зависимости (22 папки)
│  ├─ Src/                  24 подкаталога + *.props
│  ├─ _LIB/                 собранные библиотеки
│  └─ _TEMP/                промежуточные объекты
├─ artifacts/               манифесты и отчёты независимых review
│  ├─ F4.0-J2B-M0-independent-review/
│  ├─ F4.0-J2B-M1-independent-review/
│  └─ F4.0-J2B-M1-R1-independent-review/
├─ bin_x64/                 runtime-артефакты: xrEngine.exe, PDB, nvngx_dlss.dll, OpenAL32.dll, dbghelp.dll
├─ docs/                    основная документация проекта
├─ Game/                    игровые данные и Lua
│  ├─ Lua_Extensions/
│  ├─ Resources_CoP_1.6.02/gamedata/
│  ├─ ShadersSln/
│  └─ .editorconfig
├─ ogsr_engine/             исходники движка
├─ tools/
│  └─ VolumetricCloudsTest/ отдельный DX12-стенд облаков
├─ x64/                     сборочные каталоги и логи этапов F3.x
├─ j12b-independent-worktree/     отдельный worktree J1.2B
├─ j12b-review-evidence/          evidence J1.2B
├─ j12b-r2-review-evidence/       evidence J1.2B Round 2
├─ reports/                 отчёты review/анализов (F4*, R03A) + evidence/ (ZIP+SHA256)
├─ Engine.sln               решение: Debug|x64, Release|x64, ReleaseDX12|x64
├─ OgsrBuildProps.props     общие MSBuild-свойства
├─ Update_Components.cmd    клонирование 12 зависимостей
├─ cleanup.cmd
├─ LICENSE.md
└─ AGENTS.md                правила работы с кодом
```

Отчёты review и анализов вынесены в `reports/` (12 документов: `F4*`, R03A,
`DX12_RTX_Analysis.md`), а парные ZIP-пакеты evidence `OGSR_V2_3h_A03b_*`
с `.sha256` — в `reports/evidence/` (7 пар, 14 файлов; крупнейший
`..._Local_Occupancy_Storm_Topology.zip` ~100 МБ, целостность сверена
по SHA-256 после переноса). Корень очищен от review-материалов; в нём
остаются только untracked `.obj`-файлы прошлых прогонов (`b3b1_*`, `b3b2_*`,
`probe_*`, `live_producer.obj`, `debug_meta.obj`, `test-audit.obj`),
`probe-build.log` и пустой файл `0` (8 байт). Это результаты прошлых прогонов;
удалять их без отдельного согласования нельзя по политике хранения.

### 1.2 `ogsr_engine/` — модули

| Каталог | Роль |
|---|---|
| `xrCore/` | VFS (`FS.*`, `LocatorAPI.*`, squashfs), математика, память, XML/INI, логи, Tracy |
| `xrCDB/` | collision detection: `CDB::MODEL`, Opcode, ray/box/frustum queries |
| `xrSound/` | OpenAL Soft + EFX |
| `xrParticles/` | `.ope`-партиклы |
| `xr_3da/` | фронтенд: `WinMain`, `CApplication`, `CRenderDevice`, окно, ввод, консоль; здесь же DX12 game bridge |
| `xrGame/` | игровая логика, AI, UI, сеть, физическая обвязка |
| `xrServerEntities/` | скриптовый движок, ALife, серверные сущности, фабрика объектов; своего `.vcxproj` нет |
| `xrPhysics/` | ODE-слой (`CPHWorld`, `CPHElement`, `CPHShell`, ragdoll-обвязка) |
| `xrNetServer/` | сеть на DirectPlay 8 |
| `Luabind/` | пропатченный luabind |
| `LuaJIT/` | LuaJIT 2.1.0-beta3 + FFI |
| `Layers/` | рендерер и job system |
| `Include/xrRender/` | публичные абстрактные интерфейсы рендера |
| `build_config_overrides/` | локальные подмены `build_config_defines.h` |
| `_LIB/`, `_TEMP/` | результаты сборки |

### 1.3 `ogsr_engine/Layers/` — рендерер

| Каталог | Состояние |
|---|---|
| `xrRender/` | API-независимое ядро, ~217 исходников |
| `xrRenderDX10/` | реализация DirectX 11, ~53 исходника |
| `xrRenderPC_R4/` | `CRender`, кадр, render targets, фазы, ~52 исходника; **текущий игровой renderer** |
| `xrRenderDX12/` | экспериментальный DX12-слой: `CHW12`, camera, mesh, packet plan, render prep, ordering, transforms, geometry registry, X-Ray asset loader, quarantine, J2B batch, тесты |
| `xrRenderPC_R5_DX12/` | `xrRender_R5_DX12.vcxproj` — пока StaticLibrary, не игровой модуль |
| `xrAPI/` | тонкий API-слой |
| `JobSystem/` | `gr_job_system` — пул задач и GR Job System |

### 1.4 `Game/`

```text
Game/
├─ Lua_Extensions/                 рукописная Lua-библиотека
├─ Resources_CoP_1.6.02/gamedata/  оверлей OGSR для CoP: scripts, configs, levels
└─ ShadersSln/                     IDE-проект для HLSL
```

### 1.5 `3rd_party/Src/` — зависимости

Вендорены: `d3dx`, `eax`, `imgui`, `lzo`, `ode`, `OpenAL_Soft`, `RenderDoc`,
`reshade`, `tracy`, `Xiph`. Качаются скриптом: `concurrentqueue`, `cpputils`,
`DirectXMath`, `DirectXMesh`, `DirectXTex`, `DiscordRPC`, `FidelityFX-SDK`,
`libsquashfs`, `lz4`, `mimalloc`, `NVIDIA_DLSS`, `zstd`.

Ключевое: `NVIDIA_DLSS` — SDK `v310.9.1`; `FidelityFX-SDK` — форк OGSR с
DX11-бэкендом, FSR 3.1.2; `imgui` 1.92.9 WIP; `mimalloc` 3.5.1; `DirectXMesh`
клонируется, но не используется.

### 1.6 `tools/VolumetricCloudsTest/`

Изолированный стенд, не зависящий от X-Ray, `xrCore` и игрового рендера:

```text
tools/VolumetricCloudsTest/
├─ src/            Application, Renderer, CloudRayMarcher, CloudProfiler, WeatherMap, NoiseTypes
├─ shaders/        CloudRayMarch, CloudComposite, SliceViewer, DensityVolume, FormulaF1–F5,
│                  PrototypeA/B, Reference, A03a/A03b, WeatherGenerate
├─ scripts/        анализ шума, покрытия, optical depth, ROI, R01/R02 prototype, benchmark,
│                  проверки evidence (Python/PowerShell/C#)
├─ tests/          unit-тесты диагностики
├─ artifacts/
├─ build*/         множество сборочных каталогов V2.2–V2.3h
├─ CMakeLists.txt
├─ README.md
└─ REPRODUCE.md
```

Практически все `shaders/`, `scripts/` и `src/`-файлы — untracked: рабочий
материал V2.3h. Сборочные каталоги `build-v23*` — regenerable, но не удалять
без проверки.

### 1.7 `docs/` — что где лежит

| Группа | Файлы |
|---|---|
| База по движку | `01-architecture.md`, `02-renderer.md`, `03-build-and-dependencies.md`, `04-scripting-and-game.md`, `05-upscaling-current-state.md`, `06-rtx-feasibility.md`, `07-known-issues.md`, `08-roadmap.md`, `09-dx12-port-references.md` |
| DX12 ADR/аудит | `dx12-port-audit.md`, `dx12-architecture-adr.md`, `dx12-f3-5-architecture-adr.md`, `dx12-dxr-material-compatibility.md` |
| DX12 этапы | `dx12-f2-bootstrap.md`, `dx12-f2-first-frame.md`, `dx12-f2-1-b-hardening.md`, `dx12-f3-2c*`, `dx12-f3-3*`, `dx12-f3-5-1…6b`, `dx12-f3-6a-xray-asset-bridge.md`, `dx12-f4-0a-real-frame-integration-audit.md` |
| Облака | `dx12-v0`, `dx12-v1`, `dx12-v2*`, `dx12-v2-3b…3h*` — 30+ документов |
| Политика | `agent-workspace-storage-policy.md` |
| Контекст | `00-project-context.md` (этот файл) |

Важно: `docs/01–07` проверены против ветки `main_cop_cs` (commit `9d0021ce6`),
а не против текущей feature-ветки. Это база знаний о движке, но не отчёт о
состоянии текущих экспериментов.

### 1.8 `artifacts/` и evidence-каталоги

- `artifacts/F4.0-J2B-M0-independent-review/` — отчёт и манифест.
- `artifacts/F4.0-J2B-M1-independent-review/` — манифест.
- `artifacts/F4.0-J2B-M1-R1-independent-review/` — манифест corrective re-review.
- `j12b-review-evidence/` — патчи, manifest, implementation report J1.2B.
- `j12b-r2-review-evidence/` — Round 2 patch/report/manifest.
- `j12b-independent-worktree/` — отдельный checkout для независимой проверки.

### 1.9 Сборочные выходы

- `bin_x64/`: `xrEngine.exe`, `xrEngine.pdb`, `nvngx_dlss.dll` (310.9.1),
  `OpenAL32.dll`, `dbghelp.dll`.
- `x64/`: каталоги `f32-finalization`, `f32c2`, `f330*`, `f351*`, `f352*`,
  `f353*`, `f355*`, `f356*`, `f36a-results`, `ReleaseDX12`, ASan-прогоны и логи.
- `3rd_party/_LIB`, `3rd_party/_TEMP`, `ogsr_engine/_LIB`, `ogsr_engine/_TEMP` —
  библиотеки и объекты.

### 1.10 Worktrees

Зарегистрировано более 120 worktree, большинство — вне текущего каталога:
`C:/Users/GR/.codex/worktrees/...`, `C:/OGSR_*`, `D:/OGSR_*`,
`D:/Coding Project/OGSR ENGINE fork from rtx/j12b-independent-worktree`.
Значительная часть в detached HEAD на коммитах F4/J2/PhysX. Есть `prunable`
worktree в `%TEMP%/opencode/physx-*`. По политике: не использовать чужие
worktree, не делать prune/remove/reset/clean без отдельного согласования.

---

## 2. Накопленный контекст знаний

### 2.1 Что это за проект

OGSR-Engine — форк X-Ray / OpenXRay, движок S.T.A.L.K.E.R. Основная ветка CoP —
`prypiat` (от `main_cop_cs`). База: ~7 500 файлов, 641 файл игровых данных
(73,6 МБ), 26 проектов в решении. Рендерер переписан на DirectX 11, сверху
добавлены DLSS, FSR 3, HDR/ACES, ReShade, Tracy, RenderDoc, ImGui и
мод-ориентированная Lua-система.

Конечная цель RTX-форка: сохранить рабочий DX11/R4 и поэтапно получить нативный
DX12, затем аппаратный DXR (сначала RT-тени, затем отражения/GI).
Архитектурный ориентир — гибридный рендеринг в духе Control (Remedy/Northlight)
и Metro Exodus Enhanced Edition. Это проектное решение, а не реализованный
RT-backend; path tracing/DDGI не обязательны для запуска игры.

### 2.2 Архитектура и сборка

- Всё статическое, CRT `/MT`; `vcruntime140.dll` в рантайме не нужен.
- C++20 включён безусловно (`OgsrBuildProps.props:28`).
- 15 предупреждений повышены до ошибок; обновление MSVC может сломать сборку.
- Конфигурации: `Debug|x64`, `Release|x64`, `ReleaseDX12|x64`.
- `xrServerEntities` компилируется прямо в `xrGame.vcxproj`; `xrPhysics` и
  `xrNetServer` — отдельные StaticLib.
- Два независимых Lua VM: игровая (`xrServerEntities/script_engine.cpp`) и
  шейдерная (`xrRender/ResourceManager_Scripting.cpp`); глобалы не разделяются.
- VFS с модами: `fsgame.ltx` задаёт алиасы `$game_scripts$`, `$game_shaders$`,
  `$level$`; архивы `*.xdb*`/`*.xsq*` монтируются в то же дерево и конфликтуют
  по именам.
- `build_config_overrides/` позволяет подменить `build_config_defines.h` через
  `__has_include`.
- Точки входа: `WinMain` → `Startup()` → `CApplication`; рендер:
  `CRenderDevice::Run()` → `Begin()` → `CRender::Render()` → `End()` → `Present`.

### 2.3 Рендерер DX11 и апскейлинг

- Рендерер живёт в `xrRender` + `xrRenderDX10` + `xrRenderPC_R4`; в `xrCore`
  рендерера нет.
- `CHW`/`HW` — глобальное D3D11-устройство; `CBackend` — командный процессор;
  `dx10StateManager` кэширует состояния; `CResourceManager` — ресурсы.
- Кадр захардкожен на 13 стадий; отдельного render thread нет, есть пул `TTAPI`.
- G-buffer: `f_deffer.position.xy` — закодированная нормаль, `z` — глубина,
  `w` — material id/hemi; `C.rgb` — цвет, `C.a` — gloss. Нормали есть.
- Шейдерная модель — SM 5.0; compute-шейдеров в игре пока нет, хотя
  инфраструктура `SCS`/`cs_5_0` готова.
- DLSS: SDK 310.9.1, включён по умолчанию, качество по умолчанию — DLAA
  (без апскейла). FSR 3.1.2 — DX11-форк. Есть TAA/SMAA, CAS, 3DSS-прицел.
- Три закомментированных флага DLSS: `DoSharpening`, `MVJittered`,
  `AlphaUpscaling`. Джиттер считается функциями FSR3. Смена качества требует
  `vid_restart`.
- NIS: submodule `NVIDIAImageScaling` не инициализируется, потому что
  `Update_Components.cmd` клонирует DLSS без `--recurse-submodules`.

### 2.4 DX12/DXR: этапы и фактическое состояние

| Этап | Статус |
|---|---|
| F0 — аудит | Подтверждено: прямой перенос невозможен, D3D11-типы пронизывают рендерер; DXR в D3D11 не существует |
| F1 — ADR | Принят вариант B: параллельный DX12-путь, не замена R4; новый проект `xrRender_R5_DX12` + `xrDX12Smoke`; DX11 остаётся baseline и rollback |
| F2 — bootstrap | `CHW12`, swapchain, fence, command list, debug layer; F2.1-B — хардненинг девяти дефектов |
| F3.2 — GR Job System | Локальная интеграция Stage 1, в mainline не закоммичена; ownership/lifecycle открыты |
| F3.5 — CPU-side renderer | Снимок, видимость, ordering, multi-list, параллельная запись, scaling, packet draw, batch planner, parallel packet, per-object transforms, indexed geometry registry. Фундамент, а не игровой кадр |
| F3.6A — X-Ray asset bridge | Загружает **один** реальный OGF/DDS (visual 9), не уровень CoP |
| F4.0 — первый игровой кадр | В работе; полный F4 не принят |
| F5 — temporal/upscaling DX12 | Не начат |
| G — DXR | План; зависит от F; ориентир Control + Metro закреплён, реализация не начата |
| H–L | Концепции: многопоточность всего движка, world streaming, высотная видимость/растительность, бесшовные миры, переносимость DX12/DXR |

Ключевой вывод аудита F4.0-A: текущий `XR_3DA` собирается как R4/DX11;
`xrRenderPC_R5_DX12` — StaticLibrary, `xrDX12Smoke` — отдельный exe; R4 после
загрузки уровня не хранит CPU-копию статической геометрии, поэтому нельзя
передать `dxRender_Visual*` или `ID3D11Buffer*` в D3D12 — нужен независимый
extractor через VFS.

### 2.5 F4.0 J-серия — играбельный DX12-кадр

Самая свежая и самая конфликтная часть контекста.

- **G1B / J1.2B bridge.** Мост `GameDx12Bridge` с offscreen RTV/DSV, fence,
  readback и WIC PNG. Первый review G1B — `REQUEST CHANGES`. J1.2B Round 2 —
  source/PATCH PASS, fence identity PASS, COM lifetime PASS, command list
  recovery PASS, но **IN-GAME DRAW BLOCKED**; clean `Engine.sln` не собирался.
- **J1 — первый in-game DX12 Draw.** На Jupiter загружен static `MT_NORMAL`
  visual 9 (545 вершин / 1314 индексов), один `DrawIndexedInstanced(1314,1)` с
  игровой камерой, fence 3, readback PNG 10 623 non-clear пикселей, Debug
  Layer/GBV PASS, R4 Present сохранён. Принято с замечаниями (2026-11-08),
  commit `656652be7`.
- **J2A — CPU hierarchy/GR packet.** Jupiter `sector 129 → root 17431`,
  5 instances, 4 visible packets (visual 9/105/106/107). **R7 принят в
  production** (commit `b484f8d79`): opt-in CPU-only one-shot, 24 файла.
  J2B GPU/J3 — HOLD.
- **J2B-M0.** CPU-совместимость четырёх реальных Jupiter-ассетов: visual 9
  (545v/1314i), 105 (99v/312i), 106 (120v/192i), 107 (385v/876i), stride 32.
  M0 CPU ACCEPTED WITH NOTES. M1 в feature-ветке разрешён, mainline merge — HOLD.
- **J2B-M1.** Immutable 4-visual CPU batch и loading-phase handoff. R1 review —
  `REQUEST CHANGES`: P2 VFS-open внутри `noexcept` без catch, P2 adapter без
  восстановления меняет глобальный `$level$`. R2-исправления опубликованы в
  feature-ветке, independent review не закрыт; mainline merge HOLD.
- **J2B-M23.** Четыре реальных Jupiter GPU mesh и четыре indexed Draw в одном
  кадре. Image proof подтверждён: 4 draw visual 9/105/106/107, 38 246 disjoint
  non-clear pixels. Совместный Codex review — **REQUEST CHANGES / M23 NOT
  ACCEPTABLE / MAINLINE MERGE NO**: P1 unknown submitted Draw оставляет
  `ComPtr` вне quarantine; P1 persistent OOM при recording upload. Нужны
  targeted P1 fix, RAII upload/draw CL fault tests, focused re-review. J3 HOLD.

Итог: **F4.0 не принят**. Есть подтверждённый игровой DX12 Draw и
CPU-подготовка, но нет полного игрового renderer, textured scene, R4/DX12
parity, resize/reload/shutdown matrix и J3 parallel recording.

### 2.6 Облака: V0 → V2.3h

Отдельный трек, не связанный с игровым runtime.

- **V0** — 3D NoiseVolume (Perlin/Worley/combined) и SliceViewer.
- **V1** — volumetric clouds, ray marching, weather presets.
- **V2.2b** — геометрия плотности, единая для камеры и теней.
- **V2.3** — clipped sun optical depth, skylight ambient, аналитическая
  атмосферная перспектива, jitter modes, deterministic sequences, quality
  profiles. `SampleCloudDensityInternal` — единственная функция геометрии.
  README: `READY FOR V2.3 INDEPENDENT REVIEW`, V2 PASS не присвоен.
- **V2.3b–f** — морфология, изоляция, коррекция, shape prototypes, formula
  benchmark.
- **V2.3g** — проверка формул F1–F5 и PSO-коррекция.
- **V2.3h-a01…a03a** — изоляция шума, непериодическое поле, silhouette storm
  volume, density diagnostics, non-tiling macrostructure, macro coverage,
  probe weather contract.
- **V2.3h-a03b-R01** — 3D region sites: квази-регулярные ряды, 88 2D-компонент
  на 60 км.
- **V2.3h-a03b-R02** — HCS1 и HCS2. Оба **BLOCKED** по G/V/U/K. GPU/HLSL не
  создавался. V2 NOT ACCEPTED.
- **R03A Architecture Decision** — рекомендован **FOMS** (Flow-Organized Mass
  Systems): front/weather density + blue-noise placement + multi-lobe
  finite-support masses + coverage через количество масс. MCF отклонён, WFM —
  обязательный организационный слой, SECF — mass layer.
- **R03A-R2b Final Contract** — `REQUEST CHANGES`: математические P1 закрыты,
  но CPU/visual gates и B2 cost measurement открыты. **R03B NOT AUTHORIZED**,
  V2 NOT ACCEPTED, HCS1/HCS2 в HLSL не портировать. Legacy V2.3 остаётся
  default.
- На `origin/prypiat` новее: cloud 01C-B R3 — Codex blocking diagnostic P2,
  gate R4. В локальном checkout этих документов нет.

### 2.7 PhysX / ragdoll (mainline-трек)

P3.3-G5.1 — all-human ragdolls. S3R-серия: независимые review, targeted
crash/perf traces, static budget validation. Узкий 16-live gate принят,
production gate остаётся на hold; S3R-R1 Codex review заархивирован, R2
targeted follow-up. Отдельная линия, не связанная с DX12-рендером, но занимает
значительную часть свежих коммитов `origin/prypiat`.

### 2.8 Известные проблемы и техдолг

Сгруппировано по `docs/07-known-issues.md`:

- **Сборка:** безусловный post-build `dbghelp.dll`; `Update_Components.cmd` не
  пинит коммиты и не идемпотентен; NIS не качается; 15 warning-as-error;
  `stdcpp20` безусловный; Windows SDK не закреплён; `ShadersSln` на v143;
  LuaJIT не пересобирается при наличии `.lib`.
- **Мёртвый код:** `3rd_party_properties.props`, `DirectXMesh`, дубликаты в
  `xrRender/`, write-сторона `libsquashfs`, `PHSynchronize.cpp`, `lua_studio.cpp`.
- **Рендерер:** нет compute-шейдеров; джиттер DLSS считается FSR3; FSR3 без
  exposure/reactive/transparency; три флага DLSS закомментированы; DLAA по
  умолчанию; смена качества требует `vid_restart`; глубина `D24_UNORM_S8_UINT`;
  в G-buffer нет metallic/emissive, но нормали есть; SM 5.0 — потолок;
  AppID DLSS зашит.
- **CI/репозиторий:** нет кэшей, тестов, линтеров; артефакты не защищены
  `.gitignore`; нет Git LFS; `*.lua` вне автозагрузки.
- **CoP-специфика:** DirectPlay 8, `SPAWN_VERSION=128`, отсутствие `m_netpk.script`,
  циклическая линковка `xrEngine.lib` ↔ `xrGame.lib`.

### 2.9 Правила работы и хранения

`docs/agent-workspace-storage-policy.md` обязателен перед выбором worktree,
сборкой и созданием артефактов:

- Сначала проверить существующие worktree, их владельца, commit, status,
  ignored-файлы и занятость. Чужой активный worktree без согласования не
  использовать.
- Новый build-каталог — только при технической необходимости.
- Артефакты — в `artifacts/<task-id>/` с `reports/`, `logs/`, `metrics/`,
  `screenshots/`, `evidence/`, `runs/<run-id>/`; для значимого запуска —
  манифест.
- Бюджет места проверять на всех затрагиваемых томах, включая `%TEMP%`.
- Категории хранения: Permanent, Active, Regenerable, Unknown. Автоматического
  удаления по возрасту/пути/квоте нет.
- `prunable` в Git не означает безопасность удаления; `worktree prune/remove`,
  `git clean`, `reset` на основании политики не выполнять.

### 2.10 Что считается источниками истины

1. Для архитектуры движка — `docs/01`–`07` (привязаны к `main_cop_cs`
   `9d0021ce6`).
2. Для DX12-плана — `docs/08-roadmap.md`, `docs/dx12-architecture-adr.md`,
   `docs/dx12-f3-5-architecture-adr.md`.
3. Для фактического состояния F4/J2 — свежий `origin/prypiat:docs/08-roadmap.md`
   и review-отчёты в корне/`artifacts/`.
4. Для облаков — `tools/VolumetricCloudsTest/README.md`, `docs/dx12-v2-3h-*`,
   корневые `OGSR_V2_3h_A03b_R03A_*`.
5. Для правил работы — `AGENTS.md` + `docs/agent-workspace-storage-policy.md`.

### 2.11 GitHub-репозиторий и дорожная карта (roadmap)

- Репозиторий: <https://github.com/rafaelisraelyan/OGSR-Engine-rtx> (public
  fork от <https://github.com/OGSR/OGSR-Engine>), default branch `prypiat`.
- Подробная дорожная карта: `docs/08-roadmap.md` (~605 КБ) в `origin/prypiat`.
  Локальная копия в этом checkout отстаёт (ветка ушла на 181 коммит);
  актуальные статусы — только из GitHub-версии.
- Распределение ролей (закреплено в roadmap):
  - **Tech Lead (ChatGPT)** — единственный ответственный за актуализацию
    `docs/08-roadmap.md`; docs-only коммиты в `prypiat` после каждого
    подтверждённого результата.
  - **DeepSeek** — реализация кода, сборки/тесты, передача исходников, логов
    и воспроизводимых доказательств.
  - **Codex** — независимое READ-ONLY ревью крупных/критических изменений.
  - **Владелец** — приоритеты и отдельное разрешение на production integration.
- Треки дорожной карты:
  - A–D — DX11 (DLSS, NIS, апскейл, SSGI/Hi-Z).
  - F–G — DX12/DXR (аудит → CPU-side renderer F3.5 → игровой кадр F4 → DXR).
  - E — техдолг и воспроизводимость сборки.
  - H — поздний аудит/оптимизация всего движка.
  - I–L — world streaming, видимость/растительность, бесшовные миры.
  - P — PhysX Ragdoll (параллельный трек).
  - V — Volumetric Clouds (параллельный трек, изолированный DX12/compute-прототип).
- Актуальные статусы на 2026-11-20 (из GitHub roadmap):
  - M23-R1 corrective delivery: READY FOR FOCUSED INDEPENDENT CODEX REVIEW,
    NOT YET PRODUCTION ACCEPTED; M23 MAINLINE MERGE HOLD.
  - Облака: 01C-B-R4 Tech Lead PASS WITH NOTES, Codex R4 review PENDING;
    V-Q4 BLOCKED до игрового DX12 HDR/depth/queue/frame.
  - PhysX: p3.3g5.1 S3R-серия review; production gate на hold.

---

## 3. Текущее состояние рабочей копии и риски

**Изменённые tracked-файлы (20):**

- `AGENTS.md` — локальная правка.
- `tools/VolumetricCloudsTest/`: `CMakeLists.txt`, `README.md`, `REPRODUCE.md`,
  `scripts/check_sources.ps1`, `scripts/check_sources.py`,
  `shaders/CloudComposite.hlsl`, `CloudRayMarch.hlsl`, `SliceViewer.hlsl`,
  `src/Application.*`, `CloudProfiler.*`, `CloudRayMarcher.*`, `CloudTypes.h`,
  `NoiseTypes.h`, `Renderer.*`, `main.cpp`.

**Untracked (147):**

- 37 в корне: review-отчёты, R03A-документы, ZIP/SHA256, `.obj`, логи.
- 28 в `docs/`: почти все `dx12-v2-3h-*`, `dx12-f2-1-b-hardening`,
  `dx12-f3-1-*`, `dx12-f3-5-*audit*`, `dx12-f4-0a-*`, `dx12-v2-*`.
- 74 в `tools/VolumetricCloudsTest/`: шейдеры формул/прототипов, скрипты,
  тесты, `__pycache__`.
- 4 в `ogsr_engine/`: `dx12xraylevelgeometry.*`, `tests/build-xraylevel-cpu.cmd`,
  `tests/xraylevel_cpu_main.cpp`.
- 3 каталога evidence: `j12b-*`.

**Риски:**

- Локальная ветка не содержит свежий mainline (`origin/prypiat` ушёл на 181
  коммит), поэтому часть документов и кода J2B/M23/PhysX здесь отсутствует.
- `AGENTS.md` изменён, но не закоммичен; политика хранения внедрена отдельным
  коммитом `02d2a003d`.
- Отчёты и evidence-архивы собраны в `reports/` (папка целиком untracked), но
  в корне остаются untracked `.obj`-файлы прошлых прогонов; по политике они
  `Unknown/Active`, не `Regenerable`.
- `bin_x64/xrEngine.exe` и `x64/` — старые сборочные артефакты; не считать их
  доказательством текущего runtime.
- Независимые review не заменяют сборку/GPU-прогон.

---

## 4. Открытые гейты, которые сейчас определяют работу

1. **J2B-M23 P1:** unknown submitted Draw и OOM при upload — исправить
   failure-state machines, проверить Release/destructor/fence и RAII list Close,
   затем focused re-review. Mainline merge и J3 — HOLD.
2. **J2B-M1:** mainline merge HOLD до закрытия P2 по VFS `noexcept` и
   независимого evidence review.
3. **F4.0:** нет полного игрового DX12 renderer; после M23 — textured scene,
   camera, resize/reload/shutdown, R4 parity.
4. **Облака:** V2 NOT ACCEPTED; HCS1/HCS2 запрещено портировать в HLSL; R03B не
   авторизован; следующий шаг — независимый review R03A-R2b, затем CPU-reference
   FOMS.
5. **DX11-направление:** NIS/compute-гейт (B2.0) не пройден, тюнинг DLSS (C) и
   SSGI/Hi-Z (D) не начаты.
6. **PhysX:** production gate G5.1 на hold, идёт R2 targeted follow-up.
