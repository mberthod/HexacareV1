/**
 * @file system_types.h
 * @brief Contrat de données partagé entre tous les composants du firmware LexaCare V1.
 *
 * Règles d'utilisation :
 *  - Ce fichier est la seule source de vérité pour les structures inter-tâches.
 *  - Aucune variable globale : toutes les données partagées transitent via sys_context_t.
 *  - Compatible C et C++ (guards extern "C" inclus).
 *  - Inclure via le composant lexacare_types (REQUIRES dans CMakeLists.txt).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"

/**
 * @ingroup group_config
 * @brief Types partagés entre tâches (contrat de données).
 *
 * Ce fichier sert de “langage commun” entre tâches FreeRTOS : si deux parties du firmware
 * échangent une information, elle doit apparaître ici pour éviter les malentendus (tailles,
 * unités, champs “valid”, etc.).
 */

/* ================================================================
 * Constantes LIDAR
 *
 * Architecture :
 *   4 capteurs VL53L8CX frontaux (8×8 zones chacun) concaténés
 *   horizontalement → matrice unifiée 8 lignes × 32 colonnes (vue « 32×8 » en largeur×hauteur).
 *   Ordre de fusion (géométrie LexaCare) :
 *     colonnes 0–7 : LIDAR 1 (idx SPI 0), 8–15 : LIDAR 2 (1),
 *     16–23 : LIDAR 4 (3), 24–31 : LIDAR 3 (2).
 *
 *   5e capteur (vertical/sol) : flux séparé, non inclus dans la matrice.
 * ================================================================ */
#define LIDAR_ROWS              8
#define LIDAR_COLS              32
#define LIDAR_NUM_FRONT         4    /**< Capteurs alimentant la matrice */
#define LIDAR_NUM_TOTAL         5    /**< Total capteurs (4 frontaux + 1 sol) */
#define LIDAR_ZONES_PER_SENSOR  64   /**< 8×8 = 64 zones par capteur */

/* Adresses I2C (7 bits) allouées dynamiquement au démarrage */
#define LIDAR_I2C_ADDR_DEFAULT  0x29 /**< Adresse par défaut VL53L8CX (7 bits) */
#define LIDAR_I2C_ADDR_BASE     0x2A /**< Première adresse réassignée */

/* ================================================================
 * Constantes réseau et chiffrement AES-128
 *
 * Le payload de chaque trame lexacare_frame_t est chiffré en
 * AES-128-CBC par le nœud émetteur (mbedTLS) indépendamment
 * du LMK ESP-NOW — garantie de sécurité pour +1000 pairs.
 * ================================================================ */
#define LEXACARE_AES_KEY_LEN    16   /**< Clé AES-128 : 16 octets */
#define LEXACARE_AES_IV_LEN     16   /**< IV AES-CBC  : 16 octets */
#define LEXACARE_FRAME_MAGIC    0xCA5E
#define LEXACARE_MAX_PAYLOAD    128  /**< Payload chiffré (multiple de 16, ≤ 250 − header) */

/* Types de trame réseau */
#define FRAME_TYPE_TELEMETRY    0x00
#define FRAME_TYPE_AI_EVENT     0x01
#define FRAME_TYPE_OTA_FRAGMENT 0x02
#define FRAME_TYPE_BEACON       0x03

/* OTA */
#define OTA_FRAGMENT_DATA_LEN   200  /**< Octets de données utiles par fragment OTA */

/* ================================================================
 * lidar_matrix_t
 * @brief Matrice de distances fusionnées issues des 4 LIDARs frontaux.
 * @note  data[ligne][colonne], distances en millimètres (0 = zone invalide).
 * ================================================================ */
typedef struct {
    uint16_t data[LIDAR_ROWS][LIDAR_COLS]; /**< Distances en mm */
    uint16_t vertical_mm;                  /**< Distance capteur sol (5e capteur) */
    int64_t  timestamp_us;                 /**< Horodatage µs (esp_timer_get_time) */
    bool     valid;                        /**< false si lecture I2C échouée */
} lidar_matrix_t;

/* ================================================================
 * radar_data_t
 * @brief Données extraites du protocole TinyFrame du radar HLK-LD6002.
 *
 * Trames supportées :
 *   0x0A14 → breath_rate_bpm (uint16, big-endian)
 *   0x0A15 → heart_rate_bpm  (uint16, big-endian)
 *   0x0A16 → target_distance_mm (uint16, big-endian)
 * ================================================================ */
