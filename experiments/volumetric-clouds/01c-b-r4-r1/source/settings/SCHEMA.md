# Cloud graphics settings schema v1

**Область:** публичные настройки графики для будущего игрового UI. Этот файл
не является weather-конфигом и никогда не должен содержать weather controls.

## Объект

Корень — плоский JSON-объект. Вложенные объекты, массивы и более сложная
семантика дубликатов не входят в контракт.

| Ключ | Тип | Обязателен | По умолчанию | Смысл |
|---|---|---|---|---|
| `schema_version` | integer | нет | `1` | Должен быть `1`; иное значение отклоняет файл и оставляет safe defaults. |
| `clouds_enabled` | boolean | нет | `true` | Рендерить облака вкл/выкл. Не должен менять simulated weather. |
| `cloud_render_mode` | string | нет | `"volumetric3d"` | `"volumetric3d"` реализован. `"classic2d"` — будущий game-side контракт; standalone пишет warning и безопасно откатывается на volumetric 3D. |
| `cloud_quality` | string | нет | `"quality"` | `performance`, `balanced`, `quality`, `reference`. Использует существующий проверенный `--profile`. |

## Маппинг качества

| Quality | Ray steps | Shadow steps | Cloud scale |
|---|---:|---:|---|
| `performance` | 64 | 2 | half |
| `balanced` | 96 | 4 | half |
| `quality` | 128 | 6 | half |
| `reference` | 128 | 6 | full |

## Приоритет

1. Явный публичный CLI (`--clouds`, `--cloud-quality`, `--profile`,
   `--cloud-render-mode`).
2. Валидный user graphics config.
3. Profile default (`quality`).

Явные числовые developer-переопределения (например `--steps`) по-прежнему
побеждают в developer standalone и никогда не записываются в этот публичный
файл.

## Некорректные и неизвестные данные

- Файл отсутствует: safe defaults, без ошибки.
- Corrupt JSON: safe defaults, warning, рендер продолжается.
- Неподдерживаемый `schema_version`: safe defaults, warning.
- Неверное поле: safe default только для него; остальные валидные поля
  применяются.
- Unknown keys: игнорируются и перечисляются. Внедрённые `weather_mode`,
  `density`, `seed`, `noise_seed`, `size`, `format`, `shadow_od` и подобные
  не могут изменить погоду, процедурный шум облаков или рендер.
  `seed`/`size`/`format` — developer-only noise/texture controls, не публичные
  quality settings.
- Вложенные объекты/массивы отклоняются как malformed JSON; плоский контракт
  не превращает их в noise/weather controls.
- Дубликаты известных ключей работают по last-key-wins и не создают
  альтернативного пути к погоде.
- UTF-8 BOM допускается.
- Сохранение атомарное (`<file>.tmp` + `MoveFileExW` replace).

## Файлы в этой папке

- `cloud_settings.valid.json` — все публичные ключи.
- `cloud_settings.partial.json` — частичный валидный файл.
- `cloud_settings.classic2d_fallback.json` — будущий режим, safe fallback.
- `cloud_settings.corrupt.json` — некорректный JSON, safe defaults.
- `cloud_settings.unknown_future.json` — неподдерживаемая версия, safe defaults.
- `cloud_settings.injected_weather.json` — weather/debug ключи, которые должны игнорироваться.
- `cloud_settings.injected_noise.json` — `seed`/`noise_seed`/`size`/`format`, которые должны игнорироваться.
- `cloud_settings.nested_malformed.json` — вложенный объект, который должен отклоняться.
