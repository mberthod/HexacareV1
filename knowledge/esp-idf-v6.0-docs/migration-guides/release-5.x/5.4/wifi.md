<!-- Source: _sources/migration-guides/release-5.x/5.4/wifi.rst.txt (ESP-IDF v6.0 documentation) -->

# Wi-Fi

## Wi-Fi Scan and Connect

> The following types have been modified:
>
> - `esp_wifi/include/esp_wifi_he_types.h`
>
>   > - `esp_wifi_htc_omc_t`:
>   >
>   >   > - `uph_id`, `ul_pw_headroom`, `min_tx_pw_flag` are deprecated.
>
> - `esp_wifi/include/esp_wifi_types_generic.h`
>
>   > - `wifi_ap_record_t`:
>   >
>   >   > - The type of `bandwidth` has been changed from `uint8_t` to `wifi_bandwidth_t`

### Breaking Changes

#### Wi-Fi Authentication Mode Changes for WPA3-Enterprise

In ESP-IDF versions prior to v5.4, the authentication mode (`wifi_auth_mode_t`) for Access Points (APs) supporting WPA3-Enterprise-Only and WPA3-Enterprise-Transition was identified as `WIFI_AUTH_WPA2_ENTERPRISE`.

Starting from v5.4, this behavior has been updated:

- APs supporting **WPA3-Enterprise-Only** mode are now detected with `wifi_auth_mode_t` set to `WIFI_AUTH_WPA3_ENTERPRISE`.
- APs supporting **WPA3-Enterprise-Transition** mode are detected with `wifi_auth_mode_t` set to `WIFI_AUTH_WPA2_WPA3_ENTERPRISE`.

> **Note**
>
> 