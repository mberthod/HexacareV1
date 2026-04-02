/**
 * @file vl53l8cx_platform.c
 * @ingroup group_sensor_acq
 * @brief Couche plateforme VL53L8CX — implémentation SPI via ESP-IDF.
 *
 * Protocole SPI VL53L8CX (mode 3, CPOL=1 CPHA=1) :
 *   Écriture N octets : TX=[addr_hi & 0x7F][addr_lo][d0..dN-1]
 *   Lecture  N octets : TX=[addr_hi | 0x80][addr_lo][0x00 × N]
 *                       RX=[xx][xx][d0..dN-1]  (2 premiers octets ignorés)
 *
 * Les handles SPI sont créés dans hw_diag.c (spi_bus_add_device) et
 * transmis via sys_context_t → VL53L8CX_Platform.spi_dev.
 */

#include "vl53l8cx_platform.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "vl53l8cx_plat";

#define PLATFORM_TIMEOUT_MS  100
/* Taille max d'un transfert SPI (2 octets header + 4096 données) */
#define MAX_SPI_TRANSFER     (4096 + 2)

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
 * spi_xfer (interne)
 * @brief Exécute un transfert SPI full-duplex avec allocation dynamique.
 *
 * @param spi      Handle SPI ESP-IDF du capteur.
 * @param tx_buf   Buffer TX (header + données, ou header + dummy pour lecture).
 * @param rx_buf   Buffer RX (même taille que TX).
 * @param length   Nombre total d'octets à transférer.
 * @return ESP_OK si succès.
 * ================================================================ */
static esp_err_t spi_xfer(spi_device_handle_t spi,
                            const uint8_t *tx_buf, uint8_t *rx_buf,
                            size_t length)
{
    spi_transaction_t t = {
        .length    = length * 8,   /* en bits */
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
    size_t total = 2 + (size_t)size;
    uint8_t *tx  = calloc(1, total);
    uint8_t *rx  = calloc(1, total);

    if (!tx || !rx) {
        ESP_LOGE(TAG, "RdMulti : malloc(%u) échoué", (unsigned)total);
        free(tx); free(rx);
        return 1;
    }

    /* Header de lecture : bit 15 = 1 */
    tx[0] = (uint8_t)((RegisterAddress >> 8) | 0x80);
    tx[1] = (uint8_t)(RegisterAddress & 0xFF);
    /* Octets restants laissés à 0x00 (dummy pour générer l'horloge) */

    esp_err_t ret = spi_xfer(p_platform->spi_dev, tx, rx, total);

    if (ret == ESP_OK) {
        memcpy(p_values, &rx[2], size); /* Sauter les 2 octets header */
    } else {
        ESP_LOGE(TAG, "RdMulti addr=0x%04X : %s",
                 RegisterAddress, esp_err_to_name(ret));
    }

    free(tx);
    free(rx);
    return (ret == ESP_OK) ? 0 : 1;
}

/* ================================================================
 * VL53L8CX_WrMulti
 * ================================================================ */
uint8_t VL53L8CX_WrMulti(VL53L8CX_Platform *p_platform,
                           uint16_t RegisterAddress,
                           uint8_t *p_values, uint32_t size)
{
    size_t total = 2 + (size_t)size;
    uint8_t *tx  = malloc(total);

    if (!tx) {
        ESP_LOGE(TAG, "WrMulti : malloc(%u) échoué", (unsigned)total);
        return 1;
    }

    /* Header d'écriture : bit 15 = 0 */
    tx[0] = (uint8_t)((RegisterAddress >> 8) & 0x7F);
    tx[1] = (uint8_t)(RegisterAddress & 0xFF);
    memcpy(&tx[2], p_values, size);

    esp_err_t ret = spi_xfer(p_platform->spi_dev, tx, NULL, total);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WrMulti addr=0x%04X : %s",
                 RegisterAddress, esp_err_to_name(ret));
    }

    free(tx);
    return (ret == ESP_OK) ? 0 : 1;
}

/* ================================================================
 * VL53L8CX_RdByte / VL53L8CX_WrByte (délèguent à Multi)
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
