/**
 * src/domain/sound_map.c
 *
 * Изменения по сравнению с предыдущей версией:
 *   SOUND_FIRE_ALARM  : g_triple.wav → fire.wav   (файл существует в deploy/sounds/)
 *   SOUND_BUTTON      : g_single.wav → button.wav  (файл существует в deploy/sounds/)
 *   FLOOR_TYPE_NORMAL : добавлен диапазон 41–49 через 40-.wav (файл существует)
 *   SOUND_CLOSING     : TODO-комментарий удалён (closing.wav подтверждён)
 *   SOUND_OPENING     : TODO-комментарий удалён (opening.wav подтверждён)
 *   SOUND_DONT_WORK   : TODO-комментарий обновлён (g_double.wav подтверждён)
 */

#include "domain/sound_map.h"

#include <stdio.h>
#include <string.h>

/* ─── Внутренние утилиты ─────────────────────────────────────────────────── */

static void seq_add(audio_sequence_t *seq, const char *filename)
{
    if (seq->count >= AUDIO_SEQ_MAX_FILES)
        return;
    (void) snprintf(seq->files[seq->count], AUDIO_FILENAME_MAX, "%s", filename);
    seq->count++;
}

static void seq_add_num(audio_sequence_t *seq, int n)
{
    char buf[AUDIO_FILENAME_MAX];
    (void) snprintf(buf, sizeof(buf), "%d.wav", n);
    seq_add(seq, buf);
}

/* ─── Объявление этажа ───────────────────────────────────────────────────── */

/**
 * resolve_floor_announcement — построить последовательность WAV для объявления этажа.
 *
 * Правила для FLOOR_TYPE_NORMAL:
 *   1–20:   {N}.wav + floor.wav
 *   21–29:  20-.wav + {ones}.wav + floor.wav
 *   30:     30.wav  + floor.wav
 *   31–39:  30-.wav + {ones}.wav + floor.wav
 *   40:     40.wav  + floor.wav
 *   41–49:  40-.wav + {ones}.wav + floor.wav
 *   >49:    g_triple.wav  (fallback)
 *
 * Правила для подвалов и отрицательных — см. switch ниже.
 */
static void resolve_floor_announcement(floor_t floor, audio_sequence_t *p_out)
{
    switch (floor.type)
    {
    case FLOOR_TYPE_NORMAL:
    {
        int n = floor.number;
        if (n >= 1 && n <= 20)
        {
            seq_add_num(p_out, n);
            seq_add(p_out, "floor.wav");
        }
        else if (n >= 21 && n <= 29)
        {
            seq_add(p_out, "20-.wav");
            seq_add_num(p_out, n % 10);
            seq_add(p_out, "floor.wav");
        }
        else if (n == 30)
        {
            seq_add(p_out, "30.wav");
            seq_add(p_out, "floor.wav");
        }
        else if (n >= 31 && n <= 39)
        {
            seq_add(p_out, "30-.wav");
            seq_add_num(p_out, n % 10);
            seq_add(p_out, "floor.wav");
        }
        else if (n == 40)
        {
            seq_add(p_out, "40.wav");
            seq_add(p_out, "floor.wav");
        }
        else if (n >= 41 && n <= 49)
        {
            seq_add(p_out, "40-.wav");
            seq_add_num(p_out, n % 10);
            seq_add(p_out, "floor.wav");
        }
        else
        {
            /* >49: нет WAV-файлов, тройной гонг как fallback */
            seq_add(p_out, "g_triple.wav");
        }
        break;
    }

    case FLOOR_TYPE_BASEMENT:
        /* П (подвал без номера) */
        seq_add(p_out, "podval.wav");
        seq_add(p_out, "floor.wav");
        break;

    case FLOOR_TYPE_BASEMENT_N:
        /* П1–П9 */
        seq_add_num(p_out, floor.number);
        seq_add(p_out, "podval.wav");
        seq_add(p_out, "floor.wav");
        break;

    case FLOOR_TYPE_NEGATIVE:
        /* -1 .. -9 */
        seq_add(p_out, "minus.wav");
        seq_add_num(p_out, floor.number);
        seq_add(p_out, "floor.wav");
        break;

    case FLOOR_TYPE_UNKNOWN:
    default:
        seq_add(p_out, "g_single.wav");
        break;
    }
}

/* ─── Публичный API ──────────────────────────────────────────────────────── */

void sound_map_resolve(sound_t sound, floor_t floor, audio_sequence_t *p_out)
{
    (void) memset(p_out, 0, sizeof(*p_out));

    switch (sound)
    {
    case SOUND_NONE:
        p_out->valid = 0;
        return;

    case SOUND_DING:
        p_out->valid = 1;
        resolve_floor_announcement(floor, p_out);
        return;

    case SOUND_UP:
        p_out->valid = 1;
        p_out->needs_music = 1; /* после UP запускается фоновая музыка */
        seq_add(p_out, "up.wav");
        return;

    case SOUND_DOWN:
        p_out->valid = 1;
        p_out->needs_music = 1; /* после DOWN запускается фоновая музыка */
        seq_add(p_out, "down.wav");
        return;

    case SOUND_CLOSING:
        p_out->valid = 1;
        seq_add(p_out, "closing.wav");
        return;

    case SOUND_OPENING:
        p_out->valid = 1;
        seq_add(p_out, "opening.wav");
        return;

    case SOUND_OVERLOAD:
        p_out->valid = 1;
        seq_add(p_out, "overload.wav");
        return;

    case SOUND_FIRE_ALARM:
        p_out->valid = 1;
        seq_add(p_out, "fire.wav"); /* fire.wav подтверждён в deploy/sounds/ */
        return;

    case SOUND_DONT_WORK:
        p_out->valid = 1;
        seq_add(p_out, "g_double.wav"); /* g_double.wav подтверждён в deploy/sounds/ */
        return;

    case SOUND_BUTTON:
        p_out->valid = 1;
        seq_add(p_out, "button.wav"); /* button.wav подтверждён в deploy/sounds/ */
        return;

    default:
        p_out->valid = 0;
        return;
    }
}

int sound_map_volume_percent(sound_t sound, int sound_vol_pct)
{
    if (sound == SOUND_NONE)
    {
        return 0;
    }
    return sound_vol_pct;
}
