/**
 * @file platform.h
 * @brief Adaptateur plateforme pour l'ULD ST VL53L8CX (STSW-IMG040).
 *
 * L'ULD ST inclut un fichier `platform.h` que le client doit fournir.
 * Dans ce firmware, la couche plateforme (SPI) est implémentée par :
 *   components/sensor_acq/include/vl53l8cx_platform.h
 *
 * Ce fichier fait le lien entre l'ULD (API ST) et l'implémentation ESP-IDF.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* Nombre de cibles par zone (1 = plus simple/rapide, suffisant pour la matrice 8×32) */
#ifndef VL53L8CX_NB_TARGET_PER_ZONE
#define VL53L8CX_NB_TARGET_PER_ZONE 1
#endif

/* Désactive des sorties optionnelles pour réduire la charge/bandwidth (optionnel) */
/* #define VL53L8CX_DISABLE_AMBIENT_PER_SPAD */

/* Plateforme SPI + fonctions PAL attendues par l'ULD */
#include "vl53l8cx_platform.h"

/**
 * @brief Swap endianess par mots 32 bits (implémentation typique ST).
 *
 * L'ULD manipule des buffers bruts (endianness capteur) et attend que la
 * plateforme fournisse cette primitive.
 */
static inline void VL53L8CX_SwapBuffer(uint8_t *buffer, uint16_t size)
{
    if (!buffer) return;

    /* Swap par blocs de 4 octets (uint32_t) */
    for (uint16_t i = 0; (uint16_t)(i + 3U) < size; i = (uint16_t)(i + 4U)) {
        const uint8_t b0 = buffer[i + 0U];
        const uint8_t b1 = buffer[i + 1U];
        buffer[i + 0U] = buffer[i + 3U];
        buffer[i + 1U] = buffer[i + 2U];
        buffer[i + 2U] = b1;
        buffer[i + 3U] = b0;
    }
}

