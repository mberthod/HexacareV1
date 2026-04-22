<!-- Source: _sources/api-reference/bluetooth/controller_vhci.rst.txt (ESP-IDF v6.0 documentation) -->

# Controller & HCI

<!-- Only for: esp32 or esp32c3 or esp32s3 -->
## Application Examples

esp32

- `bluetooth/hci/ble_adv_scan_combined` demonstrates how to use Bluetooth capabilities for advertising and scanning with a virtual Host Controller Interface (HCI). This example shows how to implement some host functionalities without a host and displays scanned advertising reports from other devices.
- `bluetooth/hci/controller_hci_uart_esp32` demonstrates how to configure the Bluetooth LE Controller's HCI to communicate over UART on {IDF_TARGET_NAME}, enabling communication with an external Bluetooth host stack.
- `bluetooth/hci/controller_vhci_ble_adv` demonstrates how to use the ESP-IDF VHCI ble_advertising app to perform advertising without a host and display received HCI events from the controller.

<!-- Only for: esp32c3 or esp32s3 -->
- `bluetooth/hci/controller_hci_uart_esp32c3_and_esp32s3` demonstrates how to configure the Bluetooth LE Controller's HCI to communicate over UART on {IDF_TARGET_NAME}, enabling communication with an external Bluetooth host stack.

## API Reference

inc/esp_bt.inc

## HCI Vendor-specific (VS) Commands

Espressif's HCI VS commands are exclusively designed for use with Espressif's Bluetooth Host stack or internal debugging purposes. Application developers **should not** initialize or invoke these VS commands in their applications. Please refer to `bt_vhci` for detailed information.
