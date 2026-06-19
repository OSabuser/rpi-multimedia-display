# platform/dispmanx — DispmanX renderer

Платформенная реализация рендерера для Raspberry Pi.  
Скрыта за опаковым указателем `renderer_t`; `src_indicator` видит только
`renderer/renderer.h` и ничего из DispmanX / bcm_host.

---

## Структура модуля

```
platform/dispmanx/
├── renderer_impl.c      # реализация renderer.h: слоты, DIGIT, keepalive
├── font_renderer.h/.c   # адаптер fonts → ARGB8888-буфер
├── fonts/
│   ├── fonts.h/.c       # RLE-декомпрессор, draw_string, get_string_width
│   ├── CalSans260.h      # extern const tFont CalSans260
│   └── CalSans260.c      # RLE-данные глифов (сгенерирован lcd-image-converter)
└── layers/              # vendored: Andrew Duncan (MIT)
    ├── image.h/.c        # IMAGE_T: буфер, pitch, alignedHeight
    ├── imageLayer.h/.c   # DispmanX resource + element lifecycle
    └── loadpng.h/.c      # PNG → IMAGE_T через libpng
```

---

## Z-слои и слоты

Compositor VideoCore IV накладывает слои в порядке Z.  
Каждый слот — один DispmanX element с фиксированным Z.

| Слот | `sprite_slot_t` | Z | Содержимое |
|---|---|---|---|
| 0 | `SPRITE_BACKGROUND` | 2 | `BACK.png` — полный экран 1080×1920 |
| 1 | `SPRITE_MODE` | 3 | иконка режима (`modes/*.png`) или скрыт |
| 2 | `SPRITE_WEIGHT` | 4 | индикатор нагрузки (`weights/load_N.png`) |
| 3 | `SPRITE_DIGIT_LEFT` | 4 | font renderer CalSans260 (буфер 400×191) |
| 4 | `SPRITE_DIGIT_RIGHT` | 4 | reserved, не используется до F6 |
| 5 | `SPRITE_ARROW` | 4 | стрелка направления (`arrows/*.png`) |
| 6 | `SPRITE_NOTIFICATION` | 5 | USB-баннер (`notifications/*.png`, 1080×270) |

DIGIT_LEFT, DIGIT_RIGHT, ARROW — **fast\_update** слоты: element и resource
создаются один раз, при обновлении меняются только пиксели
(`changeSourceImageLayer`). Остальные — **slow**: полное пересоздание
при каждом вызове.

---

## Жизненный цикл слота

```mermaid
flowchart TD
    A["renderer_show_png(slot, path)"] --> B{fast_update\nИ initialized?}
    B -- да --> C[destroyImage буфер\nloadPng новый\nchangeSourceImageLayer]
    C --> D{размер\nсовпал?}
    D -- да --> E[✅ fast update]
    D -- нет --> F[initialized=0\ndestroyImageLayer]
    F --> G
    B -- нет --> G{initialized?}
    G -- да --> H[destroyImageLayer]
    H --> G2{initialized?}
    G2 -- нет --> I
    G -- нет --> I[memset layer=0\nloadPng\ncreateResourceImageLayer\naddElementImageLayerOffset\nupdate_submit_sync]
    I --> J[✅ slot created]
```

---

## DIGIT-слот: font renderer

Цифры этажа рисуются не PNG-файлом, а шрифтовым рендерером прямо в
ARGB8888-буфер, который затем загружается в DispmanX как ресурс.

```mermaid
flowchart LR
    A["renderer_show_digit(left, right)"]
    A --> B["compose_digit_str()\nchar_code_t → UTF-8"]
    B --> C{строка\nпустая?}
    C -- да --> D[renderer_hide\nSPRITE_DIGIT_LEFT]
    C -- нет --> E["memset digit_pixels = 0"]
    E --> F["font_measure_string()\nвычислить ширину"]
    F --> G["x_off = (400 - str_w) / 2\nцентрирование"]
    G --> H["font_render_string()\nCalSans260 → ARGB8888"]
    H --> I{initialized?}
    I -- нет --> J[slot_create_digit\ncreateResource\naddElement]
    I -- да --> K[slot_update_digit\nchangeSource]
```

### Буфер DIGIT-слота

VideoCore требует выравнивания pitch и высоты на 16 пикселей:

```bash
DIGIT_SLOT_W    = 400 px   (вмещает «П0» = 348 px + запас)
DIGIT_SLOT_H    = 191 px   (высота глифа CalSans260)
DIGIT_PITCH_PX  = 400      (400 уже кратно 16)
DIGIT_PITCH     = 1600 байт (400 × 4 байта/px, ARGB8888)
DIGIT_ALIGNED_H = 192      (ALIGN_TO_16(191))
DIGIT_BUF_LEN   = 76 800   uint32_t  (400 × 192)
```

