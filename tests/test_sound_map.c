/**
 * tests/test_sound_map.c
 *
 * Unit-тесты для sound_map_resolve() и sound_map_volume_percent().
 */

#include "domain/sound_map.h"
#include "unity.h"

#include <string.h>

void setUp(void)
{
}
void tearDown(void)
{
}

/* ── Вспомогательные функции ─────────────────────────────────────────────── */

static floor_t normal_floor(int n)
{
    floor_t f = { FLOOR_TYPE_NORMAL, n };
    return f;
}

static floor_t basement(void)
{
    floor_t f = { FLOOR_TYPE_BASEMENT, 0 };
    return f;
}

static floor_t basement_n(int n)
{
    floor_t f = { FLOOR_TYPE_BASEMENT_N, n };
    return f;
}

static floor_t negative_n(int n)
{
    floor_t f = { FLOOR_TYPE_NEGATIVE, n };
    return f;
}

static floor_t unknown_floor(void)
{
    floor_t f = { FLOOR_TYPE_UNKNOWN, 0 };
    return f;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * SOUND_NONE
 * ──────────────────────────────────────────────────────────────────────────── */

static void test_sound_none(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_NONE, normal_floor(5), &s);
    TEST_ASSERT_EQUAL(0, s.valid);
    TEST_ASSERT_EQUAL(0, s.count);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * SOUND_DING — нормальные этажи
 * ──────────────────────────────────────────────────────────────────────────── */

static void test_ding_floor_1(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, normal_floor(1), &s);
    TEST_ASSERT_EQUAL(1, s.valid);
    TEST_ASSERT_EQUAL(2, s.count);
    TEST_ASSERT_EQUAL_STRING("1.wav", s.files[0]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", s.files[1]);
    TEST_ASSERT_EQUAL(0, s.needs_music);
}

static void test_ding_floor_20(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, normal_floor(20), &s);
    TEST_ASSERT_EQUAL(1, s.valid);
    TEST_ASSERT_EQUAL(2, s.count);
    TEST_ASSERT_EQUAL_STRING("20.wav", s.files[0]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", s.files[1]);
}

static void test_ding_floor_21(void)
{
    /* "двадцать первый" */
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, normal_floor(21), &s);
    TEST_ASSERT_EQUAL(3, s.count);
    TEST_ASSERT_EQUAL_STRING("20-.wav", s.files[0]);
    TEST_ASSERT_EQUAL_STRING("1.wav", s.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", s.files[2]);
}

static void test_ding_floor_29(void)
{
    /* "двадцать девятый" */
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, normal_floor(29), &s);
    TEST_ASSERT_EQUAL(3, s.count);
    TEST_ASSERT_EQUAL_STRING("20-.wav", s.files[0]);
    TEST_ASSERT_EQUAL_STRING("9.wav", s.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", s.files[2]);
}

static void test_ding_floor_30(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, normal_floor(30), &s);
    TEST_ASSERT_EQUAL(2, s.count);
    TEST_ASSERT_EQUAL_STRING("30.wav", s.files[0]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", s.files[1]);
}

static void test_ding_floor_35(void)
{
    /* "тридцать пятый" */
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, normal_floor(35), &s);
    TEST_ASSERT_EQUAL(3, s.count);
    TEST_ASSERT_EQUAL_STRING("30-.wav", s.files[0]);
    TEST_ASSERT_EQUAL_STRING("5.wav", s.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", s.files[2]);
}

static void test_ding_floor_40(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, normal_floor(40), &s);
    TEST_ASSERT_EQUAL(2, s.count);
    TEST_ASSERT_EQUAL_STRING("40.wav", s.files[0]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", s.files[1]);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * SOUND_DING — fallback (этажи вне диапазона 1–40)
 * ──────────────────────────────────────────────────────────────────────────── */

static void test_ding_floor_41_fallback(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, normal_floor(41), &s);
    TEST_ASSERT_EQUAL(1, s.valid);
    TEST_ASSERT_EQUAL(1, s.count);
    /* fallback gong — любой из g_*.wav */
    TEST_ASSERT_NOT_NULL(strstr(s.files[0], ".wav"));
}

static void test_ding_floor_0_fallback(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, normal_floor(0), &s);
    TEST_ASSERT_EQUAL(1, s.valid);
    TEST_ASSERT_EQUAL(1, s.count);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * SOUND_DING — подвальные и отрицательные этажи
 * ──────────────────────────────────────────────────────────────────────────── */

static void test_ding_basement(void)
{
    /* П → podval.wav + floor.wav */
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, basement(), &s);
    TEST_ASSERT_EQUAL(1, s.valid);
    TEST_ASSERT_EQUAL(2, s.count);
    TEST_ASSERT_EQUAL_STRING("podval.wav", s.files[0]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", s.files[1]);
}

static void test_ding_basement_3(void)
{
    /* П3 → "третий подвальный этаж" = 3.wav + podval.wav + floor.wav */
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, basement_n(3), &s);
    TEST_ASSERT_EQUAL(1, s.valid);
    TEST_ASSERT_EQUAL(3, s.count);
    TEST_ASSERT_EQUAL_STRING("3.wav", s.files[0]);
    TEST_ASSERT_EQUAL_STRING("podval.wav", s.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", s.files[2]);
}

static void test_ding_basement_1(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, basement_n(1), &s);
    TEST_ASSERT_EQUAL_STRING("1.wav", s.files[0]);
    TEST_ASSERT_EQUAL_STRING("podval.wav", s.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", s.files[2]);
}

static void test_ding_negative_4(void)
{
    /* -4 → minus.wav + 4.wav + floor.wav */
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, negative_n(4), &s);
    TEST_ASSERT_EQUAL(1, s.valid);
    TEST_ASSERT_EQUAL(3, s.count);
    TEST_ASSERT_EQUAL_STRING("minus.wav", s.files[0]);
    TEST_ASSERT_EQUAL_STRING("4.wav", s.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", s.files[2]);
}

static void test_ding_negative_1(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, negative_n(1), &s);
    TEST_ASSERT_EQUAL_STRING("minus.wav", s.files[0]);
    TEST_ASSERT_EQUAL_STRING("1.wav", s.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", s.files[2]);
}

static void test_ding_unknown_floor(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, unknown_floor(), &s);
    TEST_ASSERT_EQUAL(1, s.valid);
    TEST_ASSERT_EQUAL(1, s.count);
    TEST_ASSERT_NOT_NULL(strstr(s.files[0], ".wav"));
}

