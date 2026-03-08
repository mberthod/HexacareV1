/**
 * @file serial_gateway.cpp
 * @brief ROOT : sortie JSON pour trames Data ; entrée OTA UART pour ota_tree_manager.
 */

#include "comm/serial_gateway.h"
#include "comm/ota_tree_manager.h"
#include "config/config.h"
#include "lexacare_protocol.h"
#include "system/log_dual.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_random.h>
#include <stdio.h>
#include <string.h>

#define OTA_CHUNK_PREFIX "OTA_CHUNK:"
#define OTA_HEX_LEN 400

// Mode binaire : 0x01 = OTA Série (ROOT se flashe), 0x02 = OTA Mesh (ROOT diffuse)
// Dans V2, les deux modes impliquent : Stockage local -> Distribution Arbre
#define OTA_SERIAL_MODE_ROOT 0x01
#define OTA_SERIAL_MODE_MESH 0x02

enum SerialGatewayState {
    SERIAL_STATE_IDLE = 0,
    SERIAL_STATE_OTA_HEADER,
    SERIAL_STATE_OTA_CHUNK
};

static bool s_serial_gateway_init_done = false;
static uint32_t s_msg_id_seq = 0;
/** True pendant la réception OTA Série (0x01 + header + chunks) : le mesh ne doit pas injecter OTA dans ota_mesh. */
static volatile bool s_ota_serial_receiving = false;

// Handles des tâches à suspendre (non utilisé dans V2 car ota_tree_manager gère la concurrence via état)
// static TaskHandle_t s_task_mesh = nullptr;
// static TaskHandle_t s_task_tx = nullptr;
// static TaskHandle_t s_task_sensor = nullptr;

int serial_gateway_is_ota_serial_receiving(void) {
    return s_ota_serial_receiving ? 1 : 0;
}

void serial_gateway_register_tasks_for_ota_suspend(TaskHandle_t mesh_handle, TaskHandle_t tx_handle, TaskHandle_t sensor_handle) {
    // s_task_mesh = mesh_handle;
    // s_task_tx = tx_handle;
    // s_task_sensor = sensor_handle;
}

