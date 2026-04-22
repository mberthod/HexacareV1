/* mesh_manager.c — état + dispatcher + stats
 *
 * Architecture :
 *   - Task `mgr` pinned Core 0 (cohabitation WiFi), priority 4, stack 4 KB
 *   - Tick 500 ms pour la FSM de failover
 *   - Queue TX commune (les deux transports pullent dedans)
 *   - RX callback unifié vers l'app
 *
 * Limitations du prototype :
 *   - Une seule queue TX (pas de PQ par priorité) — à faire si congestion
 *   - mesh_send est non-bloquant, mais peut dropper les MESH_PRIO_DATA si
 *     queue pleine
 */
#include <string.h>
#include "mesh_manager.h"
#include "tr_espnow.h"
#include "tr_wifimesh.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "mesh_mgr";

/* ------------------------------------------------------------------
 * Déclarations internes
 * ------------------------------------------------------------------ */

/* Défini dans failover_policy.c */
extern void failover_policy_tick(mesh_transport_t *current,
                                 uint32_t window_tx, uint32_t window_ack,
                                 int64_t window_start_us, int64_t now_us,
                                 int64_t *last_probe_us,
                                 uint8_t *consecutive_ok);

/* Entrée de la queue TX (copie de la payload pour ownership) */
typedef struct {
    uint16_t        dst_node_id;
    mesh_priority_t prio;
    uint8_t         flags;
    size_t          len;
    uint8_t         data[MESH_MAX_PAYLOAD];
} tx_entry_t;

typedef struct {
    mesh_manager_config_t cfg;
    tr_espnow_ctx_t       espnow;
    tr_wifimesh_ctx_t     wifimesh;
    mesh_transport_t      current;
    mesh_rx_cb_t          user_rx_cb;
    QueueHandle_t         tx_q;
    SemaphoreHandle_t     stats_lock;

    /* Fenêtre glissante pour mesure de perte ESP-NOW */
    uint32_t window_tx;
    uint32_t window_ack;
    int64_t  window_start_us;
    int64_t  last_probe_us;
    uint8_t  consecutive_ok;

    /* Stats cumulées */
    uint32_t stats_tx;
    uint32_t stats_rx;
    uint32_t stats_lost;

    TaskHandle_t task;
} mesh_mgr_t;

static mesh_mgr_t s_mgr = {0};

/* ------------------------------------------------------------------
 * Callbacks depuis les transports
 * ------------------------------------------------------------------ */

static void espnow_on_rx(uint16_t src, const uint8_t *data, size_t len,
                         void *user)
{
    (void)user;
    xSemaphoreTake(s_mgr.stats_lock, portMAX_DELAY);
    s_mgr.stats_rx++;
    xSemaphoreGive(s_mgr.stats_lock);

    if (s_mgr.user_rx_cb) {
        s_mgr.user_rx_cb(src, data, len, MESH_TRANSPORT_ESPNOW);
    }
}

static void wifimesh_on_rx(uint16_t src, const uint8_t *data, size_t len,
                           void *user)
{
    (void)user;
    xSemaphoreTake(s_mgr.stats_lock, portMAX_DELAY);
    s_mgr.stats_rx++;
    xSemaphoreGive(s_mgr.stats_lock);

    if (s_mgr.user_rx_cb) {
        s_mgr.user_rx_cb(src, data, len, MESH_TRANSPORT_WIFIMESH);
    }
}

/* Callback appelé par tr_espnow quand un ACK arrive (ou timeout — lost=1) */
static void espnow_on_tx_result(bool delivered, void *user)
{
    (void)user;
    if (delivered) {
        s_mgr.window_ack++;
    } else {
        xSemaphoreTake(s_mgr.stats_lock, portMAX_DELAY);
        s_mgr.stats_lost++;
        xSemaphoreGive(s_mgr.stats_lock);
    }
}

/* ------------------------------------------------------------------
 * Dispatcher — envoie un message via le transport actif
 * ------------------------------------------------------------------ */

