/* VL53L8CX ×4 (SPI) + LPn PCA9555 — ULD ST (vl53l8cx_api.h / uld/).
 *
 * Correspondance sketch Arduino → ce fichier :
 *   setup()  init capteurs : vl53l8cx_init, vl53l8cx_set_resolution(8×8),
 *            vl53l8cx_set_ranging_frequency_hz, vl53l8cx_set_target_order,
 *            vl53l8cx_start_ranging — voir vl53l8cx_array_init().
 *   loop()   pour chaque i : vl53l8cx_check_data_ready(&dev[i], &isReady)
 *            puis vl53l8cx_get_ranging_data — voir la boucle d’attente ci-dessous.
 *   FRAME    8 lignes × 32 colonnes, COL_ORDER — voir l’assemblage final.
 */
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "vl53l8cx_array.h"
#include "vl53l8cx_platform_esp_idf.h"
#include "vl53l8cx_api.h"
#include "pca9555_io.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "vl53l8cx";

extern uint8_t VL53L8CX_io_write(void *handle, uint16_t RegisterAddress, uint8_t *p_values, uint32_t size);
extern uint8_t VL53L8CX_io_read(void *handle, uint16_t RegisterAddress, uint8_t *p_values, uint32_t size);
extern uint8_t VL53L8CX_io_wait(void *handle, uint32_t ms);

#define NUM_SENSORS 4

/* LPn sur port PCA9555 IO0.x (indices bit) : U1..U4 = bits 6,5,4,3 */
static const uint8_t k_lpn_bit[NUM_SENSORS] = {6u, 5u, 4u, 3u};
/* Colonnes gauche → droite : capteurs #3,#4,#2,#1 → indices 2,3,1,0 */
static const uint8_t k_col_order[NUM_SENSORS] = {2u, 3u, 1u, 0u};

static VL53L8CX_Configuration s_dev[NUM_SENSORS];
static VL53L8CX_ResultsData s_results[NUM_SENSORS];
static spi_device_handle_t s_spi_dev[NUM_SENSORS];
static bool s_sensor_ok[NUM_SENSORS];
static bool s_bus_inited;
static bool s_any_ok;

static esp_err_t lpn_only(uint8_t idx, int level)
{
    uint8_t p0 = 0;
    uint8_t p1 = 0;
    pca9555_get_output_shadow(&p0, &p1);
    uint8_t bit = k_lpn_bit[idx];
    if (level) {
        p0 |= (uint8_t)(1u << bit);
    } else {
        p0 &= (uint8_t) ~(1u << bit);
    }
    return pca9555_write_output_ports(p0, p1);
}

static float norm_mm(int16_t mm, uint8_t status)
{
    (void)status;
    if (mm < 0 || mm > 4000) {
        return 0.f;
    }
    return (float)mm / 4000.f;
}

static void fill_synthetic(float *frame, int w, int h)
{
    for (int i = 0; i < w * h; i++) {
        frame[i] = 0.8f;
    }
    int center = (h / 2) * w + (w / 2);
    frame[center] = (float)(esp_random() % 100) / 100.0f;
}

