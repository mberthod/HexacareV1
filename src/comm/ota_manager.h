/**
 * @file ota_manager.h
 * @brief Gestionnaire OTA Gossip : NVS, OTA_ADV, OTA_REQ, chunks 512 octets, MD5.
 * @details Diffusion par ROOT, pull par les nœuds, vérification MD5 avant reboot.
 * Protocole : OTA_ADV (annonce), OTA_REQ (demande chunk), OTA_CHUNK (données base64).
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise l'OTA Manager.
 * @return 1 si OK, 0 en cas d'erreur.
 * @details Crée s_ota_mutex si nécessaire, réinitialise s_ota_started, s_ota_received, s_adv_size/s_adv_md5.
 */
int ota_manager_init(void);

/**
 * @brief À appeler dans loop() : diffusion OTA_ADV par ROOT toutes les OTA_ADV_INTERVAL_MS.
 */
void ota_manager_loop(void);

/**
 * @brief Envoie immédiatement un OTA_ADV sur le mesh (ROOT).
 * @details Utilisé lorsque le PC envoie une commande {"type":"OTA_ADV"} sur le port série.
 * Diffuse la version, MD5 et taille de la partition courante sans attendre OTA_ADV_INTERVAL_MS.
 */
void ota_manager_send_adv_now(void);

/**
 * @brief Retourne la version firmware courante stockée en NVS.
 * @return Version (uint32_t) lue depuis NVS namespace "system", clé "fw_ver" ; ou CURRENT_FW_VERSION si NVS non lisible.
 */
uint32_t ota_manager_get_fw_version(void);

/**
 * @brief Indique si une mise à jour OTA est en cours (réception de chunks).
 * @return 1 si OTA en cours, 0 sinon. À utiliser pour afficher la LED violette.
 */
int ota_manager_is_ota_in_progress(void);

/**
 * @brief (ROOT) Indique si une diffusion OTA est en cours (envoi des chunks vers le mesh).
 * @return 1 si le ROOT diffuse des chunks, 0 sinon. Pendant ce temps les données mesh sont bloquées.
 */
int ota_manager_is_broadcast_active(void);

/**
 * @brief (ROOT) Active ou désactive le flag "diffusion OTA en cours". Appelé par serialGatewayTask.
 * @param active 1 = diffusion en cours (bloquer trames Data), 0 = terminé.
 */
void ota_manager_set_broadcast_active(int active);

/**
 * @brief (ROOT) Réception OTA série en cours (dès l’octet mode 0x01/0x02 jusqu’à fin). Bloque trames Data et sortie [MESH].
 * @param active 1 = réception en cours, 0 = terminée.
 */
void ota_manager_set_serial_receiving(int active);

/**
 * @brief (ROOT) Indique si une réception OTA série est en cours (ne pas envoyer trames, ne pas écrire [MESH]).
 */
int ota_manager_is_serial_receiving(void);

/**
 * @brief Traite un message JSON OTA reçu du mesh (legacy painlessMesh).
 */
void ota_manager_on_message(uint32_t from, const char *msg, size_t len);

/**
 * @brief Traite un paquet OTA_ADV binaire (payload = OtaAdvPayload, 38 octets).
 * @param payload Pointeur sur OtaAdvPayload (totalSize, totalChunks, md5Hex).
 * @param len Taille (doit être >= sizeof(OtaAdvPayload)).
 */
void ota_manager_on_ota_adv(const uint8_t *payload, size_t len);

/**
 * @brief Traite un paquet OTA_CHUNK binaire (payload = OtaChunkPayload, 204 octets).
 * @param payload Pointeur sur OtaChunkPayload (chunkIndex, totalChunks, data[200]).
 * @param len Taille (doit être >= sizeof(OtaChunkPayload)).
 */
void ota_manager_on_ota_chunk(const uint8_t *payload, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MANAGER_H */
