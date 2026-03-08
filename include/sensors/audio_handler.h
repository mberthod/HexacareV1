/**
 * @file audio_handler.h
 * @brief I2S 2x ICS-43434 : niveau sonore, FFT de base pour futur ML
 */

#ifndef AUDIO_HANDLER_H
#define AUDIO_HANDLER_H

#include "config/config.h"
#include <cstdint>
#include <cstdbool>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// Initialise I2S (DMA), pin POWER_MIC et démarre la capture.
bool audio_handler_init(void);

// Lit un bloc d'échantillons, calcule le niveau (RMS) et met à jour system_state.
// Optionnel : remplir un buffer FFT pour analyse ultérieure.
void audio_handler_process(void);

// Retourne le dernier niveau sonore (positif = RMS ou peak).
int32_t audio_handler_get_level(void);

// Récupère le buffer FFT (AUDIO_FFT_SIZE échantillons) pour usage externe.
void audio_handler_get_fft_buffer(int16_t *out, size_t len);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_HANDLER_H
