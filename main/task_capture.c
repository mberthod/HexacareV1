/* task_capture.c — capture audio / lidar
 *
 * - Binaire LXCA/LXCL sur stdout si MBH_CAPTURE_BINARY_TO_STDOUT=1 (capture_*_host).
 * - PCM int16 brut continu si MBH_CAPTURE_RAW_PCM_TO_STDOUT=1 (capture_audio_raw + script --raw-pcm).
 * - ASCII une valeur int16 par ligne si MBH_CAPTURE_ASCII_SERIAL_PLOTTER=1
 *   (Arduino IDE > Outils > Traceur série, même baud que platformio monitor_speed, ex. 921600).
 * - Sinon logs périodiques sur la console USB. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include "app_config.h"
#include "lexa_config.h"
#include "i2s_stereo_mic.h"
#include "vl53l8cx_array.h"
#include "lexa_tof_frame_ascii.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "task_helpers.h"

static const char *TAG = "task_capture";

#define CAP_MAGIC_A  "LXCA"
#define CAP_MAGIC_AUDIO_ST  "LXCS"
#define CAP_MAGIC_L  "LXCL"

#define CAP_AUDIO_CHUNK_SAMPLES  1024

#if MBH_CAPTURE_BINARY_TO_STDOUT || MBH_CAPTURE_RAW_PCM_TO_STDOUT
/* Stream sur la console (USB Serial/JTAG si sdkconfig l’utilise) — pas de driver USB dédié. */
static void cap_write_all(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while (len > 0) {
        size_t n = fwrite(p, 1, len, stdout);
        if (n == 0) {
            break;
        }
        p += n;
        len -= n;
    }
    fflush(stdout);
}
#endif

