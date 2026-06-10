# Lift Indicator — Отчёт Фазы 7 (USB Media Ingest)

**Дата:** 2026-06-09
**Проект:** `rpi-multimedia-display`
**Устройство:** Raspberry Pi Zero W Rev 1.1 · ARMv6 · Debian Buster · дисплей 600×1024

---

## 1. Фаза 7 — ЗАКРЫТА ✅

**Цель фазы:** Реализовать демон `media-ingest` для замены фонового видео с USB-носителя,
IPC с indicator через именованный FIFO, и SPRITE_NOTIFICATION для информирования
пользователя о ходе операции.

### 1.1 Чеклист

| # | Пункт | Статус |
|---|---|---|
| **Шаг 7.0 — IPC-протокол** | | |
| 1 | `src/media/media_ipc.h`: `media_status_t` enum (7 кодов), FIFO-путь, API | ✅ |
| 2 | `src/media/media_ipc.c`: `media_ipc_open/read/close` (indicator side) | ✅ |
| 3 | FIFO: `/run/indicator/media_status.fifo`, O_RDONLY\|O_NONBLOCK | ✅ |
| 4 | POLLHUP reopen: indicator переоткрывает FIFO при закрытии writer'а | ✅ |
| **Шаг 7.1 — renderer_config notif_x/y** | | |
| 5 | `renderer_config_t`: +`notif_x`, `notif_y` | ✅ |
| 6 | `renderer_config_set_defaults()`: notif_x=0, notif_y=874 | ✅ |
| 7 | `renderer_config_load()`: парсинг `[slot.notification]` | ✅ |
| 8 | `slot_get_x/y`: case SPRITE_NOTIFICATION | ✅ |
| 9 | `deploy/configs/renderer.toml`: +`[slot.notification]` x=0 y=874 | ✅ |
| **Шаг 7.2 — Notification PNGs** | | |
| 10 | `scripts/gen_notifications.sh`: кросс-платформенная генерация (Linux + macOS) | ✅ |
| 11 | 8 PNG файлов 600×150 px RGBA в `deploy/resources/notifications/` | ✅ |
| 12 | Тест `notif_test` (инструмент): проверка всех PNG на живом DispmanX | ✅ |
| 13 | Текст "Нет видеофайлов на носителе" заменён на "Видеофайлы не найдены" (canvas overflow) | ✅ |
| **Шаг 7.3 — video_player_replace()** | | |
| 14 | `src/player/video_player.h/.c`: `video_player_replace()` | ✅ |
| 15 | killpg(SIGKILL) без waitpid — SIGCHLD watchdog делает restart асинхронно | ✅ |
| **Шаг 7.4 — indicator main.c интеграция** | | |
| 16 | `fd_index_t`: FD_FIFO=3, FD_NOTIF=4, FD_COUNT=5 | ✅ |
| 17 | `app_t`: +`fifo_fd`, `notif_fd` | ✅ |
| 18 | `notif_arm()`, `notif_disarm()`: one-shot timerfd для автоскрытия | ✅ |
| 19 | `on_media_status()`: NOTIF_PATHS table, MEDIA_CLEAR, MEDIA_DONE | ✅ |
| 20 | MCU-уведомления: `maybe_clear_mcu_notification()` → notif_mcu_ok + arm(4s) | ✅ |
| 21 | setup_status switch: push/pull_failed → notif_no_mcu.png | ✅ |
| 22 | `indicator.service`: +`RuntimeDirectory=indicator` (fix P-35) | ✅ |
| **Шаг 7.5 — status_pipe.c** | | |
| 23 | `src_media_ingest/status_pipe.h/.c`: write uint8_t в FIFO (ingest side) | ✅ |
| 24 | O_WRONLY\|O_NONBLOCK, новый fd на каждый вызов, ENXIO — not fatal | ✅ |
| **Шаг 7.6 — usb_watcher.c** | | |
| 25 | `src_media_ingest/usb_watcher.h/.c`: inotify /dev, sd[a-z][0-9] filter | ✅ |
| 26 | IN_CREATE / IN_DELETE: коллбэк с devpath и inserted флагом | ✅ |
| **Шаг 7.7 — mounter.c** | | |
| 27 | `src_media_ingest/mounter.h/.c`: mount(2)/umount2(MNT_DETACH) | ✅ |
| 28 | Перебор ФС: vfat → exfat → ext4 | ✅ |
| 29 | MS_RDONLY\|MS_NOEXEC\|MS_NOSUID\|MS_NODEV | ✅ |
| 30 | `mounter_is_mounted()`: проверка через /proc/mounts | ✅ |
| 31 | `AmbientCapabilities=CAP_SYS_ADMIN` в media-ingest.service | ✅ |
| **Шаг 7.8 — ffmpeg_runner.c** | | |
| 32 | `src_media_ingest/ffmpeg_runner.h/.c`: find, spawn, finish, kill | ✅ |
| 33 | `has_mp4_ext()`: фильтр `.mp4`/`.MP4`, скрытые файлы (`.`-prefix) | ✅ |
| 34 | qsort лексикографический — предсказуемый порядок склейки | ✅ |
| 35 | posix_spawnp + POSIX_SPAWN_SETPGROUP — killpg при USB remove | ✅ |
| 36 | stdout/stderr ffmpeg → /dev/null (не засорять journal) | ✅ |
| 37 | Атомарный rename: output_tmp.mp4 → output.mp4 | ✅ |
| **Шаг 7.9 — media-ingest main.c** | | |
| 38 | Конечный автомат: ST_IDLE → ST_WAIT_PROCESSING → ST_FFMPEG_RUNNING → ST_WAIT_EJECT → ST_WAIT_UMOUNT | ✅ |
| 39 | poll(): inotify_fd + signalfd(SIGTERM+SIGCHLD) + timerfd | ✅ |
| 40 | USB remove в любом состоянии: ffmpeg_kill + umount + MEDIA_CLEAR | ✅ |
| 41 | Тайминги: FOUND→PROCESSING 1с, RESULT→EJECT 3с, EJECT→UMOUNT 5с | ✅ |
| 42 | Конфиг: `config_load()` из media_ingest.toml, дефолты при отсутствии файла | ✅ |
| **Шаг 7.10 — Инфраструктура** | | |
| 43 | `deploy/systemd/media-ingest.service`: After=indicator.service, AmbientCapabilities | ✅ |
| 44 | `deploy/configs/media_ingest.toml`: mount_point, video_output, tmp_output, status_fifo | ✅ |
| 45 | `scripts/setup_pi.sh`: mkdir /mnt/usb, tmpfiles.d d /run/indicator | ✅ |
| 46 | `indicator.target` включён в `enable-services` и `deploy-systemd` | ✅ |
| 47 | `just pi::start/stop/restart`: управляют обоими сервисами | ✅ |
| 48 | `scripts/check_resources.sh`: секция 9 — 8 notification PNGs | ✅ |
| 49 | `scripts/check_resources.sh`: media_ingest.toml в секции 7 | ✅ |
| **Шаг 7.11 — Интеграционные тесты** | | |
| 50 | `indicator_checklist_2.md`: 16 тестов в 4 секциях | ✅ |
| 51 | T 1-1: Флешка без MP4 → NO_VIDEO → EJECT → CLEAR | ✅ |
| 52 | T 1-2: Флешка с 1 MP4 → FOUND → PROCESSING → DONE → omxplayer перезапущен | ✅ |
| 53 | T 1-3: Флешка с 3 MP4 → sorted A→B→C → склейка | ✅ |
| 54 | T 1-4: Повторная замена видео | ✅ |
| 55 | T 2-1: USB remove во время ffmpeg → kill → старое видео | ✅ |
| 56 | T 2-2: USB remove в ST_WAIT_EJECT → немедленный CLEAR | ✅ |
| 57 | T 2-3: Битый MP4 → ffmpeg failed → ERROR → EJECT → CLEAR | ✅ |
| 58 | T 3-1: push_failed → notif_no_mcu.png при старте | ✅ |
| 59 | T 3-2: Первый UART-фрейм → notif_mcu_ok → 4 с → hidden | ✅ |
| 60 | T 3-3: pull_failed → notif_no_mcu.png | ✅ |
| 61 | T 4-1: Рестарт indicator → media-ingest PID не меняется | ✅ |
| 62 | T 4-2: Kill media-ingest → рестарт через 10 с → работа нормальная | ✅ |
| 63 | T 4-3: Холодный старт с флешкой → не обрабатывается (ожидаемо) | ✅ |
| 64 | T 4-4: SIGTERM оба сервиса → clean shutdown | ✅ |
| 65 | T 4-5: check-resources 133 файла — все ✅ | ✅ |

