/**
 * src/protocol/types.h
 *
 * Типы домена. Enum-значения синхронизированы с STM32-прошивкой (PiMessage).
 */

#pragma once

#include <stdint.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Коды символов (поля L / R)
 * ──────────────────────────────────────────────────────────────────────────── */
typedef enum
{
    CHAR_0          = 0,
    CHAR_1          = 1,
    CHAR_2          = 2,
    CHAR_3          = 3,
    CHAR_4          = 4,
    CHAR_5          = 5,
    CHAR_6          = 6,
    CHAR_7          = 7,
    CHAR_8          = 8,
    CHAR_9          = 9,
    CHAR_A          = 10,
    CHAR_b          = 11,
    CHAR_C          = 12,
    CHAR_d          = 13,
    CHAR_E          = 14,
    CHAR_F          = 15,
    CHAR_BLANK      = 16,
    CHAR_PI_CYR     = 17,
    CHAR_P_LAT      = 18,
    CHAR_pi_cyr     = 19,
    CHAR_N_CYR      = 20,
    CHAR_U          = 21,
    CHAR_MINUS      = 22,
    CHAR_UNDERSCORE = 23,
    CHAR_u_lower    = 24,
    CHAR_L          = 25,
    CHAR_U_CYR      = 26,
    CHAR_B_CYR      = 27,
    CHAR_G_CYR      = 28,
    CHAR_R          = 29,
    CHAR_V          = 30,
    CHAR_N          = 31,
    CHAR_S          = 32,
    CHAR_K          = 33,
    CHAR_Y          = 34,
    CHAR_G          = 35,
    CHAR_B          = 36,
    CHAR_T          = 37,

    CHAR_CODE_MAX = 37
} char_code_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Направление / стрелка (поле A)
 * Источник: arrows_state_t в STM32 (подтверждено)
 * ──────────────────────────────────────────────────────────────────────────── */
typedef enum
{
    ARROW_NONE = 0, /* A_NO_ARROW    */
    ARROW_UP   = 1, /* A_ARROW_UP    */
    ARROW_DOWN = 2, /* A_ARROW_DOWN  */
    ARROW_BOTH = 3, /* A_ARROWS_BOTH */

    ARROW_CODE_MAX = 3
} arrow_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Звуковые события (поле S)
 * Источник: s_code_t в STM32
 *
 * STM32 выставляет ненулевой код на один фрейм, затем сбрасывает в 0.
 * ──────────────────────────────────────────────────────────────────────────── */
typedef enum
{
    SOUND_NONE       = 0, /* нет события                          */
    SOUND_DING       = 1, /* S_GONG        — прибытие, анонс этажа */
    SOUND_UP         = 2, /* S_MOVING_UP   — движение вверх        */
    SOUND_DOWN       = 3, /* S_MOVING_DOWN — движение вниз         */
    SOUND_CLOSING    = 4, /* S_DOORS_CLOSE — двери закрываются     */
    SOUND_OPENING    = 5, /* S_DOORS_OPEN  — двери открываются     */
    SOUND_OVERLOAD   = 6, /* S_OVERLOAD    — перегрузка            */
    SOUND_FIRE_ALARM = 7, /* S_FIRE_ALARM  — пожарная опасность    */
    SOUND_DONT_WORK  = 8, /* S_DONT_WORK   — лифт не работает      */
    SOUND_BUTTON     = 9, /* S_BUTTON      — нажатие кнопки        */

    SOUND_CODE_MAX = 9
} sound_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Режим работы (поле M)
 * Источник: el_mode_t в STM32
 *
 * Значения НЕ образуют непрерывный диапазон (0–9, 100, 101, 255).
 * Для валидации использовать mode_is_valid(), не сравнение с MAX.
 * ──────────────────────────────────────────────────────────────────────────── */
typedef enum
{
    MODE_NORMAL      = 0, /* M_NORMAL           — нормальная работа     */
    MODE_FIRE_ALARM  = 1, /* M_FIRE_ALARM       — пожарная опасность    */
    MODE_MALFUNCTION = 2, /* M_MALFUNCTION      — лифт не работает      */
    MODE_LOADING     = 3, /* M_LOADING          — погрузка              */
    MODE_OVERLOAD    = 4, /* M_OVERLOAD         — превышение нагрузки   */
    MODE_SEIS_ALARM  = 5, /* M_SEIS_ALARM       — сейсмическая опасность*/
    MODE_FIREMANS    = 6, /* M_FIREMANS         — перевозка пожарных    */
    MODE_SERVICE     = 7, /* M_SERVISE          — на обслуживании       */
    MODE_EVACUATION  = 8, /* M_EVACUATION       — эвакуация             */
    MODE_UPS_MALFUNCTION = 9,   /* M_UPS_MALFUNCTION  — неисправность ИБП     */
    MODE_DISPATCH_CALL   = 100, /* M_DISPATCH_CALL    — вызов диспетчера      */
    MODE_DISPATCH_ANSWER = 101, /* M_DISPATCH_ANSWER  — ответ диспетчера      */
    MODE_CONN_LOST       = 255  /* M_CONN_LOST        — потеря связи          */
} inndicator_mode_t;

/**
 * mode_is_valid — проверить допустимость числового значения поля M.
 * Значения mode_t не образуют непрерывный диапазон — нельзя проверять <= MAX.
 */
static inline int mode_is_valid(int v)
{
    return (v >= 0 && v <= 9) || (v == 100) || (v == 101) || (v == 255);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Распарсенный фрейм — результат protocol_parse_payload()
 * ──────────────────────────────────────────────────────────────────────────── */
typedef struct
{
    char_code_t left_char;
    char_code_t right_char;
    arrow_t arrow;
    sound_t sound;
    inndicator_mode_t mode;
} parsed_frame_t;
