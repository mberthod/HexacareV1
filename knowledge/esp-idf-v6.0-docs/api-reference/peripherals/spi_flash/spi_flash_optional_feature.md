<!-- Source: _sources/api-reference/peripherals/spi_flash/spi_flash_optional_feature.rst.txt (ESP-IDF v6.0 documentation) -->

# Optional Features for Flash

Some features are not supported on all ESP chips and flash chips. You can check the list below for more information:

- `auto-suspend-intro`
- `flash-unique-id`
- `high-performance-mode`
- `32-bit-flash-doc`
- `oct-flash-doc`

> **Note**
>
> > **Attention**
>
> ## Auto Suspend & Resume

This feature is only supported on ESP32-S3, ESP32-C2, ESP32-C3, ESP32-C6, and ESP32-H2 for now.

The support for ESP32-P4 may be added in the future.

SOC_SPI_MEM_SUPPORT_AUTO_SUSPEND

List of flash chips that support this feature:

1.  XM25QxxC series
2.  GD25QxxE series
3.  FM25Q32

> **Attention**
>
> </div>

## Flash Unique ID

This feature is supported on all Espressif chips.

Unique ID is not flash id, which means flash has 64-bit unique ID for each device. The instruction to read the unique ID (4Bh) accesses a factory-set read-only 64-bit number that is unique to each flash device. This ID number helps you to recognize each device. Not all flash vendors support this feature. If you try to read the unique ID on a chip which does not have this feature, the behavior is not determined.

List of flash chips that support this feature:

1.  ISSI
2.  GD
3.  TH
4.  FM
5.  Winbond
6.  XMC
7.  BOYA

## High Performance Mode of QSPI Flash Chips

This feature is only supported on ESP32-S3 for now.

The support for ESP32-S2, ESP32-C3, ESP32-C6, ESP32-H2, and ESP32-P4 may be added in the future.

> **Note**
>
> <!-- Only for: esp32s3 -->
High Performance mode (HPM) means that the SPI1 and flash chip works under high frequency. Usually, when the operating frequency of the flash is greater than 80 MHz, it is considered that the flash works under HPM.

As far as we acknowledged, there are more than three strategies for HPM in typical SPI flash parts. For some flash chips, HPM is controlled by dummy cycle (DC) bit in the registers, while for other chips, it can be controlled by other bits (like HPM bit) in the register, or some special command. The difference in strategies requires the driver to explicitly add support for each chip.

> **Attention**
>
> >
> Attention

It is hard to create several strategies to cover all situations, so all flash chips using HPM need to be supported explicitly. Therefore, if you try to use a flash not listed in `hpm_dc_support_list`, it might cause some error. So, when you try to use the flash chip beyond supported list, please test properly.

Moreover, when the <span class="title-ref">DC adjustment</span> strategy is adopted by the flash chip, the flash remains in a state in which DC is different from the default value after a software reset. The sub mode of HPM that adjusts the DC to run at higher frequency in the application is called <span class="title-ref">HPM-DC</span>. <span class="title-ref">HPM-DC</span> feature needs a feature <span class="title-ref">DC Aware</span> to be enabled in the bootloader. Otherwise different DC value will forbid the 2nd bootloader from being boot up after reset.

To enable High Performance mode:

1.  De-select `CONFIG_ESPTOOLPY_OCT_FLASH` and `CONFIG_ESPTOOLPY_FLASH_MODE_AUTO_DETECT`. HPM is not used for Octal flash, enabling related options may bypass HPM functions.

2.  Enable `CONFIG_SPI_FLASH_HPM_ENA` option.

3.  Switch flash frequency to HPM ones. For example, `CONFIG_ESPTOOLPY_FLASHFREQ_120M`.

4.  Make sure the config option for <span class="title-ref">HPM-DC</span> feature (under `CONFIG_SPI_FLASH_HPM_DC` choices) is selected correctly according to whether the bootloader supports <span class="title-ref">DC Aware</span>.

    > - If bootloader supports <span class="title-ref">DC Aware</span>, select `CONFIG_SPI_FLASH_HPM_DC_AUTO`. This allows the usage of flash chips that adopted <span class="title-ref">DC adjustment</span> strategy.
    > - If bootloader doesn't support <span class="title-ref">DC Aware</span>, select `CONFIG_SPI_FLASH_HPM_DC_DISABLE`. It avoids consequences caused by running <span class="title-ref">HPM-DC</span> with non-DC-aware bootloaders. But please avoid using flash chips that adopts <span class="title-ref">DC adjustment</span> strategy if `CONFIG_SPI_FLASH_HPM_DC_DISABLE` is selected. See list of flash models that adpot DC strategy below.

