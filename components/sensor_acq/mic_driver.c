/**
 * @file mic_driver.c
 * @ingroup group_sensor_acq
 * @brief Driver microphone MEMS I2S — Amplitude RMS et crête.
 *
 * Utilise le driver I2S standard d'ESP-IDF v5/v6 (driver/i2s_std.h).
 * Le microphone MEMS (ex : INMP441, SPH0645) émet des données en I2S
 * 32 bits, canal gauche (ou droit selon LRCK). Les 24 bits supérieurs
 * contiennent les données audio, les 8 bits inférieurs sont nuls.
 */

#include "mic_driver.h"
#include "pins_config.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>
#include <stdint.h>

static const char *TAG = "mic_driver";

#define MIC_SAMPLE_RATE     16000   /**< Fréquence d'échantillonnage (Hz) */
#define MIC_BURST_SAMPLES   512     /**< Nombre d'échantillons par burst */
#define MIC_READ_TIMEOUT_MS 100

static i2s_chan_handle_t s_rx_chan = NULL;

/* Buffer de réception (I2S retourne des mots de 32 bits) */
static int32_t s_buf[MIC_BURST_SAMPLES];

/* ================================================================
 * mic_driver_init
 * ================================================================ */
esp_err_t mic_driver_init(void)
{
    /* Création du canal I2S RX */
    const i2s_chan_config_t chan_cfg = {
        .id            = I2S_NUM_AUTO,
        .role          = I2S_ROLE_MASTER,
        .dma_desc_num  = 4,
        .dma_frame_num = 256,
        .auto_clear    = true,
    };

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Création canal I2S échouée : %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configuration standard I2S */
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT,
                        I2S_SLOT_MODE_STEREO),  /* Stéréo pour lire LRCK */
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_MIC_SCK,
            .ws   = PIN_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = PIN_MIC_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init mode standard échoué : %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
        return ret;
    }

    ret = i2s_channel_enable(s_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Activation canal I2S échouée : %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "Microphone I2S initialisé (%d Hz, %d samples/burst)",
             MIC_SAMPLE_RATE, MIC_BURST_SAMPLES);
    return ESP_OK;
}

/* ================================================================
 * mic_driver_read
 * ================================================================ */
esp_err_t mic_driver_read(uint32_t *rms, uint32_t *peak)
{
    if (!s_rx_chan) return ESP_ERR_INVALID_STATE;

    size_t bytes_read = 0;
    /* Lecture de MIC_BURST_SAMPLES × 2 canaux × 4 octets = burst stéréo */
    const size_t read_size = MIC_BURST_SAMPLES * sizeof(int32_t) * 2;
    static int32_t stereo_buf[MIC_BURST_SAMPLES * 2];

    esp_err_t ret = i2s_channel_read(s_rx_chan, stereo_buf, read_size,
                                      &bytes_read,
                                      pdMS_TO_TICKS(MIC_READ_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Lecture I2S échouée : %s", esp_err_to_name(ret));
        return ret;
    }

    size_t n_samples = bytes_read / (sizeof(int32_t) * 2);
    if (n_samples == 0) return ESP_ERR_TIMEOUT;

    /* Extraction du canal gauche (pairs = index 0, 2, 4, ...) */
    /* Les données MEMS INMP441 sont dans les 24 bits supérieurs (shift >> 8) */
    uint64_t sum_sq = 0;
    uint32_t peak_val = 0;

    for (size_t i = 0; i < n_samples; i++) {
        int32_t sample = stereo_buf[i * 2] >> 8;  /* 24 bits significatifs */
        uint32_t abs_val = (sample < 0) ? (uint32_t)(-sample) : (uint32_t)sample;
        sum_sq += (uint64_t)abs_val * abs_val;
        if (abs_val > peak_val) peak_val = abs_val;
    }

    *rms  = (uint32_t)sqrtf((float)sum_sq / n_samples);
    *peak = peak_val;

    return ESP_OK;
}

/* ================================================================
 * mic_driver_deinit
 * ================================================================ */
void mic_driver_deinit(void)
{
    if (s_rx_chan) {
        i2s_channel_disable(s_rx_chan);
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
    }
}
