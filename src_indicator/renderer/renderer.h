/**
 * @file src/renderer/renderer.h
 * @brief Публичный API рендерера — платформонезависимый заголовок.
 *
 * Реализация скрыта за опаковым указателем renderer_t.
 * Никакие типы DispmanX / bcm_host.h в этот заголовок не проникают.
 *
 * Вариант A (слот-ориентированный): main.c сам решает что и когда показывать.
 * Рендерер — тупой слой без доменной логики.
 */
#pragma once

#include "protocol/types.h" /* char_code_t */

/* ─── Конфигурация рендерера ──────────────────────────────────────────────── */

#define RENDERER_RESOURCES_DIR_MAX 256U

typedef struct renderer_config_s
{
    char resources_dir[RENDERER_RESOURCES_DIR_MAX]; /**< Путь к папке resources/  */

    /* Позиции слотов (левый верхний угол PNG в пикселях) */
    int digit_left_x;
    int digit_left_y;
    int digit_right_x;
    int digit_right_y;
    int arrow_x;
    int arrow_y;
    int weight_x;
    int weight_y;
    int notif_x; /**< позиция SPRITE_NOTIFICATION по X (px от левого края) */
    int notif_y; /**< позиция SPRITE_NOTIFICATION по Y (px от верхнего края) */
} renderer_config_t;

/* ─── Слоты ──────────────────────────────────────────────────────────────── */

/**
 * Идентификатор DispmanX-слота.
 *
 * Порядок соответствует z-слоям:
*   BACKGROUND   Z=2  — BACK.png (всегда виден, 1080×1920)
*   MODE         Z=3  — mode-иконка (1080×1920) или скрыт
*   WEIGHT       Z=4  — load_N.png (размер уточняется, ожидает PNG)
*   DIGIT_LEFT   Z=4  — font renderer CalSans260 (буфер 450×239) ← fast_update
*   DIGIT_RIGHT  Z=4  — reserved, не используется (рисуется в DIGIT_LEFT); до F6
*   ARROW        Z=4  — arrows/_.png (размер уточняется, ожидает PNG) ← fast_update
*   NOTIFICATION Z=5  — USB-баннер (1080×270)
 */
typedef enum sprite_slot_e
{
    SPRITE_BACKGROUND   = 0,
    SPRITE_MODE         = 1,
    SPRITE_WEIGHT       = 2,
    SPRITE_DIGIT_LEFT   = 3,
    SPRITE_DIGIT_RIGHT  = 4,
    SPRITE_ARROW        = 5,
    SPRITE_NOTIFICATION = 6,
    SPRITE_SLOT_COUNT   = 7,
} sprite_slot_t;

/* ─── Opaque handle ──────────────────────────────────────────────────────── */

typedef struct renderer_s renderer_t;

/* ─── API ────────────────────────────────────────────────────────────────── */

/**
 * renderer_create — открыть DispmanX display (0), вызвать bcm_host_init().
 *
 * @param cfg  Конфигурация (копируется внутрь).
 * @return     Указатель на renderer_t или NULL при ошибке.
 */
renderer_t *renderer_create(const renderer_config_t *p_cfg);

/**
 * renderer_destroy — уничтожить все слоты, закрыть display, вызвать bcm_host_deinit().
 * Безопасен при NULL.
 */
void renderer_destroy(renderer_t *p_r);

/**
 * renderer_show_png — показать PNG-файл в заданном слоте.
 *
 * fast_update слоты (DIGIT_LEFT, DIGIT_RIGHT, ARROW):
 *   Первый вызов:    loadPng → createResource → addElement
 *   Последующие:     loadPng → changeSourceImageLayer  (без мигания, та же RAM)
 *
 * slow слоты (BACKGROUND, MODE, WEIGHT, NOTIFICATION):
 *   Любой вызов:     destroyImageLayer (если был) → loadPng → createResource → addElement
 *
 * @param path  Абсолютный путь к PNG-файлу.
 */
void renderer_show_png(renderer_t *p_r, sprite_slot_t slot, const char *p_path);

/**
 * renderer_hide — скрыть слот (destroyImageLayer если initialized).
 * Безопасен при уже скрытом слоте.
 */
void renderer_hide(renderer_t *p_r, sprite_slot_t slot);

/**
 * renderer_keepalive — отправить пустой DispmanX update.
 *
 * Вызывать из watchdog каждые ~30 с.
 *
 * Проблема (P-28): VideoCore IV переходит в dormant state при отсутствии
 * DispmanX-активности. omxplayer продолжает декодировать, но кадры не
 * рендерятся на экран. Пустой update_submit_sync пробуждает compositor.
 *
 * Безопасен при NULL-указателе.
 */
void renderer_keepalive(renderer_t *p_r);

/**
* renderer_show_digit — отрисовать номер этажа через font renderer (CalSans260).
*
* Принимает два кода символа от STM32 (left, right), строит UTF-8 строку,
* рендерит в ARGB8888-буфер 450×239 px, выводит в слот SPRITE_DIGIT_LEFT.
* SPRITE_DIGIT_RIGHT не используется на HD-варианте.
*
* Поведение:
*   - Оба CHAR_BLANK → renderer_hide(SPRITE_DIGIT_LEFT), слот скрыт.
*   - Символы без глифа в CalSans260 → пропускаются (не вызывают ошибку).
*   - Строка центрируется по X внутри буфера 450 px.
*   - fast_update: первый вызов создаёт ресурс, последующие обновляют пиксели.
*
* @param r      renderer из renderer_create()
* @param left   код левого символа  (char_code_t, types.h)
* @param right  код правого символа (char_code_t, types.h)
*/
void renderer_show_digit(renderer_t *p_r, char_code_t left, char_code_t right);
