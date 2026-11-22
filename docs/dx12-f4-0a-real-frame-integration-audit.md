# F4.0-A — Real Game Frame Integration Audit

**Проект:** OGSR-Engine-rtx  
**Ветка:** `prypiat`  
**Базовая ревизия:** `ccf0d22d1e33c3d5c38b4fac4e124459cf9a9bef`  
**Режим:** READ-ONLY архитектурный аудит. Код не изменялся, commit/push не выполнялись.

---

## A. Git & Baseline State

- HEAD: `ccf0d22d1e33c3d5c38b4fac4e124459cf9a9bef`
- Branch: `prypiat`
- `origin/prypiat`: совпадает с HEAD
- Working tree: только ранее существовавшие пользовательские изменения (`AGENTS.md`, шесть untracked docs). Аудит не изменил ни одного файла.
- Commit/push не выполнялись.

Текущий baseline:

- Игровой executable собирается как **R4/DX11**: `XR_3DA.vcxproj` ссылается на `xrRenderPC_R4`, а не на DX12.
- `xrRenderPC_R5_DX12` — **StaticLibrary**, не полноценный игровой renderer module.
- `xrDX12Smoke` — отдельный executable, не подключён к `XR_3DA`.
- F3.6A подтверждает только один real X-Ray OGF/DDS asset, не загрузку уровня CoP.

Метки: `VERIFIED IN CODE`, `VERIFIED BY TEST`, `REPORTED`, `INFERRED`, `NOT TESTED` используются ниже.

---

## B. Game Frame Call Graph

### B.1 Подтверждённая цепочка

| Этап | Реальный путь / функция | Вызывающий код | Поток / владелец | Точка возможного DX12-подключения |
|---|---|---|---|---|
| Запуск executable | `ogsr_engine/xr_3da/x_ray.cpp:378-441`, `WinMain_impl()`; `Core._initialize`, `InitEngine`, `Engine.External.Initialize`, `Startup()` | Windows CRT `WinMain` | main thread | Только feature-gated sidecar startup |
| Выбор renderer | `EngineAPI.cpp:44-56`, `CEngineAPI::Initialize()`; `g_current_renderer = 4`; `Console->Execute("renderer renderer_r4")` | `Startup` | main | Не заменять `::Render`; sidecar отдельно |
| Attach R4 | `Layers/xrRenderPC_R4/xrRender_R4.cpp:8-15`, `AttachRender()`; `::Render = &RImplementation` | `EngineAPI::Initialize` | main | Не трогать |
| Device + swapchain | `xr_3da/Device_create.cpp:40-65`, `CRenderDevice::Create()`; `dxRenderDeviceRender.cpp:101-150`; `dx10HW.cpp:103-198` | `Startup` / `Device.Create` | main | DX12 host должен иметь **свой** HWND/swapchain |
| App/window loop | `xr_3da/device.cpp:391-409`, `message_loop()`; `:212-389`, `on_idle()` | `CRenderDevice::Run` | main | Отдельный `pureRender` callback или feature-gated hook |
| Game update | `device.cpp:253`, `FrameMove()`; `device.cpp:466-505`; `seqFrame.Process` | `on_idle` | main | Camera capture only |
| Level start | `x_ray.cpp:505-528`, `CApplication::OnEvent(eStart)`; `Level_start.cpp:17-64`, `CLevel::net_Start` | event `KERNEL:start` / load | main | Level-generation event |
| Level load | `Level_start.cpp:71-164`, `net_start_client3`; `IGame_Level.cpp:76-114`, `Load()`; `r4_loader.cpp:21-160`, `CRender::level_Load` | loading events / `on_idle` | main | Read-only VFS extraction after load |
| Geometry upload R4 | `r4_loader.cpp:261-332`, `CRender::LoadBuffers`; `FVisual.cpp:30-170`, `Fvisual::Load` | `level_Load` | main | Только независимый DX12 extractor; не читать D3D11 resources |
| Camera update | `Actor.cpp:968-972`, `CActor::UpdateCL`; `ActorCameras.cpp:288-415`; `CameraManager.cpp:452-472`, `ApplyDevice`; `device.cpp:128-153`, `OnCameraUpdated` | `seqFrame` / `on_idle` | main | Copy current `Device.mView/mProject` |
| Render scheduling | `r2_R_calculate.cpp:20-79`, `render_main::calculate_static/dynamic`; `TTAPI->submit` | `CRender::Calculate` | main + TTAPI workers | Не захватывать R4 `dxRender_Visual*` |
| Scene traversal | `r4_R_render.cpp:56-156`, `main_pass_static`; `:158-264`, `main_pass_dynamic` | `CRender::Render` | main + TTAPI | Независимый DX12 traversal по extracted data |
| Draw submission | `r__dsgraph_render.cpp:14-201`; `FVisual.cpp:152-169`; `r4_R_render.cpp:266-536` | R4 render | main | Отдельный DX12 command list |
| Present | `device.cpp:64-115`, `CRenderDevice::End`; `r4.cpp:998-1073`, `CRender::End`, `Present` | `on_idle` | main | Только child preview swapchain, не main HWND |
| Shutdown | `IGame_Level.cpp:25-39`; `r4_loader.cpp:174-258`; `Device_destroy.cpp:9-55` | event/destruction | main | Preview unload + fence wait |

