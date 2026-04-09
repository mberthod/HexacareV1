/**
 * @file analog_handler.h
 * @brief ADC : surveillance V_IN, V_BATT, 1V8, 3V3 avec lissage
 */

#ifndef ANALOG_HANDLER_H
#define ANALOG_HANDLER_H

#include "config/config.h"
#include <cstdint>
#include <cstdbool>

#ifdef __cplusplus
extern "C" {
#endif

// Initialise les canaux ADC (attenuation, largeur).
void analog_handler_init(void);

// Lit les 4 rails, applique lissage, met à jour system_state (rails_status).
void analog_handler_update(void);

// Retourne une tension en mV pour le canal (0=V_IN, 1=V_BATT, 2=1V8, 3=3V3).
uint32_t analog_handler_read_rail_mv(int channel);

#ifdef __cplusplus
}
#endif

#endif // ANALOG_HANDLER_H
