/**
 * @file test_uart.c
 * @brief Unit-тесты для uart_process_rx() через pipe().
 *
 * Cleanup через tearDown():
 *   Unity вызывает tearDown() после каждого теста независимо от результата,
 *   включая случаи когда TEST_ASSERT делает longjmp. Поэтому ресурсы
 *   (uart_t, pipe fd) хранятся в статических переменных модуля и
 *   освобождаются в tearDown() — не в теле теста.
 *
 * CRC byte order:
 *   build_frame() использует little-endian [LO][HI] — как STM32 MU_tx_frame_create:
 *     txFrame[DATA+len]   = crc;       // LO первым
 *     txFrame[DATA+len+1] = crc >> 8;  // HI вторым
 */

#define _GNU_SOURCE

#include "protocol/parser.h"
#include "transport/uart.h"
#include "unity.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/* ─── Ресурсы теста — освобождаются в tearDown() ────────────────────────── */

static uart_t *s_uart = NULL;
static int s_wr_fd    = -1;

/* ─── Захват вызовов коллбэка ────────────────────────────────────────────── */

#define MAX_CAPTURED_FRAMES 4U

typedef struct capture_s
{
    int call_count;
    mu_frame_t frames[MAX_CAPTURED_FRAMES];
} capture_t;

static capture_t s_cap;

static void capture_cb(const mu_frame_t *p_frame, void *p_ctx)
{
    capture_t *p_cap = (capture_t *) p_ctx;
    if (p_cap->call_count < (int) MAX_CAPTURED_FRAMES)
    {
        p_cap->frames[p_cap->call_count] = *p_frame;
    }
    p_cap->call_count++;
}

/* ─── Построитель фреймов ────────────────────────────────────────────────── */

/**
 * CRC little-endian: [LO][HI] — как STM32 MU_tx_frame_create.
 *   txFrame[DATA+len]   = crc;       // LO первым
 *   txFrame[DATA+len+1] = crc >> 8;  // HI вторым
 */
static size_t build_frame(uint8_t *p_buf, uint8_t opcode, const uint8_t *p_data, uint8_t data_len)
{
    uint8_t crc_src[MU_DATA_MAX + 1U];
    crc_src[0U] = opcode;
    (void) memcpy(&crc_src[1U], p_data, data_len);
    uint16_t crc = protocol_crc16(crc_src, (size_t) data_len + 1U);

    size_t idx   = 0U;
    p_buf[idx++] = MU_SYNC1;
    p_buf[idx++] = data_len;
    p_buf[idx++] = opcode;
    (void) memcpy(&p_buf[idx], p_data, data_len);
    idx += data_len;
    p_buf[idx++] = (uint8_t) (crc & 0xFFU);         /* LO — первым */
    p_buf[idx++] = (uint8_t) ((crc >> 8U) & 0xFFU); /* HI — вторым */
    p_buf[idx++] = MU_SYNC2;
    return idx;
}

/* ─── Инициализация теста ────────────────────────────────────────────────── */

static void open_test_pipe_and_uart(void)
{
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(fds));
    TEST_ASSERT_EQUAL_INT(0, fcntl(fds[0], F_SETFL, O_NONBLOCK));
    s_wr_fd = fds[1];
    s_uart  = uart_wrap_fd(fds[0], capture_cb, &s_cap);
    TEST_ASSERT_NOT_NULL(s_uart);
}

/* ─── setUp / tearDown ───────────────────────────────────────────────────── */

void setUp(void)
{
    s_uart  = NULL;
    s_wr_fd = -1;
    (void) memset(&s_cap, 0, sizeof(s_cap));
}

/**
 * Вызывается Unity после каждого теста — включая упавшие по TEST_ASSERT.
 * Гарантирует освобождение ресурсов даже при longjmp.
 */
void tearDown(void)
{
    /* uart_close безопасен при NULL */
    uart_close(s_uart);
    s_uart = NULL;

    if (s_wr_fd >= 0)
    {
        (void) close(s_wr_fd);
        s_wr_fd = -1;
    }
}

/* ─── Типичные payload ───────────────────────────────────────────────────── */

static const uint8_t PAYLOAD_FLOOR5[] = "#STM:L16:R5:A1:S1:M0:E#\r\n";
static const uint8_t PAYLOAD_FLOOR6[] = "#STM:L16:R6:A1:S0:M0:E#\r\n";

/* ─── Тесты ──────────────────────────────────────────────────────────────── */

/**
 * Тест 1: Валидный фрейм → коллбэк вызван ровно один раз.
 */
static void test_valid_frame_calls_callback(void)
{
    open_test_pipe_and_uart();

    uint8_t buf[MU_DATA_MAX + MU_FRAME_OVERHEAD + 1U];
    uint8_t data_len = (uint8_t) (sizeof(PAYLOAD_FLOOR5) - 1U);
    size_t frame_len = build_frame(buf, MU_OPCODE_ELEVATOR_STATUS, PAYLOAD_FLOOR5, data_len);

    TEST_ASSERT_EQUAL_INT((int) frame_len, (int) write(s_wr_fd, buf, frame_len));
    TEST_ASSERT_EQUAL_INT(0, uart_process_rx(s_uart));

    TEST_ASSERT_EQUAL_INT(1, s_cap.call_count);
    TEST_ASSERT_EQUAL_UINT8(MU_OPCODE_ELEVATOR_STATUS, s_cap.frames[0].opcode);
    TEST_ASSERT_EQUAL_UINT8(data_len, s_cap.frames[0].data_len);
    TEST_ASSERT_EQUAL_MEMORY(PAYLOAD_FLOOR5, s_cap.frames[0].data, data_len);
}

