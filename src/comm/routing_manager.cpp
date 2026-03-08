/**
 * @file routing_manager.cpp
 * @brief Routage Tree Mesh (CDC Hexacare V2) : Construction du réseau, Heartbeats, Self-Healing.
 *
 * Machine d'état : INIT → SCANNING → EVALUATING → JOINING → CONNECTED.
 * En cas de perte parent : CONNECTED → ORPHAN → SCANNING.
 */

#include "comm/routing_manager.h"
#include "system/led_manager.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <string.h>
#include <algorithm>

static const char* TAG = "ROUTING";

enum NodeState {
    STATE_INIT,
    STATE_SCANNING,
    STATE_EVALUATING,
    STATE_JOINING,
    STATE_CONNECTED,
    STATE_ORPHAN
};

static NodeState s_state = STATE_INIT;
static uint8_t s_my_layer = 255;
static uint8_t s_my_layer_previous = 255; /* Pour loop avoidance (rejeter layer >= previous) */
static uint16_t s_my_id = 0;
static uint8_t s_parent_mac[6] = {0};
static uint16_t s_parent_id = 0;
static uint32_t s_last_heartbeat_ack = 0;  /* Dernier tick où on a reçu HEARTBEAT_ACK */
static uint32_t s_last_heartbeat_send = 0;
static uint8_t s_heartbeat_fail_count = 0;

static std::vector<ChildInfo> s_children;

/* Candidats parent (scan & evaluate) : on garde le meilleur */
static uint16_t s_best_parent_candidate = 0;
static uint8_t s_best_parent_mac[6] = {0};
static uint8_t s_best_layer = 255;
static uint8_t s_best_children_count = 255;

static void add_parent_as_peer(const uint8_t* mac) {
    if (!mac || esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        ESP_LOGD(TAG, "Peer parent ajoute");
    }
}

static void update_child(uint16_t childId, const uint8_t* mac) {
    uint32_t now = xTaskGetTickCount();
    for (auto& child : s_children) {
        if (child.nodeId == childId) {
            child.lastSeen = now;
            child.is_active = true;
            return;
        }
    }
    if (s_children.size() < MAX_CHILDREN_PER_NODE) {
        ChildInfo c;
        memcpy(c.mac, mac, 6);
        c.nodeId = childId;
        c.lastSeen = now;
        c.is_active = true;
        s_children.push_back(c);
        add_parent_as_peer(mac);
        ESP_LOGI(TAG, "Nouvel enfant: %04X", childId);
    }
}

/** Vérifie les timeouts enfants (30 s) et heartbeat manqués (3 → ORPHAN). */
static void routing_check_timeouts(void) {
    uint32_t now = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(ROUTING_CHILD_TIMEOUT_MS);

    /* Parent : retirer les enfants sans heartbeat depuis 30 s */
    auto it = s_children.begin();
    while (it != s_children.end()) {
        if ((now - it->lastSeen) > timeout) {
            esp_now_del_peer(it->mac);
            ESP_LOGW(TAG, "Enfant perdu (timeout 30s): %04X", it->nodeId);
            it = s_children.erase(it);
        } else {
            ++it;
        }
    }

    /* Enfant : si 3 heartbeats sans ACK → ORPHAN */
    if (s_state == STATE_CONNECTED && s_my_layer != 0) {
        if ((now - s_last_heartbeat_ack) > pdMS_TO_TICKS(ROUTING_HEARTBEAT_INTERVAL_MS + 2000)) {
            s_heartbeat_fail_count++;
            if (s_heartbeat_fail_count >= ROUTING_HEARTBEAT_FAIL_ORPHAN) {
                ESP_LOGW(TAG, "Parent perdu (3 heartbeats sans ACK) -> ORPHAN");
                s_state = STATE_ORPHAN;
                led_manager_set_state(LED_STATE_ORPHAN);
            }
        }
    }
}

