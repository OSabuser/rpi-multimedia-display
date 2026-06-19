/**
 * @file src_indicator/app_handlers.c
 * @brief Обработчики событий: UART, media IPC, watchdog, уведомления.
 *
 * Все функции получают app_t* и работают через публичный API подсистем.
 * Статистика хранится в g_s_stats (определена в main.c, extern в app_private.h).
 */
#define _GNU_SOURCE

#include "app_private.h"
#include "audio/audio.h"
#include "domain/floor.h"
#include "domain/sound_map.h"
#include "domain/state.h"
#include "media/media_ipc.h"
#include "player/video_player.h"
#include "protocol/parser.h"
#include "renderer/renderer.h"

#include <errno.h>
#include <string.h>
#include <sys/timerfd.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

/* ─── Константы ──────────────────────────────────────────────────────────── */

static const unsigned int S_NOTIF_MCU_OK_HIDE_S = 4U;

/* ─── Маппинг звук → приоритет ───────────────────────────────────────────── */

/**
 * sound_to_prio — маппинг звукового события → приоритет воспроизведения.
 *
 * CRITICAL : SOUND_OVERLOAD, SOUND_FIRE_ALARM, SOUND_DONT_WORK
 *            вытесняют всё, сбрасывают музыку.
 * FLOOR    : SOUND_DING — вытесняет движение и музыку.
 * MOVEMENT : UP/DOWN/CLOSING/OPENING/BUTTON — не сбрасывают music_wanted.
 */
static audio_prio_t sound_to_prio(sound_t sound)
{
    switch (sound)
    {
    case SOUND_OVERLOAD:
    case SOUND_FIRE_ALARM:
    case SOUND_DONT_WORK:
        return AUDIO_PRIO_CRITICAL;
    case SOUND_DING:
        return AUDIO_PRIO_FLOOR;
    default:
        return AUDIO_PRIO_MOVEMENT;
    }
}

/* ─── Notification timer ─────────────────────────────────────────────────── */

void notif_arm(int notif_fd, unsigned int delay_sec)
{
    if (notif_fd < 0)
    {
        return;
    }
    const struct itimerspec ts = {
        .it_interval = { .tv_sec = 0, .tv_nsec = 0 },
        .it_value    = { .tv_sec = (time_t) delay_sec, .tv_nsec = 0 },
    };
    if (timerfd_settime(notif_fd, 0, &ts, NULL) < 0)
    {
        syslog(LOG_WARNING, "notif_arm: timerfd_settime: %s", strerror(errno));
    }
}

void notif_disarm(int notif_fd)
{
    if (notif_fd < 0)
    {
        return;
    }
    const struct itimerspec ts = {
        .it_interval = { .tv_sec = 0, .tv_nsec = 0 },
        .it_value    = { .tv_sec = 0, .tv_nsec = 0 },
    };
    if (timerfd_settime(notif_fd, 0, &ts, NULL) < 0)
    {
        syslog(LOG_WARNING, "notif_disarm: timerfd_settime: %s", strerror(errno));
    }
}

/* ─── MCU notification ───────────────────────────────────────────────────── */

void maybe_clear_mcu_notification(app_t *p_app)
{
    if (p_app->setup_status == SETUP_STATUS_OK)
    {
        return;
    }

    p_app->setup_status = SETUP_STATUS_OK;
    syslog(LOG_NOTICE, "setup: MCU communication established — clearing notification");
    renderer_show_png(p_app->renderer, SPRITE_NOTIFICATION, NOTIF_DIR "/notif_mcu_ok.png");
    notif_arm(p_app->notif_fd, S_NOTIF_MCU_OK_HIDE_S);
}

/* ─── Media status ───────────────────────────────────────────────────────── */

void on_media_status(app_t *p_app)
{
    static const char *const S_NOTIF_PATHS[MEDIA_STATUS_MAX] = {
        [MEDIA_FOUND]      = NOTIF_DIR "/notif_found.png",
        [MEDIA_PROCESSING] = NOTIF_DIR "/notif_processing.png",
        [MEDIA_DONE]       = NOTIF_DIR "/notif_success.png",
        [MEDIA_NO_VIDEO]   = NOTIF_DIR "/notif_no_video.png",
        [MEDIA_EJECT]      = NOTIF_DIR "/notif_eject.png",
        [MEDIA_ERROR]      = NOTIF_DIR "/notif_error.png",
        /* [MEDIA_CLEAR] (6) → NULL — handled below */
    };

    media_status_t status;
    if (media_ipc_read(p_app->fifo_fd, &status) < 0)
    {
        return;
    }

    syslog(LOG_INFO, "media: status=%d", (int) status);

    if (status == MEDIA_CLEAR)
    {
        renderer_hide(p_app->renderer, SPRITE_NOTIFICATION);
        notif_disarm(p_app->notif_fd);
        return;
    }

    if (status == MEDIA_DONE)
    {
        video_player_replace(p_app->video);
    }

    if (S_NOTIF_PATHS[(unsigned int) status] != NULL)
    {
        renderer_show_png(p_app->renderer, SPRITE_NOTIFICATION,
                          S_NOTIF_PATHS[(unsigned int) status]);
    }
}

/* ─── Watchdog tick ──────────────────────────────────────────────────────── */

