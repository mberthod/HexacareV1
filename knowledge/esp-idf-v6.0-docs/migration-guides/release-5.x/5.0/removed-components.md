<!-- Source: _sources/migration-guides/release-5.x/5.0/removed-components.rst.txt (ESP-IDF v6.0 documentation) -->

# Removed or Deprecated Components

## Components Moved to ESP Component Registry

Following components are removed from ESP-IDF and moved to [ESP Component Registry](https://components.espressif.com/):

- [libsodium](https://components.espressif.com/component/espressif/libsodium)
- [cbor](https://components.espressif.com/component/espressif/cbor)
- [jsmn](https://components.espressif.com/component/espressif/jsmn)
- [esp_modem](https://components.espressif.com/component/espressif/esp_modem)
- [nghttp](https://components.espressif.com/component/espressif/nghttp)
- [mdns](https://components.espressif.com/component/espressif/mdns)
- [esp_websocket_client](https://components.espressif.com/component/espressif/esp_websocket_client)
- [asio](https://components.espressif.com/component/espressif/asio)
- [freemodbus](https://components.espressif.com/component/espressif/esp-modbus)
- [sh2lib](https://components.espressif.com/component/espressif/sh2lib)
- [expat](https://components.espressif.com/component/espressif/expat)
- [coap](https://components.espressif.com/component/espressif/coap)
- [esp-cryptoauthlib](https://components.espressif.com/component/espressif/esp-cryptoauthlib)
- [qrcode](https://components.espressif.com/component/espressif/qrcode)
- [tjpgd](https://components.espressif.com/component/espressif/esp_jpeg)
- [esp_serial_slave_link](https://components.espressif.com/components/espressif/esp_serial_slave_link)
- [tinyusb](https://components.espressif.com/components/espressif/esp_tinyusb)

> **Note**
>
> >
> Note

Please note that http parser functionality which was previously part of `nghttp` component is now part of `http_parser <http_parser>` component.

These components can be installed using `idf.py add-dependency` command.

For example, to install libsodium component with the exact version X.Y, run `idf.py add-dependency libsodium==X.Y`.

To install libsodium component with the latest version compatible to X.Y according to [semver](https://semver.org/) rules, run `idf.py add-dependency libsodium~X.Y`.

To find out which versions of each component are available, open <https://components.espressif.com>, search for the component by its name and check the versions listed on the component page.

## Deprecated Components

The following components are removed since they were deprecated in ESP-IDF v4.x:

- `tcpip_adapter`. Please use the `ESP-NETIF </api-reference/network/esp_netif>` component instead; you can follow the `tcpip-adapter`.

> **Note**
>
> >
> Note

OpenSSL-API component is no longer supported. It is not available in the ESP Component Registry, either. Please use `ESP-TLS </api-reference/protocols/esp_tls>` or `mbedtls` API directly.

> **Note**
>
> >
> Note

`esp_adc_cal` component is no longer supported. New adc calibration driver is in `esp_adc` component. Legacy adc calibration driver has been moved into `esp_adc` component. To use legacy `esp_adc_cal` driver APIs, you should add `esp_adc` component to the list of component requirements in CMakeLists.txt. Also check `Peripherals Migration Guide </migration-guides/release-5.x/5.0/peripherals>` for more details.

The targets components are no longer necessary after refactoring and have been removed:

> - `esp32`
> - `esp32s2`
> - `esp32s3`
> - `esp32c2`
> - `esp32c3`
> - `esp32h2`
