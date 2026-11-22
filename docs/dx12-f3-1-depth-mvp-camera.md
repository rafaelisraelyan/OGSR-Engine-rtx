# F3.1 — Depth Buffer, MVP Transformations & 3D Camera

## HEAD и ветка

| Параметр | Значение |
|---|---|
| Ветка | `prypiat` |
| HEAD | `7ae6958d7b6acde77e5cd65cd73eca3cfe4743fa` — F3.0 |
| Предшествующий документ | `docs/dx12-f2-first-frame.md`, раздел F3.0 (PASS) |
| GPU | NVIDIA GeForce RTX 5070 Laptop GPU (10DE:2D18) |
| Дата | 2026-10-15 |
| Изменения кода | **Отсутствуют** — только документация |

## Статус

**DESIGN AGREED.** Архитектура согласована с владельцем проекта. Исполнительный
промпт для Hermes — в разделе 12. Код не написан, коммит и push не выполнялись.

---

## 1. Принятые решения

| # | Решение | Обоснование |
|---|---|---|
| D1 | Формат depth — `DXGI_FORMAT_D32_FLOAT` | Stencil в smoke-пути не используется; `D32` исключает из зоны валидации stencil-состояния и subresource-семантику. Миграция на F4 — смена одной константы и одного поля PSO, а не архитектуры |
| D2 | Три per-frame constant buffer по 256 байт | MVP меняется каждый кадр, в полёте 3 кадра — см. §3 |
| D3 | Root parameter `CBV`, register 0, `ShaderVisibility_ALL` | Дескриптор не нужен: адрес передаётся через `SetGraphicsRootConstantBufferView`. Убирает CBV_SRV_UAV heap, шейдер-visible heap и его пересоздание при resize. Незаданный root CBV GBV ловит как execution error |
| D4 | DirectXMath для матриц | Header-only, уже в дереве (`Update_Components.cmd:7-8`), уже в include-путях DX11-слоя (`xrRender_R4.vcxproj:64`). CI не меняется. Нужна одна правка include-пути в `xrRender_R5_DX12.vcxproj` |
| D5 | Вершины POSITION + COLOR (`R32G32B32A32_FLOAT`) | Однотонный куб выглядит как шестиугольный силуэт: перекрытие передних и задних граней визуально не доказуемо. COLOR делает depth-тест наблюдаемым и прогревает multi-attribute input layout до F3.2 |
| D6 | DX12 Core не зависит от `xrCore` | DX12-библиотека сейчас не ссылается ни на один OGSR-проект. `Fmatrix`/`Fvector` создали бы зависимость ровно того вида, которую при адаптации под IX-Ray придётся выдирать заново |
| D7 | `SmokeTriangle12` → `SmokeMesh12`, файлы `dx12mesh.{h,cpp}` | Содержание компонента меняется принципиально: depth-tested, MVP, куб. Имя «Triangle» на кубе сделало бы код лгущим. Переименование без рефакторинга, регрессионные проверки сохраняются |
| D8 | `CullMode = NONE` сохраняется | F3.1 валидирует глубину, а не winding order. Winding проверяется на реальном формате вершин OGSR в F3.2 |
| D9 | Шейдеры — строки в `.cpp`, компиляция через `D3DCompile` | Сохраняется подход F3.0. DXC и DXIL не подключаются: они нужны только для DXR |

## 2. Уточнения к черновику дизайна

### 2.1 `D3D12_CLEAR_VALUE` не является требованием API

В черновике было написано, что для depth-stencil в состоянии `DEPTH_WRITE`
optimized clear value **обязателен**. Это неверно.

По `ID3D12Device::CreateCommittedResource` параметр `pOptimizedClearValue`
помечен `[in, optional]`. Единственное жёсткое требование API:

> When you use `D3D12_RESOURCE_DIMENSION_BUFFER`, you must set
> `pOptimizedClearValue` to `nullptr`.

Для текстур с флагом `ALLOW_DEPTH_STENCIL` (или `ALLOW_RENDER_TARGET`)
документация формулирует это как рекомендацию:

> you should choose the value with which the clear operation will most
> commonly be called. You can call the clear operation with other values,
> but those operations won't be as efficient as when the value matches the
> one passed in to resource creation.

**Решение:** `D3D12_CLEAR_VALUE { D32_FLOAT, { 1.0f, 0 } }` передаётся, потому
что мы очищаем ровно этим значением каждый кадр и получаем оптимизированную
очистку. Это оптимизация, а не требование корректности. Несовпадение значения
со `ClearDepthStencilView` не сломало бы рендер — только замедлило очистку.

### 2.2 Воспроизводимый depth-тест с автоматической проверкой

Описан в §5. Ключевое свойство: тест **дискриминирующий**, а не декоративный.
При сломанном depth-тесте он падает, а не проходит «по совпадению».

Проверка выполняется через `GetCopyableFootprints` + `CopyTextureRegion`,
а **не** через `CopyResource`: backbuffer — Texture2D, readback — Buffer, и
`CopyResource` требует совместимых типов ресурсов. Типы копирования —
`D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX` для источника и
`D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT` для приёмника; размера буфера
берите из `pTotalBytes`. Подробности — в §5.3.

### 2.3 Одна ViewProjection на кадр: мировые координаты в вершинах

F3.1 использует **вариант A**: одна общая `ViewProjection`, мировые координаты
записаны непосредственно в вершины обоих кубов. Вариант B (отдельные
выровненные CB-region на каждый draw) не реализуется.

Причина запрета на перезапись: в кадре два draw call, и между ними **нельзя**
переписывать одну и ту же CB-память. GPU может ещё читать предыдущие данные,
которыми уже запечён `SetGraphicsRootConstantBufferView` первого draw. GBV это
не ловит: адрес выровнен и валиден, но второй куб получит матрицу первого, а
первый — потенциально мусор. Вариант A устраняет класс ошибки целиком, потому
что матрица не зависит от объекта.

Следствие, зафиксированное как ограничение: геометрия «запечена» в мировых
координатах и не может быть независимо трансформирована. Per-object transforms
появятся в F3.2 вместе с индексным буфером и корректной схемой дескрипторов.

### 2.4 Автоматическая орбитальная камера и фиксированные ракурсы

Описан в §4.3. Пользовательский ввод не добавляется.

### 2.5 Допуски float в CPU-тестах и контракт WVP/transpose/mul

Описан в §6. Вывод контракта дан поэлементно, с разбором смещений памяти.
Эталон проекционной матрицы для `near = 0.1`, `far = 100` приведён в §6.5.

### 2.6 Lifetime постоянно отображённых constant buffers

Описан в §3.3. Запись только после ожидания fence слота; при карантине
`Detach` с обнулением CPU-указателей; при штатном Shutdown — очистка после
GPU drain.

### 2.7 Порядок операций в RenderFrame

В черновике barrier стоял **до** ожидания fence слота и `Reset` аллокатора.
Это неверно и ломает порядок F3.0. Исправленный порядок — в §4.6.

---

## 3. Constant buffers: архитектура и lifetime

### 3.1 Почему CB на слот, а не один общий

Главная корректностная точка F3.1. MVP меняется каждый кадр, в полёте 3 кадра.

- **Один общий CB — гонка.** Пока GPU читает CB для кадра N, CPU пишет в него
  для кадра N+2. Debug Layer и GBV это **не ловят**: с точки зрения валидации
  адрес корректен и выровнен, а данные просто оказываются «не той» матрицей.
  Артефакт выглядит как дрожание или неверная проекция при нулевых validation
  errors.
- **CB на слот — безопасно по уже существующему инварианту.** `RenderFrame`
  в начале ждёт `fenceValue` **этого слота** (`CHW12.cpp:742-752`), значит
  предыдущая отправка слота завершена и единственный потенциальный читатель
  CB этого слота мёртв. Новая синхронизация не нужна.
- Реализация: `FrameResource` получает `ComPtr<ID3D12Resource> constantBuffer`
  и `void* mapped`. Upload heap, `Map` один раз при создании, `Unmap` не
  вызывается. Порядок записи внутри кадра не важен: адрес запекается в
  `SetGraphicsRootConstantBufferView` в момент записи команд.

### 3.2 Параметры буфера

| Параметр | Значение |
|---|---|
| Heap | `D3D12_HEAP_TYPE_UPLOAD` |
| `InitialResourceState` | `D3D12_RESOURCE_STATE_GENERIC_READ` (требование API для upload heap) |
| `pOptimizedClearValue` | `nullptr` (требование API для `DIMENSION_BUFFER`) |
| Размер | 256 байт — `D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT` |
| Содержимое | одна `float4x4` `ViewProjection` (64 байта), остальное — резерв |
| Количество | `FRAME_COUNT`, по одному на слот |
| Записей за кадр | **Ровно одна.** Оба draw call используют один и тот же адрес |

