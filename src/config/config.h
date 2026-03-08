/**
 * @file config.h
 * @brief Constantes globales, seuils et tailles pour le firmware Lexacare.
 * 
 * Ce fichier centralise toutes les configurations du système : réseau, capteurs,
 * paramètres RTOS et gestion des événements.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>
#include <cstddef>

/** @name Réseau / Mesh painlessMesh
 *  painlessMesh (LEXACARE_MESH_PAINLESS) ou ESP-NOW (LEXACARE_MESH_32B).
 *  @{ */
#define MESH_SSID       "LexacareMesh"      ///< SSID painlessMesh
#define MESH_PASSWORD   "LexacareMeshSecret"
#define MESH_PORT       (5555)
#define MQTT_SERVER     "broker.hivemq.com"
#define MQTT_PORT       (1883)
#define MQTT_TOPIC_PUB  "lexacare/device"   ///< Données JSON sortent sur Serial (pas MQTT WiFi)
#define MQTT_PUB_INTERVAL_MS (5000)
/** @} */

/** @name ESP-NOW Mesh
 *  Réseau mesh entre cartes basé sur ESP-NOW (sans WiFi-AP/STA classique).
 *  @{ */
#define ESPNOW_CHANNEL      (1)    ///< Canal WiFi pour ESP-NOW (1–14)
#define ESPNOW_MAX_PEERS    (6)    ///< Nombre max de pairs (pour liste manuelle)
#define ESPNOW_SEND_MS     (2000) ///< Intervalle d’envoi test (ms)
#define ESPNOW_PAYLOAD_MAX     (250)  ///< Taille max payload ESP-NOW (octets)
#define ESPNOW_MSG_CACHE_SIZE  (100)  ///< Cache anti-doublon (100 msgId, spec Managed Flooding)
#define ESPNOW_TTL_DEFAULT     (10)   ///< TTL initial
#define ESPNOW_JITTER_MS_MIN   (10)   ///< Jitter min avant retransmission (ms)
#define ESPNOW_JITTER_MS_MAX   (100)  ///< Jitter max avant retransmission (ms)
#define QUEUE_ESPNOW_RX_LEN   (10)    ///< Taille queue RX (paquets recus)
#define QUEUE_ESPNOW_TX_LEN   (4)     ///< Taille queue TX (trames Data)
/** @} */

/** @name OTA
 *  @{ */
#define OTA_SKIP_MD5_VERIFY   (1)    ///< 1 = ignorer vérif MD5 et boot quand même (test uniquement, risqué)
/** @} */

/** @name Lidar (VL53L8CX)
 *  Configuration de la matrice de profondeur 32x8.
 *  @{ */
#define LIDAR_COUNT           (4)           ///< Nombre de capteurs VL53L8CX
#define LIDAR_MATRIX_ROWS     (8)           ///< Nombre de lignes de la matrice globale
#define LIDAR_MATRIX_COLS     (32)          ///< Nombre de colonnes de la matrice globale
#define LIDAR_I2C_ADDR_BASE   (0x54)        ///< Adresse I2C de base pour le réadressage
#define LIDAR_I2C_FREQ_HZ     (400000)      ///< Fréquence du bus I2C (400kHz)
#define LIDAR_FALL_DIST_THRESHOLD_MM   (800) ///< Seuil de distance pour la détection au sol
#define LIDAR_FALL_VARIATION_MM        (500) ///< Variation de distance (mm) déclenchant une chute
#define LIDAR_FALL_HISTORY_LEN         (8)   ///< Longueur de l'historique pour le filtrage
#define LIDAR_MAX_RETRIES             (3)    ///< Nombre max de tentatives I2C
/** @} */

/** @name Radar HLK-LD6002
 *  Configuration de la liaison UART avec le radar.
 *  @{ */
#define RADAR_UART_BAUD       (115200)      ///< Vitesse UART
#define RADAR_UART_RX_BUF     (256)         ///< Taille du buffer de réception
#define RADAR_UART_TX_BUF     (128)         ///< Taille du buffer d'émission
/** @} */

/** @name Audio I2S
 *  Configuration de l'acquisition DMA pour les micros ICS-43434.
 *  @{ */
#define AUDIO_SAMPLE_RATE     (16000)       ///< Fréquence d'échantillonnage (Hz)
#define AUDIO_DMA_BUF_COUNT   (8)           ///< Nombre de buffers DMA
#define AUDIO_DMA_BUF_LEN     (512)         ///< Taille de chaque buffer DMA
#define AUDIO_FFT_SIZE        (256)         ///< Taille de la fenêtre FFT
#define AUDIO_PEAK_THRESHOLD  (2000)        ///< Seuil de détection de pic sonore
/** @} */

/** @name ADC / Rails de tension
 *  Surveillance des alimentations système.
 *  @{ */
#define ADC_SAMPLES_SMOOTH    (16)          ///< Nombre d'échantillons pour le lissage
#define ADC_ATTEN             (ADC_11db)    ///< Atténuation ADC (obsolète, remplacé par DB_12)
#define ADC_WIDTH             (ADC_BITWIDTH_12) ///< Résolution ADC
#define ADC_VREF_MV           (3300)        ///< Tension de référence (mV)
/** @} */

/** @name FreeRTOS
 *  Configuration des tâches et de l'assignation des cœurs.
 *  @{ */
