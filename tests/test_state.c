/**
 * tests/test_state.c
 *
 * Unit-тесты для state_init() и state_apply_frame().
 */

#include "domain/state.h"
#include "unity.h"

void setUp(void)
{
}
void tearDown(void)
{
}

/* ── Вспомогательная: собрать parsed_frame_t ─────────────────────────────── */
static parsed_frame_t make_frame(char_code_t l, char_code_t r, arrow_t a, sound_t s,
                                 inndicator_mode_t m)
{
    parsed_frame_t f;
    f.left_char  = l;
    f.right_char = r;
    f.arrow      = a;
    f.sound      = s;
    f.mode       = m;
    return f;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * state_init
 * ──────────────────────────────────────────────────────────────────────────── */

void test_state_init_not_initialized(void)
{
    indicator_state_t st;
    state_init(&st);
    TEST_ASSERT_EQUAL(0, st.initialized);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Первый фрейм
 * ──────────────────────────────────────────────────────────────────────────── */

void test_first_frame_all_flags_set(void)
{
    indicator_state_t st;
    state_init(&st);

    parsed_frame_t f        = make_frame(CHAR_1, CHAR_5, ARROW_UP, SOUND_DING, MODE_NORMAL);
    state_update_result_t r = state_apply_frame(&st, &f);

    TEST_ASSERT_EQUAL(1, r.first_frame);
    TEST_ASSERT_EQUAL(1, r.floor_changed);
    TEST_ASSERT_EQUAL(1, r.arrow_changed);
    TEST_ASSERT_EQUAL(1, r.mode_changed);
    TEST_ASSERT_EQUAL(1, r.sound_triggered);
}

void test_first_frame_state_stored(void)
{
    indicator_state_t st;
    state_init(&st);

    parsed_frame_t f = make_frame(CHAR_2, CHAR_7, ARROW_DOWN, SOUND_UP, MODE_FIRE_ALARM);
    state_apply_frame(&st, &f);

    TEST_ASSERT_EQUAL(CHAR_2, st.left_char);
    TEST_ASSERT_EQUAL(CHAR_7, st.right_char);
    TEST_ASSERT_EQUAL(ARROW_DOWN, st.arrow);
    TEST_ASSERT_EQUAL(MODE_FIRE_ALARM, st.mode);
    TEST_ASSERT_EQUAL(1, st.initialized);
}

void test_first_frame_sound_none_not_triggered(void)
{
    indicator_state_t st;
    state_init(&st);

    parsed_frame_t f        = make_frame(CHAR_1, CHAR_0, ARROW_NONE, SOUND_NONE, MODE_NORMAL);
    state_update_result_t r = state_apply_frame(&st, &f);

    TEST_ASSERT_EQUAL(1, r.first_frame);
    TEST_ASSERT_EQUAL(0, r.sound_triggered);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Идентичный второй фрейм — никаких изменений
 * ──────────────────────────────────────────────────────────────────────────── */

void test_identical_second_frame_no_changes(void)
{
    indicator_state_t st;
    state_init(&st);

    parsed_frame_t f = make_frame(CHAR_3, CHAR_5, ARROW_UP, SOUND_NONE, MODE_NORMAL);
    state_apply_frame(&st, &f);                           /* первый */
    state_update_result_t r = state_apply_frame(&st, &f); /* второй, идентичный */

    TEST_ASSERT_EQUAL(0, r.first_frame);
    TEST_ASSERT_EQUAL(0, r.floor_changed);
    TEST_ASSERT_EQUAL(0, r.arrow_changed);
    TEST_ASSERT_EQUAL(0, r.mode_changed);
    TEST_ASSERT_EQUAL(0, r.sound_triggered);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Изменение отдельных полей
 * ──────────────────────────────────────────────────────────────────────────── */

void test_floor_change_only(void)
{
    indicator_state_t st;
    state_init(&st);

    parsed_frame_t f1 = make_frame(CHAR_1, CHAR_0, ARROW_UP, SOUND_NONE, MODE_NORMAL);
    parsed_frame_t f2 = make_frame(CHAR_1, CHAR_1, ARROW_UP, SOUND_NONE, MODE_NORMAL);

    state_apply_frame(&st, &f1);
    state_update_result_t r = state_apply_frame(&st, &f2);

    TEST_ASSERT_EQUAL(1, r.floor_changed);
    TEST_ASSERT_EQUAL(0, r.arrow_changed);
    TEST_ASSERT_EQUAL(0, r.mode_changed);
}

void test_arrow_change_only(void)
{
    indicator_state_t st;
    state_init(&st);

    parsed_frame_t f1 = make_frame(CHAR_5, CHAR_BLANK, ARROW_UP, SOUND_NONE, MODE_NORMAL);
    parsed_frame_t f2 = make_frame(CHAR_5, CHAR_BLANK, ARROW_DOWN, SOUND_NONE, MODE_NORMAL);

    state_apply_frame(&st, &f1);
    state_update_result_t r = state_apply_frame(&st, &f2);

    TEST_ASSERT_EQUAL(0, r.floor_changed);
    TEST_ASSERT_EQUAL(1, r.arrow_changed);
    TEST_ASSERT_EQUAL(0, r.mode_changed);
    TEST_ASSERT_EQUAL(ARROW_DOWN, st.arrow);
}

void test_mode_change_only(void)
{
    indicator_state_t st;
    state_init(&st);

    parsed_frame_t f1 = make_frame(CHAR_5, CHAR_BLANK, ARROW_NONE, SOUND_NONE, MODE_NORMAL);
    parsed_frame_t f2 = make_frame(CHAR_5, CHAR_BLANK, ARROW_NONE, SOUND_NONE, MODE_FIRE_ALARM);

    state_apply_frame(&st, &f1);
    state_update_result_t r = state_apply_frame(&st, &f2);

    TEST_ASSERT_EQUAL(0, r.floor_changed);
    TEST_ASSERT_EQUAL(0, r.arrow_changed);
    TEST_ASSERT_EQUAL(1, r.mode_changed);
    TEST_ASSERT_EQUAL(MODE_FIRE_ALARM, st.mode);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Sound — edge-triggered
 * ──────────────────────────────────────────────────────────────────────────── */

void test_sound_edge_triggered(void)
{
    indicator_state_t st;
    state_init(&st);

    parsed_frame_t f0 = make_frame(CHAR_5, CHAR_BLANK, ARROW_UP, SOUND_NONE, MODE_NORMAL);
    parsed_frame_t f1 = make_frame(CHAR_5, CHAR_BLANK, ARROW_UP, SOUND_DING, MODE_NORMAL);
    parsed_frame_t f2 = make_frame(CHAR_5, CHAR_BLANK, ARROW_UP, SOUND_NONE, MODE_NORMAL);

    state_apply_frame(&st, &f0); /* init frame */

    state_update_result_t r1 = state_apply_frame(&st, &f1);
    TEST_ASSERT_EQUAL(1, r1.sound_triggered);

    /* После сброса STM → SOUND_NONE — событие не повторяется */
    state_update_result_t r2 = state_apply_frame(&st, &f2);
    TEST_ASSERT_EQUAL(0, r2.sound_triggered);
}

void test_sound_not_stored_in_state(void)
{
    /* State не хранит sound — только current floor/arrow/mode */
    indicator_state_t st;
    state_init(&st);

    parsed_frame_t f = make_frame(CHAR_3, CHAR_BLANK, ARROW_NONE, SOUND_DING, MODE_NORMAL);
    state_apply_frame(&st, &f);

    /* Повторный идентичный фрейм — floor/arrow/mode не изменились */
    state_update_result_t r = state_apply_frame(&st, &f);
    TEST_ASSERT_EQUAL(0, r.floor_changed);
    TEST_ASSERT_EQUAL(0, r.arrow_changed);
    /* sound_triggered выставлен, потому что в фрейме SOUND_DING */
    TEST_ASSERT_EQUAL(1, r.sound_triggered);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * main
 * ──────────────────────────────────────────────────────────────────────────── */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_state_init_not_initialized);

    RUN_TEST(test_first_frame_all_flags_set);
    RUN_TEST(test_first_frame_state_stored);
    RUN_TEST(test_first_frame_sound_none_not_triggered);

    RUN_TEST(test_identical_second_frame_no_changes);

    RUN_TEST(test_floor_change_only);
    RUN_TEST(test_arrow_change_only);
    RUN_TEST(test_mode_change_only);

    RUN_TEST(test_sound_edge_triggered);
    RUN_TEST(test_sound_not_stored_in_state);

    return UNITY_END();
}
