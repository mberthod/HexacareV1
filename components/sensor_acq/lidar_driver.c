/**
 * @file lidar_driver.c
 * @ingroup group_sensor_acq
 * @brief Acquisition LIDAR VL53L8CX — fusion 8×32 sur double bus I2C.
 *
 * LIDAR : bus SPI dédié (4 NCS). Capteurs enviro : I2C0 SDA=11 / SCL=12 (voir hw_diag).
 *
 * Fusion : colonnes L1|L2|L4|L3 (voir s_fusion_phys_idx + system_types.h).
 * Cadence : 5 Hz via vTaskDelayUntil (TASK_PERIOD_MS).
 * TWDT    : esp_task_wdt_reset() à chaque itération.
 *
 * Dépendance ULD :
 *   Les appels vl53l8cx_*() nécessitent l'ULD VL53L8CX de ST.
 *   Voir instructions dans vl53l8cx_platform.h.
 *   Sans l'ULD, compiler avec -DLIDAR_STUB_MODE pour utiliser
 *   des données simulées (développement sans matériel).
 */

#include "lidar_driver.h"
#include "radar_driver.h"
#include "vl53l8cx_platform.h"
#include "hdc1080_driver.h"
#include "bme280_driver.h"
#include "mic_driver.h"
#include "lexacare_config.h"
#include "pins_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Lecture des capteurs environnementaux toutes les N trames LIDAR (1 s à 15 Hz) */
#define ENVIRO_READ_PERIOD_FRAMES  15

/* LIDAR réel (ULD ST) vs STUB (simulé) */
#if LEXACARE_LIDAR_USE_ST_ULD
#  if !defined(LEXACARE_HAVE_ST_ULD)
#    error "LEXACARE_LIDAR_USE_ST_ULD=1 mais l'ULD ST (STSW-IMG040) est absent. Copier l'ULD dans components/sensor_acq/uld/ (vl53l8cx_api.c/.h) puis rebuild."
#  endif
#  include "vl53l8cx_api.h"
#  define LEXA_LIDAR_STUB 0
#else
#  define LEXA_LIDAR_STUB 1
#endif

static const char *TAG = "lidar_driver";

/* Kick TWDT — ne jamais utiliser ESP_ERROR_CHECK (abort si échec). */
static void lidar_wdt_kick(void)
{
    esp_err_t e = esp_task_wdt_reset();
    if (e != ESP_OK) {
        static uint32_t s_wdt_log;
        if ((s_wdt_log++ & 0xFFu) == 0u) {
            ESP_LOGW(TAG, "TWDT reset : %s", esp_err_to_name(e));
        }
    }
}

static void lidar_wdt_try_add_current(void)
{
    esp_err_t e = esp_task_wdt_add(NULL);
    if (e == ESP_OK) {
        return;
    }
    /* Déjà enregistré ou TWDT désactivé : on continue sans abort. */
    if (e == ESP_ERR_INVALID_STATE) {
        ESP_LOGD(TAG, "TWDT : tâche déjà enregistrée");
        return;
    }
    ESP_LOGW(TAG, "TWDT add échoué : %s — pas de surveillance WDT pour cette tâche",
             esp_err_to_name(e));
}

/* ================================================================
 * Constantes de la tâche
 * ================================================================ */
/* ================================================================
 * Fréquence d'acquisition VL53L8CX :
 *
 *  TASK_PERIOD_MS  Fréquence  Portée max typique
 *  200 ms          5 Hz       ~4000 mm (max précision)   ← valeur actuelle
 *  143 ms          7 Hz       ~3000 mm
 *  100 ms         10 Hz       ~2500 mm
 *   67 ms         15 Hz       ~2000 mm
 *
 * Pour la détection de chute (personne debout/allongée à 0.5–3 m),
 * 5 Hz offre la meilleure sensibilité au bruit de fond et la portée max.
 * La fréquence ULD (vl53l8cx_set_ranging_frequency_hz) DOIT correspondre.
 * ================================================================ */