/** CDC : reconnexion après perte du parent. */
static void routing_orphan_recovery(void) {
    s_my_layer_previous = s_my_layer;
    s_my_layer = 255;
    s_parent_id = 0;
    memset(s_parent_mac, 0, 6);
    s_heartbeat_fail_count = 0;
    s_best_layer = 255;
    s_best_parent_candidate = 0;
    memset(s_best_parent_mac, 0, 6);
    s_state = STATE_SCANNING;
    led_manager_set_state(LED_STATE_SCANNING);
    ESP_LOGI(TAG, "Orphan recovery -> SCANNING");
}

void routing_init(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    s_my_id = (uint16_t)((mac[4] << 8) | mac[5]);
    s_state = STATE_INIT;
    ESP_LOGI(TAG, "Init Routing. My ID: %04X", s_my_id);
}

void routing_set_root(void) {
    s_my_layer = 0;
    s_state = STATE_CONNECTED;
    led_manager_set_state(LED_STATE_ROOT);
    ESP_LOGI(TAG, "Mode ROOT (Layer 0)");
}

bool routing_send_unicast(const uint8_t* dest_mac, uint8_t msgType, const uint8_t* payload, uint16_t len) {
    uint8_t buffer[250];
    if (!dest_mac || sizeof(TreeMeshHeader) + len > sizeof(buffer)) return false;

    TreeMeshHeader* hdr = (TreeMeshHeader*)buffer;
    hdr->magic = MESH_MAGIC_BYTE;
    hdr->msgType = msgType;
    hdr->srcNodeId = s_my_id;
    hdr->destNodeId = 0;
    hdr->layer = s_my_layer;
    hdr->ttl = 15;
    hdr->payloadLen = len;
    if (len > 0 && payload) memcpy(buffer + sizeof(TreeMeshHeader), payload, len);

    if (!esp_now_is_peer_exist(dest_mac)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, dest_mac, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        if (esp_now_add_peer(&peerInfo) != ESP_OK) return false;
    }
    return esp_now_send(dest_mac, buffer, sizeof(TreeMeshHeader) + len) == ESP_OK;
}

void routing_forward_upstream(const uint8_t* data, size_t len) {
    if (!data || len < sizeof(TreeMeshHeader)) return;
    TreeMeshHeader* h = (TreeMeshHeader*)data;
    if (h->magic != MESH_MAGIC_BYTE || h->ttl == 0) return;
    if (s_my_layer == 0) return; /* ROOT ne forward pas en upstream */
    uint8_t parent_mac[6];
    if (!routing_get_parent_mac(parent_mac)) return;

    /* Décrémenter TTL (copie pour ne pas modifier le buffer original si partagé) */
    uint8_t buf[250];
    if (len > sizeof(buf)) return;
    memcpy(buf, data, len);
    TreeMeshHeader* h2 = (TreeMeshHeader*)buf;
    h2->ttl--;

    if (!esp_now_is_peer_exist(parent_mac)) add_parent_as_peer(parent_mac);
    esp_now_send(parent_mac, buf, len);
}

void routing_route_downstream(uint16_t target_id, const uint8_t* data, size_t len) {
    if (!data || len < sizeof(TreeMeshHeader)) return;
    for (const auto& c : s_children) {
        if (c.nodeId == target_id && c.is_active) {
            if (!esp_now_is_peer_exist(c.mac)) add_parent_as_peer(c.mac);
            esp_now_send(c.mac, data, len);
            return;
        }
    }
    ESP_LOGD(TAG, "route_downstream: cible %04X pas dans enfants directs", target_id);
}

