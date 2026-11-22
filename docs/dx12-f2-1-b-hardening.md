# F2.1-B — D3D12 Bootstrap Hardening (retrofit)

**Дата:** 2026-10-20
**Ветка:** `prypiat`
**Фактический HEAD:** `b52326224` — `feat(dx12): add sequential multi-list recording foundation`
**Baseline из задания:** `64b1b81c5`
**Режим работы:** изолированный worktree, без commit/push, runtime запрещён

---

## 1. Резюме

Задание F2.1-B описывало девять дефектов bootstrap-реализации `CHW12` и
`xrDX12Smoke`, относящихся к состоянию `64b1b81c5`. К моменту выполнения
репозиторий ушёл вперёд: HEAD — `b52326224` (F3.5.0, +8 коммитов к
`origin/prypiat`), а разделы F2.1-B.1 и F2.1-B.2 в
`docs/dx12-f2-first-frame.md` фиксируют, что эти дефекты уже закрыты, включая
подтверждённый GPU-прогон первого кадра.

Единственным сохранившимся дефектом оставался пункт 9 задания —
неорганизованные build output/intermediate directories при прямом вызове
`msbuild` на `.vcxproj`. Он и исправлен в этой работе.

| Пункт задания | Фактическое состояние |
|---|---|
| 1. Неполная обработка HRESULT | Уже реализовано в HEAD |
| 2. Небезопасная fence/GPU-синхронизация | Уже реализовано |
| 3. Новый command list каждый кадр | Уже переиспользуются per-frame lists |
| 4. Отсутствует `WM_SIZE` | Уже реализовано |
| 5. Игнорируется результат `RenderFrame()` | Уже обрабатывается |
| 6. Нет cleanup после частичной инициализации | Уже реализовано |
| 7. Отсутствует Debug Layer | Уже реализовано |
| 8. Дублирующийся `ProjectReference` | Дубликата нет |
| 9. Неупорядоченные build directories | **Исправлено в этой работе** |

**Статус этапа: PARTIAL.** Код исправлен, собран и слинкован; полный
solution rebuild в изолированном worktree невозможен из-за отсутствия
vendored-исходников `3rd_party`, которые не отслеживаются git. GPU runtime
именно этой правки не выполнялся: **NOT RUN**. Исторические runtime-прогоны
F2.1, записанные в `docs/dx12-f2-first-frame.md`, сохраняются и не
переписываются.

---

## 2. PRE-FLIGHT

### 2.1. Состояние репозитория

```
branch : prypiat
HEAD   : b52326224  feat(dx12): add sequential multi-list recording foundation
origin : 86b6439bd  (ahead 8)
```

В основной рабочей копии находится незакоммиченная работа F3.5.3b:
`CHW12.cpp/.h`, `dx12mesh.cpp/.h`, `dx12quarantine.h`, `xrDX12Smoke.cpp`,
`tests/run-wmtimer.ps1`, а также шесть untracked файлов (аналитика и тестовые
скрипты). **Она не изменялась.**

### 2.2. Изоляция

Правки выполнялись в отдельном worktree:

```
C:\Users\GR\AppData\Local\Temp\opencode\f21b-soldir
HEAD: b52326224 (detached)
```

Основное дерево осталось нетронутым. Патч для применения:
`C:\Users\GR\AppData\Local\Temp\opencode\f21b-soldir.patch`
(`git apply --check` — exit 0).

---

## 3. Аудит девяти дефектов

Проверка велась по фактическим исходникам HEAD, без восстановления старых
версий из git.

### 3.1. HRESULT и отказ устройства

В `CHW12` проверяются результаты:

- `CreateDXGIFactory1`;
- `EnumAdapterByGpuPreference`, `GetDesc1`;
- `D3D12CreateDevice`, `CreateCommandQueue`;
- `CreateSwapChainForHwnd`, `QueryInterface` на `IDXGISwapChain4`;
- `GetBuffer`, `CreateDescriptorHeap`, `CreateRenderTargetView` (void);
- `CreateCommandAllocator`, `CreateCommandList`, `Close`;
- `allocator->Reset`, `list->Reset`;
- `CreateFence`, `CreateEvent`, `SetEventOnCompletion`, `Signal`;
- `Present`, `ResizeBuffers`;
- `CreateCommittedResource`, `Map`/`Unmap`;
- `GetTimestampFrequency`.

