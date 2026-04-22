<!-- Source: _sources/api-reference/protocols/esp_tls.rst.txt (ESP-IDF v6.0 documentation) -->

# ESP-TLS

## Overview

The ESP-TLS component provides a simplified API interface for accessing the commonly used TLS functions. It supports common scenarios like CA certification validation, SNI, ALPN negotiation, and non-blocking connection among others. All the configurations can be specified in the `esp_tls_cfg_t` data structure. Once done, TLS communication can be conducted using the following APIs:

> - `esp_tls_init`: for initializing the TLS connection handle.
> - `esp_tls_conn_new_sync`: for opening a new blocking TLS connection.
> - `esp_tls_conn_new_async`: for opening a new non-blocking TLS connection.
> - `esp_tls_conn_read`: for reading from the connection.
> - `esp_tls_conn_write`: for writing into the connection.
> - `esp_tls_conn_destroy`: for freeing up the connection.

Any application layer protocol like HTTP1, HTTP2, etc can be executed on top of this layer.

## Application Example

Simple HTTPS example that uses ESP-TLS to establish a secure socket connection: `protocols/https_request`.

## Tree Structure for ESP-TLS Component

``` none
├── esp_tls.c
├── esp_tls.h
├── esp_tls_mbedtls.c
├── esp_tls_custom_stack.c
└── private_include
    ├── esp_tls_mbedtls.h
    └── esp_tls_custom_stack.h
```

The ESP-TLS component has a file `esp-tls/esp_tls.h` which contains the public API headers for the component. Internally, the ESP-TLS component operates using MbedTLS as the default SSL/TLS library, or a custom TLS stack registered via the `esp_tls_register_stack` API. APIs specific to MbedTLS are present in `esp-tls/private_include/esp_tls_mbedtls.h` and APIs for custom stack registration are present in `esp-tls/esp_tls_custom_stack.h`.

## TLS Server Verification

ESP-TLS provides multiple options for TLS server verification on the client side. The ESP-TLS client can verify the server by validating the peer's server certificate or with the help of pre-shared keys. The user should select only one of the following options in the `esp_tls_cfg_t` structure for TLS server verification. If no option is selected, the client will return a fatal error by default during the TLS connection setup.

