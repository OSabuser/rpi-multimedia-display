/**
 * @file src_indicator/main.c
 * @brief Lift Indicator — composition root и главный poll-цикл.
 *
 * Использование:
 *   indicator --config=/path/to/nku_scheme.toml
 *
 * Sibling-файлы рядом с --config:
 *   video.toml    → параметры окна omxplayer
 *   renderer.toml → позиции слотов DispmanX, путь к ресурсам
 *   pi_scheme.toml → UART порт и baudrate
 *
 * Декомпозиция:
 *   app_render.c   — renderer_apply_elevator/mode/dispatch, mode_to_rel_path
 *   app_handlers.c — on_uart_frame, on_media_status, on_watchdog_tick,
 *                    maybe_clear_mcu_notification, notif_arm, notif_disarm
 *
 * _GNU_SOURCE требуется для:
 *   signalfd / SFD_NONBLOCK / SFD_CLOEXEC    <sys/signalfd.h>
 *   timerfd_create / TFD_NONBLOCK / TFD_CLOEXEC  <sys/timerfd.h>
 *   CLOCK_MONOTONIC                           <time.h>
 *   sigemptyset / sigaddset / sigprocmask     <signal.h>
 */
#define _GNU_SOURCE

#include "app_private.h"
#include "config/config.h"
#include "player/video_player.h"
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

static const char *const S_DEFAULT_CONFIG_PATH = "/data/pi_nku_configs/nku_scheme.toml";

static const char *const S_VIDEO_CONFIG_FILENAME    = "video.toml";
static const char *const S_RENDERER_CONFIG_FILENAME = "renderer.toml";
static const char *const S_UART_CONFIG_FILENAME     = "pi_scheme.toml";
static const char *const S_SOUNDS_DIR               = "/data/sounds";
static const char *const S_VIDEO_PATH               = "/data/videos/output.mp4";

static const int WATCHDOG_INTERVAL_S = 30;

/* ─── Индексы pollfd ─────────────────────────────────────────────────────── */

typedef enum fd_index_e
{
    FD_UART  = 0,
    FD_SIG   = 1,
    FD_TIMER = 2,
    FD_FIFO  = 3,
    FD_NOTIF = 4,
    FD_COUNT = 5,
} fd_index_t;

/* ─── Глобальная статистика ──────────────────────────────────────────────── */

stats_t g_s_stats;

/* ─── Утилиты путей ──────────────────────────────────────────────────────── */

/**
 * derive_sibling_path — построить путь к файлу в той же директории, что p_base_path.
 *
 * @param p_base_path  базовый путь (например, /data/pi_nku_configs/nku_scheme.toml)
 * @param p_filename   имя файла-соседа (например, video.toml)
 * @param p_out        буфер результата
 * @param out_sz       размер буфера
 */
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

/* ─── Парсинг аргументов ─────────────────────────────────────────────────── */

/**
 * parse_config_path — извлечь путь к конфигу из аргументов командной строки.
 *
 * @return значение --config=PATH, или S_DEFAULT_CONFIG_PATH если не задан.
 */
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
    return S_DEFAULT_CONFIG_PATH;
}

/* ─── signalfd ───────────────────────────────────────────────────────────── */

/**
 * setup_signalfd — заблокировать SIGTERM/SIGINT/SIGCHLD и создать signalfd.
 *
 * @return fd при успехе, -1 при ошибке.
 */
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

/* ─── timerfd ────────────────────────────────────────────────────────────── */

/**
 * setup_timerfd — создать периодический timerfd с интервалом WATCHDOG_INTERVAL_S.
 *
 * @return fd при успехе, -1 при ошибке.
 */
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

/* ─── Обработка сигналов ─────────────────────────────────────────────────── */

/**
 * handle_signal — обработать сигнал из signalfd.
 *
 * @return 1 если нужно завершить poll loop, 0 иначе.
 */
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

/* ─── Инициализация UART ─────────────────────────────────────────────────── */

/**
 * open_uart — открыть UART с параметрами из конфига.
 *
 * @return указатель на uart_t при успехе, NULL при ошибке.
 */
