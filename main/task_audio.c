/* task_audio.c — Core 0 : I2S → MFCC → TFLM audio inference
 *
 * Pipeline :
 *   1. Lecture I2S (via i2s_stereo_mic) dans un ring buffer PSRAM
 *   2. Toutes les ~500 ms, extraction d'une fenêtre → MFCC (via mfcc_dsp)
 *   3. Feed TFLM audio arena (via tflm_dual_runtime) → softmax
 *   4. Si confiance ≥ seuil, émettre audio_event_t vers audio_q
 *
 * Le ring buffer audio tourne en continu. L'inférence est périodique,
 * pas every-chunk — évite le traitement systématique quand il n'y a
 * rien d'intéressant.
 *
 * En mode capture audio : `capture_audio` = logs seuls sur USB ;
 * `capture_audio_host` = flux binaire LXCA sur stdout (script hôte).
 * Ce fichier ne s’exécute que pour les modes inférence audio.
 */
#include <string.h>
#include "app_config.h"
#include "app_events.h"
#include "lexa_config.h"
#include "i2s_stereo_mic.h"
#include "mfcc_dsp.h"
#include "tflm_dual_runtime.h"
#include "usb_telemetry.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"

static const char *TAG = "task_audio";

/* Queue de sortie (publiée par task_audio, consommée par orchestrator) */
QueueHandle_t audio_event_q = NULL;

/* Fenêtre d'analyse : 1 seconde de signal = 16000 samples × 2 bytes = 32 KB */
#define AUDIO_WINDOW_SAMPLES    16000
#define AUDIO_WINDOW_BYTES      (AUDIO_WINDOW_SAMPLES * sizeof(int16_t))

/* Inférence toutes les 500 ms */
#define INFER_PERIOD_MS         500

/* Seuil de confiance minimal pour émettre un event */
#define CONF_MIN_PCT            APP_AUDIO_CONF_MIN_PCT

static void task_audio_entry(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    /* Allocations workspace en PSRAM */
    int16_t *window = heap_caps_malloc(AUDIO_WINDOW_BYTES,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    configASSERT(window != NULL);
#if MBH_USB_TELEMETRY_STREAM
    int16_t *window_lr = heap_caps_malloc(AUDIO_WINDOW_SAMPLES * 2 * sizeof(int16_t),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    configASSERT(window_lr != NULL);
    uint32_t pcm_seq = 0;
#endif

    /* MFCC output : (AUDIO_WINDOW_SAMPLES - n_fft) / hop + 1 frames × n_coeff */
    const size_t n_frames = (AUDIO_WINDOW_SAMPLES - LEXA_MFCC_N_FFT)
                          / LEXA_MFCC_HOP + 1;
    const size_t mfcc_elements = n_frames * LEXA_MFCC_N_COEFF;
    float *mfcc_buf = heap_caps_malloc(mfcc_elements * sizeof(float),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    configASSERT(mfcc_buf != NULL);

    ESP_LOGI(TAG, "started, window=%d samples, mfcc=%ux%d",
             AUDIO_WINDOW_SAMPLES, (unsigned)n_frames, LEXA_MFCC_N_COEFF);

    const TickType_t period = pdMS_TO_TICKS(INFER_PERIOD_MS);
    TickType_t next_wake = xTaskGetTickCount();

    for (;;) {
        esp_task_wdt_reset();

        /* 1. Lire la dernière fenêtre du ring buffer I2S (mono MFCC ou stéréo + dérivation mono) */
#if MBH_USB_TELEMETRY_STREAM
        size_t read = i2s_stereo_mic_read_latest_interleaved_lr(window_lr, AUDIO_WINDOW_SAMPLES,
                                                                100) / 2U;
        if (read < AUDIO_WINDOW_SAMPLES) {
            ESP_LOGD(TAG, "short read %zu/%d, skip", read, AUDIO_WINDOW_SAMPLES);
            vTaskDelayUntil(&next_wake, period);
            continue;
        }
        for (size_t i = 0; i < AUDIO_WINDOW_SAMPLES; i++) {
            window[i] = window_lr[i * 2U];
        }
        usb_telemetry_enqueue_pcm_tail(window_lr, AUDIO_WINDOW_SAMPLES, pcm_seq++);
#else
        size_t read = i2s_stereo_mic_read_latest(window, AUDIO_WINDOW_SAMPLES,
                                                 100 /* ms timeout */);
        if (read < AUDIO_WINDOW_SAMPLES) {
            ESP_LOGD(TAG, "short read %zu/%d, skip", read, AUDIO_WINDOW_SAMPLES);
            vTaskDelayUntil(&next_wake, period);
            continue;
        }
#endif

        /* 2. Calculer le MFCC */
        int64_t t0 = esp_timer_get_time();
        esp_err_t err = mfcc_compute(window, AUDIO_WINDOW_SAMPLES,
                                     mfcc_buf, mfcc_elements);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "mfcc_compute failed: %s", esp_err_to_name(err));
            vTaskDelayUntil(&next_wake, period);
            continue;
        }
        int64_t t1 = esp_timer_get_time();

        /* 3. Inférer via TFLM audio interpreter */
        int32_t label_idx = -1;
        uint8_t confidence = 0;
        err = tflm_dual_infer_audio(mfcc_buf, mfcc_elements,
                                    &label_idx, &confidence);
        int64_t t2 = esp_timer_get_time();

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "tflm_infer_audio failed: %s", esp_err_to_name(err));
            vTaskDelayUntil(&next_wake, period);
            continue;
        }

#if MBH_USB_TELEMETRY_STREAM
        ESP_LOGD(TAG, "mfcc %lldus | inf %lldus | label=%ld conf=%u%%",
                 t1 - t0, t2 - t1, label_idx, confidence);
#else
        ESP_LOGI(TAG, "mfcc %lldus | inf %lldus | label=%ld conf=%u%%",
                 t1 - t0, t2 - t1, label_idx, confidence);
#endif

        /* 4. Émettre l'event si confiance suffisante */
        if (label_idx >= 0 && confidence >= CONF_MIN_PCT) {
            audio_event_t ev = {
                .label_idx    = (uint16_t)label_idx,
                .confidence_pct = confidence,
                .timestamp_ms = xTaskGetTickCount() * 1000 / configTICK_RATE_HZ,
            };
            if (xQueueSend(audio_event_q, &ev, 0) != pdPASS) {
                ESP_LOGW(TAG, "audio_event_q full, drop");
            }
        }

        vTaskDelayUntil(&next_wake, period);
    }
}

void task_audio_start(void)
{
    audio_event_q = xQueueCreate(4, sizeof(audio_event_t));
    configASSERT(audio_event_q != NULL);

    BaseType_t ok = xTaskCreatePinnedToCore(
        task_audio_entry, "task_audio", 8192, NULL, 5, NULL, 0);
    configASSERT(ok == pdPASS);
}
