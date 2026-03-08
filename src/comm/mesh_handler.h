/**
 * @file mesh_handler.h
 * @brief Mesh Lexacare : painlessMesh (LEXACARE_MESH_PAINLESS) ou ESP-NOW (LEXACARE_MESH_32B).
 * @details Trame 32 octets, ROOT = min ID, JSON OTA dispatché à ota_manager.
 */

#ifndef MESH_HANDLER_H
#define MESH_HANDLER_H

#include "lexacare_protocol.h"
#include <stdint.h>

/** États LED pour le callback. */
typedef enum {
    LEX_LED_ROOT = 0,   ///< Nœud ROOT (vert)
    LEX_LED_DEVICE,     ///< Nœud standard du mesh (jaune / ambre)
    LEX_LED_BLUE,       ///< Flash réception (bleu)
    LEX_LED_RED,        ///< Flash erreur CRC (rouge)
    LEX_LED_OTA_IN_PROGRESS  ///< Mise à jour OTA en cours (violet)
} lex_led_state_t;

/** Callback LED : ROOT=vert, DEVICE=jaune, Bleu=réception, Rouge=erreur CRC. */
typedef void (*mesh_handler_led_cb_t)(lex_led_state_t state);

/**
 * @brief Initialise le mesh (painlessMesh ou ESP-NOW selon config).
 * @param led_cb Callback pour la LED (peut être NULL).
 * @return 1 si OK, 0 en cas d'erreur.
 */
int mesh_handler_init(mesh_handler_led_cb_t led_cb);

/**
 * @brief À appeler dans loop() : mise à jour du mesh, envoi périodique de la trame 32 octets, boucle OTA.
 * @details Appelle s_mesh.update() (painlessMesh), met à jour le statut ROOT, ota_manager_loop(),
 * et envoie la dernière trame simulée (sensor_sim_get_latest_frame) toutes les MESH_SEND_INTERVAL_MS.
 */
void mesh_handler_loop(void);

/**
 * @brief Envoie une trame 32 octets en broadcast.
 * @param frame32 Pointeur sur 32 octets (LexaFullFrame).
 * @return 1 si envoyé, 0 sinon.
 */
int mesh_handler_send_frame(const uint8_t *frame32);

/**
 * @brief Envoie une chaîne en broadcast (pour OTA JSON).
 * @param msg Chaîne à envoyer (JSON OTA).
 * @return 1 si envoyé, 0 sinon.
 */
int mesh_handler_send_broadcast_raw(const char *msg);

/**
 * @brief Envoie une chaîne à un nœud donné (pour OTA_REQ, OTA_CHUNK).
 * @param to ID du nœud destinataire
 * @param msg Chaîne à envoyer
 * @return 1 si envoyé, 0 sinon.
 */
int mesh_handler_send_to(uint32_t to, const char *msg);

/**
 * @brief Retourne 1 si ce nœud est ROOT (élection min ID pour painlessMesh).
 */
int mesh_handler_is_root(void);

#endif /* MESH_HANDLER_H */
