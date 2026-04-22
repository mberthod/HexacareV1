#pragma once

/** À appeler une fois après spi_bus_initialize : mutex bus SPI partagé par les 4 VL53L8CX. */
void vl53l8cx_platform_esp_bus_lock_init(void);
