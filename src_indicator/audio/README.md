# Модуль `audio` — техническая документация

**Файлы:** `src/audio/audio.h`, `src/audio/audio.c`  
**Зависимости:** `src/domain/sound_map.h`, POSIX threads, `aplay`, `amixer`  
**Платформа:** Raspberry Pi Zero 2W / Debian Buster / ALSA

---

## 1. Назначение

Модуль отвечает за асинхронное воспроизведение звуков и фоновой музыки на лифтовом индикаторе. Он принимает события от доменного уровня (`audio_sequence_t`), управляет приоритетами вытеснения, и взаимодействует с ALSA через внешние утилиты `aplay` и `amixer`.

Модуль не знает о UART, DispmanX, renderer, video player или конфигурации — он получает только путь к директории звуков и два числа громкости.

---

## 2. Архитектура

### 2.1 Потоковая модель

```bash
Главный поток (poll loop)          Worker-поток (audio_worker)
─────────────────────────          ──────────────────────────
on_uart_frame()                    ждёт pthread_cond_wait
  └─ audio_player_play()     ───►  просыпается
       lock → queue → signal       lock → pop queue → unlock
                                   set_alsa_volume()  [amixer]
audio_player_cancel_music()        play_file()        [aplay]
  └─ lock → music_wanted=0         waitpid()
     → kill(music_pid)             lock → next item / music
```

**Инварианты:**

- Главный поток **никогда не блокируется** на I/O внутри `audio_player_play()` или `audio_player_cancel_music()`.
- Worker-поток **единственный** кто вызывает `spawn_aplay()` и `waitpid()`.
- Все поля `audio_player_s`, кроме immutable-конфига (`sounds_dir`, `*_vol_pct`), защищены `mutex`.

### 2.2 Структура данных

```c
struct audio_player_s {
    pthread_t        thread;        // worker
    pthread_mutex_t  mutex;
    pthread_cond_t   cond;

    audio_queue_item_t queue[3];    // FIFO очередь событий
    int                queue_count;

    pid_t        current_pid;       // PID aplay или -1
    audio_prio_t current_prio;      // приоритет текущего или AUDIO_PRIO_NONE(99)

    int music_wanted;               // 1 = запустить музыку при первой возможности
    int music_next;                 // индекс следующего трека (0-based, mod 7)
    int shutdown;                   // 1 = завершить worker

    char sounds_dir[256];           // /data/sounds
    int  sound_vol_pct;             // громкость событий, 0–100
    int  music_vol_pct;             // громкость музыки, 0–100; 0 = отключена
};
```

---

## 3. Система приоритетов

| Значение | Константа           | Звуки                                              | Сбрасывает `music_wanted` |
|----------|---------------------|----------------------------------------------------|---------------------------|
| 0        | `AUDIO_PRIO_CRITICAL` | `SOUND_OVERLOAD`, `SOUND_FIRE_ALARM`, `SOUND_DONT_WORK` | **ДА**              |
| 1        | `AUDIO_PRIO_FLOOR`    | `SOUND_DING` (анонс этажа)                         | **ДА**                    |
| 2        | `AUDIO_PRIO_MOVEMENT` | `SOUND_UP`, `SOUND_DOWN`, `SOUND_CLOSING`, `SOUND_OPENING`, `SOUND_BUTTON` | НЕТ |
| 3        | `AUDIO_PRIO_MUSIC`    | Фоновая музыка (внутренний, не передаётся в API)   | —                         |
| 99       | `AUDIO_PRIO_NONE`     | Сентинель «ничего не воспроизводится»              | —                         |

Меньше число — **выше приоритет**. Вытеснение происходит только когда `prio_new < effective_prio`.

Маппинг из `sound_t` в приоритет — в `main.c`, функция `sound_to_prio()`.

---

## 4. Семантика вытеснения

### 4.1 Определение

**Вытеснение (preemption) — hard cut:** текущее воспроизведение немедленно прерывается, вся очередь очищается, новое событие занимает единственное место.

