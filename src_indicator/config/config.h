/**
 * @file src/config/config.h
 * @brief Конфигурация приложения.
 *
 * Три источника:
 *   nku_scheme.toml  — параметры, синхронизируемые с STM32 (не трогаем)
 *   video.toml       — параметры окна omxplayer
 *   renderer.toml    — позиции слотов DispmanX, директория ресурсов (Phase 4)
 */
#pragma once

#define UART_PORT_MAX 64

typedef struct uart_config_s
{
    char port[UART_PORT_MAX];
    int baudrate;
} uart_config_t;

#include "renderer/renderer.h" /* renderer_config_t */

/* ─── Дефолты — используются в config_load при отсутствии файла ──────────── */

#define CONFIG_DEFAULT_SOUND_VOLUME 70
#define CONFIG_DEFAULT_MUSIC_VOLUME 50
#define CONFIG_DEFAULT_LOAD_IDX     0

#define CONFIG_DEFAULT_VIDEO_WIN_X 0
#define CONFIG_DEFAULT_VIDEO_WIN_Y 0
#define CONFIG_DEFAULT_VIDEO_WIN_W 600
#define CONFIG_DEFAULT_VIDEO_WIN_H 1024

#define CONFIG_DEFAULT_UART_PORT "/dev/ttyAMA0"
#define CONFIG_DEFAULT_UART_BAUD 115200
/* ─── Единая структура конфигурации ───────────────────────────────────────── */

typedef struct config_s
{
    /* ── nku_scheme.toml ──────────────────────────────────────────────────── */
    int sound_volume_percent; /**< Громкость звука (0–100)        */
    int music_volume_percent; /**< Громкость музыки (0–100)       */
    int load_capacity_idx;    /**< Индекс иконки грузоподъёмности */

    /* ── video.toml ───────────────────────────────────────────────────────── */
    int video_win_x; /**< Позиция окна omxplayer X (px) */
    int video_win_y; /**< Позиция окна omxplayer Y (px) */
    int video_win_w; /**< Ширина окна omxplayer (px)    */
    int video_win_h; /**< Высота окна omxplayer (px)    */

    /* ── renderer.toml (Phase 4) ──────────────────────────────────────────── */
    renderer_config_t rdr;
} config_t;

/* ─── API ────────────────────────────────────────────────────────────────── */

/**
 * config_load — загрузить nku_scheme.toml.
 * При отсутствии файла — дефолты, возврат -1.
 */
int config_load(const char *p_path, config_t *p_cfg);

/**
 * video_config_load — загрузить video.toml.
 * Заполняет только поля video_win_* в cfg.
 */
int video_config_load(const char *p_path, config_t *p_cfg);

/**
 * renderer_config_load — загрузить renderer.toml.
 * Заполняет renderer_config_t напрямую (не весь config_t).
 * Дефолты: позиции из indicator.h, resources_dir=/home/pi/indicator/resources.
 * @return 0 при успехе, -1 при ошибке / отсутствии файла.
 */
int renderer_config_load(const char *p_path, renderer_config_t *p_cfg);

int uart_config_load(const char *p_path, uart_config_t *p_cfg);