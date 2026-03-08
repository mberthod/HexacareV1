/**
 * @file espnow_mesh.h
 * @brief Couche Radio ESP-NOW V2 (Point-to-Point / Unicast).
 */

#ifndef ESPNOW_MESH_H
#define ESPNOW_MESH_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Initialise le WiFi et ESP-NOW.
 * @return 1 si succès, 0 sinon.
 */
int espnow_mesh_init(void);

/**
 * @brief Envoie un paquet ESP-NOW (Unicast).
 * @param dest_mac Adresse MAC de destination (ou broadcast si nécessaire).
 * @param data Pointeur vers les données.
 * @param len Longueur des données.
 * @return 1 si succès (envoyé à la couche MAC), 0 sinon.
 */
int espnow_mesh_send(const uint8_t *dest_mac, const uint8_t *data, size_t len);

/**
 * @brief Récupère l'adresse MAC locale.
 */
void espnow_mesh_get_my_mac(uint8_t *mac_out);

#endif
