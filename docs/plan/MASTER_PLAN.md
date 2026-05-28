# Lift Indicator — Мастер-план

> **Статус:** Фазы 0–1 закрыты. Фаза 2 в работе.
> **Стек:** C11 · DispmanX · omxplayer · ALSA · systemd · CMake · Docker · just
> **Компилятор:** `zig cc` (`arm-zig-cc` wrapper) — ARMv6, arm1176jzf_s, hard-float
> **Устройство:** Raspberry Pi Zero W Rev 1.1 · ARM1176JZF-S · ARMv6ZK · Debian Buster

---

## 1. Все принятые решения

| Тема | Решение | Обоснование |
|---|---|---|
| ОС | Raspbian **Buster Lite** (CLI, без X11) | DispmanX и omxplayer работают без GUI; экономия ~100 MB RAM и ~20 с загрузки |
| Железо | **Raspberry Pi Zero W** (ARM1176JZF-S, ARMv6ZK) | Уточнено в Фазе 1 — не 2W, а первый Zero W |
| Автозапуск | **systemd units** | Supervision, логи в journald, restart policy |
| Рендеринг | **DispmanX** (сохраняем) | Zero-overhead overlay поверх omxplayer на этом железе |
| Видеоплеер | **omxplayer** (сохраняем) | OpenMAX IL, минимальная нагрузка на CPU; заменить только при смене ОС |
| Аудио backend | **ALSA softvol + aplay** | MAX98357 не имеет HW volume в ALSA; softvol в asound.conf — легче PulseAudio |
| Аудио из кода | `posix_spawn("aplay")` в отдельном pthread | Нет shell overhead; latency ~30 ms вместо ~150 ms с `system()` |
| Аудио очередь | Приоритетная, глубина 3 | CRITICAL > FLOOR > MOVEMENT > MUSIC |
| notify | **Упраздняется** | SPRITE_NOTIFICATION — слот в renderer; нет spawn/pkill |
| Рендеринг цифр | **PNG сейчас**, font-renderer — Фаза 8 | Все PNG одного размера → changeSourceImageLayer без мигания |
| BACK.png | RGBA PNG поверх видео, путь в конфиге | Статична сейчас, легко сменить в будущем |
| group_number | Только конфиг MCU | Не используется в логике индикатора |
| Task runner | **just** (mod build / mod pi) | `mod build` = devcontainer, `mod pi` = хост |
| Кросс-компилятор | **zig cc** (`arm-zig-cc` wrapper) | `arm-linux-gnueabihf-gcc` из Bookworm содержит crt*.o под glibc 2.34+ — segfault на Buster; zig компилирует crt точно под arm1176jzf_s |
| Docker base | **Ubuntu 22.04 + LLVM 17 + zig 0.13.0** | Debian Buster в Docker — проблемы с arm64 (Rosetta); Ubuntu Jammy + official LLVM repo решает всё |
| Деплой | **Base image + rsync** | Flash один раз, обновление приложения через rsync |
| Тесты | **Unity** (vendored, MIT) | Один .c/.h файл, компилируется везде |
| Код Andrew Duncan | **Переносим без изменений** в `platform/dispmanx/layers/` | MIT, корректный, не трогаем |
| Конфиг | **`nku_scheme.toml`** (TOML) | `pizero.ini` — legacy, не используется; `config_utility` и `rpi_menu` работают с TOML |
| inih | **Не используется** | Конфиг — TOML, свой парсер; inih зарезервирован, не подключён |

---

## 2. Архитектура системы

> SSH: `ssh -i ~/.ssh/id_ed25519 pi@indicator-01.local`

### 2.1 Процессная модель

```
systemd
├── indicator-setup.service   [oneshot, Before=indicator.service]
│     config_utility --mode=pull
│     rpi_menu (TUI на /dev/tty1)
│     config_utility --mode=push
│
├── indicator.service         [Restart=always, RestartSec=2]
│     ├── [poll loop]  uart_fd · fifo_fd · signalfd · timerfd
│     ├── [pthread]    audio player (posix_spawn aplay)
│     └── [child]      omxplayer  (posix_spawn, SIGCHLD watchdog)
│
└── media-ingest.service      [Restart=on-failure, RestartSec=5]
      inotify /dev → mount → ffmpeg → FIFO → indicator
```

### 2.2 Z-порядок DispmanX

