<!-- Source: _sources/api-reference/peripherals/lcd/index.rst.txt (ESP-IDF v6.0 documentation) -->

# LCD

## Introduction

ESP chips can generate various kinds of timings needed by common LCDs on the market, like SPI LCD, I2C LCD, Parallel LCD (Intel 8080), RGB/SRGB LCD, MIPI DSI LCD, etc. The `esp_lcd` component offers an abstracted driver framework to support them in an unified way.

An LCD typically consists of two main planes:

- **Control Plane**: This plane allows us to read and write to the internal registers of the LCD device controller. Host typically uses this plane for tasks such as initializing the LCD power supply and performing gamma calibration.
- **Data Plane**: The data plane is responsible for transmitting pixel data to the LCD device.

## Functional Overview

In the context of `esp_lcd`, both the data plane and the control plane are represented by the `esp_lcd_panel_handle_t` type.

On some LCDs, these two planes may be combined into a single plane. In this configuration, pixel data is transmitted through the control plane, achieving functionality similar to that of the data plane. This merging is common in SPI LCDs and I2C LCDs.

Additionally, there are LCDs that do not require a separate control plane. For instance, certain RGB LCDs automatically execute necessary initialization procedures after power-up. Host devices only need to continuously refresh pixel data through the data plane. However, it's essential to note that not all RGB LCDs eliminate the control plane entirely. Some LCD devices can simultaneously support multiple interfaces, requiring the Host to send specific commands via the control plane (such as those based on the SPI interface) to enable the RGB mode.

This document will discuss how to create the control plane and data plane, as mentioned earlier, based on different types of LCDs.

SOC_GPSPI_SUPPORTED  
spi_lcd

SOC_I2C_SUPPORTED  
i2c_lcd

SOC_LCD_I80_SUPPORTED  
i80_lcd

SOC_LCD_RGB_SUPPORTED  
rgb_lcd

SOC_MIPI_DSI_SUPPORTED  
dsi_lcd

SOC_PARLIO_LCD_SUPPORTED  
parl_lcd

> **Note**
>
> ## LCD Control Panel Operations

- `esp_lcd_panel_reset` can reset the LCD control panel.
- `esp_lcd_panel_init` performs a basic initialization of the control panel. To perform more manufacturer specific initialization, please refer to `steps_add_manufacture_init`.
- By combining using `esp_lcd_panel_swap_xy` and `esp_lcd_panel_mirror`, you can achieve the functionality of rotating or mirroring the LCD screen.
- `esp_lcd_panel_disp_on_off` can turn on or off the LCD screen by cutting down the output path from the frame buffer to the LCD screen. Please note, this is not controlling the LCD backlight. Backlight control is not covered by the `esp_lcd` driver.
- `esp_lcd_panel_disp_sleep` can reduce the power consumption of the LCD screen by entering the sleep mode. The internal frame buffer is still retained.

## LCD Data Panel Operations

- `esp_lcd_panel_reset` can reset the LCD data panel.
- `esp_lcd_panel_init` performs a basic initialization of the data panel.
- `esp_lcd_panel_draw_bitmap` is the function which does the magic to flush the user draw buffer to the LCD screen, where the target draw window is configurable. Please note, this function expects that the draw buffer is a 1-D array and there's no stride in between each lines.
- `esp_lcd_panel_draw_bitmap_2d` is the function which does the magic to flush the user draw buffer to the LCD screen, where the source and target draw windows are configurable. Please note, the draw buffer can be a 2-D array or a 1-D array with no stride in between each lines.

## Steps to Add Manufacturer Specific Initialization

The LCD controller drivers (e.g., st7789) in ESP-IDF only provide basic initialization in the `esp_lcd_panel_init`, leaving the vast majority of settings to the default values. Some LCD modules need to set a bunch of manufacturer specific configurations before it can display normally. These configurations usually include gamma, power voltage and so on. If you want to add manufacturer specific initialization, please follow the steps below:

``` c
esp_lcd_panel_reset(panel_handle);
esp_lcd_panel_init(panel_handle);
// set extra configurations e.g., gamma control
// with the underlying IO handle
// please consult your manufacturer for special commands and corresponding values
esp_lcd_panel_io_tx_param(io_handle, GAMMA_CMD, (uint8_t[]) {
       GAMMA_ARRAY
    }, N);
// turn on the display
esp_lcd_panel_disp_on_off(panel_handle, true);
```

## Application Example

\* `peripherals/lcd/tjpgd` shows how to decode a JPEG image and display it on an SPI-interfaced LCD, and rotate the image periodically. :SOC_GPSPI_SUPPORTED: \* `peripherals/lcd/spi_lcd_touch` demonstrates how to drive the LCD and touch panel on the same SPI bus, and display a simple GUI using the LVGL library. :SOC_LCD_I80_SUPPORTED: \* `peripherals/lcd/i80_controller` demonstrates how to port the LVGL library onto the <span class="title-ref">esp_lcd</span> driver layer to create GUIs. :SOC_LCD_RGB_SUPPORTED: \* `peripherals/lcd/rgb_panel` demonstrates how to install an RGB panel driver, display a scatter chart on the screen based on the LVGL library. :SOC_I2C_SUPPORTED: \* `peripherals/lcd/i2c_oled` demonstrates how to use the SSD1306 panel driver from the <span class="title-ref">esp_lcd</span> component to facilitate the porting of LVGL library and display a scrolling text on the OLED screen. :SOC_MIPI_DSI_SUPPORTED: \* `peripherals/lcd/mipi_dsi` demonstrates the general process of installing a MIPI DSI LCD driver, and displays a LVGL widget on the screen. :SOC_PARLIO_LCD_SUPPORTED: \* `peripherals/lcd/parlio_simulate` demonstrates how to use Parallel IO peripheral to drive an SPI or I80 Interfaced LCD.

## API Reference

inc/lcd_types.inc

inc/esp_lcd_types.inc

inc/esp_lcd_panel_io.inc

inc/esp_lcd_panel_ops.inc

inc/esp_lcd_panel_vendor.inc

