/**
 * tests/test_parser.c — обновлено под новые диапазоны types.h
 *
 * Изменения vs v1:
 *   - test_parse_payload_max_values: SOUND_CODE_MAX=9, корректный mode
 *   - test_parse_payload_out_of_range_sound: теперь S=10 (было 7)
 *   - Добавлены тесты mode_is_valid для разрывных значений
 *   - Добавлен тест mode=255 (CONN_LOST) и mode=100 (DISPATCH_CALL)
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
static size_t make_frame(const char *payload, uint8_t *buf)
{
    uint8_t opcode  = MU_OPCODE_ELEVATOR_STATUS;
    size_t data_len = strlen(payload);
    uint8_t crc_input[1u + MU_DATA_MAX];
    crc_input[0] = opcode;
    memcpy(crc_input + 1, payload, data_len);
    uint16_t crc = protocol_crc16(crc_input, 1u + data_len);
    size_t i     = 0;
    buf[i++]     = MU_SYNC1;
    buf[i++]     = (uint8_t) data_len;
    buf[i++]     = opcode;
    memcpy(buf + i, payload, data_len);
    i += data_len;
    buf[i++] = (uint8_t) (crc >> 8u);
    buf[i++] = (uint8_t) (crc & 0xFFu);
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
void test_crc16_check_vector(void)
{
    const uint8_t data[] = { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39 };
    TEST_ASSERT_EQUAL_HEX16(0x29B1u, protocol_crc16(data, sizeof(data)));
}
void test_crc16_empty(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, protocol_crc16(NULL, 0));
}
void test_crc16_deterministic(void)
{
    const uint8_t b = 0xDA;
    TEST_ASSERT_EQUAL_HEX16(protocol_crc16(&b, 1), protocol_crc16(&b, 1));
}

/* ── parse_frame OK ──────────────────────────────────────────────────────── */
void test_parse_frame_valid(void)
{
    const char *payload = "#STM:L10:R3:A1:S4:M0:E#\r\n";
    uint8_t buf[512];
    size_t flen = make_frame(payload, buf);
    mu_frame_t frame;
    size_t consumed = 0;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_frame(buf, flen, &frame, &consumed));
    TEST_ASSERT_EQUAL(flen, consumed);
    TEST_ASSERT_EQUAL_MEMORY(payload, frame.data, frame.data_len);
}
void test_parse_frame_garbage_before_sync1(void)
{
    const char *payload = "#STM:L1:R2:A0:S1:M0:E#\r\n";
    uint8_t raw[512], fbuf[512];
    size_t flen = make_frame(payload, fbuf);
    raw[0]      = 0x00;
    raw[1]      = 0x99;
    memcpy(raw + 2, fbuf, flen);
    mu_frame_t frame;
    size_t consumed = 0;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_frame(raw, 2 + flen, &frame, &consumed));
    TEST_ASSERT_EQUAL(2 + flen, consumed);
}
void test_parse_frame_two_frames(void)
{
    const char *p1 = "#STM:L1:R2:A0:S1:M0:E#\r\n";
    const char *p2 = "#STM:L3:R4:A2:S0:M1:E#\r\n";
    uint8_t buf[512];
    size_t f1 = make_frame(p1, buf);
    size_t f2 = make_frame(p2, buf + f1);
    mu_frame_t frame;
    size_t consumed = 0;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_frame(buf, f1 + f2, &frame, &consumed));
    TEST_ASSERT_EQUAL(f1, consumed);
    TEST_ASSERT_EQUAL(PARSE_OK,
                      protocol_parse_frame(buf + consumed, f1 + f2 - consumed, &frame, &consumed));
    TEST_ASSERT_EQUAL_STRING(p2, (const char *) frame.data);
}

/* ── parse_frame errors ──────────────────────────────────────────────────── */
void test_parse_frame_no_sync1(void)
{
    const uint8_t buf[] = { 0x00, 0x01, 0x02 };
    mu_frame_t frame;
    size_t consumed = 0;
    TEST_ASSERT_EQUAL(PARSE_ERROR_SYNC1, protocol_parse_frame(buf, sizeof(buf), &frame, &consumed));
    TEST_ASSERT_EQUAL(sizeof(buf), consumed);
}
void test_parse_frame_need_more(void)
{
    const char *payload = "#STM:L1:R2:A0:S1:M0:E#\r\n";
    uint8_t buf[512];
    size_t flen = make_frame(payload, buf);
    mu_frame_t frame;
    size_t consumed = 0;
    TEST_ASSERT_EQUAL(PARSE_NEED_MORE_DATA, protocol_parse_frame(buf, flen - 1, &frame, &consumed));
}
void test_parse_frame_bad_sync2(void)
{
    const char *payload = "#STM:L1:R2:A0:S1:M0:E#\r\n";
    uint8_t buf[512];
    size_t flen   = make_frame(payload, buf);
    buf[flen - 1] = 0x00;
    mu_frame_t frame;
    size_t consumed = 0;
    TEST_ASSERT_EQUAL(PARSE_ERROR_SYNC2, protocol_parse_frame(buf, flen, &frame, &consumed));
}
void test_parse_frame_bad_crc(void)
{
    const char *payload = "#STM:L1:R2:A0:S1:M0:E#\r\n";
    uint8_t buf[512];
    size_t flen = make_frame(payload, buf);
    buf[3] ^= 0xFF;
    mu_frame_t frame;
    size_t consumed = 0;
    TEST_ASSERT_EQUAL(PARSE_ERROR_CRC, protocol_parse_frame(buf, flen, &frame, &consumed));
}