void on_watchdog_tick(const app_t *p_app)
{
    syslog(LOG_NOTICE,
           "watchdog: frames_ok=%u parse_errors=%u unknown_opcodes=%u | "
           "last: floor=%d arrow=%d mode=%d | "
           "omxplayer_pid=%d win=%d,%d,%dx%d",
           g_s_stats.frames_ok, g_s_stats.parse_errors, g_s_stats.unknown_opcodes,
           g_s_stats.last_floor_num, g_s_stats.last_arrow, g_s_stats.last_mode,
           video_player_get_pid(p_app->video), p_app->cfg.video_win_x, p_app->cfg.video_win_y,
           p_app->cfg.video_win_w, p_app->cfg.video_win_h);

    /* P-28: VideoCore IV dormant workaround.
     * Пустой DispmanX update каждые 30 с предотвращает остановку видео
     * при длительном отсутствии активности рендерера. */
    renderer_keepalive(p_app->renderer);
}

/* ─── UART callback ──────────────────────────────────────────────────────── */

void on_uart_frame(const mu_frame_t *p_frame, void *p_ctx)
{
    app_t *p_app = (app_t *) p_ctx;

    /* ── Диспетчерская связь (opcode=0xAA) ───────────────────────────────── */
    if (p_frame->opcode == MU_OPCODE_DISPATCH)
    {
        dispatch_state_t dispatch;
        parse_result_t r = protocol_parse_dispatch(p_frame, &dispatch);
        if (r != PARSE_OK)
        {
            g_s_stats.parse_errors++;
            syslog(LOG_WARNING, "dispatch: parse error %d", (int) r);
            return;
        }

        maybe_clear_mcu_notification(p_app);
        state_update_result_t upd = state_apply_dispatch(&p_app->state, dispatch);
        if (upd.dispatch_changed)
        {
            syslog(LOG_NOTICE, "dispatch: state=%d (%s)", (int) dispatch,
                   dispatch == DISPATCH_CALL     ? "CALL"
                   : dispatch == DISPATCH_ANSWER ? "ANSWER"
                                                 : "OFF");

            if (p_app->renderer != NULL)
            {
                renderer_apply_dispatch(p_app, dispatch);
            }

            if (p_app->audio != NULL && p_app->state.active_dispatch != DISPATCH_OFF)
            {
                audio_player_cancel_music(p_app->audio);
            }
        }
        return;
    }

    /* ── Статус лифта (opcode=0xDA) ───────────────────────────────────────── */
    if (p_frame->opcode == MU_OPCODE_ELEVATOR_STATUS)
    {
        parsed_frame_t payload;
        parse_result_t r = protocol_parse_payload(p_frame, &payload);
        if (r != PARSE_OK)
        {
            g_s_stats.parse_errors++;
            syslog(LOG_WARNING, "uart: protocol_parse_payload error %d", (int) r);
            return;
        }

        g_s_stats.frames_ok++;

        maybe_clear_mcu_notification(p_app);

        floor_t floor             = floor_decode(payload.left_char, payload.right_char);
        state_update_result_t upd = state_apply_frame(&p_app->state, &payload);

        syslog(LOG_INFO,
               "frame #%u: floor=%d(%d) arrow=%d sound=%d mode=%d | "
               "changed: floor=%d arrow=%d mode=%d sound=%d first=%d dispatch_active=%d",
               g_s_stats.frames_ok, floor.number, (int) floor.type, (int) payload.arrow,
               (int) payload.sound, (int) payload.mode, upd.floor_changed, upd.arrow_changed,
               upd.mode_changed, upd.sound_triggered, upd.first_frame,
               (p_app->state.active_dispatch != DISPATCH_OFF) ? 1 : 0);

        g_s_stats.last_floor_num = floor.number;
        g_s_stats.last_arrow     = (int) payload.arrow;
        g_s_stats.last_mode      = (int) payload.mode;

        if (p_app->renderer != NULL)
        {
            renderer_apply_elevator(p_app, &upd, &payload);
        }

        /* ── Аудио ──────────────────────────────────────────────────────── */
        if (p_app->audio != NULL)
        {
            if (upd.sound_triggered)
            {
                audio_sequence_t seq;
                sound_map_resolve(payload.sound, floor, &seq);
                if (seq.valid)
                {
                    /* Не запускать музыку в нештатном режиме или при активном диспетчере */
                    if (payload.mode != MODE_NORMAL || p_app->state.active_dispatch != DISPATCH_OFF)
                    {
                        seq.needs_music = 0;
                    }
                    audio_prio_t prio = sound_to_prio(payload.sound);
                    audio_player_play(p_app->audio, &seq, prio);
                }
            }

            /* Отменить музыку при нештатном режиме */
            if ((upd.mode_changed || upd.first_frame) && payload.mode != MODE_NORMAL)
            {
                audio_player_cancel_music(p_app->audio);
            }
        }
        return;
    }

    /* ── Неизвестный opcode ───────────────────────────────────────────────── */
    g_s_stats.unknown_opcodes++;
    syslog(LOG_DEBUG, "uart: unknown opcode 0x%02X len=%u", (unsigned) p_frame->opcode,
           (unsigned) p_frame->data_len);
}