World-матрицы нет (§6.1), поэтому WVP в F3.1 не существует как понятие.
Root parameter: `D3D12_ROOT_PARAMETER_TYPE_CBV`, `ShaderVisibility_ALL`,
register 0, space 0. Флаг
`D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT` сохраняется —
без него лист с `IASetVertexBuffers` не проходит validation
(`dx12triangle.cpp:332`).

### 3.3 Lifetime: три состояния

**Запись.** Каждый кадр, после ожидания `frame.fenceValue` в начале
`RenderFrame` и после `Reset` аллокатора, до `ExecuteCommandLists`. Это
единственный допустимый момент: гарантия даётся ожиданием слота. Запись вне
этого окна — гонка.

**Карантин (`GpuLifetime::Unknown`).** Владение передаётся через `Detach()`,
счётчики ссылок не меняются, `AddRef` не нужен. **CPU-указатель `mapped`
обнуляется** в том же вызове. Это не косметика: после `Detach` объект
принадлежит карантину, и оставшийся валидным `mapped` позволял бы писать в
upload heap, который потенциально ещё читает GPU. Обнуление делает запись
невозможной, а не «плохой практикой».

**Штатный Shutdown (`WorkCompleted` / `DeviceRemoved` / `NoWorkSubmitted`).**
Порядок: `DetermineGpuLifetime()` → при подтверждённом дренаже `mapped =
nullptr` явно, затем `constantBuffer.Reset()` на каждом слоте, затем
`m_device.Reset()`. Обнуление `mapped` идёт **до** `Reset()` ресурса: dangling
указатель на освобождённый upload heap не должен существовать даже временно.

**Инвариант, который фиксируется и не подлежит «починке»:** `Map` без `Unmap`
— штатный режим для upload-heap CB. Добавляющий `Unmap` «для симметрии»
получает невалидный `mapped` на следующем кадре.

---

## 4. Компоненты

### 4.1 Владение

| Компонент | Владеет |
|---|---|
| `CHW12` | device, queue, swapchain, per-frame allocators/lists, RTV heap, **DSV heap + depth buffer**, **per-frame CB + mapped**, fence, GPU lifetime, readback resource (в тестовом режиме) |
| `SmokeCamera12` (`dx12camera.{h,cpp}`) | только математика view/proj/WVP, ни одного COM-объекта |
| `SmokeMesh12` (`dx12mesh.{h,cpp}`) | vertex/upload buffer, root signature, PSO, shader blobs |

`SmokeCamera12` выделен именно потому, что не содержит COM: он тестируем на
CPU без GPU и переносим в IX-Ray-адаптер как есть.

### 4.2 Depth buffer

| Параметр | Значение |
|---|---|
| Heap | `D3D12_HEAP_TYPE_DEFAULT` |
| Флаг | `D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL` |
| `InitialResourceState` | `D3D12_RESOURCE_STATE_DEPTH_WRITE` |
| `pOptimizedClearValue` | `{ DXGI_FORMAT_D32_FLOAT, { 1.0f, 0 } }` — оптимизация очистки (§2.1) |
| Размер | `m_width × m_height` |
| DSV heap | `D3D12_DESCRIPTOR_HEAP_TYPE_DSV`, `NumDescriptors = 1`, `FLAG_NONE` |

Для **текстур**, в отличие от буферов, `InitialResourceState` применяется
по-настоящему (у буферов он игнорируется — разбор в `dx12triangle.cpp:278-299`),
поэтому `DEPTH_WRITE` корректен и барьер «в» него не нужен.

Пока глубину никто не сэмплирует, она остаётся в `DEPTH_WRITE` весь кадр —
**барьеров на depth в F3.1 нет**. Переход в `PIXEL_SHADER_RESOURCE` появится
вместе с чтением глубины и будет отдельной задачей.

`DSVFormat` в PSO обязан совпадать с форматом ресурса. Расхождение даёт
validation error, то есть детектируемо.

### 4.3 Камера

| Режим | Как включается | Поведение |
|---|---|---|
| Орбита (default) | без аргументов | Yaw растёт линейно по `QueryPerformanceCounter`, pitch фиксирован. Камера крутится вокруг origin, perspective подтверждается движением. Мировые координаты кубов не меняются — вращается камера |
| Фиксированный ракурс | `-fixedpose` | Детерминированные позиция/target/FOV. Для тестов и для сравнения кадров между прогонами |

Ввода с клавиатуры и мыши нет и не планируется на F3.1. `FOV = 60°`,
`near = 0.1`, `far = 100`.

### 4.4 Геометрия

**Один vertex buffer на 72 вершины** (два куба по 36), **без индексного
буфера** — индексы переносятся в F3.2. Формат вершины:
`POSITION` (`R32G32B32_FLOAT`) + `COLOR` (`R32G32B32A32_FLOAT`), каждая грань
своим цветом.

Раскладка буфера:

| Диапазон вершин | Объект |
|---|---|
| `[0, 36)` | Ближний куб, около origin |
| `[36, 72)` | Дальний куб, сдвинут по Z так, чтобы перекрываться на экране |

**Мировые координаты записаны непосредственно в вершины** (вариант A, §2.3).
Матрица одна на кадр и от объекта не зависит, поэтому между двумя draw call CB
не перезаписывается.

### 4.4a Интерфейс `SmokeMesh12::Draw`

```cpp
void Draw(ID3D12GraphicsCommandList* cmd,
          D3D12_GPU_VIRTUAL_ADDRESS    cbVA,
          UINT                         firstVertex,
          UINT                         vertexCount);
```

Вызовы из `RenderFrame`:

```cpp
m_mesh.Draw(cmd, frame.constantBuffer->GetGPUVirtualAddress(), 0,  36);  // ближний
m_mesh.Draw(cmd, frame.constantBuffer->GetGPUVirtualAddress(), 36, 36);  // дальний
```

Внутри — один `D3D12_VERTEX_BUFFER_VIEW` на весь буфер и два вызова
`DrawInstanced` с разными `StartVertexLocation`:

```cpp
cmd->DrawInstanced(vertexCount, 1, firstVertex, 0);
```

`Draw` **не пишет в CB** — только читает переданный адрес и вызывает
`SetGraphicsRootConstantBufferView(0, cbVA)`. Повторный вызов с тем же
адресом допустим и дешев: корневой параметр переустанавливается, память не
трогается. Границы диапазона валидируются внутри `Draw`:
`firstVertex + vertexCount <= 72`, иначе в лог уходит отказ и вызов не
выполняется — выход за пределы буфера был бы GPU page fault.

Одна `ViewProjection` остаётся общей, CB не перезаписывается между вызовами.

### 4.5 PSO

| Поле | Значение |
|---|---|
| `DepthEnable` | `TRUE` |
| `DepthFunc` | `D3D12_COMPARISON_FUNC_LESS` |
| `DepthWriteMask` | `D3D12_DEPTH_WRITE_MASK_ALL` |
| `StencilEnable` | `FALSE` |
| `DSVFormat` | `DXGI_FORMAT_D32_FLOAT` |
| `CullMode` | `D3D12_CULL_MODE_NONE` |
| `InputLayout` | POSITION + COLOR |
| `RTVFormats[0]` | `DXGI_FORMAT_R8G8B8A8_UNORM` |
| `NumRenderTargets` | 1, `SampleDesc.Count = 1` |

### 4.6 RenderFrame

Порядок **точно** как в F3.0 (`CHW12.cpp:730-870`), расширенный depth, камерой
и CB. Ожидание fence слота и `Reset` аллокатора/листа идут **первыми**, до
любого `ResourceBarrier`:

1. Проверка `m_initialized` и `m_deviceRemoved`.
2. `FrameResource& frame = m_frameResources[m_frameIndex]`.
3. **Ожидание `frame.fenceValue`** этого слота (`CHW12.cpp:742-752`). GPU
   завершил предыдущую отправку слота — аллокатор, лист и CB слота безопасны
   для записи.
4. `frame.commandAllocator->Reset()`.
5. `frame.commandList->Reset(allocator, nullptr)`.
6. Барьер RTV `PRESENT → RENDER_TARGET`.
7. `OMSetRenderTargets(1, &rtvHandle, FALSE, &m_dsv)`.
8. Viewport и scissor из актуальных `m_width`/`m_height`.
9. `ClearRenderTargetView`.
10. `ClearDepthStencilView(m_dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr)`.
11. Расчёт `ViewProjection` камерой.
12. `memcpy` в `frame.mapped` — единственная запись CB в кадре.
13. Два вызова с общим адресом CB, без перезаписи между ними (§4.4a):
    `m_mesh.Draw(cmd, frame.constantBuffer->GetGPUVirtualAddress(), 0, 36)`
    затем `..., 36, 36`.
