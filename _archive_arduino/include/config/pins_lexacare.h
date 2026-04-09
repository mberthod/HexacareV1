#ifndef PINS_LEXACARE_H
#define PINS_LEXACARE_H

#include <Arduino.h>

// ============== I2C (DS3231, TMP117, EEPROM) ==============
#define PIN_I2C_SDA     8   
#define PIN_I2C_SCL     9   

// ============== Lidars 4x VL53L8CX (Bus SPI) ==============
#define PIN_SPI_MISO    13  
#define PIN_SPI_MOSI    11  
#define PIN_SPI_MCLK    12  
#define PIN_LIDAR_SYNC  14
#define PIN_LIDAR_INT   10

#define PIN_LIDAR_LPN_1 1  
#define PIN_LIDAR_LPN_2 2
#define PIN_LIDAR_LPN_3 3
#define PIN_LIDAR_LPN_4 4

#define PIN_SPI_NCS1    5
#define PIN_SPI_NCS2    6
#define PIN_SPI_NCS3    7
#define PIN_SPI_NCS4    15

// ============== PIN POWER ==============
#define PIN_POWER_MLX   45
#define PIN_POWER_RADAR 46
#define PIN_POWER_MIC   47
#define PIN_POWER_FAN   48
#define PIN_VL53L0X_XSHUT 21

// ============== Radar HLK-LD6002 (UART) ==============
#define PIN_RADAR_RX    17  // Déplacé (était 44)
#define PIN_RADAR_TX    18  // Déplacé (était 43)
#define PIN_RADAR_GPIO20 0  // Déplacé (était 35)
#define PIN_RADAR_GPIO7  43 // Déplacé (était 39)

// ============== ADC (Surveillance Rails) ==============
#define PIN_ADC_VIN     1
#define PIN_ADC_VBATT   2
#define PIN_ADC_1V8     3
#define PIN_ADC_3V3     4

// ============== Micros I2S (ICS-43434) ==============
#define PIN_MIC_WS      19  // Déplacé (était 40)
#define PIN_MIC_SD      20  // Déplacé (était 41)
#define PIN_MIC_SCK     0   // Déplacé (était 38)

// ============== LED RGB (Neopixel) ==============
#define PIN_RGB_LED     48  // DevKitC-1 : LED intégrée sur GPIO 48 (sinon 38 selon carte)

// ============== Log dual (USB + UART0) ==============
#define PIN_LOG_UART_RX 44  // UART0 RX (second lien log, avec USB)
#define PIN_LOG_UART_TX 43  // UART0 TX

#endif