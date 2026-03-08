/**
 * @file lexacare_protocol.h
 * @brief Le Dictionnaire (Définition du Langage Commun).
 * 
 * @details
 * Pour que les boîtiers puissent se comprendre, ils doivent parler exactement la même langue.
 * Ce fichier définit la grammaire et le vocabulaire de cette langue binaire.
 * 
 * Il définit deux "phrases" principales :
 * 
 * 1. **L'Enveloppe (EspNowMeshHeader)** :
 *    - C'est ce qui est écrit sur l'enveloppe du courrier.
 *    - Qui l'envoie ? (Source ID)
 *    - C'est quel type de courrier ? (Données, Mise à jour...)
 *    - Combien de temps peut-il voyager ? (TTL)
 * 
 * 2. **La Lettre (LexaFullFrame)** :
 *    - C'est le contenu du courrier, les données utiles.
 *    - Contient tout : Pouls, Température, Batterie, Alerte Chute...
 *    - Tout est compressé au maximum pour tenir dans 32 octets (très petit !).
 *    - Exemple : La température 25.5°C est stockée comme le nombre entier 2550.
 */

#ifndef LEXACARE_PROTOCOL_H
#define LEXACARE_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/** Taille fixe de la trame en octets. */
#define LEXA_FRAME_SIZE 32

/** Types de message ESP-NOW Mesh (msgType dans EspNowMeshHeader). */
#define MSG_TYPE_DATA      0x01  ///< Payload = LexaFullFrame (32 octets)
#define MSG_TYPE_OTA_ADV   0x02  ///< Payload = OtaAdvPayload (annonce OTA)
#define MSG_TYPE_OTA_CHUNK 0x03  ///< Payload = OtaChunkPayload (bloc firmware)
#define ESPNOW_MSG_TYPE_DATA     MSG_TYPE_DATA
#define ESPNOW_MSG_TYPE_OTA_ADV  MSG_TYPE_OTA_ADV
#define ESPNOW_MSG_TYPE_OTA_CHUNK MSG_TYPE_OTA_CHUNK

/** Taille max payload ESP-NOW (octets). */
#define ESPNOW_MAX_PAYLOAD 250

/**
 * @struct EspNowMeshHeader
 * @brief En-tête de routage pour chaque paquet ESP-NOW (8 octets packed).
 * @details Permet l'anti-doublon (msgId), le TTL et le typage (Data / OTA_ADV / OTA_CHUNK).
 */
struct __attribute__((packed)) EspNowMeshHeader {
    uint32_t msgId;          ///< ID unique du message (anti-doublon)
    uint8_t  msgType;        ///< ESPNOW_MSG_TYPE_* (Data, OTA_ADV, OTA_CHUNK)
    uint8_t  ttl;            ///< Time To Live (décrémenté à chaque saut)
    uint16_t sourceNodeId;   ///< Auteur original (2 derniers octets MAC)
};

/** Taille de l'en-tête mesh. */
#define ESPNOW_MESH_HEADER_SIZE 8

#if defined(__cplusplus)
static_assert(sizeof(struct EspNowMeshHeader) == ESPNOW_MESH_HEADER_SIZE, "EspNowMeshHeader must be 8 bytes");
#endif

// OTA payloads removed from here, see mesh_tree_protocol.h

/**
 * @struct LexaFullFrame
 * @brief Trame packed 32 octets, sans padding.
 * @details CRC16-CCITT calculé sur les 30 premiers octets (champ crc16 exclu).
 */
struct __attribute__((packed)) LexaFullFrame {
    uint16_t nodeShortId;     ///< 2 derniers octets de la MAC
    uint32_t epoch;            ///< Timestamp type Unix (DS3231 simulé)
    uint8_t  probFallLidar;   ///< Probabilité chute Lidar (0-100)
    uint8_t  probFallAudio;   ///< Probabilité chute Audio (0-100)
    uint8_t  heartRate;       ///< BPM (60-120)
    uint8_t  respRate;        ///< Respiration (12-25)
    int16_t  tempExt;         ///< TMP117 : Temp * 100
    uint16_t humidity;        ///< HDC1080 : % * 100
    uint16_t pressure;        ///< BME280 : (hPa - 800) * 10
    int16_t  thermalMax;      ///< Pic MLX dans le lit : Temp * 100
    uint8_t  volumeOccupancy; ///< Lidar volume index (0-255)
    uint16_t vBat;            ///< Tension batterie (mV)
    uint16_t sensorFlags;     ///< Bitfield état capteurs
    uint16_t parentId;        ///< ID du parent (pour topologie)
    uint8_t  layer;           ///< Couche réseau (pour topologie)
    uint8_t  reserved[4];     ///< Réservé (7 - 2 - 1 = 4)
    uint16_t crc16;           ///< CRC16-CCITT (False), calculé sur les 30 premiers octets
};

/** Vérification à la compilation : taille exacte 32 octets. */
#if defined(__cplusplus)
static_assert(sizeof(struct LexaFullFrame) == LEXA_FRAME_SIZE, "LexaFullFrame must be 32 bytes");
#else
_Static_assert(sizeof(struct LexaFullFrame) == LEXA_FRAME_SIZE, "LexaFullFrame must be 32 bytes");
#endif

typedef struct LexaFullFrame LexaFullFrame_t;
typedef struct EspNowMeshHeader EspNowMeshHeader_t;
// OTA typedefs removed from here, see mesh_tree_protocol.h

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calcule le CRC16-CCITT (False) : init 0xFFFF, poly 0x1021.
 * @param data Pointeur sur les données
 * @param len  Nombre d'octets (sans le champ crc16)
 * @return CRC16 calculé
 */
uint16_t calculateCRC16(const uint8_t *data, size_t len);

/**
 * @brief Remplit le champ crc16 de la trame (sur les 30 premiers octets).
 * @param frame Trame à compléter (champ crc16 ignoré pour le calcul)
 */
void lexaframe_fill_crc(LexaFullFrame_t *frame);

/**
 * @brief Vérifie le CRC16 de la trame reçue.
 * @param frame Trame reçue (32 octets)
 * @return 1 si CRC valide, 0 sinon
 */
int lexaframe_verify_crc(const LexaFullFrame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* LEXACARE_PROTOCOL_H */