14. Барьер RTV `RENDER_TARGET → PRESENT`.
15. `Close()`, `ExecuteCommandLists()`, `Present(1, 0)`,
    `frame.fenceValue = ++m_fenceValue`, `Signal`.
16. `MoveToNextFrame()`, `DrainInfoQueue()`.

**Почему ожидание слота стоит до барьера.** Аллокатор нельзя сбрасывать, пока
GPU читает записанные в него команды. Барьер — такая же команда: запись его в
аллокатор до подтверждённого завершения предыдущей отправки была бы записью в
память, которую GPU потенциально ещё читает. Порядок F3.0 соблюдается
дословно; F3.1 только вставляет шаги 10–12 между 9 и 13, не меняя границ.

### 4.7 Resize

Порядок: `WaitForGpu` → сброс per-frame `fenceValue` → освобождение
lists/allocators/renderTargets → **освобождение depth buffer и DSV heap** →
`m_rtvHeap.Reset()` → `ResizeBuffers` → `CreateRenderTargetViews` →
**`CreateDepthBuffer`** → `CreateFrameResources` (CB пересоздаются вместе со
слотами, `Map` заново) → `m_camera.SetAspect(w, h)`.

`SetAspect` обязателен: иначе проекция останется от старого соотношения сторон,
куб растянется, и это **не** validation error, а тихая визуальная регрессия.

---

## 5. Воспроизводимый depth-тест с автоматической проверкой

### 5.1 Замысел

Один общий D32 depth buffer на все три слота кадра. Тест должен доказать, что
depth-тест действительно отсекает дальние фрагменты, **а не** что картинка
«выглядит правильно».

### 5.2 Сцена

| Объект | Позиция | Цвет | Порядок отрисовки |
|---|---|---|---|
| Ближний куб | ближе к камере | A | **первым** |
| Дальний куб | дальше, перекрывается с ближним в экранном пространстве | B | **последним** |

Порядок обратен painter's algorithm **намеренно**. Если depth-тест выключен
или сломан, дальний куб рисуется последним и **перезатирает** ближний — тест
падает. При работающем depth-тесте ближний куб первым пишет цвет A и глубину,
дальний проваливает `LESS` и не пишет ничего.

### 5.3 Автоматическая проверка через readback

Визуальная проверка заменяется чтением пикселей из backbuffer.

**`CopyResource` здесь неприменим.** Backbuffer — `Texture2D`,
readback — `Buffer`. `ID3D12GraphicsCommandList::CopyResource` требует, чтобы
источник и приёмник были одного типа: `Texture2D → Buffer` возвращает
`E_INVALIDARG`, и Debug Layer дополнительно ругается на несовместимые
`D3D12_RESOURCE_DESC`. Копирование текстуры в буфер делается только через
`CopyTextureRegion` с placed footprint.

**Последовательность:**

1. **Один раз при инициализации теста** (и повторно при resize, §5.6) —
   получить layout через `GetCopyableFootprints`:

   ```cpp
   D3D12_RESOURCE_DESC bbDesc = frame.renderTarget->GetDesc();
   UINT   numRows        = 0;
   UINT64 rowSizeInBytes = 0;
   UINT64 totalBytes     = 0;
   D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};

   device->GetCopyableFootprints(
       &bbDesc, 0, 1, 0,
       &layout, &numRows, &rowSizeInBytes, &totalBytes);
   ```

   `layout.Footprint.RowPitch` — фактический шаг строки. Для
   `R8G8B8A8_UNORM` он **не обязан** равняться `width * 4`: драйвер выравнивает
   по `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT` (256 байт), поэтому при ширине,
   не кратной 64 пикселям, в конце каждой строки идёт padding. Чтение по
   `width * 4` дало бы сдвиг всей картинки начиная со второй строки.

   **Допустимость результата проверяется**, а не предполагается:

   ```cpp
   if (numRows == 0 || totalBytes == 0 ||
       layout.Footprint.RowPitch == 0 ||
       totalBytes < static_cast<UINT64>(numRows) * layout.Footprint.RowPitch)
   {
       dx12log::Write("[DX12][readback] GetCopyableFootprints returned an "
                      "unusable layout: rows=%u totalBytes=%llu rowPitch=%u",
                      numRows, static_cast<unsigned long long>(totalBytes),
                      layout.Footprint.RowPitch);
       return false;
   }
   ```

   Последнее неравенство — не педантизм: `pTotalBytes` по документации
   включает padding, и он **может** быть меньше `numRows * RowPitch`, если
   драйвер упаковывает подресурсы плотно. Буфер размером ровно `totalBytes`
   при этом всё ещё корректен, потому что `PlacedFootprint.Offset` последнего
   подресурса плюс его `RowPitch * numRows` не выходит за `totalBytes`. Но
   полагаться на это без проверки нельзя — при расхождении лучше отказаться,
   чем поймать page fault в readback.

2. Создать readback-ресурс: `D3D12_HEAP_TYPE_READBACK`,
   `D3D12_RESOURCE_DIMENSION_BUFFER`, `Width = totalBytes`,
   `D3D12_RESOURCE_STATE_COPY_DEST`, `pOptimizedClearValue = nullptr`.

   Именно `totalBytes`, а не `RowPitch * numRows`: первое может быть меньше за
   счёт упаковки, и выделение по нему не оставляет мусора в буфере. Для нашего
   случая (один подресурс, `BaseOffset = 0`) значения обычно совпадают, но
   источник размера — `pTotalBytes`.

3. В тестовом кадре, после обоих `Draw` и **до** барьера в `PRESENT`:

   ```cpp
   // барьер backbuffer RENDER_TARGET -> COPY_SOURCE

   D3D12_TEXTURE_COPY_LOCATION dst = {};
   dst.pResource       = m_readbackBuffer.Get();
   dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
   dst.PlacedFootprint = m_copyableFootprint;   // Offset = 0

   D3D12_TEXTURE_COPY_LOCATION src = {};
   src.pResource        = frame.renderTarget.Get();
   src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
   src.SubresourceIndex = 0;

   cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

   // барьер обратно COPY_SOURCE -> PRESENT
   ```

   `D3D12_TEXTURE_COPY_TYPE` имеет ровно два значения:
   `D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX = 0` и
   `D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT = 1`
   (`d3d12.h:3329-3333`, SDK 10.0.26100.0). Значения
   `SUBRESOURCE_FOOTPRINT` не существует.

   `Type` и соответствующее поле union **обязаны** согласовываться: при
   `PLACED_FOOTPRINT` читается `PlacedFootprint`, при `SUBRESOURCE_INDEX` —
   `SubresourceIndex`. Запись неправильного поля даёт UB, а не ошибку
   компиляции: union не типизирован по discriminant.

   `PlacedFootprint.Offset = 0`, координаты копии `(0, 0, 0)` — копируем весь
   backbuffer в начало буфера.

4. **`WaitForFenceValue()` до `Map`, а не просто до конца кадра.** Копирование
   выполняется GPU; `Map` readback-буфера до подтверждённого fence completion
   читает память, которую GPU ещё пишет. Ожидание выполняется через
   `CHW12::WaitForFenceValue()`, а результат сверяется с
   `CheckFenceForRemoval` (`UINT64_MAX` = device removed, не «всё готово»).

5. `Map` readback с `D3D12_RANGE{0, totalBytes}`, чтение пикселей по адресу
   `base + layout.PlacedFootprint.Offset + y * layout.Footprint.RowPitch + x * 4`,
   `Unmap` с `D3D12_RANGE{0, 0}`.

   `Offset` входит в адрес явно. В нашем случае он нулевой, но формула,
   включающая его, остаётся корректной при любом `BaseOffset` и не требует
   переписывания, если копирование когда-либо уйдёт не в начало буфера.

Readback-ресурс создаётся **только** в тестовом режиме и включается в карантин
наравне с остальными GPU-ресурсами.

### 5.6 Пересоздание readback при resize

RowPitch и число строк зависят от размера backbuffer, поэтому readback
пересоздаётся вместе с ним. В `CHW12::Resize`:

1. Освободить старый readback **до** `ResizeBuffers` — он не держит ссылок на
   backbuffer через дескрипторы, но пересоздание с другим `Width` требует,
   чтобы старый буфер был освобождён, иначе видеопамять удерживается зря.
2. После успешного `ResizeBuffers` и `CreateRenderTargetViews` вызвать
   `GetCopyableFootprints` заново на **новом** дескрипторе backbuffer, сохранив
   результат в `m_copyableFootprint` и `m_readbackTotalBytes`.
