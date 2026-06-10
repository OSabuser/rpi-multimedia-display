# Lift Indicator — Отчёт Фазы Deploy (Шаги 0–4)

**Дата:** 2026-06-01
**Проект:** `rpi-multimedia-display`
**Устройство:** Raspberry Pi Zero W Rev 1.1 · ARMv6 · Debian Buster · дисплей 600×1024

---

## 1. Фаза Deploy — ЗАКРЫТА ✅

**Цель фазы:** Подготовить правильную инфраструктуру деплоя, файловую структуру
устройства и systemd-оркестрацию запуска до начала Фазы 5 (аудио).

### 1.1 Чеклист

| # | Пункт | Статус |
|---|---|---|
| **Шаг 0 — Зафиксировать пути** | | |
| 1 | `/data/` layout: три логических слоя (pi_nku_configs, resources, sounds, videos) | ✅ |
| 2 | bind-монты в `/etc/fstab`: /data/* → /home/pi/indicator/* | ✅ |
| 3 | `src/main.c`: DEFAULT_CONFIG_PATH → `/data/pi_nku_configs/nku_scheme.toml` | ✅ |
| 4 | `src/main.c`: VIDEO_PATH → `/data/videos/output.mp4` | ✅ |
| 5 | `deploy/systemd/indicator.service`: --config → `/data/pi_nku_configs/nku_scheme.toml` | ✅ |
| 6 | `deploy/configs/renderer.toml`: resources_dir → `/data/resources` | ✅ |
| 7 | `scripts/migrate_data.sh`: одноразовая миграция существующей Pi | ✅ |
| 8 | `scripts/setup_pi.sh`: обновлён под /data layout + bind mounts + getty@tty1 mask | ✅ |
| **Шаг 0 — Реструктуризация репозитория** | | |
| 9 | `deploy/systemd/` — systemd units (из `deploy/*.service`) | ✅ |
| 10 | `deploy/configs/` — все device-конфиги (из build-env + third_party submodule) | ✅ |
| 11 | `deploy/resources/` — PNG-ресурсы (из build-env/resources/) | ✅ |
| 12 | `deploy/sounds/` — WAV-файлы (из build-env/resources/sounds/) | ✅ |
| 13 | `deploy/boot/config.txt` — boot-конфиг Pi | ✅ |
| 14 | `deploy/bin/` — pre-built Rust бинари (pi_nku_sync, pi_nku_menu) | ✅ |
| 15 | `deploy/tools/` — диагностические бинари (MUp-rpi0) | ✅ |
| 16 | `build-env/` — очищен: только Dockerfile + pi-sysroot | ✅ |
| **Шаг 0 — just/pi.just** | | |
| 17 | Убран `UV_MIN` (неиспользуемая переменная) | ✅ |
| 18 | `deploy`: исправлен glob → `deploy/systemd/`, починен баг с `.target` и `MUp-rpi0` | ✅ |
| 19 | `deploy`: добавлен деплой `check_resources.sh` в `scripts/` | ✅ |
| 20 | `deploy-configs`: новый рецепт, `deploy/configs/` → `/data/pi_nku_configs/` | ✅ |
| 21 | `deploy-resources`: убран аргумент, `deploy/resources/` → `/data/resources/` | ✅ |
| 22 | `deploy-sounds`: убран аргумент, `deploy/sounds/` → `/data/sounds/` | ✅ |
| 23 | `deploy-rust`: новый рецепт, `deploy/bin/` → `PI_DIR/` | ✅ |
| 24 | `migrate-data`: новый рецепт | ✅ |
| 25 | `enable-services`: новый рецепт | ✅ |
| 26 | `test-video`: исправлен баг `${VAR}` → `{{VAR}}` | ✅ |
| 27 | `deploy-debug`: исправлен hardcoded path → `{{BUILD_DIR}}/pi-debug/` | ✅ |
| 28 | `fetch-sysroot`: исправлен баг `pi@${PI_HOST}` → `{{PI_USER}}@{{PI_HOST}}` | ✅ |
| **Шаг 0 — Скрипты** | | |
| 29 | `scripts/smoke_test.sh`: убрана проверка legacy pizero.ini | ✅ |
| 30 | `scripts/smoke_test.sh`: путь видео → `/data/videos/output.mp4` | ✅ |
| 31 | `scripts/smoke_test.sh`: добавлены проверки конфигов и bind-монтов | ✅ |
| 32 | `scripts/check_resources.sh`: секция 7 — конфиги в `/data/pi_nku_configs/` | ✅ |
| 33 | `scripts/check_resources.sh`: секция 8 — writable checks + bind mounts | ✅ |
| 34 | `scripts/check_resources.sh`: секция Rust utilities — pi_nku_sync, pi_nku_menu | ✅ |
| **Шаг 1 — uart_config_load()** | | |
| 35 | `src/config/config.h`: `uart_config_t`, `UART_PORT_MAX`, `CONFIG_DEFAULT_UART_*` | ✅ |
| 36 | `src/config/config.c`: `uart_config_load()` — парсит pi_scheme.toml | ✅ |
| 37 | `src/config/config.c`: исправлен баг однострочных массивов `[...]` в обоих парсерах | ✅ |
| 38 | `src/config/config.c`: `renderer_config_set_defaults()` → `/data/resources` | ✅ |
| 39 | `src/main.c`: убран hardcode UART_DEVICE + BAUD_115200 | ✅ |
| 40 | `src/main.c`: `open_uart()` принимает `const uart_config_t *` | ✅ |
| 41 | `tests/test_config.c`: 7 новых тестов `uart_config_*` (итого 21/21 PASS) | ✅ |
| **Шаг 2 — setup_status** | | |
| 42 | `src/main.c`: `setup_status_t` enum + поле в `app_t` | ✅ |
| 43 | `src/main.c`: `read_setup_status()` — читает `/data/setup_status` | ✅ |
| 44 | `src/main.c`: `maybe_clear_mcu_notification()` — сбрасывается на первом UART-фрейме | ✅ |
| 45 | `src/main.c`: блок чтения статуса после renderer init, switch по всем значениям | ✅ |
| 46 | `src/main.c`: вызов `maybe_clear_mcu_notification()` в обеих ветках `on_uart_frame` | ✅ |
| **Шаг 3 — indicator-setup.service** | | |
| 47 | `scripts/run_setup.sh`: pull → menu (30s) → push, пишет `/data/setup_status` | ✅ |
| 48 | `deploy/systemd/indicator-setup.service`: oneshot, TTYPath=/dev/tty1, RemainAfterExit=yes | ✅ |
| 49 | `deploy/systemd/indicator.service`: Wants + After indicator-setup.service | ✅ |
| **Шаг 4 — indicator-firstboot.service** | | |
| 50 | `scripts/first_boot.sh`: уникальный hostname + SSH keys + machine-id + first_boot_done | ✅ |
| 51 | `deploy/systemd/indicator-firstboot.service`: ConditionPathExists=!/data/first_boot_done | ✅ |
| **On-target** | | |
| 52 | `just pi::check-resources`: 79 файлов проверено, все ✅ (7 WAV missing — Phase 5) | ✅ |
| 53 | `just pi::smoke`: все проверки пройдены | ✅ |
| 54 | indicator-setup: pull → menu → push отработали корректно | ✅ |
| 55 | Повторный restart indicator: setup не перезапускается (RemainAfterExit=yes) | ✅ |
| 56 | `/data/setup_status = "ok"` → indicator логирует `setup: status=ok` | ✅ |
| 57 | `maybe_clear_mcu_notification()` срабатывает на первом UART-фрейме | ✅ |

---

## 2. Проблемы и решения

### P-30 — rsync не передаёт бит +x для скриптов

**Симптом:** `indicator-setup.service: Failed at step EXEC: Permission denied` (status=203).

**Причина:** rsync без `--chmod` не гарантирует `+x` при разных umask и файловых
системах. Скрипт существует, но не исполняемый.

**Решение:** добавить в рецепт `deploy` после rsync скриптов:
```
ssh "{{_pi}}" "chmod +x {{PI_DIR}}/scripts/*.sh"
```

**Статус:** закрыт ✅

### P-31 — Однострочные массивы `[...]` ломали парсер

**Симптом:** `test_uart_config_valid: FAIL: Expected '/dev/serial0' Was '/dev/ttyAMA0'`

**Причина:** `possible_values = ["/dev/serial0"]` (массив на одной строке) — парсер
видел `[`, ставил `in_array = 1` и пропускал `current` и `default` как тело массива.

**Решение:** проверять наличие `]` на той же строке:
```c
const char *open = strchr(line, '[');
if (open != NULL && strchr(open + 1, ']') == NULL)
    in_array = 1;
```
Применено в обоих парсерах: `process_line()` и `uart_config_load()`.

**Статус:** закрыт ✅

### P-24 — dbus-daemon не убивается killpg (carry-over из Фазы 3)

**Симптом:** `indicator.service: State 'stop-final-sigterm' timed out. Killing.`
`indicator.service: Failed with result 'timeout'.`

**Статус:** не блокирует. Косметика. Закрыть в Phase Deploy v2.

---

## 3. Итоговая файловая структура на устройстве

```
/home/pi/indicator/         ← PI_DIR (read-write; будет read-only в Phase Deploy v2)
├── indicator               ← C бинарь
├── media_ingest
├── pi_nku_sync             ← Rust утилита
├── pi_nku_menu             ← Rust утилита
├── pi_nku_configs/         ← bind mount → /data/pi_nku_configs/
│   ├── nku_scheme.toml     ← пишется pi_nku_sync pull; читается всеми
│   ├── pi_scheme.toml      ← UART config
│   ├── menu_style.toml
│   ├── video.toml
│   └── renderer.toml
├── resources/              ← bind mount → /data/resources/
├── sounds/                 ← bind mount → /data/sounds/
├── videos/                 ← bind mount → /data/videos/
├── scripts/
│   ├── run_setup.sh
│   ├── first_boot.sh
│   ├── smoke_test.sh
│   └── check_resources.sh
└── tools/
    └── uart_rx_dump

/data/                      ← writable data partition (сейчас директория, в Phase Deploy v2 — раздел)
├── first_boot_done         ← флаг первого старта
├── setup_status            ← ok | pull_failed | push_failed | pending
├── pi_nku_configs/
├── resources/
├── sounds/
└── videos/
    └── output.mp4
```

---

## 4. Итоговый граф systemd

```
multi-user.target
├── indicator-firstboot.service   [ConditionPathExists=!/data/first_boot_done]
│     first_boot.sh: hostname + SSH keys + machine-id
│           ↓ Before
├── indicator-setup.service       [oneshot, RemainAfterExit=yes, TTYPath=/dev/tty1]
│     run_setup.sh: pull → menu(30s) → push → /data/setup_status
│           ↓ Wants + Before
└── indicator.service             [Restart=always, RestartSec=2]
      reads /data/setup_status → логирует / готовит SPRITE_NOTIFICATION (Phase 7)
      ├── [child]   omxplayer
      └── [pthread] audio (Phase 5)
```

---

## 5. Открытые вопросы

| ID | Вопрос | Приоритет | Phase |
|----|--------|-----------|-------|
| P-24 | dbus-daemon timeout при stop indicator | низкий | Deploy v2 |
| P-29 | ARROW slow path при каждом появлении | низкий | Deploy v2 |
| TODO-1 | Рефакторинг main.c: extract render_dispatch.c + uart_handler.c | средний | Deploy v2 |

---

## 6. Контекст для Фазы 5

При начале нового треда передать:

1. `PHASE_DEPLOY_REPORT.md`
2. `MASTER_PLAN.md`
3. `DEV_ARCH.md`
4. `src/main.c`, `src/config/config.h`, `src/domain/sound_map.h`, `src/audio/audio.h`

**Стартовая фраза:**
> Фазы 0–4 и Deploy закрыты. Начинаем Фазу 5 — аудио.
> Прикладываю PHASE_DEPLOY_REPORT и MASTER_PLAN.

---

*Документ сгенерирован по итогам сессии Фазы Deploy.*