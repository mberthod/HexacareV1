/**
 * @file lidar_driver.h
 * @brief Interface publique du driver LIDAR VL53L8CX — acquisition et fusion 8×32.
 *
 * Task_Sensor_Acq (Core 1, priorité 10) pilote les 4 LIDARs frontaux
 * via les deux bus I2C initialisés par hw_diag_run().
 */

#pragma once

#include "system_types.h"
#include "esp_err.h"

/**
 * @defgroup group_sensor_acq Acquisition Capteurs
 * @brief Lecture capteurs, fusion des données, et production de trames prêtes pour l'IA.
 *
 * Ce module existe pour “isoler le monde matériel” :
 * - la partie IA reçoit une trame stable et cohérente (mêmes unités, mêmes dimensions)
 * - la partie diagnostic PC reçoit des mesures prêtes à afficher
 *
 * @{
 */

/* ================================================================
 * lidar_driver_start
 * @brief Crée la tâche Task_Sensor_Acq épinglée sur le Core 1.
 *
 * Prérequis : hw_diag_run() doit avoir été appelée au préalable
 * pour initialiser ctx->i2c_bus0, ctx->i2c_bus1 et ctx->lidar_ok[].
 *
 * La tâche :
 *   - S'abonne au TWDT (esp_task_wdt_add).
 *   - Configure chaque VL53L8CX détecté en résolution 8×8, ranging continu.
 *   - Cadence 15 Hz via vTaskDelayUntil.
 *   - Fusionne les 4 trames 8×8 en une lidar_matrix_t 8×32.
 *   - Pousse la sensor_frame_t dans ctx->sensor_to_ai_queue (non bloquant).
 *   - Appelle esp_task_wdt_reset() à chaque itération.
 *
 * @param ctx Pointeur vers le contexte système.
 * @return ESP_OK si la tâche est créée avec succès.
 * ================================================================ */
esp_err_t lidar_driver_start(sys_context_t *ctx);

/** @} */ /* end of group_sensor_acq */
