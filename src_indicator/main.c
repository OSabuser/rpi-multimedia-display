/**
 * @file main.c
 * @brief Lift Indicator — composition root и главный poll-цикл.
 *
 * Использование:
 *   indicator --config=/path/to/nku_scheme.toml
 *
 * Sibling-файлы рядом с --config:
 *   video.toml    → параметры окна omxplayer
 *   renderer.toml → позиции слотов DispmanX, путь к ресурсам
 *
 * _GNU_SOURCE требуется для:
 *   signalfd / SFD_NONBLOCK / SFD_CLOEXEC    <sys/signalfd.h>
 *   timerfd_create / TFD_NONBLOCK / TFD_CLOEXEC  <sys/timerfd.h>
 *   CLOCK_MONOTONIC                           <time.h>
 *   sigemptyset / sigaddset / sigprocmask     <signal.h>
 */
#define _GNU_SOURCE

#include "audio/audio.h"
#include "config/config.h"
#include "domain/floor.h"
#include "domain/sound_map.h"
#include "domain/state.h"
#include "media/media_ipc.h"
#include "player/video_player.h"
#include "protocol/parser.h"
#include "protocol/types.h"
#include "renderer/renderer.h"
#include "transport/uart.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

/* ─── Константы ─────────────────────────────────────────────────────────── */

static const char *const DEFAULT_CONFIG_PATH = "/data/pi_nku_configs/nku_scheme.toml";

static const char *const VIDEO_CONFIG_FILENAME    = "video.toml";
static const char *const RENDERER_CONFIG_FILENAME = "renderer.toml";
static const char *const UART_CONFIG_FILENAME     = "pi_scheme.toml";
static const char *const SOUNDS_DIR               = "/data/sounds";
static const char *const VIDEO_PATH               = "/data/videos/output.mp4";
#define NOTIF_DIR "/data/resources/notifications"
static const int WATCHDOG_INTERVAL_S = 30;

static const unsigned int NOTIF_MCU_OK_HIDE_S = 4U;
/* ─── Индексы pollfd ─────────────────────────────────────────────────────── */

typedef enum fd_index_e
{
    FD_UART  = 0,
    FD_SIG   = 1,
    FD_TIMER = 2,
    FD_FIFO  = 3, /* FIFO от media-ingest → media_ipc_open()   */
    FD_NOTIF = 4, /* one-shot timerfd для автоскрытия уведомления */
    FD_COUNT = 5,
} fd_index_t;

typedef enum setup_status_e
{
    SETUP_STATUS_UNKNOWN     = 0, /* файл не найден или не распознан */
    SETUP_STATUS_OK          = 1, /* pull + push успешны             */
    SETUP_STATUS_PENDING     = 2, /* setup ещё не завершён           */
    SETUP_STATUS_PULL_FAILED = 3, /* pull провалился                 */
    SETUP_STATUS_PUSH_FAILED = 4, /* push провалился — MCU не стримит */
} setup_status_t;

/* ─── Контекст приложения ────────────────────────────────────────────────── */

typedef struct app_s
{
    config_t cfg;
    indicator_state_t state;
    setup_status_t setup_status;
    uart_t *uart;
    video_player_t *video;
    renderer_t *renderer;
    audio_player_t *audio;
    int fifo_fd;  /**< fd FIFO от media-ingest; -1 если не открыт  */
    int notif_fd; /**< timerfd автоскрытия уведомления; -1 если нет */
} app_t;

/* ─── Статистика ─────────────────────────────────────────────────────────── */

typedef struct stats_s
{
    unsigned frames_ok;
    unsigned parse_errors;
    unsigned unknown_opcodes;
    int last_floor_num;
    int last_arrow;
    int last_mode;
} stats_t;

static stats_t g_s_stats;

static const char *const SETUP_STATUS_PATH = "/data/setup_status";

/* ─── Утилиты путей ──────────────────────────────────────────────────────── */

static void derive_sibling_path(const char *p_base_path, const char *p_filename, char *p_out,
                                size_t out_sz)
{
    const char *last_slash = strrchr(p_base_path, '/');
    if (last_slash == NULL)
    {
        strncpy(p_out, p_filename, out_sz - 1U);
        p_out[out_sz - 1U] = '\0';
        return;
    }
    size_t dir_len = (size_t) (last_slash - p_base_path) + 1U;
    if (dir_len >= out_sz)
    {
        dir_len = out_sz - 1U;
    }
    strncpy(p_out, p_base_path, dir_len);
    p_out[dir_len] = '\0';
    strncat(p_out, p_filename, out_sz - dir_len - 1U);
}

