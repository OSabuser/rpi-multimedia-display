/**
 * @file video_player.c
 * @brief omxplayer supervised child process adapter — implementation.
 *
 * Архитектура процессов omxplayer:
 *   /usr/bin/omxplayer — bash-скрипт, порождает:
 *     dbus-daemon      — сессионная шина
 *     omxplayer.bin    — реальный декодер (внук indicator)
 *
 * Завершение:
 *   SIGTERM к bash-скрипту игнорируется пока он в wait().
 *   SIGKILL через killpg() надёжно убивает всю group.
 *   waitpid() ждёт только прямого дочернего (bash-скрипт).
 *
 * Watchdog:
 *   При краше omxplayer.bin bash-скрипт завершается (его wait() вернул).
 *   indicator получает SIGCHLD на прямого дочернего (bash-скрипт).
 *   check_and_restart перезапускает.
 */

#define _GNU_SOURCE

#include "player/video_player.h"

#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

/* ─── Константы ──────────────────────────────────────────────────────────── */

#define VIDEO_PATH_MAX     256
#define WIN_ARG_MAX        32
#define RESTART_BACKOFF_MS 500

/* ─── Структура ──────────────────────────────────────────────────────────── */

struct video_player_s
{
    pid_t pid;
    char video_path[VIDEO_PATH_MAX];
    char win_arg[WIN_ARG_MAX];
};

extern char **environ;

/* ─── spawn_omxplayer ────────────────────────────────────────────────────── */

static int spawn_omxplayer(video_player_t *p_vp)
{
    char *argv[] = { (char *) "omxplayer", (char *) "--layer",
                     (char *) "1",         (char *) "--no-keys",
                     (char *) "--loop",    (char *) "--no-osd",
                     (char *) "--win",     p_vp->win_arg,
                     p_vp->video_path,     NULL };

    /*
     * Отдельная process group: PGID = PID дочернего (bash-скрипта).
     * Это позволяет killpg() убивать bash + dbus-daemon + omxplayer.bin
     * одним сигналом, не затрагивая indicator.
     */
    posix_spawnattr_t attr;
    (void) posix_spawnattr_init(&attr);
    (void) posix_spawnattr_setpgroup(&attr, 0);
    (void) posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);

    pid_t pid;
    int ret = posix_spawnp(&pid, "omxplayer", NULL, &attr, argv, environ);
    (void) posix_spawnattr_destroy(&attr);

    if (ret != 0)
    {
        syslog(LOG_ERR, "video_player: posix_spawnp omxplayer: %s", strerror(ret));
        p_vp->pid = -1;
        return -1;
    }

    p_vp->pid = pid;
    syslog(LOG_NOTICE, "omxplayer started pid=%d pgid=%d win=%s path=%s", (int) pid, (int) pid,
           p_vp->win_arg, p_vp->video_path);
    return 0;
}

/* ─── Публичный API ──────────────────────────────────────────────────────── */

video_player_t *video_player_open(const char *p_video_path, video_window_t window)
{
    video_player_t *p_vp = malloc(sizeof(*p_vp));
    if (p_vp == NULL)
    {
        syslog(LOG_ERR, "video_player_open: malloc: %s", strerror(errno));
        return NULL;
    }

    p_vp->pid = -1;

    (void) strncpy(p_vp->video_path, p_video_path, VIDEO_PATH_MAX - 1u);
    p_vp->video_path[VIDEO_PATH_MAX - 1u] = '\0';

    (void) snprintf(p_vp->win_arg, WIN_ARG_MAX, "%d,%d,%d,%d", window.x, window.y, window.width,
                    window.height);

    if (spawn_omxplayer(p_vp) < 0)
    {
        free(p_vp);
        return NULL;
    }

    return p_vp;
}

void video_player_close(video_player_t *p_vp)
{
    if (p_vp == NULL)
    {
        return;
    }

    if (p_vp->pid > 0)
    {
        syslog(LOG_NOTICE, "video_player: stopping omxplayer pgid=%d", (int) p_vp->pid);

        /*
         * SIGKILL вместо SIGTERM: bash-скрипт omxplayer игнорирует SIGTERM
         * пока находится в wait() ожидая omxplayer.bin. SIGKILL не может
         * быть перехвачен или проигнорирован ни одним процессом.
         *
         * killpg() доставляет сигнал всей group: bash + dbus-daemon + omxplayer.bin.
         */
        if (killpg(p_vp->pid, SIGKILL) < 0)
        {
            /* ESRCH: group уже не существует — нормально */
            if (errno != ESRCH)
            {
                syslog(LOG_WARNING, "video_player: killpg(SIGKILL) pgid=%d: %s", (int) p_vp->pid,
                       strerror(errno));
            }
        }

        /* waitpid только для прямого дочернего (bash-скрипт).
         * После SIGKILL завершается немедленно. */
        int status;
        (void) waitpid(p_vp->pid, &status, 0);

        syslog(LOG_NOTICE, "video_player: omxplayer stopped");
        p_vp->pid = -1;
    }

    free(p_vp);
}

void video_player_check_and_restart(video_player_t *p_vp)
{
    if (p_vp == NULL || p_vp->pid <= 0)
    {
        return;
    }

    int status;
    pid_t r = waitpid(p_vp->pid, &status, WNOHANG);

    if (r == 0)
    {
        return;
    }

    if (r < 0)
    {
        if (errno == ECHILD)
        {
            syslog(LOG_WARNING, "video_player: waitpid ECHILD for pid=%d", (int) p_vp->pid);
            p_vp->pid = -1;
        }
        else
        {
            syslog(LOG_ERR, "video_player: waitpid: %s", strerror(errno));
            return;
        }
    }
    else
    {
        if (WIFEXITED(status))
        {
            syslog(LOG_WARNING, "video_player: omxplayer exited code=%d pid=%d",
                   WEXITSTATUS(status), (int) r);
        }
        else if (WIFSIGNALED(status))
        {
            syslog(LOG_WARNING, "video_player: omxplayer killed signal=%d pid=%d", WTERMSIG(status),
                   (int) r);
        }
        else
        {
            syslog(LOG_WARNING, "video_player: omxplayer stopped unexpectedly pid=%d", (int) r);
        }
        p_vp->pid = -1;
    }

    const struct timespec backoff = {
        .tv_sec  = 0,
        .tv_nsec = RESTART_BACKOFF_MS * 1000000L,
    };
    (void) nanosleep(&backoff, NULL);

    (void) spawn_omxplayer(p_vp);
}

int video_player_get_pid(const video_player_t *p_vp)
{
    if (p_vp == NULL)
    {
        return -1;
    }
    return (int) p_vp->pid;
}