3. Пересоздать readback с `Width = totalBytes`.

Старые `RowPitch`, `Offset` и `totalBytes` после resize **недействительны**:
они описывали прежний backbuffer. Использование сохранённого footprint от
прежнего размера дало бы чтение за пределами буфера.

`GetCopyableFootprints` вызывается на `ID3D12Device` и **не** требует валидного
ресурса — только корректного `D3D12_RESOURCE_DESC`, поэтому его можно вызывать
до создания буфера. Но в нашей последовательности backbuffer уже существует, и
описание берётся с него, что исключает рассинхронизацию описания и реальности.

Порядок в `Resize` с учётом readback:

```
WaitForGpu
→ сброс per-frame fenceValue
→ освобождение lists/allocators/renderTargets
→ освобождение depth buffer, DSV heap и readback buffer
→ m_rtvHeap.Reset()
→ ResizeBuffers
→ CreateRenderTargetViews
→ CreateDepthBuffer
→ CreateReadback (только в тестовом режиме)
→ CreateFrameResources (CB, Map заново)
→ m_camera.SetAspect(w, h)
```

### 5.4 Утверждения (три, включая контроль достоверности)

| # | Пиксель | Ожидание (RGBA 0–255) | Что доказывает |
|---|---|---|---|
| 1 | (640,360) — центр экрана | (0,255,0,255) — зелёный | Depth-тест отсекает дальний фрагмент: в центре видна задняя грань ближнего куба (z=-1, зелёная) |
| 2 | (640,180) — выше центра | (0,0,255,255) — синий | Дальний объект действительно нарисован — тест не проходит «по совпадению», если бы куб был невидим |
| 3 | (640,40) — верх экрана | (18,41,82,255) — фон | Backbuffer и viewport согласованы |

**Обоснование координат.** Фиксированный ракурс (§4.3): eye=(0,0,-6),
target=(0,0,0), up=(0,1,0), FOV=60°, aspect=1280/720.
- Ближний куб (центр (0,0,0), z∈[-1,1]): задняя грань z=-1 находится на
  расстоянии 5 от камеры — это ближайшая поверхность в направлении взгляда.
  В проекции она покрывает центральную область экрана, включая (640,360).
- Дальний куб (центр (0,0,2), z∈[0,4]): передняя грань z=0 на расстоянии 6.
  Её проекция покрывает область y∈[182,538], что включает (640,180), но не
  включает (640,40) — там виден фон.
- Фон `kClearColor = {0.07, 0.16, 0.32, 1.0}` → (18,41,82,255) в UNORM.

Утверждение 2 — контроль достоверности (falsification control). Без него тест
проходил бы и при полностью невидимом дальнем кубе, то есть не проверял бы
ничего.

Допуск: точное совпадение байта (UNORM, `BlendEnable = FALSE`).

Адресация пикселя — **через фактический `RowPitch` и `Offset`**, а не
`width * 4` (§5.3). Тест обязан читать оба значения из
`GetCopyableFootprints`, иначе на ширине, не кратной 64, все утверждения
сдвинутся на несуществующие пиксели и тест упадёт на корректном рендере — то
есть станет ложноотрицательным.

При несовпадении любого из трёх утверждений: в лог уходят координаты,
ожидаемый и фактический байты, тест возвращает ненулевой код возврата.

### 5.5 Запуск

`xrDX12Smoke.exe -depthtest`: фиксированный ракурс, два куба, readback, три
утверждения, печать результата в лог и exit code 0/1.

---

## 6. Контракт матриц: WVP, transpose и HLSL mul

Это самая тихая из возможных ошибок F3.1: неверный порядок даёт зеркальное
или перевёрнутое изображение при **нулевых** validation errors. Поэтому
контракт фиксируется явно, с разбором памяти.

### 6.1 Порядок умножения (CPU)

DirectXMath — row-major, row-vectors, pre-multiplication. Порядок умножения
совпадает с порядком применения трансформаций:

```cpp
XMMATRIX view = XMMatrixLookAtLH(eye, target, up);
XMMATRIX proj = XMMatrixPerspectiveFovLH(fov, aspect, nearZ, farZ);
XMMATRIX viewProj = view * proj;   // XMMatrixMultiply
```

**World-матрицы в F3.1 нет.** Мировые координаты запечены в вершины (вариант
A, §2.3), поэтому в CB уходит `ViewProjection`, а не полный WVP. `world`
равен `Identity` и опускается не по недосмотру, а потому что умножение на
`Identity` ничего не меняет, а поддержка отдельной world-матрицы потребовала
бы per-object записи в CB — ровно того, что §2.3 запрещает.

В шейдере соответственно:

```hlsl
output.position = mul(float4(input.position, 1.0f), ViewProjection);
```

### 6.2 Хранение в constant buffer

DirectXMath хранит row-major: `_11, _12, _13, _14, _21, _22, ...`, то есть
элемент `_ij` лежит по смещению `4*(i-1) + (j-1)`.

Компилятор HLSL по умолчанию упаковывает матрицы в constant buffer
**column-major**: каждый столбец занимает отдельный constant register, то есть
элемент `_ij` читается по смещению `4*(j-1) + (i-1)`.

Эти два порядка противоположны, поэтому передача «как есть» интерпретирует
транспонированную матрицу. Решение — **транспонировать на CPU**:

```cpp
XMStoreFloat4x4(&dst, XMMatrixTranspose(viewProj));
```

Эквивалент: `XMMatrixMultiplyTranspose` в самом умножении.

### 6.3 Порядок аргументов `mul` в HLSL

После транспонирования на CPU контракт в шейдере — **вектор слева**:

```hlsl
output.position = mul(float4(input.position, 1.0f), ViewProjection);
```

Здесь `float4` трактуется как row vector (`float1x4`), и
`mul(float1x4, float4x4)` даёт `float1x4`. Это согласовано с row-vector
конвенцией DirectXMath.

**Запрещённая комбинация:** транспонирование на CPU + `mul(ViewProjection,
float4(...))`. Это применит матрицу дважды транспонированно, то есть исходную
`viewProj` в другом порядке. Рендер даст искажённую геометрию, validation
промолчит.

### 6.4 Допуски в CPU-тестах

Тесты математики сравнивают `float` поэлементно с **комбинированным**
допуском:

```cpp
bool FloatNear(float a, float b, float absEps, float relEps)
{
    const float diff = std::fabs(a - b);
    const float scale = std::fmax(std::fabs(a), std::fabs(b));
    return diff <= absEps || diff <= relEps * scale;
}
```

| Параметр | Значение | Почему |
|---|---|---|
| `absEps` | `1e-5f` | Ловит расхождение около нуля, где относительная погрешность бессмысленна. Один ulp при величине ~1.7 равен `2.4e-7`, поэтому `1e-5` — это ~40 ulp, с запасом на накопление при нескольких умножениях |
| `relEps` | `1e-4f` | Нужен для элементов, чья величина выходит за единицы. В `ViewProjection` присутствуют элементы вида `1/tan(fov/2)` и результат деления на `near = 0.1`; при величине ~10 один ulp равен `9.5e-7`, и жёсткий абсолютный порог `1e-5` стал бы там ~10 ulp — недостаточно для композиции двух матриц |

Чисто абсолютное сравнение (`fabs(a-b) < 1e-5`) неверно для матрицы, где
элементы имеют разный масштаб: проекционная матрица при `near = 0.1`,
`far = 100` содержит `_22 ≈ 1.732`, а полная `ViewProjection` — величину
`1/tan(fov/2)` и перспективное деление на `near`. Накопленная погрешность
float32 растёт с величиной элемента: один ulp при значении 1.732 равен
`2.4e-7`, и при нескольких умножениях подряд он законно доходит до `1e-6`.
Чисто относительное сравнение неверно около нуля: `_14`, `_24`, `_34`, `_41`,
`_42`, `_44` равны нулю ровно, и относительная погрешность там не определена.
Комбинация нужна именно потому, что в матрице одновременно есть точные нули и
величины около двух.

### 6.5 Эталон проекционной матрицы

`XMMatrixPerspectiveFovLH(fov = 60°, aspect = 16/9, nearZ = 0.1,
farZ = 100)`, row-major, **до** транспонирования:

| Элемент | Значение |
|---|---|
| `_11` | `0.9742786` |
| `_22` | `1.7320508` |
| `_33` | `1.0010010` |
| `_34` | `1.0` |
| `_43` | `-0.1001001` |
| `_44` | `0.0` |
| остальные | `0.0` |

Вывод: `_33 = far/(far-near) = 100/99.9`, `_43 = -near*far/(far-near) =
-10/99.9`, `_22 = 1/tan(fov/2)`, `_11 = _22 / aspect`.