esp_err_t vl53l8cx_array_init(const vl53l8cx_array_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Alim LIDAR rail (GPIO MCU, pas PCA9555) */
    if (cfg->power_en_gpio >= 0) {
        gpio_config_t pwr = {
            .pin_bit_mask = 1ULL << (unsigned)cfg->power_en_gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&pwr));
        gpio_set_level((gpio_num_t)cfg->power_en_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = cfg->spi_mosi_gpio,
        .miso_io_num = cfg->spi_miso_gpio,
        .sclk_io_num = cfg->spi_clk_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 8192,
    };
    if (!s_bus_inited) {
        esp_err_t e = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(e));
            return e;
        }
        s_bus_inited = true;
    }
    vl53l8cx_platform_esp_bus_lock_init();

    for (int i = 0; i < NUM_SENSORS; i++) {
        s_sensor_ok[i] = false;
        spi_device_handle_t devh = NULL;
        spi_device_interface_config_t devcfg = {
            .mode = 3,
            .clock_speed_hz = cfg->spi_freq_hz,
            .spics_io_num = cfg->ncs_gpios[i],
            .queue_size = 2,
            .flags = 0,
        };
        ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &devh));
        s_spi_dev[i] = devh;

        memset(&s_dev[i], 0, sizeof(s_dev[i]));
        s_dev[i].platform.address = VL53L8CX_DEFAULT_I2C_ADDRESS;
        s_dev[i].platform.Write = VL53L8CX_io_write;
        s_dev[i].platform.Read = VL53L8CX_io_read;
        s_dev[i].platform.Wait = VL53L8CX_io_wait;
        s_dev[i].platform.handle = (void *)devh;

        /* Reset LPn capteur i (autres LPn inchangés via shadow) */
        ESP_ERROR_CHECK(lpn_only((uint8_t)i, 0));
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_ERROR_CHECK(lpn_only((uint8_t)i, 1));
        vTaskDelay(pdMS_TO_TICKS(10));

        uint8_t alive = 0;
        if (vl53l8cx_is_alive(&s_dev[i], &alive) != 0 || !alive) {
            ESP_LOGW(TAG, "capteur %d non detecte (alive=%u)", i, (unsigned)alive);
            continue;
        }

        uint8_t st = vl53l8cx_init(&s_dev[i]);
        if (st != 0) {
            ESP_LOGW(TAG, "vl53l8cx_init capteur %d status=%u", i, (unsigned)st);
            continue;
        }
        st |= vl53l8cx_set_resolution(&s_dev[i], VL53L8CX_RESOLUTION_8X8);
        st |= vl53l8cx_set_ranging_frequency_hz(&s_dev[i], 15);
        st |= vl53l8cx_set_target_order(&s_dev[i], VL53L8CX_TARGET_ORDER_STRONGEST);
        st |= vl53l8cx_start_ranging(&s_dev[i]);
        if (st != 0) {
            ESP_LOGW(TAG, "config/start capteur %d status=%u", i, (unsigned)st);
            (void)vl53l8cx_stop_ranging(&s_dev[i]);
            continue;
        }
        s_sensor_ok[i] = true;
        ESP_LOGI(TAG, "capteur %d pret (SPI %d Hz)", i, cfg->spi_freq_hz);
    }

    s_any_ok = false;
    for (int i = 0; i < NUM_SENSORS; i++) {
        if (s_sensor_ok[i]) {
            s_any_ok = true;
            break;
        }
    }
    if (!s_any_ok) {
        ESP_LOGW(TAG, "aucun VL53L8CX operationnel — frame synthetique");
    }
    return ESP_OK;
}

esp_err_t vl53l8cx_array_read_frame(float *frame, int16_t *mm_out_opt, int w,
                                    int h, int timeout_ms)
{
    if (!frame || w != VL53L8CX_ARRAY_FRAME_W || h != VL53L8CX_ARRAY_FRAME_H) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_any_ok) {
        fill_synthetic(frame, w, h);
        if (mm_out_opt) {
            memset(mm_out_opt, 0, (size_t)(w * h) * sizeof(int16_t));
        }
        return ESP_OK;
    }

    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;

    /* Ne pas effacer s_results : en cas de timeout partiel on garde la dernière trame
     * valide par capteur (même logique qu’Arduino si un capteur n’a pas encore poussé). */
    bool got[NUM_SENSORS] = {false, false, false, false};

    while (esp_timer_get_time() < deadline) {
        bool any = false;
        for (int i = 0; i < NUM_SENSORS; i++) {
            if (!s_sensor_ok[i] || got[i]) {
                continue;
            }
            uint8_t ready = 0;
            if (vl53l8cx_check_data_ready(&s_dev[i], &ready) != 0) {
                continue;
            }
            if (ready) {
                if (vl53l8cx_get_ranging_data(&s_dev[i], &s_results[i]) == 0) {
                    got[i] = true;
                    any = true;
                }
            }
        }
        bool all_fresh = true;
        for (int i = 0; i < NUM_SENSORS; i++) {
            if (s_sensor_ok[i] && !got[i]) {
                all_fresh = false;
                break;
            }
        }
        if (all_fresh) {
            break;
        }
        if (!any) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    /* Assemblage 8×32 (row-major) : identique au triple for Arduino
     *   for (row) for (block) sensor=COL_ORDER[block] for (col 0..7)
     *       distance_mm[row*8+col]  →  frame[row*32 + block*8 + col] */
    for (int row = 0; row < 8; row++) {
        for (int b = 0; b < 4; b++) {
            uint8_t sens = k_col_order[b];
            for (int col = 0; col < 8; col++) {
                int out = row * 32 + b * 8 + col;
                float v = 0.f;
                int16_t mm = 0;
                if (s_sensor_ok[sens]) {
                    int idx = row * 8 + col;
                    mm = s_results[sens].distance_mm[idx];
                    uint8_t st = s_results[sens].target_status[idx];
                    v = norm_mm(mm, st);
                }
                frame[out] = v;
                if (mm_out_opt) {
                    mm_out_opt[out] = mm;
                }
            }
        }
    }

    return ESP_OK;
}