```
Z = 1   omxplayer  (--layer 1)        видео
Z = 2   BACKGROUND                    BACK.png (RGBA)
Z = 3   DIGIT_LEFT, DIGIT_RIGHT       цифры этажа
Z = 3   ARROW                         стрелка направления
Z = 3   WEIGHT                        шильдик грузоподъёмности
Z = 4   MODE                          режим (пожар, перегрузка…)
Z = 5   NOTIFICATION                  баннер USB-статуса
```

### 2.3 Главный цикл — однопоточный poll

```
poll()
  ├── uart_fd     → сборка бинарного фрейма → CRC16 → parse_payload()
  │                  → state_apply_frame()
  │                  → renderer_show_sprite_*()
  │                  → audio_player_play()
  │
  ├── fifo_fd     → media_status_t от media-ingest
  │                  → renderer_show_sprite(NOTIFICATION, ...)
  │                  → video_player_replace() при DONE
  │
  ├── signalfd    → SIGCHLD → video_player_check_and_restart()
  │
  └── timerfd     → watchdog heartbeat, лог статистики
```

### 2.4 Обновление DispmanX-слотов

```
DIGIT_LEFT, DIGIT_RIGHT, ARROW   →  fast_update = true
  Первый раз: loadPng → createResourceImageLayer → addElementImageLayerOffset
  Обновление: loadPng → resource_write_data → changeSourceImageLayer
  Скрытие:    destroyImageLayer

BACKGROUND, WEIGHT, MODE,
NOTIFICATION                     →  fast_update = false
  Любое изменение: destroyImageLayer → loadPng → create → add
```

### 2.5 Аудио — приоритеты

```
AUDIO_PRIO_CRITICAL = 0   ←  overload / fire alarm (высший)
AUDIO_PRIO_FLOOR    = 1   ←  анонс этажа (ding + "этаж N")
AUDIO_PRIO_MOVEMENT = 2   ←  up / down (+ музыка)
AUDIO_PRIO_MUSIC    = 3   ←  только трек (низший)

Правило: новый < текущего  →  прервать немедленно
         новый >= текущего →  добавить в очередь (FIFO, depth=3)
```

---

## 3. Модули

### Платформонезависимые (тестируются на хосте)

| Модуль | Файл | Ответственность |
|---|---|---|
| Config | `src/config/config.c/.h` | Парсинг nku_scheme.toml, дефолты, валидация |
| Protocol types | `src/protocol/types.h` | Enum'ы: `arrows_state_t`, `el_mode_t`, `s_code_t`, `parsed_payload_t` |
| Protocol parser | `src/protocol/parser.c/.h` | `protocol_parse_frame()` + `protocol_parse_payload()`, CRC-16, без malloc |
| Floor codec | `src/domain/floor.c/.h` | Коды символов → номер этажа |
| Sound map | `src/domain/sound_map.c/.h` | `sound_map_resolve()`, `sound_map_volume_percent()` |
| State machine | `src/domain/state.c/.h` | `state_apply_frame()` → `state_update_result_t` |
| Renderer interface | `src/renderer/renderer.h` | Только API, без bcm_host.h |

### Только Pi

| Модуль | Файл | Ответственность |
|---|---|---|
| UART transport | `src/transport/uart.c/.h` | termios, сборка бинарных фреймов, `frame_ready_cb_t` |
| Audio player | `src/audio/audio.c/.h` | pthread, приоритетная очередь, posix_spawn aplay |
| Video player | `src/player/video_player.c/.h` | posix_spawn omxplayer, SIGCHLD watchdog |
| Media IPC | `src/media/media_ipc.c/.h` | Чтение статуса из FIFO |
| DispmanX renderer | `platform/dispmanx/renderer_impl.c/.h` | Реализует renderer.h через DispmanX |
| DispmanX layers | `platform/dispmanx/layers/` | Andrew Duncan — без изменений |
| Main | `src/main.c` | Composition root, poll-цикл, signal handling |

### Отдельный демон media-ingest

| Файл | Ответственность |
|---|---|
| `src_media_ingest/main.c` | Точка входа, poll-цикл |
| `src_media_ingest/usb_watcher.c/.h` | inotify на /dev (IN_CREATE/IN_DELETE) |
| `src_media_ingest/mounter.c/.h` | mount/umount, fallback /dev/sdX1 → /dev/sdX |
| `src_media_ingest/ffmpeg_runner.c/.h` | ffmpeg concat, atomic rename() |
| `src_media_ingest/status_pipe.c/.h` | Запись статуса в FIFO |

