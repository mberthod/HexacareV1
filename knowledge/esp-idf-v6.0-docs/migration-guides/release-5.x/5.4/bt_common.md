<!-- Source: _sources/migration-guides/release-5.x/5.4/bt_common.rst.txt (ESP-IDF v6.0 documentation) -->

# Bluetooth Common

The following Bluetooth Common header declarations have been moved:

<!-- Only for: esp32 -->
- `/bt/include/esp32/include/esp_bt.h`

  > - Move the declarations of `esp_wifi_bt_power_domain_on` and `esp_wifi_bt_power_domain_off` from `esp_bt.h` to `esp_phy_init.h`, since they belong to component `esp_phy` and are not expected to be used by customer.

<!-- Only for: esp32c3 or esp32s3 -->
- `/bt/include/esp32c3/include/esp_bt.h`

  > - Move the declarations of `esp_wifi_bt_power_domain_on` and `esp_wifi_bt_power_domain_off` from `esp_bt.h` to `esp_phy_init.h`, since they belong to component `esp_phy` and are not expected to be used by customer.