### B.2 Render thread

Отдельного выделенного render thread нет.

- `CRenderDevice::Run()` фиксирует main thread и запускает `message_loop` (`device.cpp:417-460`).
- `on_idle` сначала выполняет `FrameMove`, затем `OnCameraUpdated(false)`, затем отправляет часть работы в `TTAPI` (`device.cpp:253-305`).
- `seqRender.Process` и фактический вызов `CLevel::OnRender` выполняются на main thread (`device.cpp:318`).
- R4 `render_main` отправляет static/dynamic calculation в `TTAPI` (`r2_R_calculate.cpp:20-79`), но `CRender::Render` ждёт их и выполняет submission на main thread (`r4_R_render.cpp:359-369`).
- `TTAPI` worker не является отдельным renderer thread с собственным device; это общий worker pool.

---

## C. Level Loading / Geometry Architecture

### C.1 VFS, не loose files

`CApplication::Level_Scan()` перечисляет `$game_levels$` через `FS.file_list_open` (`x_ray.cpp:668-679`), а `Level_Set` задаёт `$level$` (`x_ray.cpp:727-733`).

`IGame_Level::Load()`:

- открывает `level` через `FS.r_open(fsgame::level, ...)` (`IGame_Level.cpp:86`);
- вызывает `Render->level_Load` (`:101`);
- закрывает reader (`:110`);
- регистрирует level в `Device.seqRender` и `Device.seqFrame` (`:113-114`).

R4 `CRender::level_Load()`:

- открывает `level.geom`/`level.geomx` через `FS.rs_open` (`r4_loader.cpp:70-81`);
- читает `fsL_VISUALS`, `fsL_SECTORS`, lights/details/HOM (`r4_loader.cpp:87-104`);
- формат chunks определён в `xr_3da/xrLevel.h:9-25`.

`CLocatorAPI` регистрирует архивы и открывает файлы через `r_open_impl`/`rs_open` (`LocatorAPI.cpp:330-346`, `:1004-1044`). Поэтому нельзя предполагать наличие loose `level.geom`: его нужно получать через `FS`/VFS.

**Проверка локальной установки:** `gamedata/levels` содержит `jupiter`, `pripyat`, `zaton`, но в этих папках видны только `level.puddles`; остальные level payload находятся в VFS/archives. Это `REPORTED`, не `VERIFIED IN CODE` для конкретного архива.

### C.2 Как формируется R4 geometry

`CRender::LoadBuffers` (`r4_loader.cpp:261-332`):

- читает declarators, vertex counts и index counts;
- создаёт D3D11 vertex/index buffers через `dx10BufferUtils::CreateVertexBuffer/CreateIndexBuffer`;
- после upload освобождает временный CPU `pData`;
- сохраняет только `ID3D11Buffer*`.

`Fvisual::Load` (`FVisual.cpp:30-170`):

- читает `OGF_GCONTAINER` или inline `OGF_VERTICES/INDICES`;
- для `GCONTAINER` получает shared VB/IB из `RImplementation.getVB/getIB`;
- в `IRender_Mesh` (`FBasicVisual.h:14-36`) хранит только D3D11 pointers, `vBase/vCount/iBase/iCount`.

`FHierrarhyVisual` (`FHierrarhyVisual.cpp:38-50`) загружает children по `OGF_CHILDREN_L`.

