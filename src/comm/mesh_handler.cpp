/**
 * @file mesh_handler.cpp
 * @brief Mesh Lexacare : painlessMesh (LEXACARE_MESH_PAINLESS) ou ESP-NOW (LEXACARE_MESH_32B).
 * @details Gestion du réseau mesh sur Core 0. Trame binaire 32 octets (LexaFullFrame) en broadcast ;
 * élection du ROOT par ID minimal (painlessMesh). Le ROOT convertit les trames reçues en JSON
 * (nodeId, vBat, probFallLidar, tempExt, fw_ver, etc.) pour le dashboard. Messages JSON OTA
 * (OTA_ADV, OTA_REQ, OTA_CHUNK) dispatchés à ota_manager. Pas de donnée partagée avec Core 1
 * hormis sensor_sim_get_latest_frame() (protégé par mutex dans sensor_sim).
 */

#include "mesh_handler.h"
#include "config/config.h"
#include "sensors/sensor_sim.h"
#include "comm/ota_manager.h"
#include "system/log_dual.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include <string.h>

static mesh_handler_led_cb_t s_led_cb = nullptr;
static bool s_init_done = false;
static uint32_t s_last_send_ms = 0;
static uint32_t s_diag_count = 0;
#define MESH_SEND_INTERVAL_MS 1000
#define MESH_DIAG_INTERVAL_SENDS 15  /* Log diagnostic toutes les 15 s (ROOT) */

static void frame_to_json(const LexaFullFrame_t *f, char *buf, size_t buf_size) {
    StaticJsonDocument<384> doc;
    doc["nodeId"] = f->nodeShortId;       /* Compatibilité dashboard Python */
    doc["nodeShortId"] = f->nodeShortId;
    doc["epoch"] = f->epoch;
    doc["probFallLidar"] = f->probFallLidar;
    doc["probFallAudio"] = f->probFallAudio;
    doc["heartRate"] = f->heartRate;
    doc["respRate"] = f->respRate;
    doc["tempExt"] = (float)f->tempExt / 100.0f;
    doc["humidity"] = (float)f->humidity / 100.0f;
    doc["pressure"] = 800.0f + (float)f->pressure / 10.0f;
    doc["thermalMax"] = (float)f->thermalMax / 100.0f;
    doc["volumeOccupancy"] = f->volumeOccupancy;
    doc["vBat"] = f->vBat;
    doc["sensorFlags"] = f->sensorFlags;
#if LEXACARE_MESH_PAINLESS
    doc["fw_ver"] = ota_manager_get_fw_version();  /* Version passerelle (ROOT) */
#endif
    serializeJson(doc, buf, buf_size);
}

#if LEXACARE_MESH_PAINLESS

#include <painlessMesh.h>

static painlessMesh s_mesh;
static bool s_is_root = false;

static void painless_recv_cb(uint32_t from, String &msg) {
    size_t len = msg.length();
    if (len == LEXA_FRAME_SIZE) {
        /* Trame binaire 32 octets */
        LexaFullFrame_t frame;
        memcpy(&frame, msg.c_str(), LEXA_FRAME_SIZE);
        if (!lexaframe_verify_crc(&frame)) {
            if (s_led_cb) s_led_cb(LEX_LED_RED);
            return;
        }
        if (s_is_root) {
            char json[400];
            frame_to_json(&frame, json, sizeof(json));
            log_dual_println(json);
        }
        if (s_led_cb) s_led_cb(LEX_LED_BLUE);
    } else if (len > 0 && msg.c_str()[0] == '{') {
        /* JSON OTA */
        ota_manager_on_message(from, msg.c_str(), len);
    }
}

static void update_root_status(void) {
    auto list = s_mesh.getNodeList(true);
    uint32_t my_id = s_mesh.getNodeId();
    uint32_t min_id = my_id;
    for (uint32_t id : list) {
        if (id < min_id) min_id = id;
    }
    s_is_root = (my_id == min_id);
}

int mesh_handler_init(mesh_handler_led_cb_t led_cb) {
    if (s_init_done) return 1;
    s_led_cb = led_cb;
    s_mesh.init(LEXACARE_MESH_SSID, LEXACARE_MESH_PASSWORD, LEXACARE_MESH_PORT);
    s_mesh.onReceive(painless_recv_cb);
    update_root_status();
    if (s_led_cb)
        s_led_cb(s_is_root ? LEX_LED_ROOT : LEX_LED_DEVICE);
    s_init_done = true;
    return 1;
}

