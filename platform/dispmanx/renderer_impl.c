/**
 * @file platform/dispmanx/renderer_impl.c
 * @brief DispmanX-реализация renderer.h.
 *
 * Зависимости (Pi-only):
 *   bcm_host.h          → /opt/vc/include/
 *   imageLayer.h/.c     → layers/ (Andrew Duncan, MIT)
 *   loadpng.h/.c        → layers/ (Andrew Duncan, MIT)
 *   image.h/.c          → layers/ (Andrew Duncan, MIT)
 *
 * Соглашения:
 *   fast_update = 1  → DIGIT_LEFT, DIGIT_RIGHT, ARROW
 *     Первый вызов:   loadPng → set rects → createResourceImageLayer → addElement
 *     Последующие:    destroyImage → loadPng → changeSourceImageLayer
 *     (PNG одинакового размера — гарантировано конфигом ресурсов)
 *
 *   fast_update = 0  → BACKGROUND, MODE, WEIGHT, NOTIFICATION
 *     Любой вызов:    destroyImageLayer (если был) → loadPng → createResource → addElement
 */
#define _GNU_SOURCE

#include "layers/imageLayer.h"
#include "layers/loadpng.h"
#include "renderer/renderer.h"

#include <bcm_host.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

/* ─── Свойства слотов (константы, не меняются) ───────────────────────────── */

typedef struct slot_props_s
{
    int fast_update; /**< 1 = reuse resource при обновлении   */
    int32_t z;       /**< DispmanX z-layer                     */
} slot_props_t;

static const slot_props_t SLOT_PROPS[SPRITE_SLOT_COUNT] = {
    [SPRITE_BACKGROUND]   = { .fast_update = 0, .z = 2 },
    [SPRITE_MODE]         = { .fast_update = 0, .z = 3 },
    [SPRITE_WEIGHT]       = { .fast_update = 0, .z = 4 },
    [SPRITE_DIGIT_LEFT]   = { .fast_update = 1, .z = 4 },
    [SPRITE_DIGIT_RIGHT]  = { .fast_update = 1, .z = 4 },
    [SPRITE_ARROW]        = { .fast_update = 1, .z = 4 },
    [SPRITE_NOTIFICATION] = { .fast_update = 0, .z = 5 },
};

/* ─── Состояние одного слота ─────────────────────────────────────────────── */

typedef struct slot_state_s
{
    IMAGE_LAYER_T layer;
    int initialized; /**< 0 = element/resource не созданы */
} slot_state_t;

/* ─── Структура renderer (opaque снаружи) ────────────────────────────────── */

struct renderer_s
{
    renderer_config_t cfg;
    DISPMANX_DISPLAY_HANDLE_T display;
    slot_state_t slots[SPRITE_SLOT_COUNT];
};

/* ─── Вспомогательные функции ────────────────────────────────────────────── */

static int slot_get_x(const renderer_t *r, sprite_slot_t slot)
{
    switch (slot)
    {
    case SPRITE_DIGIT_LEFT:
        return r->cfg.digit_left_x;
    case SPRITE_DIGIT_RIGHT:
        return r->cfg.digit_right_x;
    case SPRITE_ARROW:
        return r->cfg.arrow_x;
    case SPRITE_WEIGHT:
        return r->cfg.weight_x;
    default:
        return 0;
    }
}

static int slot_get_y(const renderer_t *r, sprite_slot_t slot)
{
    switch (slot)
    {
    case SPRITE_DIGIT_LEFT:
        return r->cfg.digit_left_y;
    case SPRITE_DIGIT_RIGHT:
        return r->cfg.digit_right_y;
    case SPRITE_ARROW:
        return r->cfg.arrow_y;
    case SPRITE_WEIGHT:
        return r->cfg.weight_y;
    default:
        return 0;
    }
}

/**
 * load_and_setup_rects — загрузить PNG в il->image и настроить bmpRect/srcRect/dstRect.
 *
 * Вызов loadPng выделяет il->image.buffer через initImage внутри.
 * После этого ректы выставляются по реальным размерам загруженного PNG.
 *
 * @return 1 при успехе, 0 при ошибке.
 */
static int load_and_setup_rects(IMAGE_LAYER_T *il, const char *path)
{
    if (!loadPng(&il->image, path))
    {
        syslog(LOG_ERR, "renderer: loadPng failed: %s", path);
        return 0;
    }

    int32_t w = il->image.width;
    int32_t h = il->image.height;

    vc_dispmanx_rect_set(&il->bmpRect, 0, 0, w, h);
    vc_dispmanx_rect_set(&il->srcRect, 0, 0, w << 16, h << 16);
    vc_dispmanx_rect_set(&il->dstRect, 0, 0, w, h);

    return 1;
}

/**
 * slot_create — полный цикл создания слота (slow path, а также первый вызов fast_update).
 *
 * Предполагает, что il->initialized == 0 и il->layer свежий / обнулённый.
 */
static int slot_create(renderer_t *r, slot_state_t *s, sprite_slot_t slot, const char *path)
{
    if (!load_and_setup_rects(&s->layer, path))
        return 0;

    createResourceImageLayer(&s->layer, SLOT_PROPS[slot].z);

    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
    addElementImageLayerOffset(&s->layer, slot_get_x(r, slot), slot_get_y(r, slot), r->display,
                               update);
    vc_dispmanx_update_submit_sync(update);

    s->initialized = 1;
    syslog(LOG_DEBUG, "renderer: slot %d created, z=%d, pos=(%d,%d), size=%dx%d", (int) slot,
           (int) SLOT_PROPS[slot].z, slot_get_x(r, slot), slot_get_y(r, slot),
           (int) s->layer.image.width, (int) s->layer.image.height);
    return 1;
}

