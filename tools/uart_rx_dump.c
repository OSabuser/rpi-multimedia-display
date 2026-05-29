/**
 * @file tools/uart_rx_dump.c
 * @brief Диагностический дамп UART — показывает ВСЁ.
 *
 * В отличие от uart_process_rx(), этот инструмент не молчит при ошибках.
 * Для каждого вызова protocol_parse_frame() печатает результат:
 *   PARSE_OK            — полный разобранный фрейм
 *   PARSE_ERROR_CRC     — ожидаемый и полученный CRC
 *   PARSE_ERROR_SYNC2   — где должен быть 0xBB и что там на самом деле
 *   PARSE_NEED_MORE_DATA — сколько байт накоплено, ждём ещё
 *   PARSE_ERROR_SYNC1   — весь буфер мусор, показываем hex
 *   PARSE_ERROR_PAYLOAD — фрейм валидный, но payload не разобрался
 *
 * Дополнительно: hex-дамп каждого сырого read() чтобы видеть
 * поток байт до любого разбора.
 *
 * Использование:
 *   ./uart_rx_dump [device] [baud]
 *   ./uart_rx_dump /dev/ttyAMA0 115200
 */

#define _GNU_SOURCE

#include "domain/floor.h"
#include "protocol/parser.h"
#include "protocol/types.h"
#include "transport/uart.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ─── Размер буфера (такой же как в uart.c) ──────────────────────────────── */

#define DUMP_BUF_SIZE 512U

/* ─── Флаг завершения ────────────────────────────────────────────────────── */

static volatile int s_running = 1;

static void sig_handler(int signo)
{
    (void) signo;
    s_running = 0;
}

/* ─── Цвета для терминала ────────────────────────────────────────────────── */

#define CLR_RESET  "\033[0m"
#define CLR_GREEN  "\033[0;32m"
#define CLR_RED    "\033[0;31m"
#define CLR_YELLOW "\033[1;33m"
#define CLR_CYAN   "\033[0;36m"
#define CLR_GRAY   "\033[0;90m"

/* ─── Вспомогательные функции вывода ─────────────────────────────────────── */

static void print_timestamp(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *p_tm = localtime(&ts.tv_sec);
    printf("%02d:%02d:%02d.%03ld", p_tm->tm_hour, p_tm->tm_min, p_tm->tm_sec,
           ts.tv_nsec / 1000000L);
}

static void print_hex_line(const uint8_t *p_data, size_t len, const char *p_color)
{
    printf("%s", p_color);
    for (size_t i = 0U; i < len; i++)
    {
        printf("%02X", (unsigned) p_data[i]);
        if (i < len - 1U)
        {
            printf(" ");
        }
    }
    printf("%s", CLR_RESET);
}

static void print_text_inline(const uint8_t *p_data, uint8_t len)
{
    for (uint8_t i = 0U; i < len; i++)
    {
        unsigned char c = (unsigned char) p_data[i];
        putchar((c >= 0x20U && c < 0x7FU) ? (int) c : '.');
    }
}

/* ─── Декодирование enum в строки ────────────────────────────────────────── */

static const char *arrow_str(arrow_t a)
{
    switch (a)
    {
    case ARROW_NONE:
        return "NONE";
    case ARROW_UP:
        return "UP";
    case ARROW_DOWN:
        return "DOWN";
    case ARROW_BOTH:
        return "BOTH";
    default:
        return "?";
    }
}

static const char *sound_str(sound_t s)
{
    switch (s)
    {
    case SOUND_NONE:
        return "NONE";
    case SOUND_DING:
        return "DING";
    case SOUND_UP:
        return "UP";
    case SOUND_DOWN:
        return "DOWN";
    case SOUND_CLOSING:
        return "CLOSING";
    case SOUND_OPENING:
        return "OPENING";
    case SOUND_OVERLOAD:
        return "OVERLOAD";
    case SOUND_FIRE_ALARM:
        return "FIRE_ALARM";
    case SOUND_DONT_WORK:
        return "DONT_WORK";
    case SOUND_BUTTON:
        return "BUTTON";
    default:
        return "?";
    }
}

