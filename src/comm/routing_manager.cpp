/**
 * @file routing_manager.cpp
 * @brief Moteur d'auto-cicatrisation (Self-Healing) pour le Tree Mesh.
 */

#include "routing_manager.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <string.h>
#include <algorithm>

static const char* TAG = "ROUTING";

enum NodeState {
    STATE_SCANNING,
    STATE_JOINING,
    STATE_CONNECTED
};

static NodeState s_state = STATE_SCANNING;
static uint8_t s_my_layer = 255;
static uint16_t s_my_id = 0;
static uint8_t s_parent_mac[6] = {0};
static uint16_t s_parent_id = 0;
static uint32_t s_last_heartbeat_ack = 0;

// Enfants connectés
static std::vector<ChildInfo> s_children;
static const uint32_t CHILD_TIMEOUT_MS = 15000; // 15s sans heartbeat = enfant perdu

// Variables pour le scan
static uint16_t s_best_parent_candidate = 0;
static uint8_t s_best_parent_mac[6];
static int8_t s_best_rssi = -127;
static uint8_t s_best_layer = 255;

void routing_init(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    s_my_id = (uint16_t)((mac[4] << 8) | mac[5]);
    ESP_LOGI(TAG, "Init Routing. My ID: %04X", s_my_id);
}

// Envoi d'un message Unicast via ESP-NOW
bool routing_send_unicast(const uint8_t* dest_mac, uint8_t msgType, const uint8_t* payload, uint16_t len) {
    uint8_t buffer[250];
    if (sizeof(TreeMeshHeader) + len > sizeof(buffer)) return false;

    TreeMeshHeader* hdr = (TreeMeshHeader*)buffer;
    hdr->magic = MESH_MAGIC_BYTE;
    hdr->msgType = msgType;
    hdr->srcNodeId = s_my_id;
    hdr->destNodeId = 0; // À remplir si besoin de routage plus complexe
    hdr->layer = s_my_layer;
    hdr->ttl = 15;
    hdr->payloadLen = len;
    
    if (len > 0 && payload != nullptr) {
        memcpy(buffer + sizeof(TreeMeshHeader), payload, len);
    }
    
    // S'assurer que le pair existe
    if (!esp_now_is_peer_exist(dest_mac)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, dest_mac, 6);
        peerInfo.channel = 0; // Utiliser le canal actuel
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }

    return esp_now_send(dest_mac, buffer, sizeof(TreeMeshHeader) + len) == ESP_OK;
}

// Gestion des enfants
static void update_child(uint16_t childId, const uint8_t* mac) {
    uint32_t now = xTaskGetTickCount();
    bool found = false;
    for (auto& child : s_children) {
        if (child.nodeId == childId) {
            child.lastSeen = now;
            found = true;
            break;
        }
    }
    if (!found && s_children.size() < MAX_CHILDREN_PER_NODE) {
        ChildInfo newChild;
        newChild.nodeId = childId;
        memcpy(newChild.mac, mac, 6);
        newChild.lastSeen = now;
        s_children.push_back(newChild);
        ESP_LOGI(TAG, "Nouvel enfant connecte: %04X", childId);
    }
}

static void cleanup_children() {
    uint32_t now = xTaskGetTickCount();
    auto it = s_children.begin();
    while (it != s_children.end()) {
        if ((now - it->lastSeen) > pdMS_TO_TICKS(CHILD_TIMEOUT_MS)) {
            ESP_LOGW(TAG, "Enfant perdu (timeout): %04X", it->nodeId);
            it = s_children.erase(it);
        } else {
            ++it;
        }
    }
}

