/**
 * @file platform/dispmanx/font_renderer.c
 * @brief Адаптер fonts.c → плоский ARGB8888-буфер для DispmanX.
 *
 * Реализует font_render_string() и font_measure_string() из font_renderer.h.
 *
 * Механизм:
 *   draw_string() из fonts.c принимает коллбэк font_draw_pixel_fn —
 *   указатель на функцию void(uint16_t x, uint16_t y, uint32_t color)
 *   без параметра контекста. Текущий целевой буфер хранится в статической
 *   переменной s_render_ctx, которая устанавливается непосредственно перед
 *   вызовом draw_string() и обнуляется сразу после.
 *
 * Ограничение реентерабельности:
 *   Indicator — однопоточный, рендеринг выполняется синхронно в event-loop.
 *   Вложенные вызовы font_render_string() невозможны. Ограничение принято.
 *
 * Формат пикселей: ARGB8888 premultiplied (A==R==G==B для белого).
 *   Значение 0xFFFFFFFF — непрозрачный белый.
 *   Значение 0x00000000 — полностью прозрачный (фон).
 *   Конвертация не нужна: DispmanX VC_IMAGE_ARGB8888 + FROM_SOURCE
 *   принимает premultiplied напрямую.
 */

#define _GNU_SOURCE

#include "font_renderer.h"

#include "fonts/fonts.h"

/* ─── Именованные константы для RLE-маски (magic numbers запрещены) ─────── */

/* Смещение X целевого буфера при текущем вызове font_render_string() */
/* (хранится рядом с контекстом для удобства draw_pixel_to_target)    */

/* ─── Статический контекст текущего рендера ─────────────────────────────── */

/** Текущий целевой буфер. NULL вне активного вызова font_render_string(). */
static font_render_target_t *s_render_ctx;

/** X-смещение текущей строки внутри буфера (px). */
static uint32_t s_render_x;

/** Y-смещение текущей строки внутри буфера (px). */
static uint32_t s_render_y;

/* ─── Коллбэк для fonts.c ────────────────────────────────────────────────── */

/**
 * draw_pixel_to_target — записать пиксель ARGB8888 в статический буфер.
 *
 * Сигнатура соответствует font_draw_pixel_fn из fonts.h.
 * Координаты (x, y) — логические внутри глифа, начало координат —
 * левый верхний угол строки. Смещение s_render_x / s_render_y
 * добавляется здесь, приводя к абсолютным координатам в буфере.
 *
 * Полностью прозрачные пиксели (color == 0) не записываются:
 * фон буфера уже обнулён вызывающим (calloc / memset).
 *
 * Пиксели вне границ буфера молча пропускаются.
 */
static void draw_pixel_to_target(uint16_t x, uint16_t y, uint32_t color)
{
    if (s_render_ctx == NULL)
    {
        return;
    }

    if (color == 0U)
    {
        return;
    }

    uint32_t abs_x = s_render_x + (uint32_t) x;
    uint32_t abs_y = s_render_y + (uint32_t) y;

    if (abs_x >= s_render_ctx->width || abs_y >= s_render_ctx->height)
    {
        return;
    }

    s_render_ctx->pixels[abs_y * s_render_ctx->stride + abs_x] = color;
}

/* ─── Публичный API ──────────────────────────────────────────────────────── */

draw_result_t font_render_string(const tFont *p_font, const char *p_str, uint32_t x, uint32_t y,
                                 font_render_target_t *p_target)
{
    if (p_font == NULL || p_str == NULL || p_target == NULL || p_target->pixels == NULL)
    {
        return DRAW_ERR_NOT_FOUND;
    }

    s_render_ctx = p_target;
    s_render_x   = x;
    s_render_y   = y;

    draw_string(p_font, p_str, 0U, 0U, draw_pixel_to_target);

    s_render_ctx = NULL;

    return DRAW_OK;
}

uint32_t font_measure_string(const tFont *p_font, const char *p_str)
{
    return (uint32_t) get_string_width(p_font, p_str);
}