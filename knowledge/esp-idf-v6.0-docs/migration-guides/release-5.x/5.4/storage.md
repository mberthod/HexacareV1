<!-- Source: _sources/migration-guides/release-5.x/5.4/storage.rst.txt (ESP-IDF v6.0 documentation) -->

# Storage

## SPI Flash Driver

XMC-C series flash suspend support has been removed. According to feedback from the flash manufacturer, in some situations the XMC-C flash would require a 1ms interval between resume and next command. This is too long for a software request. Based on the above reason, in order to use suspend safely, we decide to remove flash suspend support from XMC-C series. But you can still force enable it via <span class="title-ref">CONFIG_SPI_FLASH_FORCE_ENABLE_XMC_C_SUSPEND</span>. If you have any questions, please contact espressif business support.
