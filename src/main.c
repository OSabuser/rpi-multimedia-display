/**
 * @file main.c
 * @brief Lift Indicator — composition root и главный poll-цикл.
 *
 * _GNU_SOURCE требуется для:
 *   - signalfd(2) / SFD_NONBLOCK / SFD_CLOEXEC   (linux/signalfd.h)
 *   - timerfd_create(2) / TFD_NONBLOCK / TFD_CLOEXEC (sys/timerfd.h)
 *   - CLOCK_MONOTONIC                              (time.h)
 *   - sigemptyset / sigaddset / sigprocmask / SIG_BLOCK (signal.h, POSIX.1-2001
 *     — строго говоря POSIX, но clangd без _GNU_SOURCE их не видит)
 *
 * Определение должно стоять ДО любых #include.
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

#define UART_DEVICE         "/dev/ttyAMA0"
#define UART_BAUD           115200
#define UART_PARITY         UART_PARITY_NONE
#define CONFIG_PATH         "/home/pi/indicator/configs/device/nku_scheme.toml"
#define WATCHDOG_INTERVAL_S 30

/* ─── Индексы pollfd ─────────────────────────────────────────────────────── */

enum
{
    FD_UART = 0,
    FD_SIG,
    FD_TIMER,
    /* FD_FIFO — добавить в Фазе 7 */
    FD_COUNT
};

/* ─── Контекст приложения ────────────────────────────────────────────────── */

typedef struct
{
    config_t cfg;
    indicator_state_t state;
    uart_t *uart;
    /* video_player_t *video;    Фаза 3 */
    /* renderer_t     *renderer; Фаза 4 */
    /* audio_player_t *audio;    Фаза 5 */
} app_t;

/* ─── Статистика ─────────────────────────────────────────────────────────── */

typedef struct
{
    unsigned frames_ok;
    unsigned parse_errors;
    int last_floor_num;
    int last_arrow;
    int last_mode;
} stats_t;

static stats_t g_stats;

/* ─── UART коллбэк ───────────────────────────────────────────────────────── */

static void on_uart_frame(const mu_frame_t *frame, void *ctx)
{
    app_t *app = (app_t *) ctx;

    if (frame->opcode != MU_OPCODE_ELEVATOR_STATUS)
    {
        syslog(LOG_DEBUG, "uart: unknown opcode 0x%02X", frame->opcode);
        return;
    }

    parsed_frame_t payload;
    parse_result_t r = protocol_parse_payload(frame, &payload);
    if (r != PARSE_OK)
    {
        g_stats.parse_errors++;
        syslog(LOG_WARNING, "uart: protocol_parse_payload error %d", (int) r);
        return;
    }

    g_stats.frames_ok++;

    floor_t floor             = floor_decode(payload.left_char, payload.right_char);
    state_update_result_t upd = state_apply_frame(&app->state, &payload);

    syslog(LOG_INFO,
           "frame: floor=%d type=%d  arrow=%d  sound=%d  mode=%d  "
           "(changed: floor=%d arrow=%d mode=%d sound=%d first=%d)",
           floor.number, (int) floor.type, (int) payload.arrow, (int) payload.sound,
           (int) payload.mode, upd.floor_changed, upd.arrow_changed, upd.mode_changed,
           upd.sound_triggered, upd.first_frame);

    g_stats.last_floor_num = floor.number;
    g_stats.last_arrow     = (int) payload.arrow;
    g_stats.last_mode      = (int) payload.mode;

    /* Фаза 4: renderer_update(app->renderer, &upd, &payload, floor); */
    /* Фаза 5: if (upd.sound_triggered) audio_play(app->audio, payload.sound, floor, &app->cfg); */
}

/* ─── signalfd ───────────────────────────────────────────────────────────── */

static int setup_signalfd(void)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGCHLD);

    /* Блокируем сигналы от стандартной доставки — они придут через signalfd.
     * Без этого и signalfd, и обычный обработчик получили бы сигнал. */
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
    {
        syslog(LOG_ERR, "sigprocmask: %s", strerror(errno));
        return -1;
    }

    int fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (fd < 0)
    {
        syslog(LOG_ERR, "signalfd: %s", strerror(errno));
        return -1;
    }
    return fd;
}

/**
 * @return 1 если нужно завершить цикл, 0 иначе.
 */
static int handle_signal(const struct signalfd_siginfo *si)
{
    switch (si->ssi_signo)
    {
    case SIGTERM:
    case SIGINT:
        syslog(LOG_NOTICE, "received signal %u, shutting down", si->ssi_signo);
        return 1;
    case SIGCHLD:
        syslog(LOG_DEBUG, "SIGCHLD: child pid=%u status=%u", si->ssi_pid, si->ssi_status);
        /* Фаза 3: video_player_check_and_restart(app->video); */
        break;
    default:
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
        close(fd);
        return -1;
    }
    return fd;
}

