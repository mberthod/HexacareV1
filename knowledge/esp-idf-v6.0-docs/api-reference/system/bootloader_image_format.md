<!-- Source: _sources/api-reference/system/bootloader_image_format.rst.txt (ESP-IDF v6.0 documentation) -->

# Bootloader Image Format

The bootloader image consists of the same structures as the application image, see `Application Image Structures <app-image-structures>`. The only difference is in the `image-format-bootloader-description` structure.

To get information about the bootloader image, please run the following command:

``` 
esptool --chip {IDF_TARGET_PATH_NAME} image-info ./build/bootloader/bootloader.bin
```

The resultant output will resemble the following:

``` 
esptool v5.0.2
Image size: 26352 bytes

ESP32 Image Header
==================
Image version: 1
Entry point: 0x40080644
Segments: 3
Flash size: 2MB
Flash freq: 40m
Flash mode: DIO

ESP32 Extended Image Header
===========================
WP pin: 0xee (disabled)
Flash pins drive settings: clk_drv: 0x0, q_drv: 0x0, d_drv: 0x0, cs0_drv: 0x0, hd_drv: 0x0, wp_drv: 0x0
Chip ID: 0 (ESP32)
Minimal chip revision: v0.0, (legacy min_rev = 0)
Maximal chip revision: v3.99

Segments Information
====================
Segment   Length   Load addr   File offs  Memory types
-------  -------  ----------  ----------  ------------
    0  0x018e8  0x3fff0030  0x00000018  BYTE_ACCESSIBLE, DRAM, DIRAM_DRAM
    1  0x03e58  0x40078000  0x00001908  CACHE_APP
    2  0x00f5c  0x40080400  0x00005768  IRAM

ESP32 Image Footer
==================
Checksum: 0x6b (valid)
Validation hash: 09fdc81d436a927b5018e19073a787cd37ffce655f505ad92675edd784419034 (valid)

Bootloader Information
======================
Bootloader version: 1
ESP-IDF: v6.0-dev-1620-g15d7e41a848-dirt
Compile time: Aug  8 2025 16:22:1
```

## Bootloader Description

The `DRAM0` segment of the bootloader binary starts with the `esp_bootloader_desc_t` structure which carries specific fields describing the bootloader. This structure is located at a fixed offset = sizeof(`esp_image_header_t`) + sizeof(`esp_image_segment_header_t`).

> - `magic_byte`: the magic byte for the esp_bootloader_desc structure
> - `reserved`: reserved for the future IDF use
> - `secure_version`: the secure version used by the bootloader anti-rollback feature, see `CONFIG_BOOTLOADER_ANTI_ROLLBACK_ENABLE`.
> - `version`: bootloader version, see `CONFIG_BOOTLOADER_PROJECT_VER`
> - `idf_ver`: ESP-IDF version.[^1]
> - `date` and `time`: compile date and time
> - `reserved2`: reserved for the future IDF use

To get the `esp_bootloader_desc_t` structure from the running bootloader, use `esp_bootloader_get_description`.

To get the `esp_bootloader_desc_t` structure from a running application, use `esp_ota_get_bootloader_description`.

## API Reference

inc/esp_bootloader_desc.inc

[^1]: The maximum length is 32 characters, including null-termination character.
