# Lift Indicator — Финальный план


> **Статус:** согласован, готов к реализации
> **Стек:** C11 · DispmanX · omxplayer · ALSA · systemd · CMake · Docker · just

---

## 1. Все принятые решения

| Тема | Решение | Обоснование |
|---|---|---|
| ОС | Raspbian **Buster Lite** (CLI, без X11) | DispmanX и omxplayer работают без GUI; экономия ~100 MB RAM и ~20 с загрузки |
| Автозапуск | **systemd units** | Заменяет LXDE autostart; supervision, логи в journald бесплатно |
| Рендеринг | **DispmanX** (сохраняем) | Единственный вариант с zero-overhead overlay поверх omxplayer на этом железе |
| Видеоплеер | **omxplayer** (сохраняем) | OpenMAX IL, минимальная нагрузка на CPU; заменить только при смене ОС |
| Аудио backend | **ALSA softvol + aplay** | MAX98357 не имеет HW volume в ALSA; softvol в asound.conf — легче PulseAudio |
| Аудио из кода | `posix_spawn("aplay")` в отдельном pthread | Нет shell overhead; latency ~30 ms вместо ~150 ms с `system()` |
| Аудио очередь | Приоритетная, глубина 3 | CRITICAL > FLOOR > MOVEMENT > MUSIC |
| notify | **Упраздняется** | SPRITE_NOTIFICATION — слот в renderer; нет spawn/pkill |
| Рендеринг цифр | **PNG сейчас**, font-renderer — Фаза 8 | Все PNG одного размера → changeSourceImageLayer без мигания |
| BACK.png | RGBA PNG поверх видео, путь в конфиге | Статична сейчас, легко сменить в будущем |
| group_number | Только конфиг MCU | Не используется в логике индикатора |
| Task runner | **just** (в стиле tft_manufacture_test) | `mod build` = devcontainer, `mod pi` = хост |
| Кросс-компиляция | **Docker** с Pi sysroot | Единый образ для macOS / Linux / Windows / CI |
| Деплой | **Base image + rsync** | Flash один раз, обновление приложения через rsync |
| Тесты | **Unity** (vendored, MIT) | Один .c/.h файл, компилируется везде |
| Код Andrew Duncan | **Переносим без изменений** в `platform/dispmanx/layers/` | MIT, корректный, не трогаем |
| inih | **Переносим без изменений** в `third_party/inih/` | MIT, корректный |

---

## 2. Архитектура системы

```bash
Password: 150344@!
Hostname: indicator-01
ssh -i /Users/von_akimow/.ssh/id_ed25519 'pi@indicator-01.local'
```

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
  ├── uart_fd     → сборка фрейма → parse → state_apply_frame()
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
AUDIO_PRIO_CRITICAL = 0   ←  overload (высший)
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
| Config | `src/config/config.c/.h` | Парсинг pizero.ini, дефолты, валидация |
| Protocol types | `src/protocol/types.h` | Enum'ы: direction_t, mode_t, sound_t, parsed_frame_t |
| Protocol parser | `src/protocol/parser.c/.h` | `protocol_parse()` — чистая функция, без malloc |
| Floor codec | `src/domain/floor.c/.h` | Коды символов → номер этажа |
| Sound map | `src/domain/sound_map.c/.h` | sound_t + floor → audio_sequence_t |
| State machine | `src/domain/state.c/.h` | Текущее/предыдущее состояние, diff → события |
| Renderer interface | `src/renderer/renderer.h` | Только API, без bcm_host.h |

### Только Pi

| Модуль | Файл | Ответственность |
|---|---|---|
| UART transport | `src/transport/uart.c/.h` | termios, сборка фреймов, frame_ready_cb |
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
├── bootstrap.sh                     ← уровень 0: uv → just → just pi::bootstrap
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
│   ├── Toolchain-RPiZero2W.cmake    ← arm-linux-gnueabihf-gcc + /opt/vc
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
├── third_party/
│   └── inih/
│       ├── CMakeLists.txt
│       ├── ini.h / ini.c            ← MIT, без изменений
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
│   ├── Dockerfile                   ← arm-gcc + clang + cmake + /opt/vc
│   └── pi-sysroot/                  ← .gitignore; копируется: just pi::fetch-sysroot
│       └── vc/
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
    ├── DEV_ARCH.md                  ← этот документ (рабочее окружение)
    ├── ARCHITECTURE.md              ← архитектура приложения
    └── UART_PROTOCOL.md             ← протокол STM32 → Pi
