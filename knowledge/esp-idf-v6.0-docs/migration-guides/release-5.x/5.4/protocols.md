<!-- Source: _sources/migration-guides/release-5.x/5.4/protocols.rst.txt (ESP-IDF v6.0 documentation) -->

# Protocols

## HTTPS Server

### Certificate Selection Hook

In order to enable the Certificate Selection hook feature in ESP HTTPS Server, now you need to enable `CONFIG_ESP_HTTPS_SERVER_CERT_SELECT_HOOK` instead of `CONFIG_ESP_TLS_SERVER_CERT_SELECT_HOOK`.

The new `CONFIG_ESP_HTTPS_SERVER_CERT_SELECT_HOOK` option automatically selects `CONFIG_ESP_TLS_SERVER_CERT_SELECT_HOOK`.
