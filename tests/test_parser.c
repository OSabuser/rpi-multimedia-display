/**
 * tests/test_parser.c
 *
 * Изменения:
 *   - make_frame(): CRC little-endian [LO][HI] — как у STM32 MU_tx_frame_create
 *   - test_crc16_empty: убран NULL, заменён на пустой массив (UB-free)
 *   - Добавлен test_parse_frame_crc_byte_order: явная проверка little-endian
 */

#include "protocol/parser.h"
#include "unity.h"

#include <stdint.h>
#include <string.h>

void setUp(void)
{
}
void tearDown(void)
{
}

/* ── Утилита ─────────────────────────────────────────────────────────────── */

/**
 * Строит бинарный фрейм в точности как MU_tx_frame_create на STM32:
 *   [SYNC1][SIZE][OPCODE][DATA...][CRC_LO][CRC_HI][SYNC2]
 *
 * CRC_LO = младший байт первым (little-endian) — именно так STM32.
 */
static size_t make_frame(const char *payload, uint8_t *buf)
{
    uint8_t opcode  = MU_OPCODE_ELEVATOR_STATUS;
    size_t data_len = strlen(payload);

    uint8_t crc_input[1u + MU_DATA_MAX];
    crc_input[0u] = opcode;
    memcpy(crc_input + 1u, payload, data_len);
    uint16_t crc = protocol_crc16(crc_input, 1u + data_len);

    size_t i = 0u;
    buf[i++] = MU_SYNC1;
    buf[i++] = (uint8_t) data_len;
    buf[i++] = opcode;
    memcpy(buf + i, payload, data_len);
    i += data_len;
    buf[i++] = (uint8_t) (crc & 0xFFu);         /* LO — младший байт первым */
    buf[i++] = (uint8_t) ((crc >> 8u) & 0xFFu); /* HI — старший байт вторым */
    buf[i++] = MU_SYNC2;
    return i;
}

static mu_frame_t make_payload_frame(const char *s)
{
    mu_frame_t f;
    memset(&f, 0, sizeof(f));
    f.opcode   = MU_OPCODE_ELEVATOR_STATUS;
    f.data_len = (uint8_t) strlen(s);
    memcpy(f.data, s, f.data_len);
    return f;
}

/* ── CRC ─────────────────────────────────────────────────────────────────── */

static void test_crc16_check_vector(void)
{
    const uint8_t data[] = { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39 };
    TEST_ASSERT_EQUAL_HEX16(0x29B1u, protocol_crc16(data, sizeof(data)));
}

static void test_crc16_empty(void)
{
    /* len=0 → init value без итераций */
    const uint8_t dummy = 0u;
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, protocol_crc16(&dummy, 0u));
}

static void test_crc16_deterministic(void)
{
    const uint8_t b = 0xDAu;
    TEST_ASSERT_EQUAL_HEX16(protocol_crc16(&b, 1u), protocol_crc16(&b, 1u));
}

/* ── CRC byte order — критический тест ──────────────────────────────────── */

/**
 * Явно проверяет little-endian порядок байт CRC в кадре.
 *
 * Строим кадр вручную с заранее известным CRC, затем проверяем
 * что protocol_parse_frame() читает байты в правильном порядке.
 *
 * Если порядок байт перепутан — PARSE_ERROR_CRC.
 * Этот тест защищает от регрессии после исправления.
 */
static void test_parse_frame_crc_byte_order(void)
{
    /* Минимальный payload */
    const char *payload = "#STM:L0:R0:A0:S0:M0:E#\r\n";
    uint8_t buf[512];
    size_t flen = make_frame(payload, buf);

    /* Убедиться что фрейм парсится без ошибки CRC */
    mu_frame_t frame;
    size_t consumed  = 0u;
    parse_result_t r = protocol_parse_frame(buf, flen, &frame, &consumed);
    TEST_ASSERT_EQUAL_MESSAGE(
        PARSE_OK, r, "CRC mismatch — byte order in parser.c likely wrong (expected little-endian)");
    TEST_ASSERT_EQUAL(flen, consumed);

    /* Проверить что если поменять байты местами — будет ошибка */
    uint8_t buf2[512];
    size_t flen2 = make_frame(payload, buf2);
    /* Инвертируем порядок двух CRC-байт */
    size_t crc_pos     = flen2 - 3u; /* [...][CRC_LO][CRC_HI][SYNC2] */
    uint8_t lo         = buf2[crc_pos];
    uint8_t hi         = buf2[crc_pos + 1u];
    buf2[crc_pos]      = hi;
    buf2[crc_pos + 1u] = lo;
    parse_result_t r2  = protocol_parse_frame(buf2, flen2, &frame, &consumed);
    TEST_ASSERT_EQUAL_MESSAGE(PARSE_ERROR_CRC, r2,
                              "Swapped CRC bytes should cause PARSE_ERROR_CRC");
}