Размеры буфера определяются самым широким глифом шрифта.
При смене шрифта обновить `DIGIT_SLOT_W/H` — `digit_slot_validate_font()`
в `renderer_create()` проверит соответствие при старте и вернёт `NULL`
с сообщением в syslog если буфер мал.

### Формат пикселей

`ARGB8888 premultiplied` — непрозрачный белый = `0xFFFFFFFF`,
прозрачный фон = `0x00000000`. Совместим с `VC_IMAGE_ARGB8888` +
`DISPMANX_FLAGS_ALPHA_FROM_SOURCE` без конвертации.

---

## Шрифтовый стек

```mermaid
flowchart TB
    subgraph "fonts/ (платформонезависимый)"
        F1["fonts.h — tFont, tImage, draw_result_t\nfont_draw_pixel_fn callback"]
        F2["fonts.c — draw_string(), get_string_width()\nRLE-декомпрессор, UTF-8 декодер"]
        F3["CalSans260.c — RLE-данные\n(сгенерирован lcd-image-converter)"]
        F3 --> F2
        F1 --> F2
    end

    subgraph "font_renderer (адаптер)"
        R1["font_renderer.h — font_render_string()\nfont_measure_string()"]
        R2["font_renderer.c — статический контекст\ns_render_ctx / s_render_x / s_render_y\ndraw_pixel_to_target() callback"]
        R1 --> R2
    end

    subgraph "renderer_impl.c"
        D["renderer_show_digit()"]
    end

    D --> R2
    R2 --> F2
```

`fonts.c` не знает ничего о DispmanX — он вызывает `font_draw_pixel_fn`
для каждого непрозрачного пикселя. `font_renderer.c` подставляет
`draw_pixel_to_target` как коллбэк, который пишет в `digit_pixels[]`
с bounds-check (пиксели за границей буфера молча пропускаются).

Статические переменные `s_render_ctx / s_render_x / s_render_y` —
допустимо: indicator однопоточный, рендеринг синхронный в event-loop.

### RLE-формат CalSans260

Два типа записей в `image->data[]`:

```bash
header & 0xFFFFFF00 == 0xFFFFFF00  →  UNIQUE:
    len = 0x100 - (header & 0xFF)
    следующие len слов — уникальные пиксели ARGB8888

иначе  →  REPEATABLE:
    len = header & 0xFFFF
    следующее слово — пиксель, повторить len раз
```

---

## Взаимодействие с DispmanX

```mermaid
sequenceDiagram
    participant M as main.c
    participant R as renderer_impl.c
    participant VC as VideoCore IV

    M->>R: renderer_create(cfg)
    R->>VC: bcm_host_init()
    R->>VC: vc_dispmanx_display_open(0)
    R-->>M: renderer_t*

    M->>R: renderer_show_png(BACKGROUND, path)
    R->>VC: vc_dispmanx_resource_create(ARGB8888, w|pitch<<16, h|alignedH<<16)
    R->>VC: vc_dispmanx_resource_write_data(resource, pitch, buffer)
    R->>VC: vc_dispmanx_update_start(0)
    R->>VC: vc_dispmanx_element_add(resource, z, dstRect)
    R->>VC: vc_dispmanx_update_submit_sync(update)

    M->>R: renderer_show_digit(left, right)
    R->>R: compose + render → digit_pixels[]
    R->>VC: vc_dispmanx_resource_write_data (fast: changeSource)
    R->>VC: vc_dispmanx_update_submit_sync(update)

    loop каждые 30 с
        M->>R: renderer_keepalive()
        R->>VC: update_start → update_submit_sync
        note over VC: предотвращает dormant state (P-28)
    end

    M->>R: renderer_destroy()
    R->>VC: destroyImageLayer × N
    R->>VC: vc_dispmanx_display_close()
    R->>VC: bcm_host_deinit()
```

### Keepalive (P-28)

VideoCore IV переходит в dormant при отсутствии DispmanX-активности (~2 дня).
omxplayer продолжает декодировать, но кадры перестают рендериться на экран.
Пустая транзакция `update_start → update_submit_sync` каждые 30 с
удерживает compositor активным. Стоимость: ~1 мс round-trip.

---

## Зависимости сборки

Собирается только для Pi (DispmanX недоступен на host).
Все зависимости — в `platform/dispmanx/CMakeLists.txt`.

| Зависимость | Откуда | Линковка |
|---|---|---|
| `bcm_host` | `/opt/vc/lib/` (Pi sysroot) | статическая `-L/opt/vc/lib` |
| `libpng16` | `build-env/pi-sysroot/usr/lib/` | динамическая `-lpng16` |
| `libz` | `build-env/pi-sysroot/usr/lib/` | динамическая `-lz` |

`libpng16.a` из Buster/armhf скомпилирована с `-march=armv7-a` (Thumb-2)
и вызывает SIGSEGV на ARMv6 в `.init_array` до `main()`.
На Cortex-A53 (Pi Zero 2W) это неактуально, но динамическая линковка
сохранена для единообразия.
