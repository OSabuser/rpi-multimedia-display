# RPi Multimedia Display

Встраиваемая система отображения состояния лифта на базе Raspberry Pi Zero W.
Получает данные от STM32 по UART, отображает этаж и режим работы на HDMI-дисплее,
воспроизводит голосовые объявления и позволяет обновлять фоновое видео с USB-носителя.

---

## Возможности

- **Дисплей** — DispmanX-оверлей (7 слотов) поверх фонового видео: цифры этажа, стрелка направления, иконки нештатных режимов (9 режимов), диспетчерская связь, информационные уведомления
- **Аудио** — голосовые объявления этажей (в т.ч. составные: 21–49, П1–П9, −1..−9), звуки событий, фоновая музыка; приоритетная очередь с вытеснением
- **USB Media Ingest** — замена фонового видео с FAT32/exFAT/ext4 носителя; поддержка склейки нескольких MP4-файлов; индикация прогресса на экране
- **Надёжность** — read-only rootfs (overlayfs), watchdog, автоперезапуск через systemd, keepalive для VideoCore IV
- **Provisioning** — factory image: уникальный hostname из SoC serial, SSH keys, WiFi seed из `/boot/`, TUI-меню настройки параметров MCU

---

## Оборудование

| Компонент | Модель |
|---|---|
| SBC | Raspberry Pi Zero W Rev 1.1 (ARM1176JZF-S · ARMv6ZK) |
| ОС | Raspbian Buster Lite (Debian 10) |
| Дисплей | HDMI, 600×1024 |
| Аудио | MAX98357A (Adafruit Speaker Bonnet, I2S) |
| MCU | STM32 на несущей плате, UART `/dev/ttyAMA0`, 115200 8N1 |
| Накопитель | SD-карта ≥16 GB (3 раздела: `/boot` · rootfs · `/data`) |

---

## Быстрый старт

### Прошивка нового устройства

```bash
# 1. Прошить factory image через balenaEtcher
indicator-base-YYYYMMDD.img.gz → SD-карта

# 2. Положить WiFi credentials (опционально):
cat > /Volumes/boot/wpa_supplicant.conf << 'EOF'
country=RU
ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev
network={
    ssid="YOUR_SSID"
    psk="YOUR_PASSWORD"
}
EOF

# 3. Вставить SD-карту в Pi, включить питание
#    Старт 1: first_boot.sh → hostname + SSH + WiFi + overlayfs ON + reboot
#    Старт 2: устройство готово к работе (~2 мин от включения)
```

После загрузки устройство доступно по `indicator-<serial>.local`.

### Обновление ПО на production устройстве

```bash
just pi::update-start    # overlayfs OFF → reboot (rootfs writable)
just pi::deploy          # rsync бинари + конфиги
just pi::restart         # проверить что работает
just pi::update-finish   # overlayfs ON → reboot (rootfs protected)
```

### Замена фонового видео

Вставить USB-носитель с `.mp4` файлами (H.264 MP4) — устройство обработает и заменит видео автоматически. Подробности: [`docs/MEDIA_GUIDE.md`](docs/MEDIA_GUIDE.md).

---

## Архитектура

```bash
  Несущая плата
  ┌──────────────┐      UART (115200 8N1)
  │  STM32 MCU   │ ─────────────────────────────────────────────────┐
  └──────────────┘                                                   │
                                                                     ▼
  USB-носитель                                            ┌─────────────────────┐
  (MP4-файлы) ──► media-ingest ──► FIFO ──► indicator ──► DispmanX + omxplayer
                  (демон,            IPC    (демон,               │
                  CAP_SYS_ADMIN)            poll loop)            │
                       │                        │           HDMI-дисплей
                  /data/videos/           aplay/amixer        600×1024
                  output.mp4                    │
                                          MAX98357A (I2S)
                                          Динамик
```

**Два systemd-сервиса:**

`indicator.service` — основной демон. Однопоточный `poll()` на `uart_fd · fifo_fd · signalfd · timerfd`. DispmanX-рендерер, аудио-pthread, omxplayer как supervised child.

`media-ingest.service` — USB ingest демон. inotify на `/dev`, mount(2), ffmpeg `-c copy`, rename(). Изолирован от indicator; общается только через FIFO.

**Файловая система:**

```bash
/boot/          ← FAT32 (p1): ядро, overlayfs hook, WiFi seed
/               ← ext4 (p2): rootfs, защищён overlayfs в production
/data/          ← ext4 (p3): writable, persistent
  ├── pi_nku_configs/   ← TOML-конфиги (bind → ~/indicator/pi_nku_configs/)
  ├── resources/        ← PNG-ресурсы (bind → ~/indicator/resources/)
  ├── sounds/           ← WAV-файлы   (bind → ~/indicator/sounds/)
  └── videos/           ← output.mp4  (bind → ~/indicator/videos/)
```

