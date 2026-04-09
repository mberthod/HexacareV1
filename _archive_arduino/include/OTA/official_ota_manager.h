/**
 * @file official_ota_manager.h
 * @brief Gestionnaire OTA via la librairie officielle Espressif espnow_ota.
 *
 * @details
 * - Côté ROOT (Initiator) : après réception du firmware par UART et écriture en flash
 *   (partition OTA), lance la distribution vers les nœuds enfants via espnow_ota_initiator_send.
 * - Côté CHILD (Responder) : démarre espnow_ota_responder_start pour accepter les mises à jour.
 * Nécessite espnow_storage_init() et (selon composant) espnow_init() avant usage.
 */

#ifndef OFFICIAL_OTA_MANAGER_H
#define OFFICIAL_OTA_MANAGER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise le sous-système OTA officiel (espnow_storage_init si composant présent).
 * À appeler une fois au boot, après WiFi/ESP-NOW.
 * @return 0 en échec, 1 en succès.
 */
int official_ota_init(void);

/**
 * @brief Démarre le responder OTA (nœud enfant).
 * À appeler au boot sur les nœuds non-ROOT.
 * @return 0 en échec, 1 en succès.
 */
int official_ota_responder_start(void);

/**
 * @brief Callback fourni à l'initiator : lit les données de la partition OTA
 * (staged) pour envoi ESP-NOW. Utilisé en interne par start_ota_initiator_distribution.
 */
typedef int (*official_ota_read_partition_cb_t)(size_t offset, void *dst, size_t size);

/**
 * @brief Lance la distribution du firmware depuis la partition OTA courante
 * vers les responders (scan + envoi). À appeler côté ROOT après esp_ota_end.
 * @param firmware_size Taille totale du firmware (octets).
 * @param sha256_hex Si non NULL, 64 caractères hex (SHA-256). Sinon, calculé depuis la partition.
 * @return 0 en échec, 1 si au moins un nœud a reçu avec succès.
 */
int start_ota_initiator_distribution(size_t firmware_size, const char *sha256_hex);

#ifdef __cplusplus
}
#endif

#endif /* OFFICIAL_OTA_MANAGER_H */
