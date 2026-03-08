/**
 * @file mesh_mqtt.h
 * @brief painlessMesh + élection ROOT ; sortie des données en série uniquement (pas de MQTT WiFi).
 */

#ifndef MESH_MQTT_H
#define MESH_MQTT_H

#include "config/config.h"
#include <cstdbool>

#ifdef __cplusplus
extern "C" {
#endif

// Initialise le Mesh. À appeler sur le core 0.
void mesh_mqtt_init(void);

// À appeler dans loop() : mise à jour du mesh ; si ROOT, envoi JSON périodique sur port série uniquement.
void mesh_mqtt_loop(void);

// Retourne true si ce nœud est le ROOT du mesh.
bool mesh_mqtt_is_root(void);

#ifdef __cplusplus
}
#endif

#endif // MESH_MQTT_H
