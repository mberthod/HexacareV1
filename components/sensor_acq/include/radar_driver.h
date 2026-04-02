/**
 * @file radar_driver.h
 * @brief Interface publique du driver Radar HLK-LD6002 — UART + TinyFrame.
 *
 * Le driver utilise le ring buffer UART installé par hw_diag_run()
 * (uart_driver_install sur UART_NUM_2). Il parse le protocole TinyFrame :
 *   SOF(0x01) | Type(2o) | Longueur(1o) | Données | EOF(0x04)
 *
 * Types reconnus :
 *   0x0A14 → breath_rate_bpm  (uint16 big-endian)
 *   0x0A15 → heart_rate_bpm   (uint16 big-endian)
 *   0x0A16 → target_distance_mm (uint16 big-endian)
 */

#pragma once

#include "system_types.h"
#include "esp_err.h"

/**
 * @ingroup group_sensor_acq
 * @brief Driver radar HLK-LD6002 (UART + TinyFrame) — optionnel selon la configuration.
 */

/* ================================================================
 * radar_driver_start
 * @brief Lance la tâche de réception UART du radar dans sensor_acq.
 *
 * Le radar tourne dans la même tâche que le LIDAR (Task_Sensor_Acq).
 * Cette fonction est appelée par lidar_driver.c pour initialiser
 * l'état interne du parser radar avant la boucle d'acquisition.
 *
 * @return ESP_OK si l'initialisation réussit.
 * ================================================================ */
esp_err_t radar_driver_init(void);

/* ================================================================
 * radar_driver_poll
 * @brief Lit les octets UART disponibles et met à jour radar_data_t.
 *
 * À appeler périodiquement depuis la boucle de Task_Sensor_Acq.
 * Non bloquante (timeout UART = 0 ms).
 *
 * @param out Pointeur vers la structure radar_data_t à mettre à jour.
 * @return true si de nouvelles données ont été reçues.
 * ================================================================ */
bool radar_driver_poll(radar_data_t *out);