---

## 2. Проблемы и решения

### P-31 — Опечатка в media_ingest.toml (fifo path)

**Симптом:** `status_pipe: open '/run/indicator/media_status.fifos': No such file or directory`

**Причина:** При создании `deploy/configs/media_ingest.toml` допущена опечатка —
лишняя `s` в конце пути FIFO (`media_status.fifos` вместо `media_status.fifo`).

**Решение:** Исправлен путь в `media_ingest.toml`.

**Статус:** закрыт ✅

---

### P-32 — macOS AppleDouble-файлы `._video.mp4`

**Симптом:** `ffmpeg_runner: found 2 MP4 file(s) in '/mnt/usb'` при одном файле на флешке.

**Причина:** macOS при записи на FAT32 создаёт скрытые файлы `._<name>` (AppleDouble
метаданные). `has_mp4_ext()` не отфильтровывал файлы с точки в начале имени.

**Решение:** Добавлена проверка `p_name[0] == '.'` в начало `has_mp4_ext()`.

**Статус:** закрыт ✅

---

### P-33 — h264_omx конфликт с omxplayer за VideoCore IV

**Симптом:** ffmpeg с `-c:v h264_omx` зависал на ~4 минуты вместо реалтайм-обработки.

**Причина:** На Pi Zero W omxplayer декодирует видео через VideoCore IV hardware decoder.
При одновременном запуске `h264_omx` encoder оба процесса конкурируют за `/dev/vchiq`,
что приводит к блокировке ffmpeg в ожидании GPU.

