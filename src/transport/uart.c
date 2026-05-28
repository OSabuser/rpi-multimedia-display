/**
 * @file uart.c
 * @brief UART transport — реализация.
 *
 * Намеренные решения по переносимости:
 *
 *  cfmakeraw()  — не используется (__USE_MISC / _GNU_SOURCE).
 *                 Заменена явной установкой флагов в termios_set_raw().
 *
 *  O_CLOEXEC   — не передаётся в open() (__O_CLOEXEC / _GNU_SOURCE).
 *                 Заменена fcntl(F_SETFD, FD_CLOEXEC) — чистый POSIX.1-2001.
 *
 *  _GNU_SOURCE  — не требуется в этом файле.
 */

#include "uart.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>

/* ─── Размер приёмного буфера ────────────────────────────────────────────── */

/** Должен быть > максимального фрейма: MU_FRAME_OVERHEAD(6) + MU_DATA_MAX(255) = 261 */
#define RX_BUF_SIZE 512U

/* ─── Дескриптор ─────────────────────────────────────────────────────────── */

struct uart_s
{
    int fd;
    frame_ready_cb_t cb;
    void *ctx;
    uint8_t rx_buf[RX_BUF_SIZE];
    size_t rx_len;
};

/* ─── Вспомогательные функции ────────────────────────────────────────────── */

static speed_t baud_to_speed(baud_rate_t baud)
{
    switch (baud)
    {
    case BAUD_9600:
        return B9600;
    case BAUD_19200:
        return B19200;
    case BAUD_38400:
        return B38400;
    case BAUD_57600:
        return B57600;
    case BAUD_115200:
        return B115200;
    default:
        return B0;
    }
}

/**
 * Raw-режим без cfmakeraw() — явная установка флагов termios.
 * Эквивалент cfmakeraw() из glibc (см. man 3 cfmakeraw).
 */
