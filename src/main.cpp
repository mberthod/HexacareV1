/**
 * @file main.cpp
 * @brief Point d'entrée Lexacare V2 – Tree Mesh & OTA Store & Forward.
 */

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "config/config.h"
#include "config/pins_lexacare.h"
#include "system/log_dual.h"
#include "lexacare_protocol.h"
#include "comm/mesh_tree_protocol.h"
#include "comm/routing_manager.h"
#include "comm/ota_tree_manager.h"
#include "comm/serial_gateway.h"
#include "rtos/queues_events.h"
#include "sensors/sensor_sim.h"
#include <Wire.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <freertos/task.h>

#define TEST_LED_PIN PIN_RGB_LED
Adafruit_NeoPixel pixel(1, TEST_LED_PIN, NEO_GRB + NEO_KHZ800);

static const char *TAG_MAIN = "MAIN";

// Callback ESP-NOW global
static void on_espnow_recv(const uint8_t * mac_addr, const uint8_t *data, int len) {
    if (len < sizeof(TreeMeshHeader)) return;
    TreeMeshHeader* hdr = (TreeMeshHeader*)data;
    
    // 1. Routage (Beacons, Joins, Heartbeats)
    on_mesh_receive(mac_addr, data, len);

    // 2. OTA (Adv, Req, Chunk, Done)
    ota_tree_on_mesh_message(mac_addr, hdr->msgType, data + sizeof(TreeMeshHeader), len - sizeof(TreeMeshHeader));

    // 3. Data (Forwarding)
    if (hdr->msgType == MSG_DATA) {
        if (LEXACARE_THIS_NODE_IS_GATEWAY) {
            // ROOT: Envoyer au PC via Serial
            // Note: Le payload est une LexaFullFrame (ou DataPayload V2)
            // Ici on suppose LexaFullFrame pour compatibilité
            if (len - sizeof(TreeMeshHeader) >= sizeof(LexaFullFrame_t)) {
                serial_gateway_send_data_json(data + sizeof(TreeMeshHeader), 0);
            }
        } else {
            // NOEUD: Forwarder au parent
            uint8_t parent_mac[6];
            if (routing_get_parent_mac(parent_mac)) {
                routing_send_unicast(parent_mac, MSG_DATA, data + sizeof(TreeMeshHeader), len - sizeof(TreeMeshHeader));
            }
        }
    }
}

// Tâche d'envoi des données capteurs (générées localement)
static void dataTxTask(void *pv) {
    LexaFullFrame_t frame;
    while (1) {
        if (xQueueReceive(g_queue_espnow_tx, &frame, portMAX_DELAY) == pdTRUE) {
            // Si ROOT, direct série
            if (LEXACARE_THIS_NODE_IS_GATEWAY) {
                serial_gateway_send_data_json(&frame, 0);
            } else {
                // Sinon, envoyer au parent
                uint8_t parent_mac[6];
                if (routing_get_parent_mac(parent_mac)) {
                    routing_send_unicast(parent_mac, MSG_DATA, (uint8_t*)&frame, sizeof(frame));
                }
            }
        }
    }
}

void setup() {
    delay(1000);
    log_dual_init();
    log_dual_println("[BOOT] Lexacare V2 - Tree Mesh");

    pixel.begin();
    pixel.setBrightness(50);
    pixel.setPixelColor(0, pixel.Color(0, 0, 50)); // Blue init
    pixel.show();

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // WiFi & ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "ESP-NOW Init Failed");
        return;
    }
    esp_now_register_recv_cb(on_espnow_recv);

    // Initialisation Modules
    queues_events_init();
    routing_init();
    ota_tree_init();
    
    if (LEXACARE_THIS_NODE_IS_GATEWAY) {
        serial_gateway_init();
        pixel.setPixelColor(0, pixel.Color(0, 50, 0)); // Green for Root
    } else {
        pixel.setPixelColor(0, pixel.Color(50, 30, 0)); // Amber for Node
    }
    pixel.show();

    // Tâches
    xTaskCreatePinnedToCore(routing_task, "Routing", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(ota_tree_task, "OTA_Tree", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(dataTxTask, "DataTx", 2048, NULL, 1, NULL, 0);
    
    // Simulation Capteurs (Core 1)
    sensor_sim_task_start();

    if (LEXACARE_THIS_NODE_IS_GATEWAY) {
        // Serial Gateway pour OTA UART (Core 0)
        xTaskCreatePinnedToCore(serial_gateway_task, "SerialGW", 4096, NULL, 2, NULL, 0);
    }
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
    // LED Heartbeat ?
}
