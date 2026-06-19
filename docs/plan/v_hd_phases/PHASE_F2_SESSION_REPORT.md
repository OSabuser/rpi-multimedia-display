# PHASE F2 REPORT — FullHD Renderer + Font Renderer

> **Ветка:** `dev-pi2w`
> **Статус:** ЧАСТИЧНО ЗАКРЫТА 🔄 (F2.4–F2.8 ждут PNG от дизайнера)
> **Сессии:** 2026-06-18, 2026-06-19

---

## Чеклист (MASTER_PLAN §4.F2)

| # | Пункт | Статус | Примечание |
|---|---|---|---|
| F2.1 | Получить и изучить font renderer (В2-02: API, формат, зависимости) | ✅ | `fonts.h` / `fonts.c` — callback-архитектура, ARGB8888 premultiplied |
| F2.2 | Спроектировать интеграцию: font renderer → DispmanX DIGIT-слоты, fast_update | ✅ | heap-буфер в `renderer_t`, `changeSourceImageLayer` |
| F2.3 | Layout-проход: новые координаты слотов → `renderer.toml` | ✅ | Все координаты верифицированы на железе |
| F2.4 | Пере-рендер PNG: BACK, modes, arrows, weights, notifications | ⏳ | Ждут дизайнера |
| F2.5 | `check_resources.sh`: убрать `chars/`, обновить размеры | ⚠️ ЧАСТИЧНО | `chars/` удалён; размеры arrows/weights — после F2.4 |
| F2.6 | Splash 1080×1920 | ⏳ | Ждёт дизайнера |
| F2.7 | Замер fast_update и ARROW slow path на A53 → решение P-29 | ⏳ | После финальных PNG (F2.4) |
| F2.8 | Контроль GPU-памяти: `vcgencmd get_mem gpu` + `vcdbg reloc` | ⏳ | При полном наборе слотов |

---

## Закрытые вопросы

| ID | Вопрос | Решение |
|---|---|---|
| В2-01 | `--orientation` для omxplayer | **ЗАКРЫТ.** Не нужен. `display_hdmi_rotate=3` поворачивает framebuffer на уровне VideoCore; omxplayer видит уже повёрнутое пространство. Флаг давал бы суммарно 540°=180° (перевёрнуто). `video_player.c` не изменяется. |
| В2-02 | Font renderer API | **ЗАКРЫТ.** `fonts.h` / `fonts.c` (callback-архитектура). Шрифт: **CalSans260** (Cal Sans 260pt, RLE ARGB8888). Глифы: `0`–`9` (76–160×191 px), `П` (188×191), `-` (85×191). |
| В2-03 | PNG-ассеты | **ЧАСТИЧНО.** Arrows (`up.png`, `down.png`) и modes (11 файлов) получены, включая `ups_malfunction.png` (отдельный спрайт, не алиас). Weights, BACK, splash — ждут. |
| В2-04 | `gpu_mem` | **ЗАКРЫТ.** 128 MB, подтверждено в F1. |
| В2-05 | Hostname | **ЗАКРЫТ.** `indicator-hd-<serial>.local` (генерируется `first_boot.sh` из серийника SoC). |

---

## Новые файлы

| Файл | Назначение |
|---|---|
| `platform/dispmanx/font_renderer.h` | Публичный API адаптера: `font_render_string()`, `font_measure_string()` |
| `platform/dispmanx/font_renderer.c` | Реализация: static-контекст + коллбэк `draw_pixel_to_target` → `draw_string()` из fonts.c |
| `platform/dispmanx/fonts/fonts.h` | Чистая версия без `lcd.h`; типы `tFont/tChar/tImage`, `draw_result_t`, `font_draw_pixel_fn` |
| `platform/dispmanx/fonts/fonts.c` | RLE-декомпрессор, callback-архитектура |
| `platform/dispmanx/fonts/CalSans260.h` | `extern const tFont CalSans260` |
| `platform/dispmanx/fonts/CalSans260.c` | Данные глифов (сгенерированы lcd-image-converter); добавлен `#include "fonts.h"` |
| `platform/dispmanx/README.md` | Документация блока: DispmanX lifecycle, font stack, Z-слои, Mermaid-диаграммы |

