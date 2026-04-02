/**
 * @file pins_config.h
 * @brief Définition des broches GPIO et du mapping PCA9555 pour LexaCare V1.
 *
 * Architecture hardware ESP32-S3-WROOM-2 :
 *
 *  SPI2  — 4 LIDARs VL53L8CX (NCS individuels) + SYNC
 *  UART  — Radar HLK-LD6002 (RX=43, TX=44)
 *  I2C0  — PCA9555D @0x20 (SDA=11, SCL=12) : LPn LIDAR + alim sous-systèmes
 *  I2C1  — MLX90640 (SDA=10, SCL=9) — réservé, driver non implémenté
 *  I2S   — Microphone MEMS numérique (WS=6, SD=7, SCK=8)
 *
 *  PCA9555D Port 0 :
 *    IO0.0 → DIST_SHUTDOWN : shutdown VL53L0X (TOF simple, I2C MLX bus)
 *    IO0.1 → GPIO radar LD6002 — Hi-Z (non utilisé)
 *    IO0.2 → GPIO radar LD6002 — Hi-Z (non utilisé)
 *    IO0.3 → LPn_4 : LPn LIDAR 4 (VL53L8CX)
 *    IO0.4 → LPn_3 : LPn LIDAR 3 (VL53L8CX)
 *    IO0.5 → LPn_2 : LPn LIDAR 2 (VL53L8CX)
 *    IO0.6 → LPn_1 : LPn LIDAR 1 (VL53L8CX)
 *    IO0.7 → RADAR_LPN : LPn LIDAR carte radar (option, non connecté)
 *
 *  PCA9555D Port 1 :
 *    IO1.0–IO1.3 → non utilisés (Hi-Z)
 *    IO1.4 → POWER_FAN   : alimentation ventilateur
 *    IO1.5 → POWER_RADAR : alimentation carte radar
 *    IO1.6 → POWER_MIC   : alimentation microphone
 *    IO1.7 → POWER_MLX   : alimentation capteur MLX
 *
 * IMPORTANT : vérifier ces assignations contre le schéma électrique final
 *             avant tout flashage sur le matériel de production.
 */

#pragma once

/**
 * @ingroup group_config
 * @brief Mapping “humain” entre le schéma électronique et le code.
 *
 * Pourquoi c'est important :
 * - sur un système multi-capteurs, une seule broche mal mappée peut rendre tout le bus muet
 * - les expandeurs (PCA9555) ajoutent une couche d'indirection : ces defines évitent les erreurs
 */

/* ================================================================
 * Bus I2C 0 (I2C_NUM_0) — PCA9555D @0x20
 * ================================================================ */
#define PIN_I2C0_SDA        11
#define PIN_I2C0_SCL        12

/* ================================================================
 * Bus I2C 1 (I2C_NUM_1) — MLX90640 (réservé, driver non implémenté)
 * ================================================================ */
#define PIN_I2C1_SDA        10
#define PIN_I2C1_SCL        9

/* ================================================================
 * Bus SPI LIDAR (SPI2_HOST) — 4 capteurs VL53L8CX
 *
 * Numérotation hardware vs code :
 *   NCS0 (GPIO1)  → LIDAR 1 (LPn_1 = PCA9555 IO0.6)
 *   NCS1 (GPIO2)  → LIDAR 2 (LPn_2 = PCA9555 IO0.5)
 *   NCS2 (GPIO42) → LIDAR 3 (LPn_3 = PCA9555 IO0.4)
 *   NCS3 (GPIO41) → LIDAR 4 (LPn_4 = PCA9555 IO0.3)
 *
 * Mode SPI 3 (CPOL=1, CPHA=1) — max 10 MHz.
 * SYNC (GPIO14) : synchronisation multi-capteurs.
 * ================================================================ */
#define LIDAR_SPI_HOST      SPI2_HOST
#define PIN_LIDAR_CLK       17
#define PIN_LIDAR_MOSI      18
#define PIN_LIDAR_MISO      21
#define PIN_LIDAR_SYNC      14

#define PIN_LIDAR_NCS0      1    /**< NCS LIDAR 1 */
#define PIN_LIDAR_NCS1      2    /**< NCS LIDAR 2 */
#define PIN_LIDAR_NCS2      42   /**< NCS LIDAR 3 */
#define PIN_LIDAR_NCS3      41   /**< NCS LIDAR 4 */

