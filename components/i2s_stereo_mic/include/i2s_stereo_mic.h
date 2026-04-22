#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int bclk_gpio;
    int ws_gpio;
    int din_gpio;
    int sample_rate_hz;
    /** Bits retirés après extraction 24-bit, avant int16 (≈ −6 dB par +1).
     *  0 = défaut compile (0 : pas d’atténuation en plus du 24→16). N’augmenter
     *  (2…6) que si le signal sature encore en int16. Max appliqué 12. */
    uint8_t pcm_extra_downshift;
    /** Gain numérique : décalage à gauche après conversion (×2^n), saturé int16.
     *  0 = pas de gain. Typ. 4–6 si crête ~±200–500 sans saturation ; baisser si clip. Max 7. */
    uint8_t pcm_output_shift;
} i2s_stereo_mic_config_t;

esp_err_t i2s_stereo_mic_init(const i2s_stereo_mic_config_t *cfg);

/* Lit jusqu’à N échantillons mono int16 (16 kHz) depuis l’I2S : canal gauche
 * du stéréo (ICS-43434 LR=0), conversion 24→16 selon DS-000069 / driver.
 * Retourne le nombre d’échantillons effectivement lus. */
size_t i2s_stereo_mic_read_latest(int16_t *out, size_t n_samples,
                                  int timeout_ms);

/* Même conversion que le mono, mais paires L,R entrelacées (16 kHz × 2 canaux).
 * `out_lr` reçoit L,R,L,R… ; `max_pairs` = nombre max de paires stéréo.
 * Retour : nombre d’int16 écrits (pair, toujours multiple de 2). */
size_t i2s_stereo_mic_read_latest_interleaved_lr(int16_t *out_lr, size_t max_pairs,
                                                 int timeout_ms);

#ifdef __cplusplus
}
#endif
