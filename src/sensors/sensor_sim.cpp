/**
 * @file sensor_sim.cpp
 * @brief Génération de trames LexaFullFrame simulées (vitaux, env, edge) sur Core 1.
 * @details Tâche sensorSimulationTask épinglée sur APP_CPU. Période SENSOR_SIM_PERIOD_MS.
 * La ressource partagée s_frame (et s_has_frame) est protégée par s_mutex ; toute écriture
 * (dans la tâche) et toute lecture (sensor_sim_get_latest_frame, appelée depuis Core 0) se font sous mutex.
 */

#include "sensor_sim.h"
#include "config/config.h"
#include "config/pins_lexacare.h"
#include "rtos/queues_events.h"
#include "system/log_dual.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_log.h>
#include <string.h>
#include <math.h>

static const char *TAG_SIM = "SENSOR_SIM";
static TaskHandle_t s_sensor_sim_handle = nullptr;

/** Dernière trame simulée (partagée Core 0 / Core 1). Accès uniquement sous s_mutex. */
static LexaFullFrame_t s_frame;
/** Mutex protégeant s_frame et s_has_frame : prise avant toute lecture/écriture depuis sensorSimulationTask (Core 1) ou sensor_sim_get_latest_frame (Core 0). */
static SemaphoreHandle_t s_mutex = nullptr;
static volatile uint8_t s_has_frame = 0;
static uint32_t s_tick = 0;
static uint8_t s_prob_fall_lidar = 0;
static uint32_t s_next_fall_event = 0;

static void get_node_short_id(uint16_t *out) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    *out = (uint16_t)((mac[4] << 8) | mac[5]);
}

static void fill_frame(LexaFullFrame_t *f) {
    memset(f, 0, sizeof(LexaFullFrame_t));
    get_node_short_id(&f->nodeShortId);

    /* Epoch simulé (offset depuis boot) */
    f->epoch = (uint32_t)(millis() / 1000) + 1700000000u;

    /* Signes vitaux : oscillation lente (période ~20–30 s) */
    float t = (float)s_tick * 0.001f;
    int hr = 88 + (int)(12.0f * sinf(t * 0.2f)) + (esp_random() % 5) - 2;
    int rr = 18 + (int)(4.0f * sinf(t * 0.15f)) + (esp_random() % 3) - 1;
    if (hr < 60) hr = 60;
    if (hr > 120) hr = 120;
    if (rr < 12) rr = 12;
    if (rr > 25) rr = 25;
    f->heartRate = (uint8_t)hr;
    f->respRate = (uint8_t)rr;

    /* Environnement : 22.5°C, 45%, 1013 hPa + bruit */
    f->tempExt = 2250 + (int16_t)(esp_random() % 41) - 20;
    f->humidity = 4500 + (uint16_t)(esp_random() % 101) - 50;
    f->pressure = 2130 + (uint16_t)(esp_random() % 21) - 10;

    /* Edge : probFallLidar 0 -> 0–95% toutes les ~30 s */
    if (s_tick >= s_next_fall_event) {
        s_prob_fall_lidar = (uint8_t)(esp_random() % 96);
        s_next_fall_event = s_tick + 30 + (esp_random() % 10);
    }
    f->probFallLidar = s_prob_fall_lidar;
    f->probFallAudio = (uint8_t)(s_prob_fall_lidar > 0 ? (esp_random() % (s_prob_fall_lidar + 1)) : 0);

    /* thermalMax et volumeOccupancy synchronisés (présence au lit) */
    uint8_t occupancy = (uint8_t)(128 + (int)(80.0f * sinf(t * 0.1f)) + (esp_random() % 20) - 10);
    if (occupancy > 255) occupancy = 255;
    f->volumeOccupancy = occupancy;
    /* thermalMax plus élevé quand volume élevé */
    int16_t thermal = 3200 + (int16_t)((int)f->volumeOccupancy - 128);
    if (thermal < 2500) thermal = 2500;
    if (thermal > 3800) thermal = 3800;
    f->thermalMax = thermal;

    /* Batterie simulée 3700–4200 mV */
    f->vBat = (uint16_t)(3700 + (esp_random() % 501));

    /* sensorFlags : bits simulés (ex. lidar=0, radar=1, audio=2, env=3) */
    f->sensorFlags = 0x000F;

    lexaframe_fill_crc(f);
}

static void sensorSimulationTask(void *pvParameters) {
    (void)pvParameters;
    log_dual_println("[TASK] sensorSim running (Core 1)");
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        LexaFullFrame_t frame_to_send;
        if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            fill_frame(&s_frame);
            s_has_frame = 1;
            memcpy(&frame_to_send, &s_frame, sizeof(LexaFullFrame_t));
            xSemaphoreGive(s_mutex);
        } else if (!s_mutex) {
            fill_frame(&s_frame);
            s_has_frame = 1;
            memcpy(&frame_to_send, &s_frame, sizeof(LexaFullFrame_t));
        } else {
            vTaskDelayUntil(&last, pdMS_TO_TICKS(SENSOR_SIM_PERIOD_MS));
            last = xTaskGetTickCount();
            continue;
        }
        s_tick++;
        if (g_queue_espnow_tx)
            xQueueSend(g_queue_espnow_tx, &frame_to_send, 0);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SENSOR_SIM_PERIOD_MS));
        last = xTaskGetTickCount();
    }
}

void sensor_sim_task_start(void) {
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
    }
    BaseType_t ok = xTaskCreatePinnedToCore(
        sensorSimulationTask,
        "sensorSim",
        SENSOR_SIM_TASK_STACK,
        nullptr,
        2,
        nullptr,
        (BaseType_t)CORE_APP
    );
    if (ok == pdPASS)
        ESP_LOGI(TAG_SIM, "Task sensorSim demarree (Core %d, periode %d ms)", (int)CORE_APP, (int)SENSOR_SIM_PERIOD_MS);
    else
        ESP_LOGE(TAG_SIM, "Echec creation task sensorSim");
}

TaskHandle_t sensor_sim_get_task_handle(void) {
    return s_sensor_sim_handle;
}

int sensor_sim_get_latest_frame(LexaFullFrame_t *frame_out) {
    if (!frame_out) return 0;
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return 0;
    /* Section critique : lecture s_frame sous mutex (appelé depuis Core 0). */
    int has = s_has_frame ? 1 : 0;
    if (has)
        memcpy(frame_out, &s_frame, sizeof(LexaFullFrame_t));
    xSemaphoreGive(s_mutex);
    return has;
}
