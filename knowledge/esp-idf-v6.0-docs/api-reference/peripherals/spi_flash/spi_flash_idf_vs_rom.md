<!-- Source: _sources/api-reference/peripherals/spi_flash/spi_flash_idf_vs_rom.rst.txt (ESP-IDF v6.0 documentation) -->

# SPI Flash API ESP-IDF Version vs Chip-ROM Version

There is a set of SPI flash drivers in Chip-ROM which you can use by enabling `CONFIG_SPI_FLASH_ROM_IMPL`. Most of the ESP-IDF SPI flash driver code are in internal RAM, therefore enabling this option frees some internal RAM usage. Note that if you enable this option, this means some SPI flash driver features and bugfixes that are done in ESP-IDF might not be included in the Chip-ROM version.

## Feature Supported by ESP-IDF but Not in Chip-ROM

- Octal flash chip support. See `oct-flash-doc` for details.
- 32-bit-address support on flash chips. Note that this feature is an optional feature, please do read `32-bit-flash-doc` for details.
- TH flash chip support.
- Kconfig option `CONFIG_SPI_FLASH_CHECK_ERASE_TIMEOUT_DISABLED`.
- `CONFIG_SPI_FLASH_VERIFY_WRITE`, enabling this option helps you detect bad writing.
- `CONFIG_SPI_FLASH_LOG_FAILED_WRITE`, enabling this option prints the bad writing.
- `CONFIG_SPI_FLASH_WARN_SETTING_ZERO_TO_ONE`, enabling this option checks if you are writing zero to one.
- `CONFIG_SPI_FLASH_DANGEROUS_WRITE`, enabling this option checks for flash programming to certain protected regions like bootloader, partition table or application itself.
- `CONFIG_SPI_FLASH_ENABLE_COUNTERS`, enabling this option to collect performance data for ESP-IDF SPI flash driver APIs.

\- `CONFIG_SPI_FLASH_AUTO_SUSPEND`, enabling this option to automatically suspend or resume a long flash operation when short flash operation happens. Note that this feature is an optional feature, please do read `auto-suspend-intro` for more limitations. :ESP_ROM_HAS_SPI_FLASH_MMAP and SOC_SPIRAM_XIP_SUPPORTED and not esp32s3: - `CONFIG_SPIRAM_XIP_FROM_PSRAM`, enabling this option allows you to use external PSRAM as instruction cache and read-only data cache. Some functions in the ROM don't support this usage, and an ESP-IDF version of these functions is provided. :esp32s3: - `CONFIG_SPIRAM_FETCH_INSTRUCTIONS` and `CONFIG_SPIRAM_RODATA`, enabling these options allows you to use external PSRAM as instruction cache and read-only data cache. Some functions in the ROM don't support this usage, and an ESP-IDF version of these functions is provided.

## Bugfixes Introduced in ESP-IDF but Not in Chip-ROM

\- Detected flash physical size correctly, for larger than 256 MBit flash chips. (Commit ID: b4964279d44f73cce7cfd5cf684567fbdfd6fd9e) :esp32c3: - Improved SPI1 CS setup timing, otherwise issue may happen on ZB32Q128. (Commit ID: 08f1bbe0c75382f1702e40c941e93314285105d4) :esp32s3: - Fixed issue that 4-line flash encryption can not work normally when 8-line PSRAM enabled. (Commit ID: 683d92bc884e0f2a7eebea40a551cf05f0c28256) :esp32s2: - Fixed issue that only 4 MB virtual address ranges can be mapped to read-only data on flash. :esp32s3: - Fixed issue that only 128 KB virtual address ranges can be mapped to instructions on flash. :esp32s3: - Fixed issue that only 16 MB virtual address ranges can be mapped to read-only data on flash. :esp32c3: - Fixed issue that only 128 KB virtual address ranges can be mapped to instructions on flash. :esp32c2: - Fixed issue that only at most 128 KB virtual address ranges can be mapped to instructions on flash. - Fixed issue that address range may escape from checking for erasing and writing function when their sum overflows 32-bit boundary.