```

---

## 5. Just команды (полный справочник)

`just` без аргументов выводит все команды с описаниями.

### Хост (до открытия devcontainer)

| Команда | Что делает |
|---|---|
| `./bootstrap.sh` | Установить uv + just, запустить `just pi::bootstrap` |
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
| `just build::format` | Применить clang-format |
| `just build::check-format` | Проверить форматирование (без изменений) |
| `just build::clean` | Удалить build/ (с подтверждением) |

### Хост (работа с Pi)

| Команда | Что делает |
|---|---|
| `just pi::deploy` | Бинари + скрипты + systemd units на Pi |
| `just pi::deploy-bin` | Только бинари (быстро) |
| `just pi::ssh` | SSH на Pi |
| `just pi::logs` | Хвост логов indicator в реальном времени |
| `just pi::logs-ingest` | Хвост логов media-ingest |
| `just pi::status` | Статус всех сервисов |
| `just pi::restart` | Перезапустить indicator |
| `just pi::smoke` | Smoke test после деплоя |
| `just pi::test-audio` | Воспроизвести тестовый WAV на Pi |
| `just pi::backup-image /dev/diskN` | Создать .img.gz образа SD-карты |

### Алиасы верхнего уровня

| Команда | Что делает |
|---|---|
| `just init` | = `just pi::bootstrap` |
| `just test` | = `just build::test` |
| `just ship` | = `just build::pi` + `just pi::deploy` |

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
| `inih` | static lib | ✅ | ✅ |
| `indicator_domain` | static lib | ✅ | ✅ |
| `unity` | static lib | ✅ | — |
| `indicator_tests` | executable | ✅ | — |
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

# Pi (через Docker):
cmake -B build/pi \
  -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-RPiZero2W.cmake \
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
├── Devcontainer (Docker: indicator-build)
│   ├── arm-linux-gnueabihf-gcc   ← кросс-компилятор
│   ├── clang / clangd            ← host тесты + LSP
│   ├── clang-format / clang-tidy
│   ├── cmake / ninja
│   ├── just
│   └── /opt/vc                   ← Pi sysroot (DispmanX, bcm_host)
│
└── Raspberry Pi Zero 2W
    ├── Buster Lite + systemd
    ├── SSH ←───── хост
    └── /dev/ttyAMA0 ←── STM32
```

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

### Деплой нового устройства

```bash
# 1. Flash Buster Lite через rpi-imager (hostname + SSH + WiFi)
# 2. Pi загрузилась, пингуется

# 3. Первичная настройка (один раз на устройство):
just pi::setup-pi         # пакеты, ALSA, директории

# 4. Деплой приложения:
just build::pi            # сборка (devcontainer)
just pi::deploy           # rsync на Pi

# 5. Проверка:
just pi::smoke

# 6. Если нужно размножить — создать образ:
just pi::backup-image /dev/rdisk2
```

---

## 8. Тестирование

### Host unit-тесты (Unity, на хосте, с ASan+UBSan)

| Файл | Покрытие |
|---|---|
| `test_parser.c` | Валидный фрейм; неверный header/postfix; < или > 7 токенов; пустой токен; все варианты enum |
| `test_floor.c` | 1–9; 10–64; П; П1–П9; −1..−9; нестандартные → -1; граничные значения |
| `test_state.c` | Первый фрейм → 5 событий; идентичный → 0; частичные изменения; sound/mode → NONE |
| `test_sound_map.c` | DING этажи 5/25/70; UP с музыкой и без; OVERLOAD; NONE → valid=false; volume=0 → valid=false |
| `test_config.c` | Корректный INI; несуществующий файл → дефолты; частичный INI; out-of-range; неизвестный ключ |

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
│   └── pizero.ini
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
    └── mus1.wav … mus7.wav
