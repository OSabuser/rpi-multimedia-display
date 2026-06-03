# Lift Indicator — Отчёт Фазы 5

**Дата:** 2026-06-03
**Проект:** `rpi-multimedia-display`
**Устройство:** Raspberry Pi Zero 2W · Debian Buster · дисплей 600×1024

---

## 1. Фаза 5 — ЗАКРЫТА ✅

**Цель фазы:** аудиоподсистема реализована, протестирована на HIL-стенде и
работает в продакшн-конфигурации. Фоновая музыка, приоритетная очередь событий,
управление громкостью через ALSA softvol — всё функционирует без регрессий
в дисплейной и видеоподсистемах.

### 1.1 Чеклист

| # | Пункт | Статус |
|---|---|---|
| **Код** | | |
| 1 | `src/audio/audio.h` — полный API: `audio_prio_t`, `audio_player_open/close/play/cancel_music` | ✅ |
| 2 | `src/audio/audio.c` — реализация: pthread worker, mutex/condvar, приоритетная очередь глубины 3, `posix_spawn aplay/amixer` | ✅ |
| 3 | `src/domain/sound_map.c` — исправлены 3 бага + добавлен диапазон 41–49 | ✅ |
| 4 | `tests/test_sound_map.c` — 33 теста, все проходят | ✅ |
| 5 | `src/main.c` — 7 точечных изменений: init audio, `sound_to_prio()`, play в `on_uart_frame()`, cancel на MODE/dispatch | ✅ |
| 6 | `src/main.c` — подавление `needs_music` при `mode != MODE_NORMAL` или активном dispatch | ✅ |
| **Инфраструктура** | | |
| 7 | `/etc/asound.conf` — стек `speakerbonnet → dmixer → softvol → !default` (48kHz / S32_LE / stereo) | ✅ |
| 8 | `deploy/systemd/i2s-silence.service` — I2S keepalive через dmix, `ExecStartPre` ждёт card 0 (до 15 с) | ✅ |
| 9 | `scripts/setup_pi.sh` — I2S overlay, asound.conf, i2s-silence.service, пакет `fbi` | ✅ |
| 10 | `just/pi.just` — `deploy-systemd` с миграцией `aplay→i2s-silence`, новый рецепт `restart-audio` | ✅ |
| 11 | `scripts/smoke_test.sh` — исправлена проверка аудио (S32_LE / googlevoicehat), i2s-silence | ✅ |
| 12 | `scripts/check_resources.sh` — секция 6 полностью переписана: 45 WAV-файлов, режим fail (не warn) | ✅ |
| **Документация** | | |
| 13 | `docs/AUDIO_MODULE.md` — полная техническая документация модуля (12 разделов) | ✅ |
| 14 | `docs/indicator_checklist.md` — интеграционный чек-лист: ~76 тестов, секции 1–5 | ✅ |
| **On-target** | | |
| 15 | Группа A: все звуковые события воспроизводят правильные WAV-файлы | ✅ |
| 16 | Группа B: музыкальный lifecycle (UP→музыка, ротация, BUTTON не сбрасывает, DING сбрасывает) | ✅ |
| 17 | Группа C: вытеснение по приоритету (CRITICAL > FLOOR > MOVEMENT > MUSIC) | ✅ |
| 18 | Группа D: отмена музыки при нештатном режиме и диспетчере | ✅ |
| 19 | HIL-стенд: 24-часовая непрерывная работа | ✅ |
| 20 | `just pi::check-resources` — 124/124 ✅, 0 ошибок | ✅ |
| 21 | `just pi::smoke` — все проверки зелёные (media-ingest — Phase 7, ожидаемо disabled) | ✅ |

**Критерий выполнен.**

---

## 2. Проблемы и решения

### P-30 — Гонка вытеснения: DING пропускается при включённой музыке

**Симптом:** при активной фоновой музыке SOUND_DING (анонс этажа) пропускался.
В логе — два `preempt by prio=N` подряд за одну и ту же миллисекунду,
`music_wanted` не сбрасывался, музыка возобновлялась после события:

```
13:30:37 audio: preempt by prio=1   ← DING вытесняет музыку
13:30:37 audio: preempt by prio=2   ← OPENING вытесняет DING  ← баг
13:30:37 audio: playing 'opening.wav' prio=2
13:30:38 audio: music 'mus2.wav'    ← музыка не остановилась
```