#define TASK_STACK_SIZE         (16384) /**< ULD ST (vl53l8cx_init) : grands buffers locaux */
#define TASK_PRIORITY           (10)
#define TASK_CORE               (1)
#define TASK_PERIOD_MS          (200)   /**< 5 Hz — précision maximale VL53L8CX */
#define LIDAR_FREQ_HZ           (5)     /**< Fréquence ULD à configurer (voir init) */

/**
 * Ordre des blocs de 8 colonnes dans lidar_matrix_t (32 colonnes au total) :
 * slot fusion 0..3 → index physique devs[] (NCS LIDAR1..4 = indices 0,1,2,3).
 * Demandé : LIDAR1, LIDAR2, LIDAR4, LIDAR3.
 */
static const uint8_t s_fusion_phys_idx[LIDAR_NUM_FRONT] = { 0, 1, 3, 2 };

/* ================================================================
 * lidar_dev_t (interne)
 * @brief Contexte par capteur VL53L8CX.
 * ================================================================ */
typedef struct {
    VL53L8CX_Platform platform; /**< Couche plateforme SPI */
    bool              active;   /**< true si capteur initialisé */
    int               sensor_idx;
#if !LEXA_LIDAR_STUB
    VL53L8CX_Configuration uld;
    VL53L8CX_ResultsData   results;
#endif
    /** Température silicium (VL53L8CX_ResultsData, datasheet) — ULD uniquement. */
    int8_t            last_silicon_temp_c;
    bool              have_silicon_temp;
} lidar_dev_t;

/* ================================================================
 * lidar_task_ctx_t (interne)
 * @brief Contexte complet de la tâche d'acquisition.
 * ================================================================ */
typedef struct {
    lidar_dev_t    devs[LIDAR_NUM_TOTAL];
    sys_context_t *sys_ctx;
} lidar_task_ctx_t;

/* ================================================================
 * lidar_init_sensor (interne)
 * @brief Initialise un capteur VL53L8CX via SPI.
 *
 * Configure la résolution 8×8 et démarre le ranging continu.
 * Le handle SPI est déjà créé par hw_diag_run() (spi_bus_add_device).
 *
 * @param dev      Contexte du capteur.
 * @param spi_dev  Handle SPI ESP-IDF du capteur (NCS dédié).
 * @param idx      Index du capteur (0–3).
 * @return true si l'initialisation réussit.
 * ================================================================ */
static bool lidar_init_sensor(lidar_dev_t *dev,
                               spi_device_handle_t spi_dev,
                               int idx)
{
    dev->platform.spi_dev = spi_dev;

    lidar_wdt_kick();
    if (VL53L8CX_PlatformInit(&dev->platform) != 0) {
        ESP_LOGE(TAG, "LIDAR[%d] : échec PlatformInit SPI", idx);
        return false;
    }

#if !LEXA_LIDAR_STUB
    memset(&dev->uld, 0, sizeof(dev->uld));
    dev->uld.platform = dev->platform;

    /* vl53l8cx_init charge le firmware capteur : centaines de ms + gros transferts SPI. */
    lidar_wdt_kick();
    if (vl53l8cx_init(&dev->uld) != VL53L8CX_STATUS_OK) {
        ESP_LOGE(TAG, "LIDAR[%d] : vl53l8cx_init échoué", idx);
        return false;
    }
    lidar_wdt_kick();

    if (vl53l8cx_set_resolution(&dev->uld, VL53L8CX_RESOLUTION_8X8) != VL53L8CX_STATUS_OK) {
        ESP_LOGE(TAG, "LIDAR[%d] : set_resolution échoué", idx);
        return false;
    }
    lidar_wdt_kick();

    if (vl53l8cx_set_ranging_frequency_hz(&dev->uld, LIDAR_FREQ_HZ) != VL53L8CX_STATUS_OK) {
        ESP_LOGE(TAG, "LIDAR[%d] : set_ranging_frequency(%d Hz) échoué", idx, LIDAR_FREQ_HZ);
        return false;
    }
    lidar_wdt_kick();

    if (vl53l8cx_set_ranging_mode(&dev->uld, VL53L8CX_RANGING_MODE_AUTONOMOUS) != VL53L8CX_STATUS_OK) {
        ESP_LOGE(TAG, "LIDAR[%d] : set_ranging_mode échoué", idx);
        return false;
    }
    lidar_wdt_kick();

    if (vl53l8cx_start_ranging(&dev->uld) != VL53L8CX_STATUS_OK) {
        ESP_LOGE(TAG, "LIDAR[%d] : start_ranging échoué", idx);
        return false;
    }
    lidar_wdt_kick();

    ESP_LOGI(TAG, "LIDAR[%d] SPI : ULD ST actif (8×8, %d Hz)", idx, LIDAR_FREQ_HZ);
#else
    ESP_LOGW(TAG, "LIDAR[%d] SPI : STUB actif (LEXACARE_LIDAR_USE_ST_ULD=0)", idx);
#endif

    dev->active = true;
    return true;
}