**Важно:** это чистая проекция. `ViewProjection = view * proj` имеет другие
значения, потому что `view` домножается слева и перемешивает элементы. Тест
проекционной матрицы проверяет `proj` **отдельно**, до композиции с `view`.
Сравнение `ViewProjection` с таблицей выше — неверный тест, который провалится
на корректной реализации.

### 6.6 Валидация аргументов камеры

**Нельзя опираться на NaN от DirectXMath.** Поведение `XMMatrixLookAtLH` при
`eye == target` в документации не специфицировано как гарантированный NaN:
библиотека содержит `assert`, но `assert` отключён в `Release` (проект
собирается с `NDEBUG`), и в релизной сборке результат может оказаться
неопределённым, а не аккуратно NaN. UD на входе не даёт права на предсказуемый
выход.

Поэтому аргументы проверяются **до** вызова:

```cpp
static bool VectorIsFinite(XMVECTOR v)
{
    const XMVECTOR t = XMVector3Dot(v, v);
    // NaN не сравняется само с собой: условие ловит и NaN, и Inf, и 0.
    return !(XMVectorGetX(t) != XMVectorGetX(t)) &&
            XMVectorGetX(t) != std::numeric_limits<float>::infinity();
}

bool SmokeCamera12::SetLookAt(XMVECTOR eye, XMVECTOR target, XMVECTOR up)
{
    if (!VectorIsFinite(eye) || !VectorIsFinite(target) || !VectorIsFinite(up))
    {
        dx12log::Write("[camera] SetLookAt refused: non-finite input coordinate");
        return false;
    }

    const XMVECTOR dir = XMVectorSubtract(target, eye);
    if (XMVector3LessOrEqual(XMVector3LengthSq(dir), XMVectorReplicate(1e-12f)))
    {
        dx12log::Write("[camera] SetLookAt refused: eye == target");
        return false;
    }

    if (XMVector3LessOrEqual(XMVector3LengthSq(up), XMVectorReplicate(1e-12f)))
    {
        dx12log::Write("[camera] SetLookAt refused: zero up vector");
        return false;
    }

    // forward и up параллельны или практически параллельны: базис вырожден.
    // 1e-6f по |cos| ≈ 0.06° между векторами — заведомо в пределах погрешности
    // любой осмысленной установки камеры, но далеко от порога float32.
    const XMVECTOR dirN = XMVector3Normalize(dir);
    const XMVECTOR upN  = XMVector3Normalize(up);
    const float cosAngle = std::fabs(XMVectorGetX(XMVector3Dot(dirN, upN)));
    if (cosAngle >= 1.0f - 1e-6f)
    {
        dx12log::Write("[camera] SetLookAt refused: forward and up are "
                       "parallel (|cos| = %.9f)", cosAngle);
        return false;
    }

    // ... XMMatrixLookAtLH
}

bool SmokeCamera12::SetAspect(float aspect)
{
    if (!(aspect > 0.0f) || !std::isfinite(aspect))
    {
        dx12log::Write("[camera] SetAspect refused: aspect = %f is not "
                       "positive and finite", aspect);
        return false;
    }
    m_aspect = aspect;
    return true;
}
```

Отказ возвращает `false`, камера сохраняет предыдущую валидную матрицу.

**Проверка `aspect` отрицается, а не обрезается.** `!(aspect > 0.0f)` — не
опечатка: эта форма одним выражением отсекает и `aspect <= 0`, и NaN (NaN не
удовлетворяет `>`), поэтому `isfinite` для `aspect` избыточен, но оставлен для
ясности намерения. `XMMatrixPerspectiveFovLH` при `aspect <= 0` даёт
деление на ноль или отрицательный масштаб, то есть вывернутую картинку, а не
ошибку.

**Про «практически параллельные».** Порог `1e-6f` по `|cos|`, а не `1e-12f`
как у остальных проверок. Причина: `LookAt` нормализует `forward` и строит
базис через кросс-произведение. При `|cos| → 1` результат нормализации `up`
после ортогонализации (Gram–Schmidt) теряет точность катастрофически:
относительная ошибка растёт как `1/sin(angle)`, и при угле в сотые доли градуса
базис содержит мусор. Это уже не «почти параллельно», это численно
некорректный базис. Порог `1e-6f` соответствует примерно `0.06°` — заведомо
ниже любого осмысленного наклона камеры, но выше области потери точности.

Тест на вырожденную камеру — часть набора: он проверяет, что `SetLookAt`
**возвращает false и не меняет матрицу**, а не что результат равен NaN.
Отдельные тесты: NaN в `eye`, Inf в `target`, `up == 0`, `up == forward`,
`up == -forward`, `aspect = 0`, `aspect = -1`, `aspect = NaN`.

---

## 7. Почему один общий D32 depth buffer безопасен при единственной direct queue

Это deserves отдельного обоснования, потому что интуиция подсказывает «три
кадра в полёте — значит три depth buffer», и это неверно.

**Факт 1. Depth buffer пишется только GPU.** В отличие от constant buffer,
который CPU перезаписывает каждый кадр, depth buffer является чистым
GPU-ресурсом: CPU в него не пишет никогда. Поэтому гонка CPU-против-GPU,
которая делает общий CB недопустимым, здесь отсутствует в принципе.

**Факт 2. Внутри одной очереди команды выполняются строго в порядке отправки.**
Все три кадра пишутся в один и тот же `m_commandQueue`
(`D3D12_COMMAND_LIST_TYPE_DIRECT`). D3D12 гарантирует, что команды из одной
очереди исполняются в порядке их постановки. Следовательно `ClearDepthStencilView`
кадра N+1 не может выполняться раньше `DrawInstanced` кадра N: он стоит позже
в той же очереди.

**Факт 3. Между кадрами нет пересечения в одном backbuffer.** Каждый кадр
работает со своим слотом: свой RTV, свой allocator, свой CB, свой fenceValue.
Backbuffer'ы не переиспользуются, пока GPU не завершит соответствующий fence.

**Следствие.** Перекрытие кадров на GPU затрагивает только те ресурсы, которые
пишет CPU (constant buffers) или которые принадлежат конкретному слоту
(backbuffer). Depth buffer не попадает ни в одну категорию: он
перезаписывается целиком каждый кадр через `ClearDepthStencilView`, и этот
clear — команда очереди, а не операция CPU. Поэтому общий depth buffer
корректен.

**Что сломает это обоснование** — и почему оно должно быть пересмотрено
вместе с событием, а не заранее:

1. Появление второй очереди (compute) или нескольких direct queue. Тогда
   clear из очереди A может соперничать с draw из очереди B, и потребуется
   либо fence между очередями, либо отдельный depth buffer на очередь.
2. Чтение глубины шейдером (`PIXEL_SHADER_RESOURCE`). Требует барьеров и
   пересмотра состояний, но не само по себе ломает общий буфер.
3. Переход на несколько одновременно записываемых command list'ов из worker
   потоков (F6). Порядок внутри очереди сохраняется, но запись команд из
   нескольких потоков требует отдельных аллокаторов.

До любого из этих событий обоснование держится. Пункты 1 и 3 относятся к F5/F6
и в рамках F3.1 не активируются.

---

## 8. Карантин: новые поля

| Поле | Тип |
|---|---|
| `depthBuffer` | `ID3D12Resource*` |
| `dsvHeap` | `ID3D12DescriptorHeap*` |
| `constantBuffers[kQuarantineFrameCount]` | `ID3D12Resource*` |
| `readbackBuffer` | `ID3D12Resource*` (тестовый режим) |

Передача через `Detach()`, счётчики ссылок не меняются. CPU-указатели
`mapped` обнуляются в момент передачи (§3.3). `mappedReadback` обнуляется
там же — readback-буфер тоже `Map`ится, и запись в него после передачи
владения была бы ошибкой того же класса.

Политика карантина (smoke-only, single-instance) не меняется: для игрового
renderer'а удержание видеопамяти до перезапуска недопустимо, и там нужно
решение на уровне движка — fail-stop с завершением процесса либо полное
пересоздание устройства.

---

## 9. Критерии приёмки

