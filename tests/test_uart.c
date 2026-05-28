/**
 * @file test_uart.c
 * @brief Unit-тесты для uart_process_rx() через pipe().
 *
 * Зачем pipe(), а не реальный UART:
 *   uart_process_rx() вызывает только read() — ему не важно, откуда fd.
 *   pipe() даёт детерминированный контроль над байтами без железа.
 *
 * Что тестируется:
 *   1. Валидный фрейм → коллбэк вызван с правильными данными.
 *   2. Мусор перед фреймом → игнорируется, коллбэк всё равно вызван.
 *   3. Плохой CRC → коллбэк не вызван.
 *   4. Разбитый фрейм (два write) → коллбэк вызван после второго write.
 *   5. Два фрейма подряд → два вызова коллбэка.
 */

#define _GNU_SOURCE /* pipe2, O_NONBLOCK */

#include "protocol/parser.h"
#include "transport/uart.h"
#include "unity.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/* ─── Захват вызовов коллбэка ────────────────────────────────────────────── */

#define MAX_CAPTURED_FRAMES 4

typedef struct capture_s
{
    int call_count;
    mu_frame_t frames[MAX_CAPTURED_FRAMES];
} capture_t;

static void capture_cb(const mu_frame_t *p_frame, void *p_ctx)
{
    capture_t *p_cap = (capture_t *) p_ctx;
    if (p_cap->call_count < MAX_CAPTURED_FRAMES)
    {
        p_cap->frames[p_cap->call_count] = *p_frame;
    }
    p_cap->call_count++;
}

/* ─── Построитель тестовых фреймов ───────────────────────────────────────── */

/**
 * Сконструировать бинарный фрейм в p_buf.
 * CRC считается от [opcode || data] — как в protocol_crc16().
 *
 * @return Число записанных байт.
 */
static size_t build_frame(uint8_t *p_buf, uint8_t opcode, const uint8_t *p_data, uint8_t data_len)
{
    /* CRC от [opcode][data...] */
    uint8_t crc_src[MU_DATA_MAX + 1U];
    crc_src[0] = opcode;
    (void) memcpy(&crc_src[1], p_data, data_len);
    uint16_t crc = protocol_crc16(crc_src, (size_t) data_len + 1U);

    size_t idx   = 0U;
    p_buf[idx++] = MU_SYNC1;
    p_buf[idx++] = data_len;
    p_buf[idx++] = opcode;
    (void) memcpy(&p_buf[idx], p_data, data_len);
    idx += data_len;
    p_buf[idx++] = (uint8_t) (crc >> 8U);
    p_buf[idx++] = (uint8_t) (crc & 0xFFU);
    p_buf[idx++] = MU_SYNC2;
    return idx;
}

/* ─── Вспомогательные функции теста ─────────────────────────────────────── */

/** Открыть pipe, выставить read-конец в non-blocking. */
static void open_test_pipe(int *p_rd, int *p_wr)
{
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(fds));
    TEST_ASSERT_EQUAL_INT(0, fcntl(fds[0], F_SETFL, O_NONBLOCK));
    *p_rd = fds[0];
    *p_wr = fds[1];
}

/** Типичная текстовая нагрузка для opcode 0xDA. */
static const uint8_t PAYLOAD_FLOOR5[] = "#STM:L16:R5:A1:S1:M0:E#\r\n";
static const uint8_t PAYLOAD_FLOOR6[] = "#STM:L16:R6:A1:S0:M0:E#\r\n";

/* ─── setUp / tearDown ───────────────────────────────────────────────────── */

void setUp(void)
{ /* ничего */
}
void tearDown(void)
{ /* ничего */
}

/* ─── Тесты ──────────────────────────────────────────────────────────────── */

/**
 * Тест 1: Валидный фрейм → коллбэк вызван ровно один раз.
 * Проверяем opcode, data_len, содержимое data.
 */