---

## 4. Структура репозитория

```
lift-indicator/
│
├── justfile                         ← корневой оркестратор (mod build, pi, ci)
├── bootstrap.sh                     ← уровень 0: just + SSH ключ + sysroot + Docker
├── .env.example                     ← шаблон (PI_HOST, PI_USER, PI_DIR, BUILD_DIR)
├── .gitignore
├── .clang-format
│
├── just/
│   ├── build.just                   ← devcontainer: тесты, Pi сборка, format
│   ├── pi.just                      ← хост: deploy, SSH, setup, logs, smoke
│   └── ci.just                      ← CI pipeline
│
├── cmake/
│   ├── Toolchain-RPiZeroW.cmake     ← arm-zig-cc + /opt/vc (ARMv6, arm1176jzf_s)
│   ├── Warnings.cmake               ← apply_warnings(target)
│   └── Sanitizers.cmake             ← ASan / UBSan
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
│   └── media/          media_ipc.h / media_ipc.c
│
├── src_media_ingest/
│   ├── main.c
│   ├── usb_watcher.h/c
│   ├── mounter.h/c
│   ├── ffmpeg_runner.h/c
│   └── status_pipe.h/c
│
├── platform/
│   └── dispmanx/
│       ├── CMakeLists.txt
│       ├── renderer_impl.h / renderer_impl.c
│       └── layers/                  ← Andrew Duncan (MIT), без изменений
│           ├── image.h / image.c
│           ├── image_layer.h / image_layer.c
│           ├── background_layer.h / background_layer.c
│           ├── loadpng.h / loadpng.c
│           └── element_change.h
│
├── tests/
│   ├── CMakeLists.txt
│   ├── unity/           unity.h / unity.c   (MIT, vendored)
│   ├── test_parser.c
│   ├── test_floor.c
│   ├── test_state.c
│   ├── test_sound_map.c
│   └── test_config.c
│
├── build-env/
│   ├── Dockerfile                   ← Ubuntu 22.04 + LLVM 17 + zig 0.13.0 + /opt/vc
│   └── pi-sysroot/                  ← .gitignore; копируется: just pi::fetch-sysroot
│       └── opt/vc/
│
├── .devcontainer/
│   └── devcontainer.json
│
├── deploy/
│   ├── indicator-setup.service
│   ├── indicator.service
│   ├── media-ingest.service
│   ├── indicator.target
│   └── asound.conf                  ← ALSA softvol для MAX98357
│
├── scripts/
│   ├── setup_pi.sh                  ← первичная настройка Pi (пакеты, ALSA, dirs)
│   └── smoke_test.sh                ← проверка после деплоя
│
└── docs/
    ├── DEV_ARCH.md                  ← рабочее окружение (актуально)
    ├── ARCHITECTURE.md              ← архитектура приложения (Фаза 9)
    └── UART_PROTOCOL.md             ← протокол STM32 → Pi (Фаза 9)
```

---

## 5. Just команды (полный справочник)

`just` без аргументов выводит все команды с описаниями.

### Хост (до открытия devcontainer)

| Команда | Что делает |
|---|---|
| `./bootstrap.sh` | Установить just, запустить `just pi::bootstrap` |
| `just pi::check-deps` | Проверить just, docker, ssh, rsync |
| `just pi::setup-ssh` | Сгенерировать SSH-ключ, скопировать на Pi |
| `just pi::fetch-sysroot` | Скопировать /opt/vc с Pi для Docker |
| `just pi::build-image` | Собрать Docker образ indicator-build |
| `just pi::bootstrap` | Всё выше в одну команду |
| `just pi::setup-pi` | Первичная настройка Pi (пакеты, ALSA, dirs) |

### Devcontainer (сборка и тесты)

| Команда | Что делает |
|---|---|
| `just build::test` | Host unit-тесты (Debug + ASan/UBSan) |
| `just build::test-verbose` | То же, подробный вывод |
| `just build::pi` | Кросс-компиляция indicator + media_ingest |
| `just build::pi-indicator` | Только indicator |
| `just build::pi-debug` | Debug-сборка (-g3 -O0) для GDB |
| `just build::format` | Применить clang-format |
| `just build::check-format` | Проверить форматирование (без изменений) |
| `just build::clean` | Удалить build/ (с подтверждением) |

