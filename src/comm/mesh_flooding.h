/**
 * @file mesh_flooding.h
 * @brief Managed Flooding ESP-NOW : cache msgId dans le callback RX, tâche de traitement + relay TTL/jitter.
 * @details
 * - Callback RX (ISR) : si msgId dans msgCache[100], ignorer ; sinon ajouter au cache et pousser dans rxQueue.
 * - meshProcessorTask (Core 0) : lit la queue, dispatch Data/OTA_ADV/OTA_CHUNK, si TTL>0 jitter 10–100 ms puis esp_now_send.
 * - Pas de WiFi AP/STA connecté ; WiFi.mode(WIFI_STA) + WiFi.disconnect().
 */

#ifndef MESH_FLOODING_H
#define MESH_FLOODING_H

#include "lexacare_protocol.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Callback appelé lorsqu'un paquet MSG_TYPE_DATA (LexaFullFrame) est reçu (nœud Gateway). */
typedef void (*mesh_flooding_data_cb_t)(const LexaFullFrame_t *frame);

/**
 * @brief Initialise WiFi (STA déconnecté), ESP-NOW, cache 100 msgId, enregistre le callback RX.
 * @return true si succès. g_queue_espnow_rx doit exister (queues_events_init avant).
 */
bool mesh_flooding_init(void);

/**
 * @brief Envoie un paquet en broadcast (FF:FF:FF:FF:FF:FF).
 * @param data Payload (header + body), max 250 octets.
 * @param len Longueur.
 * @return true si envoi lancé avec succès.
 */
bool mesh_flooding_send_broadcast(const uint8_t *data, size_t len);

/**
 * @brief Récupère l'adresse MAC de cette carte (6 octets).
 * @param mac_out Buffer d'au moins 6 octets.
 */
void mesh_flooding_get_my_mac(uint8_t *mac_out);

/**
 * @brief Enregistre le callback appelé pour chaque trame Data reçue (Gateway).
 * @param cb NULL pour désactiver.
 */
void mesh_flooding_set_data_cb(mesh_flooding_data_cb_t cb);

/**
 * @brief Point d'entrée de la tâche processeur mesh (lit rxQueue, dispatch, relay TTL + jitter 10–100 ms).
 * @param pv Paramètre non utilisé.
 * @details À lier à xTaskCreatePinnedToCore(..., CORE_PRO). Ne pas appeler depuis le callback RX.
 */
void mesh_flooding_task(void *pv);

#ifdef __cplusplus
}
#endif

#endif /* MESH_FLOODING_H */
