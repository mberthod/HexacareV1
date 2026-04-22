/* app_main.c — mbh-firmware entry point unifié
 *
 * Séquence de boot :
 *   1. NVS + event loop + netif
 *   2. GPIO strapping sûr (NCS ToF sur GPIO 1,2 → output HIGH avant init SPI)
 *   3. Init PCA9555 (I²C) + bus SPI ToF + drivers bas niveau LexaCare
 *   4. Init TFLM dual runtime (arenas PSRAM)
 *   5. Init I2S micros + ring buffer audio
 *   6. Init mesh_manager (si !MBH_DISABLE_MESH)
 *   7. Lancer task_audio (Core 0), task_vision (Core 1), orchestrator (Core 1)
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "lexa_config.h"
#include "driver/gpio.h"
#include "task_helpers.h"

#if !MBH_DISABLE_MESH
#include "mesh_manager.h"
#endif

/* Composants LexaCare */
#include "pca9555_io.h"
#include "vl53l8cx_array.h"
#include "i2s_stereo_mic.h"
#include "tflm_dual_runtime.h"
#include "sensors_board.h"
#include "usb_telemetry.h"

extern void task_audio_start(void);
extern void task_vision_start(void);
extern void orchestrator_start(void);

static const char *TAG = "app";

/* ------------------------------------------------------------------
 * GPIO strapping pins — CRITIQUE, doit être fait en tout premier
 * ------------------------------------------------------------------ */
static void init_strapping_pins(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << LEXA_TOF_NCS_0_GPIO)
                      | (1ULL << LEXA_TOF_NCS_1_GPIO)
                      | (1ULL << LEXA_TOF_NCS_2_GPIO)
                      | (1ULL << LEXA_TOF_NCS_3_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    /* CS inactive HIGH (SPI mode 3). GPIO 1 sert aussi d'UART0_TX au boot ROM
     * — le mettre HIGH évite que le bootloader ne soit muet. */
    gpio_set_level(LEXA_TOF_NCS_0_GPIO, 1);
    gpio_set_level(LEXA_TOF_NCS_1_GPIO, 1);
    gpio_set_level(LEXA_TOF_NCS_2_GPIO, 1);
    gpio_set_level(LEXA_TOF_NCS_3_GPIO, 1);
}

/* ------------------------------------------------------------------
 * app_main
 * ------------------------------------------------------------------ */