`CRender::level_Unload` освобождает `Visuals`, `nVB/nIB`, shaders и сбрасывает `b_loaded` (`r4_loader.cpp:174-258`).

**Главный вывод:** R4 после загрузки уровня **не хранит CPU-копию статической геометрии**. Нельзя безопасно передать `dxRender_Visual*`, `ID3D11Buffer*` или `nVB/nIB` в D3D12.

### C.3 Что именно нужно для F4.0-B

Минимальный срез:

- статические `MT_NORMAL` leaves;
- контейнер `fsL_VISUALS` + shared buffers из `level.geom` (если визуал использует `OGF_GCONTAINER`);
- `MT_HIERRARHY` только как контейнер children;
- один выбранный sector root из `fsL_SECTORS`;
- без skeletal, particles, LOD, tree/progressive, terrain/details, HUD и dynamic objects.

F3.6A loader (`dx12xrayasset.h:1-65`) поддерживает только один static OGF subset (format 4, `MT_NORMAL`, FVF `0x112`, inline vertices/indices). Это **не** CoP level loader.

**Кандидат уровня: Jupiter.**  
Jupiter доступен как именованная CoP-локация (`gamedata/levels/jupiter`), является outdoor/industrial уровнем с потенциально удобным набором статической hard-surface геометрии. Точные vertex/index counts и sector ID **не проверены**; это `INFERRED` до extraction preflight. Zaton как первый outdoor-уровень сложнее для первого среза из-за vegetation/terrain. Pripyat откладывается из-за городской динамики/HUD.

---

## D. Camera & WVP Contract

### D.1 Реальный источник камеры

- `CActor::UpdateCL` вызывает `cam_Update()` и затем `Device.OnCameraUpdated(true)` (`Actor.cpp:968-972`).
- `CCameraManager::ApplyDevice` задаёт:
  - `Device.mView.build_camera_dir(...)`;
  - `Device.vCameraPosition/Direction/Top/Right`;
  - `Device.fFOV`, `Device.fASPECT`;
  - `Device.mProject.build_projection(...)` (`CameraManager.cpp:452-472`).
- `CRenderDevice::OnCameraUpdated` пересчитывает `mInvView`, `mFullTransform = mProject.mul(mView)` и HUD-матрицы (`device.cpp:128-153`).
- `on_idle` вызывает `OnCameraUpdated(false)` перед `seqRender` (`device.cpp:253-318`), поэтому в `CLevel::OnRender` камера уже актуальна.

### D.2 Matrix convention

`Fmatrix` хранит 4×4 в `m[4][4]` (`_matrix.h:29-60`), translation находится в row 3; `transform_tiny` использует row-vector формулу (`_matrix.h:784-789`).

`build_projection` формирует D3D-style perspective с clip Z range `[0,1]` (`_matrix.h:644-672`). `build_camera_dir` формирует view matrix (`:735-769`).

DX12 smoke camera использует DirectXMath row-vector:

- `XMMatrixLookAtLH`;
- `XMMatrixPerspectiveFovLH`;
- `XMMatrixMultiply(view, proj)`;
- transpose перед constant buffer (`dx12camera.cpp:150-179`);
- HLSL: `mul(float4(position, 1), ViewProjection)` (`dx12mesh.cpp:163-175`).

### Обязательный вывод для F4.0-B

Нельзя просто взять `SmokeCamera12` или сделать предположение о transpose. Нужен отдельный `GameCameraCapture12`:

- читает **текущие** `Device.mView`/`Device.mProject` после `OnCameraUpdated`;
- сохраняет view/projection/aspect/viewport;
- использует тот же порядок умножения и transpose, что проверен в R4/DX12;
- проверяется golden test: несколько world points проходят через `Device.mFullTransform` и через DX12 VP, сравниваются clip-space `x/y/z/w`;
- отдельно проверяются near/far, depth `[0,1]`, aspect после resize.

`mViewSaved/mProjectSaved` — предыдущий кадр (`device.cpp:106`, `:153`), их нельзя использовать как текущую камеру.

---

## E. R4 Scene Traversal

### E.1 Static path

`CRender::main_pass_static` (`r4_R_render.cpp:56-156`):

1. определяет sector по `Device.vCameraPosition`;
2. выполняет `PortalTraverser.traverse(...)` (`:119-120`);
3. обходит sector roots и вызывает `dsgraph.add_static(...)` (`:134-145`);
4. fallback — все сектора (`:149-155`).

