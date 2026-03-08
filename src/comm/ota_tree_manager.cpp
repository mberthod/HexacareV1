/**
 * @file ota_tree_manager.cpp
 * @brief Implémentation du Store & Forward OTA.
 */

#include "ota_tree_manager.h"
#include "routing_manager.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <vector>

static const char* TAG = "OTA_TREE";

enum OtaState {
    OTA_IDLE,
    OTA_RECEIVING_UART, // ROOT: Reçoit du PC
    OTA_RECEIVING_MESH, // CHILD: Reçoit du Parent
    OTA_DISTRIBUTING,   // SERVER: Distribue aux enfants
    OTA_FINISHED        // Prêt à rebooter
};

static OtaState s_state = OTA_IDLE;
static const esp_partition_t* s_update_partition = nullptr;
static uint32_t s_total_size = 0;
static uint32_t s_written_size = 0;
static char s_expected_md5[33] = {0};

// Pour la distribution
static std::vector<ChildInfo> s_targets;
static size_t s_current_target_idx = 0;
static bool s_current_child_done = false;
static uint32_t s_last_req_time = 0;

void ota_tree_init(void) {
    s_state = OTA_IDLE;
    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_update_partition) {
        ESP_LOGE(TAG, "Pas de partition OTA trouvée !");
    }
}

// --- ROOT: Réception UART ---
void ota_tree_on_uart_chunk(uint32_t offset, const uint8_t* data, uint16_t len, uint32_t totalSize, const char* md5) {
    if (s_state == OTA_IDLE) {
        s_state = OTA_RECEIVING_UART;
        s_total_size = totalSize;
        strncpy(s_expected_md5, md5, 32);
        s_written_size = 0;
        ESP_LOGI(TAG, "Debut OTA UART. Taille: %lu", s_total_size);
    }

    if (s_state != OTA_RECEIVING_UART) return;

    if (esp_partition_write(s_update_partition, offset, data, len) != ESP_OK) {
        ESP_LOGE(TAG, "Erreur ecriture flash offset %lu", offset);
        return;
    }
    s_written_size += len;

    if (s_written_size >= s_total_size) {
        ESP_LOGI(TAG, "Reception UART terminee. Passage en mode DISTRIBUTION.");
        s_state = OTA_DISTRIBUTING;
        // Charger la liste des enfants
        s_targets = routing_get_children();
        s_current_target_idx = 0;
        s_current_child_done = false;
    }
}

// --- CHILD: Réception Mesh ---
static void handle_ota_adv(const uint8_t* src_mac, const uint8_t* payload) {
    OtaAdvPayload* adv = (OtaAdvPayload*)payload;
    if (s_state == OTA_IDLE) {
        ESP_LOGI(TAG, "Recu OTA ADV. Taille: %lu. Passage en RECEIVING_MESH", adv->totalSize);
        s_state = OTA_RECEIVING_MESH;
        s_total_size = adv->totalSize;
        memcpy(s_expected_md5, adv->md5Hex, 32);
        s_written_size = 0;
        // On demandera le premier chunk dans la loop
    }
}

static void handle_ota_chunk(const uint8_t* payload, uint16_t len) {
    if (s_state != OTA_RECEIVING_MESH) return;
    
    OtaChunkPayload* chunk = (OtaChunkPayload*)payload;
    uint32_t offset = chunk->chunkIndex * 200; // Attention: chunkIndex est u16, data 200
    // Note: Dans le protocole V1 c'était chunkIndex * 200. Ici on garde la compatibilité.
    
    if (esp_partition_write(s_update_partition, offset, chunk->data, len - sizeof(uint16_t)*2) == ESP_OK) {
        s_written_size += (len - sizeof(uint16_t)*2);
        // Logique simplifiée : on incrémente juste.
        // Vrai système : bitmap pour savoir quel chunk manque.
    }
}

