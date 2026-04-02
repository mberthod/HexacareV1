/**
 * @file pc_diag.c
 * @ingroup group_pc_diag
 * @brief Interface PC USB-JTAG — Rapport JSON temps réel (Lexacare Studio).
 *
 * Émet deux types de trames JSON vers le PC via USB Serial/JTAG :
 *
 * TRAME SENSOR (toutes les 1 s — données temps réel) :
 *   {
 *     "type": "sensor",
 *     "lidar_matrix": [[...8 rangées × 32 colonnes (mm)...]],
 *     "radar": {
 *       "resp_phase": 0.5, "heart_phase": 0.1,
 *       "breath_rate": 16, "heart_rate": 72,
 *       "distance_mm": 1200, "presence": true
 *     },
 *     "ai_event": "AI_NORMAL",
 *     "confidence": 0,
 *     "hdc1080": { "temp_c": 23.5, "humidity_pct": 45.2 },
 *     "bme280":  { "temp_c": 23.2, "pressure_hpa": 1013.2, "humidity_pct": 44.8 },
 *     "mic":     { "rms": 1234, "peak": 4096 }
 *   }
 *
 * TRAME SYS (toutes les 5 s — métriques firmware) :
 *   {
 *     "type": "sys",
 *     "uptime_s": 1234,
 *     "cpu_load": [45, 80],
 *     "heap_free": 8700000,
 *     "psram_free": 8000000,
 *     "tasks_hwm": { "Task_Sensor_Acq": 2048, ... }
 *   }
 *
 * Commandes acceptées depuis stdin (JSON + '\n') :
 *   {"cmd":"get_diag"}        → rapport JSON immédiat
 *   {"cmd":"download_logs"}   → transfert /spiffs/app.log
 *   {"cmd":"clear_logs"}      → suppression /spiffs/app.log
 */

#include "pc_diag.h"
#include "sys_storage.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static const char *TAG = "pc_diag";

/* ================================================================
 * Constantes de la tâche
 * ================================================================ */
#define TASK_STACK_SIZE         (12288)  /* Augmenté pour JSON LIDAR 8×32 + cJSON heap */
#define TASK_PRIORITY           (3)
#define TASK_CORE               (0)
#define SENSOR_INTERVAL_MS      (1000)   /* Rapport sensor : 1 s */
#define SYS_INTERVAL_MS         (5000)   /* Rapport sys    : 5 s */
#define CMD_BUF_SIZE            (256)
#define LOG_TRANSFER_BLOCK_SIZE (512)
#define MAX_TASKS               (32)

/* Mapping état IA (entier legacy → string cible) */
static const char *ai_event_strings[] = {
    "AI_NORMAL",
    "AI_CHUTE_DETECTEE",
    "AI_MOUVEMENT_ANORMAL",
};
#define AI_STATE_COUNT  (sizeof(ai_event_strings) / sizeof(ai_event_strings[0]))

/* ================================================================
 * pc_diag_ctx_t (interne)
 * ================================================================ */
typedef struct {
    sys_context_t *sys_ctx;
    ai_event_t     last_event;
    bool           has_event;
} pc_diag_ctx_t;

/* ================================================================
 * build_sensor_json (interne)
 * @brief Construit la trame JSON "type:sensor" depuis le snapshot
 *        partagé de sys_context_t.
 *
 * @return Chaîne JSON allouée (libérer avec cJSON_free), ou NULL.
 * ================================================================ */