`CPortalTraverser::traverse` и `traverse_sector` (`r__sector_traversal.cpp:14-40`, `110-193`) выполняют portal/frustum/HOM-culling.

### E.2 Dynamic path

`main_pass_dynamic` (`r4_R_render.cpp:158-264`):

- `g_SpatialSpace->q_frustum(...)` (`:165`);
- сортировка `lstRenderables` (`:168`);
- HOM/frustum/field-of-view checks (`:220-242`);
- `renderable->renderable_Render(...)` (`:250`);
- `CGameObject::renderable_Render` вызывает `::Render->add_Visual(...)` и пишет `hom_frame` (`GameObject.cpp:649-654`).

### E.3 Render graph

`R_dsgraph_structure::r_dsgraph_insert_static/dynamic` (`r__dsgraph_build.cpp:203-418`):

- копирует `Fmatrix` в `_MatrixItem`/`_MatrixItemS`;
- сохраняет `IRenderable*` и `dxRender_Visual*`;
- выбирает `ShaderElement`/`SPass`;
- мутирует `vis_data` markers и `hom_frame`;
- складывает данные в frame-local maps.

`r_dsgraph_render_graph_static/dynamic` (`r__dsgraph_render.cpp:14-201`) устанавливает shaders/textures, затем вызывает `pVisual->Render(...)`; `Fvisual::Render` делает `cmd_list.Render` (`FVisual.cpp:152-169`).

После render maps очищаются (`r__dsgraph_render.cpp`, static/dynamic loops). Поэтому **после R4 frame нельзя получить готовый immutable список** из dsgraph.

### E.4 Game-owned vs render-owned

| Данные | Владелец | Проблема для DX12 |
|---|---|---|
| `CLevel`, `Objects`, `ObjectSpace`, `IRenderable::renderable.xform` | game | mutable, raw pointers, main/game lifecycle |
| `IRenderable*`, `dxRender_Visual*` в `_MatrixItem` | R4/game | нельзя сохранять в DX12 packet |
| `vis_data` markers, `hom_frame` | R4 | мутируются traversal |
| `Visuals`, `nVB/nIB` | R4 | D3D11 GPU-only, без CPU copy |
| `ShaderElement`, `SPass`, `ref_shader` | R4 | D3D11 shader contract |
| `R_dsgraph_structure` maps | R4 frame-local | очищаются, не immutable |
| `Device.mView/mProject`, camera | engine | можно **скопировать** в DX12-owned snapshot |

---

## F. DX11 Isolation Boundaries

### F.1 Фактический renderer selection

`CEngineAPI::Initialize` жёстко выбирает R4:

```cpp
g_current_renderer = 4;
Console->Execute("renderer renderer_r4");
```

(`EngineAPI.cpp:44-56`)

`xrRender_R4.cpp:8-15` устанавливает глобальные `::Render`, `::RenderFactory`, `::DU`, `UIRender`, `DRender` на R4.

`XR_3DA.vcxproj:378-381` ссылается только на `xrRenderPC_R4`; `xrRender_R5_DX12.vcxproj:39-67` — static library с `OGSR_USE_DX12`, но не игровой renderer.

**DX12 game backend сейчас отсутствует.** `xrDX12Smoke` — отдельный executable.

### F.2 Изоляционная карта

```text
Game / Engine
  ├─ CLevel / Objects / ObjectSpace / camera
  │
  ├─ CEngineAPI::Initialize
  │     └─ AttachRender -> ::Render = R4
  │
  └─ CRenderDevice -> dxRenderDeviceRender -> CHW / D3D11 / R4 swapchain

Future additive path:

Game / Engine
  └─ optional DX12 Preview Host
        ├─ own ID3D12Device / queue / child swapchain
        ├─ read-only VFS level extraction
        ├─ copied current camera
        └─ no pointer into R4 visuals or D3D11 resources
```

### F.3 Same-HWND restriction

R4 creates the main D3D11 swapchain through `dx10HW.cpp:103-198`; `CHW12` creates its own DX12 swapchain with `CreateSwapChainForHwnd` (`CHW12.cpp:596-626`).