static esp_err_t dispatch_one(const tx_entry_t *e)
{
    /* BULK priority = force WIFI-MESH (meilleur throughput) */
    mesh_transport_t use = s_mgr.current;
    if (e->prio == MESH_PRIO_BULK && use != MESH_TRANSPORT_WIFIMESH) {
        use = MESH_TRANSPORT_WIFIMESH;
    }

    esp_err_t err = ESP_FAIL;
    switch (use) {
    case MESH_TRANSPORT_ESPNOW:
    case MESH_TRANSPORT_PROBING:
        /* En mode PROBING on continue à utiliser ESP-NOW pour le trafic,
         * le probe est géré en parallèle par la tâche manager */
        err = tr_espnow_send(s_mgr.espnow, e->dst_node_id, e->data, e->len);
        if (err == ESP_OK) {
            s_mgr.window_tx++;
            xSemaphoreTake(s_mgr.stats_lock, portMAX_DELAY);
            s_mgr.stats_tx++;
            xSemaphoreGive(s_mgr.stats_lock);
        }
        break;
    case MESH_TRANSPORT_WIFIMESH:
        err = tr_wifimesh_send(s_mgr.wifimesh, e->dst_node_id, e->data, e->len);
        if (err == ESP_OK) {
            xSemaphoreTake(s_mgr.stats_lock, portMAX_DELAY);
            s_mgr.stats_tx++;
            xSemaphoreGive(s_mgr.stats_lock);
        }
        break;
    default:
        break;
    }
    return err;
}

/* ------------------------------------------------------------------
 * Tâche manager — pompe TX + FSM
 * ------------------------------------------------------------------ */

static void task_mgr(void *arg)
{
    (void)arg;
    const TickType_t fsm_period = pdMS_TO_TICKS(500);
    TickType_t next_fsm = xTaskGetTickCount() + fsm_period;

    s_mgr.window_start_us = esp_timer_get_time();

    for (;;) {
        /* Attendre un message TX (avec timeout = période FSM) */
        tx_entry_t e;
        TickType_t wait = next_fsm - xTaskGetTickCount();
        if ((int32_t)wait < 0) wait = 0;

        if (xQueueReceive(s_mgr.tx_q, &e, wait) == pdPASS) {
            dispatch_one(&e);
        }

        /* FSM tick si on a atteint la période */
        if ((int32_t)(xTaskGetTickCount() - next_fsm) >= 0) {
            mesh_transport_t prev = s_mgr.current;
            failover_policy_tick(&s_mgr.current,
                                 s_mgr.window_tx, s_mgr.window_ack,
                                 s_mgr.window_start_us,
                                 esp_timer_get_time(),
                                 &s_mgr.last_probe_us,
                                 &s_mgr.consecutive_ok);
            if (prev != s_mgr.current) {
                ESP_LOGW(TAG, "transport %d -> %d", prev, s_mgr.current);
                /* Brancher/débrancher WIFI-MESH selon l'état cible.
                 * ESP-NOW reste toujours initialisé (coût négligeable). */
                if (s_mgr.current == MESH_TRANSPORT_WIFIMESH
                    && prev != MESH_TRANSPORT_WIFIMESH) {
                    esp_err_t err = tr_wifimesh_activate(s_mgr.wifimesh);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "wifimesh activate failed: %s",
                                 esp_err_to_name(err));
                        /* Revenir en ESP-NOW — fallback indisponible */
                        s_mgr.current = MESH_TRANSPORT_ESPNOW;
                    }
                }
                if (prev == MESH_TRANSPORT_WIFIMESH
                    && s_mgr.current != MESH_TRANSPORT_WIFIMESH) {
                    tr_wifimesh_deactivate(s_mgr.wifimesh);
                }
            }

            /* Reset de la fenêtre toutes les LOSS_WINDOW_MS */
            int64_t now = esp_timer_get_time();
            if ((now - s_mgr.window_start_us) / 1000
                >= CONFIG_MESH_LOSS_WINDOW_MS) {
                s_mgr.window_tx = 0;
                s_mgr.window_ack = 0;
                s_mgr.window_start_us = now;
            }

            /* Probe actif si en PROBING */
            if (s_mgr.current == MESH_TRANSPORT_PROBING) {
                /* On utilise le root comme peer de probe par défaut —
                 * en prod, cibler le parent qu'on avait avant le fallback */
                if (tr_espnow_probe(s_mgr.espnow, MESH_NODE_ROOT, 500) == ESP_OK) {
                    s_mgr.consecutive_ok++;
                } else {
                    s_mgr.consecutive_ok = 0;
                }
            }

            next_fsm += fsm_period;
        }
    }
}

/* ------------------------------------------------------------------
 * API publique
 * ------------------------------------------------------------------ */

static esp_err_t init_wifi_basic(uint8_t channel)
{
    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));  /* critique pour ESP-NOW */
    ESP_LOGI(TAG, "wifi ready, channel=%u, ps=NONE", channel);
    return ESP_OK;
}