/* ─────────────────────────────────────────────────────────────────────────────
 * SOUND_UP / DOWN — с флагом needs_music
 * ──────────────────────────────────────────────────────────────────────────── */

static void test_sound_up(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_UP, normal_floor(5), &s);
    TEST_ASSERT_EQUAL(1, s.valid);
    TEST_ASSERT_EQUAL(1, s.count);
    TEST_ASSERT_EQUAL_STRING("up.wav", s.files[0]);
    TEST_ASSERT_EQUAL(1, s.needs_music);
}

static void test_sound_down(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_DOWN, normal_floor(5), &s);
    TEST_ASSERT_EQUAL(1, s.valid);
    TEST_ASSERT_EQUAL(1, s.count);
    TEST_ASSERT_EQUAL_STRING("down.wav", s.files[0]);
    TEST_ASSERT_EQUAL(1, s.needs_music);
}

static void test_sound_ding_no_music(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_DING, normal_floor(5), &s);
    TEST_ASSERT_EQUAL(0, s.needs_music);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * SOUND_OVERLOAD / CLOSING / OPENING
 * ──────────────────────────────────────────────────────────────────────────── */

static void test_sound_overload(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_OVERLOAD, normal_floor(5), &s);
    TEST_ASSERT_EQUAL(1, s.valid);
    TEST_ASSERT_EQUAL_STRING("overload.wav", s.files[0]);
    TEST_ASSERT_EQUAL(0, s.needs_music);
}

static void test_sound_closing(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_CLOSING, normal_floor(5), &s);
    TEST_ASSERT_EQUAL(1, s.valid);
    TEST_ASSERT_EQUAL_STRING("closing.wav", s.files[0]);
}

static void test_sound_opening(void)
{
    audio_sequence_t s;
    sound_map_resolve(SOUND_OPENING, normal_floor(5), &s);
    TEST_ASSERT_EQUAL(1, s.valid);
    TEST_ASSERT_EQUAL_STRING("opening.wav", s.files[0]);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * sound_map_volume_percent
 * ──────────────────────────────────────────────────────────────────────────── */

static void test_volume_none(void)
{
    TEST_ASSERT_EQUAL(0, sound_map_volume_percent(SOUND_NONE, 75));
}

static void test_volume_ding(void)
{
    TEST_ASSERT_EQUAL(50, sound_map_volume_percent(SOUND_DING, 50));
}

static void test_volume_up(void)
{
    TEST_ASSERT_EQUAL(75, sound_map_volume_percent(SOUND_UP, 75));
}

static void test_volume_zero(void)
{
    TEST_ASSERT_EQUAL(0, sound_map_volume_percent(SOUND_DING, 0));
}

/* ─────────────────────────────────────────────────────────────────────────────
 * main
 * ──────────────────────────────────────────────────────────────────────────── */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_sound_none);

    /* DING: нормальные */
    RUN_TEST(test_ding_floor_1);
    RUN_TEST(test_ding_floor_20);
    RUN_TEST(test_ding_floor_21);
    RUN_TEST(test_ding_floor_29);
    RUN_TEST(test_ding_floor_30);
    RUN_TEST(test_ding_floor_35);
    RUN_TEST(test_ding_floor_40);

    /* DING: fallback */
    RUN_TEST(test_ding_floor_41_fallback);
    RUN_TEST(test_ding_floor_0_fallback);

    /* DING: подвальные и отрицательные */
    RUN_TEST(test_ding_basement);
    RUN_TEST(test_ding_basement_3);
    RUN_TEST(test_ding_basement_1);
    RUN_TEST(test_ding_negative_4);
    RUN_TEST(test_ding_negative_1);
    RUN_TEST(test_ding_unknown_floor);

    /* UP/DOWN */
    RUN_TEST(test_sound_up);
    RUN_TEST(test_sound_down);
    RUN_TEST(test_sound_ding_no_music);

    /* Прочие события */
    RUN_TEST(test_sound_overload);
    RUN_TEST(test_sound_closing);
    RUN_TEST(test_sound_opening);

    /* Volume */
    RUN_TEST(test_volume_none);
    RUN_TEST(test_volume_ding);
    RUN_TEST(test_volume_up);
    RUN_TEST(test_volume_zero);

    return UNITY_END();
}