static const char *mode_str(indicator_mode_t m)
{
    switch (m)
    {
    case MODE_NORMAL:
        return "NORMAL";
    case MODE_FIRE_ALARM:
        return "FIRE_ALARM";
    case MODE_MALFUNCTION:
        return "MALFUNCTION";
    case MODE_LOADING:
        return "LOADING";
    case MODE_OVERLOAD:
        return "OVERLOAD";
    case MODE_SEIS_ALARM:
        return "SEIS_ALARM";
    case MODE_FIREMANS:
        return "FIREMANS";
    case MODE_SERVICE:
        return "SERVICE";
    case MODE_EVACUATION:
        return "EVACUATION";
    case MODE_UPS_MALFUNCTION:
        return "UPS_MALFUNCTION";
    case MODE_DISPATCH_CALL:
        return "DISPATCH_CALL";
    case MODE_DISPATCH_ANSWER:
        return "DISPATCH_ANSWER";
    case MODE_CONN_LOST:
        return "CONN_LOST";
    default:
        return "?";
    }
}

static const char *dispatch_str(dispatch_state_t d)
{
    switch (d)
    {
    case DISPATCH_OFF:
        return "OFF";
    case DISPATCH_CALL:
        return "CALL";
    case DISPATCH_ANSWER:
        return "ANSWER";
    default:
        return "?";
    }
}
/* ─── Печать разобранного фрейма ─────────────────────────────────────────── */

static void print_ok_frame(const mu_frame_t *p_frame, unsigned int n)
{
    print_timestamp();
    printf("  " CLR_GREEN "#%-4u  PARSE_OK" CLR_RESET "  opcode=0x%02X  len=%u\n", n,
           (unsigned) p_frame->opcode, (unsigned) p_frame->data_len);

    printf("         hex:  ");
    print_hex_line(p_frame->data, p_frame->data_len, CLR_CYAN);
    printf("\n");

    printf("         text: " CLR_CYAN);
    print_text_inline(p_frame->data, p_frame->data_len);
    printf(CLR_RESET "\n");

    if (p_frame->opcode == MU_OPCODE_ELEVATOR_STATUS)
    {
        parsed_frame_t payload;
        parse_result_t r = protocol_parse_payload(p_frame, &payload);

        if (r == PARSE_OK)
        {
            floor_t floor = floor_decode(payload.left_char, payload.right_char);

            printf("         floor: " CLR_GREEN);
            switch (floor.type)
            {
            case FLOOR_TYPE_NORMAL:
                printf("%d", floor.number);
                break;
            case FLOOR_TYPE_BASEMENT:
                printf("П");
                break;
            case FLOOR_TYPE_BASEMENT_N:
                printf("П%d", floor.number);
                break;
            case FLOOR_TYPE_NEGATIVE:
                printf("-%d", floor.number);
                break;
            case FLOOR_TYPE_UNKNOWN:
                printf("UNKNOWN (L=%u R=%u)", (unsigned) payload.left_char,
                       (unsigned) payload.right_char);
                break;
            default:
                break;
            }
            printf(CLR_RESET "\n");

            printf("         arrow: %s\n", arrow_str(payload.arrow));
            printf("         sound: %s\n", sound_str(payload.sound));
            printf("         mode:  %s\n", mode_str(payload.mode));
        }
        else
        {
            printf("         " CLR_RED "payload parse error: %d" CLR_RESET "\n", (int) r);
        }
    }
    else if (p_frame->opcode == MU_OPCODE_DISPATCH)
    {
        dispatch_state_t ds;
        parse_result_t r = protocol_parse_dispatch(p_frame, &ds);

        if (r == PARSE_OK)
        {
            printf("         dispatch: " CLR_GREEN "%s" CLR_RESET "\n", dispatch_str(ds));
        }
        else
        {
            printf("         " CLR_RED "dispatch parse error: %d" CLR_RESET "\n", (int) r);
        }
    }
    printf("\n");
}

