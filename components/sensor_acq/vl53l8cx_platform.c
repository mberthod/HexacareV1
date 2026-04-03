/**
 * @file vl53l8cx_platform.c
 * @ingroup group_sensor_acq
 * @brief Couche plateforme VL53L8CX — SPI ESP-IDF (PAL pour l’ULD ST).
 *
 * Éléments critiques (datasheet VL53L8CX + contraintes ESP32) :
 *
 *   - Mode SPI 3 (CPOL=1, CPHA=1), NCS actif bas, MSB first (défaut driver).
 *   - Full-duplex : chaque octet d’horloge échange MOSI↔MISO. Avec `rx_buffer`
 *     NULL, le driver peut ne pas échantillonner MISO correctement → MISO
 *     « collé » à 0xFF (ligne relâchée). Toutes les transactions utilisent donc
 *     un buffer RX (données ignorées pour les écritures).
 *   - DMA du périphérique SPI : les buffers doivent être en **RAM interne**
 *     `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`. Un `malloc()` classique peut
 *     retourner de la **PSRAM** (CONFIG_SPIRAM_USE_MALLOC) : transferts
 *     silencieusement incorrects après le petit ping au boot (petits buffers
 *     parfois en interne).
 *   - Taille max : l’ULD télécharge le firmware par WrMulti de **0x8000** octets
 *     + 2 octets d’en-tête → **0x8002** (voir vl53l8cx_api.c). Le bus doit
 *     exposer `max_transfer_sz` ≥ cette valeur (hw_diag.c).
 *
 * Protocole registre 16 bits (big-endian) :
 *   - Écriture : TX=[addr_hi & 0x7F][addr_lo][d0..dN-1]
 *   - Lecture  : TX=[addr_hi | 0x80][addr_lo][0x00 × N] → RX ignore 2 premiers octets
 */

#include "vl53l8cx_platform.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "vl53l8cx_plat";

/** Aligné sur le plus gros WrMulti de l’ULD (firmware), + marge lecture ranging. */
#define MAX_SPI_TRANSFER     ((size_t)0x8000u + 2u)

static uint8_t *spi_dma_buf_alloc(size_t n)
{
    uint8_t *p = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!p) {
        ESP_LOGE(TAG, "alloc SPI DMA interne %" PRIu32 " o échoué (réserver DRAM ?)",
                 (uint32_t)n);
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
    return 0;
}

/* ================================================================
 * spi_xfer — full-duplex, tx et rx obligatoires (buffers DMA internes).
 * ================================================================ */
static esp_err_t spi_xfer(spi_device_handle_t spi,
                          const uint8_t *tx_buf, uint8_t *rx_buf, size_t length)
{
    if (!spi || !tx_buf || !rx_buf || length == 0 || length > MAX_SPI_TRANSFER) {
        ESP_LOGE(TAG, "spi_xfer args invalides (len=%u)", (unsigned)length);
        return ESP_ERR_INVALID_ARG;
    }
    spi_transaction_t t = {
        .length    = (uint32_t)(length * 8u),
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };
    return spi_device_polling_transmit(spi, &t);
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
        ESP_LOGE(TAG, "RdMulti len %" PRIu32 " > max %" PRIu32, (uint32_t)total,
                 (uint32_t)MAX_SPI_TRANSFER);
        return 1;
    }

    uint8_t *tx = spi_dma_buf_alloc(total);
    uint8_t *rx = spi_dma_buf_alloc(total);
    if (!tx || !rx) {
        spi_dma_buf_free(tx);
        spi_dma_buf_free(rx);
        return 1;
    }

    memset(tx, 0, total);
    tx[0] = (uint8_t)((RegisterAddress >> 8) | 0x80);
    tx[1] = (uint8_t)(RegisterAddress & 0xFF);

    esp_err_t ret = spi_xfer(p_platform->spi_dev, tx, rx, total);

    if (ret == ESP_OK) {
        memcpy(p_values, &rx[2], size);
    } else {
        ESP_LOGE(TAG, "RdMulti addr=0x%04X : %s", RegisterAddress, esp_err_to_name(ret));
    }

    spi_dma_buf_free(tx);
    spi_dma_buf_free(rx);
    return (ret == ESP_OK) ? 0u : 1u;
}

/* ================================================================
 * VL53L8CX_WrMulti — RX dummy en RAM DMA (ne pas passer rx_buffer = NULL).
 * ================================================================ */
uint8_t VL53L8CX_WrMulti(VL53L8CX_Platform *p_platform,
                         uint16_t RegisterAddress,
                         uint8_t *p_values, uint32_t size)
{
    const size_t total = 2u + (size_t)size;
    if (total > MAX_SPI_TRANSFER) {
        ESP_LOGE(TAG, "WrMulti len %" PRIu32 " > max %" PRIu32, (uint32_t)total,
                 (uint32_t)MAX_SPI_TRANSFER);
        return 1;
    }

    uint8_t *tx = spi_dma_buf_alloc(total);
    uint8_t *rx = spi_dma_buf_alloc(total);
    if (!tx || !rx) {
        spi_dma_buf_free(tx);
        spi_dma_buf_free(rx);
        return 1;
    }

    tx[0] = (uint8_t)((RegisterAddress >> 8) & 0x7F);
    tx[1] = (uint8_t)(RegisterAddress & 0xFF);
    memcpy(&tx[2], p_values, size);
    memset(rx, 0, total);

    esp_err_t ret = spi_xfer(p_platform->spi_dev, tx, rx, total);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WrMulti addr=0x%04X : %s", RegisterAddress, esp_err_to_name(ret));
    }

    spi_dma_buf_free(tx);
    spi_dma_buf_free(rx);
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
    vTaskDelay(pdMS_TO_TICKS(TimeMs));
    return 0;
}