**Причина:** worker-поток обновляет `current_prio` только когда сам подбирает
элемент из очереди. В промежутке между добавлением DING(prio=1) в очередь
и тем моментом когда worker устанавливает `current_prio=1` — `current_prio`
содержит stale-значение `MUSIC(3)`. OPENING(prio=2) видит `2 < 3 → true` и
вытесняет DING, очищая очередь.

**Решение:** изменить условие вытеснения с `prio < current_prio` на
`prio < effective_prio`, где `effective_prio = min(current_prio, min_prio_in_queue)`:

```c
int effective_prio = (int) ap->current_prio;
for (int i = 0; i < ap->queue_count; i++) {
    if ((int) ap->queue[i].prio < effective_prio)
        effective_prio = (int) ap->queue[i].prio;
}
int should_preempt = ((int) prio < effective_prio);
```

Дополнительно: при вытеснении `current_prio` немедленно сбрасывается в
`AUDIO_PRIO_NONE(99)` чтобы убрать stale-значение.

**Проверка после фикса:**
```
13:56:41 audio: preempt by prio=1       ← DING вытесняет музыку
13:56:41 audio: playing '30.wav' prio=1
13:56:42 audio: playing 'floor.wav' prio=1
13:56:42 audio: playing 'opening.wav' prio=2  ← OPENING из очереди, после анонса
                                               ← тишина, music_wanted=0
```

**Файл:** `src/audio/audio.c` — функция `audio_player_play()`
**Статус:** закрыт ✅

---

### P-31 — Исправления sound_map.c (3 бага, обнаружены при верификации WAV)

**Симптом 1:** `SOUND_FIRE_ALARM` → воспроизводился `g_triple.wav`
вместо `fire.wav` (файл существовал в `deploy/sounds/`, но не был прописан).

**Симптом 2:** `SOUND_BUTTON` → воспроизводился `g_single.wav`
вместо `button.wav`. В коде стояли TODO-комментарии.

**Симптом 3:** этажи 41–49 — fallback `g_triple.wav`.
Файл `40-.wav` присутствовал в `deploy/sounds/`, диапазон не был реализован.

**Решение:** три точечных исправления в `sound_map_resolve()`:
```c
case SOUND_FIRE_ALARM:
    seq_add(out, "fire.wav");   /* было: g_triple.wav */

case SOUND_BUTTON:
    seq_add(out, "button.wav"); /* было: g_single.wav */

/* Добавлен блок для 41–49: */
else if (n >= 41 && n <= 49)
{
    seq_add(out, "40-.wav");
    seq_add_num(out, n % 10);
    seq_add(out, "floor.wav");
}
```

Также удалены устаревшие TODO-комментарии у `SOUND_CLOSING` и `SOUND_OPENING`
(файлы `closing.wav`, `opening.wav` подтверждены).

**Тесты:** добавлены `test_sound_fire_alarm_uses_fire_wav`,
`test_sound_button_uses_button_wav`, `test_ding_floor_41/45/49`.

**Файл:** `src/domain/sound_map.c`, `tests/test_sound_map.c`
**Статус:** закрыт ✅

---

### P-32 — dmix IPC deadlock после 24 часов работы

**Симптом:** после ~24 часов работы звук пропал. `aplay -q /data/sounds/up.wav`
зависал без вывода. `amixer sget 'PCM'` — показывал громкость корректно.
Все новые попытки открыть `dmixer` блокировались навсегда.

**Диагностика:**
```bash
ipcs -m | awk 'NR>3 ...'   # зависшие shared memory сегменты dmix
ipcs -s | ...               # зависшие семафоры dmix
```

**Причина:** при вытеснении `audio.c` отправлял `kill(aplay_pid, SIGTERM)`.
Если `aplay` в этот момент удерживал dmix-семафор (запись в буфер),
он завершался не успев его освободить. Все последующие `sem_wait` в новых
`aplay`/`amixer` зависали на этом семафоре навсегда.

Усугубляющий фактор: старый `aplay.service` (Adafruit) работал через
`-D default` с частотой `44100 Hz`, а slave в `dmixer` был `48000 Hz`.
plug-конвертер добавлял лишние слои входа в dmix, повышая вероятность
удержания семафора в момент SIGTERM.

**Решение:**

Немедленный (на текущем устройстве):
```bash
sudo systemctl stop indicator
sudo killall -9 aplay
ipcs -m | awk 'NR>3 && $3=="pi"' | xargs -r ipcrm -m
ipcs -s | awk 'NR>3 && $3=="pi"' | xargs -r ipcrm -s
```

