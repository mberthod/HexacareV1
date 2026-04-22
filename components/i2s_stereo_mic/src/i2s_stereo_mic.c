/* i2s_stereo_mic.c — I2S RX Philips 32-bit stéréo (ICS-43434) : slots L et R lus séparément.
 *
 * Données micro (TDK / InvenSense DS-000069 v1.2, ICS-43434) :
 *   - I2S Philips, 24 bits/canal, complément à deux, MSB en premier ;
 *   - 64 fronts SCK par période WS stéréo (32 bits × 2 canaux).
 *
 * Conversion 32-bit (DMA) → int16 : une extraction 24-bit signée, puis passage
 * int16 sans atténuation numérique par défaut (éviter signal quasi nul / 0 et -1024
 * au traceur si « extra » trop fort). `pcm_extra_downshift` ajoute des bits de
 * droite retirés seulement si tu satures encore (clip int16).
 *
 * Extraction du mot 32 :
 *   I2S_STEREO_MIC_RAW_EXTRACT=0 (défaut) : 24 bits utiles en 31..8 → s24 = raw >> 8
 *   I2S_STEREO_MIC_RAW_EXTRACT=1 : 24 bits en 23..0 → s24 = (raw << 8) >> 8
 * Surcharge : platformio build_flags -DI2S_STEREO_MIC_RAW_EXTRACT=1
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "i2s_stereo_mic.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "i2s_mic";

#define I2S_DMA_DESC_NUM          8
#define I2S_DMA_FRAME_NUM         512
#define I2S_RAW_BUF_STEREO_FRAMES 512
#define I2S_RAW_BUF_BYTES           (I2S_RAW_BUF_STEREO_FRAMES * 2 * (int)sizeof(int32_t))

#ifndef I2S_STEREO_MIC_RAW_EXTRACT
#define I2S_STEREO_MIC_RAW_EXTRACT 0
#endif

/* Si cfg->pcm_extra_downshift == 0 : aucun décalage en plus du 24→16 (recommandé).
 * Chaque +1 ≈ −6 dB après extraction 24-bit. Surcharge : -DI2S_STEREO_MIC_PCM_EXTRA_DEFAULT=3 */
#ifndef I2S_STEREO_MIC_PCM_EXTRA_DEFAULT
#define I2S_STEREO_MIC_PCM_EXTRA_DEFAULT 0u
#endif

static unsigned s_pcm_extra_downshift;
static unsigned s_pcm_output_shift;

static i2s_chan_handle_t s_rx = NULL;
static int32_t *s_raw = NULL;
static bool s_inited = false;
static int s_sample_rate_hz = 0;

static int32_t raw_to_s24(int32_t raw)
{
#if I2S_STEREO_MIC_RAW_EXTRACT
    return (int32_t)(((uint32_t)raw << 8u) >> 8u);
#else
    return raw >> 8;
#endif
}

static int16_t convert_left_ics43434(int32_t raw)
{
    int32_t s24 = raw_to_s24(raw);
    unsigned sh = 8u + s_pcm_extra_downshift;
    if (sh > 30u) {
        sh = 30u;
    }
    int32_t y = s24 >> (int)sh;
    int64_t z = (int64_t)y;
    if (s_pcm_output_shift > 0u) {
        z <<= s_pcm_output_shift;
    }
    if (z > 32767) {
        z = 32767;
    }
    if (z < -32768) {
        z = -32768;
    }
    return (int16_t)z;
}

