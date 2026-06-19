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

#include "font_renderer.h"    /* font_render_string, font_measure_string  */
#include "fonts/CalSans260.h" /* extern const tFont CalSans260            */
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

/*
 * Размер DispmanX-ресурса DIGIT-слота (px).
 *
 * Определяется размером самого широкого глифа шрифта CalSans260.
 * Самый широкий двухсимвольный глиф — «П0» (348 px по ширине).
 * DIGIT_SLOT_W = 400 — с запасом для центрирования.
 * DIGIT_SLOT_H = 191 — высота глифа CalSans260.
 *
 * При смене шрифта: обновить DIGIT_SLOT_W/H по новому глифу
 * и исправить _Static_assert ниже.
 * digit_slot_validate_font() в renderer_create() проверит соответствие при старте.
 */
enum
{
    DIGIT_SLOT_W = 400,
    DIGIT_SLOT_H = 191,
    /* VideoCore требует выравнивания pitch и высоты на 16.
+     * ARGB8888 = 4 байт/пиксель.                            */
    DIGIT_ALIGN     = 16,
    DIGIT_BYTES_PP  = 4,
    DIGIT_PITCH_PX  = (DIGIT_SLOT_W + DIGIT_ALIGN - 1) & ~(DIGIT_ALIGN - 1),
    DIGIT_ALIGNED_H = (DIGIT_SLOT_H + DIGIT_ALIGN - 1) & ~(DIGIT_ALIGN - 1),
    DIGIT_BUF_LEN   = DIGIT_PITCH_PX * DIGIT_ALIGNED_H, /* в uint32_t */
    DIGIT_BUF_BYTES = DIGIT_BUF_LEN * DIGIT_BYTES_PP,
};

/**
 * S_CHAR_UTF8 — маппинг char_code_t → UTF-8 строка для CalSans260.
 *
 * NULL = символ отсутствует в шрифте, пропускается при рендере.
 * Индекс = числовое значение char_code_t (0..CHAR_CODE_MAX).
 */
static const char *const S_CHAR_UTF8[(int) CHAR_CODE_MAX + 1] = {
    /*  0 CHAR_0          */ "0",
    /*  1 CHAR_1          */ "1",
    /*  2 CHAR_2          */ "2",
    /*  3 CHAR_3          */ "3",
    /*  4 CHAR_4          */ "4",
    /*  5 CHAR_5          */ "5",
    /*  6 CHAR_6          */ "6",
    /*  7 CHAR_7          */ "7",
    /*  8 CHAR_8          */ "8",
    /*  9 CHAR_9          */ "9",
    /* 10 CHAR_A          */ NULL,
    /* 11 CHAR_b          */ NULL,
    /* 12 CHAR_C          */ NULL,
    /* 13 CHAR_d          */ NULL,
    /* 14 CHAR_E          */ NULL,
    /* 15 CHAR_F          */ NULL,
    /* 16 CHAR_BLANK      */ NULL,
    /* 17 CHAR_PI_CYR     */ "\xd0\x9f", /* П (U+041F, UTF-8: 0xD0 0x9F)  */
    /* 18 CHAR_P_LAT      */ NULL,
    /* 19 CHAR_pi_cyr     */ NULL,
    /* 20 CHAR_N_CYR      */ NULL,
    /* 21 CHAR_U          */ NULL,
    /* 22 CHAR_MINUS      */ "-",
    /* 23 CHAR_UNDERSCORE */ NULL,
    /* 24 CHAR_u_lower    */ NULL,
    /* 25 CHAR_L          */ NULL,
    /* 26 CHAR_U_CYR      */ NULL,
    /* 27 CHAR_B_CYR      */ NULL,
    /* 28 CHAR_G_CYR      */ NULL,
    /* 29 CHAR_R          */ NULL,
    /* 30 CHAR_V          */ NULL,
    /* 31 CHAR_N          */ NULL,
    /* 32 CHAR_S          */ NULL,
    /* 33 CHAR_K          */ NULL,
    /* 34 CHAR_Y          */ NULL,
    /* 35 CHAR_G          */ NULL,
    /* 36 CHAR_B          */ NULL,
    /* 37 CHAR_T          */ NULL,
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
    uint32_t *digit_pixels;
};

