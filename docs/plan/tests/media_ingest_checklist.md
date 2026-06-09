# Lift Indicator — Интеграционные испытания №2
## Версия документа: 1.0 (Фаза 7 — USB Media Ingest)

**Дата:** _______________
**Устройство / hostname:** _______________
**Версия прошивки (git SHA):** _______________
**Исполнитель:** _______________

---

## Архитектура Фазы 7 (справка)

```
USB insert → media-ingest (inotify /dev) → mount /mnt/usb
         → ffmpeg concat → rename /data/videos/output.mp4
         → FIFO /run/indicator/media_status.fifo → indicator
         → SPRITE_NOTIFICATION (z=5, y=874, 600×150 px)
         → video_player_replace() → omxplayer перезапускается

Тайминги конечного автомата media-ingest:
  Вставка → 1 с → PROCESSING (ffmpeg запускается)
  Результат → 3 с → EJECT
  EJECT → 5 с → umount + CLEAR
```

---

## Команды мониторинга (держать открытыми во время тестов)

```bash
# Терминал 1 — indicator
journalctl -u indicator -f --no-pager

# Терминал 2 — media-ingest
journalctl -u media-ingest -f --no-pager

# Терминал 3 — оба сервиса вместе
just pi::logs-tail 50

# Быстрая проверка статуса обоих
just pi::status
```

---

## Предусловия

Выполнить перед **любым** тестом. Все пункты должны быть ✅.

| # | Проверка | Команда | Ожидание | ✅/❌ |
|---|---|---|---|---|
| П-1 | Ресурсы на Pi | `just pi::check-resources` | `Result: ✅ ALL RESOURCES OK`, `Checked: ≥133 files`, 0 ошибок | ☐ |
| П-2 | indicator запущен | `just pi::status` | `indicator.service: active (running)` | ☐ |
| П-3 | media-ingest запущен | `just pi::status` | `media-ingest.service: active (running)` | ☐ |
| П-4 | FIFO существует | `ls -la /run/indicator/media_status.fifo` | `prw-rw-rw- ... /run/indicator/media_status.fifo` | ☐ |
| П-5 | /mnt/usb свободна | `mountpoint /mnt/usb` | `not a mountpoint` (ничего не смонтировано) | ☐ |
| П-6 | Видео воспроизводится | Визуально на экране | omxplayer играет фоновое видео | ☐ |
| П-7 | Notification слот пуст | `journalctl -u indicator -n 5` | Нет `slot 6 created` при последнем рестарте | ☐ |

---

## Секция 1 — Базовые USB-сценарии

Цель: проверить полный жизненный цикл обработки USB-носителя — от вставки до
извлечения — во всех базовых конфигурациях содержимого.

---

### 1-1 — Флешка без MP4-файлов

| Поле | Значение |
|---|---|
| **Подготовка** | Флешка отформатирована (FAT32), содержит только не-MP4 файлы (например `.txt`, `.jpg`) или пустая |
| **Действие** | Вставить флешку в USB OTG порт |
| **Лог media-ingest** | `usb_watcher: insert /dev/sdaN` |
| | `mounter: /dev/sdaN → /mnt/usb (fstype=vfat)` |
| | `ffmpeg_runner: no MP4 files in '/mnt/usb'` |
| | `status_pipe: sent status=3` (MEDIA_NO_VIDEO) |
| | *(3 с)* `status_pipe: sent status=4` (MEDIA_EJECT) |
| | *(5 с)* `mounter: unmounted '/mnt/usb'` |
| | `status_pipe: sent status=6` (MEDIA_CLEAR) |
| **Лог indicator** | `media: status=3` → `renderer: slot 6 created, z=5, pos=(0,874)` |
| | `media: status=4` → `slot 6 fast-updated` |
| | `media: status=6` → `renderer: slot 6 hidden` |
| **Визуально** | «Видеофайлы не найдены» → «Извлеките носитель» → уведомление исчезает |
| | Фоновое видео **не меняется** |
| **Критерий** | Уведомление точно проходит все три стадии; видео не прерывается |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | |

---

### 1-2 — Флешка с одним MP4-файлом

