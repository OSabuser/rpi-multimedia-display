/**
 * tests/test_floor.c
 *
 * Unit-тесты для floor_decode().
 */

#include "domain/floor.h"
#include "unity.h"

void setUp(void)
{
}
void tearDown(void)
{
}

/* ── NORMAL: однозначные этажи ────────────────────────────────────────────── */

static void test_floor_single_1(void)
{
    floor_t f = floor_decode(CHAR_BLANK, CHAR_1);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_NORMAL, f.type);
    TEST_ASSERT_EQUAL(1, f.number);
}

static void test_floor_single_9(void)
{
    floor_t f = floor_decode(CHAR_BLANK, CHAR_9);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_NORMAL, f.type);
    TEST_ASSERT_EQUAL(9, f.number);
}

static void test_floor_single_0(void)
{
    floor_t f = floor_decode(CHAR_BLANK, CHAR_0);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_NORMAL, f.type);
    TEST_ASSERT_EQUAL(0, f.number);
}

/* ── NORMAL: двузначные этажи ─────────────────────────────────────────────── */

static void test_floor_two_digit_10(void)
{
    floor_t f = floor_decode(CHAR_1, CHAR_0);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_NORMAL, f.type);
    TEST_ASSERT_EQUAL(10, f.number);
}

static void test_floor_two_digit_25(void)
{
    floor_t f = floor_decode(CHAR_2, CHAR_5);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_NORMAL, f.type);
    TEST_ASSERT_EQUAL(25, f.number);
}

static void test_floor_two_digit_40(void)
{
    floor_t f = floor_decode(CHAR_4, CHAR_0);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_NORMAL, f.type);
    TEST_ASSERT_EQUAL(40, f.number);
}

static void test_floor_two_digit_99(void)
{
    floor_t f = floor_decode(CHAR_9, CHAR_9);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_NORMAL, f.type);
    TEST_ASSERT_EQUAL(99, f.number);
}

/* ── BASEMENT: П (без номера) ─────────────────────────────────────────────── */

static void test_floor_basement(void)
{
    /* left=BLANK(16), right=П(17) */
    floor_t f = floor_decode(CHAR_BLANK, CHAR_PI_CYR);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_BASEMENT, f.type);
}

/* ── BASEMENT_N: П1–П9 ────────────────────────────────────────────────────── */

static void test_floor_basement_1(void)
{
    floor_t f = floor_decode(CHAR_PI_CYR, CHAR_1);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_BASEMENT_N, f.type);
    TEST_ASSERT_EQUAL(1, f.number);
}

static void test_floor_basement_3(void)
{
    floor_t f = floor_decode(CHAR_PI_CYR, CHAR_3);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_BASEMENT_N, f.type);
    TEST_ASSERT_EQUAL(3, f.number);
}

static void test_floor_basement_9(void)
{
    floor_t f = floor_decode(CHAR_PI_CYR, CHAR_9);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_BASEMENT_N, f.type);
    TEST_ASSERT_EQUAL(9, f.number);
}

static void test_floor_basement_0_is_unknown(void)
{
    /* П0 не предусмотрен таблицей декодирования */
    floor_t f = floor_decode(CHAR_PI_CYR, CHAR_0);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_UNKNOWN, f.type);
}

/* ── NEGATIVE: -1 .. -9 ───────────────────────────────────────────────────── */

static void test_floor_negative_1(void)
{
    floor_t f = floor_decode(CHAR_MINUS, CHAR_1);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_NEGATIVE, f.type);
    TEST_ASSERT_EQUAL(1, f.number);
}

static void test_floor_negative_4(void)
{
    floor_t f = floor_decode(CHAR_MINUS, CHAR_4);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_NEGATIVE, f.type);
    TEST_ASSERT_EQUAL(4, f.number);
}

static void test_floor_negative_9(void)
{
    floor_t f = floor_decode(CHAR_MINUS, CHAR_9);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_NEGATIVE, f.type);
    TEST_ASSERT_EQUAL(9, f.number);
}

static void test_floor_minus_0_is_unknown(void)
{
    /* -0 не предусмотрен */
    floor_t f = floor_decode(CHAR_MINUS, CHAR_0);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_UNKNOWN, f.type);
}

