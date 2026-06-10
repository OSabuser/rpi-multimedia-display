/**
 * @file src_media_ingest/main.c
 * @brief Media Ingest Daemon — детектирование USB, монтирование, запуск ffmpeg.
 *
 * Конечный автомат:
 *   ST_IDLE → (USB insert) → ST_WAIT_PROCESSING → ST_FFMPEG_RUNNING
 *          → ST_WAIT_EJECT → ST_WAIT_UMOUNT → ST_IDLE
 *
 * Использование:
 *   media_ingest --config=/data/pi_nku_configs/media_ingest.toml
 *
 * _GNU_SOURCE требуется для:
 *   signalfd / SFD_NONBLOCK / SFD_CLOEXEC   <sys/signalfd.h>
 *   timerfd_create / TFD_NONBLOCK            <sys/timerfd.h>
 *   killpg                                   <signal.h>
 */
#define _GNU_SOURCE

#include "ffmpeg_runner.h"
#include "media/media_ipc.h"
#include "mounter.h"
#include "status_pipe.h"
#include "usb_watcher.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

/* ─── Константы ──────────────────────────────────────────────────────────── */

/** Задержка MEDIA_FOUND → начало ffmpeg (секунды) */
static const unsigned int DELAY_FOUND_TO_PROCESSING_S = 1U;

/** Задержка результат → MEDIA_EJECT (секунды) */
static const unsigned int DELAY_RESULT_TO_EJECT_S = 3U;

/** Задержка MEDIA_EJECT → umount + MEDIA_CLEAR (секунды) */
static const unsigned int DELAY_EJECT_TO_UMOUNT_S = 5U;

/** Максимальная длина строки конфига */
enum
{
    CFG_PATH_MAX = 256
};

/* ─── Состояния конечного автомата ───────────────────────────────────────── */

typedef enum ingest_state_e
{
    ST_IDLE,            /**< ожидание USB-носителя                            */
    ST_WAIT_PROCESSING, /**< носитель смонтирован, таймер 1 с перед ffmpeg   */
    ST_FFMPEG_RUNNING, /**< ffmpeg запущен, ждём SIGCHLD                    */
    ST_WAIT_EJECT, /**< результат отправлен, таймер 3 с → EJECT         */
    ST_WAIT_UMOUNT, /**< EJECT отправлен, таймер 5 с → umount + CLEAR    */
} ingest_state_t;

/* ─── Конфигурация ────────────────────────────────────────────────────────── */

typedef struct ingest_config_s
{
    char mount_point[CFG_PATH_MAX];  /**< точка монтирования USB              */
    char video_output[CFG_PATH_MAX]; /**< финальный путь output.mp4           */
    char tmp_output[CFG_PATH_MAX];   /**< временный файл ffmpeg (до rename)   */
    char status_fifo[CFG_PATH_MAX];  /**< путь к FIFO indicator               */
} ingest_config_t;

/* ─── Контекст демона ────────────────────────────────────────────────────── */

typedef struct ingest_ctx_s
{
    ingest_state_t state;
    ingest_config_t cfg;
    usb_watcher_t *p_watcher;
    pid_t ffmpeg_pid;
    int timer_fd;
    char mp4_paths[FFMPEG_FILES_MAX][FFMPEG_PATH_MAX];
    int mp4_count;
} ingest_ctx_t;

/* Единственный экземпляр: BSS, zero-initialized */
static ingest_ctx_t s_ctx;

/* ─── Конфиг: defaults и загрузка ───────────────────────────────────────── */

static void config_set_defaults(ingest_config_t *p_cfg)
{
    (void) snprintf(p_cfg->mount_point, CFG_PATH_MAX, "%s", "/mnt/usb");
    (void) snprintf(p_cfg->video_output, CFG_PATH_MAX, "%s", "/data/videos/output.mp4");
    (void) snprintf(p_cfg->tmp_output, CFG_PATH_MAX, "%s", "/data/videos/output_tmp.mp4");
    (void) snprintf(p_cfg->status_fifo, CFG_PATH_MAX, "%s", MEDIA_STATUS_FIFO_PATH);
}

