<!-- Source: _sources/security/security.rst.txt (ESP-IDF v6.0 documentation) -->

# Security Overview

{IDF_TARGET_CIPHER_SCHEME:default="RSA", esp32h2="RSA or ECDSA", esp32p4="RSA or ECDSA", esp32c5="RSA or ECDSA", esp32c61="ECDSA", esp32h21="RSA or ECDSA"}

{IDF_TARGET_SIG_PERI:default="DS", esp32h2="DS or ECDSA", esp32p4="DS or ECDSA", esp32c5="DS or ECDSA"}

This guide provides an overview of the overall security features available in various Espressif solutions. It is highly recommended to consider this guide while designing the products with the Espressif platform and the ESP-IDF software stack from the **security** perspective.

> **Note**
>
> >
> Note

In this guide, most used commands are in the form of `idf.py secure-<command>`, which is a wrapper around corresponding `espsecure <command>`. The `idf.py` based commands provides more user-friendly experience, although may lack some of the advanced functionality of their `espsecure` based counterparts.

TARGET_SUPPORT_QEMU

> **Important**
>
> </div>

## Goals

High level security goals are as follows:

1.  Preventing untrustworthy code from being executed
2.  Protecting the identity and integrity of the code stored in the off-chip flash memory
3.  Securing device identity
4.  Secure storage for confidential data
5.  Authenticated and encrypted communication from the device

## Platform Security

### Secure Boot

The Secure Boot feature ensures that only authenticated software can execute on the device. The Secure Boot process forms a chain of trust by verifying all **mutable** software entities involved in the `../api-guides/startup`. Signature verification happens during both boot-up as well as in OTA updates.

Please refer to `secure-boot-v2` for detailed documentation about this feature.

<!-- Only for: esp32 -->
For ESP32 before ECO3, please refer to `secure-boot-v1`.

> **Important**
>
> #### Secure Boot Best Practices

- Generate the signing key on a system with a quality source of entropy.
- Always keep the signing key private. A leak of this key will compromise the Secure Boot system.
- Do not allow any third party to observe any aspects of the key generation or signing process using `idf.py secure-` or `espsecure` commands. Both processes are vulnerable to timing or other side-channel attacks.
- Ensure that all security eFuses have been correctly programmed, including disabling of the debug interfaces, non-required boot mediums (e.g., UART DL mode), etc.

### Flash Encryption

The Flash Encryption feature helps to encrypt the contents on the off-chip flash memory and thus provides the **confidentiality** aspect to the software or data stored in the flash memory.

Please refer to `flash-encryption` for detailed information about this feature.

SOC_SPIRAM_SUPPORTED and not esp32

