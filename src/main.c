/**
 * @file main.c
 * @brief Lift Indicator — composition root и главный poll-цикл.
 *
 * Использование:
 *   indicator --config=/path/to/nku_scheme.toml
 *
 * _GNU_SOURCE требуется для:
 *   signalfd / SFD_NONBLOCK / SFD_CLOEXEC    <sys/signalfd.h>
 *   timerfd_create / TFD_NONBLOCK / TFD_CLOEXEC  <sys/timerfd.h>
 *   CLOCK_MONOTONIC                           <time.h>
 *   sigemptyset / sigaddset / sigprocmask     <signal.h>
 *
 * Должно стоять ДО любых #include.
 */
#define _GNU_SOURCE

#include "config/config.h"
#include "domain/floor.h"
#include "domain/state.h"
#include "protocol/parser.h"
#include "protocol/types.h"
#include "transport/uart.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

/* ─── Константы ─────────────────────────────────────────────────────────── */

static const char *const DEFAULT_CONFIG_PATH = "/home/pi/indicator/configs/device/nku_scheme.toml";

static const char *const UART_DEVICE = "/dev/ttyAMA0";

/** Период watchdog-лога в секундах. */
static const int WATCHDOG_INTERVAL_S = 30;

/* ─── Индексы pollfd ─────────────────────────────────────────────────────── */

typedef enum fd_index_e
{
    FD_UART  = 0,
    FD_SIG   = 1,
    FD_TIMER = 2,
    /* FD_FIFO = 3  — добавить в Фазе 7 */
    FD_COUNT = 3,
} fd_index_t;

/* ─── Контекст приложения ────────────────────────────────────────────────── */

typedef struct app_s
{
    config_t cfg;
    indicator_state_t state;
    uart_t *uart;
    /* video_player_t *video;    Фаза 3 */
    /* renderer_t     *renderer; Фаза 4 */
    /* audio_player_t *audio;    Фаза 5 */
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

/** Единственная глобальная переменная: статистика для watchdog. */
static stats_t g_s_stats;

/* ─── Парсинг аргументов командной строки ────────────────────────────────── */

/**
 * Ищет аргумент вида --config=/some/path.
 * Возвращает путь или DEFAULT_CONFIG_PATH если аргумент не передан.
 */
static const char *parse_config_path(int argc, char *p_argv[])
{
    static const char PREFIX[]  = "--config=";
    static const int PREFIX_LEN = 9; /* strlen("--config=") */

    for (int i = 1; i < argc; i++)
    {
        if (strncmp(p_argv[i], PREFIX, (size_t) PREFIX_LEN) == 0)
        {
            return &p_argv[i][PREFIX_LEN];
        }
    }
    return DEFAULT_CONFIG_PATH;
}

/* ─── UART коллбэк ───────────────────────────────────────────────────────── */

/* ─────────────────────────────────────────────────────────────────────────────
 * on_uart_frame — заменить в main.c
 *
 * Добавлена ветка MU_OPCODE_DISPATCH (0xAA).
 * Диспетчер имеет наивысший приоритет над mode из 0xDA.
 * ──────────────────────────────────────────────────────────────────────────── */
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

            /* Фаза 4: renderer_update_dispatch(p_app->renderer, dispatch); */
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
               "changed: floor=%d arrow=%d mode=%d sound=%d first=%d "
               "dispatch_active=%d",
               g_s_stats.frames_ok, floor.number, (int) floor.type, (int) payload.arrow,
               (int) payload.sound, (int) payload.mode, upd.floor_changed, upd.arrow_changed,
               upd.mode_changed, upd.sound_triggered, upd.first_frame,
               (p_app->state.active_dispatch != DISPATCH_OFF) ? 1 : 0);

        g_s_stats.last_floor_num = floor.number;
        g_s_stats.last_arrow     = (int) payload.arrow;
        g_s_stats.last_mode      = (int) payload.mode;

        /*
         * Фаза 4: renderer_update(p_app->renderer, &upd, &payload, floor,
         *                         p_app->state.active_dispatch);
         * Фаза 5: if (upd.sound_triggered)
         *             audio_play(p_app->audio, payload.sound, floor, &p_app->cfg);
         */
        (void) p_app;
        return;
    }

    /* ── Неизвестный opcode ───────────────────────────────────────────────── */
    g_s_stats.unknown_opcodes++;
    syslog(LOG_DEBUG, "uart: unknown opcode 0x%02X len=%u", (unsigned) p_frame->opcode,
           (unsigned) p_frame->data_len);
}

/* ─── Инициализация signalfd ─────────────────────────────────────────────── */

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

