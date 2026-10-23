# Lift Indicator HD

Встраиваемая система отображения состояния лифта на базе Raspberry Pi Zero 2W.
Получает данные от STM32 по UART, отображает этаж и режим работы на HDMI-дисплее
1080×1920, воспроизводит голосовые объявления и позволяет обновлять фоновое видео
с USB-носителя.

---

## Возможности

- **Дисплей** — DispmanX-оверлей (7 слотов) поверх фонового видео: цифры этажа через font renderer (CalSans260), стрелка направления, иконки нештатных режимов (9 режимов), диспетчерская связь, информационные уведомления
- **Аудио** — голосовые объявления этажей (в т.ч. составные: 21–49, П1–П9, −1..−9), звуки событий, фоновая музыка; приоритетная очередь с вытеснением
- **USB Media Ingest** — замена фонового видео с FAT32/exFAT/ext4 носителя; поддержка склейки нескольких MP4-файлов; индикация прогресса на экране
- **Надёжность** — read-only rootfs (overlayfs), watchdog keepalive (VideoCore IV P-28), автоперезапуск через systemd
- **Provisioning** — factory image: уникальный hostname из SoC serial, SSH keys, WiFi seed из `/boot/`, TUI-меню настройки параметров MCU

---

## Оборудование

| Компонент | Модель |
|---|---|
| SBC | Raspberry Pi Zero 2W (BCM2710A1 · 4× Cortex-A53 · ARMv8-A 32-bit) |
| ОС | Raspbian Buster Lite 2023-05-03 (Debian 10, armhf) |
| Дисплей | HDMI, 1080×1920 (портрет, `display_hdmi_rotate=3`) |
| Аудио | hifiberry-dac (I2S, несущая плата) |
| MCU | STM32 на несущей плате, UART `/dev/serial0` → `ttyAMA0`, 115200 8N1 |
| Сеть | WiFi **только 2.4 GHz**, WPA2 (5 GHz / WPA3 не поддерживаются) |
| Питание | **12 VDC** на несущую плату. ⚠️ 24 VDC выводит из строя плату питания подсветки дисплея (КЗ, 22.10.2026) |
| Накопитель | SD-карта ≥16 GB (3 раздела: `/boot` 256M · rootfs 8G · `/data` 4G `LABEL=data`) |

---

## Быстрый старт

### Прошивка нового устройства

> ⚠️ Factory image для 2W (`indicator-hd-base-*.img.gz`) собирается в фазе F5 (конвейер готов, снимок эталонной платы — после её проверки). `indicator-base-v1.0.0*.img.gz` — образ для Pi Zero W (ARMv6, 600×1024), на 2W не использовать. Пошагово для производства — [`FLASHING_GUIDE.md`](docs/FLASHING_GUIDE.md).

```bash
# 1. Прошить factory image через balenaEtcher
indicator-hd-base-YYYYMMDD.img.gz → SD-карта

# 2. Положить WiFi credentials (опционально; ssid/psk — в прямых кавычках, сеть 2.4 GHz):
cat > /Volumes/boot/wpa_supplicant.conf << 'EOF'
ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev
update_config=1
country=RU

network={
    ssid="YOUR_SSID"
    psk="YOUR_PASSWORD"
}
EOF

# 3. Вставить SD-карту в Pi, включить питание
#    Старт 1: first_boot.sh → hostname + SSH keys + machine-id + WiFi seed → /data + overlayfs ON + reboot
#    Старт 2: устройство готово к работе (~2 мин от включения)
```

После загрузки устройство доступно по `indicator-hd-<serial>.local`.

### Обновление ПО на production устройстве

```bash
just pi::update-start    # overlayfs OFF → reboot (rootfs writable)
just pi::deploy          # rsync бинари + конфиги (config.txt — отдельно: just pi::deploy-boot)
just pi::restart         # проверить что работает
just pi::update-finish   # overlayfs ON → reboot (rootfs protected)
```

### Замена фонового видео

Вставить USB-носитель с `.mp4` файлами (H.264 MP4, уровень ≤ 4.1) — устройство обработает и заменит видео автоматически. Подробности: [`MEDIA_GUIDE.md`](docs/MEDIA_GUIDE.md).

---

## Архитектура

```mermaid
flowchart TD
    STM32["STM32 MCU\n(несущая плата)"]
    USB["USB-носитель\n(MP4-файлы)"]
    IND["indicator\n(демон, poll loop)"]
    MI["media-ingest\n(демон, CAP_SYS_ADMIN)"]
    FIFO["FIFO IPC\n/run/indicator/\nmedia_status.fifo"]
    DMX["DispmanX\n+ omxplayer"]
    HDMI["HDMI-дисплей\n1080×1920"]
    AUDIO["hifiberry-dac\n(I2S)"]
    DATA["/data/videos/\noutput.mp4"]

    STM32 -->|"UART 115200 8N1\n/dev/serial0"| IND
    USB -->|inotify /dev| MI
    MI -->|"mount + ffmpeg -c copy"| DATA
    DATA --> DMX
    MI -->|"write(status)"| FIFO
    FIFO -->|"POLLIN FD_FIFO"| IND
    IND --> DMX
    IND -->|"aplay"| AUDIO
    DMX --> HDMI
```

