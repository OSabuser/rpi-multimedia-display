# Lift Indicator — Отчёт Фазы 3

**Дата:** 2026-05-29
**Проект:** `rpi-multimedia-display`
**Устройство:** Raspberry Pi Zero W Rev 1.1 · ARMv6 · Debian Buster · дисплей 600×1024

---

## 1. Фаза 3 — ЗАКРЫТА ✅

**Цель фазы:** omxplayer запущен под надзором `indicator`, автоматически
перезапускается при падении. Параметры окна настраиваемы через `video.toml`.

### 1.1 Чеклист

| # | Пункт | Статус |
|---|---|---|
| **Код** | | |
| 1 | `src/player/video_player.h` — `video_window_t`, API: `open/close/check_and_restart/get_pid` | ✅ |
| 2 | `src/player/video_player.c` — `posix_spawnp` + `POSIX_SPAWN_SETPGROUP`, `killpg(SIGKILL)`, watchdog | ✅ |
| 3 | `src/main.c` — `handle_signal(p_si, p_app)`, SIGCHLD → `video_player_check_and_restart` | ✅ |
| 4 | `src/config/config.h` — поля `video_win_*`, дефолты, `video_config_load()` declaration | ✅ |
| 5 | `src/config/config.c` — `video_config_load()` парсит `[video]` секцию с целыми числами | ✅ |
| 6 | `configs/device/video.toml` — новый файл, `win_x/y/w/h` настраиваемы | ✅ |
| 7 | `deploy/indicator.service` — `KillMode=control-group` явно | ✅ |
| **Тесты** | | |
| 8 | `tests/test_config.c` — +5 тестов `video_config_load`: full, missing, null, partial, comments | ✅ |
| 9 | `just build::test` → 14/14 passed, ASan+UBSan чисто | ✅ |
| **On-target** | | |
| 10 | `indicator` стартует → omxplayer запускается, видео играет на полный экран | ✅ |
| 11 | `pkill -KILL -f omxplayer.bin` → перезапуск < 1 с, новый PID в логах | ✅ |
| 12 | `systemctl stop indicator` → `indicator stopped` без блокировки (< 1 с) | ✅ |
| 13 | `KillMode=control-group` → dbus-daemon убивается systemd при stop | ✅ |

**Критерий выполнен.**

---

## 2. Проблемы и решения

### P-24 — dbus-daemon не убивается killpg

**Симптом:** после `systemctl stop indicator` в логах:
```
indicator.service: State 'stop-final-sigterm' timed out. Killing.
indicator.service: Killing process XXXX (dbus-daemon) with signal SIGKILL.
indicator.service: Failed with result 'timeout'.
```

**Причина:** `/usr/bin/omxplayer` запускает `dbus-daemon` через `dbus-launch`,
который создаёт **собственную session** и выходит из process group omxplayer.
`killpg(pgid, SIGKILL)` не достигает dbus-daemon — он в другой pgroup.

`indicator` при этом завершается корректно и быстро (`indicator stopped` в логах).
`Failed with result 'timeout'` — это статус systemd о dbus-daemon, не об indicator.

**Текущее решение:** `KillMode=control-group` в `indicator.service` — systemd
убивает dbus-daemon через cgroup после `TimeoutStopSec=5`. Функционально корректно.

**Статус:** не блокирует работу. Косметически `Failed with result 'timeout'` в
статусе сервиса. Разобрать в **Фазе 6** (systemd lifecycle).

**Варианты решения в Фазе 6:**
- `TimeoutStopSec=6` (скрыть проблему) — не рекомендуется
- Добавить `ExecStopPost=pkill -KILL -f dbus-daemon` — грубо
- Использовать D-Bus API для graceful quit omxplayer перед close — правильно, но сложно
- Пересмотреть необходимость dbus для omxplayer: `--no-dbus` флаг если поддерживается

### P-25 — SIGTERM не работает для завершения omxplayer

**Симптом:** `killpg(pgid, SIGTERM)` в `video_player_close` не завершал
omxplayer — `waitpid` блокировал на 5 с до таймаута systemd.

**Причина:** bash-скрипт `/usr/bin/omxplayer` находится в `wait` ожидая
`omxplayer.bin`. В неинтерактивном режиме bash не обрабатывает SIGTERM
во время `wait` — сигнал принимается но исполнение не прерывается.

