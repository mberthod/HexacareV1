#pragma once

#include <stdint.h>

/** Une ligne `FRAME:v0,...,v255` (mm, row-major 8×32), pour outils type read_matrix.py. */
void lexa_tof_frame_emit_ascii_line(const int16_t *mm, int w, int h);
