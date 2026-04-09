/**
 * @file vl53l0x_handler.cpp
 * @brief Stub / placeholder VL53L0X (DIST_SHUTDOWN = IO5). À brancher sur driver réel si présent.
 */

#include "vl53l0x_handler.h"
#include "config/pins_lexacare.h"
#include <Arduino.h>

bool vl53l0x_handler_init(void) {
    pinMode(PIN_VL53L0X_XSHUT, OUTPUT);
    digitalWrite(PIN_VL53L0X_XSHUT, HIGH);
    delay(10);
    return true;
}

uint16_t vl53l0x_handler_read_mm(void) {
    (void)0;
    return 0;
}
