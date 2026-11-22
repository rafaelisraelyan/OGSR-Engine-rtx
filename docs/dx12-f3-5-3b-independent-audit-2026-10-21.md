# Независимый аудит F3.5.3B — остановка по High H1

Дата: 2026-10-21. Режим: read-only аудит исходников, без исправлений.

## 1. Вердикт и точный объект

**FAIL для исторической F3.5.3B `53b82c5b7` по GPU lifetime.** Найден один
доказанный High-дефект условия завершения fence. Полный аудит остановлен по
прямому требованию пользователя после обнаружения High. Этот документ —
подробный промежуточный отчёт с воспроизводимым блокером, а не полный sign-off.

Фактический HEAD: `5b025aaa97d6ef9aa64ce2a28b9f445583b3a4f9`, ветка `prypiat`,
уже F3.5.6B. F3.5.3B ранее закоммичен как `53b82c5b7`; заявленное исходное
состояние «реализовано без коммита» больше не соответствует Git.
Baseline: `b523262242ac857e8d6c866a72aafb325c54e910`.
Между baseline и HEAD есть также F3.5.4A/B, F3.5.5A/B/C, H1 hardening,
F3.5.6A/B и другие изменения. Их нельзя приписывать реализации F3.5.3B.

**Текущий HEAD: общий вердикт не выдан.** Конкретный H1 уже исправлен
`d38ea6d85a6363739f850c81a9faafe727ba93b0`; это проверено по коду и CPU harness.
Исправление одного дефекта не означает приёмку всех последующих этапов.

Порядок анализа: Git → архитектурный контракт/исходный fence helper → найденный
контрпример → проверка исправления в HEAD → сопоставление с отчётом Hermes.
Предыдущие успешные результаты не использовались как доказательство безопасности.

## 2. Finding H1 — High, доказанный дефект проверки completion

### Точные места

Все следующие номера относятся к **ревизии `53b82c5b7`**, не к нынешнему файлу:

- `ogsr_engine/Layers/xrRenderDX12/CHW12.cpp:937` — WaitForFenceValue.
- `CHW12.cpp:985` — чтение completedAfter после WAIT_OBJECT_0.
- `CHW12.cpp:986–989` — проверяется только removed marker, затем возвращается true;
  отсутствует условие `completedAfter >= value`.
- `CHW12.cpp:1056` — DetermineGpuLifetime превращает этот true в WorkCompleted.
- `CHW12.cpp:1858` — Shutdown принимает WorkCompleted и пишет сообщение о drain.
- `CHW12.cpp:1886` — CollectGpuTiming для pending slots при ложном WorkCompleted.
- `CHW12.cpp:1893–1906` — освобождение GPU resources, включая queries/readback,
  lists/allocators, при lifetime != Unknown.

Для точной локальной навигации сохранён неизменённый Git blob:
`x64/audit-f353b/CHW12-at-53b82c5b7.cpp`.
Сравнение тел WaitForFenceValue в baseline и F3.5.3B дало полное совпадение.
**Дефект унаследован от F3.5.3A, а не внесён parallel recording.** Но F3.5.3B
использует небезопасный helper и расширяет набор зависящих от него ресурсов.

### Сценарий

1. В direct queue есть работа трёх slots со значениями fence 10, 11, 12.
2. При повторном использовании slot с value=10 owner регистрирует event для 10.
3. Ожидание завершается timeout, когда completed ещё 9. Регистрация события
   не становится доказательством завершения и не отменяется этим CPU timeout.
4. Rendering прекращается. Shutdown ставит новый Signal(13) и ждёт value=13,
   регистрируя тот же event handle.
5. GPU доходит до 10; прежняя регистрация сигналит event. Новое ожидание получает
   WAIT_OBJECT_0, хотя completed=10 и GPU ещё может работать над slots 11/12.
6. Исторический код видит completedAfter=10 != UINT64_MAX и возвращает true.
7. Shutdown ошибочно получает WorkCompleted, читает readback и освобождает
   память, для которой completion не подтверждён, вместо quarantine.

### Последствия и уровень доказанности