Постоянный: замена `aplay.service` → `i2s-silence.service`:
- `ExecStart=/usr/bin/aplay -D dmixer -t raw -r 48000 -c 2 -f S32_LE /dev/zero`
- Прямое подключение к dmix минуя plug/softvol — меньше слоёв удержания
- Совпадение частоты (48kHz) и формата со slave

**Файл:** `deploy/systemd/i2s-silence.service`, `scripts/setup_pi.sh`
**Статус:** закрыт ✅

---

### P-33 — Неверный формат ALSA: S16_LE вместо S32_LE

**Симптом:** `i2s-silence.service` немедленно завершался с кодом 1
после каждого старта:
```
aplay: set_params:1339: Sample format non available
Available formats:
- S32_LE
```
Сервис входил в цикл 20 рестартов (StartLimitBurst), затем сдавался.
Из-за `After=i2s-silence.service` в `indicator.service` — indicator
ждал завершения всех рестартов (~2 минуты) и только потом стартовал.

**Причина:** чип MAX98357A поддерживает только **S32_LE**. В `asound.conf`
был прописан `format S16_LE`. Ранее это не проявлялось: `aplay.service`
шёл через `-D default` → plug → softvol → dmix; plug-конвертер
автоматически конвертировал форматы. Как только перешли на `-D dmixer`
напрямую — формат должен совпадать точно.

**Решение:**
```bash
# На устройстве (немедленно):
sudo sed -i 's/format      S16_LE/format      S32_LE/' /etc/asound.conf
sudo sed -i 's/-f S16_LE/-f S32_LE/' /etc/systemd/system/i2s-silence.service
```

WAV-файлы (S16_LE) не меняются — plug-конвертер в цепочке `!default`
автоматически конвертирует S16_LE→S32_LE перед dmix.

**Файл:** `scripts/setup_pi.sh` (asound.conf + embedded unit),
`deploy/systemd/i2s-silence.service`
**Статус:** закрыт ✅

---

### P-34 — indicator-setup.service: inline аннотации в значениях

**Симптом:** tty1 не переключался корректно; после `run_setup.sh`
терминал не сбрасывался в начальное состояние.

**Причина:** в unit-файле остались черновые аннотации прямо в значениях:
```ini
TTYVHangup=yes     ← добавить: сбрасывает tty после завершения сервиса
TTYReset=yes       ← добавить: восстанавливает настройки tty
```
Systemd парсит всё что стоит после `=` как значение. Строка
`yes     ← добавить:...` не является валидным boolean → parse error →
директивы игнорировались → tty не сбрасывался корректно.

**Решение:** убраны аннотации `← добавить:...`, оставлены чистые значения:
```ini
TTYVHangup=yes
TTYReset=yes
```

**Файл:** `deploy/systemd/indicator-setup.service`
**Статус:** закрыт ✅

---

### P-35 — i2s-silence.service не стартует на холодном старте

**Симптом:** при перезагрузке `i2s-silence.service` стартовал,
но завершался с кодом 1. Без keepalive — щелчки при каждом звуке.

**Причина:** `sound.target` активируется раньше, чем I2S карта
(googlevoicehat overlay) полностью инициализируется ядром.
Попытка открыть `dmixer` (который открывает `hw:0`) в этот момент
завершается с ошибкой «no such device».

**Решение:** добавлен `ExecStartPre` с ожиданием card 0 (до 15 секунд),
увеличены лимиты для большего числа ретраев:

```ini
StartLimitBurst=20
StartLimitIntervalSec=120

ExecStartPre=/bin/bash -c \
    'for i in $(seq 15); do aplay -l 2>/dev/null | grep -q "card 0" && exit 0; \
     sleep 1; done; echo "card 0 not ready after 15s" >&2; exit 1'
```

**Файл:** `deploy/systemd/i2s-silence.service`, `scripts/setup_pi.sh`
**Статус:** закрыт ✅

---

## 3. Архитектура аудиомодуля

### 3.1 Стек ALSA (финальный)

```
WAV (S16_LE/48kHz/stereo)
    └── aplay → pcm.!default
                    └── plug     ← конвертация S16_LE → S32_LE
                        └── softvol  'PCM'  ← amixer sset 'PCM' N%
                            └── dmixer  (ipc_key=1024, ipc_perm=0666)
                                └── speakerbonnet  hw:0  ← MAX98357A
                                                          S32_LE / 48kHz / stereo

i2s-silence.service:
    aplay -D dmixer -f S32_LE /dev/zero  ← напрямую в dmix, без plug/softvol
```

