# F0 — Аудит зависимостей DX11/DX12

> Документ создан в рамках этапа F0 дорожной карты. Никаких переписываний кода.

## 1. Архитектурный вывод

**Прямой перенос невозможен.** `xrRender_R4` собирает файлы из `xrRenderPC_R4`,
`xrRender` и `xrRenderDX10` **в одну статическую библиотеку**. `CRender`,
`CHW`, `CBackend` и ресурсные классы сильно привязаны к D3D11 на уровне типов:
`ID3D11DeviceContext`, `ID3D11Buffer`, `ID3D11ShaderResourceView`,
`ID3D11RenderTargetView` и т.д. пронизывают все заголовки и `.cpp` файлы.

`RenderFactory` (`RenderFactory.h`) — фабрика игровых/render-объектов,
**не готовый RHI**. Абстракции над GPU API нет.

## 2. Карта классов и D3D11-зависимостей

### 2.1 CHW (устройство) — `HW.h:13-83`, `dx10HW.cpp`

| Поле/метод | Тип D3D11 | Роль | Владелец |
|---|---|---|---|
| `pDevice` | `ID3D11Device*` | глобальный устройство | `HW` (singleton) |
| `m_pSwapChain` | `IDXGISwapChain2*` | swapchain | `HW` |
| `d3d_contexts_pool[10]` | `ID3D11DeviceContext*[10]` | 9 deferred + 1 immediate | `HW` |
| `m_pFactory` | `IDXGIFactory1*` | фабрика DXGI | `HW` |
| `m_pAdapter` | `IDXGIAdapter1*` | адаптер | `HW` |

**D3D11-вызовы:** `D3D11CreateDevice` (`dx10HW.cpp:178`), `CreateSwapChain`
(`:103-146`), 9 deferred contexts (`:192-196`), `D3D_DRIVER_TYPE_UNKNOWN` (`:180`).

**Проблема для DX12:** `ID3D11DeviceContext` — это одновременно command queue,
command list и render state. В DX12 все три роли разделены: `ID3D12CommandQueue`
(Submit), `ID3D12GraphicsCommandList` (Record/Execute), `ID3D12PipelineState`
(State). `contexts_pool[10]` нельзя заменить на `ID3D12CommandList*` без
переработки всего рендер-цикла.

### 2.2 CBackend (командный процессор) — `R_Backend.h:74-665`

| Поле | Тип D3D11 | Роль |
|---|---|---|
| `pRT[4]` | `ID3DRenderTargetView*[4]` | render targets |
| `pZB` | `ID3DDepthStencilView*` | depth-stencil |
| `decl` | `SDeclaration*` (D3D9 FVF) | vertex declaration |
| `vb` | `ID3DVertexBuffer*` | vertex buffer |
| `ib` | `ID3DIndexBuffer*` | index buffer |
| `ps/vs/gs/hs/ds/cs` | `ID3D{Vertex,Pixel,Geometry,Hull,Domain,Compute}Shader*` | шейдеры |
| `state` | `ID3DState*` | cached state object |
| `T/M/C` | `STextureList*` и др. | texture/matrix/constant lists |

**Проблема для DX12:**
- Шейдеры: D3D11 использует `ID3D11VertexShader` и т.д. DX12 не имеет
  ID3D12*Shader — шейдеры хранятся как `D3D12_SHADER_BYTECODE` внутри PSO.
- State: D3D11 имеет отдельные `ID3D11BlendState`, `ID3D11DepthStencilState`,
  `ID3D11RasterizerState`. DX12 объединяет всё в **PSO**
  (`ID3D12PipelineState`) — монолитный объект, включая signature входных
  данных.
- Vertex declaration: D3D11 `ID3DInputLayout` → DX12 `D3D12_INPUT_LAYOUT_DESC`
  внутри root signature / PSO.
- `D3DVERTEXELEMENT9` (D3D9 legacy) используется в `tss_def.cpp` и
  транслируется `dx10StateUtils.cpp`.

### 2.3 CResourceManager — `ResourceManager.h:15-196`

