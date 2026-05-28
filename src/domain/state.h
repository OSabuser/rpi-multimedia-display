/**
 * src/domain/state.h
 *
 * Машина состояний индикатора.
 *
 * Хранит: left_char, right_char, arrow, mode.
 * НЕ хранит: sound — он edge-triggered (STM32 сбрасывает в 0 сразу после события).
 *
 * state_apply_frame() возвращает битовую маску изменений; main.c решает,
 * что обновлять на дисплее и какой звук воспроизвести.
 */

#pragma once

#include "protocol/types.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Состояние индикатора
 * ──────────────────────────────────────────────────────────────────────────── */
typedef struct
{
    char_code_t left_char;  /* текущий левый символ          */
    char_code_t right_char; /* текущий правый символ         */
    arrow_t arrow;          /* текущее направление           */
    mode_t mode;            /* текущий режим                 */
    int initialized;        /* 0 = ещё не получали ни одного фрейма */
} indicator_state_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Результат применения фрейма
 * ──────────────────────────────────────────────────────────────────────────── */
typedef struct
{
    int floor_changed;   /* left_char или right_char изменились */
    int arrow_changed;   /* arrow изменился                     */
    int mode_changed;    /* mode изменился                      */
    int sound_triggered; /* sound != SOUND_NONE (edge event)    */
    int first_frame;     /* первый фрейм после инициализации    */
} state_update_result_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * API
 * ──────────────────────────────────────────────────────────────────────────── */

/** Инициализировать состояние (обнулить, initialized=0). */
void state_init(indicator_state_t *state);

/**
 * state_apply_frame — применить новый фрейм к состоянию.
 *
 * При первом вызове (initialized=0): все флаги изменения = 1, first_frame = 1.
 * При последующих: флаг выставляется только если значение действительно изменилось.
 * sound_triggered всегда отражает frame->sound != SOUND_NONE.
 *
 * Чистая функция (без побочных эффектов кроме изменения *state).
 */
state_update_result_t state_apply_frame(indicator_state_t *state, const parsed_frame_t *frame);