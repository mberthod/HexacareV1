/**
 * @file main.cpp
 * @brief Point d'entrée Lexacare V1 – OTA haute résilience (Managed Flooding + Random Access OTA).
 * @details Init : log_dual, NeoPixel, NVS, queues, mesh_flooding, ota_mesh, serial_gateway (si ROOT).
 * Tâches : mesh_flooding_task (Core 0), espnowTxTask (Core 0), serial_gateway_task (Core 0, si Gateway), sensor_sim (Core 1).
 * Loop : LED heartbeat, EVENT_OTA_READY / EVENT_OTA_FAIL → reboot.
 */

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "config/config.h"
#include "config/pins_lexacare.h"
#include "system/log_dual.h"
#include "lexacare_protocol.h"
#include "comm/mesh_flooding.h"
#include "comm/ota_mesh.h"
#include "comm/serial_gateway.h"
#include "rtos/queues_events.h"
#include "sensors/sensor_sim.h"
#include <Wire.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <freertos/task.h>
#include <cstdarg>
#include <string.h>

#define TEST_LED_PIN PIN_RGB_LED
Adafruit_NeoPixel pixel(1, TEST_LED_PIN, NEO_GRB + NEO_KHZ800);

static const char *TAG_MAIN = "MAIN";

#if ENABLE_SERIAL_LOGS
static int serial_vprintf(const char *fmt, va_list args) {
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    if (n > 0) {
        if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
        log_dual_write((const uint8_t *)buf, (size_t)n);
    }
    return n;
}
#endif

static uint32_t s_led_override_until = 0;
static const uint32_t LED_OVERRIDE_MS = 400;

/** Callback Gateway : trame Data reçue → JSON sur Serial (serial_gateway). */
static void gateway_data_cb(const LexaFullFrame_t *f) {
    serial_gateway_send_data_json(f, ota_mesh_get_fw_version());
}

static void set_led_gateway(void) {
    pixel.setPixelColor(0, pixel.Color(0, 50, 0));
    pixel.show();
}
static void set_led_device(void) {
    pixel.setPixelColor(0, pixel.Color(50, 30, 0));
    pixel.show();
}
static void set_led_ota(void) {
    pixel.setPixelColor(0, pixel.Color(40, 0, 50));
    pixel.show();
}
static void set_led_ota_broadcast(void) {
    pixel.setPixelColor(0, pixel.Color(0, 30, 50));
    pixel.show();
}
static void set_led_ota_success(void) {
    pixel.setPixelColor(0, pixel.Color(0, 80, 0));
    pixel.show();
}
static void set_led_ota_error(void) {
    pixel.setPixelColor(0, pixel.Color(80, 0, 0));
    pixel.show();
}
/** Clignote 4 fois (vert si success, rouge si erreur) puis retour. */
static void blink_led_4_times(bool success) {
    uint32_t c = success ? pixel.Color(0, 80, 0) : pixel.Color(80, 0, 0);
    for (int i = 0; i < 4; i++) {
        pixel.setPixelColor(0, c);
        pixel.show();
        delay(200);
        pixel.setPixelColor(0, 0);
        pixel.show();
        delay(200);
    }
}

/** Tâche TX : lit g_queue_espnow_tx, construit Header+Data, envoie via mesh_flooding. Pendant OTA réception, ne pas envoyer. */
static void espnowTxTask(void *pv) {
    (void)pv;
    uint8_t my_mac[6];
    mesh_flooding_get_my_mac(my_mac);
    uint16_t source_node = (uint16_t)((my_mac[4] << 8) | my_mac[5]);
    uint32_t msg_seq = 0;
    log_dual_println("[TASK] espnowTx running (Core 0)");
    LexaFullFrame_t frame;
    for (;;) {
        if (xQueueReceive(g_queue_espnow_tx, &frame, pdMS_TO_TICKS(100)) != pdTRUE)
            continue;
        if (ota_mesh_is_ota_in_progress())
            continue;
        uint8_t packet[ESPNOW_MESH_HEADER_SIZE + LEXA_FRAME_SIZE];
        EspNowMeshHeader_t *h = (EspNowMeshHeader_t *)packet;
        h->msgId = ((uint32_t)source_node << 16) | (msg_seq++ & 0xFFFF);
        h->msgType = MSG_TYPE_DATA;
        h->ttl = ESPNOW_TTL_DEFAULT;
        h->sourceNodeId = source_node;
        memcpy(packet + ESPNOW_MESH_HEADER_SIZE, &frame, LEXA_FRAME_SIZE);
        mesh_flooding_send_broadcast(packet, sizeof(packet));
    }
}

