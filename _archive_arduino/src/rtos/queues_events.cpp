/**
 * @file queues_events.cpp
 * @brief Implémentation des objets de synchronisation FreeRTOS.
 * @details EventGroups (OTA, fall, vitals), queues vitals, espnow_rx (paquets reçus),
 * espnow_tx (LexaFullFrame à envoyer). À appeler avant création des tâches.
 */

#include "config/config.h"
#include "rtos/queues_events.h"

EventGroupHandle_t g_system_events = NULL;
QueueHandle_t g_queue_vitals = NULL;
QueueHandle_t g_queue_espnow_rx = NULL;
QueueHandle_t g_queue_espnow_tx = NULL;

/**
 * @brief Crée les objets FreeRTOS si non déjà existants.
 */
void queues_events_init(void)
{
    if (g_system_events == NULL)
        g_system_events = xEventGroupCreate();
    if (g_queue_vitals == NULL)
        g_queue_vitals = xQueueCreate(QUEUE_VITALS_LEN, sizeof(vitals_msg_t));
    if (g_queue_espnow_rx == NULL)
        g_queue_espnow_rx = xQueueCreate(QUEUE_ESPNOW_RX_LEN, sizeof(espnow_rx_item_t));
    if (g_queue_espnow_tx == NULL)
        g_queue_espnow_tx = xQueueCreate(QUEUE_ESPNOW_TX_LEN, sizeof(LexaFullFrame_t));
}