Check whether the bootloader supports <span class="title-ref">DC Aware</span> in the following way:

- If you are starting a new project, it's suggested to enable <span class="title-ref">DC Aware</span> by selecting `CONFIG_BOOTLOADER_FLASH_DC_AWARE` option in the bootloader menu. Please note that, you won't be able to modify this option via OTA, because the support is in the bootloader.

- If you are working on an existing project and want to update <span class="title-ref">HPM-DC</span> config option in the app via OTA, check the sdkconfig file used to build your bootloader (upgrading ESP-IDF version may make this file different from the one used by bootloader to build):

  > - For latest version (v4.4.7+, v5.0.7+, v5.1.4+, v5.2 and above), if `CONFIG_BOOTLOADER_FLASH_DC_AWARE` is selected, the bootloader supports <span class="title-ref">DC Aware</span>.
  > - For other versions (v4.4.4-v4.4.6, v5.0-v5.0.6, and v5.1-v5.1.3), if `CONFIG_ESPTOOLPY_FLASHFREQ_120M` is selected, the bootloader supports <span class="title-ref">DC Aware</span>. In this case, enable `CONFIG_BOOTLOADER_FLASH_DC_AWARE` to confirm this (though it will not affect bootloader in devices in the field).
  > - For versions below v4.4.4, the bootloader doesn't support <span class="title-ref">DC Aware</span>.

### Quad Flash HPM support list

Flash chips that don't need HPM-DC:

1.  GD25Q64C (ID: 0xC84017)
2.  GD25Q32C (ID: 0xC84016)
3.  ZB25VQ32B (ID: 0x5E4016)
4.  GD25LQ255E (ID: 0xC86019)

Following flash chips also have HPM feature, but requires the bootloader to support \`DC Aware\`:

1.  GD25Q64E (ID: 0xC84017)
2.  GD25Q128E (ID: 0xC84018)
3.  XM25QH64C (ID: 0x204017)
4.  XM25QH128C (ID: 0x204018)

## 32-bit Address Support of QSPI Flash Chips

This feature is supported on all Espressif chips (see restrictions to application below).

> **Note**
>
> Most NOR flash chips used by Espressif chips use 24-bits address, which can cover 16 MB memory. However, for larger memory (usually equal to or larger than 32 MB), flash uses a 32-bits address to address memory region higher than 16 MB. Unfortunately, 32-bits address chips have vendor-specific commands, so we need to support the chips one by one.

List of Flash chips that support this feature:

1.  W25Q256
2.  GD25Q256
3.  XM25QH256D

### Restrictions

not SOC_SPI_MEM_SUPPORT_CACHE_32BIT_ADDR_MAP

> **Important**
>
> </div>

SOC_SPI_MEM_SUPPORT_CACHE_32BIT_ADDR_MAP

For Quad flash chips, by default, the part of flash above 16 MB can be used for data storage, for example using a file system.

*Experimental Feature*: To enable full support for Quad flash addresses above 16MB (for both code execution and data access), enable this experimental feature by setting below options to `y`: - `CONFIG_IDF_EXPERIMENTAL_FEATURES` - `CONFIG_BOOTLOADER_CACHE_32BIT_ADDR_QUAD_FLASH`

Please note that, this option is experimental, which means that it can not be used on all Quad flash chips stably. For more information, please contact [Espressif's business team](https://www.espressif.com/en/contact-us/sales-questions).

For Octal flash chips, this feature is enabled by default if `CONFIG_ESPTOOLPY_OCT_FLASH` is enabled.

## OPI Flash Support

This feature is only supported on ESP32-S3 for now.

OPI flash means that the flash chip supports octal peripheral interface, which has octal I/O pins. Different octal flash has different configurations and different commands. Hence, it is necessary to carefully check the support list.

<!-- Only for: esp32s3 -->
> **Note**
>
> 