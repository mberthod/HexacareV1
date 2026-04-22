/* tr_espnow.h — transport ESP-NOW avec routing tree
 *
 * Opaque context, API minimaliste utilisée par mesh_manager.
 * L'implémentation gère :
 *   - HELLO broadcast périodique (parent selection)
 *   - Forwarding unicast via parent (up-tree vers root)
 *   - Broadcast avec dedup pour down-tree
 *   - Probing bidirectionnel pour le failover
 *   - Encryption AES-CCMP avec PMK + LMK par peer
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TR_ESPNOW_PMK_LEN    16

typedef struct tr_espnow_ctx_s *tr_espnow_ctx_t;

typedef void (*tr_espnow_rx_cb_t)(uint16_t src_node_id,
                                  const uint8_t *data,
                                  size_t len,
                                  void *user_ctx);

typedef void (*tr_espnow_tx_result_cb_t)(bool delivered, void *user_ctx);

typedef struct {
    uint16_t node_id;                    /* 1..65534 */
    uint8_t  pmk[TR_ESPNOW_PMK_LEN];
    uint8_t  wifi_channel;
    bool     is_root;
    tr_espnow_rx_cb_t        on_rx;
    tr_espnow_tx_result_cb_t on_tx_result;  /* peut être NULL */
    void    *user_ctx;
} tr_espnow_config_t;

/* Init. Suppose esp_wifi_start() déjà appelé avec le bon canal. */
esp_err_t tr_espnow_init(const tr_espnow_config_t *cfg, tr_espnow_ctx_t *out);

/* Envoi unicast ou broadcast (dst = 0xFFFF).
 * - unicast vers un voisin direct : envoi direct
 * - unicast vers non-voisin : forward via parent (si on n'est pas root)
 *                              forward via broadcast filtré (si root)
 * - broadcast : envoi broadcast avec dedup applicatif */
esp_err_t tr_espnow_send(tr_espnow_ctx_t ctx, uint16_t dst,
                         const uint8_t *data, size_t len);

/* Probe bidirectionnel vers `dst`. Retourne ESP_OK si une réponse PROBE_ACK
 * est reçue dans `timeout_ms`. Bloquant. */
esp_err_t tr_espnow_probe(tr_espnow_ctx_t ctx, uint16_t dst, int timeout_ms);

/* Stats cumulées. */
void tr_espnow_get_stats(tr_espnow_ctx_t ctx,
                         uint32_t *tx, uint32_t *rx, uint32_t *lost);

/* Infos topologie actuelles (debug / monitoring). */
uint16_t tr_espnow_get_parent_id(tr_espnow_ctx_t ctx);
uint8_t  tr_espnow_get_hop_count(tr_espnow_ctx_t ctx);
size_t   tr_espnow_get_neighbor_count(tr_espnow_ctx_t ctx);

esp_err_t tr_espnow_deinit(tr_espnow_ctx_t ctx);

#ifdef __cplusplus
}
#endif