### Хост (работа с Pi)

| Команда | Что делает |
|---|---|
| `just pi::deploy` | Бинари + скрипты + systemd units на Pi |
| `just pi::deploy-bin` | Только бинари (быстро) |
| `just pi::deploy-debug` | Debug-бинарь для GDB |
| `just pi::ssh` | SSH на Pi |
| `just pi::logs` | Хвост логов indicator в реальном времени |
| `just pi::logs-ingest` | Хвост логов media-ingest |
| `just pi::status` | Статус всех сервисов |
| `just pi::restart` | Перезапустить indicator |
| `just pi::smoke` | Smoke test после деплоя |
| `just pi::test-audio` | Воспроизвести тестовый WAV на Pi |
| `just pi::gdbserver-start` | SSH-туннель + gdbserver на Pi |
| `just pi::gdbserver-stop` | Остановить gdbserver |
| `just pi::backup-image /dev/diskN` | Создать .img.gz образа SD-карты |

### Алиасы верхнего уровня

| Команда | Что делает |
|---|---|
| `just init` | = `just pi::bootstrap` |
| `just test` | = `just build::test` |
| `just ship` | docker run → `just build::pi` + `just pi::deploy` |

### CI

| Команда | Что делает |
|---|---|
| `just ci::pipeline` | format-check + host tests |
| `just ci::test` | Host unit-тесты |
| `just ci::check-format` | Проверка форматирования |

---

## 6. CMake таргеты

| Таргет | Тип | Хост | Pi |
|---|---|---|---|
| `indicator_domain` | static lib | ✅ | ✅ |
| `unity` | static lib | ✅ | — |
| `test_parser` … `test_config` | executables | ✅ | — |
| `dispmanx_layers` | static lib | ❌ | ✅ |
| `dispmanx_renderer` | static lib | ❌ | ✅ |
| `indicator` | executable | ❌ | ✅ |
| `media_ingest` | executable | ❌ | ✅ |

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
  -DCMAKE_BUILD_TYPE=Release
```

---

## 7. Рабочее окружение

### Разделение контекстов

```
ПК (macOS / Linux / Windows+GitBash)
│
├── Хост
│   ├── just       ← just pi::*
│   ├── docker     ← управление devcontainer
│   ├── ssh/rsync  ← деплой на Pi
│   └── VSCode     ← IDE (Dev Containers extension)
│
├── Devcontainer (Docker: indicator-build, Ubuntu 22.04)
│   ├── arm-zig-cc (zig 0.13.0)      ← кросс-компилятор ARMv6
│   ├── clang-17 / clangd-17          ← host тесты + LSP
│   ├── clang-format-17 / clang-tidy-17
│   ├── gdb-multiarch                 ← remote debug
│   ├── cmake 3.28 / ninja
│   ├── just 1.36
│   └── /opt/vc                       ← Pi sysroot (DispmanX, bcm_host)
│
└── Raspberry Pi Zero W (ARM1176JZF-S, ARMv6ZK)
    ├── Buster Lite + systemd
    ├── SSH ←───── хост
    ├── gdbserver (порт 3333)
    └── /dev/ttyAMA0 ←── STM32
```

### Контексты: строгое правило

| Контекст | Что можно делать |
|---|---|
| Devcontainer | `just build::*` — компиляция, тесты, форматирование |
| Хост | `just pi::*` — деплой, SSH, управление сервисами |
| Хост | `just ship` — docker run build + deploy |

### Типичная сессия разработки

```bash
# ── Devcontainer ──────────────────────────────────────────────
just build::test          # тесты зелёные?
just build::pi            # собирается под Pi?

# ── Хост ──────────────────────────────────────────────────────
just pi::deploy-bin       # быстрый деплой бинарей
just pi::restart          # перезапустить сервис
just pi::logs             # смотреть логи в реальном времени
just pi::smoke            # финальная проверка
```

### Remote GDB

```bash
# 1. Собрать debug (devcontainer):
just build::pi-debug

# 2. Задеплоить (хост):
just pi::deploy-debug

# 3. Запустить туннель (хост, отдельный терминал):
just pi::gdbserver-start