`ExecuteCommandLists` возвращает `void`; корректность подтверждается Debug
Layer, `DrainInfoQueue()` и `GetDeviceRemovedReason()`, а не фиктивной
проверкой HRESULT.

При критическом отказе `RenderFrame()` возвращает `false` и кадр не
продолжается.

### 3.2. Синхронизация fence

- Три frame resources, у каждого собственное значение fence.
- Перед повторным использованием allocator ожидается fence именно этого слота.
- `WaitForFenceValue()`: проверка `GetCompletedValue()` до и после ожидания,
  `SetEventOnCompletion`, `WaitForSingleObject` с таймаутом `GPU_WAIT_TIMEOUT_MS`
  (5000 мс).
- `WAIT_TIMEOUT` и `WAIT_FAILED` не трактуются как завершение GPU; таймаут
  отличается от отказа API.
- `GetCompletedValue() == UINT64_MAX` трактуется как device removal.
- `WaitForGpu()` сигналит новое значение fence и ждёт именно его; при провале
  allocator не переиспользуется.

### 3.3. Переиспользуемый command list

Один graphics queue. На каждый слот кадра при инициализации создаются
allocator и list; list сразу закрывается. В кадре выполняется
`Reset` allocator → `Reset` list → запись команд → `Close` →
`ExecuteCommandLists`. Новых command list в `RenderFrame()` не создаётся.

### 3.4. Win32 lifecycle

- `WM_SIZE`: `SIZE_MINIMIZED` останавливает рендер; ненулевые размеры
  откладываются.
- `WM_ENTERSIZEMOVE` / `WM_EXITSIZEMOVE`: промежуточные размеры накапливаются,
  один `Resize()` выполняется на выходе.
- `g_resizeInProgress` защищает от реентерабельности.
- `RenderFrame() == false` завершает цикл, вызывает `Shutdown()` и возвращает
  ненулевой код; в `WM_TIMER`-пути сбой латчится и закрытие идёт через
  `WM_CLOSE`.
- Начальный размер клиентской области вычисляется через
  `AdjustWindowRectEx`; окно изменяемого размера.

### 3.5. Частичная инициализация и Shutdown

- `Stage` фиксирует достигнутую стадию.
- `Shutdown()` идемпотентен через `m_shutdownComplete`, безопасен после
  частично неуспешного `Initialize`, закрывает `HANDLE` ровно один раз,
  не освобождает ресурсы без подтверждённой синхронизации.
- При неизвестном состоянии GPU ресурсы передаются process-lifetime
  карантину, выставляется `m_terminalGpuFailure`.

### 3.6. Swapchain resize

Порядок: `WaitForGpu` → сброс per-frame fence tracking → освобождение list,
allocator, CB, RTV-heap, depth/DSV, readback → `ResizeBuffers` → обновление
`m_width`/`m_height` и индекса backbuffer → пересоздание RTV → frame
resources → depth → readback → `SetAspect`. Провал любого шага останавливает
рендеринг с диагностикой.

### 3.7. Debug Layer

- `D3D12GetDebugInterface` и `ID3D12Debug::EnableDebugLayer` до
  `D3D12CreateDevice`;
- опционально `ID3D12Debug1::SetEnableGPUBasedValidation`;
- DRED: auto-breadcrumbs и page faults;
- `ID3D12InfoQueue` и `DrainInfoQueue()` с различением
  `CORRUPTION`/`ERROR`/`WARNING`/`INFO`;
- `ReportGpuFailure()` печатает `GetDeviceRemovedReason()` и DRED page-fault VA;
- отсутствие Graphics Tools не является фатальным — факт пишется в лог.

### 3.8. ProjectReference

- `xrRender_R5_DX12.vcxproj`: одна ссылка (`JobSystem`).
- `xrDX12Smoke.vcxproj`: две ссылки — `xrRender_R5_DX12` и `JobSystem`;
  дублирования нет.

---

## 4. Исправленный дефект: `$(SolutionDir)` при прямом билде

### 4.1. Симптом

```
msbuild ogsr_engine\Layers\xrRenderPC_R5_DX12\xrRender_R5_DX12.vcxproj \
        /t:Rebuild /p:Configuration=ReleaseDX12 /p:Platform=x64
```

