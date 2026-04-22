#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Calcule le MFCC d'un signal int16 mono.
 * mfcc_out doit être pré-alloué pour mfcc_elements floats.
 *
 * Paramètres MFCC : voir lexa_config.h (LEXA_MFCC_*).
 * Doit être bit-équivalent à la chaîne librosa Python (skill mfcc-validation).
 */
esp_err_t mfcc_compute(const int16_t *input, size_t n_samples,
                       float *mfcc_out, size_t mfcc_elements);

#ifdef __cplusplus
}
#endif
