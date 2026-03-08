/**
 * @file espnow_mesh.cpp
 * @brief Mesh ESP-NOW par inondation : callback RX → queue, cache 50 msgId, TTL, jitter, retransmission.
 * @details Ne crée pas de tâches ; espnowRxTask (dans main) appelle espnow_mesh_handle_packet().
 */

#include "config/config.h"
#include "comm/espnow_mesh.h"
#include "comm/ota_manager.h"
#include "rtos/queues_events.h"
#include "system/log_dual.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ESPNOW";

static uint8_t s_broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static espnow_mesh_data_cb_t s_data_cb = nullptr;
static bool s_init_done = false;

/** Cache anti-doublon : 50 msgId (buffer circulaire). */
static uint32_t s_msg_cache[ESPNOW_MSG_CACHE_SIZE];
static uint8_t s_msg_cache_head = 0;
static uint8_t s_msg_cache_count = 0;
static SemaphoreHandle_t s_cache_mutex = nullptr;

/**
 * @brief Retourne 1 si msgId est déjà dans le cache.
 */
static bool is_msg_cached(uint32_t msgId) {
    if (!s_cache_mutex) return false;
    if (xSemaphoreTake(s_cache_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return true;
    bool found = false;
    for (uint8_t i = 0; i < s_msg_cache_count; i++) {
        if (s_msg_cache[i] == msgId) {
            found = true;
            break;
        }
    }
    xSemaphoreGive(s_cache_mutex);
    return found;
}

/**
 * @brief Ajoute msgId au cache (écrase l'entrée la plus ancienne si plein).
 */
static void add_msg_to_cache(uint32_t msgId) {
    if (!s_cache_mutex) return;
    if (xSemaphoreTake(s_cache_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
    if (s_msg_cache_count < ESPNOW_MSG_CACHE_SIZE) {
        s_msg_cache[s_msg_cache_count++] = msgId;
    } else {
        s_msg_cache[s_msg_cache_head] = msgId;
        s_msg_cache_head = (s_msg_cache_head + 1) % ESPNOW_MSG_CACHE_SIZE;
    }
    xSemaphoreGive(s_cache_mutex);
}

/**
 * @brief Callback ESP-NOW : copie mac + payload dans la queue RX (contexte tâche).
 */
static void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    (void)mac_addr;
    if (!g_queue_espnow_rx || data_len <= 0 || data_len > ESPNOW_RX_PAYLOAD_MAX) {
        if (data_len > 0)
            ESP_LOGW(TAG, "RX rejete: len=%d (max=%d) ou queue NULL", data_len, ESPNOW_RX_PAYLOAD_MAX);
        return;
    }
    uint8_t msgType = (data_len >= (int)ESPNOW_MESH_HEADER_SIZE) ? data[4] : 0;
    ESP_LOGD(TAG, "RX paquet %d octets type=0x%02X", data_len, (unsigned)msgType);
    espnow_rx_item_t item;
    memcpy(item.mac, mac_addr, 6);
    item.len = (uint16_t)data_len;
    memcpy(item.payload, data, (size_t)data_len);
    BaseType_t ok = xQueueSend(g_queue_espnow_rx, &item, 0);
    if (ok != pdTRUE) {
        ESP_LOGW(TAG, "Queue RX pleine, paquet ignore (%d octets)", data_len);
    }
}

static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    (void)mac_addr;
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "Envoi ESP-NOW echoue status=%d", (int)status);
    }
}

bool espnow_mesh_init(void) {
    if (s_init_done) return true;
    if (!g_queue_espnow_rx) {
        ESP_LOGE(TAG, "g_queue_espnow_rx non initialise (queues_events_init avant)");
        return false;
    }

    if (s_cache_mutex == nullptr)
        s_cache_mutex = xSemaphoreCreateMutex();
    if (s_cache_mutex == nullptr) {
        ESP_LOGE(TAG, "Mutex cache OOM");
        return false;
    }
    memset(s_msg_cache, 0, sizeof(s_msg_cache));
    s_msg_cache_head = 0;
    s_msg_cache_count = 0;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(50);

    esp_wifi_set_channel((uint8_t)ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_now_register_recv_cb(espnow_recv_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_register_recv_cb failed");
        esp_now_deinit();
        return false;
    }

    err = esp_now_register_send_cb(espnow_send_cb);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_now_register_send_cb failed (non bloquant)");
    }

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, s_broadcast_mac, 6);
    peer.channel = (uint8_t)ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "esp_now_add_peer(broadcast) failed: %s", esp_err_to_name(err));
        esp_now_unregister_recv_cb();
        esp_now_deinit();
        return false;
    }

    s_init_done = true;
    ESP_LOGI(TAG, "ESP-NOW flooding init OK (canal %d, cache %d)", ESPNOW_CHANNEL, ESPNOW_MSG_CACHE_SIZE);
    return true;
}

bool espnow_mesh_send_broadcast(const uint8_t *data, size_t len) {
    if (!s_init_done || !data) return false;
    if (len > ESP_NOW_MAX_DATA_LEN) len = ESP_NOW_MAX_DATA_LEN;
    return (esp_now_send(s_broadcast_mac, data, len) == ESP_OK);
}