/* ── parse_frame OK ──────────────────────────────────────────────────────── */

static void test_parse_frame_valid(void)
{
    const char *payload = "#STM:L10:R3:A1:S4:M0:E#\r\n";
    uint8_t buf[512];
    size_t flen = make_frame(payload, buf);
    mu_frame_t frame;
    size_t consumed = 0u;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_frame(buf, flen, &frame, &consumed));
    TEST_ASSERT_EQUAL(flen, consumed);
    TEST_ASSERT_EQUAL_MEMORY(payload, frame.data, frame.data_len);
}

static void test_parse_frame_garbage_before_sync1(void)
{
    const char *payload = "#STM:L1:R2:A0:S1:M0:E#\r\n";
    uint8_t raw[512];
    uint8_t fbuf[512];
    size_t flen = make_frame(payload, fbuf);
    raw[0u]     = 0x00u;
    raw[1u]     = 0x99u;
    memcpy(raw + 2u, fbuf, flen);
    mu_frame_t frame;
    size_t consumed = 0u;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_frame(raw, 2u + flen, &frame, &consumed));
    TEST_ASSERT_EQUAL(2u + flen, consumed);
}

static void test_parse_frame_two_frames(void)
{
    const char *p1 = "#STM:L1:R2:A0:S1:M0:E#\r\n";
    const char *p2 = "#STM:L3:R4:A2:S0:M1:E#\r\n";
    uint8_t buf[512];
    size_t f1 = make_frame(p1, buf);
    size_t f2 = make_frame(p2, buf + f1);
    mu_frame_t frame;
    size_t consumed = 0u;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_frame(buf, f1 + f2, &frame, &consumed));
    TEST_ASSERT_EQUAL(f1, consumed);
    TEST_ASSERT_EQUAL(PARSE_OK,
                      protocol_parse_frame(buf + consumed, f1 + f2 - consumed, &frame, &consumed));
    TEST_ASSERT_EQUAL_STRING(p2, (const char *) frame.data);
}

/* ── parse_frame errors ──────────────────────────────────────────────────── */

static void test_parse_frame_no_sync1(void)
{
    const uint8_t buf[] = { 0x00u, 0x01u, 0x02u };
    mu_frame_t frame;
    size_t consumed = 0u;
    TEST_ASSERT_EQUAL(PARSE_ERROR_SYNC1, protocol_parse_frame(buf, sizeof(buf), &frame, &consumed));
    TEST_ASSERT_EQUAL(sizeof(buf), consumed);
}

static void test_parse_frame_need_more(void)
{
    const char *payload = "#STM:L1:R2:A0:S1:M0:E#\r\n";
    uint8_t buf[512];
    size_t flen = make_frame(payload, buf);
    mu_frame_t frame;
    size_t consumed = 0u;
    TEST_ASSERT_EQUAL(PARSE_NEED_MORE_DATA,
                      protocol_parse_frame(buf, flen - 1u, &frame, &consumed));
}

static void test_parse_frame_bad_sync2(void)
{
    const char *payload = "#STM:L1:R2:A0:S1:M0:E#\r\n";
    uint8_t buf[512];
    size_t flen    = make_frame(payload, buf);
    buf[flen - 1u] = 0x00u;
    mu_frame_t frame;
    size_t consumed = 0u;
    TEST_ASSERT_EQUAL(PARSE_ERROR_SYNC2, protocol_parse_frame(buf, flen, &frame, &consumed));
}

static void test_parse_frame_bad_crc(void)
{
    const char *payload = "#STM:L1:R2:A0:S1:M0:E#\r\n";
    uint8_t buf[512];
    size_t flen = make_frame(payload, buf);
    buf[3u] ^= 0xFFu; /* испортить первый байт data */
    mu_frame_t frame;
    size_t consumed = 0u;
    TEST_ASSERT_EQUAL(PARSE_ERROR_CRC, protocol_parse_frame(buf, flen, &frame, &consumed));
}

