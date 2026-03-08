/**
 * @file queues_events.h
 * @brief Queues, sémaphores et EventGroups FreeRTOS pour Lexacare
 */

#ifndef QUEUES_EVENTS_H
#define QUEUES_EVENTS_H

#include "config/config.h"
#include "lexacare_protocol.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>

#ifdef __cplusplus
extern "C" {
#endif

/** EventGroup pour signaux entre tâches (fall, vitals, OTA). */
extern EventGroupHandle_t g_system_events;

#define EVENT_FALL_DETECTED   (1 << 0)
#define EVENT_VITALS_UPDATE   (1 << 1)
#define EVENT_OTA_READY       (1 << 2)
#define EVENT_OTA_FAIL        (1 << 3)

/** Queue signaux vitaux (optionnel). */
typedef struct {
    uint16_t heart_rate_bpm;
    uint16_t breath_rate_bpm;
    uint16_t target_distance_mm;
    uint8_t  presence;
} vitals_msg_t;
extern QueueHandle_t g_queue_vitals;

/** Élément reçu par le callback ESP-NOW (copié dans g_queue_espnow_rx). */
#define ESPNOW_RX_PAYLOAD_MAX 250
typedef struct {
    uint8_t  mac[6];
    uint16_t len;
    uint8_t  payload[ESPNOW_RX_PAYLOAD_MAX];
} espnow_rx_item_t;

/** Queue des paquets ESP-NOW reçus (consommée par espnowRxTask). */
extern QueueHandle_t g_queue_espnow_rx;

/** Queue des trames Data à envoyer (LexaFullFrame, produite par sensorTask). */
extern QueueHandle_t g_queue_espnow_tx;

/**
 * @brief Crée les objets FreeRTOS (à appeler depuis main avant les tâches).
 */
void queues_events_init(void);

#ifdef __cplusplus
}
#endif

#endif // QUEUES_EVENTS_H
