# Lift Indicator — Отчёт Фазы 0 и Вопросы Фазы 1

**Дата:** 2025-05-27  
**Проект:** `rpi-multimedia-display`  
**Устройство:** Raspberry Pi Zero 2W · Debian Buster · дисплей 600×1024

---

## 1. Фаза 0 — ЗАКРЫТА ✅

**Цель фазы:** Pi готова, Docker-образ собран, кросс-компиляция работает, деплой проходит без ошибок.

### 1.1 Чеклист

| # | Пункт | Статус |
|---|---|---|
| **Pi** | | |
| 1 | Flash Buster Lite, hostname `indicator-01.local`, SSH, WiFi | ✅ |
| 2 | `just pi::setup-pi` — пакеты, ALSA softvol, директории | ✅ |
| 3 | `just pi::test-audio` — звук воспроизводится | ✅ |
| 4 | `omxplayer /tmp/test.mp4` — видео воспроизводится | ✅ |
| **Хост / Devcontainer** | | |
| 5 | Скелет репозитория, `git init`, первый коммит | ✅ |
| 6 | `bootstrap.sh` → sysroot `/opt/vc` скопирован, Docker-образ `indicator-build` собран | ✅ |
| 7 | `just build::test` → стабы компилируются, тесты проходят (пустые) | ✅ |
| 8 | `just build::pi` → ARM-бинари `indicator` + `media_ingest` | ✅ |
| 9 | `build/` примонтирован как `type=bind` — виден на хосте | ✅ |
| 10 | `CMAKE_RUNTIME_OUTPUT_DIRECTORY` — оба бинаря плоско в `build/pi/` | ✅ |
| 11 | `just pi::deploy` → rsync бинари + скрипты + systemd-юниты | ✅ |
| 12 | `deploy/` — созданы `.service` + `.target` файлы | ✅ |
| **Критерий фазы** | | |
| 13 | `just ship` с хоста завершается без ошибок | ✅ |

**Критерий выполнен.**

---

### 1.2 Проблемы, возникшие в процессе, и их решения

#### P-01 — Отсутствовали stub-файлы для CMake

**Симптом:** `just build::test` и `just build::pi` падали с ошибкой `Cannot find source file`.

**Причина:** CMakeLists.txt был написан под финальную структуру, но файлы ещё не существовали.

**Решение:** созданы минимальные Phase-0 стабы для всех модулей:
`src/config/`, `src/protocol/`, `src/domain/`, `src/transport/`, `src/audio/`, `src/player/`, `src/media/`, `platform/dispmanx/`, `src_media_ingest/`, `third_party/inih/`, `tests/unity/`, `tests/test_*.c`.

---

#### P-02 — `build/` был named Docker volume, невидимый на хосте

**Симптом:** `just pi::deploy` падал с `Binaries not found`. На хосте `ls build/` — пусто, хотя внутри контейнера `build/pi/indicator` существовал.

**Причина:** в `.devcontainer/devcontainer.json` была строка:

```json
"source=indicator-build-cache,target=/project/build,type=volume"
```

`type=volume` — изолированный Docker volume, недоступный с хоста.

**Решение:** заменить на bind mount:

```json
"source=${localWorkspaceFolder}/build,target=/project/build,type=bind,consistency=delegated"
```

Удалить старый volume: `docker volume rm indicator-build-cache`.  
Пересоздать контейнер: `Dev Containers: Rebuild Container Without Cache`.

---

#### P-03 — Конфликт двух mount-точек при rebuild контейнера

**Симптом:** контейнер не запускался, ошибка `docker run` — два mount на `/project/build`.

**Причина:** VSCode закэшировал старый `devcontainer.json` и применял `indicator-build-cache` volume одновременно с новым bind mount.

**Решение:**

```bash
docker ps -aq --filter "label=devcontainer.local_folder=${PWD}" | xargs docker rm -f
docker volume rm indicator-build-cache
mkdir -p build
# Dev Containers: Rebuild Container Without Cache
```

---

#### P-04 — `media_ingest` оказался во вложенной директории

**Симптом:** `ls build/pi/media_ingest` — нет файла. Бинарь находился в `build/pi/src_media_ingest/media_ingest`.

**Причина:** `indicator` объявлен в корневом CMakeLists.txt и попадает в `build/pi/` напрямую. `media_ingest` объявлен в `add_subdirectory(src_media_ingest)` и попадает в соответствующую поддиректорию.

**Решение:** добавить в `CMakeLists.txt` сразу после `project(...)`:

```cmake
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
```

Теперь все исполняемые файлы плоско в `build/pi/`.

---

#### P-05 — `deploy/` не существовала

**Симптом:** `just pi::deploy` завершался с `rsync error: code 23` при синхронизации systemd-юнитов.

**Решение:** создана директория `deploy/` с файлами:

- `indicator.service`
- `media-ingest.service`
- `indicator.target`

---

#### P-06 — `just ship` запускался внутри контейнера