Нельзя создавать второй DX12 flip-model swapchain на том же main HWND, который уже использует R4. Для F4.0-B безопасный вариант — **отдельный child HWND** или offscreen readback. Same-window composition потребует изменений R4 present/swapchain и должна быть отдельным решением; в текущем scope это blocker.

### F.4 Fail-safe contract

DX12 preview должен:

- быть выключен по умолчанию;
- не создавать `ID3D12Device`/swapchain без явного флага;
- при ошибке инициализации только логировать и отключаться;
- не заменять `::Render`, `RenderFactory`, `Device.m_pRender`;
- не менять DX11 `CHW`, RenderTarget, ResourceManager, shaders, DLSS/FSR/TAA;
- не трогать R4 frame resources;
- при level unload освобождать только свои ресурсы;
- при restart игры без флага вообще не загружать preview path.

---

## G. DX12 Integration Candidates

| Вариант | Суть | Плюсы | Минусы / риск | Решение |
|---|---|---|---|---|
| A. R4 snapshot bridge over loaded visuals | Использовать `dxRender_Visual*`, `dsgraph` или D3D11 buffers | Минимум нового parsing | Нет CPU geometry; D3D11 resources нельзя читать D3D12; mutable pointers; потребует менять R4 loader | **Reject** |
| B. DX12-owned VFS extractor + independent traversal | Читать `level`/`level.geom` через `FS`; строить DX12 CPU/GPU data; свой camera/frame | Не меняет R4; real VFS path; ownership изолирован | Дублирует parsing; нужно поддержать GCONTAINER/FVF; больше кода | **Preferred** |
| C. Common read-only geometry bank | Добавить в R4 loader CPU copies и read-only interface | Меньше duplicate parsing | Меняет общий loader; lifetime coupling; риск DX11 | Не выбирать без отдельного approval |
| D. D3D11/D3D12 interop | Shared textures/buffers между API | Потенциально использует R4 data | Требует shared resources, cross-queue sync, R4 changes; не доказано | **Reject for F4.0-B** |

Вывод: **B** — единственный вариант, который сохраняет R4 неизменным и даёт реальную геометрию через настоящий VFS path.

---

## H. Recommended F4.0-B Architecture

### H.1 Форма

Новый **DX12 Preview Host** (отдельный optional target/config, не замена R4):

- создаёт собственный `ID3D12Device`, queue, fence, swapchain на **child HWND**;
- загружает один real CoP level через `FS` (`$level$`), не через loose asset paths;
- извлекает ограниченный static subset из `level`/`level.geom`;
- копирует текущую игровую камеру;
- рисует один merged mesh с flat diagnostic color и depth test;
- презентует в child window;
- R4 продолжает рисовать и презентовать основной игровой кадр.

### H.2 Поток

```text
Game frame
  ├─ R4 frame (unchanged)
  │    CLevel::OnRender -> R4 Calculate/Render/Present
  │
  └─ DX12 preview frame (feature flag)
       read Device.mView/mProject (current)
       own level-generation token
       own extracted static VB/IB
       own depth target + flat PSO
       own command list + fence
       child swapchain Present
```

Hook: новый `pureRender`/feature-gated callback, зарегистрированный после R4 level render. Если порядок `seqRender` окажется нестабильным, допускается одна guarded строка в `CLevel::OnRender`/startup, но **без изменения R4 логики**. Это нужно явно согласовать.

### H.3 Почему это настоящий game frame

- Запущен реальный `xrEngine`.
- Загружен реальный CoP level через `FS` и game level path.
- Используется текущая игровая камера и viewport.
- D3D12 выполняет реальные draw calls с depth test.
- Это не `xrDX12Smoke` и не отдельный synthetic scene.

### H.4 Ограничение F4.0-B

F4.0-B **не** обязан:

- повторять весь R4 visible set;
- поддерживать все материалы/текстуры;
- поддерживать vegetation, terrain, skeletal, particles, HUD, rain, wallmarks;
- заменять основной swapchain.

Он должен доказать real geometry/depth/camera/D3D12 path. Textures/materials — F4.0-C.

### H.5 Jupiter, первый срез

- Уровень: **Jupiter**.
- Один sector root или ограниченный static subset.
- Только `MT_NORMAL`/`MT_HIERARHY` leaves.
- `level` и `level.geom` читаются через VFS.
- Если extraction не находит валидный static subset — остановиться и доложить; не подменять standalone OGF.
- Точные counts/sector ID должны быть залогированы в B; текущая оценка Jupiter — `INFERRED`.

