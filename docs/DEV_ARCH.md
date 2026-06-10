# Архитектура рабочего окружения

> Проект: Lift Indicator
> Устройство: Raspberry Pi Zero W Rev 1.1 (ARM1176JZF-S, ARMv6ZK, Debian Buster)
> `ssh-add --apple-use-keychain ~/.ssh/id_ed25519` — добавить ключ в агент с сохранением в Keychain
> `ffmpeg -i input.mp4 -c:v libx264 -profile:v baseline -an output.mp4` - конвертация видео в H.264 MP4 для загрузки на USB-носитель

---

## Концепция

Рабочее окружение разделено на два контекста с чёткой границей:

**Devcontainer** — всё что касается кода: сборка (host + Pi), тесты, форматирование, LSP.
Управляется через `just build::`.

**Хост** — всё что касается железа: деплой на Pi, SSH, настройка устройства, отладка.
Управляется через `just pi::`.

Правило строгое: кросс-компиляция только в devcontainer, деплой и SSH только с хоста.
Исключение: `just ship` запускается с хоста и вызывает docker run внутри себя.

---

## Компоненты окружения

```bash
ПК разработчика (macOS / Linux)
│
├── Хост
│   ├── just          ← just pi::* (деплой, SSH, отладка)
│   ├── docker        ← управление devcontainer
│   ├── ssh / rsync   ← деплой, туннель GDB
│   └── VSCode        ← IDE (Dev Containers extension)
│
├── Devcontainer (Docker: indicator-build)  ← Ubuntu 22.04 + LLVM 17 + zig
│   ├── arm-zig-cc               ← кросс-компилятор для Pi (zig cc wrapper)
│   ├── clang-17                 ← host unit-тесты (ASan + UBSan)
│   ├── clangd-17                ← LSP (два профиля: host / pi)
│   ├── clang-format-17          ← форматирование по .clang-format
│   ├── clang-tidy-17            ← статический анализ
│   ├── gdb-multiarch            ← remote debug → Pi Zero W
│   ├── cmake 3.28 / ninja
│   ├── just
│   └── /opt/vc + /usr/          ← Pi sysroot (DispmanX, bcm_host, libpng16, zlib)
│
└── Raspberry Pi Zero W  (indicator-01.local)
    ├── SSH ◄──────── хост (деплой, логи, GDB-туннель)
    ├── /dev/ttyAMA0  ◄── UART от STM32 (115200 8N1)
    ├── gdbserver     ← remote debug (порт 3333)
    ├── DispmanX + omxplayer ← дисплей
    └── /data/        ← writable data (bind mount source)
```

---

## Почему zig cc, а не arm-linux-gnueabihf-gcc

`arm-linux-gnueabihf-gcc` из Debian/Ubuntu содержит `crt1.o`, `crtbegin.o`
скомпилированные под **ARMv7 Thumb-2**. На Pi Zero W (ARMv6) они вызывают
segfault до запуска `main()`.

`zig cc -target arm-linux-gnueabihf -mcpu=arm1176jzf_s` компилирует
собственные crt-объекты точно под указанный CPU — это тот же механизм,
который использует Rust/cross+zig.

Wrapper `/usr/local/bin/arm-zig-cc`:

```sh
exec zig cc -target arm-linux-gnueabihf -mcpu=arm1176jzf_s "$@"
```

Флаги компиляции (`cmake/Toolchain-RPiZeroW.cmake`):

```bash
-marm -mfloat-abi=hard
```

`-marm` обязателен: Thumb-1 + hard-float VFP — неподдерживаемая комбинация в GCC.

---

## Dockerfile — двухстадийная сборка

```bash
Stage 1 (zig-stage): скачать zig → /opt/zig/
Stage 2 (dev):       Ubuntu 22.04 + LLVM 17 + copy zig from stage 1
```

Двухстадийность даёт кэширование: при обновлении clangd zig-слой не
пересобирается. Оба стейджа arch-aware (x86_64 / aarch64).

Sysroot (два шага, один раз):

```bash
just pi::fetch-sysroot       # /opt/vc с Pi → build-env/pi-sysroot/opt/vc/
just pi::fetch-png-sysroot   # libpng16.so + headers → build-env/pi-sysroot/usr/
```

Сборка образа:

```bash
just pi::build-image   # из корня репо (с хоста)
```

---

## LSP — clangd

clangd настроен через `.clangd` в корне репо с тремя профилями:

| Путь | compile_commands | Компилятор | Назначение |
|---|---|---|---|
| `tests/` | `build/host` | clang-17 x86/arm64 | LSP для тестов |
| `src/` | `build/host` | clang-17 x86/arm64 | LSP для domain-кода |
| `platform/` | `build/pi` | arm-zig-cc | LSP для DispmanX |

