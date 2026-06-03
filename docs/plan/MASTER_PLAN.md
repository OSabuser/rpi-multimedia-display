# Lift Indicator — Мастер-план

> **Статус:** Фазы 0–5, Phase Deploy и Phase Deploy Addendum — ЗАКРЫТЫ.
> Фаза 6 (Deploy v2: overlayroot, build\_image, factory-test) — В РАБОТЕ.
> **Стек:** C11 · DispmanX · omxplayer · ALSA · systemd · CMake · Docker · just
> **Компилятор:** `zig cc` (`arm-zig-cc` wrapper) — ARMv6, arm1176jzf\_s, hard-float
> **Устройство:** Raspberry Pi Zero W Rev 1.1 · ARM1176JZF-S · ARMv6ZK · Debian Buster

---

## 1. Все принятые решения

| Тема | Решение | Обоснование |
|---|---|---|
| ОС | Raspbian **Buster Lite** (CLI, без X11) | DispmanX и omxplayer работают без GUI; экономия ~100 MB RAM и ~20 с загрузки |
| Железо | **Raspberry Pi Zero W Rev 1.1** (ARM1176JZF-S, ARMv6ZK) | Уточнено в Фазе 1 — не 2W, а первый Zero W; ARMv6, не ARMv8 |
| Автозапуск | **systemd units** | Supervision, логи в journald, restart policy |
| Рендеринг | **DispmanX** (сохраняем) | Zero-overhead overlay поверх omxplayer на этом железе |
| Видеоплеер | **omxplayer** (сохраняем) | OpenMAX IL, минимальная нагрузка на CPU; заменить только при смене ОС |
| omxplayer надзор | `posix_spawn` + `POSIX_SPAWN_SETPGROUP` + `killpg(SIGKILL)` + SIGCHLD watchdog | SIGTERM не работает для bash-обёртки omxplayer; SIGKILL надёжен |
| VideoCore dormant | `renderer_keepalive()` — пустой DispmanX update каждые 30 с | P-28: VideoCore IV засыпает при отсутствии DispmanX-активности |
| libpng | Динамическая линковка `libpng16.so` (не .a) | P-27: `libpng16.a` из Debian armhf скомпилирована под ARMv7 → segfault на ARMv6 до `main()` |
| Аудио backend | **ALSA softvol + aplay** | MAX98357 не имеет HW volume в ALSA; softvol в asound.conf — легче PulseAudio |
| Аудио из кода | `posix_spawn("aplay")` в отдельном pthread | Нет shell overhead; latency ~30 ms вместо ~150 ms с `system()` |
| Аудио очередь | Приоритетная, глубина 3 | CRITICAL > FLOOR > MOVEMENT > MUSIC |
| notify | **Упраздняется** — SPRITE\_NOTIFICATION — слот в renderer | Нет spawn/pkill; нет отдельного `notify` процесса |
| Рендеринг цифр | **PNG сейчас**, font-renderer — Фаза 8 (low priority) | Все PNG одного размера → `changeSourceImageLayer` без мигания |
| BACK.png | RGBA PNG поверх видео, путь в конфиге | Статична сейчас, легко сменить в будущем |
| Конфиг | **TOML** (`nku_scheme.toml`, `pi_scheme.toml`, `video.toml`, `renderer.toml`) | `pizero.ini` — legacy, не используется |
| Конфиг — парсер | **Hand-written line parser** (без зависимостей) | inih не подключён; TOML-подмножество достаточно |
| Setup TUI | **pi\_nku\_menu** (Rust, pre-built) + **pi\_nku\_sync** (Rust, pre-built) | Внешние бинари из `config_toolset`; С-демон их только вызывает через `indicator-setup.service` |
| Task runner | **just** (mod build / mod pi) | `mod build` = devcontainer, `mod pi` = хост |
| Кросс-компилятор | **zig cc** (`arm-zig-cc` wrapper) | `arm-linux-gnueabihf-gcc` из Bookworm содержит crt*.o под glibc 2.34+ — segfault на Buster; zig компилирует crt точно под arm1176jzf\_s |
| Docker base | **Ubuntu 22.04 + LLVM 17 + zig 0.13.0** | Debian Buster в Docker — проблемы с arm64 (Rosetta); Ubuntu Jammy + official LLVM repo решает всё |
| Деплой | **rsync** (быстро) + **base image** (flash для новых устройств) | rsync для обновлений при разработке; образ для производства |
| Layout данных | **`/data/`** — writable раздел (сейчас директория, в Deploy v2 — ext4 раздел) | Bind-монты: `/data/*` → `/home/pi/indicator/*`; rootfs в будущем read-only |
| Boot splash | **fbi** (3 сек, через sudoers, в `run_setup.sh`) | Plymouth не установлен на Buster Lite; отдельный systemd service не работал надёжно |
| Консоль ядра | **tty3** (`console=tty3 quiet loglevel=3` в cmdline.txt) | Артефакты ядра/systemd на tty1 мешали TUI; tty1 — только для меню |
| KillMode | **`control-group`** в `indicator.service` | dbus-daemon (от omxplayer) вне pgroup; cgroup убивает его надёжно |
| Тесты | **Unity** (vendored, MIT) | Один .c/.h файл, компилируется везде |
| Код Andrew Duncan | Перенесён без изменений в `platform/dispmanx/layers/` | MIT, корректный, clang-tidy отключён для этой директории |

---

## 2. Архитектура системы

> SSH: `ssh -i ~/.ssh/id_ed25519 pi@indicator-01.local`

### 2.1 Граф systemd

```
multi-user.target
├── indicator-firstboot.service   [ConditionPathExists=!/data/first_boot_done]
│     first_boot.sh: уникальный hostname + SSH keys + machine-id
│           ↓ Before
├── indicator-setup.service       [oneshot, RemainAfterExit=yes, TTYPath=/dev/tty1]
│     run_setup.sh:
│       0. fbi splash.jpg (3 с)
│       1. pi_nku_sync --mode=pull     → /data/pi_nku_configs/
│       2. pi_nku_menu (TUI, 30 с)    → правка параметров
│       3. pi_nku_sync --mode=push    → конфиг → MCU
│       4. echo status → /data/setup_status
│           ↓ Wants + Before
├── i2s-silence.service           [After=sound.target, StartLimitBurst=20]
│     ExecStartPre: ждёт card 0 (до 15 с, I2S overlay успевает init)
│     ExecStart: aplay -D dmixer -f S32_LE /dev/zero  ← I2S keepalive
│           ↓ After/Wants
└── indicator.service             [Restart=always, RestartSec=2, KillMode=control-group]
      reads /data/setup_status
      ├── [poll loop]  uart_fd · fifo_fd · signalfd · timerfd
      ├── [pthread]    audio player (posix_spawn aplay, amixer)
      └── [child]      omxplayer  (posix_spawn, POSIX_SPAWN_SETPGROUP, SIGCHLD watchdog)

media-ingest.service              [Restart=on-failure, RestartSec=5]  ← Фаза 7
      inotify /dev → mount → ffmpeg → FIFO → indicator
```

### 2.2 Z-порядок DispmanX (финальный, Phase 4)

```
Z = 1   omxplayer           видео (--layer 1)
Z = 2   SPRITE_BACKGROUND   BACK.png, 600×1024, полупрозрачный RGBA
Z = 3   SPRITE_MODE         mode-иконка, 600×1024 (или скрыт при MODE_NORMAL)
Z = 4   SPRITE_WEIGHT       load_N.png, 237×59
Z = 4   SPRITE_DIGIT_LEFT   chars/N.png, 202×346   ← fast_update
Z = 4   SPRITE_DIGIT_RIGHT  chars/N.png, 202×346   ← fast_update
Z = 4   SPRITE_ARROW        arrows/up|down.png, 188×209
Z = 5   SPRITE_NOTIFICATION (Фаза 7 — не реализован)
```

