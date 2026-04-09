/**
 * @file hw_diag.c
 * @ingroup group_hw_diag
 * @brief Diagnostic matériel au démarrage — I2C LIDAR + UART Radar.
 *
 * Exécuté avant les tâches FreeRTOS dans app_main().
 * Initialise les bus I2C partagés et les stocke dans sys_context_t
 * pour réutilisation par sensor_acq sans réinitialisation.
 */

#include "hw_diag.h"
#include "lexacare_config.h"
#include "pins_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "hw_diag";

/* ================================================================
 * Paramètres I2C (PCA9555 uniquement)
 * ================================================================ */
#define I2C_BUS_FREQ_HZ         400000   /**< 400 kHz pour le PCA9555 */
#define I2C_TIMEOUT_MS          10
#define LIDAR_LPN_DELAY_MS      5        /**< Délai après LPn=1 avant test SPI */

/* ================================================================
 * Paramètres SPI LIDAR (fréquence : pins_config.h → LIDAR_SPI_FREQ_HZ)
 * ================================================================ */
/* Doit couvrir le WrMulti firmware ULD : 0x8000 octets + 2 octets d’en-tête (vl53l8cx_api.c). */
#define LIDAR_SPI_MAX_XFER      ((int)((0x8000u + 2u)))

/* Registre Model ID du VL53L8CX (0x7FFF) → valeur attendue 0xF0 */
#define VL53L8CX_REG_MODEL_ID   0x7FFF
#define VL53L8CX_MODEL_ID       0xF0

/* Table des NCS GPIO par index capteur */
static const int s_ncs_pins[LIDAR_NUM_FRONT] = {
    PIN_LIDAR_NCS0,
    PIN_LIDAR_NCS1,
    PIN_LIDAR_NCS2,
    PIN_LIDAR_NCS3,
};

/* ================================================================
 * Paramètres UART Radar
 * ================================================================ */
#define RADAR_UART_PORT         UART_NUM_2
#define RADAR_BAUD_RATE         1382400
#define RADAR_RX_BUF_SIZE       512
#define RADAR_DETECT_TIMEOUT_MS 2000
#define TF_SOF                  0x01     /**< TinyFrame Start Of Frame */

/* Mapping NCS array index → masque LPn PCA9555 Port 0.
 *   index 0 → NCS0 (GPIO1)  → LIDAR 1 → LPn_1 = IO0.6
 *   index 1 → NCS1 (GPIO2)  → LIDAR 2 → LPn_2 = IO0.5
 *   index 2 → NCS2 (GPIO42) → LIDAR 3 → LPn_3 = IO0.4
 *   index 3 → NCS3 (GPIO41) → LIDAR 4 → LPn_4 = IO0.3 */
static const uint8_t s_pca_lpn_bit[LIDAR_NUM_FRONT] = {
    PCA9555_BIT_LPN1,  /* index 0 → LIDAR 1 → IO0.6 */
    PCA9555_BIT_LPN2,  /* index 1 → LIDAR 2 → IO0.5 */
    PCA9555_BIT_LPN3,  /* index 2 → LIDAR 3 → IO0.4 */
    PCA9555_BIT_LPN4,  /* index 3 → LIDAR 4 → IO0.3 */
};

/* État courant des registres de sortie du PCA9555 (Port 0 et Port 1) */
static uint8_t s_pca_out  = 0x00;   /* Port 0 — LPn LIDAR */
static uint8_t s_pca1_out = 0x00;   /* Port 1 — alimentations sous-systèmes */

/* Handle PCA9555 persistant (initialisé par hw_diag_run, réutilisé ensuite) */
static i2c_master_bus_handle_t s_pca_bus = NULL;

/* Device handle PCA9555 — créé UNE SEULE FOIS dans pca9555_init().
 * Évite d'appeler i2c_master_bus_add_device() à chaque écriture,
 * ce qui causait des allocations heap après WiFi → LoadStoreAlignment. */
static i2c_master_dev_handle_t s_pca_dev = NULL;