**Симптом:** `rsync: Failed to exec ssh: No such file or directory` внутри контейнера.

**Причина:** `ship` вызывался из devcontainer, где нет `ssh`/`rsync` для работы с Pi.

**Правило зафиксировано:**

```bash
Контейнер:  just build::*   (компиляция, тесты)
Хост:       just pi::*      (деплой, SSH, управление сервисами)
just ship   запускается ТОЛЬКО с хоста
```

**Рекомендуемый `ship` для хоста** (добавить в корневой `justfile`):

```just
ship:
    #!/usr/bin/env bash
    set -euo pipefail
    docker run --rm \
        -v "{{justfile_directory()}}:/project" \
        -v "{{justfile_directory()}}/build:/project/build" \
        -w /project \
        indicator-build \
        just build::pi
    just pi::deploy
```

---

#### P-07 — Raspbian Buster репозитории ушли в архив (декабрь 2025)

**Симптом:** `apt update` падает на чистой Pi с ошибками 404.

**Решение:** добавить в начало `scripts/setup_pi.sh`:

```bash
fix_buster_apt_sources() {
    sudo tee /etc/apt/sources.list > /dev/null <<'EOF'
deb http://archive.debian.org/debian buster main contrib non-free
deb http://archive.debian.org/debian-security buster/updates main contrib non-free
EOF
    sudo tee /etc/apt/sources.list.d/raspi.list > /dev/null <<'EOF'
deb http://archive.raspberrypi.org/debian/ buster main
EOF
    sudo tee /etc/apt/apt.conf.d/99archive > /dev/null <<'EOF'
Acquire::Check-Valid-Until "false";
EOF
}
fix_buster_apt_sources
sudo apt-get update
```

---

### 1.3 Итоговое состояние окружения

```text
Хост (macOS)
├── just, docker, ssh, rsync — установлены
├── build-env/pi-sysroot/vc/ — скопирован с Pi
├── Docker-образ indicator-build — готов
├── build/                   — bind mount, виден на хосте и в контейнере
└── .env                     — PI_HOST=indicator-01.local

Devcontainer (indicator-build)
├── arm-linux-gnueabihf-gcc  — кросс-компилятор
├── clang 14 / clangd        — host тесты + LSP
├── cmake / ninja / just
└── /opt/vc                  — DispmanX sysroot

Raspberry Pi Zero 2W (indicator-01.local)
├── Debian Buster Lite
├── /home/pi/indicator/      — директория приложения
│   ├── indicator            ← ARM stub-бинарь ✅
│   ├── media_ingest         ← ARM stub-бинарь ✅
│   ├── scripts/smoke_test.sh
│   ├── configs/
│   ├── resources/
│   ├── sounds/
│   └── videos/
└── /etc/systemd/system/
    ├── indicator.service
    ├── media-ingest.service
    └── indicator.target
```

---

## 2. Фаза 1 — Domain-слой и тесты

**Цель:** бизнес-логика написана, покрыта тестами, ASan+UBSan чисто. Ни строчки DispmanX.

**Файлы к реализации:**

| Файл | Функция |
|---|---|
| `src/protocol/types.h` | Все enum'ы: `direction_t`, `mode_t`, `sound_t`, `parsed_frame_t` |
| `src/protocol/parser.c/.h` | `protocol_parse()` — без malloc, без side effects |
| `src/domain/floor.c/.h` | `floor_decode()` |
| `src/domain/sound_map.c/.h` | `sound_map_resolve()`, `sound_map_volume_percent()` |
| `src/domain/state.c/.h` | `state_apply_frame()` → `state_update_result_t` |
| `src/config/config.c/.h` | `config_load()`, дефолты, валидация |
| `tests/test_parser.c` | Полное покрытие парсера |
| `tests/test_floor.c` | Все случаи декодирования этажа |
| `tests/test_state.c` | Все переходы состояний |
| `tests/test_sound_map.c` | Маппинг звуков и громкости |
| `tests/test_config.c` | Загрузка INI, дефолты, ошибки |

**Критерий:** `just build::test` → 100% зелёных тестов, ASan+UBSan без ошибок.

---

### 2.1 Открытые вопросы — требуют ответа перед написанием кода

---

#### В-01 — Формат UART-фрейма: синтаксис токенов

Из MASTER_PLAN известен формат:

```bash
#STM:<X><val>:<X><val>:<X><val>:<X><val>:<X><val>:E#\r\n
```

**Вопрос:** `<X>` — это литеральная буква-префикс (например `L`, `R`, `D`) или просто разделитель?

Т.е. реальный фрейм выглядит как вариант A:

```bash
#STM:L5:R3:D1:S1:M0:E#
```

или как вариант B:

```bash
#STM:5:3:1:1:0:E#
```

Это влияет на реализацию парсера и тесты.

---

#### В-02 — Sound=4 (Closing) и Sound=5 (Opening)

В MASTER_PLAN указано:

