/**
 * @file bme280_driver.c
 * @ingroup group_sensor_acq
 * @brief Driver Bosch BME280 — Température, Pression, Humidité.
 *
 * Implémentation de l'algorithme de compensation entier 32/64 bits
 * issu du datasheet Bosch BME280 (v1.24, Appendix A).
 *
 * Mode forced (single-shot) : déclenche une mesure puis passe en sleep.
 * Oversampling T×2, P×4, H×4 — filtre IIR × 4.
 */

#include "bme280_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "bme280";

/* ─── Registres ───────────────────────────────────────────────────────────── */
#define REG_CHIP_ID       0xD0
#define REG_RESET         0xE0
#define REG_CTRL_HUM      0xF2
#define REG_STATUS        0xF3
#define REG_CTRL_MEAS     0xF4
#define REG_CONFIG        0xF5
#define REG_DATA_START    0xF7   /* 0xF7..0xFC : pression + temp + humidité */
#define REG_CALIB_00      0x88   /* 0x88..0x9F : trim T + P */
#define REG_CALIB_H1      0xA1
#define REG_CALIB_H2      0xE1   /* 0xE1..0xE7 : trim H */

#define BME280_CHIP_ID    0x60
#define I2C_TIMEOUT_MS    20
#define MEAS_DELAY_MS     10     /* Attente conversion (worst-case ~9.4 ms) */

/* ─── Coefficients de calibration ─────────────────────────────────────────── */
typedef struct {
    uint16_t T1; int16_t T2; int16_t T3;
    uint16_t P1; int16_t P2; int16_t P3; int16_t P4;
    int16_t  P5; int16_t P6; int16_t P7; int16_t P8; int16_t P9;
    uint8_t  H1; int16_t H2; uint8_t H3;
    int16_t  H4; int16_t H5; int8_t H6;
} bme280_calib_t;

static i2c_master_dev_handle_t s_dev   = NULL;
static bme280_calib_t          s_calib = {0};

/* ─── Helpers I2C ──────────────────────────────────────────────────────────── */

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t read_regs(uint8_t reg, uint8_t *out, size_t len)
{
    esp_err_t ret = i2c_master_transmit(s_dev, &reg, 1,
                                         pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (ret != ESP_OK) return ret;
    return i2c_master_receive(s_dev, out, len,
                              pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

/* ─── Lecture calibration ──────────────────────────────────────────────────── */

static esp_err_t read_calibration(void)
{
    uint8_t c[26] = {0};
    esp_err_t ret = read_regs(REG_CALIB_00, c, 24);
    if (ret != ESP_OK) return ret;

    s_calib.T1 = (uint16_t)(c[1]  << 8 | c[0]);
    s_calib.T2 = (int16_t) (c[3]  << 8 | c[2]);
    s_calib.T3 = (int16_t) (c[5]  << 8 | c[4]);
    s_calib.P1 = (uint16_t)(c[7]  << 8 | c[6]);
    s_calib.P2 = (int16_t) (c[9]  << 8 | c[8]);
    s_calib.P3 = (int16_t) (c[11] << 8 | c[10]);
    s_calib.P4 = (int16_t) (c[13] << 8 | c[12]);
    s_calib.P5 = (int16_t) (c[15] << 8 | c[14]);
    s_calib.P6 = (int16_t) (c[17] << 8 | c[16]);
    s_calib.P7 = (int16_t) (c[19] << 8 | c[18]);
    s_calib.P8 = (int16_t) (c[21] << 8 | c[20]);
    s_calib.P9 = (int16_t) (c[23] << 8 | c[22]);

    ret = read_regs(REG_CALIB_H1, c, 1);
    if (ret != ESP_OK) return ret;
    s_calib.H1 = c[0];

    uint8_t h[7] = {0};
    ret = read_regs(REG_CALIB_H2, h, 7);
    if (ret != ESP_OK) return ret;
    s_calib.H2 = (int16_t) (h[1] << 8 | h[0]);
    s_calib.H3 = h[2];
    s_calib.H4 = (int16_t) ((h[3] << 4) | (h[4] & 0x0F));
    s_calib.H5 = (int16_t) ((h[5] << 4) | (h[4] >> 4));
    s_calib.H6 = (int8_t)  h[6];

    return ESP_OK;
}

/* ─── Algorithme de compensation Bosch (datasheet v1.24 Appendix A) ─────── */

static int32_t s_t_fine = 0;

static float compensate_temperature(int32_t raw_t)
{
    int32_t var1 = ((((raw_t >> 3) - ((int32_t)s_calib.T1 << 1))) *
                    (int32_t)s_calib.T2) >> 11;
    int32_t var2 = (((((raw_t >> 4) - (int32_t)s_calib.T1) *
                       ((raw_t >> 4) - (int32_t)s_calib.T1)) >> 12) *
                    (int32_t)s_calib.T3) >> 14;
    s_t_fine = var1 + var2;
    return (float)((s_t_fine * 5 + 128) >> 8) / 100.0f;
}

static float compensate_pressure(int32_t raw_p)
{
    int64_t var1 = (int64_t)s_t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)s_calib.P6;
    var2 += (var1 * (int64_t)s_calib.P5) << 17;
    var2 += ((int64_t)s_calib.P4) << 35;
    var1  = ((var1 * var1 * (int64_t)s_calib.P3) >> 8) +
            ((var1 * (int64_t)s_calib.P2) << 12);
    var1  = (((int64_t)1 << 47) + var1) * (int64_t)s_calib.P1 >> 33;
    if (var1 == 0) return 0.0f;
    int64_t p = 1048576 - raw_p;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)s_calib.P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)s_calib.P8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((int64_t)s_calib.P7 << 4);
    return (float)p / 25600.0f;  /* hPa */
}

