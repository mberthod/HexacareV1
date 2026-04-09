/**
 * @file tasks_core1.h
 * @brief Tâches FreeRTOS épinglées au Core 1 : Lidar, Radar, Audio, Analog
 */

#ifndef TASKS_CORE1_H
#define TASKS_CORE1_H

#include "config/config.h"
#include <cstdbool>

#ifdef __cplusplus
extern "C" {
#endif

void task_lidar(void *pvParameters);
void task_radar(void *pvParameters);
void task_audio(void *pvParameters);
void task_analog(void *pvParameters);

// Crée et démarre les 4 tâches sur le core 1.
void tasks_core1_start(void);

#ifdef __cplusplus
}
#endif

#endif // TASKS_CORE1_H
