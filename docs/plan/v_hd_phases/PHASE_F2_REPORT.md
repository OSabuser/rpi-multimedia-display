# PHASE F2 REPORT — FullHD Renderer + Font Renderer

> **Ветка:** `dev-pi2w`
> **Статус:** ЗАКРЫТА ✅
> **Сессии:** 2026-06-18, 2026-06-19, 2026-06-22

---

## Чеклист (MASTER_PLAN §4.F2)

| # | Пункт | Статус | Примечание |
|---|---|---|---|
| F2.1 | Получить и изучить font renderer (В2-02) | ✅ | `fonts.h/c` — callback-архитектура, ARGB8888 premultiplied |
| F2.2 | Спроектировать интеграцию: font renderer → DispmanX DIGIT-слоты | ✅ | heap-буфер в `renderer_t`, `changeSourceImageLayer` fast-update |
| F2.3 | Layout-проход: координаты слотов → `renderer.toml` | ✅ | Все координаты верифицированы на железе |
| F2.4 | PNG: BACK, modes, arrows, weights, notifications | ✅ | BACK/modes 1080×1920; arrows 172×191; weights 237×59; notif 1080×270 |
| F2.5 | `check_resources.sh`: размеры arrows, weights | ✅ | `check_png_size()` добавлена; arrows 172×191, weights 237×59 |
| F2.6 | Splash 1080×1920 | ✅ | Задеплоен на устройство |
| F2.7 | Замер ARROW slow path на A53 → решение P-29 | ✅ | P-29 закрыт: визуально незаметно, OPACITY-путь не нужен |
| F2.8 | GPU-память: `vcgencmd get_mem gpu` + `vcdbg reloc` | ✅ | 128 MB достаточно с запасом; пиковое потребление ~24 MB |

---

## Верификация запуска (финальное состояние)

```
июн 22 08:09:09 indicator-95e49a indicator[2654]: renderer config: resources=/data/resources
    digit_l=(485,200) digit_r=(0,0) arrow=(880,225) weight=(800,27) notif=(0,1650)
июн 22 08:09:09 indicator-95e49a indicator[2654]: renderer: slot 0 created, z=2, pos=(0,0), size=1080x1920
июн 22 08:09:09 indicator-95e49a indicator[2654]: renderer: digit slot created, pos=(485,200), size=450x239
июн 22 08:09:27 indicator-95e49a indicator[2654]: renderer: slot 5 created, z=4, pos=(880,225), size=172x191
июн 22 08:09:14 indicator-95e49a indicator[2654]: watchdog: frames_ok=158 parse_errors=0 unknown_opcodes=0
    | last: floor=0 arrow=2 mode=0 | omxplayer_pid=2656 win=0,0,1080x1920
```

---

## Новые файлы

| Файл | Назначение |
|---|---|
| `src_indicator/app_private.h` | Общие типы `app_t`, `stats_t`, `setup_status_t`; forward-объявления трёх TU |
| `src_indicator/app_handlers.c` | `on_uart_frame`, `on_media_status`, `on_watchdog_tick`, `notif_arm/disarm`, `maybe_clear_mcu_notification` |
| `src_indicator/app_render.c` | `renderer_apply_elevator/mode/dispatch`, `mode_to_rel_path` |
| `platform/dispmanx/font_renderer.h/.c` | Адаптер CalSans260 → DispmanX буфер; stride-aware коллбэк |
| `platform/dispmanx/fonts/fonts.h/.c` | RLE-декомпрессор, callback-архитектура |
| `platform/dispmanx/fonts/CalSans260.h/.c` | Данные глифов 325pt (сгенерированы lcd-image-converter) |
| `platform/dispmanx/README.md` | Документация: Z-слои, font stack, stride/pitch, lifecycle |
| `src_indicator/README.md` | Event loop, poll fd, инициализация, watchdog, signalfd |
| `README.md` (корень) | Актуализирован под Pi Zero 2W, 1080×1920, текущее состояние |

---

## Изменённые файлы

| Файл | Изменение |
|---|---|
| `src_indicator/main.c` | Рефакторинг: вынесены handlers и render; остался composition root + poll loop |
| `platform/dispmanx/renderer_impl.c` | `DIGIT_SLOT_W=450`, `DIGIT_SLOT_H=239`; `font_render_target_t.stride`; `digit_slot_validate_font()`; P-41 fix |
| `platform/dispmanx/CMakeLists.txt` | Добавлен явный `-lvcos`; `font_renderer.c`, `fonts/fonts.c`, `fonts/CalSans260.c` |
| `CMakeLists.txt` | `app_handlers.c`, `app_render.c` в `add_executable(indicator)`; LSP stub таргет |
| `deploy/configs/video.toml` | `win_w=1080`, `win_h=1920` |
| `deploy/configs/renderer.toml` | Все координаты под 1080×1920, верифицированы на железе |
| `scripts/check_resources.sh` | `check_png_size()`; arrows 172×191; weights 237×59; удалён блок `chars/` |
| `scripts/gen_notifications.sh` | `SIZE=1080x270`, `POINTSIZE=64` |
| `just/build.just` | `CMAKE_C_FLAGS` → `CMAKE_C_FLAGS_DEBUG` в рецепте `pi-debug` |

---

## Исправленные баги