**Доказано:** функция принимает incomplete fence за success; этот success ведёт
к разрешению чтения/освобождения GPU-ресурсов.
**Возможное следствие:** stale timestamp readback, освобождение allocator/list,
query heap, CB, RTV/depth/mesh ресурсов при незавершённом GPU доступе, GPU UAF,
validation errors, device removal или падение.
**Не утверждается:** что реальный GPU UAF/TDR был воспроизведён. Разрушительный
GPU-прогон не выполнялся.

### Безопасное воспроизведение

Создан CPU-only harness `x64/audit-f353b/stale_wake_repro.cpp`.
Тела исторических CheckFenceForRemoval/WaitForFenceValue извлечены непосредственно
из Git без изменения ветвлений. D3D12/Win32 ответы подставлены stub-объектами:
первый wait timeout, второй signaled; completed sequence 9, 9, 10, targets 10/13.
В том же процессе проверен текущий dx12fence12::WaitForValue из HEAD.

MSVC compilation и запуск завершились exit 0; вывод:

```text
historical first=0 timeout=1 second=1 completed=10 requested=13
current first=Timeout second=SpuriousWake completed=10 requested=13
```

Harness exit 0 означает, что контрпример к старому коду и отказ нового helper
подтверждены. Это НЕ PASS исторической реализации.

### Рекомендуемое исправление / текущее состояние

После любого пробуждения проверять реальное completed value; успех только если
completed >= requested и значение не removed marker. При меньшем значении
возвращать незавершённость либо повторять ожидание в пределах общего deadline;
не разрешать Reset/Map/release. Добавить stub-тест stale event после timeout,
покрывающий решение lifetime, а не только строку лога.

В текущем HEAD такая проверка уже есть:
`ogsr_engine/Layers/xrRenderDX12/dx12fencewait.h:112` (ветка completedAfter < value),
`CHW12.cpp:1818` (использование helper), `CHW12.cpp:1853` (отказ SpuriousWake).
Точный статус: исторический High, исправленный в HEAD. Никаких правок в этом
аудите не внесено.

## 3. Инварианты: что действительно подтверждено

| Инвариант | Статус | Основание / граница |
|---|---|---|
| Успех fence wait требует completed >= requested | НАРУШЕН в F3.5.3B | H1, CPU counterexample |
| Слот reset/readback защищён истинным completion | Не гарантирован историческим helper | Логический вызов Wait недостаточен из-за H1 |
| WorkCompleted разрешает release только после GPU drain | НАРУШЕН в F3.5.3B | Shutdown доверяет false-positive WaitForGpu |
| При неизвестном GPU lifetime используется quarantine | Условно | Ветка есть, но H1 ошибочно обходит её |
| Current helper отклоняет completed=10 при target=13 | Подтверждён CPU-тестом | SpuriousWake; не full-HEAD GPU sign-off |
| First/second GR Submit всегда дренируются | Полное доказательство не завершено | Остановка аудита по High; предыдущий отчёт не считается новым доказательством |
| Exclusive allocator/list и отсутствие CPU data races | Полная независимая проверка не завершена | Нельзя выводить из ASan/GBV |
| Полнота bindings, barriers, depth и deterministic submission | Не выдан новый sign-off | GPU matrix не повторялась после остановки |
| Естественный overlap без rendezvous | Независимо не измерен в этом аудите | Имеются заявления Hermes; не переобозначены как новый результат |
| Нет скрытых stalls и корректны все timing populations | Не проверено полностью | Требуется продолжение профилирования после согласования scope |

## 4. GPU lifetime, races и recovery

H1 — ошибка GPU completion/lifetime, а не доказанная гонка между двумя C++
recording callbacks. Даже полностью корректный JobGroup::Wait завершает только
CPU recording; он не доказывает завершение GPU. Ошибочный WorkCompleted после
этого может разрушить ресурсы, используемые direct queue.

Восстановление после recording failure на штатном GPU и тесты повторного
Initialize не воспроизводят сценарий зависшего/задержанного slot fence с
оставшейся регистрацией event. Они не покрывают автоматическое решение
«release или quarantine» после false wake.

Failed Signal, первый/второй Submit, исключения callbacks, reentrancy из Win32,
конкурентный lifecycle и все shared COM access paths не объявляются полностью
проверенными этим прерванным аудитом. Они остаются в очереди независимой
проверки. Непроверенная ветвь не считается найденным дефектом.

