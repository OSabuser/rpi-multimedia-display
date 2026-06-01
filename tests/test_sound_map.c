/**
 * tests/test_sound_map.c
 *
 * Unit-тесты для src/domain/sound_map.c.
 * Компилируются на хосте, не требуют Pi.
 *
 * Охват:
 *   - sound_map_resolve() для всех sound_t значений
 *   - resolve_floor_announcement() для всех floor_type_t и граничных значений
 *   - sound_map_volume_percent()
 *   - Корректность имён файлов после правок Фазы 5:
 *       SOUND_FIRE_ALARM → fire.wav (не g_triple.wav)
 *       SOUND_BUTTON     → button.wav (не g_single.wav)
 *       floor 41–49      → 40-.wav + {ones}.wav + floor.wav
 */

#include "domain/floor.h"
#include "domain/sound_map.h"
#include "protocol/types.h"
#include "unity/unity.h"

#include <string.h>

/* ─── Хелперы ────────────────────────────────────────────────────────────── */

static floor_t make_floor(floor_type_t type, int number)
{
    floor_t f;
    f.type   = type;
    f.number = number;
    return f;
}

void setUp(void)
{
}
void tearDown(void)
{
}

/* ─── SOUND_NONE ─────────────────────────────────────────────────────────── */

void test_sound_none_gives_invalid_sequence(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 5);
    sound_map_resolve(SOUND_NONE, floor, &seq);
    TEST_ASSERT_EQUAL_INT(0, seq.valid);
    TEST_ASSERT_EQUAL_INT(0, seq.count);
}

/* ─── SOUND_UP / SOUND_DOWN — music flag ────────────────────────────────── */

void test_sound_up_sets_needs_music(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 1);
    sound_map_resolve(SOUND_UP, floor, &seq);
    TEST_ASSERT_EQUAL_INT(1, seq.valid);
    TEST_ASSERT_EQUAL_INT(1, seq.needs_music);
    TEST_ASSERT_EQUAL_INT(1, seq.count);
    TEST_ASSERT_EQUAL_STRING("up.wav", seq.files[0]);
}

void test_sound_down_sets_needs_music(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 1);
    sound_map_resolve(SOUND_DOWN, floor, &seq);
    TEST_ASSERT_EQUAL_INT(1, seq.valid);
    TEST_ASSERT_EQUAL_INT(1, seq.needs_music);
    TEST_ASSERT_EQUAL_INT(1, seq.count);
    TEST_ASSERT_EQUAL_STRING("down.wav", seq.files[0]);
}

/* ─── SOUND_CLOSING / SOUND_OPENING ─────────────────────────────────────── */

void test_sound_closing(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 1);
    sound_map_resolve(SOUND_CLOSING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(1, seq.valid);
    TEST_ASSERT_EQUAL_INT(0, seq.needs_music);
    TEST_ASSERT_EQUAL_INT(1, seq.count);
    TEST_ASSERT_EQUAL_STRING("closing.wav", seq.files[0]);
}

void test_sound_opening(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 1);
    sound_map_resolve(SOUND_OPENING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(1, seq.valid);
    TEST_ASSERT_EQUAL_INT(0, seq.needs_music);
    TEST_ASSERT_EQUAL_INT(1, seq.count);
    TEST_ASSERT_EQUAL_STRING("opening.wav", seq.files[0]);
}

/* ─── SOUND_OVERLOAD ─────────────────────────────────────────────────────── */

void test_sound_overload(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 1);
    sound_map_resolve(SOUND_OVERLOAD, floor, &seq);
    TEST_ASSERT_EQUAL_INT(1, seq.valid);
    TEST_ASSERT_EQUAL_INT(1, seq.count);
    TEST_ASSERT_EQUAL_STRING("overload.wav", seq.files[0]);
}

/* ─── SOUND_FIRE_ALARM (Фаза 5: fire.wav) ───────────────────────────────── */

void test_sound_fire_alarm_uses_fire_wav(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 1);
    sound_map_resolve(SOUND_FIRE_ALARM, floor, &seq);
    TEST_ASSERT_EQUAL_INT(1, seq.valid);
    TEST_ASSERT_EQUAL_INT(1, seq.count);
    /* Критическая проверка: должен быть fire.wav, не g_triple.wav */
    TEST_ASSERT_EQUAL_STRING("fire.wav", seq.files[0]);
}

/* ─── SOUND_DONT_WORK ────────────────────────────────────────────────────── */

void test_sound_dont_work(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 1);
    sound_map_resolve(SOUND_DONT_WORK, floor, &seq);
    TEST_ASSERT_EQUAL_INT(1, seq.valid);
    TEST_ASSERT_EQUAL_INT(1, seq.count);
    TEST_ASSERT_EQUAL_STRING("g_double.wav", seq.files[0]);
}

/* ─── SOUND_BUTTON (Фаза 5: button.wav) ─────────────────────────────────── */