`src/` намеренно использует host compile\_commands: domain-код платформонезависим,
host clang знает все системные заголовки идеально.

`third_party/` и `platform/dispmanx/layers/` — clang-tidy отключён
(vendored/legacy код).

Для работы clangd оба индекса должны быть созданы:

```bash
just build::test   # → build/host/compile_commands.json
just build::pi     # → build/pi/compile_commands.json
```

После изменений: `Ctrl+Shift+P` → **clangd: Restart Language Server**.

---

## Форматирование — clang-format

Конфиг: `.clang-format` в корне (BasedOnStyle: LLVM, IndentWidth: 4, Allman braces).

```bash
just build::format        # применить форматирование
just build::check-format  # проверить без изменений (используется в CI)
```

В VSCode форматирование применяется автоматически при сохранении (`formatOnSave: true`).

---

## Сборка

### Host unit-тесты (в devcontainer)

```bash
just build::test          # Debug + ASan + UBSan
just build::test-verbose  # с подробным выводом
```

Компилятор: clang-17. Каждый `test_*.c` → отдельный исполняемый файл.
6 тест-суитов: `test_parser`, `test_floor`, `test_sound_map`, `test_state`, `test_config`, `test_uart`.

### Кросс-компиляция для Pi (в devcontainer)

```bash
just build::pi          # Release: indicator + media_ingest + uart_rx_dump
just build::pi-debug    # Debug: -g3 -O0 -fno-omit-frame-pointer
just build::pi-dump     # Только uart_rx_dump
just build::pi-ingest   # Только media_ingest
```

Компилятор: arm-zig-cc → ARMv6, hard-float, ARM32 mode.
Результат: `build/pi/indicator`, `build/pi/media_ingest`, `build/pi/uart_rx_dump`.

### Полный цикл (с хоста)

```bash
just ship   # docker run → just build::pi → just pi::deploy → check-resources → restart
```

---

## Деплой

### Быстрый деплой (при разработке)

```bash
just pi::deploy       # бинари + tools + Rust + скрипты + systemd + конфиги
just pi::deploy-bin   # только C-бинари (indicator + media_ingest) — быстро для итераций
just pi::restart      # перезапустить indicator.service
```

### Полный деплой (первая установка / обновление ресурсов)

```bash
just pi::deploy-full  # deploy + ресурсы + звуки + splash + видео
```

### Деплой по частям

```bash
just pi::deploy-systemd   # systemd units + daemon-reload
just pi::deploy-configs   # TOML конфиги → /data/pi_nku_configs/
just pi::deploy-resources # PNG ресурсы → /data/resources/
just pi::deploy-sounds    # WAV файлы → /data/sounds/
just pi::deploy-splash    # boot splash.jpg
just pi::deploy-video     # output.mp4 → /data/videos/
just pi::deploy-rust      # pi_nku_sync + pi_nku_menu
just pi::deploy-tools     # uart_rx_dump + MUp-rpi0
just pi::deploy-scripts   # run_setup.sh, first_boot.sh, smoke_test.sh, check_resources.sh
```

Деплой использует rsync. Pi должна быть доступна по SSH-ключу.

---

## Remote Debug (GDB)

Схема:

```bash
VSCode (devcontainer)
  gdb-multiarch
      │ host.docker.internal:3333
      │
  SSH tunnel (хост)  localhost:3333 → Pi:3333
                              │
                         gdbserver
                              │
                      indicator-debug (Pi)
```

`host.docker.internal` — стандартный DNS-адрес хостовой машины из контейнера,
регистрируется через `--add-host=host.docker.internal:host-gateway` в `runArgs`.

### Первичная настройка (один раз)

```bash
just pi::setup-gdbserver  # установить gdbserver на Pi
```

### Рабочий цикл отладки

```bash
# 1. Собрать debug-бинарь (devcontainer):
just build::pi-debug

# 2. Задеплоить на Pi (хост):
just pi::deploy-debug

# 3. Запустить туннель (хост, отдельный терминал):
just pi::gdbserver-start
# Терминал "висит" — туннель активен пока окно открыто

# 4. В VSCode: F5 → "🐛 Debug: indicator (Pi Zero W)"
#    Breakpoint → Variables → Step (F10/F11)

# 5. Смотреть stdout программы (хост):
just pi::debug-output   # tail -f /tmp/gdbserver.log на Pi
```

Завершить сеанс: **Shift+F5** → туннель закрывается автоматически через tasks.json.

### Проверить доступность туннеля (devcontainer)

```bash
timeout 3 bash -c 'echo > /dev/tcp/host.docker.internal/3333' && echo OK || echo FAIL
```

---

## Управление Pi