void setup() {
    delay(1000);
    log_dual_init();
    log_dual_println("[BOOT] log_dual_init OK");
#if ENABLE_SERIAL_LOGS
    esp_log_set_vprintf(serial_vprintf);
    esp_log_level_set("*", GLOBAL_LOG_LEVEL);
#endif
    pixel.begin();
    pixel.setBrightness(80);
    log_dual_println("[BOOT] NeoPixel OK");

    log_dual_println("[BOOT] ======== LEXACARE - ESP-NOW Floodingddf Mesh ========");

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Serial2.begin(115200, SERIAL_8N1, PIN_RADAR_RX, PIN_RADAR_TX);
    log_dual_println("[BOOT] I2C + UART2 OK");

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        log_dual_println("[BOOT] NVS erase + re-init");
        nvs_flash_erase();
        nvs_flash_init();
    }
    log_dual_println("[BOOT] NVS OK");
    queues_events_init();
    log_dual_println("[BOOT] Queues OK");
    if (!mesh_flooding_init()) {
        log_dual_println("[ERREUR] init mesh_flooding echouee");
        pixel.setPixelColor(0, pixel.Color(50, 0, 0));
        pixel.show();
        return;
    }
    log_dual_println("[BOOT] mesh_flooding init OK");
    ota_mesh_init();
    log_dual_println("[BOOT] ota_mesh init OK");
    if (LEXACARE_THIS_NODE_IS_GATEWAY) {
        mesh_flooding_set_data_cb(gateway_data_cb);
        serial_gateway_init();
        set_led_gateway();
        log_dual_println("[BOOT] Mode GATEWAY (LED verte)");
    } else {
        set_led_device();
        log_dual_println("[BOOT] Mode DEVICE (LED ambre)");
    }
    sensor_sim_task_start();
    log_dual_println("[BOOT] Sensor sim task started");

    TaskHandle_t mesh_handle = nullptr, tx_handle = nullptr;
    xTaskCreatePinnedToCore(mesh_flooding_task, "meshProc", 4096, nullptr, 3, &mesh_handle, (BaseType_t)CORE_PRO);
    xTaskCreatePinnedToCore(espnowTxTask, "espnowTx", 2048, nullptr, 2, &tx_handle, (BaseType_t)CORE_PRO);
    if (LEXACARE_THIS_NODE_IS_GATEWAY) {
        xTaskCreatePinnedToCore(serial_gateway_task, "serialGW", 6144, nullptr, 2, nullptr, (BaseType_t)CORE_PRO);
        serial_gateway_register_tasks_for_ota_suspend(mesh_handle, tx_handle, sensor_sim_get_task_handle());
    }
    log_dual_println("[BOOT] Tasks created");

    log_dual_println("[BOOT] === Setup TERMINE === OTA format: OTA_CHUNK:index:total:400hex");
    log_dual_println("----------------------------------------\n");
}

void loop() {
    static bool s_loop_once = true;
    if (s_loop_once) {
        s_loop_once = false;
        log_dual_println("[MAIN] Loop entered (premier passage)");
    }
    if (g_system_events && (xEventGroupGetBits(g_system_events) & EVENT_OTA_READY)) {
        xEventGroupClearBits(g_system_events, EVENT_OTA_READY);
        log_dual_println("[MAIN] OTA OK -> 4 clignotements vert puis reboot");
        blink_led_4_times(true);
        delay(300);
        ESP.restart();
    }
    if (g_system_events && (xEventGroupGetBits(g_system_events) & EVENT_OTA_FAIL)) {
        xEventGroupClearBits(g_system_events, EVENT_OTA_FAIL);
        log_dual_println("[MAIN] OTA echec (voir message [OTA] ERREUR ci-dessus: MD5 ou set_boot_partition) -> 4 clignotements rouge puis reboot");
        blink_led_4_times(false);
        delay(300);
        ESP.restart();
    }
    uint32_t now = millis();
    if (ota_mesh_is_ota_in_progress()) {
        set_led_ota();
    } else {
        if (s_led_override_until && now > s_led_override_until)
            s_led_override_until = 0;
        static uint32_t s_heartbeat = 0;
        static uint32_t s_log_heartbeat = 0;
        if (now - s_heartbeat >= 500) {
            s_heartbeat = now;
            static bool led_on = true;
            led_on = !led_on;
            if (led_on)
                LEXACARE_THIS_NODE_IS_GATEWAY ? set_led_gateway() : set_led_device();
            else {
                pixel.setPixelColor(0, pixel.Color(0, 0, 0));
                pixel.show();
            }
        }
#if ENABLE_SERIAL_LOGS
        if (now - s_log_heartbeat >= 10000) {
            s_log_heartbeat = now;
            ESP_LOGI(TAG_MAIN, "Alive (uptime %lu s) Gateway=%d OTA=%d", (unsigned long)(now / 1000), LEXACARE_THIS_NODE_IS_GATEWAY ? 1 : 0, ota_mesh_is_ota_in_progress());
        }
#endif
    }
    delay(50);
}
