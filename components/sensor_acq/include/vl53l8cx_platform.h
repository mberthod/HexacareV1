/**
 * @file vl53l8cx_platform.h
 * @brief Couche d'abstraction plateforme pour l'ULD VL53L8CX de ST — interface SPI.
 *
 * Implémente la Platform Abstraction Layer (PAL) exigée par l'Ultra-Lite Driver (ULD)
 * VL53L8CX de STMicroelectronics, via l'API SPI master d'ESP-IDF.
 *
 * Protocole SPI VL53L8CX (datasheet) :
 *   - Mode SPI 3 (CPOL=1, CPHA=1), max 10 MHz typ., NCS actif bas, MSB first.
 *   - Adresse registre 16 bits, big-endian sur le fil.
 *   - Écriture : bit15=0 → [addr_hi & 0x7F][addr_lo][data...]
 *   - Lecture  : bit15=1 → [addr_hi | 0x80][addr_lo][dummy...] → RX [xx][xx][data...]
 *
 * Implémentation ESP-IDF (voir vl53l8cx_platform.c) :
 *   - Buffers TX/RX en **RAM interne DMA** (pas PSRAM) ; écritures avec buffer RX
 *     « dummy » (full-duplex, sinon MISO peut rester à 0xFF).
 *   - Transferts jusqu’à **0x8000+2** octets (téléchargement firmware ULD).
 *
 * INSTALLATION DE L'ULD :
 *   1. Télécharger le VL53L8CX ULD depuis st.com (STSW-IMG040).
 *   2. Copier les fichiers source ULD dans components/sensor_acq/uld/.
 *   3. Ajouter les sources ULD à la liste SRCS du CMakeLists.txt.
 *   4. Retirer la macro LIDAR_STUB_MODE de lidar_driver.c.
 */

#pragma once

#include <stdint.h>
#include "driver/spi_master.h"

/**
 * @ingroup group_sensor_acq
 * @brief Couche “adaptateur” SPI requise par l'ULD ST (VL53L8CX).
 *
 * But : permettre d'utiliser un driver ST “standard” en ne codant que
 * les primitives SPI (lire/écrire/attendre), sans réinventer le protocole.
 */

/* ================================================================
 * VL53L8CX_Platform
 * @brief Contexte de la couche plateforme — un handle SPI par capteur.
 *        Embarqué dans VL53L8CX_Configuration de l'ULD.
 * ================================================================ */
typedef struct {
    spi_device_handle_t spi_dev; /**< Handle SPI ESP-IDF du capteur */
    uint16_t address;            /**< Champ requis par l'ULD (mode I2C). Inutilisé en SPI. */
} VL53L8CX_Platform;

/* ================================================================
 * Fonctions de la PAL — appelées exclusivement par l'ULD VL53L8CX
 * ================================================================ */

/**
 * @brief Valide le handle SPI (device déjà ajouté au bus par hw_diag).
 *
 * @param p_platform Contexte plateforme (contient le handle SPI).
 * @return 0 si succès (convention ULD ST).
 */
uint8_t VL53L8CX_PlatformInit(VL53L8CX_Platform *p_platform);

/**
 * @brief Lit un octet depuis un registre 16 bits du VL53L8CX.
 * @param p_platform Contexte plateforme.
 * @param RegisterAddress Adresse 16 bits du registre.
 * @param p_value Valeur lue (1 octet).
 * @return 0 si succès (convention ULD ST).
 */
uint8_t VL53L8CX_RdByte(VL53L8CX_Platform *p_platform,
                          uint16_t RegisterAddress, uint8_t *p_value);

/**
 * @brief Écrit un octet dans un registre 16 bits du VL53L8CX.
 * @param p_platform Contexte plateforme.
 * @param RegisterAddress Adresse 16 bits du registre.
 * @param value Octet à écrire.
 * @return 0 si succès (convention ULD ST).
 */
uint8_t VL53L8CX_WrByte(VL53L8CX_Platform *p_platform,
                          uint16_t RegisterAddress, uint8_t value);

/**
 * @brief Lit plusieurs octets depuis un registre 16 bits du VL53L8CX.
 * @param p_platform Contexte plateforme.
 * @param RegisterAddress Adresse 16 bits du registre.
 * @param p_values Buffer de sortie (taille >= size).
 * @param size Nombre d'octets à lire.
 * @return 0 si succès (convention ULD ST).
 */
uint8_t VL53L8CX_RdMulti(VL53L8CX_Platform *p_platform,
                           uint16_t RegisterAddress,
                           uint8_t *p_values, uint32_t size);

/**
 * @brief Écrit plusieurs octets vers un registre 16 bits du VL53L8CX.
 * @param p_platform Contexte plateforme.
 * @param RegisterAddress Adresse 16 bits du registre.
 * @param p_values Données à écrire.
 * @param size Nombre d'octets à écrire.
 * @return 0 si succès (convention ULD ST).
 */
uint8_t VL53L8CX_WrMulti(VL53L8CX_Platform *p_platform,
                           uint16_t RegisterAddress,
                           uint8_t *p_values, uint32_t size);

/**
 * @brief Attend un délai en millisecondes.
 * @param p_platform Contexte plateforme (non utilisé, mais imposé par l'ULD).
 * @param TimeMs Durée d'attente (ms).
 * @return 0 si succès (convention ULD ST).
 */
uint8_t VL53L8CX_WaitMs(VL53L8CX_Platform *p_platform, uint32_t TimeMs);
