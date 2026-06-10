/**
 * @file src_media_ingest/usb_watcher.c
 * @brief inotify-мониторинг /dev для детектирования USB block-устройств.
 *
 * _GNU_SOURCE требуется для IN_NONBLOCK / IN_CLOEXEC (inotify_init1).
 */
#define _GNU_SOURCE

#include "usb_watcher.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <syslog.h>
#include <unistd.h>

/* ─── Константы ──────────────────────────────────────────────────────────── */

/** Директория для мониторинга block-устройств */
static const char *const DEV_DIR = "/dev";

/** Буфер для чтения inotify-событий (≈170 событий по 24 байта) */
enum
{
    INOTIFY_BUF_SIZE = 4096
};

/** Максимальная длина пути устройства "/dev/sdXN\0" */
enum
{
    DEVPATH_MAX = 32
};

/* ─── Структура ───────────────────────────────────────────────────────────── */

struct usb_watcher_s
{
    int inotify_fd;    /**< fd из inotify_init1()         */
    int watch_wd;      /**< wd из inotify_add_watch()     */
    usb_event_cb_t cb; /**< коллбэк вызывается при событии */
    void *ctx;         /**< пользовательский контекст      */
};

/* ─── Вспомогательные функции ────────────────────────────────────────────── */

/**
 * is_storage_partition — проверить что имя соответствует sd[a-z][0-9].
 *
 * Примеры: sda1 ✓  sdb2 ✓  sda ✗  nvme0n1 ✗
 */
static int is_storage_partition(const char *p_name)
{
    return p_name[0] == 's' && p_name[1] == 'd' && p_name[2] >= 'a' && p_name[2] <= 'z' &&
           p_name[3] >= '0' && p_name[3] <= '9' && p_name[4] == '\0';
}

/* ─── Публичный API ───────────────────────────────────────────────────────── */

usb_watcher_t *usb_watcher_open(usb_event_cb_t p_cb, void *p_ctx)
{
    usb_watcher_t *p_w = malloc(sizeof(*p_w));
    if (p_w == NULL)
    {
        syslog(LOG_CRIT, "usb_watcher: malloc: %s", strerror(errno));
        return NULL;
    }

    p_w->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (p_w->inotify_fd < 0)
    {
        syslog(LOG_ERR, "usb_watcher: inotify_init1: %s", strerror(errno));
        free(p_w);
        return NULL;
    }

    p_w->watch_wd = inotify_add_watch(p_w->inotify_fd, DEV_DIR, IN_CREATE | IN_DELETE);
    if (p_w->watch_wd < 0)
    {
        syslog(LOG_ERR, "usb_watcher: inotify_add_watch '%s': %s", DEV_DIR, strerror(errno));
        (void) close(p_w->inotify_fd);
        free(p_w);
        return NULL;
    }

    p_w->cb  = p_cb;
    p_w->ctx = p_ctx;

    syslog(LOG_INFO, "usb_watcher: watching '%s' for sd[a-z][0-9] devices", DEV_DIR);
    return p_w;
}

int usb_watcher_get_fd(const usb_watcher_t *p_w)
{
    if (p_w == NULL)
    {
        return -1;
    }
    return p_w->inotify_fd;
}

void usb_watcher_process(usb_watcher_t *p_w)
{
    char buf[INOTIFY_BUF_SIZE];
    ssize_t n_bytes = read(p_w->inotify_fd, buf, sizeof(buf));

    if (n_bytes < 0)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            syslog(LOG_ERR, "usb_watcher: read: %s", strerror(errno));
        }
        return;
    }

    size_t remaining = (size_t) n_bytes;
    const char *ptr  = buf;

    while (remaining >= sizeof(struct inotify_event))
    {
        const struct inotify_event *p_ev = (const struct inotify_event *) ptr;
        size_t event_size                = sizeof(*p_ev) + p_ev->len;

        if (event_size > remaining)
        {
            break; /* повреждённое событие — прекратить разбор */
        }
        ptr += event_size;
        remaining -= event_size;

        if (p_ev->len == 0 || !is_storage_partition(p_ev->name))
        {
            continue;
        }

        char devpath[DEVPATH_MAX];
        (void) snprintf(devpath, sizeof(devpath), "%s/%s", DEV_DIR, p_ev->name);

        const int INSERTED = ((p_ev->mask & IN_CREATE) != 0) ? 1 : 0;
        syslog(LOG_NOTICE, "usb_watcher: %s %s", INSERTED ? "insert" : "remove", devpath);

        p_w->cb(devpath, INSERTED, p_w->ctx);
    }
}

void usb_watcher_close(usb_watcher_t *p_w)
{
    if (p_w == NULL)
    {
        return;
    }
    (void) close(p_w->inotify_fd);
    free(p_w);
}
