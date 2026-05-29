/**
 * @file config.h
 * @brief Конфигурация индикатора.
 *
 * Два источника конфигурации:
 *   nku_scheme.toml — MCU-синхронизируемые параметры (звук, музыка, нагрузка).
 *   video.toml      — Pi-hardware параметры (окно воспроизведения видео).
 *
 * Оба файла парсятся в одну структуру config_t.
 * Отсутствие любого файла не является ошибкой — используются defaults.
 */

#pragma once

/* ─── Defaults ───────────────────────────────────────────────────────────── */

#define CONFIG_DEFAULT_SOUND_VOLUME 50 /* % */
#define CONFIG_DEFAULT_MUSIC_VOLUME 0  /* % */
#define CONFIG_DEFAULT_LOAD_IDX     0  /* индекс в possible_values: "СКРЫТО" */

#define CONFIG_DEFAULT_VIDEO_WIN_X 0
#define CONFIG_DEFAULT_VIDEO_WIN_Y 0
#define CONFIG_DEFAULT_VIDEO_WIN_W 600
#define CONFIG_DEFAULT_VIDEO_WIN_H 1024

/* ─── Структура конфигурации ─────────────────────────────────────────────── */

typedef struct config_s
{
    /* ── Из nku_scheme.toml ─────────────────────────────────────────────── */

    /** Громкость звуковых событий, % (0–100). */
    int sound_volume_percent;

    /** Громкость фоновой музыки, % (0–100). */
    int music_volume_percent;

    /**
     * Индекс выбранной грузоподъёмности в массиве possible_values секции
     * [loadcapacity]. 0 = "СКРЫТО".
     */
    int load_capacity_idx;

    /* ── Из video.toml ──────────────────────────────────────────────────── */

    /** Окно воспроизведения omxplayer: левый край, пиксели. */
    int video_win_x;

    /** Окно воспроизведения omxplayer: верхний край, пиксели. */
    int video_win_y;

    /** Окно воспроизведения omxplayer: ширина, пиксели. */
    int video_win_w;

    /** Окно воспроизведения omxplayer: высота, пиксели. */
    int video_win_h;
} config_t;

/* ─── API ────────────────────────────────────────────────────────────────── */

/**
 * config_load — загрузить nku_scheme.toml в структуру config_t.
 *
 * Всегда инициализирует *out дефолтами перед парсингом.
 *
 * @param path  путь к nku_scheme.toml; NULL допустим → только дефолты
 * @param out   заполняется результатом
 * @return      0 при успехе, -1 если файл не найден или path == NULL
 *              (в обоих случаях дефолты уже выставлены)
 */
int config_load(const char *path, config_t *out);

/**
 * video_config_load — загрузить video.toml и переопределить video_win_* поля.
 *
 * Вызывается ПОСЛЕ config_load (которая выставляет дефолты).
 * Файл не найден — не ошибка, дефолты остаются.
 * Частичный файл — только присутствующие ключи переопределяются.
 *
 * @param path  путь к video.toml; NULL допустим → ничего не меняется
 * @param out   структура config_t, уже инициализированная config_load
 * @return      0 при успехе, -1 если файл не найден или path == NULL
 */
int video_config_load(const char *path, config_t *out);