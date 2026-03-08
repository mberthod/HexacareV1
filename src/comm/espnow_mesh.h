/**
 * @file espnow_mesh.h
 * @brief Réseau mesh ESP-NOW par inondation (flooding).
 * @details Callback RX pousse les paquets dans g_queue_espnow_rx ; espnow_mesh_handle_packet()
 * consomme un élément (cache anti-doublon 50 msgId, TTL, jitter 5–50 ms, retransmission).
 * Pas de création de tâches dans ce module (tâches dans main).
 */

#ifndef ESPNOW_MESH_H
#define ESPNOW_MESH_H

#include "lexacare_protocol.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Callback appelé lorsqu'un paquet Data (LexaFullFrame) est reçu et que ce nœud est Gateway. */
typedef void (*espnow_mesh_data_cb_t)(const LexaFullFrame_t *frame);

/**
 * @brief Initialise WiFi (STA déconnecté), ESP-NOW, peer broadcast, enregistre le callback RX.
 * @return true si succès. g_queue_espnow_rx doit déjà exister (queues_events_init avant).
 */
bool espnow_mesh_init(void);

/**
 * @brief Envoie un paquet en broadcast (FF:FF:FF:FF:FF:FF).
 * @param data Payload (header + body), max 250 octets.
 * @param len Longueur.
 * @return true si envoi lancé avec succès.
 */
bool espnow_mesh_send_broadcast(const uint8_t *data, size_t len);

/**
 * @brief Récupère l'adresse MAC de cette carte (6 octets).
 */
void espnow_mesh_get_my_mac(uint8_t *mac_out);

/**
 * @brief Enregistre le callback appelé pour chaque trame Data reçue (Gateway).
 * @param cb NULL pour désactiver.
 */
void espnow_mesh_set_data_cb(espnow_mesh_data_cb_t cb);

/**
 * @brief Traite un paquet reçu (cache, dispatch OTA/Data, retransmission si TTL > 0 avec jitter).
 * @param item Élément lu depuis g_queue_espnow_rx (mac, len, payload).
 * @details À appeler depuis espnowRxTask. Vérifie le cache msgId ; si nouveau, ajoute au cache,
 * dispatch selon msgType (Data -> data_cb si Gateway ; OTA_ADV/OTA_CHUNK -> ota_manager),
 * puis si TTL > 0 décrémente TTL, attend jitter 5–50 ms, re-envoie en broadcast.
 */
void espnow_mesh_handle_packet(const void *item);

/**
 * @brief Retourne le nombre de pairs enregistrés (pour debug).
 */
int espnow_mesh_get_peer_count(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPNOW_MESH_H */