```c
kill(current_pid, SIGTERM);  // прервать aplay на полуслове
queue_count = 0;             // удалить всё из очереди
current_prio = AUDIO_PRIO_NONE;
// добавить новый элемент
```

Вытесненные звуки **не восстанавливаются**. Это намеренно: контекст, породивший их, уже неактуален.

### 4.2 Условие вытеснения — effective_prio

Наивная проверка `prio_new < current_prio` содержит гонку: worker обновляет `current_prio` только когда подбирает элемент из очереди. В промежутке между добавлением высокоприоритетного события в очередь и моментом когда worker его подберёт, `current_prio` содержит stale-значение.

**Решение:** вытеснение оценивается по `effective_prio = min(current_prio, min_prio_in_queue)`:

```c
int effective_prio = (int) ap->current_prio;
for (int i = 0; i < ap->queue_count; i++) {
    if ((int) ap->queue[i].prio < effective_prio) {
        effective_prio = (int) ap->queue[i].prio;
    }
}
int should_preempt = ((int) prio < effective_prio);
```

**Пример:** DING(1) поставлен в очередь, `current_prio` ещё = MUSIC(3). Приходит OPENING(2):

```bash
effective_prio = min(3, 1) = 1
2 < 1 → false → OPENING не вытесняет DING, встаёт в очередь за ним
```

### 4.3 Очередь после вытеснения

Звуки, пришедшие **после** высокоприоритетного события, добавляются в очередь нормально и воспроизводятся после него. Типичная последовательность:

```bash
DING(1) → вытесняет музыку → queue: [DING]
OPENING(2) → effective_prio=1, 2<1=false → queue: [DING, OPENING]

Worker: '30.wav' → 'floor.wav' → 'opening.wav' → тишина
```

### 4.4 Полная матрица вытеснений

| Новое событие ↓ \ Текущее → | CRITICAL(0) | FLOOR(1) | MOVEMENT(2) | MUSIC(3) | Ничего(99) |
|-----------------------------|-------------|----------|-------------|----------|------------|
| **CRITICAL(0)**             | Вытесняет   | Вытесняет | Вытесняет  | Вытесняет | В очередь |
| **FLOOR(1)**                | Не вытесняет | Не вытесняет | Вытесняет | Вытесняет | В очередь |
| **MOVEMENT(2)**             | Не вытесняет | Не вытесняет | Не вытесняет | Вытесняет | В очередь |
| **MUSIC(3)**                | Не вытесняет | Не вытесняет | Не вытесняет | Не вытесняет | В очередь |

*Примечание: «Вытесняет» = обрывает текущий звук и очищает очередь. «В очередь» = добавляется за текущим.*

---

## 5. Жизненный цикл музыки

Фоновая музыка (`mus1.wav`…`mus7.wav`) управляется флагом `music_wanted` и индексом `music_next`.

### 5.1 Запуск

Музыка запускается автоматически после воспроизведения любого события с `seq.needs_music = 1`. В текущей реализации это только `SOUND_UP` и `SOUND_DOWN`. Условия запуска:

1. `music_wanted == 1`
2. `queue_count == 0` (нет ожидающих событий)
3. `music_vol_pct > 0`

### 5.2 Ротация

Треки воспроизводятся последовательно: `mus1→mus2→…→mus7→mus1`. Индекс `music_next` не сбрасывается при остановке — следующий сеанс продолжает с того же места.

### 5.3 Остановка

`music_wanted` сбрасывается в 0 в трёх случаях:

| Ситуация | Место в коде | Лог |
|---|---|---|
| Worker обрабатывает событие с `prio ≤ FLOOR(1)` | `audio_worker()`, pop-ветка | *(нет отдельной строки)* |
| Вызов `audio_player_cancel_music()`, музыка играет | `audio_player_cancel_music()` | `audio: music cancelled (was playing)` |
| Вызов `audio_player_cancel_music()`, музыка не играет | `audio_player_cancel_music()` | `audio: music cancelled (music_wanted cleared)` |

`audio_player_cancel_music()` вызывается из `main.c` при:

- переходе в нештатный режим (`payload.mode != MODE_NORMAL`)
- активации диспетчерской связи (`active_dispatch != DISPATCH_OFF`)