/* Mutex + buffer TX aligné : l’I2C maître IDF peut DMA depuis le buffer ;
 * un uint8_t tx[2] sur la pile est parfois mal aligné → corruptions / panic
 * spinlock. Partagé entre app_main (Core 0) et Task_Sensor_Acq. */
static SemaphoreHandle_t s_pca_i2c_mutex = NULL;
static uint8_t s_pca_i2c_tx[4] __attribute__((aligned(4)));

/* ================================================================
 * diag_init_i2c_bus (interne)
 * @brief Initialise un bus I2C master avec les paramètres standard.
 *
 * @param port     Numéro de port I2C (I2C_NUM_0 ou I2C_NUM_1).
 * @param sda_pin  Broche SDA.
 * @param scl_pin  Broche SCL.
 * @param handle   Pointeur vers le handle de bus à remplir.
 * @return ESP_OK si succès.
 * ================================================================ */
static esp_err_t diag_init_i2c_bus(i2c_port_num_t port,
                                    int sda_pin, int scl_pin,
                                    i2c_master_bus_handle_t *handle)
{
    const i2c_master_bus_config_t cfg = {
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .i2c_port                     = port,
        .sda_io_num                   = sda_pin,
        .scl_io_num                   = scl_pin,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&cfg, handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec création bus I2C%d : %s", port, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Bus I2C%d initialisé (SDA=%d, SCL=%d, %d Hz)",
                 port, sda_pin, scl_pin, I2C_BUS_FREQ_HZ);
    }
    return ret;
}

/* ================================================================
 * diag_init_spi_lidars (interne)
 * @brief Initialise le bus SPI et ajoute les 4 devices LIDAR.
 *
 * Chaque LIDAR a son propre NCS GPIO → handle SPI individuel stocké
 * dans ctx->lidar_spi[i] pour réutilisation par sensor_acq.
 *
 * @param ctx Contexte système (lidar_spi[] rempli en sortie).
 * @return ESP_OK si succès.
 * ================================================================ */
static esp_err_t diag_init_spi_lidars(sys_context_t *ctx)
{
    const spi_bus_config_t bus_cfg = {
        .mosi_io_num     = PIN_LIDAR_MOSI,
        .miso_io_num     = PIN_LIDAR_MISO,
        .sclk_io_num     = PIN_LIDAR_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LIDAR_SPI_MAX_XFER,
    };

    esp_err_t ret = spi_bus_initialize(LIDAR_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init échoué : %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Bus SPI LIDAR initialisé (CLK=%d, MOSI=%d, MISO=%d, demandé %lu Hz)",
             PIN_LIDAR_CLK, PIN_LIDAR_MOSI, PIN_LIDAR_MISO,
             (unsigned long)LIDAR_SPI_FREQ_HZ);

    for (int i = 0; i < LIDAR_NUM_FRONT; i++) {
        /* IDF 6 : tous les champs avant clock_speed_hz doivent être cohérents.
         * Source XTAL (~40 MHz) : base stable pour viser 1 MHz (évite parfois ~16 MHz
         * si la chaîne APB / prédiv ne correspond pas au calcul attendu). */
        const spi_device_interface_config_t dev_cfg = {
            .command_bits   = 0,
            .address_bits   = 0,
            .dummy_bits     = 0,
            .mode           = 3, /* CPOL=1, CPHA=1 (VL53L8CX, datasheet) */
            .clock_source   = SPI_CLK_SRC_XTAL,
            .duty_cycle_pos = 128,
            .cs_ena_pretrans = 0, /* full-duplex : ignoré par le driver IDF */
            .cs_ena_posttrans = 2, /* ~2 demi-périodes SCK après dernier bit (marge NCS) */
            .clock_speed_hz = (int)LIDAR_SPI_FREQ_HZ,
            /* MISO : marge trajet PCB + esclave ; 0 laissait parfois FF si sample trop tôt */
            .input_delay_ns = 50,
            .spics_io_num   = s_ncs_pins[i],
            .flags          = 0,
            .queue_size     = 1,
        };

        ret = spi_bus_add_device(LIDAR_SPI_HOST, &dev_cfg, &ctx->lidar_spi[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPI add device LIDAR[%d] (NCS=GPIO%d) échoué : %s",
                     i, s_ncs_pins[i], esp_err_to_name(ret));
            return ret;
        }

        int eff_khz = 0;
        if (spi_device_get_actual_freq(ctx->lidar_spi[i], &eff_khz) == ESP_OK) {
            ESP_LOGI(TAG, "LIDAR[%d] SPI device OK (NCS=GPIO%d) — horloge effective ~%d kHz",
                     i, s_ncs_pins[i], eff_khz);
        } else {
            ESP_LOGI(TAG, "LIDAR[%d] SPI device ajouté (NCS=GPIO%d)",
                     i, s_ncs_pins[i]);
        }
    }

    return ESP_OK;
}

