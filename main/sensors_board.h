#pragma once

#include <stdint.h>
#include "esp_err.h"

/** Snapshot 1 Hz pour JSON USB (conn_* = 0/1). */
typedef struct {
    int conn_ds3231;
    int conn_cat24;
    int conn_bme280;
    int conn_hdc1080;
    int conn_tmp117;
    int conn_vl53l0;
    int conn_mlx90640;
    float ds3231_temp_c;
    float hdc1080_temp_c;
    float hdc1080_rh_pct;
    float bme280_temp_c;
    float bme280_h_pa;
    float bme280_rh_pct;
    float tmp117_temp_c;
} sensors_board_snapshot_t;

esp_err_t sensors_board_init(void);
void sensors_board_poll(sensors_board_snapshot_t *out);
void sensors_board_get_cached(sensors_board_snapshot_t *out);
/** À appeler depuis usb_telemetry_init (tâche 1 Hz sur core 1). */
void sensors_board_usb_snapshot_start(void);
