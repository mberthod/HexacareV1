/* task_mfcc_debug.c — fenêtre I2S → mfcc_compute → log BENCH: (validation Python↔C) */
#include "app_config.h"
#include "lexa_config.h"
#include "i2s_stereo_mic.h"
#include "mfcc_dsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "task_helpers.h"

static const char *TAG = "mfcc_dbg";

#define WINDOW_SAMPLES   16000

#if MBH_MODE_DEBUG_MFCC
static void mfcc_debug_entry(void *arg)
{
    (void)arg;
    int16_t *window = heap_caps_malloc(
        WINDOW_SAMPLES * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t n_frames = (WINDOW_SAMPLES - LEXA_MFCC_N_FFT)
                           / LEXA_MFCC_HOP + 1;
    const size_t mfcc_elements = n_frames * LEXA_MFCC_N_COEFF;
    float *mfcc_buf = heap_caps_malloc(
        mfcc_elements * sizeof(float),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!window || !mfcc_buf) {
        ESP_LOGE(TAG, "alloc failed");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "harness: window=%d mfcc_elements=%zu",
             WINDOW_SAMPLES, mfcc_elements);

    for (;;) {
        size_t got = i2s_stereo_mic_read_latest(
            window, WINDOW_SAMPLES, 200);
        if (got < WINDOW_SAMPLES) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        esp_err_t err = mfcc_compute(window, WINDOW_SAMPLES,
                                       mfcc_buf, mfcc_elements);
        int64_t t1 = esp_timer_get_time();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "mfcc_compute: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        ESP_LOGI(
            "BENCH",
            "BENCH:{\"kind\":\"mfcc_frame0_us\":%lld,\"c0\":%.5f,\"c1\":%.5f,"
            "\"c2\":%.5f,\"c3\":%.5f,\"c4\":%.5f,\"c5\":%.5f,\"c6\":%.5f}",
            (long long)(t1 - t0),
            mfcc_buf[0], mfcc_buf[1], mfcc_buf[2], mfcc_buf[3],
            mfcc_buf[4], mfcc_buf[5], mfcc_buf[6]);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

void task_mfcc_debug_start(void)
{
#if MBH_MODE_DEBUG_MFCC
    BaseType_t ok = xTaskCreatePinnedToCore(
        mfcc_debug_entry, "mfcc_dbg", 8192, NULL, 3, NULL, 0);
    configASSERT(ok == pdPASS);
#endif
}