| ID | Баг | Фикс |
|---|---|---|
| P-28 | `FD_TIMER` отсутствовал в poll loop → `on_watchdog_tick` никогда не вызывался | Добавлен обработчик `FD_TIMER` в main poll loop |
| P-37 | `tools/CMakeLists.txt`: include path `src/` → ошибка сборки | `src/` → `src_indicator/` |
| P-38 | `build.just`: `find src` пропускал `src_indicator/` и `src_media_ingest/` | `src` → `src_indicator src_media_ingest` |
| P-41 | `setup_digit_image()`: `pitch=0`, `alignedHeight=0` → слот прозрачный | Явное вычисление `pitch = DIGIT_PITCH_PX * DIGIT_BYTES_PP`, `alignedHeight = DIGIT_ALIGNED_H` |
| P-42 | `draw_pixel_to_target`: stride считался как `width` (450) вместо `DIGIT_PITCH_PX` (464) → артефакты при смене шрифта | Добавлено поле `stride` в `font_render_target_t`; коллбэк использует `stride` |
| P-43 | `pi-debug`: `-DCMAKE_C_FLAGS` перекрывал тулчейн → `vcos_*` undefined symbols | `CMAKE_C_FLAGS` → `CMAKE_C_FLAGS_DEBUG` (аддитивен); `-lvcos` добавлен явно в `dispmanx/CMakeLists.txt` |

---

## Архитектурные решения

| Тема | Решение | Уверенность |
|---|---|---|
| Font: шрифт | CalSans260 325pt (файл сохранил имя `CalSans260.c`) | Высокая |
| Font: размер слота | `DIGIT_SLOT_W=450` (макс. строка «П0» = 435 px + запас 15 px) | Высокая |
| Font: stride | `font_render_target_t.stride = DIGIT_PITCH_PX = 464` — обязательно, не `width` | Высокая |
| Font: буфер | heap `calloc(DIGIT_PITCH_PX × DIGIT_ALIGNED_H)` в `renderer_t` (~430 KB) | Высокая |
| Font: адаптер | static-контекст в `font_renderer.c` — допустимо в однопоточном рендерере | Высокая |
| Рефакторинг main.c | 3 TU: `main.c` + `app_handlers.c` + `app_render.c` + `app_private.h` | Высокая |
| P-29 ARROW slow path | Закрыт как неактуальный на A53: визуально незаметно | Высокая |
| GPU-память | 128 MB достаточно; пик ~24 MB (BACK + MODE + transpose); поднимать не нужно | Высокая |
| `--orientation` omxplayer | Не нужен: `display_hdmi_rotate=3` поворачивает на уровне VideoCore | Высокая |
| `-lvcos` | Явная линковка в `dispmanx/CMakeLists.txt` — `ld.lld` не резолвит транзитивно | Высокая |

### Размеры ассетов (финальные)

| Ассет | Размер | Примечание |
|---|---|---|
| BACK.png, MODE-иконки | 1080×1920 | Полный экран |
| arrows/up.png, down.png | 172×191 | Верифицировано на железе |
| weights/load_N.png | 237×59 | N = 0..15 |
| notifications/*.png | 1080×270 | y=1650 |
| DIGIT-слот | 450×239 | font renderer, fast_update |

### Глифы CalSans260 325pt

| Глиф | Ширина | Высота |
|---|---|---|
| `П` | 235 px | 239 px |
| `0` | 200 px | 239 px |
| `1`–`9`, `-` | 95–186 px | 239 px |

Из 38 кодов `char_code_t` глифы есть у 12 (`CHAR_0`–`CHAR_9`, `CHAR_PI_CYR`, `CHAR_MINUS`).

---

## GPU-память (F2.8)

```
gpu=128M   arm=384M

vcdbg reloc:
  dispmanx_resource   8.0 MB  (BACK.png или MODE-слот)
  ARM FB              8.0 MB
  transpose buffer0   8.0 MB  (display_hdmi_rotate=3)
  audioplus_tmp_buf  16 KB
```

Два полноэкранных ARGB-слота одновременно (BACK + MODE) = ~16.6 MB.
С transpose buffer: ~24.6 MB из 128 MB. Запас достаточный — `gpu_mem` не меняем.

---

## Открытые вопросы (статус после F2)

| ID | Вопрос | Статус |
|---|---|---|
| В2-01 | `--orientation` omxplayer | ✅ ЗАКРЫТ — не нужен |
| В2-02 | Font renderer API | ✅ ЗАКРЫТ |
| В2-03 | PNG-ассеты | ✅ ЗАКРЫТ |
| В2-04 | `gpu_mem` | ✅ ЗАКРЫТ — 128 MB |
| В2-05 | Hostname | ✅ ЗАКРЫТ — `indicator-hd-<serial>` |
| В2-06 | P-29 ARROW slow path | ✅ ЗАКРЫТ — неактуален на A53 |
| В2-07 | Пересборка pi_nku_sync/menu под ARMv7 | ⏳ ОТКРЫТ — низкий приоритет |

---

## Состояние сборки

```
just build::pi      ✅  indicator + media_ingest + uart_rx_dump + notif_test
just build::pi-debug ✅  debug-бинарь с -g3 -O0
just build::test    ✅  host unit-тесты (не затронуты)
```

Устройство: `indicator-hd-<serial>.local` — `indicator.service` + `media-ingest.service` active (running).
Полный UI 1080×1920: BACK, цифры (CalSans260 325pt), стрелки, режимы, уведомления, splash.

---

## Выход фазы

- Font renderer CalSans260 325pt интегрирован, артефакты устранены ✅
- Все слоты 1080×1920 верифицированы на железе ✅
- `main.c` декомпозирован на 3 TU ✅
- Watchdog keepalive (P-28) работает ✅
- GPU-память: 128 MB достаточно ✅
- P-29 закрыт ✅
- Сборка: `pi`, `pi-debug`, `test` — зелёные ✅

**Следующая фаза: F3 — FullHD video + media-ingest**

---

*Передавать в следующий чат вместе с MASTER_PLAN__HD.md, PHASE_F1_REPORT.md.*