/**
 * @file tools/notif_test.c
 * @brief Интерактивный тест SPRITE_NOTIFICATION на живом дисплее.
 *
 * Показывает все PNG-уведомления поверх работающего indicator
 * (останавливать indicator не нужно — у каждого процесса свой renderer_t).
 *
 * Использование: ./notif_test [DIR]
 *   DIR — директория с PNG (default: /data/resources/notifications)
 */

#include "config/config.h"
#include "renderer/renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* ─── Константы ──────────────────────────────────────────────────────────── */

#define NOTIF_DIR_DEFAULT "/data/resources/notifications"
#define NOTIF_PATH_MAX    512U

/** Время отображения одного уведомления (секунды) */
static const unsigned int SHOW_SECS = 2U;

/** Пауза между уведомлениями (секунды) */
static const unsigned int PAUSE_SECS = 1U;

/* 8 — в списке разрешённых значений readability-magic-numbers */
static const int NOTIF_COUNT = 8;

/* ─── Таблица уведомлений ────────────────────────────────────────────────── */

typedef struct notif_entry_s
{
    const char *filename; /**< имя PNG-файла в директории notifications/ */
    const char *label; /**< ожидаемый текст на экране (для протокола)  */
} notif_entry_t;

static const notif_entry_t NOTIF_ENTRIES[] = {
    { "notif_found.png", "Найдены видеофайлы" },
    { "notif_processing.png", "Идёт обработка..." },
    { "notif_success.png", "Успех!" },
    { "notif_no_video.png", "Нет видеофайлов на носителе" },
    { "notif_eject.png", "Извлеките носитель" },
    { "notif_error.png", "Ошибка обработки" },
    { "notif_no_mcu.png", "Нет связи с MCU" },
    { "notif_mcu_ok.png", "Связь с MCU установлена" },
};

/* ─── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *p_dir = (argc > 1) ? argv[1] : NOTIF_DIR_DEFAULT;

    renderer_config_t cfg;
    if (renderer_config_load("/data/pi_nku_configs/renderer.toml", &cfg) < 0)
    {
        (void) fprintf(stderr, "notif_test: renderer.toml not found, using defaults\n");
    }

    renderer_t *p_r = renderer_create(&cfg);
    if (p_r == NULL)
    {
        (void) fprintf(stderr, "notif_test: renderer_create failed\n");
        return EXIT_FAILURE;
    }

    (void) printf("\nnotif_test: dir=%s  show=%us  pause=%us\n\n", p_dir, SHOW_SECS, PAUSE_SECS);

    char path[NOTIF_PATH_MAX];
    int ok = 0;

    for (int i = 0; i < NOTIF_COUNT; i++)
    {
        (void) snprintf(path, sizeof(path), "%s/%s", p_dir, NOTIF_ENTRIES[i].filename);
        (void) printf("  [%d/%d]  %-28s  \"%s\"\n", i + 1, NOTIF_COUNT, NOTIF_ENTRIES[i].filename,
                      NOTIF_ENTRIES[i].label);
        (void) fflush(stdout);

        renderer_show_png(p_r, SPRITE_NOTIFICATION, path);
        (void) sleep(SHOW_SECS);
        renderer_hide(p_r, SPRITE_NOTIFICATION);
        (void) sleep(PAUSE_SECS);
        ok++;
    }

    renderer_destroy(p_r);

    (void) printf("\nnotif_test: %d/%d OK\n", ok, NOTIF_COUNT);
    return EXIT_SUCCESS;
}