/* ================================================================
 * lidar_read_sensor (interne)
 * @brief Lit les données de ranging d'un capteur et les fusionne.
 *
 * Sans ULD : génère des données simulées pour valider l'architecture.
 *
 * @param dev    Contexte du capteur.
 * @param matrix Matrice destination.
 * @param col_offset Début du bloc de 8 colonnes (slot fusion × 8, ordre L1|L2|L4|L3).
 * ================================================================ */
static void lidar_read_sensor(lidar_dev_t *dev,
                               lidar_matrix_t *matrix,
                               int col_offset)
{
    if (!dev->active) return;

#if !LEXA_LIDAR_STUB
    uint8_t ready = 0;
    if (vl53l8cx_check_data_ready(&dev->uld, &ready) != VL53L8CX_STATUS_OK || !ready) {
        return;
    }

    if (vl53l8cx_get_ranging_data(&dev->uld, &dev->results) != VL53L8CX_STATUS_OK) {
        return;
    }

    dev->last_silicon_temp_c = dev->results.silicon_temp_degc;
    dev->have_silicon_temp   = true;

    for (int zone = 0; zone < 64; zone++) {
        int row = zone / 8;
        int col = zone % 8;
        /* ULD ST : distance_mm est un tableau 1D de int16_t.
         * Si NB_TARGET_PER_ZONE > 1, on prend la cible 0 (la plus proche). */
        const int idx = zone * (int)VL53L8CX_NB_TARGET_PER_ZONE;
        uint16_t dist = (uint16_t)(dev->results.distance_mm[idx]);
        if (dist == 0 || dist > 4000) dist = 0;
        matrix->data[row][col_offset + col] = dist;
    }
#else
    /* ── Mode STUB : scan 8×8 réaliste pour validation visuelle ──────────
     *
     * Simule un arc 180° vu depuis un capteur plafond/mur :
     *  · Fond (murs/sol) à 2500–4000 mm selon l'angle et la hauteur
     *  · Distance plus courte aux rangées basses (sol plus proche)
     *  · Variation angulaire cohérente sur 32 colonnes
     *  · Bruit de mesure réaliste ±30 mm
     *
     * Valeurs dans la plage exploitable par la heatmap inferno :
     *   100 mm (très proche / chute) → 4000 mm (max portée)
     */
    static int32_t s_stub_seed = 0;
    s_stub_seed++;  /* Compteur pour variation temporelle légère */

    for (int row = 0; row < LIDAR_ROWS; row++) {
        for (int col = 0; col < 8; col++) {
            /* Position angulaire dans l'arc 180°
             * col_offset + col ∈ [0..31] → angle ∈ [0°..180°]            */
            float angle_deg = (float)(col_offset + col) * (180.0f / (LIDAR_COLS - 1));
            float angle_rad = angle_deg * 3.14159f / 180.0f;

            /* Distance de fond : mur à ~3500 mm en face (90°),
             * murs latéraux plus proches (0° et 180°) ≈ 2000 mm          */
            float bg_mm = 2000.0f + 1500.0f * sinf(angle_rad);

            /* Facteur hauteur : rangées basses (7) = sol, plus proche      */
            float height_factor = 1.0f - (float)row / (float)(LIDAR_ROWS - 1) * 0.25f;
            bg_mm *= height_factor;

            /* Bruit de mesure réaliste ±25 mm                              */
            int32_t noise = ((s_stub_seed * 1103515245 + 12345) & 0x7FFF) % 50 - 25;
            s_stub_seed = s_stub_seed * 6364136223846793005LL + 1;

            uint16_t dist = (uint16_t)(bg_mm + (float)noise);
            if (dist < 100)  dist = 100;
            if (dist > 4000) dist = 4000;

            matrix->data[row][col_offset + col] = dist;
        }
    }
#endif /* LEXA_LIDAR_STUB */
}