static int hex_to_binary(const char *hex, size_t hex_len, uint8_t *out, size_t out_size) {
    if (hex_len % 2 != 0 || out_size < hex_len / 2) return -1;
    for (size_t i = 0; i < hex_len && i / 2 < out_size; i += 2) {
        char c1 = hex[i], c2 = hex[i + 1];
        int v1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : (c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : (c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : -1;
        int v2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : (c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : (c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : -1;
        if (v1 < 0 || v2 < 0) return -1;
        out[i / 2] = (uint8_t)((v1 << 4) | v2);
    }
    return (int)(hex_len / 2);
}

void serial_gateway_send_data_json(const void *frame, uint32_t fw_ver) {
    const LexaFullFrame_t *f = (const LexaFullFrame_t *)frame;
    StaticJsonDocument<384> doc;
    doc["nodeId"] = (unsigned)f->nodeShortId;
    doc["nodeShortId"] = (unsigned)f->nodeShortId;
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
    doc["fw_ver"] = (unsigned)fw_ver;
    char buf[400];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n > 0) {
        Serial.println(buf);
    }
}

static void process_line(char *line, size_t len) {
    if (len < sizeof(OTA_CHUNK_PREFIX) - 1) return;
    if (memcmp(line, OTA_CHUNK_PREFIX, sizeof(OTA_CHUNK_PREFIX) - 1) != 0) return;
    char *p = line + sizeof(OTA_CHUNK_PREFIX) - 1;
    unsigned long idx = strtoul(p, &p, 10);
    if (*p != ':') return;
    p++;
    unsigned long total = strtoul(p, &p, 10);
    if (*p != ':') return;
    p++;
    if (idx > 0xFFFF || total > 0xFFFF || total == 0) return;
    size_t hex_len = strlen(p);
    while (hex_len > 0 && (p[hex_len - 1] == '\r' || p[hex_len - 1] == '\n')) hex_len--;
    if (hex_len != OTA_HEX_LEN) return;

    uint8_t data[OTA_CHUNK_DATA_SIZE];
    int bin_len = hex_to_binary(p, hex_len, data, sizeof(data));
    if (bin_len != (int)OTA_CHUNK_DATA_SIZE) return;

    // TODO: Adapter pour OTA Tree Manager si on veut supporter le mode texte
    // Pour l'instant, on se concentre sur le mode binaire (0x01/0x02)
    // OtaChunkPayload payload;
    // payload.chunkIndex = (uint16_t)idx;
    // payload.totalChunks = (uint16_t)total;
    // memcpy(payload.data, data, OTA_CHUNK_DATA_SIZE);
    
    // ...
}

int serial_gateway_init(void) {
    Serial.setRxBufferSize(4096); // Augmenter buffer pour OTA rapide
    s_serial_gateway_init_done = true;
    return 1;
}

void serial_gateway_task(void *pv) {
    (void)pv;
    log_dual_println("[TASK] serial_gateway running (Core 0)");
    
    static char line_buf[600];
    size_t pos = 0;
    
    enum SerialGatewayState state = SERIAL_STATE_IDLE;
    static uint8_t ota_buf[256];
    size_t ota_count = 0;
    
    // Métadonnées OTA
    uint32_t ota_total_size = 0;
    uint16_t ota_total_chunks = 0;
    char ota_md5[33] = {0};
    uint16_t ota_chunk_idx = 0;

    for (;;) {
        if (state == SERIAL_STATE_IDLE) {
            if (Serial.available() < 1) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            int b = Serial.read();
            
            // Détection début OTA Binaire (0x01 ou 0x02)
            if (b == OTA_SERIAL_MODE_ROOT || b == OTA_SERIAL_MODE_MESH) {
                log_dual_println("[SERIE] Debut OTA UART. Attente en-tête 38 octets...");
                s_ota_serial_receiving = true;
                state = SERIAL_STATE_OTA_HEADER;
                ota_count = 0;
                continue;
            }
            
            // Sinon, traitement ligne texte (ex: config, commandes futures)
            line_buf[0] = (char)(b & 0xFF);
            pos = 1;
            while (Serial.available() > 0 && pos < sizeof(line_buf) - 1) {
                char c = (char)Serial.read();
                if (c == '\n' || c == '\r') {
                    if (pos > 0) {
                        line_buf[pos] = '\0';
                        process_line(line_buf, pos);
                    }
                    pos = 0;
                    break;
                }
                line_buf[pos++] = c;
            }
            if (pos > 0 && Serial.available() == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            continue;
        }

        if (state == SERIAL_STATE_OTA_HEADER) {
            while (Serial.available() > 0 && ota_count < OTA_ADV_PAYLOAD_SIZE) {
                ota_buf[ota_count++] = (uint8_t)Serial.read();
            }
            if (ota_count >= OTA_ADV_PAYLOAD_SIZE) {
                // Parsing Header
                ota_total_size = (uint32_t)ota_buf[0] | ((uint32_t)ota_buf[1] << 8) | ((uint32_t)ota_buf[2] << 16) | ((uint32_t)ota_buf[3] << 24);
                ota_total_chunks = (uint16_t)ota_buf[4] | ((uint16_t)ota_buf[5] << 8);
                memcpy(ota_md5, ota_buf + 6, 32);
                ota_md5[32] = '\0';
                
                char buf[100];
                snprintf(buf, sizeof(buf), "[SERIE] Header OK: size=%lu, chunks=%u. MD5=%.8s...", (unsigned long)ota_total_size, (unsigned)ota_total_chunks, ota_md5);
                log_dual_println(buf);
                
                ota_chunk_idx = 0;
                state = SERIAL_STATE_OTA_CHUNK;
                ota_count = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        if (state == SERIAL_STATE_OTA_CHUNK) {
            while (Serial.available() > 0 && ota_count < OTA_CHUNK_DATA_SIZE) {
                ota_buf[ota_count++] = (uint8_t)Serial.read();
            }
            if (ota_count >= OTA_CHUNK_DATA_SIZE) {
                // Envoyer le chunk au manager OTA Tree
                uint32_t offset = ota_chunk_idx * OTA_CHUNK_DATA_SIZE;
                ota_tree_on_uart_chunk(offset, ota_buf, OTA_CHUNK_DATA_SIZE, ota_total_size, ota_md5);
                
                ota_chunk_idx++;
                ota_count = 0;
                
                if (ota_chunk_idx % 50 == 0) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "[SERIE] Chunk %u/%u recu.", (unsigned)ota_chunk_idx, (unsigned)ota_total_chunks);
                    log_dual_println(buf);
                }

                if (ota_chunk_idx >= ota_total_chunks) {
                    log_dual_println("[SERIE] OTA UART terminee.");
                    s_ota_serial_receiving = false;
                    state = SERIAL_STATE_IDLE;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
