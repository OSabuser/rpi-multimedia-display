/**
 * src/domain/sound_map.c
 */

#include "domain/sound_map.h"

#include <stdio.h>
#include <string.h>

static void seq_add(audio_sequence_t *seq, const char *filename)
{
    if (seq->count >= AUDIO_SEQ_MAX_FILES)
        return;
    snprintf(seq->files[seq->count], AUDIO_FILENAME_MAX, "%s", filename);
    seq->count++;
}

static void seq_add_num(audio_sequence_t *seq, int n)
{
    char buf[AUDIO_FILENAME_MAX];
    snprintf(buf, sizeof(buf), "%d.wav", n);
    seq_add(seq, buf);
}

static void resolve_floor_announcement(floor_t floor, audio_sequence_t *out)
{
    switch (floor.type)
    {
    case FLOOR_TYPE_NORMAL:
    {
        int n = floor.number;
        if (n >= 1 && n <= 20)
        {
            seq_add_num(out, n);
            seq_add(out, "floor.wav");
        }
        else if (n >= 21 && n <= 29)
        {
            seq_add(out, "20-.wav");
            seq_add_num(out, n % 10);
            seq_add(out, "floor.wav");
        }
        else if (n == 30)
        {
            seq_add(out, "30.wav");
            seq_add(out, "floor.wav");
        }
        else if (n >= 31 && n <= 39)
        {
            seq_add(out, "30-.wav");
            seq_add_num(out, n % 10);
            seq_add(out, "floor.wav");
        }
        else if (n == 40)
        {
            seq_add(out, "40.wav");
            seq_add(out, "floor.wav");
        }
        else
        {
            seq_add(out, "g_triple.wav");
        }
        break;
    }
    case FLOOR_TYPE_BASEMENT:
        seq_add(out, "podval.wav");
        seq_add(out, "floor.wav");
        break;
    case FLOOR_TYPE_BASEMENT_N:
        seq_add_num(out, floor.number);
        seq_add(out, "podval.wav");
        seq_add(out, "floor.wav");
        break;
    case FLOOR_TYPE_NEGATIVE:
        seq_add(out, "minus.wav");
        seq_add_num(out, floor.number);
        seq_add(out, "floor.wav");
        break;
    case FLOOR_TYPE_UNKNOWN:
    default:
        seq_add(out, "g_single.wav");
        break;
    }
}

void sound_map_resolve(sound_t sound, floor_t floor, audio_sequence_t *out)
{
    memset(out, 0, sizeof(*out));

    switch (sound)
    {

    case SOUND_NONE:
        out->valid = 0;
        return;

    case SOUND_DING:
        out->valid = 1;
        resolve_floor_announcement(floor, out);
        return;

    case SOUND_UP:
        out->valid       = 1;
        out->needs_music = 1;
        seq_add(out, "up.wav");
        return;

    case SOUND_DOWN:
        out->valid       = 1;
        out->needs_music = 1;
        seq_add(out, "down.wav");
        return;

    case SOUND_CLOSING:
        out->valid = 1;
        seq_add(out, "closing.wav"); /* TODO: подтвердить наличие файла */
        return;

    case SOUND_OPENING:
        out->valid = 1;
        seq_add(out, "opening.wav"); /* TODO: подтвердить наличие файла */
        return;

    case SOUND_OVERLOAD:
        out->valid = 1;
        seq_add(out, "overload.wav");
        return;

    case SOUND_FIRE_ALARM:
        /* TODO: уточнить WAV-файл для пожарной тревоги */
        out->valid = 1;
        seq_add(out, "g_triple.wav");
        return;

    case SOUND_DONT_WORK:
        /* TODO: уточнить WAV-файл для "лифт не работает" */
        out->valid = 1;
        seq_add(out, "g_double.wav");
        return;

    case SOUND_BUTTON:
        /* TODO: уточнить WAV-файл для нажатия кнопки */
        out->valid = 1;
        seq_add(out, "g_single.wav");
        return;

    default:
        out->valid = 0;
        return;
    }
}

int sound_map_volume_percent(sound_t sound, int sound_vol_pct)
{
    if (sound == SOUND_NONE)
        return 0;
    return sound_vol_pct;
}