static float compensate_humidity(int32_t raw_h)
{
    int32_t v = s_t_fine - 76800;
    v = (((((raw_h << 14) - ((int32_t)s_calib.H4 << 20) -
            ((int32_t)s_calib.H5 * v)) + 16384) >> 15) *
         (((((((v * (int32_t)s_calib.H6) >> 10) *
              (((v * (int32_t)s_calib.H3) >> 11) + 32768)) >> 10) +
            2097152) * (int32_t)s_calib.H2 + 8192) >> 14));
    v -= ((((v >> 15) * (v >> 15)) >> 7) * (int32_t)s_calib.H1) >> 4;
    if (v < 0)        v = 0;
    if (v > 419430400) v = 419430400;
    float rh = (float)(v >> 12) / 1024.0f;
    if (rh < 0.0f)   rh = 0.0f;
    if (rh > 100.0f) rh = 100.0f;
    return rh;
}

/* ─── API publique ─────────────────────────────────────────────────────────── */

esp_err_t bme280_init(i2c_master_bus_handle_t bus, uint8_t i2c_addr)
{
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = i2c_addr,
        .scl_speed_hz    = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ajout device I2C 0x%02X échoué : %s",
                 i2c_addr, esp_err_to_name(ret));
        return ret;
    }

    /* Vérification chip ID */
    uint8_t chip_id = 0;
    ret = read_regs(REG_CHIP_ID, &chip_id, 1);
    if (ret != ESP_OK || chip_id != BME280_CHIP_ID) {
        ESP_LOGW(TAG, "BME280 non détecté (chip_id=0x%02X)", chip_id);
        return (ret == ESP_OK) ? ESP_ERR_NOT_FOUND : ret;
    }

    /* Reset logiciel */
    write_reg(REG_RESET, 0xB6);
    vTaskDelay(pdMS_TO_TICKS(5));

    /* Lecture calibration */
    ret = read_calibration();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Lecture calibration échouée");
        return ret;
    }

    /* Configuration :
     *   ctrl_hum  : osrs_h = ×4 (101b = 5)
     *   ctrl_meas : osrs_t = ×2 (010b), osrs_p = ×4 (011b), mode = sleep (00b)
     *   config    : filtre IIR = ×4 (010b), t_sb = 0.5 ms (000b)
     */
    write_reg(REG_CTRL_HUM,  0x05);  /* osrs_h ×4 */
    write_reg(REG_CONFIG,    0x10);  /* filtre ×4 */
    write_reg(REG_CTRL_MEAS, 0x54);  /* osrs_t ×2, osrs_p ×4, sleep mode */

    ESP_LOGI(TAG, "BME280 initialisé (@0x%02X, chip_id=0x60)", i2c_addr);
    return ESP_OK;
}

esp_err_t bme280_read(float *temp_c, float *pressure_hpa, float *humidity_pct)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;

    /* Déclencher une mesure forced (mode = 01b → écriture de ctrl_meas) */
    uint8_t ctrl;
    esp_err_t ret = read_regs(REG_CTRL_MEAS, &ctrl, 1);
    if (ret != ESP_OK) return ret;

    /* Set mode = forced (01) en conservant l'oversampling */
    ctrl = (ctrl & 0xFC) | 0x01;
    ret = write_reg(REG_CTRL_MEAS, ctrl);
    if (ret != ESP_OK) return ret;

    /* Attente fin de conversion */
    vTaskDelay(pdMS_TO_TICKS(MEAS_DELAY_MS));

    /* Lecture de 8 octets : P (3), T (3), H (2) */
    uint8_t data[8] = {0};
    ret = read_regs(REG_DATA_START, data, sizeof(data));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Lecture données échouée : %s", esp_err_to_name(ret));
        return ret;
    }

    int32_t raw_p = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) | (data[2] >> 4);
    int32_t raw_t = ((int32_t)data[3] << 12) | ((int32_t)data[4] << 4) | (data[5] >> 4);
    int32_t raw_h = ((int32_t)data[6] << 8)  | data[7];

    /* Compensation (ordre obligatoire : T en premier pour s_t_fine) */
    *temp_c       = compensate_temperature(raw_t);
    *pressure_hpa = compensate_pressure(raw_p);
    *humidity_pct = compensate_humidity(raw_h);

    return ESP_OK;
}
