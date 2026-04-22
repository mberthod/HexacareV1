#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Grille assemblée 4× VL53L8CX (8×8 chacun) : 8 lignes × 32 colonnes, même ordre
 *  que le sketch Arduino (COL_ORDER #3,#4,#2,#1 → indices 2,3,1,0). */
#define VL53L8CX_ARRAY_FRAME_W 32
#define VL53L8CX_ARRAY_FRAME_H 8

typedef struct {
    int spi_clk_gpio;
    int spi_mosi_gpio;
    int spi_miso_gpio;
    int spi_freq_hz;
    int ncs_gpios[4];
    /** GPIO « POWER_LIDAR » (enable rail), ou -1 si non câblé */
    int power_en_gpio;
} vl53l8cx_array_config_t;

esp_err_t vl53l8cx_array_init(const vl53l8cx_array_config_t *cfg);

/* Lecture d'une frame assemblée [w×h] float normalisé [0,1].
 * Si mm_out_opt != NULL, remplit aussi les distances brutes (mm) dans le même
 * ordre row-major que le sketch Arduino (FRAME:).
 * w,h doivent être VL53L8CX_ARRAY_FRAME_W × VL53L8CX_ARRAY_FRAME_H (32×8). */
esp_err_t vl53l8cx_array_read_frame(float *frame, int16_t *mm_out_opt, int w,
                                    int h, int timeout_ms);

#ifdef __cplusplus
}
#endif