```bash
Токен 5: sound — 0=None, 1=Ding, 2=Up, 3=Down, 4=Closing, 5=Opening, 6=Overload
```

Но в текущем enum'е только 4 звука (None/Ding/Up/Down/Overload).

**Вопросы:**

- Closing и Opening используются в реальной прошивке STM32?
- Есть ли для них WAV-файлы? (в `sounds/` они не просматриваются явно)
- Нужно ли их добавить в `sound_t` или это устаревшие значения?

---

#### В-03 — Частота и режим отправки фреймов от STM32

**Вопрос:** STM32 отправляет фреймы:

- **постоянно** с фиксированной частотой (например, 10 Гц) — тогда нужна фильтрация дублей в `state_apply_frame()`
- **только при изменении состояния** — тогда каждый фрейм значим

Это влияет на архитектуру `state.c` и логику триггеров событий.

---

#### В-04 — Звуковая карта: edge-triggered или level?

**Вопрос:** если два последовательных фрейма содержат `sound=DING` — звук должен сыграть:

- **один раз** (sound — это уровень, не импульс)
- **дважды** (каждый фрейм с ненулевым звуком — триггер)

Текущее предположение: STM32 посылает ненулевой звук один раз как событие, затем сбрасывает в 0. Если это так — нужно подтвердить.

---

#### В-05 — Маппинг звуков: какие WAV-файлы к каким событиям?

Из `sounds/` видна структура:

```bash
1.wav … 20.wav, 30.wav, 40.wav   ← предположительно: объявление этажей
20-.wav, 30-.wav                  ← минусовые этажи?
floor.wav, podval.wav, minus.wav  ← составные объявления?
up.wav, down.wav, overload.wav    ← события направления/перегрузки
mus1.wav … mus7.wav               ← фоновая музыка
```

**Вопросы:**

- `SOUND_DING` для этажа 5 → `5.wav`? Для этажа 25 → `floor.wav` + `20.wav` + `5.wav`?
- `has_music` в `sound_map_resolve()` — когда флаг поднят, ding не играет?
- Когда именно запускается фоновая музыка (`mus*.wav`)?

Нужна полная таблица маппинга для реализации `sound_map.c` и тестов.

---

#### В-06 — Конфиг `pizero.ini` и `default.ini`: содержимое

**Запрос:** показать содержимое `configs/device/pizero.ini` и `default.ini` из проекта.

Предположительный состав `config_t`:

```c
typedef struct {
    char uart_device[64];      // /dev/ttyAMA0
    int  uart_baud;            // 9600 / 115200
    char resources_dir[256];   // /home/pi/indicator/resources
    char sounds_dir[256];      // /home/pi/indicator/sounds
    char video_path[256];      // /home/pi/indicator/videos/output.mp4
    int  display_width;        // 600
    int  display_height;       // 1024
    int  volume_percent;       // 0–100
    int  music_volume_percent;
} config_t;
```

Нужно подтвердить или скорректировать этот список.

---

#### В-07 — `state_update_result_t`: полный список событий

Предлагаемая структура:

```c
typedef struct {
    int floor_changed;       // left_char или right_char изменился
    int direction_changed;   // direction_t изменился
    int mode_changed;        // mode_t изменился
    int weight_changed;      // load_percent изменился
    int sound_triggered;     // sound != SOUND_NONE
} state_update_result_t;
```

**Вопросы:**

- Нужно ли отдельное событие `first_frame` (инициализация после старта)?
- `load_percent` — это поле в фрейме или вычисляется из других данных?
- Есть ли события которые нужно генерировать **всегда** при первом фрейме независимо от изменений (например, показать background и weight при старте)?

---

### 2.2 Что можно начать без ответов на вопросы

Следующие модули не зависят от уточнений и могут быть реализованы немедленно:

| Модуль | Почему не зависит |
|---|---|
| `src/protocol/types.h` | Enum'ы известны из MASTER_PLAN (с пометкой TODO для Closing/Opening) |
| `src/domain/floor.c` | Таблица декодирования полностью описана в MASTER_PLAN |
| `tests/test_floor.c` | Все граничные случаи известны |
| `src/config/config.c` | Можно начать с предположительным составом, расширить после В-06 |
| `tests/test_config.c` | Структура тестов не зависит от конкретных ключей |

---

## 3. Контекст для нового треда

При начале нового треда передать:

1. Этот документ (`PHASE_0_REPORT.md`)
2. `MASTER_PLAN.md` из проекта
3. Ответы на вопросы В-01 … В-07
4. Содержимое `pizero.ini` / `default.ini`
5. Содержимое текущих стаб-файлов `src/` (если изменились)

**Стартовая фраза для нового треда:**
> Фаза 0 закрыта. Начинаем Фазу 1. Прикладываю отчёт и MASTER_PLAN. Ответы на вопросы по протоколу и конфигу: [...]

---

*Документ сгенерирован по итогам сессии Фазы 0.*
