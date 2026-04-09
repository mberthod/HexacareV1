/**
 * @file vl53l8cx_platform.c
 * @ingroup group_sensor_acq
 * @brief Couche plateforme VL53L8CX — SPI ESP-IDF (PAL pour l'ULD ST).
 *
 * Éléments critiques (datasheet VL53L8CX + contraintes ESP32) :
 *
 *   - Mode SPI 3 (CPOL=1, CPHA=1), NCS actif bas, MSB first (défaut driver).
 *   - Full-duplex : pour les **lectures** (RdMulti), TX+RX DMA internes sont
 *     obligatoires. Pour les **écritures** (WrMulti), `rx_buffer = NULL` évite
 *     d'allouer ~32 Ko de DRAM DMA par transfert firmware (ESP-IDF l'accepte).
 *   - DMA du périphérique SPI : les buffers doivent être en **RAM interne**
 *     `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`. Un `malloc()` classique peut
 *     retourner de la **PSRAM** (CONFIG_SPIRAM_USE_MALLOC) : transferts
 *     silencieusement incorrects après le petit ping au boot.
 *   - Taille max : l'ULD télécharge le firmware par WrMulti de **0x8000** octets
 *     + 2 octets d'en-tête → **0x8002** (voir vl53l8cx_api.c). Le bus doit
 *     exposer `max_transfer_sz` ≥ cette valeur (hw_diag.c).
 *   - Gros transferts (≥ SPI_INTR_THRESHOLD) : utilise spi_device_transmit
 *     (mode interrompu, DMA) au lieu de spi_device_polling_transmit (busy-wait)
 *     pour éviter que le busy-wait ne bloque le tick FreeRTOS et déclenche
 *     l'interrupt WDT (TG1WDT_SYS_RST). À 1 MHz, 32 Ko ≈ 262 ms de busy-wait,
 *     proche de CONFIG_ESP_INT_WDT_TIMEOUT_MS (300 ms défaut IDF 6.0).
 *
 * Protocole registre 16 bits (big-endian) :
 *   - Écriture : TX=[addr_hi & 0x7F][addr_lo][d0..dN-1]
 *   - Lecture  : TX=[addr_hi | 0x80][addr_lo][0x00 × N] → RX ignore 2 premiers octets
 *
 * Log SPI détaillé — format d'une ligne de transaction :
 *   [N] DIR reg=0xXXXX payload=Y o mode=INTR|POLL DMA_free=Z o
 *   [N] OK  T µs → RX[0..3]=AA BB CC DD  total_rd=W o      (lectures)
 *   [N] OK  T µs   TX[2..4]=AA BB CC      total_wr=W o      (écritures)
 *   [N] ERREUR <msg> après T µs — reg=0xXXXX len=Y
 *
 * Le log "avant" est toujours émis AVANT la transaction ; si un crash survient
 * pendant le transfert, ce log reste visible et identifie la transaction fautive.
 */

#include "vl53l8cx_platform.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "vl53l8cx_plat";

/** Aligné sur le plus gros WrMulti de l'ULD (firmware), + marge lecture ranging. */
#define MAX_SPI_TRANSFER     ((size_t)0x8000u + 2u)

/**
 * Seuil (octets totaux dans le buffer SPI, header compris) au-delà duquel on
 * bascule en mode interrompu (spi_device_transmit).
 *
 * À 1 MHz : SPI_INTR_THRESHOLD octets = SPI_INTR_THRESHOLD × 8 µs.
 * 8192 octets → ~65 ms (confortable sous le IWDT de 300 ms).
 */
#define SPI_INTR_THRESHOLD   ((size_t)8192u)

/**
 * Nombre maximum d'octets de payload affichés en hex dump par transaction.
 * - 0  : une seule ligne par transaction (recommandé — évite de saturer le FIFO
 *         USB JTAG qui peut désactiver les interruptions → IWDT)
 * - 8  : affiche les 8 premiers octets de données
 * - 16 : affiche les 16 premiers octets de données
 *
 * La saturation du FIFO TX USB JTAG (64 o) par des centaines de lignes de log
 * pendant l'init ULD peut causer un busy-poll dans le driver USB → 2 s de
 * starvation tick → TG1WDT_SYS_RST. Garder à 0 jusqu'à résolution du reboot.
 */
#define SPI_DBG_DUMP_MAX   0u

/* Compteurs globaux pour suivi debug des transactions */
static uint32_t s_xfer_count = 0;
static uint32_t s_wr_total   = 0;  /**< Octets de données écrits (payload) */
static uint32_t s_rd_total   = 0;  /**< Octets de données lus    (payload) */

/* ================================================================
 * spi_dma_buf_alloc / free
 * ================================================================ */
static uint8_t *spi_dma_buf_alloc(size_t n)
{
    size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint8_t *p = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!p) {
        ESP_LOGE(TAG, "alloc DMA interne %u o ÉCHOUÉ — DMA libre=%u o",
                 (unsigned)n, (unsigned)free_dma);
    }
    return p;
}

