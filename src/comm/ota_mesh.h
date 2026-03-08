/**
 * @file ota_mesh.h
 * @brief OTA Random Access : esp_partition_write par offset, chunkMap bitfield (5000 chunks max).
 * @details Pas d'Update.h. Réception OTA_ADV (taille, chunks, MD5) puis OTA_CHUNK hors-ordre ;
 * écriture par esp_partition_write(partition, chunkIndex*200, data, 200). Fin : MD5 partition puis esp_ota_set_boot_partition.
 */

#ifndef OTA_MESH_H
#define OTA_MESH_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise le module OTA mesh (chunkMap, état). À appeler après queues_events_init.
 */
void ota_mesh_init(void);

/**
 * @brief Traite un payload OTA_ADV reçu (38 octets : totalSize, totalChunks, md5Hex).
 * @param payload Pointeur sur OtaAdvPayload (38 octets).
 * @param len Taille (doit être >= 38).
 */
void ota_mesh_on_ota_adv(const uint8_t *payload, size_t len);

/**
 * @brief Traite un payload OTA_CHUNK reçu (204 octets). Si chunk déjà reçu (bit dans chunkMap), ignore.
 * Sinon esp_partition_write(partition, chunkIndex*200, data, 200), met le bit à 1, relance le broadcast.
 * @param payload Pointeur sur OtaChunkPayload (204 octets).
 * @param len Taille (doit être >= 204).
 */
void ota_mesh_on_ota_chunk(const uint8_t *payload, size_t len);

/**
 * @brief Indique si une mise à jour OTA est en cours (réception/écriture chunks).
 * @return 1 si OTA en cours, 0 sinon.
 */
int ota_mesh_is_ota_in_progress(void);

/**
 * @brief Retourne la version firmware courante (NVS "system" / "fw_ver").
 */
uint32_t ota_mesh_get_fw_version(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MESH_H */
