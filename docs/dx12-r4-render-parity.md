# DX12 ↔ R4 — реестр полноты графических подсистем

> **Стартовая редакция: 21 ноября 2026.** Проектная задача: [Issue #14 — No feature left behind](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/14).
>
> **Этот реестр — страховка от незаметной потери возможностей X-Ray при переносе с R4/DX11 на native DX12.** Это **первоначальная карта обнаруженных source entry points**, а не завершённый аудит всего renderer. Заявления «полный охват» допустимы только после source audit всех render paths и проверок в настоящей игре. Направления могут развиваться параллельно с F4.2, без остановки реализации ради бесконечных ревью.

## 1. Определения статусов

| Статус | Смысл |
|---|---|
| **VERIFIED IN SCOPE** | Код + фактический gameplay evidence, с указанными типами и состояниями |
| **PARTIAL / IN PROGRESS** | Часть функциональности реализуется; все остаточные неподдержанные классы выписаны |
| **MISSING** | R4 путь найден, но эквивалентный DX12 путь отсутствует, что подтверждено аудитом |
| **UNKNOWN / AUDIT REQUIRED** | R4 source path существует, но работоспособность/эквивалентность DX12 ещё не проверена |
| **DEFERRED WITH FALLBACK** | Осознанно отложено; есть обоснованный fallback и отдельная приёмка |
| **NOT APPLICABLE** | Признак не относится к текущему игровому контексту; объяснение обязательно |

**Нельзя** использовать одну итоговую цифру «99% перенесено». Каждый важный класс может быть потерян целиком даже при отличном общем числе draws.

## 2. Первичный инвентарь — что нельзя забыть

R4 source paths ниже проверены в опубликованной ветке `feature/f41-full-static-jupiter`, HEAD `b9b3dd227`. Последняя видимая реализация F4.2 находится в локальном рабочем дереве Codex; результаты скриншотов **не означают независимого source acceptance**. Все статусные пометки относятся только к указанному scope.

| Семейство | Входной путь R4 / код для ревью | Текущий статус DX12 | Пример подтверждения |
|---|---|---|---|
| Static `level`, terrain, hierarchy, LOD | `Layers/xrRenderPC_R4/r4_R_render.cpp`, `Layers/xrRender/r__dsgraph_render.cpp` | **VERIFIED IN SCOPE (F4.1-E artifacts):** 20 880 matching visual IDs, 13 911 drawable, independent source review pending | Сопоставление R4↔DX12 статических visual IDs и 3 matched views |
| `MT_TREE_ST/PM` и растительность уровня | `r__dsgraph_render.cpp`, исходная `level.geom` | **PARTIAL:** статические меши показаны, ветер/instance semantics вне scope | R4-vs-DX12 лес, дальние деревья, cutout, movement |
| Game-owned dynamic rigid/skinned/animated | `r4_R_render.cpp`, `r__dsgraph_render.cpp`, `Render->model_Create` | **PARTIAL / F4.2 in progress:** двери, лампы, props появляются; другие skeletal/animated classes ещё не подтверждены | Дверь открывается/закрывается; crate движется; NPC анимирован |
| Ground details / `level.details` | `Layers/xrRender/DetailManager.cpp`, `DetailModel.cpp`, R4 detail draw | **PARTIAL / F4.2 in progress:** есть первые видимые кусты/трава, R4 instance parity не доказана | Один и тот же участок дороги: density, range, fade, wind |
| Particles / effects / groups | `Layers/xrRender/ParticleEffect.cpp`, `ParticleGroup.cpp`, `Blender_Particle.cpp` | **UNKNOWN / AUDIT REQUIRED** | Дым, огонь, пыль, искры, взрывы по исходным эффектам; emitter lifecycle |
| Decals / wallmarks / bullet hits | `Layers/xrRender/WallmarksEngine.cpp`, `dxWallMarkArray.cpp` | **KNOWN DEFECT + AUDIT REQUIRED**: [#13](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/13) — мерцание наложений; полное покрытие не доказано | Следы пуль, кровь, грязь, decal на скелетном visual, движение камеры |
| Dynamic lights / sun / shadow passes | `Layers/xrRenderPC_R4/r4_rendertarget_accum_point.cpp`, `...accum_spot.cpp`, `...phase_smap_D.cpp` | **UNKNOWN / FUTURE F4.3** | Фонарь, лампа, солнце, ночь, динамические тени |
| Sky, clouds, environment | `Layers/xrRender/dxEnvironmentRender.cpp` | **PARTIAL / FUTURE F4.3 + V** | Time of day, тучи, горизонт, погодные переходы |
| Rain, wetness, thunder, lens flare | `dxRainRender.cpp`, `dxThunderboltRender.cpp`, `dxLensFlareRender.cpp`, R4 rain path | **UNKNOWN / AUDIT REQUIRED** | Дождь/ливень, гроза, блики, дождевые капли на экране |
| HUD, UI, fonts, inventory | `Layers/xrRender/dxUIRender.cpp`, `dxFontRender.cpp`, `r__dsgraph_render.cpp` HUD phases | **UNKNOWN / FUTURE F4.4** | Миникарта, прицел, инвентарь, PDA, интерактивные окна |
| Weapon/first-person overlays, scopes | `r__dsgraph_render.cpp` HUD path; related weapon render paths must be enumerated | **UNKNOWN / FUTURE F4.4** | Выстрел, прицел, смена оружия, scope overlay |
| Postprocessing, color, screen FX | `Layers/xrRender/RenderTargetPhaseRainDrops.cpp`, `rendertarget_phase_nightvision.cpp`, `rendertarget_phase_thermalvision.cpp`; R4 bloom/combine | **UNKNOWN / FUTURE F4.3–F4.5** | Тонмаппинг, bloom, vision modes, screen-space rain |
| Blended materials, glass, water | R4 material/blender/shader paths — audit exact usage | **KNOWN DEFECT:** [#6](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues/6) | Вода и стекло без сплошных чёрных пятен |
| Scripted, mod-specific and debug draws | Найти все вызывающие renderer интерфейсы со стороны game/scripts/mods | **UNKNOWN / AUDIT REQUIRED** | Редкие gameplay события и опциональные графические режимы |

*Таблица не полная до полного обхода source registration/callers; новые семейства нужно добавлять, а не скрывать в строке «прочее». Имена источников следует уточнять по актуальному Git tree. «UNKNOWN» не означает «не реализовано».*

## 3. Как построить машинно проверяемый реестр

Первый инструмент — **source inventory**, затем **runtime trace diff**. Каждый `render_path` (а не только asset type) должен иметь запись:

```json
{
  "id": "r4.wallmarks.static",
  "family": "wallmarks",
  "r4_source": ["Layers/xrRender/WallmarksEngine.cpp"],
  "r4_producer": "to_identify",
  "dx12_source": [],
  "status": "unknown",
  "milestone": "F4.3/F4.4 — verify",
  "test_scenario": "bullet-impact-matched",
  "evidence_commit": null,
  "known_issue": 13,
  "reason": "source path present; DX12 feature parity not audited"
}
```

В текущем документе статусы нужны человеку; рядом реализовать `render-parity.json` (или CSV), валидировать schema/unique IDs и автоматически строить читаемую таблицу.

Не считать `R4 registered` доказательством `R4 submitted`. Для каждой категории различать:

`asset_loaded`, `entity_alive`, `requested`, `eligible`, `visible`, `culled(reason)`, `submitted`, `draw_calls`, `instances`, `skipped(reason)`.

Для particle effects важна **история emitters/events**, а не искусственный «статический visual ID». Фиксировать simulation seed/event/time, число emitted/alive/visible particles и material pass. Вариативные эффекты нельзя требовать сравнивать только по SHA-256 одинаковых кадров.

Включить сбор через **ограниченный opt-in**, поверх `OGSR_DX12_CMP=1` или совместимого режима, без per-frame гигабайтного лога в Release. Сравнивать R4/DX12 по исходному событию и классу, а не требовать одинаковые draw counts от разных culling/instancing подходов.

## 4. Матрица реальных игровых сценариев

| Сценарий | Что обязательно увидеть или доказать |
|---|---|
| **Jupiter interior** | Статика, динамические двери, пропы, wallmarks/decal, освещение и HUD |
| **Jupiter outdoors** | Terrain, дальние объекты, кусты и трава, LOD, alpha, weather |
| **Zaton outdoor / water** | Универсальность level assets, water/glass/transparent surfaces |
| **Combat** | Bullet marks, sparks/muzzle smoke, blood, explosions, effects, debris |
| **Anomalies / emissions** | Специальные particle/emissive/screen effects, game event timing |
| **Day → night** | Солнце, небо, локальные lights/shadows, postprocessing |
| **Rain / thunder** | Погодные примитивы, screen effects, sky transitions |
| **Gameplay UI** | HUD, PDA, inventory, icons, scopes, scripted overlays |
| **Lifecycle** | Save/load, spawn/despawn, level change, `vid_restart`, resize, shutdown |

Для каждого сценария: exact save/level/camera, погода, renderer flags, исходные события, timestamps, исходный R4 capture, DX12 capture, SHA кода, diagnosis gaps; moving FX — короткое видео/серия 30+ кадров. При неповторяемых эффектах — диапазон/визуальное соответствие, а не ложная точная pixel identity.

## 5. Правило полноты перед выпуском

**Новый DX12 render feature принят** только если:
1. Его R4 source family зарегистрирована в инвентаре; известны source ownership и material semantics.
2. Есть DX12 counterpart или обоснованная defer/fallback.
3. Есть реально работающая игровая сцена и regression тест со счётчиками, включая неподдержанные случаи.
4. Нет новых **необъяснённых** R4-only render families для принятого scope.

**Общий production DX12 готов** только если весь известный R4 render API и frame-phase topology классифицирован, все необходимые для игры классы имеют evidence и нет необъяснённых пропущенных возможностей. Ни `20 880/20 880`, ни 0 GBV errors не являются доказательством полной визуальной функциональности.

**Исторические и текущие баги** живут в [GitHub Issues](https://github.com/rafaelisraelyan/OGSR-Engine-rtx/issues); настоящий документ содержит **покрытие, критерии и ссылки**, а не полный журнал тестирования.

## 6. Работа без блокирования текущего F4.2

Это сквозная программа качества, не требование «сначала переписать 100% движка». Можно поручить отдельному субагенту read-only R4 inventory и инструмент списка class/paths, пока основной Codex завершает F4.2. Следующие функциональные пакеты постепенно закрывают категории. Статус каждого кандидата — явный. **Нельзя объявлять общий DX12 паритет, пока записи в этом документе остаются UNKNOWN для необходимых R4 функций.**