### 5.4 Поведение BUTTON

`SOUND_BUTTON` имеет `prio = AUDIO_PRIO_MOVEMENT(2)`. Он **вытесняет музыку** (`2 < 3`), но `prio=2 > FLOOR=1`, поэтому worker **не сбрасывает** `music_wanted`. После `button.wav` музыка возобновляется:

```bash
music 'musN.wav' → [BUTTON arrives] → preempt by prio=2
playing 'button.wav' prio=2 → music 'musN.wav'  (следующий трек)
```

---

## 6. ALSA-стек и громкость

### 6.1 Конфигурация

```bash
/dev/i2s (MAX98357A)
    └── speakerbonnet  (hw:0,0)
        └── dmixer     (dmix — разделяемый микшер, ipc_key=1024)
            └── softvol  (программный контрол 'PCM', 0–100%)
                └── !default  (plug → softvol)
```

`i2s-silence.service` держит `dmixer` постоянно активным через `/dev/zero`, устраняя щелчки PLL при старте/конце каждого WAV.

### 6.2 Управление громкостью

Громкость устанавливается через `amixer sset 'PCM' N%` из worker-потока. Реализован lazy-update: вызов происходит только при **смене** уровня между событиями и музыкой:

```bash
sound_vol_pct=75%  → событие: amixer sset 'PCM' 75%
                   → событие: (не вызывать, уже 75%)
                   → музыка:  amixer sset 'PCM' 75%  (одинаково → тоже пропустить)
```

Latency amixer: ~15–25 мс. Вызывается до `spawn_aplay()`, поэтому первый сэмпл уже играет с правильной громкостью.

### 6.3 Формат WAV

Все файлы в `/data/sounds/` должны быть в формате **48000 Hz / S16_LE / stereo**. Несовпадение формата с `slave` в `dmixer` приведёт к plug-конвертации и потенциальным задержкам.

---

## 7. Воспроизведение файлов

### 7.1 `spawn_aplay`

Каждый WAV запускается через `posix_spawnp("aplay", "-q", path)`. Флаг `POSIX_SPAWN_SETSIGMASK` с `sigemptyset` сбрасывает маску сигналов в дочернем процессе: главный поток блокирует `SIGTERM/SIGCHLD` через `sigprocmask` для `signalfd`, и без сброса дочерний `aplay` не получал бы `SIGTERM` при вытеснении.

### 7.2 `waitpid` и SIGCHLD

Worker делает блокирующий `waitpid(aplay_pid, 0)`. `SIGCHLD` от завершения `aplay` доставляется в `signalfd` главного потока. `handle_signal()` вызывает `video_player_check_and_restart()` с `waitpid(omxplayer_pid, WNOHANG)` — это безвредно: проверяется только конкретный PID omxplayer, не PID aplay.

### 7.3 Проверка вытеснения внутри последовательности

Для событий с несколькими файлами (например DING: `'30.wav'` + `'floor.wav'`) вытеснение проверяется **после каждого файла** через `check_preempted()`:

```c
if (check_preempted(ap, item.prio)) {
    break;  // прервать последовательность досрочно
}
```

Это позволяет CRITICAL-событию прервать анонс этажа на середине.

---

## 8. Очередь событий

**Глубина:** 3 элемента. **Порядок:** FIFO внутри одного приоритета.

При переполнении очереди (все 3 слота заняты, вытеснения нет): старейший элемент (head) удаляется, новый добавляется в конец. В лог пишется `audio: queue full, dropping oldest item`.

Глубина 3 выбрана как компромисс между памятью и realtime-поведением. Накопление более трёх событий без воспроизведения означает что worker отстаёт — это аномалия, не штатный режим.

---

## 9. Публичный API

### `audio_player_open`

```c
audio_player_t *audio_player_open(
    const char *sounds_dir,    // "/data/sounds"
    int         sound_vol_pct, // 0–100
    int         music_vol_pct  // 0–100; 0 = музыка отключена
);
```