/* ================================================================
 * diag_spi_ping_lidar (interne)
 * @brief Vérifie la présence d'un VL53L8CX via lecture du Model ID.
 *
 * Lit le registre 0x7FFF qui doit retourner 0xF0 si le capteur répond.
 * En STUB mode (sans ULD), retourne true si la transaction SPI réussit
 * et que MISO n'est pas en flottant (0xFF = absent probable).
 *
 * @param spi Handle SPI du capteur.
 * @return true si capteur détecté.
 * ================================================================ */
static bool diag_spi_ping_lidar(spi_device_handle_t spi)
{
    /* Lecture registre 0x7FFF : TX=[0xFF][0xFF][dummy], RX=[xx][xx][model_id]
     * (bit15=1 → lecture, adresse 0x7FFF → addr_hi=0x7F|0x80=0xFF, addr_lo=0xFF)
     * Valeur attendue : RX[2] = 0xF0 (Model ID VL53L8CX selon datasheet).
     * RX[2] = 0xFF → MISO flottant (capteur absent ou LPn=0). */
    const uint8_t tx[3] = {0xFF, 0xFF, 0x00};
    uint8_t rx[3] = {0x55, 0x55, 0x55};   /* valeur initiale pour détecter si non écrit */

    ESP_LOGI(TAG, "    ping SPI → TX: %02X %02X %02X", tx[0], tx[1], tx[2]);

    spi_transaction_t t = {
        .length    = sizeof(tx) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };

    esp_err_t ret = spi_device_polling_transmit(spi, &t);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "    ping SPI ERREUR : %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "    ping SPI ← RX: %02X %02X %02X  (ModelID attendu=0xF0)",
             rx[0], rx[1], rx[2]);

    /* Interprétation de RX[2] :
     *   0xF0 = Model ID VL53L8CX confirmé (LPn=1, sensor présent)
     *   0xFF = MISO flottant HAUT (pull-up présent, capteur absent ou LPn=0)
     *   0x00 = MISO tiré vers GND sur ce PCB (pas de pull-up, LPn=0 → sensor tristate → GND)
     *   autre = sensor en cours de boot ROM (LPn vient d'être activé) — ULD gérera ça
     *
     * IMPORTANT : sur ce PCB, MISO est tiré vers GND quand aucun capteur ne drive.
     * Ne pas interpréter 0x00 comme "présent" — réserver ce statut à ULD non chargé (début boot).
     */
    bool present = false;
    if (rx[2] == 0xF0) {
        ESP_LOGI(TAG, "    → ModelID=0xF0 CONFIRMÉ (capteur présent et LPn=1)");
        present = true;
    } else if (rx[2] == 0xFF) {
        ESP_LOGW(TAG, "    → RX=0xFF : MISO flottant HAUT — capteur absent ou LPn=0");
        present = false;
    } else if (rx[2] == 0x00) {
        ESP_LOGW(TAG, "    → RX=0x00 : MISO tiré vers GND (LPn=0, pas de pull-up PCB) — absent");
        present = false;
    } else {
        /* Valeur non-nulle non-0xF0 : capteur en boot ROM (LPn vient d'être activé).
         * L'ULD gère le reste de la séquence de boot. On le considère "répondant". */
        ESP_LOGI(TAG, "    → RX=0x%02X : capteur en boot ROM (LPn=1, ULD non encore chargé) — OK",
                 rx[2]);
        present = true;
    }

    return present;
}

