<!-- Source: _sources/migration-guides/release-5.x/5.1/networking.rst.txt (ESP-IDF v6.0 documentation) -->

# Networking

## SNTP

SNTP module now provides thread safe APIs to access lwIP functionality. It is recommended to use `ESP_NETIF </api-reference/network/esp_netif>` API. Please refer to the chapter `esp_netif-sntp-api` for more details.
