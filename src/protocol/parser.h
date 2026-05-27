#pragma once
#include "types.h"

/* Phase 0 stub — UART frame parser */
int protocol_parse(const char *line, parsed_frame_t *out);