/* ================================================================
 * pca9555_write_reg (interne)
 * @brief Écrit un octet dans un registre du PCA9555D via I2C.
 *
 * Utilise s_pca_dev créé UNE SEULE FOIS par pca9555_init().
 * Pas d'allocation heap à chaque appel → safe après WiFi init.
 *
 * @param bus  Ignoré (conservé pour compat. — utilise s_pca_dev).
 * @param reg  Registre cible.
 * @param val  Valeur à écrire.
 * @return ESP_OK si succès.
 * ================================================================ */
static esp_err_t pca9555_write_reg(i2c_master_bus_handle_t bus,
                                    uint8_t reg, uint8_t val)
{
    (void)bus;  /* s_pca_dev est utilisé directement */

    if (!s_pca_dev) {
        ESP_LOGE(TAG, "pca9555_write_reg : device non initialisé");
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_pca_i2c_mutex) {
        ESP_LOGE(TAG, "pca9555_write_reg : mutex non créé");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_pca_i2c_mutex, portMAX_DELAY);
    s_pca_i2c_tx[0] = reg;
    s_pca_i2c_tx[1] = val;
    esp_err_t ret = i2c_master_transmit(s_pca_dev, s_pca_i2c_tx, 2,
                                        pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    xSemaphoreGive(s_pca_i2c_mutex);
    return ret;
}

/* ================================================================
 * pca9555_init (interne)
 * @brief Initialise le PCA9555D : Port 0 + Port 1 en sortie, tout à 0.
 *
 *   Port 0 : LPn LIDAR 0–4 (tous à 0 = reset global).
 *   Port 1 : alimentation sous-systèmes (tous à 0 = éteints).
 *
 * @param bus  Handle du bus I2C maître.
 * @return ESP_OK si succès.
 * ================================================================ */
static esp_err_t pca9555_init(i2c_master_bus_handle_t bus)
{
    if (!s_pca_i2c_mutex) {
        s_pca_i2c_mutex = xSemaphoreCreateMutex();
        if (!s_pca_i2c_mutex) {
            ESP_LOGE(TAG, "PCA9555 : création mutex I2C échouée");
            return ESP_ERR_NO_MEM;
        }
    }

    /* Création du device handle PCA9555 — UNE SEULE FOIS.
     * Alloué ici (avant WiFi) pour éviter tout problème de heap après.
     * Réutilisé par pca9555_write_reg() sans nouvelle allocation. */
    if (!s_pca_dev) {
        const i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = PCA9555_I2C_ADDR,
            .scl_speed_hz    = I2C_BUS_FREQ_HZ,
        };
        esp_err_t ret_dev = i2c_master_bus_add_device(bus, &dev_cfg, &s_pca_dev);
        if (ret_dev != ESP_OK) {
            ESP_LOGE(TAG, "PCA9555 add_device échoué : %s", esp_err_to_name(ret_dev));
            return ret_dev;
        }
    }

    /* --- Port 0 ---
     * IO0.0 = sortie (DIST_SHUTDOWN)
     * IO0.1, IO0.2 = entrée Hi-Z (GPIO radar LD6002, non utilisés)
     * IO0.3 – IO0.7 = sorties (LPn_4, LPn_3, LPn_2, LPn_1, RADAR_LPN)
     * Config : 0 = sortie → PCA9555_CFG0_MASK = 0x06 (bits 1 et 2 en entrée) */
    esp_err_t ret = pca9555_write_reg(bus, PCA9555_REG_CONFIG_0, PCA9555_CFG0_MASK);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCA9555 config Port0 échoué : %s", esp_err_to_name(ret));
        return ret;
    }

    /* Valeur initiale Port 0 :
     *   IO0.0 (DIST_SHUTDOWN) = 0 → VL53L0X actif (non mis en veille)
     *   IO0.3–IO0.7 (LPn + RADAR_LPN) = 0 → tous les LIDARs en reset */
    s_pca_out = 0x00;
    ret = pca9555_write_reg(bus, PCA9555_REG_OUTPUT_0, s_pca_out);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCA9555 output Port0 échoué : %s", esp_err_to_name(ret));
        return ret;
    }

    /* --- Port 1 ---
     * IO1.0–IO1.3 = entrée Hi-Z (non utilisés)
     * IO1.4–IO1.7 = sorties (POWER_FAN, POWER_RADAR, POWER_MIC, POWER_MLX)
     * Config : PCA9555_CFG1_MASK = 0x0F (bits 0–3 en entrée, bits 4–7 en sortie) */
    ret = pca9555_write_reg(bus, PCA9555_REG_CONFIG_1, PCA9555_CFG1_MASK);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCA9555 config Port1 échoué : %s", esp_err_to_name(ret));
        return ret;
    }

    /* Valeur initiale Port 1 : toutes alimentations éteintes */
    ret = pca9555_write_reg(bus, PCA9555_REG_OUTPUT_1, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCA9555 output Port1 échoué : %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "PCA9555 initialisé (@0x%02X, SDA=%d, SCL=%d) — Port0/1 configurés",
             PCA9555_I2C_ADDR, PIN_I2C0_SDA, PIN_I2C0_SCL);
    return ESP_OK;
}

