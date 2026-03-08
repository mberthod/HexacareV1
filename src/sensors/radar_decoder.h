/**
 * @file radar_decoder.h
 * @brief Décodage UART HLK-LD6002 (TinyFrame ou ASCII) : présence, respiration, rythme cardiaque, distance
 */

#ifndef RADAR_DECODER_H
#define RADAR_DECODER_H

#include "system/system_state.h"
#include "config/config.h"
#include <cstdint>
#include <cstdbool>

#ifdef __cplusplus
extern "C" {
#endif

// Initialise le décodeur et la liaison UART (RX/TX selon pins_lexacare).
void radar_decoder_init(void);

// À appeler depuis la tâche Radar : lit les octets disponibles, parse le protocole,
// met à jour les signes vitaux dans system_state.
void radar_decoder_poll(void);

// Retourne les derniers signes vitaux (copie).
void radar_decoder_get_vitals(vital_signs_t *out);

#ifdef __cplusplus
}
#endif

#endif // RADAR_DECODER_H