/* ── UNKNOWN ──────────────────────────────────────────────────────────────── */

static void test_floor_unknown_char_a_pair(void)
{
    floor_t f = floor_decode(CHAR_A, CHAR_b);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_UNKNOWN, f.type);
}

static void test_floor_unknown_pi_pi(void)
{
    /* П П — не предусмотрено */
    floor_t f = floor_decode(CHAR_PI_CYR, CHAR_PI_CYR);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_UNKNOWN, f.type);
}

static void test_floor_unknown_blank_blank(void)
{
    /* Оба пробела — не этаж */
    floor_t f = floor_decode(CHAR_BLANK, CHAR_BLANK);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_UNKNOWN, f.type);
}

/* CHAR_pi_cyr (19) как подвальный этаж — L=pi, R=1..9 */
static void test_floor_pi_cyr_lower_basement_n(void)
{
    floor_t f = floor_decode(CHAR_pi_cyr, CHAR_5);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_BASEMENT_N, f.type);
    TEST_ASSERT_EQUAL_INT(5, f.number);
}

/* CHAR_pi_cyr слева, BLANK справа → BASEMENT */
static void test_floor_blank_pi_cyr_lower_basement(void)
{
    floor_t f = floor_decode(CHAR_BLANK, CHAR_pi_cyr);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_BASEMENT, f.type);
}

/* П0: left=CHAR_PI_CYR, right=CHAR_0 → UNKNOWN */
static void test_floor_pi_zero_unknown(void)
{
    floor_t f = floor_decode(CHAR_PI_CYR, CHAR_0);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_UNKNOWN, f.type);
}

/* п0 (строчная): left=CHAR_pi_cyr, right=CHAR_0 → UNKNOWN */
static void test_floor_pi_cyr_lower_zero_unknown(void)
{
    floor_t f = floor_decode(CHAR_pi_cyr, CHAR_0);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_UNKNOWN, f.type);
}

/* -0: left=CHAR_MINUS, right=CHAR_0 → UNKNOWN */
static void test_floor_minus_zero_unknown(void)
{
    floor_t f = floor_decode(CHAR_MINUS, CHAR_0);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_UNKNOWN, f.type);
}

/* L=19, R=5 — фрейм #4 из дампа реального STM32 */
static void test_floor_real_frame_l19_r5(void)
{
    floor_t f = floor_decode((char_code_t) 19, CHAR_5);
    TEST_ASSERT_EQUAL(FLOOR_TYPE_BASEMENT_N, f.type);
    TEST_ASSERT_EQUAL_INT(5, f.number);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * main
 * ──────────────────────────────────────────────────────────────────────────── */
int main(void)
{
    UNITY_BEGIN();

    /* Normal */
    RUN_TEST(test_floor_single_1);
    RUN_TEST(test_floor_single_9);
    RUN_TEST(test_floor_single_0);
    RUN_TEST(test_floor_two_digit_10);
    RUN_TEST(test_floor_two_digit_25);
    RUN_TEST(test_floor_two_digit_40);
    RUN_TEST(test_floor_two_digit_99);

    /* Basement */
    RUN_TEST(test_floor_basement);
    RUN_TEST(test_floor_basement_1);
    RUN_TEST(test_floor_basement_3);
    RUN_TEST(test_floor_basement_9);
    RUN_TEST(test_floor_basement_0_is_unknown);

    /* Negative */
    RUN_TEST(test_floor_negative_1);
    RUN_TEST(test_floor_negative_4);
    RUN_TEST(test_floor_negative_9);
    RUN_TEST(test_floor_minus_0_is_unknown);

    /* Unknown */
    RUN_TEST(test_floor_unknown_char_a_pair);
    RUN_TEST(test_floor_unknown_pi_pi);
    RUN_TEST(test_floor_unknown_blank_blank);

    RUN_TEST(test_floor_pi_cyr_lower_basement_n);
    RUN_TEST(test_floor_blank_pi_cyr_lower_basement);
    RUN_TEST(test_floor_pi_zero_unknown);
    RUN_TEST(test_floor_pi_cyr_lower_zero_unknown);
    RUN_TEST(test_floor_minus_zero_unknown);
    RUN_TEST(test_floor_real_frame_l19_r5);

    return UNITY_END();
}