> - **cacert_buf** and **cacert_bytes**: The CA certificate can be provided in a buffer to the `esp_tls_cfg_t` structure. The ESP-TLS uses the CA certificate present in the buffer to verify the server. The following variables in the `esp_tls_cfg_t` structure must be set.
>
>   > - `cacert_buf` - pointer to the buffer which contains the CA certification.
>   > - `cacert_bytes` - the size of the CA certificate in bytes.
>
> - **use_global_ca_store**: The `global_ca_store` can be initialized and set at once. Then it can be used to verify the server for all the ESP-TLS connections which have set `use_global_ca_store = true` in their respective `esp_tls_cfg_t` structure. See the API Reference section below for information regarding different APIs used for initializing and setting up the `global_ca_store`.
>
> - **crt_bundle_attach**: The ESP x509 Certificate Bundle API provides an easy way to include a bundle of custom x509 root certificates for TLS server verification. More details can be found at `ESP x509 Certificate Bundle </api-reference/protocols/esp_crt_bundle>`.
>
> - **psk_hint_key**: To use pre-shared keys for server verification, `CONFIG_ESP_TLS_PSK_VERIFICATION` should be enabled in the ESP-TLS menuconfig. Then the pointer to the PSK hint and key should be provided to the `esp_tls_cfg_t` structure. The ESP-TLS will use the PSK for server verification only when no other option regarding server verification is selected.
>
> - **skip server verification**: This is an insecure option provided in the ESP-TLS for testing purposes. The option can be set by enabling `CONFIG_ESP_TLS_INSECURE` and `CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY` in the ESP-TLS menuconfig. When this option is enabled the ESP-TLS will skip server verification by default when no other options for server verification are selected in the `esp_tls_cfg_t` structure.
>
>   > **Warning**
>
> >
> >   
> >
> >   If this option is enabled, there is a risk of establishing a TLS connection with a server that has a fake identity, unless the server certificate is provided through the API or other mechanisms like `ca_store`.
> >
> >   </div>
>
> ## SNI (Server Name Indication)
>
> SNI is an extension to the TLS protocol that allows the client to specify the hostname it is connecting to during the TLS handshake. This is required when connecting to servers that host multiple domains on the same IP address.
>
> **How to ensure SNI works properly:**
>
> - SNI is enabled by default in ESP-TLS when using HTTPS connections.
> - To explicitly set the SNI hostname, use the `common_name` field in `esp_tls_cfg_t`. This ensures that the correct hostname is sent to the server during the handshake.
> - The value of `common_name` must match the server certificate's CN (Common Name).
> - The `skip_common_name` field should be set to `false` to ensure the server certificate is properly validated against the hostname. This is required for SNI to function correctly.
>
> Example:
>
> ``` c
> esp_tls_cfg_t cfg = {
>     .cacert_buf = ...,
>     .cacert_bytes = ...,
>     .common_name = "example.com", // SNI hostname
>     .skip_common_name = false,    // Ensure certificate is validated
> };
> ```
>
> ## ESP-TLS Server Cert Selection Hook
>
> The ESP-TLS component provides an option to set the server certification selection hook when using the MbedTLS stack. This provides an ability to configure and use a certificate selection callback during server handshake. The callback helps to select a certificate to present to the client based on the TLS extensions supplied in the client hello message, such as ALPN and SNI. To enable this feature, please enable `CONFIG_ESP_TLS_SERVER_CERT_SELECT_HOOK` in the ESP-TLS menuconfig.
>
> The certificate selection callback can be configured in the `esp_tls_cfg_t` structure as follows:
>
> ``` c
> int cert_selection_callback(mbedtls_ssl_context *ssl)
> {
>     /* Code that the callback should execute */
>     return 0;
> }
>
> esp_tls_cfg_t cfg = {
>     cert_select_cb = cert_section_callback,
> };
> ```
>
> ## Custom TLS Stack Support
>
> The ESP-TLS component supports registering custom TLS stack implementations via the `esp_tls_register_stack` API. This allows external components to provide their own TLS stack implementation by implementing the `esp_tls_stack_ops_t` interface. Once registered, all TLS connections created after the registration will use the custom stack.
>
> > **Note**
>
> >
> > > >
> > Note
>
>
> As the custom stack implementation is internal to ESP-TLS, switching to a custom stack will not change ESP-TLS specific code for a project.

## How to Use Custom TLS Stack with ESP-IDF

To use a custom TLS stack in your project, follow these steps:

1.  Enable the custom stack option `CONFIG_ESP_TLS_CUSTOM_STACK` (Component config \> ESP-TLS \> SSL/TLS Library \> Custom TLS stack) in menuconfig.

2.  Implement all required functions defined in the `esp_tls_stack_ops_t` structure. The required functions are:

    - `create_ssl_handle` - Initialize TLS/SSL context for a new connection
    - `handshake` - Perform TLS handshake
    - `read` - Read decrypted data from TLS connection
    - `write` - Write and encrypt data to TLS connection
    - `conn_delete` - Clean up TLS connection and free resources
    - `net_init` - Initialize network context
    - `get_ssl_context` - Get stack-specific SSL context
    - `get_bytes_avail` - Get bytes available for reading
    - `init_global_ca_store` - Initialize global CA store
    - `set_global_ca_store` - Load CA certificates into global store
    - `get_global_ca_store` - Get global CA store
    - `free_global_ca_store` - Free global CA store
    - `get_ciphersuites_list` - Get list of supported ciphersuites

    Optional functions (can be NULL if not supported):

    - `get_client_session` - Get client session ticket (if CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS enabled)
    - `free_client_session` - Free client session (if CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS enabled)
    - `server_session_ticket_ctx_init` - Initialize server session ticket context (if CONFIG_ESP_TLS_SERVER_SESSION_TICKETS enabled)
    - `server_session_ticket_ctx_free` - Free server session ticket context (if CONFIG_ESP_TLS_SERVER_SESSION_TICKETS enabled)
    - `server_session_create` - Create server session (server-side, can be NULL if server_session_init is provided)
    - `server_session_init` - Initialize server session (server-side, can be NULL if server_session_create is provided)
    - `server_session_continue_async` - Continue async server handshake (server-side, can be NULL if server_session_create is provided)
    - `server_session_delete` - Delete server session (server-side, can be NULL, conn_delete will be used)

