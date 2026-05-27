#pragma once
#include "../protocol/types.h"
typedef struct { parsed_frame_t frame; int initialized; } indicator_state_t;
typedef struct { int changed; } state_update_result_t;
state_update_result_t state_apply_frame(indicator_state_t *s,
                                        const parsed_frame_t *f);
