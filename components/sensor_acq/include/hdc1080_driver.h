/**
 * @file hdc1080_driver.h
 * @brief Driver Texas Instruments HDC1080 — Température et Humidité relative.
 *
 * Interface I2C via driver/i2c_master.h (ESP-IDF v5+).
 * Adresse I2C 7 bits : 0x40 (fixe sur HDC1080).
 *
 * Caractéristiques :
 *   - Température  : −40 °C → +125 °C, résolution 14 bits (±0.2 °C typ.)
 *   - Humidité     : 0 % → 100 % RH,   résolution 14 bits (±2 % typ.)
 *   - Temps d'acquisition : ~15 ms (mode 14 bits)
 */

#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup group_sensor_acq
 * @brief Capteur environnemental HDC1080 (température/humidité).
 */

#define HDC1080_I2C_ADDR        0x40  /**< Adresse I2C 7 bits du HDC1080 */

/* ================================================================
 * hdc1080_init
 * @brief Initialise le HDC1080 et vérifie le Device ID (0x1050).
 *
 * Configure le capteur en mode acquisition séquentielle 14 bits
 * (température puis humidité en un seul tir).
 *
 * @param bus     Handle du bus I2C maître.
 * @return ESP_OK si le capteur est détecté et configuré.
 * ================================================================ */
esp_err_t hdc1080_init(i2c_master_bus_handle_t bus);

/* ================================================================
 * hdc1080_read
 * @brief Déclenche une acquisition et lit température + humidité.
 *
 * Bloque ~15 ms pendant l'acquisition (vTaskDelay).
 *
 * @param temp_c       Pointeur vers la température résultante (°C).
 * @param humidity_pct Pointeur vers l'humidité résultante (%).
 * @return ESP_OK si succès.
 * ================================================================ */
esp_err_t hdc1080_read(float *temp_c, float *humidity_pct);

#ifdef __cplusplus
}
#endif