# 4. VSCode → F5 → "🐛 Debug: indicator (Pi Zero W)"
```

GDB подключается через `host.docker.internal:3333`
(SSH-туннель `localhost:3333 → Pi:3333`, открыт `gdbserver-start`).

---

## 8. Тестирование

### Host unit-тесты (Unity, на хосте, с ASan+UBSan)

| Файл | Покрытие |
|---|---|
| `test_parser.c` | CRC-16 корректный/некорректный; parse_frame (SOF/EOF/size/opcode); parse_payload (валидный, токены, пустые, enum bounds); mode_is_valid() |
| `test_floor.c` | 1–9; 10–64; П; П1–П9; −1..−9; нестандартные → -1; граничные значения |
| `test_state.c` | Первый фрейм → события; идентичный → 0; частичные изменения; edge-triggered sound |
| `test_sound_map.c` | DING этажи 1/5/25/40/П; UP с музыкой и без; OVERLOAD; FIRE_ALARM; NONE → valid=false |
| `test_config.c` | Корректный TOML; файл не найден → дефолты; частичный TOML; комментарии; неизвестный ключ |

### On-target smoke test (`just pi::smoke`)

Проверяет: сервисы запущены, бинари существуют, конфиг есть, видеофайл есть, /dev/ttyAMA0 доступен, hifiberry видна в ALSA.

### CI pipeline (`just ci::pipeline`)

```yaml
# GitHub Actions: хост-сборка без Pi
steps:
  - just ci::check-format
  - just ci::test          # host unit-тесты с ASan+UBSan
```

---

## 9. Файлы на устройстве

```
/home/pi/indicator/
├── indicator             ← главный демон
├── media_ingest          ← USB демон
├── config_utility        ← Rust (без изменений)
├── rpi_menu              ← Rust TUI (без изменений)
├── scripts/
│   └── smoke_test.sh
├── configs/device/
│   └── nku_scheme.toml   ← конфигурация (TOML, не pizero.ini)
├── videos/
│   └── output.mp4
├── resources/
│   ├── BACK.png
│   ├── chars/            ← 0.png … 37.png
│   ├── arrows/           ← up.png, down.png
│   ├── modes/            ← firealarm.png, overload.png, calling.png, talking.png
│   ├── weights/          ← load_0.png … load_15.png
│   └── notifications/    ← 0.png … 3.png
└── sounds/
    ├── 1.wav … 20.wav, 30.wav, 40.wav
    ├── 20-.wav, 30-.wav
    ├── floor.wav, podval.wav, minus.wav
    ├── up.wav, down.wav, overload.wav
    ├── closing.wav, opening.wav        ← TODO: подтвердить наличие (В-02)
    ├── g_single.wav, g_double.wav, g_triple.wav  ← TODO: уточнить маппинг (В-01, В-03)
    └── mus1.wav … mus7.wav