#if LEXACARE_LIDAR_LOG_MATRIX
/**
 * Affiche la matrice fusionnée sur UART (USB-JTAG) : 8 lignes × 32 colonnes (mm).
 * Colonnes : L1 | L2 | L4 | L3 (blocs de 8). T_silicon selon même ordre d’affichage.
 */
static void lidar_log_matrix_uart(const lidar_matrix_t *m, bool valid,
                                   const lidar_dev_t *devs)
{
    if (!valid || !m) {
        return;
    }

    ESP_LOGI(TAG,
             "════════ Matrice distances 32×8 mm (col: L1 | L2 | L4 | L3) t=%" PRId64 " µs ════════",
             (int64_t)m->timestamp_us);

    for (int row = 0; row < LIDAR_ROWS; row++) {
        char line[420];
        int n = snprintf(line, sizeof(line), "z%d", row);
        for (int c = 0; c < LIDAR_COLS && n < (int)sizeof(line) - 8; c++) {
            n += snprintf(line + (size_t)n, sizeof(line) - (size_t)n, " %4u",
                          (unsigned)m->data[row][c]);
        }
        ESP_LOGI(TAG, "%s", line);
    }

    static const char *const lbl[LIDAR_NUM_FRONT] = { "L1", "L2", "L4", "L3" };
    char tline[160];
    int p = snprintf(tline, sizeof(tline), "T_silicon (°C, doc VL53L8CX) : ");
    for (int slot = 0; slot < LIDAR_NUM_FRONT && p < (int)sizeof(tline) - 24; slot++) {
        uint8_t phys = s_fusion_phys_idx[slot];
        if (devs[phys].have_silicon_temp) {
            p += snprintf(tline + (size_t)p, sizeof(tline) - (size_t)p, "%s=%d ",
                          lbl[slot], (int)devs[phys].last_silicon_temp_c);
        } else {
            p += snprintf(tline + (size_t)p, sizeof(tline) - (size_t)p, "%s=-- ", lbl[slot]);
        }
    }
    ESP_LOGI(TAG, "%s", tline);
    ESP_LOGI(TAG, "══════════════════════════════════════════════════════════════════");
}
#endif /* LEXACARE_LIDAR_LOG_MATRIX */

/* ================================================================
 * task_sensor_acq (interne)
 * @brief Tâche FreeRTOS d'acquisition capteurs — Core 1, priorité 10.
 *
 * Boucle cadencée à 5 Hz (vTaskDelayUntil) — portée maximale VL53L8CX.
 * Appelle radar_driver_poll() à chaque itération.
 * Pousse la sensor_frame_t dans sensor_to_ai_queue (non bloquant).
 *
 * @param pvParam Pointeur vers lidar_task_ctx_t alloué par lidar_driver_start.
 * ================================================================ */