**Примечание:** `dtoverlay=googlevoicehat-soundcard` — единственный поддерживаемый
overlay для MAX98357A от Adafruit. Несмотря на имя «googlevoicehat» — это не
Google Voice HAT; Adafruit переиспользует его для Speaker Bonnet.

### 3.2 Потоковая модель

```
Главный поток (poll loop)
    audio_player_play(seq, prio)     ← thread-safe, non-blocking
    audio_player_cancel_music()      ← thread-safe, non-blocking
            │  mutex + condvar
            ▼
    Worker-поток (audio_worker)
        ├── pop queue (FIFO, глубина 3)
        ├── maybe_set_volume(vol, &current_vol)   ← lazy amixer, только при смене
        ├── play_file(path)  →  posix_spawn aplay → waitpid (блокирует worker)
        └── music loop: mus1→mus2→…→mus7→mus1 (пока music_wanted && queue пуста)
```

### 3.3 Система приоритетов

| Значение | Константа | Звуки | Сбрасывает `music_wanted` |
|---|---|---|---|
| 0 | `AUDIO_PRIO_CRITICAL` | OVERLOAD, FIRE_ALARM, DONT_WORK | **ДА** |
| 1 | `AUDIO_PRIO_FLOOR` | DING (анонс этажа) | **ДА** |
| 2 | `AUDIO_PRIO_MOVEMENT` | UP, DOWN, CLOSING, OPENING, BUTTON | НЕТ |
| 3 | `AUDIO_PRIO_MUSIC` | Фоновая музыка | — |
| 99 | `AUDIO_PRIO_NONE` | Сентинель (ничего не играет) | — |

**Правило вытеснения:**
`effective_prio = min(current_prio, min_prio_in_queue)`
`should_preempt = (new_prio < effective_prio)`

### 3.4 Музыкальный lifecycle

```
SOUND_UP / SOUND_DOWN с seq.needs_music=1:
    worker → play up.wav/down.wav → music_wanted=1 → music loop

SOUND_BUTTON (prio=2):
    вытесняет музыку → button.wav → music_wanted остаётся 1 → музыка продолжается

SOUND_DING (prio=1):
    вытесняет музыку → анонс этажа → music_wanted=0 → тишина

MODE != NORMAL или dispatch != OFF (из main.c):
    seq.needs_music=0 (перед audio_player_play) → music_wanted не устанавливается
    audio_player_cancel_music() → музыка убивается если играла
```

---

## 4. Маппинг WAV-файлов (финальный)

### Звуковые события

| `sound_t` | WAV | Приоритет |
|---|---|---|
| `SOUND_UP` | `up.wav` + музыка | MOVEMENT |
| `SOUND_DOWN` | `down.wav` + музыка | MOVEMENT |
| `SOUND_CLOSING` | `closing.wav` | MOVEMENT |
| `SOUND_OPENING` | `opening.wav` | MOVEMENT |
| `SOUND_BUTTON` | `button.wav` | MOVEMENT |
| `SOUND_DING` | см. анонс этажа ниже | FLOOR |
| `SOUND_OVERLOAD` | `overload.wav` | CRITICAL |
| `SOUND_FIRE_ALARM` | `fire.wav` | CRITICAL |
| `SOUND_DONT_WORK` | `g_double.wav` | CRITICAL |

### Анонс этажа (SOUND_DING)

| Тип | Диапазон | Последовательность |
|---|---|---|
| NORMAL | 1–20 | `{N}.wav` + `floor.wav` |
| NORMAL | 21–29 | `20-.wav` + `{ones}.wav` + `floor.wav` |
| NORMAL | 30 | `30.wav` + `floor.wav` |
| NORMAL | 31–39 | `30-.wav` + `{ones}.wav` + `floor.wav` |
| NORMAL | 40 | `40.wav` + `floor.wav` |
| NORMAL | 41–49 | `40-.wav` + `{ones}.wav` + `floor.wav` |
| NORMAL | ≥50 | `g_triple.wav` (fallback) |
| BASEMENT | П | `podval.wav` + `floor.wav` |
| BASEMENT_N | П1–П9 | `{N}.wav` + `podval.wav` + `floor.wav` |
| NEGATIVE | −1..−9 | `minus.wav` + `{N}.wav` + `floor.wav` |
| UNKNOWN | — | `g_single.wav` (fallback) |

### Фоновая музыка

`mus1.wav` … `mus7.wav` — последовательная ротация, индекс не сбрасывается
при остановке (продолжает с того же места в следующей поездке).

---

## 5. Latency (on-target, Pi Zero 2W)

