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
typedef enum char_code_e
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
    CHAR_PI_CYR     = 17, /* П (заглавная) */
    CHAR_P_LAT      = 18,
    CHAR_pi_cyr     = 19, /* п (строчная) — тоже подвальный этаж */
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
 * ──────────────────────────────────────────────────────────────────────────── */
typedef enum arrow_e
{
    ARROW_NONE = 0,
    ARROW_UP   = 1,
    ARROW_DOWN = 2,
    ARROW_BOTH = 3,

    ARROW_CODE_MAX = 3
} arrow_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Звуковые события (поле S, opcode=0xDA)
 * ──────────────────────────────────────────────────────────────────────────── */
typedef enum sound_e
{
    SOUND_NONE       = 0,
    SOUND_DING       = 1,
    SOUND_UP         = 2,
    SOUND_DOWN       = 3,
    SOUND_CLOSING    = 4,
    SOUND_OPENING    = 5,
    SOUND_OVERLOAD   = 6,
    SOUND_FIRE_ALARM = 7,
    SOUND_DONT_WORK  = 8,
    SOUND_BUTTON     = 9,

    SOUND_CODE_MAX = 9
} sound_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Режим работы (поле M, opcode=0xDA)
 * Значения НЕ образуют непрерывный диапазон.
 * ──────────────────────────────────────────────────────────────────────────── */
typedef enum indicator_mode_e
{
    MODE_NORMAL          = 0,
    MODE_FIRE_ALARM      = 1,
    MODE_MALFUNCTION     = 2,
    MODE_LOADING         = 3,
    MODE_OVERLOAD        = 4,
    MODE_SEIS_ALARM      = 5,
    MODE_FIREMANS        = 6,
    MODE_SERVICE         = 7,
    MODE_EVACUATION      = 8,
    MODE_UPS_MALFUNCTION = 9,
    MODE_DISPATCH_CALL   = 100,
    MODE_DISPATCH_ANSWER = 101,
    MODE_CONN_LOST       = 255
} indicator_mode_t;

/**
 * mode_is_valid — проверить допустимость числового значения поля M.
 */
static inline int mode_is_valid(int v)
{
    return (v >= 0 && v <= 9) || (v == 100) || (v == 101) || (v == 255);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Состояние диспетчерской связи (opcode=0xAA)
 *
 * Приоритет выше любого mode_t из opcode=0xDA.
 * Приходит строго однократно при изменении сигнальных входов.
 *
 * DISPATCH_OFF    → скрыть иконку диспетчера, вернуться к mode из 0xDA
 * DISPATCH_CALL   → показать иконку вызова диспетчера
 * DISPATCH_ANSWER → показать иконку ответа диспетчера
 * ──────────────────────────────────────────────────────────────────────────── */
typedef enum dispatch_state_e
{
    DISPATCH_OFF    = 0, /* нет активной диспетчерской связи */
    DISPATCH_CALL   = 1, /* вызов диспетчера                 */
    DISPATCH_ANSWER = 2  /* ответ диспетчера                 */
} dispatch_state_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Распарсенный фрейм — результат protocol_parse_payload() (opcode=0xDA)
 * ──────────────────────────────────────────────────────────────────────────── */
typedef struct parsed_frame_s
{
    char_code_t left_char;
    char_code_t right_char;
    arrow_t arrow;
    sound_t sound;
    indicator_mode_t mode;
} parsed_frame_t;