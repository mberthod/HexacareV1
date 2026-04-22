/* mesh_manager.h — API unifiée mesh dual-stack
 *
 * Expose une interface unique `mesh_send` / `mesh_recv` qui masque à
 * l'application le transport actif (ESP-NOW mesh primaire, ESP-WIFI-MESH
 * fallback). La bascule est gérée par une machine à état interne selon la
 * politique définie dans `failover_policy.c`.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * Constantes
 * ------------------------------------------------------------------ */
#define MESH_NODE_BROADCAST   0xFFFFu
#define MESH_NODE_ROOT        0x0001u
#define MESH_NODE_INVALID     0x0000u

#define MESH_PMK_LEN          16
#define MESH_MAX_PAYLOAD      200  /* réserve 50 bytes pour l'enveloppe */

/* ------------------------------------------------------------------
 * Types
 * ------------------------------------------------------------------ */

typedef enum {
    MESH_TRANSPORT_NONE     = 0,
    MESH_TRANSPORT_ESPNOW   = 1,   /* primaire — longue portée */
    MESH_TRANSPORT_WIFIMESH = 2,   /* fallback — throughput */
    MESH_TRANSPORT_PROBING  = 3,   /* test retour Primary */
} mesh_transport_t;

typedef enum {
    MESH_PRIO_CONTROL = 0,   /* heartbeat, alert — jamais droppé */
    MESH_PRIO_DATA    = 1,   /* telemetry — droppable si saturation */
    MESH_PRIO_BULK    = 2,   /* OTA, logs bulk — force WIFI-MESH */
} mesh_priority_t;

#define MESH_MSG_FLAG_ACK_REQ    (1u << 0)
#define MESH_MSG_FLAG_ENCRYPTED  (1u << 1)

typedef struct {
    uint16_t        dst_node_id;   /* MESH_NODE_BROADCAST pour flood */
    const uint8_t  *payload;
    size_t          len;           /* ≤ MESH_MAX_PAYLOAD */
    mesh_priority_t prio;
    uint8_t         flags;
} mesh_msg_t;

typedef void (*mesh_rx_cb_t)(uint16_t src_node_id,
                             const uint8_t *data,
                             size_t len,
                             mesh_transport_t via);

typedef struct {
    uint16_t node_id;                 /* 1..65534, unique dans le réseau */
    uint8_t  primary_pmk[MESH_PMK_LEN];
    uint8_t  wifi_channel;            /* 1..13 */
    bool     is_root;                 /* exactement 1 node par réseau */
} mesh_manager_config_t;

/* ------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------ */

/* Initialise le manager. Démarre WiFi en mode STA+AP sur le canal demandé,
 * initialise ESP-NOW, prépare WIFI-MESH en standby. L'état initial est
 * MESH_TRANSPORT_ESPNOW (Primary). */
esp_err_t mesh_manager_init(const mesh_manager_config_t *cfg);

/* Enqueue un message pour envoi. Retour immédiat (non-bloquant).
 * Le dispatcher interne route selon le transport actif et la priorité.
 * BULK priority force une bascule temporaire WIFI-MESH si pas déjà actif. */
esp_err_t mesh_send(const mesh_msg_t *msg);

/* Enregistre le callback de réception. Un seul callback à la fois —
 * écrase l'ancien. Le callback est appelé depuis la tâche manager,
 * ne pas bloquer dedans. */
esp_err_t mesh_recv_register(mesh_rx_cb_t cb);

/* Transport actuellement actif. */
mesh_transport_t mesh_manager_status(void);

/* Stats cumulées depuis le boot. */
void mesh_manager_get_stats(uint32_t *tx, uint32_t *rx, uint32_t *lost);

/* Topologie actuelle (pour monitoring / test bench). */
uint16_t mesh_manager_get_parent(void);
uint8_t  mesh_manager_get_hops(void);
size_t   mesh_manager_get_neighbor_count(void);

/* Libère les ressources (à n'appeler que pour tests). */
esp_err_t mesh_manager_deinit(void);

#ifdef __cplusplus
}
#endif