| # | Критерий | Как проверяется |
|---|---|---|
| 1 | Depth создан, `ClearDepthStencilView` вызывается каждый кадр, `DSVFormat` в PSO совпадает с форматом ресурса | Лог + GBV |
| 2 | Чистая `proj` совпадает с эталоном §6.5, композиция `view * proj` совпадает с независимо посчитанным значением | Детерминированные CPU-проверки, допуски по §6.4 |
| 3 | Root CBV задан и адрес выровнен по 256 | GBV: 0 ERROR / CORRUPTION |
| 4 | Depth-тест работает **независимо от порядка отрисовки** | Автоматический readback-тест (§5), три утверждения, exit code |
| 5 | `-depthtest` возвращает 0 на корректном рендере и **ненулевой код** при искусственно сломанном depth-тесте | Прогон с намеренно отключённым `DepthEnable` |
| 6 | Readback корректен при resize: `RowPitch` пересчитан, утверждения не сдвинулись | Прогон `-depthtest` после изменения размера окна |
| 7 | Орбитальная камера вращается, перспектива визуально корректна | Визуально, пользователь |
| 8 | Фиксированный ракурс воспроизводим между прогонами | Повторный прогон даёт тот же readback |
| 9 | Resize: depth пересоздан, aspect пересчитан, 0 validation ERROR/WARNING | Стресс resize |
| 10 | Минимизация / восстановление без ошибок | Лог пауз + отсутствие validation errors |
| 11 | Shutdown: `GPU drained`, новых live objects в `ReportLiveObjects` нет | DebugView-захват |
| 12 | Локальная сборка ReleaseDX12: 0 ошибок, 0 предупреждений в DX12-слоях | MSBuild |
| 13 | DX11 `Release\|x64` не тронут | `git status` по DX11-каталогам пуст |
| 14 | CI: `dx12-smoke-build` success на новом коммите | GitHub Actions run |

Критерий 5 — проверка самого теста. Depth-тест, который не умеет падать, не
является тестом. Порядок отрисовки (ближний первым) выбран именно для того,
чтобы сломанный `DepthEnable` давал наблюдаемую ошибку, а не проходил
молча.

**Что остаётся NOT TESTED и почему.** Реальный device removal: путь не
исполнялся, провокация TDR запрещена. Аварийный карантин на живом GPU: для
достижения `GpuLifetime::Unknown` нужен TDR; проверяется только модельно.
Post-cleanup leak audit: `ReportLiveObjects` вызывается до освобождения
`m_device` и `m_fence`, поэтому их окончательное состояние не проверяется.

---

## 10. Последовательность реализации

| # | Шаг |
|---|---|
| 1 | `dx12camera.{h,cpp}` — DirectXMath, `view * proj`, `XMMatrixTranspose` при записи. Без COM-объектов. Интерфейс: `BuildViewProjection(out float4x4)`, `bool SetAspect(float)`, `bool SetLookAt(eye, target, up)` с полной валидацией (§6.6), `GetFixedPose()`. Детерминированные CPU-проверки: отдельный эталон чистой `proj` по §6.5 и отдельная проверка композиции `view * proj`, допуски по §6.4 |
| 2 | `CHW12.h`: `FrameResource.constantBuffer` + `mapped`; `CreateDepthBuffer`, `m_depthBuffer`, `m_dsvHeap`, `m_dsv`; `CreateReadback`, `m_readbackBuffer`, `mappedReadback`, `m_copyableFootprint`, `m_readbackTotalBytes`; `SmokeCamera12 m_camera`; `Stage::DepthBuffer` |
| 3 | `CHW12::CreateFrameResources` — создание CB на слот с `Map` |
| 4 | `CHW12::CreateDepthBuffer` + DSV; вызов в `Initialize` после `CreateRenderTargetViews` |
| 5 | `CHW12::CreateReadback` — `GetCopyableFootprints`, проверка допустимости, сохранение footprint, буфер `Width = totalBytes`. **Без CPU Map**. Только в тестовом режиме |
| 6 | Переименование `dx12triangle` → `dx12mesh`: один VB на 72 вершины с мировыми координатами и COLOR, root signature с CBV, PSO с depth, `Draw(cmd, cbVA, firstVertex, vertexCount)` с валидацией диапазона |
| 7 | `RenderFrame` — строго по порядку §4.6: ожидание fence слота → `Reset` → барьер → clear RT → clear depth → расчёт `ViewProjection` → одна запись в CB → два `Draw` с общим адресом |
| 8 | `Resize` — порядок §5.6, включая пересоздание readback и `SetAspect` |
| 9 | Карантин: новые поля + обнуление `mapped` и `mappedReadback` |
| 10 | Тестовый режим `-depthtest`: барьеры backbuffer, `CopyTextureRegion` с корректными типами, `WaitForFenceValue` → `Map` → чтение по `Offset + RowPitch` → `Unmap`, три утверждения, exit code |
| 11 | Правка `xrRender_R5_DX12.vcxproj`: include DirectXMath, файлы `dx12camera`, `dx12mesh`; `xrDX12Smoke.vcxproj`: заголовки |
| 12 | Орбита по умолчанию, `-fixedpose`, `-depthtest`. Разбор аргументов в `WinMain` без новых зависимостей |
| 13 | Сборка (lib + smoke + ReleaseDX12), затем CPU-тесты, затем GPU-прогон, затем leak audit, затем DX11-регрессия, затем запрос на коммит |

---

## 11. Риски

1. **CPU-проверки математики — единственная защита** от неверного
   transpose/порядка матриц, который невидим для validation. Без них критерий
   «проекция корректна» непроверяем.
2. **Эталон §6.5 относится к чистой `proj`, не к `ViewProjection`.** Сравнение
   композиции с этой таблицей провалится на корректной реализации. Тесты должны
   быть раздельными.
3. **`RowPitch != width * 4`** при ширине, не кратной 64. Чтение по
   `width * 4` даёт ложноотрицательный тест: рендер верен, assertions сдвинуты.
4. **Перезапись CB между draw call** — источник артефакта, невидимого для GBV.
   Устранён выбором варианта A (§2.3), но требует, чтобы `Draw` принимал
   готовый адрес и диапазон вершин и не писал в буфер сам.
5. **`Type` в `D3D12_TEXTURE_COPY_LOCATION` и поле union должны согласовываться.**
   Несовпадение — не ошибка компиляции, а UB: union не типизирован по
   discriminant. Зафиксировано в §5.3 с точными именами enum.
6. **`Map` без `Unmap`** — штатный режим для upload CB, должен быть записан
   как инвариант. Readback: `Map` только после fence completion в
   `VerifyDepthTest`, `Unmap` сразу после чтения через RAII-гвард.
7. **Расхождение `DSVFormat` в PSO и ресурсе** ловится только при запущенном
   Debug Layer.
8. **Рост лога** на стресс-resize: depth и readback добавляют строки на ресайз.
9. **`assert` в DirectXMath отключён в Release**, поэтому валидация аргументов
   камеры обязательна до вызова (§6.6), а не после.

---

## 12. Исполнительный промпт Hermes

Ниже — самодостаточный промпт. Копируется целиком.

---

