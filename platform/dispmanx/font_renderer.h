/**
 * @file platform/dispmanx/font_renderer.h
 * @brief Адаптер fonts.c → плоский ARGB8888-буфер для DispmanX.
 *
 * Соединяет растровый рендерер шрифтов (fonts.c, callback-архитектура)
 * с DispmanX-слоями indicator. Вызывающий выделяет ARGB8888-буфер под
 * один DispmanX-ресурс, передаёт его в font_render_string() — функция
 * заполняет пиксели через draw_string() из fonts.c.
 *
 * Формат пикселей: ARGB8888 premultiplied (A==R==G==B для белого текста).
 * Совместим с DispmanX VC_IMAGE_ARGB8888 + DISPMANX_FLAGS_ALPHA_FROM_SOURCE
 * без конвертации.
 *
 * Ограничение: не реентерабелен. Indicator — однопоточный, рендеринг
 * синхронный в event-loop; ограничение несущественно.
 *
 * Зависимости компиляции:
 *   fonts/fonts.h  — tFont / draw_result_t / font_draw_pixel_fn
 */
#pragma once

#include "fonts/fonts.h"

#include <stdint.h>

/* ─── Целевой буфер ──────────────────────────────────────────────────────── */

/**
 * font_render_target_t — плоский ARGB8888-буфер, выделенный вызывающим.
 *
 * Раскладка: пиксель (x, y) → pixels[y * width + x], row-major.
 * Вызывающий обязан обнулить буфер перед вызовом — фон прозрачный.
 */
typedef struct font_render_target_s
{
    uint32_t *pixels; /**< ARGB8888-пиксели; size = width * height элементов */
    uint32_t width;  /**< ширина буфера в пикселях                           */
    uint32_t height; /**< высота буфера в пикселях                           */
} font_render_target_t;

/* ─── API ────────────────────────────────────────────────────────────────── */

/**
 * font_render_string — отрисовать UTF-8 строку в ARGB8888-буфер.
 *
 * Делегирует отрисовку в draw_string() из fonts.c через коллбэк
 * font_draw_pixel_fn. Поддерживает ASCII (0x20–0x7E) и кириллицу
 * (двухбайтовые UTF-8 последовательности 0xD0xx / 0xD1xx).
 *
 * Пиксели вне границ буфера молча пропускаются (bounds-check внутри).
 *
 * @param p_font    шрифт (например &PTMono215)
 * @param p_str     строка UTF-8, завершённая '\0'; NULL — no-op
 * @param x         X-смещение от левого края буфера (px)
 * @param y         Y-смещение от верхнего края буфера (px)
 * @param p_target  целевой буфер (должен быть обнулён вызывающим); NULL — no-op
 * @return          DRAW_OK при успехе
 */
draw_result_t font_render_string(const tFont *p_font, const char *p_str, uint32_t x, uint32_t y,
                                 font_render_target_t *p_target);

/**
 * font_measure_string — вычислить ширину строки в пикселях без отрисовки.
 *
 * Тонкая обёртка над get_string_width() из fonts.c с расширением типа
 * до uint32_t для совместимости с координатами DispmanX.
 *
 * @param p_font  шрифт
 * @param p_str   строка UTF-8; NULL → 0
 * @return        ширина в пикселях
 */
uint32_t font_measure_string(const tFont *p_font, const char *p_str);