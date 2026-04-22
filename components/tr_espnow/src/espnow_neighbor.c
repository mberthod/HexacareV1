/* espnow_neighbor.c — table de voisins + dedup broadcast
 *
 * La table de voisins est de taille fixe (MAX_NEIGHBORS=20, limite ESP-NOW).
 * Eviction LRU si pleine. Accès sous nb_lock.
 */
#include <string.h>
#include "tr_espnow_priv.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"

static const char *TAG = "espnow_nb";

void nb_table_init(struct tr_espnow_ctx_s *c)
{
    memset(c->neighbors, 0, sizeof(c->neighbors));
    c->nb_count = 0;
    c->dedup_idx = 0;
    memset(c->dedup, 0, sizeof(c->dedup));
}

neighbor_t *nb_find(struct tr_espnow_ctx_s *c, uint16_t node_id)
{
    for (size_t i = 0; i < c->nb_count; i++) {
        if (c->neighbors[i].node_id == node_id) return &c->neighbors[i];
    }
    return NULL;
}

neighbor_t *nb_find_by_mac(struct tr_espnow_ctx_s *c, const uint8_t mac[6])
{
    for (size_t i = 0; i < c->nb_count; i++) {
        if (memcmp(c->neighbors[i].mac, mac, 6) == 0) return &c->neighbors[i];
    }
    return NULL;
}

/* Recherche du voisin à évincer (LRU) */
static size_t nb_find_oldest(struct tr_espnow_ctx_s *c)
{
    size_t idx = 0;
    int64_t oldest = c->neighbors[0].last_seen_us;
    for (size_t i = 1; i < c->nb_count; i++) {
        if (c->neighbors[i].last_seen_us < oldest) {
            oldest = c->neighbors[i].last_seen_us;
            idx = i;
        }
    }
    return idx;
}

void nb_table_upsert(struct tr_espnow_ctx_s *c, uint16_t node_id,
                     const uint8_t mac[6], int8_t rssi, uint8_t hop_count,
                     uint16_t parent_id)
{
    xSemaphoreTake(c->nb_lock, portMAX_DELAY);
    int64_t now = esp_timer_get_time();

    neighbor_t *n = nb_find(c, node_id);
    if (n) {
        memcpy(n->mac, mac, 6);
        n->rssi         = rssi;
        n->hop_count    = hop_count;
        n->parent_id    = parent_id;
        n->last_seen_us = now;
        xSemaphoreGive(c->nb_lock);
        return;
    }

    /* Nouveau voisin */
    size_t slot;
    if (c->nb_count < MAX_NEIGHBORS) {
        slot = c->nb_count++;
    } else {
        slot = nb_find_oldest(c);
        ESP_LOGW(TAG, "neighbor table full, evicting node 0x%04X",
                 c->neighbors[slot].node_id);
        /* Retirer le peer ESP-NOW aussi */
        if (c->neighbors[slot].is_peer_added) {
            esp_now_del_peer(c->neighbors[slot].mac);
            c->neighbors[slot].is_peer_added = false;
        }
    }

    c->neighbors[slot] = (neighbor_t){
        .node_id       = node_id,
        .rssi          = rssi,
        .hop_count     = hop_count,
        .parent_id     = parent_id,
        .last_seen_us  = now,
        .is_peer_added = false,
    };
    memcpy(c->neighbors[slot].mac, mac, 6);

    /* Ajouter comme peer ESP-NOW pour pouvoir unicast vers lui */
    esp_now_peer_info_t peer = {
        .channel   = c->cfg.wifi_channel,
        .ifidx     = WIFI_IF_STA,
        .encrypt   = true,
    };
    memcpy(peer.peer_addr, mac, 6);
    memcpy(peer.lmk, c->cfg.pmk, 16);  /* pour simplicité, LMK = PMK ici */
    esp_err_t err = esp_now_add_peer(&peer);
    if (err == ESP_OK || err == ESP_ERR_ESPNOW_EXIST) {
        c->neighbors[slot].is_peer_added = true;
    } else {
        ESP_LOGW(TAG, "esp_now_add_peer failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "neighbor add: 0x%04X mac=%02X:%02X:%02X:%02X:%02X:%02X "
                  "rssi=%d hops=%u",
             node_id, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             rssi, hop_count);

    xSemaphoreGive(c->nb_lock);
}

void nb_table_expire(struct tr_espnow_ctx_s *c, int64_t now_us,
                     int64_t max_age_ms)
{
    xSemaphoreTake(c->nb_lock, portMAX_DELAY);
    for (size_t i = 0; i < c->nb_count; /* i incrémenté ci-dessous */) {
        int64_t age_ms = (now_us - c->neighbors[i].last_seen_us) / 1000;
        if (age_ms > max_age_ms) {
            ESP_LOGW(TAG, "expire neighbor 0x%04X (silent %lldms)",
                     c->neighbors[i].node_id, age_ms);
            if (c->neighbors[i].is_peer_added) {
                esp_now_del_peer(c->neighbors[i].mac);
            }
            /* Compact : remplacer par la dernière entrée */
            c->neighbors[i] = c->neighbors[--c->nb_count];
        } else {
            i++;
        }
    }
    xSemaphoreGive(c->nb_lock);
}

/* ------------------------------------------------------------------
 * Dedup ring buffer
 * ------------------------------------------------------------------ */
bool dedup_check_and_add(struct tr_espnow_ctx_s *c,
                         uint16_t src_id, uint16_t msg_id)
{
    for (size_t i = 0; i < DEDUP_TABLE_SIZE; i++) {
        if (c->dedup[i].src_id == src_id && c->dedup[i].msg_id == msg_id) {
            return true;   /* déjà vu */
        }
    }
    /* Pas vu — ajouter */
    c->dedup[c->dedup_idx] = (dedup_entry_t){
        .src_id = src_id,
        .msg_id = msg_id,
        .seen_us = esp_timer_get_time(),
    };
    c->dedup_idx = (c->dedup_idx + 1) % DEDUP_TABLE_SIZE;
    return false;
}