/**
 * sound_to_prio — маппинг звукового события → приоритет воспроизведения.
 *
 * CRITICAL : SOUND_OVERLOAD, SOUND_FIRE_ALARM, SOUND_DONT_WORK
 *            вытесняют всё, сбрасывают музыку.
 * FLOOR    : SOUND_DING
 *            вытесняет движение и музыку, сбрасывает музыку.
 * MOVEMENT : SOUND_UP, SOUND_DOWN, SOUND_CLOSING, SOUND_OPENING, SOUND_BUTTON
 *            не сбрасывают music_wanted → музыка может возобновиться после BUTTON.
 */
static audio_prio_t sound_to_prio(sound_t sound) /* PHASE 5 */
{
    switch (sound)
    {
    case SOUND_OVERLOAD:
    case SOUND_FIRE_ALARM:
    case SOUND_DONT_WORK:
        return AUDIO_PRIO_CRITICAL;
    case SOUND_DING:
        return AUDIO_PRIO_FLOOR;
    default: /* UP, DOWN, CLOSING, OPENING, BUTTON */
        return AUDIO_PRIO_MOVEMENT;
    }
}

/* ─── Парсинг аргументов ─────────────────────────────────────────────────── */

static const char *parse_config_path(int argc, char *p_argv[])
{
    static const char PREFIX[]  = "--config=";
    static const int PREFIX_LEN = 9;

    for (int i = 1; i < argc; i++)
    {
        if (strncmp(p_argv[i], PREFIX, (size_t) PREFIX_LEN) == 0)
        {
            return &p_argv[i][PREFIX_LEN];
        }
    }
    return DEFAULT_CONFIG_PATH;
}

/* ─── Маппинг mode → относительный путь к PNG ───────────────────────────── */

/**
 * mode_to_rel_path — вернуть путь к PNG относительно resources_dir.
 * NULL → скрыть MODE-слот.
 */
static const char *mode_to_rel_path(indicator_mode_t mode)
{
    switch (mode)
    {
    case MODE_FIRE_ALARM:
        return "modes/firealarm.png";
    case MODE_MALFUNCTION:
        return "modes/malfunction.png";
    case MODE_LOADING:
        return "modes/loading.png";
    case MODE_OVERLOAD:
        return "modes/overload.png";
    case MODE_SEIS_ALARM:
        return "modes/seismo.png";
    case MODE_FIREMANS:
        return "modes/fireman.png";
    case MODE_SERVICE:
        return "modes/inspection.png";
    case MODE_EVACUATION:
        return "modes/evacuation.png";
    case MODE_UPS_MALFUNCTION:
        return "modes/malfunction.png";
    case MODE_DISPATCH_CALL:
        return "modes/calling.png";
    case MODE_DISPATCH_ANSWER:
        return "modes/talking.png";
    default:
        return NULL; /* MODE_NORMAL, MODE_CONN_LOST → hide */
    }
}

/* ─── Обновление рендерера ───────────────────────────────────────────────── */

/**
 * renderer_apply_mode — обновить MODE-слот по текущему режиму.
 * Вызывается и при изменении mode (opcode=0xDA), и при изменении dispatch (opcode=0xAA).
 */
static void renderer_apply_mode(app_t *p_app, indicator_mode_t mode)
{
    const char *rd  = p_app->cfg.rdr.resources_dir;
    const char *rel = mode_to_rel_path(mode);
    char path[512];

    if (rel != NULL)
    {
        (void) snprintf(path, sizeof(path), "%s/%s", rd, rel);
        renderer_show_png(p_app->renderer, SPRITE_MODE, path);
    }
    else
    {
        renderer_hide(p_app->renderer, SPRITE_MODE);
    }
}

/**
 * renderer_apply_dispatch — обновить MODE-слот при изменении диспетчерского состояния.
 *
 * Dispatch имеет приоритет над mode из 0xDA:
 *   DISPATCH_CALL / DISPATCH_ANSWER → показать соответствующую иконку
 *   DISPATCH_OFF                    → восстановить mode из текущего состояния лифта
 */