собирал библиотеку в

```
...\ogsr_engine\Layers\xrRenderPC_R5_DX12\ogsr_engine\_LIB\Engine\ReleaseDX12\x64\
```

а smoke — в `...\ogsr_engine\Layers\xrRenderDX12\x64\ReleaseDX12\`.

### 4.2. Причина

`Engine_lib_properties.props` вычисляет `OutDir`/`IntDir` от `$(SolutionDir)`.
При сборке через `Engine.sln` MSBuild задаёт `SolutionDir` = корень
репозитория. При прямом вызове на `.vcxproj` `Microsoft.Cpp.Default.props`
подставляет `SolutionDir` = каталог проекта, поэтому относительные пути
схлопывались внутрь дерева исходников.

Ключевая деталь: `SolutionDir` в этом режиме **не пуст** и не равен
`*Undefined*`. Поэтому условие `'$(SolutionDir)' == ''` не срабатывает, и
наивный fallback бесполезен. Признак прямого запуска нужно снимать **до**
импорта `Microsoft.Cpp.Default.props`.

### 4.3. Изменение

В обоих DX12-проектах до импорта `Microsoft.Cpp.Default.props` добавлена проба:

```xml
<PropertyGroup Label="DirectBuildProbe">
  <_DirectBuildSolutionDir Condition="'$(SolutionDir)' == ''">1</_DirectBuildSolutionDir>
</PropertyGroup>
```

После импорта свойств — fallback:

```xml
<PropertyGroup Label="DirectBuildSolutionDir" Condition="'$(_DirectBuildSolutionDir)' == '1'">
  <SolutionDir>$([System.IO.Path]::GetFullPath('$(MSBuildProjectDirectory)\..\..\..'))\</SolutionDir>
</PropertyGroup>
```

`xrDX12Smoke.vcxproj` дополнительно задаёт пути явно, повторяя значения,
которые MSBuild выводит при сборке через solution:

```xml
<OutDir>$(SolutionDir)$(Platform)\$(Configuration)\</OutDir>
<IntDir>$(ProjectName)\$(Platform)\$(Configuration)\</IntDir>
```

### 4.4. Почему не меняется поведение через `Engine.sln`

При сборке из solution `SolutionDir` задан заранее, проба остаётся пустой,
группы не применяются. Итоговые `OutDir`/`IntDir` и DX11 baseline не
затрагиваются.

### 4.5. Результат

| Способ запуска | Библиотека | Smoke |
|---|---|---|
| Прямой `.vcxproj` | `<root>\ogsr_engine\_LIB\Engine\ReleaseDX12\x64\...lib` | `<root>\x64\ReleaseDX12\xrDX12Smoke.exe` |
| `Engine.sln` | тот же путь | тот же путь |

---

## 5. Изменённые файлы

```
 docs/dx12-f2-first-frame.md                        | 75 ++++++++++++++++++++++
 .../Layers/xrRenderDX12/xrDX12Smoke.vcxproj        | 15 +++++
 .../xrRenderPC_R5_DX12/xrRender_R5_DX12.vcxproj    | 14 ++++
 3 files changed, 104 insertions(+)