static void spi_dma_buf_free(uint8_t *p)
{
    if (p) {
        heap_caps_free(p);
    }
}

/* ================================================================
 * VL53L8CX_PlatformInit
 * ================================================================ */
uint8_t VL53L8CX_PlatformInit(VL53L8CX_Platform *p_platform)
{
    if (!p_platform->spi_dev) {
        ESP_LOGE(TAG, "PlatformInit : spi_dev NULL");
        return 1;
    }
    ESP_LOGI(TAG,
             "PlatformInit OK — spi_dev=%p | DMA interne libre=%u o | "
             "PSRAM libre=%u o",
             (void *)p_platform->spi_dev,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}

/* ================================================================
 * spi_xfer — full-duplex ; rx_buf peut être NULL pour les écritures (WrMulti).
 *
 * < SPI_INTR_THRESHOLD : polling (faible latence, acceptable)
 * ≥ SPI_INTR_THRESHOLD : interrompu (libère CPU, évite TG1WDT_SYS_RST)
 *
 * Log complet de chaque transaction :
 *   1. Entête + dump hex TX  AVANT l'envoi (visible si crash pendant le transfert)
 *   2. Durée + dump hex RX   APRÈS réception (ou message d'erreur)
 * ================================================================ */
static esp_err_t spi_xfer(spi_device_handle_t spi,
                           const uint8_t *tx_buf, uint8_t *rx_buf, size_t length)
{
    if (!spi || !tx_buf || length == 0 || length > MAX_SPI_TRANSFER) {
        ESP_LOGE(TAG, "spi_xfer args invalides — spi=%p tx=%p len=%u",
                 (void *)spi, (const void *)tx_buf, (unsigned)length);
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t xfer_id    = ++s_xfer_count;
    uint16_t reg        = ((uint16_t)(tx_buf[0] & 0x7Fu) << 8) | tx_buf[1];
    bool     is_rd      = (tx_buf[0] & 0x80u) != 0u;
    bool     use_intr   = (length >= SPI_INTR_THRESHOLD);
    size_t   payload_sz = (length > 2u) ? (length - 2u) : 0u;

    /* Log AVANT la transaction en DEBUG uniquement.
     * Ne pas utiliser ESP_LOGI ici : les centaines de transactions ULD
     * satureraient le FIFO TX USB JTAG (64 o) et le driver busy-poll
     * sans libérer les interruptions → IWDT (TG1WDT_SYS_RST, 2000 ms).
     * Passer à LOG_LEVEL=DEBUG pour voir toutes les transactions. */
    ESP_LOGD(TAG,
             "#%"PRIu32" %s 0x%04X %u o %s",
             xfer_id, is_rd ? "RD" : "WR", reg,
             (unsigned)payload_sz, use_intr ? "INTR" : "POLL");

    spi_transaction_t t = {
        .length    = (uint32_t)(length * 8u),
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };

    int64_t   t0  = esp_timer_get_time();
    esp_err_t err;

    if (use_intr) {
        /* spi_device_transmit : bloque jusqu'à fin de DMA via semaphore.
         * Le CPU est libéré → le tick FreeRTOS tourne → IWDT content. */
        err = spi_device_transmit(spi, &t);
    } else {
        err = spi_device_polling_transmit(spi, &t);
    }

    int64_t elapsed_us = esp_timer_get_time() - t0;
    (void)esp_task_wdt_reset();
    taskYIELD();

    /* ── LOG RX : DEBUG pour les succès, ERROR pour les échecs ──────── */
    if (err == ESP_OK) {
        if (is_rd) {
            s_rd_total += (uint32_t)payload_sz;
        } else {
            s_wr_total += (uint32_t)payload_sz;
        }
        /* Résultat en DEBUG — pas de sortie USB en mode INFO normal */
        ESP_LOGD(TAG,
                 "#%"PRIu32" OK %"PRId64"µs %s wr=%"PRIu32" rd=%"PRIu32,
                 xfer_id, elapsed_us, is_rd ? "RD" : "WR",
                 s_wr_total, s_rd_total);
    } else {
        /* Erreurs toujours visibles — un seul log, pas de saturation USB */
        ESP_LOGE(TAG,
                 "SPI #%"PRIu32" ERREUR %s (%"PRId64"µs) reg=0x%04X len=%u %s",
                 xfer_id, esp_err_to_name(err), elapsed_us,
                 reg, (unsigned)length, use_intr ? "INTR" : "POLL");
    }

    return err;
}

/* ================================================================
 * VL53L8CX_RdMulti
 * ================================================================ */
uint8_t VL53L8CX_RdMulti(VL53L8CX_Platform *p_platform,
                          uint16_t RegisterAddress,
                          uint8_t *p_values, uint32_t size)
{
    const size_t total = 2u + (size_t)size;
    if (total > MAX_SPI_TRANSFER) {
        ESP_LOGE(TAG, "RdMulti 0x%04X : len %u > max %u — REJETÉ",
                 RegisterAddress, (unsigned)total, (unsigned)MAX_SPI_TRANSFER);
        return 1;
    }

    uint8_t *tx = spi_dma_buf_alloc(total);
    uint8_t *rx = spi_dma_buf_alloc(total);
    if (!tx || !rx) {
        ESP_LOGE(TAG, "RdMulti 0x%04X : alloc DMA ÉCHOUÉ (besoin=%u o)",
                 RegisterAddress, (unsigned)total);
        spi_dma_buf_free(tx);
        spi_dma_buf_free(rx);
        return 1;
    }

    memset(tx, 0, total);
    tx[0] = (uint8_t)((RegisterAddress >> 8) | 0x80u);
    tx[1] = (uint8_t)(RegisterAddress & 0xFFu);

    esp_err_t ret = spi_xfer(p_platform->spi_dev, tx, rx, total);

    if (ret == ESP_OK) {
        memcpy(p_values, &rx[2], size);
    } else {
        ESP_LOGE(TAG, "RdMulti 0x%04X : spi_xfer → %s",
                 RegisterAddress, esp_err_to_name(ret));
    }

    spi_dma_buf_free(tx);
    spi_dma_buf_free(rx);
    return (ret == ESP_OK) ? 0u : 1u;
}

/* ================================================================
 * VL53L8CX_WrMulti — TX DMA uniquement (rx_buffer NULL) pour économiser la DRAM.
 * ================================================================ */
uint8_t VL53L8CX_WrMulti(VL53L8CX_Platform *p_platform,
                          uint16_t RegisterAddress,
                          uint8_t *p_values, uint32_t size)
{
    const size_t total = 2u + (size_t)size;
    if (total > MAX_SPI_TRANSFER) {
        ESP_LOGE(TAG, "WrMulti 0x%04X : len %u > max %u — REJETÉ",
                 RegisterAddress, (unsigned)total, (unsigned)MAX_SPI_TRANSFER);
        return 1;
    }

    uint8_t *tx = spi_dma_buf_alloc(total);
    if (!tx) {
        ESP_LOGE(TAG, "WrMulti 0x%04X : alloc DMA ÉCHOUÉ (besoin=%u o)",
                 RegisterAddress, (unsigned)total);
        return 1;
    }

    tx[0] = (uint8_t)((RegisterAddress >> 8) & 0x7Fu);
    tx[1] = (uint8_t)(RegisterAddress & 0xFFu);
    memcpy(&tx[2], p_values, size);

    esp_err_t ret = spi_xfer(p_platform->spi_dev, tx, NULL, total);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WrMulti 0x%04X : spi_xfer → %s",
                 RegisterAddress, esp_err_to_name(ret));
    }

    spi_dma_buf_free(tx);
    return (ret == ESP_OK) ? 0u : 1u;
}

