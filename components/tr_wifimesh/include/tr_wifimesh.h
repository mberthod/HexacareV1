/* tr_wifimesh.h — transport ESP-WIFI-MESH (fallback)
 *
 * État : **STUB** (compilation sans dépendance CMake `mesh` séparée).
 * L’API est figée pour `mesh_manager` ; `tr_wifimesh_send` renvoie
 * `ESP_ERR_NOT_SUPPORTED` tant que l’impl réelle n’est pas branchée.
 *
 * Stratégie de failover (documentée pour la suite) :
 *   1. Le primaire reste **ESP-NOW** (`tr_espnow`) sur le même canal WiFi que
 *      le mesh tree Espressif.
 *   2. Quand `failover_policy` bascule vers `MESH_TRANSPORT_WIFIMESH`,
 *      `mesh_manager` appelle `tr_wifimesh_activate` puis route les paquets
 *      via `tr_wifimesh_send`.
 *   3. Impl réelle : inclure `esp_mesh.h` (fourni avec le composant **esp_wifi**
 *      dans ESP-IDF), initialiser `esp_mesh_set_self_organized`, router
 *      `MESH_DATA_TOSERVER` / `MESH_DATA_FROMDS` vers `on_rx`, et mapper
 *      `dst_node_id` sur les adresses mesh internes. Vérifier la cible
 *      (ESP32-S3) et la version IDF : certains paquets PlatformIO n’exposent
 *      pas toutes les options du menuconfig mesh — dans ce cas, garder ce
 *      stub et documenter l’environnement de build officiel (IDF natif).
 *   4. Tant que le stub est actif, le failover WiFi-mesh **ne transporte pas**
 *      de données : seul ESP-NOW est fiable en production.
 *
 * Référence IDF : `examples/network/esp_mesh/internal_communication/` (chemin
 * exact selon version IDF).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tr_wifimesh_ctx_s *tr_wifimesh_ctx_t;

typedef void (*tr_wifimesh_rx_cb_t)(uint16_t src_node_id,
                                    const uint8_t *data,
                                    size_t len,
                                    void *user_ctx);

typedef struct {
    uint16_t node_id;
    uint8_t  wifi_channel;
    bool     is_root;
    tr_wifimesh_rx_cb_t on_rx;
    void    *user_ctx;
} tr_wifimesh_config_t;

esp_err_t tr_wifimesh_init(const tr_wifimesh_config_t *cfg,
                           tr_wifimesh_ctx_t *out);

esp_err_t tr_wifimesh_send(tr_wifimesh_ctx_t ctx, uint16_t dst,
                           const uint8_t *data, size_t len);

/* Active effectivement l'association WIFI-MESH. À appeler quand mesh_manager
 * bascule en fallback. no-op dans le stub. */
esp_err_t tr_wifimesh_activate(tr_wifimesh_ctx_t ctx);

/* Désactive (quitte le mesh) pour laisser le canal à ESP-NOW exclusivement. */
esp_err_t tr_wifimesh_deactivate(tr_wifimesh_ctx_t ctx);

esp_err_t tr_wifimesh_deinit(tr_wifimesh_ctx_t ctx);

#ifdef __cplusplus
}
#endif