static void config_load(const char *p_path, ingest_config_t *p_cfg)
{
    FILE *p_fp = fopen(p_path, "r");
    if (p_fp == NULL)
    {
        syslog(LOG_INFO, "ingest: config '%s' not found, using defaults", p_path);
        return;
    }

    char line[512];
    while (fgets(line, (int) sizeof(line), p_fp) != NULL)
    {
        if (line[0] == '#' || line[0] == '[' || line[0] == '\n' || line[0] == '\r')
        {
            continue;
        }

        char key[128];
        char val[CFG_PATH_MAX];
        if (sscanf(line, " %127[^= ] = %255[^\n\r]", key, val) != 2)
        {
            continue;
        }

        if (strcmp(key, "mount_point") == 0)
        {
            (void) snprintf(p_cfg->mount_point, CFG_PATH_MAX, "%s", val);
        }
        else if (strcmp(key, "video_output") == 0)
        {
            (void) snprintf(p_cfg->video_output, CFG_PATH_MAX, "%s", val);
        }
        else if (strcmp(key, "tmp_output") == 0)
        {
            (void) snprintf(p_cfg->tmp_output, CFG_PATH_MAX, "%s", val);
        }
        else if (strcmp(key, "status_fifo") == 0)
        {
            (void) snprintf(p_cfg->status_fifo, CFG_PATH_MAX, "%s", val);
        }
    }

    (void) fclose(p_fp);
    syslog(LOG_INFO, "ingest: config loaded from '%s'", p_path);
}

/* ─── Таймер state-machine ────────────────────────────────────────────────── */

static void timer_arm(int fd, unsigned int sec)
{
    const struct itimerspec ts = {
        .it_interval = { .tv_sec = 0, .tv_nsec = 0 },
        .it_value    = { .tv_sec = (time_t) sec, .tv_nsec = 0 },
    };
    if (timerfd_settime(fd, 0, &ts, NULL) < 0)
    {
        syslog(LOG_WARNING, "ingest: timer_arm %us: %s", sec, strerror(errno));
    }
}

static void timer_disarm(int fd)
{
    const struct itimerspec ts = {
        .it_interval = { .tv_sec = 0, .tv_nsec = 0 },
        .it_value    = { .tv_sec = 0, .tv_nsec = 0 },
    };
    if (timerfd_settime(fd, 0, &ts, NULL) < 0)
    {
        syslog(LOG_WARNING, "ingest: timer_disarm: %s", strerror(errno));
    }
}

/* ─── Setup helpers ──────────────────────────────────────────────────────── */

static int setup_signalfd(void)
{
    sigset_t mask;
    (void) sigemptyset(&mask);
    (void) sigaddset(&mask, SIGTERM);
    (void) sigaddset(&mask, SIGCHLD);

    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
    {
        syslog(LOG_ERR, "ingest: sigprocmask: %s", strerror(errno));
        return -1;
    }

    int fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (fd < 0)
    {
        syslog(LOG_ERR, "ingest: signalfd: %s", strerror(errno));
    }
    return fd;
}

static int setup_timerfd(void)
{
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0)
    {
        syslog(LOG_ERR, "ingest: timerfd_create: %s", strerror(errno));
    }
    return fd;
}

/* ─── Конечный автомат ───────────────────────────────────────────────────── */

/** Убрать последние цифры из имени устройства: "/dev/sda1" → "/dev/sda" */
static void make_disk_dev(const char *p_partition, char *p_disk, size_t sz)
{
    (void) snprintf(p_disk, sz, "%s", p_partition);
    size_t len = strlen(p_disk);
    while (len > 0U && p_disk[len - 1U] >= '0' && p_disk[len - 1U] <= '9')
    {
        p_disk[--len] = '\0';
    }
}

static void do_usb_remove(ingest_ctx_t *p_ctx)
{
    syslog(LOG_NOTICE, "ingest: USB remove, state=%d", (int) p_ctx->state);

    if (p_ctx->state == ST_FFMPEG_RUNNING)
    {
        ffmpeg_kill(p_ctx->ffmpeg_pid);
        p_ctx->ffmpeg_pid = -1;
    }

    timer_disarm(p_ctx->timer_fd);

    if (mounter_is_mounted(p_ctx->cfg.mount_point))
    {
        (void) mounter_umount(p_ctx->cfg.mount_point);
    }

    (void) status_pipe_write(p_ctx->cfg.status_fifo, MEDIA_CLEAR);
    p_ctx->state = ST_IDLE;
}