3.  Create a static/global structure containing your function implementations:

    ``` c
    #include "esp_tls_custom_stack.h"

    static const esp_tls_stack_ops_t my_tls_ops = {
        .version = ESP_TLS_STACK_OPS_VERSION,
        .create_ssl_handle = my_create_ssl_handle,
        .handshake = my_handshake,
        .read = my_read,
        .write = my_write,
        .conn_delete = my_conn_delete,
        .net_init = my_net_init,
        .get_ssl_context = my_get_ssl_context,
        .get_bytes_avail = my_get_bytes_avail,
        .init_global_ca_store = my_init_global_ca_store,
        .set_global_ca_store = my_set_global_ca_store,
        .get_global_ca_store = my_get_global_ca_store,
        .free_global_ca_store = my_free_global_ca_store,
        .get_ciphersuites_list = my_get_ciphersuites_list,
        // Optional functions can be NULL if not supported
        .get_client_session = NULL,
        .free_client_session = NULL,
        .server_session_ticket_ctx_init = NULL,
        .server_session_ticket_ctx_free = NULL,
        .server_session_create = NULL,
        .server_session_init = NULL,
        .server_session_continue_async = NULL,
        .server_session_delete = NULL,
    };
    ```

4.  Register your custom stack before creating any TLS connections:

    ``` c
    void app_main(void) {
        // The second parameter is user context passed to global callbacks
        // (init_global_ca_store, set_global_ca_store, etc.)
        // Use NULL if not needed, or pass a pointer for C++ implementations
        esp_err_t ret = esp_tls_register_stack(&my_tls_ops, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE("APP", "Failed to register TLS stack: %s", esp_err_to_name(ret));
            return;
        }

        // Now all TLS connections will use your custom stack
        // ... create TLS connections as usual using esp_tls_conn_new(), etc. ...
    }
    ```

> **Important**
>
> ## ATECC608A (Secure Element) with ESP-TLS

ESP-TLS provides support for using ATECC608A cryptoauth chip with ESP32 series of SoCs. The use of ATECC608A is supported only when ESP-TLS is used with MbedTLS as its underlying SSL/TLS stack. ESP-TLS uses MbedTLS as its underlying TLS/SSL stack by default unless changed manually.

> **Note**
>
> To enable the secure element support, and use it in your project for TLS connection, you have to follow the below steps:

1)  Add [esp-cryptoauthlib](https://github.com/espressif/esp-cryptoauthlib) in your project, for details please refer [how to use esp-cryptoauthlib with ESP-IDF](https://github.com/espressif/esp-cryptoauthlib#how-to-use-esp-cryptoauthlib-with-esp-idf).

2)  Enable the menuconfig option `CONFIG_ESP_TLS_USE_SECURE_ELEMENT`:

    ``` none
    menuconfig > Component config > ESP-TLS > Use Secure Element (ATECC608A) with ESP-TLS
    ```

3)  Select type of ATECC608A chip with following option:

    ``` none
    menuconfig > Component config > esp-cryptoauthlib > Choose Type of ATECC608A chip
    ```

    To know more about different types of ATECC608A chips and how to obtain the type of ATECC608A connected to your ESP module, please visit [ATECC608A chip type](https://github.com/espressif/esp-cryptoauthlib/blob/master/esp_cryptoauth_utility/README.md#find-type-of-atecc608a-chip-connected-to-esp32-wroom32-se).

4)  Enable the use of ATECC608A in ESP-TLS by providing the following config option in `esp_tls_cfg_t`:

    ``` c
    esp_tls_cfg_t cfg = {
        /* other configurations options */
        .use_secure_element = true,
    };
    ```

SOC_DIG_SIGN_SUPPORTED

## Digital Signature with ESP-TLS