/* ─── Вспомогательные функции ────────────────────────────────────────────── */

/*
 * digit_slot_validate_font — проверить что буфер DIGIT-слота вмещает
 * все глифы CalSans260. Вызывается один раз при renderer_create().
 * @return 1 если OK, 0 если буфер мал.
 */
static int digit_slot_validate_font(void)
{
    const tFont *p_font = &CalSans260;
    for (uint8_t i = 0U; i < p_font->length; i++)
    {
        const tImage *p_img = p_font->chars[i].image;
        if (p_img == NULL)
        {
            continue;
        }
        if ((int32_t) p_img->width > DIGIT_SLOT_W || (int32_t) p_img->height > DIGIT_SLOT_H)
        {
            syslog(LOG_CRIT,
                   "renderer: CalSans260 glyph 0x%lx size %dx%d exceeds "
                   "DIGIT slot %dx%d — update DIGIT_SLOT_W/H",
                   p_font->chars[i].code, (int) p_img->width, (int) p_img->height, DIGIT_SLOT_W,
                   DIGIT_SLOT_H);
            return 0;
        }
    }
    return 1;
}

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
    case SPRITE_NOTIFICATION:
        return r->cfg.notif_x;
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
    case SPRITE_NOTIFICATION:
        return r->cfg.notif_y;
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

/**
 * compose_digit_str — собрать UTF-8 строку из двух char_code_t.
 *
 * CHAR_BLANK и коды без глифа в CalSans260 пропускаются.
 * Нулевой терминатор гарантирован.
 *
 * @param left   код левого символа
 * @param right  код правого символа
 * @param p_buf  выходной буфер (минимум 8 байт)
 * @param sz     размер буфера
 */
static void compose_digit_str(char_code_t left, char_code_t right, char *p_buf, size_t sz)
{
    p_buf[0] = '\0';

    if ((int) left <= (int) CHAR_CODE_MAX && S_CHAR_UTF8[(int) left] != NULL)
    {
        (void) strncat(p_buf, S_CHAR_UTF8[(int) left], sz - 1U);
    }

    if ((int) right <= (int) CHAR_CODE_MAX && S_CHAR_UTF8[(int) right] != NULL)
    {
        (void) strncat(p_buf, S_CHAR_UTF8[(int) right], sz - strlen(p_buf) - 1U);
    }
}

/**
 * setup_digit_image — заполнить IMAGE_T для DIGIT-слота из heap-буфера.
 *
 * Не вызывает loadPng — данные уже в r->digit_pixels.
 */
static void setup_digit_image(renderer_t *r, IMAGE_LAYER_T *p_il)
{
    p_il->image.buffer        = r->digit_pixels;
    p_il->image.width         = DIGIT_SLOT_W;
    p_il->image.height        = DIGIT_SLOT_H;
    p_il->image.type          = VC_IMAGE_ARGB8888;
    p_il->image.pitch         = (int32_t) (DIGIT_PITCH_PX * DIGIT_BYTES_PP);
    p_il->image.alignedHeight = (int32_t) DIGIT_ALIGNED_H;

    vc_dispmanx_rect_set(&p_il->bmpRect, 0, 0, DIGIT_SLOT_W, DIGIT_SLOT_H);
    vc_dispmanx_rect_set(&p_il->srcRect, 0, 0, DIGIT_SLOT_W << 16, DIGIT_SLOT_H << 16);
    vc_dispmanx_rect_set(&p_il->dstRect, 0, 0, DIGIT_SLOT_W, DIGIT_SLOT_H);
}

/**
 * slot_create_digit — создать DispmanX element для DIGIT-слота (первый вызов).
 *
 * buffer = r->digit_pixels (heap). Не вызываем destroyImage при fast-update —
 * управление буфером на стороне renderer_t, free() в renderer_destroy().
 */