void espnow_mesh_get_my_mac(uint8_t *mac_out) {
    if (!mac_out) return;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    memcpy(mac_out, mac, 6);
}

void espnow_mesh_set_data_cb(espnow_mesh_data_cb_t cb) {
    s_data_cb = cb;
}

void espnow_mesh_handle_packet(const void *item_ptr) {
    const espnow_rx_item_t *item = (const espnow_rx_item_t *)item_ptr;
    if (!item || item->len < ESPNOW_MESH_HEADER_SIZE) {
        ESP_LOGW(TAG, "handle_packet: item invalide ou len=%u < header", item ? (unsigned)item->len : 0);
        return;
    }

    const EspNowMeshHeader_t *hdr = (const EspNowMeshHeader_t *)item->payload;
    uint32_t msgId = hdr->msgId;
    uint8_t msgType = hdr->msgType;
    if (is_msg_cached(msgId)) {
        ESP_LOGD(TAG, "msgId 0x%08lX deja en cache, ignore", (unsigned long)msgId);
        return;
    }
    add_msg_to_cache(msgId);
    ESP_LOGD(TAG, "handle type=0x%02X msgId=0x%08lX src=%u ttl=%u", (unsigned)msgType, (unsigned long)msgId, (unsigned)hdr->sourceNodeId, (unsigned)hdr->ttl);

    const uint8_t *body = item->payload + ESPNOW_MESH_HEADER_SIZE;
    size_t body_len = item->len - ESPNOW_MESH_HEADER_SIZE;

    switch (msgType) {
        case ESPNOW_MSG_TYPE_DATA: {
            if (body_len >= LEXA_FRAME_SIZE) {
                const LexaFullFrame_t *frame = (const LexaFullFrame_t *)body;
                if (lexaframe_verify_crc(frame)) {
                    if (s_data_cb) {
                        ESP_LOGD(TAG, "Data CRC OK -> callback Gateway nodeId=%u", (unsigned)frame->nodeShortId);
                        s_data_cb(frame);
                    }
                } else {
                    ESP_LOGW(TAG, "Data CRC invalide, trame ignoree (nodeId=%u)", (unsigned)frame->nodeShortId);
                }
            } else {
                ESP_LOGW(TAG, "Data body trop court %u < %u", (unsigned)body_len, (unsigned)LEXA_FRAME_SIZE);
            }
            break;
        }
        case ESPNOW_MSG_TYPE_OTA_ADV: {
            if (body_len >= OTA_ADV_PAYLOAD_SIZE) {
                if (LEXACARE_THIS_NODE_IS_GATEWAY && ota_manager_is_broadcast_active())
                    ESP_LOGD(TAG, "OTA_ADV ignore (ROOT diffuse, pas recepteur)");
                else {
                    ESP_LOGI(TAG, "OTA_ADV recu -> ota_manager");
                    ota_manager_on_ota_adv(body, body_len);
                }
            } else {
                ESP_LOGW(TAG, "OTA_ADV body trop court %u", (unsigned)body_len);
            }
            break;
        }
        case ESPNOW_MSG_TYPE_OTA_CHUNK: {
            if (body_len >= OTA_CHUNK_PAYLOAD_SIZE) {
                const OtaChunkPayload_t *ch = (const OtaChunkPayload_t *)body;
                if (LEXACARE_THIS_NODE_IS_GATEWAY && ota_manager_is_broadcast_active())
                    ESP_LOGD(TAG, "OTA_CHUNK ignore (ROOT diffuse)");
                else {
                    ESP_LOGD(TAG, "OTA_CHUNK recu idx=%u/%u", (unsigned)ch->chunkIndex, (unsigned)ch->totalChunks);
                    ota_manager_on_ota_chunk(body, body_len);
                }
            } else {
                ESP_LOGW(TAG, "OTA_CHUNK body trop court %u", (unsigned)body_len);
            }
            break;
        }
        default:
            ESP_LOGW(TAG, "Type message inconnu 0x%02X", (unsigned)msgType);
            break;
    }

    /* Retransmission si TTL > 0 (jitter puis broadcast) */
    if (hdr->ttl > 0) {
        uint8_t packet[ESPNOW_RX_PAYLOAD_MAX];
        if (item->len <= sizeof(packet)) {
            memcpy(packet, item->payload, item->len);
            EspNowMeshHeader_t *h = (EspNowMeshHeader_t *)packet;
            h->ttl--;
            int jitter_ms = ESPNOW_JITTER_MS_MIN + (esp_random() % (ESPNOW_JITTER_MS_MAX - ESPNOW_JITTER_MS_MIN + 1));
            vTaskDelay(pdMS_TO_TICKS(jitter_ms));
            bool sent = espnow_mesh_send_broadcast(packet, item->len);
            ESP_LOGD(TAG, "Relay TTL=%u -> %s", (unsigned)h->ttl, sent ? "OK" : "ECHEC");
        }
    }
}

int espnow_mesh_get_peer_count(void) {
    if (!s_init_done) return 0;
    esp_now_peer_num_t num = {};
    if (esp_now_get_peer_num(&num) != ESP_OK) return 0;
    return num.total_num;
}
