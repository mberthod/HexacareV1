/* espnow_tree.c — logique tree routing
 *
 * Parent selection :
 *   - Si je suis root (cfg.is_root), parent = 0 (pas de parent), hop = 0
 *   - Sinon, je choisis le voisin qui minimise un score :
 *        score = neighbor.hop_count * 10 + rssi_penalty(neighbor.rssi)
 *     où rssi_penalty = max(0, |rssi| - 40) / 5
 *   - Je ne choisis pas un voisin dont parent_id == mon_id (évite boucles).
 *
 * Next hop for dst :
 *   - Si dst est voisin direct → envoi direct
 *   - Sinon → remonte vers mon parent (upstream)
 *   - Si je suis root et dst non voisin → broadcast filtré (TODO : table de routes)
 */
#include <string.h>
#include "tr_espnow_priv.h"
#include "esp_log.h"

static const char *TAG = "espnow_tree";

/* Pénalité RSSI : 0 à -40dBm = 0, -80dBm = 8, etc.
 * Chaque -5 dB = +1 de pénalité. */
static int rssi_penalty(int8_t rssi)
{
    int a = (rssi < 0) ? -rssi : rssi;
    if (a < 40) return 0;
    return (a - 40) / 5;
}

void tree_recompute_parent(struct tr_espnow_ctx_s *c)
{
    if (c->cfg.is_root) {
        c->parent_id = 0;
        c->hop_count = 0;
        c->rssi_to_parent = 0;
        return;
    }

    xSemaphoreTake(c->nb_lock, portMAX_DELAY);

    uint16_t best_id = 0;
    int      best_score = INT32_MAX;
    uint8_t  best_hop = 0xFF;
    int8_t   best_rssi = -127;

    for (size_t i = 0; i < c->nb_count; i++) {
        neighbor_t *n = &c->neighbors[i];

        /* Un voisin sans hop_count (0xFF ou > max_layer) n'est pas un
         * candidat parent (il n'a pas encore de route vers root) */
        if (n->hop_count >= 8) continue;

        /* Anti-boucle : un voisin dont le parent est moi, ne peut pas être
         * mon parent */
        if (n->parent_id == c->cfg.node_id) continue;

        int score = (int)n->hop_count * 10 + rssi_penalty(n->rssi);
        if (score < best_score) {
            best_score = score;
            best_id = n->node_id;
            best_hop = n->hop_count;
            best_rssi = n->rssi;
        }
    }

    xSemaphoreGive(c->nb_lock);

    if (best_id != 0) {
        if (c->parent_id != best_id) {
            ESP_LOGI(TAG, "new parent: 0x%04X (hops=%u, rssi=%d, score=%d)",
                     best_id, best_hop, best_rssi, best_score);
        }
        c->parent_id = best_id;
        c->hop_count = best_hop + 1;
        c->rssi_to_parent = best_rssi;
    } else {
        if (c->parent_id != 0) {
            ESP_LOGW(TAG, "no candidate parent found, orphaned");
        }
        c->parent_id = 0;
        c->hop_count = 0xFF;
        c->rssi_to_parent = 0;
    }
}

uint16_t tree_next_hop_for(struct tr_espnow_ctx_s *c, uint16_t dst)
{
    /* Cas trivial : le dst est un voisin direct */
    xSemaphoreTake(c->nb_lock, portMAX_DELAY);
    if (nb_find(c, dst)) {
        xSemaphoreGive(c->nb_lock);
        return dst;
    }
    xSemaphoreGive(c->nb_lock);

    /* Sinon : upstream via parent (si j'en ai un) */
    if (c->parent_id != 0) return c->parent_id;

    /* Si je suis root et dst inconnu : pas de route directe, l'appelant
     * tombera sur un broadcast filtré (géré dans tr_espnow.c) */
    return 0;
}