static uart_t *open_uart(app_t *p_app, const uart_config_t *p_uart_cfg)
{
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

/* ─── Setup status ───────────────────────────────────────────────────────── */

/**
 * read_setup_status — прочитать результат синхронизации с MCU из файла.
 *
 * @return статус, или SETUP_STATUS_UNKNOWN если файл отсутствует / нераспознан.
 */
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

    buf[strcspn(buf, "\r\n")] = '\0';

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

/* ─── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char *p_argv[])
{
    if (argc > 1 && strcmp(p_argv[1], "--version") == 0)
    {
        (void) printf("v" INDICATOR_VERSION " (" INDICATOR_GIT_SHA ")\n");
        return 0;
    }
    openlog("indicator", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_NOTICE, "indicator v" INDICATOR_VERSION " (" INDICATOR_GIT_SHA ") starting");

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
    derive_sibling_path(p_config_path, S_VIDEO_CONFIG_FILENAME, video_cfg_path,
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
    derive_sibling_path(p_config_path, S_RENDERER_CONFIG_FILENAME, renderer_cfg_path,
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
    derive_sibling_path(p_config_path, S_UART_CONFIG_FILENAME, uart_cfg_path,
                        sizeof(uart_cfg_path));
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
        syslog(LOG_WARNING, "media_ipc_open failed — media status updates disabled");
    }

    /* ── Notification auto-hide timer ───────────────────────────────────── */

    app.notif_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (app.notif_fd < 0)
    {
        syslog(LOG_WARNING, "notif timerfd_create: %s — auto-hide disabled", strerror(errno));
    }

    /* ── Video player ────────────────────────────────────────────────────── */

    const video_window_t WIN = {
        .x      = app.cfg.video_win_x,
        .y      = app.cfg.video_win_y,
        .width  = app.cfg.video_win_w,
        .height = app.cfg.video_win_h,
    };
    app.video = video_player_open(S_VIDEO_PATH, WIN);
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

        (void) snprintf(path, sizeof(path), "%s/BACK.png", app.cfg.rdr.resources_dir);
        renderer_show_png(app.renderer, SPRITE_BACKGROUND, path);

        if (app.cfg.load_capacity_idx > 0)
        {
            (void) snprintf(path, sizeof(path), "%s/weights/load_%d.png", app.cfg.rdr.resources_dir,
                            app.cfg.load_capacity_idx);
            renderer_show_png(app.renderer, SPRITE_WEIGHT, path);
        }
    }

    /* ── Setup status ────────────────────────────────────────────────────── */

    static const char *const S_SETUP_STATUS_PATH = "/data/setup_status";
    app.setup_status                             = read_setup_status(S_SETUP_STATUS_PATH);
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

    /* ── Audio ───────────────────────────────────────────────────────────── */

    app.audio =
        audio_player_open(S_SOUNDS_DIR, app.cfg.sound_volume_percent, app.cfg.music_volume_percent);
    if (app.audio == NULL)
    {
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
    fds[FD_FIFO].fd      = app.fifo_fd;
    fds[FD_FIFO].events  = POLLIN;
    fds[FD_NOTIF].fd     = app.notif_fd;
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
            {
                syslog(LOG_ERR, "uart_process_rx: %s", strerror(errno));
            }
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
            media_ipc_close(app.fifo_fd);
            app.fifo_fd     = media_ipc_open();
            fds[FD_FIFO].fd = app.fifo_fd;
        }

        if ((fds[FD_NOTIF].revents & POLLIN) != 0)
        {
            uint64_t exp = 0U;
            (void) read(app.notif_fd, &exp, sizeof(exp));
            renderer_hide(app.renderer, SPRITE_NOTIFICATION);
            syslog(LOG_DEBUG, "notif: auto-hide timer fired");
        }

        if ((fds[FD_TIMER].revents & POLLIN) != 0)
        {
            uint64_t exp = 0U;
            (void) read(timer_fd, &exp, sizeof(exp));
            on_watchdog_tick(&app);
        }
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

    syslog(LOG_NOTICE, "indicator stopped");
    closelog();
    return 0;

fail_early:
    syslog(LOG_CRIT, "startup failed, exiting");
    closelog();
    return 1;
}