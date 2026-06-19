/*
 * fonts.c
 *
 *  Created on: 3 июл. 2024 г.
 *      Author: Dmitry Akimov
 */

#include "fonts.h"

#include <stdint.h>
#include <string.h>

/* ── Продвижение курсора внутри глифа ───────────────────────────────────── */

/*
 * Глиф обходится построчно: col растёт слева направо,
 * при достижении width — сбрасывается в 0, row++.
 */
static inline void advance_pixel(uint32_t *col, uint32_t *row, uint32_t width)
{
    (*col)++;
    if (*col >= width)
    {
        *col = 0U;
        (*row)++;
    }
}

/* ── Отрисовка одного глифа ──────────────────────────────────────────────── */

static draw_result_t draw_char(const tFont *font, uint16_t character, uint16_t x_pos,
                               uint16_t y_pos, font_draw_pixel_fn draw_pixel_fn)
{
    const tImage *image = NULL;

    /* Бинарный поиск по codepoint (символы отсортированы по возрастанию) */
    int lo = 0;
    int hi = (int) font->length - 1;

    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;

        if (font->chars[mid].code == (long int) character)
        {
            image = font->chars[mid].image;
            break;
        }
        else if (font->chars[mid].code < (long int) character)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }

    if (image == NULL || image->data == NULL)
    {
        return DRAW_ERR_NOT_FOUND;
    }

    const uint32_t total_pixels = (uint32_t) image->width * image->height;
    const uint32_t UNIQUE_MASK  = 0xFFFFFF00U;

    uint32_t in_idx  = 0U;
    uint32_t out_len = 0U;
    uint32_t col     = 0U;
    uint32_t row     = 0U;

    /* RLE декомпрессия */
    while (out_len < total_pixels)
    {
        uint32_t header = image->data[in_idx++];

        if ((header & UNIQUE_MASK) == UNIQUE_MASK)
        {
            /* UNIQUE: следующие len пикселей уникальны */
            uint32_t len = 0x100U - (header & 0xFFU);

            for (uint32_t i = 0U; i < len && out_len < total_pixels; i++, in_idx++)
            {
                uint32_t px = image->data[in_idx];

                /*
         * Координаты в ЛОГИЧЕСКОМ порядке: (x_pos + col, y_pos + row).
         * Трансформ под ориентацию выполняет compositor в draw_pixel_fn.
         *
         * До рефакторинга здесь был своп: draw_pixel(y+row, x+col, ...)
         * — он убран. Трансформ переехал в hal_compositor_draw_pixel().
         */
                draw_pixel_fn((uint16_t) (x_pos + col), (uint16_t) (y_pos + row), px);

                advance_pixel(&col, &row, image->width);
                out_len++;
            }
        }
        else
        {
            /* REPEATABLE: один пиксель повторяется len раз */
            uint32_t len = header & 0xFFFFU;
            uint32_t px  = image->data[in_idx++];

            for (uint32_t i = 0U; i < len && out_len < total_pixels; i++)
            {
                draw_pixel_fn((uint16_t) (x_pos + col), (uint16_t) (y_pos + row), px);

                advance_pixel(&col, &row, image->width);
                out_len++;
            }
        }
    }

    return (out_len >= total_pixels) ? DRAW_OK : DRAW_ERR_CORRUPT_DATA;
}

/* ── Ширина одного символа ───────────────────────────────────────────────── */

static uint16_t get_char_width(const tFont *font, uint16_t character)
{
    /* Линейный поиск — вызывается редко и таблица небольшая */
    for (uint32_t i = 0U; i < font->length; i++)
    {
        if (font->chars[i].code == (long int) character)
        {
            return (font->chars[i].image != NULL) ? font->chars[i].image->width : 0U;
        }
    }
    return 0U;
}

/* ── Декодирование UTF-8 кодпоинта ──────────────────────────────────────── */

/*
 * Кириллица в UTF-8: двухбайтовые последовательности 0xD0xx / 0xD1xx.
 * Функция читает из string начиная с позиции *idx и сдвигает *idx вперёд
 * на число прочитанных байт (1 или 2).
 */
static uint16_t read_codepoint(const char *string, uint8_t *idx)
{
    uint8_t byte = (uint8_t) string[*idx];

    if (byte == 0xD0U || byte == 0xD1U)
    {
        /* Двухбайтовый UTF-8 (кириллица) */
        uint16_t cp = ((uint16_t) byte << 8U) | (uint8_t) string[*idx + 1U];
        *idx += 2U;
        return cp;
    }

    /* Однобайтовый ASCII */
    *idx += 1U;
    return (uint16_t) byte;
}

/* ── Публичный API ───────────────────────────────────────────────────────── */

void draw_string(const tFont *font, const char *string, uint16_t x_pos, uint16_t y_pos,
                 font_draw_pixel_fn draw_pixel_fn)
{
    if (font == NULL || string == NULL || draw_pixel_fn == NULL)
    {
        return;
    }

    uint8_t len = (uint8_t) strlen(string);
    uint16_t x  = x_pos;
    uint8_t i   = 0U;

    while (i < len)
    {
        uint16_t cp    = read_codepoint(string, &i);
        uint16_t width = get_char_width(font, cp);

        draw_char(font, cp, x, y_pos, draw_pixel_fn);

        x = (uint16_t) (x + width);
    }
}

uint16_t get_string_width(const tFont *font, const char *string)
{
    if (font == NULL || string == NULL)
    {
        return 0U;
    }

    uint8_t len    = (uint8_t) strlen(string);
    uint16_t total = 0U;
    uint8_t i      = 0U;

    while (i < len)
    {
        uint16_t cp = read_codepoint(string, &i);
        total       = (uint16_t) (total + get_char_width(font, cp));
    }

    return total;
}