esp_err_t i2s_stereo_mic_init(const i2s_stereo_mic_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_inited) {
        return ESP_OK;
    }

    if (cfg->pcm_extra_downshift != 0) {
        s_pcm_extra_downshift = cfg->pcm_extra_downshift;
    } else {
        s_pcm_extra_downshift = I2S_STEREO_MIC_PCM_EXTRA_DEFAULT;
    }
    if (s_pcm_extra_downshift > 12u) {
        ESP_LOGW(TAG,
                 "pcm_extra_downshift=%u plafonné à 12 (évite silence numérique)",
                 (unsigned)s_pcm_extra_downshift);
        s_pcm_extra_downshift = 12u;
    }

    s_pcm_output_shift = cfg->pcm_output_shift;
    if (s_pcm_output_shift > 7u) {
        ESP_LOGW(TAG, "pcm_output_shift=%u plafonné à 7", (unsigned)s_pcm_output_shift);
        s_pcm_output_shift = 7u;
    }

    s_raw = (int32_t *)malloc(I2S_RAW_BUF_BYTES);
    if (!s_raw) {
        ESP_LOGE(TAG, "malloc raw I2S failed");
        return ESP_ERR_NO_MEM;
    }

    /* ESP-IDF 6.0 : le port I2S est un int (type i2s_port_t retiré). I2S_NUM_0 reste un macro entier. */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((int)I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = I2S_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = I2S_DMA_FRAME_NUM;

    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
        goto fail_free_raw;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)cfg->sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)cfg->bclk_gpio,
            .ws = (gpio_num_t)cfg->ws_gpio,
            .dout = I2S_GPIO_UNUSED,
            .din = (gpio_num_t)cfg->din_gpio,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(s_rx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode: %s", esp_err_to_name(err));
        goto fail_del_chan;
    }

    err = i2s_channel_enable(s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable: %s", esp_err_to_name(err));
        goto fail_del_chan;
    }

    /* Vider un peu le FIFO au démarrage (équivalent i2s_zero_dma_buffer) */
    size_t n = 0;
    for (int i = 0; i < 4; i++) {
        (void)i2s_channel_read(s_rx, s_raw, I2S_RAW_BUF_BYTES, &n, pdMS_TO_TICKS(10));
    }

    s_sample_rate_hz = cfg->sample_rate_hz;
    s_inited = true;
    ESP_LOGI(TAG,
             "I2S0 RX @ %d Hz bclk=%d ws=%d din=%d extract=%d downshift=%u out_shift=%u (<< gain)",
             s_sample_rate_hz, cfg->bclk_gpio, cfg->ws_gpio, cfg->din_gpio,
             I2S_STEREO_MIC_RAW_EXTRACT, (unsigned)s_pcm_extra_downshift,
             (unsigned)s_pcm_output_shift);
    return ESP_OK;

fail_del_chan:
    if (s_rx) {
        (void)i2s_del_channel(s_rx);
        s_rx = NULL;
    }
fail_free_raw:
    free(s_raw);
    s_raw = NULL;
    return err;
}

size_t i2s_stereo_mic_read_latest(int16_t *out, size_t n_samples, int timeout_ms)
{
    if (!s_inited || !out || n_samples == 0) {
        return 0;
    }

    size_t produced = 0;
    TickType_t tmo = (timeout_ms <= 0) ? pdMS_TO_TICKS(1) : pdMS_TO_TICKS(timeout_ms);

    while (produced < n_samples) {
        size_t want_mono = n_samples - produced;
        size_t max_frames = I2S_RAW_BUF_STEREO_FRAMES;
        size_t want_frames = want_mono;
        if (want_frames > max_frames) {
            want_frames = max_frames;
        }

        size_t need_bytes = want_frames * 2 * sizeof(int32_t);
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(s_rx, s_raw, need_bytes, &bytes_read, tmo);
        if (err != ESP_OK || bytes_read < 2 * sizeof(int32_t)) {
            break;
        }

        size_t frames = bytes_read / (2 * sizeof(int32_t));
        for (size_t i = 0; i < frames && produced < n_samples; i++) {
            out[produced++] = convert_left_ics43434(s_raw[i * 2]);
        }
    }

    return produced;
}

size_t i2s_stereo_mic_read_latest_interleaved_lr(int16_t *out_lr, size_t max_pairs,
                                                 int timeout_ms)
{
    if (!s_inited || !out_lr || max_pairs == 0) {
        return 0;
    }

    size_t pair_out = 0;
    TickType_t tmo = (timeout_ms <= 0) ? pdMS_TO_TICKS(1) : pdMS_TO_TICKS(timeout_ms);

    while (pair_out < max_pairs) {
        size_t want_pairs = max_pairs - pair_out;
        size_t max_frames = I2S_RAW_BUF_STEREO_FRAMES;
        size_t want_frames = want_pairs;
        if (want_frames > max_frames) {
            want_frames = max_frames;
        }

        size_t need_bytes = want_frames * 2 * sizeof(int32_t);
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(s_rx, s_raw, need_bytes, &bytes_read, tmo);
        if (err != ESP_OK || bytes_read < 2 * sizeof(int32_t)) {
            break;
        }

        size_t frames = bytes_read / (2 * sizeof(int32_t));
        for (size_t i = 0; i < frames && pair_out < max_pairs; i++) {
            out_lr[2 * pair_out] = convert_left_ics43434(s_raw[i * 2]);
            out_lr[2 * pair_out + 1U] = convert_left_ics43434(s_raw[i * 2 + 1U]);
            pair_out++;
        }
    }

    return pair_out * 2U;
}
