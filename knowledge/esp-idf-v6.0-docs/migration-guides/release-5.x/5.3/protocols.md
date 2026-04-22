<!-- Source: _sources/migration-guides/release-5.x/5.3/protocols.rst.txt (ESP-IDF v6.0 documentation) -->

# Protocols

## ESP HTTPS OTA

### Breaking Changes (Summary)

- If the image length is found in the HTTP header and `esp_https_ota_config_t::bulk_flash_erase` is set to true, then instead of erasing the entire flash, the erase operation will be performed to accommodate the size of the image length.