ESP-TLS provides support for using the Digital Signature (DS) with {IDF_TARGET_NAME}. Use of the DS for TLS is supported only when ESP-TLS is used with MbedTLS (default stack) as its underlying SSL/TLS stack. For more details on Digital Signature, please refer to the `Digital Signature (DS) </api-reference/peripherals/ds>`. The technical details of Digital Signature such as how to calculate private key parameters can be found in **{IDF_TARGET_NAME} Technical Reference Manual** \> **Digital Signature (DS)** \[[PDF](%7BIDF_TARGET_TRM_EN_URL%7D#digsig)\]. The DS peripheral must be configured before it can be used to perform Digital Signature, see `configure-the-ds-peripheral`.

The DS peripheral must be initialized with the required encrypted private key parameters, which are obtained when the DS peripheral is configured. ESP-TLS internally initializes the DS peripheral when provided with the required DS context, i.e., DS parameters. Please see the below code snippet for passing the DS context to the ESP-TLS context. The DS context passed to the ESP-TLS context should not be freed till the TLS connection is deleted.

``` c
#include "esp_tls.h"
esp_ds_data_ctx_t *ds_ctx;
/* initialize ds_ctx with encrypted private key parameters, which can be read from the nvs or provided through the application code */
esp_tls_cfg_t cfg = {
    .clientcert_buf = /* the client certification */,
    .clientcert_bytes = /* length of the client certification */,
    /* other configurations options */
    .ds_data = (void *)ds_ctx,
};
```

> **Note**
>
> >
> Note

When using Digital Signature for the TLS connection, along with the other required params, only the client certification (<span class="title-ref">clientcert_buf</span>) and the DS params (<span class="title-ref">ds_data</span>) are required and the client key (<span class="title-ref">clientkey_buf</span>) can be set to NULL.

- A mutual-authentication example that utilizes the DS peripheral is shipped with the standalone [espressif/mqtt](https://components.espressif.com/components/espressif/mqtt) component and internally relies on ESP-TLS for the TLS connection. Follow the component documentation to fetch and build that example.

SOC_ECDSA_SUPPORTED

## ECDSA Peripheral with ESP-TLS

ESP-TLS provides support for using the ECDSA peripheral with {IDF_TARGET_NAME}. The use of ECDSA peripheral is supported only when ESP-TLS is used with MbedTLS as its underlying SSL/TLS stack. The ECDSA private key should be present in the eFuse for using the ECDSA peripheral. Please refer to `ECDSA Guide <../peripherals/ecdsa>` for programming the ECDSA key in the eFuse.

This will enable the use of ECDSA peripheral for private key operations. As the client private key is already present in the eFuse, it need not be supplied to the `esp_tls_cfg_t` structure. Please see the below code snippet for enabling the use of ECDSA peripheral for a given ESP-TLS connection.

``` c
#include "esp_tls.h"
esp_tls_cfg_t cfg = {
    .use_ecdsa_peripheral = true,
    .ecdsa_key_efuse_blk = 4,     // Low eFuse block for ECDSA key
    .ecdsa_key_efuse_blk_high = 5,   // High eFuse block for ECDSA key (SECP384R1 only)
    .ecdsa_curve = ESP_TLS_ECDSA_CURVE_SECP384R1, // set this to ESP_TLS_ECDSA_CURVE_SECP256R1 for SECP256R1 curve
};
```

> **Note**
>
> </div>

## Client Session Tickets

ESP-TLS supports client-side session resumption, which can significantly reduce the time and resources spent on full TLS handshakes for subsequent connections to the same server. This feature is available when ESP-TLS uses MbedTLS as its underlying SSL/TLS stack.

The mechanism for session resumption differs slightly between TLS versions:

- **TLS 1.2**: Session resumption can be achieved using session IDs (managed internally by the TLS stack) or session tickets (as per [RFC 5077](https://tools.ietf.org/html/rfc5077)). ESP-TLS focuses on the session ticket mechanism for explicit application control.
- **TLS 1.3**: Session resumption is accomplished exclusively through session tickets, which are sent by the server via a "NewSessionTicket" message after the main handshake is complete. Unlike TLS 1.2, these tickets can be sent at any time during the session, not just immediately after the handshake.

To enable and use client session tickets:

1.  Enable the Kconfig option `CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS`.

2.  After a successful TLS connection (and handshake completion), retrieve the session ticket using `esp_tls_get_client_session`.

    > - **For TLS 1.3**: Since session tickets can arrive from the server at any point after the handshake, an application might need to call `esp_tls_get_client_session` periodically or after specific application-level exchanges if it wants to ensure it has the most recent ticket. Each new ticket received and processed by the TLS stack supersedes the previous one for future resumption attempts.

3.  Store this session ticket securely.

4.  For subsequent connections to the same server, provide the stored session ticket in the `esp_tls_cfg_t::client_session` field.

5.  Remember to free the client session context using `esp_tls_free_client_session` when it's no longer needed or before obtaining a new one.

``` c
#include "esp_tls.h"

// Global or persistent storage for the client session
esp_tls_client_session_t *saved_session = NULL;

void connect_to_server(bool use_saved_session_arg) {
    esp_tls_cfg_t cfg = {0}; // Initialize other config parameters as needed
    // ... set other cfg members like cacert_buf, common_name etc. ...

    if (use_saved_session_arg && saved_session) {
        cfg.client_session = saved_session;
        // ESP_LOGI(TAG, "Attempting connection with saved session ticket.");
    } else {
        // ESP_LOGI(TAG, "Attempting connection without a saved session ticket (full handshake).");
    }

    esp_tls_t *tls = esp_tls_init();
    if (!tls) {
        // ESP_LOGE(TAG, "Failed to initialize ESP-TLS handle.");
        return;
    }

    if (esp_tls_conn_http_new_sync("https://your-server.com", &cfg, tls) == 1) {
        // ESP_LOGI(TAG, "Connection successful.");

        // Always try to get/update the session ticket to have the latest one.
        // This is beneficial whether the connection was a new handshake or a resumption,
        // especially for TLS 1.3 where new tickets can arrive post-handshake.
        if (saved_session) {
            esp_tls_free_client_session(saved_session); // Free previous session if any
            saved_session = NULL;
        }
        saved_session = esp_tls_get_client_session(tls);
        if (saved_session) {
            // ESP_LOGI(TAG, "Successfully retrieved/updated client session ticket.");
        } else {
            // ESP_LOGW(TAG, "Failed to get client session ticket even after a successful connection.");
        }

        // ... do TLS communication ...

    }
    esp_tls_conn_destroy(tls);
}
```

> **Note**
>
> ## TLS Ciphersuites

ESP-TLS provides the ability to set a ciphersuites list in client mode. The TLS ciphersuites list informs the server about the supported ciphersuites for the specific TLS connection regardless of the TLS stack configuration. If the server supports any ciphersuite from this list, then the TLS connection will succeed; otherwise, it will fail.

You can set `ciphersuites_list` in the `esp_tls_cfg_t` structure during client connection as follows:

``` c
/* ciphersuites_list must end with 0 and must be available in the memory scope active during the entire TLS connection */
static const int ciphersuites_list[] = {MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384, MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384, 0};
esp_tls_cfg_t cfg = {
    .ciphersuites_list = ciphersuites_list,
};
```

ESP-TLS will not check the validity of `ciphersuites_list` that was set, you should call `esp_tls_get_ciphersuites_list` to get ciphersuites list supported in the TLS stack and cross-check it against the supplied list.

> **Note**
>
> ## TLS Protocol Version

ESP-TLS provides the ability to set the TLS protocol version for the respective TLS connection. Once the version is specified, it should be exclusively used to establish the TLS connection. This provides an ability to route different TLS connections to different protocol versions like TLS 1.2 and TLS 1.3 at runtime.

> **Note**
>
> To set TLS protocol version with ESP-TLS, set `esp_tls_cfg_t::tls_version` to the required protocol version from `esp_tls_proto_ver_t`. If the protocol version field is not set, then the default policy is to allow TLS connection based on the server requirement.

The ESP-TLS connection can be configured to use the specified protocol version as follows:

``` c
#include "esp_tls.h"
esp_tls_cfg_t cfg = {
    .tls_version = ESP_TLS_VER_TLS_1_2,
};
```

## API Reference

inc/esp_tls.inc

inc/esp_tls_errors.inc

inc/esp_tls_custom_stack.inc

