/**
 * src/protocol/parser.c
 */

#include "parser.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * CRC-16/CCITT-FALSE
 * ──────────────────────────────────────────────────────────────────────────── */
uint16_t protocol_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t) ((uint16_t) data[i] << 8u);
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc & 0x8000u) ? (uint16_t) ((crc << 1u) ^ 0x1021u) : (uint16_t) (crc << 1u);
        }
    }
    return crc;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * protocol_parse_frame
 * ──────────────────────────────────────────────────────────────────────────── */
parse_result_t protocol_parse_frame(const uint8_t *buf, size_t len, mu_frame_t *out,
                                    size_t *consumed)
{
    *consumed = 0U;

    size_t start = 0U;
    while (start < len && buf[start] != MU_SYNC1)
    {
        start++;
    }

    if (start == len)
    {
        *consumed = len;
        return PARSE_ERROR_SYNC1;
    }
    if (len - start < 3U)
    {
        *consumed = start;
        return PARSE_NEED_MORE_DATA;
    }

    uint8_t data_size = buf[start + 1U];
    size_t frame_len  = (size_t) MU_FRAME_OVERHEAD + (size_t) data_size;

    if (len - start < frame_len)
    {
        *consumed = start;
        return PARSE_NEED_MORE_DATA;
    }
    if (buf[start + frame_len - 1U] != MU_SYNC2)
    {
        *consumed = start + 1U;
        return PARSE_ERROR_SYNC2;
    }

    uint8_t opcode          = buf[start + 2U];
    const uint8_t *data_ptr = &buf[start + 3U];

    uint8_t crc_input[1U + MU_DATA_MAX];
    crc_input[0U] = opcode;
    if (data_size > 0U)
    {
        memcpy(crc_input + 1U, data_ptr, data_size);
    }

    uint16_t crc_calc = protocol_crc16(crc_input, 1U + (size_t) data_size);
    uint16_t crc_recv =
        ((uint16_t) buf[start + 4U + data_size] << 8U) | (uint16_t) buf[start + 3U + data_size];

    if (crc_calc != crc_recv)
    {
        *consumed = start + 1U;
        return PARSE_ERROR_CRC;
    }

    out->opcode   = opcode;
    out->data_len = data_size;
    if (data_size > 0U)
    {
        memcpy(out->data, data_ptr, data_size);
    }
    out->data[data_size] = 0U;

    *consumed = start + frame_len;
    return PARSE_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * protocol_parse_payload
 * Формат: "#STM:L%d:R%d:A%d:S%d:M%d:E#\r\n"
 * ──────────────────────────────────────────────────────────────────────────── */
parse_result_t protocol_parse_payload(const mu_frame_t *frame, parsed_frame_t *out)
{
    if (frame->opcode != MU_OPCODE_ELEVATOR_STATUS)
    {
        return PARSE_ERROR_PAYLOAD;
    }

    const char *s = (const char *) frame->data;

    if (frame->data_len < 5U || strncmp(s, "#STM:", 5) != 0)
    {
        return PARSE_ERROR_PAYLOAD;
    }

    static const char PREFIXES[5] = { 'L', 'R', 'A', 'S', 'M' };
    int values[5];
    const char *p = s + 5;

    for (int i = 0; i < 5; i++)
    {
        if (*p != PREFIXES[i])
        {
            return PARSE_ERROR_PAYLOAD;
        }
        p++;
        char *end;
        long val = strtol(p, &end, 10);
        if (end == p)
        {
            return PARSE_ERROR_PAYLOAD;
        }
        values[i] = (int) val;
        p         = end;
        if (i < 4)
        {
            if (*p != ':')
            {
                return PARSE_ERROR_PAYLOAD;
            }
            p++;
        }
    }

    if (p[0] != ':' || p[1] != 'E' || p[2] != '#')
    {
        return PARSE_ERROR_PAYLOAD;
    }

    /* Валидация диапазонов */
    if (values[0] < 0 || values[0] > (int) CHAR_CODE_MAX)
    {
        return PARSE_ERROR_RANGE;
    }

    if (values[1] < 0 || values[1] > (int) CHAR_CODE_MAX)
    {
        return PARSE_ERROR_RANGE;
    }

    if (values[2] < 0 || values[2] > (int) ARROW_CODE_MAX)
    {
        return PARSE_ERROR_RANGE;
    }

    if (values[3] < 0 || values[3] > (int) SOUND_CODE_MAX)
    {
        return PARSE_ERROR_RANGE;
    }

    /* mode_t: нельзя проверять <= MAX из-за разрыва 0–9, 100, 101, 255 */
    if (!mode_is_valid(values[4]))
    {
        return PARSE_ERROR_RANGE;
    }

    out->left_char  = (char_code_t) values[0];
    out->right_char = (char_code_t) values[1];
    out->arrow      = (arrow_t) values[2];
    out->sound      = (sound_t) values[3];
    out->mode       = (indicator_mode_t) values[4];

    return PARSE_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 *
 * Payload opcode=0xAA, формат: "DISPATCH CALL\r\n" и т.д.
 * STM32 может включать null-терминатор в data_len (как в 0xDA payload).
 * Сравниваем по длине строки без '\0' — нечувствительно к наличию нуля.
 * ──────────────────────────────────────────────────────────────────────────── */
parse_result_t protocol_parse_dispatch(const mu_frame_t *p_frame, dispatch_state_t *p_out)
{
    static const char STR_CALL[]   = "DISPATCH CALL\r\n";
    static const char STR_ANSWER[] = "DISPATCH ANSWER\r\n";
    static const char STR_OFF[]    = "DISPATCH OFF\r\n";

    /* Размер строк без null-терминатора */
    static const size_t LEN_CALL   = 15U; /* strlen("DISPATCH CALL\r\n")   */
    static const size_t LEN_ANSWER = 17U; /* strlen("DISPATCH ANSWER\r\n") */
    static const size_t LEN_OFF    = 14U; /* strlen("DISPATCH OFF\r\n")    */

    if (p_frame->opcode != MU_OPCODE_DISPATCH)
    {
        return PARSE_ERROR_PAYLOAD;
    }

    const char *p_s = (const char *) p_frame->data;

    if (p_frame->data_len >= (uint8_t) LEN_CALL && strncmp(p_s, STR_CALL, LEN_CALL) == 0)
    {
        *p_out = DISPATCH_CALL;
        return PARSE_OK;
    }
    if (p_frame->data_len >= (uint8_t) LEN_ANSWER && strncmp(p_s, STR_ANSWER, LEN_ANSWER) == 0)
    {
        *p_out = DISPATCH_ANSWER;
        return PARSE_OK;
    }
    if (p_frame->data_len >= (uint8_t) LEN_OFF && strncmp(p_s, STR_OFF, LEN_OFF) == 0)
    {
        *p_out = DISPATCH_OFF;
        return PARSE_OK;
    }

    return PARSE_ERROR_PAYLOAD;
}
