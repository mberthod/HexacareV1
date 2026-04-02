/**
 * @file bme280_driver.h
 * @brief Driver Bosch BME280 — Température, Pression, Humidité.
 *
 * Interface I2C via driver/i2c_master.h (ESP-IDF v5+).
 * Adresse I2C 7 bits : 0x76 (SDO=GND) ou 0x77 (SDO=VDD).
 *
 * Caractéristiques :
 *   - Température  : −40 °C → +85 °C,     résolution 0.01 °C
 *   - Pression     : 300 hPa → 1100 hPa,  résolution 0.18 Pa
 *   - Humidité     : 0 % → 100 % RH,      résolution 0.008 %
 *   - Mode utilisé : Forced mode (single-shot) + filtre IIR × 4
 */

#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup group_sensor_acq
 * @brief Capteur environnemental BME280 (pression/température/humidité).
 */

#define BME280_I2C_ADDR_DEFAULT  0x76  /**< SDO reliée à GND */
#define BME280_I2C_ADDR_ALT      0x77  /**< SDO reliée à VDD */

/* ================================================================
 * bme280_init
 * @brief Initialise le BME280, lit les coefficients de calibration,
 *        configure le filtre IIR × 4 et l'oversampling standard.
 *
 * @param bus      Handle du bus I2C maître.
 * @param i2c_addr Adresse I2C (BME280_I2C_ADDR_DEFAULT ou _ALT).
 * @return ESP_OK si le capteur est détecté (chip_id = 0x60).
 * ================================================================ */
esp_err_t bme280_init(i2c_master_bus_handle_t bus, uint8_t i2c_addr);

/* ================================================================
 * bme280_read
 * @brief Déclenche une mesure forced-mode et lit les 3 grandeurs.
 *
 * Bloque ~10 ms le temps de la conversion.
 *
 * @param temp_c        Température (°C).
 * @param pressure_hpa  Pression (hPa).
 * @param humidity_pct  Humidité (%).
 * @return ESP_OK si succès.
 * ================================================================ */
esp_err_t bme280_read(float *temp_c, float *pressure_hpa, float *humidity_pct);

#ifdef __cplusplus
}
#endif
