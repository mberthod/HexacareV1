#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pca9555_io_init(int sda_gpio, int scl_gpio, uint8_t i2c_addr);
esp_err_t pca9555_set_output_mode(uint8_t pin_mask);
esp_err_t pca9555_write_output(uint8_t values);
esp_err_t pca9555_write_output_ports(uint8_t port0, uint8_t port1);
esp_err_t pca9555_read_input(uint8_t *out);
esp_err_t pca9555_get_output_shadow(uint8_t *port0, uint8_t *port1);

/** Mutex récursif partagé bus I2C_NUM_0 (PCA9555 + capteurs board). */
void lexa_i2c0_bus_lock(void);
void lexa_i2c0_bus_unlock(void);

#ifdef __cplusplus
}
#endif