void on_mesh_receive(const uint8_t* mac, const uint8_t* data, int len) {
    if (!mac || !data || len < (int)sizeof(TreeMeshHeader)) return;
    TreeMeshHeader* hdr = (TreeMeshHeader*)data;
    if (hdr->magic != MESH_MAGIC_BYTE) return;

    /* ---------- SCANNING : collecte des Beacons pour évaluation ---------- */
    if (s_state == STATE_SCANNING && hdr->msgType == MSG_BEACON) {
        if (len < (int)(sizeof(TreeMeshHeader) + sizeof(BeaconPayload))) return;
        BeaconPayload* b = (BeaconPayload*)(data + sizeof(TreeMeshHeader));
        /* Loop avoidance : rejeter si layer >= ma couche précédente */
        if (hdr->layer >= s_my_layer_previous) return;
        if (b->currentChildrenCount >= MAX_CHILDREN_PER_NODE) return;
        /* Meilleur candidat : couche la plus basse, puis moins d'enfants */
        if (hdr->layer < s_best_layer || (hdr->layer == s_best_layer && b->currentChildrenCount < s_best_children_count)) {
            s_best_layer = hdr->layer;
            s_best_children_count = b->currentChildrenCount;
            s_best_parent_candidate = hdr->srcNodeId;
            memcpy(s_best_parent_mac, mac, 6);
            led_manager_set_state(LED_STATE_SCANNING);
            ESP_LOGD(TAG, "Beacon recu parent %04X layer=%u enfants=%u", hdr->srcNodeId, (unsigned)hdr->layer, (unsigned)b->currentChildrenCount);
        }
        return;
    }

    /* ---------- JOINING : réception JOIN_ACK avec couche assignée ---------- */
    if (s_state == STATE_JOINING && hdr->msgType == MSG_JOIN_ACK && hdr->srcNodeId == s_best_parent_candidate) {
        uint8_t assigned = hdr->layer + 1; /* Par défaut : parent_layer + 1 */
        if (len >= (int)(sizeof(TreeMeshHeader) + sizeof(JoinAckPayload))) {
            JoinAckPayload* j = (JoinAckPayload*)(data + sizeof(TreeMeshHeader));
            assigned = j->assigned_layer;
        }
        s_state = STATE_CONNECTED;
        s_parent_id = hdr->srcNodeId;
        memcpy(s_parent_mac, mac, 6);
        s_my_layer = assigned;
        s_last_heartbeat_ack = xTaskGetTickCount();
        s_heartbeat_fail_count = 0;
        add_parent_as_peer(mac);
        ESP_LOGI(TAG, "Connecte au parent %04X, couche assignee: %u", s_parent_id, assigned);
        led_manager_set_state(LED_STATE_CONNECTED);
        return;
    }

    /* ---------- CONNECTED / ROOT : JOIN_REQ, HEARTBEAT, HEARTBEAT_ACK ---------- */
    if (s_state == STATE_CONNECTED || s_my_layer == 0) {
        if (hdr->msgType == MSG_JOIN_REQ) {
            if (s_children.size() < MAX_CHILDREN_PER_NODE) {
                update_child(hdr->srcNodeId, mac);
                JoinAckPayload j;
                j.assigned_layer = s_my_layer + 1;
                routing_send_unicast(mac, MSG_JOIN_ACK, (const uint8_t*)&j, sizeof(j));
            }
            return;
        }
        if (hdr->msgType == MSG_HEARTBEAT) {
            update_child(hdr->srcNodeId, mac);
            /* Réponse explicite HEARTBEAT_ACK (CDC) */
            routing_send_unicast(mac, MSG_HEARTBEAT_ACK, nullptr, 0);
            return;
        }
    }

    /* ---------- Enfant : réception HEARTBEAT_ACK du parent ---------- */
    if (s_state == STATE_CONNECTED && s_my_layer != 0 && hdr->msgType == MSG_HEARTBEAT_ACK && hdr->srcNodeId == s_parent_id) {
        s_last_heartbeat_ack = xTaskGetTickCount();
        s_heartbeat_fail_count = 0;
    }
}

