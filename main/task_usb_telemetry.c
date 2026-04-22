/* Télémétrie USB : enveloppe LXJS + JSON schéma v3 (audio, lidar mm, thermique). */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "app_config.h"
#include "lexa_config.h"
#include "usb_telemetry.h"
#include "sensors_board.h"
#include "vl53l8cx_array.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#if MBH_USB_TELEMETRY_STREAM

static const char *TAG = "usb_telem";

static char *s_json_buf;
static size_t s_json_cap;

#define MAGIC_JS "LXJS"
#define PCM_MAX_PAIRS 512

typedef struct {
    int16_t samples[PCM_MAX_PAIRS * 2];
    uint32_t n_pairs;
    uint32_t seq;
} usb_pcm_msg_t;

typedef struct {
    float cells[VL53L8CX_ARRAY_FRAME_W * VL53L8CX_ARRAY_FRAME_H];
    uint32_t seq;
} usb_lidar_msg_t;

static QueueHandle_t s_q_pcm;
static QueueHandle_t s_q_lidar;
static uint32_t s_pcm_drop;
static uint32_t s_ld_drop;

/** Dernière trame PCM connue ; matrice lidar (float 0–1) dans s_last_ld (zéro si jamais reçue). */
static usb_pcm_msg_t s_last_pcm;
static bool s_have_pcm;
static usb_lidar_msg_t s_last_ld;

static void log_drops_if_needed(void)
{
    static uint32_t last_log;
    uint32_t d = s_pcm_drop + s_ld_drop;
    if (d != last_log && d != 0 && (d % 32u) == 0u) {
        ESP_LOGE(TAG, "files USB pleines (drops pcm=%u lidar=%u)",
                 (unsigned)s_pcm_drop, (unsigned)s_ld_drop);
        last_log = d;
    }
}

static bool json_append(char *buf, size_t cap, int *po, const char *fmt, ...)
{
    if (*po < 0 || (size_t)*po >= cap) {
        return false;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *po, cap - (size_t)*po, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - (size_t)*po) {
        return false;
    }
    *po += n;
    return true;
}

static void write_stdout(const void *data, size_t len)
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

/** Préfixe magique 4 octets + corps JSON (UTF-8). */
static void emit_lxjs_json_line(char *line, int json_body_len)
{
    if (json_body_len <= 0 || !s_json_buf || s_json_cap == 0) {
        return;
    }
    if ((size_t)json_body_len + 4U > s_json_cap) {
        return;
    }
    memmove(line + 4, line, (size_t)json_body_len);
    memcpy(line, MAGIC_JS, 4);
    write_stdout(line, (size_t)(json_body_len + 4));
}

static bool append_audio_lr_arrays(char *line, size_t line_cap, int *o,
                                   const int16_t *left, const int16_t *right, size_t n)
{
    if (!json_append(line, line_cap, o, ",\"audio_left\":[")) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (!json_append(line, line_cap, o, i ? ",%d" : "%d", (int)left[i])) {
            return false;
        }
    }
    if (!json_append(line, line_cap, o, "],\"audio_right\":[")) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (!json_append(line, line_cap, o, i ? ",%d" : "%d", (int)right[i])) {
            return false;
        }
    }
    return json_append(line, line_cap, o, "]");
}

static bool append_lidar_matrix_mm(char *line, size_t line_cap, int *o, const float *cells,
                                   size_t ncells)
{
    if (!json_append(line, line_cap, o,
                      ",\"lidar_matrix\":{\"rows\":%d,\"cols\":%d,\"unit\":\"mm\","
                      "\"dtype\":\"uint16\",\"order\":\"row_major\",\"data\":[",
                      VL53L8CX_ARRAY_FRAME_H, VL53L8CX_ARRAY_FRAME_W)) {
        return false;
    }
    for (size_t i = 0; i < ncells; i++) {
        float c = cells[i];
        if (isnan((double)c) || isinf((double)c)) {
            c = 0.f;
        } else if (c < 0.f) {
            c = 0.f;
        } else if (c > 1.f) {
            c = 1.f;
        }
        long mm = lroundf(c * (float)LEXA_USB_JSON_LIDAR_MM_MAX);
        if (mm < 0) {
            mm = 0;
        }
        if (mm > (long)LEXA_USB_JSON_LIDAR_MM_MAX) {
            mm = (long)LEXA_USB_JSON_LIDAR_MM_MAX;
        }
        if (!json_append(line, line_cap, o, i ? ",%u" : "%u", (unsigned)mm)) {
            return false;
        }
    }
    return json_append(line, line_cap, o, "]}");
}

