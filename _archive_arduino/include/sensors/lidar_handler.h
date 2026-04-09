/**
 * @file lidar_handler.h
 * @brief Gestion des 4 Lidars VL53L8CX : init, matrice 32x8, détection de chute
 */

#ifndef LIDAR_HANDLER_H
#define LIDAR_HANDLER_H

#include "config/config.h"
#include <cstdint>
#include <cstdbool>

#ifdef __cplusplus
extern "C" {
#endif

// Matrice globale 32 colonnes x 8 lignes (4 capteurs 8x8 côte à côte)
extern uint16_t g_lidar_matrix[LIDAR_MATRIX_ROWS][LIDAR_MATRIX_COLS];

// Initialise I2C, GPIO LPn et assigne les adresses aux 4 Lidars (0x54, 0x56, 0x58, 0x5A).
// Retourne true si au moins un capteur répond.
bool lidar_handler_init(void);

// Lit les 4 capteurs et remplit g_lidar_matrix. Retourne le nombre de capteurs lus avec succès (0-4).
int lidar_handler_read_frame(void);

// Algorithme de détection de chute : variation rapide de distance dans les zones.
// Met à jour l'état système (fall_detected) si une chute est détectée.
void lidar_handler_update_fall_detection(void);

// Calcule le résumé de la matrice (min, max, sum, valid_zones) pour MQTT.
void lidar_handler_get_summary(uint16_t *min_mm, uint16_t *max_mm, uint32_t *sum_mm, uint16_t *valid_zones);

// Réessaie la réinit d'un capteur perdu (re-séquence LPn). À appeler périodiquement si besoin.
void lidar_handler_recover_i2c(void);

#ifdef __cplusplus
}
#endif

#endif // LIDAR_HANDLER_H
