<!-- Source: _sources/migration-guides/release-5.x/5.3/security.rst.txt (ESP-IDF v6.0 documentation) -->

# Security

SOC_FLASH_ENC_SUPPORTED

## Platform security features

When flash encryption is enabled, only the app images present in the app partition are encrypted, instead of encrypting the whole partition. This can help to optimize the encryption time required during the first boot.

This could be configured using the config `CONFIG_SECURE_FLASH_ENCRYPT_ONLY_IMAGE_LEN_IN_APP_PART`, which is enabled by default from ESP-IDF v5.3, and is disabled for all earlier releases to avoid any breaking behaviour.

