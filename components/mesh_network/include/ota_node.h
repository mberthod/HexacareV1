/**
 * @file ota_node.h
 * @brief Interface publique du module OTA nœud-à-nœud.
 *
 * Protocole OTA par fragments (200 octets de données utiles) :
 *   1. Réception d'un fragment OTA_FRAGMENT via le callback ESP-NOW.
 *   2. Déchiffrement AES-128-CBC du payload.
 *   3. Si premier fragment : esp_ota_begin() sur la partition ota_next.
 *   4. esp_ota_write() avec les données du fragment.
 *   5. Retransmission immédiate du fragment (chiffré) à tous les enfants.
 *   6. Si dernier fragment :
 *      esp_ota_end() → esp_ota_set_boot_partition() → esp_restart().
 */

#pragma once

#include "system_types.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @ingroup group_mesh
 * @brief Module OTA “nœud-à-nœud” utilisé par le réseau maillé.
 */

/* ================================================================
 * ota_node_init
 * @brief Initialise le module OTA (état interne, partition cible).
 *
 * @return ESP_OK si l'initialisation réussit.
 * ================================================================ */
esp_err_t ota_node_init(void);

/* ================================================================
 * ota_node_process_fragment
 * @brief Traite un fragment OTA reçu via ESP-NOW.
 *
 * Appelée depuis le callback de réception ESP-NOW de mesh_manager.
 * Gère l'écriture dans la partition OTA et la retransmission aux enfants.
 *
 * @param frag         Payload du fragment (déjà déchiffré).
 * @param children_mac Tableau des adresses MAC des enfants.
 * @param child_count  Nombre d'enfants.
 * @param raw_frame    Trame brute (chiffrée) à retransmettre aux enfants.
 * @param frame_len    Longueur de la trame brute.
 * @return ESP_OK si le traitement réussit.
 * ================================================================ */
esp_err_t ota_node_process_fragment(const ota_fragment_payload_t *frag,
                                     const uint8_t (*children_mac)[6],
                                     int child_count,
                                     const uint8_t *raw_frame,
                                     size_t frame_len);
