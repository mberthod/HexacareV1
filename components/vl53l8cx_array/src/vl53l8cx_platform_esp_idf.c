/* Couche plateforme SPI ESP-IDF pour l’ULD VL53L8CX.
 *
 * Les appels ULD (RdMulti / WrMulti) découpent les transferts en chunks : sans mutex
 * bus global, une autre instance capteur peut prendre le bus entre deux chunks →
 * données corrompues / bandes fantômes. Ici tout accès SPI est sérialisé.
 *
 * Tampons statiques alignés (DRAM) : évite malloc en charge utile + adresses stables
 * pour le contrôleur SPI. */
#include <stdlib.h>
#include <string.h>
#include "vl53l8cx_platform.h"
#include "vl53l8cx_platform_esp_idf.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define SPI_TMP_MAX (VL53L8CX_COMMS_CHUNK_SIZE + 2U)

static SemaphoreHandle_t s_bus_mutex;
static uint8_t s_spi_tx[SPI_TMP_MAX] __attribute__((aligned(4)));
static uint8_t s_spi_rx[SPI_TMP_MAX] __attribute__((aligned(4)));

void vl53l8cx_platform_esp_bus_lock_init(void)
{
    if (s_bus_mutex != NULL) {
        return;
    }
    s_bus_mutex = xSemaphoreCreateMutex();
}

static void bus_take(void)
{
    if (s_bus_mutex == NULL) {
        vl53l8cx_platform_esp_bus_lock_init();
    }
    (void)xSemaphoreTake(s_bus_mutex, portMAX_DELAY);
}

static void bus_give(void)
{
    (void)xSemaphoreGive(s_bus_mutex);
}

uint8_t VL53L8CX_io_write(void *handle, uint16_t RegisterAddress, uint8_t *p_values, uint32_t size)
{
    spi_device_handle_t dev = (spi_device_handle_t)handle;
    if (!dev || !p_values) {
        return 1;
    }

    bus_take();
    for (uint32_t position = 0; position < size; position += VL53L8CX_COMMS_CHUNK_SIZE) {
        uint32_t data_size = size - position;
        if (data_size > VL53L8CX_COMMS_CHUNK_SIZE) {
            data_size = VL53L8CX_COMMS_CHUNK_SIZE;
        }
        uint16_t temp = RegisterAddress + (uint16_t)position;
        uint32_t total = 2 + data_size;
        if (total > SPI_TMP_MAX) {
            bus_give();
            return 1;
        }
        s_spi_tx[0] = (uint8_t)(SPI_WRITE_MASK(temp) >> 8);
        s_spi_tx[1] = (uint8_t)(SPI_WRITE_MASK(temp) & 0xFF);
        memcpy(s_spi_tx + 2, p_values + position, data_size);

        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = (uint32_t)(total * 8U);
        t.tx_buffer = s_spi_tx;
        esp_err_t e = spi_device_polling_transmit(dev, &t);
        if (e != ESP_OK) {
            bus_give();
            return 1;
        }
    }
    bus_give();
    return 0;
}

uint8_t VL53L8CX_io_read(void *handle, uint16_t RegisterAddress, uint8_t *p_values, uint32_t size)
{
    spi_device_handle_t dev = (spi_device_handle_t)handle;
    if (!dev || !p_values) {
        return 1;
    }

    bus_take();
    for (uint32_t position = 0; position < size; position += VL53L8CX_COMMS_CHUNK_SIZE) {
        uint32_t data_size = size - position;
        if (data_size > VL53L8CX_COMMS_CHUNK_SIZE) {
            data_size = VL53L8CX_COMMS_CHUNK_SIZE;
        }
        uint16_t temp = RegisterAddress + (uint16_t)position;
        uint32_t total = 2 + data_size;
        if (total > SPI_TMP_MAX) {
            bus_give();
            return 1;
        }
        s_spi_tx[0] = (uint8_t)(SPI_READ_MASK(temp) >> 8);
        s_spi_tx[1] = (uint8_t)(SPI_READ_MASK(temp) & 0xFF);
        memset(s_spi_tx + 2, 0, data_size);

        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = (uint32_t)(total * 8U);
        t.tx_buffer = s_spi_tx;
        t.rx_buffer = s_spi_rx;
        esp_err_t e = spi_device_polling_transmit(dev, &t);
        if (e != ESP_OK) {
            bus_give();
            return 1;
        }
        memcpy(p_values + position, s_spi_rx + 2, data_size);
    }
    bus_give();
    return 0;
}

uint8_t VL53L8CX_io_wait(void *handle, uint32_t ms)
{
    (void)handle;
    if (ms == 0) {
        return 0;
    }
    TickType_t ticks = pdMS_TO_TICKS(ms);
    if (ticks > 0) {
        vTaskDelay(ticks);
    } else {
        /* 1 ms avec CONFIG_FREERTOS_HZ > 1000 : arrondi à 0 tick → attente active courte */
        esp_rom_delay_us(ms * 1000U);
    }
    return 0;
}