**Решение:** Принято решение использовать `-c copy` (потоковое копирование без
перекодировки). Обоснование: контент для лифтового индикатора — подготовленный
профессиональный материал в формате H.264 MP4. Время обработки с `-c copy` —
секунды независимо от длины видео.

**Задокументированное требование:** файлы должны быть H.264 в MP4-контейнере.
При несовместимом формате ffmpeg возвращает ошибку → экран показывает
«Ошибка обработки», исходное видео не изменяется.

**Статус:** закрыт ✅. GPU encode (h264_omx) зафиксирован как TODO для
исследования в будущем (отдельный процесс без конкуренции с omxplayer).

---

### P-34 — indicator.target не был включён

**Симптом:** `media-ingest.service: inactive (dead)` после `just pi::enable-services`.

**Причина:** `media-ingest.service` имеет `WantedBy=indicator.target`, но
`indicator.target` не был включён (`systemctl enable`). Сервис никогда не
стартовал автоматически.

**Решение:** `indicator.target` добавлен в `enable-services` и `deploy-systemd`.

**Статус:** закрыт ✅

---

### P-35 — Permission denied при создании /run/indicator

**Симптом:** `media_ipc: mkdir '/run/indicator': Permission denied`

**Причина:** indicator работает от пользователя `pi`, который не может создавать
директории напрямую в `/run` (принадлежит root).

**Решение:** Добавлен `RuntimeDirectory=indicator` в `indicator.service`.
systemd создаёт `/run/indicator/` с правильным владельцем до запуска сервиса.

**Статус:** закрыт ✅

---

## 3. Архитектурные решения Фазы 7

