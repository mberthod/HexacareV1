/**
 * @file vl53l0x_handler.h
 * @brief VL53L0X (carte radar) : mesure distance objet, XSHUT sur IO5
 * @note Optionnel selon schéma
 */

#ifndef VL53L0X_HANDLER_H
#define VL53L0X_HANDLER_H

#include <cstdint>
#include <cstdbool>

#ifdef __cplusplus
extern "C" {
#endif

bool vl53l0x_handler_init(void);
uint16_t vl53l0x_handler_read_mm(void);

#ifdef __cplusplus
}
#endif

#endif // VL53L0X_HANDLER_H