static char *build_sensor_json(pc_diag_ctx_t *ctx)
{
    sys_context_t *sys = ctx->sys_ctx;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "type", "sensor");

    /* ── Matrice LIDAR 8×32 ──────────────────────────────────────── */
    sensor_frame_t snap = {0};
    bool snap_valid = false;

    if (sys->data_mutex &&
        xSemaphoreTake(sys->data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        snap       = sys->latest_sensor;
        snap_valid = sys->sensor_valid;
        xSemaphoreGive(sys->data_mutex);
    }

    cJSON *matrix = cJSON_CreateArray();
    for (int row = 0; row < LIDAR_ROWS; row++) {
        cJSON *row_arr = cJSON_CreateArray();
        for (int col = 0; col < LIDAR_COLS; col++) {
            uint16_t d = snap_valid ? snap.lidar.data[row][col] : 0;
            cJSON_AddItemToArray(row_arr, cJSON_CreateNumber(d));
        }
        cJSON_AddItemToArray(matrix, row_arr);
    }
    cJSON_AddItemToObject(root, "lidar_matrix", matrix);

    /* ── Radar ───────────────────────────────────────────────────── */
    cJSON *radar = cJSON_CreateObject();
    if (snap_valid && snap.radar_valid) {
        cJSON_AddNumberToObject(radar, "resp_phase",  snap.radar.resp_phase);
        cJSON_AddNumberToObject(radar, "heart_phase", snap.radar.heart_phase);
        cJSON_AddNumberToObject(radar, "breath_rate", snap.radar.breath_rate_bpm);
        cJSON_AddNumberToObject(radar, "heart_rate",  snap.radar.heart_rate_bpm);
        cJSON_AddNumberToObject(radar, "distance_mm", snap.radar.target_distance_mm);
        cJSON_AddBoolToObject  (radar, "presence",    snap.radar.presence);
    } else {
        cJSON_AddNumberToObject(radar, "resp_phase",  0.0);
        cJSON_AddNumberToObject(radar, "heart_phase", 0.0);
        cJSON_AddNumberToObject(radar, "breath_rate", 0);
        cJSON_AddNumberToObject(radar, "heart_rate",  0);
        cJSON_AddNumberToObject(radar, "distance_mm", 0);
        cJSON_AddBoolToObject  (radar, "presence",    false);
    }
    cJSON_AddItemToObject(root, "radar", radar);

    /* ── Événement IA ────────────────────────────────────────────── */
    /* Récupérer le dernier événement depuis la queue de diagnostic */
    ai_event_t evt = ctx->last_event;
    while (xQueueReceive(sys->diag_to_pc_queue, &evt, 0) == pdTRUE) {
        ctx->last_event = evt;
        ctx->has_event  = true;
    }
    uint32_t ai_state = (ctx->has_event) ? ctx->last_event.state : 0;
    const char *ai_str = (ai_state < AI_STATE_COUNT)
                         ? ai_event_strings[ai_state]
                         : "AI_NORMAL";
    int confidence = (ctx->has_event) ? (int)ctx->last_event.confidence : 0;
    cJSON_AddStringToObject(root, "ai_event",   ai_str);
    cJSON_AddNumberToObject(root, "confidence", confidence);

    /* ── HDC1080 ─────────────────────────────────────────────────── */
    cJSON *hdc = cJSON_CreateObject();
    if (snap_valid) {
        enviro_data_t env;
        mic_data_t    mic_data;
        if (sys->data_mutex &&
            xSemaphoreTake(sys->data_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            env      = sys->enviro;
            mic_data = sys->mic;
            xSemaphoreGive(sys->data_mutex);
        } else {
            memset(&env,      0, sizeof(env));
            memset(&mic_data, 0, sizeof(mic_data));
        }

        if (!isnan(env.temp_hdc_c))
            cJSON_AddNumberToObject(hdc, "temp_c",       (double)env.temp_hdc_c);
        if (!isnan(env.humidity_hdc))
            cJSON_AddNumberToObject(hdc, "humidity_pct", (double)env.humidity_hdc);

        /* ── BME280 ─────────────────────────────────────────────────── */
        cJSON *bme = cJSON_CreateObject();
        if (!isnan(env.temp_bme_c))
            cJSON_AddNumberToObject(bme, "temp_c",       (double)env.temp_bme_c);
        if (!isnan(env.pressure_hpa))
            cJSON_AddNumberToObject(bme, "pressure_hpa", (double)env.pressure_hpa);
        if (!isnan(env.humidity_bme))
            cJSON_AddNumberToObject(bme, "humidity_pct", (double)env.humidity_bme);
        cJSON_AddItemToObject(root, "bme280", bme);

        /* ── Microphone ─────────────────────────────────────────────── */
        cJSON *mic = cJSON_CreateObject();
        cJSON_AddNumberToObject(mic, "rms",  (double)mic_data.rms);
        cJSON_AddNumberToObject(mic, "peak", (double)mic_data.peak);
        cJSON_AddItemToObject(root, "mic", mic);
    }
    cJSON_AddItemToObject(root, "hdc1080", hdc);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

/* ================================================================
 * build_sys_json (interne)
 * @brief Construit la trame JSON "type:sys".
 * ================================================================ */
static char *build_sys_json(pc_diag_ctx_t *ctx)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "type", "sys");

    int64_t uptime_us = esp_timer_get_time();
    cJSON_AddNumberToObject(root, "uptime_s", (double)(uptime_us / 1000000LL));

    /* cpu_load : non fourni nativement par ESP-IDF sans FreeRTOS runtime stats.
     * On expose [0, 0] en attendant l'intégration de CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS. */
    cJSON *cpu_load = cJSON_CreateArray();
    cJSON_AddItemToArray(cpu_load, cJSON_CreateNumber(0));
    cJSON_AddItemToArray(cpu_load, cJSON_CreateNumber(0));
    cJSON_AddItemToObject(root, "cpu_load", cpu_load);

    cJSON_AddNumberToObject(root, "heap_free",
                             (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "psram_free",
                             (double)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* Tâches FreeRTOS — High Water Mark */
    TaskStatus_t task_statuses[MAX_TASKS];
    UBaseType_t  task_count = uxTaskGetSystemState(task_statuses, MAX_TASKS, NULL);

    cJSON *tasks_hwm = cJSON_CreateObject();
    for (UBaseType_t i = 0; i < task_count; i++) {
        /* HWM en mots (4 octets) → convertir en octets */
        uint32_t hwm_bytes = (uint32_t)task_statuses[i].usStackHighWaterMark
                             * sizeof(StackType_t);
        cJSON_AddNumberToObject(tasks_hwm,
                                 task_statuses[i].pcTaskName,
                                 (double)hwm_bytes);
    }
    cJSON_AddItemToObject(root, "tasks_hwm", tasks_hwm);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

/* ================================================================
 * cmd_download_logs / cmd_clear_logs (interne) — inchangés
 * ================================================================ */
static void cmd_download_logs(void)
{
    FILE *f = fopen(LOG_FILE_PATH, "r");
    if (!f) {
        printf("{\"status\":\"error\",\"msg\":\"Fichier log introuvable\"}\n");
        fflush(stdout);
        return;
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);
    printf("{\"status\":\"ok\",\"filename\":\"app.log\",\"size\":%ld}\n", file_size);
    fflush(stdout);
    uint8_t block[LOG_TRANSFER_BLOCK_SIZE];
    size_t  rb;
    while ((rb = fread(block, 1, sizeof(block), f)) > 0) {
        fwrite(block, 1, rb, stdout);
    }
    fclose(f);
    printf("\n{\"status\":\"done\"}\n");
    fflush(stdout);
    ESP_LOGI(TAG, "Transfert log terminé (%ld octets)", file_size);
}

static void cmd_clear_logs(void)
{
    int ret = remove(LOG_FILE_PATH);
    if (ret == 0) {
        printf("{\"status\":\"ok\",\"msg\":\"Log effacé\"}\n");
        ESP_LOGI(TAG, "Fichier log supprimé");
    } else {
        printf("{\"status\":\"error\",\"msg\":\"Échec suppression\"}\n");
    }
    fflush(stdout);
}

/* ================================================================
 * process_command (interne)
 * ================================================================ */
static void process_command(pc_diag_ctx_t *ctx, const char *line)
{
    cJSON *cmd_json = cJSON_Parse(line);
    if (!cmd_json) {
        ESP_LOGW(TAG, "Commande JSON invalide : %s", line);
        return;
    }
    const cJSON *cmd = cJSON_GetObjectItem(cmd_json, "cmd");
    if (!cJSON_IsString(cmd)) {
        cJSON_Delete(cmd_json);
        return;
    }
    const char *cmd_str = cmd->valuestring;
    ESP_LOGI(TAG, "Commande reçue : %s", cmd_str);

    if (strcmp(cmd_str, "download_logs") == 0) {
        cmd_download_logs();
    } else if (strcmp(cmd_str, "clear_logs") == 0) {
        cmd_clear_logs();
    } else if (strcmp(cmd_str, "get_diag") == 0) {
        char *json = build_sys_json(ctx);
        if (json) { printf("%s\n", json); fflush(stdout); cJSON_free(json); }
    } else {
        printf("{\"status\":\"error\",\"msg\":\"Commande inconnue : %s\"}\n", cmd_str);
        fflush(stdout);
    }
    cJSON_Delete(cmd_json);
}

/* ================================================================
 * task_diag_pc (interne)
 * @brief Tâche FreeRTOS — Core 0, priorité 3.
 * ================================================================ */
static void task_diag_pc(void *pvParam)
{
    pc_diag_ctx_t *ctx = (pc_diag_ctx_t *)pvParam;
    ESP_LOGI(TAG, "Task_Diag_PC démarrée sur Core %d", xPortGetCoreID());

    /* ── Stdin non-bloquant (IDF 6.0 / USB CDC) ──────────────────────
     * fgets() peut bloquer indéfiniment sur USB JTAG en ESP-IDF 6.0.
     * select() peut déclencher des allocations VFS récurrentes (esp_vfs_select)
     * qui, sur certains builds, exposent des asserts TLSF.
     * Stratégie : O_NONBLOCK + read() brut + parsing en mémoire. */
    const int stdin_fd = fileno(stdin);
    {
        /* Tentative de mise en mode O_NONBLOCK (peut échouer sur certains VFS) */
        int fl = fcntl(stdin_fd, F_GETFL, 0);
        if (fl >= 0) fcntl(stdin_fd, F_SETFL, fl | O_NONBLOCK);
    }

    static char cmd_buf[CMD_BUF_SIZE];   /* statique pour économiser le stack */
    static int  cmd_len = 0;

    TickType_t last_sensor = xTaskGetTickCount();
    TickType_t last_sys    = xTaskGetTickCount();

    ESP_LOGI(TAG, "JSON démarré : sensor@%d s, sys@%d s",
             SENSOR_INTERVAL_MS / 1000, SYS_INTERVAL_MS / 1000);

    while (true) {
        TickType_t now = xTaskGetTickCount();

        /* ── Lecture commandes stdin (NON BLOQUANT) ───────────────────
         * read() non bloquant : retourne -1/EAGAIN si rien à lire.      */
        {
            char rx[64];
            ssize_t r = read(stdin_fd, rx, sizeof(rx));
            if (r > 0) {
                for (ssize_t i = 0; i < r; i++) {
                    const char c = rx[i];
                    if (c == '\r') continue;
                    if (c == '\n' || cmd_len >= CMD_BUF_SIZE - 1) {
                        cmd_buf[cmd_len] = '\0';
                        if (cmd_len > 2) process_command(ctx, cmd_buf);
                        cmd_len = 0;
                    } else {
                        cmd_buf[cmd_len++] = c;
                    }
                }
            } else if (r < 0) {
                if (!(errno == EAGAIN || errno == EWOULDBLOCK)) {
                    ESP_LOGW(TAG, "read(stdin) échoué : errno=%d", errno);
                }
            }
        }

        /* ── Rapport SENSOR (@SENSOR_INTERVAL_MS) ────────────────────
         * Émis en JSON une ligne : {"type":"sensor","lidar_matrix":…}  */
        if ((now - last_sensor) >= pdMS_TO_TICKS(SENSOR_INTERVAL_MS)) {
            last_sensor = now;
            char *json = build_sensor_json(ctx);
            if (json) {
                printf("%s\n", json);
                fflush(stdout);
                cJSON_free(json);
            } else {
                ESP_LOGW(TAG, "build_sensor_json : allocation échouée (heap ?)");
            }
        }

        /* ── Rapport SYS (@SYS_INTERVAL_MS) ─────────────────────────
         * Émis en JSON une ligne : {"type":"sys","cpu_load":…}         */
        if ((now - last_sys) >= pdMS_TO_TICKS(SYS_INTERVAL_MS)) {
            last_sys = now;
            char *json = build_sys_json(ctx);
            if (json) {
                printf("%s\n", json);
                fflush(stdout);
                cJSON_free(json);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50)); /* Cède le CPU — 50 ms = 20 itérations/s */
    }
}

/* ================================================================
 * pc_diag_task_start
 * ================================================================ */
esp_err_t pc_diag_task_start(sys_context_t *ctx)
{
    pc_diag_ctx_t *task_ctx = calloc(1, sizeof(pc_diag_ctx_t));
    if (!task_ctx) return ESP_ERR_NO_MEM;
    task_ctx->sys_ctx  = ctx;
    task_ctx->has_event = false;

    BaseType_t ret = xTaskCreatePinnedToCore(
        task_diag_pc,
        "Task_Diag_PC",
        TASK_STACK_SIZE,
        task_ctx,
        TASK_PRIORITY,
        NULL,
        TASK_CORE);

    if (ret != pdPASS) {
        free(task_ctx);
        ESP_LOGE(TAG, "Échec création Task_Diag_PC");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Task_Diag_PC créée (Core %d, prio %d, sensor@1Hz sys@5Hz)",
             TASK_CORE, TASK_PRIORITY);
    return ESP_OK;
}