static void slot_create_digit(renderer_t *r, slot_state_t *p_s)
{
    (void) memset(&p_s->layer, 0, sizeof(p_s->layer));
    setup_digit_image(r, &p_s->layer);

    createResourceImageLayer(&p_s->layer, SLOT_PROPS[SPRITE_DIGIT_LEFT].z);

    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
    addElementImageLayerOffset(&p_s->layer, slot_get_x(r, SPRITE_DIGIT_LEFT),
                               slot_get_y(r, SPRITE_DIGIT_LEFT), r->display, update);
    vc_dispmanx_update_submit_sync(update);

    p_s->initialized = 1;
    syslog(LOG_DEBUG, "renderer: digit slot created, pos=(%d,%d), size=%dx%d",
           slot_get_x(r, SPRITE_DIGIT_LEFT), slot_get_y(r, SPRITE_DIGIT_LEFT), DIGIT_SLOT_W,
           DIGIT_SLOT_H);
}

/**
 * slot_update_digit — обновить пиксели DIGIT-слота без пересоздания element.
 *
 * r->digit_pixels уже заполнены вызывающим.
 */
static void slot_update_digit(renderer_t *r, slot_state_t *p_s)
{
    p_s->layer.image.buffer = r->digit_pixels;

    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
    changeSourceImageLayer(&p_s->layer, update);
    vc_dispmanx_update_submit_sync(update);

    syslog(LOG_DEBUG, "renderer: digit slot fast-updated");
}

/**
 * digit_pixels_alloc — выделить heap-буфер для DIGIT-слота (один раз).
 *
 * @return 1 при успехе или если буфер уже выделен, 0 при ошибке calloc.
 */
static int digit_pixels_alloc(renderer_t *r)
{
    if (r->digit_pixels != NULL)
    {
        return 1;
    }

    r->digit_pixels = calloc((size_t) DIGIT_BUF_LEN, sizeof(uint32_t));

    if (r->digit_pixels == NULL)
    {
        syslog(LOG_ERR, "renderer: digit_pixels calloc failed");
        return 0;
    }
    return 1;
}

void renderer_show_digit(renderer_t *r, char_code_t left, char_code_t right)
{
    if (r == NULL)
    {
        return;
    }

    char str[8]; /* макс: 2 × 3-байтовых UTF-8 + '\0' = 7 байт */
    compose_digit_str(left, right, str, sizeof(str));

    if (str[0] == '\0')
    {
        renderer_hide(r, SPRITE_DIGIT_LEFT);
        return;
    }

    if (!digit_pixels_alloc(r))
    {
        return;
    }

    (void) memset(r->digit_pixels, 0, (size_t) DIGIT_BUF_BYTES);

    uint32_t str_w = font_measure_string(&CalSans260, str);
    uint32_t x_off =
        (str_w < (uint32_t) DIGIT_SLOT_W) ? ((uint32_t) DIGIT_SLOT_W - str_w) / 2U : 0U;
    font_render_target_t target = {
        .pixels = r->digit_pixels,
        .width  = DIGIT_SLOT_W,
        .height = DIGIT_SLOT_H,
    };
    (void) font_render_string(&CalSans260, str, x_off, 0U, &target);

    slot_state_t *p_s = &r->slots[SPRITE_DIGIT_LEFT];
    if (!p_s->initialized)
    {
        slot_create_digit(r, p_s);
    }
    else
    {
        slot_update_digit(r, p_s);
    }
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

    if (!digit_slot_validate_font())
    {
        bcm_host_deinit();
        free(r);
        return NULL;
    }

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
    {
        return;
    }

    if (r->digit_pixels != NULL)
    {
        r->slots[SPRITE_DIGIT_LEFT].layer.image.buffer = NULL;
        free(r->digit_pixels);
        r->digit_pixels = NULL;
    }

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
