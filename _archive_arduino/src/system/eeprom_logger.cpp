/**
 * @file eeprom_logger.cpp
 * @brief Implémentation du logging sur EEPROM I2C CAT24M01W (1Mbit).
 *
 * @details
 * Le CAT24M01W possède 128KB de stockage. On implémente ici un buffer circulaire
 * simplifié pour stocker les logs sans système de fichiers, optimisant ainsi
 * la durée de vie de l'EEPROM et la rapidité d'accès.
 */

#include "system/eeprom_logger.h"
#include "config/config.h"
#include "config/pins_lexacare.h"
#include <Wire.h>
#include "esp_log.h"

static const char *TAG = "EEPROM";
static bool s_eeprom_ready = false;
static uint32_t s_current_addr = EEPROM_LOG_START_ADDR;

/**
 * @brief Écrit un octet à une adresse spécifique (adressage 17 bits).
 * CAT24M01 utilise l'adresse I2C + un bit de l'octet d'adresse pour atteindre 128KB.
 */
static bool eeprom_write_byte(uint32_t addr, uint8_t data)
{
    uint8_t i2c_addr = EEPROM_I2C_ADDR | ((addr >> 16) & 0x01);
    Wire.beginTransmission(i2c_addr);
    Wire.write((uint8_t)((addr >> 8) & 0xFF)); // MSB
    Wire.write((uint8_t)(addr & 0xFF));        // LSB
    Wire.write(data);
    if (Wire.endTransmission() != 0)
        return false;
    delay(5); // Temps d'écriture (tWR = 5ms)
    return true;
}

/**
 * @brief Initialise l'EEPROM.
 */
bool eeprom_logger_init(void)
{
    Wire.beginTransmission(EEPROM_I2C_ADDR);
    if (Wire.endTransmission() != 0)
    {
        ESP_LOGE(TAG, "CAT24M01W non détectée à l'adresse 0x%02X", EEPROM_I2C_ADDR);
        return false;
    }

    ESP_LOGI(TAG, "CAT24M01W (128KB) détectée");
    s_eeprom_ready = true;
    return true;
}

/**
 * @brief Ajoute un log dans l'EEPROM.
 */
void eeprom_log_append(const char *level, const char *tag, const char *msg)
{
    if (!s_eeprom_ready)
        return;

    char line[128];
    int len = snprintf(line, sizeof(line), "[%s][%s] %s\n", level, tag, msg);
    if (len <= 0)
        return;

    // Écriture caractère par caractère (simplifiée pour CAT24M01)
    // Pour optimiser, on pourrait utiliser l'écriture par page (256 octets)
    for (int i = 0; i < len; i++)
    {
        if (!eeprom_write_byte(s_current_addr, line[i]))
        {
            ESP_LOGE(TAG, "Erreur écriture EEPROM à 0x%05X", s_current_addr);
            return;
        }
        s_current_addr++;

        // Boucle circulaire
        if (s_current_addr >= EEPROM_MAX_LOG_SIZE)
        {
            s_current_addr = EEPROM_LOG_START_ADDR;
        }
    }
}

/**
 * @brief Logging formaté.
 */
void eeprom_log_printf(const char *level, const char *tag, const char *format, ...)
{
    char buf[96];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    eeprom_log_append(level, tag, buf);
}

/**
 * @brief Effacement logique.
 */
void eeprom_log_clear(void)
{
    s_current_addr = EEPROM_LOG_START_ADDR;
    ESP_LOGI(TAG, "Logs EEPROM réinitialisés");
}
