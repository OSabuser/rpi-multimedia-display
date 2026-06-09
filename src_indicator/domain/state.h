/**
 * src/domain/state.h
 *
 * Машина состояний индикатора.
 *
 * Хранит два независимых контекста:
 *
 *   1. Состояние лифта (из opcode=0xDA):
 *      left_char, right_char, arrow, mode
 *
 *   2. Состояние диспетчерской связи (из opcode=0xAA):
 *      active_dispatch
 *
 * Приоритет отображения:
 *   active_dispatch != DISPATCH_OFF → показывать dispatch-иконку,
 *                                     игнорировать mode из 0xDA для рендера
 *   active_dispatch == DISPATCH_OFF → показывать mode из 0xDA
 *
 * Решение о том что именно рендерить принимает main.c на основе флагов
 * state_update_result_t — state.c только фиксирует изменения.
 */

#pragma once

#include "protocol/types.h"

/* ─── Состояние индикатора ────────────────────────────────────────────────── */

typedef struct indicator_state_s
{
    /* Состояние лифта (opcode=0xDA) */
    char_code_t left_char;
    char_code_t right_char;
    arrow_t arrow;
    indicator_mode_t mode;
    int initialized; /* 0 = ни одного 0xDA фрейма не получено */

    /* Состояние диспетчерской связи (opcode=0xAA) */
    dispatch_state_t active_dispatch;
} indicator_state_t;

/* ─── Результат применения фрейма ────────────────────────────────────────── */

typedef struct state_update_result_s
{
    /* Изменения от opcode=0xDA */
    int floor_changed;   /* left_char или right_char изменились */
    int arrow_changed;   /* arrow изменился                     */
    int mode_changed;    /* mode изменился                      */
    int sound_triggered; /* sound != SOUND_NONE (edge event)    */
    int first_frame;     /* первый 0xDA фрейм после старта      */

    /* Изменения от opcode=0xAA */
    int dispatch_changed; /* active_dispatch изменился           */
} state_update_result_t;

/* ─── API ────────────────────────────────────────────────────────────────── */

/** Инициализировать состояние. */
void state_init(indicator_state_t *p_state);

/**
 * state_apply_frame — применить фрейм opcode=0xDA к состоянию лифта.
 *
 * При первом вызове (initialized=0): все флаги = 1, first_frame = 1.
 * При последующих: флаг только если значение изменилось.
 * sound_triggered всегда = (frame->sound != SOUND_NONE).
 */
state_update_result_t state_apply_frame(indicator_state_t *p_state, const parsed_frame_t *p_frame);

/**
 * state_apply_dispatch — применить payload opcode=0xAA к состоянию диспетчера.
 *
 * Возвращает dispatch_changed=1 если значение изменилось.
 * Не зависит от initialized (диспетчер независим от состояния лифта).
 */
state_update_result_t state_apply_dispatch(indicator_state_t *p_state, dispatch_state_t dispatch);
