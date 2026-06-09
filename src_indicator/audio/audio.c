/**
 * @file src/audio/audio.c
 * @brief Асинхронный аудиоплеер — реализация.
 *
 * Дизайн потоковой безопасности:
 *   Все поля audio_player_s защищены mutex, кроме:
 *   - immutable-полей (sounds_dir, *_vol_pct), которые задаются при open() и не меняются.
 *   Функции API (play, cancel_music) держат mutex только для изменения состояния,
 *   никогда не блокируются на ввод/вывод.
 *
 * Вытеснение:
 *   Главный поток (audio_player_play) записывает current_pid, если prio < current_prio,
 *   вызывает kill(current_pid, SIGTERM). Worker-поток ждёт waitpid, который вернётся
 *   сразу после того как aplay поймёт SIGTERM. Затем worker проверяет очередь.
 *
 * Громкость:
 *   ALSA PCM контролируется через amixer. Объём изменяется только при переходе между
 *   sound_vol_pct и music_vol_pct (lazy, через current_vol кэш в worker).
 *
 * Музыка (mus1..mus7.wav):
 *   music_wanted = 1 устанавливается в worker после воспроизведения элемента с
 *   needs_music = 1 (только SOUND_UP / SOUND_DOWN).
 *   music_wanted = 0 сбрасывается при:
 *     - воспроизведении элемента с prio <= AUDIO_PRIO_FLOOR
 *     - вызове audio_player_cancel_music()
 *
 * SIGCHLD:
 *   SIGCHLD от aplay-дочерних процессов будет доставлен в signalfd главного потока.
 *   video_player_check_and_restart() вызовет waitpid(omxplayer_pid, WNOHANG) —
 *   безвредно: будет проверять только pid omxplayer, не трогая pid aplay.
 *   Worker делает waitpid(aplay_pid, 0), что корректно пожнёт aplay-дочерних.
 */
#define _GNU_SOURCE

#include "audio/audio.h"

#include "domain/sound_map.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

/* ─── Константы ──────────────────────────────────────────────────────────── */

/** Глубина очереди событий */
#define AUDIO_QUEUE_DEPTH 3

/** Количество файлов фоновой музыки (mus1.wav .. mus7.wav) */
#define AUDIO_MUSIC_COUNT 7

/**
 * IDLE приоритет «ничего не воспроизводится».
 * Любой реальный приоритет меньше этого значения →
 * любой новый элемент «вытесняет» (на деле — просто добавляется в пустую очередь).
 */
#define AUDIO_PRIO_NONE 99

/** Максимальный путь к файлу */
#define AUDIO_PATH_MAX 512

/* ─── Элемент очереди ────────────────────────────────────────────────────── */

typedef struct
{
    audio_sequence_t seq;
    audio_prio_t prio;
} audio_queue_item_t;

/* ─── Структура плеера ───────────────────────────────────────────────────── */

struct audio_player_s
{
    /* Потоковые примитивы */
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    /* Очередь (защищена mutex) */
    audio_queue_item_t queue[AUDIO_QUEUE_DEPTH];
    int queue_count;

    /* Текущее воспроизведение (защищено mutex) */
    pid_t current_pid;         /**< PID aplay или -1 */
    audio_prio_t current_prio; /**< Приоритет текущего или AUDIO_PRIO_NONE */

    /* Состояние музыки (защищено mutex) */
    int music_wanted; /**< 1 = музыка должна играть при первой возможности */
    int music_next; /**< Индекс следующего трека, 0-based, циклически */

    /* Управление (защищено mutex) */
    int shutdown; /**< 1 = завершить worker */

    /* Immutable после open() (не защищены mutex) */
    char sounds_dir[256]; /**< Путь к директории WAV-файлов */
    int sound_vol_pct;    /**< Громкость событий, % */
    int music_vol_pct;    /**< Громкость музыки, %; 0 = отключена */
};

/* ─── Внутренние утилиты ─────────────────────────────────────────────────── */

/**
 * set_alsa_volume — установить ALSA PCM через amixer.
 *
 * Синхронный вызов из worker-потока. Latency ~15–25 мс.
 * При ошибке spawn — логирует и продолжает.
 */
static void set_alsa_volume(int vol_pct)
{
    char vol_str[16];
    (void) snprintf(vol_str, sizeof(vol_str), "%d%%", vol_pct);

    char *argv[] = { "amixer", "sset", "PCM", vol_str, NULL };

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);

    /* Сбросить маску сигналов в дочернем процессе:
     * main-поток блокирует SIGTERM/SIGCHLD через sigprocmask для signalfd.
     * Без сброса amixer унаследует заблокированные сигналы. */
    sigset_t empty;
    sigemptyset(&empty);
    (void) posix_spawnattr_setsigmask(&attr, &empty);
    (void) posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);

    pid_t pid;
    int rc = posix_spawnp(&pid, "amixer", NULL, &attr, argv, environ);
    posix_spawnattr_destroy(&attr);

    if (rc != 0)
    {
        syslog(LOG_WARNING, "audio: amixer spawn failed: %s", strerror(rc));
        return;
    }

    int status;
    if (waitpid(pid, &status, 0) < 0)
    {
        syslog(LOG_WARNING, "audio: amixer waitpid: %s", strerror(errno));
    }
}