static bool append_thermal_zeros(char *line, size_t line_cap, int *o)
{
    if (!json_append(line, line_cap, o,
                      ",\"thermal_image\":{\"rows\":%d,\"cols\":%d,\"unit\":\"deg_c\","
                      "\"dtype\":\"float32\",\"order\":\"row_major\",\"data\":[",
                      LEXA_MLX_THERMAL_ROWS, LEXA_MLX_THERMAL_COLS)) {
        return false;
    }
    for (int i = 0; i < LEXA_MLX_THERMAL_CELLS; i++) {
        if (!json_append(line, line_cap, o, i ? ",0.0" : "0.0")) {
            return false;
        }
    }
    return json_append(line, line_cap, o, "]}");
}

static void task_usb_telemetry_entry(void *arg)
{
    (void)arg;
    uint32_t json_seq = 0;
    int64_t t0 = esp_timer_get_time();

    esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set("main_task", ESP_LOG_ERROR);

    ESP_LOGD(TAG, "flux USB GUI : LXJS JSON v3 (debit USB eleve — fermer le moniteur serie IDE)");

    for (;;) {
        while (s_q_pcm && xQueueReceive(s_q_pcm, &s_last_pcm, 0) == pdPASS) {
            s_have_pcm = true;
        }
        while (s_q_lidar && xQueueReceive(s_q_lidar, &s_last_ld, 0) == pdPASS) {
            /* conserve la dernière trame */
        }

        int64_t now = esp_timer_get_time();
        log_drops_if_needed();

        if ((now - t0) >= 1000000) {
            t0 = now;
            sensors_board_snapshot_t sb;
            sensors_board_get_cached(&sb);
            int ms = (int)(now / 1000);
            uint32_t seq = json_seq++;

            if (!s_json_buf || s_json_cap < 4096U) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }

            char *line = s_json_buf;
            const size_t line_cap = s_json_cap;
            int o = 0;
            bool ok = json_append(
                line,
                line_cap,
                &o,
                "{\"lexa_telem_schema_version\":3,\"lexa_usb_telemetry_envelope\":\"LXJS\","
                "\"uptime_ms\":%d,\"t_ms\":%d,\"json_seq\":%u,"
                "\"vision_fall_pct\":0.0,\"audio_fall_pct\":0.0,"
                "\"conn_ds3231\":%d,\"conn_cat24\":%d,\"conn_bme280\":%d,"
                "\"conn_hdc1080\":%d,\"conn_tmp117\":%d,\"conn_vl53l0\":%d,\"conn_mlx90640\":%d",
                ms,
                ms,
                (unsigned)seq,
                sb.conn_ds3231,
                sb.conn_cat24,
                sb.conn_bme280,
                sb.conn_hdc1080,
                sb.conn_tmp117,
                sb.conn_vl53l0,
                sb.conn_mlx90640);
            if (ok) {
                if (sb.conn_ds3231 && !isnan((double)sb.ds3231_temp_c)) {
                    ok = json_append(line, line_cap, &o, ",\"ds3231_temp_c\":%.4f",
                                     (double)sb.ds3231_temp_c);
                } else {
                    ok = json_append(line, line_cap, &o, ",\"ds3231_temp_c\":null");
                }
            }
            if (ok) {
                if (sb.conn_hdc1080 && !isnan((double)sb.hdc1080_temp_c)) {
                    ok = json_append(line, line_cap, &o, ",\"hdc1080_temp_c\":%.4f",
                                     (double)sb.hdc1080_temp_c);
                } else {
                    ok = json_append(line, line_cap, &o, ",\"hdc1080_temp_c\":null");
                }
            }
            if (ok) {
                if (sb.conn_hdc1080 && !isnan((double)sb.hdc1080_rh_pct)) {
                    ok = json_append(line, line_cap, &o, ",\"hdc1080_rh_pct\":%.3f",
                                     (double)sb.hdc1080_rh_pct);
                } else {
                    ok = json_append(line, line_cap, &o, ",\"hdc1080_rh_pct\":null");
                }
            }
            if (ok) {
                if (sb.conn_bme280 && !isnan((double)sb.bme280_temp_c)) {
                    ok = json_append(line, line_cap, &o, ",\"bme280_temp_c\":%.4f",
                                     (double)sb.bme280_temp_c);
                } else {
                    ok = json_append(line, line_cap, &o, ",\"bme280_temp_c\":null");
                }
            }
            if (ok) {
                if (sb.conn_bme280 && !isnan((double)sb.bme280_h_pa)) {
                    ok = json_append(line, line_cap, &o, ",\"bme280_h_pa\":%.2f", (double)sb.bme280_h_pa);
                } else {
                    ok = json_append(line, line_cap, &o, ",\"bme280_h_pa\":null");
                }
            }
            if (ok) {
                if (sb.conn_bme280 && !isnan((double)sb.bme280_rh_pct)) {
                    ok = json_append(line, line_cap, &o, ",\"bme280_rh_pct\":%.3f",
                                     (double)sb.bme280_rh_pct);
                } else {
                    ok = json_append(line, line_cap, &o, ",\"bme280_rh_pct\":null");
                }
            }
            if (ok) {
                if (sb.conn_tmp117 && !isnan((double)sb.tmp117_temp_c)) {
                    ok = json_append(line, line_cap, &o, ",\"tmp117_temp_c\":%.4f",
                                     (double)sb.tmp117_temp_c);
                } else {
                    ok = json_append(line, line_cap, &o, ",\"tmp117_temp_c\":null");
                }
            }

            int16_t al[LEXA_USB_JSON_AUDIO_SAMPLES];
            int16_t ar[LEXA_USB_JSON_AUDIO_SAMPLES];
            size_t n_audio = 0;
            if (ok && s_have_pcm && s_last_pcm.n_pairs > 0) {
                size_t np = s_last_pcm.n_pairs;
                size_t take = np;
                if (take > LEXA_USB_JSON_AUDIO_SAMPLES) {
                    take = LEXA_USB_JSON_AUDIO_SAMPLES;
                }
                size_t base = (np - take) * 2U;
                n_audio = take;
                for (size_t k = 0; k < take; k++) {
                    al[k] = s_last_pcm.samples[base + 2U * k];
                    ar[k] = s_last_pcm.samples[base + 2U * k + 1U];
                }
            }
            if (ok) {
                if (n_audio > 0) {
                    ok = append_audio_lr_arrays(line, line_cap, &o, al, ar, n_audio);
                } else {
                    ok = append_audio_lr_arrays(line, line_cap, &o, al, ar, 0);
                }
            }

            const float *ld_cells = s_last_ld.cells;
            if (ok) {
                ok = append_lidar_matrix_mm(line, line_cap, &o, ld_cells,
                                            (size_t)(VL53L8CX_ARRAY_FRAME_W * VL53L8CX_ARRAY_FRAME_H));
            }
            if (ok) {
                ok = append_thermal_zeros(line, line_cap, &o);
            }

            if (ok) {
                ok = json_append(line, line_cap, &o, "}\n");
            }
            if (!ok) {
                snprintf(line, line_cap,
                         "{\"lexa_telem_schema_version\":3,\"lexa_usb_telemetry_envelope\":\"LXJS\","
                         "\"uptime_ms\":%d,\"t_ms\":%d,\"lexa_json_err\":\"overflow\",\"json_seq\":%u}\n",
                         ms, ms, (unsigned)seq);
                o = (int)strlen(line);
            }
            emit_lxjs_json_line(line, o);
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/** Réserve tôt une zone contiguë (avant WiFi/mesh) : PSRAM d’abord pour ne pas grappiller la DRAM interne (WiFi / DMA). */
static bool ensure_json_line_buffer(void)
{
    if (s_json_buf) {
        return true;
    }
    static const size_t k_sizes[] = {
        LEXA_USB_JSON_BUF_BYTES,
        96u * 1024u,
        64u * 1024u,
        48u * 1024u,
        32u * 1024u,
        24u * 1024u,
        16u * 1024u,
    };
    for (size_t zi = 0; zi < sizeof(k_sizes) / sizeof(k_sizes[0]); zi++) {
        size_t z = k_sizes[zi];
        void *p = heap_caps_malloc(z, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        const char *where = "PSRAM";
        if (!p) {
            /* WiFi / buffers statiques utilisent surtout la DRAM interne : plafonner le repli. */
            if (z > 32u * 1024u) {
                continue;
            }
            p = heap_caps_malloc(z, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            where = "DRAM interne";
        }
        if (p) {
            s_json_buf = (char *)p;
            s_json_cap = z;
            ESP_LOGI(TAG, "tampon JSON LXJS : %u o (%s)", (unsigned)z, where);
            return true;
        }
    }
    ESP_LOGW(
        TAG,
        "alloc JSON impossible (plus gros bloc interne %u o) — LXJS désactivé",
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return false;
}

void usb_telemetry_init(void)
{
    if (!s_q_pcm) {
        s_q_pcm = xQueueCreate(3, sizeof(usb_pcm_msg_t));
    }
    if (!s_q_lidar) {
        s_q_lidar = xQueueCreate(2, sizeof(usb_lidar_msg_t));
    }
    (void)ensure_json_line_buffer();
}

void usb_telemetry_start(void)
{
    usb_telemetry_init();
    if (!ensure_json_line_buffer()) {
        ESP_LOGE(TAG, "init usb telem (pcm=%p lidar=%p json=%p)",
                 (void *)s_q_pcm, (void *)s_q_lidar, (void *)s_json_buf);
        return;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(
        task_usb_telemetry_entry, "usb_telem", 12288, NULL, 3, NULL, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "creation tache usb_telem");
        return;
    }
    sensors_board_usb_snapshot_start();
}

void usb_telemetry_post_vision(const float *frame, int w, int h)
{
    if (!s_q_lidar || !frame || w != VL53L8CX_ARRAY_FRAME_W
        || h != VL53L8CX_ARRAY_FRAME_H) {
        return;
    }
    static uint32_t seq;
    usb_lidar_msg_t msg;
    memcpy(msg.cells, frame, sizeof(msg.cells));
    msg.seq = seq++;
    if (xQueueSend(s_q_lidar, &msg, 0) != pdPASS) {
        s_ld_drop++;
    }
}

void usb_telemetry_enqueue_pcm_tail(const int16_t *interleaved_lr, size_t n_pairs,
                                    uint32_t seq)
{
    if (!s_q_pcm || !interleaved_lr || n_pairs == 0) {
        return;
    }
    size_t take = n_pairs;
    if (take > PCM_MAX_PAIRS) {
        take = PCM_MAX_PAIRS;
    }
    usb_pcm_msg_t msg;
    msg.n_pairs = take;
    msg.seq = seq;
    memcpy(msg.samples, interleaved_lr + (n_pairs - take) * 2,
           take * 2U * sizeof(int16_t));
    if (xQueueSend(s_q_pcm, &msg, 0) != pdPASS) {
        s_pcm_drop++;
    }
}

#else /* !MBH_USB_TELEMETRY_STREAM */

void usb_telemetry_init(void) {}

void usb_telemetry_start(void) {}

void usb_telemetry_post_vision(const float *frame, int w, int h)
{
    (void)frame;
    (void)w;
    (void)h;
}

void usb_telemetry_enqueue_pcm_tail(const int16_t *interleaved_lr, size_t n_pairs,
                                    uint32_t seq)
{
    (void)interleaved_lr;
    (void)n_pairs;
    (void)seq;
}

#endif /* MBH_USB_TELEMETRY_STREAM */
