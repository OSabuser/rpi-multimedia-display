/**
 * src/domain/floor.c
 */

#include "domain/floor.h"

/**
 * Вспомогательная функция: является ли код символа подвальной буквой П.
 * STM32 может присылать как заглавную CHAR_PI_CYR (17),
 * так и строчную CHAR_pi_cyr (19) — обе обозначают подвальный этаж.
 */
static int is_pi_char(char_code_t c)
{
    return (c == CHAR_PI_CYR) || (c == CHAR_pi_cyr);
}

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

    /* ── Подвал без номера: left=BLANK, right=П (заглавная или строчная) ── */
    if (left == CHAR_BLANK && is_pi_char(right))
    {
        f.type   = FLOOR_TYPE_BASEMENT;
        f.number = 0;
        return f;
    }

    /* ── Подвальный уровень П1–П9 ────────────────────────────────────────── */
    /*    left=П (любой регистр), right=цифра 1–9                            */
    /*    right=0 (П0) → не существует → UNKNOWN (см. ниже)                 */
    if (is_pi_char(left) && right >= CHAR_1 && right <= CHAR_9)
    {
        f.type   = FLOOR_TYPE_BASEMENT_N;
        f.number = (int) (right - CHAR_0);
        return f;
    }

    /* ── Отрицательный этаж -1..-9: left=MINUS, right=цифра 1–9 ─────────── */
    /*    right=0 (-0) → валидное состояние STM32, но не озвучивается        */
    /*    → UNKNOWN → g_triple (см. ниже)                                    */
    if (left == CHAR_MINUS && right >= CHAR_1 && right <= CHAR_9)
    {
        f.type   = FLOOR_TYPE_NEGATIVE;
        f.number = (int) (right - CHAR_0);
        return f;
    }

    /* ── Всё остальное — UNKNOWN ─────────────────────────────────────────── */
    /*    Включает: П0 (left=П, right=0), -0 (left=MINUS, right=0),         */
    /*    а также любые другие комбинации char_code_t.                       */
    /*    Звук: g_triple.wav                                                 */
    f.type = FLOOR_TYPE_UNKNOWN;
    return f;
}