void mesh_handler_loop(void) {
    s_mesh.update();
    update_root_status();
    ota_manager_loop();
    uint32_t now = millis();
    if (now - s_last_send_ms >= MESH_SEND_INTERVAL_MS) {
        s_last_send_ms = now;
        LexaFullFrame_t frame;
        if (sensor_sim_get_latest_frame(&frame)) {
            String payload;
            payload.reserve(LEXA_FRAME_SIZE);
            for (size_t i = 0; i < LEXA_FRAME_SIZE; i++)
                payload += (char)((const uint8_t *)&frame)[i];
            s_mesh.sendBroadcast(payload, false);
            /* ROOT envoie aussi sa propre trame en JSON sur Serial (painlessMesh ne renvoie pas ses propres broadcasts) */
            if (s_is_root) {
                char json[400];
                frame_to_json(&frame, json, sizeof(json));
                log_dual_println(json);
                s_diag_count++;
                if (s_diag_count >= MESH_DIAG_INTERVAL_SENDS) {
                    s_diag_count = 0;
                    log_dual_printf("[LEXACARE] mesh actif ROOT=1, trames JSON => Serial\n");
                }
            }
        }
    }
}

int mesh_handler_send_frame(const uint8_t *frame32) {
    if (!s_init_done || !frame32) return 0;
    String payload;
    payload.reserve(LEXA_FRAME_SIZE);
    for (size_t i = 0; i < LEXA_FRAME_SIZE; i++)
        payload += (char)frame32[i];
    return s_mesh.sendBroadcast(payload, false) ? 1 : 0;
}

int mesh_handler_send_broadcast_raw(const char *msg) {
    if (!s_init_done || !msg) return 0;
    return s_mesh.sendBroadcast(msg, false) ? 1 : 0;
}

int mesh_handler_send_to(uint32_t to, const char *msg) {
    if (!s_init_done || !msg) return 0;
    return s_mesh.sendSingle(to, String(msg)) ? 1 : 0;
}

int mesh_handler_is_root(void) {
    return s_is_root ? 1 : 0;
}

#else /* LEXACARE_MESH_32B */

#include "comm/espnow_mesh.h"

static const bool s_is_gateway = (LEXACARE_THIS_NODE_IS_GATEWAY != 0);

static void espnow_recv_cb(const uint8_t *mac, const uint8_t *data, size_t len) {
    (void)mac;
    if (len != LEXA_FRAME_SIZE) return;
    LexaFullFrame_t frame;
    memcpy(&frame, data, LEXA_FRAME_SIZE);
    if (!lexaframe_verify_crc(&frame)) {
        if (s_is_gateway && s_led_cb) s_led_cb(LEX_LED_RED);
        return;
    }
    if (!s_is_gateway) return;
    char json[400];
    frame_to_json(&frame, json, sizeof(json));
    log_dual_println(json);
    if (s_led_cb) s_led_cb(LEX_LED_BLUE);
}

int mesh_handler_init(mesh_handler_led_cb_t led_cb) {
    if (s_init_done) return 1;
    s_led_cb = led_cb;
    if (!espnow_mesh_init()) return 0;
    espnow_mesh_set_recv_cb(espnow_recv_cb);
    if (s_led_cb)
        s_led_cb(s_is_gateway ? LEX_LED_ROOT : LEX_LED_DEVICE);
    s_init_done = true;
    return 1;
}

void mesh_handler_loop(void) {
    uint32_t now = millis();
    if (now - s_last_send_ms >= MESH_SEND_INTERVAL_MS) {
        s_last_send_ms = now;
        LexaFullFrame_t frame;
        if (sensor_sim_get_latest_frame(&frame)) {
            mesh_handler_send_frame((const uint8_t *)&frame);
            /* Passerelle : envoie aussi sa trame en JSON sur Serial */
            if (s_is_gateway) {
                char json[400];
                frame_to_json(&frame, json, sizeof(json));
                log_dual_println(json);
            }
        }
    }
}

int mesh_handler_send_frame(const uint8_t *frame32) {
    if (!s_init_done || !frame32) return 0;
    return espnow_mesh_send_broadcast(frame32, LEXA_FRAME_SIZE) ? 1 : 0;
}

int mesh_handler_send_broadcast_raw(const char *msg) {
    (void)msg;
    return 0;  /* ESP-NOW n'a pas d'envoi raw OTA */
}

int mesh_handler_send_to(uint32_t to, const char *msg) {
    (void)to;
    (void)msg;
    return 0;  /* ESP-NOW n'a pas sendSingle par ID */
}

int mesh_handler_is_root(void) {
    return s_is_gateway ? 1 : 0;
}

#endif /* LEXACARE_MESH_PAINLESS / LEXACARE_MESH_32B */