/* ─── Печать ошибок фрейма ───────────────────────────────────────────────── */

/**
 * Показать raw-байты буфера и вычислить CRC для диагностики.
 * p_frame_start — указатель на 0xAA в буфере.
 * frame_len     — полная длина фрейма включая overhead.
 */
static void print_crc_error(const uint8_t *p_frame_start, size_t frame_len)
{
    if (frame_len < MU_FRAME_OVERHEAD)
    {
        return;
    }

    uint8_t size      = p_frame_start[1];
    uint8_t opcode    = p_frame_start[2];
    uint8_t crc_lo_r  = p_frame_start[frame_len - 3U]; // первый байт — младший
    uint8_t crc_hi_r  = p_frame_start[frame_len - 2U]; // второй байт — старший
    uint16_t crc_recv = (uint16_t) ((uint16_t) crc_hi_r << 8U | crc_lo_r);

    /* Вычислить CRC от [opcode][data] */
    uint8_t crc_input[MU_DATA_MAX + 1U];
    crc_input[0] = opcode;
    (void) memcpy(&crc_input[1], &p_frame_start[3], size);
    uint16_t crc_calc = protocol_crc16(crc_input, (size_t) size + 1U);

    printf("         size=%-3u  opcode=0x%02X\n", (unsigned) size, (unsigned) opcode);
    printf("         CRC received:  0x%04X\n", (unsigned) crc_recv);
    printf("         CRC computed:  0x%04X  %s\n", (unsigned) crc_calc,
           (crc_calc == crc_recv) ? CLR_GREEN "MATCH" CLR_RESET : CLR_RED "MISMATCH" CLR_RESET);
    printf("         raw frame: ");
    print_hex_line(p_frame_start, frame_len, CLR_YELLOW);
    printf("\n");
}

/* ─── Главный цикл разбора ───────────────────────────────────────────────── */