static void on_watchdog_tick(void)
{
    syslog(LOG_INFO,
           "watchdog: frames_ok=%u parse_errors=%u  "
           "last: floor=%d arrow=%d mode=%d",
           g_stats.frames_ok, g_stats.parse_errors, g_stats.last_floor_num, g_stats.last_arrow,
           g_stats.last_mode);
}

/* ─── main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    openlog("indicator", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_NOTICE, "indicator starting");

    app_t app;
    memset(&app, 0, sizeof(app));
    memset(&g_stats, 0, sizeof(g_stats));

    /* ── 1. Конфиг ───────────────────────────────────────────────────────── */

    if (config_load(CONFIG_PATH, &app.cfg) < 0)
        syslog(LOG_WARNING, "config: using defaults (file not found)");

    syslog(LOG_INFO, "config: sound=%d%% music=%d%% load_idx=%d", app.cfg.sound_volume_percent,
           app.cfg.music_volume_percent, app.cfg.load_capacity_idx);

    state_init(&app.state);

    /* ── 2. signalfd ─────────────────────────────────────────────────────── */

    int sig_fd = setup_signalfd();
    if (sig_fd < 0)
    {
        goto fail_early;
    }

    /* ── 3. timerfd ──────────────────────────────────────────────────────── */

    int timer_fd = setup_timerfd();
    if (timer_fd < 0)
    {
        close(sig_fd);
        goto fail_early;
    }

    /* ── 4. Video player (Фаза 3) ────────────────────────────────────────── */
    /* TODO: app.video = video_player_open(...); */

    /* ── 5. Renderer (Фаза 4) ────────────────────────────────────────────── */
    /* TODO: app.renderer = renderer_init(&app.cfg); */

    /* ── 6. Audio (Фаза 5) ───────────────────────────────────────────────── */
    /* TODO: app.audio = audio_player_open(...); */

    /* ── 7. UART ─────────────────────────────────────────────────────────── */

    app.uart = uart_open(UART_DEVICE, UART_BAUD, UART_PARITY, on_uart_frame, &app);
    if (!app.uart)
    {
        syslog(LOG_ERR, "uart_open(%s): %s", UART_DEVICE, strerror(errno));
        close(timer_fd);
        close(sig_fd);
        goto fail_early;
    }
    syslog(LOG_NOTICE, "UART open: %s @ %d baud", UART_DEVICE, UART_BAUD);

    /* ── 8. Poll loop ────────────────────────────────────────────────────── */

    struct pollfd fds[FD_COUNT];
    memset(fds, 0, sizeof(fds));
    fds[FD_UART].fd      = uart_get_fd(app.uart);
    fds[FD_UART].events  = POLLIN;
    fds[FD_SIG].fd       = sig_fd;
    fds[FD_SIG].events   = POLLIN;
    fds[FD_TIMER].fd     = timer_fd;
    fds[FD_TIMER].events = POLLIN;

    syslog(LOG_NOTICE, "event loop started");

    int running = 1;
    while (running)
    {
        int ret = poll(fds, FD_COUNT, -1);
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            syslog(LOG_ERR, "poll: %s", strerror(errno));
            break;
        }

        if (fds[FD_UART].revents & POLLIN)
        {
            if (uart_process_rx(app.uart) < 0)
            {
                syslog(LOG_WARNING, "uart_process_rx: %s", strerror(errno));
            }
        }
        if (fds[FD_UART].revents & (POLLERR | POLLHUP))
        {
            syslog(LOG_ERR, "uart: device error revents=0x%x", fds[FD_UART].revents);
        }

        if (fds[FD_SIG].revents & POLLIN)
        {
            struct signalfd_siginfo si;
            if (read(sig_fd, &si, sizeof(si)) == (ssize_t) sizeof(si))
            {
                running = !handle_signal(&si);
            }
        }

        if (fds[FD_TIMER].revents & POLLIN)
        {
            uint64_t exp;
            if (read(timer_fd, &exp, sizeof(exp)) == (ssize_t) sizeof(exp))
            {
                on_watchdog_tick();
            }
        }

        /* Фаза 7: if (fds[FD_FIFO].revents & POLLIN) media_ipc_process(...); */
    }

    /* ── Cleanup ─────────────────────────────────────────────────────────── */

    syslog(LOG_NOTICE, "shutdown: frames_ok=%u parse_errors=%u", g_stats.frames_ok,
           g_stats.parse_errors);

    uart_close(app.uart);
    close(timer_fd);
    close(sig_fd);

    /* TODO Фаза 3: video_player_close(app.video);   */
    /* TODO Фаза 4: renderer_destroy(app.renderer);  */
    /* TODO Фаза 5: audio_player_close(app.audio);   */

    syslog(LOG_NOTICE, "indicator stopped");
    closelog();
    return 0;

fail_early:
    syslog(LOG_CRIT, "startup failed");
    closelog();
    return 1;
}