**fast\_update:** DIGIT\_LEFT, DIGIT\_RIGHT используют `changeSourceImageLayer` (без мигания).
**ARROW:** при каждом появлении использует slow path (destroy + recreate) — P-29, будет исправлено в Deploy v2.

### 2.3 Главный цикл — однопоточный poll

```
poll()
  ├── uart_fd     → сборка бинарного фрейма → CRC16 → protocol_parse_frame()
  │                  → on_uart_frame()
  │                  │   opcode=0xDA: state_apply_frame() → renderer_apply_elevator()
  │                  │                                    → audio_player_play()
  │                  │   opcode=0xAA: state_apply_dispatch() → renderer_apply_dispatch()
  │                  │                                       → audio_player_cancel_music()
  │                  └   opcode=0xC0: LOG_DEBUG (служебный, игнорируется)
  │
  ├── fifo_fd     → media_status_t от media-ingest  ← Фаза 7
  │                  → renderer_show_png(SPRITE_NOTIFICATION, ...)
  │                  → video_player_replace() при MEDIA_STATUS_DONE
  │
  ├── signalfd    → SIGTERM/SIGINT → shutdown
  │               → SIGCHLD → video_player_check_and_restart()
  │
  └── timerfd     → watchdog tick каждые 30 с:
                      log статистика + renderer_keepalive() (P-28 fix)
```

### 2.4 Обновление DispmanX-слотов

```
DIGIT_LEFT, DIGIT_RIGHT → fast_update = true
  Первый раз: loadPng → createResourceImageLayer → addElementImageLayerOffset
  Обновление: loadPng → resource_write_data → changeSourceImageLayer
  Скрытие:    destroyImageLayer (→ initialized=0; P-29: будет ELEMENT_CHANGE_OPACITY=0)

BACKGROUND, WEIGHT, MODE, NOTIFICATION → fast_update = false
  Любое изменение: destroyImageLayer → loadPng → create → add

ARROW → slow path при каждом появлении (P-29, см. Deploy v2)
```

### 2.5 Аудио — приоритеты

```
AUDIO_PRIO_CRITICAL = 0   ← SOUND_OVERLOAD / SOUND_FIRE_ALARM / SOUND_DONT_WORK
AUDIO_PRIO_FLOOR    = 1   ← SOUND_DING (анонс этажа)
AUDIO_PRIO_MOVEMENT = 2   ← SOUND_UP / SOUND_DOWN / SOUND_CLOSING / SOUND_OPENING / SOUND_BUTTON
AUDIO_PRIO_MUSIC    = 3   ← фоновая музыка (mus1..mus7.wav)
AUDIO_PRIO_NONE     = 99  ← сентинель «ничего не играет»

Правило вытеснения: effective_prio = min(current_prio, min_prio_in_queue)
  new_prio < effective_prio → kill(aplay, SIGTERM) + clear queue + current_prio=NONE
  new_prio ≥ effective_prio → добавить в очередь (FIFO depth=3)

После UP/DOWN: music_wanted=1 → worker запускает mus<N>.wav
При MODE_ABNORMAL или dispatch активен: needs_music=0 (музыка не запускается)
При отмене: audio_player_cancel_music() → kill если играет, clear music_wanted
```

### 2.6 ALSA стек (финальный, Phase 5)

```
Чип:  MAX98357A (Adafruit Speaker Bonnet)
dtoverlay: googlevoicehat-soundcard  ← единственный поддерживаемый overlay

WAV (S16_LE/любая частота)
    └── aplay → pcm.!default
                    └── plug       ← конвертация S16_LE → S32_LE, ресемплинг → 48kHz
                        └── softvol  'PCM'   ← amixer sset 'PCM' N%  (lazy, 1 вызов при смене)
                            └── dmixer  (ipc_key=1024, ipc_perm=0666, S32_LE/48kHz/stereo)
                                └── speakerbonnet  hw:0
                                                   S32_LE / 48kHz / stereo

i2s-silence.service:
    aplay -D dmixer -f S32_LE /dev/zero  ← напрямую в dmix (без plug/softvol)
                                         ← устраняет щелчки при каждом звуке
```

⚠️ **dmix IPC deadlock (P-34):** при `kill(aplay, SIGTERM)` в момент удержания
dmix-семафора aplay завершается не освободив его → все последующие `aplay`/`amixer`
зависают навсегда. Лечится только `ipcrm` + рестарт. Обойдён переносом на `i2s-silence.service`
(прямой путь в dmix минует plug — меньше точек удержания).

### 2.7 Загрузочная последовательность

```
GPU bootloader      → чёрный экран (splash не поддерживается этой прошивкой)
Ядро + systemd      → tty3 (console=tty3 quiet loglevel=3 — не видно)
indicator-firstboot → уникальный hostname + SSH keys (только первый старт)
indicator-setup     → tty2: fbi splash.jpg (3 сек)
                    → tty1: чистый экран → pi_nku_sync pull → pi_nku_menu → push
                    → /data/setup_status: ok | pull_failed | push_failed | pending
indicator           → DispmanX init → omxplayer → рендерер → UART polling
```

---

## 3. Модули

### Платформонезависимые (тестируются на хосте)

| Модуль | Файл | Ответственность | Тесты |
|---|---|---|---|
| Config | `src/config/config.c/.h` | Парсинг nku\_scheme.toml, video.toml, renderer.toml, pi\_scheme.toml; дефолты | test\_config (21 case) |
| Protocol types | `src/protocol/types.h` | Enum'ы: `char_code_t`, `arrow_t`, `sound_t`, `indicator_mode_t`, `dispatch_state_t`, `parsed_frame_t` | — |
| Protocol parser | `src/protocol/parser.c/.h` | `protocol_parse_frame()` + `protocol_parse_payload()` + `protocol_parse_dispatch()`; CRC-16 little-endian; без malloc | test\_parser |
| Floor codec | `src/domain/floor.c/.h` | char codes → `floor_t` (NORMAL/BASEMENT/BASEMENT\_N/NEGATIVE/UNKNOWN) | test\_floor |
| Sound map | `src/domain/sound_map.c/.h` | `sound_map_resolve()` → `audio_sequence_t` с WAV-файлами | test\_sound\_map |
| State machine | `src/domain/state.c/.h` | `state_apply_frame()`, `state_apply_dispatch()` → `state_update_result_t` | test\_state |
| Renderer interface | `src/renderer/renderer.h` | API-контракт: `show_png`, `hide`, `keepalive`, `create`, `destroy`; без bcm\_host.h | — |

### Только Pi (не тестируются на хосте без устройства)

| Модуль | Файл | Ответственность |
|---|---|---|
| UART transport | `src/transport/uart.c/.h` | termios raw; кольцевой буфер; `protocol_parse_frame()` интеграция; `frame_ready_cb_t`; `uart_wrap_fd()` для тестов | test\_uart (pipe) |
| Audio player | `src/audio/audio.c/.h` | pthread + mutex + condvar; приоритетная очередь depth=3; posix\_spawn aplay; amixer volume; фоновая музыка | — (on-target) |
| Video player | `src/player/video_player.c/.h` | posix\_spawn omxplayer + POSIX\_SPAWN\_SETPGROUP; killpg(SIGKILL); SIGCHLD watchdog | — (on-target) |
| Media IPC | `src/media/media_ipc.c/.h` | Чтение статуса из FIFO (stub, Фаза 7) | — |
| DispmanX renderer | `platform/dispmanx/renderer_impl.c` | Реализует renderer.h через DispmanX API | — (on-target) |
| DispmanX layers | `platform/dispmanx/layers/` | Andrew Duncan (MIT) — imageLayer, loadpng; не изменяем | — |
| Main | `src/main.c` | Composition root; poll-цикл (uart/sig/timer); init/cleanup всех модулей; setup\_status | — |