**Решение:** заменить `SIGTERM` на `SIGKILL` в `video_player_close`:
```c
killpg(p_vp->pid, SIGKILL); /* SIGKILL нельзя перехватить или игнорировать */
```

**Статус:** закрыт ✅

### P-26 — Каскадные рестарты при pkill -f omxplayer.bin

**Симптом:** после `pkill -KILL -f omxplayer.bin` watchdog перезапускал
omxplayer (PID 1590), затем новый процесс тоже убивался (1618, 1646).

**Причина:** паттерн `-f omxplayer.bin` совпадает со всеми экземплярами
omxplayer.bin включая свежезапущенные. Если `pkill` запущен несколько раз
или успевает поймать новый процесс — watchdog снова перезапускает.

**Это ожидаемое поведение watchdog.** Для чистого тестирования использовать
`kill <specific_pid>` вместо `pkill -f`.

**Статус:** не баг, документирован ✅

---

## 3. Уточнения архитектуры процессов omxplayer

```
indicator (PID 1501, PGID = indicator's group)
└── /bin/bash /usr/bin/omxplayer ... (PID 1502, PGID 1502) ← posix_spawnp + SETPGROUP
    ├── dbus-daemon (PID XXXX, PGID != 1502)               ← dbus-launch создаёт отдельную session
    └── omxplayer.bin (PID YYYY, PGID 1502)                ← наследует pgroup shell-скрипта
```

`killpg(1502, SIGKILL)` убивает: bash-скрипт + omxplayer.bin.
`dbus-daemon` — вне group, убивается systemd через cgroup (KillMode=control-group).

---

## 4. Новые и изменённые файлы

| Файл | Действие |
|---|---|
| `src/player/video_player.h` | Новый: `video_window_t`, API плеера |
| `src/player/video_player.c` | Новый: spawn + SETPGROUP + killpg(SIGKILL) + watchdog |
| `src/config/config.h` | Обновлён: `video_win_*` поля, defaults, `video_config_load()` |
| `src/config/config.c` | Обновлён: defaults для video в `config_load`, новый `video_config_load()` |
| `src/main.c` | Обновлён: `handle_signal(p_si, p_app)`, video init/close, `derive_sibling_path` |
| `configs/device/video.toml` | Новый: `[video] win_x/y/w/h` |
| `deploy/indicator.service` | Обновлён: `KillMode=control-group` явно |
| `tests/test_config.c` | Обновлён: +5 тестов video_config_load, tearDown убирает оба файла |

---

## 5. Итоговое состояние окружения

```
Raspberry Pi Zero W (indicator-01.local)
├── /home/pi/indicator/
│   ├── indicator            ← ARMv6 Release (phase-3) ✅
│   ├── media_ingest         ← ARMv6 Release (stub) ✅
│   ├── tools/uart_rx_dump   ✅
│   ├── configs/device/
│   │   ├── nku_scheme.toml  ← не тронут
│   │   └── video.toml       ← новый ✅
│   ├── sounds/              ← WAV файлы (Фаза 5)
│   ├── resources/           ← пусто (Фаза 4)
│   └── videos/output.mp4    ← видео играет ✅
└── /etc/systemd/system/
    ├── indicator.service    ← KillMode=control-group ✅
    ├── media-ingest.service
    └── indicator.target
```

---

## 6. Открытые вопросы

| ID | Вопрос | Приоритет | Фаза |
|----|--------|-----------|------|
| P-24 | `dbus-daemon` не в pgroup omxplayer → `Failed with result 'timeout'` | низкий | 6 |
| В-05 | `s_close.wav`, `s_open.wav` — удалить из деплоя или оставить? | низкий | 5 |
| В-06 | `g_single.wav`, `g_double.wav` — аналогично | низкий | 5 |

---

## 7. Контекст для Фазы 4

При начале нового треда передать:

1. `PHASE_3_REPORT.md`
2. `MASTER_PLAN.md`
3. `DEV_ARCH.md`
4. Текущие исходники: `src/main.c`, `src/renderer/renderer.h`,
   `platform/dispmanx/` (stub + структура)

**Стартовая фраза:**
> Фазы 0–3 закрыты. Начинаем Фазу 4 — DispmanX renderer.
> Прикладываю PHASE_3_REPORT и MASTER_PLAN.

---

*Документ сгенерирован по итогам сессии Фазы 3.*