#if MBH_MODE_CAPTURE_AUDIO
static void capture_audio_entry(void *arg)
{
    (void)arg;

    int16_t *buf = heap_caps_malloc(CAP_AUDIO_CHUNK_SAMPLES * 2U * sizeof(int16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "alloc audio chunk failed");
        vTaskDelete(NULL);
        return;
    }

    uint32_t seq = 0;
#if MBH_CAPTURE_BINARY_TO_STDOUT
    ESP_LOGW(TAG,
             "PCM stéréo sur USB (magic %.4s L+R int16) — python3 tools/record_lexa_audio.py …",
             CAP_MAGIC_AUDIO_ST);
#elif MBH_CAPTURE_RAW_PCM_TO_STDOUT
    esp_log_level_set("*", ESP_LOG_ERROR);
    ESP_LOGW(TAG,
             "PCM int16 brut sur stdout (pas de LXCA) — PC : "
             "python3 tools/record_lexa_audio.py --port <tty> --raw-pcm --skip-bytes 4096 -o cap.wav");
#elif MBH_CAPTURE_ASCII_SERIAL_PLOTTER
    esp_log_level_set("*", ESP_LOG_ERROR);
    ESP_LOGI(TAG,
             "ASCII traceur : moyenne int16 sur %d echantillons (~%d Hz), gain_affichage=%d — "
             "fidelite -> capture_audio_host + record_lexa_audio.py",
             MBH_CAPTURE_PLOT_DECIM,
             (MBH_CAPTURE_PLOT_DECIM > 0) ? (16000 / MBH_CAPTURE_PLOT_DECIM) : 0,
             MBH_CAPTURE_PLOT_LINE_GAIN);
#else
    ESP_LOGW(TAG,
             "PCM désactivé sur USB — WAV: capture_audio_host / capture_audio_raw + record_lexa_audio.py ; "
             "graphique: capture_audio_plot + Traceur série Arduino");
#endif

    for (;;) {
        size_t got = i2s_stereo_mic_read_latest_interleaved_lr(buf, CAP_AUDIO_CHUNK_SAMPLES, 200);
#if MBH_CAPTURE_BINARY_TO_STDOUT
        uint8_t hdr[12];
        memcpy(hdr, CAP_MAGIC_AUDIO_ST, 4);
        hdr[4] = (uint8_t)(seq & 0xFF);
        hdr[5] = (uint8_t)((seq >> 8) & 0xFF);
        hdr[6] = (uint8_t)((seq >> 16) & 0xFF);
        hdr[7] = (uint8_t)((seq >> 24) & 0xFF);
        uint32_t ns = (uint32_t)got;
        memcpy(hdr + 8, &ns, sizeof(ns));
        cap_write_all(hdr, sizeof(hdr));
        if (got > 0) {
            cap_write_all(buf, got * sizeof(int16_t));
        }
#elif MBH_CAPTURE_RAW_PCM_TO_STDOUT
        if (got > 0) {
            cap_write_all(buf, got * sizeof(int16_t));
        }
#elif MBH_CAPTURE_ASCII_SERIAL_PLOTTER
        /* Moyenne sur une fenêtre de N échantillons : un seul pic / N (ancien code)
         * donnait des sauts « brutaux » et masquait une sinusoïde (sous-échantillonnage affichage). */
        static int64_t plot_sum;
        static uint32_t plot_n;
        uint32_t need = (uint32_t)MBH_CAPTURE_PLOT_DECIM;
        if (need < 1u) {
            need = 1u;
        }
        for (size_t i = 0; i + 1U < got; i += 2U) {
            plot_sum += (int32_t)buf[i];
            plot_n++;
            if (plot_n >= need) {
                int64_t n64 = (int64_t)plot_n;
                int32_t avg = (int32_t)((plot_sum + n64 / 2) / n64);
                plot_sum = 0;
                plot_n = 0;
#if MBH_CAPTURE_PLOT_LINE_GAIN != 0
                int64_t v = (int64_t)avg * (int64_t)MBH_CAPTURE_PLOT_LINE_GAIN;
                int64_t lim = (int64_t)MBH_CAPTURE_PLOT_LINE_CLAMP;
                if (lim < 1) {
                    lim = 1;
                }
                if (v > lim) {
                    v = lim;
                }
                if (v < -lim) {
                    v = -lim;
                }
                printf("%lld\n", (long long)v);
#else
                printf("%d\n", (int)avg);
#endif
            }
        }
        fflush(stdout);
#else
        /* Log peu fréquent : le mode sans binaire sert au debug série lisible. */
        if ((seq % 500U) == 0U) {
            ESP_LOGI(TAG, "audio seq=%" PRIu32 " samples_lus=%u (pas de PCM sur serial)", seq, (unsigned)got);
        }
#endif
        seq++;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#endif

#if MBH_MODE_CAPTURE_LIDAR
static void capture_lidar_entry(void *arg)
{
    (void)arg;

    const int w = VL53L8CX_ARRAY_FRAME_W;
    const int h = VL53L8CX_ARRAY_FRAME_H;
    float *frame = heap_caps_malloc((size_t)(w * h) * sizeof(float),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!frame) {
        ESP_LOGE(TAG, "alloc lidar frame failed");
        vTaskDelete(NULL);
        return;
    }

    uint32_t seq = 0;
#if MBH_SERIAL_ASCII_TOF_FRAME
    ESP_LOGI(TAG, "ToF ASCII FRAME: sur stdout (read_lexa_tof_frame.py)");
#elif MBH_CAPTURE_BINARY_TO_STDOUT
    ESP_LOGI(TAG, "flux binaire stdout ON (magic %.4s, %dx%d float32 LE)", CAP_MAGIC_L, w, h);
#else
    ESP_LOGI(TAG, "flux binaire stdout OFF — tty lisible ; outil hôte: pio run -e capture_lidar_host");
#endif

    for (;;) {
#if MBH_SERIAL_ASCII_TOF_FRAME
        int16_t mm[VL53L8CX_ARRAY_FRAME_W * VL53L8CX_ARRAY_FRAME_H];
        if (vl53l8cx_array_read_frame(frame, mm, w, h, 100) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        lexa_tof_frame_emit_ascii_line(mm, w, h);
#elif MBH_CAPTURE_BINARY_TO_STDOUT
        if (vl53l8cx_array_read_frame(frame, NULL, w, h, 100) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        uint8_t hdr[16];
        memcpy(hdr, CAP_MAGIC_L, 4);
        memcpy(hdr + 4, &seq, sizeof(seq));
        int32_t ww = w, hh = h;
        memcpy(hdr + 8, &ww, sizeof(ww));
        memcpy(hdr + 12, &hh, sizeof(hh));
        cap_write_all(hdr, sizeof(hdr));
        cap_write_all(frame, (size_t)(w * h) * sizeof(float));
#else
        if (vl53l8cx_array_read_frame(frame, NULL, w, h, 100) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if ((seq % 20U) == 0U) {
            ESP_LOGI(TAG, "lidar seq=%" PRIu32 " frame %dx%d", seq, w, h);
        }
#endif
        seq++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
#endif

void task_capture_start(void)
{
#if MBH_MODE_CAPTURE_AUDIO
    BaseType_t ok = xTaskCreatePinnedToCore(
        capture_audio_entry, "cap_audio", 8192, NULL, 4, NULL, 0);
    configASSERT(ok == pdPASS);
#elif MBH_MODE_CAPTURE_LIDAR
    BaseType_t ok = xTaskCreatePinnedToCore(
        capture_lidar_entry, "cap_lidar", 8192, NULL, 4, NULL, 1);
    configASSERT(ok == pdPASS);
#else
    /* Autres modes : pas de capture */
#endif
}
