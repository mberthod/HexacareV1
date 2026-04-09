/**
 * @file main.c
 * @brief Point d'entrée LexaCare V1 — démarrage complet du firmware.
 *
 * Activer/désactiver les composants dans :
 *   components/lexacare_types/include/lexacare_config.h
 */

/**
 * @defgroup group_main Démarrage (main)
 * @brief Point d'entrée : initialise les services, lance les diagnostics, puis démarre les tâches.
 *
 * L'intention est de garder une séquence de boot lisible et “déterministe” :
 * chaque étape prépare la suivante, et évite de lancer des tâches si une dépendance manque.
 *
 * @{
 */

#include <string.h>
#include <math.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "lexacare_config.h"
#include "system_types.h"
#include "pins_config.h"
#include "hw_diag.h"
#include "lidar_driver.h"
#include "pc_diag.h"
#include "sys_storage.h"

#if LEXACARE_ENABLE_AI
#include "fall_detection_ai.h"
#endif

/* Inclusions conditionnelles selon la config */
#if LEXACARE_ENABLE_HDC1080
#  include "hdc1080_driver.h"
#endif
#if LEXACARE_ENABLE_BME280
#  include "bme280_driver.h"
#endif
#if LEXACARE_ENABLE_MIC
#  include "mic_driver.h"
#endif
#if LEXACARE_ENABLE_MESH
#  include "mesh_manager.h"
#endif

static const char *TAG = "main";

/* Contexte système — statique (BSS) */
static sys_context_t s_ctx;