```

- `Engine.sln`, `.gitignore` — **без изменений**: нужные mapping и правила
  игнорирования артефактов уже присутствуют.
- Основное дерево — **без изменений**: там остаётся только WIP F3.5.3b.
- `AGENTS.md` не затрагивался.

---

## 6. Проверки сборки

| Проверка | ExitCode | Ошибок | Предупр. | Результат |
|---|---|---|---|---|
| A. Прямой `.vcxproj` rebuild библиотеки | 0 | 0 | 2 | `ogsr_engine\_LIB\Engine\ReleaseDX12\x64\xrRender_R5_DX12.lib` |
| B. Прямой `.vcxproj` rebuild smoke | 0 | 0 | 3 | `x64\ReleaseDX12\xrDX12Smoke.exe` (1 053 184 б) |
| C. `Engine.sln` targeted `xrRender_R5_DX12;xrDX12Smoke` | 0 | 0 | 3 | MSB4121 = 0, пути совпадают с solution-layout |
| C-full. `Engine.sln` `/t:Rebuild` | 1 | 76 | 25 | падение только на отсутствующих vendored-исходниках `3rd_party` |
| D. `Engine.sln` `Release\|x64` (DX11) | 1 | 1 | — | `bin_x64\xrEngine.exe` собран и слинкован; единственная ошибка — известный post-build `dbghelp.dll` |

### 6.1. Ограничение C-full

В изолированном worktree отсутствуют каталоги, исключённые вложенными
`.gitignore` и подготавливаемые `Update_Components.cmd`: `mimalloc`,
`zstd`, `LuaJIT`, `DiscordRPC`, `lz4`, `FidelityFX`, `imgui`, `libvorbis`.
Отсюда 76 ошибок `C1083` в чужих проектах. Оба DX12-проекта в том же
прогоне собрались успешно, MSB4121 равен нулю.

### 6.2. DX11 baseline

`XR_3DA.vcxproj` → `bin_x64\xrEngine.exe` (31 463 424 байт), компиляция и
линковка успешны. Единственная ошибка — `MSB3073` при post-build
копировании `dbghelp.dll`; это известная проблема окружения (Debugging
Tools не установлены), не регрессия.

ATL подключался временным props вне репозитория: Build Tools 14.44 не
содержит ATL, установленный ATL лежит в `14.30.30705`.

---

## 7. Статическая валидация

- `Engine.sln`: MSB4121 = 0; оба DX12-проекта присутствуют и собираются.
- Оба `.vcxproj`: XML валиден (`[xml]` разбор без ошибок).
- Дублирующихся `ProjectReference` нет.
- Артефакты создаются в ожидаемых каталогах и покрыты `.gitignore`.
- Изменений исходников DX11 нет.
- Вложенный каталог `...\xrRenderPC_R5_DX12\ogsr_engine\` в worktree —
  остаток первого прогона со сломанным условием; последующие сборки в него
  не пишут.

---

## 8. Что не проверялось

- `xrDX12Smoke.exe` в рамках этой правки **не запускался**. Runtime, resize,
  minimize/restore и GPU-валидация для изменённой конфигурации сборки не
  выполнялись.
- Debug Layer для изменённой конфигурации не подтверждён выполнением: он
  реализован и компилируется, но статус этой правки — **NOT RUN**.
- GPU-синхронизация этой правкой не затрагивалась и повторно не проверялась.
- Device removal не провоцировался.

Ранее выполненные runtime-прогоны F2.1 (первый кадр, resize, Debug Layer,
depth-тест) описаны в `docs/dx12-f2-first-frame.md` и остаются в силе.

---

## 9. Остаточные риски

1. C-full проверен только частично: полный solution rebuild требует
   `Update_Components.cmd` в worktree. В основном дереве прогон не выполнялся,
   чтобы не смешивать результат с WIP F3.5.3b.
2. `JobSystem` собирается в `Layers\JobSystem\x64\ReleaseDX12` — вне области
   задачи, поведение не менялось.
3. Патч меняет два `.vcxproj`; при будущем коммите их нужно включать отдельно
   от WIP F3.5.3b.
4. `docs/dx12-f2-first-frame.md` — исторический документ; новый раздел
   добавлен в конец, предыдущие результаты не переписаны.

---

## 10. Применение

```cmd
git apply --check C:\Users\GR\AppData\Local\Temp\opencode\f21b-soldir.patch
git apply       C:\Users\GR\AppData\Local\Temp\opencode\f21b-soldir.patch
```

Патч проверен: `git apply --check` — exit 0.

---

## 11. Следующий шаг

F2.1-C уже пройден: первый DX12-кадр, resize, Debug Layer и depth-тест
выполнялись ранее, результаты зафиксированы в
`docs/dx12-f2-first-frame.md`. Эта правка их не переписывает и не отменяет.

Отдельный прогон нужен только для проверки изменённой конфигурации сборки
(`$(SolutionDir)` fallback) — чтобы подтвердить, что пути вывода не влияют
на содержимое исполняемого файла. Критерии такого прогона:

- окно открывается, кадр отображается;
- в логе нет `[DX12][validation]` уровня `ERROR`/`CORRUPTION`;
- resize и minimize/restore не дают validation errors и device removal;
- `GetDeviceRemovedReason()` остаётся `S_OK`.

Запуск выполняется только по отдельному разрешению.

---

**Коммитов и push нет. Runtime-тесты не выполнялись.**
