/**
 * @file src_media_ingest/status_pipe.c
 * @brief Реализация writer-side записи статуса в FIFO indicator.
 */

#include "status_pipe.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

int status_pipe_write(const char *p_fifo_path, media_status_t status)
{
    int fd = open(p_fifo_path, O_WRONLY | O_NONBLOCK);
    if (fd < 0)
    {
        if (errno == ENXIO)
        {
            /* Нет reader-а: indicator не запущен или ещё не открыл FIFO.
             * Не фатально — статус потеряется, но operation продолжится. */
            syslog(LOG_WARNING, "status_pipe: no reader on '%s' — indicator not ready",
                   p_fifo_path);
        }
        else
        {
            syslog(LOG_ERR, "status_pipe: open '%s': %s", p_fifo_path, strerror(errno));
        }
        return -1;
    }

    const uint8_t BYTE    = (uint8_t) status;
    const ssize_t N_BYTES = write(fd, &BYTE, sizeof(BYTE));

    (void) close(fd);

    if (N_BYTES != (ssize_t) sizeof(BYTE))
    {
        syslog(LOG_ERR, "status_pipe: write '%s': %s", p_fifo_path,
               (N_BYTES < 0) ? strerror(errno) : "short write");
        return -1;
    }

    syslog(LOG_DEBUG, "status_pipe: sent status=%d to '%s'", (int) status, p_fifo_path);
    return 0;
}
