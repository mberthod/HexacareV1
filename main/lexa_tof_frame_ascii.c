#include <stdio.h>
#include "lexa_tof_frame_ascii.h"

void lexa_tof_frame_emit_ascii_line(const int16_t *mm, int w, int h)
{
    if (!mm || w <= 0 || h <= 0) {
        return;
    }
    const int n = w * h;
    fputs("FRAME:", stdout);
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            fputc(',', stdout);
        }
        fprintf(stdout, "%d", (int)mm[i]);
    }
    fputc('\n', stdout);
    fflush(stdout);
}
