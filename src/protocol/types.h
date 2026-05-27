#pragma once

/* Phase 0 stub — protocol types, implemented in Phase 1 */
typedef enum { DIRECTION_NONE = 0, DIRECTION_UP, DIRECTION_DOWN } direction_t;
typedef enum { MODE_NORMAL = 0, MODE_FIRE_ALARM, MODE_OVERLOAD,
               MODE_CALLING, MODE_TALKING } mode_t;
typedef enum { SOUND_NONE = 0, SOUND_DING, SOUND_UP,
               SOUND_DOWN, SOUND_OVERLOAD } sound_t;

typedef struct {
    int         floor;
    direction_t direction;
    mode_t      mode;
    sound_t     sound;
    int         load_percent;
    int         valid;
} parsed_frame_t;