static void run_dump_loop(int fd)
{
    uint8_t rx_buf[DUMP_BUF_SIZE];
    size_t rx_len        = 0U;
    unsigned frame_count = 0U;
    unsigned error_count = 0U;

    struct pollfd pfd;
    pfd.fd     = fd;
    pfd.events = POLLIN;

    while (s_running != 0)
    {

        /* ── poll с таймаутом чтобы проверять s_running ─────────────────── */
        int ret = poll(&pfd, 1U, 500);
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            fprintf(stderr, "poll: %s\n", strerror(errno));
            break;
        }

        if ((pfd.revents & POLLIN) == 0)
        {
            continue;
        }

        /* ── Читать доступные байты ──────────────────────────────────────── */
        while (rx_len < DUMP_BUF_SIZE)
        {
            ssize_t n = read(fd, &rx_buf[rx_len], DUMP_BUF_SIZE - rx_len);
            if (n > 0)
            {
                /* Показать сырые байты каждого read() */
                print_timestamp();
                printf("  " CLR_GRAY "raw +%zd bytes: ", n);
                print_hex_line(&rx_buf[rx_len], (size_t) n, CLR_GRAY);
                printf(CLR_RESET "\n");
                rx_len += (size_t) n;
            }
            else if (n == 0)
            {
                break;
            }
            else
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                fprintf(stderr, "read: %s\n", strerror(errno));
                s_running = 0;
                break;
            }
        }

        /* ── Разбирать все накопленные фреймы ────────────────────────────── */
        for (;;)
        {
            if (rx_len == 0U)
            {
                break;
            }

            /* Запомним указатель на начало буфера до consumed */
            const uint8_t *p_buf_before = rx_buf;

            mu_frame_t frame;
            size_t consumed  = 0U;
            parse_result_t r = protocol_parse_frame(rx_buf, rx_len, &frame, &consumed);

            switch (r)
            {

            case PARSE_OK:
                frame_count++;
                print_ok_frame(&frame, frame_count);
                break;

            case PARSE_NEED_MORE_DATA:
                print_timestamp();
                printf("  " CLR_YELLOW "NEED_MORE_DATA" CLR_RESET
                       "  buf=%zu bytes (waiting for complete frame)\n\n",
                       rx_len);
                goto consume_and_wait;

            case PARSE_ERROR_SYNC1:
                error_count++;
                print_timestamp();
                printf("  " CLR_RED "ERROR_SYNC1" CLR_RESET "  no 0xAA in %zu bytes, discarding: ",
                       consumed);
                print_hex_line(p_buf_before, consumed, CLR_RED);
                printf("\n\n");
                break;

            case PARSE_ERROR_SYNC2:
                error_count++;
                print_timestamp();
                printf("  " CLR_RED "ERROR_SYNC2" CLR_RESET
                       "  expected 0xBB at offset %zu, got 0x%02X\n",
                       consumed - 1U,
                       (consumed > 0U) ? (unsigned) p_buf_before[consumed - 1U] : 0U);
                printf("\n");
                break;

            case PARSE_ERROR_CRC:
                error_count++;
                print_timestamp();
                printf("  " CLR_RED "ERROR_CRC" CLR_RESET "\n");
                /* Показать детали: нужен полный фрейм до consumed */
                if (consumed >= MU_FRAME_OVERHEAD)
                {
                    print_crc_error(p_buf_before, consumed);
                }
                printf("\n");
                break;

            case PARSE_ERROR_PAYLOAD:
                error_count++;
                print_timestamp();
                printf("  " CLR_RED "ERROR_PAYLOAD" CLR_RESET "  opcode=0x%02X  data: ",
                       (unsigned) frame.opcode);
                print_text_inline(frame.data, frame.data_len);
                printf("\n\n");
                break;

            case PARSE_ERROR_RANGE:
                error_count++;
                print_timestamp();
                printf("  " CLR_RED "ERROR_RANGE" CLR_RESET "  value out of range in payload\n\n");
                break;

            default:
                break;
            }

            /* Сдвинуть буфер */
            if (consumed == 0U)
            {
                /* Защита от зависания */
                rx_len = 0U;
                break;
            }
            if (consumed >= rx_len)
            {
                rx_len = 0U;
            }
            else
            {
                rx_len -= consumed;
                (void) memmove(rx_buf, &rx_buf[consumed], rx_len);
            }

            if (r == PARSE_NEED_MORE_DATA)
            {
                break;
            }
        }

    consume_and_wait:
        fflush(stdout);
    }

    printf("\n─────────────────────────────────────────\n");
    printf("frames OK: %u   errors: %u\n", frame_count, error_count);
}

/* ─── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char *p_argv[])
{
    const char *p_device = "/dev/ttyAMA0";
    baud_rate_t baud     = BAUD_115200;

    if (argc >= 2)
    {
        p_device = p_argv[1];
    }
    if (argc >= 3)
    {
        int b = atoi(p_argv[2]);
        switch (b)
        {
        case 9600:
            baud = BAUD_9600;
            break;
        case 19200:
            baud = BAUD_19200;
            break;
        case 38400:
            baud = BAUD_38400;
            break;
        case 57600:
            baud = BAUD_57600;
            break;
        case 115200:
            baud = BAUD_115200;
            break;
        default:
            fprintf(stderr, "unsupported baud: %d\n", b);
            return 1;
        }
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    uart_t *p_uart = uart_open(p_device, baud, UART_PARITY_NONE, NULL, NULL);
    if (p_uart == NULL)
    {
        fprintf(stderr, "uart_open(%s): %s\n", p_device, strerror(errno));
        return 1;
    }

    printf("uart_rx_dump: %s @ %d baud\n", p_device, (int) baud);
    printf("Verbose mode: showing all bytes and parse events\n");
    printf("─────────────────────────────────────────\n\n");

    run_dump_loop(uart_get_fd(p_uart));

    uart_close(p_uart);
    return 0;
}