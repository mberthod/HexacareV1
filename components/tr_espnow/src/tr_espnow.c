/* tr_espnow.c — point d'entrée du transport ESP-NOW
 *
 * Orchestre : init ESP-NOW, tâche HELLO périodique, tâche RX pour traitement
 * asynchrone, logique send avec forward tree, probing bidirectionnel.
 */
#include <string.h>
#include <stdlib.h>
#include "tr_espnow.h"
#include "tr_espnow_priv.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "tr_espnow";

#define HELLO_PERIOD_MS       5000
#define NEIGHBOR_MAX_AGE_MS   20000   /* 4 HELLOs manqués */
#define DEFAULT_TTL           6

/* Broadcast MAC pour ESP-NOW */
static const uint8_t BCAST_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/* Il ne peut y avoir qu'un seul contexte actif à la fois, car les callbacks
 * ESP-NOW sont globaux. On stocke le contexte courant pour que les
 * callbacks puissent le retrouver. */
static struct tr_espnow_ctx_s *s_ctx = NULL;

/* ------------------------------------------------------------------
 * Callbacks ESP-NOW — tournent dans le contexte WiFi, donc interdit de
 * bloquer ou faire des traitements lourds. On pousse vers rx_q et on laisse
 * rx_task traiter.
 * ------------------------------------------------------------------ */

static void on_espnow_recv(const esp_now_recv_info_t *info,
                           const uint8_t *data, int data_len)
{
    if (!s_ctx || data_len <= 0 || data_len > RX_BUF_MAX) return;

    rx_entry_t e;
    memcpy(e.mac, info->src_addr, 6);
    e.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : -127;
    e.len = (size_t)data_len;
    memcpy(e.buf, data, data_len);

    BaseType_t hp_wake = pdFALSE;
    xQueueSendFromISR(s_ctx->rx_q, &e, &hp_wake);
    if (hp_wake) portYIELD_FROM_ISR();
}

static void on_espnow_send(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    (void)mac_addr;
    if (!s_ctx) return;
    bool ok = (status == ESP_NOW_SEND_SUCCESS);
    if (!ok) {
        xSemaphoreTake(s_ctx->stats_lock, portMAX_DELAY);
        s_ctx->stats_lost++;
        xSemaphoreGive(s_ctx->stats_lock);
    }
    if (s_ctx->cfg.on_tx_result) {
        s_ctx->cfg.on_tx_result(ok, s_ctx->cfg.user_ctx);
    }
}

/* ------------------------------------------------------------------
 * Envoi bas niveau (après construction enveloppe)
 * ------------------------------------------------------------------ */

static esp_err_t raw_send(const uint8_t dst_mac[6], const uint8_t *buf,
                          size_t len)
{
    esp_err_t err = esp_now_send(dst_mac, buf, len);
    if (err == ESP_OK) {
        xSemaphoreTake(s_ctx->stats_lock, portMAX_DELAY);
        s_ctx->stats_tx++;
        xSemaphoreGive(s_ctx->stats_lock);
    }
    return err;
}

/* Construit l'enveloppe dans un buffer local et envoie */
static esp_err_t send_framed(tr_espnow_ctx_t c, const uint8_t dst_mac[6],
                             uint8_t type, uint16_t src, uint16_t dst,
                             uint16_t msg_id, uint8_t ttl,
                             const uint8_t *payload, uint8_t payload_len)
{
    uint8_t buf[sizeof(mesh_envelope_t) + 240];
    if (payload_len > 240) return ESP_ERR_INVALID_SIZE;
    mesh_envelope_t *hdr = (mesh_envelope_t *)buf;
    env_build(hdr, type, src, dst, msg_id, ttl, payload_len);
    if (payload_len && payload) {
        memcpy(buf + sizeof(*hdr), payload, payload_len);
    }
    return raw_send(dst_mac, buf, sizeof(*hdr) + payload_len);
}

/* ------------------------------------------------------------------
 * Traitement d'un message reçu (tâche rx)
 * ------------------------------------------------------------------ */

static void handle_hello(struct tr_espnow_ctx_s *c, const rx_entry_t *e,
                         const mesh_envelope_t *hdr, const uint8_t *payload)
{
    if (hdr->payload_len != sizeof(hello_payload_t)) return;
    const hello_payload_t *h = (const hello_payload_t *)payload;

    nb_table_upsert(c, hdr->src_id, e->mac, e->rssi, h->hop_count, h->parent_id);
    tree_recompute_parent(c);
}