---

## I. DX11 Regression Protection Plan

### I.1 Baseline build

- Конфигурация: `Release|x64` для `XR_3DA` + `xrRenderPC_R4`.
- Не `ReleaseDX12`: текущий DX12 smoke не является DX11 baseline.
- Установленная CoP: `D:\Games\S.T.A.L.K.E.R. Call of Pripyat` (`REPORTED`; roadmap `docs/08-roadmap.md:116-126`).
- Зафиксировать текущий `AGENTS.md`/user settings и не включать их в baseline diff.

### I.2 Обязательный ручной DX11 matrix

1. Чистый запуск R4.
2. Загрузка Jupiter.
3. Движение камеры/персонажа.
4. Статическая и динамическая геометрия.
5. Материалы/освещение.
6. Растительность/alpha-tested.
7. Смена погоды/времени.
8. HUD/UI.
9. Save/load.
10. Resize/restart renderer.
11. Shutdown.
12. DLSS/FSR3/TAA/SMAA в поддерживаемых режимах.

Фиксировать:

- скриншоты в одинаковой точке/погоде/разрешении;
- `xrEngine` log на shader/device/texture errors;
- отсутствие новых validation errors;
- отсутствие изменения `renderer renderer_r4`.

Скриншоты сравнивать визуально/по image hash только как вспомогательный признак; GPU/драйверные различия не считать функциональным FAIL.

### I.3 CI gates

**DX11 gate**

- `XR_3DA` + R4 компилируются.
- Игровой executable запускается без DX12 flag.
- `renderer_r4` selection не изменена.
- Smoke/regression logs не содержат новых ошибок.
- GPU-прогон на self-hosted Windows; CI без GPU не доказывает rendering.

**DX12 gate**

- `ReleaseDX12|x64` собирается.
- Smoke CPU tests проходят.
- F4.0-B integration test: real level, extracted counts, real camera, depth, draw calls, GPU validation.
- Debug Layer/GBV на доступном GPU.
- Отдельный gate после каждого integration commit.

**Не требовать FPS parity** между незавершённым DX12 preview и DX11.

---

## J. Ownership / Threading / GPU Lifetime Risks

| ID | Severity | Риск | Точное место | Влияние | Защита / тест |
|---|---|---|---|---|---|
| R1 | Critical | Второй flip swapchain на main HWND | `CHW12.cpp:596-626`; `dx10HW.cpp:103-198` | DX11 device/swapchain failure | Child HWND/offscreen; fail-safe test |
| R2 | Critical | Попытка читать R4 D3D11 VB/IB | `r4_loader.cpp:261-332`; `FBasicVisual.h:14-36` | invalid D3D12 use, crash | Только VFS extraction; no interop |
| R3 | High | Raw `IRenderable*`/`dxRender_Visual*` в DX12 packet | `IRenderable.h:11-25`; `r__dsgraph_build.cpp:203-418` | UAF/race/reload crash | Copy only ids/camera/geometry; no pointers |
| R4 | High | Level unload/reload | `r4_loader.cpp:174-258`; `IGame_Level.cpp:25-39` | stale DX12 resources | level generation + fence + release |
| R5 | High | Matrix convention / transpose / depth | `_matrix.h:111-133,644-672,735-769`; `dx12camera.cpp:150-179` | wrong geometry/depth | Golden clip-space test; compare R4 |
| R6 | High | Level/OGF parser malformed data | future extractor; `xrLevel.h:9-25`; `FVisual.cpp:30-170` | OOB/crash | Bounds/overflow/ASan; F3.6A validation patterns |
| R7 | High | Draw before upload/fence | new preview upload | GPU fault/validation | Copy queue + fence; no draw until complete |
| R8 | Medium | R4 materials/PSO not portable | `r4_loader.cpp:37-64`; `r__dsgraph_render.cpp:50-63` | wrong material/crash | Flat diagnostic PSO; no R4 shader reuse |
| R9 | Medium | Fixed registry capacity 3 | `dx12geometrycatalog.h:39`; `dx12geometryregistry.h:19-22` | overflow/partial level | New preview registry/cap; validate counts |
| R10 | Medium | Vegetation/terrain/dynamic/HUD omitted | `r4_R_render.cpp:158-264`; `Details->Render` | incomplete visuals | Explicit static-only scope |
| R11 | Medium | Resize/device lost | `Device_destroy.cpp:60-...`; `r4.cpp:150-193` | preview stale; R4 unaffected | Independent preview reset/shutdown tests |
| R12 | Medium | GR JobSystem misuse | `dx12renderprep`, `GpuMeshRegistry` | data race/lifetime | Only immutable data; owner submission |
| R13 | Low | Feature flag/link accidentally affects DX11 | `XR_3DA.vcxproj:378-381`; R5 static | build/default regression | Separate config; no-flag smoke |
| R14 | Low | Performance/VRAM duplication | level extraction + preview | frame hitch | Cap subset; measure; no parity claim |