/**
 * maybe_set_volume — установить громкость только если она изменилась.
 *
 * @param target_vol  целевая громкость %
 * @param current_vol [in/out] последняя установленная громкость; -1 = неизвестна
 */
static void maybe_set_volume(int target_vol, int *p_current_vol)
{
    if (*p_current_vol == target_vol)
    {
        return;
    }
    set_alsa_volume(target_vol);
    *p_current_vol = target_vol;
}

/**
 * spawn_aplay — запустить aplay в фоне, вернуть PID.
 *
 * @return  PID дочернего процесса или -1 при ошибке
 */
static pid_t spawn_aplay(const char *p_path)
{
    char *argv[] = { "aplay", "-q", (char *) p_path, NULL };

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);

    /* Разблокировать все сигналы в дочернем процессе (см. set_alsa_volume) */
    sigset_t empty;
    sigemptyset(&empty);
    (void) posix_spawnattr_setsigmask(&attr, &empty);
    (void) posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);

    pid_t pid;
    int rc = posix_spawnp(&pid, "aplay", NULL, &attr, argv, environ);
    posix_spawnattr_destroy(&attr);

    if (rc != 0)
    {
        syslog(LOG_ERR, "audio: aplay spawn '%s': %s", p_path, strerror(rc));
        return (pid_t) -1;
    }
    return pid;
}

/**
 * play_file — воспроизвести один WAV-файл синхронно.
 *
 * Обновляет current_pid в структуре плеера (под mutex).
 * waitpid() разблокируется при выходе aplay (штатном или по SIGTERM).
 *
 * @return  0 при успехе или нормальном выходе, -1 при ошибке spawn
 */
static int play_file(audio_player_t *p_ap, const char *p_path)
{
    pid_t pid = spawn_aplay(p_path);
    if (pid < 0)
    {
        return -1;
    }

    pthread_mutex_lock(&p_ap->mutex);
    p_ap->current_pid = pid;
    pthread_mutex_unlock(&p_ap->mutex);

    int status;
    (void) waitpid(pid, &status, 0);

    pthread_mutex_lock(&p_ap->mutex);
    p_ap->current_pid = (pid_t) -1;
    pthread_mutex_unlock(&p_ap->mutex);

    return 0;
}

/**
 * check_preempted — проверить, был ли текущий элемент вытеснен.
 *
 * Вытеснение происходит если в очереди появился элемент с более высоким приоритетом,
 * чем тот, что сейчас воспроизводится.
 *
 * @param current_prio приоритет текущего (воспроизводимого) элемента
 */
static int check_preempted(audio_player_t *p_ap, audio_prio_t current_prio)
{
    pthread_mutex_lock(&p_ap->mutex);
    int preempted = (p_ap->queue_count > 0 && p_ap->queue[0].prio < current_prio);
    pthread_mutex_unlock(&p_ap->mutex);
    return preempted;
}

/* ─── Worker-поток ───────────────────────────────────────────────────────── */