```bash
just pi::ssh              # SSH на Pi
just pi::logs             # journalctl -u indicator -f
just pi::logs-ingest      # journalctl -u media-ingest -f
just pi::logs-tail [n]    # последние N строк обоих сервисов (default 50)
just pi::status           # статус всех сервисов
just pi::restart          # перезапустить indicator
just pi::restart-audio    # перезапустить i2s-silence.service (при зависании dmix)
just pi::smoke            # smoke test после деплоя
just pi::check-resources  # валидация 144 ресурсов, конфигов, bind-монтов
just pi::test-audio       # проверить аудио (aplay тестового WAV)
just pi::test-notif       # тест SPRITE_NOTIFICATION — показать все 8 PNG на живом дисплее
just pi::test-video       # ffmpeg test.mp4 → Pi → omxplayer
just pi::dump             # uart_rx_dump (indicator стоп → слушать → поднять)
just pi::dump-passive     # uart_rx_dump без остановки indicator
just pi::top              # CPU/RAM
just pi::df               # место на SD-карте
just pi::backup-image /dev/diskN  # создать .img.gz
just pi::update-start      # overlayfs выключить + reboot (начало обновления)
just pi::update-finish     # overlayfs включить + reboot (конец обновления)
just pi::overlay-status    # показать статус overlayfs
just pi::reboot            # перезагрузить Pi
```

---

## Быстрый старт

```bash
git clone <repo-url>
cd lift-indicator

# 1. Установить just на хосте
./bootstrap.sh

# 2. Переменные окружения
cp .env.example .env
# Отредактировать: PI_HOST=indicator-01.local

# 3. Первичная настройка (один раз):
just pi::setup-ssh           # SSH-ключ → Pi
just pi::fetch-sysroot       # /opt/vc с Pi → build-env/pi-sysroot/
just pi::fetch-png-sysroot   # libpng16.so + headers с Pi
just pi::build-image         # собрать Docker-образ indicator-build

# 4. VSCode → "Reopen in Container"
#    postCreateCommand почистит macOS-специфичные SSH-опции автоматически

# 5. В devcontainer:
just build::test   # 6/6 суитов зелёных — всё в порядке
just build::pi     # ARM32 бинари собираются без ошибок

# 6. С хоста (первичная настройка Pi):
just pi::setup-pi      # пакеты, ALSA, /data layout, fstab bind-монты
just pi::deploy-full   # все бинари + ресурсы + звуки + конфиги + видео
just pi::enable-services  # systemctl enable для всех сервисов
just pi::smoke         # финальная проверка

# 7. При разработке (итерация):
just ship              # build::pi → deploy → check-resources → restart

# 8. Прошивка нового устройства:

#    - balenaEtcher → indicator-base-YYYYMMDD.img.gz → карта
#    - Положить wpa_supplicant.conf в /boot/ (FAT32, с macOS)
#    - Вставить карту, включить Pi → 2 авто-ребута → готово

# 9. Обновление ПО на production:

just pi::update-start   # overlayfs OFF + reboot
just pi::deploy         # rsync
just pi::restart
just pi::update-finish  # overlayfs ON + reboot
```

---

## Конфигурация — `.env`

`.env` в корне — единый источник конфигурации. `.env.example` коммитится в git, `.env` — нет.

```ini
PI_HOST=indicator-01.local   # hostname Pi (или IP)
PI_USER=pi
PI_DIR=/home/pi/indicator
BUILD_DIR=build
```

---

## Структура build/

```bash
build/
├── host/                    ← host тесты (clang, x86/arm64)
│   ├── compile_commands.json   ← используется clangd для src/ и tests/
│   ├── test_parser
│   ├── test_floor
│   ├── test_sound_map
│   ├── test_state
│   ├── test_config
│   └── test_uart
│
└── pi/                      ← Pi бинари (arm-zig-cc, ARMv6)
    ├── compile_commands.json   ← используется clangd для platform/
    ├── indicator               ← главный демон
    ├── media_ingest            ← USB демон
    ├── uart_rx_dump            ← диагностическая утилита
    └── platform/dispmanx/
        └── libdispmanx_renderer.a

build/pi-debug/              ← debug-бинари (-g3 -O0)
    └── indicator-debug
```

`build/` примонтирован как bind mount в devcontainer — файлы видны и на хосте, и в контейнере одновременно.

---

## Файловая структура на Pi