typedef struct {
    float   resp_phase;          /**< Phase respiratoire (rad) — calculée si dispo */
    float   heart_phase;         /**< Phase cardiaque (rad) — calculée si dispo */
    float   total_phase;         /**< Phase totale (rad) */
    uint16_t breath_rate_bpm;    /**< Fréquence respiratoire (cycles/min) */
    uint16_t heart_rate_bpm;     /**< Fréquence cardiaque (bpm) */
    uint16_t target_distance_mm; /**< Distance de la cible (mm) */
    bool     presence;           /**< true si une personne est détectée */
    int64_t  timestamp_us;       /**< Horodatage µs */
} radar_data_t;

/* ================================================================
 * sensor_frame_t
 * @brief Trame composite poussée dans sensor_to_ai_queue.
 *        Contient les données LIDAR et radar d'un même cycle d'acquisition.
 * ================================================================ */
typedef struct {
    lidar_matrix_t lidar;       /**< Données LIDAR fusionnées */
    radar_data_t   radar;       /**< Données radar LD6002 */
    bool           lidar_valid; /**< true : lidar contient des données fraîches */
    bool           radar_valid; /**< true : radar contient des données fraîches */
} sensor_frame_t;

/* ================================================================
 * ai_event_state_e
 * @brief États détectés par le moteur d'inférence (seuil ou modèle).
 * ================================================================ */
typedef enum {
    AI_NORMAL            = 0, /**< Situation normale */
    AI_CHUTE_DETECTEE    = 1, /**< Chute détectée — injection via xQueueSendToFront */
    AI_MOUVEMENT_ANORMAL = 2, /**< Mouvement anormal (agitation, posture inhabituelle) */
} ai_event_state_e;

/* ================================================================
 * ai_event_t
 * @brief Événement généré par Task_AI_Inference.
 *
 * Règle de priorité (contrainte stricte) :
 *   AI_CHUTE_DETECTEE  → xQueueSendToFront(ctx->ai_to_mesh_queue, ...)
 *   Autres états       → xQueueSend(ctx->ai_to_mesh_queue, ...)
 *
 * Cette règle garantit que les alertes de chute court-circuitent
 * la télémétrie normale dans la file d'envoi réseau.
 * ================================================================ */
typedef struct {
    ai_event_state_e state;       /**< État détecté */
    uint8_t          confidence;  /**< Confiance 0–100 % */
    int64_t          timestamp_us;
} ai_event_t;

/* ================================================================
 * lexacare_frame_t
 * @brief Trame réseau ESP-NOW avec payload chiffré AES-128-CBC.
 *
 * Taille totale : 2+1+1+6+16+128+2+2 = 158 octets (< 250 max ESP-NOW).
 *
 * Séquence d'émission :
 *   1. Sérialiser le payload en clair dans un buffer temporaire.
 *   2. Générer un IV aléatoire (esp_fill_random).
 *   3. Chiffrer avec mbedtls_aes_crypt_cbc (clé 16 o provisionnée en NVS).
 *   4. Calculer CRC16 sur (magic + type + hop + src_mac + iv + payload).
 *   5. Appeler esp_now_send().
 * ================================================================ */
typedef struct {
    uint16_t magic;                            /**< LEXACARE_FRAME_MAGIC (0xCA5E) */
    uint8_t  type;                             /**< FRAME_TYPE_* */
    uint8_t  hop_count;                        /**< Nombre de sauts depuis la source */
    uint8_t  src_mac[6];                       /**< MAC de l'émetteur original */
    uint8_t  iv[LEXACARE_AES_IV_LEN];          /**< IV aléatoire pour AES-128-CBC */
    uint8_t  payload[LEXACARE_MAX_PAYLOAD];    /**< Payload chiffré */
    uint16_t payload_len;                      /**< Longueur du payload chiffré (octets) */
    uint16_t crc16;                            /**< CRC16-CCITT sur header + iv + payload */
} lexacare_frame_t;

/* ================================================================
 * ota_fragment_payload_t
 * @brief Contenu en clair du payload pour un fragment OTA (avant chiffrement).
 * ================================================================ */
