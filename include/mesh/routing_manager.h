/**
 * @file routing_manager.h
 * @brief Gestion du routage Tree Mesh : Parent, Enfants, Heartbeats, Self-Healing.
 *
 * Conforme au Cahier des Charges Hexacare V2 (Tree Mesh via ESP-NOW).
 */

#ifndef ROUTING_MANAGER_H
#define ROUTING_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <vector>
#include "mesh/mesh_tree_protocol.h"

/** Table de routage locale : clé = adresse MAC ; nodeId dérivé de la MAC (affichage). */
struct ChildInfo {
    uint8_t  mac[6];
    uint16_t nodeId;         /* Dérivé de mac[4:5] pour affichage / compat (routing_get_my_id, etc.) */
    uint32_t lastSeen;       /* last_heartbeat_timestamp (ticks) */
    bool     is_active;
};

/** Constantes CDC : Beacon 2–5 s, Heartbeat 10 s, timeout enfant 30 s, 3 échecs → ORPHAN. */
#define ROUTING_BEACON_INTERVAL_MS_MIN  2000
#define ROUTING_BEACON_INTERVAL_MS_MAX  5000
#define ROUTING_HEARTBEAT_INTERVAL_MS   10000
#define ROUTING_CHILD_TIMEOUT_MS        30000
#define ROUTING_HEARTBEAT_FAIL_ORPHAN   3
#define ROUTING_JOIN_WAIT_MS            1000

/** Initialisation et rôle ROOT */
void routing_init(void);
void routing_set_root(void);

/** Tâche principale (Network Building, Heartbeats, Self-Healing). */
void routing_task(void *pv);

/** Envoi Unicast (header + payload). */
bool routing_send_unicast(const uint8_t* dest_mac, uint8_t msgType, const uint8_t* payload, uint16_t len);

/** Remontée des données vers le parent (TTL décrémenté). */
void routing_forward_upstream(const uint8_t* data, size_t len);

/** Descente : envoi vers un nœud cible par ID court (dérivé MAC). */
void routing_route_downstream(uint16_t target_id, const uint8_t* data, size_t len);

/** Propagation : envoi des données à tous les enfants directs (par adresse MAC). */
void routing_propagate_to_children(const uint8_t* data, size_t len);

/** Envoi vers un enfant identifié par son adresse MAC. */
bool routing_send_to_child_mac(const uint8_t* dest_mac, const uint8_t* data, size_t len);

/** API pour les autres modules */
std::vector<ChildInfo> routing_get_children(void);
bool routing_get_parent_mac(uint8_t* mac_out);
uint16_t routing_get_my_id(void);
uint8_t routing_get_layer(void);
uint16_t routing_get_parent_id(void);

/** Callback réception ESP-NOW (Beacons, JOIN, Heartbeat, etc.). */
void on_mesh_receive(const uint8_t* mac, const uint8_t* data, int len);

#endif