| Решение | Обоснование |
|---|---|
| FIFO (именованный канал) для IPC | Простейший надёжный IPC для однонаправленной передачи кодов состояния; нет shared memory overhead |
| Один байт на сообщение | 7 кодов помещаются в uint8_t; нет framing, нет буферизации |
| O_WRONLY\|O_NONBLOCK + новый fd на каждый write | Надёжнее постоянного fd: FIFO переживает рестарты indicator |
| POLLHUP → reopen в indicator | Корректная обработка lifecycle media-ingest; нет busy-loop |
| `-c copy` вместо перекодировки | GPU занят omxplayer; `-c copy` мгновенный для H.264 MP4 |
| Атомарный rename(tmp→final) | omxplayer не видит частично записанного файла |
| AmbientCapabilities=CAP_SYS_ADMIN | mount(2) без sudo, без root; минимальные привилегии |
| MNT_DETACH при umount | Lazy unmount безопасен если ffmpeg ещё держит fd (race condition) |
| Фильтр `p_name[0] == '.'` | Убирает macOS AppleDouble, .DS_Store и прочие системные файлы |
| notif_y=874, PNG 600×150 | Нижняя полоса экрана не перекрывает цифры этажа и стрелку |
| FD_NOTIF timerfd в indicator | Non-blocking автоскрытие notif_mcu_ok через 4 с без блокировки poll loop |

---

## 4. Итоговая файловая структура Фазы 7

```
src/media/
├── media_ipc.h              ← media_status_t enum, FIFO API (indicator side)
└── media_ipc.c              ← mkfifo + O_RDONLY open

src/player/
├── video_player.h           ← + video_player_replace()
└── video_player.c           ← + video_player_replace()

src/main.c                   ← FD_FIFO, FD_NOTIF, on_media_status(), notif_arm/disarm,
                                MCU notifications, setup_status switch

src_media_ingest/
├── status_pipe.h/.c         ← write side FIFO
├── usb_watcher.h/.c         ← inotify /dev
├── mounter.h/.c             ← mount(2) / umount2
├── ffmpeg_runner.h/.c       ← find MP4, spawn ffmpeg, rename
└── main.c                   ← конечный автомат, poll loop, config

deploy/resources/notifications/
├── notif_found.png          ← "Найдены видеофайлы"
├── notif_processing.png     ← "Идёт обработка..."
├── notif_success.png        ← "Успех!"
├── notif_no_video.png       ← "Видеофайлы не найдены"
├── notif_eject.png          ← "Извлеките носитель"
├── notif_error.png          ← "Ошибка обработки"
├── notif_no_mcu.png         ← "Нет связи с MCU"
└── notif_mcu_ok.png         ← "Связь с MCU установлена"

deploy/configs/
└── media_ingest.toml        ← mount_point, video_output, tmp_output, status_fifo

deploy/systemd/
└── media-ingest.service     ← After=indicator.service, AmbientCapabilities=CAP_SYS_ADMIN

scripts/
└── gen_notifications.sh     ← генерация PNG через imagemagick (cross-platform)

tools/
└── notif_test.c             ← интерактивный тест SPRITE_NOTIFICATION на дисплее
```

---

## 5. Открытые вопросы

| ID | Вопрос | Приоритет | Фаза |
|----|--------|-----------|------|
| P-24 | dbus-daemon timeout при stop indicator | низкий | Deploy v2 |
| P-29 | ARROW slow path при каждом появлении | низкий | Deploy v2 |
| TODO | Флешка вставленная до старта демона не обрабатывается (inotify видит только новые события) | низкий | known limitation |
| TODO | h264_omx + omxplayer GPU conflict — исследовать запуск ffmpeg с отдельным /dev/vchiq контекстом | низкий | future |
| TODO | Рефакторинг main.c: extract render_dispatch.c + uart_handler.c | средний | Deploy v2 |

---

## 6. Интеграционные испытания №2

Все 16 тестов пройдены. Результат: **PASS**.
Чеклист: `indicator_checklist_2.md` (сохранён отдельно).

---

## 7. Контекст для Фазы 6 (Deploy v2)

При начале нового треда передать:
1. `PHASE_7_REPORT.md`
2. `MASTER_PLAN.md` (обновлённый)
3. `DEV_ARCH.md` (обновлённый)
4. `src/main.c`, `src_media_ingest/main.c`

**Стартовая фраза:**
> Фазы 0–7 и Phase Deploy закрыты. Начинаем Фазу 6 (Deploy v2) —
> overlayroot, разделы SD-карты, build\_image.sh, factory-test.

---

*Документ составлен по итогам сессии Фазы 7.*