```

---

## 10. Фазы реализации

### Фаза 0 — Настройка окружения (1–2 дня)

**Цель:** Pi готова, Docker образ собран, кросс-компиляция работает.

**Pi:**
- [ ] Flash Buster Lite через rpi-imager (hostname, SSH, WiFi, пароль)
- [ ] Дождаться загрузки: `ping indicator-01.local` → ответ
- [ ] `just pi::setup-pi` — пакеты, ALSA softvol, директории
- [ ] Проверить аудио: `just pi::test-audio`
- [ ] Проверить видео: `ssh pi@indicator-01.local "omxplayer --layer 1 /tmp/test.mp4"`

**Хост (macOS):**
- [ ] Распаковать скелет, `git init`, первый коммит
- [ ] `cp .env.example .env`
- [ ] `./bootstrap.sh` → uv + just + SSH ключ + sysroot + Docker образ
- [ ] `just build::test` → убедиться что тесты компилируются (пока пустые, должны пройти)
- [ ] `just build::pi` → убедиться что кросс-компиляция работает (пока пустой main.c)
- [ ] `just pi::deploy` → rsync работает

**Критерий:** `just ship` (build-pi + deploy) завершается без ошибок.

---

### Фаза 1 — Domain-слой и тесты (3–4 дня)

**Цель:** бизнес-логика написана, покрыта тестами, ASan+UBSan чисто.

- [ ] `src/protocol/types.h` — все enum'ы (direction_t, mode_t, sound_t, parsed_frame_t)
- [ ] `src/protocol/parser.c/.h` — `protocol_parse()`, без malloc, без side effects
- [ ] `src/domain/floor.c/.h` — `floor_decode()`
- [ ] `src/domain/sound_map.c/.h` — `sound_map_resolve()`, `sound_map_volume_percent()`
- [ ] `src/domain/state.c/.h` — `state_apply_frame()` → `state_update_result_t`
- [ ] `src/config/config.c/.h` — `config_load()`, дефолты, валидация
- [ ] Все unit-тесты в `tests/`
- [ ] `just build::test` → зелёный, ASan+UBSan без ошибок

**Критерий:** 100% тестов зелёные. Ни строчки DispmanX.

---

### Фаза 2 — UART transport + event loop (2–3 дня)

**Цель:** демон читает UART от STM32, парсит фреймы, логирует события.

- [ ] `src/transport/uart.c/.h` — termios, frame assembly, `frame_ready_cb_t`, без malloc
- [ ] `src/main.c` — poll-цикл: uart_fd + signalfd + timerfd
- [ ] Логирование через syslog (уровни: ERROR / WARN / INFO / DEBUG)
- [ ] Кросс-компиляция + деплой + `just pi::logs`

**Критерий:** в `journalctl -u indicator -f` виден каждый фрейм от STM32 с распаршенными полями.

---

### Фаза 3 — Video player (1–2 дня)

**Цель:** omxplayer под надзором демона, автоперезапуск при падении.

- [ ] `src/player/video_player.c/.h`
  - `posix_spawn` omxplayer (`--layer 1 --no-keys --loop --no-osd --win 0,0,W,H`)
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
- [ ] Обработка всех `state_event_t` → вызовы renderer

**Критерий:** все состояния от STM32 корректно отображаются. Замерить latency: UART → экран (цель < 100 мс).

---

### Фаза 5 — Audio player (2–3 дня)

**Цель:** аудио через aplay, приоритетная очередь, posix_spawn вместо system().

- [ ] `src/audio/audio.c/.h`
  - pthread + mutex + condvar
  - Кольцевая очередь depth=3 с приоритетами
  - `posix_spawn("aplay", file)` + waitpid
  - `kill(pid, SIGTERM)` при вытеснении
- [ ] `amixer sset 'PCM' N%` при инициализации (из config)
- [ ] Проверка приоритетов: DING прерывает музыку, CRITICAL прерывает DING

**Критерий:** latency UART → начало звука < 100 мс. Приоритеты работают корректно.

---

### Фаза 6 — systemd + lifecycle (1–2 дня)

**Цель:** корректный запуск с нуля, supervision, логи в journald.

- [ ] Все `.service` файлы в `deploy/`
- [ ] `indicator.target` → autostart
- [ ] Проверка: `power on` → система работает через N секунд (замерить)
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

- [ ] Перенести свой font-renderer в `src/font/` или `platform/font/`
- [ ] Адаптер: `font_render_to_rgba(char_code, w, h, rgba_buf)`
- [ ] Переключить DIGIT_LEFT/RIGHT на `renderer_show_sprite_buffer()`
- [ ] Убрать зависимость от `resources/chars/*.png`

---

### Фаза 9 — Полировка (ongoing)

- [ ] `docs/UART_PROTOCOL.md` — финальная спецификация протокола
- [ ] `docs/ARCHITECTURE.md` — финальная архитектура приложения
- [ ] GitHub Actions CI: `just ci::pipeline`
- [ ] Финальные замеры latency (UART → экран, UART → звук)
- [ ] Ревью логов: достаточно ли информации для диагностики в поле?

---

## 11. Что не переписываем

| Файл | Действие |
|---|---|
| `config_utility` (Rust) | Без изменений |
| `rpi_menu` (Rust TUI) | Без изменений |
| `ini.c/.h` | → `third_party/inih/` |
| Andrew Duncan layers | → `platform/dispmanx/layers/` |
| `pizero.ini`, `config.txt` | Без изменений |

---

## 12. UART протокол (справочник)

```
Формат: #STM:<X><val>:<X><val>:<X><val>:<X><val>:<X><val>:E#\r\n

Токен 1: #STM  (header)
Токен 2: left_char   — код левого символа
Токен 3: right_char  — код правого символа
Токен 4: direction   — 0=None, 1=Up, 2=Down
Токен 5: sound       — 0=None, 1=Ding, 2=Up, 3=Down, 4=Closing, 5=Opening, 6=Overload
Токен 6: mode        — 0=None, 3=FireAlarm, 6=Overload, 50=Calling, 51=Talking
Токен 7: E#\r\n (postfix)

Кодирование символов:
  0–9   → цифра
  16    → пробел (пустая позиция)
  17    → П (подвал)
  22    → - (минус)

Декодирование этажа:
  left=0–9,  right=0–9  → two-digit floor (10–64)
  left=16,   right=0–9  → single-digit floor (1–9)
  left=16,   right=17   → П (70)
  left=17,   right=1–9  → П1–П9 (71–79)
  left=22,   right=1–9  → −1..−9 (81–89)
  otherwise             → -1 (нестандартный)
```