// Fonction appelée lors de la réception d'un paquet ESP-NOW
void on_mesh_receive(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < sizeof(TreeMeshHeader)) return;
    TreeMeshHeader* hdr = (TreeMeshHeader*)data;
    if (hdr->magic != MESH_MAGIC_BYTE) return;

    // 1. Gestion des messages entrants (SCAN & JOIN)
    if (s_state == STATE_SCANNING && hdr->msgType == MSG_BEACON) {
        BeaconPayload* b = (BeaconPayload*)(data + sizeof(TreeMeshHeader));
        // On cherche un parent qui a de la place et qui est plus proche du root
        if (b->currentChildrenCount < MAX_CHILDREN_PER_NODE && hdr->layer < s_best_layer) {
            s_best_layer = hdr->layer;
            s_best_parent_candidate = hdr->srcNodeId;
            memcpy(s_best_parent_mac, mac, 6);
        }
    } 
    else if (s_state == STATE_JOINING && hdr->msgType == MSG_JOIN_ACK) {
        if (hdr->srcNodeId == s_best_parent_candidate) {
            s_state = STATE_CONNECTED;
            s_parent_id = hdr->srcNodeId;
            memcpy(s_parent_mac, mac, 6);
            s_my_layer = hdr->layer + 1;
            s_last_heartbeat_ack = xTaskGetTickCount();
            ESP_LOGI(TAG, "Connecte au parent %04X, ma couche: %d", s_parent_id, s_my_layer);
        }
    }

    // 2. Gestion des messages venant des enfants (si je suis connecté ou ROOT)
    // Le ROOT est toujours "connecté" (layer 0)
    if (s_state == STATE_CONNECTED || s_my_layer == 0) {
        if (hdr->msgType == MSG_JOIN_REQ) {
            // Un orphelin veut me rejoindre
            if (s_children.size() < MAX_CHILDREN_PER_NODE) {
                update_child(hdr->srcNodeId, mac);
                // Répondre ACK
                routing_send_unicast(mac, MSG_JOIN_ACK, nullptr, 0);
            }
        }
        else if (hdr->msgType == MSG_HEARTBEAT) {
            update_child(hdr->srcNodeId, mac);
        }
    }
    
    // 3. Ack du Heartbeat parent
    if (s_state == STATE_CONNECTED && hdr->msgType == MSG_HEARTBEAT && hdr->srcNodeId == s_parent_id) {
        // C'est un ping de mon parent ? Non, le parent n'envoie pas de heartbeat vers l'enfant en général
        // Mais si on veut du bidirectionnel...
        // Pour l'instant, on considère que le parent envoie des BEACONS qui servent de keepalive implicite
    }
}

void routing_task(void *pv) {
    TickType_t last_beacon = 0;
    
    // Si Layer 0 (ROOT), on est d'office connecté
    if (s_my_layer == 0) {
        s_state = STATE_CONNECTED;
    }

    while (1) {
        // Maintenance des enfants
        cleanup_children();

        // Machine à états
        if (s_state == STATE_SCANNING) {
            // Si on n'est pas ROOT, on cherche
            if (s_my_layer != 0) {
                ESP_LOGI(TAG, "Recherche d'un parent...");
                s_best_layer = 255;
                vTaskDelay(pdMS_TO_TICKS(2000)); 
                
                if (s_best_layer != 255) {
                    s_state = STATE_JOINING;
                    routing_send_unicast(s_best_parent_mac, MSG_JOIN_REQ, nullptr, 0);
                }
            }
        } 
        else if (s_state == STATE_JOINING) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (s_state != STATE_CONNECTED) {
                s_state = STATE_SCANNING; // Timeout
            }
        } 
        else if (s_state == STATE_CONNECTED) {
            // 1. Envoi Heartbeat au parent (sauf si ROOT)
            if (s_my_layer != 0) {
                routing_send_unicast(s_parent_mac, MSG_HEARTBEAT, nullptr, 0);
                
                // Timeout parent ? (On peut utiliser la réception de Beacon ou ACK comme keepalive)
                // Ici simplifié : on suppose que si l'envoi échoue (ACK mac manquant), ESP-NOW le dira ? 
                // Non, ESP-NOW sans ACK ne garantit rien. Il faudrait un ACK applicatif ou surveiller les Beacons.
            }

            // 2. Diffusion Beacon pour les orphelins (toutes les secondes par ex)
            if (xTaskGetTickCount() - last_beacon > pdMS_TO_TICKS(1000)) {
                BeaconPayload b;
                b.currentChildrenCount = (uint8_t)s_children.size();
                b.rssi = 0; 
                // Broadcast Beacon
                uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                routing_send_unicast(broadcast_mac, MSG_BEACON, (uint8_t*)&b, sizeof(b));
                last_beacon = xTaskGetTickCount();
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

// API Publique
std::vector<ChildInfo> routing_get_children(void) {
    return s_children;
}

bool routing_get_parent_mac(uint8_t* mac_out) {
    if (s_state != STATE_CONNECTED || s_my_layer == 0) return false;
    memcpy(mac_out, s_parent_mac, 6);
    return true;
}

uint16_t routing_get_my_id(void) {
    return s_my_id;
}
