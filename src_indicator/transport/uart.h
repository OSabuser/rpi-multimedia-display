/**
 * @file uart.h
 * @brief UART transport — приём бинарных фреймов от STM32.
 *
 * Использование в главном цикле:
 *
 *   uart_t *p_uart = uart_open("/dev/ttyAMA0", BAUD_115200,
 *                              UART_PARITY_NONE, on_frame, p_ctx);
 *
 *   struct pollfd pfd = { uart_get_fd(p_uart), POLLIN, 0 };
 *   while (...) {
 *       poll(&pfd, 1, -1);
 *       if (pfd.revents & POLLIN)
 *           uart_process_rx(p_uart);
 *   }
 *
 * Формат фрейма:
 *   [0xAA][SIZE][OPCODE][DATA × SIZE][CRC_H][CRC_L][0xBB]
 *   CRC-16/CCITT-FALSE от [OPCODE || DATA].
 */

#pragma once

#include "protocol/parser.h" /* mu_frame_t */

/* ─── Скорость порта ─────────────────────────────────────────────────────── */

/**
 * Допустимые скорости для uart_open().
 * Именованные значения вместо магических чисел.
 */
typedef enum baud_rate_e
{
    BAUD_9600   = 9600,
    BAUD_19200  = 19200,
    BAUD_38400  = 38400,
    BAUD_57600  = 57600,
    BAUD_115200 = 115200,
} baud_rate_t;

/* ─── Чётность ───────────────────────────────────────────────────────────── */

/**
 * Текущая прошивка STM32: UART_PARITY_NONE.
 * Параметр добавлен заранее — смена чётности не потребует изменения сигнатуры.
 */
typedef enum uart_parity_e
{
    UART_PARITY_NONE = 0,
    UART_PARITY_ODD,
    UART_PARITY_EVEN,
} uart_parity_t;

/* ─── Коллбэк ────────────────────────────────────────────────────────────── */

/**
 * Вызывается из uart_process_rx() для каждого валидного (CRC-OK) фрейма.
 * Указатель p_frame действителен только внутри вызова — не сохранять.
 */
typedef void (*frame_ready_cb_t)(const mu_frame_t *p_frame, void *p_ctx);

/* ─── Непрозрачный дескриптор ────────────────────────────────────────────── */

typedef struct uart_s uart_t;

/* ─── Публичный API ──────────────────────────────────────────────────────── */

/**
 * Открыть UART, настроить termios (raw, non-blocking).
 *
 * @param p_device  Путь: "/dev/ttyAMA0".
 * @param baud      Скорость (enum baud_rate_t).
 * @param parity    Чётность.
 * @param p_cb      Коллбэк. Не NULL.
 * @param p_ctx     Пользовательский контекст.
 * @return Дескриптор или NULL (errno установлен).
 */
uart_t *uart_open(const char *p_device, baud_rate_t baud, uart_parity_t parity,
                  frame_ready_cb_t p_cb, void *p_ctx);

/** Закрыть и освободить. Безопасен при p_u == NULL. */
void uart_close(uart_t *p_u);

/** fd для poll(). Возвращает -1 при p_u == NULL. */
int uart_get_fd(const uart_t *p_u);

/**
 * Читать доступные байты и извлечь полные фреймы.
 * Вызывать при POLLIN. Для каждого CRC-валидного фрейма вызывает p_cb.
 *
 * @return 0 (включая EAGAIN), -1 при аппаратной ошибке read().
 */
int uart_process_rx(uart_t *p_u);

/* ─── Test helper ────────────────────────────────────────────────────────── */

#ifdef INDICATOR_BUILD_TESTS
/**
 * Обернуть существующий fd в uart_t без настройки termios.
 * Только для тестов: вместо реального UART передаётся read-конец pipe().
 */
uart_t *uart_wrap_fd(int fd, frame_ready_cb_t p_cb, void *p_ctx);
#endif
