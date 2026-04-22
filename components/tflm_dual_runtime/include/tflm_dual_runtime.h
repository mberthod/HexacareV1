#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Init des deux interpreters TFLM (audio + vision), arenas en PSRAM
 * dimensionnées par LEXA_TFLM_ARENA_*_KB dans lexa_config.h. */
esp_err_t tflm_dual_runtime_init(void);

/* Inférence audio. mfcc en input float32, label_out = argmax,
 * conf_pct_out = (max softmax * 100). */
esp_err_t tflm_dual_infer_audio(const float *mfcc, size_t n_elements,
                                int32_t *label_out, uint8_t *conf_pct_out);

/* Inférence vision. frame en input float32 [0,1] (h*w elements). */
esp_err_t tflm_dual_infer_vision(const float *frame, size_t n_elements,
                                 int32_t *label_out, uint8_t *conf_pct_out);

#ifdef __cplusplus
}
#endif