static void handle_data(struct tr_espnow_ctx_s *c, const rx_entry_t *e,
                        const mesh_envelope_t *hdr, const uint8_t *payload)
{
    /* Destiné à moi ? */
    if (hdr->dst_id == c->cfg.node_id || hdr->dst_id == TR_MESH_NODE_BROADCAST) {
        /* Dedup : n'appeler le callback qu'une fois par (src, msg_id) */
        if (dedup_check_and_add(c, hdr->src_id, hdr->msg_id)) {
            return;  /* déjà remonté */
        }
        xSemaphoreTake(c->stats_lock, portMAX_DELAY);
        c->stats_rx++;
        xSemaphoreGive(c->stats_lock);
        if (c->cfg.on_rx) {
            c->cfg.on_rx(hdr->src_id, payload, hdr->payload_len, c->cfg.user_ctx);
        }
        /* Si broadcast, on forward aussi (voir plus bas) */
        if (hdr->dst_id != TR_MESH_NODE_BROADCAST) return;
    }

    /* Forward si TTL le permet */
    if (hdr->ttl <= 1) {
        ESP_LOGD(TAG, "TTL expired, drop msg src=0x%04X dst=0x%04X",
                 hdr->src_id, hdr->dst_id);
        return;
    }

    /* Dedup forward : ne jamais ré-émettre un broadcast déjà relayé */
    if (hdr->dst_id == TR_MESH_NODE_BROADCAST) {
        if (dedup_check_and_add(c, hdr->src_id, hdr->msg_id)) return;
        /* Relai broadcast */
        uint8_t new_ttl = hdr->ttl - 1;
        send_framed(c, BCAST_MAC, hdr->type, hdr->src_id, hdr->dst_id,
                    hdr->msg_id, new_ttl, payload, hdr->payload_len);
        xSemaphoreTake(c->stats_lock, portMAX_DELAY);
        c->stats_forwarded++;
        xSemaphoreGive(c->stats_lock);
        return;
    }

    /* Unicast : next hop via tree */
    uint16_t next = tree_next_hop_for(c, hdr->dst_id);
    if (next == 0) {
        ESP_LOGD(TAG, "no route to 0x%04X, drop", hdr->dst_id);
        return;
    }

    xSemaphoreTake(c->nb_lock, portMAX_DELAY);
    neighbor_t *n = nb_find(c, next);
    if (!n) {
        xSemaphoreGive(c->nb_lock);
        return;
    }
    uint8_t target_mac[6];
    memcpy(target_mac, n->mac, 6);
    xSemaphoreGive(c->nb_lock);

    uint8_t new_ttl = hdr->ttl - 1;
    send_framed(c, target_mac, hdr->type, hdr->src_id, hdr->dst_id,
                hdr->msg_id, new_ttl, payload, hdr->payload_len);
    xSemaphoreTake(c->stats_lock, portMAX_DELAY);
    c->stats_forwarded++;
    xSemaphoreGive(c->stats_lock);
}

static void handle_probe(struct tr_espnow_ctx_s *c, const rx_entry_t *e,
                         const mesh_envelope_t *hdr, const uint8_t *payload)
{
    if (hdr->payload_len != sizeof(probe_payload_t)) return;
    const probe_payload_t *p = (const probe_payload_t *)payload;
    /* Renvoyer un PROBE_ACK à l'émetteur (unicast direct) */
    send_framed(c, e->mac, MSG_TYPE_PROBE_ACK,
                c->cfg.node_id, hdr->src_id,
                hdr->msg_id, 1, (const uint8_t *)p, sizeof(*p));
}

static void handle_probe_ack(struct tr_espnow_ctx_s *c,
                             const mesh_envelope_t *hdr,
                             const uint8_t *payload)
{
    if (hdr->payload_len != sizeof(probe_payload_t)) return;
    const probe_payload_t *p = (const probe_payload_t *)payload;
    if (p->nonce == c->probe_nonce) {
        c->probe_rtt_us = esp_timer_get_time() - p->sent_us;
        xSemaphoreGive(c->probe_sem);
    }
}

static void task_rx(void *arg)
{
    struct tr_espnow_ctx_s *c = (struct tr_espnow_ctx_s *)arg;
    rx_entry_t e;
    for (;;) {
        if (xQueueReceive(c->rx_q, &e, portMAX_DELAY) != pdPASS) continue;

        const mesh_envelope_t *hdr = NULL;
        const uint8_t *payload = NULL;
        if (!env_parse(e.buf, e.len, &hdr, &payload)) continue;

        switch (hdr->type) {
        case MSG_TYPE_HELLO:
            handle_hello(c, &e, hdr, payload);
            break;
        case MSG_TYPE_DATA:
        case MSG_TYPE_DATA_BCAST:
            handle_data(c, &e, hdr, payload);
            break;
        case MSG_TYPE_PROBE:
            handle_probe(c, &e, hdr, payload);
            break;
        case MSG_TYPE_PROBE_ACK:
            handle_probe_ack(c, hdr, payload);
            break;
        default:
            ESP_LOGD(TAG, "unknown msg type 0x%02X", hdr->type);
            break;
        }
    }
}

