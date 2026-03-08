/**
 * @file mesh_flooding.cpp
 * @brief Managed Flooding : callback RX avec msgCache[100] (critical section), meshProcessorTask (dispatch + TTL + jitter 10–100 ms).
 */

#include "comm/mesh_flooding.h"
#include "comm/ota_mesh.h"
#include "comm/serial_gateway.h"
#include "config/config.h"
#include "lexacare_protocol.h"
#include "rtos/queues_events.h"
#include "system/log_dual.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_log.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char *TAG = "MESH_FLD";

static uint8_t s_broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static mesh_flooding_data_cb_t s_data_cb = nullptr;
static bool s_init_done = false;

/** Cache anti-doublon : 100 msgId (buffer circulaire). Accès uniquement dans le callback RX sous critical section. */
#define MSG_CACHE_SIZE 100
static uint32_t s_msg_cache[MSG_CACHE_SIZE];
static uint8_t s_msg_cache_head = 0;
static uint8_t s_msg_cache_count = 0;
static portMUX_TYPE s_msg_cache_mux = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief Callback ESP-NOW : si msgId dans le cache, ignorer ; sinon ajouter au cache et pousser dans rxQueue.
 */
static void mesh_flooding_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    if (!g_queue_espnow_rx || data_len < (int)ESPNOW_MESH_HEADER_SIZE || data_len > ESPNOW_RX_PAYLOAD_MAX)
        return;
    uint32_t msg_id = *(const uint32_t *)data;
    bool already_cached = false;
    portENTER_CRITICAL(&s_msg_cache_mux);
    for (uint8_t i = 0; i < s_msg_cache_count; i++) {
        if (s_msg_cache[i] == msg_id) {
            already_cached = true;
            break;
        }
    }
    if (already_cached) {
        portEXIT_CRITICAL(&s_msg_cache_mux);
        return;
    }
    if (s_msg_cache_count < MSG_CACHE_SIZE)
        s_msg_cache[s_msg_cache_count++] = msg_id;
    else {
        s_msg_cache[s_msg_cache_head] = msg_id;
        s_msg_cache_head = (s_msg_cache_head + 1) % MSG_CACHE_SIZE;
    }
    portEXIT_CRITICAL(&s_msg_cache_mux);

    espnow_rx_item_t item;
    memcpy(item.mac, mac_addr, 6);
    item.len = (uint16_t)data_len;
    memcpy(item.payload, data, (size_t)data_len);
    xQueueSend(g_queue_espnow_rx, &item, 0);
}

static void mesh_flooding_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    (void)mac_addr;
    if (status != ESP_NOW_SEND_SUCCESS)
        ESP_LOGW(TAG, "Send failed %d", (int)status);
}

bool mesh_flooding_init(void) {
    if (s_init_done) return true;
    if (!g_queue_espnow_rx) {
        ESP_LOGE(TAG, "g_queue_espnow_rx NULL (queues_events_init first)");
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
        ESP_LOGE(TAG, "esp_now_init %s", esp_err_to_name(err));
        return false;
    }
    err = esp_now_register_recv_cb(mesh_flooding_recv_cb);
    if (err != ESP_OK) {
        esp_now_deinit();
        return false;
    }
    esp_now_register_send_cb(mesh_flooding_send_cb);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, s_broadcast_mac, 6);
    peer.channel = (uint8_t)ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        esp_now_unregister_recv_cb();
        esp_now_deinit();
        return false;
    }
    s_init_done = true;
    log_dual_println("[MESH] mesh_flooding init OK (cache 100, jitter 10-100ms)");
    return true;
}

bool mesh_flooding_send_broadcast(const uint8_t *data, size_t len) {
    if (!s_init_done || !data) return false;
    if (len > ESP_NOW_MAX_DATA_LEN) len = ESP_NOW_MAX_DATA_LEN;
    return (esp_now_send(s_broadcast_mac, data, len) == ESP_OK);
}

void mesh_flooding_get_my_mac(uint8_t *mac_out) {
    if (!mac_out) return;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    memcpy(mac_out, mac, 6);
}

void mesh_flooding_set_data_cb(mesh_flooding_data_cb_t cb) {
    s_data_cb = cb;
}

void mesh_flooding_task(void *pv) {
    (void)pv;
    log_dual_println("[TASK] meshProcessor running (Core 0)");
    espnow_rx_item_t item;
    uint8_t packet[ESPNOW_RX_PAYLOAD_MAX];
    for (;;) {
        if (xQueueReceive(g_queue_espnow_rx, &item, portMAX_DELAY) != pdTRUE)
            continue;
        if (item.len < ESPNOW_MESH_HEADER_SIZE) continue;
        const EspNowMeshHeader_t *hdr = (const EspNowMeshHeader_t *)item.payload;
        uint8_t msg_type = hdr->msgType;
        const uint8_t *body = item.payload + ESPNOW_MESH_HEADER_SIZE;
        size_t body_len = item.len - ESPNOW_MESH_HEADER_SIZE;

        switch (msg_type) {
            case MSG_TYPE_DATA:
                if (body_len >= LEXA_FRAME_SIZE && lexaframe_verify_crc((const LexaFullFrame_t *)body))
                    if (s_data_cb) s_data_cb((const LexaFullFrame_t *)body);
                break;
            case MSG_TYPE_OTA_ADV:
                /* Ne pas injecter OTA mesh pendant OTA Série (éviter mélange et MD5 invalide). */
                if (body_len >= OTA_ADV_PAYLOAD_SIZE && !serial_gateway_is_ota_serial_receiving())
                    ota_mesh_on_ota_adv(body, body_len);
                break;
            case MSG_TYPE_OTA_CHUNK:
                if (body_len >= OTA_CHUNK_PAYLOAD_SIZE && !serial_gateway_is_ota_serial_receiving())
                    ota_mesh_on_ota_chunk(body, body_len);
                break;
            default:
                break;
        }

        if (hdr->ttl > 0 && item.len <= sizeof(packet)) {
            memcpy(packet, item.payload, item.len);
            EspNowMeshHeader_t *h = (EspNowMeshHeader_t *)packet;
            h->ttl--;
            int jitter_ms = 10 + (int)(esp_random() % 91);
            vTaskDelay(pdMS_TO_TICKS(jitter_ms));
            mesh_flooding_send_broadcast(packet, item.len);
        }
    }
}