### Отдельный демон media-ingest (Фаза 7 — stub)

| Файл | Ответственность |
|---|---|
| `src_media_ingest/main.c` | Точка входа (сейчас stub) |
| `src_media_ingest/usb_watcher.c/.h` | inotify на /dev (IN\_CREATE/IN\_DELETE) |
| `src_media_ingest/mounter.c/.h` | mount/umount, fallback /dev/sdX1 → /dev/sdX |
| `src_media_ingest/ffmpeg_runner.c/.h` | ffmpeg concat, atomic rename() |
| `src_media_ingest/status_pipe.c/.h` | Запись статуса в FIFO → indicator |

---

## 4. Структура репозитория

```
lift-indicator/
│
├── justfile                        ← корневой оркестратор (mod build, pi, ci)
├── bootstrap.sh                    ← уровень 0: just + SSH ключ + sysroot + Docker
├── .env.example                    ← шаблон (PI_HOST, PI_USER, PI_DIR, BUILD_DIR)
│
├── just/
│   ├── build.just                  ← devcontainer: тесты, Pi сборка, format
│   ├── pi.just                     ← хост: deploy, SSH, setup, logs, smoke
│   └── ci.just                     ← CI pipeline
│
├── cmake/
│   ├── Toolchain-RPiZeroW.cmake    ← arm-zig-cc + /opt/vc (ARMv6, arm1176jzf_s)
│   ├── Warnings.cmake              ← apply_warnings(target)
│   └── Sanitizers.cmake            ← ASan / UBSan
│
├── CMakeLists.txt
│
├── src/
│   ├── main.c
│   ├── config/         config.h / config.c
│   ├── protocol/       types.h · parser.h / parser.c
│   ├── domain/         floor.h/c · sound_map.h/c · state.h/c
│   ├── renderer/       renderer.h  (только интерфейс)
│   ├── transport/      uart.h / uart.c
│   ├── audio/          audio.h / audio.c
│   ├── player/         video_player.h / video_player.c
│   └── media/          media_ipc.h / media_ipc.c  (stub, Фаза 7)
│
├── src_media_ingest/               ← отдельный демон (stub main.c, Фаза 7)
│   └── main.c
│
├── platform/
│   └── dispmanx/
│       ├── CMakeLists.txt
│       ├── renderer_impl.c
│       └── layers/                 ← Andrew Duncan (MIT)
│
├── tests/
│   ├── CMakeLists.txt
│   ├── unity/           unity.h / unity.c (MIT, vendored)
│   ├── test_parser.c
│   ├── test_floor.c
│   ├── test_state.c
│   ├── test_sound_map.c
│   ├── test_config.c
│   └── test_uart.c
│
├── tools/
│   ├── uart_rx_dump.c              ← диагностическая утилита UART
│   └── CMakeLists.txt
│
├── build-env/
│   ├── Dockerfile                  ← Ubuntu 22.04 + LLVM 17 + zig 0.13.0 + /opt/vc
│   └── pi-sysroot/                 ← .gitignore; копируется fetch-sysroot + fetch-png-sysroot
│       ├── opt/vc/                 ← DispmanX, bcm_host
│       └── usr/                    ← libpng16.so + zlib.so + headers
│
├── deploy/
│   ├── systemd/
│   │   ├── indicator-firstboot.service
│   │   ├── indicator-setup.service
│   │   ├── indicator.service
│   │   ├── indicator.target
│   │   ├── i2s-silence.service         ← I2S keepalive (Phase 5, новый)
│   │   └── media-ingest.service
│   ├── configs/                    ← nku_scheme.toml, pi_scheme.toml, video.toml, renderer.toml, menu_style.toml
│   ├── resources/                  ← PNG (chars, arrows, modes, weights, BACK.png)
│   ├── sounds/                     ← WAV файлы
│   ├── video/
│   │   └── output.mp4              ← фоновое видео
│   ├── boot/
│   │   ├── config.txt              ← /boot/config.txt
│   │   └── splash.jpg              ← boot splash (600×1024)
│   ├── bin/                        ← pre-built Rust: pi_nku_sync, pi_nku_menu
│   └── tools/                      ← MUp-rpi0 (диагностика STM32)
│
├── scripts/
│   ├── setup_pi.sh                 ← первичная настройка Pi (пакеты, ALSA, /data, fstab)
│   ├── run_setup.sh                ← indicator-setup: splash → pull → menu → push → status
│   ├── first_boot.sh               ← уникальный hostname + SSH keys + machine-id
│   ├── smoke_test.sh               ← проверка после деплоя
│   └── check_resources.sh          ← валидация 79 ресурсов на устройстве
│
├── third_party/
│   └── config_toolset/             ← Rust workspace: pi_nku_sync, pi_nku_menu (external submodule)
│
└── docs/
    ├── DEV_ARCH.md                 ← рабочее окружение разработчика
    ├── RPI_SYSROOT.md              ← инструкция по sysroot
    ├── AUDIO_MODULE.md             ← техническая документация аудиомодуля (Phase 5)
    ├── indicator_checklist.md      ← интеграционный чек-лист ~76 тестов (Phase 5)
    └── plan/                       ← история фаз (phase reports)
        ├── MASTER_PLAN.md
        ├── PHASE_0_REPORT.md … PHASE_4_REPORT.md
        ├── PHASE_DEPLOY_PLAN.md
        ├── PHASE_DEPLOY_REPORT.md
        └── ADD_DEPLOY_REPORT.md
```

---

## 5. Just команды (полный справочник)

`just` без аргументов выводит все команды с описаниями.

### Хост — первичная настройка

| Команда | Что делает |
|---|---|
| `./bootstrap.sh` | Установить just, запустить `just pi::bootstrap` |
| `just pi::check-deps` | Проверить just, docker, ssh, rsync |
| `just pi::setup-ssh` | Сгенерировать SSH-ключ, скопировать на Pi |
| `just pi::fetch-sysroot` | Скопировать /opt/vc с Pi для Docker |
| `just pi::fetch-png-sysroot` | Скопировать libpng16.so + headers с Pi |
| `just pi::build-image` | Собрать Docker образ indicator-build |
| `just pi::bootstrap` | Всё выше в одну команду |
| `just pi::setup-pi` | Первичная настройка Pi (пакеты, ALSA, /data layout, bind mounts, fstab) |
| `just pi::migrate-data` | Одноразовая миграция существующей Pi на /data layout |
| `just pi::enable-services` | `systemctl enable` для всех indicator сервисов |
| `just pi::setup-gdbserver` | Установить gdbserver на Pi (один раз) |

### Devcontainer — сборка и тесты

| Команда | Что делает |
|---|---|
| `just build::test` | Host unit-тесты (Debug + ASan/UBSan) — 6 суитов |
| `just build::test-verbose` | То же, подробный вывод |
| `just build::pi` | Кросс-компиляция: indicator + media\_ingest + uart\_rx\_dump |
| `just build::pi-indicator` | Только indicator |
| `just build::pi-ingest` | Только media\_ingest |
| `just build::pi-dump` | Только uart\_rx\_dump |
| `just build::pi-debug` | Debug-сборка (-g3 -O0) для GDB |
| `just build::format` | Применить clang-format |
| `just build::check-format` | Проверить форматирование (CI) |
| `just build::clean` | Удалить build/ (с подтверждением) |