/* ================================================================
 * pca9555_set_lpn (interne)
 * @brief Active ou désactive le LPn d'un capteur via le PCA9555.
 *
 * @param bus    Handle du bus I2C_NUM_0.
 * @param idx    Index du capteur (1–4).
 * @param active true → LPn=1 (actif), false → LPn=0 (reset).
 * @return ESP_OK si succès.
 * ================================================================ */
static esp_err_t pca9555_set_lpn(i2c_master_bus_handle_t bus,
                                   int idx, bool active)
{
    /* idx est un index tableau (0 = LIDAR 1 hardware, 3 = LIDAR 4 hardware) */
    if (idx < 0 || idx >= LIDAR_NUM_FRONT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (active) {
        s_pca_out |=  s_pca_lpn_bit[idx];
    } else {
        s_pca_out &= ~s_pca_lpn_bit[idx];
    }
    return pca9555_write_reg(bus, PCA9555_REG_OUTPUT_0, s_pca_out);
}

/* ================================================================
 * diag_scan_lidars (interne)
 * @brief Active les LIDARs un par un via LPn (PCA9555) et les teste en SPI.
 *
 * Tous les LPn sont gérés par le PCA9555 :
 *   LIDAR 0 : IO7, carte radar — non connecté, ignoré.
 *   LIDAR 1–3 : IO3/IO5/IO4.
 * Pas de réassignation d'adresse : chaque capteur a son NCS SPI dédié.
 *
 * @param ctx      Contexte système (lidar_ok[] rempli en sortie).
 * @param pca_bus  Handle du bus I2C PCA9555 (NULL = PCA9555 non disponible).
 * ================================================================ */
static void diag_scan_lidars(sys_context_t *ctx,
                               i2c_master_bus_handle_t pca_bus)
{
    /* Initialisation PCA9555 : Port 0 + Port 1 en sortie, tout à 0 */
    if (pca_bus) {
        esp_err_t ret = pca9555_init(pca_bus);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "PCA9555 inaccessible — LPn non contrôlables");
            pca_bus = NULL;
        }
    }

    /* --- Validation bus SPI AVANT toute manipulation LPN ─────────────────
     * Envoie une trame de test sur NCS0 (LIDAR 1) pour confirmer que
     * GPIO17 (CLK) est bien piloté par le contrôleur SPI.
     * Le VL53L8CX ne répondra pas (LPN=0, pas de firmware ULD chargé)
     * mais le CLK doit impérativement bouger. ----------------------------*/
    {
        const uint8_t tx_test[3] = {0xFF, 0xFF, 0x00};
        uint8_t rx_test[3]       = {0};
        spi_transaction_t t_test = {
            .length    = sizeof(tx_test) * 8,
            .tx_buffer = tx_test,
            .rx_buffer = rx_test,
        };
        esp_err_t spi_test = spi_device_polling_transmit(ctx->lidar_spi[0], &t_test);
        if (spi_test == ESP_OK) {
            ESP_LOGI(TAG, "Bus SPI LIDAR validé — GPIO%d (CLK) fonctionne",
                     PIN_LIDAR_CLK);
        } else {
            ESP_LOGE(TAG, "Bus SPI LIDAR ERREUR : %s — vérifier GPIO%d",
                     esp_err_to_name(spi_test), PIN_LIDAR_CLK);
        }
    }

    vTaskDelay(pdMS_TO_TICKS(10)); /* Stabilisation après reset global */

    /* --- Scan séquentiel des 4 LIDARs (index 0–3 = LIDAR 1–4 hardware) ---
     *
     * Seuls les LIDARs présents dans LEXACARE_LIDAR_ACTIVE_MASK reçoivent
     * LPn=1. Les autres restent en reset (LPn=0) pour ne pas consommer
     * d'énergie ni parasiter le bus.
     *
     * RÈGLE : le ping SPI est TOUJOURS exécuté (génère des impulsions CLK).
     * Le LIDAR ne répondra que si LPn=1 ET ULD chargé — les capteurs en
     * reset apparaîtront "absents", ce qui est le comportement attendu.
     * -------------------------------------------------------------------*/
    for (int i = 0; i < LIDAR_NUM_FRONT; i++) {

        /* Activation LPn via PCA9555 — uniquement pour les LIDARs actifs */
        const bool is_active = (LEXACARE_LIDAR_ACTIVE_MASK & (1u << i)) != 0u;
        bool lpn_ok = false;

        if (pca_bus && is_active) {
            esp_err_t lpn_ret = pca9555_set_lpn(pca_bus, i, true);
            if (lpn_ret == ESP_OK) {
                lpn_ok = true;
            } else {
                ESP_LOGW(TAG, "LIDAR%d : PCA9555 LPn échoué (%s) — SPI testé quand même",
                         i + 1, esp_err_to_name(lpn_ret));
            }
        } else if (!is_active) {
            /* LPn reste à 0 : capteur maintenu en reset matériel */
            ESP_LOGI(TAG, "LIDAR%d : hors masque actif — LPn maintenu à 0 (reset)",
                     i + 1);
        }

        vTaskDelay(pdMS_TO_TICKS(LIDAR_LPN_DELAY_MS));

        /* Test SPI inconditionnnel — génère toujours des impulsions sur CLK */
        ctx->lidar_ok[i] = diag_spi_ping_lidar(ctx->lidar_spi[i]);

        ESP_LOGI(TAG, "LIDAR%d SPI %-6s | NCS=GPIO%-2d | LPn=IO0.%d (%s)",
                 i + 1,
                 ctx->lidar_ok[i] ? "OK" : "absent",
                 s_ncs_pins[i],
                 6 - i,   /* LPn1=IO0.6, LPn2=IO0.5, LPn3=IO0.4, LPn4=IO0.3 */
                 is_active ? (lpn_ok ? "actif" : "échec") : "désactivé (mask)");
    }
}