`CJobGroup::Wait` не является GPU fence. Для preview нужен собственный D3D12 fence/quarantine contract.

---

## K. Proposed F4.0-B Implementation Steps

Только план, без кода:

1. **Заморозить DX11 baseline**: собрать `Release|x64`, записать R4 screenshots/logs до изменений.
2. Добавить optional preview target/config с флагом `-dx12preview`; по умолчанию ноль DX12-кода/устройств.
3. Реализовать DX12 preview host с child HWND, device/queue/swapchain/fence/quarantine.
4. Реализовать read-only VFS extractor `level`/`level.geom`; поддержать `GCONTAINER`, hierarchy, sector roots, FVF `0x112`; reject unsupported.
5. Выбрать Jupiter static subset (один sector; только `MT_NORMAL`/`MT_HIERARHY`).
6. Реализовать `GameCameraCapture12` из `Device.mView/mProject`, aspect/viewport; golden clip/depth tests.
7. Смёржить subset в один/несколько VB/IB; создать flat unlit PSO, depth target, upload + fence.
8. Подключить `pureRender` hook после R4; рисовать real D3D12 draw calls в child preview; R4 не менять.
9. Добавить level-generation invalidation, unload wait/release, failure no-op, resize/shutdown.
10. Добавить evidence: level name, VFS chunks, vertex/index counts, camera checksum, depth readback, GPU validation; проверить DX11 baseline.

**Stop:** если шаг требует изменения `CHW`, `CRender`, `dxRenderDeviceRender`, `CResourceManager`, R4 RenderTarget/shader contracts или same-HWND composition — остановиться и вынести на approval.

---

## L. Blocking Dependencies

### Настоящие blockers

1. **Explicit approval of child preview HWND.**  
   Same-HWND DX12 swapchain рядом с R4 не считается безопасным. Без child/offscreen решения F4.0-B в текущем scope заблокирован.

2. **Approval of optional preview target/link and one feature-gated frame hook.**  
   Это не изменение R4 behavior, но добавляет DX12-only startup/render path. Без approval не реализовывать.

3. **No R4 resource sharing.**  
   Если F4.0-B потребует CPU copy из R4 or D3D11/D3D12 interop — это blocker, требуется новый architecture approval.

4. **Real CoP assets.**  
   Jupiter payload должен быть доступен через `FS`; использование loose `level.geom` не предполагается.

### Не blockers, будущая работа

- textures/materials (F4.0-C);
- full R4-visible parity;
- vegetation/terrain/dynamic/skeletal/HUD;
- portal/HOM exact visibility;
- DLSS/FSR/TAA integration;
- performance parity;
- full DX12 backend replacement.

---

## M. Evidence & Verification