static void termios_set_raw(struct termios *p_t)
{
    /* Отключить все преобразования входного потока */
    p_t->c_iflag &= ~(unsigned) (IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    /* Отключить обработку выходного потока */
    p_t->c_oflag &= ~(unsigned) OPOST;
    /* Отключить echo, canonical mode, сигналы */
    p_t->c_lflag &= ~(unsigned) (ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    /* Сбросить CSIZE и PARENB — будут выставлены ниже */
    p_t->c_cflag &= ~(unsigned) (CSIZE | PARENB);
    p_t->c_cflag |= CS8;
}

static void termios_set_parity(struct termios *p_t, uart_parity_t parity)
{
    switch (parity)
    {
    case UART_PARITY_NONE:
        /* PARENB уже снят в termios_set_raw() */
        break;
    case UART_PARITY_ODD:
        p_t->c_cflag |= PARENB | PARODD;
        p_t->c_iflag |= INPCK;
        break;
    case UART_PARITY_EVEN:
        p_t->c_cflag |= PARENB;
        p_t->c_cflag &= ~(unsigned) PARODD;
        p_t->c_iflag |= INPCK;
        break;
    default:
        break;
    }
}

/** Убрать первые n байт из rx_buf. */
static void rx_consume(struct uart_s *p_u, size_t n)
{
    if (n == 0U)
    {
        return;
    }
    if (n >= p_u->rx_len)
    {
        p_u->rx_len = 0U;
        return;
    }
    p_u->rx_len -= n;
    (void) memmove(p_u->rx_buf, &p_u->rx_buf[n], p_u->rx_len);
}

/** Выделить и заполнить uart_t без открытия/настройки устройства. */
static uart_t *alloc_uart(int fd, frame_ready_cb_t p_cb, void *p_ctx)
{
    uart_t *p_u = malloc(sizeof(*p_u));
    if (p_u == NULL)
    {
        return NULL;
    }
    p_u->fd     = fd;
    p_u->cb     = p_cb;
    p_u->ctx    = p_ctx;
    p_u->rx_len = 0U;
    (void) memset(p_u->rx_buf, 0, sizeof(p_u->rx_buf));
    return p_u;
}

/* ─── Публичный API ──────────────────────────────────────────────────────── */

uart_t *uart_open(const char *p_device, baud_rate_t baud, uart_parity_t parity,
                  frame_ready_cb_t p_cb, void *p_ctx)
{
    if ((p_device == NULL) || (p_cb == NULL))
    {
        errno = EINVAL;
        return NULL;
    }

    speed_t speed = baud_to_speed(baud);
    if (speed == B0)
    {
        errno = EINVAL;
        return NULL;
    }

    int fd = open(p_device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        return NULL;
    }

    /* close-on-exec: fd не наследуется дочерними процессами (aplay, omxplayer) */
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
    {
        goto fail;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0)
    {
        goto fail;
    }

    termios_set_raw(&tty);
    termios_set_parity(&tty, parity);

    tty.c_cflag &= ~(unsigned) CSTOPB; /* 1 стоп-бит */
    tty.c_cflag |= CLOCAL | CREAD; /* не ждать несущей, разрешить чтение */

    if (cfsetispeed(&tty, speed) != 0)
    {
        goto fail;
    }
    if (cfsetospeed(&tty, speed) != 0)
    {
        goto fail;
    }

    tty.c_cc[VMIN]  = 0; /* read() возвращает немедленно */
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        goto fail;
    }

    tcflush(fd, TCIFLUSH);

    {
        uart_t *p_u = alloc_uart(fd, p_cb, p_ctx);
        if (p_u == NULL)
        {
            goto fail;
        }
        return p_u;
    }

fail:
{
    int saved = errno;
    (void) close(fd);
    errno = saved;
    return NULL;
}
}

void uart_close(uart_t *p_u)
{
    if (p_u == NULL)
    {
        return;
    }
    (void) close(p_u->fd);
    free(p_u);
}

int uart_get_fd(const uart_t *p_u)
{
    return (p_u != NULL) ? p_u->fd : -1;
}

int uart_process_rx(uart_t *p_u)
{
    /* ── Читать доступные байты ──────────────────────────────────────────── */
    while (p_u->rx_len < RX_BUF_SIZE)
    {
        ssize_t n = read(p_u->fd, &p_u->rx_buf[p_u->rx_len], RX_BUF_SIZE - p_u->rx_len);
        if (n > 0)
        {
            p_u->rx_len += (size_t) n;
        }
        else if (n == 0)
        {
            break;
        }
        else
        {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
            {
                break;
            }
            return -1;
        }
    }

    /* ── Извлечь полные фреймы ───────────────────────────────────────────── */
    for (;;)
    {
        if (p_u->rx_len == 0U)
        {
            break;
        }

        mu_frame_t frame;
        size_t consumed  = 0U;
        parse_result_t r = protocol_parse_frame(p_u->rx_buf, p_u->rx_len, &frame, &consumed);
        rx_consume(p_u, consumed);

        switch (r)
        {
        case PARSE_OK:
            p_u->cb(&frame, p_u->ctx);
            break;

        case PARSE_ERROR_SYNC1:
        case PARSE_NEED_MORE_DATA:
            goto done;

        case PARSE_ERROR_SYNC2:
        case PARSE_ERROR_CRC:
            if (consumed == 0U)
            {
                syslog(LOG_WARNING, "uart: parse error without progress, clearing");
                p_u->rx_len = 0U;
                goto done;
            }
            break;

        case PARSE_ERROR_PAYLOAD:
        case PARSE_ERROR_RANGE:
            syslog(LOG_DEBUG, "uart: payload error %d", (int) r);
            if (consumed == 0U)
            {
                p_u->rx_len = 0U;
                goto done;
            }
            break;

        default:
            break;
        }
    }

done:
    return 0;
}

/* ─── Test helper ────────────────────────────────────────────────────────── */

#ifdef INDICATOR_BUILD_TESTS
uart_t *uart_wrap_fd(int fd, frame_ready_cb_t p_cb, void *p_ctx)
{
    if (p_cb == NULL)
    {
        errno = EINVAL;
        return NULL;
    }
    return alloc_uart(fd, p_cb, p_ctx);
}
#endif