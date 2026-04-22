<!-- Source: _sources/migration-guides/release-5.x/5.2/bluetooth-classic.rst.txt (ESP-IDF v6.0 documentation) -->

# Bluetooth Classic

## Bluedroid

- The following Bluedroid APIs have been changed:

  > - `/bt/host/bluedroid/api/include/api/esp_bt_main.h`
  >
  >   > - The function `esp_bluedroid_init` has been deprecated and replaced by `esp_bluedroid_init_with_cfg`.
  >   > - A new configuration structure `esp_bluedroid_config_t` has been introduced as a parameter for `esp_bluedroid_init_with_cfg`. The field `ssp_en` in this structure determines the pairing mode.

- The configuration `CONFIG_BT_SSP_ENABLED` has been removed, as pairing mode can now be configured at runtime.