## 5. Достаточность тестов и возможные ложные PASS

В историческом `tests/run-parallel-recording.ps1:15` успех shutdown проверяется
по наличию строки `GPU drained before shutdown`; на `:25` считаются два таких
сообщения. Источник этой строки — тот же ошибочный WorkCompleted
(`CHW12.cpp:1858–1859`). Поэтому эта assertion не является независимым oracle
для GPU completion: H1 мог бы дать ложное подтверждение drain.

На `run-parallel-recording.ps1:47` перечислены faults allocator/reset/record/
close/submit/throw. Stale fence wake не входит в эту матрицу.
Проверка backend ошибок Submit и работа recording callbacks не заменяют
stub-тесты Win32 event/fence state machine.

Из наблюдаемого кода также видно ограничение Submit coverage:
`CHW12.cpp:1456` инъекция привязана к i==1 (второй Submit); разные номера Draw
в CLI не создают отдельный отказ первого Submit. Это зафиксировано как пробел
покрытия, а не доказанный runtime-дефект первого Submit.

Текущий `dx12fencewait_tests.cpp:101–105` уже содержит assertion против stale
wake. В этом аудите независимо исполнен только минимальный helper harness;
весь текущий regression suite не запускался.

## 6. Сопоставление с отчётом Hermes

Сопоставление выполнено после нахождения H1 по исходникам.
В версии отчёта F3.5.3B `53b82c5b7`:

- `docs/dx12-f3-5-3b-parallel-recording.md:160` утверждает отсутствие блокеров.
- `:173–179` обосновывает timestamp/release ожиданием slot fence/полным drain.
- `:271` и `:290` используют сообщения о drain как результаты проверки.

Эти рассуждения предполагают корректность WaitForFenceValue, но не проверяют
его postcondition после wake. H1 опровергает безусловную историческую гарантию
GPU lifetime. Успешные depth/resize/GBV и CPU ASan результаты сами по себе
этому не противоречат: они не моделируют данный failure schedule.

Отчёт честно ограничивает ASan CPU harness и признаёт отсутствие реального
Signal/TDR/quarantine fault testing. Эти оговорки полезны, но не компенсируют
ошибку проверяемого постусловия в исходниках.

## 7. Проведённые / непроведённые проверки

Проведены: фактические Git HEAD/status/history/diff; отделение B от последующих
этапов; чтение AGENTS и релевантных roadmap/ADR контрактов; исторический fence
helper, lifetime call chain, соответствующий код HEAD; чтение релевантных
утверждений отчётов A/B и assertions recording runner; сравнение helper с
baseline; компиляция/запуск безопасного CPU harness старой и текущей логики.

Не проведены повторно: ReleaseDX12 всего приложения, GPU validation, depth во
всех режимах, WM_TIMER, resize, shutdown/reinitialize на GPU, JobSystem suite,
CPU/ASan suite, настоящий overlap, profiler capture. Причина — обнаружен High
и соблюдено требование остановки. Старые цифры из отчёта не выдаются за новые.
TDR/device removal намеренно не запускались.

## 8. Решение и продолжение

Историческую F3.5.3B без исправления H1 нельзя подписать как безопасную основу
для следующего этапа. Фактически она уже закоммичена и F3.5.4 уже реализован;
аудитор не выполнял commit/push/merge/rebase и не менял эту историю.

Для текущего HEAD конкретный H1 устранён, но общий deep audit не завершён.
Нужно согласовать продолжение: полный аудит актуального HEAD с проверкой
H1 и последующих этапов либо продолжение строго исторического F3.5.3B с
отдельным учётом уже внесённых исправлений. До этого — STOP, без blanket PASS.

Исходники, JobSystem, DX11/R4, roadmap и пользовательские файлы не изменялись.
Добавлены только этот отчёт и игнорируемые локальные CPU audit artifacts.

API-контракты: [SetEventOnCompletion](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-seteventoncompletion)
связывает событие с достижением указанного значения;
[GetCompletedValue](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue)
возвращает текущий completed value либо UINT64_MAX при removal. Проверка
completed >= target необходима именно для ожидаемого значения, не для любого
сигнала event.