**Два systemd-сервиса:**

`indicator.service` — основной демон. Однопоточный `poll()` на пяти fd: `uart · fifo · signalfd · timerfd · notif_timerfd`. DispmanX-рендерер с font renderer (CalSans260), аудио-pthread, omxplayer как supervised child.

`media-ingest.service` — USB ingest демон. inotify на `/dev`, `mount(2)`, `ffmpeg -c copy`, `rename()`. Изолирован от indicator — общается только через FIFO.

### Файловая система

```mermaid
graph LR
    P1["/boot\nFAT32 p1\nядро, overlayfs hook\nWiFi seed"]
    P2["\n/ext4 p2\nrootfs\noverlafs в production"]
    P3["/data\next4 p3\nwritable, persistent"]

    P3 --- CFG["/data/pi_nku_configs/\nnku_scheme.toml\nvideo.toml\nrenderer.toml\npi_scheme.toml"]
    P3 --- RES["/data/resources/\nPNG-спрайты"]
    P3 --- SND["/data/sounds/\nWAV-файлы"]
    P3 --- VID["/data/videos/\noutput.mp4"]
```

### Z-слои DispmanX

| Z | Слот | Содержимое | Размер |
|---|---|---|---|
| 1 | omxplayer | фоновое видео | 1080×1920 |
| 2 | `SPRITE_BACKGROUND` | `BACK.png` | 1080×1920 |
| 3 | `SPRITE_MODE` | иконка режима | 1080×1920 |
| 4 | `SPRITE_WEIGHT` | грузоподъёмность | 237×59 |
| 4 | `SPRITE_DIGIT_LEFT` | цифра этажа (CalSans260) | 450×239 |
| 4 | `SPRITE_DIGIT_RIGHT` | резерв (F6) | — |
| 4 | `SPRITE_ARROW` | стрелка up/down | 172×191 |
| 5 | `SPRITE_NOTIFICATION` | уведомление | 1080×270 |

---

## Разработка

### Требования (хост)

- macOS или Linux
- Docker Desktop ≥ 24.0
- `just` ≥ 1.36.0
- SSH-ключ для Pi

### Первичная настройка

```bash
git clone --recurse-submodules <repo>
cd lift-indicator-hd

# Первичная настройка хоста (SSH + sysroot + Docker image)
just pi::bootstrap

# Открыть в VSCode → "Reopen in Container"
# Devcontainer: Ubuntu 22.04 + LLVM 17 + zig 0.13.0 + Pi sysroot

# Новая плата: подготовить SD (3 раздела + /boot), прошить, загрузить
just pi::prepare-image wpa_supplicant.conf   # .env: PI_HOST=raspberrypi.local
just pi::setup-ssh
# Первичная настройка Pi (пакеты, ALSA, /data на p3, WiFi на /data, локаль)
just pi::setup-pi
just pi::deploy-full
just pi::enable-services
```

