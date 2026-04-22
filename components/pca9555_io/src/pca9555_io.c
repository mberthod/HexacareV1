/* PCA9555 (I2C) — LexaCare U24, adresse 0x20. Port0 : LPn ToF, DIST_SHUTDOWN, etc.
 * Port1 : POWER_FAN/RADAR/MIC/MLX (TPS22917, actif haut). */
#include <string.h>
#include "pca9555_io.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "pca9555";

#define PCA_REG_INPUT0     0x00
#define PCA_REG_INPUT1     0x01
#define PCA_REG_OUTPUT0    0x02
#define PCA_REG_OUTPUT1    0x03
#define PCA_REG_POLINV0    0x04
#define PCA_REG_POLINV1    0x05
#define PCA_REG_CONFIG0    0x06
#define PCA_REG_CONFIG1    0x07

static i2c_port_t s_port = I2C_NUM_0;
static uint8_t s_addr_7bit;
static uint8_t s_out0;
static uint8_t s_out1;
static SemaphoreHandle_t s_i2c0_mu;

static esp_err_t i2c0_exec_cmd(i2c_cmd_handle_t cmd)
{
    if (!s_i2c0_mu) {
        i2c_cmd_link_delete(cmd);
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTakeRecursive(s_i2c0_mu, portMAX_DELAY);
    esp_err_t e = i2c_master_cmd_begin(s_port, cmd, pdMS_TO_TICKS(80));
    xSemaphoreGiveRecursive(s_i2c0_mu);
    i2c_cmd_link_delete(cmd);
    return e;
}

void lexa_i2c0_bus_lock(void)
{
    if (s_i2c0_mu) {
        xSemaphoreTakeRecursive(s_i2c0_mu, portMAX_DELAY);
    }
}

void lexa_i2c0_bus_unlock(void)
{
    if (s_i2c0_mu) {
        xSemaphoreGiveRecursive(s_i2c0_mu);
    }
}

static esp_err_t write_reg8(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_addr_7bit << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    return i2c0_exec_cmd(cmd);
}

static esp_err_t write_reg16(uint8_t reg, uint8_t v0, uint8_t v1)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_addr_7bit << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, v0, true);
    i2c_master_write_byte(cmd, v1, true);
    i2c_master_stop(cmd);
    return i2c0_exec_cmd(cmd);
}

static esp_err_t read_reg16(uint8_t reg, uint8_t *v0, uint8_t *v1)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_addr_7bit << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_addr_7bit << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, v0, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, v1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    return i2c0_exec_cmd(cmd);
}

esp_err_t pca9555_io_init(int sda_gpio, int scl_gpio, uint8_t i2c_addr_7bit)
{
    s_addr_7bit = i2c_addr_7bit;

    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = 100000,
        },
    };
    esp_err_t err = i2c_param_config(s_port, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_driver_install(s_port, cfg.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (s_i2c0_mu == NULL) {
        s_i2c0_mu = xSemaphoreCreateRecursiveMutex();
        if (s_i2c0_mu == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    /* Port0 : tout en sortie (0 = output). Port1 : IO1.0–1.3 entrées, IO1.4–1.7 sorties */
    ESP_ERROR_CHECK(write_reg8(PCA_REG_CONFIG0, 0x00));
    ESP_ERROR_CHECK(write_reg8(PCA_REG_CONFIG1, 0x0F));

    /* État initial sûr : LPn ToF bas ; DIST_SHUTDOWN haut (VL53L0 actif) ;
     * alims sous-systèmes ON sur port1 (bits 4–7). */
    s_out0 = (uint8_t)(1u << 0); /* DIST_SHUTDOWN */
    s_out1 = (uint8_t)(0xF0);    /* POWER_* ON */
    err = write_reg16(PCA_REG_OUTPUT0, s_out0, s_out1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write outputs failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "init ok addr=0x%02X sda=%d scl=%d", i2c_addr_7bit, sda_gpio, scl_gpio);
    return ESP_OK;
}

esp_err_t pca9555_set_output_mode(uint8_t pin_mask)
{
    (void)pin_mask;
    return ESP_OK;
}

esp_err_t pca9555_write_output(uint8_t values)
{
    /* API historique : un seul octet → port0 seulement, conserve port1 shadow */
    s_out0 = values;
    return write_reg16(PCA_REG_OUTPUT0, s_out0, s_out1);
}

esp_err_t pca9555_write_output_ports(uint8_t port0, uint8_t port1)
{
    s_out0 = port0;
    s_out1 = port1;
    return write_reg16(PCA_REG_OUTPUT0, s_out0, s_out1);
}

esp_err_t pca9555_read_input(uint8_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t in0 = 0, in1 = 0;
    esp_err_t e = read_reg16(PCA_REG_INPUT0, &in0, &in1);
    if (e != ESP_OK) {
        return e;
    }
    *out = in0;
    (void)in1;
    return ESP_OK;
}

esp_err_t pca9555_get_output_shadow(uint8_t *port0, uint8_t *port1)
{
    if (port0) {
        *port0 = s_out0;
    }
    if (port1) {
        *port1 = s_out1;
    }
    return ESP_OK;
}