| Поле | Значение |
|---|---|
| **Подготовка** | Флешка с одним файлом `video.mp4` (любое корректное MP4-видео) |
| **Действие** | Вставить флешку |
| **Лог media-ingest** | `ffmpeg_runner: found 1 MP4 file(s) in '/mnt/usb'` |
| | `status_pipe: sent status=0` (MEDIA_FOUND) |
| | *(1 с)* `status_pipe: sent status=1` (MEDIA_PROCESSING) |
| | `ffmpeg_runner: spawned pid=N count=1 → '/data/videos/output_tmp.mp4'` |
| | `ffmpeg_runner: output ready: '/data/videos/output.mp4'` |
| | `status_pipe: sent status=2` (MEDIA_DONE) |
| | *(3 с)* `status_pipe: sent status=4` (MEDIA_EJECT) |
| | *(5 с)* umount → `status_pipe: sent status=6` (MEDIA_CLEAR) |
| **Лог indicator** | `media: status=0` → «Найдены видеофайлы» |
| | `media: status=1` → «Идёт обработка...» |
| | `media: status=2` → «Успех!» + `video_player: replace — killing pgid=N` |
| | `omxplayer started pid=M` (новый PID) |
| | `media: status=4` → «Извлеките носитель» |
| | `media: status=6` → уведомление скрыто |
| **Визуально** | Полная последовательность уведомлений; фоновое видео меняется на содержимое флешки |
| **Критерий** | Новый omxplayer PID ≠ старому; новое видео воспроизводится в loop |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | |

---

### 1-3 — Флешка с несколькими MP4-файлами (проверка порядка склейки)

| Поле | Значение |
|---|---|
| **Подготовка** | Флешка с тремя файлами: `c_third.mp4`, `a_first.mp4`, `b_second.mp4` (разный визуальный контент для идентификации) |
| **Действие** | Вставить флешку |
| **Лог media-ingest** | `ffmpeg_runner: found 3 MP4 file(s) in '/mnt/usb'` |
| | `ffmpeg_runner: spawned pid=N count=3 → '/data/videos/output_tmp.mp4'` |
| **Критерий** | После смены видео воспроизводится в **алфавитном** порядке: `a_first` → `b_second` → `c_third` → loop |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | |

---

### 1-4 — Повторная замена видео (вторая флешка)

| Поле | Значение |
|---|---|
| **Подготовка** | После успешного теста 1-2 (видео уже заменено) — подготовить другую флешку с другим MP4 |
| **Действие** | Вставить вторую флешку |
| **Критерий** | Цикл повторяется корректно; видео меняется снова; `/data/videos/output_tmp.mp4` не остаётся на диске |
| **Проверить** | `ls /data/videos/` — только `output.mp4`, нет `output_tmp.mp4` |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | |

---

## Секция 2 — Прерывание и граничные случаи

Цель: убедиться в корректной обработке аномальных ситуаций.

---

### 2-1 — Извлечение флешки во время ffmpeg

| Поле | Значение |
|---|---|
| **Подготовка** | Флешка с большим MP4-файлом (≥50 МБ, чтобы ffmpeg работал несколько секунд) |
| **Действие** | Вставить флешку; дождаться уведомления «Идёт обработка...»; **извлечь флешку** |
| **Лог media-ingest** | `usb_watcher: remove /dev/sdaN` |
| | `ffmpeg_runner: killing pgid=N` |
| | `mounter: unmounted '/mnt/usb'` |
| | `status_pipe: sent status=6` (MEDIA_CLEAR) |
| **Лог indicator** | `media: status=6` → `renderer: slot 6 hidden` |
| **Критерий** | ffmpeg убит немедленно; уведомление скрыто; `/data/videos/output.mp4` **не изменился** (старое видео продолжает играть); `/data/videos/output_tmp.mp4` удалён |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | |

---

### 2-2 — Извлечение флешки в ST_WAIT_EJECT (до umount)

| Поле | Значение |
|---|---|
| **Подготовка** | Флешка без MP4-файлов (сценарий 1-1) |
| **Действие** | Вставить флешку; дождаться уведомления «Извлеките носитель»; **извлечь флешку вручную** (не ждать 5 с авто-umount) |
| **Лог media-ingest** | `usb_watcher: remove /dev/sdaN` |
| | `mounter_is_mounted` → false или `mounter: umount … already unmounted` |
| | `status_pipe: sent status=6` (MEDIA_CLEAR) |
| **Критерий** | Нет ошибок umount; уведомление скрыто; ST_IDLE достигнут |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | |

---

### 2-3 — Флешка только с не-видео MP4 (повреждённый файл)

| Поле | Значение |
|---|---|
| **Подготовка** | Флешка с файлом `broken.mp4` (переименованный `.txt`-файл — не валидный MP4) |
| **Действие** | Вставить флешку |
| **Лог media-ingest** | `ffmpeg_runner: found 1 MP4 file(s)` |
| | `ffmpeg_runner: ffmpeg failed pid=N status=1` |
| | `status_pipe: sent status=5` (MEDIA_ERROR) |
| **Лог indicator** | `media: status=5` → «Ошибка обработки» |
| | *(3 с)* → «Извлеките носитель» → *(5 с)* → уведомление скрыто |
| **Критерий** | «Ошибка обработки» отображается; `/data/videos/output.mp4` **не изменился**; старое видео продолжает играть |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | |

