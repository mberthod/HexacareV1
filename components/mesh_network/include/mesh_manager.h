/**
 * @file mesh_manager.h
 * @brief Interface publique du gestionnaire de réseau maillé ESP-NOW.
 *
 * Topologie arborescente (Tree Mesh) :
 *   - Chaque nœud maintient une liste de pairs (peers).
 *   - Sélection du parent : peer avec le RSSI le plus élevé parmi ceux
 *     ayant un hop_to_root minimal.
 *   - Chiffrement : PMK/LMK ESP-NOW (couche hardware) + AES-128-CBC
 *     sur le payload applicatif (couche logicielle, mbedTLS).
 *
 * Clé AES-128 :
 *   Provisionnée en NVS (namespace "lexacare", clé "aes_key") lors
 *   de la fabrication. Si absente, une clé par défaut est utilisée
 *   (NON recommandé en production).
 */

#pragma once

#include "system_types.h"
#include "esp_err.h"

/**
 * @defgroup group_mesh Réseau maillé & OTA
 * @brief Communication entre nœuds (ESP-NOW), chiffrement applicatif, et mise à jour OTA.
 *
 * Idée simple :
 * - le Wi‑Fi sert ici de transport local (ESP‑NOW) pour relier des nœuds sans infrastructure
 * - la sécurité est renforcée par un chiffrement applicatif (AES) en plus d'ESP‑NOW
 *
 * @{
 */

/* ================================================================
 * mesh_manager_init
 * @brief Initialise le Wi-Fi en mode Station et configure ESP-NOW.
 *
 * Séquence :
 *   1. esp_netif_create_default_wifi_sta().
 *   2. esp_wifi_init() + esp_wifi_set_mode(WIFI_MODE_STA).
 *   3. esp_wifi_start().
 *   4. esp_now_init() + esp_now_set_pmk() (PMK 16 octets).
 *   5. Enregistrement des callbacks rx/tx ESP-NOW.
 *   6. Chargement de la clé AES depuis NVS.
 *
 * @param ctx Pointeur vers le contexte système.
 * @return ESP_OK si l'initialisation réussit.
 * ================================================================ */
esp_err_t mesh_manager_init(sys_context_t *ctx);

/* ================================================================
 * mesh_task_start
 * @brief Crée la tâche Task_Mesh_Com épinglée sur le Core 0.
 *
 * La tâche :
 *   - Écoute ai_to_mesh_queue (portMAX_DELAY).
 *   - Chiffre le payload en AES-128-CBC (IV aléatoire + mbedTLS).
 *   - Calcule le CRC16 sur la trame lexacare_frame_t.
 *   - Envoie via esp_now_send() vers le MAC du pair parent.
 *
 * @param ctx Pointeur vers le contexte système.
 * @return ESP_OK si la tâche est créée avec succès.
 * ================================================================ */
esp_err_t mesh_task_start(sys_context_t *ctx);

/** @} */ /* end of group_mesh */
