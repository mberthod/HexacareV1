/**
 * @file tasks_core1.cpp
 * @brief Définition des tâches temps réel s'exécutant sur l'APP_CPU (Core 1).
 * 
 * Ce module contient les boucles de tâches FreeRTOS pour l'acquisition haute
 * fréquence des Lidars, le décodage Radar, le traitement Audio et la surveillance
 * analogique. Ces tâches sont isolées du réseau (Core 0) pour garantir le temps réel.
 */

#include "config/config.h"
#include "rtos/tasks_core1.h"
#include "rtos/queues_events.h"
#include "sensors/lidar_handler.h"
#include "sensors/radar_decoder.h"
#include "sensors/audio_handler.h"
#include "sensors/analog_handler.h"
#include "sensors/tmp117_handler.h"
#include "system/system_state.h"
#include "comm/routing_manager.h"
#include "lexacare_protocol.h"
#include "comm/mesh_tree_protocol.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Arduino.h>

/**
 * @brief Tâche de gestion des Lidars.
 * Fréquence : ~50Hz (20ms).
 * Lit la matrice de profondeur et exécute la détection de chute.
 */
void task_lidar(void *pvParameters) {
    (void)pvParameters;
    for (;;) {
        int n = lidar_handler_read_frame();
        if (n > 0) {
            lidar_handler_update_fall_detection();
            matrix_summary_t sum = {};
            lidar_handler_get_summary(&sum.min_mm, &sum.max_mm, &sum.sum_mm, &sum.valid_zones);
            sum.last_update_ms = millis();
            system_state_set_matrix_summary(&sum);
            
            // Signalement immédiat en cas de chute
            if (system_state_get_fall_detected())
                xEventGroupSetBits(g_system_events, EVENT_FALL_DETECTED);
            
            // V2: Envoyer les données au parent via Unicast (Tree Mesh)
            // On construit une LexaFullFrame (compatible V1) et on l'envoie dans MSG_DATA
            LexaFullFrame_t frame;
            // Remplir la frame avec les données système (à implémenter proprement avec un getter global)
            // Pour l'instant, on suppose que system_state a tout
            // ... (logique de remplissage simplifiée)
            
            uint8_t parent_mac[6];
            if (routing_get_parent_mac(parent_mac)) {
                // routing_send_unicast(parent_mac, MSG_DATA, (uint8_t*)&frame, sizeof(frame));
                // Note: L'envoi direct depuis Core 1 est possible si routing_send_unicast est thread-safe
                // Sinon, passer par g_queue_espnow_tx comme avant
                xQueueSend(g_queue_espnow_tx, &frame, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/**
 * @brief Tâche de décodage du Radar.
 * Fréquence : ~100Hz (10ms).
 * Analyse le flux UART pour extraire les signes vitaux.
 */
void task_radar(void *pvParameters) {
    (void)pvParameters;
    for (;;) {
        radar_decoder_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Tâche de traitement Audio.
 * Fréquence : ~200Hz (5ms).
 * Gère l'acquisition DMA et le calcul du niveau sonore.
 */
void task_audio(void *pvParameters) {
    (void)pvParameters;
    for (;;) {
        audio_handler_process();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/**
 * @brief Tâche de surveillance analogique et température.
 * Fréquence : ~5Hz (200ms).
 * Surveille les rails de tension et le capteur TMP117.
 */
void task_analog(void *pvParameters) {
    (void)pvParameters;
    for (;;) {
        analog_handler_update();
        tmp117_handler_read_temp_c();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/**
 * @brief Lance toutes les tâches du Core 1.
 * Utilise xTaskCreatePinnedToCore pour forcer l'exécution sur l'APP_CPU.
 */
void tasks_core1_start(void) {
    xTaskCreatePinnedToCore(task_lidar, "TaskLidar", TASK_LIDAR_STACK, NULL, TASK_PRIO_LIDAR, NULL, CORE_APP);
    xTaskCreatePinnedToCore(task_radar, "TaskRadar", TASK_RADAR_STACK, NULL, TASK_PRIO_RADAR, NULL, CORE_APP);
    xTaskCreatePinnedToCore(task_audio, "TaskAudio", TASK_AUDIO_STACK, NULL, TASK_PRIO_AUDIO, NULL, CORE_APP);
    xTaskCreatePinnedToCore(task_analog, "TaskAnalog", TASK_ANALOG_STACK, NULL, TASK_PRIO_ANALOG, NULL, CORE_APP);
}