---

## Изменённые файлы

| Файл | Изменение |
|---|---|
| `platform/dispmanx/CMakeLists.txt` | Добавлены sources: `font_renderer.c`, `fonts/fonts.c`, `fonts/CalSans260.c`; private includes; `CalSans260.c` в `-w` |
| `platform/dispmanx/renderer_impl.c` | См. подробный список ниже |
| `src_indicator/renderer/renderer.h` | Добавлен `#include "protocol/types.h"`; объявления `renderer_show_digit()`; doc-комментарий enum обновлён (1080×1920, CalSans260, reserved DIGIT_RIGHT) |
| `src_indicator/main.c` | `renderer_apply_elevator()`: два вызова `renderer_show_png(DIGIT_LEFT/RIGHT)` → один `renderer_show_digit(left_char, right_char)`; `mode_to_rel_path()`: `MODE_UPS_MALFUNCTION` → `"modes/ups_malfunction.png"` |
| `tools/CMakeLists.txt` | P-37: `src/` → `src_indicator/` (два места) |
| `deploy/configs/video.toml` | `win_w=1080`, `win_h=1920` |
| `deploy/configs/renderer.toml` | Все координаты слотов верифицированы на железе; `notif_y=1650`; комментарии: `PTMono215` → `CalSans260` |
| `scripts/gen_notifications.sh` | `SIZE=1080x270`, `POINTSIZE=64`, `rectangle 0,0,1079,269` |
| `scripts/check_resources.sh` | Удалён блок `chars/` (38 PNG); `MODE_UPS_MALFUNCTION` → `ups_malfunction.png` |
| `scripts/setup_pi.sh` | Удалена строка `"$DATA/resources/chars"` из `mkdir -p` |
| `just/build.just` | P-38: `find src` → `find src_indicator src_media_ingest` (рецепты `check-format` и `format`) |
| `.clangd` | `PathMatch: "src/.*"` → `"(src_indicator|src_media_ingest)/.*"` (LSP теперь видит оба модуля) |

### `renderer_impl.c` — детальный список изменений

| Изменение | Описание |
|---|---|
| includes | `font_renderer.h`, `fonts/CalSans260.h` |
| enum `DIGIT_SLOT_*` | `DIGIT_SLOT_W=400`, `DIGIT_SLOT_H=191`; производные: `DIGIT_PITCH_PX`, `DIGIT_ALIGNED_H`, `DIGIT_BUF_LEN`, `DIGIT_BUF_BYTES`; комментарий — размеры определяются самым широким глифом шрифта |
| `S_CHAR_UTF8[]` | Lookup-таблица `char_code_t` → UTF-8: 12 символов из 38 имеют глиф в CalSans260 |
| `renderer_t` | Поле `uint32_t *digit_pixels` (heap) |
| `compose_digit_str()` | `char_code_t left/right` → UTF-8 строка |
| `setup_digit_image()` | Заполнение `IMAGE_T` из heap-буфера; **P-41**: `pitch` и `alignedHeight` теперь заполняются явно (без них `vc_dispmanx_resource_write_data` передавал stride=0, слот был прозрачным) |
| `slot_create_digit()` | Первый вызов: `createResourceImageLayer` + `addElementImageLayerOffset` |
| `slot_update_digit()` | Fast-update: `changeSourceImageLayer` |
| `digit_pixels_alloc()` | `calloc(DIGIT_BUF_LEN, sizeof(uint32_t))` — размер с учётом `alignedHeight` |
| `digit_slot_validate_font()` | Проверка при старте: все глифы CalSans260 влезают в `DIGIT_SLOT_W/H`; если нет — `LOG_CRIT` + `renderer_create` возвращает NULL |
| `renderer_show_digit()` | Публичный API: `compose` → `memset` → `font_render_string` → `create/update` |
| `renderer_create()` | Вызов `digit_slot_validate_font()` после `bcm_host_init()` |
| `renderer_destroy()` | `layer.image.buffer = NULL` перед `destroyImageLayer` (предотвращение double-free) |

