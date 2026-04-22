<!-- Source: _sources/migration-guides/release-5.x/5.3/peripherals.rst.txt (ESP-IDF v6.0 documentation) -->

# Peripherals

## Drivers

In order to control the dependence of other components on drivers at a smaller granularity, the original peripheral drivers under the `driver` component were split into separate components:

- <span class="title-ref">esp_driver_gptimer</span> - Driver for general purpose timers
- <span class="title-ref">esp_driver_pcnt</span> - Driver for pulse counter
- <span class="title-ref">esp_driver_gpio</span> - Driver for GPIO
- <span class="title-ref">esp_driver_spi</span> - Driver for GPSPI
- <span class="title-ref">esp_driver_mcpwm</span> - Driver for Motor Control PWM
- <span class="title-ref">esp_driver_sdmmc</span> - Driver for SDMMC
- <span class="title-ref">esp_driver_sdspi</span> - Driver for SDSPI
- <span class="title-ref">esp_driver_sdio</span> - Driver for SDIO
- <span class="title-ref">esp_driver_ana_cmpr</span> - Driver for Analog Comparator
- <span class="title-ref">esp_driver_i2s</span> - Driver for I2S
- <span class="title-ref">esp_driver_dac</span> - Driver for DAC
- <span class="title-ref">esp_driver_rmt</span> - Driver for RMT
- <span class="title-ref">esp_driver_tsens</span> - Driver for Temperature Sensor
- <span class="title-ref">esp_driver_sdm</span> - Driver for Sigma-Delta Modulator
- <span class="title-ref">esp_driver_i2c</span> - Driver for I2C
- <span class="title-ref">esp_driver_uart</span> - Driver for UART
- <span class="title-ref">esp_driver_ledc</span> - Driver for LEDC
- <span class="title-ref">esp_driver_parlio</span> - Driver for Parallel IO
- <span class="title-ref">esp_driver_usb_serial_jtag</span> - Driver for USB_SERIAL_JTAG

For compatibility, the original `driver` component is still treated as an all-in-one component by registering these <span class="title-ref">esp_driver_xyz</span> components as its public dependencies. In other words, you do not need to modify the CMake file of an existing project, but you now have a way to specify the specific peripheral driver that your project depends on.

Originally, you may have used **linker.lf** to specify the link location of some driver functions in memory space, but now, because the location of the driver files have been moved, you need to make changes your **linker.lf** file accordingly. For example, a linker.lf file with the following entries:

``` none
[mapping:my_mapping_scheme]
archive: libdriver.a
entries:
    gpio (noflash)
```

Should be changed to:

``` none
[mapping:my_mapping_scheme]
archive: libesp_driver_gpio.a
entries:
    gpio (noflash)
```

## Secure Element

The ATECC608A secure element interfacing example has been moved to [ESP Cryptoauthlib Repository](https://github.com/espressif/esp-cryptoauthlib/tree/master/examples/atecc608_ecdsa) on GitHub.

This example is also part of the [esp-cryptoauthlib](https://components.espressif.com/component/espressif/esp-cryptoauthlib) in the ESP Component Registry.

## I2S

Due to the cumbersome usage of the secondary pointer of DMA buffer, the `data` field in the callback event `i2s_event_data_t` is deprecated, please use the newly added first-level pointer `dma_buf` instead.
