/*
 * fonts.h
 *
 *  Created on: 3 июл. 2024 г.
 *      Author: Dmitry Akimov
 */

#ifndef FONTS_FONTS_H_
#define FONTS_FONTS_H_

#include <stddef.h>
#include <stdint.h>

/* ── Типы данных шрифта ──────────────────────────────────────────────────── */

typedef struct
{
    const uint32_t *data; /* RLE-сжатые данные глифа             */
    uint16_t width;
    uint16_t height;
    uint8_t dataSize;
} tImage;

typedef struct
{
    long int code; /* Unicode codepoint                   */
    const tImage *image;
} tChar;

typedef struct
{
    uint8_t length;
    const tChar *chars;
} tFont;

/* ── Коды возврата ───────────────────────────────────────────────────────── */

typedef enum
{
    DRAW_OK = 0,
    DRAW_ERR_NOT_FOUND,
    DRAW_ERR_OUT_OF_BOUNDS,
    DRAW_ERR_CORRUPT_DATA,
} draw_result_t;

/* ── Коллбэк отрисовки пикселя ───────────────────────────────────────────── */

/*
 * Функция отрисовки одного пикселя в ЛОГИЧЕСКИХ координатах.
 *
 * Реализуется на стороне compositor: hal_compositor_get_draw_pixel_fn().
 * Логические координаты (x, y) — левый верхний угол = (0, 0).
 * Трансформ под ориентацию дисплея выполняется внутри compositor,
 * fonts.c о нём ничего не знает.
 *
 * color — ARGB8888: 0xFF000000 = непрозрачный чёрный.
 *
 * Типичный вызов:
 *   draw_string(&JBMono24, "22", 50, 150, hal_compositor_get_draw_pixel_fn());
 */
typedef void (*font_draw_pixel_fn)(uint16_t x, uint16_t y, uint32_t color);

/* ── API ─────────────────────────────────────────────────────────────────── */

/**
 * @brief Нарисовать строку через коллбэк draw_pixel.
 *
 * Перебирает символы строки (UTF-8, кириллица через двухбайтовый codepoint),
 * ищет глиф бинарным поиском, декомпрессирует RLE и вызывает draw_pixel_fn
 * для каждого непрозрачного пикселя.
 *
 * Координаты — логические, трансформ не выполняется.
 * Выход за пределы canvas — ответственность draw_pixel_fn (no-op).
 *
 * @param font          шрифт
 * @param string        строка UTF-8 (NULL — no-op)
 * @param x_pos         логическая X-координата левого края первого символа
 * @param y_pos         логическая Y-координата верхнего края
 * @param draw_pixel_fn коллбэк отрисовки пикселя (NULL — no-op)
 */
void draw_string(const tFont *font, const char *string, uint16_t x_pos, uint16_t y_pos,
                 font_draw_pixel_fn draw_pixel_fn);

/**
 * @brief Получить ширину строки в пикселях (без отрисовки).
 *
 * Используется для центрирования и проверки выхода за пределы canvas.
 *
 * @return ширина в пикселях; 0 если string == NULL или шрифт не содержит символ
 */
uint16_t get_string_width(const tFont *font, const char *string);

#endif /* FONTS_FONTS_H_ */