Подробная архитектура event loop и ALSA стека: [`docs/DEV_ARCH.md`](docs/DEV_ARCH.md).

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
cd rpi-multimedia-display

# Первичная настройка хоста (SSH + sysroot + Docker image)
just pi::bootstrap

# Открыть в VSCode → "Reopen in Container"
# Devcontainer: Ubuntu 22.04 + LLVM 17 + zig 0.13.0 + Pi sysroot

# Первичная настройка Pi (пакеты, ALSA, /data layout, fstab)
just pi::setup-pi
just pi::enable-services
```

### Сборка и деплой

```bash
# Собрать (внутри devcontainer)
just build::pi              # indicator + media_ingest + uart_rx_dump
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

`zig cc -target arm-linux-gnueabihf -mcpu=arm1176jzf_s` — компилирует crt-объекты точно под ARM1176JZF-S. Стандартный `arm-linux-gnueabihf-gcc` содержит ARMv7 Thumb-2 объекты → segfault до `main()` на ARMv6.

---

## Структура репозитория

```bash
.
├── src_indicator/          ← C-код демона indicator
│   ├── main.c              ← event loop, composition root
│   ├── protocol/           ← UART parser, types, CRC-16
│   ├── domain/             ← floor decoder, sound map, state machine
│   ├── config/             ← TOML конфиг-загрузчик
│   ├── audio/              ← pthread audio player
│   ├── player/             ← omxplayer supervisor
│   ├── transport/          ← UART transport
│   ├── media/              ← FIFO IPC (indicator side)
│   └── renderer/           ← renderer.h (интерфейс)
├── src_media_ingest/       ← C-код демона media-ingest
│   ├── main.c              ← конечный автомат, poll loop
│   ├── usb_watcher.c/.h    ← inotify /dev
│   ├── mounter.c/.h        ← mount(2) / umount2
│   ├── ffmpeg_runner.c/.h  ← find MP4, spawn ffmpeg, rename
│   └── status_pipe.c/.h    ← запись статуса в FIFO
├── platform/dispmanx/      ← DispmanX renderer (bcm_host, Andrew Duncan MIT)
├── tests/                  ← Unity unit-тесты (6 суитов)
├── tools/                  ← uart_rx_dump, notif_test
├── deploy/                 ← ресурсы, звуки, конфиги, systemd units, скрипты
│   ├── resources/          ← PNG: chars, arrows, modes, weights, notifications
│   ├── sounds/             ← WAV: события, анонсы этажей, музыка
│   ├── configs/            ← nku_scheme.toml, renderer.toml, video.toml, …
│   └── systemd/            ← *.service, indicator.target
├── scripts/                ← setup_pi.sh, first_boot.sh, smoke_test.sh, …
├── just/                   ← build.just, pi.just, ci.just
├── build-env/              ← Dockerfile, Pi sysroot (.gitignore)
├── third_party/
│   └── config_toolset/     ← pi_nku_sync, pi_nku_menu (Rust, submodule)
└── docs/
    ├── DEV_ARCH.md         ← рабочее окружение, toolchain, just-команды
    ├── MEDIA_GUIDE.md      ← руководство по обновлению видео через USB
    ├── plan/
    │   ├── MASTER_PLAN_V1.md   ← архитектурные решения v1
    │   └── v1_phases/          ← отчёты фаз 0–7, Phase Deploy, Phase 6
    └── test/
        └── v1/                 ← интеграционные чеклисты
```

---

## Документация

| Документ | Описание |
|---|---|
| [`docs/DEV_ARCH.md`](docs/DEV_ARCH.md) | Рабочее окружение, toolchain, ALSA стек, UART протокол, all just-команды |
| [`docs/MEDIA_GUIDE.md`](docs/MEDIA_GUIDE.md) | Замена фонового видео через USB, требования к формату, сценарии |
| [`CHANGELOG.md`](CHANGELOG.md) | История релизов |
| `docs/plan/MASTER_PLAN_V1.md` | Архитектурные решения, UART протокол v1, roadmap |
| `docs/test/v1/` | Интеграционные чеклисты (~107 тестов) |

---

## Roadmap (v2.0.0)

- **Phase 8 — Font renderer**: замена PNG-спрайтов цифр на кастомный C-рендерер; устранение slow path стрелки (P-29)
- **Phase 9 — Полировка**: рефакторинг `main.c`, fix P-24 (`omxplayer --no-dbus`), CI/CD pipeline
- **Future**: GPU encode без конкуренции с omxplayer за VideoCore IV

Известные ограничения текущей версии: [`CHANGELOG.md → Known Issues`](CHANGELOG.md#known-issues).

---

## Лицензия

Код проекта: MIT.  
`platform/dispmanx/layers/` — код Andrew Duncan, MIT.  
`tests/unity/` — Unity Test Framework, MIT.  
`third_party/config_toolset/` — см. лицензию submodule.