esp_err_t mesh_manager_init(const mesh_manager_config_t *cfg)
{
    if (!cfg || cfg->node_id == MESH_NODE_INVALID || cfg->node_id == MESH_NODE_BROADCAST) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(&s_mgr.cfg, cfg, sizeof(*cfg));
    s_mgr.current = MESH_TRANSPORT_ESPNOW;
    s_mgr.stats_lock = xSemaphoreCreateMutex();
    s_mgr.tx_q = xQueueCreate(CONFIG_MESH_TX_QUEUE_DEPTH, sizeof(tx_entry_t));
    configASSERT(s_mgr.stats_lock && s_mgr.tx_q);

    ESP_ERROR_CHECK(init_wifi_basic(cfg->wifi_channel));

    tr_espnow_config_t ec = {
        .node_id      = cfg->node_id,
        .wifi_channel = cfg->wifi_channel,
        .is_root      = cfg->is_root,
        .on_rx        = espnow_on_rx,
        .on_tx_result = espnow_on_tx_result,
        .user_ctx     = NULL,
    };
    memcpy(ec.pmk, cfg->primary_pmk, MESH_PMK_LEN);
    ESP_ERROR_CHECK(tr_espnow_init(&ec, &s_mgr.espnow));

    /* WIFI-MESH init en mode "standby" — le SDK Espressif reste prêt mais
     * n'associe pas tant qu'on ne bascule pas */
    tr_wifimesh_config_t wc = {
        .node_id      = cfg->node_id,
        .wifi_channel = cfg->wifi_channel,
        .is_root      = cfg->is_root,
        .on_rx        = wifimesh_on_rx,
        .user_ctx     = NULL,
    };
    ESP_ERROR_CHECK(tr_wifimesh_init(&wc, &s_mgr.wifimesh));

    BaseType_t ok = xTaskCreatePinnedToCore(
        task_mgr, "mesh_mgr", 4096, NULL, 4, &s_mgr.task, 0);
    configASSERT(ok == pdPASS);

    ESP_LOGI(TAG, "init done, node_id=0x%04X, is_root=%d, channel=%u",
             cfg->node_id, cfg->is_root, cfg->wifi_channel);
    return ESP_OK;
}

esp_err_t mesh_send(const mesh_msg_t *msg)
{
    if (!msg || !msg->payload || msg->len == 0 || msg->len > MESH_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }
    tx_entry_t e = {
        .dst_node_id = msg->dst_node_id,
        .prio        = msg->prio,
        .flags       = msg->flags,
        .len         = msg->len,
    };
    memcpy(e.data, msg->payload, msg->len);

    /* CONTROL : bloquant court (10 ms). DATA / BULK : non-bloquant strict. */
    TickType_t to = (msg->prio == MESH_PRIO_CONTROL) ? pdMS_TO_TICKS(10) : 0;
    if (xQueueSend(s_mgr.tx_q, &e, to) != pdPASS) {
        ESP_LOGW(TAG, "tx queue full (prio=%d), dropping", msg->prio);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t mesh_recv_register(mesh_rx_cb_t cb)
{
    s_mgr.user_rx_cb = cb;
    return ESP_OK;
}

mesh_transport_t mesh_manager_status(void)
{
    return s_mgr.current;
}

void mesh_manager_get_stats(uint32_t *tx, uint32_t *rx, uint32_t *lost)
{
    xSemaphoreTake(s_mgr.stats_lock, portMAX_DELAY);
    if (tx)   *tx   = s_mgr.stats_tx;
    if (rx)   *rx   = s_mgr.stats_rx;
    if (lost) *lost = s_mgr.stats_lost;
    xSemaphoreGive(s_mgr.stats_lock);
}

uint16_t mesh_manager_get_parent(void)
{
    return s_mgr.espnow ? tr_espnow_get_parent_id(s_mgr.espnow) : 0;
}

uint8_t mesh_manager_get_hops(void)
{
    return s_mgr.espnow ? tr_espnow_get_hop_count(s_mgr.espnow) : 0xFF;
}

size_t mesh_manager_get_neighbor_count(void)
{
    return s_mgr.espnow ? tr_espnow_get_neighbor_count(s_mgr.espnow) : 0;
}

esp_err_t mesh_manager_deinit(void)
{
    if (s_mgr.task) {
        vTaskDelete(s_mgr.task);
        s_mgr.task = NULL;
    }
    if (s_mgr.espnow)   tr_espnow_deinit(s_mgr.espnow);
    if (s_mgr.wifimesh) tr_wifimesh_deinit(s_mgr.wifimesh);
    if (s_mgr.tx_q)     vQueueDelete(s_mgr.tx_q);
    if (s_mgr.stats_lock) vSemaphoreDelete(s_mgr.stats_lock);
    memset(&s_mgr, 0, sizeof(s_mgr));
    return ESP_OK;
}