static void renderer_apply_dispatch(app_t *p_app, dispatch_state_t dispatch)
{
    if (dispatch != DISPATCH_OFF)
    {
        indicator_mode_t mode =
            (dispatch == DISPATCH_CALL) ? MODE_DISPATCH_CALL : MODE_DISPATCH_ANSWER;
        renderer_apply_mode(p_app, mode);
    }
    else
    {
        /* Dispatch закончился — восстановить mode из последнего 0xDA фрейма */
        if (p_app->state.initialized)
        {
            renderer_apply_mode(p_app, p_app->state.mode);
        }
        else
        {
            renderer_hide(p_app->renderer, SPRITE_MODE);
        }
    }
}

/**
 * renderer_apply_elevator — обновить цифры, стрелку и режим по результату state_apply_frame().
 *
 * Правило MODE-слота:
 *   Если диспетчер активен (active_dispatch != OFF) — MODE-слот не трогаем:
 *   dispatch-иконка важнее.
 */
static void renderer_apply_elevator(app_t *p_app, const state_update_result_t *p_upd,
                                    const parsed_frame_t *p_payload)
{
    renderer_t *rdr = p_app->renderer;
    const char *rd  = p_app->cfg.rdr.resources_dir;
    char path[512];

    /* ── Цифры этажа ──────────────────────────────────────────────────── */
    if (p_upd->floor_changed || p_upd->first_frame)
    {
        (void) snprintf(path, sizeof(path), "%s/chars/%u.png", rd, (unsigned) p_payload->left_char);
        renderer_show_png(rdr, SPRITE_DIGIT_LEFT, path);

        (void) snprintf(path, sizeof(path), "%s/chars/%u.png", rd,
                        (unsigned) p_payload->right_char);
        renderer_show_png(rdr, SPRITE_DIGIT_RIGHT, path);
    }

    /* ── Стрелка направления ──────────────────────────────────────────── */
    if (p_upd->arrow_changed || p_upd->first_frame)
    {
        switch (p_payload->arrow)
        {
        case ARROW_UP:
            (void) snprintf(path, sizeof(path), "%s/arrows/up.png", rd);
            renderer_show_png(rdr, SPRITE_ARROW, path);
            break;
        case ARROW_DOWN:
            (void) snprintf(path, sizeof(path), "%s/arrows/down.png", rd);
            renderer_show_png(rdr, SPRITE_ARROW, path);
            break;
        default: /* ARROW_NONE, ARROW_BOTH → скрыть */
            renderer_hide(rdr, SPRITE_ARROW);
            break;
        }
    }

    /* ── Режим: только если диспетчер не активен ──────────────────────── */
    if ((p_upd->mode_changed || p_upd->first_frame) && p_app->state.active_dispatch == DISPATCH_OFF)
    {
        renderer_apply_mode(p_app, p_payload->mode);
    }
}

/* ─── Setup status ───────────────────────────────────────────────────────── */

static setup_status_t read_setup_status(const char *p_path)
{
    FILE *file_desc = fopen(p_path, "r");
    if (file_desc == NULL)
    {
        return SETUP_STATUS_UNKNOWN;
    }

    char buf[32];
    if (fgets(buf, sizeof(buf), file_desc) == NULL)
    {
        fclose(file_desc);
        return SETUP_STATUS_UNKNOWN;
    }
    fclose(file_desc);

    buf[strcspn(buf, "\r\n")] = '\0'; /* trim newline */

    if (strcmp(buf, "ok") == 0)
    {
        return SETUP_STATUS_OK;
    }
    if (strcmp(buf, "pending") == 0)
    {
        return SETUP_STATUS_PENDING;
    }
    if (strcmp(buf, "pull_failed") == 0)
    {
        return SETUP_STATUS_PULL_FAILED;
    }
    if (strcmp(buf, "push_failed") == 0)
    {
        return SETUP_STATUS_PUSH_FAILED;
    }

    return SETUP_STATUS_UNKNOWN;
}

/* ─── Notification timer helpers ─────────────────────────────────────────── */

/**
 * notif_arm — взвести одноразовый timerfd для автоскрытия уведомления.
 *
 * @param notif_fd  fd из timerfd_create(); отрицательный — no-op
 * @param delay_sec задержка в секундах до срабатывания
 */
static void notif_arm(int notif_fd, unsigned int delay_sec)
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

/**
 * notif_disarm — отменить таймер автоскрытия (it_value = 0).
 *
 * @param notif_fd  fd из timerfd_create(); отрицательный — no-op
 */
static void notif_disarm(int notif_fd)
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

