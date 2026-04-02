/**
 * @file mic_driver.h
 * @brief Driver microphone MEMS numérique — Interface I2S (ESP-IDF v5/v6).
 *
 * Capture un burst de N échantillons 32 bits via I2S en mode standard.
 * Calcule l'amplitude RMS et le pic absolu.
 *
 * Broches (depuis pins_config.h) :
 *   PIN_MIC_WS  (GPIO6)  : Word Select / LRCK
 *   PIN_MIC_SD  (GPIO7)  : Serial Data (données audio entrant)
 *   PIN_MIC_SCK (GPIO8)  : Bit Clock
 *
 * Fréquence d'échantillonnage : 16 kHz (suffisant pour voix + détection)
 * Largeur de slot              : 32 bits
 * Taille du burst              : 512 samples (32 ms à 16 kHz)
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup group_sensor_acq
 * @brief Acquisition audio simple (RMS + pic) pour diagnostic et indicateurs.
 */

/* ================================================================
 * mic_driver_init
 * @brief Initialise le canal I2S RX pour le microphone MEMS.
 *
 * @return ESP_OK si succès.
 * ================================================================ */
esp_err_t mic_driver_init(void);

/* ================================================================
 * mic_driver_read
 * @brief Capture un burst et calcule RMS + pic.
 *
 * @param rms  Pointeur vers l'amplitude RMS (raw I2S uint32).
 * @param peak Pointeur vers l'amplitude crête.
 * @return ESP_OK si succès.
 * ================================================================ */
esp_err_t mic_driver_read(uint32_t *rms, uint32_t *peak);

/* ================================================================
 * mic_driver_deinit
 * @brief Libère le canal I2S.
 * ================================================================ */
void mic_driver_deinit(void);

#ifdef __cplusplus
}
#endif