/**
 * Тест 2: Мусор перед фреймом → игнорируется, коллбэк вызван.
 */
static void test_junk_before_frame_is_skipped(void)
{
    open_test_pipe_and_uart();

    const uint8_t JUNK[] = { 0x01U, 0x02U, 0x03U, 0xBBU, 0xCCU };
    TEST_ASSERT_EQUAL_INT((int) sizeof(JUNK), (int) write(s_wr_fd, JUNK, sizeof(JUNK)));

    uint8_t buf[MU_DATA_MAX + MU_FRAME_OVERHEAD + 1U];
    uint8_t data_len = (uint8_t) (sizeof(PAYLOAD_FLOOR5) - 1U);
    size_t frame_len = build_frame(buf, MU_OPCODE_ELEVATOR_STATUS, PAYLOAD_FLOOR5, data_len);
    TEST_ASSERT_EQUAL_INT((int) frame_len, (int) write(s_wr_fd, buf, frame_len));

    TEST_ASSERT_EQUAL_INT(0, uart_process_rx(s_uart));

    TEST_ASSERT_EQUAL_INT(1, s_cap.call_count);
    TEST_ASSERT_EQUAL_UINT8(MU_OPCODE_ELEVATOR_STATUS, s_cap.frames[0].opcode);
}

/**
 * Тест 3: Неверный CRC → коллбэк не вызван.
 */
static void test_bad_crc_drops_frame(void)
{
    open_test_pipe_and_uart();

    uint8_t buf[MU_DATA_MAX + MU_FRAME_OVERHEAD + 1U];
    uint8_t data_len = (uint8_t) (sizeof(PAYLOAD_FLOOR5) - 1U);
    size_t frame_len = build_frame(buf, MU_OPCODE_ELEVATOR_STATUS, PAYLOAD_FLOOR5, data_len);

    /* Испортить LO-байт CRC: buf[frame_len-3] = CRC_LO */
    buf[frame_len - 3U] ^= 0xFFU;

    TEST_ASSERT_EQUAL_INT((int) frame_len, (int) write(s_wr_fd, buf, frame_len));
    TEST_ASSERT_EQUAL_INT(0, uart_process_rx(s_uart));

    TEST_ASSERT_EQUAL_INT(0, s_cap.call_count);
}

/**
 * Тест 4: Фрейм разбит на две части → коллбэк только после второго write.
 */
static void test_partial_frame_reassembly(void)
{
    open_test_pipe_and_uart();

    uint8_t buf[MU_DATA_MAX + MU_FRAME_OVERHEAD + 1U];
    uint8_t data_len = (uint8_t) (sizeof(PAYLOAD_FLOOR5) - 1U);
    size_t frame_len = build_frame(buf, MU_OPCODE_ELEVATOR_STATUS, PAYLOAD_FLOOR5, data_len);

    size_t half = frame_len / 2U;
    TEST_ASSERT_EQUAL_INT((int) half, (int) write(s_wr_fd, buf, half));
    TEST_ASSERT_EQUAL_INT(0, uart_process_rx(s_uart));
    TEST_ASSERT_EQUAL_INT(0, s_cap.call_count); /* ещё не полный фрейм */

    TEST_ASSERT_EQUAL_INT((int) (frame_len - half),
                          (int) write(s_wr_fd, &buf[half], frame_len - half));
    TEST_ASSERT_EQUAL_INT(0, uart_process_rx(s_uart));

    TEST_ASSERT_EQUAL_INT(1, s_cap.call_count);
    TEST_ASSERT_EQUAL_UINT8(data_len, s_cap.frames[0].data_len);
}

/**
 * Тест 5: Два фрейма подряд → два вызова коллбэка.
 */
static void test_two_consecutive_frames(void)
{
    open_test_pipe_and_uart();

    uint8_t buf[2U * (MU_DATA_MAX + MU_FRAME_OVERHEAD + 1U)];
    uint8_t data_len1 = (uint8_t) (sizeof(PAYLOAD_FLOOR5) - 1U);
    uint8_t data_len2 = (uint8_t) (sizeof(PAYLOAD_FLOOR6) - 1U);
    size_t frame_len1 = build_frame(buf, MU_OPCODE_ELEVATOR_STATUS, PAYLOAD_FLOOR5, data_len1);
    size_t frame_len2 =
        build_frame(&buf[frame_len1], MU_OPCODE_ELEVATOR_STATUS, PAYLOAD_FLOOR6, data_len2);
    size_t total = frame_len1 + frame_len2;

    TEST_ASSERT_EQUAL_INT((int) total, (int) write(s_wr_fd, buf, total));
    TEST_ASSERT_EQUAL_INT(0, uart_process_rx(s_uart));

    TEST_ASSERT_EQUAL_INT(2, s_cap.call_count);
    TEST_ASSERT_EQUAL_UINT8(data_len1, s_cap.frames[0].data_len);
    TEST_ASSERT_EQUAL_MEMORY(PAYLOAD_FLOOR5, s_cap.frames[0].data, data_len1);
    TEST_ASSERT_EQUAL_UINT8(data_len2, s_cap.frames[1].data_len);
    TEST_ASSERT_EQUAL_MEMORY(PAYLOAD_FLOOR6, s_cap.frames[1].data, data_len2);
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
