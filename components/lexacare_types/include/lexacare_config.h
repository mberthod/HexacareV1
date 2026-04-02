/**
 * @file lexacare_config.h
 * @brief Activation / désactivation des composants matériels LexaCare V1.
 *
 * Modifiez uniquement ce fichier pour adapter la configuration à votre
 * environnement de test ou de production.
 * 0 = composant absent / non connecté / désactivé
 * 1 = composant présent / connecté / activé
 *
 * Composants physiques du système :
 *
 *   ┌─ SPI2 ─────────────────────────────────────────────────────────────┐
 *   │  4 × VL53L8CX (CLK=17, MOSI=18, MISO=21, NCS0-3=1/2/42/41)       │
 *   └────────────────────────────────────────────────────────────────────┘
 *
 *   ┌─ I2C_NUM_0 (SDA=11, SCL=12) ───────────────────────────────────────┐
 *   │  PCA9555D @0x20 — LPn LIDAR + alimentation sous-systèmes           │
 *   └────────────────────────────────────────────────────────────────────┘
 *
 *   ┌─ I2C_NUM_1 (SDA=10, SCL=9) — capteurs sur carte radar ─────────────┐
 *   │  MLX90640ESF @0x33  — caméra thermique 32×24                       │
 *   │  HDC1080     @0x40  — température + humidité                       │
 *   │  BME280      @0x76  — pression + température + humidité            │
 *   │  VL53L0X     @0x29  — TOF simple (géré via DIST_SHUTDOWN PCA9555)  │
 *   │  CAT24M01W   @0x50  — EEPROM 1 Mbit (réservé)                      │
 *   └────────────────────────────────────────────────────────────────────┘
 *
 *   ┌─ UART_NUM_2 (RX=43, TX=44) ────────────────────────────────────────┐
 *   │  HLK-LD6002 — radar vital signs                                    │
 *   └────────────────────────────────────────────────────────────────────┘
 *
 *   ┌─ I2S (WS=6, SD=7, SCK=8) ──────────────────────────────────────────┐
 *   │  Microphone MEMS numérique                                         │
 *   └────────────────────────────────────────────────────────────────────┘
 */

#pragma once

/**
 * @defgroup group_config Configuration & Types
 * @brief Options simples pour activer/désactiver des sous-systèmes et décrire les données partagées.
 *
 * Objectif : permettre de passer facilement d'un montage “banc de test” (capteurs partiels,
 * modules absents) à un montage “production” sans modifier le code applicatif.
 *
 * @{
 */

/* ================================================================
 * LIDAR VL53L8CX (SPI)
 * Nombre de capteurs actifs (1–4).
 * Les LPn des capteurs absents resteront à 0 (reset).
 * ================================================================ */
#define LEXACARE_LIDAR_COUNT           4

/* ================================================================
 * LIDAR — Source des données (réel vs stub)
 *
 * 0 = STUB (données simulées) — utile tant que l'ULD ST n'est pas intégré.
 * 1 = RÉEL (ST ULD STSW-IMG040) — nécessite les sources ST dans
 *     components/sensor_acq/uld/ + CMake auto-détecté.
 * ================================================================ */
#define LEXACARE_LIDAR_USE_ST_ULD      1

/* ================================================================
 * Radar HLK-LD6002 (UART_NUM_2)
 * Mettre à 0 quand le module radar n'est PAS connecté.
 * → Évite le crash uart_read_bytes (IWD spinlock ringbuf IDF 6.0)
 * ================================================================ */
#define LEXACARE_ENABLE_RADAR          0

/* ================================================================
 * Capteurs I2C sur carte radar (I2C_NUM_1, SDA=10, SCL=9)
 * Ces capteurs nécessitent que POWER_RADAR soit activé.
 * ================================================================ */
#define LEXACARE_ENABLE_HDC1080        1   /**< @0x40 — temp + humidité      */
#define LEXACARE_ENABLE_BME280         1   /**< @0x76 — pression + temp + hum */
#define LEXACARE_ENABLE_MLX90640       0   /**< @0x33 — caméra thermique      */
#define LEXACARE_ENABLE_VL53L0X        0   /**< @0x29 — TOF simple (DIST_SHD) */
#define LEXACARE_ENABLE_CAT24M01W      0   /**< @0x50 — EEPROM 1Mbit          */

/* ================================================================
 * Microphone MEMS I2S (WS=6, SD=7, SCK=8)
 * ================================================================ */
#define LEXACARE_ENABLE_MIC            1

/* ================================================================
 * IA (détection chute)
 * Mettre à 0 tant qu'on ne veut QUE l'image LIDAR.
 * ================================================================ */
#define LEXACARE_ENABLE_AI             0

/* ================================================================
 * Réseau mesh ESP-NOW + WiFi
 * Mettre à 0 pour un boot ultrarapide sans réseau (test capteurs seuls)
 * ================================================================ */
#define LEXACARE_ENABLE_MESH           1

/* ================================================================
 * Alimentations sous-systèmes via PCA9555 Port 1
 *
 * IMPORTANT : activer POWER_RADAR pour alimenter les capteurs
 * HDC1080, BME280, MLX90640 et VL53L0X montés sur la carte radar.
 *
 * IO1.4 → POWER_FAN    (ventilateur)
 * IO1.5 → POWER_RADAR  (carte radar + ses capteurs I2C)
 * IO1.6 → POWER_MIC    (microphone MEMS)
 * IO1.7 → POWER_MLX    (alimentation dédiée MLX90640 via TPS22917)
 * ================================================================ */
#define LEXACARE_PWR_FAN_ENABLED       0
#define LEXACARE_PWR_RADAR_ENABLED     1   /**< Alimente HDC1080 + BME280 ! */
#define LEXACARE_PWR_MIC_ENABLED       1
#define LEXACARE_PWR_MLX_ENABLED       0

/** @} */ /* end of group_config */