Хранит:
- `map_RT m_rtargets` — `CRT*` (render targets)
- `map_Texture m_textures` — `CTexture*`
- `map_VS/PS/GS/HS/DS/CS m_vs/m_ps/...` — `SVS*`, `SPS*` и т.д.
- `v_constant_buffer[R__NUM_CONTEXTS]` — `dx10ConstantBuffer*`
- `v_states` — `SState*` (cached D3D11 state objects)
- `v_declarations` — `SDeclaration*` (D3D9 vertex declarations)

**Проблема для DX12:**
- Каждый тип ресурса (VS/PS/GS/...) создаётся и уничтожается по-разному.
  DX12 унифицирует всё через PSO + root signature.
- `SDeclaration` основан на `D3DVERTEXELEMENT9` — DX12 использует
  `D3D12_INPUT_ELEMENT_DESC`.
- `SState` — D3D11 state objects. DX12: нет аналога, всё в PSO.

### 2.4 CRT (render target) — `SH_RT.h:4-54`

| Поле | Тип | Проблема для DX12 |
|---|---|---|
| `pSurface` | `ID3DTexture2D*` | DX12: `ID3D12Resource` + heap |
| `pRT` | `ID3DRenderTargetView*` | DX12: descriptor heap (RTV) |
| `pZRT[10]` | `ID3DDepthStencilView*[10]` | DX12: descriptor heap (DSV) |
| `pUAView` | `ID3D11UnorderedAccessView*` | DX12: UAV descriptor heap |

**64 RT** (60 `ref_rt` + `rt_base` vector) — каждый с собственными дескрипторами
RTV/DSV/UAV. В DX12 все дескрипторы живут в heap'ах с явным lifetime.

### 2.5 CTexture — `SH_Texture.h:9-135`

| Поле | Тип | Проблема |
|---|---|---|
| `pSurface` | `ID3DBaseTexture*` | DX12: `ID3D12Resource` |
| `m_pSRView` | `ID3DShaderResourceView*` | DX12: SRV descriptor heap |
| `srv_per_slice` | `xr_vector<ID3DShaderResourceView*>` | per-slice SRV |

### 2.6 dx10StateManager — `dx10StateManager.h:3-92`

Кэширует D3D11 state objects:
- `ID3DRasterizerState* m_pRState`
- `ID3DDepthStencilState* m_pDepthStencilState`
- `ID3DBlendState* m_pBlendState`

**Проблема для DX12:** все три типа state objects исчезают. Их заменяет
`ID3D12PipelineState` (PSO), который включает rasterizer, depth-stencil,
blend, input layout, sample mask и т.д. одним объектом.

### 2.7 CRender (RImplementation) — `r4.h:51-465`

Глобальный синглтон `RImplementation`. Содержит:
- `contexts_pool[R__NUM_CONTEXTS]` — `R_dsgraph_structure` (по одной на
  каждый deferred context)
- `nVB/xVB` — `xr_vector<ID3DVertexBuffer*>` (vertex buffers)
- `nIB/xIB` — `xr_vector<ID3DIndexBuffer*>` (index buffers)
- `QuadIB` — `ID3DIndexBuffer*`

**Проблема для DX12:**
- Vertex/index buffers: D3D11 `ID3D11Buffer` → DX12 `ID3D12Resource` +
  vertex buffer view (`D3D12_VERTEX_BUFFER_VIEW`) + index buffer view.
- `contexts_pool[10]` — каждая `R_dsgraph_structure` содержит `CBackend`,
  который привязан к `ID3D11DeviceContext`.

## 3. Оконный цикл и Device

`CRenderDevice::Run()` (`device.cpp:417`) → `Begin()` → `CRender::Render()`
→ `End()` → `Present`.

`dxRenderDeviceRender.cpp:101-156` — `Create()`:
- `HW.CreateDevice(hWnd)` → `D3D11CreateDevice`
- `Resources = xr_new<CResourceManager>()`

`dxRenderDeviceRender.cpp:70-92` — `Reset()`:
- `HW.ResetDevice(hWnd)` → `IDXGISwapChain::ResizeBuffers`

**Проблема для DX12:**
- Swapchain: `IDXGISwapChain2` → `IDXGISwapChain4` + `Present1`
- Resize: `ResizeBuffers` → освобождение всех RTV/DSV + пересоздание
  swapchain buffers + descriptor heaps