### Хост — деплой

| Команда | Что делает |
|---|---|
| `just pi::deploy` | Бинари + tools + Rust + скрипты + systemd + конфиги |
| `just pi::deploy-full` | deploy + ресурсы + звуки + splash + видео (первая установка) |
| `just pi::deploy-bin` | Только indicator + media\_ingest (быстро) |
| `just pi::deploy-tools` | uart\_rx\_dump + MUp-rpi0 |
| `just pi::deploy-rust` | pi\_nku\_sync + pi\_nku\_menu |
| `just pi::deploy-scripts` | Все sh-скрипты |
| `just pi::deploy-systemd` | systemd units + daemon-reload |
| `just pi::deploy-configs` | deploy/configs/ → /data/pi\_nku\_configs/ |
| `just pi::deploy-resources` | deploy/resources/ → /data/resources/ |
| `just pi::deploy-sounds` | deploy/sounds/ → /data/sounds/ |
| `just pi::deploy-splash` | deploy/boot/splash.jpg → Pi |
| `just pi::deploy-video` | deploy/video/output.mp4 → /data/videos/ |
| `just pi::deploy-debug` | indicator-debug для GDB |
| `just pi::check-resources` | Валидация 79 ресурсов на Pi |

### Хост — управление

| Команда | Что делает |
|---|---|
| `just pi::restart` | Перезапустить indicator.service |
| `just pi::restart-audio` | Перезапустить i2s-silence.service (при зависании dmix) |
| `just pi::start` | Запустить indicator.service |
| `just pi::stop` | Остановить indicator.service |
| `just pi::status` | Статус indicator + media-ingest |
| `just pi::logs` | Хвост логов indicator (следить в реальном времени) |
| `just pi::logs-ingest` | Хвост логов media-ingest |
| `just pi::logs-tail [n]` | Последние N строк обоих сервисов |
| `just pi::ssh` | SSH на Pi |
| `just pi::smoke` | Smoke test после деплоя |
| `just pi::test-audio` | Воспроизвести тестовый WAV на Pi |
| `just pi::test-video` | Сгенерировать тестовое видео ffmpeg → Pi |
| `just pi::dump` | uart\_rx\_dump (остановить indicator, слушать, поднять обратно) |
| `just pi::dump-passive` | uart\_rx\_dump без остановки indicator (только наблюдение) |
| `just pi::top` | CPU/RAM на Pi |
| `just pi::df` | Место на SD-карте |
| `just pi::backup-image /dev/diskN` | Создать .img.gz образа SD-карты |
| `just pi::gdbserver-start` | SSH-туннель + gdbserver на Pi |
| `just pi::gdbserver-stop` | Остановить gdbserver |
| `just pi::debug-output` | tail /tmp/gdbserver.log |

### Алиасы верхнего уровня

| Команда | Что делает |
|---|---|
| `just init` | = `just pi::bootstrap` |
| `just test` | = `just build::test` |
| `just ship` | docker run build::pi → deploy → check-resources → restart |

### CI

| Команда | Что делает |
|---|---|
| `just ci::pipeline` | check-format + host tests |
| `just ci::test` | Host unit-тесты |
| `just ci::check-format` | Проверка форматирования |

---

## 6. CMake таргеты

| Таргет | Тип | Хост | Pi | Санитайзеры |
|---|---|---|---|---|
| `indicator_domain` | static lib | ✅ | ✅ | ASan+UBSan (host Debug) |
| `indicator_transport` | static lib | ✅ | ✅ | ASan+UBSan (host Debug) |
| `unity` | static lib | ✅ | — | — |
| `test_parser … test_config` | executables (5) | ✅ | — | ✅ |
| `test_uart` | executable | ✅ | — | ✅ |
| `dispmanx_renderer` | static lib | ❌ | ✅ | — |
| `indicator` | executable | ❌ | ✅ | — |
| `media_ingest` | executable | ❌ | ✅ | — |
| `uart_rx_dump` | executable | ❌ | ✅ | — |

```bash
# Host (тесты):
cmake -B build/host \
  -DINDICATOR_PLATFORM_DISPMANX=OFF \
  -DINDICATOR_BUILD_TESTS=ON \
  -DINDICATOR_ENABLE_ASAN=ON \
  -DINDICATOR_ENABLE_UBSAN=ON \
  -DCMAKE_BUILD_TYPE=Debug

# Pi (через Docker / devcontainer):
cmake -B build/pi \
  -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-RPiZeroW.cmake \
  -DINDICATOR_PLATFORM_DISPMANX=ON \
  -DINDICATOR_BUILD_TESTS=OFF \
  -DINDICATOR_BUILD_INGEST=ON \
  -DCMAKE_BUILD_TYPE=Release
```

---

## 7. Рабочее окружение

```
ПК (macOS / Linux)
│
├── Хост
│   ├── just       ← just pi::*
│   ├── docker     ← управление devcontainer
│   ├── ssh/rsync  ← деплой на Pi
│   └── VSCode     ← IDE (Dev Containers extension)
│
├── Devcontainer (Docker: indicator-build, Ubuntu 22.04)
│   ├── arm-zig-cc (zig 0.13.0)       ← кросс-компилятор ARMv6
│   ├── clang-17 / clangd-17           ← host тесты + LSP
│   ├── clang-format-17 / clang-tidy-17
│   ├── gdb-multiarch                  ← remote debug
│   ├── cmake 3.28 / ninja
│   ├── just 1.36
│   └── /opt/vc / usr/                 ← Pi sysroot (DispmanX, libpng16)
│
└── Raspberry Pi Zero W (ARM1176JZF-S, ARMv6ZK)
    ├── Debian Buster Lite + systemd
    ├── SSH ←───── хост
    ├── gdbserver (порт 3333)
    ├── /dev/ttyAMA0 ←── STM32 (115200 8N1)
    └── /data/ ← writable data directory (bind mount source)
```

**Правило контекстов:** кросс-компиляция только в devcontainer; деплой и SSH только с хоста.

### Remote GDB

```bash
just build::pi-debug          # devcontainer
just pi::deploy-debug         # хост
just pi::gdbserver-start      # хост, отдельный терминал (туннель висит)
# VSCode → F5 → "🐛 Debug: indicator (Pi Zero W)"
```

GDB подключается через `host.docker.internal:3333` → SSH-туннель → Pi:3333 → gdbserver.

---

## 8. Тестирование

### Host unit-тесты (Unity, clang-17, ASan+UBSan)

| Файл | Покрытие |
|---|---|
| `test_parser.c` | CRC-16 корректный/некорректный; byte order (little-endian, регрессия P-18); parse\_frame (SOF/EOF/size/opcode); parse\_payload; parse\_dispatch; mode\_is\_valid() |
| `test_floor.c` | 1–9; 10–64; П/CHAR\_PI\_CYR(17)/CHAR\_pi\_cyr(19); П1–П9; −1..−9; нестандартные; L=19 R=5 (реальный трафик) |
| `test_state.c` | Первый фрейм → события; идентичный → 0; частичные изменения; edge-triggered sound; state\_apply\_dispatch |
| `test_sound_map.c` | DING этажи 1–49 (все диапазоны 20-/30-/40-); П, П1-П9; −1..−9; UP с music; CLOSING, OPENING; FIRE\_ALARM→fire.wav; BUTTON→button.wav; DONT\_WORK→g\_double.wav; NONE→valid=false; всего **33 теста** |
| `test_config.c` | nku\_scheme.toml; video.toml (full/missing/partial); renderer.toml; uart\_config (valid/defaults/port\_list); массивы в одну строку (P-31 регрессия); всего 21+ тест-кейс |
| `test_uart.c` | valid frame через pipe(); junk перед SOF; bad CRC; partial frame; два фрейма подряд; tearDown cleanup (P-19 регрессия) |