// --- SERVER: Distribution ---
static void handle_ota_req(const uint8_t* src_mac, const uint8_t* payload) {
    if (s_state != OTA_DISTRIBUTING) return;
    
    // Vérifier que c'est l'enfant en cours
    if (s_current_target_idx < s_targets.size()) {
        if (memcmp(src_mac, s_targets[s_current_target_idx].mac, 6) == 0) {
            OtaReqPayload* req = (OtaReqPayload*)payload;
            
            // Lire le chunk en flash
            uint8_t buffer[250];
            OtaChunkPayload* chunk = (OtaChunkPayload*)(buffer + sizeof(TreeMeshHeader)); // Astuce pour offset
            // On prépare juste le payload
            uint8_t chunkData[200];
            if (esp_partition_read(s_update_partition, req->offset, chunkData, req->length) == ESP_OK) {
                // Construire réponse MSG_OTA_CHUNK
                OtaChunkPayload resp;
                resp.chunkIndex = req->offset / 200;
                resp.totalChunks = s_total_size / 200; // Approx
                memcpy(resp.data, chunkData, req->length);
                
                routing_send_unicast(src_mac, MSG_OTA_CHUNK, (uint8_t*)&resp, sizeof(OtaChunkPayload));
                s_last_req_time = xTaskGetTickCount();
            }
        }
    }
}

static void handle_ota_done(const uint8_t* src_mac) {
    if (s_state != OTA_DISTRIBUTING) return;
    if (s_current_target_idx < s_targets.size()) {
        if (memcmp(src_mac, s_targets[s_current_target_idx].mac, 6) == 0) {
            ESP_LOGI(TAG, "Enfant %d a fini sa MAJ.", s_current_target_idx);
            s_current_child_done = true;
        }
    }
}

void ota_tree_on_mesh_message(const uint8_t* src_mac, uint8_t msgType, const uint8_t* payload, uint16_t len) {
    switch (msgType) {
        case MSG_OTA_ADV: handle_ota_adv(src_mac, payload); break;
        case MSG_OTA_CHUNK: handle_ota_chunk(payload, len); break;
        case MSG_OTA_REQ: handle_ota_req(src_mac, payload); break;
        case MSG_OTA_DONE: handle_ota_done(src_mac); break;
    }
}

void ota_tree_task(void *pv) {
    while (1) {
        if (s_state == OTA_RECEIVING_MESH) {
            // Demander le prochain chunk manquant
            // Simplification: on demande séquentiellement
            if (s_written_size < s_total_size) {
                uint8_t parent_mac[6];
                if (routing_get_parent_mac(parent_mac)) {
                    OtaReqPayload req;
                    req.offset = s_written_size;
                    req.length = (s_total_size - s_written_size > 200) ? 200 : (s_total_size - s_written_size);
                    routing_send_unicast(parent_mac, MSG_OTA_REQ, (uint8_t*)&req, sizeof(req));
                    vTaskDelay(pdMS_TO_TICKS(100)); // Throttle
                }
            } else {
                // Fini !
                ESP_LOGI(TAG, "Reception Mesh terminee. Verification...");
                // TODO: Verify MD5
                uint8_t parent_mac[6];
                if (routing_get_parent_mac(parent_mac)) {
                    routing_send_unicast(parent_mac, MSG_OTA_DONE, nullptr, 0);
                }
                s_state = OTA_DISTRIBUTING;
                s_targets = routing_get_children();
                s_current_target_idx = 0;
                s_current_child_done = false;
            }
        }
        else if (s_state == OTA_DISTRIBUTING) {
            if (s_current_target_idx < s_targets.size()) {
                // Envoyer ADV à l'enfant courant
                if (!s_current_child_done) {
                    OtaAdvPayload adv;
                    adv.totalSize = s_total_size;
                    adv.totalChunks = s_total_size / 200; // Approx
                    memcpy(adv.md5Hex, s_expected_md5, 32);
                    routing_send_unicast(s_targets[s_current_target_idx].mac, MSG_OTA_ADV, (uint8_t*)&adv, sizeof(adv));
                    
                    // Timeout si l'enfant ne répond pas (ex: 30s)
                    if (xTaskGetTickCount() - s_last_req_time > pdMS_TO_TICKS(30000)) {
                        ESP_LOGW(TAG, "Timeout enfant %d, passage au suivant", s_current_target_idx);
                        s_current_target_idx++;
                        s_current_child_done = false;
                        s_last_req_time = xTaskGetTickCount();
                    }
                } else {
                    s_current_target_idx++;
                    s_current_child_done = false;
                    s_last_req_time = xTaskGetTickCount();
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
            } else {
                // Tous les enfants servis
                ESP_LOGI(TAG, "Distribution terminee. Reboot !");
                esp_ota_set_boot_partition(s_update_partition);
                esp_restart();
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