/* ------------------------------------------------------------------
 * Tâche HELLO — broadcast périodique + expire voisins morts
 * ------------------------------------------------------------------ */

static void task_hello(void *arg)
{
    struct tr_espnow_ctx_s *c = (struct tr_espnow_ctx_s *)arg;
    const TickType_t period = pdMS_TO_TICKS(HELLO_PERIOD_MS);
    TickType_t next = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&next, period);

        /* Expirer les voisins muets */
        nb_table_expire(c, esp_timer_get_time(), NEIGHBOR_MAX_AGE_MS);

        /* Recomputer parent au cas où on aurait perdu le précédent */
        tree_recompute_parent(c);

        /* Construire et émettre HELLO */
        hello_payload_t h = {
            .parent_id      = c->parent_id,
            .hop_count      = c->hop_count,
            .rssi_to_parent = c->rssi_to_parent,
            .node_role      = c->cfg.is_root ? 2 : (c->parent_id ? 1 : 0),
        };
        send_framed(c, BCAST_MAC, MSG_TYPE_HELLO,
                    c->cfg.node_id, TR_MESH_NODE_BROADCAST,
                    c->next_msg_id++, 1,
                    (const uint8_t *)&h, sizeof(h));

        ESP_LOGD(TAG, "HELLO parent=0x%04X hops=%u rssi=%d neighbors=%zu",
                 c->parent_id, c->hop_count, c->rssi_to_parent, c->nb_count);
    }
}

/* ------------------------------------------------------------------
 * API publique
 * ------------------------------------------------------------------ */

esp_err_t tr_espnow_init(const tr_espnow_config_t *cfg, tr_espnow_ctx_t *out)
{
    if (!cfg || !out) return ESP_ERR_INVALID_ARG;
    if (s_ctx) return ESP_ERR_INVALID_STATE;

    struct tr_espnow_ctx_s *c = calloc(1, sizeof(*c));
    if (!c) return ESP_ERR_NO_MEM;
    memcpy(&c->cfg, cfg, sizeof(*cfg));

    c->nb_lock    = xSemaphoreCreateMutex();
    c->stats_lock = xSemaphoreCreateMutex();
    c->probe_sem  = xSemaphoreCreateBinary();
    c->rx_q       = xQueueCreate(16, sizeof(rx_entry_t));
    configASSERT(c->nb_lock && c->stats_lock && c->probe_sem && c->rx_q);

    nb_table_init(c);

    if (cfg->is_root) {
        c->parent_id = 0;
        c->hop_count = 0;
    } else {
        c->parent_id = 0;
        c->hop_count = 0xFF;
    }

    /* Init ESP-NOW */
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_set_pmk(cfg->pmk));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_espnow_recv));
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_espnow_send));

    /* Ajouter le broadcast comme peer (non chiffré) */
    esp_now_peer_info_t bpeer = {
        .channel = cfg->wifi_channel,
        .ifidx   = WIFI_IF_STA,
        .encrypt = false,
    };
    memcpy(bpeer.peer_addr, BCAST_MAC, 6);
    esp_err_t err = esp_now_add_peer(&bpeer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "add bcast peer failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ctx = c;
    *out  = c;

    BaseType_t ok;
    ok = xTaskCreatePinnedToCore(task_rx, "espnow_rx", 4096, c, 5,
                                 &c->rx_task, 0);
    configASSERT(ok == pdPASS);
    ok = xTaskCreatePinnedToCore(task_hello, "espnow_hello", 3072, c, 3,
                                 &c->hello_task, 0);
    configASSERT(ok == pdPASS);

    ESP_LOGI(TAG, "init done, node=0x%04X is_root=%d", cfg->node_id,
             cfg->is_root);
    return ESP_OK;
}

