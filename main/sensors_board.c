/* Bus I2C board : capteurs sur I2C_NUM_0 (PCA9555 déjà installé) ; MLX90640 sur I2C_NUM_1.
 * I2C_NUM_0 : mutex récursif (pca9555_io) — tout le poll tient le bus pendant séquences multi-octets. */
#include "sensors_board.h"
#include "lexa_config.h"
#include "pca9555_io.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <math.h>
#include <string.h>

static const char *TAG = "sensors_board";

#define I2C_MAIN    I2C_NUM_0
#define I2C_MLX_BUS I2C_NUM_1

static bool s_mlx_bus_ready;

/** Appeler uniquement avec `lexa_i2c0_bus_lock()` déjà pris (pas de mutex interne). */
static bool i2c_probe_main_unlocked(uint8_t addr_7bit)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)((addr_7bit << 1) | I2C_MASTER_WRITE), true);
    i2c_master_stop(cmd);
    esp_err_t e = i2c_master_cmd_begin(I2C_MAIN, cmd, pdMS_TO_TICKS(60));
    i2c_cmd_link_delete(cmd);
    return e == ESP_OK;
}

static esp_err_t i2c_write_read_main_unlocked(uint8_t addr_7bit, const uint8_t *wr, size_t wr_len,
                                              uint8_t *rd, size_t rd_len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (wr_len == 0 && rd_len > 0) {
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (uint8_t)((addr_7bit << 1) | I2C_MASTER_READ), true);
    } else {
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (uint8_t)((addr_7bit << 1) | I2C_MASTER_WRITE), true);
        if (wr_len) {
            i2c_master_write(cmd, wr, wr_len, true);
        }
        if (rd_len) {
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (uint8_t)((addr_7bit << 1) | I2C_MASTER_READ), true);
        }
    }
    if (rd_len) {
        if (rd_len > 1) {
            i2c_master_read(cmd, rd, rd_len - 1, I2C_MASTER_ACK);
        }
        i2c_master_read_byte(cmd, rd + rd_len - 1, I2C_MASTER_NACK);
    }
    i2c_master_stop(cmd);
    esp_err_t e = i2c_master_cmd_begin(I2C_MAIN, cmd, pdMS_TO_TICKS(80));
    i2c_cmd_link_delete(cmd);
    return e;
}

static bool i2c_probe_mlx_bus(uint8_t addr_7bit)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)((addr_7bit << 1) | I2C_MASTER_WRITE), true);
    i2c_master_stop(cmd);
    esp_err_t e = i2c_master_cmd_begin(I2C_MLX_BUS, cmd, pdMS_TO_TICKS(60));
    i2c_cmd_link_delete(cmd);
    return e == ESP_OK;
}