**Запуск:** `just build::test` → `ctest --output-on-failure` (6 суитов, ASan+UBSan чисто)

### On-target валидация

| Скрипт / команда | Что проверяет |
|---|---|
| `just pi::smoke` | Сервисы active, бинари существуют, /data/pi\_nku\_configs/ доступен, /dev/serial0 открывается, ALSA видит звуковую карту |
| `just pi::check-resources` | **124/124** файлов: PNG, WAV (45), конфиги, bind-монты, writable /data, i2s-silence active |
| `just pi::test-audio` | aplay тестового WAV через ALSA |
| `just pi::test-video` | omxplayer тестового mp4 |
| `just pi::dump` | uart\_rx\_dump на реальном трафике STM32 |

### CI pipeline (`just ci::pipeline`)

```
format-check → host unit-тесты (6 суитов, ASan+UBSan) → exit 0
```

---

## 9. Файлы на устройстве

```
/home/pi/indicator/             ← PI_DIR (будет read-only в Deploy v2)
├── indicator                   ← C-демон
├── media_ingest                ← USB демон (stub сейчас)
├── pi_nku_sync                 ← Rust (sync MCU конфига)
├── pi_nku_menu                 ← Rust TUI (редактирование параметров)
├── pi_nku_configs/             ← bind mount → /data/pi_nku_configs/
│   ├── nku_scheme.toml         ← звук, музыка, грузоподъёмность
│   ├── pi_scheme.toml          ← UART port + baudrate
│   ├── video.toml              ← параметры окна omxplayer
│   ├── renderer.toml           ← позиции слотов, resources_dir
│   └── menu_style.toml
├── resources/                  ← bind mount → /data/resources/
│   ├── BACK.png
│   ├── chars/                  ← 0.png … 37.png (char_code_t)
│   ├── arrows/                 ← up.png, down.png
│   ├── modes/                  ← firealarm/malfunction/loading/overload/seismo/
│   │                              fireman/inspection/evacuation/calling/talking.png
│   ├── weights/                ← load_0.png … load_15.png
│   └── notifications/          ← Фаза 7
├── sounds/                     ← bind mount → /data/sounds/
│   ├── 0.wav … 19.wav, 20.wav, 20-.wav, 30.wav, 30-.wav, 40.wav, 40-.wav
│   ├── floor.wav, podval.wav, minus.wav
│   ├── up.wav, down.wav
│   ├── closing.wav, opening.wav
│   ├── overload.wav, fire.wav
│   ├── g_single.wav, g_double.wav, g_triple.wav
│   ├── button.wav
│   └── mus1.wav … mus7.wav     (45 WAV итого)
├── videos/                     ← bind mount → /data/videos/
│   └── output.mp4
├── splash/
│   └── splash.jpg              ← boot splash
├── scripts/
│   ├── run_setup.sh
│   ├── first_boot.sh
│   ├── smoke_test.sh
│   └── check_resources.sh
└── tools/
    ├── uart_rx_dump
    └── MUp-rpi0

/data/                          ← writable data (сейчас dir; Deploy v2 → ext4 раздел)
├── first_boot_done             ← флаг первого старта
├── setup_status                ← ok | pull_failed | push_failed | pending
├── pi_nku_configs/
├── resources/
├── sounds/
└── videos/
    └── output.mp4
```

```
/etc/asound.conf                ← plug→softvol→dmixer→speakerbonnet (S32_LE / 48kHz)
/etc/systemd/system/
├── indicator-firstboot.service
├── indicator-setup.service     ← TTYVHangup=yes, TTYReset=yes (без inline-аннотаций)
├── indicator.service           ← After=i2s-silence.service
├── i2s-silence.service         ← I2S keepalive, ExecStartPre ждёт card 0
└── media-ingest.service        ← ConditionPathExists (Phase 7)
```

---

## 10. Фазы реализации

### Фаза 0 — Настройка окружения ✅ ЗАКРЫТА

**Итог:** Pi готова (Buster Lite), Docker-образ собран (Ubuntu 22.04 + LLVM 17 + zig 0.13.0),
кросс-компиляция ARM stub работает, rsync деплой работает.

P-01..P-07: CMake stubs, bind mount, RuntimeOutputDirectory, deploy/, ship, Buster архив.

---

### Фаза 1 — Domain-слой и тесты ✅ ЗАКРЫТА

**Итог:** бизнес-логика написана (parser, floor, state, sound\_map, config); 5/5 тестов; ASan+UBSan чисты; remote GDB работает.

P-08..P-17: glibc 2.34 → zig cc; ARMv6 segfault → `-marm`; docker arm64; ASan runtime; stale CMakeCache; clangd; GDB; SSH UseKeychain; vscode cppdbg; multiple main().

---

### Фаза 2 — UART transport + event loop ✅ ЗАКРЫТА

**Итог:** демон читает бинарные фреймы от STM32 через UART; парсит dispatch (opcode=0xAA) и elevator (opcode=0xDA); логирует события; стабильный lifecycle под systemd; 6/6 тестов зелёных.

**Ключевые решения:**
- poll-цикл: uart\_fd + signalfd + timerfd (watchdog 30 с)
- Dispatch opcode 0xAA: CALL/ANSWER/OFF — приоритет выше mode из 0xDA
- Service opcode 0xC0: пустой payload, LOG\_DEBUG, игнорируется
- CHAR\_pi\_cyr=19 (строчная «п») — тоже подвальный этаж

P-18: CRC byte order — STM32 шлёт little-endian (LO first, HI second).
P-19: test\_uart утечка через Unity longjmp → tearDown().
P-20: опечатка `inndicator_mode_t`.
P-21: `cfmakeraw()` / `O_CLOEXEC` без `_GNU_SOURCE` → явные termios + fcntl.
P-22: clangd + `_GNU_SOURCE` → явный `#define _GNU_SOURCE` первой строкой main.c.
P-23: stale CMakeCache при запуске с хоста → `rm -rf build/pi` перед configure.

---

### Фаза 3 — Video player ✅ ЗАКРЫТА

**Итог:** omxplayer под надзором indicator; автоперезапуск < 1 с при падении; параметры окна из video.toml; 14/14 тестов.

**Ключевые решения:**
- `posix_spawnp` + `POSIX_SPAWN_SETPGROUP` → отдельная process group
- `killpg(pgid, SIGKILL)` в `video_player_close` (SIGTERM не работает для bash-обёртки)
- `KillMode=control-group` → systemd убивает dbus-daemon через cgroup

P-24: dbus-daemon вне pgroup omxplayer → `Failed with result 'timeout'` (косметика; не блокирует). **Исправить в Deploy v2.**
P-25: SIGTERM не убивает bash-обёртку omxplayer → SIGKILL.
P-26: каскадные рестарты при `pkill -f omxplayer.bin` — ожидаемое поведение watchdog.

---

### Фаза 4 — DispmanX renderer ✅ ЗАКРЫТА

**Итог:** все 6 слотов (BACKGROUND, MODE, WEIGHT, DIGIT\_LEFT, DIGIT\_RIGHT, ARROW) отображаются корректно поверх видео; latency UART → экран 88–158 мс; 14/14 тестов.

