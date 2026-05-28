/**
 * src/protocol/parser.h
 *
 * Двухуровневый парсер протокола MU:
 *   1. protocol_parse_frame()   — бинарный фрейм (sync/size/opcode/data/CRC/sync2)
 *   2. protocol_parse_payload() — текстовая нагрузка "#STM:L%d:R%d:A%d:S%d:M%d:E#"
 *
 */

#pragma once

#include "types.h"

#include <stddef.h>
#include <stdint.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Константы бинарного протокола
 * ──────────────────────────────────────────────────────────────────────────── */
#define MU_SYNC1                  0xAAu
#define MU_SYNC2                  0xBBu
#define MU_OPCODE_ELEVATOR_STATUS 0xDAu

/** Накладные расходы фрейма: sync1(1)+size(1)+opcode(1)+crc_h(1)+crc_l(1)+sync2(1) */
#define MU_FRAME_OVERHEAD 6u

/** Максимальный размер поля data (по спецификации: 0..255) */
#define MU_DATA_MAX 255u

/* ─────────────────────────────────────────────────────────────────────────────
 * Бинарный фрейм — результат protocol_parse_frame()
 * ──────────────────────────────────────────────────────────────────────────── */
typedef struct
{
    uint8_t opcode;
    uint8_t data[MU_DATA_MAX + 1u]; /* +1: null-терминатор для строковых операций */
    uint8_t data_len;
} mu_frame_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Коды результата парсинга
 * ──────────────────────────────────────────────────────────────────────────── */
typedef enum
{
    PARSE_OK = 0, /* успех                                          */
    PARSE_NEED_MORE_DATA = 1, /* фрейм неполный, ждать следующих байт           */
    PARSE_ERROR_SYNC1 = 2, /* байт sync1 (0xAA) не найден                   */
    PARSE_ERROR_SYNC2 = 3, /* байт sync2 (0xBB) не совпал                   */
    PARSE_ERROR_CRC   = 4, /* CRC не совпадает                               */
    PARSE_ERROR_PAYLOAD = 5, /* неверный формат текстовой нагрузки             */
    PARSE_ERROR_RANGE = 6 /* значение поля вне допустимого диапазона        */
} parse_result_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * API
 * ──────────────────────────────────────────────────────────────────────────── */

/**
 * protocol_crc16 — CRC-16/CCITT-FALSE
 *
 *   poly=0x1021, init=0xFFFF, RefIn=false, RefOut=false, XorOut=0x0000
 *   Контрольное значение ("123456789") = 0x29B1
 *
 * CRC вычисляется от: [opcode][data[0]..data[N-1]]
 */
uint16_t protocol_crc16(const uint8_t *data, size_t len);

/**
 * protocol_parse_frame — разобрать один бинарный фрейм из буфера.
 *
 * @param buf      буфер с входными байтами
 * @param len      число байт в буфере
 * @param out      [out] заполненный mu_frame_t при PARSE_OK
 * @param consumed [out] число байт, которые следует убрать из буфера
 *
 * Алгоритм:
 *   1. Сканировать до sync1=0xAA (весь мусор до него включается в consumed).
 *   2. Проверить достаточность буфера для полного фрейма.
 *   3. Проверить sync2.
 *   4. Вычислить и сравнить CRC16 (от [opcode || data]).
 *   5. Заполнить out, выставить consumed = start + frame_len.
 *
 * При PARSE_NEED_MORE_DATA: consumed = кол-во байт мусора до sync1.
 * При ошибках sync2/CRC: consumed = start+1 (продвинуться за плохой sync1).
 * При отсутствии sync1: consumed = len (весь буфер — мусор).
 */
parse_result_t protocol_parse_frame(const uint8_t *buf, size_t len, mu_frame_t *out,
                                    size_t *consumed);

/**
 * protocol_parse_payload — разобрать текстовую нагрузку opcode=0xDA.
 *
 * Ожидаемый формат: "#STM:L%d:R%d:A%d:S%d:M%d:E#\r\n"
 *
 * Возвращает PARSE_OK или PARSE_ERROR_PAYLOAD / PARSE_ERROR_RANGE.
 */
parse_result_t protocol_parse_payload(const mu_frame_t *frame, parsed_frame_t *out);