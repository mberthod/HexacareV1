/**
 * @file eeprom_logger.h
 * @brief Gestion de l'EEPROM I2C (CAT24M01W) pour le logging système.
 */

#ifndef EEPROM_LOGGER_H
#define EEPROM_LOGGER_H

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Met à jour la couleur de la LED RGB système.
 */
void set_status_led(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Initialise la communication avec l'EEPROM CAT24M01W.
 * @return true si l'EEPROM répond sur le bus I2C.
 */
bool eeprom_logger_init(void);

/**
 * @brief Ajoute une entrée de log dans l'EEPROM (mode circulaire).
 * @param level Niveau de log (INFO, WARN, ERROR).
 * @param tag Module émetteur.
 * @param msg Message à sauvegarder.
 */
void eeprom_log_append(const char *level, const char *tag, const char *msg);

/**
 * @brief Formate et logue un message dans l'EEPROM (style printf).
 */
void eeprom_log_printf(const char *level, const char *tag, const char *format, ...);

/**
 * @brief Réinitialise l'index de log (effacement logique).
 */
void eeprom_log_clear(void);

#ifdef __cplusplus
}
#endif

#endif // EEPROM_LOGGER_H
