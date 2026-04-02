/**
 * @file lidar_handler.cpp
 * @brief Implémentation de la gestion des 4 capteurs VL53L8CX via SPI.
 * 
 * Ce module gère l'initialisation des capteurs via le bus SPI,
 * l'utilisation des broches LPn et NCS pour la sélection,
 * et l'algorithme de détection de chute.
 * Supporte également les capteurs Osram TMF8829 sur le même bus.
 */

#include "config/config.h"
#include "lidar_handler.h"
#include "config/pins_lexacare.h"
#include "system/system_state.h"
#include <SPI.h>
#include <Arduino.h>
#include "esp_log.h"

static const char* TAG = "LIDAR_SPI";

static const uint8_t LPN_PINS[] = { PIN_LIDAR_LPN_1, PIN_LIDAR_LPN_2, PIN_LIDAR_LPN_3, PIN_LIDAR_LPN_4 };
static const uint8_t NCS_PINS[] = { PIN_SPI_NCS1, PIN_SPI_NCS2, PIN_SPI_NCS3, PIN_SPI_NCS4 };

/** @brief Matrice globale de profondeur (32 colonnes x 8 lignes) */
uint16_t g_lidar_matrix[LIDAR_MATRIX_ROWS][LIDAR_MATRIX_COLS];

static uint16_t s_prev_matrix[LIDAR_MATRIX_ROWS][LIDAR_MATRIX_COLS];
static bool s_prev_valid = false;
static int s_lidar_count = 0;

/**
 * @brief Sélectionne un capteur via sa broche NCS.
 */
static void select_sensor(int index) {
    for (int i = 0; i < LIDAR_COUNT; i++) {
        digitalWrite(NCS_PINS[i], (i == index) ? LOW : HIGH);
    }
}

/**
 * @brief Désélectionne tous les capteurs.
 */
static void deselect_all(void) {
    for (int i = 0; i < LIDAR_COUNT; i++) {
        digitalWrite(NCS_PINS[i], HIGH);
    }
}

/**
 * @brief Lit les zones d'un capteur VL53L8CX via SPI.
 */
static int read_sensor_zones_spi(int index, int col_start) {
    select_sensor(index);
    
    // Note : Implémentation réelle via driver ST SPI (vl53l8cx_get_ranging_data).
    // Ici, on simule des données pour la structure.
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            int col = col_start + c;
            if (col < LIDAR_MATRIX_COLS)
                g_lidar_matrix[r][col] = 400; // placeholder 400 mm
        }
    }
    
    deselect_all();
    return 1;
}

/**
 * @brief Initialise le bus SPI et configure les 4 capteurs.
 */
bool lidar_handler_init(void) {
    ESP_LOGI(TAG, "Initialisation SPI Lidars...");
    
    // Configuration des broches de contrôle
    pinMode(PIN_LIDAR_SYNC, OUTPUT);
    digitalWrite(PIN_LIDAR_SYNC, LOW);
    
    for (int i = 0; i < LIDAR_COUNT; i++) {
        pinMode(LPN_PINS[i], OUTPUT);
        digitalWrite(LPN_PINS[i], LOW);
        pinMode(NCS_PINS[i], OUTPUT);
        digitalWrite(NCS_PINS[i], HIGH);
    }
    
    // Initialisation SPI
    SPI.begin(PIN_SPI_MCLK, PIN_SPI_MISO, PIN_SPI_MOSI);
    
    delay(10);

    s_lidar_count = 0;
    for (int i = 0; i < LIDAR_COUNT; i++) {
        ESP_LOGD(TAG, "Réveil Lidar %d (LPn HIGH)...", i+1);
        digitalWrite(LPN_PINS[i], HIGH);
        delay(5);
        
        // Séquence d'initialisation SPI spécifique au VL53L8CX ou TMF8829
        // s_lidar_count++; 
        
        // Simulation de détection
        s_lidar_count++;
        ESP_LOGI(TAG, "Lidar %d détecté sur NCS%d", i+1, i+1);
    }

    memset(g_lidar_matrix, 0, sizeof(g_lidar_matrix));
    memset(s_prev_matrix, 0, sizeof(s_prev_matrix));
    s_prev_valid = false;
    
    ESP_LOGI(TAG, "%d/%d Lidars SPI initialisés", s_lidar_count, LIDAR_COUNT);
    return s_lidar_count > 0;
}

/**
 * @brief Lit une frame complète (4 capteurs via SPI).
 */
int lidar_handler_read_frame(void) {
    int ok = 0;
    ESP_LOGV(TAG, "Acquisition frame SPI...");
    for (int i = 0; i < LIDAR_COUNT; i++) {
        if (read_sensor_zones_spi(i, i * 8))
            ok++;
    }
    return ok;
}

/**
 * @brief Algorithme de détection de chute (identique à la version I2C).
 */
void lidar_handler_update_fall_detection(void) {
    if (!s_prev_valid) {
        memcpy(s_prev_matrix, g_lidar_matrix, sizeof(g_lidar_matrix));
        s_prev_valid = true;
        return;
    }

    uint32_t sum_curr = 0, sum_prev = 0;
    uint16_t count = 0;
    for (int r = 0; r < LIDAR_MATRIX_ROWS; r++) {
        for (int c = 0; c < LIDAR_MATRIX_COLS; c++) {
            uint16_t v = g_lidar_matrix[r][c];
            uint16_t p = s_prev_matrix[r][c];
            if (v < 4000 && p < 4000) {
                sum_curr += v;
                sum_prev += p;
                count++;
            }
        }
    }
    if (count == 0) {
        memcpy(s_prev_matrix, g_lidar_matrix, sizeof(g_lidar_matrix));
        return;
    }
    uint16_t avg_curr = (uint16_t)(sum_curr / count);
    uint16_t avg_prev = (uint16_t)(sum_prev / count);
    int16_t delta = (int16_t)avg_curr - (int16_t)avg_prev;

    if (delta >= (int16_t)LIDAR_FALL_VARIATION_MM) {
        ESP_LOGW(TAG, "!!! CHUTE DÉTECTÉE !!! Delta: %d mm", delta);
        system_state_set_fall_detected(true);
    }

    memcpy(s_prev_matrix, g_lidar_matrix, sizeof(g_lidar_matrix));
}

void lidar_handler_get_summary(uint16_t *min_mm, uint16_t *max_mm, uint32_t *sum_mm, uint16_t *valid_zones) {
    uint16_t min_v = 0xFFFF, max_v = 0;
    uint32_t sum = 0;
    uint16_t n = 0;
    for (int r = 0; r < LIDAR_MATRIX_ROWS; r++) {
        for (int c = 0; c < LIDAR_MATRIX_COLS; c++) {
            uint16_t v = g_lidar_matrix[r][c];
            if (v > 0 && v < 4000) {
                if (v < min_v) min_v = v;
                if (v > max_v) max_v = v;
                sum += v;
                n++;
            }
        }
    }
    if (min_mm) *min_mm = (n == 0) ? 0 : min_v;
    if (max_mm) *max_mm = max_v;
    if (sum_mm) *sum_mm = sum;
    if (valid_zones) *valid_zones = n;
}

void lidar_handler_recover_i2c(void) {
    // Non utilisé en mode SPI, mais conservé pour compatibilité interface
    lidar_handler_init();
}