**Ключевые решения:**
- fast\_update для DIGIT\_LEFT/RIGHT: `changeSourceImageLayer` без пересоздания
- `renderer_keepalive()` — пустой DispmanX update каждые 30 с (watchdog tick)
- `renderer_config_load()` — позиции слотов и resources\_dir из renderer.toml
- `libpng16.so` (динамическая) — не `.a` (ARMv7 static → SIGSEGV на ARMv6)

P-27: `libpng16.a` ARMv7 → segfault до main() → динамическая `.so` с Pi.
P-28: VideoCore IV засыпает при простое → `renderer_keepalive()` каждые 30 с.
P-29: ARROW slow path (destroy+recreate при каждом появлении) — не блокирует. **Исправить в Deploy v2.**

**Latency (on-target):**

| Операция | Время |
|---|---|
| UART frame → first digit update | 45–63 мс |
| UART frame → both digits updated | 88–158 мс |
| Arrow creation (slow path) | ~48 мс |
| keepalive null update | ~1 мс |

---

### Фаза 5 — Audio player ✅ ЗАКРЫТА

**Итог:** аудиоподсистема реализована, протестирована на HIL-стенде (24 ч), работает в продакшн-конфигурации. Latency UART → начало звука < 100 мс.

**Реализовано (код):**
- `audio_player_t` — pthread + mutex + condvar
- Приоритетная очередь depth=3: CRITICAL(0) > FLOOR(1) > MOVEMENT(2) > MUSIC(3)
- effective\_prio = min(current, min\_queue) — корректное вытеснение без потери DING (P-32)
- При вытеснении: `current_prio` немедленно сбрасывается в NONE (убирает stale-значение)
- `posix_spawnp("aplay")` + waitpid; SIGTERM при вытеснении
- amixer lazy volume: меняется только при переходе sound\_vol ↔ music\_vol (~20 мс)
- Фоновая музыка: mus1..mus7.wav по кругу, индекс не сбрасывается при остановке
- Подавление музыки при `mode != MODE_NORMAL` и при активном dispatch
- `needs_music=0` если нештатный режим — up.wav играет, музыка не запускается

**Реализовано (инфраструктура):**
- `/etc/asound.conf` — стек `plug → softvol 'PCM' → dmixer → speakerbonnet hw:0` (S32\_LE/48kHz)
- `deploy/systemd/i2s-silence.service` — I2S keepalive через dmix напрямую; `ExecStartPre` ждёт card 0 до 15 с; `StartLimitBurst=20`
- `indicator.service` обновлён: `After=i2s-silence.service`, `Wants=i2s-silence.service`
- `scripts/setup_pi.sh` — I2S overlay, asound.conf, i2s-silence, fbi
- `scripts/check_resources.sh` — секция 6 полностью переписана: 45 WAV (режим fail, не warn)
- `docs/AUDIO_MODULE.md` — техническая документация (12 разделов)
- `docs/indicator_checklist.md` — интеграционный чек-лист (~76 тестов)

**Проблемы (правильная нумерация — P-30/P-31 заняты Фазой Deploy):**

P-32 (отчёт: P-30): **Гонка вытеснения — DING пропускался при активной музыке.**
Worker обновляет `current_prio` только при взятии из очереди. В промежутке OPENING(2) видел stale `MUSIC(3)` вместо уже поставленного DING(1) → `2<3=true` → вытеснял DING. Фикс: `effective_prio = min(current_prio, min_prio_in_queue)`.

P-33 (отчёт: P-31): **Три бага в sound\_map.c.**
`SOUND_FIRE_ALARM` → `g_triple.wav` (должно: `fire.wav`);
`SOUND_BUTTON` → `g_single.wav` (должно: `button.wav`);
этажи 41–49 — не был реализован диапазон `40-.wav + ones + floor.wav`.

P-34 (отчёт: P-32): **dmix IPC deadlock после 24 часов работы.**
`kill(aplay, SIGTERM)` в момент удержания dmix-семафора → aplay завершается не освободив его → все последующие aplay/amixer зависают навсегда. Лечение на устройстве: `ipcrm -m`/`-s` + рестарт. Обходной путь: `i2s-silence.service` работает напрямую через `-D dmixer` (меньше слоёв — меньше вероятность deadlock). Старый `aplay.service` (44100/S16\_LE) удалён.

P-35 (отчёт: P-33): **Неверный формат ALSA: S16\_LE вместо S32\_LE.**
MAX98357A поддерживает только S32\_LE. В `asound.conf` был S16\_LE → `i2s-silence.service` падал немедленно → indicator ждал всех рестартов (~2 мин). Фикс: `format S32_LE` в `asound.conf`; WAV (S16\_LE) конвертируются plug-слоем автоматически.

P-36 (отчёт: P-34): **indicator-setup.service: inline-аннотации в значениях.**
`TTYVHangup=yes     ← добавить: ...` — systemd читал всю строку как значение → parse error → директивы игнорировались → tty не сбрасывался после setup. Фикс: убраны аннотации.

P-37 (отчёт: P-35): **i2s-silence.service не стартует на холодном старте.**
I2S карта (googlevoicehat overlay) инициализируется позже, чем `sound.target`. `aplay -D dmixer` → «no such device» → цикл рестартов. Фикс: `ExecStartPre` ждёт `card 0` до 15 с; `StartLimitBurst=20`, `StartLimitIntervalSec=120`.

**Latency (on-target, HIL-стенд):**

| Операция | Время |
|---|---|
| UART frame → начало воспроизведения (без смены громкости) | < 100 мс ✅ |
| Смена громкости (amixer lazy call) | ~20 мс |
| posix\_spawn aplay | ~5–10 мс |
| Вытеснение: SIGTERM → следующий файл | < 200 мс |
| i2s-silence keepalive CPU load | ~0.3% |

---

### Фаза Deploy ✅ ЗАКРЫТА

**Итог:** корректная файловая структура на устройстве, bind-монты, systemd-оркестрация, uart\_config\_load(), setup\_status, indicator-firstboot.service; 21/21 тестов.

**Ключевые решения:**
- `/data/` layout + bind-монты в `/etc/fstab`
- `indicator-setup.service` [oneshot, RemainAfterExit=yes, TTYPath=/dev/tty1]
- `indicator-firstboot.service` [ConditionPathExists=!/data/first\_boot\_done]
- `uart_config_load()` из `pi_scheme.toml` (port, baudrate)
- `setup_status` читается из `/data/setup_status` при старте indicator

P-30: rsync не сохраняет +x → `chmod +x scripts/*.sh` после rsync.
P-31: однострочные TOML-массивы `[...]` ломали парсер → проверка закрывающей `]` на той же строке.

---

### Phase Deploy Addendum ✅ ЗАКРЫТА

**Итог:** boot splash (fbi), подавление артефактов ядра на tty1.

**Ключевые решения:**
- `/boot/cmdline.txt`: `console=tty3 quiet loglevel=3` — ядро молчит на tty1
- `run_setup.sh`: `printf '\033[2J\033[H' > /dev/tty1` → чистый экран перед TUI
- fbi splash.jpg (3 сек) в начале `run_setup.sh`, sudoers для `pi → fbi`
- Plymouth не установлен; GPU bootloader не поддерживает splash на этой прошивке

---

### Фаза 6 — Deploy v2 🔄 В РАБОТЕ

**Цель:** надёжность производственного устройства, read-only rootfs, воспроизводимый образ.

#### Блок 1 — Рефакторинг main.c (средний приоритет)