---

### 2-4 — Вставка во время активного состояния (не ST_IDLE)

| Поле | Значение |
|---|---|
| **Подготовка** | Флешка с большим MP4 (ffmpeg работает долго) |
| **Действие** | Вставить первую флешку; дождаться «Идёт обработка...»; попытаться вставить вторую флешку (реально невозможно с одним портом — симулировать через ручной inotify-ивент) |
| **Лог media-ingest** | `ingest: insert /dev/sdaN ignored (state=2)` (ST_FFMPEG_RUNNING) |
| **Критерий** | Второе событие вставки проигнорировано; первая обработка завершается нормально |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | Один OTG-порт физически исключает одновременную вставку двух носителей; тест документирует поведение ПО |

---

## Секция 3 — Уведомления MCU-связи

Цель: проверить уведомления о состоянии связи с MCU, не связанные с USB.

---

### 3-1 — Уведомление «Нет связи с MCU» при push_failed

| Поле | Значение |
|---|---|
| **Подготовка** | Записать `echo push_failed > /data/setup_status` |
| **Действие** | `just pi::restart` |
| **Лог indicator** | `setup: push_failed — MCU не получил команду стриминга` |
| | `renderer: slot 6 created, z=5, pos=(0,874), size=600x150` |
| **Визуально** | «Нет связи с MCU» — уведомление остаётся на экране |
| **Критерий** | Уведомление показывается сразу при старте и **не исчезает** само по себе (ждёт первого UART-фрейма) |
| **Восстановление** | `echo ok > /data/setup_status` |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | |

---

### 3-2 — Автоскрытие «Связь с MCU установлена»

| Поле | Значение |
|---|---|
| **Предусловие** | Тест 3-1 завершён (уведомление видно); STM32 подключён и стримит фреймы |
| **Триггер** | Первый валидный UART-фрейм (0xDA или 0xAA) от MCU |
| **Лог indicator** | `setup: MCU communication established — clearing notification` |
| | `renderer: slot 6 fast-updated` (notif_mcu_ok.png) |
| | *(4 с)* `renderer: slot 6 hidden` (notif timer fired) |
| **Визуально** | «Нет связи с MCU» → «Связь с MCU установлена» → уведомление исчезает через ~4 с |
| **Критерий** | Смена уведомления и автоскрытие происходят ровно один раз |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | |

---

### 3-3 — Уведомление при pull_failed

| Поле | Значение |
|---|---|
| **Подготовка** | `echo pull_failed > /data/setup_status` |
| **Действие** | `just pi::restart` |
| **Лог indicator** | `setup: pull_failed — не удалось прочитать параметры MCU` |
| **Визуально** | «Нет связи с MCU» (тот же PNG что и при push_failed) |
| **Критерий** | Поведение идентично тесту 3-1 |
| **Восстановление** | `echo ok > /data/setup_status` |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | |

---

## Секция 4 — Надёжность и перезапуск

Цель: убедиться в корректном поведении при сбоях и перезапусках сервисов.

---

### 4-1 — Перезапуск indicator при живом media-ingest

| Поле | Значение |
|---|---|
| **Предусловие** | Оба сервиса работают; флешка НЕ вставлена |
| **Действие** | `just pi::restart` |
| **Лог media-ingest** | `status_pipe: no reader on '...'` (возможно, пока indicator перезапускается) |
| | После рестарта indicator: следующий write в FIFO проходит без ошибок |
| **Лог indicator** | `media_ipc: closed fd=6` → `media_ipc: ready fd=6` |
| **Критерий** | После рестарта indicator FIFO переоткрыт; media-ingest продолжает работать без перезапуска; следующая вставка флешки обрабатывается нормально |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | |

---

### 4-2 — Перезапуск media-ingest (сбой демона)

| Поле | Значение |
|---|---|
| **Предусловие** | Оба сервиса работают; флешка НЕ вставлена |
| **Действие** | `ssh pi@indicator-02.local 'sudo kill -KILL $(pgrep media_ingest)'` |
| **Лог media-ingest** | systemd: `Started Media Ingest Daemon` (рестарт через RestartSec=10) |
| | `media_ingest starting` → `usb_watcher: watching '/dev' ...` |
| **Критерий** | Демон перезапускается через ≤10 с; indicator не затронут; следующая вставка флешки работает |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | |

---

### 4-3 — Рестарт Pi с вставленной флешкой (холодный старт)

