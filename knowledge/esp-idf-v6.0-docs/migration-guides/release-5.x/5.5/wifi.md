<!-- Source: _sources/migration-guides/release-5.x/5.5/wifi.rst.txt (ESP-IDF v6.0 documentation) -->

# Wi-Fi

## Breaking Changes

The parameters for the ESP-NOW sending data callback function has changed from `uint8_t *mac_addr` to `esp_now_send_info_t *tx_info`.