```
Реализуй этап F3.1 проекта OGSR-Engine RTX: Depth Buffer, MVP Transformations
& 3D Camera. Техническое задание — docs/dx12-f3-1-depth-mvp-camera.md в том же
репозитории. Прочитай его ПЕРВЫМ ДЕЛОМ и целиком, затем CHW12.{h,cpp},
dx12triangle.{h,cpp}, dx12quarantine.{h,cpp}, оба .vcxproj и раздел F3.0 в
docs/dx12-f2-first-frame.md. Все номера строк в ТЗ проверены против кода на
HEAD 7ae6958d7.

Репозиторий: D:\Coding Project\OGSR ENGINE fork from rtx, ветка prypiat,
HEAD 7ae6958d7. GPU: NVIDIA RTX 5070 Laptop.

ЖЁСТКИЕ ЗАПРЕТЫ:
- Не изменяй AGENTS.md, не добавляй его в коммиты.
- Не выполняй commit и push — только сборка и тесты.
- Не трогай ни один файл DX11-рендерера (xrRenderPC_R4, xrRender, xrRenderDX10,
  xrRenderPC_R1, xr_3DA).
- Не провоцируй TDR, hang или принудительное удаление устройства.
- Не запускай xrDX12Smoke.exe без отдельного разрешения — сначала собери и
  доложи, что собрано.
- Не изобретай допущения там, где можно свериться с документацией D3D12.
- Не добавляй абстракций, кэшей PSO, менеджеров ресурсов — это bootstrap.

=== ЗАДАЧА 1: dx12camera.{h,cpp} ===

Создай ogsr_engine/Layers/xrRenderDX12/dx12camera.{h,cpp}.

- DirectXMath, row-major. ViewProjection = view * proj (XMMatrixMultiply).
- WORLD-МАТРИЦЫ НЕТ. Мировые координаты запечены в вершины. В CB уходит
  ViewProjection, не WVP. Это не упрощение, а требование: см. §2.3 ТЗ.
- view = XMMatrixLookAtLH(eye, target, up),
  proj = XMMatrixPerspectiveFovLH(60°, aspect, 0.1f, 100.0f).
- Запись в CB: XMStoreFloat4x4(&dst, XMMatrixTranspose(viewProj)).
- Интерфейс: BuildViewProjection(float4x4& out), bool SetAspect(float),
  bool SetLookAt(eye, target, up), GetFixedPose().
- Ни одного COM-объекта в этом файле.

SetLookAt ВАЛИДИРУЕТ аргументы ДО вызова DirectXMath, полный набор отказов
(§6.6 ТЗ). НЕ полагайся на NaN от библиотеки: assert отключён в Release,
поведение не специфицировано. Отказ = return false, предыдущая матрица
СОХРАНЯЕТСЯ.

  1. NaN или Inf в любой координате eye/target/up.
     Проверка через XMVector3Dot(v, v) и сравнение значения само с собой:
     NaN не равен себе. Inf ловится сравнением с numeric_limits<float>::infinity().
  2. |target - eye|² <= 1e-12  (eye == target)
  3. |up|² <= 1e-12            (нулевой up)
  4. |dot(normalize(dir), normalize(up))| >= 1.0 - 1e-6
     (forward и up параллельны ИЛИ практически параллельны)
     Порог 1e-6 по |cos|, НЕ 1e-12: при приближении к 1 ортогонализация up
     через Gram-Schmidt теряет точность как 1/sin(angle), и базис содержит
     мусор. 1e-6f ≈ 0.06°.

SetAspect отклоняет aspect <= 0, NaN и Inf. Форма проверки:
  if (!(aspect > 0.0f) || !std::isfinite(aspect)) return false;
Конструкция !(aspect > 0.0f) намеренная: одним выражением отсекает и
aspect <= 0, и NaN. aspect НЕ обрезается до минимума, а отклоняется.

Детерминированные CPU-проверки (без GPU), допуски по §6.4 ТЗ:
комбинированный absEps=1e-5 ИЛИ relEps=1e-4 от максимума модулей.

  а) Чистая proj, отдельно, против эталона §6.5 ТЗ:
     _11=0.9742786 _22=1.7320508 _33=1.0010010 _34=1.0
     _43=-0.1001001 _44=0.0, остальные 0.
     ВАЖНО: эталон относится к ЧИСТОЙ проекции. НЕ сравнивай с ним
     ViewProjection — такой тест провалится на корректном коде.
  б) Композиция view * proj — против независимо посчитанного эталона
     (посчитай вручную для фиксированного pose и зафиксируй константы).
  в) Каждый отказ SetLookAt из списка 1-4 возвращает false И НЕ меняет
     ранее установленную матрицу (сравни с снимком до вызова).
  г) Каждый отказ SetAspect: aspect = 0, -1, NaN.
  д) Идентичность: ViewProjection = Identity * proj == proj поэлементно.

=== ЗАДАЧА 2: CHW12.h ===

- FrameResource: добавь ComPtr<ID3D12Resource> constantBuffer и void* mapped.
- Добавь SmokeCamera12 m_camera (значение-член, как m_triangle сейчас).
- Добавь m_depthBuffer, m_dsvHeap, D3D12_CPU_DESCRIPTOR_HANDLE m_dsv,
  m_dsvDescriptorSize.
- Добавь m_readbackBuffer, void* mappedReadback, UINT64 m_readbackTotalBytes,
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_copyableFootprint (только для теста).
- Добавь Stage::DepthBuffer.

=== ЗАДАЧА 3: per-frame CB ===

В CHW12::CreateFrameResources на каждый слот: CB 256 байт, upload heap,
GENERIC_READ, pOptimizedClearValue = nullptr (требование API для
DIMENSION_BUFFER). Map один раз, Unmap НЕ вызывается — это инвариант,
зафиксируй его комментарием.

=== ЗАДАЧА 4: depth buffer ===

Новый CHW12::CreateDepthBuffer: D32_FLOAT, DEFAULT heap,
ALLOW_DEPTH_STENCIL, InitialResourceState = DEPTH_WRITE,
pOptimizedClearValue = {D32_FLOAT, {1.0f, 0}} — это ОПТИМИЗАЦИЯ очистки,
НЕ требование API (§2.1 ТЗ, параметр [in, optional]). DSV heap с одним
дескриптором, FLAG_NONE. Вызов в Initialize ПОСЛЕ CreateRenderTargetViews.

Барьеров на depth не добавляй: ресурс остаётся в DEPTH_WRITE весь кадр.

=== ЗАДАЧА 5: readback ===

Новый CHW12::CreateReadback (вызывается только в тестовом режиме):

  D3D12_RESOURCE_DESC bbDesc = <описание backbuffer>;
  UINT   numRows = 0;  UINT64 rowSizeInBytes = 0;  UINT64 totalBytes = 0;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
  device->GetCopyableFootprints(&bbDesc, 0, 1, 0,
                                &layout, &numRows, &rowSizeInBytes, &totalBytes);

ДОПУСТИМОСТЬ РЕЗУЛЬТАТА ПРОВЕРЯЕТСЯ, а не предполагается: отказ при
numRows == 0, totalBytes == 0, layout.Footprint.RowPitch == 0 или
totalBytes < numRows * RowPitch. При провале — запись в лог и return false.

Сохранить layout в m_copyableFootprint, totalBytes в m_readbackTotalBytes.
Буфер: D3D12_HEAP_TYPE_READBACK, DIMENSION_BUFFER, Width = totalBytes
(именно pTotalBytes, НЕ RowPitch * numRows), D3D12_RESOURCE_STATE_COPY_DEST,
pOptimizedClearValue = nullptr.

COPYRESOURCE ЗДЕСЬ НЕПРИМЕНИМ: backbuffer — Texture2D, readback — Buffer,
CopyResource требует одинакового типа ресурсов. Блокер §2.2 ТЗ.

=== ЗАДАЧА 6: dx12mesh (переименование) ===

Переименуй dx12triangle.{h,cpp} в dx12mesh.{h,cpp}, класс SmokeTriangle12
в SmokeMesh12. БЕЗ рефакторинга сверх необходимого: сохрани структуру
Create/OnUploadComplete/Draw/Quarantine/Release, выгрузку вершин через upload
heap, состояния ресурсов и ВСЕ комментарии про COMMON vs COPY_DEST.

Изменения:
- ОДИН vertex buffer на 72 вершины (два куба по 36). Раскладка:
  вершины [0, 36) — ближний куб около origin, [36, 72) — дальний, сдвинут
  по Z так, чтобы перекрываться на экране. Мировые координаты прямо в вершинах.
- Вершина: POSITION (R32G32B32_FLOAT) + COLOR (R32G32B32A32_FLOAT),
  каждая грань своим цветом.
- Root signature: один root CBV, register 0, ShaderVisibility_ALL, флаг
  ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT СОХРАНЯЕТСЯ (без него IASetVertexBuffers
  не проходит validation).
- PSO: DepthEnable=TRUE, DepthFunc=LESS, DepthWriteMask=ALL,
  StencilEnable=FALSE, DSVFormat=D32_FLOAT, CullMode=NONE (сохраняется).
- Сигнатура Draw (§4.4a ТЗ):
    void Draw(ID3D12GraphicsCommandList* cmd,
              D3D12_GPU_VIRTUAL_ADDRESS cbVA,
              UINT firstVertex,
              UINT vertexCount);
  Внутри: SetGraphicsRootSignature, SetPipelineState, IASetPrimitiveTopology,
  IASetVertexBuffers (один VBV на весь буфер), SetGraphicsRootConstantBufferView,
  затем DrawInstanced(vertexCount, 1, firstVertex, 0).
  Draw НЕ пишет в CB — только читает переданный адрес.
  ВАЛИДАЦИЯ ДИАПАЗОНА ОБЯЗАТЕЛЬНА: при firstVertex + vertexCount > 72 —
  запись в лог и выход без рисования. Выход за пределы буфера = GPU page fault.
- Шейдер: строки, D3DCompile, mul(float4(input.position, 1.0f), ViewProjection)
  — вектор СЛЕВА. Комбинация «transpose на CPU + mul(matrix, vector)»
  ЗАПРЕЩЕНА, она даёт искажение при нулевых validation errors (§6.3 ТЗ).

=== ЗАДАЧА 7: RenderFrame ===

Строго по порядку §4.6 ТЗ. Ожидание fence слота и Reset аллокатора/листа
идут ПЕРВЫМИ, ДО любого ResourceBarrier:

  1. проверка m_initialized / m_deviceRemoved
  2. frame = m_frameResources[m_frameIndex]
  3. ОЖИДАНИЕ frame.fenceValue (существующий код CHW12.cpp:742-752)
  4. commandAllocator->Reset()
  5. commandList->Reset(allocator, nullptr)
  6. барьер RTV PRESENT -> RENDER_TARGET
  7. OMSetRenderTargets(1, &rtvHandle, FALSE, &m_dsv)
  8. viewport/scissor из m_width/m_height
  9. ClearRenderTargetView
 10. ClearDepthStencilView(m_dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr)
 11. m_camera.BuildViewProjection(out)
 12. memcpy в frame.mapped — ЕДИНСТВЕННАЯ запись CB за кадр
 13. ДВА вызова с ОБЩИМ адресом CB:
       m_mesh.Draw(cmd, frame.constantBuffer->GetGPUVirtualAddress(), 0,  36);
       m_mesh.Draw(cmd, frame.constantBuffer->GetGPUVirtualAddress(), 36, 36);
 14. барьер RTV RENDER_TARGET -> PRESENT
 15. Close / ExecuteCommandLists / Present(1,0) / fenceValue = ++m_fenceValue / Signal
 16. MoveToNextFrame / DrainInfoQueue

Между шагами 13a и 13b память CB НЕ перезаписывается ни при каких условиях.
Ближний куб (0,36) рисуется ПЕРВЫМ, дальний (36,36) ПОСЛЕДНИМ.

=== ЗАДАЧА 8: Resize ===

Порядок §5.6 ТЗ, дословно:
  WaitForGpu → сброс per-frame fenceValue → освобождение lists/allocators/
  renderTargets → освобождение depth, DSV heap и readback → m_rtvHeap.Reset()
  → ResizeBuffers → CreateRenderTargetViews → CreateDepthBuffer →
  CreateReadback (ПЕРЕСЧЁТ footprint и totalBytes!) →
  CreateFrameResources (CB, Map заново) → m_camera.SetAspect(w, h)

Старые RowPitch, Offset и totalBytes после resize НЕДЕЙСТВИТЕЛЬНЫ: они
описывали прежний backbuffer. SetAspect обязателен: иначе куб растянется, и
это тихая регрессия без validation error.

=== ЗАДАЧА 9: карантин ===

dx12quarantine.h: добавь depthBuffer, dsvHeap, constantBuffers[3],
readbackBuffer. В QuarantineGpuResources — Detach для всех, плюс ОБНУЛЕНИЕ
всех mapped и mappedReadback указателей в момент передачи. В Shutdown при
штатных состояниях: mapped = nullptr и mappedReadback = nullptr ДО
соответствующих Reset().

=== ЗАДАЧА 10: -depthtest ===

Флаг -depthtest: фиксированный ракурс, readback, три утверждения §5.4 ТЗ,
exit code 0/1. Порядок в кадре ПОСЛЕ обоих Draw и ДО барьера в PRESENT:
  барьер backbuffer RENDER_TARGET -> COPY_SOURCE

  D3D12_TEXTURE_COPY_LOCATION dst = {};
  dst.pResource       = m_readbackBuffer.Get();
  dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = m_copyableFootprint;

  D3D12_TEXTURE_COPY_LOCATION src = {};
  src.pResource        = frame.renderTarget.Get();
  src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = 0;

  cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  барьер обратно COPY_SOURCE -> PRESENT
ЗАТЕМ WaitForGpu() и только ПОТОМ Map.

D3D12_TEXTURE_COPY_TYPE имеет РОВНО ДВА значения (d3d12.h:3329-3333, SDK
10.0.26100.0): D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX = 0 и
D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT = 1. Значения SUBRESOURCE_FOOTPRINT
НЕ СУЩЕСТВУЕТ — код не скомпилируется. Type и поле union ОБЯЗАНЫ
согласовываться: при PLACED_FOOTPRINT читается PlacedFootprint, при
SUBRESOURCE_INDEX — SubresourceIndex. Несовпадение = UB, не ошибка компиляции.

Чтение пикселей:
  base + m_copyableFootprint.PlacedFootprint.Offset
      + y * m_copyableFootprint.Footprint.RowPitch + x * 4
Offset входит в формулу явно. НЕ width * 4 вместо RowPitch: RowPitch
выровнен по 256 байт, и на ширине не кратной 64 в конце строки padding —
чтение по width*4 сдвинет всю картинку со второй строки.

Три утверждения:
  1. центр перекрытия == цвет ближнего куба
  2. область ТОЛЬКО дальнего куба == цвет дальнего (контроль достоверности:
     без него тест проходит и при невидимом дальнем кубе)
  3. фон == цвет clear
Сравнение uint8 побайтово, без допуска. При несовпадении — координаты,
ожидаемый и фактический байты в лог + ненулевой exit code.

=== ЗАДАЧА 11: проекты ===

xrRender_R5_DX12.vcxproj: добавь
$(SolutionDir)3rd_party\Src\DirectXMath\DirectXMath\Inc в
AdditionalIncludeDirectories; dx12camera.cpp и dx12mesh.cpp в ClCompile;
dx12camera.h и dx12mesh.h в ClInclude.
xrDX12Smoke.vcxproj: dx12camera.h и dx12mesh.h в ClInclude.
Ничего больше в .vcxproj не меняй.

=== ЗАДАЧА 12: режимы ===

Орбита по умолчанию (yaw линейно по QueryPerformanceCounter, pitch фиксирован,
мировые координаты кубов не меняются), -fixedpose, -depthtest. Разбор
аргументов в WinMain без новых зависимостей.

=== СБОРКА (выполни и покажи вывод) ===

  msbuild xrRender_R5_DX12.vcxproj /t:Rebuild /p:Configuration=ReleaseDX12 /p:Platform=x64
  msbuild xrDX12Smoke.vcxproj /t:Rebuild /p:Configuration=ReleaseDX12 /p:Platform=x64
  msbuild Engine.sln /t:Rebuild /p:Configuration=ReleaseDX12 /p:Platform=x64

НЕ ЗАПУСКАЙ xrDX12Smoke.exe. GPU-прогон — только после отдельного разрешения.

=== ТРЕБОВАНИЯ К КАЧЕСТВУ ===

- Комментарии на русском, в стиле существующего кода: каждый неочевидный
  выбор обоснован, особенно там, где поведение неочевидно из API.
- Каждый HRESULT проверяется. Ни одного молчаливо отброшенного результата.
- 0 ошибок и 0 предупреждений в DX12-слоях.

=== ФОРМА ОТЧЁТА ===

1. Exit code каждой из трёх сборок.
2. Число ошибок и предупреждений именно в DX12-слоях (отдели от 3rd_party).
3. Список созданных и изменённых файлов.
4. ЯВНЫЙ перечень всего, что осталось непроверенным.

Не утверждай, что что-либо работает на GPU, если exe не запускался. Отличай
mock-проверки состояний от реальных GPU-прогонов. Если какое-либо требование
ТЗ противоречит документации D3D12 или твоей проверке кода — ОСТАНОВИСЬ и
доложи противоречие, а не угадывай.
```

