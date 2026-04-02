/**
 * @file fall_detection_ai.h
 * @brief Interface C du moteur d'inférence de détection de chute.
 *
 * Ce header est inclus par du code C (main.c).
 * L'implémentation est en C++ (fall_detection_ai.cc) pour ESP-DL.
 * Les guards extern "C" assurent la compatibilité de linkage.
 *
 * Mode de fonctionnement :
 *   Contrôlé par Kconfig (idf.py menuconfig → LexaCare AI Engine) :
 *   - CONFIG_AI_USE_ESPDL_MODEL=y  → inférence CNN via ESP-DL
 *   - CONFIG_AI_USE_ESPDL_MODEL=n  → algorithme géométrique à seuil
 *
 * Basculement sans recompilation complexe :
 *   idf.py menuconfig → modifier CONFIG_AI_USE_ESPDL_MODEL → idf.py build
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "system_types.h"
#include "esp_err.h"

/**
 * @defgroup group_ai_engine IA / Détection de chute
 * @brief Transforme les mesures capteurs en “état” (normal, chute, anormal) et notifications.
 *
 * Pourquoi une tâche dédiée :
 * - séparer la logique “métier” (détection) du matériel (capteurs) et du transport (mesh/USB)
 * - garantir des délais : la tâche écoute une file (queue) et produit un événement rapidement
 *
 * @{
 */

/* ================================================================
 * ai_engine_task_start
 * @brief Crée la tâche Task_AI_Inference épinglée sur le Core 1.
 *
 * La tâche :
 *   - S'abonne au TWDT (esp_task_wdt_add).
 *   - Attend sur sensor_to_ai_queue (portMAX_DELAY).
 *   - Exécute l'inférence (modèle ou seuil selon Kconfig).
 *   - Si AI_CHUTE_DETECTEE → xQueueSendToFront(ai_to_mesh_queue).
 *   - Autres états          → xQueueSend(ai_to_mesh_queue).
 *   - Pousse également dans diag_to_pc_queue.
 *   - Appelle esp_task_wdt_reset() à chaque itération.
 *
 * Prérequis mode modèle :
 *   Le fichier /littlefs/fall_model.espdl doit être présent sur la
 *   partition storage. Flasher avec : idf.py storage-flash (si configuré).
 *
 * @param ctx Pointeur vers le contexte système.
 * @return ESP_OK si la tâche est créée avec succès.
 * ================================================================ */
esp_err_t ai_engine_task_start(sys_context_t *ctx);

#ifdef __cplusplus
}
#endif

/** @} */ /* end of group_ai_engine */