```bash
/home/pi/indicator/
├── indicator              ← C-демон
├── media_ingest           ← USB демон
├── pi_nku_sync            ← Rust: синхронизация конфига с MCU
├── pi_nku_menu            ← Rust TUI: редактирование параметров
├── pi_nku_configs/        ← bind mount → /data/pi_nku_configs/
├── resources/             ← bind mount → /data/resources/
├── sounds/                ← bind mount → /data/sounds/
├── videos/                ← bind mount → /data/videos/
├── splash/
│   └── splash.jpg
├── scripts/
│   ├── run_setup.sh       ← splash → pull → menu → push → status
│   ├── first_boot.sh      ← hostname + SSH keys (один раз)
│   ├── smoke_test.sh
│   └── check_resources.sh
└── tools/
    ├── uart_rx_dump
    └── MUp-rpi0

/data/                     ← p3 ext4 раздел (LABEL=data), writable, persistent при overlayfs
├── first_boot_done        ← флаг первого старта
├── setup_status           ← ok | pull_failed | push_failed | pending
├── pi_nku_configs/
├── resources/
├── sounds/
├── videos/
│   └── output.mp4
└── wpa_supplicant.conf    ← WiFi credentials (bind-mount → /etc/wpa_supplicant/wpa_supplicant.conf)

/run/indicator/
└── media_status.fifo          ← FIFO IPC indicator ↔ media-ingest (tmpfs, tmpfiles.d)
```

---

## Типичные проблемы

| Симптом | Причина | Решение |
|---|---|---|
| `CMake Warning: CMAKE_TOOLCHAIN_FILE was not used` | Старый `CMakeCache.txt` | `rm -rf build/pi && just build::pi` |
| clangd подчёркивает все заголовки красным | Нет compile_commands | `just build::test && just build::pi` |
| `ld: cannot find libclang_rt.asan-aarch64.a` | Нет `libclang-rt-17-dev` | Пересобрать Docker-образ |
| SSH: `Bad configuration option: usekeychain` | macOS SSH config в контейнере | Rebuild Container (postCreateCommand патчит автоматически) |
| GDB: `Connection timed out` на `localhost:3333` | Туннель на хосте, gdb в контейнере | Использовать `host.docker.internal:3333` |
| `printf` не виден в debug console | stdout буферизован в файл | `just pi::debug-output` или добавить `fflush(stdout)` |
| SIGSEGV до `main()` при первом запуске | `libpng16.a` ARMv7 статически слинкована | Запустить `just pi::fetch-png-sysroot` (берёт `.so` с Pi) |
| `indicator.service: Failed with result 'timeout'` при stop | dbus-daemon вне pgroup omxplayer | Ожидаемо (P-24), не блокирует; исправить в Deploy v2 |
| Видео зависает после длительного простоя | VideoCore IV dormant state | `renderer_keepalive()` вызывается из watchdog tick — уже реализовано |
| `just pi::dump` — порт хардкодирован | TODO в pi.just | Временно: /dev/ttyAMA0 115200 работает на реальном устройстве |
| Звук пропал, aplay зависает без вывода | dmix IPC deadlock (P-34): aplay убит в момент удержания семафора | `sudo killall -9 aplay && ipcs -m | awk 'NR>3 && $3=="pi"' | xargs -r ipcrm -m && just pi::restart-audio` |
| `i2s-silence.service` падает с кодом 1 при старте | I2S карта ещё не инициализирована (P-37) | Убедиться что в service есть `ExecStartPre` ожидающий card 0; `just pi::setup-pi` устанавливает правильный unit |
| Щелчки при каждом звуке | Нет I2S keepalive | `just pi::restart-audio` — проверить что `i2s-silence.service` active |
| `found 2 MP4 file(s)` при одном файле | macOS AppleDouble `._video.mp4` на флешке | Исправлено в Фазе 7: фильтр `p_name[0]=='.'`; для старых устройств: `dot_clean /Volumes/<флешка>` на macOS |
| `h264_omx` зависает на несколько минут | Конкуренция с omxplayer за VideoCore IV `/dev/vchiq` | Использовать `-c copy` (мгновенно для H.264 MP4) или libx264 (медленно, без конфликтов) |
| `media-ingest.service: inactive (dead)` | `indicator.target` не включён | `sudo systemctl enable indicator.target && sudo systemctl start indicator.target` |
| `media_ipc: mkdir '/run/indicator': Permission denied` | Нет `RuntimeDirectory=indicator` в indicator.service | Добавить `RuntimeDirectory=indicator` в `[Service]` секцию |
| WiFi не поднимается после первого старта | Race condition dhcpcd/wpa_supplicant при overlayfs (P-38) | `sudo systemctl enable wpa_supplicant.service` |
| Запись в rootfs теряется после ребута | overlayfs активен — записи идут в tmpfs | Использовать `just pi::update-start` → deploy → `just pi::update-finish` |
| Нельзя записать в `/lower` | raspi-config overlayfs не экспортирует `/lower` в user-space (P-39) | 2-reboot update cycle (см. выше) |
| WiFi credentials сброшены после ребута | Конфиг на rootfs, не на `/data/` | Редактировать `/data/wpa_supplicant.conf` напрямую |