If {IDF_TARGET_NAME} is connected to an external SPI RAM, the contents written to or read from the SPI RAM will also be encrypted and decrypted respectively (via the MMU's flash cache, provided that FLash Encryption is enabled). This provides an additional safety layer for the data stored in SPI RAM, hence configurations like `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` can be safely enabled in this case.

#### Flash Encryption Best Practices

- It is recommended to use `flash-enc-release-mode` for the production use-cases.
- It is recommended to have a unique flash encryption key per device.
- Enable `secure_boot-guide` as an extra layer of protection, and to prevent an attacker from selectively corrupting any part of the flash before boot.

SOC_DIG_SIGN_SUPPORTED

### Device Identity

The Digital Signature peripheral in {IDF_TARGET_NAME} produces hardware-accelerated RSA digital signatures with the assistance of HMAC, without the RSA private key being accessible by software. This allows the private key to be kept secured on the device without anyone other than the device hardware being able to access it.

SOC_ECDSA_SUPPORTED

{IDF_TARGET_NAME} also supports ECDSA peripheral for generating hardware-accelerated ECDSA digital signatures. ECDSA private key can be directly programmed in an eFuse block and marked as read protected from the software.

{IDF_TARGET_SIG_PERI} peripheral can help to establish the **Secure Device Identity** to the remote endpoint, e.g., in the case of TLS mutual authentication based on the {IDF_TARGET_CIPHER_SCHEME} cipher scheme.

not SOC_ECDSA_SUPPORTED

Please refer to the `../api-reference/peripherals/ds` for detailed documentation.

SOC_ECDSA_SUPPORTED

Please refer to the `../api-reference/peripherals/ecdsa` and `../api-reference/peripherals/ds` guides for detailed documentation.

</div>

SOC_MEMPROT_SUPPORTED or SOC_CPU_IDRAM_SPLIT_USING_PMP

### Memory Protection

{IDF_TARGET_NAME} supports the **Memory Protection** scheme, either through architecture or special peripheral like PMS, which provides an ability to enforce and monitor permission attributes to memory and, in some cases, peripherals. ESP-IDF application startup code configures the permissions attributes like Read/Write access on data memories and Read/Execute access on instruction memories using the relevant peripheral. If there is any attempt made that breaks these permission attributes, e.g., a write operation to instruction memory region, then a violation interrupt is raised, and it results in system panic.

This feature depends on the config option `CONFIG_ESP_SYSTEM_MEMPROT` and it is kept enabled by default. Please note that the API for this feature is **private** and used exclusively by ESP-IDF code only.

> **Note**
>
> </div>

SOC_CRYPTO_DPA_PROTECTION_SUPPORTED or SOC_AES_SUPPORT_PSEUDO_ROUND_FUNCTION

### Protection Against Side-Channel Attacks

SOC_CRYPTO_DPA_PROTECTION_SUPPORTED

#### DPA (Differential Power Analysis) Protection

{IDF_TARGET_NAME} has support for protection mechanisms against the Differential Power Analysis related security attacks. DPA protection dynamically adjusts the clock frequency of the crypto peripherals, thereby blurring the power consumption trajectory during its operation. Based on the configured DPA security level, the clock variation range changes. Please refer to the *{IDF_TARGET_NAME} Technical Reference Manual* \[[PDF](%7BIDF_TARGET_TRM_EN_URL%7D)\]. for more details on this topic.

`CONFIG_ESP_CRYPTO_DPA_PROTECTION_LEVEL` can help to select the DPA level. Higher level means better security, but it can also have an associated performance impact. By default, the lowest DPA level is kept enabled but it can be modified based on the security requirement.

> **Note**
>
> >
> Note

Please note that hardware `RNG <../api-reference/system/random>` must be enabled for DPA protection to work correctly.

</div>

SOC_AES_SUPPORT_PSEUDO_ROUND_FUNCTION

#### AES Peripheral's Pseudo-Round Function

{IDF_TARGET_NAME} incorporates a pseudo-round function in the AES peripheral, thus enabling the peripheral to randomly insert pseudo-rounds before and after the original operation rounds and also generate a pseudo key to perform these dummy operations. These operations do not alter the original result, but they increase the complexity to perform side channel analysis attacks by randomizing the power profile.

`CONFIG_MBEDTLS_AES_USE_PSEUDO_ROUND_FUNC_STRENGTH` can be used to select the strength of the pseudo-round function. Increasing the strength improves the security provided, but would slow down the encrryption/decryption operations.

| **Strength** | **Performance Impact** |
|--------------|------------------------|
| Low          | 20.9 %                 |
| Medium       | 47.6 %                 |
| High         | 72.4 %                 |

Performance impact on AES operations per strength level

Considering the above performance impact, ESP-IDF by-default does not enable the pseudo-round function to avoid any performance-related degrade. But it is recommended to enable the pseudo-round function for better security.

</div>

### Debug Interfaces

#### JTAG

- JTAG interface stays disabled if any of the security features are enabled. Please refer to `jtag-debugging-security-features` for more information.

\- JTAG interface can also be disabled in the absence of any other security features using `efuse_API`. :SOC_HMAC_SUPPORTED: - {IDF_TARGET_NAME} supports soft disabling the JTAG interface and it can be re-enabled by programming a secret key through HMAC. (`hmac_for_enabling_jtag`)

#### UART Download Mode

<!-- Only for: esp32 -->
For ESP32 ECO3 case, UART Download mode stays disabled if any of the security features are enabled in their release configuration. Alternatively, it can also be disabled by calling `esp_efuse_disable_rom_download_mode` at runtime.

> **Important**
>
> SOC_SUPPORTS_SECURE_DL_MODE

In {IDF_TARGET_NAME}, Secure UART Download mode gets activated if any of the security features are enabled.

- Secure UART Download mode can also be enabled by calling `esp_efuse_enable_rom_secure_download_mode`.
- This mode does not allow any arbitrary code to execute if downloaded through the UART download mode.
- It also limits the available commands in Download mode to update SPI config, e.g., changing baud rate, basic flash write, and the command to return a summary of currently enabled security features (`get-security-info`).
- To disable Download Mode entirely, select the `CONFIG_SECURE_UART_ROM_DL_MODE` to the recommended option `Permanently disable ROM Download Mode` or call `esp_efuse_disable_rom_download_mode` at runtime.

> **Important**
>
> </div>

SOC_WIFI_SUPPORTED

## Network Security

### Wi-Fi

In addition to the traditional security methods (WEP/WPA-TKIP/WPA2-CCMP), Wi-Fi driver in ESP-IDF also supports additional state-of-the-art security protocols. Please refer to the `../api-guides/wifi-security` for detailed documentation.

### TLS (Transport Layer Security)

It is recommended to use TLS (Transport Layer Security) in all external communications (e.g., cloud communication, OTA updates) from the ESP device. ESP-IDF supports `../api-reference/protocols/mbedtls` as the official TLS stack.

TLS is default integrated in `../api-reference/protocols/esp_http_client`, `../api-reference/protocols/esp_https_server` and several other components that ship with ESP-IDF.

> **Note**
>
> #### ESP-TLS Abstraction

ESP-IDF provides an abstraction layer for the most-used TLS functionalities. Hence, it is recommended that an application uses the API exposed by `../api-reference/protocols/esp_tls`.

`esp_tls_server_verification` section highlights diverse ways in which the identity of server could be established on the device side.

#### ESP Certificate Bundle

The `../api-reference/protocols/esp_crt_bundle` API provides an easy way to include a bundle of custom x509 root certificates for TLS server verification. The certificate bundle is the easiest way to verify the identity of almost all standard TLS servers.

> **Important**
>
> #### Managing Root Certificates

Root Certificates embedded inside the application must be managed carefully. Any update to the root certificate list or the `../api-reference/protocols/esp_crt_bundle` can have an impact on the TLS connection with the remote endpoint. This includes a connection to the OTA update server. In some cases, the problem shall be visible on the next OTA update and it may leave device unable to perform OTA updates forever.

Root certificates list update could have following reasons:

- New firmware has different set of remote endpoint(s).
- The existing certificate has expired.
- The certificate has been added or retracted from the upstream certificate bundle.
- The certificate list changed due to market share statistics (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN` case).

Some guidelines to consider on this topic:

- Please consider enabling `OTA rollback <ota_rollback>` and then keep the successful connection to the OTA update server as the checkpoint to cancel the rollback process. This ensures that the newly updated firmware can successfully reach till the OTA update server, otherwise rollback process will go back to the previous firmware on the device.
- If you plan to enable the `CONFIG_MBEDTLS_HAVE_TIME_DATE` option, then please consider to have the time sync mechanism (SNTP) and sufficient number of trusted certificates in place.

## Product Security

SOC_WIFI_SUPPORTED

### Secure Provisioning

Secure Provisioning refers to a process of secure on-boarding of the ESP device on to the Wi-Fi network. This mechanism also allows provision of additional custom configuration data during the initial provisioning phase from the provisioning entity, e.g., Smartphone.

ESP-IDF provides various security schemes to establish a secure session between ESP and the provisioning entity, they are highlighted at `provisioning_security_schemes`.

Please refer to [network_provisioning](https://github.com/espressif/idf-extra-components/tree/master/network_provisioning) for details and the example code for this feature.

> **Note**
>
> </div>

### Secure OTA (Over-the-air) Updates

- OTA Updates must happen over secure transport, e.g., HTTPS.
- ESP-IDF provides a simplified abstraction layer `../api-reference/system/esp_https_ota` for this.
- If `secure_boot-guide` is enabled, then the server should host the signed application image.
- If `flash_enc-guide` is enabled, then no additional steps are required on the server side, encryption shall be taken care on the device itself during flash write.
- OTA update `ota_rollback` can help to switch the application as `active` only after its functionality has been verified.

#### Anti-Rollback Protection

Anti-rollback protection feature ensures that device only executes the application that meets the security version criteria as stored in its eFuse. So even though the application is trusted and signed by legitimate key, it may contain some revoked security feature or credential. Hence, device must reject any such application.

ESP-IDF allows this feature for the application only and it is managed through 2nd stage bootloader. The security version is stored in the device eFuse and it is compared against the application image header during both boot-up and over-the-air updates.

Please see more information to enable this feature in the `anti-rollback` guide.

#### Encrypted Firmware Distribution

Encrypted firmware distribution during over-the-air updates ensures that the application stays encrypted **in transit** from the server to the the device. This can act as an additional layer of protection on top of the TLS communication during OTA updates and protect the identity of the application.

Please see working example for this documented in `ota_updates_pre-encrypted-firmware` section.

### Secure Storage

Secure storage refers to the application-specific data that can be stored in a secure manner on the device, i.e., off-chip flash memory. This is typically a read-write flash partition and holds device specific configuration data, e.g., Wi-Fi credentials.

ESP-IDF provides the **NVS (Non-volatile Storage)** management component which allows encrypted data partitions. This feature is tied with the platform `flash_enc-guide` feature described earlier.

Please refer to the `nvs_encryption` for detailed documentation on the working and instructions to enable this feature.

> **Important**
>
> ### Secure Device Control

ESP-IDF provides capability to control an ESP device over `Wi-Fi/Ethernet + HTTP` or `BLE` in a secure manner using ESP Local Control component.

Please refer to the `../api-reference/protocols/esp_local_ctrl` for detailed documentation about this feature.

## Security Policy

The ESP-IDF GitHub repository has attached [Security Policy Brief](https://github.com/espressif/esp-idf/blob/master/SECURITY.md).

### Advisories

- Espressif publishes critical [Security Advisories](https://www.espressif.com/en/support/documents/advisories), which includes security advisories regarding both hardware and software.
- The specific advisories of the ESP-IDF software components are published through the [GitHub repository](https://github.com/espressif/esp-idf/security/advisories).

### Software Updates

Critical security issues in the ESP-IDF components, and third-party libraries are fixed as and when we find them or when they are reported to us. Gradually, we make the fixes available in all applicable release branches in ESP-IDF.

Applicable security issues and CVEs for the ESP-IDF components, third-party libraries are mentioned in the ESP-IDF release notes.

> **Important**
>
> 