static void do_mount(ingest_ctx_t *p_ctx, const char *p_devpath)
{
    syslog(LOG_NOTICE, "ingest: USB insert %s", p_devpath);

    int mounted = (mounter_mount(p_devpath, p_ctx->cfg.mount_point) == 0);

    if (!mounted)
    {
        /* Fallback: попробовать сырой диск без номера раздела */
        char disk_dev[64];
        make_disk_dev(p_devpath, disk_dev, sizeof(disk_dev));
        syslog(LOG_INFO, "ingest: mount fallback %s", disk_dev);
        mounted = (mounter_mount(disk_dev, p_ctx->cfg.mount_point) == 0);
    }

    if (!mounted)
    {
        syslog(LOG_ERR, "ingest: cannot mount %s", p_devpath);
        (void) status_pipe_write(p_ctx->cfg.status_fifo, MEDIA_ERROR);
        timer_arm(p_ctx->timer_fd, DELAY_RESULT_TO_EJECT_S);
        p_ctx->state = ST_WAIT_EJECT;
        return;
    }

    ffmpeg_result_t scan =
        ffmpeg_find_files(p_ctx->cfg.mount_point, p_ctx->mp4_paths, &p_ctx->mp4_count);
    if (scan == FFMPEG_NO_FILES)
    {
        (void) status_pipe_write(p_ctx->cfg.status_fifo, MEDIA_NO_VIDEO);
        timer_arm(p_ctx->timer_fd, DELAY_RESULT_TO_EJECT_S);
        p_ctx->state = ST_WAIT_EJECT;
        return;
    }

    if (scan == FFMPEG_ERROR)
    {
        (void) status_pipe_write(p_ctx->cfg.status_fifo, MEDIA_ERROR);
        timer_arm(p_ctx->timer_fd, DELAY_RESULT_TO_EJECT_S);
        p_ctx->state = ST_WAIT_EJECT;
        return;
    }

    /* Файлы найдены */
    (void) status_pipe_write(p_ctx->cfg.status_fifo, MEDIA_FOUND);
    timer_arm(p_ctx->timer_fd, DELAY_FOUND_TO_PROCESSING_S);
    p_ctx->state = ST_WAIT_PROCESSING;
}

static void on_usb_event(const char *p_devpath, int inserted, void *p_ctx_raw)
{
    ingest_ctx_t *p_ctx = (ingest_ctx_t *) p_ctx_raw;

    if (inserted != 0)
    {
        if (p_ctx->state == ST_IDLE)
        {
            do_mount(p_ctx, p_devpath);
        }
        else
        {
            syslog(LOG_WARNING, "ingest: insert %s ignored (state=%d)", p_devpath,
                   (int) p_ctx->state);
        }
    }
    else
    {
        if (p_ctx->state != ST_IDLE)
        {
            do_usb_remove(p_ctx);
        }
    }
}

static void on_sigchld(ingest_ctx_t *p_ctx)
{
    if (p_ctx->state != ST_FFMPEG_RUNNING)
    {
        return;
    }

    ffmpeg_result_t result =
        ffmpeg_finish(p_ctx->ffmpeg_pid, p_ctx->cfg.tmp_output, p_ctx->cfg.video_output);
    p_ctx->ffmpeg_pid = -1;

    if (result == FFMPEG_OK)
    {
        (void) status_pipe_write(p_ctx->cfg.status_fifo, MEDIA_DONE);
    }
    else
    {
        (void) status_pipe_write(p_ctx->cfg.status_fifo, MEDIA_ERROR);
    }

    timer_arm(p_ctx->timer_fd, DELAY_RESULT_TO_EJECT_S);
    p_ctx->state = ST_WAIT_EJECT;
}

static void on_timer_tick(ingest_ctx_t *p_ctx)
{
    switch (p_ctx->state)
    {
    case ST_WAIT_PROCESSING:
        (void) status_pipe_write(p_ctx->cfg.status_fifo, MEDIA_PROCESSING);
        if (ffmpeg_spawn(p_ctx->mp4_paths, p_ctx->mp4_count, p_ctx->cfg.tmp_output,
                         &p_ctx->ffmpeg_pid) < 0)
        {
            syslog(LOG_ERR, "ingest: ffmpeg_spawn failed");
            (void) status_pipe_write(p_ctx->cfg.status_fifo, MEDIA_ERROR);
            timer_arm(p_ctx->timer_fd, DELAY_RESULT_TO_EJECT_S);
            p_ctx->state = ST_WAIT_EJECT;
        }
        else
        {
            p_ctx->state = ST_FFMPEG_RUNNING;
        }
        break;

    case ST_WAIT_EJECT:
        (void) status_pipe_write(p_ctx->cfg.status_fifo, MEDIA_EJECT);
        timer_arm(p_ctx->timer_fd, DELAY_EJECT_TO_UMOUNT_S);
        p_ctx->state = ST_WAIT_UMOUNT;
        break;

    case ST_WAIT_UMOUNT:
        (void) mounter_umount(p_ctx->cfg.mount_point);
        (void) status_pipe_write(p_ctx->cfg.status_fifo, MEDIA_CLEAR);
        p_ctx->state = ST_IDLE;
        break;

    default:
        syslog(LOG_WARNING, "ingest: timer in state=%d (unexpected)", (int) p_ctx->state);
        break;
    }
}