/**
 * slot_update_fast — обновить данные слота без пересоздания element/resource.
 *
 * Требование: PNG должен иметь те же размеры, что при первом создании.
 * Нарушение: syslog WARNING + деградация до slot_create (после destroyImageLayer).
 *
 * @return 1 если fast update удался, 0 если нужно пересоздание.
 */
static int slot_update_fast(slot_state_t *s, const char *path)
{
    int32_t prev_w = s->layer.image.width;
    int32_t prev_h = s->layer.image.height;

    /* Освобождаем буфер пикселей; resource и element остаются живы */
    destroyImage(&s->layer.image);

    if (!load_and_setup_rects(&s->layer, path))
    {
        /* Буфер удалён, resource/element висят без данных.
         * Помечаем как неинициализированный — следующий вызов сделает полное пересоздание. */
        syslog(LOG_ERR, "renderer: fast update loadPng failed: %s", path);
        s->initialized = 0;
        return 0;
    }

    if (s->layer.image.width != prev_w || s->layer.image.height != prev_h)
    {
        syslog(LOG_WARNING,
               "renderer: fast update dimension mismatch %dx%d → %dx%d for %s, "
               "falling back to full recreate",
               (int) prev_w, (int) prev_h, (int) s->layer.image.width, (int) s->layer.image.height,
               path);
        /* Деградируем: resource/element ещё живы, сигнализируем вызывающему */
        s->initialized = 0;
        return 0;
    }

    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
    changeSourceImageLayer(&s->layer, update);
    vc_dispmanx_update_submit_sync(update);

    syslog(LOG_DEBUG, "renderer: slot fast-updated, path=%s", path);
    return 1;
}

/* ─── Публичный API ──────────────────────────────────────────────────────── */

renderer_t *renderer_create(const renderer_config_t *cfg)
{
    renderer_t *r = calloc(1u, sizeof(*r));
    if (r == NULL)
    {
        syslog(LOG_CRIT, "renderer: calloc failed");
        return NULL;
    }
    r->cfg = *cfg;

    bcm_host_init();

    r->display = vc_dispmanx_display_open(0);
    if (r->display == DISPMANX_NO_HANDLE)
    {
        syslog(LOG_ERR, "renderer: vc_dispmanx_display_open(0) failed");
        bcm_host_deinit();
        free(r);
        return NULL;
    }

    syslog(LOG_NOTICE, "renderer: display opened (handle=%u) resources_dir=%s",
           (unsigned) r->display, cfg->resources_dir);
    return r;
}

void renderer_destroy(renderer_t *r)
{
    if (r == NULL)
        return;

    for (int i = 0; i < (int) SPRITE_SLOT_COUNT; i++)
    {
        if (r->slots[i].initialized)
        {
            destroyImageLayer(&r->slots[i].layer);
            r->slots[i].initialized = 0;
        }
    }

    vc_dispmanx_display_close(r->display);
    bcm_host_deinit();
    free(r);

    syslog(LOG_NOTICE, "renderer: destroyed");
}

void renderer_show_png(renderer_t *r, sprite_slot_t slot, const char *path)
{
    if (r == NULL || path == NULL || slot >= SPRITE_SLOT_COUNT)
        return;

    slot_state_t *s = &r->slots[slot];

    /* ── Fast path: обновить данные без пересоздания element ─────────────── */
    if (SLOT_PROPS[slot].fast_update && s->initialized)
    {
        if (slot_update_fast(s, path))
            return;
        /* Fast update не удался (размер поменялся или loadPng error).
         * s->initialized уже сброшен в slot_update_fast.
         * Но resource/element могут ещё висеть — нужно их уничтожить. */
        destroyImageLayer(&s->layer);
        s->initialized = 0;
    }

    /* ── Slow path: уничтожить старый слот (если есть) и создать новый ──── */
    if (s->initialized)
    {
        destroyImageLayer(&s->layer);
        s->initialized = 0;
    }

    /* Обнуляем структуру layer перед созданием */
    (void) memset(&s->layer, 0, sizeof(s->layer));

    slot_create(r, s, slot, path);
}

void renderer_hide(renderer_t *r, sprite_slot_t slot)
{
    if (r == NULL || slot >= SPRITE_SLOT_COUNT)
        return;

    slot_state_t *s = &r->slots[slot];
    if (!s->initialized)
        return;

    destroyImageLayer(&s->layer);
    s->initialized = 0;

    syslog(LOG_DEBUG, "renderer: slot %d hidden", (int) slot);
}

void renderer_keepalive(renderer_t *r)
{
    if (r == NULL)
        return;

    /* Пустая транзакция: start → submit без изменений.
     * Стоимость: ~1 мс (один round-trip к VideoCore).
     * Эффект: предотвращает переход compositor в dormant state (P-28). */
    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
    if (update == DISPMANX_NO_HANDLE)
    {
        syslog(LOG_WARNING, "renderer_keepalive: update_start failed");
        return;
    }
    vc_dispmanx_update_submit_sync(update);
}