Создаёт плеер, запускает worker-поток, устанавливает начальную громкость через amixer. При ошибке `malloc` или `pthread_create` возвращает `NULL`. Ошибка amixer — non-fatal, работа продолжается без звука.

### `audio_player_close`

```c
void audio_player_close(audio_player_t *ap); // safe with NULL
```

Устанавливает `shutdown=1`, убивает текущий aplay, сигнализирует worker через condvar, ждёт `pthread_join`. Безопасен при `NULL`.

### `audio_player_play`

```c
void audio_player_play(
    audio_player_t         *ap,
    const audio_sequence_t *seq,  // из sound_map_resolve()
    audio_prio_t            prio  // из sound_to_prio() в main.c
);
```

Thread-safe, не блокируется. При `seq->valid == 0` — no-op.

### `audio_player_cancel_music`

```c
void audio_player_cancel_music(audio_player_t *ap); // safe with NULL
```

Thread-safe, не блокируется. Сбрасывает `music_wanted`. Если музыка играет — убивает текущий aplay. Вызывать при переходе в нештатный режим или активации диспетчера.

---

## 10. Интеграция в main.c

```c
// Инициализация (после config_load)
app.audio = audio_player_open(SOUNDS_DIR,
                              app.cfg.sound_volume_percent,
                              app.cfg.music_volume_percent);
if (app.audio == NULL) {
    syslog(LOG_ERR, "audio_player_open failed — audio disabled");
}

// В on_uart_frame(), opcode=0xDA
if (p_app->audio != NULL) {
    if (upd.sound_triggered) {
        audio_sequence_t seq;
        sound_map_resolve(payload.sound, floor, &seq);
        if (seq.valid) {
            audio_player_play(p_app->audio, &seq, sound_to_prio(payload.sound));
        }
    }
    if ((upd.mode_changed || upd.first_frame) && payload.mode != MODE_NORMAL) {
        audio_player_cancel_music(p_app->audio);
    }
}

// В on_uart_frame(), opcode=0xAA (dispatch)
if (p_app->audio != NULL && p_app->state.active_dispatch != DISPATCH_OFF) {
    audio_player_cancel_music(p_app->audio);
}

// Завершение
audio_player_close(app.audio);
```

---

## 11. Диагностика через journalctl

```bash
journalctl -u indicator -f | grep "audio:"
```

| Сообщение | Значение |
|---|---|
| `audio: opened sounds_dir=… sound=75% music=75%` | Успешная инициализация |
| `audio: player ready, sound=75% music=75%` | Worker запущен |
| `audio: playing 'X.wav' prio=N` | Начало воспроизведения файла |
| `audio: music 'musN.wav'` | Начало фонового трека |
| `audio: preempt by prio=N` | Вытеснение: текущее прервано, очередь очищена |
| `audio: preempted at file M of N` | Последовательность прервана досрочно |
| `audio: music cancelled (was playing)` | Музыка убита по cancel_music() |
| `audio: music cancelled (music_wanted cleared)` | Флаг сброшен, музыка не играла |
| `audio: queue full, dropping oldest item` | Очередь переполнена, старейший элемент потерян |
| `audio: amixer spawn failed: …` | amixer недоступен (non-fatal) |
| `audio: aplay spawn '…': …` | aplay недоступен (файл пропускается) |
| `audio: closed` | Штатное завершение |

---

## 12. Известные ограничения

| Ограничение | Последствие | Обходной путь |
|---|---|---|
| `aplay` — внешний процесс | +fork/exec latency ~10–30 мс | Приемлемо: latency UART→звук ≈ 50–100 мс |
| `amixer` — синхронный вызов из worker | Блокирует worker на ~20 мс при смене громкости | Lazy update: вызов только при смене уровня |
| Глубина очереди = 3 | При быстрой серии событий старые теряются | Нормальное поведение; аномалия если >3 |
| WAV-формат фиксирован | `aplay` упадёт при несовпадении с dmixer slave | Конвертировать все звуки при деплое |
| Нет host-side тестов для audio.c | Зависит от posix_spawn/aplay/amixer | Тестируется только на Pi (HIL) |