void app_main(void)
{
    ESP_LOGI(TAG, "mbh-firmware boot, node_id=0x%04X is_root=%d",
             APP_NODE_ID, APP_IS_ROOT);

    /* 1. Strapping pins en état sûr (AVANT tout le reste) */
    init_strapping_pins();

    /* 2. NVS + event loop */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 3–5. Drivers LexaCare (sous-ensemble selon le mode pour boot rapide) */
#if MBH_MODE_MESH_ONLY || MBH_MODE_TEST_MESH
    ESP_LOGI(TAG, "modes mesh : pas d'init capteurs LexaCare");
#elif MBH_MODE_DEBUG_MFCC || MBH_MODE_CAPTURE_AUDIO
    ESP_LOGI(TAG, "init I2S uniquement (MFCC debug ou capture audio)");
    {
        i2s_stereo_mic_config_t mic_cfg = {
            .bclk_gpio  = LEXA_I2S_BCLK_GPIO,
            .ws_gpio    = LEXA_I2S_WS_GPIO,
            .din_gpio   = LEXA_I2S_DIN_GPIO,
            .sample_rate_hz = LEXA_I2S_SAMPLE_RATE_HZ,
            .pcm_extra_downshift = (uint8_t)LEXA_I2S_PCM_EXTRA_DOWNSHIFT,
            .pcm_output_shift    = (uint8_t)LEXA_I2S_PCM_OUTPUT_SHIFT,
        };
        ESP_ERROR_CHECK(i2s_stereo_mic_init(&mic_cfg));
    }
#elif MBH_MODE_CAPTURE_LIDAR
    ESP_LOGI(TAG, "init ToF pour capture lidar");
    ESP_ERROR_CHECK(pca9555_io_init(LEXA_I2C_SDA_GPIO, LEXA_I2C_SCL_GPIO,
                                    LEXA_PCA9555_ADDR));
    vl53l8cx_array_config_t tof_cap = {
        .spi_clk_gpio  = LEXA_TOF_SPI_CLK_GPIO,
        .spi_mosi_gpio = LEXA_TOF_SPI_MOSI_GPIO,
        .spi_miso_gpio = LEXA_TOF_SPI_MISO_GPIO,
        .spi_freq_hz   = LEXA_TOF_SPI_FREQ_HZ,
        .ncs_gpios     = { LEXA_TOF_NCS_0_GPIO, LEXA_TOF_NCS_1_GPIO,
                           LEXA_TOF_NCS_2_GPIO, LEXA_TOF_NCS_3_GPIO },
        .power_en_gpio = LEXA_TOF_POWER_EN_GPIO,
    };
    ESP_ERROR_CHECK(vl53l8cx_array_init(&tof_cap));
#else
    ESP_LOGI(TAG, "init LexaCare stack");
    ESP_ERROR_CHECK(pca9555_io_init(LEXA_I2C_SDA_GPIO, LEXA_I2C_SCL_GPIO,
                                    LEXA_PCA9555_ADDR));
    (void)sensors_board_init();

    vl53l8cx_array_config_t tof_cfg = {
        .spi_clk_gpio  = LEXA_TOF_SPI_CLK_GPIO,
        .spi_mosi_gpio = LEXA_TOF_SPI_MOSI_GPIO,
        .spi_miso_gpio = LEXA_TOF_SPI_MISO_GPIO,
        .spi_freq_hz   = LEXA_TOF_SPI_FREQ_HZ,
        .ncs_gpios     = { LEXA_TOF_NCS_0_GPIO, LEXA_TOF_NCS_1_GPIO,
                           LEXA_TOF_NCS_2_GPIO, LEXA_TOF_NCS_3_GPIO },
        .power_en_gpio = LEXA_TOF_POWER_EN_GPIO,
    };
    ESP_ERROR_CHECK(vl53l8cx_array_init(&tof_cfg));

    ESP_ERROR_CHECK(tflm_dual_runtime_init());
    {
        i2s_stereo_mic_config_t mic_cfg = {
            .bclk_gpio  = LEXA_I2S_BCLK_GPIO,
            .ws_gpio    = LEXA_I2S_WS_GPIO,
            .din_gpio   = LEXA_I2S_DIN_GPIO,
            .sample_rate_hz = LEXA_I2S_SAMPLE_RATE_HZ,
            .pcm_extra_downshift = (uint8_t)LEXA_I2S_PCM_EXTRA_DOWNSHIFT,
            .pcm_output_shift    = (uint8_t)LEXA_I2S_PCM_OUTPUT_SHIFT,
        };
        ESP_ERROR_CHECK(i2s_stereo_mic_init(&mic_cfg));
    }
#if MBH_MODE_FULL && MBH_USB_TELEMETRY_STREAM
    usb_telemetry_init();
#endif
#endif

    /* 6. Mesh (si activé par le mode de build) */
#if !MBH_DISABLE_MESH
    ESP_LOGI(TAG, "init mesh stack");
    static const uint8_t pmk[16] = APP_ESPNOW_PMK;
    mesh_manager_config_t mcfg = {
        .node_id      = APP_NODE_ID,
        .wifi_channel = APP_WIFI_CHANNEL,
        .is_root      = (APP_IS_ROOT != 0),
    };
    memcpy(mcfg.primary_pmk, pmk, 16);
    ESP_ERROR_CHECK(mesh_manager_init(&mcfg));
#else
    ESP_LOGW(TAG, "mesh DISABLED for this build");
#endif

    /* 7. Tâches applicatives */
#if MBH_MODE_FULL
    /* Logs INFO/WARN sur le même port que stdout → octets parasites entre LXCS/LXCL/LXJS. */
#if MBH_USB_TELEMETRY_STREAM
    esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set("main_task", ESP_LOG_ERROR);
#endif
    /* Mode complet : audio, lidar + orchestrateur + mesh */
    task_audio_start();
    task_vision_start();
    orchestrator_start();
#if MBH_USB_TELEMETRY_STREAM
    usb_telemetry_start();
#endif
#elif MBH_MODE_CAPTURE_AUDIO
    ESP_LOGI(TAG, "CAPTURE AUDIO mode – streaming binaire sur USB");
    task_capture_start();
#elif MBH_MODE_CAPTURE_LIDAR
    ESP_LOGI(TAG, "CAPTURE LIDAR mode – streaming ToF sur USB");
    task_capture_start();
#elif MBH_MODE_MODEL_AUDIO
    /* Modèle audio uniquement – inference audio */
    ESP_LOGI(TAG, "MODEL AUDIO mode – inference audio uniquement");
    task_audio_start();
#elif MBH_MODE_MODEL_LIDAR
    /* Modèle lidar uniquement – inference vision */
    ESP_LOGI(TAG, "MODEL LIDAR mode – inference lidar uniquement");
    task_vision_start();
#elif MBH_MODE_MODEL_BOTH
    /* Modèle double (audio + lidar) – inference combinée */
    ESP_LOGI(TAG, "MODEL BOTH mode – inference audio + lidar");
    task_audio_start();
    task_vision_start();
    orchestrator_start();
#elif MBH_MODE_MESH_ONLY
    ESP_LOGI(TAG, "MESH ONLY mode – heartbeat BENCH");
    task_mesh_bench_start();
#elif MBH_MODE_TEST_MESH
    ESP_LOGI(TAG, "TEST MESH mode – envoi périodique + BENCH");
    task_mesh_bench_start();
#elif MBH_MODE_DEBUG_MFCC
    ESP_LOGI(TAG, "DEBUG_MFCC mode – harness mfcc_compute + BENCH");
    task_mfcc_debug_start();
#endif

#if MBH_USB_TELEMETRY_STREAM && MBH_MODE_FULL
    /* Déjà masqué par esp_log_level_set("*", ERROR) ; garder pour clarté si niveau relâché. */
    ESP_LOGD(TAG, "init done");
#else
    ESP_LOGI(TAG, "init done");
#endif
}
