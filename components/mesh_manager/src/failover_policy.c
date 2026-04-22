/* failover_policy.c — politique de bascule, logique pure
 *
 * Isolé de mesh_manager.c pour être testable en hôte sans ESP-IDF.
 * Entrées/sorties explicites, aucun global.
 */
#include <stdint.h>
#include <stdbool.h>
#include "mesh_manager.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "failover";

void failover_policy_tick(mesh_transport_t *current,
                          uint32_t window_tx, uint32_t window_ack,
                          int64_t window_start_us, int64_t now_us,
                          int64_t *last_probe_us,
                          uint8_t *consecutive_ok)
{
    switch (*current) {

    case MESH_TRANSPORT_ESPNOW: {
        /* Évaluer le taux de perte sur la fenêtre actuelle */
        int64_t window_ms = (now_us - window_start_us) / 1000;
        if (window_ms < CONFIG_MESH_LOSS_WINDOW_MS) break;
        if (window_tx < (uint32_t)CONFIG_MESH_LOSS_MIN_SAMPLES) break;

        uint32_t lost = (window_tx > window_ack) ? (window_tx - window_ack) : 0;
        uint32_t loss_pct = (lost * 100) / window_tx;

        if (loss_pct >= (uint32_t)CONFIG_MESH_PRIMARY_LOSS_THRESHOLD_PCT) {
            ESP_LOGW(TAG, "ESP-NOW loss %lu%% >= %d%% on %lu samples, "
                          "switching to WIFI-MESH fallback",
                     loss_pct, CONFIG_MESH_PRIMARY_LOSS_THRESHOLD_PCT,
                     window_tx);
            *current = MESH_TRANSPORT_WIFIMESH;
            *last_probe_us = now_us;
            *consecutive_ok = 0;
        }
        break;
    }

    case MESH_TRANSPORT_WIFIMESH: {
        /* Timer : entrer en PROBING toutes les CONFIG_MESH_PROBE_INTERVAL_MS */
        int64_t since_probe_ms = (now_us - *last_probe_us) / 1000;
        if (since_probe_ms >= CONFIG_MESH_PROBE_INTERVAL_MS) {
            ESP_LOGI(TAG, "probing ESP-NOW recovery");
            *current = MESH_TRANSPORT_PROBING;
            *consecutive_ok = 0;
        }
        break;
    }

    case MESH_TRANSPORT_PROBING: {
        /* consecutive_ok est incrémenté par mesh_manager.c après chaque probe
         * réussi. Ici on décide si on rentre en Primary. */
        if (*consecutive_ok >= (uint32_t)CONFIG_MESH_PROBE_CONSECUTIVE_OK) {
            ESP_LOGI(TAG, "%u consecutive probes OK, restore ESP-NOW Primary",
                     *consecutive_ok);
            *current = MESH_TRANSPORT_ESPNOW;
            *consecutive_ok = 0;
        }
        /* Note : si le probe échoue, mesh_manager.c remet consecutive_ok=0
         * mais on reste en PROBING jusqu'au prochain tick. Pour éviter de
         * pomper ESP-NOW en boucle, on pourrait ajouter un nb max de
         * tentatives avant de retourner en WIFIMESH — TODO amélioration. */
        break;
    }

    case MESH_TRANSPORT_NONE:
    default:
        break;
    }
}
