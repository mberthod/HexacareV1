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

#endif
