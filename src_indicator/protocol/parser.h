/**
 * src/protocol/parser.h
 *
 * Двухуровневый парсер протокола MU:
 *   1. protocol_parse_frame()    — бинарный фрейм (sync/size/opcode/data/CRC/sync2)
 *   2. protocol_parse_payload()  — payload opcode=0xDA: "#STM:L%d:R%d:A%d:S%d:M%d:E#"
 *   3. protocol_parse_dispatch() — payload opcode=0xAA: "DISPATCH CALL/ANSWER/OFF\r\n"
 */

#pragma once

#include "types.h"

#include <stddef.h>
#include <stdint.h>

/* ─── Константы бинарного протокола ──────────────────────────────────────── */

#define MU_SYNC1                  0xAAu
#define MU_SYNC2                  0xBBu
#define MU_OPCODE_ELEVATOR_STATUS 0xDAu

/**
 * Опкод диспетчерской связи.
 *
 * Примечание: значение совпадает с MU_SYNC1 (0xAA), но занимает другую
 * позицию в кадре (byte[2] = OPCODE, а не byte[0] = SYNC1),
 * поэтому парсер работает корректно.
 */
#define MU_OPCODE_DISPATCH 0xAAu

/** Накладные расходы: sync1(1)+size(1)+opcode(1)+crc_h(1)+crc_l(1)+sync2(1) */
#define MU_FRAME_OVERHEAD 6u

/** Максимальный размер поля data */
#define MU_DATA_MAX 255u

/* ─── Бинарный фрейм ─────────────────────────────────────────────────────── */

typedef struct mu_frame_s
{
    uint8_t opcode;
    uint8_t data[MU_DATA_MAX + 1U]; /* +1: null-терминатор */
    uint8_t data_len;
} mu_frame_t;

/* ─── Коды результата парсинга ───────────────────────────────────────────── */

typedef enum parse_result_e
{
    PARSE_OK             = 0,
    PARSE_NEED_MORE_DATA = 1,
    PARSE_ERROR_SYNC1    = 2,
    PARSE_ERROR_SYNC2    = 3,
    PARSE_ERROR_CRC      = 4,
    PARSE_ERROR_PAYLOAD  = 5,
    PARSE_ERROR_RANGE    = 6
} parse_result_t;

/* ─── API ────────────────────────────────────────────────────────────────── */

/**
 * CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF, RefIn=false, RefOut=false.
 * Контрольное значение ("123456789") = 0x29B1.
 * Вычисляется от: [opcode][data[0]..data[N-1]].
 */
uint16_t protocol_crc16(const uint8_t *p_data, size_t len);

/**
 * Разобрать один бинарный фрейм из буфера.
 * CRC: little-endian [LO][HI] — как в STM32 MU_tx_frame_create.
 */
parse_result_t protocol_parse_frame(const uint8_t *p_buf, size_t len, mu_frame_t *p_out,
                                    size_t *p_consumed);

/**
 * Разобрать payload opcode=0xDA.
 * Формат: "#STM:L%d:R%d:A%d:S%d:M%d:E#\r\n"
 */
parse_result_t protocol_parse_payload(const mu_frame_t *p_frame, parsed_frame_t *p_out);

/**
 * Разобрать payload opcode=0xAA (диспетчерская связь).
 *
 * Ожидаемые строки:
 *   "DISPATCH CALL\r\n"   → DISPATCH_CALL
 *   "DISPATCH ANSWER\r\n" → DISPATCH_ANSWER
 *   "DISPATCH OFF\r\n"    → DISPATCH_OFF
 *
 * Возвращает PARSE_OK или PARSE_ERROR_PAYLOAD.
 */
parse_result_t protocol_parse_dispatch(const mu_frame_t *p_frame, dispatch_state_t *p_out);