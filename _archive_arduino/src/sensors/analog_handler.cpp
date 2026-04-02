/**
 * @file analog_handler.cpp
 * @brief Surveillance des rails d'alimentation via l'ADC1.
 * 
 * Ce module gère la lecture des tensions V_IN, V_BATT, 1V8 et 3V3.
 * Il applique un lissage par moyenne glissante et utilise les caractéristiques
 * d'étalonnage de l'ESP32-S3 pour convertir les valeurs brutes en millivolts.
 */

#include "config/config.h"
#include "analog_handler.h"
#include "config/pins_lexacare.h"
#include "system/system_state.h"
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <Arduino.h>
#include "esp_log.h"

static const char* TAG = "ANALOG";

/** @brief Mapping des broches ADC1 (GPIO 6, 7, 9, 10 sur ESP32-S3) */
static const int PINS[] = { PIN_ADC_VIN, PIN_ADC_VBATT, PIN_ADC_1V8, PIN_ADC_3V3 };
static const adc1_channel_t CHANNELS[] = {
    ADC1_CHANNEL_5,
    ADC1_CHANNEL_6,
    ADC1_CHANNEL_8,
    ADC1_CHANNEL_9
};
#define NUM_CHANNELS 4                      ///< Nombre de rails surveillés

static esp_adc_cal_characteristics_t s_adc_cal; ///< Caractéristiques d'étalonnage
static bool s_cal_done = false;             ///< État de l'étalonnage

/**
 * @brief Convertit un numéro de broche GPIO en canal ADC1.
 * @param pin Numéro du GPIO.
 * @return Canal ADC1 correspondant.
 */
static adc1_channel_t pin_to_channel(int pin) {
    if (pin == 6) return ADC1_CHANNEL_5;
    if (pin == 7) return ADC1_CHANNEL_6;
    if (pin == 9) return ADC1_CHANNEL_8;
    if (pin == 10) return ADC1_CHANNEL_9;
    return ADC1_CHANNEL_5;
}

/**
 * @brief Initialise les canaux ADC et effectue l'étalonnage.
 */
void analog_handler_init(void) {
    ESP_LOGI(TAG, "Initialisation ADC1...");
    for (int i = 0; i < NUM_CHANNELS; i++) {
        adc1_channel_t ch = pin_to_channel(PINS[i]);
        adc1_config_channel_atten(ch, ADC_ATTEN_DB_12);
    }
    adc1_config_width(ADC_WIDTH_BIT_12);
    
    // Caractérisation de l'ADC pour conversion précise en mV
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 3300, &s_adc_cal);
    (void)val_type;
    s_cal_done = true;
    ESP_LOGI(TAG, "ADC1 initialisé et étalonné");
}

/**
 * @brief Lit la tension d'un rail spécifique avec lissage.
 * @param channel Index du rail (0-3).
 * @return Tension en millivolts.
 */
uint32_t analog_handler_read_rail_mv(int channel) {
    if (channel < 0 || channel >= NUM_CHANNELS) return 0;
    adc1_channel_t ch = pin_to_channel(PINS[channel]);
    uint32_t raw = 0;
    
    // Moyennage pour filtrer le bruit
    for (int i = 0; i < ADC_SAMPLES_SMOOTH; i++)
        raw += adc1_get_raw(ch);
    raw /= ADC_SAMPLES_SMOOTH;
    
    uint32_t mv;
    if (!s_cal_done) mv = (raw * 3300) / 4095;
    else mv = esp_adc_cal_raw_to_voltage(raw, &s_adc_cal);
    
    ESP_LOGV(TAG, "Rail %d: %u mV (raw: %u)", channel, mv, raw);
    return mv;
}

/**
 * @brief Met à jour l'état système avec les dernières mesures ADC.
 */
void analog_handler_update(void) {
    rails_status_t r = {};
    r.v_in_mv = analog_handler_read_rail_mv(0);
    r.v_batt_mv = analog_handler_read_rail_mv(1);
    r.v_1v8_mv = analog_handler_read_rail_mv(2);
    r.v_3v3_mv = analog_handler_read_rail_mv(3);
    r.last_update_ms = millis();
    
    ESP_LOGD(TAG, "V_IN: %u, V_BATT: %u, 1V8: %u, 3V3: %u", r.v_in_mv, r.v_batt_mv, r.v_1v8_mv, r.v_3v3_mv);
    
    // Enregistrement sécurisé dans l'état partagé
    system_state_set_rails(&r);
}
