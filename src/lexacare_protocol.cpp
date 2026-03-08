/**
 * @file lexacare_protocol.cpp
 * @brief Implémentation CRC16-CCITT (False) et helpers LexaFullFrame.
 * @details Polynôme 0x1021, valeur initiale 0xFFFF. Le CRC est calculé sur les 30 premiers
 * octets de la trame (champ crc16 exclu). Utilisé pour valider l'intégrité des trames
 * reçues sur le mesh ; toute trame avec CRC invalide est rejetée.
 */

#include "lexacare_protocol.h"
#include <string.h>

#define CRC16_CCITT_POLY  0x1021
#define CRC16_CCITT_INIT  0xFFFF
#define LEXA_CRC_LEN      30  /**< Octets utilisés pour le CRC (sans crc16) */

/**
 * @brief Calcule le CRC16-CCITT (False) sur une région mémoire.
 * @param data Pointeur sur les données (peut être NULL)
 * @param len  Nombre d'octets
 * @return CRC16 calculé, ou 0 si data est NULL
 */
uint16_t calculateCRC16(const uint8_t *data, size_t len) {
    if (!data) return 0;
    uint16_t crc = CRC16_CCITT_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int k = 0; k < 8; k++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ CRC16_CCITT_POLY;
            else
                crc = crc << 1;
        }
    }
    return crc;
}

/**
 * @brief Remplit le champ crc16 de la trame.
 * @param frame Trame à compléter (peut être NULL)
 */
void lexaframe_fill_crc(LexaFullFrame_t *frame) {
    if (!frame) return;
    frame->crc16 = calculateCRC16((const uint8_t *)frame, LEXA_CRC_LEN);
}

/**
 * @brief Vérifie le CRC16 de la trame reçue.
 * @param frame Trame reçue (32 octets, peut être NULL)
 * @return 1 si CRC valide, 0 sinon
 */
int lexaframe_verify_crc(const LexaFullFrame_t *frame) {
    if (!frame) return 0;
    uint16_t computed = calculateCRC16((const uint8_t *)frame, LEXA_CRC_LEN);
    return (computed == frame->crc16) ? 1 : 0;
}
