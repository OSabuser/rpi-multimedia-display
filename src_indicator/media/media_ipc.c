/**
 * @file src/media/media_ipc.c
 * @brief IPC indicator-side: FIFO создание и чтение статусов от media-ingest.
 *
 * Не требует _GNU_SOURCE: используется стандартный POSIX API.
 */

#include "media/media_ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

/* ─── Права доступа ──────────────────────────────────────────────────────── */

/** rwxr-xr-x — директория /run/indicator */
static const mode_t DIR_MODE = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

/** rw-rw-rw- — FIFO: читает indicator (pi), пишет media-ingest (pi) */
static const mode_t FIFO_MODE = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

/* ─── Реализация ─────────────────────────────────────────────────────────── */

int media_ipc_open(void)
{
    /* /run — tmpfs, пересоздаётся при каждом буте; создаём директорию заново */
    if (mkdir(MEDIA_STATUS_FIFO_DIR, DIR_MODE) < 0)
    {
        if (errno != EEXIST)
        {
            syslog(LOG_ERR, "media_ipc: mkdir '%s': %s", MEDIA_STATUS_FIFO_DIR, strerror(errno));
            return -1;
        }
    }

    /* FIFO переживает рестарты indicator: EEXIST — норма */
    if (mkfifo(MEDIA_STATUS_FIFO_PATH, FIFO_MODE) < 0)
    {
        if (errno != EEXIST)
        {
            syslog(LOG_ERR, "media_ipc: mkfifo '%s': %s", MEDIA_STATUS_FIFO_PATH, strerror(errno));
            return -1;
        }
    }

    /*
     * O_RDONLY | O_NONBLOCK: открывается немедленно без ожидания writer.
     * poll() вернёт POLLIN при поступлении байта,
     * POLLHUP при закрытии всех write-fd (media-ingest остановлен).
     */
    int fd = open(MEDIA_STATUS_FIFO_PATH, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
    {
        syslog(LOG_ERR, "media_ipc: open '%s': %s", MEDIA_STATUS_FIFO_PATH, strerror(errno));
        return -1;
    }

    syslog(LOG_INFO, "media_ipc: ready fd=%d path=%s", fd, MEDIA_STATUS_FIFO_PATH);
    return fd;
}

int media_ipc_read(int fd, media_status_t *p_out)
{
    uint8_t byte = 0U;
    ssize_t n    = read(fd, &byte, sizeof(byte));

    if (n < 0)
    {
        /* EAGAIN / EWOULDBLOCK — нормально при O_NONBLOCK без данных */
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            syslog(LOG_ERR, "media_ipc: read: %s", strerror(errno));
        }
        return -1;
    }

    if (n == 0)
    {
        /*
         * EOF: все write-fd FIFO закрыты (media-ingest остановлен или упал).
         * poll() продолжит возвращать POLLHUP; caller должен переоткрыть.
         */
        return -1;
    }

    if (byte >= (uint8_t) MEDIA_STATUS_MAX)
    {
        syslog(LOG_WARNING, "media_ipc: unknown status byte=0x%02x — ignoring", (unsigned) byte);
        return -1;
    }

    *p_out = (media_status_t) byte;
    syslog(LOG_DEBUG, "media_ipc: rx status=%d", (int) *p_out);
    return 0;
}

void media_ipc_close(int fd)
{
    if (fd < 0)
    {
        return;
    }
    (void) close(fd);
    syslog(LOG_DEBUG, "media_ipc: closed fd=%d", fd);
}
