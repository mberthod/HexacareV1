/* task_vision.c — Core 1 : ToF SPI → TFLM vision inference
 *
 * Pipeline :
 *   1. Polling des 4× VL53L8CX (via vl53l8cx_array) en résolution 8×8
 *   2. Assemblage d'une frame 8×32 (32×8 float row-major, layout Arduino / PCB)
 *   3. Normalisation (distance / 4000 mm), invalid → 0
 *   4. Inférence TFLM vision arena
 *   5. Si confiance ≥ seuil, émettre vision_event_t
 *
 * Rate cible : 10 Hz (acquisition ToF = 34 ms × 4 = 136 ms, +TFLM ~1 ms).
 * Latence OK pour détection chute (événement > 100 ms).
 */
#include <stdint.h>
#include <string.h>
#include "app_config.h"
#include "app_events.h"
#include "lexa_config.h"
#include "vl53l8cx_array.h"
#include "tflm_dual_runtime.h"
#include "usb_telemetry.h"
#include "lexa_tof_frame_ascii.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"

static const char *TAG = "task_vision";

QueueHandle_t vision_event_q = NULL;

#define TOF_FRAME_W   VL53L8CX_ARRAY_FRAME_W
#define TOF_FRAME_H   VL53L8CX_ARRAY_FRAME_H
#define TOF_FRAME_N   (TOF_FRAME_W * TOF_FRAME_H)

#define INFER_PERIOD_MS     100    /* 10 Hz */
#define CONF_MIN_PCT        APP_VISION_CONF_MIN_PCT

static void task_vision_entry(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    /* Frame assemblée normalisée (float32 [0,1]) en DRAM — petit buffer */
    float *frame = heap_caps_malloc(TOF_FRAME_N * sizeof(float),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    configASSERT(frame != NULL);

    ESP_LOGI(TAG, "started, frame %dx%d @ %d Hz", TOF_FRAME_W, TOF_FRAME_H,
             1000 / INFER_PERIOD_MS);

    const TickType_t period = pdMS_TO_TICKS(INFER_PERIOD_MS);
    TickType_t next_wake = xTaskGetTickCount();

    for (;;) {
        esp_task_wdt_reset();

        /* 1. Acquisition + assemblage frame 32×8 (8 lignes × 32 colonnes) */
        int64_t t0 = esp_timer_get_time();
#if MBH_SERIAL_ASCII_TOF_FRAME
        int16_t mm_tof[TOF_FRAME_N];
#endif
        /* 4 capteurs @ ~15 Hz : laisser assez de marge pour une trame complète par device */
        esp_err_t err = vl53l8cx_array_read_frame(
            frame,
#if MBH_SERIAL_ASCII_TOF_FRAME
            mm_tof,
#else
            NULL,
#endif
            TOF_FRAME_W, TOF_FRAME_H, 150 /* ms timeout */);
        int64_t t1 = esp_timer_get_time();
        if (err != ESP_OK) {
            ESP_LOGD(TAG, "tof read failed: %s", esp_err_to_name(err));
            vTaskDelayUntil(&next_wake, period);
            continue;
        }

#if MBH_SERIAL_ASCII_TOF_FRAME
        lexa_tof_frame_emit_ascii_line(mm_tof, TOF_FRAME_W, TOF_FRAME_H);
#endif

#if MBH_USB_TELEMETRY_STREAM
        usb_telemetry_post_vision(frame, TOF_FRAME_W, TOF_FRAME_H);
#endif

        /* 2. Inférer TFLM vision */
        int32_t label_idx = -1;
        uint8_t confidence = 0;
        err = tflm_dual_infer_vision(frame, TOF_FRAME_N,
                                     &label_idx, &confidence);
        int64_t t2 = esp_timer_get_time();

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "tflm_infer_vision failed: %s", esp_err_to_name(err));
            vTaskDelayUntil(&next_wake, period);
            continue;
        }

        ESP_LOGD(TAG, "tof %lldus | inf %lldus | label=%ld conf=%u%%",
                 t1 - t0, t2 - t1, label_idx, confidence);

        /* 3. Émettre si confiance suffisante ET classe d'intérêt (chute) */
        if (label_idx >= 0 && confidence >= CONF_MIN_PCT) {
            vision_event_t ev = {
                .label_idx     = (uint16_t)label_idx,
                .confidence_pct = confidence,
                .timestamp_ms  = xTaskGetTickCount() * 1000 / configTICK_RATE_HZ,
            };
            if (xQueueSend(vision_event_q, &ev, 0) != pdPASS) {
                ESP_LOGW(TAG, "vision_event_q full, drop");
            }
        }

        vTaskDelayUntil(&next_wake, period);
    }
}

void task_vision_start(void)
{
    vision_event_q = xQueueCreate(4, sizeof(vision_event_t));
    configASSERT(vision_event_q != NULL);

    BaseType_t ok = xTaskCreatePinnedToCore(
        task_vision_entry, "task_vision", 8192, NULL, 5, NULL, 1);
    configASSERT(ok == pdPASS);
}