/* ─── Главный цикл ───────────────────────────────────────────────────────── */

static void run_loop(ingest_ctx_t *p_ctx, int sig_fd)
{
    enum
    {
        FD_INOTIFY = 0,
        FD_SIG     = 1,
        FD_TIMER   = 2,
        FD_N       = 3
    };

    struct pollfd fds[FD_N];
    (void) memset(fds, 0, sizeof(fds));
    fds[FD_INOTIFY].fd     = usb_watcher_get_fd(p_ctx->p_watcher);
    fds[FD_INOTIFY].events = POLLIN;
    fds[FD_SIG].fd         = sig_fd;
    fds[FD_SIG].events     = POLLIN;
    fds[FD_TIMER].fd       = p_ctx->timer_fd;
    fds[FD_TIMER].events   = POLLIN;

    int running = 1;
    while (running != 0)
    {
        int ret = poll(fds, (nfds_t) FD_N, -1);
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            syslog(LOG_ERR, "ingest: poll: %s", strerror(errno));
            break;
        }

        if ((fds[FD_INOTIFY].revents & POLLIN) != 0)
        {
            usb_watcher_process(p_ctx->p_watcher);
        }

        if ((fds[FD_SIG].revents & POLLIN) != 0)
        {
            struct signalfd_siginfo si;
            if (read(sig_fd, &si, sizeof(si)) == (ssize_t) sizeof(si))
            {
                if (si.ssi_signo == (uint32_t) SIGTERM)
                {
                    syslog(LOG_NOTICE, "ingest: SIGTERM — stopping");
                    running = 0;
                }
                else if (si.ssi_signo == (uint32_t) SIGCHLD)
                {
                    on_sigchld(p_ctx);
                }
            }
        }

        if ((fds[FD_TIMER].revents & POLLIN) != 0)
        {
            uint64_t exp = 0U;
            (void) read(p_ctx->timer_fd, &exp, sizeof(exp));
            on_timer_tick(p_ctx);
        }
    }
}

/* ─── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char *p_argv[])
{
    openlog("media_ingest", LOG_PID, LOG_DAEMON);
    syslog(LOG_NOTICE, "media_ingest starting");

    /* Разбор --config= аргумента */
    const char *p_config_path = NULL;
    for (int i = 1; i < argc; i++)
    {
        if (strncmp(p_argv[i], "--config=", 9U) == 0)
        {
            p_config_path = p_argv[i] + 9;
        }
    }

    config_set_defaults(&s_ctx.cfg);
    if (p_config_path != NULL)
    {
        config_load(p_config_path, &s_ctx.cfg);
    }

    int sig_fd = setup_signalfd();
    if (sig_fd < 0)
    {
        goto fail;
    }

    s_ctx.timer_fd = setup_timerfd();
    if (s_ctx.timer_fd < 0)
    {
        (void) close(sig_fd);
        goto fail;
    }

    s_ctx.p_watcher = usb_watcher_open(on_usb_event, &s_ctx);
    if (s_ctx.p_watcher == NULL)
    {
        (void) close(s_ctx.timer_fd);
        (void) close(sig_fd);
        goto fail;
    }

    s_ctx.state      = ST_IDLE;
    s_ctx.ffmpeg_pid = -1;

    syslog(LOG_NOTICE, "ingest: mount=%s output=%s fifo=%s", s_ctx.cfg.mount_point,
           s_ctx.cfg.video_output, s_ctx.cfg.status_fifo);

    run_loop(&s_ctx, sig_fd);

    /* Cleanup */
    if (s_ctx.state == ST_FFMPEG_RUNNING)
    {
        ffmpeg_kill(s_ctx.ffmpeg_pid);
    }
    if (mounter_is_mounted(s_ctx.cfg.mount_point))
    {
        (void) mounter_umount(s_ctx.cfg.mount_point);
    }
    usb_watcher_close(s_ctx.p_watcher);
    (void) close(s_ctx.timer_fd);
    (void) close(sig_fd);

    syslog(LOG_NOTICE, "media_ingest stopped");
    closelog();
    return 0;

fail:
    syslog(LOG_CRIT, "media_ingest startup failed");
    closelog();
    return 1;
}