| Утверждение | Доказательство | Статус |
|---|---|---|
| R4 выбран жёстко в `CEngineAPI::Initialize` | `EngineAPI.cpp:44-56` | VERIFIED IN CODE |
| R4 AttachRender задаёт глобальные renderer pointers | `xrRender_R4.cpp:8-15` | VERIFIED IN CODE |
| `XR_3DA` линкует R4, не R5 | `XR_3DA.vcxproj:378-381`; `xrRender_R5_DX12.vcxproj:39-67` | VERIFIED IN CODE |
| Main loop: `on_idle` → `FrameMove` → `OnCameraUpdated` → `seqRender` → `End` | `device.cpp:212-389`, `:417-460` | VERIFIED IN CODE |
| Level loading идёт через VFS | `IGame_Level.cpp:86-110`; `r4_loader.cpp:70-81`; `x_ray.cpp:727-733` | VERIFIED IN CODE |
| R4 level buffers загружаются в D3D11 и CPU data освобождается | `r4_loader.cpp:261-332`; `FVisual.cpp:30-170`; `FBasicVisual.h:14-36` | VERIFIED IN CODE |
| R4 Visuals/level buffers освобождаются при unload | `r4_loader.cpp:174-258` | VERIFIED IN CODE |
| Static traversal: portal/frustum/HOM | `r4_R_render.cpp:56-156`; `r__sector_traversal.cpp:14-40,110-193` | VERIFIED IN CODE |
| Dynamic traversal uses mutable game objects and TTAPI | `r4_R_render.cpp:158-264`; `GameObject.cpp:649-654`; `r2_R_calculate.cpp:50-79` | VERIFIED IN CODE |
| Render graph stores raw visual/object pointers and clears maps | `r__dsgraph_build.cpp:203-418`; `r__dsgraph_render.cpp:14-201` | VERIFIED IN CODE |
| Camera matrices are built in Device/CameraManager | `CameraManager.cpp:452-472`; `device.cpp:128-153`; `_matrix.h:644-672,735-769` | VERIFIED IN CODE |
| DX12 smoke camera is fixed-pose and not game camera | `dx12camera.cpp:150-179`; `dx12camera.h` | VERIFIED IN CODE |
| Current DX12 geometry registry capacity = 3 | `dx12geometrycatalog.h:39`; `dx12geometryregistry.h:19-22` | VERIFIED IN CODE |
| F3.6A loader supports only one static OGF subset | `dx12xrayasset.h:1-65`; `docs/dx12-f3-6a-xray-asset-bridge.md` | VERIFIED IN CODE |
| `CHW12` creates its own flip swapchain | `CHW12.cpp:596-626` | VERIFIED IN CODE |
| DX12 smoke is separate, not game backend | `xrDX12Smoke.vcxproj`; `XR_3DA.vcxproj` | VERIFIED IN CODE |
| CoP Jupiter/Pripyat/Zaton folders exist; only puddles loose | local filesystem observation | REPORTED |
| Jupiter is suitable static first candidate | no extraction counts yet | INFERRED |
| Game camera conversion/transpose is correct | not yet tested against R4 | INFERRED / NOT TESTED |
| Same-HWND DX12+R4 safety | not tested; DXGI risk | INFERRED |
| DX11 baseline runtime and screenshot matrix | not run in this audit | NOT TESTED |
| F3.6A asset/depth regression evidence | `docs/dx12-f3-6a-xray-asset-bridge.md`; prior F3.6A clean run | VERIFIED BY TEST (F3.6A only) |

---

## N. Final Recommendation

**READY FOR F4.0-B DESIGN APPROVAL**

Предпочтительная архитектура:

- R4/DX11 остаётся production path;
- DX12 preview — отдельный optional host с child HWND;
- real CoP geometry через read-only VFS extraction;
- real game camera;
- flat color + depth;
- R4 не изменяется;
- никакого hot switching, только restart без флага.

Одно обязательное решение перед B: **approve отдельный child preview window**. Если требование — рисовать DX12 в main game window, текущая рекомендация **BLOCKED** до отдельного composition/swapchain review.

---

## O. DX11 Safety Verdict

**Да, предложенный F4.0-B можно реализовать без изменения существующего DX11/R4 behavior — при следующих границах:**

- не менять `CHW`, `CRender`, `dxRenderDeviceRender`, R4 RenderTarget, `CResourceManager`, R4 shaders;
- не менять `::Render`, `RenderFactory`, `Device.m_pRender`;
- не использовать R4 `dxRender_Visual*`, dsgraph или D3D11 buffers;
- не создавать второй swapchain на main HWND — только child/offscreen;
- DX12 preview выключен по умолчанию;
- ошибка DX12 инициализации/device loss не влияет на R4;
- level unload освобождает только preview resources после fence;
- DX11 baseline проверяется отдельно до/после каждого integration commit.

**Если same-window DX12 presentation или R4 CPU geometry bank потребуются — ответ «нет» без отдельного approval.** Это потребует изменений R4/frame lifetime и выходит за безопасный F4.0-B scope.

**Фактический DX11 runtime PASS не заявляется:** в рамках read-only аудита DX11 baseline не запускался; статус — `NOT TESTED`.