/* ================================================================
 * PCA9555D — Expandeur GPIO I2C (@0x20, A0=A1=A2=GND)
 * ================================================================ */
#define PCA9555_I2C_ADDR        0x20

/* Adresses des registres PCA9555 */
#define PCA9555_REG_OUTPUT_0    0x02  /**< Sortie Port 0 */
#define PCA9555_REG_OUTPUT_1    0x03  /**< Sortie Port 1 */
#define PCA9555_REG_CONFIG_0    0x06  /**< Config Port 0 (0=sortie, 1=entrée) */
#define PCA9555_REG_CONFIG_1    0x07  /**< Config Port 1 (0=sortie, 1=entrée) */

/* --- Port 0 --- */
/* IO0.0 : DIST_SHUTDOWN — shutdown VL53L0X (TOF simple sur bus I2C MLX) */
#define PCA9555_BIT_DIST_SHUTDOWN   (1u << 0)  /**< IO0.0 — actif = TOF en veille */

/* IO0.1, IO0.2 : GPIO radar LD6002 — laissés en entrée (Hi-Z) */
/* (pas de define actif — configurés en entrée dans pca9555_init) */

/* IO0.3 – IO0.6 : LPn VL53L8CX (actif bas = reset, haut = opérationnel) */
#define PCA9555_BIT_LPN4    (1u << 3)  /**< IO0.3 → LPn_4 LIDAR 4 (NCS3/GPIO41) */
#define PCA9555_BIT_LPN3    (1u << 4)  /**< IO0.4 → LPn_3 LIDAR 3 (NCS2/GPIO42) */
#define PCA9555_BIT_LPN2    (1u << 5)  /**< IO0.5 → LPn_2 LIDAR 2 (NCS1/GPIO2)  */
#define PCA9555_BIT_LPN1    (1u << 6)  /**< IO0.6 → LPn_1 LIDAR 1 (NCS0/GPIO1)  */

/* IO0.7 : LPn LIDAR carte radar (option — non connecté actuellement) */
#define PCA9555_BIT_RADAR_LPN   (1u << 7)  /**< IO0.7 → LPn LIDAR radar (réservé) */

/* Masque de config Port 0 :
 *   IO0.0 = sortie (DIST_SHUTDOWN)
 *   IO0.1, IO0.2 = entrée Hi-Z (radar GPIO non utilisés)
 *   IO0.3 – IO0.7 = sorties (LPn + RADAR_LPN)
 *   Config register : 0 = sortie, 1 = entrée → 0b00000110 = 0x06 */
#define PCA9555_CFG0_MASK   0x06u

/* --- Port 1 --- */
/* IO1.0 – IO1.3 : non utilisés — laissés en entrée (Hi-Z) */
/* IO1.4 – IO1.7 : alimentations sous-systèmes */
#define PCA9555_BIT_PWR_FAN     (1u << 4)  /**< IO1.4 → Alimentation ventilateur */
#define PCA9555_BIT_PWR_RADAR   (1u << 5)  /**< IO1.5 → Alimentation carte radar  */
#define PCA9555_BIT_PWR_MIC     (1u << 6)  /**< IO1.6 → Alimentation microphone   */
#define PCA9555_BIT_PWR_MLX     (1u << 7)  /**< IO1.7 → Alimentation MLX90640     */

/* Masque de config Port 1 :
 *   IO1.0–IO1.3 = entrée Hi-Z, IO1.4–IO1.7 = sorties → 0b00001111 = 0x0F */
#define PCA9555_CFG1_MASK   0x0Fu

/* ================================================================
 * UART — Radar HLK-LD6002 (TinyFrame, 1 382 400 bauds, UART_NUM_2)
 * ================================================================ */
#define PIN_RADAR_RX        43
#define PIN_RADAR_TX        44

/* ================================================================
 * Microphone MEMS numérique — interface I2S
 * MIK_WS  = Word Select / LRCK
 * MIK_SD  = Serial Data (données audio)
 * MIC_SDK = Serial Clock / BCLK
 * (driver non implémenté — réservé)
 * ================================================================ */
#define PIN_MIC_WS          6
#define PIN_MIC_SD          7
#define PIN_MIC_SCK         8

/* ================================================================
 * LED de statut NeoPixel WS2812
 * ================================================================ */
#define PIN_LED_STATUS      38

/* ================================================================
 * Sélection ROOT / NODE (pull-up interne, niveau bas = ROOT)
 * ================================================================ */
#define PIN_ROOT_SEL        35

