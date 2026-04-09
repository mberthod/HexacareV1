/**
 * @file sensor_sim.h
 * @brief Simulation Edge Computing des capteurs pour la trame LexaFullFrame (Core 1).
 * @details Tâche sensorSimulationTask épinglée sur APP_CPU (CORE_APP). Génère des trames
 * LexaFullFrame réalistes (vitaux, environnement, probabilités de chute, batterie). La trame
 * partagée s_frame est protégée par un Mutex ; sensor_sim_get_latest_frame() (appelée depuis Core 0)
 * prend le mutex avant lecture. Pile de la tâche : SENSOR_SIM_TASK_STACK (2048 octets).
 */

#ifndef SENSOR_SIM_H
#define SENSOR_SIM_H

#include "lexacare_protocol.h"
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Handle de la tâche simulation (pour suspension pendant OTA série). */
TaskHandle_t sensor_sim_get_task_handle(void);

/** Taille de pile pour la tâche de simulation. */
#define SENSOR_SIM_TASK_STACK 5120

/** Période de génération (ms). */
#define SENSOR_SIM_PERIOD_MS  1000

/**
 * @brief Crée et démarre la tâche sensorSimulationTask sur Core 1 (APP_CPU).
 * @details À appeler une fois depuis setup(). Crée le Mutex protégeant s_frame/s_has_frame
 * puis xTaskCreatePinnedToCore(..., CORE_APP). Période de génération : SENSOR_SIM_PERIOD_MS.
 */
void sensor_sim_task_start(void);

/**
 * @brief Copie la dernière trame simulée (thread-safe).
 * @param frame_out Buffer de sortie (au moins sizeof(LexaFullFrame_t)). Ne pas être NULL.
 * @return 1 si une trame est disponible et copiée, 0 sinon (timeout mutex 20 ms ou pas de frame).
 * @details Section critique : prise du mutex avant lecture de s_frame/s_has_frame, libération après.
 */
int sensor_sim_get_latest_frame(LexaFullFrame_t *frame_out);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_SIM_H */