- `Present` в D3D11 неявно переводит ресурсы в `PRESENT` state. В DX12
  нужен явный `D3D12_RESOURCE_BARRIER` (`RENDER_TARGET` → `PRESENT`).

## 4. Шейдеры

**424 HLSL-файла** в `Game/Resources_CoP_1.6.02/gamedata/shaders/r3/`:
- 206 `.ps`, 120 `.vs`, 3 `.gs`, 1 `.hs`, 1 `.ds`, 0 `.cs`, 125 `.s`, 93 `.h`

**Компиляция:**
- Сейчас: `D3DCompile` (`r4.cpp:873`) → DXBC (SM 5.0)
- Для DX12 raster: можно продолжить использовать DXBC (SM 5.0) через
  `d3dcompiler.lib`, либо перейти на DXC (`dxcompiler.lib`) → DXIL (SM 6.x)
- Для DXR: только DXC → DXIL (`lib_6_3`, `raygeneration_6_0` и т.д.)

**Проблема:**
- 424 файла шейдеров используют `vs_5_0`/`ps_5_0`/`gs_5_0`/`hs_5_0`/`ds_5_0`.
- DX12 raster может принять DXBC без перекомпиляции (через `d3dcompiler.lib`).
- DXR требует DXIL — все новые трассировочные шейдеры нужно компилировать
  через DXC.
- Смешение DXBC (растр) и DXIL (DXR) в одном проекте возможно, но
  требует двух путей компиляции.

## 5. Предлагаемая граница backend

| Слой | Текущее состояние | Предлагаемая граница |
|---|---|---|
| Device | `CHW` (D3D11) | `CHW12` (D3D12) — новый класс, параллельно с существующим |
| Command List | `CBackend` (D3D11 context) | `CBackend12` (D3D12 command list) |
| Resources | `CResourceManager` | Новый `CResourceManager12` |
| State | `dx10StateManager` | Заменяется PSO cache |
| Swapchain | `IDXGISwapChain2` | `IDXGISwapChain4` |
| Window | `CRenderDevice` | Без изменений (hwnd, message loop) |
| Frame | `CRender::Render()` | Без изменений (игровая логика) |
| Shaders | `r4.cpp:674-906` | Новый путь компиляции через DXC для DXR |

**Принцип:** не заменять существующий `CHW`/`CBackend` в одном коммите.
Создать параллельный DX12-путь, переключение через флаг сборки.
DX11 baseline остаётся основным путём и способом отката.

## 6. Сравнение альтернатив по риску

| Альтернатива | Риск | Описание |
|---|---|---|
| **A. Полная замена CHW/CBackend** | Критический | Всё переписывается разом. Нет откатa. Невозможно тестировать поэтапно. |
| **B. Параллельный DX12-путь (рекомендуется)** | Высокий | `CHW12` + `CBackend12` + новый resource manager. Переключение через флаг. DX11 остаётся. |
| **C. Минимальный DX12 для DXR only** | Средний | DX12 только для compute/DXR проходов, растр остаётся на D3D11. Сложная интеграция: два GPU API в одном кадре, общие ресурсы через shared handles. |
| **D. Compute-трассировка на DX11 (Stage D)** | Средний | `cs_5_0` + DDA. Без DX12. Ограничения screen-space, но проще. |

**Рекомендация:** B с элементов D (compute-гейт B2.0/D0 — оба требуют
минимальный `Dispatch` + UAV, что является хорошей подготовкой к DX12).

## 7. Что НЕ нужно делать

1. **Не переносить все шейдеры на SM6 сразу.** DX12 raster принимает DXBC.
2. **Не заменять все 424 HLSL файла разом.** Сначала — вертикальный срез.
3. **Не добавлять пользовательский переключатель DX11/DX12** до F4/F5.
   Экспериментальный режим — через сборку/флаг.
4. **Не строить render graph** на F0. При необходимости — минимальный
   state/lifetime tracker.

## 8. Следующий шаг

**F1 — проектирование.** Выбрать конкретную архитектуру параллельного
DX12-пути: интерфейсы device, swapchain, command recording, shader & resources.
Описать правила совместного существования с DX11. Результат — ADR с явными
правилами ABI, ownership, переключения/отката.