Сборка golden image целиком (снимок → cleanup → проверка) — в [`DEV_ARCH.md`](docs/DEV_ARCH.md#golden-image-factory-image).

### Сборка и деплой

```bash
# Собрать (внутри devcontainer)
just build::pi              # indicator + media_ingest + uart_rx_dump + notif_test
just build::test            # unit-тесты на хосте (ASan + UBSan)

# Задеплоить (с хоста)
just pi::deploy             # бинари + конфиги + systemd
just pi::deploy-full        # + ресурсы + звуки + видео (первый деплой)
just pi::restart

# Мониторинг
just pi::logs               # journalctl indicator -f
just pi::logs-ingest        # journalctl media-ingest -f
just pi::status             # статус обоих сервисов
just pi::test-smoke         # быстрая проверка после деплоя
just pi::check-resources    # валидация всех ресурсов на устройстве
```

### Кросс-компилятор

`zig cc -target arm-linux-gnueabihf -mcpu=cortex_a53` — компилирует crt-объекты точно под Cortex-A53 с hard-float ABI. Стандартный `arm-linux-gnueabihf-gcc` тоже работает на A53 (ARMv7 Thumb-2 легален), но zig обеспечивает однородность инфраструктуры с веткой `dev-pi`.

---

## Структура репозитория

```bash
.
├── src_indicator/          ← C-код демона indicator
│   ├── main.c              ← composition root: инициализация, poll loop, cleanup
│   ├── app_handlers.c      ← on_uart_frame, on_media_status, on_watchdog_tick
│   ├── app_render.c        ← renderer_apply_elevator/mode/dispatch
│   ├── app_private.h       ← общие типы app_t / stats_t, forward-объявления
│   ├── protocol/           ← UART parser, types, CRC-16
│   ├── domain/             ← floor decoder, sound map, state machine
│   ├── config/             ← TOML конфиг-загрузчик
│   ├── audio/              ← pthread audio player (aplay)
│   ├── player/             ← omxplayer supervisor
│   ├── transport/          ← UART transport (termios)
│   ├── media/              ← FIFO IPC (indicator side)
│   └── renderer/           ← renderer.h (публичный интерфейс)
├── src_media_ingest/       ← C-код демона media-ingest
│   ├── main.c              ← конечный автомат, poll loop
│   ├── usb_watcher.c/.h    ← inotify /dev
│   ├── mounter.c/.h        ← mount(2) / umount2
│   ├── ffmpeg_runner.c/.h  ← find MP4, spawn ffmpeg, rename
│   └── status_pipe.c/.h    ← запись статуса в FIFO
├── platform/dispmanx/      ← DispmanX renderer + font renderer (bcm_host)
│   ├── renderer_impl.c     ← реализация слотов, ресурсов, fast_update
│   ├── font_renderer.c/.h  ← адаптер CalSans260 → DispmanX буфер
│   └── fonts/              ← CalSans260 (RLE ARGB8888, 260pt)
├── tests/                  ← Unity unit-тесты (6 суитов)
├── tools/                  ← uart_rx_dump, notif_test
├── scripts/                ← setup_pi.sh, first_boot.sh, smoke_test.sh, …
├── just/                   ← build.just, pi.just, ci.just
├── third_party/
│   └── config_toolset/     ← pi_nku_sync, pi_nku_menu (Rust, submodule)
├── *.toml                  ← nku_scheme, renderer, video, pi_scheme, menu_style
├── *.service               ← systemd units
└── docs/                   ← MASTER_PLAN_HD, отчёты фаз, гайды, чеклисты испытаний
```

---

## Документация

| Документ | Описание |
|---|---|
| [`MASTER_PLAN_HD.md`](docs/plan/MASTER_PLAN_HD.md) | Архитектурные решения, Z-слои, roadmap фаз F0–F6 |
| [`MEDIA_GUIDE.md`](docs/MEDIA_GUIDE.md) | Замена фонового видео через USB, требования к формату |
| [`SYSTEMD_FLOW.md`](docs/SYSTEMD_FLOW.md) | Граф systemd-юнитов, порядок старта, overlayfs |
| [`MEDIA_INGEST_FLOW.md`](src_media_ingest/MEDIA_INGEST_FLOW.md) | Конечный автомат media-ingest, FIFO протокол |
| [`FLASHING_GUIDE.md`](docs/FLASHING_GUIDE.md) | Прошивка SD-карты на производстве (balenaEtcher) |
| [`DEV_ARCH.md`](docs/DEV_ARCH.md) | Рабочее окружение, toolchain, деплой, golden image, типичные проблемы |
| [`RPI_SYSROOT.md`](build-env/RPI_SYSROOT.md) | Что копируется с Pi для сборки и как подключается |
| [`platform/README.md`](platform/README.md) | DispmanX renderer, Z-слои, font renderer |
| [`src_indicator/README.md`](src_indicator/README.md) | Event loop, poll fd, инициализация, watchdog, signalfd |
| [`src_indicator/audio/README.md`](src_indicator/audio/README.md) | Аудио-очередь, ALSA-стек, P-36 |
| [`tools/README.md`](tools/README.md) | `uart_rx_dump`, `notif_test` |
| [`docs/plan/v_hd_phases/`](docs/plan/v_hd_phases/) | Отчёты фаз F0–F3 |
| [`docs/tests/hd/`](docs/tests/hd/) | Чеклист интеграционных испытаний 2W |

---

## Roadmap

```mermaid
gantt
    dateFormat  YYYY-MM-DD
    axisFormat  F%e

    section Завершено
    F0 Toolchain + сборка      :done, f0, 2026-06-01, 2d
    F1 Bring-up устройства     :done, f1, after f0, 2d
    F2 FullHD renderer + fonts :done, f2, after f1, 7d
    F3 FullHD video + ingest   :done, f3, 2026-06-20, 4d
    F4 Audio + испытания       :done, f4, after f3, 2d

    section В процессе
    F5 Factory image           :active, f5, 2026-10-21, 3d

    section Далее
    F6 Trunk-унификация        :f6, after f5, 3d
```

| Фаза | Статус | Содержание |
|---|---|---|
| F0 | ✅ | Toolchain `cortex_a53`, zig wrapper, CMake, сборка |
| F1 | ✅ | Bring-up 2W, UART, DispmanX, overlayfs |
| F2 | ✅ | FullHD renderer, font renderer CalSans260, layout, рефакторинг main.c |
| F3 | ✅ | 1080p video loop, media-ingest, температурный контроль |
| F4 | ✅ | Испытания на объекте; P-36 закрыт, smoke 26/26 |
| F5 | 🔄 | Конвейер образа готов (`just pi::prepare/cleanup/inspect/shrink-image`); снимок эталонной платы — впереди |
| F6 | ⏳ | Унификация `dev-pi` и `dev-pi2w` в один trunk |

---

## Лицензия

Код проекта: MIT.
`platform/dispmanx/` — частично код Andrew Duncan, MIT.
`tests/unity/` — Unity Test Framework, MIT.
`third_party/config_toolset/` — см. лицензию submodule.
