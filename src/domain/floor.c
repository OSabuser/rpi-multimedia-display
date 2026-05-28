/**
 * src/domain/floor.c
 */

#include "domain/floor.h"

floor_t floor_decode(char_code_t left, char_code_t right)
{
    floor_t f;
    f.number = 0;

    /* ── Двузначный этаж: оба символа — цифры 0–9 ───────────────────────── */
    if (left >= CHAR_0 && left <= CHAR_9 && right >= CHAR_0 && right <= CHAR_9)
    {
        f.type   = FLOOR_TYPE_NORMAL;
        f.number = (int) (left - CHAR_0) * 10 + (int) (right - CHAR_0);
        return f;
    }

    /* ── Однозначный этаж: left=BLANK, right=цифра 0–9 ──────────────────── */
    if (left == CHAR_BLANK && right >= CHAR_0 && right <= CHAR_9)
    {
        f.type   = FLOOR_TYPE_NORMAL;
        f.number = (int) (right - CHAR_0);
        return f;
    }

    /* ── Подвал без номера: left=BLANK, right=П ──────────────────────────── */
    if (left == CHAR_BLANK && right == CHAR_PI_CYR)
    {
        f.type   = FLOOR_TYPE_BASEMENT;
        f.number = 0;
        return f;
    }

    /* ── Подвальный уровень П1–П9: left=П, right=цифра 1–9 ──────────────── */
    if (left == CHAR_PI_CYR && right >= CHAR_1 && right <= CHAR_9)
    {
        f.type   = FLOOR_TYPE_BASEMENT_N;
        f.number = (int) (right - CHAR_0);
        return f;
    }

    /* ── Отрицательный этаж -1..-9: left=MINUS, right=цифра 1–9 ─────────── */
    if (left == CHAR_MINUS && right >= CHAR_1 && right <= CHAR_9)
    {
        f.type   = FLOOR_TYPE_NEGATIVE;
        f.number = (int) (right - CHAR_0);
        return f;
    }

    /* ── Всё остальное — неизвестно ──────────────────────────────────────── */
    f.type = FLOOR_TYPE_UNKNOWN;
    return f;
}