/* ─── Обработка сигнала ──────────────────────────────────────────────────── */

/**
 * @return 1 если нужно завершить цикл, 0 иначе.
 */
static int handle_signal(const struct signalfd_siginfo *p_si)
{
    switch (p_si->ssi_signo)
    {
    case SIGTERM:
    case SIGINT:
        syslog(LOG_NOTICE, "received signal %u, shutting down", p_si->ssi_signo);
        return 1;

    case SIGCHLD:
        syslog(LOG_DEBUG, "SIGCHLD: child pid=%u status=%u", p_si->ssi_pid, p_si->ssi_status);
        /* Фаза 3: video_player_check_and_restart(p_app->video); */
        break;

    default:
        syslog(LOG_DEBUG, "unexpected signal %u", p_si->ssi_signo);
        break;
    }
    return 0;
}

/* ─── Инициализация timerfd ──────────────────────────────────────────────── */

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

static void on_watchdog_tick(void)
{
    syslog(LOG_NOTICE,
           "watchdog: frames_ok=%u parse_errors=%u unknown_opcodes=%u | "
           "last: floor=%d arrow=%d mode=%d",
           g_s_stats.frames_ok, g_s_stats.parse_errors, g_s_stats.unknown_opcodes,
           g_s_stats.last_floor_num, g_s_stats.last_arrow, g_s_stats.last_mode);
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
    syslog(LOG_NOTICE, "indicator starting (phase-2)");

    const char *p_config_path = parse_config_path(argc, p_argv);

    /* ── Контекст и статистика ───────────────────────────────────────────── */

    app_t app;
    (void) memset(&app, 0, sizeof(app));
    (void) memset(&g_s_stats, 0, sizeof(g_s_stats));
    state_init(&app.state);

    /* ── Конфиг ──────────────────────────────────────────────────────────── */

    if (config_load(p_config_path, &app.cfg) < 0)
    {
        syslog(LOG_WARNING, "config: file not found '%s', using defaults", p_config_path);
    }
    else
    {
        syslog(LOG_INFO, "config: sound=%d%% music=%d%% load_idx=%d | path=%s",
               app.cfg.sound_volume_percent, app.cfg.music_volume_percent,
               app.cfg.load_capacity_idx, p_config_path);
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

    /* ── Video player (Фаза 3) ────────────────────────────────────────────── */
    /* TODO: app.video = video_player_open(&app.cfg); */

    /* ── Renderer (Фаза 4) ────────────────────────────────────────────────── */
    /* TODO: app.renderer = renderer_init(&app.cfg); */

    /* ── Audio (Фаза 5) ───────────────────────────────────────────────────── */
    /* TODO: app.audio = audio_player_open(&app.cfg); */

    /* ── UART (последним: только после готовности всех потребителей) ──────── */

    app.uart = open_uart(&app);
    if (app.uart == NULL)
    {
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

    syslog(LOG_NOTICE, "event loop started");

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

        /* ── UART ─────────────────────────────────────────────────────────── */
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

        /* ── Сигналы ──────────────────────────────────────────────────────── */
        if ((fds[FD_SIG].revents & POLLIN) != 0)
        {
            struct signalfd_siginfo si;
            if (read(sig_fd, &si, sizeof(si)) == (ssize_t) sizeof(si))
            {
                running = !handle_signal(&si);
            }
        }

        /* ── Watchdog tick ────────────────────────────────────────────────── */
        if ((fds[FD_TIMER].revents & POLLIN) != 0)
        {
            uint64_t exp = 0U;
            if (read(timer_fd, &exp, sizeof(exp)) == (ssize_t) sizeof(exp))
            {
                on_watchdog_tick();
            }
        }

        /* ── Media FIFO (Фаза 7) ──────────────────────────────────────────── */
        /* if ((fds[FD_FIFO].revents & POLLIN) != 0)
         *     media_ipc_process(p_app); */
    }

    /* ── Cleanup ──────────────────────────────────────────────────────────── */

    syslog(LOG_NOTICE, "shutdown: frames_ok=%u parse_errors=%u", g_s_stats.frames_ok,
           g_s_stats.parse_errors);

    uart_close(app.uart);
    (void) close(timer_fd);
    (void) close(sig_fd);

    /* TODO Фаза 3: video_player_close(app.video);   */
    /* TODO Фаза 4: renderer_destroy(app.renderer);  */
    /* TODO Фаза 5: audio_player_close(app.audio);   */

    syslog(LOG_NOTICE, "indicator stopped");
    closelog();
    return 0;

fail_early:
    syslog(LOG_CRIT, "startup failed, exiting");
    closelog();
    return 1;
}