| Операция | Замеренное время |
|---|---|
| UART frame → начало воспроизведения (без смены громкости) | < 100 мс |
| Смена громкости (amixer lazy call) | ~20 мс |
| posix_spawn aplay | ~5–10 мс |
| Вытеснение: SIGTERM → следующий файл | < 200 мс |
| i2s-silence keepalive CPU load | ~0.3% |

Цель (MASTER_PLAN): latency UART → звук < 100 мс — **выполнена**.

---

## 6. Новые и изменённые файлы

| Файл | Действие |
|---|---|
| `src/audio/audio.h` | Новый: полный API (заменяет stub `int audio_play(...)`) |
| `src/audio/audio.c` | Новый: полная реализация (~520 строк) |
| `src/domain/sound_map.c` | Обновлён: fire.wav, button.wav, диапазон 41–49 |
| `tests/test_sound_map.c` | Обновлён: 33 теста (+7 новых) |
| `src/main.c` | Обновлён: 7 точечных изменений для интеграции аудио |
| `deploy/systemd/i2s-silence.service` | Новый: I2S keepalive (ExecStartPre, S32_LE) |
| `deploy/systemd/indicator.service` | Обновлён: After/Wants=i2s-silence.service |
| `deploy/systemd/indicator-setup.service` | Исправлен: убраны inline-аннотации |
| `deploy/systemd/media-ingest.service` | Обновлён: ConditionPathExists, Restart=on-failure |
| `scripts/setup_pi.sh` | Обновлён: I2S overlay, asound.conf (S32_LE), i2s-silence, fbi |
| `scripts/smoke_test.sh` | Обновлён: аудио check (googlevoicehat/PCM), i2s-silence, state info |
| `scripts/check_resources.sh` | Обновлён: секция 6 — 45 WAV файлов, режим fail |
| `just/pi.just` | Обновлён: deploy-systemd миграция, новый рецепт restart-audio |
| `docs/AUDIO_MODULE.md` | Новый: техническая документация модуля (12 разделов) |
| `docs/indicator_checklist.md` | Новый: интеграционный чек-лист (~76 тестов) |

---

## 7. Итоговое состояние окружения

```
Raspberry Pi Zero 2W (indicator-01.local)
├── /home/pi/indicator/
│   ├── indicator              ← ARMv6 Release (phase-5) ✅
│   ├── media_ingest           ← ARMv6 Release (stub, Phase 7) ✅
│   ├── pi_nku_sync            ← Rust, pre-built ✅
│   ├── pi_nku_menu            ← Rust, pre-built ✅
│   ├── scripts/
│   │   ├── run_setup.sh       ✅
│   │   ├── first_boot.sh      ✅
│   │   ├── check_resources.sh ← 124 файла ✅
│   │   └── smoke_test.sh      ← обновлён ✅
│   ├── sounds/               ← bind mount → /data/sounds/ (45 WAV) ✅
│   ├── resources/            ← bind mount → /data/resources/ ✅
│   ├── pi_nku_configs/       ← bind mount → /data/pi_nku_configs/ ✅
│   └── videos/               ← bind mount → /data/videos/ ✅
├── /etc/asound.conf            ← dmix+softvol, S32_LE/48kHz ✅
└── /etc/systemd/system/
    ├── indicator-firstboot.service  ✅
    ├── indicator-setup.service      ← TTYVHangup/Reset исправлены ✅
    ├── indicator.service            ← After=i2s-silence ✅
    ├── i2s-silence.service          ← новый, ExecStartPre, S32_LE ✅
    └── media-ingest.service         ← ConditionPathExists, Phase 7 ✅
```

**check-resources:** 124/124 ✅ · **smoke:** все зелёные (media-ingest disabled — ожидаемо)

---

## 8. Открытые вопросы

| ID | Вопрос | Приоритет | Фаза |
|----|--------|-----------|------|
| P-24 | dbus-daemon timeout при stop (вне pgroup omxplayer) | низкий | 6 |
| P-29 | ARROW slow path при каждом появлении | низкий | 6 |

---

## 9. Контекст для Фазы 6

При начале нового треда передать:

1. `PHASE_5_REPORT.md`
2. `MASTER_PLAN.md`
3. `DEV_ARCH.md`
4. Текущие исходники: `src/main.c`, `src/audio/audio.h`,
   `deploy/systemd/*.service`

**Стартовая фраза:**
> Фазы 0–5 и Phase Deploy закрыты. Начинаем Фазу 6 — systemd lifecycle.
> Прикладываю PHASE_5_REPORT и MASTER_PLAN.

---

*Документ сгенерирован по итогам сессии Фазы 5.*
