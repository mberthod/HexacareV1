/**
 * @file hdc1080_driver.c
 * @ingroup group_sensor_acq
 * @brief Driver HDC1080 — Température et Humidité (TI).
 *
 * Protocole I2C (adresse 0x40) :
 *   1. Écriture du registre cible (0x00 = Température)
 *   2. Attente 15 ms (conversion 14 bits)
 *   3. Lecture de 4 octets : [Temp_MSB][Temp_LSB][Hum_MSB][Hum_LSB]
 *
 * Conversion :
 *   Température (°C) = (raw_T / 65536.0) × 165.0 - 40.0
 *   Humidité (%)     = (raw_H / 65536.0) × 100.0
 */

#include "hdc1080_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "hdc1080";

/* Registres HDC1080 */
#define REG_TEMPERATURE  0x00
#define REG_HUMIDITY     0x01
#define REG_CONFIG       0x02
#define REG_MANUF_ID     0xFE
#define REG_DEVICE_ID    0xFF

#define HDC1080_DEVICE_ID   0x1050U
#define I2C_TIMEOUT_MS      20
#define ACQ_DELAY_MS        15   /* Temps de conversion 14 bits */

static i2c_master_dev_handle_t s_dev = NULL;

/* ================================================================
 * write_register (interne)
 * ================================================================ */
static esp_err_t write_register(uint8_t reg, uint16_t value)
{
    uint8_t buf[3] = {
        reg,
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFF),
    };
    return i2c_master_transmit(s_dev, buf, sizeof(buf),
                               pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

/* ================================================================
 * read_bytes (interne)
 * ================================================================ */
static esp_err_t read_bytes(uint8_t reg, uint8_t *out, size_t len)
{
    /* Pointer write */
    esp_err_t ret = i2c_master_transmit(s_dev, &reg, 1,
                                         pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (ret != ESP_OK) return ret;

    /* Attente acquisition */
    vTaskDelay(pdMS_TO_TICKS(ACQ_DELAY_MS));

    /* Lecture */
    return i2c_master_receive(s_dev, out, len,
                              pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

/* ================================================================
 * hdc1080_init
 * ================================================================ */
esp_err_t hdc1080_init(i2c_master_bus_handle_t bus)
{
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = HDC1080_I2C_ADDR,
        .scl_speed_hz    = 400000,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ajout device I2C échoué : %s", esp_err_to_name(ret));
        return ret;
    }

    /* Vérification Device ID */
    uint8_t id_buf[2] = {0};
    uint8_t reg = REG_DEVICE_ID;
    ret = i2c_master_transmit(s_dev, &reg, 1, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(5));
        ret = i2c_master_receive(s_dev, id_buf, 2, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HDC1080 non détecté sur bus I2C");
        return ret;
    }
    uint16_t device_id = ((uint16_t)id_buf[0] << 8) | id_buf[1];
    if (device_id != HDC1080_DEVICE_ID) {
        ESP_LOGW(TAG, "Device ID inattendu : 0x%04X (attendu 0x%04X)",
                 device_id, HDC1080_DEVICE_ID);
    }

    /* Configuration :
     *   Bit 13 = 1 : Acquisition température + humidité simultanée
     *   Bit 10 = 0 : Résolution température 14 bits
     *   Bit  9 = 0 : Résolution humidité 14 bits
     */
    const uint16_t config = (1u << 13);
    ret = write_register(REG_CONFIG, config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Écriture config échouée : %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "HDC1080 initialisé (Device ID=0x%04X, config=0x%04X)",
             device_id, config);
    return ESP_OK;
}

/* ================================================================
 * hdc1080_read
 * ================================================================ */
esp_err_t hdc1080_read(float *temp_c, float *humidity_pct)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Déclenchement acquisition séquentielle (écriture REG_TEMPERATURE) */
    uint8_t raw[4] = {0};
    esp_err_t ret = read_bytes(REG_TEMPERATURE, raw, sizeof(raw));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Lecture échouée : %s", esp_err_to_name(ret));
        return ret;
    }

    uint16_t raw_t = ((uint16_t)raw[0] << 8) | raw[1];
    uint16_t raw_h = ((uint16_t)raw[2] << 8) | raw[3];

    *temp_c       = (raw_t / 65536.0f) * 165.0f - 40.0f;
    *humidity_pct = (raw_h / 65536.0f) * 100.0f;

    /* Clamp physique */
    if (*humidity_pct < 0.0f)   *humidity_pct = 0.0f;
    if (*humidity_pct > 100.0f) *humidity_pct = 100.0f;

    return ESP_OK;
}