/** Intervalle Beacon avec jitter 2–5 s (CDC). */
static uint32_t beacon_interval_ms(void) {
    uint32_t base = ROUTING_BEACON_INTERVAL_MS_MAX - ROUTING_BEACON_INTERVAL_MS_MIN;
    return ROUTING_BEACON_INTERVAL_MS_MIN + (esp_random() % (base + 1));
}

void routing_task(void* pv) {
    (void)pv;
    TickType_t last_beacon = 0;
    TickType_t last_heartbeat = 0;
    uint32_t next_beacon_ms = beacon_interval_ms();

    if (s_my_layer == 0) {
        s_state = STATE_CONNECTED;
        led_manager_set_state(LED_STATE_ROOT);
    } else if (s_state == STATE_INIT) {
        s_state = STATE_SCANNING;
        led_manager_set_state(LED_STATE_SCANNING);
    }

    while (1) {
        routing_check_timeouts();

        if (s_state == STATE_ORPHAN) {
            routing_orphan_recovery();
            continue;
        }

        if (s_state == STATE_SCANNING && s_my_layer != 0) {
            led_manager_set_state(LED_STATE_SCANNING);
            s_best_layer = 255;
            s_best_children_count = 255;
            vTaskDelay(pdMS_TO_TICKS(500));
            /* Évaluation : si on a un candidat, passer en JOINING */
            if (s_best_layer != 255) {
                s_state = STATE_JOINING;
                add_parent_as_peer(s_best_parent_mac);
                routing_send_unicast(s_best_parent_mac, MSG_JOIN_REQ, nullptr, 0);
                ESP_LOGI(TAG, "JOIN_REQ envoye au parent %04X (layer %u)", s_best_parent_candidate, (unsigned)s_best_layer);
            }
            continue;
        }

        if (s_state == STATE_JOINING) {
            ESP_LOGI(TAG, "Etat JOINING (attente JOIN_ACK %u ms)...", (unsigned)ROUTING_JOIN_WAIT_MS);
            vTaskDelay(pdMS_TO_TICKS(ROUTING_JOIN_WAIT_MS));
            if (s_state != STATE_CONNECTED) {
                ESP_LOGW(TAG, "JOIN timeout -> SCANNING");
                s_state = STATE_SCANNING;
            }
            continue;
        }

        if (s_state == STATE_CONNECTED) {
            TickType_t now = xTaskGetTickCount();

            /* Heartbeat vers parent toutes les 10 s (CDC) */
            if (s_my_layer != 0) {
                if ((now - last_heartbeat) >= pdMS_TO_TICKS(ROUTING_HEARTBEAT_INTERVAL_MS)) {
                    routing_send_unicast(s_parent_mac, MSG_HEARTBEAT, nullptr, 0);
                    last_heartbeat = now;
                }
            }

            /* Beacon broadcast 2–5 s avec jitter (CDC) */
            if ((now - last_beacon) >= pdMS_TO_TICKS(next_beacon_ms)) {
                BeaconPayload b;
                b.currentChildrenCount = (uint8_t)s_children.size();
                b.rssi = 0;
                uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                routing_send_unicast(broadcast, MSG_BEACON, (uint8_t*)&b, sizeof(b));
                ESP_LOGD(TAG, "Beacon envoye layer=%u enfants=%u", (unsigned)s_my_layer, (unsigned)s_children.size());
                last_beacon = now;
                next_beacon_ms = beacon_interval_ms();
            }

            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

std::vector<ChildInfo> routing_get_children(void) {
    return s_children;
}

bool routing_get_parent_mac(uint8_t* mac_out) {
    if (!mac_out || s_state != STATE_CONNECTED || s_my_layer == 0) return false;
    memcpy(mac_out, s_parent_mac, 6);
    return true;
}

uint16_t routing_get_my_id(void) {
    return s_my_id;
}

uint8_t routing_get_layer(void) {
    return s_my_layer;
}

uint16_t routing_get_parent_id(void) {
    if (s_state != STATE_CONNECTED || s_my_layer == 0) return 0xFFFF;
    return s_parent_id;
}