/* ── parse_payload OK ────────────────────────────────────────────────────── */

static void test_parse_payload_basic(void)
{
    const char *payload = "#STM:L10:R3:A1:S4:M0:E#\r\n";
    uint8_t raw[512];
    size_t flen = make_frame(payload, raw);
    mu_frame_t frame;
    size_t consumed = 0u;
    protocol_parse_frame(raw, flen, &frame, &consumed);
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_payload(&frame, &pf));
    TEST_ASSERT_EQUAL(CHAR_A, pf.left_char);
    TEST_ASSERT_EQUAL(CHAR_3, pf.right_char);
    TEST_ASSERT_EQUAL(ARROW_UP, pf.arrow);
    TEST_ASSERT_EQUAL(SOUND_CLOSING, pf.sound);
    TEST_ASSERT_EQUAL(MODE_NORMAL, pf.mode);
}

static void test_parse_payload_all_zeros(void)
{
    mu_frame_t f = make_payload_frame("#STM:L0:R0:A0:S0:M0:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_payload(&f, &pf));
    TEST_ASSERT_EQUAL(SOUND_NONE, pf.sound);
    TEST_ASSERT_EQUAL(MODE_NORMAL, pf.mode);
}

static void test_parse_payload_max_sound(void)
{
    mu_frame_t f = make_payload_frame("#STM:L0:R0:A3:S9:M4:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_payload(&f, &pf));
    TEST_ASSERT_EQUAL(ARROW_BOTH, pf.arrow);
    TEST_ASSERT_EQUAL(SOUND_BUTTON, pf.sound);
    TEST_ASSERT_EQUAL(MODE_OVERLOAD, pf.mode);
}

static void test_parse_payload_mode_100(void)
{
    mu_frame_t f = make_payload_frame("#STM:L0:R0:A0:S0:M100:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_payload(&f, &pf));
    TEST_ASSERT_EQUAL(MODE_DISPATCH_CALL, pf.mode);
}

static void test_parse_payload_mode_255(void)
{
    mu_frame_t f = make_payload_frame("#STM:L0:R0:A0:S0:M255:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_payload(&f, &pf));
    TEST_ASSERT_EQUAL(MODE_CONN_LOST, pf.mode);
}

static void test_parse_payload_mode_10_invalid(void)
{
    mu_frame_t f = make_payload_frame("#STM:L0:R0:A0:S0:M10:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_ERROR_RANGE, protocol_parse_payload(&f, &pf));
}

static void test_parse_payload_mode_50_invalid(void)
{
    mu_frame_t f = make_payload_frame("#STM:L0:R0:A0:S0:M50:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_ERROR_RANGE, protocol_parse_payload(&f, &pf));
}

/* ── parse_payload errors ────────────────────────────────────────────────── */

static void test_parse_payload_wrong_prefix(void)
{
    mu_frame_t f = make_payload_frame("$STM:L1:R2:A0:S0:M0:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_ERROR_PAYLOAD, protocol_parse_payload(&f, &pf));
}

static void test_parse_payload_out_of_range_char(void)
{
    mu_frame_t f = make_payload_frame("#STM:L38:R0:A0:S0:M0:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_ERROR_RANGE, protocol_parse_payload(&f, &pf));
}

static void test_parse_payload_out_of_range_sound(void)
{
    mu_frame_t f = make_payload_frame("#STM:L0:R0:A0:S10:M0:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_ERROR_RANGE, protocol_parse_payload(&f, &pf));
}

static void test_parse_payload_wrong_opcode(void)
{
    mu_frame_t f = make_payload_frame("#STM:L1:R2:A0:S0:M0:E#\r\n");
    f.opcode     = 0x01u;
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_ERROR_PAYLOAD, protocol_parse_payload(&f, &pf));
}

/* Утилита: построить dispatch-фрейм */
static size_t make_dispatch_frame(const char *p_payload, uint8_t *p_buf)
{
    uint8_t opcode  = MU_OPCODE_DISPATCH;
    size_t data_len = strlen(p_payload);

    uint8_t crc_input[1u + MU_DATA_MAX];
    crc_input[0u] = opcode;
    memcpy(crc_input + 1u, p_payload, data_len);
    uint16_t crc = protocol_crc16(crc_input, 1u + data_len);

    size_t i   = 0u;
    p_buf[i++] = MU_SYNC1;
    p_buf[i++] = (uint8_t) data_len;
    p_buf[i++] = opcode;
    memcpy(p_buf + i, p_payload, data_len);
    i += data_len;
    p_buf[i++] = (uint8_t) (crc & 0xFFu);
    p_buf[i++] = (uint8_t) ((crc >> 8u) & 0xFFu);
    p_buf[i++] = MU_SYNC2;
    return i;
}

static void test_parse_dispatch_call(void)
{
    uint8_t raw[512];
    size_t flen = make_dispatch_frame("DISPATCH CALL\r\n", raw);
    mu_frame_t frame;
    size_t consumed = 0u;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_frame(raw, flen, &frame, &consumed));
    dispatch_state_t ds;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_dispatch(&frame, &ds));
    TEST_ASSERT_EQUAL(DISPATCH_CALL, ds);
}

static void test_parse_dispatch_answer(void)
{
    uint8_t raw[512];
    size_t flen = make_dispatch_frame("DISPATCH ANSWER\r\n", raw);
    mu_frame_t frame;
    size_t consumed = 0u;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_frame(raw, flen, &frame, &consumed));
    dispatch_state_t ds;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_dispatch(&frame, &ds));
    TEST_ASSERT_EQUAL(DISPATCH_ANSWER, ds);
}

static void test_parse_dispatch_off(void)
{
    uint8_t raw[512];
    size_t flen = make_dispatch_frame("DISPATCH OFF\r\n", raw);
    mu_frame_t frame;
    size_t consumed = 0u;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_frame(raw, flen, &frame, &consumed));
    dispatch_state_t ds;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_dispatch(&frame, &ds));
    TEST_ASSERT_EQUAL(DISPATCH_OFF, ds);
}

static void test_parse_dispatch_wrong_opcode(void)
{
    /* Фрейм с opcode=0xDA не должен парситься как dispatch */
    mu_frame_t f = make_payload_frame("#STM:L0:R0:A0:S0:M0:E#\r\n");
    dispatch_state_t ds;
    TEST_ASSERT_EQUAL(PARSE_ERROR_PAYLOAD, protocol_parse_dispatch(&f, &ds));
}

static void test_parse_dispatch_unknown_payload(void)
{
    /* Неизвестная строка в dispatch-фрейме */
    mu_frame_t f;
    memset(&f, 0, sizeof(f));
    f.opcode   = MU_OPCODE_DISPATCH;
    f.data_len = 10u;
    memcpy(f.data, "UNKNOWN\r\n", 10u);
    dispatch_state_t ds;
    TEST_ASSERT_EQUAL(PARSE_ERROR_PAYLOAD, protocol_parse_dispatch(&f, &ds));
}
/* ─────────────────────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_crc16_check_vector);
    RUN_TEST(test_crc16_empty);
    RUN_TEST(test_crc16_deterministic);

    RUN_TEST(test_parse_frame_crc_byte_order); /* новый — регрессия byte order */
    RUN_TEST(test_parse_frame_valid);
    RUN_TEST(test_parse_frame_garbage_before_sync1);
    RUN_TEST(test_parse_frame_two_frames);
    RUN_TEST(test_parse_frame_no_sync1);
    RUN_TEST(test_parse_frame_need_more);
    RUN_TEST(test_parse_frame_bad_sync2);
    RUN_TEST(test_parse_frame_bad_crc);

    RUN_TEST(test_parse_payload_basic);
    RUN_TEST(test_parse_payload_all_zeros);
    RUN_TEST(test_parse_payload_max_sound);
    RUN_TEST(test_parse_payload_mode_100);
    RUN_TEST(test_parse_payload_mode_255);
    RUN_TEST(test_parse_payload_mode_10_invalid);
    RUN_TEST(test_parse_payload_mode_50_invalid);
    RUN_TEST(test_parse_payload_wrong_prefix);
    RUN_TEST(test_parse_payload_out_of_range_char);
    RUN_TEST(test_parse_payload_out_of_range_sound);
    RUN_TEST(test_parse_payload_wrong_opcode);

    RUN_TEST(test_parse_dispatch_call);
    RUN_TEST(test_parse_dispatch_answer);
    RUN_TEST(test_parse_dispatch_off);
    RUN_TEST(test_parse_dispatch_wrong_opcode);
    RUN_TEST(test_parse_dispatch_unknown_payload);

    return UNITY_END();
}