#define TASK_LIDAR_STACK      (4096)        ///< Pile tâche Lidar
#define TASK_RADAR_STACK      (2048)        ///< Pile tâche Radar
#define TASK_AUDIO_STACK      (4096)        ///< Pile tâche Audio
#define TASK_ANALOG_STACK     (2048)        ///< Pile tâche Analogique
#define TASK_MESH_STACK       (4096)        ///< Pile tâche Mesh
#define TASK_PRIO_LIDAR       (configMAX_PRIORITIES - 2) ///< Priorité haute
#define TASK_PRIO_RADAR       (configMAX_PRIORITIES - 2) ///< Priorité haute
#define TASK_PRIO_AUDIO       (configMAX_PRIORITIES - 2) ///< Priorité haute
#define TASK_PRIO_ANALOG      (configMAX_PRIORITIES - 2) ///< Priorité haute
#define TASK_PRIO_MESH        (1)           ///< Priorité basse (Core 0)
#define CORE_APP              (1)           ///< Cœur pour les capteurs (APP_CPU)
#define CORE_PRO              (0)           ///< Cœur pour le réseau (PRO_CPU)
/** @} */

/** @name Queues et Événements
 *  Communication inter-tâches.
 *  @{ */
#define QUEUE_VITALS_LEN      (4)           ///< Taille de la queue des signes vitaux
#define QUEUE_OTA_FRAG_LEN    (8)           ///< Taille de la queue des fragments OTA
#define EVENT_BIT_FALL        (1 << 0)      ///< Bit : Chute détectée
#define EVENT_BIT_VITALS      (1 << 1)      ///< Bit : Signes vitaux mis à jour
#define EVENT_BIT_OTA_READY   (1 << 2)      ///< Bit : Mise à jour OTA prête
#define EVENT_OTA_READY       (1 << 2)      ///< Alias pour EVENT_BIT_OTA_READY
#define EVENT_OTA_FAIL        (1 << 3)      ///< Bit : Échec OTA
/** @} */

/** @name EEPROM I2C (CAT24M01W)
 *  Gestion de la mémoire non-volatile pour le logging.
 *  @{ */
#define EEPROM_I2C_ADDR       (0x50)             ///< Adresse I2C de base (A1, A2 selon câblage)
#define EEPROM_SIZE_BYTES     (128 * 1024)       ///< 1Mbit = 128KB
#define EEPROM_PAGE_SIZE      (256)              ///< Taille de page CAT24M01
#define EEPROM_LOG_START_ADDR (0)                ///< Adresse de début des logs
#define EEPROM_MAX_LOG_SIZE   (100 * 1024)       ///< 100KB réservés aux logs
/** @} */

/** @name Debug / Logging
 *  Logs et messages sur le port série (USB). Quand activé, esp_log est redirigé vers Serial.
 *  @{ */
#define ENABLE_SERIAL_LOGS    1                  ///< 1 = tout (debug + messages) sur Serial USB, 0 pour désactiver
#define GLOBAL_LOG_LEVEL      ESP_LOG_VERBOSE    ///< Niveau : ESP_LOG_NONE, ERROR, WARN, INFO, DEBUG, VERBOSE
/** @} */

/** @name Mode Test / Debug
 *  Désactivation des modules pour tests unitaires.
 *  @{ */
#define TEST_MODE_MESH_ONLY    0  ///< 1 = Mesh painlessMesh uniquement, 0 = selon ESPNOW_MESH_TEST
#define ESPNOW_MESH_TEST       1  ///< 1 = test mesh ESP-NOW entre cartes (2+ ESP32), 0 = mode normal / survival
/** @} */

/** @name Mode Mesh (Sandbox ESP-NOW Flooding)
 *  Un seul mode actif : LEXACARE_MESH_ESPNOW_FLOODING.
 *  @{ */
#define LEXACARE_MESH_ESPNOW_FLOODING 1  ///< 1 = mesh ESP-NOW par inondation (cache 50 msgId, TTL, jitter)
#define LEXACARE_MESH_PAINLESS        0  ///< Désactivé (legacy painlessMesh)
#define LEXACARE_MESH_32B             0  ///< Désactivé
#define LEXACARE_THIS_NODE_IS_GATEWAY 1  ///< 1 = passerelle (Node 0) : Serial JSON + OTA depuis PC ; 0 = nœud
#define LEXACARE_MESH_SSID     "LexacareMesh"
#define LEXACARE_MESH_PASSWORD "LexacareMeshSecret"
#define LEXACARE_MESH_PORT     (5555)
/** @} */

/** @name OTA ESP-NOW (partitions default_16MB.csv)
 *  Push par Série vers Gateway, propagation par flooding. Chunks 200 octets.
 *  @{ */
#define CURRENT_FW_VERSION       4       ///< Version firmware (NVS "system" / "fw_ver")
#define OTA_CHUNK_DATA_BYTES    200     ///< Octets de données par OTA_CHUNK (OtaChunkPayload.data)
#define OTA_MAX_SIZE             (6*1024*1024)  ///< Limite binaire 6.4 Mo (slot app0/app1)
#if LEXACARE_MESH_PAINLESS
#define OTA_CHUNK_SIZE            512
#define OTA_ADV_INTERVAL_MS       30000
#endif
/** @} */

#endif // CONFIG_H