void test_sound_button_uses_button_wav(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 1);
    sound_map_resolve(SOUND_BUTTON, floor, &seq);
    TEST_ASSERT_EQUAL_INT(1, seq.valid);
    TEST_ASSERT_EQUAL_INT(1, seq.count);
    /* Критическая проверка: должен быть button.wav, не g_single.wav */
    TEST_ASSERT_EQUAL_STRING("button.wav", seq.files[0]);
}

/* ─── SOUND_DING — обычные этажи ─────────────────────────────────────────── */

void test_ding_floor_1(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 1);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(1, seq.valid);
    TEST_ASSERT_EQUAL_INT(0, seq.needs_music); /* DING не запускает музыку */
    TEST_ASSERT_EQUAL_INT(2, seq.count);
    TEST_ASSERT_EQUAL_STRING("1.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[1]);
}

void test_ding_floor_5(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 5);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(2, seq.count);
    TEST_ASSERT_EQUAL_STRING("5.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[1]);
}

void test_ding_floor_20(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 20);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(2, seq.count);
    TEST_ASSERT_EQUAL_STRING("20.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[1]);
}

void test_ding_floor_21(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 21);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(3, seq.count);
    TEST_ASSERT_EQUAL_STRING("20-.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("1.wav", seq.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[2]);
}

void test_ding_floor_25(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 25);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(3, seq.count);
    TEST_ASSERT_EQUAL_STRING("20-.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("5.wav", seq.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[2]);
}

void test_ding_floor_29(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 29);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(3, seq.count);
    TEST_ASSERT_EQUAL_STRING("20-.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("9.wav", seq.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[2]);
}

void test_ding_floor_30(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 30);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(2, seq.count);
    TEST_ASSERT_EQUAL_STRING("30.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[1]);
}

void test_ding_floor_31(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 31);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(3, seq.count);
    TEST_ASSERT_EQUAL_STRING("30-.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("1.wav", seq.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[2]);
}

void test_ding_floor_39(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 39);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(3, seq.count);
    TEST_ASSERT_EQUAL_STRING("30-.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("9.wav", seq.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[2]);
}

void test_ding_floor_40(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 40);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(2, seq.count);
    TEST_ASSERT_EQUAL_STRING("40.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[1]);
}

/* ─── SOUND_DING — этажи 41–49 (Фаза 5: новый диапазон) ────────────────── */

void test_ding_floor_41(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 41);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(3, seq.count);
    TEST_ASSERT_EQUAL_STRING("40-.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("1.wav", seq.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[2]);
}

void test_ding_floor_45(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 45);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(3, seq.count);
    TEST_ASSERT_EQUAL_STRING("40-.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("5.wav", seq.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[2]);
}

void test_ding_floor_49(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 49);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(3, seq.count);
    TEST_ASSERT_EQUAL_STRING("40-.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("9.wav", seq.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[2]);
}

void test_ding_floor_50_uses_fallback(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 50);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(1, seq.count);
    TEST_ASSERT_EQUAL_STRING("g_triple.wav", seq.files[0]);
}

/* ─── SOUND_DING — подвалы ───────────────────────────────────────────────── */

void test_ding_basement(void)
{
    /* П (подвал без номера) */
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_BASEMENT, 0);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(2, seq.count);
    TEST_ASSERT_EQUAL_STRING("podval.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[1]);
}

void test_ding_basement_1(void)
{
    /* П1 */
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_BASEMENT_N, 1);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(3, seq.count);
    TEST_ASSERT_EQUAL_STRING("1.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("podval.wav", seq.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[2]);
}

void test_ding_basement_3(void)
{
    /* П3 */
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_BASEMENT_N, 3);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(3, seq.count);
    TEST_ASSERT_EQUAL_STRING("3.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("podval.wav", seq.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[2]);
}

/* ─── SOUND_DING — отрицательные этажи ──────────────────────────────────── */

void test_ding_negative_1(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NEGATIVE, 1);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(3, seq.count);
    TEST_ASSERT_EQUAL_STRING("minus.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("1.wav", seq.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[2]);
}

void test_ding_negative_5(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NEGATIVE, 5);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(3, seq.count);
    TEST_ASSERT_EQUAL_STRING("minus.wav", seq.files[0]);
    TEST_ASSERT_EQUAL_STRING("5.wav", seq.files[1]);
    TEST_ASSERT_EQUAL_STRING("floor.wav", seq.files[2]);
}

/* ─── SOUND_DING — неизвестный этаж ─────────────────────────────────────── */

void test_ding_unknown_floor_uses_fallback(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_UNKNOWN, 0);
    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(1, seq.count);
    TEST_ASSERT_EQUAL_STRING("g_single.wav", seq.files[0]);
}

/* ─── sound_map_volume_percent ───────────────────────────────────────────── */

void test_volume_none_returns_zero(void)
{
    TEST_ASSERT_EQUAL_INT(0, sound_map_volume_percent(SOUND_NONE, 75));
}

