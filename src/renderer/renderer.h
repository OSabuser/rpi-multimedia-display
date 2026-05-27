#pragma once
/* Renderer abstraction — platform implementations in platform/ */
typedef struct renderer_s renderer_t;
renderer_t *renderer_create(void);
void        renderer_destroy(renderer_t *r);
void        renderer_update(renderer_t *r, int floor, int direction,
                            int mode, int load_percent);