void test_valid_frame_calls_callback(void)
{
    int rd, wr;
    open_test_pipe(&rd, &wr);

    capture_t cap;
    (void) memset(&cap, 0, sizeof(cap));

    uart_t *p_u = uart_wrap_fd(rd, capture_cb, &cap);
    TEST_ASSERT_NOT_NULL(p_u);

    uint8_t buf[MU_DATA_MAX + MU_FRAME_OVERHEAD + 1U];
    uint8_t data_len = (uint8_t) (sizeof(PAYLOAD_FLOOR5) - 1U); /* без '\0' */
    size_t frame_len = build_frame(buf, MU_OPCODE_ELEVATOR_STATUS, PAYLOAD_FLOOR5, data_len);

    TEST_ASSERT_EQUAL_INT((int) frame_len, (int) write(wr, buf, frame_len));
    TEST_ASSERT_EQUAL_INT(0, uart_process_rx(p_u));

    TEST_ASSERT_EQUAL_INT(1, cap.call_count);
    TEST_ASSERT_EQUAL_UINT8(MU_OPCODE_ELEVATOR_STATUS, cap.frames[0].opcode);
    TEST_ASSERT_EQUAL_UINT8(data_len, cap.frames[0].data_len);
    TEST_ASSERT_EQUAL_MEMORY(PAYLOAD_FLOOR5, cap.frames[0].data, data_len);

    uart_close(p_u);
    (void) close(wr);
}

/**
 * Тест 2: Мусор перед фреймом → парсер ищет sync1, коллбэк вызван.
 */
void test_junk_before_frame_is_skipped(void)
{
    int rd, wr;
    open_test_pipe(&rd, &wr);

    capture_t cap;
    (void) memset(&cap, 0, sizeof(cap));

    uart_t *p_u = uart_wrap_fd(rd, capture_cb, &cap);
    TEST_ASSERT_NOT_NULL(p_u);

    /* Сначала пишем мусор — байты, не содержащие 0xAA */
    const uint8_t JUNK[] = { 0x01U, 0x02U, 0x03U, 0xBBU, 0xCCU };
    TEST_ASSERT_EQUAL_INT((int) sizeof(JUNK), (int) write(wr, JUNK, sizeof(JUNK)));

    /* Затем — валидный фрейм */
    uint8_t buf[MU_DATA_MAX + MU_FRAME_OVERHEAD + 1U];
    uint8_t data_len = (uint8_t) (sizeof(PAYLOAD_FLOOR5) - 1U);
    size_t frame_len = build_frame(buf, MU_OPCODE_ELEVATOR_STATUS, PAYLOAD_FLOOR5, data_len);
    TEST_ASSERT_EQUAL_INT((int) frame_len, (int) write(wr, buf, frame_len));

    TEST_ASSERT_EQUAL_INT(0, uart_process_rx(p_u));

    TEST_ASSERT_EQUAL_INT(1, cap.call_count);
    TEST_ASSERT_EQUAL_UINT8(MU_OPCODE_ELEVATOR_STATUS, cap.frames[0].opcode);

    uart_close(p_u);
    (void) close(wr);
}

/**
 * Тест 3: Неверный CRC → коллбэк не вызван.
 */
void test_bad_crc_drops_frame(void)
{
    int rd, wr;
    open_test_pipe(&rd, &wr);

    capture_t cap;
    (void) memset(&cap, 0, sizeof(cap));

    uart_t *p_u = uart_wrap_fd(rd, capture_cb, &cap);
    TEST_ASSERT_NOT_NULL(p_u);

    uint8_t buf[MU_DATA_MAX + MU_FRAME_OVERHEAD + 1U];
    uint8_t data_len = (uint8_t) (sizeof(PAYLOAD_FLOOR5) - 1U);
    size_t frame_len = build_frame(buf, MU_OPCODE_ELEVATOR_STATUS, PAYLOAD_FLOOR5, data_len);

    /* Испортить CRC: инвертировать старший байт */
    size_t crc_h_idx = frame_len - 3U; /* [...][crc_h][crc_l][0xBB] */
    buf[crc_h_idx] ^= 0xFFU;

    TEST_ASSERT_EQUAL_INT((int) frame_len, (int) write(wr, buf, frame_len));
    TEST_ASSERT_EQUAL_INT(0, uart_process_rx(p_u));

    TEST_ASSERT_EQUAL_INT(0, cap.call_count);

    uart_close(p_u);
    (void) close(wr);
}

