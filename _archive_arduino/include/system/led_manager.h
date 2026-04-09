/**
 * @file led_manager.h
 * @brief Gestionnaire de la LED d'état NeoPixel (Feedback visuel).
 */

#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <stdint.h>

// États de la LED
enum LedState {
    LED_STATE_OFF,
    LED_STATE_SCANNING,     // Clignotement rapide (Bleu)
    LED_STATE_CONNECTED,    // Battement lent (Vert)
    LED_STATE_ROOT,         // Fixe ou lent (Violet)
    LED_STATE_OTA,          // Alerte générique (Orange)
    LED_STATE_OTA_SERIAL,   // OTA Série (0x01) : ROOT seul, violet pulsé
    LED_STATE_OTA_MESH,       // OTA générique (orange)
    LED_STATE_OTA_MESH_ROOT,  // OTA Mesh (0x02) ROOT : orange fixe, tâches arrêtées
    LED_STATE_OTA_MESH_CHILD, // OTA Mesh enfant : rouge fade vers bleu
    LED_STATE_ORPHAN,         // Perte parent : flash Orange/Rouge (reconnexion)
    LED_STATE_ERROR         // Rouge fixe
};

void led_manager_init(void);
void led_manager_task(void *pv);
void led_manager_set_state(LedState state);

// Événements ponctuels (flash)
void led_flash_rx(void); // Flash Cyan
void led_flash_yellow_rx(void); // Flash Yellow
void led_flash_tx(void); // Flash Blanc

#endif
