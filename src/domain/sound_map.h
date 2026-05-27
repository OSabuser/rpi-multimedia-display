#pragma once
#include "../protocol/types.h"
typedef struct { const char *path; int volume_percent; int valid; } sound_entry_t;
sound_entry_t sound_map_resolve(sound_t sound, int floor, int has_music);
