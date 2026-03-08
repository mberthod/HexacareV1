/**
 * @file tmp117_handler.cpp
 * @brief Gestion du capteur de température haute précision TMP117.
 * 
 * Le TMP117 est un capteur I2C offrant une résolution de 0.0078125 °C par LSB.
 * Ce module lit le registre de température et met à jour l'état système.
 */

#include "tmp117_handler.h"
#include "config/pins_lexacare.h"
#include "system/system_state.h"
#include <Wire.h>
#include <Arduino.h>

/**
 * @brief Vérifie la présence du capteur sur le bus I2C.
 * @return true si le capteur répond.
 */
bool tmp117_handler_init(void) {
    Wire.beginTransmission(TMP117_I2C_ADDR);
    return Wire.endTransmission() == 0;
}

/**
 * @brief Lit la température actuelle et la stocke dans l'état système.
 * 
 * Effectue une lecture de 16 bits sur le registre TMP117_REG_TEMP.
 * 
 * @return Température en degrés Celsius, ou -273.15 en cas d'erreur.
 */
float tmp117_handler_read_temp_c(void) {
    Wire.beginTransmission(TMP117_I2C_ADDR);
    Wire.write(TMP117_REG_TEMP);
    if (Wire.endTransmission(false) != 0) return -273.15f;
    
    if (Wire.requestFrom((uint8_t)TMP117_I2C_ADDR, (uint8_t)2) != 2) return -273.15f;
    
    int16_t raw = (int16_t)(Wire.read() << 8 | Wire.read());
    float temp = (float)raw * 0.0078125f;
    
    // Mise à jour de l'état partagé
    system_state_set_sys_temp(temp);
    
    return temp;
}
