<!-- Source: _sources/api-reference/storage/sdmmc.rst.txt (ESP-IDF v6.0 documentation) -->

# SD/SDIO/MMC Driver

## Overview

The SD/SDIO/MMC driver supports SD memory, SDIO cards, and eMMC chips. This is a protocol layer driver (`sdmmc/include/sdmmc_cmd.h`) which can work together with:

### Protocol Layer vs Host Layer

The SDMMC protocol layer described in this document handles the specifics of the SD protocol, such as the card initialization flow and various data transfer command flows. The protocol layer works with the host via the `sdmmc_host_t` structure. This structure contains pointers to various functions of the host.

Host layer driver(s) implement the protocol layer driver by supporting these functions:

- Sending commands to slave devices
- Sending and receiving data
- Handling error conditions within the bus

/../\_static/diagrams/sd/sd_arch.diag

## Application Examples

SOC_SDMMC_HOST_SUPPORTED  
- `storage/sd_card/sdmmc` demonstrates how to operate an SD card formatted with the FatFS file system via the SDMMC interface.

SOC_SDMMC_HOST_SUPPORTED  
- `storage/emmc` demonstrates how to operate an eMMC chip formatted with the FatFS file system via the SDMMC interface.

SOC_GPSPI_SUPPORTED  
- `storage/sd_card/sdspi` demonstrates how to operate an SD card formatted with the FatFS file system via the SPI interface.

## Protocol Layer API

The protocol layer is given the `sdmmc_host_t` structure. This structure describes the SD/MMC host driver, lists its capabilities, and provides pointers to functions for the implementation driver. The protocol layer stores card-specific information in the `sdmmc_card_t` structure. When sending commands to the SD/MMC host driver, the protocol layer uses the `sdmmc_command_t` structure to describe the command, arguments, expected return values, and data to transfer if there is any.

### Using API with SD Memory Cards

> - To initialize the card, call `sdmmc_card_init` and pass to it the parameters `host` - the host driver information, and `card` - a pointer to the structure `sdmmc_card_t` which will be filled with information about the card when the function completes.
> - To read and write sectors of the card, use `sdmmc_read_sectors` and `sdmmc_write_sectors` respectively and pass to it the parameter `card` - a pointer to the card information structure.
> - If the card is not used anymore, call the host driver function to disable the host peripheral and free the resources allocated by the driver (`sdmmc_host_deinit` for SDMMC or `sdspi_host_deinit` for SDSPI).

not SOC_SDMMC_HOST_SUPPORTED

### eMMC Support

{IDF_TARGET_NAME} does not have an SDMMC Host controller, and can only use SPI protocol for communication with cards. However, eMMC chips cannot be used over SPI. Therefore it is not possible to use eMMC chips with {IDF_TARGET_NAME}.

SOC_SDMMC_HOST_SUPPORTED

### Using API with eMMC Chips

From the protocol layer's perspective, eMMC memory chips behave exactly like SD memory cards. Even though eMMCs are chips and do not have a card form factor, the terminology for SD cards can still be applied to eMMC due to the similarity of the protocol (<span class="title-ref">sdmmc_card_t</span>, <span class="title-ref">sdmmc_card_init</span>). Note that eMMC chips cannot be used over SPI, which makes them incompatible with the SD SPI host driver.

To initialize eMMC memory and perform read/write operations, follow the steps listed for SD cards in the previous section.

### Using API with SDIO Cards

Initialization and the probing process are the same as with SD memory cards. The only difference is in data transfer commands in SDIO mode.

During the card initialization and probing, performed with `sdmmc_card_init`, the driver only configures the following registers of the IO card:

1.  The IO portion of the card is reset by setting RES bit in the I/O Abort (0x06) register.
2.  If 4-line mode is enabled in host and slot configuration, the driver attempts to set the Bus width field in the Bus Interface Control (0x07) register. If setting the filed is successful, which means that the slave supports 4-line mode, the host is also switched to 4-line mode.
3.  If high-speed mode is enabled in the host configuration, the SHS bit is set in the High Speed (0x13) register.

In particular, the driver does not set any bits in (1) I/O Enable and Int Enable registers, (2) I/O block sizes, etc. Applications can set them by calling `sdmmc_io_write_byte`.

For card configuration and data transfer, choose the pair of functions relevant to your case from the table below.

| Action                                                                   | Read Function          | Write Function          |
|--------------------------------------------------------------------------|------------------------|-------------------------|
| Read and write a single byte using IO_RW_DIRECT (CMD52)                  | `sdmmc_io_read_byte`   | `sdmmc_io_write_byte`   |
| Read and write multiple bytes using IO_RW_EXTENDED (CMD53) in byte mode  | `sdmmc_io_read_bytes`  | `sdmmc_io_write_bytes`  |
| Read and write blocks of data using IO_RW_EXTENDED (CMD53) in block mode | `sdmmc_io_read_blocks` | `sdmmc_io_write_blocks` |

SDIO interrupts can be enabled by the application using the function `sdmmc_io_enable_int`. When using SDIO in 1-line mode, the D1 line also needs to be connected to use SDIO interrupts.

If you want the application to wait until the SDIO interrupt occurs, use `sdmmc_io_wait_int`.

SOC_SDMMC_HOST_SUPPORTED and SOC_SDIO_SLAVE_SUPPORTED

There is a component [ESSL](https://components.espressif.com/components/espressif/esp_serial_slave_link) (ESP Serial Slave Link) to use if you are communicating with an ESP32 SDIO slave. See example `peripherals/sdio/host`.

### Combo (Memory + IO) Cards

The driver does not support SD combo cards. Combo cards are treated as IO cards.

### Thread Safety

Most applications need to use the protocol layer only in one task. For this reason, the protocol layer does not implement any kind of locking on the `sdmmc_card_t` structure, or when accessing SDMMC or SD SPI host drivers. Such locking is usually implemented on a higher layer, e.g., in the filesystem driver.

## API Reference

inc/sdmmc_cmd.inc

inc/sd_protocol_types.inc

inc/sdmmc_types.inc

