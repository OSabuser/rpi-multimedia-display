/**
 * src/domain/state.c
 */

#include "domain/state.h"

#include <string.h> /* memset */

void state_init(indicator_state_t *state)
{
    memset(state, 0, sizeof(*state));
    /* initialized = 0 — ждём первый фрейм */
}

state_update_result_t state_apply_frame(indicator_state_t *state, const parsed_frame_t *frame)
{
    state_update_result_t result;
    memset(&result, 0, sizeof(result));

    if (!state->initialized)
    {
        /* ── Первый фрейм: принять всё как есть, выставить все флаги ──── */
        state->left_char   = frame->left_char;
        state->right_char  = frame->right_char;
        state->arrow       = frame->arrow;
        state->mode        = frame->mode;
        state->initialized = 1;

        result.first_frame     = 1;
        result.floor_changed   = 1;
        result.arrow_changed   = 1;
        result.mode_changed    = 1;
        result.sound_triggered = (frame->sound != SOUND_NONE) ? 1 : 0;
        return result;
    }

    /* ── Последующие фреймы: выставить флаг только при реальном изменении ── */

    if (state->left_char != frame->left_char || state->right_char != frame->right_char)
    {
        result.floor_changed = 1;
        state->left_char     = frame->left_char;
        state->right_char    = frame->right_char;
    }

    if (state->arrow != frame->arrow)
    {
        result.arrow_changed = 1;
        state->arrow         = frame->arrow;
    }

    if (state->mode != frame->mode)
    {
        result.mode_changed = 1;
        state->mode         = frame->mode;
    }

    /* Sound — всегда edge-triggered, STM32 сбрасывает после события */
    result.sound_triggered = (frame->sound != SOUND_NONE) ? 1 : 0;

    return result;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * state_apply_dispatch — добавить в конец state.c
 * ──────────────────────────────────────────────────────────────────────────── */
state_update_result_t state_apply_dispatch(indicator_state_t *p_state, dispatch_state_t dispatch)
{
    state_update_result_t result;
    (void) memset(&result, 0, sizeof(result));

    if (p_state->active_dispatch != dispatch)
    {
        p_state->active_dispatch = dispatch;
        result.dispatch_changed  = 1;
    }

    return result;
}