static void task_sensor_acq(void *pvParam)
{
    lidar_task_ctx_t *ctx = (lidar_task_ctx_t *)pvParam;

    if (!ctx || !ctx->sys_ctx) {
        ESP_LOGE(TAG, "Task_Sensor_Acq : contexte NULL — arrêt");
        vTaskDelete(NULL);
        return;
    }

    lidar_wdt_try_add_current();
    ESP_LOGI(TAG, "Task_Sensor_Acq démarrée sur Core %d", xPortGetCoreID());

    /* ================================================================
     * Initialisation capteurs (déplacée ici pour éviter de bloquer app_main)
     * ================================================================ */
    ESP_LOGI(TAG, "Initialisation LIDARs (ULD) dans la tâche capteurs…");
    for (int i = 0; i < LIDAR_NUM_FRONT; i++) {
        ctx->devs[i].sensor_idx = i;

        if (!ctx->sys_ctx->lidar_ok[i]) {
            ctx->devs[i].active = false;
            ESP_LOGW(TAG, "LIDAR[%d] absent (hw_diag) — ignoré", i);
            continue;
        }

        if (!ctx->sys_ctx->lidar_spi[i]) {
            ESP_LOGW(TAG, "LIDAR[%d] : handle SPI NULL — ignoré", i);
            ctx->devs[i].active = false;
            continue;
        }

        lidar_wdt_kick();
        vTaskDelay(pdMS_TO_TICKS(10));

        if (!lidar_init_sensor(&ctx->devs[i], ctx->sys_ctx->lidar_spi[i], i)) {
            ESP_LOGW(TAG, "LIDAR[%d] : initialisation SPI/ULD échouée", i);
            ctx->devs[i].active = false;
        }
    }

    int active_lidars = 0;
    for (int i = 0; i < LIDAR_NUM_FRONT; i++) {
        if (ctx->devs[i].active) {
            active_lidars++;
        }
    }
    if (active_lidars == 0) {
        ESP_LOGW(TAG, "Aucun LIDAR actif — boucle envoie des trames invalides (enviro/MIC actifs)");
    } else {
        ESP_LOGI(TAG, "%d LIDAR(s) actif(s) sur %d", active_lidars, LIDAR_NUM_FRONT);
    }
    ESP_LOGI(TAG,
             "Matrice fusion : colonnes 0-7=L1, 8-15=L2, 16-23=L4, 24-31=L3 (phys. dev 0,1,3,2)");

    /* Radar : uniquement si connecté */
#if LEXACARE_ENABLE_RADAR
    {
        esp_err_t re = radar_driver_init();
        if (re != ESP_OK) {
            ESP_LOGW(TAG, "radar_driver_init : %s", esp_err_to_name(re));
        }
    }
#else
    ESP_LOGI(TAG, "Radar LD6002 désactivé (LEXACARE_ENABLE_RADAR=0)");
#endif

    TickType_t last_wake  = xTaskGetTickCount();
    uint32_t   frame_idx  = 0;
#if LEXACARE_LIDAR_LOG_MATRIX
    uint32_t   log_frame_counter = 0;
#endif

    while (true) {
        lidar_wdt_kick();

        sensor_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.lidar.timestamp_us = esp_timer_get_time();

        /* Fusion : 4 blocs de 8 colonnes dans l’ordre L1 | L2 | L4 | L3 */
        for (int slot = 0; slot < LIDAR_NUM_FRONT; slot++) {
            uint8_t phys = s_fusion_phys_idx[slot];
            lidar_read_sensor(&ctx->devs[phys], &frame.lidar, slot * 8);
        }

        const bool lidar_ok_frame = (active_lidars > 0);
        frame.lidar.valid   = lidar_ok_frame;
        frame.lidar_valid   = lidar_ok_frame;

#if LEXACARE_LIDAR_LOG_MATRIX
        if (lidar_ok_frame && LEXACARE_LIDAR_LOG_EVERY_N_FRAMES > 0) {
            log_frame_counter++;
            if (log_frame_counter >= (uint32_t)LEXACARE_LIDAR_LOG_EVERY_N_FRAMES) {
                log_frame_counter = 0;
                lidar_log_matrix_uart(&frame.lidar, true, ctx->devs);
            }
        }
#endif

        /* Lecture du radar (non bloquant) — uniquement si connecté */
#if LEXACARE_ENABLE_RADAR
        frame.radar_valid = radar_driver_poll(&frame.radar);
#else
        frame.radar_valid = false;
#endif

        /* Envoi dans la queue AI (non bloquant) */
        if (xQueueSend(ctx->sys_ctx->sensor_to_ai_queue, &frame, 0) != pdTRUE) {
            ESP_LOGD(TAG, "sensor_to_ai_queue pleine — trame abandonnée");
        }

        /* ── Mise à jour du snapshot partagé (protégé par mutex) ─────── */
        if (ctx->sys_ctx->data_mutex &&
            xSemaphoreTake(ctx->sys_ctx->data_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            ctx->sys_ctx->latest_sensor = frame;
            ctx->sys_ctx->sensor_valid  = true;
            xSemaphoreGive(ctx->sys_ctx->data_mutex);
        }

        /* ── Lecture capteurs environnementaux (1 Hz) ─────────────────── */
        frame_idx++;
        if (frame_idx >= ENVIRO_READ_PERIOD_FRAMES) {
            frame_idx = 0;
            lidar_wdt_kick(); /* avant I2S/I2C potentiellement longs */
            enviro_data_t env = {
                .temp_hdc_c   = NAN, .humidity_hdc  = NAN,
                .temp_bme_c   = NAN, .pressure_hpa  = NAN,
                .humidity_bme = NAN,
            };

            /* HDC1080 */
            float t_hdc = 0.0f, h_hdc = 0.0f;
            if (hdc1080_read(&t_hdc, &h_hdc) == ESP_OK) {
                env.temp_hdc_c  = t_hdc;
                env.humidity_hdc = h_hdc;
            }

            /* BME280 */
            float t_bme = 0.0f, p_bme = 0.0f, h_bme = 0.0f;
            if (bme280_read(&t_bme, &p_bme, &h_bme) == ESP_OK) {
                env.temp_bme_c   = t_bme;
                env.pressure_hpa = p_bme;
                env.humidity_bme = h_bme;
            }

            /* Microphone — burst rapide */
            uint32_t rms = 0, peak = 0;
            bool mic_ok = (mic_driver_read(&rms, &peak) == ESP_OK);

            /* Mise à jour sys_context_t */
            if (ctx->sys_ctx->data_mutex &&
                xSemaphoreTake(ctx->sys_ctx->data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                ctx->sys_ctx->enviro = env;
                ctx->sys_ctx->mic.rms   = rms;
                ctx->sys_ctx->mic.peak  = peak;
                ctx->sys_ctx->mic.valid = mic_ok;
                xSemaphoreGive(ctx->sys_ctx->data_mutex);
            }
        }

        /* Cadence TASK_PERIOD_MS via vTaskDelayUntil (drift-free) */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_MS));
    }
}

