/**
 * tests/test_config.c
 *
 * Unit-тесты для config_load().
 * Тест-данные пишутся во временный файл /tmp/test_indicator_config.toml.
 */

#include "config/config.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

#define TEST_TOML_PATH "/tmp/test_indicator_config.toml"

void setUp(void)
{
}
void tearDown(void)
{
    remove(TEST_TOML_PATH);
}

/* ── Записать тестовый TOML в файл ───────────────────────────────────────── */
static int write_toml(const char *content)
{
    FILE *f = fopen(TEST_TOML_PATH, "w");
    if (!f)
        return -1;
    fputs(content, f);
    fclose(f);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Базовый нормальный путь
 * ──────────────────────────────────────────────────────────────────────────── */

static void test_config_basic(void)
{
    const char *toml = "[soundvolume]\n"
                       "name = \"Test\"\n"
                       "possible_values = [\n"
                       "\"0%\",\n\"25%\",\n\"50%\",\n\"75%\",\n\"100%\"\n"
                       "]\n"
                       "default = \"50%\"\n"
                       "current = \"75%\"\n"
                       "\n"
                       "[musicvolume]\n"
                       "name = \"Test\"\n"
                       "possible_values = [\n"
                       "\"0%\",\n\"25%\",\n\"50%\",\n\"75%\",\n\"100%\"\n"
                       "]\n"
                       "default = \"0%\"\n"
                       "current = \"25%\"\n"
                       "\n"
                       "[loadcapacity]\n"
                       "name = \"Test\"\n"
                       "possible_values = [\n"
                       "\"NONE\",\n\"200kg\",\n\"400kg\",\n\"600kg\"\n"
                       "]\n"
                       "default = \"NONE\"\n"
                       "current = \"400kg\"\n";

    TEST_ASSERT_EQUAL(0, write_toml(toml));

    config_t cfg;
    int rc = config_load(TEST_TOML_PATH, &cfg);

    TEST_ASSERT_EQUAL(0, rc);
    TEST_ASSERT_EQUAL(75, cfg.sound_volume_percent);
    TEST_ASSERT_EQUAL(25, cfg.music_volume_percent);
    TEST_ASSERT_EQUAL(2, cfg.load_capacity_idx); /* "400kg" @ index 2 */
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Значения percent
 * ──────────────────────────────────────────────────────────────────────────── */

static void test_config_sound_0_percent(void)
{
    const char *toml = "[soundvolume]\ncurrent = \"0%\"\ndefault = \"50%\"\n"
                       "[musicvolume]\ncurrent = \"0%\"\ndefault = \"0%\"\n"
                       "[loadcapacity]\npossible_values = [\n\"NONE\"\n]\ncurrent = "
                       "\"NONE\"\ndefault = \"NONE\"\n";

    write_toml(toml);
    config_t cfg;
    config_load(TEST_TOML_PATH, &cfg);
    TEST_ASSERT_EQUAL(0, cfg.sound_volume_percent);
}

static void test_config_sound_100_percent(void)
{
    const char *toml = "[soundvolume]\ncurrent = \"100%\"\ndefault = \"50%\"\n"
                       "[musicvolume]\ncurrent = \"0%\"\ndefault = \"0%\"\n"
                       "[loadcapacity]\npossible_values = [\n\"NONE\"\n]\ncurrent = "
                       "\"NONE\"\ndefault = \"NONE\"\n";

    write_toml(toml);
    config_t cfg;
    config_load(TEST_TOML_PATH, &cfg);
    TEST_ASSERT_EQUAL(100, cfg.sound_volume_percent);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Fallback: current пустой → используется default
 * ──────────────────────────────────────────────────────────────────────────── */

static void test_config_fallback_to_default(void)
{
    /* current отсутствует — должен использоваться default */
    const char *toml = "[soundvolume]\ndefault = \"25%\"\n"
                       "[musicvolume]\ndefault = \"0%\"\n"
                       "[loadcapacity]\npossible_values = [\n\"A\",\n\"B\"\n]\ndefault = \"B\"\n";

    write_toml(toml);
    config_t cfg;
    config_load(TEST_TOML_PATH, &cfg);

    TEST_ASSERT_EQUAL(25, cfg.sound_volume_percent);
    TEST_ASSERT_EQUAL(0, cfg.music_volume_percent);
    TEST_ASSERT_EQUAL(1, cfg.load_capacity_idx); /* "B" @ index 1 */
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Файл не найден → дефолты
 * ──────────────────────────────────────────────────────────────────────────── */

static void test_config_file_not_found(void)
{
    config_t cfg;
    int rc = config_load("/tmp/DOES_NOT_EXIST_indicator_config.toml", &cfg);

    TEST_ASSERT_EQUAL(-1, rc);
    TEST_ASSERT_EQUAL(CONFIG_DEFAULT_SOUND_VOLUME, cfg.sound_volume_percent);
    TEST_ASSERT_EQUAL(CONFIG_DEFAULT_MUSIC_VOLUME, cfg.music_volume_percent);
    TEST_ASSERT_EQUAL(CONFIG_DEFAULT_LOAD_IDX, cfg.load_capacity_idx);
}

static void test_config_null_path(void)
{
    config_t cfg;
    int rc = config_load(NULL, &cfg);
    TEST_ASSERT_EQUAL(-1, rc);
    TEST_ASSERT_EQUAL(CONFIG_DEFAULT_SOUND_VOLUME, cfg.sound_volume_percent);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Файл с комментариями (как настоящий nku_scheme.toml)
 * ──────────────────────────────────────────────────────────────────────────── */

static void test_config_with_comments(void)
{
    const char *toml = "# nku_scheme.toml — конфиг устройства\n"
                       "\n"
                       "[soundvolume]\n"
                       "name = \"Громкость звука\" # loud\n"
                       "possible_values = [\n"
                       "\"0%\",\n"
                       "\"25%\",\n"
                       "\"50%\"\n"
                       "]\n"
                       "default = \"50%\"\n"
                       "current = \"50%\"\n"
                       "should_sync = \"true\"\n"
                       "\n"
                       "# Музыкальный блок\n"
                       "[musicvolume]\n"
                       "possible_values = [\n\"0%\",\n\"25%\"\n]\n"
                       "default = \"0%\"\n"
                       "current = \"25%\"\n"
                       "\n"
                       "[loadcapacity]\n"
                       "possible_values = [\n"
                       "\"HIDDEN\",\n"
                       "\"320kg\",\n"
                       "\"400kg\"\n"
                       "]\n"
                       "default = \"HIDDEN\"\n"
                       "current = \"320kg\"\n";

    write_toml(toml);
    config_t cfg;
    int rc = config_load(TEST_TOML_PATH, &cfg);

    TEST_ASSERT_EQUAL(0, rc);
    TEST_ASSERT_EQUAL(50, cfg.sound_volume_percent);
    TEST_ASSERT_EQUAL(25, cfg.music_volume_percent);
    TEST_ASSERT_EQUAL(1, cfg.load_capacity_idx); /* "320kg" @ index 1 */
}

/* ─────────────────────────────────────────────────────────────────────────────
 * loadcapacity: current не в массиве → дефолтный индекс 0
 * ──────────────────────────────────────────────────────────────────────────── */

static void test_config_load_idx_current_not_in_array(void)
{
    const char *toml = "[soundvolume]\ncurrent = \"50%\"\ndefault = \"50%\"\n"
                       "[musicvolume]\ncurrent = \"0%\"\ndefault = \"0%\"\n"
                       "[loadcapacity]\n"
                       "possible_values = [\n\"A\",\n\"B\",\n\"C\"\n]\n"
                       "current = \"UNKNOWN_VALUE\"\n" /* нет в массиве */
                       "default = \"A\"\n";

    write_toml(toml);
    config_t cfg;
    config_load(TEST_TOML_PATH, &cfg);
    /* Не найдено → остаётся CONFIG_DEFAULT_LOAD_IDX */
    TEST_ASSERT_EQUAL(CONFIG_DEFAULT_LOAD_IDX, cfg.load_capacity_idx);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Симуляция реального nku_scheme.toml (soundvolume=50%, musicvolume=25%)
 * ──────────────────────────────────────────────────────────────────────────── */

static void test_config_realistic_toml(void)
{
    const char *toml =
        "[groupnumber]\n"
        "name = \"Номер в группе\"\n"
        "possible_values = [\n\"0\",\n\"1\",\n\"2\"\n]\n"
        "default = \"1\"\ncurrent = \"1\"\nshould_sync = \"true\"\n"
        "\n"
        "[soundvolume]\n"
        "name = \"Громкость звука\"\n"
        "possible_values = [\n\"0%\",\n\"25%\",\n\"50%\",\n\"75%\",\n\"100%\"\n]\n"
        "default = \"50%\"\ncurrent = \"50%\"\nshould_sync = \"true\"\n"
        "\n"
        "[musicvolume]\n"
        "name = \"Громкость музыки\"\n"
        "possible_values = [\n\"0%\",\n\"25%\",\n\"50%\",\n\"75%\",\n\"100%\"\n]\n"
        "default = \"0%\"\ncurrent = \"25%\"\nshould_sync = \"true\"\n"
        "\n"
        "[loadcapacity]\n"
        "name = \"Грузоподъёмность\"\n"
        "possible_values = [\n"
        "\"СКРЫТО\",\n\"240кг 3чел.\",\n\"320кг 4чел.\",\n\"400кг 5чел.\",\n\"450кг 6чел.\"\n"
        "]\n"
        "default = \"СКРЫТО\"\ncurrent = \"320кг 4чел.\"\nshould_sync = \"true\"\n";

    write_toml(toml);
    config_t cfg;
    int rc = config_load(TEST_TOML_PATH, &cfg);

    TEST_ASSERT_EQUAL(0, rc);
    TEST_ASSERT_EQUAL(50, cfg.sound_volume_percent);
    TEST_ASSERT_EQUAL(25, cfg.music_volume_percent);
    TEST_ASSERT_EQUAL(2, cfg.load_capacity_idx); /* "320кг 4чел." @ index 2 */
}

/* ─────────────────────────────────────────────────────────────────────────────
 * main
 * ──────────────────────────────────────────────────────────────────────────── */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_config_basic);
    RUN_TEST(test_config_sound_0_percent);
    RUN_TEST(test_config_sound_100_percent);
    RUN_TEST(test_config_fallback_to_default);
    RUN_TEST(test_config_file_not_found);
    RUN_TEST(test_config_null_path);
    RUN_TEST(test_config_with_comments);
    RUN_TEST(test_config_load_idx_current_not_in_array);
    RUN_TEST(test_config_realistic_toml);

    return UNITY_END();
}
