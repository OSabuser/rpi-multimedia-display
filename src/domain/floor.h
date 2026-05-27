/**
 * src/domain/floor.h
 *
 * Декодирование номера этажа из пары кодов символов (L, R).
 * Не зависит от платформы — тестируется на хосте.
 *
 * Таблица декодирования (MASTER_PLAN §12):
 *   left=0–9,  right=0–9   → NORMAL,     number = left*10 + right
 *   left=BLANK,right=0–9   → NORMAL,     number = right  (1–9)
 *   left=BLANK,right=PI    → BASEMENT,   number = 0
 *   left=PI,   right=1–9   → BASEMENT_N, number = right  (П1–П9)
 *   left=MINUS,right=1–9   → NEGATIVE,   number = right  (-1..-9)
 *   иначе                  → UNKNOWN
 */

#pragma once

#include "protocol/types.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Тип этажа
 * ──────────────────────────────────────────────────────────────────────────── */
typedef enum {
    FLOOR_TYPE_NORMAL,     /* обычный: 0–99                             */
    FLOOR_TYPE_BASEMENT,   /* П  (подвал без номера)                    */
    FLOOR_TYPE_BASEMENT_N, /* П1–П9                                     */
    FLOOR_TYPE_NEGATIVE,   /* -1 .. -9                                  */
    FLOOR_TYPE_UNKNOWN     /* не удалось декодировать                   */
} floor_type_t;

/**
 * floor_t — декодированный этаж.
 *
 * Поле number:
 *   NORMAL:     значение этажа (0–99)
 *   BASEMENT:   0 (не используется)
 *   BASEMENT_N: номер подвального уровня (1–9)
 *   NEGATIVE:   модуль (1–9); реальный этаж = -number
 *   UNKNOWN:    0 (не используется)
 */
typedef struct {
    floor_type_t type;
    int          number;
} floor_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * API
 * ──────────────────────────────────────────────────────────────────────────── */

/**
 * floor_decode — декодировать пару кодов символов в структуру этажа.
 *
 * Чистая функция без побочных эффектов.
 */
floor_t floor_decode(char_code_t left, char_code_t right);