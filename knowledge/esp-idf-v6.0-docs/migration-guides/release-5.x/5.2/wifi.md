<!-- Source: _sources/migration-guides/release-5.x/5.2/wifi.rst.txt (ESP-IDF v6.0 documentation) -->

# Wi-Fi

## Wi-Fi Enterprise Security

APIs defined in <span class="title-ref">esp_wpa2.h</span> have been deprecated. Please use newer APIs from <span class="title-ref">esp_eap_client.h</span>.

## Wi-Fi Disconnect Reason Codes

For the event WIFI_EVENT_STA_DISCONNECTED, the original reason code WIFI_REASON_NO_AP_FOUND has been split as follows:

- REASON_NO_AP_FOUND(original and still used in some scenarios)
- REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD
- REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD
- REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY

For details, please refer to `esp_wifi_reason_code`.

## WiFi Multiple Antennas

WiFi multiple antennas api will be deprecated. Please use newer APIs from <span class="title-ref">esp_phy.h</span>.