void app_main(void)
{
    /* ================================================================
     * 1. Infrastructure de base
     * ================================================================ */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corrompu — effacement");
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* ================================================================
     * 2. Stockage SPIFFS + panic handler
     * ================================================================ */
    if (littlefs_manager_init() == ESP_OK) {
        if (panic_handler_install() == ESP_OK)
            panic_handler_check_on_boot();
    } else {
        ESP_LOGW(TAG, "SPIFFS indisponible — logs désactivés");
    }

    /* ================================================================
     * 3. Diagnostic matériel (SPI LIDAR + PCA9555 LPn)
     * ================================================================ */
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.enviro.temp_hdc_c   = NAN;
    s_ctx.enviro.humidity_hdc = NAN;
    s_ctx.enviro.temp_bme_c   = NAN;
    s_ctx.enviro.pressure_hpa = NAN;
    s_ctx.enviro.humidity_bme = NAN;

    hw_diag_result_t diag = hw_diag_run(&s_ctx);
    if (diag & HW_DIAG_LIDAR_ALL)     ESP_LOGE(TAG, "Aucun LIDAR — dégradé");
    if (diag & HW_DIAG_LIDAR_PARTIAL) ESP_LOGW(TAG, "LIDARs partiels");
    if (diag & HW_DIAG_RADAR_MISSING) ESP_LOGW(TAG, "Radar absent");

    /* ================================================================
     * 4. Alimentations PCA9555 — AVANT init des capteurs
     *    POWER_RADAR alimente HDC1080 + BME280 sur la carte radar.
     * ================================================================ */
#if LEXACARE_PWR_RADAR_ENABLED
    if (hw_diag_pca9555_set_power(PCA9555_BIT_PWR_RADAR, true) == ESP_OK) {
        ESP_LOGI(TAG, "POWER_RADAR (IO1.5) activé — capteurs I2C alimentés");
        vTaskDelay(pdMS_TO_TICKS(50));  /* Stabilisation alimentation */
    }
#endif
#if LEXACARE_PWR_FAN_ENABLED
    hw_diag_pca9555_set_power(PCA9555_BIT_PWR_FAN,   true);
    ESP_LOGI(TAG, "POWER_FAN (IO1.4) activé");
#endif
#if LEXACARE_PWR_MLX_ENABLED
    hw_diag_pca9555_set_power(PCA9555_BIT_PWR_MLX,   true);
    ESP_LOGI(TAG, "POWER_MLX (IO1.7) activé");
    vTaskDelay(pdMS_TO_TICKS(20));
#endif

    /* ================================================================
     * 5. Capteurs I2C environnementaux — même bus que PCA9555 (I2C0, SDA=11, SCL=12)
     * ================================================================ */
    i2c_master_bus_handle_t sensors_bus = NULL;
#if LEXACARE_ENABLE_HDC1080 || LEXACARE_ENABLE_BME280 || LEXACARE_ENABLE_MLX90640
    if (hw_diag_init_sensor_bus(&sensors_bus) != ESP_OK) {
        ESP_LOGW(TAG, "Bus I2C0 capteurs indisponible — capteurs enviro off");
    }
#endif

#if LEXACARE_ENABLE_HDC1080
    if (sensors_bus) {
        if (hdc1080_init(sensors_bus) == ESP_OK)
            ESP_LOGI(TAG, "HDC1080 prêt @0x40");
        else
            ESP_LOGW(TAG, "HDC1080 non détecté @0x40");
    }
#endif

#if LEXACARE_ENABLE_BME280
    if (sensors_bus) {
        if (bme280_init(sensors_bus, BME280_I2C_ADDR_DEFAULT) == ESP_OK) {
            ESP_LOGI(TAG, "BME280 prêt @0x76");
        } else {
            ESP_LOGW(TAG, "BME280 @0x76 échoué — essai @0x77");
            if (bme280_init(sensors_bus, BME280_I2C_ADDR_ALT) == ESP_OK)
                ESP_LOGI(TAG, "BME280 prêt @0x77");
            else
                ESP_LOGW(TAG, "BME280 non détecté");
        }
    }
#endif

    /* ================================================================
     * 6. Queues FreeRTOS + mutex
     * ================================================================ */
    s_ctx.data_mutex = xSemaphoreCreateMutex();
    if (!s_ctx.data_mutex) { ESP_LOGE(TAG, "Mutex KO"); esp_restart(); }

    s_ctx.sensor_to_ai_queue = xQueueCreate(4,  sizeof(sensor_frame_t));
    s_ctx.ai_to_mesh_queue   = xQueueCreate(8,  sizeof(ai_event_t));
    s_ctx.diag_to_pc_queue   = xQueueCreate(8,  sizeof(ai_event_t));
    s_ctx.log_queue          = xQueueCreate(16, sizeof(char *));

    if (!s_ctx.sensor_to_ai_queue || !s_ctx.ai_to_mesh_queue ||
        !s_ctx.diag_to_pc_queue   || !s_ctx.log_queue) {
        ESP_LOGE(TAG, "Queues KO — redémarrage");
        esp_restart();
    }

    /* ================================================================
     * 7. Alimentation MIC — AVANT WiFi
     *    Toutes les opérations PCA9555 (allocation du device handle I2C)
     *    doivent être effectuées AVANT mesh_manager_init().
     *    Après WiFi, le heap peut être fragmenté → LoadStoreAlignment
     *    sur l'allocation dans i2c_master_bus_add_device.
     * ================================================================ */
#if LEXACARE_PWR_MIC_ENABLED && LEXACARE_ENABLE_MIC
    if (hw_diag_pca9555_set_power(PCA9555_BIT_PWR_MIC, true) == ESP_OK) {
        ESP_LOGI(TAG, "POWER_MIC (IO1.6) activé");
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#endif

    /* ================================================================
     * 8. Microphone I2S — AVANT WiFi (évite fragmentation heap TLSF)
     * ================================================================ */
#if LEXACARE_ENABLE_MIC
    if (mic_driver_init() == ESP_OK)
        ESP_LOGI(TAG, "Microphone I2S prêt");
    else
        ESP_LOGW(TAG, "Microphone I2S KO");
#endif

    /* ================================================================
     * 9. Réseau mesh ESP-NOW — APRÈS toutes les opérations PCA9555
     * ================================================================ */
#if LEXACARE_ENABLE_MESH
    ESP_ERROR_CHECK(mesh_manager_init(&s_ctx));
#else
    ESP_LOGW(TAG, "Mesh WiFi désactivé (LEXACARE_ENABLE_MESH=0)");
#endif

    /* ================================================================
     * 10. Démarrage des tâches
     * ================================================================ */
#if LEXACARE_ENABLE_AI
    ESP_ERROR_CHECK(ai_engine_task_start(&s_ctx));   /* Core 1, prio 9  */
#else
    ESP_LOGW(TAG, "IA désactivée (LEXACARE_ENABLE_AI=0)");
#endif
    /* Ne PAS appeler esp_task_wdt_delete(NULL) ici :
     * app_main n'est pas enregistré au TWDT (voir sdkconfig TWDT settings).
     * Sur IDF v6.0, l'appel échoue avec ESP_ERR_NOT_FOUND et peut laisser
     * le mutex TWDT interne dans un état partiellement acquis → deadlock
     * dans esp_task_wdt_add() de Task_Sensor_Acq → TG1WDT_SYS_RST. */

#if LEXACARE_LIDAR_USE_ST_ULD
    /* i2c_master (PCA9555 LPn) depuis Task_Sensor_Acq → LoadStoreAlignment (IDF 6, spinlock).
     * Pulse LPn une fois par capteur ici, dans le contexte app_main uniquement.
     * Seuls les LIDARs présents dans LEXACARE_LIDAR_ACTIVE_MASK sont pulsés ;
     * les autres restent en LPn=0 (reset matériel permanent). */
    ESP_LOGI(TAG, "LPn : reset matériel VL53L8CX (masque actif=0x%02X)…",
             (unsigned)LEXACARE_LIDAR_ACTIVE_MASK);
    for (int li = 0; li < LIDAR_NUM_FRONT; li++) {
        if (!(LEXACARE_LIDAR_ACTIVE_MASK & (1u << li))) {
            continue;   /* LPn maintenu à 0 — capteur en reset */
        }
        (void)hw_diag_set_lidar_lpn(li, false);
        vTaskDelay(pdMS_TO_TICKS(2));
        (void)hw_diag_set_lidar_lpn(li, true);
        vTaskDelay(pdMS_TO_TICKS(15));
        ESP_LOGI(TAG, "LPn LIDAR[%d] pulsé (0→1)", li);
    }
#endif

    /* Lancement de la tâche capteurs — ne pas utiliser ESP_ERROR_CHECK ici :
     * un abort() dans cette séquence cause un deadlock USB CDC JTAG → IWDT. */
    {
        ESP_LOGI(TAG, "Création Task_Sensor_Acq (heap_int=%u o heap_psram=%u o)…",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        esp_err_t lidar_err = lidar_driver_start(&s_ctx);
        if (lidar_err != ESP_OK) {
            ESP_LOGE(TAG, "lidar_driver_start ÉCHOUÉ : %s (heap_int=%u o) — continuer sans LIDAR",
                     esp_err_to_name(lidar_err),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        } else {
            ESP_LOGI(TAG, "Task_Sensor_Acq créée — attente de son premier log…");
        }
    }
#if LEXACARE_ENABLE_MESH
    ESP_ERROR_CHECK(mesh_task_start(&s_ctx));         /* Core 0, prio 8  */
#endif
    ESP_ERROR_CHECK(pc_diag_task_start(&s_ctx));      /* Core 0, prio 3  */

    /* ================================================================
     * Rapport de démarrage
     * ================================================================ */
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "  LexaCare V1 — Firmware opérationnel");
    ESP_LOGI(TAG, "  IDF : %s | Rôle : %s",
             esp_get_idf_version(),
             s_ctx.is_root_node ? "ROOT" : "NODE");
    ESP_LOGI(TAG, "  Heap interne : %" PRIu32 " B | PSRAM : %" PRIu32 " B",
             (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "  Config: LIDAR×%d RADAR=%d HDC=%d BME=%d MIC=%d MESH=%d",
             LEXACARE_LIDAR_COUNT,
             LEXACARE_ENABLE_RADAR,
             LEXACARE_ENABLE_HDC1080,
             LEXACARE_ENABLE_BME280,
             LEXACARE_ENABLE_MIC,
             LEXACARE_ENABLE_MESH);
    ESP_LOGI(TAG, "================================================");
}

/** @} */ /* end of group_main */