/* ================================================================
 * lidar_driver_start
 * @brief Initialise les capteurs et crée Task_Sensor_Acq sur Core 1.
 *
 * @param ctx Pointeur vers le contexte système (bus I2C + lidar_ok[]).
 * @return ESP_OK si la tâche est créée avec succès.
 * ================================================================ */
esp_err_t lidar_driver_start(sys_context_t *ctx)
{
    lidar_task_ctx_t *task_ctx = calloc(1, sizeof(lidar_task_ctx_t));
    if (!task_ctx) {
        return ESP_ERR_NO_MEM;
    }
    task_ctx->sys_ctx = ctx;

    /* Création de la tâche épinglée sur Core 1 */
    BaseType_t ret = xTaskCreatePinnedToCore(
        task_sensor_acq,
        "Task_Sensor_Acq",
        TASK_STACK_SIZE,
        task_ctx,
        TASK_PRIORITY,
        NULL,
        TASK_CORE);

    if (ret != pdPASS) {
        free(task_ctx);
        ESP_LOGE(TAG, "Échec création Task_Sensor_Acq");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Task_Sensor_Acq créée (Core %d, priorité %d, %d Hz)",
             TASK_CORE, TASK_PRIORITY, 1000 / TASK_PERIOD_MS);
    return ESP_OK;
}