---

## 13. Переносимость на IX-Ray

| Решение F3.1 | Вклад в переносимость |
|---|---|
| `dx12camera` без COM и без OGSR-типов, на DirectXMath | Единственный компонент, который адаптер IX-Ray возьмёт как есть. `Fmatrix` здесь был бы прямой связью с `xrCore`, которую пришлось бы выдирать |
| DX12-библиотека остаётся без ProjectReference на движковые проекты | Граница Core/движок остаётся физической, а не договорённости. Проверяемо `git diff` по `.vcxproj` |
| Root CBV вместо descriptor heap | Интерфейс Core не привязан к схеме дескрипторов, которую `xrRHI` у IX-Ray может иметь свою |
| Формат depth как константа, а не наследство OGSR | Core не зашивает выбор чужого рендерера |
| `FrameResource` с CB на слот | Модель синхронизации, переносимая на любого потребителя, дающего кадры |
| Карантин остаётся smoke-only и явно помечен | При переносе на движковый цикл политика заменяется осознанно, а не всплывает как скрытое допущение |

IX-Ray 1.6 — CMake, `src/xrRHI` с зависимостями от D3D9/D3D11 и собственной
лицензией. Поэтому при переносе Core должен поставляться как самостоятельный
модуль со своим интерфейсом на простых типах (`uint32 w/h`, `float4x4`), а
`xrRHI` подключаться адаптером, а не наоборот. F3.1 оставляет этот шов
открытым.

---

## 14. Вне объёма F3.1

Осознанно исключено: CBV_SRV_UAV heap (root CBV его не требует), индексный
буфер (F3.2), текстуры и SRV, stencil, reversed-Z, ввод с клавиатуры/мыши,
несколько PSO, кэш PSO, параллельные command lists, второй compute queue,
чтение глубины шейдером, runtime-переключатель DX11/DX12, любые правки DX11 и
`AGENTS.md`.
