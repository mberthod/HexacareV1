/**
 * @file serial_gateway.cpp
 * @brief ROOT : sortie JSON pour trames Data ; entrée = protocole binaire (0x01/0x02 + 38 octets + chunks 200)
 *        ou lignes texte OTA_CHUNK:index:total:400hex pour broadcast mesh.
 */

#include "comm/serial_gateway.h"
#include "comm/mesh_flooding.h"
#include "config/config.h"
#include "lexacare_protocol.h"
#include "ota_mesh.h"
#include "system/log_dual.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_random.h>
#include <stdio.h>
#include <string.h>

#define OTA_CHUNK_PREFIX "OTA_CHUNK:"
#define OTA_HEX_LEN 400

/** Mode binaire : 0x01 = OTA Série (ROOT se flashe), 0x02 = OTA Mesh (ROOT diffuse). */
#define OTA_SERIAL_MODE_ROOT 0x01
#define OTA_SERIAL_MODE_MESH 0x02

enum SerialGatewayState {
    SERIAL_STATE_IDLE = 0,
    SERIAL_STATE_OTA_ROOT_HEADER,
    SERIAL_STATE_OTA_ROOT_CHUNK,
    SERIAL_STATE_OTA_MESH_HEADER,
    SERIAL_STATE_OTA_MESH_CHUNK
};

static bool s_serial_gateway_init_done = false;
static uint32_t s_msg_id_seq = 0;
/** True pendant la réception OTA Série (0x01 + header + chunks) : le mesh ne doit pas injecter OTA dans ota_mesh. */
static volatile bool s_ota_serial_receiving = false;

/** Handles des tâches à suspendre pendant OTA Série (enregistrés par main). */
static TaskHandle_t s_task_mesh = nullptr;
static TaskHandle_t s_task_tx = nullptr;
static TaskHandle_t s_task_sensor = nullptr;

/** Payload OTA réutilisable (évite gros buffer sur la pile de serial_gateway_task). */
static OtaChunkPayload_t s_ota_chunk_payload;

int serial_gateway_is_ota_serial_receiving(void) {
    return s_ota_serial_receiving ? 1 : 0;
}

void serial_gateway_register_tasks_for_ota_suspend(TaskHandle_t mesh_handle, TaskHandle_t tx_handle, TaskHandle_t sensor_handle) {
    s_task_mesh = mesh_handle;
    s_task_tx = tx_handle;
    s_task_sensor = sensor_handle;
}

/** Suspend mesh, tx et sensor pour ne garder que le processus OTA série actif. */
static void ota_serial_suspend_other_tasks(void) {
    if (s_task_mesh) { vTaskSuspend(s_task_mesh); }
    if (s_task_tx)   { vTaskSuspend(s_task_tx); }
    if (s_task_sensor) { vTaskSuspend(s_task_sensor); }
    log_dual_println("[SERIE] Tâches meshProc, espnowTx, sensorSim suspendues (OTA Série uniquement).");
}

/** Reprend les tâches après OTA Série. */
static void ota_serial_resume_other_tasks(void) {
    if (s_task_mesh)   { vTaskResume(s_task_mesh); }
    if (s_task_tx)     { vTaskResume(s_task_tx); }
    if (s_task_sensor) { vTaskResume(s_task_sensor); }
    log_dual_println("[SERIE] Tâches meshProc, espnowTx, sensorSim reprises.");
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
        //Serial.println(buf);
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

    OtaChunkPayload_t payload;
    payload.chunkIndex = (uint16_t)idx;
    payload.totalChunks = (uint16_t)total;
    memcpy(payload.data, data, OTA_CHUNK_DATA_SIZE);

    uint8_t packet[ESPNOW_MESH_HEADER_SIZE + OTA_CHUNK_PAYLOAD_SIZE];
    EspNowMeshHeader_t *h = (EspNowMeshHeader_t *)packet;
    h->msgId = (uint32_t)esp_random() | ((uint32_t)s_msg_id_seq++ << 16);
    h->msgType = MSG_TYPE_OTA_CHUNK;
    h->ttl = (uint8_t)ESPNOW_TTL_DEFAULT;
    uint8_t my_mac[6];
    mesh_flooding_get_my_mac(my_mac);
    h->sourceNodeId = (uint16_t)((my_mac[4] << 8) | my_mac[5]);
    memcpy(packet + ESPNOW_MESH_HEADER_SIZE, &payload, OTA_CHUNK_PAYLOAD_SIZE);
    mesh_flooding_send_broadcast(packet, sizeof(packet));
}

