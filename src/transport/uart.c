#include "uart.h"

#include <stdlib.h>
uart_t *uart_open(const char *d, int b)
{
    (void) d;
    (void) b;
    return NULL;
}
void uart_close(uart_t *u)
{
    (void) u;
}
int uart_read_line(uart_t *u, char *buf, int n)
{
    (void) u;
    (void) buf;
    (void) n;
    return -1;
}