- [ ] Выделить `src/app/render_dispatch.c/.h`: `mode_to_rel_path`, `renderer_apply_mode`, `renderer_apply_dispatch`, `renderer_apply_elevator`
- [ ] Выделить `src/app/uart_handler.c/.h`: `on_uart_frame`, `maybe_clear_mcu_notification`
- [ ] Результат: main.c ~300 строк (только poll loop + init + cleanup)

#### Блок 2 — P-24: dbus-daemon (низкий приоритет)

- [ ] Попробовать `--no-dbus` флаг omxplayer (вариант A, низкая сложность)
- [ ] Если не поддерживается: `ExecStopPost=pkill -KILL -f dbus-daemon` (вариант B)
- [ ] Убрать `Failed with result 'timeout'` из статуса сервиса

#### Блок 3 — P-29: ARROW slow path (низкий приоритет)

- [ ] `renderer_hide()`: скрывать через `ELEMENT_CHANGE_OPACITY=0` вместо `destroyImageLayer`
- [ ] `renderer_show_png()`: fast\_update если size совпадает
- [ ] Проверка: в логах `slot fast-updated` вместо `slot created` для стрелки

#### Блок 4 — overlayroot (read-only rootfs)

- [ ] `apt-get install overlayroot`
- [ ] `/etc/overlayroot.conf`: `overlayroot="tmpfs"`
- [ ] Убедиться что bind-монты из fstab отрабатывают при overlayroot
- [ ] Убедиться что `/tmp`, `/run`, `/data` writable

#### Блок 5 — 3-раздельная схема SD-карты

- [ ] `fdisk`: создать `/dev/mmcblk0p3` (ext4, ~9 GB, label=data)
- [ ] fstab: `LABEL=data /data ext4 defaults,noatime 0 2`
- [ ] `setup_pi.sh`: проверять `mountpoint -q /data` вместо `mkdir -p /data`

#### Блок 6 — `build_image.sh`

- [ ] Скачать Buster Lite .img.xz (зафиксированный URL + sha256)
- [ ] Расширить образ, создать p3 (ext4)
- [ ] `chroot + qemu-arm-static`: запуск `setup_pi.sh`, деплой всего, overlayroot ON
- [ ] Сжать: `zstd` → `.img.zst` + SHA256
- [ ] `just image [VERSION]` → `release/indicator-vX.Y.Z.img.zst`

#### Блок 7 — Производственный pipeline

- [ ] Инструкция: balenaEtcher → flash → insert → wait 90 s → ready
- [ ] Хранилище образов: NAS / S3-совместимое

#### Блок 8 — factory-test

- [ ] `just pi::factory-test`: indicator active, setup ok, UART открывается, omxplayer жив, /data writable, rootfs read-only, VERSION file есть, bind-монты активны

**Зависимости:** Блок 4 → 5 → 6 → 7. Блоки 1–3 независимы.
**Минимум для производства:** Блоки 4 → 5 → 6 → 7.

---

### Фаза 7 — Media ingest ⏳ ОЖИДАЕТ

**Цель:** USB-флешка заменяет видео, уведомления на экране.

- [ ] `src_media_ingest/` полностью:
  - inotify на /dev (IN\_CREATE / IN\_DELETE)
  - mount с fallback: /dev/sdX1 → /dev/sdX
  - Поиск .mp4, ffmpeg concat
  - `rename()` для атомарной замены
  - Запись статуса в FIFO
- [ ] `src/media/media_ipc.c/.h` — чтение из FIFO в главном демоне
- [ ] `SPRITE_NOTIFICATION` через renderer при каждом статусе
- [ ] `FD_FIFO` в poll-цикле main.c (заглушка `/* Фаза 7 */` уже есть)
- [ ] `video_player_replace()` после `MEDIA_STATUS_DONE`
- [ ] `media-ingest.service` — реальный сервис (не stub)

**Критерий:** полный цикл USB работает; уведомления отображаются; видео заменяется.

---

### Фаза 8 — Font renderer (low priority, опционально)

**Цель:** цифры этажа через растеризацию глифов, без PNG для chars/.

- [ ] Перенести font-renderer в `src/font/`
- [ ] `font_render_to_rgba(char_code, w, h, rgba_buf)`
- [ ] Переключить DIGIT\_LEFT/RIGHT на `renderer_show_sprite_buffer()`
- [ ] Убрать зависимость от `resources/chars/*.png`

---

### Фаза 9 — Полировка (ongoing)

- [ ] `docs/UART_PROTOCOL.md` — финальная спецификация протокола
- [ ] `docs/ARCHITECTURE.md` — финальная архитектура
- [ ] GitHub Actions CI: `just ci::pipeline` в `.github/workflows/`
- [ ] Финальные замеры latency (UART → экран, UART → звук)
- [ ] Ревью логов: достаточно ли информации для диагностики в поле?

---

## 11. Что не переписываем

| Компонент | Действие |
|---|---|
| `pi_nku_sync` (Rust) | Без изменений; pre-built в `deploy/bin/` |
| `pi_nku_menu` (Rust TUI) | Без изменений; pre-built в `deploy/bin/` |
| Andrew Duncan layers | → `platform/dispmanx/layers/`; clang-tidy отключён |
| `nku_scheme.toml` | Без изменений (читаем, не генерируем) |
| MUp-rpi0 | Диагностический бинарь STM32; в `deploy/tools/` |

---

## 12. UART протокол (справочник)

### 12.1 Бинарный фрейм

```
┌──────┬──────┬────────┬──────────────────────┬──────────┬──────────┬──────┐
│ SOF  │ SIZE │ OPCODE │        DATA          │  CRC_LO  │  CRC_HI  │ EOF  │
│ 0xAA │  1B  │   1B   │     SIZE байт        │    1B    │    1B    │ 0xBB │
└──────┴──────┴────────┴──────────────────────┴──────────┴──────────┴──────┘

SOF    = 0xAA           (Start of Frame)
SIZE   = длина DATA в байтах
OPCODE — см. 12.2
DATA   = текстовый payload
CRC    = CRC-16/CCITT-FALSE по DATA; **little-endian** (LO first, HI second)
EOF    = 0xBB           (End of Frame)
```

⚠️ **CRC byte order — критично:** STM32 (`MU_tx_frame_create`) записывает `[CRC_LO][CRC_HI]`.
Было ошибочно задокументировано как big-endian. Исправлено в P-18 (Фаза 2).

### 12.2 Opcodes

| Opcode | Название | Payload | Обработка |
|---|---|---|---|
| `0xDA` | `MU_OPCODE_ELEVATOR_STATUS` | `#STM:L…:R…:A…:S…:M…:E#\r\n\0` | Основные данные лифта |
| `0xAA` | `MU_OPCODE_DISPATCH` | `DISPATCH CALL\r\n` / `DISPATCH ANSWER\r\n` / `DISPATCH OFF\r\n` | Диспетчерская связь |
| `0xC0` | служебный | пустой (len=0) | LOG\_DEBUG, игнорируется |

**Примечание по opcode=0xAA:** совпадает с SOF-байтом, но занимает разную позицию в кадре (byte[2] = OPCODE), парсер работает корректно.

**Dispatch приоритет:** dispatch активен → MODE-слот показывает dispatch-иконку (CALL/ANSWER), не mode из 0xDA. При `DISPATCH_OFF` — восстанавливается mode из последнего 0xDA.

STM32 отправляет фреймы **только при изменении** (edge-triggered). Payload заканчивается `\r\n\0`; null-байт включён в `data_len`.

### 12.3 Payload opcode=0xDA

