<!-- Source: _sources/migration-guides/release-5.x/5.0/protocols.rst.txt (ESP-IDF v6.0 documentation) -->

# Protocols

## Mbed TLS

For ESP-IDF v5.0, [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) has been updated from v2.x to v3.1.0.

For more details about Mbed TLS's migration from version 2.x to version 3.0 or greater, please refer to the [official guide](https://github.com/espressif/mbedtls/blob/9bb5effc3298265f829878825d9bd38478e67514/docs/3.0-migration-guide.md).

### Breaking Changes (Summary)

#### Most Structure Fields Are Now Private

- Direct access to fields of structures (`struct` types) declared in public headers is no longer supported.
- Appropriate accessor functions (getter/setter) must be used for the same. A temporary workaround would be to use `MBEDTLS_PRIVATE` macro (**not recommended**).
- For more details, refer to the [official guide](https://github.com/espressif/mbedtls/blob/9bb5effc3298265f829878825d9bd38478e67514/docs/3.0-migration-guide.md#most-structure-fields-are-now-private).

#### SSL

- Removed support for TLS 1.0, 1.1, and DTLS 1.0
- Removed support for SSL 3.0

#### Deprecated Functions Were Removed from Cryptography Modules

- The functions `mbedtls_*_ret()` (related to MD, SHA, RIPEMD, RNG, HMAC modules) was renamed to replace the corresponding functions without `_ret` appended and updated return value.
- For more details, refer to the [official guide](https://github.com/espressif/mbedtls/blob/9bb5effc3298265f829878825d9bd38478e67514/docs/3.0-migration-guide.md#deprecated-functions-were-removed-from-hashing-modules).

#### Deprecated Config Options

Following are some of the important config options deprecated by this update. The configs related to and/or dependent on these have also been deprecated.

- `MBEDTLS_SSL_PROTO_SSL3` : Support for SSL 3.0
- `MBEDTLS_SSL_PROTO_TLS1` : Support for TLS 1.0
- `MBEDTLS_SSL_PROTO_TLS1_1`: Support for TLS 1.1
- `MBEDTLS_SSL_PROTO_DTLS` : Support for DTLS 1.1 (Only DTLS 1.2 is supported now)
- `MBEDTLS_DES_C` : Support for 3DES ciphersuites
- `MBEDTLS_RC4_MODE` : Support for RC4-based ciphersuites

> **Note**
>
> ## Miscellaneous

### Disabled Diffie-Hellman Key Exchange Modes

The Diffie-Hellman Key Exchange modes have now been disabled by default due to security risks (see warning text [here](https://github.com/espressif/mbedtls/blob/9bb5effc3298265f829878825d9bd38478e67514/include/mbedtls/dhm.h#L20)). Related configs are given below:

- `MBEDTLS_DHM_C` : Support for the Diffie-Hellman-Merkle module
- `MBEDTLS_KEY_EXCHANGE_DHE_PSK` : Support for Diffie-Hellman PSK (pre-shared-key) TLS authentication modes
- `MBEDTLS_KEY_EXCHANGE_DHE_RSA` : Support for cipher suites with the prefix `TLS-DHE-RSA-WITH-`

> **Note**
>
> ### Remove `certs` Module from X509 Library

- The `mbedtls/certs.h` header is no longer available in mbedtls 3.1. Most applications can safely remove it from the list of includes.

### Breaking Change for `esp_crt_bundle_set` API

- The `esp_crt_bundle_set()` API now requires one additional argument named `bundle_size`. The return type of the API has also been changed to `esp_err_t` from `void`.

### Breaking Change for `esp_ds_rsa_sign` API

- The `esp_ds_rsa_sign()` API now requires one less argument. The argument `mode` is no longer required.

## HTTPS Server

### Breaking Changes (Summary)

Names of variables holding different certs in `httpd_ssl_config_t` structure have been updated.

\* `httpd_ssl_config::servercert` variable inherits role of `cacert_pem` variable. \* `httpd_ssl_config::servercert_len` variable inherits role of `cacert_len` variable \* `httpd_ssl_config::cacert_pem` variable inherits role of `client_verify_cert_pem` variable \* `httpd_ssl_config::cacert_len` variable inherits role of `client_verify_cert_len` variable

The return type of the `httpd_ssl_stop` API has been changed to `esp_err_t` from `void`.

## ESP HTTPS OTA

### Breaking Changes (Summary)

- The function `esp_https_ota` now requires pointer to `esp_https_ota_config_t` as argument instead of pointer to `esp_http_client_config_t`.

## ESP-TLS

### Breaking Changes (Summary)

#### `esp_tls_t` Structure Is Now Private

The `esp_tls_t` has now been made completely private. You cannot access its internal structures directly. Any necessary data that needs to be obtained from the ESP-TLS handle can be done through respective getter/setter functions. If there is a requirement of a specific getter/setter function, please raise an [issue](https://github.com/espressif/esp-idf/issues) on ESP-IDF.

The list of newly added getter/setter function is as as follows:

- `esp_tls_get_ssl_context` - Obtain the ssl context of the underlying ssl stack from the ESP-TLS handle.

#### Function Deprecations And Recommended Alternatives

Following table summarizes the deprecated functions removed and their alternatives to be used from ESP-IDF v5.0 onwards.

| Deprecated Function     | Alternative             |
|-------------------------|-------------------------|
| `esp_tls_conn_new()`    | `esp_tls_conn_new_sync` |
| `esp_tls_conn_delete()` | `esp_tls_conn_destroy`  |

- The function `esp_tls_conn_http_new` has now been termed as deprecated. Please use the alternative function `esp_tls_conn_http_new_sync` (or its asynchronous `esp_tls_conn_http_new_async`). Note that the alternatives need an additional parameter `esp_tls_t`, which has to be initialized using the `esp_tls_init` function.

## HTTP Server

### Breaking Changes (Summary)

- `http_server.h` header is no longer available in `esp_http_server`. Please use `esp_http_server.h` instead.

## ESP HTTP Client

### Breaking Changes (Summary)

- The functions `esp_http_client_read` and `esp_http_client_fetch_headers` now return an additional return value `-ESP_ERR_HTTP_EAGAIN` for timeout errors - call timed-out before any data was ready.

## TCP Transport

### Breaking Changes (Summary)

- The function `esp_transport_read` now returns `0` for a connection timeout and `< 0` for other errors. Please refer `esp_tcp_transport_err_t` for all possible return values.

## MQTT Client

### Breaking Changes (Summary)

- `esp_mqtt_client_config_t` have all fields grouped in sub structs.

Most common configurations are listed below:

- Broker address now is set in `esp_mqtt_client_config_t::broker::address::uri`
- Security related to broker verification in `esp_mqtt_client_config_t::broker::verification`
- Client username is set in `esp_mqtt_client_config_t::credentials::username`
- `esp_mqtt_client_config_t` no longer supports the `user_context` field. Please use `esp_mqtt_client_register_event` instead for registering an event handler; the last argument `event_handler_arg` can be used to pass user context to the handler.

## ESP-Modbus

### Breaking Changes (Summary)

The ESP-IDF component `freemodbus` has been removed from ESP-IDF and is supported as a separate component. Additional information for the `ESP-Modbus` component can be found in the separate repository:

- [ESP-Modbus component on GitHub](https://www.github.com/espressif/esp-modbus)

The `main` component folder of the new application shall include the component manager manifest file `idf_component.yml` as in the example below:

``` text
dependencies:
  espressif/esp-modbus:
    version: "^1.0"
```

The `esp-modbus` component can be found in [ESP Component Registry](https://components.espressif.com/component/espressif/esp-modbus). Refer to [component manager documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/tools/idf-component-manager.html) for more information on how to set up the component manager.

For applications targeting v4.x releases of ESP-IDF that need to use new `esp-modbus` component, adding the component manager manifest file `idf_component.yml` will be sufficient to pull in the new component. However, users should also exclude the legacy `freemodbus` component from the build. This can be achieved using the statement below in the project's `CMakeLists.txt`:

``` cmake
set(EXCLUDE_COMPONENTS freemodbus)
```