---

## Исправленные баги

| ID | Баг | Фикс |
|---|---|---|
| P-37 | `tools/CMakeLists.txt`: include path `src/` (несуществующая директория) вызывал ошибку сборки | `src/` → `src_indicator/` в двух местах |
| P-38 | `build.just`: `find src` молча пропускал весь `src_indicator/` и `src_media_ingest/` в рецептах форматирования | `src` → `src_indicator src_media_ingest` |
| P-39 | `pi.just` `deploy-tools`: не деплоил `notif_test` | Подтверждено присутствие в актуальном `pi.just`; закрыт как уже исправленный |
| P-40 | `MUp-rpi0` — ARMv6-бинарь на A53 (cosmetic) | Отмечен, не исправляется до F6/В2-07 |
| P-41 | `renderer_impl.c`: `pitch=0` и `alignedHeight=0` в `IMAGE_T` для DIGIT-слота → `vc_dispmanx_resource_write_data` получал stride=0 → слот создавался, но оставался полностью прозрачным | `setup_digit_image()`: явное вычисление `pitch = DIGIT_PITCH_PX * DIGIT_BYTES_PP`, `alignedHeight = DIGIT_ALIGNED_H`; буфер расширен до `DIGIT_PITCH_PX * DIGIT_ALIGNED_H` (76 800 вместо 76 400 uint32_t) |

---

## Архитектурные решения

| Тема | Решение | Уверенность |
|---|---|---|
| `--orientation` omxplayer | НЕ нужен; `display_hdmi_rotate=3` — достаточно | Высокая (верифицировано на железе) |
| Font: шрифт для цифр этажа | CalSans260 (260pt, пропорциональный, 13 символов) | Высокая |
| Font: адаптер | `font_renderer.c` со static-контекстом; `fonts.c` as-is | Высокая |
| Font: буфер DIGIT | heap в `renderer_t`, `DIGIT_PITCH_PX × DIGIT_ALIGNED_H × 4` байт (~300 KB) | Высокая |
| Font: один слот | `SPRITE_DIGIT_LEFT` только; `SPRITE_DIGIT_RIGHT` — reserved до F6 | Высокая |
| Font: валидация | `digit_slot_validate_font()` в `renderer_create()` — runtime-проверка при старте; static_assert не используется (проверял бы константу против себя) | Высокая |
| Font: размер слота | Определяется самым широким глифом шрифта; при смене шрифта обновить `DIGIT_SLOT_W/H` | Высокая |
| Notification позиция | `y=1650` (1920−270) | Высокая |
| `video.toml` | `win_w=1080`, `win_h=1920` | Высокая |
| `MODE_UPS_MALFUNCTION` | Отдельный спрайт `ups_malfunction.png` (не алиас `malfunction.png`) | Высокая |
| Имена файлов modes/ arrows/ | Сохранены из `dev-pi` (v1); `ups_malfunction.png` — новое | Высокая |

### Char code → CalSans260

Из 38 кодов `char_code_t` глифы есть у 12:

| Код | Символ | Глиф |
|---|---|---|
| `CHAR_0`–`CHAR_9` | `0`–`9` | ✅ |
| `CHAR_PI_CYR` (17) | `П` | ✅ (UTF-8: `0xD0 0x9F`) |
| `CHAR_MINUS` (22) | `-` | ✅ |
| Остальные 25 | — | `NULL` → символ пропускается |
| `CHAR_BLANK` (16) | — | `NULL` → если оба BLANK, слот скрывается |

### Маппинг modes/