int serial_gateway_init(void) {
    Serial.setRxBufferSize(1024);
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
    uint16_t ota_total_chunks = 0;
    uint16_t ota_chunk_idx = 0;

    for (;;) {
        if (state == SERIAL_STATE_IDLE) {
            if (Serial.available() < 1) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            int b = Serial.read();
            if (b == OTA_SERIAL_MODE_ROOT) {
                log_dual_println("[SERIE] Octet mode reçu: 0x01 (OTA Série ROOT). Attente en-tête 38 octets...");
                s_ota_serial_receiving = true;  /* bloquer OTA mesh pendant réception série */
                state = SERIAL_STATE_OTA_ROOT_HEADER;
                ota_count = 0;
                continue;
            }
            if (b == OTA_SERIAL_MODE_MESH) {
                log_dual_println("[SERIE] Octet mode reçu: 0x02 (OTA Mesh). Attente en-tête 38 octets...");
                state = SERIAL_STATE_OTA_MESH_HEADER;
                ota_count = 0;
                continue;
            }
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

        if (state == SERIAL_STATE_OTA_ROOT_HEADER) {
            while (Serial.available() > 0 && ota_count < OTA_ADV_PAYLOAD_SIZE) {
                ota_buf[ota_count++] = (uint8_t)Serial.read();
            }
            if (ota_count >= OTA_ADV_PAYLOAD_SIZE) {
                uint32_t total_size = (uint32_t)ota_buf[0] | ((uint32_t)ota_buf[1] << 8) | ((uint32_t)ota_buf[2] << 16) | ((uint32_t)ota_buf[3] << 24);
                ota_total_chunks = (uint16_t)ota_buf[4] | ((uint16_t)ota_buf[5] << 8);
                char buf[100];
                snprintf(buf, sizeof(buf), "[SERIE] En-tête OK: size=%lu octets, %u chunks. Réception chunks 200 octets...", (unsigned long)total_size, (unsigned)ota_total_chunks);
                log_dual_println(buf);
                ota_mesh_on_ota_adv(ota_buf, OTA_ADV_PAYLOAD_SIZE);
                ota_chunk_idx = 0;
                state = SERIAL_STATE_OTA_ROOT_CHUNK;
                ota_count = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        if (state == SERIAL_STATE_OTA_ROOT_CHUNK) {
            while (Serial.available() > 0 && ota_count < OTA_CHUNK_DATA_SIZE) {
                ota_buf[ota_count++] = (uint8_t)Serial.read();
            }
            if (ota_count >= OTA_CHUNK_DATA_SIZE) {
                s_ota_chunk_payload.chunkIndex = ota_chunk_idx;
                s_ota_chunk_payload.totalChunks = ota_total_chunks;
                memcpy(s_ota_chunk_payload.data, ota_buf, OTA_CHUNK_DATA_SIZE);
                ota_mesh_on_ota_chunk((const uint8_t *)&s_ota_chunk_payload, OTA_CHUNK_PAYLOAD_SIZE);
                ota_chunk_idx++;
                ota_count = 0;
                if (ota_chunk_idx % 50 == 0 || ota_chunk_idx == ota_total_chunks) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "[SERIE] Chunk %u/%u reçu (OTA Série ROOT).", (unsigned)ota_chunk_idx, (unsigned)ota_total_chunks);
                    log_dual_println(buf);
                }
                if (ota_chunk_idx >= ota_total_chunks) {
                    log_dual_println("[SERIE] OTA Série ROOT: réception terminée (tous les chunks envoyés à ota_mesh).");
                    log_dual_println("[SERIE] -> ota_mesh va vérifier MD5 puis définir partition boot et rebooter.");
                    s_ota_serial_receiving = false;  /* réautoriser OTA mesh */
                    ota_serial_resume_other_tasks();
                    state = SERIAL_STATE_IDLE;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        if (state == SERIAL_STATE_OTA_MESH_HEADER) {
            while (Serial.available() > 0 && ota_count < OTA_ADV_PAYLOAD_SIZE) {
                ota_buf[ota_count++] = (uint8_t)Serial.read();
            }
            if (ota_count >= OTA_ADV_PAYLOAD_SIZE) {
                log_dual_println("[SERIE] En-tête 38 octets reçu (OTA Mesh). Diffusion OTA_ADV + chunks...");
                uint8_t packet_adv[ESPNOW_MESH_HEADER_SIZE + OTA_ADV_PAYLOAD_SIZE];
                EspNowMeshHeader_t *h = (EspNowMeshHeader_t *)packet_adv;
                h->msgId = (uint32_t)esp_random() | ((uint32_t)s_msg_id_seq++ << 16);
                h->msgType = MSG_TYPE_OTA_ADV;
                h->ttl = (uint8_t)ESPNOW_TTL_DEFAULT;
                uint8_t my_mac[6];
                mesh_flooding_get_my_mac(my_mac);
                h->sourceNodeId = (uint16_t)((my_mac[4] << 8) | my_mac[5]);
                memcpy(packet_adv + ESPNOW_MESH_HEADER_SIZE, ota_buf, OTA_ADV_PAYLOAD_SIZE);
                mesh_flooding_send_broadcast(packet_adv, sizeof(packet_adv));
                ota_total_chunks = (uint16_t)ota_buf[4] | ((uint16_t)ota_buf[5] << 8);
                ota_chunk_idx = 0;
                state = SERIAL_STATE_OTA_MESH_CHUNK;
                ota_count = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        if (state == SERIAL_STATE_OTA_MESH_CHUNK) {
            while (Serial.available() > 0 && ota_count < OTA_CHUNK_DATA_SIZE) {
                ota_buf[ota_count++] = (uint8_t)Serial.read();
            }
            if (ota_count >= OTA_CHUNK_DATA_SIZE) {
                s_ota_chunk_payload.chunkIndex = ota_chunk_idx;
                s_ota_chunk_payload.totalChunks = ota_total_chunks;
                memcpy(s_ota_chunk_payload.data, ota_buf, OTA_CHUNK_DATA_SIZE);
                uint8_t packet[ESPNOW_MESH_HEADER_SIZE + OTA_CHUNK_PAYLOAD_SIZE];
                EspNowMeshHeader_t *h = (EspNowMeshHeader_t *)packet;
                h->msgId = (uint32_t)esp_random() | ((uint32_t)s_msg_id_seq++ << 16);
                h->msgType = MSG_TYPE_OTA_CHUNK;
                h->ttl = (uint8_t)ESPNOW_TTL_DEFAULT;
                uint8_t my_mac[6];
                mesh_flooding_get_my_mac(my_mac);
                h->sourceNodeId = (uint16_t)((my_mac[4] << 8) | my_mac[5]);
                memcpy(packet + ESPNOW_MESH_HEADER_SIZE, &s_ota_chunk_payload, OTA_CHUNK_PAYLOAD_SIZE);
                mesh_flooding_send_broadcast(packet, sizeof(packet));
                ota_chunk_idx++;
                ota_count = 0;
                if (ota_chunk_idx >= ota_total_chunks) {
                    log_dual_println("[SERIE] OTA Mesh: diffusion terminée.");
                    state = SERIAL_STATE_IDLE;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
