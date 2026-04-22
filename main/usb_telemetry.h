#pragma once

#include <stdint.h>
#include <stddef.h>

void usb_telemetry_init(void);
void usb_telemetry_start(void);
void usb_telemetry_post_vision(const float *frame, int w, int h);
/** Appelé depuis task_audio : derniers échantillons stéréo déjà lus (L,R,…). */
void usb_telemetry_enqueue_pcm_tail(const int16_t *interleaved_lr, size_t n_pairs,
                                      uint32_t seq);