static void *audio_worker(void *p_arg)
{
    audio_player_t *ap = (audio_player_t *) p_arg;

    /* Локальный кэш текущего уровня ALSA — избегаем лишних вызовов amixer */
    int current_vol = -1; /* -1 = уровень не установлен */

    while (1)
    {
        /* ── Ждём работы ─────────────────────────────────────────────────── */
        pthread_mutex_lock(&ap->mutex);

        while (ap->queue_count == 0 && !(ap->music_wanted && ap->music_vol_pct > 0) &&
               !ap->shutdown)
        {
            pthread_cond_wait(&ap->cond, &ap->mutex);
        }

        if (ap->shutdown && ap->queue_count == 0)
        {
            pthread_mutex_unlock(&ap->mutex);
            break;
        }

        /* ── Обработка элемента очереди ──────────────────────────────────── */
        if (ap->queue_count > 0)
        {
            /* Pop front (FIFO) */
            audio_queue_item_t item = ap->queue[0];
            (void) memmove(ap->queue, ap->queue + 1,
                           sizeof(ap->queue[0]) * (size_t) (ap->queue_count - 1));
            ap->queue_count--;
            ap->current_prio = item.prio;

            /* CRITICAL и FLOOR сбрасывают music_wanted */
            if (item.prio <= AUDIO_PRIO_FLOOR)
            {
                ap->music_wanted = 0;
            }

            pthread_mutex_unlock(&ap->mutex);

            /* Установить громкость для звуковых событий */
            maybe_set_volume(ap->sound_vol_pct, &current_vol);

            /* Воспроизвести каждый файл из последовательности */
            for (int i = 0; i < item.seq.count; i++)
            {
                /* Проверить shutdown */
                pthread_mutex_lock(&ap->mutex);
                int do_shutdown = ap->shutdown;
                pthread_mutex_unlock(&ap->mutex);
                if (do_shutdown)
                {
                    break;
                }

                char path[AUDIO_PATH_MAX];
                (void) snprintf(path, sizeof(path), "%s/%s", ap->sounds_dir, item.seq.files[i]);

                syslog(LOG_DEBUG, "audio: playing '%s' prio=%d", item.seq.files[i],
                       (int) item.prio);

                if (play_file(ap, path) < 0)
                {
                    continue;
                }

                /* Проверить вытеснение: вышестоящий приоритет появился в очереди */
                if (check_preempted(ap, item.prio))
                {
                    syslog(LOG_DEBUG, "audio: preempted at file %d of %d", i + 1, item.seq.count);
                    break;
                }
            }

            /* После завершения последовательности: запомнить запрос на музыку */
            pthread_mutex_lock(&ap->mutex);
            if (item.seq.needs_music)
            {
                ap->music_wanted = 1;
            }
            ap->current_prio = AUDIO_PRIO_NONE;
            pthread_mutex_unlock(&ap->mutex);

            continue; /* вернуться в начало — проверить очередь снова */
        }

        /* ── Воспроизведение фоновой музыки ──────────────────────────────── */
        /* Сюда попадаем если queue_count == 0, music_wanted = 1, music_vol > 0 */
        {
            int next_idx     = ap->music_next;
            ap->music_next   = (ap->music_next + 1) % AUDIO_MUSIC_COUNT;
            ap->current_prio = AUDIO_PRIO_MUSIC;
            pthread_mutex_unlock(&ap->mutex);

            /* Установить громкость для музыки */
            maybe_set_volume(ap->music_vol_pct, &current_vol);

            char filename[32];
            (void) snprintf(filename, sizeof(filename), "mus%d.wav", next_idx + 1);
            char path[AUDIO_PATH_MAX];
            (void) snprintf(path, sizeof(path), "%s/%s", ap->sounds_dir, filename);

            syslog(LOG_DEBUG, "audio: music '%s'", filename);

            if (play_file(ap, path) < 0)
            {
                /* Ошибка spawn: пауза чтобы не молотить в цикле */
                const struct timespec PAUSE = { .tv_sec = 1, .tv_nsec = 0 };
                nanosleep(&PAUSE, NULL);
            }

            pthread_mutex_lock(&ap->mutex);
            ap->current_prio = AUDIO_PRIO_NONE;
            /* music_wanted не трогаем: если не отменили — следующий трек сыграет */
            pthread_mutex_unlock(&ap->mutex);
        }
        /* Цикл продолжается: на следующей итерации проверит music_wanted снова */
    }

    return NULL;
}

/* ─── Публичный API ──────────────────────────────────────────────────────── */

audio_player_t *audio_player_open(const char *p_sounds_dir, int sound_vol_pct, int music_vol_pct)
{
    audio_player_t *ap = calloc(1, sizeof(*ap));
    if (ap == NULL)
    {
        syslog(LOG_CRIT, "audio: calloc failed");
        return NULL;
    }

    /* Immutable конфиг */
    (void) strncpy(ap->sounds_dir, p_sounds_dir, sizeof(ap->sounds_dir) - 1U);
    ap->sound_vol_pct = sound_vol_pct;
    ap->music_vol_pct = music_vol_pct;

    /* Изначальное состояние */
    ap->current_pid  = (pid_t) -1;
    ap->current_prio = AUDIO_PRIO_NONE;
    ap->music_next   = 0;

    /* Потоковые примитивы */
    if (pthread_mutex_init(&ap->mutex, NULL) != 0)
    {
        syslog(LOG_CRIT, "audio: pthread_mutex_init failed");
        free(ap);
        return NULL;
    }
    if (pthread_cond_init(&ap->cond, NULL) != 0)
    {
        syslog(LOG_CRIT, "audio: pthread_cond_init failed");
        pthread_mutex_destroy(&ap->mutex);
        free(ap);
        return NULL;
    }

    /* Установить начальную громкость */
    set_alsa_volume(sound_vol_pct);
    syslog(LOG_INFO, "audio: opened sounds_dir=%s sound=%d%% music=%d%%", ap->sounds_dir,
           sound_vol_pct, music_vol_pct);

    /* Запустить worker */
    if (pthread_create(&ap->thread, NULL, audio_worker, ap) != 0)
    {
        syslog(LOG_CRIT, "audio: pthread_create failed");
        pthread_cond_destroy(&ap->cond);
        pthread_mutex_destroy(&ap->mutex);
        free(ap);
        return NULL;
    }

    return ap;
}