void test_volume_event_returns_sound_vol(void)
{
    TEST_ASSERT_EQUAL_INT(75, sound_map_volume_percent(SOUND_DING, 75));
    TEST_ASSERT_EQUAL_INT(50, sound_map_volume_percent(SOUND_UP, 50));
    TEST_ASSERT_EQUAL_INT(100, sound_map_volume_percent(SOUND_OVERLOAD, 100));
    TEST_ASSERT_EQUAL_INT(25, sound_map_volume_percent(SOUND_FIRE_ALARM, 25));
    TEST_ASSERT_EQUAL_INT(70, sound_map_volume_percent(SOUND_BUTTON, 70));
}

void test_volume_zero_config(void)
{
    /* 0% конфиг → NONE возвращает 0, всё остальное тоже 0 */
    TEST_ASSERT_EQUAL_INT(0, sound_map_volume_percent(SOUND_NONE, 0));
    TEST_ASSERT_EQUAL_INT(0, sound_map_volume_percent(SOUND_DING, 0));
}

/* ─── Инвариант: needs_music = 0 для всего кроме UP/DOWN ────────────────── */

void test_needs_music_only_for_up_down(void)
{
    audio_sequence_t seq;
    floor_t floor = make_floor(FLOOR_TYPE_NORMAL, 5);

    sound_map_resolve(SOUND_DING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(0, seq.needs_music);

    sound_map_resolve(SOUND_CLOSING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(0, seq.needs_music);

    sound_map_resolve(SOUND_OPENING, floor, &seq);
    TEST_ASSERT_EQUAL_INT(0, seq.needs_music);

    sound_map_resolve(SOUND_OVERLOAD, floor, &seq);
    TEST_ASSERT_EQUAL_INT(0, seq.needs_music);

    sound_map_resolve(SOUND_FIRE_ALARM, floor, &seq);
    TEST_ASSERT_EQUAL_INT(0, seq.needs_music);

    sound_map_resolve(SOUND_DONT_WORK, floor, &seq);
    TEST_ASSERT_EQUAL_INT(0, seq.needs_music);

    sound_map_resolve(SOUND_BUTTON, floor, &seq);
    TEST_ASSERT_EQUAL_INT(0, seq.needs_music);

    /* Только UP и DOWN имеют needs_music = 1 */
    sound_map_resolve(SOUND_UP, floor, &seq);
    TEST_ASSERT_EQUAL_INT(1, seq.needs_music);

    sound_map_resolve(SOUND_DOWN, floor, &seq);
    TEST_ASSERT_EQUAL_INT(1, seq.needs_music);
}

/* ─── main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    /* SOUND_NONE */
    RUN_TEST(test_sound_none_gives_invalid_sequence);

    /* UP / DOWN */
    RUN_TEST(test_sound_up_sets_needs_music);
    RUN_TEST(test_sound_down_sets_needs_music);

    /* CLOSING / OPENING */
    RUN_TEST(test_sound_closing);
    RUN_TEST(test_sound_opening);

    /* OVERLOAD */
    RUN_TEST(test_sound_overload);

    /* Фаза 5 исправления */
    RUN_TEST(test_sound_fire_alarm_uses_fire_wav);
    RUN_TEST(test_sound_dont_work);
    RUN_TEST(test_sound_button_uses_button_wav);

    /* DING — обычные этажи */
    RUN_TEST(test_ding_floor_1);
    RUN_TEST(test_ding_floor_5);
    RUN_TEST(test_ding_floor_20);
    RUN_TEST(test_ding_floor_21);
    RUN_TEST(test_ding_floor_25);
    RUN_TEST(test_ding_floor_29);
    RUN_TEST(test_ding_floor_30);
    RUN_TEST(test_ding_floor_31);
    RUN_TEST(test_ding_floor_39);
    RUN_TEST(test_ding_floor_40);

    /* DING — Фаза 5: новый диапазон 41–49 */
    RUN_TEST(test_ding_floor_41);
    RUN_TEST(test_ding_floor_45);
    RUN_TEST(test_ding_floor_49);
    RUN_TEST(test_ding_floor_50_uses_fallback);

    /* DING — подвалы */
    RUN_TEST(test_ding_basement);
    RUN_TEST(test_ding_basement_1);
    RUN_TEST(test_ding_basement_3);

    /* DING — отрицательные */
    RUN_TEST(test_ding_negative_1);
    RUN_TEST(test_ding_negative_5);

    /* DING — UNKNOWN */
    RUN_TEST(test_ding_unknown_floor_uses_fallback);

    /* volume */
    RUN_TEST(test_volume_none_returns_zero);
    RUN_TEST(test_volume_event_returns_sound_vol);
    RUN_TEST(test_volume_zero_config);

    /* needs_music инвариант */
    RUN_TEST(test_needs_music_only_for_up_down);

    return UNITY_END();
}
