<!-- Source: _sources/api-reference/network/esp_wifi.rst.txt (ESP-IDF v6.0 documentation) -->

# Wi-Fi

## Introduction

The Wi-Fi libraries provide support for configuring and monitoring the {IDF_TARGET_NAME} Wi-Fi networking functionality. This includes configuration for:

- Station mode (aka STA mode or Wi-Fi client mode). {IDF_TARGET_NAME} connects to an access point.
- AP mode (aka Soft-AP mode or Access Point mode). Stations connect to the {IDF_TARGET_NAME}.
- Station/AP-coexistence mode ({IDF_TARGET_NAME} is concurrently an access point and a station connected to another access point).
- Various security modes for the above (WPA, WPA2, WPA3, etc.)
- Scanning for access points (active & passive scanning).
- Promiscuous mode for monitoring of IEEE802.11 Wi-Fi packets.

## Application Examples

Several application examples demonstrating the functionality of Wi-Fi library are provided in `wifi` directory of ESP-IDF repository. Please check the `README <wifi/README.md>` for more details.

## API Reference

inc/esp_wifi.inc

inc/esp_wifi_types.inc

inc/esp_wifi_types_generic.inc

inc/esp_eap_client.inc

inc/esp_wps.inc

inc/esp_rrm.inc

inc/esp_wnm.inc

inc/esp_mbo.inc