/* ── parse_payload OK ────────────────────────────────────────────────────── */
void test_parse_payload_basic(void)
{
    const char *payload = "#STM:L10:R3:A1:S4:M0:E#\r\n";
    uint8_t raw[512];
    size_t flen = make_frame(payload, raw);
    mu_frame_t frame;
    size_t consumed = 0;
    protocol_parse_frame(raw, flen, &frame, &consumed);
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_payload(&frame, &pf));
    TEST_ASSERT_EQUAL(CHAR_A, pf.left_char);
    TEST_ASSERT_EQUAL(CHAR_3, pf.right_char);
    TEST_ASSERT_EQUAL(ARROW_UP, pf.arrow);
    TEST_ASSERT_EQUAL(SOUND_CLOSING, pf.sound);
    TEST_ASSERT_EQUAL(MODE_NORMAL, pf.mode);
}
void test_parse_payload_all_zeros(void)
{
    mu_frame_t f = make_payload_frame("#STM:L0:R0:A0:S0:M0:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_payload(&f, &pf));
    TEST_ASSERT_EQUAL(SOUND_NONE, pf.sound);
    TEST_ASSERT_EQUAL(MODE_NORMAL, pf.mode);
}

/* max valid values: S=9 (SOUND_BUTTON), A=3 (BOTH) */
void test_parse_payload_max_sound(void)
{
    mu_frame_t f = make_payload_frame("#STM:L0:R0:A3:S9:M4:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_payload(&f, &pf));
    TEST_ASSERT_EQUAL(ARROW_BOTH, pf.arrow);
    TEST_ASSERT_EQUAL(SOUND_BUTTON, pf.sound);
    TEST_ASSERT_EQUAL(MODE_OVERLOAD, pf.mode);
}

/* mode=100 (DISPATCH_CALL) */
void test_parse_payload_mode_100(void)
{
    mu_frame_t f = make_payload_frame("#STM:L0:R0:A0:S0:M100:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_payload(&f, &pf));
    TEST_ASSERT_EQUAL(MODE_DISPATCH_CALL, pf.mode);
}

/* mode=255 (CONN_LOST) */
void test_parse_payload_mode_255(void)
{
    mu_frame_t f = make_payload_frame("#STM:L0:R0:A0:S0:M255:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_OK, protocol_parse_payload(&f, &pf));
    TEST_ASSERT_EQUAL(MODE_CONN_LOST, pf.mode);
}

/* mode=10 — невалидный (разрыв 9→100) */
void test_parse_payload_mode_10_invalid(void)
{
    mu_frame_t f = make_payload_frame("#STM:L0:R0:A0:S0:M10:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_ERROR_RANGE, protocol_parse_payload(&f, &pf));
}

/* mode=50 — невалидный (старое значение из MASTER_PLAN, теперь неверно) */
void test_parse_payload_mode_50_invalid(void)
{
    mu_frame_t f = make_payload_frame("#STM:L0:R0:A0:S0:M50:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_ERROR_RANGE, protocol_parse_payload(&f, &pf));
}

/* ── parse_payload errors ────────────────────────────────────────────────── */
void test_parse_payload_wrong_prefix(void)
{
    mu_frame_t f = make_payload_frame("$STM:L1:R2:A0:S0:M0:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_ERROR_PAYLOAD, protocol_parse_payload(&f, &pf));
}
void test_parse_payload_out_of_range_char(void)
{
    mu_frame_t f = make_payload_frame("#STM:L38:R0:A0:S0:M0:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_ERROR_RANGE, protocol_parse_payload(&f, &pf));
}
void test_parse_payload_out_of_range_sound(void)
{
    /* S=10 > SOUND_CODE_MAX (9) */
    mu_frame_t f = make_payload_frame("#STM:L0:R0:A0:S10:M0:E#\r\n");
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_ERROR_RANGE, protocol_parse_payload(&f, &pf));
}
void test_parse_payload_wrong_opcode(void)
{
    mu_frame_t f = make_payload_frame("#STM:L1:R2:A0:S0:M0:E#\r\n");
    f.opcode     = 0x01;
    parsed_frame_t pf;
    TEST_ASSERT_EQUAL(PARSE_ERROR_PAYLOAD, protocol_parse_payload(&f, &pf));
}

/* ─────────────────────────────────────────────────────────────────────────── */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_crc16_check_vector);
    RUN_TEST(test_crc16_empty);
    RUN_TEST(test_crc16_deterministic);

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

    return UNITY_END();
}