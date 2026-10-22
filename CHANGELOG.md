# Changelog

Формат основан на [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Версионирование следует [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased] — ветка `dev-pi2w` (Lift Indicator HD, Pi Zero 2W)

Целевое устройство: Raspberry Pi Zero 2W · Cortex-A53 (ARMv8-A 32-bit) · Debian Buster Lite armhf · 1080×1920.
Фазы F0–F4 закрыты (см. `docs/plan/v_hd_phases/`, `docs/plan/MASTER_PLAN_HD.md`), F5 в работе.

### Added

- Toolchain `arm-zig-cc-a53` (`-mcpu=cortex_a53`, hard-float) и `cmake/Toolchain-RPiZero2W.cmake` (F0)
- FullHD renderer 1080×1920; цифры этажа через font renderer CalSans260 вместо PNG `chars/` (F2)
- `scripts/prepare_base_image.sh`: 3 раздела на чистом Buster до первой загрузки
  (p2 8G, p3 4G `LABEL=data`), `cmdline.txt`/`config.txt` из `deploy/boot/`, проверка `WPA_CONF`
- `scripts/cleanup_image.sh`: снимок эталонной платы → factory image (проверки fstab и first_boot,
  сброс hostname, SSH host keys, machine-id, WiFi, логов; zerofree; обрезка хвоста после p3)
- `scripts/inspect_image.sh`, `scripts/shrink_image.sh` (перенесены из `dev-pi`)
- `just pi::` группа `image`: `prepare-image`, `backup-image` (только съёмный диск, с подтверждением),
  `cleanup-image`, `inspect-image`, `shrink-image`; рецепт `deploy-boot` (config.txt → /boot)
- `setup_pi.sh`: `/data` на p3 (`LABEL=data` до bind-монтов), WiFi на `/data` + bind-mount,
  `wpa_supplicant` enabled (P-38), `raspberrypi-net-mods` masked, `first_boot_done` на эталонной плате,
  локаль ru_RU.UTF-8 + консольный шрифт CyrSlav Terminus 14x28, overlayfs OFF
- `first_boot.sh`: hostname `indicator-hd-<serial>`, WiFi seed `/boot/wpa_supplicant.conf` → `/data`

### Fixed

- P-36: контрол softvol `PCM` отсутствовал до первого воспроизведения — `alsactl store` в `setup_pi.sh`,
  восстановление `alsa-restore.service`
- `deploy/boot/config.txt` без финального `\n`: `enable_overlayfs` склеил бы `initramfs …`
  с `dtparam=watchdog=on` (не стартовали бы watchdog и overlayfs)
- `cmdline.txt` штатного Buster: `console=serial0,115200` слал лог ядра в UART STM32 (убран при prepare)
- `setup_pi.sh`: apt неинтерактивен (скрипт идёт через stdin); ожидание NTP и проверка кандидатов пакетов;
  идемпотентные bind-монты
- `shrink_image.sh`: зависание на подтверждении `parted resizepart`
- `smoke_test.sh`, `cleanup_image.sh`: ложные провалы `cmd | grep -q` под `pipefail` (SIGPIPE)

### Changed

- `PI_HOST` по умолчанию — `raspberrypi.local` (свежая плата); production — `indicator-hd-<serial>.local`
- Документация приведена к Zero 2W: `DEV_ARCH.md`, `RPI_SYSROOT.md`, `SYSTEMD_FLOW.md`, `FLASHING_GUIDE.md`,
  README модулей; дубли мастер-планов удалены, план — `docs/plan/MASTER_PLAN_HD.md`

---

## [1.0.0] — 2026-06-10

Первый стабильный релиз. Система полностью функциональна и прошла
интеграционные испытания (~107 тестов) на целевом оборудовании.

Фабричный образ: `indicator-base-20260610.img.gz`  
Целевое устройство: Raspberry Pi Zero W Rev 1.1 · ARMv6 · Debian Buster Lite

---

### Added

> см. docs/plan/v1_phases/

#### Ядро системы — indicator

##### Протокол и парсер (Phase 1, 2)

- Бинарный парсер UART-фреймов от STM32: `SOF=0xAA · LEN · OPCODE · DATA · CRC16 · EOF=0xBB`
- Opcode `0xDA` — статус лифта (этаж, стрелка, режим, звук); opcode `0xAA` — диспетчерская связь
- CRC-16 little-endian; кольцевой буфер; poll-based event loop с signalfd/timerfd
- Декодирование этажей: NORMAL (1–49+), BASEMENT (П/П1–П9), NEGATIVE (−1..−9), UNKNOWN
- Конфигурация через TOML: `nku_scheme.toml`, `pi_scheme.toml`, `video.toml`, `renderer.toml`
- Дефолты при отсутствии конфига; без внешних парсерных зависимостей

##### Отображение — DispmanX renderer (Phase 4)

- 7 sprite-слотов с Z-порядком поверх omxplayer видео:
  - Z=2 `BACKGROUND`: `BACK.png` 600×1024
  - Z=3 `MODE`: иконки нештатных режимов (9 режимов + 2 dispatch-состояния)
  - Z=4 `WEIGHT`: иконка грузоподъёмности из `nku_scheme.toml`
  - Z=4 `DIGIT_LEFT` / `DIGIT_RIGHT`: цифры этажа, fast-update без мигания
  - Z=4 `ARROW`: стрелка направления
  - Z=5 `NOTIFICATION`: информационная полоса 600×150 px, y=874
- `fast_update` для цифр: `changeSourceImageLayer` без пересоздания ресурса
- `renderer_keepalive()` каждые 30 с — предотвращает переход VideoCore IV в dormant-режим
- Watchdog-лог каждые 30 с: статистика фреймов + PID omxplayer

##### Видеоплеер (Phase 3)

- omxplayer как supervisor-child через `posix_spawn` + `POSIX_SPAWN_SETPGROUP`
- `killpg(SIGKILL)` для корректного завершения bash-обёртки omxplayer
- SIGCHLD watchdog: автоматический рестарт < 2 с при падении
- `video_player_replace()`: атомарная замена видео без остановки event loop

##### Аудио (Phase 5)

- Однопоточный pthread-worker с приоритетной очередью глубиной 3:
  - `CRITICAL=0`: `overload.wav`, `fire.wav`, `g_double.wav`
  - `FLOOR=1`: анонс этажа (до 3 WAV-файлов: составной номер + `floor.wav`)
  - `MOVEMENT=2`: `up.wav`, `down.wav`, `closing.wav`, `opening.wav`, `button.wav`
  - `MUSIC=3`: фоновая музыка `mus1–mus7.wav` (циклически)
- Вытеснение по приоритету с немедленным SIGKILL текущего aplay
- Музыкальный lifecycle: запускается автоматически после UP/DOWN; отменяется DING и нештатными режимами
- ALSA softvol-стек: MAX98357A (S32_LE/48kHz) через dmix; `i2s-silence.service` как I2S keepalive
- Раздельные настройки громкости: звуки событий и фоновая музыка

##### IPC и уведомления (Phase 7)

- Именованный FIFO `/run/indicator/media_status.fifo` для получения статусов от `media-ingest`
- POLLHUP → автоматическое переоткрытие при перезапуске media-ingest
- 8 PNG-уведомлений (600×150, Z=5): found / processing / success / no_video / eject / error / no_mcu / mcu_ok
- Уведомление `notif_no_mcu.png` при `setup_status ∈ {push_failed, pull_failed}`
- Уведомление `notif_mcu_ok.png` при первом валидном UART-фрейме + timerfd авто-скрытие через 4 с

#### Демон media-ingest (Phase 7)

- Отдельный сервис с `CAP_SYS_ADMIN`; изолирован от indicator
- inotify на `/dev` для обнаружения USB-носителей (`sd[a-z][0-9]`)
- Монтирование через `mount(2)` с перебором ФС: vfat → exfat → ext4; `MS_RDONLY|MS_NOEXEC|MS_NOSUID|MS_NODEV`
- Поиск `.mp4`/`.MP4` файлов; фильтр скрытых файлов (`._`-prefix, macOS AppleDouble)
- Конкатенация MP4 через ffmpeg `-c copy` (без перекодировки); лексикографический порядок
- Атомарный `rename(output_tmp.mp4 → output.mp4)` — omxplayer никогда не видит неполный файл
- Конечный автомат: ST_IDLE → ST_WAIT_PROCESSING → ST_FFMPEG_RUNNING → ST_WAIT_EJECT → ST_WAIT_UMOUNT
- Тайминги: вставка→обработка 1 с, результат→eject 3 с, eject→umount 5 с
- Корректная обработка USB remove в любом состоянии: killpg + lazy umount + MEDIA_CLEAR

#### Инфраструктура и производство (Phase Deploy, Phase 6)

##### Загрузочная последовательность

- `indicator-firstboot.service`: уникальный hostname из SoC serial, SSH keys, machine-id, WiFi seed из `/boot/wpa_supplicant.conf`, raspi-config `enable_overlayfs`
- `indicator-setup.service` (tty1): fbi splash → `pi_nku_sync pull` → `pi_nku_menu` (TUI, 30 с) → `pi_nku_sync push` → `/data/setup_status`
- Ядро на tty3 (`console=tty3 quiet loglevel=3`); tty1 — только для меню

##### Защита rootfs (overlayfs)

- SD-карта: 3 раздела — p1 FAT32 (`/boot`), p2 ext4 (rootfs, protected), p3 ext4 (`/data`, writable, `LABEL=data`)
- Overlayfs через `raspi-config nonint enable/disable_overlayfs` — rootfs read-only в production
- `/data/` — persistent раздел для конфигов, ресурсов, звуков, видео, WiFi credentials
- Bind-монты: `/data/{pi_nku_configs,resources,sounds,videos}` → `/home/pi/indicator/*`
- WiFi credentials в `/data/wpa_supplicant.conf` + bind-mount → `/etc/wpa_supplicant/wpa_supplicant.conf`

##### Workflow обновления ПО**

```bash
just pi::update-start   # disable_overlayfs → reboot (rootfs writable)
just pi::deploy         # rsync бинари + конфиги
just pi::restart        # проверка
just pi::update-finish  # enable_overlayfs → reboot (rootfs protected)
```

##### Toolchain

- Кросс-компиляция: `zig cc -target arm-linux-gnueabihf -mcpu=arm1176jzf_s` (ARMv6, hard-float)
- Docker devcontainer: Ubuntu 22.04 + LLVM 17 + zig 0.13.0
- libpng16 динамическая линковка (`.so`) — статическая `.a` из Debian armhf несовместима с ARMv6
- Task runner: `just` (группы `build::`, `pi::`)
- Unit-тесты: Unity framework, 6 тест-суитов, запуск на хосте с ASan/UBSan

---

### Known Issues

| ID | Описание | Влияние | Приоритет |
|---|---|---|---|
| P-24 | `dbus-daemon` (запускается omxplayer) не попадает в process group → cosmetic timeout ~5 с при `systemctl stop indicator` | Только при ручной остановке; на работу не влияет | Низкий |
| P-29 | `ARROW`-слот использует slow path (destroy + recreate) при каждом появлении | Незначительное мигание стрелки; визуально приемлемо | Низкий |
| В-12 | USB-носитель, вставленный **до** старта `media-ingest`, не обрабатывается (inotify не видит уже существующие устройства) | Только при нетипичном порядке старта; обходится извлечением и повторной вставкой | Низкий |

---

### Roadmap (v2.0.0)

- **Phase 8** — Font renderer: замена PNG-цифр на кастомный C font renderer (устранение P-29 как побочный эффект)
- **Phase 9** — Полировка: рефакторинг `main.c` (extract `render_dispatch.c` + `uart_handler.c`), P-24 (omxplayer `--no-dbus`), CI/CD pipeline
- **Future** — GPU encode: исследование независимого контекста ffmpeg h264_omx без конкуренции с omxplayer за `/dev/vchiq`

---

[1.0.0]: https://github.com/your-org/rpi-multimedia-display/releases/tag/v1.0.0