```

---

## 10. Фазы реализации

### Фаза 0 — Настройка окружения ✅ ЗАКРЫТА

**Итог:** Pi готова (Buster Lite), Docker-образ собран (Ubuntu 22.04 + LLVM 17 + zig 0.13.0),
кросс-компиляция ARM stub работает, rsync деплой работает.

**Проблемы решены:**
- P-01: созданы stub-файлы для CMake
- P-02/P-03: `build/` переведён с named volume на bind mount
- P-04: `CMAKE_RUNTIME_OUTPUT_DIRECTORY` → все бинари плоско в `build/pi/`
- P-05: создана `deploy/` с `.service`/`.target` файлами
- P-06: `just ship` только с хоста (запускает docker run внутри)
- P-07: Raspbian Buster репозитории перенесены в архив → fix в setup_pi.sh

---

### Фаза 1 — Domain-слой и тесты ✅ ЗАКРЫТА

**Итог:** бизнес-логика написана, 5/5 тестов зелёных, ASan+UBSan чисто, remote GDB работает.

**Уточнения протокола, полученные в фазе:**
- Устройство — Pi Zero W (ARMv6), не 2W; компилятор заменён на zig cc
- Протокол — бинарный фрейм с CRC-16 (см. §12)
- `s_code_t` расширен до 9 значений (включая CLOSING, OPENING, FIRE_ALARM, DONT_WORK, BUTTON)
- `el_mode_t` — разрывный диапазон (0–9, 100, 101, 255); проверка через `mode_is_valid()`
- Конфиг — только `nku_scheme.toml` (TOML), pizero.ini — legacy

**Проблемы решены:**
- P-08: GLIBC_2.34 mismatch → zig cc
- P-09: Segfault до main() на ARMv6 → `-marm -mfloat-abi=hard`, `arm1176jzf_s`
- P-10: Docker arm64/x86 проблемы → Ubuntu 22.04
- P-11: ASan runtime → `libclang-rt-17-dev`
- P-12: стale CMakeCache → `rm -rf build/pi` перед configure
- P-13: clangd красные заголовки → `src/` использует `build/host/compile_commands.json`
- P-14: GDB timeout → `host.docker.internal:3333`
- P-15: SSH UseKeychain → patch в postCreateCommand
- P-16: `cppdbg` требует `ms-vscode.cpptools`, не `cortex-debug`
- P-17: multiple definition of main → отдельный executable на каждый test_*.c

---

### Фаза 2 — UART transport + event loop 🚧 В РАБОТЕ

**Цель:** демон читает UART от STM32, парсит бинарные фреймы, логирует события.

- [ ] `src/transport/uart.c/.h`
  - termios: 115200 baud, 8-bit, no parity, 1 stop bit, raw mode
  - Сборка бинарного фрейма: ожидание SOF `0xAA`, чтение size, накопление data+CRC
  - `frame_ready_cb_t` — коллбэк при полном фрейме
  - Без malloc, без глобального состояния
- [ ] `src/main.c` — poll-цикл
  - `poll()` на uart_fd + signalfd + timerfd
  - signalfd для SIGTERM / SIGINT / SIGCHLD
  - timerfd: heartbeat раз в 30 с (лог статистики)
  - Логирование через syslog: ERROR / WARN / INFO / DEBUG
- [ ] Кросс-компиляция + деплой + проверка `just pi::logs`

**Критерий:** в `journalctl -u indicator -f` виден каждый фрейм от STM32
с распаршенными полями (floor, direction, mode, sound).

**Открытые вопросы для этой фазы:**
- В-04: подтвердить UART параметры на реальном трафике (115200, no parity, 8N1)
- Проверить корректность CRC-16 на живых фреймах от STM32

---

### Фаза 3 — Video player (1–2 дня)

**Цель:** omxplayer под надзором демона, автоперезапуск при падении.

- [ ] `src/player/video_player.c/.h`
  - `posix_spawn` omxplayer (`--layer 1 --no-keys --loop --no-osd --win 0,0,600,1024`)
  - Отслеживание PID
  - `video_player_check_and_restart()` через SIGCHLD + signalfd
- [ ] Проверка: `kill <omxplayer_pid>` → демон перезапускает за < 3 с

---

### Фаза 4 — DispmanX renderer (3–4 дня)

**Цель:** видео + оверлеи работают; полный цикл STM32 → экран.

- [ ] `src/renderer/renderer.h` — интерфейс: `show_sprite_png`, `show_sprite_buffer`, `hide_sprite`
- [ ] `platform/dispmanx/renderer_impl.c/.h`
  - Инициализация display handle
  - 7 слотов с позициями из конфига
  - fast_update для DIGIT_LEFT/RIGHT и ARROW
  - destroy+recreate для WEIGHT, MODE, NOTIFICATION, BACKGROUND
- [ ] null-renderer (stub) для интеграционных тестов на хосте
- [ ] init renderer в `main.c` → BACKGROUND + WEIGHT на старте
- [ ] Обработка всех `state_update_result_t` → вызовы renderer

**Критерий:** все состояния от STM32 корректно отображаются. Latency UART → экран < 100 мс.

---

### Фаза 5 — Audio player (2–3 дня)

**Цель:** аудио через aplay, приоритетная очередь, posix_spawn вместо system().

- [ ] `src/audio/audio.c/.h`
  - pthread + mutex + condvar
  - Кольцевая очередь depth=3 с приоритетами
  - `posix_spawn("aplay", file)` + waitpid
  - `kill(pid, SIGTERM)` при вытеснении
- [ ] `amixer sset 'PCM' N%` при инициализации (из config)
- [ ] Проверка: DING прерывает музыку, CRITICAL прерывает DING

**Критерий:** latency UART → начало звука < 100 мс. Приоритеты работают корректно.

---

### Фаза 6 — systemd + lifecycle (1–2 дня)

**Цель:** корректный запуск с нуля, supervision, логи в journald.

- [ ] Все `.service` файлы в `deploy/` (включая `indicator-setup.service`)
- [ ] `indicator.target` → autostart через `multi-user.target`
- [ ] Проверка: power on → система работает через N секунд (замерить)
- [ ] Проверка: `systemctl kill indicator` → перезапуск за 2 с

---

### Фаза 7 — Media ingest (3–4 дня)

**Цель:** USB-флешка заменяет видео, уведомления на экране.

- [ ] `src_media_ingest/` полностью
  - inotify на /dev (IN_CREATE / IN_DELETE)
  - mount с fallback: /dev/sdX1 → /dev/sdX
  - Поиск .mp4, ffmpeg concat
  - `rename()` для атомарной замены
  - Запись статуса в FIFO
- [ ] `src/media/media_ipc.c/.h` в главном демоне
- [ ] SPRITE_NOTIFICATION через renderer при каждом статусе
- [ ] `video_player_replace()` после MEDIA_STATUS_DONE

**Критерий:** полный цикл USB работает; уведомления отображаются; видео заменяется.

---

### Фаза 8 — Font renderer (после стабилизации Фазы 4)

**Цель:** цифры этажа через растеризацию глифов, без PNG.

- [ ] Перенести font-renderer в `src/font/` или `platform/font/`
- [ ] Адаптер: `font_render_to_rgba(char_code, w, h, rgba_buf)`
- [ ] Переключить DIGIT_LEFT/RIGHT на `renderer_show_sprite_buffer()`
- [ ] Убрать зависимость от `resources/chars/*.png`

---

### Фаза 9 — Полировка (ongoing)

- [ ] `docs/UART_PROTOCOL.md` — финальная спецификация протокола
- [ ] `docs/ARCHITECTURE.md` — финальная архитектура
- [ ] GitHub Actions CI: `just ci::pipeline`
- [ ] Финальные замеры latency (UART → экран, UART → звук)
- [ ] Ревью логов: достаточно ли информации для диагностики в поле?

---

## 11. Что не переписываем

| Файл | Действие |
|---|---|
| `config_utility` (Rust) | Без изменений |
| `rpi_menu` (Rust TUI) | Без изменений |
| Andrew Duncan layers | → `platform/dispmanx/layers/` |
| `nku_scheme.toml` | Без изменений (читаем, не генерируем) |

---

## 12. UART протокол (справочник)

### 12.1 Бинарный фрейм

```
┌──────┬──────┬────────┬──────────────────────┬──────────┬──────────┬──────┐
│ SOF  │ SIZE │ OPCODE │        DATA          │  CRC_HI  │  CRC_LO  │ EOF  │
│ 0xAA │  1B  │   1B   │     SIZE байт        │    1B    │    1B    │ 0xBB │
└──────┴──────┴────────┴──────────────────────┴──────────┴──────────┴──────┘

SOF    = 0xAA           (Start of Frame)
SIZE   = длина DATA в байтах
OPCODE = 0xDA           (индикаторные данные)
DATA   = текстовый payload (см. 12.2)
CRC    = CRC-16/CCITT-FALSE по полю DATA
EOF    = 0xBB           (End of Frame)
```

**Примечание:** CRC считается только по DATA, без SOF/SIZE/OPCODE/EOF.

### 12.2 Текстовый payload (opcode=0xDA)

```
#STM:L<val>:R<val>:A<val>:S<val>:M<val>:E#\r\n

Поле  Токен  Описание
L     2      left_char  — код левого символа дисплея
R     3      right_char — код правого символа дисплея
A     4      arrows_state_t
S     5      s_code_t (звук)
M     6      el_mode_t (режим)
```

STM32 отправляет фреймы **только при изменении состояния** (edge-triggered, не polling).
Звук — тоже edge-triggered: ненулевое значение один раз как событие.

### 12.3 Коды символов (left_char / right_char)

| Код | Символ | Значение |
|-----|--------|----------|
| 0–9 | `'0'`–`'9'` | Цифра |
| 16  | ` ` (пробел) | Пустая позиция |
| 17  | `П` | Подвал |
| 22  | `-` | Минус |

### 12.4 Декодирование этажа (floor_decode)

| left | right | Результат |
|------|-------|-----------|
| 0–9 | 0–9 | двузначный этаж 10–64 |
| 16 | 0–9 | однозначный этаж 1–9 |
| 16 | 17 | П (подвал, код 70) |
| 17 | 1–9 | П1–П9 (коды 71–79) |
| 22 | 1–9 | −1..−9 (коды 81–89) |
| иначе | | -1 (нестандартный) |

### 12.5 arrows_state_t

```c
typedef enum {
    ARROWS_NONE = 0,
    ARROWS_UP   = 1,
    ARROWS_DOWN = 2,
} arrows_state_t;
```

### 12.6 s_code_t (звук)

```c
typedef enum {
    SOUND_NONE       = 0,
    SOUND_DING       = 1,   // анонс этажа
    SOUND_UP         = 2,   // движение вверх
    SOUND_DOWN       = 3,   // движение вниз
    SOUND_CLOSING    = 4,   // двери закрываются
    SOUND_OPENING    = 5,   // двери открываются
    SOUND_OVERLOAD   = 6,   // перегрузка
    SOUND_FIRE_ALARM = 7,   // пожарная тревога
    SOUND_DONT_WORK  = 8,   // не работает
    SOUND_BUTTON     = 9,   // нажатие кнопки
} s_code_t;
```

**WAV-маппинг (частично уточнить — открытые вопросы В-01, В-02):**

| s_code_t | WAV-файлы |
|----------|-----------|
| SOUND_DING | зависит от этажа (см. таблицу sound_map) |
| SOUND_UP | `up.wav` |
| SOUND_DOWN | `down.wav` |
| SOUND_CLOSING | `closing.wav` (TODO: подтвердить В-02) |
| SOUND_OPENING | `opening.wav` (TODO: подтвердить В-02) |
| SOUND_OVERLOAD | `overload.wav` |
| SOUND_FIRE_ALARM | `g_triple.wav` (TODO: уточнить В-01) |
| SOUND_DONT_WORK | `g_double.wav` (TODO: уточнить В-01) |
| SOUND_BUTTON | `g_single.wav` (TODO: уточнить В-01) |

**Логика DING (sound_map_resolve):**
- Этаж 1–20 → `floor.wav` + `N.wav`
- Этаж 21–40 → `floor.wav` + `20.wav` + `N.wav` (где N = остаток)
- Этаж 30, 40 → `floor.wav` + `N.wav` (одно число)
- Этаж П → `podval.wav`
- Этаж П1–П9 → `podval.wav` + `N.wav`
- Этаж −1..−9 → `minus.wav` + `N.wav`
- `has_music=true` → добавить `mus<N>.wav` с пониженной громкостью

### 12.7 el_mode_t (режим)

```c
typedef enum {
    MODE_NORMAL              = 0,
    MODE_FIRE_ALARM          = 1,   // пожарная тревога
    MODE_OVERLOAD            = 2,   // перегрузка
    // ... значения 3–9 ...
    MODE_UPS_MALFUNCTION     = 9,
    MODE_DISPATCH_CALL       = 100, // вызов диспетчера
    MODE_DISPATCH_ANSWER     = 101, // ответ диспетчера
    MODE_CONN_LOST           = 255, // связь потеряна
} el_mode_t;
```

**Диапазон разрывный.** Проверка валидности: `mode_is_valid()`, не `<= MAX`.

### 12.8 Громкость (из nku_scheme.toml)

```toml
[soundvolume]
possible_values = ["0%", "25%", "50%", "75%", "100%"]
default = "50%"

[musicvolume]
possible_values = ["0%", "25%", "50%", "75%", "100%"]
default = "0%"
```

`sound_map_volume_percent()` переводит строки конфига в integer 0–100.
`musicvolume = "0%"` → музыка не воспроизводится (`valid = false`).

---

## 13. Открытые вопросы

| ID | Вопрос | Влияет на |
|----|--------|-----------|
| В-01 | Какие WAV для SOUND_FIRE_ALARM / DONT_WORK / BUTTON? | sound_map.c, тесты |
| В-02 | Есть ли closing.wav и opening.wav в sounds/? | sound_map.c |
| В-03 | Когда g_double / g_single — зависимость от контекста? | sound_map.c |
| В-04 | Подтвердить UART параметры на реальном трафике с STM32 | uart.c |