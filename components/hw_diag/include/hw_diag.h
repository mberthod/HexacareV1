/**
 * @file hw_diag.h
 * @brief Interface publique du diagnostic matériel au démarrage.
 *
 * hw_diag_run() doit être appelée dans app_main() AVANT la création
 * des tâches FreeRTOS. Elle initialise les bus I2C partagés et
 * remplit les champs lidar_ok[] et radar_ok de sys_context_t.
 */

#pragma once

#include "system_types.h"
#include "esp_err.h"
#include "driver/i2c_master.h"

/**
 * @defgroup group_hw_diag Diagnostic Matériel
 * @brief Vérifications au démarrage : bus, capteurs, et expandeur PCA9555.
 *
 * Raison d'être :
 * - détecter tôt les problèmes de câblage ou d'alimentation
 * - éviter de lancer des tâches qui bloqueraient si un périphérique est absent
 * - produire un rapport lisible (JSON) pour accélérer le diagnostic côté PC
 *
 * @{
 */

/* ================================================================
 * hw_diag_result_t
 * @brief Masque de bits indiquant les composants défaillants.
 *        HW_DIAG_OK (0) = tout fonctionne.
 * ================================================================ */
typedef enum {
    HW_DIAG_OK            = 0x00,
    HW_DIAG_LIDAR_PARTIAL = 0x01, /**< Au moins un LIDAR absent */
    HW_DIAG_LIDAR_ALL     = 0x02, /**< Aucun LIDAR détecté */
    HW_DIAG_RADAR_MISSING = 0x04, /**< Radar LD6002 non détecté */
} hw_diag_result_t;

/* ================================================================
 * hw_diag_run
 * @brief Exécute le diagnostic matériel complet et remplit sys_context_t.
 *
 * Séquence :
 *   1. Initialise I2C_NUM_0 (SDA=8, SCL=9) et I2C_NUM_1 (SDA=16, SCL=15).
 *   2. Met tous les LPn à 0 (reset global de tous les VL53L8CX).
 *   3. Active les LIDARs un par un : LPn=1, attente 5 ms,
 *      probe I2C à LIDAR_I2C_ADDR_DEFAULT (0x29),
 *      si OK → réassigne l'adresse à (LIDAR_I2C_ADDR_BASE + i).
 *   4. Configure UART2 à 1382400 bauds (LD6002).
 *      Écoute RX pendant 2000 ms et valide la trame TinyFrame.
 *   5. Formate le rapport JSON avec cJSON et l'imprime sur la console.
 *
 * @param ctx  Pointeur vers le contexte système (rempli par cette fonction).
 * @return     Masque hw_diag_result_t (0 = succès total).
 * ================================================================ */
hw_diag_result_t hw_diag_run(sys_context_t *ctx);

/* ================================================================
 * hw_diag_init_sensor_bus
 * @brief Initialise le bus I2C_NUM_1 (SDA=10, SCL=9) dédié aux
 *        capteurs environnementaux (HDC1080, BME280, MLX90640).
 *
 * À appeler depuis app_main() après hw_diag_run().
 * Le handle retourné doit être passé aux drivers (hdc1080_init, bme280_init).
 *
 * @param out_handle  Pointeur vers le handle de bus à remplir.
 * @return ESP_OK si succès.
 * ================================================================ */
esp_err_t hw_diag_init_sensor_bus(i2c_master_bus_handle_t *out_handle);

/* ================================================================
 * hw_diag_pca9555_set_power
 * @brief Active ou désactive une alimentation sous-système via
 *        le PCA9555D Port 1 (IO1.4–IO1.7).
 *
 * Exemples :
 *   hw_diag_pca9555_set_power(PCA9555_BIT_PWR_MIC,   true);  → alim micro ON
 *   hw_diag_pca9555_set_power(PCA9555_BIT_PWR_RADAR, true);  → alim radar ON
 *   hw_diag_pca9555_set_power(PCA9555_BIT_PWR_FAN,   false); → ventilateur OFF
 *
 * Prérequis : hw_diag_run() doit avoir été appelée.
 *
 * @param port1_bit  Masque de bit Port 1 (PCA9555_BIT_PWR_*).
 * @param enable     true = mettre à 1, false = mettre à 0.
 * @return ESP_OK si succès.
 * ================================================================ */
esp_err_t hw_diag_pca9555_set_power(uint8_t port1_bit, bool enable);

/** @} */ /* end of group_hw_diag */
