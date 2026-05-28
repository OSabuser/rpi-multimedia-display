#pragma once
typedef struct uart_s uart_t;
uart_t *uart_open(const char *device, int baud);
void uart_close(uart_t *u);
int uart_read_line(uart_t *u, char *buf, int maxlen);