```
#STM:L<val>:R<val>:A<val>:S<val>:M<val>:E#\r\n\0

L — left_char  (char_code_t)
R — right_char (char_code_t)
A — arrow_t
S — sound_t (edge-triggered: ненулевое значение ровно 1 фрейм)
M — indicator_mode_t
```

### 12.4 Коды символов (char\_code\_t)

| Код | Символ | Значение |
|-----|--------|----------|
| 0–9 | `'0'`–`'9'` | Цифра |
| 16 | ` ` (CHAR\_BLANK) | Пустая позиция |
| 17 | `П` (CHAR\_PI\_CYR) | Подвал (заглавная) |
| 19 | `п` (CHAR\_pi\_cyr) | Подвал (строчная, тоже декодируется как basement) |
| 22 | `-` (CHAR\_MINUS) | Минус |

Полный список кодов: CHAR\_A(10)..CHAR\_T(37) — символы латиницы и кириллицы; `CHAR_CODE_MAX=37`.

### 12.5 Декодирование этажа (floor\_decode)

| left | right | Тип | Результат |
|------|-------|-----|-----------|
| 0–9 | 0–9 | NORMAL | двузначный этаж 10–64 |
| BLANK | 1–9 | NORMAL | однозначный этаж 1–9 |
| BLANK | BLANK | UNKNOWN | → g\_triple fallback |
| PI\_CYR / pi\_cyr | BLANK | BASEMENT | П (подвал без номера) |
| PI\_CYR / pi\_cyr | 1–9 | BASEMENT\_N | П1–П9 |
| MINUS | 1–9 | NEGATIVE | −1..−9 |
| 22 | 0 | UNKNOWN | `-0` → g\_triple |
| 22 | BLANK | UNKNOWN | → g\_triple |
| иначе | | UNKNOWN | → g\_single |

### 12.6 arrow\_t

```c
ARROW_NONE = 0   // нет стрелки
ARROW_UP   = 1   // вверх
ARROW_DOWN = 2   // вниз
ARROW_BOTH = 3   // оба направления → скрыть (как NONE)
```

### 12.7 sound\_t (edge-triggered, S-поле в 0xDA)

| sound\_t | WAV-файл(ы) | Приоритет |
|----------|-------------|-----------|
| SOUND\_DING | floor.wav + N.wav (см. §12.8) | AUDIO\_PRIO\_FLOOR |
| SOUND\_UP | up.wav | AUDIO\_PRIO\_MOVEMENT |
| SOUND\_DOWN | down.wav | AUDIO\_PRIO\_MOVEMENT |
| SOUND\_CLOSING | closing.wav | AUDIO\_PRIO\_MOVEMENT |
| SOUND\_OPENING | opening.wav | AUDIO\_PRIO\_MOVEMENT |
| SOUND\_BUTTON | button.wav | AUDIO\_PRIO\_MOVEMENT |
| SOUND\_OVERLOAD | overload.wav | AUDIO\_PRIO\_CRITICAL |
| SOUND\_FIRE\_ALARM | fire.wav | AUDIO\_PRIO\_CRITICAL |
| SOUND\_DONT\_WORK | g\_double.wav | AUDIO\_PRIO\_CRITICAL |

### 12.8 Логика DING (sound\_map\_resolve → SOUND\_DING)

| Этаж | WAV-последовательность |
|---|---|
| 1–20 | `N.wav` + `floor.wav` |
| 21–29 | `20-.wav` + `ones.wav` + `floor.wav` |
| 30 | `30.wav` + `floor.wav` |
| 31–39 | `30-.wav` + `ones.wav` + `floor.wav` |
| 40 | `40.wav` + `floor.wav` |
| 41–49 | `40-.wav` + `ones.wav` + `floor.wav` |
| > 49 | `g_triple.wav` (fallback) |
| П | `podval.wav` + `floor.wav` |
| П1–П9 | `N.wav` + `podval.wav` + `floor.wav` |
| −1..−9 | `minus.wav` + `N.wav` + `floor.wav` |
| UNKNOWN | `g_single.wav` |

**После UP/DOWN:** `needs_music=1` → worker запускает `mus<N>.wav` (циклически).

### 12.9 indicator\_mode\_t (M-поле в 0xDA)

| Значение | Константа | PNG |
|---|---|---|
| 0 | MODE\_NORMAL | — (скрыть слот) |
| 1 | MODE\_FIRE\_ALARM | modes/firealarm.png |
| 2 | MODE\_MALFUNCTION | modes/malfunction.png |
| 3 | MODE\_LOADING | modes/loading.png |
| 4 | MODE\_OVERLOAD | modes/overload.png |
| 5 | MODE\_SEIS\_ALARM | modes/seismo.png |
| 6 | MODE\_FIREMANS | modes/fireman.png |
| 7 | MODE\_SERVICE | modes/inspection.png |
| 8 | MODE\_EVACUATION | modes/evacuation.png |
| 9 | MODE\_UPS\_MALFUNCTION | modes/malfunction.png |
| 100 | MODE\_DISPATCH\_CALL | modes/calling.png (через dispatch) |
| 101 | MODE\_DISPATCH\_ANSWER | modes/talking.png (через dispatch) |
| 255 | MODE\_CONN\_LOST | — (скрыть слот) |

Диапазон разрывный: `mode_is_valid()` вместо `<= MAX`.

### 12.10 Громкость (из nku\_scheme.toml)

```toml
[soundvolume]
possible_values = ["0%", "25%", "50%", "75%", "100%"]
default = "50%"

[musicvolume]
possible_values = ["0%", "25%", "50%", "75%", "100%"]
default = "0%"
```

`musicvolume = "0%"` → музыка не воспроизводится (`needs_music` игнорируется, `music_vol_pct=0`).

---

## 13. Открытые вопросы

| ID | Вопрос | Статус | Влияет на |
|----|--------|--------|-----------|
| В-01 | WAV для FIRE\_ALARM / DONT\_WORK / BUTTON | ✅ ЗАКРЫТ: fire.wav / g\_double.wav / button.wav | sound\_map.c |
| В-02 | Есть ли closing.wav и opening.wav | ✅ ЗАКРЫТ: оба файла подтверждены | sound\_map.c |
| В-03 | g\_double / g\_single — контекст | ✅ ЗАКРЫТ: g\_single → UNKNOWN этаж; g\_double → SOUND\_DONT\_WORK | sound\_map.c |
| В-04 | UART параметры | ✅ ЗАКРЫТ: 115200 baud / /dev/serial0 (= ttyAMA0) / 8N1 | uart.c |
| В-05 | s\_close.wav, s\_open.wav — нужны? | ⏳ ОТКРЫТ: файлы есть в deploy/sounds/, в sound\_map не используются | очистка |
| В-06 | g\_single.wav, g\_double.wav, g\_triple.wav — убрать из прямого деплоя? | ⏳ ОТКРЫТ: g\_triple используется как fallback для >49 и UNKNOWN | очистка |
| В-07 | ARROW\_BOTH (=3) — специальная обработка? | ✅ ЗАКРЫТ: скрывать стрелку (как ARROW\_NONE) | renderer |
| В-08 | P-24 dbus-daemon timeout — omxplayer --no-dbus? | ⏳ ОТКРЫТ: исследовать в Deploy v2 | Deploy v2 |
| В-09 | Когда нужен indicator.target (multi-service группа)? | ⏳ ОТКРЫТ: нужен после реализации Фазы 7 (media-ingest) | Фаза 7 |
| В-10 | TODO в dump: читать port/baud из pi\_scheme.toml динамически | ⏳ ОТКРЫТ: dump сейчас хардкодит /dev/ttyAMA0 115200 | pi.just |