typedef struct {
    uint32_t firmware_size;                    /**< Taille totale du firmware (octets) */
    uint32_t fragment_offset;                  /**< Offset de ce fragment dans le firmware */
    uint16_t fragment_data_len;                /**< Longueur des données utiles */
    uint8_t  data[OTA_FRAGMENT_DATA_LEN];      /**< Données du fragment */
    bool     is_last;                          /**< true si dernier fragment */
} ota_fragment_payload_t;

/* ================================================================
 * sys_context_t
 * @brief Contexte global injecté par pointeur dans toutes les tâches.
 *
 * Principe d'injection de dépendances :
 *   - Aucune variable globale directe.
 *   - Chaque tâche reçoit un pointeur const vers ce contexte.
 *   - Les handles I2C sont initialisés par hw_diag_run() et
 *     réutilisés par sensor_acq sans réinitialisation.
 *
 * Queues et dimensionnement :
 *   sensor_to_ai_queue  : sensor_frame_t × 4  (profondeur conservatrice)
 *   ai_to_mesh_queue    : ai_event_t     × 8  (AI_CHUTE via xQueueSendToFront)
 *   diag_to_pc_queue    : ai_event_t     × 8
 *   log_queue           : char*          × 16 (pointeurs vers chaînes allouées)
 *
 * Capteurs environnementaux (I2C_NUM_0, SDA=11, SCL=12, bus partagé PCA9555) :
 *   HDC1080 : temp/humidité (0x40)
 *   BME280  : temp/pression/humidité (0x76)
 * ================================================================ */

/* ================================================================
 * enviro_data_t
 * @brief Données capteurs environnementaux (HDC1080 + BME280).
 *        NAN dans un champ = capteur absent ou lecture échouée.
 * ================================================================ */
typedef struct {
    float temp_hdc_c;     /**< HDC1080 température (°C)         */
    float humidity_hdc;   /**< HDC1080 humidité relative (%)    */
    float temp_bme_c;     /**< BME280  température (°C)         */
    float pressure_hpa;   /**< BME280  pression (hPa)           */
    float humidity_bme;   /**< BME280  humidité relative (%)    */
} enviro_data_t;

/* ================================================================
 * mic_data_t
 * @brief Données microphone MEMS numérique (I2S).
 * ================================================================ */
typedef struct {
    uint32_t rms;         /**< Amplitude RMS (unités brutes I2S) */
    uint32_t peak;        /**< Amplitude crête                   */
    bool     valid;       /**< true si une lecture valide existe */
} mic_data_t;

typedef struct {
    /* --- Queues FreeRTOS (créées dans app_main) --- */
    QueueHandle_t sensor_to_ai_queue; /**< sensor_frame_t, profondeur 4 */
    QueueHandle_t ai_to_mesh_queue;   /**< ai_event_t,    profondeur 8  */
    QueueHandle_t diag_to_pc_queue;   /**< ai_event_t,    profondeur 8  */
    QueueHandle_t log_queue;          /**< char*,         profondeur 16 */

    /* --- Handles SPI LIDAR (initialisés par hw_diag_run) --- */
    spi_device_handle_t lidar_spi[LIDAR_NUM_FRONT]; /**< SPI device par capteur LIDAR */

    /* --- Snapshot LIDAR partagé (protégé par data_mutex) --- */
    SemaphoreHandle_t  data_mutex;      /**< Mutex lecture/écriture snapshot */
    sensor_frame_t     latest_sensor;   /**< Dernière trame LIDAR + Radar + IA */
    bool               sensor_valid;    /**< true si latest_sensor contient des données */

    /* --- Capteurs environnementaux (mis à jour par Task_Sensor_Acq) --- */
    enviro_data_t      enviro;

    /* --- Microphone (mis à jour par mic_driver_read) --- */
    mic_data_t         mic;

    /* --- Flags d'état matériel (remplis par hw_diag_run) --- */
    bool lidar_ok[LIDAR_NUM_TOTAL]; /**< true si le capteur i a répondu au ping */
    bool radar_ok;                  /**< true si une trame LD6002 valide a été reçue */
    bool is_root_node;              /**< true si PIN_ROOT_SEL est bas au démarrage */
} sys_context_t;

#ifdef __cplusplus
}
#endif
