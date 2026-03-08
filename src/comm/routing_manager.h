/**
 * @file routing_manager.h
 * @brief Gestion du routage Tree Mesh : Parent, Enfants, Heartbeats.
 */

#ifndef ROUTING_MANAGER_H
#define ROUTING_MANAGER_H

#include <stdint.h>
#include <vector>
#include "mesh_tree_protocol.h"

// Structure pour stocker les infos d'un enfant
struct ChildInfo {
    uint16_t nodeId;
    uint8_t  mac[6];
    uint32_t lastSeen;
};

// Initialisation et tâche
void routing_init(void);
void routing_task(void *pv);

// API Publique pour les autres modules (OTA, etc.)
std::vector<ChildInfo> routing_get_children(void);
bool routing_get_parent_mac(uint8_t* mac_out);
uint16_t routing_get_my_id(void);
bool routing_send_unicast(const uint8_t* dest_mac, uint8_t msgType, const uint8_t* payload, uint16_t len);

// Callback de réception (appelé depuis le callback ESP-NOW)
void on_mesh_receive(const uint8_t* mac, const uint8_t* data, int len);

#endif
