/* lexa_config.h — paramètres techniques LexaCare
 *
 * Pinout hardware, MFCC, tailles d'arenas TFLM, fréquences bus.
 * Doit matcher le PCB LexaCare V1 rev A.
 */
#pragma once

/* ------------------------------------------------------------------
 * ToF — VL53L8CX × 4 sur SPI partagé
 * ------------------------------------------------------------------ */
#define LEXA_TOF_SPI_CLK_GPIO       4
#define LEXA_TOF_SPI_MOSI_GPIO      15
#define LEXA_TOF_SPI_MISO_GPIO      21
/* 2 MHz = HAL ULD Arduino (platform.cpp SPISettings(2000000,...)) pour init + ranging.
 * Le ping brut du .ino utilise 400 kHz seulement pour WrByte/RdByte avant vl53l8cx_init. */
#define LEXA_TOF_SPI_FREQ_HZ        2000000

/* Enable rail alimentation LIDAR (load switch, GPIO MCU direct) */
#define LEXA_TOF_POWER_EN_GPIO      40

/* NCS par capteur. GPIO 1 et 2 = strapping pins — config output HIGH
 * avant toute autre init dans app_main ! */
#define LEXA_TOF_NCS_0_GPIO         1
#define LEXA_TOF_NCS_1_GPIO         2
#define LEXA_TOF_NCS_2_GPIO         42
#define LEXA_TOF_NCS_3_GPIO         41

/* INT ToF (wired-OR), SYNC, INT PCA9555 — option driver ultérieur */
#define LEXA_TOF_INT_GPIO           13
#define LEXA_TOF_SYNC_GPIO          14
#define LEXA_PCA9555_INT_GPIO       5

/* ------------------------------------------------------------------
 * I2C — PCA9555 I/O expander pour LPn ToF
 * ------------------------------------------------------------------ */
#define LEXA_I2C_SDA_GPIO           11
#define LEXA_I2C_SCL_GPIO           12
#define LEXA_I2C_FREQ_HZ            100000
#define LEXA_PCA9555_ADDR           0x20

/* I2C second bus (ESP-IDF I2C_NUM_1) : MLX90640 uniquement (TMP117 / VL53L0 sont sur I2C_NUM_0).
 * Alimentation MLX90640 : POWER_RADAR via PCA9555 IO1.5 (port1 bit 5), actif haut. */
#define LEXA_I2C_MLX_SDA_GPIO       10
#define LEXA_I2C_MLX_SCL_GPIO       9

#define LEXA_PCA9555_IO1_POWER_RADAR_MASK  (1u << 5)

/* Adresses I2C 7 bits — schéma LexaCare V1 rev A (à valider si straps EEPROM) */
#define LEXA_I2C_ADDR_DS3231     0x68
#define LEXA_I2C_ADDR_CAT24      0x50
#define LEXA_I2C_ADDR_BME280_76  0x76
#define LEXA_I2C_ADDR_BME280_77  0x77
#define LEXA_I2C_ADDR_HDC1080    0x40
#define LEXA_I2C_ADDR_TMP117     0x48
#define LEXA_I2C_ADDR_VL53L0     0x29
#define LEXA_I2C_ADDR_MLX90640   0x33

/* LD6002 (UART0 réaffecté) — ne pas utiliser pour ESP_LOG en prod si radar actif */
#define LEXA_RADAR_UART_TX_GPIO     43
#define LEXA_RADAR_UART_RX_GPIO     44
#define LEXA_RADAR_NCS_GPIO         3

/* ------------------------------------------------------------------
 * Audio — I2S stéréo (ICS-43434, mot 32 bits / donnée 24 bits MSB)
 *
 * Les deux slots Philips (L puis R) sont convertis séparément : pas de copie L→R côté firmware.
 *
 * Seeed XIAO ESP32-S3 (sketch Arduino de référence) : MIC_SCK=8, MIK_WS=6,
 * MIK_SD=7. PCB LexaCare V1 rev A historique : BCLK=5, WS=6, DIN=7.
 * ------------------------------------------------------------------ */
#define LEXA_I2S_BCLK_GPIO          8
#define LEXA_I2S_WS_GPIO            6
#define LEXA_I2S_DIN_GPIO           7
#define LEXA_I2S_SAMPLE_RATE_HZ     16000
#define LEXA_I2S_BITS_PER_SAMPLE    16

/* Atténuation micro (bits retirés après extraction 24-bit). 0 = recommandé.
 * Augmenter seulement si saturation int16 persiste (2, 3…). */
#ifndef LEXA_I2S_PCM_EXTRA_DOWNSHIFT
#define LEXA_I2S_PCM_EXTRA_DOWNSHIFT 0
#endif

/* Gain sortie int16 : décalage à gauche (×2^n), saturé. Utile si crête ~±200–500
 * avec downshift=0 (ex. n=5 → ×32). Baisser si clip, monter si trop faible. Max 7. */
#ifndef LEXA_I2S_PCM_OUTPUT_SHIFT
#define LEXA_I2S_PCM_OUTPUT_SHIFT 10
#endif

/* ------------------------------------------------------------------
 * MFCC — identique Python (librosa) pour bit-équivalence
 * Voir skills/mfcc-validation/SKILL.md pour la procédure de validation.
 * ------------------------------------------------------------------ */
#define LEXA_MFCC_SAMPLE_RATE_HZ    16000
#define LEXA_MFCC_N_FFT             512
#define LEXA_MFCC_HOP               256
#define LEXA_MFCC_N_MEL             40
#define LEXA_MFCC_N_COEFF           13
#define LEXA_MFCC_FMIN_HZ           20
#define LEXA_MFCC_FMAX_HZ           8000
#define LEXA_MFCC_PREEMPH           0.97f

/* ------------------------------------------------------------------
 * TFLM arenas (en PSRAM, allouées au boot)
 * ------------------------------------------------------------------ */
#define LEXA_TFLM_ARENA_AUDIO_KB    128
#define LEXA_TFLM_ARENA_VISION_KB    64

#define LEXA_TFLM_ARENA_AUDIO_BYTES  (LEXA_TFLM_ARENA_AUDIO_KB * 1024)
#define LEXA_TFLM_ARENA_VISION_BYTES (LEXA_TFLM_ARENA_VISION_KB * 1024)

/* ------------------------------------------------------------------
 * Télémétrie USB JSON unique (schéma v3) — audio / lidar embarqués dans LXJS
 * ------------------------------------------------------------------ */
#define LEXA_USB_JSON_AUDIO_SAMPLES   64
#define LEXA_USB_JSON_LIDAR_MM_MAX    4000
#define LEXA_USB_JSON_BUF_BYTES       (128 * 1024)
#define LEXA_MLX_THERMAL_COLS         32
#define LEXA_MLX_THERMAL_ROWS         24
#define LEXA_MLX_THERMAL_CELLS        (LEXA_MLX_THERMAL_COLS * LEXA_MLX_THERMAL_ROWS)

/* ------------------------------------------------------------------
 * Pinout alerte (sortie GPIO, optionnel — peut être désactivé si la
 * seule sortie est le mesh)
 * ------------------------------------------------------------------ */
#define LEXA_ALERT_GPIO             48     /* LED on-board devkit, à remapper sur PCB */