/* ================================================================
 * VL53L8CX_RdByte / VL53L8CX_WrByte
 * ================================================================ */
uint8_t VL53L8CX_RdByte(VL53L8CX_Platform *p_platform,
                         uint16_t RegisterAddress, uint8_t *p_value)
{
    return VL53L8CX_RdMulti(p_platform, RegisterAddress, p_value, 1);
}

uint8_t VL53L8CX_WrByte(VL53L8CX_Platform *p_platform,
                         uint16_t RegisterAddress, uint8_t value)
{
    return VL53L8CX_WrMulti(p_platform, RegisterAddress, &value, 1);
}

/* ================================================================
 * VL53L8CX_WaitMs
 * ================================================================ */
uint8_t VL53L8CX_WaitMs(VL53L8CX_Platform *p_platform, uint32_t TimeMs)
{
    (void)p_platform;

    /* DEBUG seulement — WaitMs est appelé 200× pendant le poll de boot */
    ESP_LOGD(TAG,
             "WaitMs(%"PRIu32"ms) xfers=%"PRIu32" wr=%"PRIu32" rd=%"PRIu32,
             TimeMs, s_xfer_count, s_wr_total, s_rd_total);

    uint32_t total_ms = TimeMs;
    /* Découper en tranches de 50 ms max pour laisser le scheduler respirer. */
    while (TimeMs > 0u) {
        uint32_t chunk = (TimeMs > 50u) ? 50u : TimeMs;
        vTaskDelay(pdMS_TO_TICKS(chunk));
        TimeMs -= chunk;
        taskYIELD();
    }

    /* Log de progression uniquement pour les attentes significatives (≥50 ms) */
    if (total_ms >= 50u) {
        ESP_LOGI(TAG, "WaitMs(%"PRIu32"ms) OK — wr=%"PRIu32"o rd=%"PRIu32"o (#xfers=%"PRIu32")",
                 total_ms, s_wr_total, s_rd_total, s_xfer_count);
    }
    ESP_LOGD(TAG, "WaitMs terminé");
    return 0;
}