esp_err_t tr_espnow_send(tr_espnow_ctx_t c, uint16_t dst,
                         const uint8_t *data, size_t len)
{
    if (!c || !data || len == 0 || len > 240) return ESP_ERR_INVALID_ARG;

    uint16_t msg_id = c->next_msg_id++;

    /* Broadcast : envoi direct en BCAST + dedup nous-mêmes pour ne pas
     * répéter le message à nous-mêmes au forward */
    if (dst == TR_MESH_NODE_BROADCAST) {
        dedup_check_and_add(c, c->cfg.node_id, msg_id);
        return send_framed(c, BCAST_MAC, MSG_TYPE_DATA_BCAST,
                           c->cfg.node_id, dst, msg_id, DEFAULT_TTL,
                           data, (uint8_t)len);
    }

    /* Unicast : trouver le next hop */
    uint16_t next = tree_next_hop_for(c, dst);
    if (next == 0) {
        /* Aucune route — fallback broadcast avec filtrage par dst.
         * Efficace seulement sur petit réseau. */
        dedup_check_and_add(c, c->cfg.node_id, msg_id);
        return send_framed(c, BCAST_MAC, MSG_TYPE_DATA,
                           c->cfg.node_id, dst, msg_id, DEFAULT_TTL,
                           data, (uint8_t)len);
    }

    xSemaphoreTake(c->nb_lock, portMAX_DELAY);
    neighbor_t *n = nb_find(c, next);
    if (!n) {
        xSemaphoreGive(c->nb_lock);
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t mac[6];
    memcpy(mac, n->mac, 6);
    xSemaphoreGive(c->nb_lock);

    return send_framed(c, mac, MSG_TYPE_DATA,
                       c->cfg.node_id, dst, msg_id, DEFAULT_TTL,
                       data, (uint8_t)len);
}

esp_err_t tr_espnow_probe(tr_espnow_ctx_t c, uint16_t dst, int timeout_ms)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    /* Vider la sem au cas où un PROBE_ACK résiduel traîne */
    xSemaphoreTake(c->probe_sem, 0);

    probe_payload_t p = {
        .nonce   = esp_random(),
        .sent_us = esp_timer_get_time(),
    };
    c->probe_nonce = p.nonce;
    c->probe_sent_us = p.sent_us;

    /* Envoi unicast vers dst (via tree si besoin) */
    uint16_t next = tree_next_hop_for(c, dst);
    uint8_t mac[6];
    if (next == 0) {
        memcpy(mac, BCAST_MAC, 6);  /* fallback bcast */
    } else {
        xSemaphoreTake(c->nb_lock, portMAX_DELAY);
        neighbor_t *n = nb_find(c, next);
        if (!n) {
            xSemaphoreGive(c->nb_lock);
            return ESP_ERR_NOT_FOUND;
        }
        memcpy(mac, n->mac, 6);
        xSemaphoreGive(c->nb_lock);
    }

    esp_err_t err = send_framed(c, mac, MSG_TYPE_PROBE,
                                c->cfg.node_id, dst,
                                c->next_msg_id++, DEFAULT_TTL,
                                (const uint8_t *)&p, sizeof(p));
    if (err != ESP_OK) return err;

    /* Attendre le ACK */
    if (xSemaphoreTake(c->probe_sem, pdMS_TO_TICKS(timeout_ms)) == pdPASS) {
        ESP_LOGI(TAG, "probe 0x%04X OK, rtt=%lld us", dst, c->probe_rtt_us);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "probe 0x%04X timeout", dst);
    return ESP_ERR_TIMEOUT;
}

void tr_espnow_get_stats(tr_espnow_ctx_t c, uint32_t *tx, uint32_t *rx,
                         uint32_t *lost)
{
    if (!c) return;
    xSemaphoreTake(c->stats_lock, portMAX_DELAY);
    if (tx)   *tx = c->stats_tx;
    if (rx)   *rx = c->stats_rx;
    if (lost) *lost = c->stats_lost;
    xSemaphoreGive(c->stats_lock);
}

uint16_t tr_espnow_get_parent_id(tr_espnow_ctx_t c) { return c ? c->parent_id : 0; }
uint8_t  tr_espnow_get_hop_count(tr_espnow_ctx_t c) { return c ? c->hop_count : 0xFF; }
size_t   tr_espnow_get_neighbor_count(tr_espnow_ctx_t c) { return c ? c->nb_count : 0; }

esp_err_t tr_espnow_deinit(tr_espnow_ctx_t c)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    if (c->hello_task) { vTaskDelete(c->hello_task); c->hello_task = NULL; }
    if (c->rx_task)    { vTaskDelete(c->rx_task);    c->rx_task = NULL; }
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    if (c->rx_q)       vQueueDelete(c->rx_q);
    if (c->nb_lock)    vSemaphoreDelete(c->nb_lock);
    if (c->stats_lock) vSemaphoreDelete(c->stats_lock);
    if (c->probe_sem)  vSemaphoreDelete(c->probe_sem);
    if (s_ctx == c) s_ctx = NULL;
    free(c);
    return ESP_OK;
}