| Поле | Значение |
|---|---|
| **Подготовка** | Вставить флешку с MP4 до перезагрузки |
| **Действие** | `sudo reboot` |
| **Ожидание** | После старта: media-ingest НЕ детектирует уже вставленную флешку (inotify видит только новые IN_CREATE события) |
| **Лог indicator** | Нормальный старт; никаких media-статусов |
| **Критерий** | Система стартует чисто; уведомлений не показывается; при извлечении и повторной вставке флешки — нормальная обработка |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | Известное ограничение: флешки, вставленные до старта, не обрабатываются автоматически |

---

### 4-4 — Корректное завершение при SIGTERM (оба сервиса)

| Поле | Значение |
|---|---|
| **Предусловие** | Флешка НЕ вставлена |
| **Действие** | `sudo systemctl stop indicator.service media-ingest.service` |
| **Лог indicator** | `received signal 15` → `indicator stopped` |
| **Лог media-ingest** | `ingest: SIGTERM — stopping` → `media_ingest stopped` |
| **Критерий** | Оба сервиса завершаются без `Failed with result 'timeout'`; media-ingest завершается < 2 с |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | P-24 (dbus-daemon timeout в indicator.service) — известная косметическая проблема, не блокирует |

---

### 4-5 — check-resources: все ресурсы Фазы 7 на месте

| Поле | Значение |
|---|---|
| **Действие** | `just pi::check-resources` |
| **Вывод** | `Result: ✅ ALL RESOURCES OK` |
| | `Checked: ≥133 files` |
| | Секция «Notifications»: 8 PNG ✅ |
| | `media_ingest.toml` ✅ |
| **Критерий** | 0 ошибок |
| **Результат** | ☐ Pass  ☐ Fail |
| **Примечание** | |

---

## Итоговая сводка

| Секция | Тестов | Pass | Fail | Примечание |
|---|---|---|---|---|
| 1 — Базовые USB-сценарии | 4 | | | |
| 2 — Прерывание и граничные случаи | 4 | | | |
| 3 — Уведомления MCU-связи | 3 | | | |
| 4 — Надёжность и перезапуск | 5 | | | |
| **Итого** | **16** | | | |

**Общий результат:** ☐ PASS  ☐ FAIL (есть блокирующие дефекты)

**Блокирующие дефекты:**

1. ___________________________________________________________________________

2. ___________________________________________________________________________

3. ___________________________________________________________________________

**Некритичные замечания:**

_______________________________________________________________________________

---

## Справка: коды статусов FIFO

| Код | Константа | Уведомление на экране |
|---|---|---|
| 0 | `MEDIA_FOUND` | «Найдены видеофайлы» |
| 1 | `MEDIA_PROCESSING` | «Идёт обработка...» |
| 2 | `MEDIA_DONE` | «Успех!» + video_player_replace() |
| 3 | `MEDIA_NO_VIDEO` | «Видеофайлы не найдены» |
| 4 | `MEDIA_EJECT` | «Извлеките носитель» |
| 5 | `MEDIA_ERROR` | «Ошибка обработки» |
| 6 | `MEDIA_CLEAR` | *(уведомление скрывается)* |

## Справка: тайминги конечного автомата

| Переход | Задержка |
|---|---|
| MEDIA_FOUND → ffmpeg start (MEDIA_PROCESSING) | 1 с |
| Результат (DONE/ERROR/NO_VIDEO) → MEDIA_EJECT | 3 с |
| MEDIA_EJECT → umount + MEDIA_CLEAR | 5 с |
| «Связь с MCU установлена» → автоскрытие | 4 с |

## Справка: SPRITE_NOTIFICATION

| Параметр | Значение |
|---|---|
| Слот | 6 (`SPRITE_NOTIFICATION`) |
| Z-порядок | 5 (выше всех) |
| Позиция | x=0, y=874 (настраивается в `renderer.toml`) |
| Размер PNG | 600×150 px, RGBA |
| Расположение PNG | `/data/resources/notifications/` |

## Справка: диагностика

```bash
# Проверить FIFO
ls -la /run/indicator/media_status.fifo

# Проверить /mnt/usb
mount | grep usb
ls /mnt/usb

# Проверить output.mp4
ls -lh /data/videos/output.mp4
stat /data/videos/output.mp4   # смотреть mtime — изменился ли файл

# Отправить статус вручную (test stub)
printf '\x00' > /run/indicator/media_status.fifo   # MEDIA_FOUND
printf '\x06' > /run/indicator/media_status.fifo   # MEDIA_CLEAR

# Лог только media:
journalctl -u media-ingest -f --no-pager
journalctl -u indicator -f | grep "media:"
```
