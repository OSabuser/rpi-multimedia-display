# Архитектура рабочего окружения

> Проект: Lift Indicator (Raspberry Pi Zero 2W)

---

## Концепция

Рабочее окружение разделено на два контекста с чёткой границей:

**Devcontainer** — всё что касается кода: сборка (host + Pi), тесты, форматирование.
Управляется через `just build::`.

**Хост** — всё что касается железа: деплой на Pi, SSH, настройка устройства.
Управляется через `just pi::`.

---

## Компоненты окружения

```bash
ПК разработчика (macOS / Linux / Windows + Git Bash)
│
├── Хост
│   ├── just          ← задачи хостового уровня (just pi::*)
│   ├── docker        ← управление devcontainer
│   ├── ssh / rsync   ← деплой на Pi
│   └── VSCode        ← IDE (Dev Containers extension)
│
├── Devcontainer (Docker: indicator-build)
│   ├── arm-linux-gnueabihf-gcc  ← кросс-компилятор для Pi
│   ├── clang / clangd           ← host тесты + LSP
│   ├── clang-format / tidy      ← форматирование, анализ
│   ├── cmake / ninja
│   ├── just
│   ├── /opt/vc (Pi sysroot)     ← DispmanX, bcm_host
│   └── Unity                    ← фреймворк host-тестов
│
└── Raspberry Pi Zero 2W
    ├── SSH ──────────────────▶ хост (деплой, логи)
    ├── /dev/ttyAMA0           ← UART от STM32
    └── DispmanX + omxplayer   ← дисплей
```

---

## Что где выполняется  

| Задача | Контекст | Команда |
|---|---|---|
| Host unit-тесты | devcontainer | `just build::test` |
| Кросс-компиляция под Pi | devcontainer | `just build::pi` |
| Форматирование кода | devcontainer | `just build::format` |
| Первичная настройка Pi | хост | `just pi::setup-pi` |
| Деплой на Pi | хост | `just pi::deploy` |
| Просмотр логов | хост | `just pi::logs` |
| Smoke test | хост | `just pi::smoke` |
| Создание образа SD | хост | `just pi::backup-image /dev/diskN` |

---

## Быстрый старт

```bash
git clone <repo-url>
cd lift-indicator

# 1. Установить just + uv (один раз)
./bootstrap.sh

# 2. Скопировать .env
cp .env.example .env
# Отредактировать: PI_HOST=indicator-01.local

# 3. Настройка хоста (SSH ключ + sysroot + Docker образ)
just pi::bootstrap

# 4. Открыть в VSCode → "Reopen in Container"

# 5. Внутри devcontainer:
just build::test    # убедиться что тесты зелёные
just build::pi      # собрать для Pi

# 6. На хосте:
just pi::deploy     # задеплоить на Pi
just pi::smoke      # проверить
just pi::logs       # смотреть логи
```

---

## Конфигурация — `.env`

`.env` в корне — единый источник конфигурации. `.env.example` коммитится в git.

```ini
PI_HOST=indicator-01.local   # hostname Pi (или IP)
PI_USER=pi
PI_DIR=/home/pi/indicator
BUILD_DIR=build
```