static void ensure_power_radar(void)
{
    uint8_t p0 = 0, p1 = 0;
    if (pca9555_get_output_shadow(&p0, &p1) != ESP_OK) {
        return;
    }
    uint8_t p1n = (uint8_t)(p1 | LEXA_PCA9555_IO1_POWER_RADAR_MASK);
    if (p1n != p1) {
        (void)pca9555_write_output_ports(p0, p1n);
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

static esp_err_t init_mlx_i2c_bus(void)
{
    ensure_power_radar();

    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = LEXA_I2C_MLX_SDA_GPIO,
        .scl_io_num = LEXA_I2C_MLX_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {.clk_speed = LEXA_I2C_FREQ_HZ},
    };
    esp_err_t err = i2c_param_config(I2C_MLX_BUS, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_driver_install(I2C_MLX_BUS, cfg.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "I2C MLX install: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

esp_err_t sensors_board_init(void)
{
    ESP_LOGI(TAG,
             "I2C principal NUM_0 GPIO%d/%d — DS3231, CAT24, BME280, HDC1080, TMP117, VL53L0 ; "
             "I2C MLX NUM_1 GPIO%d/%d — MLX90640 (POWER_RADAR IO1.5) ; UART LD6002 %d/%d",
             LEXA_I2C_SDA_GPIO, LEXA_I2C_SCL_GPIO,
             LEXA_I2C_MLX_SDA_GPIO, LEXA_I2C_MLX_SCL_GPIO,
             LEXA_RADAR_UART_TX_GPIO, LEXA_RADAR_UART_RX_GPIO);

    s_mlx_bus_ready = (init_mlx_i2c_bus() == ESP_OK);
    if (!s_mlx_bus_ready) {
        ESP_LOGW(TAG, "bus MLX90640 non initialisé");
    }
    return ESP_OK;
}

static void read_ds3231_temp(sensors_board_snapshot_t *s)
{
    uint8_t reg = 0x11;
    uint8_t buf[2];
    if (i2c_write_read_main_unlocked(LEXA_I2C_ADDR_DS3231, &reg, 1, buf, 2) != ESP_OK) {
        return;
    }
    int8_t ti = (int8_t)buf[0];
    s->ds3231_temp_c = (float)ti + (float)((buf[1] >> 6) & 3u) * 0.25f;
}

static void read_hdc1080(sensors_board_snapshot_t *s)
{
    uint8_t p0 = 0x00;
    if (i2c_write_read_main_unlocked(LEXA_I2C_ADDR_HDC1080, &p0, 1, NULL, 0) != ESP_OK) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t tb[2];
    if (i2c_write_read_main_unlocked(LEXA_I2C_ADDR_HDC1080, NULL, 0, tb, 2) != ESP_OK) {
        return;
    }
    uint16_t rt = (uint16_t)((uint16_t)tb[0] << 8 | tb[1]);
    s->hdc1080_temp_c = (float)rt * (165.0f / 65536.0f) - 40.0f;

    uint8_t p1 = 0x01;
    if (i2c_write_read_main_unlocked(LEXA_I2C_ADDR_HDC1080, &p1, 1, NULL, 0) != ESP_OK) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t hb[2];
    if (i2c_write_read_main_unlocked(LEXA_I2C_ADDR_HDC1080, NULL, 0, hb, 2) != ESP_OK) {
        return;
    }
    uint16_t rh = (uint16_t)((uint16_t)hb[0] << 8 | hb[1]);
    s->hdc1080_rh_pct = (float)rh * (100.0f / 65536.0f);
}

static void read_tmp117(sensors_board_snapshot_t *s)
{
    uint8_t reg = 0x00;
    uint8_t tb[2];
    if (i2c_write_read_main_unlocked(LEXA_I2C_ADDR_TMP117, &reg, 1, tb, 2) != ESP_OK) {
        return;
    }
    int16_t raw = (int16_t)((int16_t)((uint16_t)tb[0] << 8 | tb[1]));
    s->tmp117_temp_c = (float)raw * (1.0f / 128.0f);
}

static void validate_cat24(sensors_board_snapshot_t *s)
{
    uint8_t hdr[2] = {0x00, 0x00};
    uint8_t b = 0;
    if (i2c_write_read_main_unlocked(LEXA_I2C_ADDR_CAT24, hdr, 2, &b, 1) != ESP_OK) {
        s->conn_cat24 = 0;
    }
}

void sensors_board_poll(sensors_board_snapshot_t *out)
{
    memset(out, 0, sizeof(*out));
    out->ds3231_temp_c = NAN;
    out->hdc1080_temp_c = NAN;
    out->hdc1080_rh_pct = NAN;
    out->bme280_temp_c = NAN;
    out->bme280_h_pa = NAN;
    out->bme280_rh_pct = NAN;
    out->tmp117_temp_c = NAN;

    lexa_i2c0_bus_lock();

    out->conn_ds3231 = i2c_probe_main_unlocked(LEXA_I2C_ADDR_DS3231) ? 1 : 0;
    out->conn_cat24 = i2c_probe_main_unlocked(LEXA_I2C_ADDR_CAT24) ? 1 : 0;
    out->conn_bme280 = (i2c_probe_main_unlocked(LEXA_I2C_ADDR_BME280_76)
                         || i2c_probe_main_unlocked(LEXA_I2C_ADDR_BME280_77))
                            ? 1
                            : 0;
    out->conn_hdc1080 = i2c_probe_main_unlocked(LEXA_I2C_ADDR_HDC1080) ? 1 : 0;
    out->conn_tmp117 = i2c_probe_main_unlocked(LEXA_I2C_ADDR_TMP117) ? 1 : 0;
    out->conn_vl53l0 = i2c_probe_main_unlocked(LEXA_I2C_ADDR_VL53L0) ? 1 : 0;

    ensure_power_radar();

    if (out->conn_ds3231) {
        read_ds3231_temp(out);
    }
    if (out->conn_hdc1080) {
        read_hdc1080(out);
    }
    if (out->conn_tmp117) {
        read_tmp117(out);
    }
    if (out->conn_cat24) {
        validate_cat24(out);
    }

    lexa_i2c0_bus_unlock();

    if (s_mlx_bus_ready) {
        ensure_power_radar();
        out->conn_mlx90640 = i2c_probe_mlx_bus(LEXA_I2C_ADDR_MLX90640) ? 1 : 0;
    } else {
        out->conn_mlx90640 = 0;
    }
}

/* --- Snapshot 1 Hz (tâche dédiée) pour ne pas bloquer la tâche USB LXCS/LXCL --- */
static sensors_board_snapshot_t s_snap_cached;
static SemaphoreHandle_t s_snap_mu;
static bool s_snap_worker_started;

void sensors_board_get_cached(sensors_board_snapshot_t *out)
{
    if (!out) {
        return;
    }
    if (s_snap_mu == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    if (xSemaphoreTake(s_snap_mu, pdMS_TO_TICKS(200)) != pdTRUE) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = s_snap_cached;
    xSemaphoreGive(s_snap_mu);
}

static void sensors_snap_worker(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        sensors_board_snapshot_t snap;
        sensors_board_poll(&snap);
        if (s_snap_mu) {
            if (xSemaphoreTake(s_snap_mu, portMAX_DELAY) == pdTRUE) {
                s_snap_cached = snap;
                xSemaphoreGive(s_snap_mu);
            }
        }
    }
}

void sensors_board_usb_snapshot_start(void)
{
    if (s_snap_worker_started) {
        return;
    }
    if (s_snap_mu == NULL) {
        s_snap_mu = xSemaphoreCreateMutex();
        if (s_snap_mu == NULL) {
            ESP_LOGE(TAG, "snap mutex alloc");
            return;
        }
    }
    s_snap_worker_started = true;
    /* Pile large (I2C + logs) ; prio < task_vision (5) pour limiter la contention core 1. */
    BaseType_t ok = xTaskCreatePinnedToCore(
        sensors_snap_worker, "sens_snap", 16384, NULL, 2, NULL, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "sens_snap task");
        s_snap_worker_started = false;
    }
}