/* ================================================================
 * diag_test_radar (interne)
 * @brief Configure UART2 et attend une trame TinyFrame valide (SOF=0x01).
 *
 * @param ctx Contexte système (radar_ok rempli).
 * ================================================================ */
static void diag_test_radar(sys_context_t *ctx)
{
    const uart_config_t uart_cfg = {
        .baud_rate           = RADAR_BAUD_RATE,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .source_clk          = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(RADAR_UART_PORT,
                                         RADAR_RX_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(RADAR_UART_PORT, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(RADAR_UART_PORT,
                                  PIN_RADAR_TX, PIN_RADAR_RX,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "Attente trame LD6002 (%d ms, %d bauds)...",
             RADAR_DETECT_TIMEOUT_MS, RADAR_BAUD_RATE);

    uint8_t buf[RADAR_RX_BUF_SIZE];
    TickType_t deadline = xTaskGetTickCount() +
                          pdMS_TO_TICKS(RADAR_DETECT_TIMEOUT_MS);
    ctx->radar_ok = false;

    while (xTaskGetTickCount() < deadline) {
        int len = uart_read_bytes(RADAR_UART_PORT, buf, sizeof(buf),
                                   pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;
        }
        /* Recherche du SOF TinyFrame (0x01) dans les données reçues */
        for (int i = 0; i < len; i++) {
            if (buf[i] == TF_SOF && (len - i) >= 5) {
                ctx->radar_ok = true;
                ESP_LOGI(TAG, "Radar LD6002 : trame TinyFrame détectée (%d octets reçus)",
                         len);
                goto radar_done;
            }
        }
    }

radar_done:
    if (!ctx->radar_ok) {
        ESP_LOGW(TAG, "Radar LD6002 : aucune trame reçue dans le délai imparti");
    }

    /* Le driver UART reste installé (réutilisé par radar_driver dans sensor_acq) */
}

/* ================================================================
 * diag_print_json_report (interne)
 * @brief Formate et imprime le rapport de diagnostic en JSON.
 *
 * @param ctx     Contexte système contenant les résultats.
 * @param result  Masque hw_diag_result_t.
 * ================================================================ */
static void diag_print_json_report(const sys_context_t *ctx,
                                    hw_diag_result_t result)
{
    cJSON *root    = cJSON_CreateObject();
    cJSON *lidars  = cJSON_CreateArray();

    for (int i = 0; i < LIDAR_NUM_FRONT; i++) {
        cJSON *lidar = cJSON_CreateObject();
        cJSON_AddNumberToObject(lidar, "id", i);
        cJSON_AddBoolToObject(lidar, "ok", ctx->lidar_ok[i]);
        cJSON_AddNumberToObject(lidar, "ncs_gpio", s_ncs_pins[i]);
        cJSON_AddItemToArray(lidars, lidar);
    }

    cJSON_AddItemToObject(root, "lidars", lidars);
    cJSON_AddBoolToObject(root, "radar_ok", ctx->radar_ok);
    cJSON_AddNumberToObject(root, "result_mask", (double)result);

    char *json_str = cJSON_Print(root);
    if (json_str) {
        ESP_LOGI(TAG, "Rapport diagnostic matériel :\n%s", json_str);
        cJSON_free(json_str);
    }
    cJSON_Delete(root);
}

/* ================================================================
 * hw_diag_run
 * @brief Point d'entrée du diagnostic matériel.
 *
 * @param ctx Pointeur vers le contexte système à remplir.
 * @return    Masque hw_diag_result_t (0 = tout OK).
 * ================================================================ */
hw_diag_result_t hw_diag_run(sys_context_t *ctx)
{
    ESP_LOGI(TAG, "=== Démarrage du diagnostic matériel ===");

    /* --- Bus SPI LIDAR : initialise le bus + 4 devices NCS dédiés --- */
    ESP_ERROR_CHECK(diag_init_spi_lidars(ctx));

    /* --- Bus I2C PCA9555 : PERSISTANT (contrôle LPn + alimentation) ---
     * Ne pas détruire ce bus — il est réutilisé par hw_diag_pca9555_set_power()
     * pour piloter les alimentations (POWER_MIC, POWER_RADAR, etc.) */
    esp_err_t pca_ret = diag_init_i2c_bus(I2C_NUM_0,
                                            PIN_I2C0_SDA, PIN_I2C0_SCL,
                                            &s_pca_bus);
    if (pca_ret != ESP_OK) {
        ESP_LOGW(TAG, "PCA9555 I2C init échoué — LPn et alimentations non contrôlables");
    }
    i2c_master_bus_handle_t pca_bus = s_pca_bus; /* alias local pour lisibilité */

    /* --- Lecture du sélecteur ROOT/NODE --- */
    gpio_set_pull_mode(PIN_ROOT_SEL, GPIO_PULLUP_ONLY);
    gpio_set_direction(PIN_ROOT_SEL, GPIO_MODE_INPUT);
    ctx->is_root_node = (gpio_get_level(PIN_ROOT_SEL) == 0);
    ESP_LOGI(TAG, "Rôle nœud : %s", ctx->is_root_node ? "ROOT (passerelle)" : "NODE");

    /* --- Scan des LIDARs via SPI --- */
    memset(ctx->lidar_ok, 0, sizeof(ctx->lidar_ok));
    diag_scan_lidars(ctx, pca_bus);

    /* Bus PCA9555 conservé dans s_pca_bus — réutilisé par hw_diag_pca9555_set_power() */

    /* --- Test du radar (uniquement si connecté) --- */
#if LEXACARE_ENABLE_RADAR
    diag_test_radar(ctx);
#else
    ctx->radar_ok = false;
    ESP_LOGI(TAG, "Radar LD6002 ignoré (LEXACARE_ENABLE_RADAR=0) — boot +2 s économisés");
#endif

    /* --- Calcul du masque de résultat --- */
    hw_diag_result_t result = HW_DIAG_OK;
    int lidar_count = 0;
    for (int i = 0; i < LIDAR_NUM_FRONT; i++) {
        if (ctx->lidar_ok[i]) lidar_count++;
    }

    if (lidar_count == 0) {
        result |= HW_DIAG_LIDAR_ALL;
    } else if (lidar_count < LIDAR_NUM_FRONT) {
        result |= HW_DIAG_LIDAR_PARTIAL;
    }

#if LEXACARE_ENABLE_RADAR
    if (!ctx->radar_ok) {
        result |= HW_DIAG_RADAR_MISSING;
    }
#endif

    /* Rapport JSON sur la console */
    diag_print_json_report(ctx, result);

    ESP_LOGI(TAG, "=== Diagnostic terminé (mask=0x%02X) ===", result);
    return result;
}

/* ================================================================
 * hw_diag_init_sensor_bus
 * @brief Retourne le bus I2C_NUM_0 (SDA=11, SCL=12) déjà créé pour le PCA9555.
 *
 * HDC1080, BME280, etc. partagent le même bus physique que le PCA9555D @0x20
 * (adresses I2C différentes). Ne pas appeler i2c_new_master_bus deux fois sur
 * I2C_NUM_0 avec les mêmes broches.
 * ================================================================ */
esp_err_t hw_diag_init_sensor_bus(i2c_master_bus_handle_t *out_handle)
{
    if (!out_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_pca_bus) {
        *out_handle = s_pca_bus;
        ESP_LOGI(TAG, "Bus I2C capteurs = I2C0 partagé (SDA=%d, SCL=%d) avec PCA9555",
                 PIN_I2C0_SDA, PIN_I2C0_SCL);
        return ESP_OK;
    }

    /* hw_diag_run() n'a pas initialisé le PCA (ex. test minimal) */
    return diag_init_i2c_bus(I2C_NUM_0, PIN_I2C0_SDA, PIN_I2C0_SCL, out_handle);
}

/* ================================================================
 * hw_diag_pca9555_set_power
 * ================================================================ */
esp_err_t hw_diag_set_lidar_lpn(int lidar_idx, bool active)
{
    if (!s_pca_bus) {
        ESP_LOGW(TAG, "set_lidar_lpn[%d] : PCA9555 non initialisé", lidar_idx);
        return ESP_ERR_INVALID_STATE;
    }
    return pca9555_set_lpn(s_pca_bus, lidar_idx, active);
}

esp_err_t hw_diag_pca9555_set_power(uint8_t port1_bit, bool enable)
{
    if (!s_pca_bus) {
        ESP_LOGE(TAG, "pca9555_set_power : bus PCA9555 non initialisé");
        return ESP_ERR_INVALID_STATE;
    }

    if (enable) {
        s_pca1_out |=  port1_bit;
    } else {
        s_pca1_out &= ~port1_bit;
    }

    esp_err_t ret = pca9555_write_reg(s_pca_bus, PCA9555_REG_OUTPUT_1, s_pca1_out);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "PCA9555 Port1 → 0x%02X (%s bit 0x%02X)",
                 s_pca1_out, enable ? "SET" : "CLR", port1_bit);
    } else {
        ESP_LOGE(TAG, "PCA9555 Port1 écriture échouée : %s", esp_err_to_name(ret));
    }
    return ret;
}
