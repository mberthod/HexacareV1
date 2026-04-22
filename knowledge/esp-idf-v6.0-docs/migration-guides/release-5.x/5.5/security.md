<!-- Source: _sources/migration-guides/release-5.x/5.5/security.rst.txt (ESP-IDF v6.0 documentation) -->

# Security

SOC_SHA_SUPPORTED

## Mbed TLS

Starting from **ESP-IDF v5.5**, there is a change in how the SHA sub-function APIs, `esp_sha_block` and `esp_sha_dma`, are used.

Previously, these APIs used to set the SHA mode internally, however, in the updated version, you must explicitly set the SHA mode before invoking them.

For instance, if you intend to use the **SHA-256** algorithm, you must first call `esp_sha_set_mode` with the argument `SHA2_256`:

``` c
esp_sha_set_mode(SHA2_256);
```

