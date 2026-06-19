/**
 * @file src_indicator/app_render.c
 * @brief Обновление DispmanX-слотов по событиям лифта и диспетчера.
 *
 * Функции получают указатель на app_t и делегируют вызовы в renderer.h.
 * Логика выбора PNG по режиму сосредоточена в mode_to_rel_path().
 */
#define _GNU_SOURCE

#include "app_private.h"
#include "renderer/renderer.h"

#include <stdio.h>
#include <syslog.h>

/* ─── Маппинг mode → относительный путь к PNG ───────────────────────────── */

/**
 * mode_to_rel_path — вернуть путь к PNG относительно resources_dir.
 *
 * @return относительный путь, или NULL если слот нужно скрыть.
 */
static const char *mode_to_rel_path(indicator_mode_t mode)
{
    switch (mode)
    {
    case MODE_FIRE_ALARM:
        return "modes/firealarm.png";
    case MODE_MALFUNCTION:
        return "modes/malfunction.png";
    case MODE_LOADING:
        return "modes/loading.png";
    case MODE_OVERLOAD:
        return "modes/overload.png";
    case MODE_SEIS_ALARM:
        return "modes/seismo.png";
    case MODE_FIREMANS:
        return "modes/fireman.png";
    case MODE_SERVICE:
        return "modes/inspection.png";
    case MODE_EVACUATION:
        return "modes/evacuation.png";
    case MODE_UPS_MALFUNCTION:
        return "modes/ups_malfunction.png";
    case MODE_DISPATCH_CALL:
        return "modes/calling.png";
    case MODE_DISPATCH_ANSWER:
        return "modes/talking.png";
    default:
        return NULL; /* MODE_NORMAL, MODE_CONN_LOST → hide */
    }
}

/* ─── Публичный API ──────────────────────────────────────────────────────── */

void renderer_apply_mode(app_t *p_app, indicator_mode_t mode)
{
    const char *rd  = p_app->cfg.rdr.resources_dir;
    const char *rel = mode_to_rel_path(mode);
    char path[512];

    if (rel != NULL)
    {
        (void) snprintf(path, sizeof(path), "%s/%s", rd, rel);
        renderer_show_png(p_app->renderer, SPRITE_MODE, path);
    }
    else
    {
        renderer_hide(p_app->renderer, SPRITE_MODE);
    }
}

void renderer_apply_dispatch(app_t *p_app, dispatch_state_t dispatch)
{
    if (dispatch != DISPATCH_OFF)
    {
        indicator_mode_t mode =
            (dispatch == DISPATCH_CALL) ? MODE_DISPATCH_CALL : MODE_DISPATCH_ANSWER;
        renderer_apply_mode(p_app, mode);
    }
    else
    {
        /* Dispatch закончился — восстановить mode из последнего 0xDA фрейма */
        if (p_app->state.initialized)
        {
            renderer_apply_mode(p_app, p_app->state.mode);
        }
        else
        {
            renderer_hide(p_app->renderer, SPRITE_MODE);
        }
    }
}

void renderer_apply_elevator(app_t *p_app, const state_update_result_t *p_upd,
                             const parsed_frame_t *p_payload)
{
    renderer_t *rdr = p_app->renderer;
    const char *rd  = p_app->cfg.rdr.resources_dir;
    char path[512];

    /* ── Цифры этажа ──────────────────────────────────────────────────── */
    if (p_upd->floor_changed || p_upd->first_frame)
    {
        renderer_show_digit(rdr, p_payload->left_char, p_payload->right_char);
    }

    /* ── Стрелка направления ──────────────────────────────────────────── */
    if (p_upd->arrow_changed || p_upd->first_frame)
    {
        switch (p_payload->arrow)
        {
        case ARROW_UP:
            (void) snprintf(path, sizeof(path), "%s/arrows/up.png", rd);
            renderer_show_png(rdr, SPRITE_ARROW, path);
            break;
        case ARROW_DOWN:
            (void) snprintf(path, sizeof(path), "%s/arrows/down.png", rd);
            renderer_show_png(rdr, SPRITE_ARROW, path);
            break;
        default: /* ARROW_NONE, ARROW_BOTH → скрыть */
            renderer_hide(rdr, SPRITE_ARROW);
            break;
        }
    }

    /* ── Режим: только если диспетчер не активен ──────────────────────── */
    if ((p_upd->mode_changed || p_upd->first_frame) && p_app->state.active_dispatch == DISPATCH_OFF)
    {
        renderer_apply_mode(p_app, p_payload->mode);
    }
}