/* ─── Media status handler ───────────────────────────────────────────────── */

/**
 * on_media_status — прочитать статус из FIFO и обновить SPRITE_NOTIFICATION.
 *
 * MEDIA_CLEAR  → скрыть уведомление, отменить таймер.
 * MEDIA_DONE   → дополнительно вызвать video_player_replace().
 * Остальные    → показать соответствующий PNG.
 */
static void on_media_status(app_t *p_app)
{
    static const char *const NOTIF_PATHS[MEDIA_STATUS_MAX] = {
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

    if (NOTIF_PATHS[(unsigned int) status] != NULL)
    {
        renderer_show_png(p_app->renderer, SPRITE_NOTIFICATION, NOTIF_PATHS[(unsigned int) status]);
    }
}
/**
 * Вызывается при первом валидном фрейме любого opcode.
 * Скрывает уведомление об отсутствии связи с MCU.
 * Идемпотентна: после первого срабатывания ничего не делает.
 */
static void maybe_clear_mcu_notification(app_t *p_app)
{
    if (p_app->setup_status == SETUP_STATUS_OK)
    {
        return;
    }

    p_app->setup_status = SETUP_STATUS_OK;
    syslog(LOG_NOTICE, "setup: MCU communication established — clearing notification");
    renderer_show_png(p_app->renderer, SPRITE_NOTIFICATION, NOTIF_DIR "/notif_mcu_ok.png");
    notif_arm(p_app->notif_fd, NOTIF_MCU_OK_HIDE_S);
}
/* ─── UART коллбэк ───────────────────────────────────────────────────────── */

static void on_uart_frame(const mu_frame_t *p_frame, void *p_ctx)
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

        /* ── Аудио (Фаза 5) ────────────────────────────────────────────── */
        if (p_app->audio != NULL)
        {
            /* Воспроизвести звуковое событие */
            if (upd.sound_triggered)
            {
                audio_sequence_t seq;
                sound_map_resolve(payload.sound, floor, &seq);
                if (seq.valid)
                {
                    /* Не запускать музыку в нештатном режиме или при активном диспетчере.
         * Сам звук (up.wav / down.wav) воспроизводится как обычно — только
         * флаг needs_music подавляется, чтобы worker не поставил music_wanted=1. */
                    if (payload.mode != MODE_NORMAL || p_app->state.active_dispatch != DISPATCH_OFF)
                    {
                        seq.needs_music = 0;
                    }
                    audio_prio_t prio = sound_to_prio(payload.sound);
                    audio_player_play(p_app->audio, &seq, prio);
                }
            }

            /* Отменить музыку при нештатном режиме (mode != NORMAL) */
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

/* ─── signalfd ───────────────────────────────────────────────────────────── */

static int setup_signalfd(void)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGCHLD);

    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
    {
        syslog(LOG_ERR, "sigprocmask: %s", strerror(errno));
        return -1;
    }

    int fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (fd < 0)
    {
        syslog(LOG_ERR, "signalfd: %s", strerror(errno));
    }
    return fd;
}

/* ─── Обработка сигналов ─────────────────────────────────────────────────── */

static int handle_signal(const struct signalfd_siginfo *p_si, app_t *p_app)
{
    switch (p_si->ssi_signo)
    {
    case SIGTERM:
    case SIGINT:
        syslog(LOG_NOTICE, "received signal %u, shutting down", p_si->ssi_signo);
        return 1;

    case SIGCHLD:
        syslog(LOG_DEBUG, "SIGCHLD: child pid=%u status=%u", p_si->ssi_pid, p_si->ssi_status);
        video_player_check_and_restart(p_app->video);
        break;

    default:
        syslog(LOG_DEBUG, "unexpected signal %u", p_si->ssi_signo);
        break;
    }
    return 0;
}

/* ─── timerfd ────────────────────────────────────────────────────────────── */

static int setup_timerfd(void)
{
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0)
    {
        syslog(LOG_ERR, "timerfd_create: %s", strerror(errno));
        return -1;
    }

    const struct itimerspec ts = {
        .it_interval = { .tv_sec = WATCHDOG_INTERVAL_S, .tv_nsec = 0 },
        .it_value    = { .tv_sec = WATCHDOG_INTERVAL_S, .tv_nsec = 0 },
    };
    if (timerfd_settime(fd, 0, &ts, NULL) < 0)
    {
        syslog(LOG_ERR, "timerfd_settime: %s", strerror(errno));
        (void) close(fd);
        return -1;
    }
    return fd;
}

