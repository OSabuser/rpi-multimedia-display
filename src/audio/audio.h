/**
 * @file src/audio/audio.h
 * @brief Асинхронный аудиоплеер на базе pthread + posix_spawn aplay.
 *
 * Ответственность:
 *   - Воспроизводить последовательности WAV-файлов из audio_sequence_t.
 *   - Управлять приоритетами: CRITICAL > FLOOR > MOVEMENT > MUSIC.
 *   - Вытеснять текущее воспроизведение при приходе более приоритетного события.
 *   - Управлять фоновой музыкой (mus1–mus7.wav) после SOUND_UP/DOWN.
 *   - Устанавливать громкость через amixer sset 'PCM' N%.
 *
 * Потоковая модель:
 *   Все вызовы API (кроме open/close) thread-safe.
 *   Воспроизведение происходит в отдельном worker-потоке.
 *   Главный поток не блокируется никогда.
 *
 * Не знает о:
 *   DispmanX, UART, state machine, renderer, video player.
 *   Не знает о config_t — получает параметры при open().
 *
 * Только Pi: зависит от posix_spawn, aplay, amixer.
 * На хосте не тестируется (Platform-specific).
 */

#pragma once

#include "domain/sound_map.h" /* audio_sequence_t */

/* ─── Приоритеты ──────────────────────────────────────────────────────────── */

/**
 * audio_prio_t — приоритет события.
 *
 * Меньше число → выше приоритет.
 * Вытеснение происходит когда prio_new < prio_current.
 *
 * Маппинг событий (sound_to_prio() в main.c):
 *   CRITICAL  ←  SOUND_OVERLOAD, SOUND_FIRE_ALARM, SOUND_DONT_WORK
 *   FLOOR     ←  SOUND_DING
 *   MOVEMENT  ←  SOUND_UP, SOUND_DOWN, SOUND_CLOSING, SOUND_OPENING, SOUND_BUTTON
 *   MUSIC     ←  фоновая музыка (запускается автоматически, не из on_uart_frame)
 */
typedef enum audio_prio_e
{
    AUDIO_PRIO_CRITICAL = 0, /* немедленное вытеснение, сброс музыки   */
    AUDIO_PRIO_FLOOR    = 1, /* анонс этажа, сброс музыки              */
    AUDIO_PRIO_MOVEMENT = 2, /* движение: не сбрасывает music_wanted   */
    AUDIO_PRIO_MUSIC = 3, /* фоновая музыка, низший приоритет        */
} audio_prio_t;

/* ─── Opaque handle ───────────────────────────────────────────────────────── */

typedef struct audio_player_s audio_player_t;

/* ─── API ─────────────────────────────────────────────────────────────────── */

/**
 * audio_player_open — создать плеер и запустить worker-поток.
 *
 * Устанавливает ALSA PCM на sound_vol_pct% через amixer.
 *
 * @param sounds_dir      путь к директории звуков (/data/sounds)
 * @param sound_vol_pct   громкость событий (0–100)
 * @param music_vol_pct   громкость музыки (0–100); 0 = музыка отключена
 * @return                дескриптор или NULL при ошибке malloc/pthread
 *
 * При ошибке amixer — продолжает работу (non-fatal).
 */
audio_player_t *audio_player_open(const char *p_sounds_dir, int sound_vol_pct, int music_vol_pct);

/**
 * audio_player_close — остановить worker, дождаться завершения, освободить память.
 *
 * Текущее воспроизведение прерывается через SIGTERM.
 * Безопасен при NULL.
 */
void audio_player_close(audio_player_t *p_ap);

/**
 * audio_player_play — поставить в очередь или вытеснить текущее воспроизведение.
 *
 * Thread-safe, не блокирует.
 *
 * Правила:
 *   prio < current_prio → вытеснить (kill текущего aplay, очистить очередь).
 *   prio >= current_prio → добавить в конец очереди (FIFO, глубина 3).
 *   При полной очереди — удаляет самый старый элемент (overwrite head).
 *
 * Если seq->valid == 0 — вызов игнорируется.
 */
void audio_player_play(audio_player_t *p_ap, const audio_sequence_t *p_seq, audio_prio_t prio);

/**
 * audio_player_cancel_music — немедленно прекратить музыку и запретить её запуск.
 *
 * Thread-safe, не блокирует.
 *
 * Вызывать из main.c при:
 *   - переходе в нештатный режим (mode != MODE_NORMAL)
 *   - активации диспетчерской связи (dispatch != DISPATCH_OFF)
 *
 * Если музыка не играет — сбрасывает только music_wanted флаг.
 */
void audio_player_cancel_music(audio_player_t *p_ap);