/**
 * Тест 4: Фрейм разбит на две части → коллбэк вызван только после второго write.
 * Моделирует реальную ситуацию, когда байты приходят порциями через UART.
 */
void test_partial_frame_reassembly(void)
{
    int rd, wr;
    open_test_pipe(&rd, &wr);

    capture_t cap;
    (void) memset(&cap, 0, sizeof(cap));

    uart_t *p_u = uart_wrap_fd(rd, capture_cb, &cap);
    TEST_ASSERT_NOT_NULL(p_u);

    uint8_t buf[MU_DATA_MAX + MU_FRAME_OVERHEAD + 1U];
    uint8_t data_len = (uint8_t) (sizeof(PAYLOAD_FLOOR5) - 1U);
    size_t frame_len = build_frame(buf, MU_OPCODE_ELEVATOR_STATUS, PAYLOAD_FLOOR5, data_len);

    /* Первая половина: sync1 + size + opcode + часть данных */
    size_t half = frame_len / 2U;
    TEST_ASSERT_EQUAL_INT((int) half, (int) write(wr, buf, half));
    TEST_ASSERT_EQUAL_INT(0, uart_process_rx(p_u));

    /* Коллбэк ещё не должен был сработать */
    TEST_ASSERT_EQUAL_INT(0, cap.call_count);

    /* Вторая половина */
    TEST_ASSERT_EQUAL_INT((int) (frame_len - half), (int) write(wr, &buf[half], frame_len - half));
    TEST_ASSERT_EQUAL_INT(0, uart_process_rx(p_u));

    TEST_ASSERT_EQUAL_INT(1, cap.call_count);
    TEST_ASSERT_EQUAL_UINT8(data_len, cap.frames[0].data_len);

    uart_close(p_u);
    (void) close(wr);
}

/**
 * Тест 5: Два фрейма в одном write → два вызова коллбэка.
 */
void test_two_consecutive_frames(void)
{
    int rd, wr;
    open_test_pipe(&rd, &wr);

    capture_t cap;
    (void) memset(&cap, 0, sizeof(cap));

    uart_t *p_u = uart_wrap_fd(rd, capture_cb, &cap);
    TEST_ASSERT_NOT_NULL(p_u);

    uint8_t buf[2U * (MU_DATA_MAX + MU_FRAME_OVERHEAD + 1U)];
    uint8_t data_len1 = (uint8_t) (sizeof(PAYLOAD_FLOOR5) - 1U);
    uint8_t data_len2 = (uint8_t) (sizeof(PAYLOAD_FLOOR6) - 1U);
    size_t frame_len1 = build_frame(buf, MU_OPCODE_ELEVATOR_STATUS, PAYLOAD_FLOOR5, data_len1);
    size_t frame_len2 =
        build_frame(&buf[frame_len1], MU_OPCODE_ELEVATOR_STATUS, PAYLOAD_FLOOR6, data_len2);
    size_t total = frame_len1 + frame_len2;

    TEST_ASSERT_EQUAL_INT((int) total, (int) write(wr, buf, total));
    TEST_ASSERT_EQUAL_INT(0, uart_process_rx(p_u));

    TEST_ASSERT_EQUAL_INT(2, cap.call_count);

    /* Первый фрейм — PAYLOAD_FLOOR5 */
    TEST_ASSERT_EQUAL_UINT8(data_len1, cap.frames[0].data_len);
    TEST_ASSERT_EQUAL_MEMORY(PAYLOAD_FLOOR5, cap.frames[0].data, data_len1);

    /* Второй фрейм — PAYLOAD_FLOOR6 */
    TEST_ASSERT_EQUAL_UINT8(data_len2, cap.frames[1].data_len);
    TEST_ASSERT_EQUAL_MEMORY(PAYLOAD_FLOOR6, cap.frames[1].data, data_len2);

    uart_close(p_u);
    (void) close(wr);
}

/* ─── Точка входа ────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_frame_calls_callback);
    RUN_TEST(test_junk_before_frame_is_skipped);
    RUN_TEST(test_bad_crc_drops_frame);
    RUN_TEST(test_partial_frame_reassembly);
    RUN_TEST(test_two_consecutive_frames);
    return UNITY_END();
}