void audio_player_close(audio_player_t *p_ap)
{
    if (p_ap == NULL)
    {
        return;
    }

    /* Сигнализируем worker о завершении */
    pthread_mutex_lock(&p_ap->mutex);
    p_ap->shutdown = 1;
    /* Прервать текущее воспроизведение */
    if (p_ap->current_pid > 0)
    {
        (void) kill(p_ap->current_pid, SIGTERM);
    }
    pthread_cond_signal(&p_ap->cond);
    pthread_mutex_unlock(&p_ap->mutex);

    /* Ждём завершения worker */
    (void) pthread_join(p_ap->thread, NULL);

    pthread_cond_destroy(&p_ap->cond);
    pthread_mutex_destroy(&p_ap->mutex);
    free(p_ap);

    syslog(LOG_INFO, "audio: closed");
}

void audio_player_play(audio_player_t *p_ap, const audio_sequence_t *p_seq, audio_prio_t prio)
{
    if (p_ap == NULL || p_seq == NULL || !p_seq->valid)
    {
        return;
    }

    pthread_mutex_lock(&p_ap->mutex);

    /*
     * Решение о вытеснении через эффективный приоритет.
     *
     * Проблема наивной проверки (prio < current_prio):
     *   worker-поток обновляет current_prio только когда подбирает элемент.
     *   Если DING(1) уже поставлен в очередь, но worker ещё не успел взять его,
     *   current_prio всё ещё = MUSIC(3). Следующий OPENING(2) видит 2 < 3 → true
     *   и вытесняет DING из очереди — DING теряется, music_wanted не сбрасывается.
     *
     * Решение: effective_prio = min(current_prio, min приоритет в очереди).
     *   Тогда OPENING(2) видит effective_prio=min(3,1)=1 → 2 < 1 = false → не вытесняет.
     *
     * При вытеснении: убиваем текущий aplay, очищаем очередь,
     * сбрасываем current_prio в NONE чтобы не оставлять stale-значение.
     */
    int effective_prio = (int) p_ap->current_prio;
    for (int i = 0; i < p_ap->queue_count; i++)
    {
        if ((int) p_ap->queue[i].prio < effective_prio)
        {
            effective_prio = (int) p_ap->queue[i].prio;
        }
    }
    int should_preempt = ((int) prio < effective_prio);

    if (should_preempt)
    {
        if (p_ap->current_pid > 0)
        {
            (void) kill(p_ap->current_pid, SIGTERM);
        }
        p_ap->queue_count = 0;
        p_ap->current_prio = AUDIO_PRIO_NONE; /* сбросить: worker обновит когда подберёт элемент */
        syslog(LOG_DEBUG, "audio: preempt by prio=%d", (int) prio);
    }

    /* Добавить в очередь */
    if (p_ap->queue_count < AUDIO_QUEUE_DEPTH)
    {
        p_ap->queue[p_ap->queue_count].seq  = *p_seq;
        p_ap->queue[p_ap->queue_count].prio = prio;
        p_ap->queue_count++;
    }
    else
    {
        /* Очередь полна: удалить старейший (head), добавить новый в конец */
        syslog(LOG_WARNING, "audio: queue full, dropping oldest item");
        (void) memmove(p_ap->queue, p_ap->queue + 1,
                       sizeof(p_ap->queue[0]) * (size_t) (AUDIO_QUEUE_DEPTH - 1));
        p_ap->queue[AUDIO_QUEUE_DEPTH - 1].seq  = *p_seq;
        p_ap->queue[AUDIO_QUEUE_DEPTH - 1].prio = prio;
    }

    pthread_cond_signal(&p_ap->cond);
    pthread_mutex_unlock(&p_ap->mutex);
}

void audio_player_cancel_music(audio_player_t *p_ap)
{
    if (p_ap == NULL)
    {
        return;
    }

    pthread_mutex_lock(&p_ap->mutex);

    p_ap->music_wanted = 0;

    /* Если музыка сейчас играет — убить немедленно */
    if (p_ap->current_pid > 0 && p_ap->current_prio == AUDIO_PRIO_MUSIC)
    {
        (void) kill(p_ap->current_pid, SIGTERM);
        syslog(LOG_DEBUG, "audio: music cancelled (was playing)");
    }
    else
    {
        syslog(LOG_DEBUG, "audio: music cancelled (music_wanted cleared)");
    }

    pthread_mutex_unlock(&p_ap->mutex);
}
