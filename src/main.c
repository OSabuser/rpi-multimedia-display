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

#include "config/config.h"
#include "domain/floor.h"
#include "domain/state.h"
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

static const char *const DEFAULT_CONFIG_PATH = "/home/pi/indicator/configs/device/nku_scheme.toml";

static const char *const UART_DEVICE = "/dev/ttyAMA0";

static const char *const VIDEO_CONFIG_FILENAME    = "video.toml";
static const char *const RENDERER_CONFIG_FILENAME = "renderer.toml";

static const char *const VIDEO_PATH = "/home/pi/indicator/videos/output.mp4";

static const int WATCHDOG_INTERVAL_S = 30;

/* ─── Индексы pollfd ─────────────────────────────────────────────────────── */

typedef enum fd_index_e
{
    FD_UART  = 0,
    FD_SIG   = 1,
    FD_TIMER = 2,
    /* FD_FIFO = 3  — Фаза 7 */
    FD_COUNT = 3,
} fd_index_t;

/* ─── Контекст приложения ────────────────────────────────────────────────── */

typedef struct app_s
{
    config_t cfg;
    indicator_state_t state;
    uart_t *uart;
    video_player_t *video;
    renderer_t *renderer;
    /* audio_player_t *audio; — Фаза 5 */
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

/* ─── Утилиты путей ──────────────────────────────────────────────────────── */

static void derive_sibling_path(const char *base_path, const char *filename, char *out,
                                size_t out_sz)
{
    const char *last_slash = strrchr(base_path, '/');
    if (last_slash == NULL)
    {
        strncpy(out, filename, out_sz - 1u);
        out[out_sz - 1u] = '\0';
        return;
    }
    size_t dir_len = (size_t) (last_slash - base_path) + 1u;
    if (dir_len >= out_sz)
        dir_len = out_sz - 1u;
    strncpy(out, base_path, dir_len);
    out[dir_len] = '\0';
    strncat(out, filename, out_sz - dir_len - 1u);
}

/* ─── Парсинг аргументов ─────────────────────────────────────────────────── */

static const char *parse_config_path(int argc, char *p_argv[])
{
    static const char PREFIX[]  = "--config=";
    static const int PREFIX_LEN = 9;

    for (int i = 1; i < argc; i++)
    {
        if (strncmp(p_argv[i], PREFIX, (size_t) PREFIX_LEN) == 0)
            return &p_argv[i][PREFIX_LEN];
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
            renderer_apply_elevator(p_app, &upd, &payload);

        /* Фаза 5: if (upd.sound_triggered)
         *             audio_play(p_app->audio, payload.sound, floor, &p_app->cfg); */
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

static uart_t *open_uart(app_t *p_app)
{
    uart_t *p_u = uart_open(UART_DEVICE, BAUD_115200, UART_PARITY_NONE, on_uart_frame, p_app);
    if (p_u == NULL)
    {
        syslog(LOG_ERR, "uart_open(%s, 115200): %s", UART_DEVICE, strerror(errno));
        return NULL;
    }
    syslog(LOG_NOTICE, "UART open: %s @ 115200 baud, 8N1", UART_DEVICE);
    return p_u;
}

/* ─── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char *p_argv[])
{
    openlog("indicator", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_NOTICE, "indicator starting (phase-4)");

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
               "arrow=(%d,%d) weight=(%d,%d)",
               app.cfg.rdr.resources_dir, app.cfg.rdr.digit_left_x, app.cfg.rdr.digit_left_y,
               app.cfg.rdr.digit_right_x, app.cfg.rdr.digit_right_y, app.cfg.rdr.arrow_x,
               app.cfg.rdr.arrow_y, app.cfg.rdr.weight_x, app.cfg.rdr.weight_y);
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

    /* ── Audio (Фаза 5) ───────────────────────────────────────────────────── */
    /* TODO: app.audio = audio_player_open(&app.cfg); */

    /* ── UART (последним: только после готовности всех потребителей) ──────── */

    app.uart = open_uart(&app);
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

    syslog(LOG_NOTICE, "event loop started, omxplayer_pid=%d", video_player_get_pid(app.video));

    int running = 1;
    while (running != 0)
    {
        int ret = poll(fds, (nfds_t) FD_COUNT, -1);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            syslog(LOG_ERR, "poll: %s", strerror(errno));
            break;
        }

        if ((fds[FD_UART].revents & POLLIN) != 0)
        {
            if (uart_process_rx(app.uart) < 0)
                syslog(LOG_ERR, "uart_process_rx: %s", strerror(errno));
        }
        if ((fds[FD_UART].revents & (POLLERR | POLLHUP)) != 0)
            syslog(LOG_ERR, "uart: device error revents=0x%x", (unsigned) fds[FD_UART].revents);

        if ((fds[FD_SIG].revents & POLLIN) != 0)
        {
            struct signalfd_siginfo si;
            if (read(sig_fd, &si, sizeof(si)) == (ssize_t) sizeof(si))
                running = !handle_signal(&si, &app);
        }

        if ((fds[FD_TIMER].revents & POLLIN) != 0)
        {
            uint64_t exp = 0U;
            if (read(timer_fd, &exp, sizeof(exp)) == (ssize_t) sizeof(exp))
                on_watchdog_tick(&app);
        }

        /* FD_FIFO (Фаза 7) */
    }

    /* ── Cleanup ──────────────────────────────────────────────────────────── */

    syslog(LOG_NOTICE, "shutdown: frames_ok=%u parse_errors=%u", g_s_stats.frames_ok,
           g_s_stats.parse_errors);

    uart_close(app.uart);
    renderer_destroy(app.renderer);
    video_player_close(app.video);
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