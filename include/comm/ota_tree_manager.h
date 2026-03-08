/**
 * @file ota_tree_manager.h
 * @brief Gestionnaire OTA "Store & Forward" pour topologie en arbre.
 * 
 * Stratégie :
 * 1. Le nœud reçoit l'update complet (soit par UART si ROOT, soit par son Parent).
 * 2. Il stocke tout en flash (partition OTA).
 * 3. Une fois complet et vérifié, il devient "Serveur de distribution".
 * 4. Il contacte ses enfants UN PAR UN pour leur pousser la mise à jour.
 * 5. Une fois tous les enfants servis (ou timeout), il reboote sur la nouvelle partition.
 */

#ifndef OTA_TREE_MANAGER_H
#define OTA_TREE_MANAGER_H

#include <stdint.h>
#include "mesh_tree_protocol.h"

// Initialisation
void ota_tree_init(void);

/** À appeler juste avant réception UART : 0x01 = OTA Série (ROOT seul), 0x02 = OTA Mesh (diffusion). */
void ota_tree_set_uart_mode(uint8_t mode);

/** Démarre la propagation après réception UART complète (mode 0x02). À appeler depuis serial_gateway. */
void ota_tree_start_propagation(uint32_t total_size, uint16_t total_chunks, const char *md5);

// Tâche principale (machine à états réception/distribution)
void ota_tree_task(void *pv);

// Entrées (événements)
void ota_tree_on_uart_chunk(uint32_t offset, const uint8_t* data, uint16_t len, uint32_t totalSize, const char* md5);
void ota_tree_on_mesh_message(const uint8_t* src_mac, uint8_t msgType, const uint8_t* payload, uint16_t len);

#endif
