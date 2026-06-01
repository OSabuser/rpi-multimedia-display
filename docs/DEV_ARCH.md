# Архитектура рабочего окружения

> Проект: Lift Indicator
> Устройство: Raspberry Pi Zero W (ARM1176JZF-S, ARMv6, Debian Buster)
> ssh-add --apple-use-keychain ~/.ssh/id_ed25519 - Добавить ключ в агент с сохранением в Keychain (--apple-use-keychain)
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
│   └── /opt/vc                  ← Pi sysroot (DispmanX, bcm_host)
│
└── Raspberry Pi Zero W  (indicator-01.local)
    ├── SSH ◄──────── хост (деплой, логи, GDB-туннель)
    ├── /dev/ttyAMA0  ◄── UART от STM32
    ├── gdbserver     ← remote debug (порт 3333)
    └── DispmanX + omxplayer ← дисплей
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

`src/` намеренно использует host compile_commands: domain-код платформонезависим,
host clang знает все системные заголовки идеально. arm-zig-cc хранит
системные заголовки внутри `/opt/zig/lib/libc/` — clangd их не находит
без сложного query-driver.

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
5 тест-сьютов: `test_parser`, `test_floor`, `test_sound_map`, `test_state`, `test_config`.

### Кросс-компиляция для Pi (в devcontainer)

```bash
just build::pi       # Release: indicator + media_ingest
just build::pi-debug # Debug: -g3 -O0 -fno-omit-frame-pointer
```

Компилятор: arm-zig-cc → ARMv6, hard-float, ARM32 mode.
Результат: `build/pi/indicator`, `build/pi/media_ingest`.

### Полный цикл (с хоста)

```bash
just ship   # docker run → just build::pi → just pi::deploy
```

---

## Деплой

```bash
just pi::deploy      # бинари + скрипты + systemd units
just pi::deploy-bin  # только бинари (быстро, для итераций)
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
just pi::ssh          # SSH на Pi
just pi::logs         # journalctl -u indicator -f
just pi::logs-ingest  # journalctl -u media-ingest -f
just pi::status       # статус всех сервисов
just pi::restart      # перезапустить indicator
just pi::smoke        # smoke test после деплоя
just pi::test-audio   # проверить аудио
just pi::top          # CPU/RAM
just pi::df           # место на SD-карте
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
just pi::setup-ssh       # SSH-ключ → Pi
just pi::fetch-sysroot   # /opt/vc с Pi → build-env/pi-sysroot/
just pi::build-image     # собрать Docker-образ indicator-build

# 4. VSCode → "Reopen in Container"
#    postCreateCommand почистит macOS-специфичные SSH-опции автоматически

# 5. В devcontainer:
just build::test   # 5/5 тестов зелёных — всё в порядке
just build::pi     # ARM32 бинарь собирается без ошибок

# 6. С хоста:
just pi::setup-pi  # пакеты, ALSA, директории на Pi (один раз)
just pi::deploy    # rsync на Pi
just pi::smoke     # финальная проверка
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
│   └── test_config
│
└── pi/                      ← Pi бинари (arm-zig-cc, ARMv6)
    ├── compile_commands.json   ← используется clangd для platform/
    ├── indicator
    └── media_ingest

build/pi-debug/              ← debug-бинари (-g3 -O0)
    └── indicator-debug
```

`build/` примонтирован как bind mount в devcontainer — файлы видны и на хосте, и в контейнере одновременно.

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