/* ─── Watchdog tick ──────────────────────────────────────────────────────── */

static void on_watchdog_tick(const app_t *p_app)
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

/* ─── Инициализация UART ─────────────────────────────────────────────────── */

static uart_t *open_uart(app_t *p_app, const uart_config_t *p_uart_cfg)
{
    /* baud_rate_t enum values == числа baudrate — прямой каст безопасен.
     * Неизвестные значения отфильтрованы в uart_config_load(). */
    baud_rate_t baud = (baud_rate_t) p_uart_cfg->baudrate;

    uart_t *p_u = uart_open(p_uart_cfg->port, baud, UART_PARITY_NONE, on_uart_frame, p_app);
    if (p_u == NULL)
    {
        syslog(LOG_ERR, "uart_open(%s, %d): %s", p_uart_cfg->port, p_uart_cfg->baudrate,
               strerror(errno));
        return NULL;
    }
    syslog(LOG_NOTICE, "UART open: %s @ %d baud, 8N1", p_uart_cfg->port, p_uart_cfg->baudrate);
    return p_u;
}

/* ─── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char *p_argv[])
{
    openlog("indicator", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_NOTICE, "indicator starting (phase-deploy)");

    const char *p_config_path = parse_config_path(argc, p_argv);

    /* ── Контекст и статистика ───────────────────────────────────────────── */

    app_t app;
    (void) memset(&app, 0, sizeof(app));
    (void) memset(&g_s_stats, 0, sizeof(g_s_stats));
    state_init(&app.state);

    /* ── nku_scheme.toml ─────────────────────────────────────────────────── */
    if (config_load(p_config_path, &app.cfg) < 0)
    {
        syslog(LOG_WARNING, "config: '%s' not found, using defaults", p_config_path);
    }
    else
    {
        syslog(LOG_INFO, "config: sound=%d%% music=%d%% load_idx=%d", app.cfg.sound_volume_percent,
               app.cfg.music_volume_percent, app.cfg.load_capacity_idx);
    }

    /* ── video.toml ──────────────────────────────────────────────────────── */
    char video_cfg_path[256];
    derive_sibling_path(p_config_path, VIDEO_CONFIG_FILENAME, video_cfg_path,
                        sizeof(video_cfg_path));
    if (video_config_load(video_cfg_path, &app.cfg) < 0)
    {
        syslog(LOG_INFO, "video config: '%s' not found, using defaults win=%d,%d,%dx%d",
               video_cfg_path, app.cfg.video_win_x, app.cfg.video_win_y, app.cfg.video_win_w,
               app.cfg.video_win_h);
    }
    else
    {
        syslog(LOG_INFO, "video config: win=%d,%d,%dx%d", app.cfg.video_win_x, app.cfg.video_win_y,
               app.cfg.video_win_w, app.cfg.video_win_h);
    }

    /* ── renderer.toml ───────────────────────────────────────────────────── */
    char renderer_cfg_path[256];
    derive_sibling_path(p_config_path, RENDERER_CONFIG_FILENAME, renderer_cfg_path,
                        sizeof(renderer_cfg_path));
    if (renderer_config_load(renderer_cfg_path, &app.cfg.rdr) < 0)
    {
        syslog(LOG_INFO, "renderer config: '%s' not found, using defaults", renderer_cfg_path);
    }
    else
    {
        syslog(LOG_INFO,
               "renderer config: resources=%s digit_l=(%d,%d) digit_r=(%d,%d) "
               "arrow=(%d,%d) weight=(%d,%d) notif=(%d,%d)",
               app.cfg.rdr.resources_dir, app.cfg.rdr.digit_left_x, app.cfg.rdr.digit_left_y,
               app.cfg.rdr.digit_right_x, app.cfg.rdr.digit_right_y, app.cfg.rdr.arrow_x,
               app.cfg.rdr.arrow_y, app.cfg.rdr.weight_x, app.cfg.rdr.weight_y, app.cfg.rdr.notif_x,
               app.cfg.rdr.notif_y);
    }

    /* ── pi_scheme.toml (UART port + baudrate) ───────────────────────────── */
    uart_config_t uart_cfg;
    char uart_cfg_path[256];
    derive_sibling_path(p_config_path, UART_CONFIG_FILENAME, uart_cfg_path, sizeof(uart_cfg_path));
    if (uart_config_load(uart_cfg_path, &uart_cfg) < 0)
    {
        syslog(LOG_WARNING, "uart config: '%s' not found, using defaults port=%s baud=%d",
               uart_cfg_path, uart_cfg.port, uart_cfg.baudrate);
    }
    else
    {
        syslog(LOG_INFO, "uart config: port=%s baud=%d", uart_cfg.port, uart_cfg.baudrate);
    }

    /* ── signalfd ─────────────────────────────────────────────────────────── */

    int sig_fd = setup_signalfd();
    if (sig_fd < 0)
    {
        goto fail_early;
    }

    /* ── timerfd ──────────────────────────────────────────────────────────── */

    int timer_fd = setup_timerfd();
    if (timer_fd < 0)
    {
        (void) close(sig_fd);
        goto fail_early;
    }

    /* ── FIFO (media-ingest → indicator IPC) ────────────────────────────── */

    app.fifo_fd = media_ipc_open();
    if (app.fifo_fd < 0)
    {
        /* Не фатально: media-ingest может стартовать позже */
        syslog(LOG_WARNING, "media_ipc_open failed — media status updates disabled");
    }

    /* ── Notification auto-hide timer ───────────────────────────────────── */

    app.notif_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (app.notif_fd < 0)
    {
        /* Не фатально: MCU-уведомление не будет автоскрываться */
        syslog(LOG_WARNING, "notif timerfd_create: %s — auto-hide disabled", strerror(errno));
    }

    /* ── Video player ────────────────────────────────────────────────────── */

    const video_window_t WIN = {
        .x      = app.cfg.video_win_x,
        .y      = app.cfg.video_win_y,
        .width  = app.cfg.video_win_w,
        .height = app.cfg.video_win_h,
    };
    app.video = video_player_open(VIDEO_PATH, WIN);
    if (app.video == NULL)
    {
        syslog(LOG_CRIT, "video_player_open failed, cannot start");
        (void) close(timer_fd);
        (void) close(sig_fd);
        goto fail_early;
    }

    /* ── Renderer ────────────────────────────────────────────────────────── */

    app.renderer = renderer_create(&app.cfg.rdr);
    if (app.renderer == NULL)
    {
        syslog(LOG_CRIT, "renderer_create failed, cannot start");
        video_player_close(app.video);
        (void) close(timer_fd);
        (void) close(sig_fd);
        goto fail_early;
    }

    /* Показать фон и грузоподъёмность немедленно (до первого UART фрейма) */
    {
        char path[512];

        /* BACKGROUND — всегда */
        (void) snprintf(path, sizeof(path), "%s/BACK.png", app.cfg.rdr.resources_dir);
        renderer_show_png(app.renderer, SPRITE_BACKGROUND, path);

        /* WEIGHT — только если сконфигурирован */
        if (app.cfg.load_capacity_idx > 0)
        {
            (void) snprintf(path, sizeof(path), "%s/weights/load_%d.png", app.cfg.rdr.resources_dir,
                            app.cfg.load_capacity_idx);
            renderer_show_png(app.renderer, SPRITE_WEIGHT, path);
        }
    }

    /* ── Setup status (результат синхронизации с MCU при старте) ─────────── */

    app.setup_status = read_setup_status(SETUP_STATUS_PATH);
    switch (app.setup_status)
    {
    case SETUP_STATUS_OK:
        syslog(LOG_INFO, "setup: status=ok");
        break;
    case SETUP_STATUS_PUSH_FAILED:
        syslog(LOG_WARNING, "setup: push_failed — MCU не получил команду стриминга");
        renderer_show_png(app.renderer, SPRITE_NOTIFICATION, NOTIF_DIR "/notif_no_mcu.png");
        break;
    case SETUP_STATUS_PULL_FAILED:
        syslog(LOG_WARNING, "setup: pull_failed — не удалось прочитать параметры MCU");
        renderer_show_png(app.renderer, SPRITE_NOTIFICATION, NOTIF_DIR "/notif_no_mcu.png");
        break;
    case SETUP_STATUS_PENDING:
        syslog(LOG_WARNING, "setup: pending — setup ещё не завершился");
        break;
    default:
        syslog(LOG_WARNING, "setup: status=unknown (файл не найден или не распознан)");
        break;
    }

    /* ── Audio (Фаза 5) ───────────────────────────────────────────────────── */
    app.audio =
        audio_player_open(SOUNDS_DIR, app.cfg.sound_volume_percent, app.cfg.music_volume_percent);
    if (app.audio == NULL)
    {
        /* Аудио не критично для работы: продолжаем без звука */
        syslog(LOG_ERR, "audio_player_open failed — audio disabled");
    }
    else
    {
        syslog(LOG_INFO, "audio: player ready, sound=%d%% music=%d%%", app.cfg.sound_volume_percent,
               app.cfg.music_volume_percent);
    }

    /* ── UART (последним: только после готовности всех потребителей) ──────── */

    app.uart = open_uart(&app, &uart_cfg);
    if (app.uart == NULL)
    {
        renderer_destroy(app.renderer);
        video_player_close(app.video);
        (void) close(timer_fd);
        (void) close(sig_fd);
        goto fail_early;
    }

    /* ── Poll loop ────────────────────────────────────────────────────────── */

    struct pollfd fds[FD_COUNT];
    (void) memset(fds, 0, sizeof(fds));
    fds[FD_UART].fd      = uart_get_fd(app.uart);
    fds[FD_UART].events  = POLLIN;
    fds[FD_SIG].fd       = sig_fd;
    fds[FD_SIG].events   = POLLIN;
    fds[FD_TIMER].fd     = timer_fd;
    fds[FD_TIMER].events = POLLIN;
    fds[FD_FIFO].fd      = app.fifo_fd; /* -1 → poll игнорирует запись */
    fds[FD_FIFO].events  = POLLIN;
    fds[FD_NOTIF].fd     = app.notif_fd; /* -1 → poll игнорирует запись */
    fds[FD_NOTIF].events = POLLIN;

    syslog(LOG_NOTICE, "event loop started, omxplayer_pid=%d", video_player_get_pid(app.video));

    int running = 1;
    while (running != 0)
    {
        int ret = poll(fds, (nfds_t) FD_COUNT, -1);
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            syslog(LOG_ERR, "poll: %s", strerror(errno));
            break;
        }

        if ((fds[FD_UART].revents & POLLIN) != 0)
        {
            if (uart_process_rx(app.uart) < 0)
                syslog(LOG_ERR, "uart_process_rx: %s", strerror(errno));
        }
        if ((fds[FD_UART].revents & (POLLERR | POLLHUP)) != 0)
        {
            syslog(LOG_ERR, "uart: device error revents=0x%x", (unsigned) fds[FD_UART].revents);
        }

        if ((fds[FD_SIG].revents & POLLIN) != 0)
        {
            struct signalfd_siginfo si;
            if (read(sig_fd, &si, sizeof(si)) == (ssize_t) sizeof(si))
            {
                running = !handle_signal(&si, &app);
            }
        }

        if ((fds[FD_FIFO].revents & POLLIN) != 0)
        {
            on_media_status(&app);
        }
        if ((fds[FD_FIFO].revents & POLLHUP) != 0)
        {
            /* media-ingest закрыл все write-fd (остановился / упал).
                * Переоткрыть FIFO: следующий write-fd откроется без блокировки. */
            media_ipc_close(app.fifo_fd);
            app.fifo_fd     = media_ipc_open();
            fds[FD_FIFO].fd = app.fifo_fd;
        }

        if ((fds[FD_NOTIF].revents & POLLIN) != 0)
        {
            uint64_t exp = 0U;
            (void) read(app.notif_fd, &exp, sizeof(exp)); /* drain */
            renderer_hide(app.renderer, SPRITE_NOTIFICATION);
            syslog(LOG_DEBUG, "notif: auto-hide timer fired");
        }

        /* FD_FIFO (Фаза 7) */
    }

    /* ── Cleanup ──────────────────────────────────────────────────────────── */

    syslog(LOG_NOTICE, "shutdown: frames_ok=%u parse_errors=%u", g_s_stats.frames_ok,
           g_s_stats.parse_errors);

    uart_close(app.uart);
    renderer_destroy(app.renderer);
    video_player_close(app.video);
    audio_player_close(app.audio);
    media_ipc_close(app.fifo_fd);
    (void) close(app.notif_fd);
    (void) close(timer_fd);
    (void) close(sig_fd);

    /* TODO Фаза 5: audio_player_close(app.audio); */

    syslog(LOG_NOTICE, "indicator stopped");
    closelog();
    return 0;

fail_early:
    syslog(LOG_CRIT, "startup failed, exiting");
    closelog();
    return 1;
}