| `indicator_mode_t` | Файл |
|---|---|
| `MODE_NORMAL` (0) | — (hide) |
| `MODE_FIRE_ALARM` (1) | `modes/firealarm.png` |
| `MODE_MALFUNCTION` (2) | `modes/malfunction.png` |
| `MODE_LOADING` (3) | `modes/loading.png` |
| `MODE_OVERLOAD` (4) | `modes/overload.png` |
| `MODE_SEIS_ALARM` (5) | `modes/seismo.png` |
| `MODE_FIREMANS` (6) | `modes/fireman.png` |
| `MODE_SERVICE` (7) | `modes/inspection.png` |
| `MODE_EVACUATION` (8) | `modes/evacuation.png` |
| `MODE_UPS_MALFUNCTION` (9) | `modes/ups_malfunction.png` |
| `MODE_DISPATCH_CALL` (100) | `modes/calling.png` |
| `MODE_DISPATCH_ANSWER` (101) | `modes/talking.png` |
| `MODE_CONN_LOST` (255) | — (hide) |

---

## Открытые вопросы (статус после F2)

| ID | Вопрос | Статус |
|---|---|---|
| В2-01 | Ориентация / `--orientation` | ✅ ЗАКРЫТ ОКОНЧАТЕЛЬНО |
| В2-02 | Font renderer API | ✅ ЗАКРЫТ — CalSans260, callback, ARGB8888 |
| В2-03 | PNG-ассеты от дизайнера | ⏳ ОТКРЫТ — arrows + modes получены; weights, BACK, splash ждут |
| В2-04 | `gpu_mem` | ✅ ЗАКРЫТ — 128 MB |
| В2-05 | Hostname | ✅ ЗАКРЫТ — `indicator-hd-<serial>` |
| В2-06 | P-29 на A53: ARROW slow path | ⏳ ОТКРЫТ — замер в F2.7 после финальных PNG |
| В2-07 | Пересборка pi_nku_sync / pi_nku_menu под ARMv7 | ⏳ ОТКРЫТ — низкий приоритет |

---

## Состояние сборки

```
just build::pi   ✅  indicator + media_ingest + uart_rx_dump + notif_test
just build::test ✅  host unit-тесты (не затронуты)
```

Устройство: `indicator-hd-<serial>.local` — `indicator.service` + `media-ingest.service` active (running).
Цифры этажа отображаются через font renderer CalSans260. Координаты слотов верифицированы на железе.

---

## Что делать в следующем чате (F2 продолжение)

### Приоритет 1 — Когда придут PNG от дизайнера

1. Проверить размеры `weights/load_N.png`, `BACK.png` → зафиксировать координаты `weight` в `renderer.toml`
2. `check_resources.sh`: добавить проверки размеров для arrows и weights (функция `check_png` уже есть)
3. `scripts/setup_pi.sh`: убедиться что `modes/` создаётся без `chars/` (уже исправлено)
4. Задеплоить: `just pi::deploy-resources`, `just pi::deploy-configs`, `just pi::restart`
5. Верифицировать на железе: все слоты, замер ARROW slow path (F2.7), GPU-память (F2.8)
6. Splash 1080×1920: `just pi::deploy-splash`

### Для нового чата — передать

```
1. Этот отчёт (PHASE_F2_REPORT.md)
2. MASTER_PLAN__HD.md
3. PHASE_F1_REPORT.md
4. Актуальные файлы: renderer_impl.c, renderer.h, renderer.toml, CMakeLists.txt (dispmanx)
5. PNG от дизайнера (если есть) — с указанием размеров
```

**Стартовая фраза для нового чата:**

> Продолжаем F2 ветки dev-pi2w. Сборка зелёная, цифры работают.
> Прилагаю PHASE_F2_REPORT, MASTER_PLAN и актуальные файлы.
> [Пришли PNG от дизайнера / хочу замерить ARROW slow path / готов к F2.7–F2.8]

---

*Отчёт является входным документом для следующего чата. Передавать вместе с MASTER_PLAN__HD.md и PHASE_F1_REPORT.md.*