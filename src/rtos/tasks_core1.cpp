/**
 * @file tasks_core1.cpp
 * @brief L'Usine de Traitement (Tâches Temps Réel - Core 1).
 * 
 * @details
 * L'ESP32-S3 possède deux cerveaux (Cœurs).
 * - **Le Cœur 0 (Pro CPU)** : S'occupe du Wifi, du Mesh et de la gestion globale (Le Manager).
 * - **Le Cœur 1 (App CPU)** : S'occupe du travail intensif et mathématique (L'Ouvrier spécialisé).
 * 
 * Ce fichier définit toutes les tâches qui tournent sur le Cœur 1 pour ne pas ralentir le réseau :
 * 1. **Lidar** : Analyse les distances pour détecter les chutes.
 * 2. **Radar** : Analyse les ondes pour trouver la respiration.
 * 3. **Audio** : Écoute les bruits (sans enregistrer) pour aider à la détection.
 * 4. **Analogique** : Surveille la batterie et la température.
 */

#include "config/config.h"
#include "rtos/tasks_core1.h"
#include "rtos/queues_events.h"
#include "sensors/lidar_handler.h"
#include "sensors/radar_decoder.h"
#include "sensors/audio_handler.h"
#include "sensors/analog_handler.h"
#include "sensors/tmp117_handler.h"
#include "sensors/sensor_sim.h"
#include "system/system_state.h"
#include "mesh/routing_manager.h"
#include "lexacare_protocol.h"
#include "mesh/mesh_tree_protocol.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Arduino.h>

/**
 * @brief Tâche de gestion des Lidars.
 * Fréquence : ~50Hz (20ms).
 * Lit la matrice de profondeur et exécute la détection de chute.
 */
/**
 * @brief Tâche Lidar (Détecteur de distance).
 *
 * @details
 * Cette tâche mesure la distance avec le sol 50 fois par seconde.
 * Si la distance change brutalement, elle crie "CHUTE DÉTECTÉE !".
 * Elle envoie aussi régulièrement un résumé (min/max/moyenne) au système central.
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
            sensor_sim_get_latest_frame(&frame); // Utiliser la fonction existante qui copie s_frame
            
            uint8_t parent_mac[6];
            if (routing_get_parent_mac(parent_mac)) {
                // Envoi direct Unicast au parent
                routing_send_unicast(parent_mac, MSG_DATA, (uint8_t*)&frame, sizeof(frame));
            } else {
                // Si pas de parent (ex: ROOT ou orphelin), on peut quand même envoyer en queue TX pour traitement local (ex: Serial Gateway)
                // Mais routing_send_unicast gère l'envoi radio.
                // Si on est ROOT, on n'a pas de parent.
                // Le ROOT doit envoyer ses propres données au Serial Gateway.
                // La tâche dataTxTask dans main.cpp s'occupe de ça via g_queue_espnow_tx.
                // Donc on continue d'utiliser la queue TX pour découpler.
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
/**
 * @brief Tâche Radar (Détecteur de vie).
 *
 * @details
 * Cette tâche écoute le capteur Radar 100 fois par seconde.
 * Le radar est capable de "voir" la poitrine bouger pour mesurer la respiration,
 * même à travers une couette.
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
/**
 * @brief Tâche Audio (L'Oreille).
 *
 * @details
 * Cette tâche écoute les bruits ambiants 200 fois par seconde.
 * Elle ne comprend pas les mots (confidentialité), mais elle détecte les cris
 * ou les bruits sourds (choc) qui pourraient confirmer une chute.
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
/**
 * @brief Tâche Analogique (Le Médecin du système).
 *
 * @details
 * Cette tâche vérifie la santé du boîtier 5 fois par seconde.
 * - Elle mesure la tension de la batterie (pour savoir quand recharger).
 * - Elle mesure la température interne (pour éviter la surchauffe).
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
/**
 * @brief Démarrage de l'Usine (Lancement des tâches Core 1).
 *
 * @details
 * Cette fonction embauche tous les ouvriers spécialisés (Lidar, Radar, Audio, Analog)
 * et les envoie travailler dans l'usine n°1 (Core 1).
 * C'est le coup de sifflet de début de journée.
 */
void tasks_core1_start(void) {
    xTaskCreatePinnedToCore(task_lidar, "TaskLidar", TASK_LIDAR_STACK, NULL, TASK_PRIO_LIDAR, NULL, CORE_APP);
    xTaskCreatePinnedToCore(task_radar, "TaskRadar", TASK_RADAR_STACK, NULL, TASK_PRIO_RADAR, NULL, CORE_APP);
    xTaskCreatePinnedToCore(task_audio, "TaskAudio", TASK_AUDIO_STACK, NULL, TASK_PRIO_AUDIO, NULL, CORE_APP);
    xTaskCreatePinnedToCore(task_analog, "TaskAnalog", TASK_ANALOG_STACK, NULL, TASK_PRIO_ANALOG, NULL, CORE_APP);
}
