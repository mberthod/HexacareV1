/* tr_espnow_priv.h — types internes partagés entre les .c du composant
 *
 * Enveloppe de message, table de voisins, contexte global.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "tr_espnow.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define MAGIC_BYTE        0xA7
#define PROTO_VERSION     1

/* Identique à MESH_NODE_BROADCAST (mesh_manager.h) — défini ici pour éviter
 * un include mesh_manager dans tr_espnow (cycle CMake : mesh_manager → tr_espnow). */
#define TR_MESH_NODE_BROADCAST   0xFFFFu

#define MSG_TYPE_HELLO         0x01
#define MSG_TYPE_DATA          0x02
#define MSG_TYPE_DATA_BCAST    0x03
#define MSG_TYPE_PROBE         0x04
#define MSG_TYPE_PROBE_ACK     0x05

/* Enveloppe envoyée sur ESP-NOW — packed, little-endian.
 * Taille = 10 bytes. Payload max = 250 - 10 = 240 bytes. */
typedef struct __attribute__((packed)) {
    uint8_t  magic;       /* MAGIC_BYTE */
    uint8_t  type;        /* MSG_TYPE_* */
    uint8_t  ttl;         /* décrémente à chaque hop, drop si 0 */
    uint16_t src_id;      /* toujours l'émetteur d'origine */
    uint16_t dst_id;      /* 0xFFFF = broadcast */
    uint16_t msg_id;      /* unique par src_id, pour dedup */
    uint8_t  flags;       /* réservé */
    uint8_t  payload_len; /* 0..240 */
    /* payload suit */
} mesh_envelope_t;

/* HELLO payload — annonce topologie */
typedef struct __attribute__((packed)) {
    uint16_t parent_id;     /* 0 si pas encore de parent, ou si je suis root */
    uint8_t  hop_count;     /* 0 = root */
    int8_t   rssi_to_parent;/* dBm, -127..0 */
    uint8_t  node_role;     /* 0=leaf, 1=relay, 2=root */
} hello_payload_t;

/* Probe payload — ping/pong bidir */
typedef struct __attribute__((packed)) {
    uint32_t nonce;
    int64_t  sent_us;
} probe_payload_t;

/* Entrée de la table de voisins */
typedef struct {
    uint16_t node_id;
    uint8_t  mac[6];
    int8_t   rssi;
    uint8_t  hop_count;
    uint16_t parent_id;     /* qu'il a annoncé */
    int64_t  last_seen_us;
    bool     is_peer_added; /* enregistré via esp_now_add_peer ? */
} neighbor_t;

#define MAX_NEIGHBORS    20   /* limite matérielle ESP-NOW = 20 peers */
#define DEDUP_TABLE_SIZE 32

typedef struct {
    uint16_t src_id;
    uint16_t msg_id;
    int64_t  seen_us;
} dedup_entry_t;

/* Contexte tr_espnow */
struct tr_espnow_ctx_s {
    tr_espnow_config_t cfg;

    /* Topologie */
    uint16_t parent_id;
    uint8_t  hop_count;
    int8_t   rssi_to_parent;

    /* Voisins (accès protégé par nb_lock) */
    neighbor_t        neighbors[MAX_NEIGHBORS];
    size_t            nb_count;
    SemaphoreHandle_t nb_lock;

    /* Dedup broadcast */
    dedup_entry_t     dedup[DEDUP_TABLE_SIZE];
    size_t            dedup_idx;  /* ring buffer write index */

    /* Compteur msg_id local */
    uint16_t next_msg_id;

    /* Stats */
    uint32_t stats_tx;
    uint32_t stats_rx;
    uint32_t stats_lost;
    uint32_t stats_forwarded;
    SemaphoreHandle_t stats_lock;

    /* Pour probing : on stocke le nonce attendu et on signale via une sem */
    uint32_t           probe_nonce;
    SemaphoreHandle_t  probe_sem;
    int64_t            probe_sent_us;
    int64_t            probe_rtt_us;

    /* Tâche HELLO */
    TaskHandle_t hello_task;

    /* Queue RX interne pour traitement asynchrone depuis la callback ESP-NOW */
    QueueHandle_t rx_q;
    TaskHandle_t  rx_task;
};

/* ------------------------------------------------------------------
 * Entrée struct RX : buffer complet reçu via ESP-NOW callback
 * ------------------------------------------------------------------ */
#define RX_BUF_MAX 260
typedef struct {
    uint8_t mac[6];
    int8_t  rssi;
    uint8_t buf[RX_BUF_MAX];
    size_t  len;
} rx_entry_t;

/* ------------------------------------------------------------------
 * Fonctions internes (définies dans espnow_tree.c, espnow_neighbor.c, etc.)
 * ------------------------------------------------------------------ */

/* Neighbor table */
void     nb_table_init(struct tr_espnow_ctx_s *c);
void     nb_table_upsert(struct tr_espnow_ctx_s *c, uint16_t node_id,
                         const uint8_t mac[6], int8_t rssi, uint8_t hop_count,
                         uint16_t parent_id);
void     nb_table_expire(struct tr_espnow_ctx_s *c, int64_t now_us,
                         int64_t max_age_ms);
neighbor_t *nb_find(struct tr_espnow_ctx_s *c, uint16_t node_id);
neighbor_t *nb_find_by_mac(struct tr_espnow_ctx_s *c, const uint8_t mac[6]);

/* Tree */
void     tree_recompute_parent(struct tr_espnow_ctx_s *c);
uint16_t tree_next_hop_for(struct tr_espnow_ctx_s *c, uint16_t dst);

/* Dedup */
bool     dedup_check_and_add(struct tr_espnow_ctx_s *c,
                             uint16_t src_id, uint16_t msg_id);

/* Envelope */
esp_err_t env_build(mesh_envelope_t *hdr, uint8_t type, uint16_t src,
                    uint16_t dst, uint16_t msg_id, uint8_t ttl,
                    uint8_t payload_len);
bool      env_parse(const uint8_t *buf, size_t buf_len,
                    const mesh_envelope_t **hdr, const uint8_t **payload);
