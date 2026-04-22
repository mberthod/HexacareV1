<!-- Source: _sources/api-reference/system/random.rst.txt (ESP-IDF v6.0 documentation) -->

# Random Number Generation

{IDF_TARGET_RF_NAME: default="Wi-Fi or Bluetooth", esp32s2="Wi-Fi", esp32h2="Bluetooth or 802.15.4 Thread/Zigbee", esp32c6="Wi-Fi or Bluetooth or 802.15.4 Thread/Zigbee", esp32c5="Wi-Fi or Bluetooth or 802.15.4 Thread/Zigbee"} {IDF_TARGET_RF_IS: default="are", esp32s2="is"}

{IDF_TARGET_NAME} contains a hardware random number generator (RNG). You can use the APIs `esp_random` and `esp_fill_random` to obtained random values from it.

Every 32-bit value that the system reads from the RNG_DATA_REG register of the random number generator is a true random number. These true random numbers are generated based on the thermal noise in the system and the asynchronous clock mismatch.

SOC_WIFI_SUPPORTED or SOC_IEEE802154_SUPPORTED or SOC_BT_SUPPORTED

- Thermal noise comes from the high-speed ADC or SAR ADC or both. Whenever the high-speed ADC or SAR ADC is enabled, bit streams will be generated and fed into the random number generator through an XOR logic gate as random seeds.

not SOC_WIFI_SUPPORTED and not SOC_IEEE802154_SUPPORTED and not SOC_BT_SUPPORTED

- Thermal noise comes from the SAR ADC. Whenever the SAR ADC is enabled, bit streams will be generated and fed into the random number generator through an XOR logic gate as random seeds.

<!-- Only for: not esp32 -->
- RC_FAST_CLK is an asynchronous clock source, and it increases the RNG entropy by introducing circuit metastability. See the `secondary entropy` section for more details.

The following diagram shows the noise sources for the RNG on the {IDF_TARGET_NAME}:

SOC_WIFI_SUPPORTED or SOC_IEEE802154_SUPPORTED or SOC_BT_SUPPORTED

<img src="/../_static/esp_rng_noise_source_rf_available.svg" class="align-center" alt="image" />

not SOC_WIFI_SUPPORTED and not SOC_IEEE802154_SUPPORTED and not SOC_BT_SUPPORTED

<img src="/../_static/esp_rng_noise_source_rf_unavailable.svg" class="align-center" alt="image" />

The hardware RNG produces true random numbers so long as one or more of the following conditions are met:

SOC_WIFI_SUPPORTED or SOC_IEEE802154_SUPPORTED or SOC_BT_SUPPORTED  
- RF subsystem is enabled, i.e., {IDF_TARGET_RF_NAME} {IDF_TARGET_RF_IS} enabled. When enabled, the RF subsystem internally enables the High Speed ADC that can be used as the entropy source. The High Speed ADC may only be available when the respective RF subsystem is active (e.g., not in sleep mode). See the `enabling RF subsystem` section for more details.

- The internal entropy source SAR ADC has been enabled by calling `bootloader_random_enable` and not yet disabled by calling `bootloader_random_disable`.
- While the ESP-IDF `second-stage-bootloader` is running. This is because the default ESP-IDF bootloader implementation calls `bootloader_random_enable` when the bootloader starts, and `bootloader_random_disable` before executing the application.

When any of these conditions are true, samples of physical noise are continuously mixed into the internal hardware RNG state to provide entropy. Consult the **{IDF_TARGET_NAME} Technical Reference Manual** \> **Random Number Generator (RNG)** \[[PDF](%7BIDF_TARGET_TRM_EN_URL%7D#rng)\] chapter for more details.

If none of the above conditions are true, the output of the RNG should be considered as pseudo-random only.

SOC_WIFI_SUPPORTED or SOC_IEEE802154_SUPPORTED or SOC_BT_SUPPORTED

## Enabling RF subsystem

The RF subsystem can be enabled with help of one of the following APIs:

SOC_WIFI_SUPPORTED  
- Wi-Fi: `esp_wifi_start`

SOC_BT_SUPPORTED  
- Bluetooth (NimBLE): `nimble_port_init()` which internally calls `esp_bt_controller_enable()`

SOC_BT_SUPPORTED  
- Bluetooth (Bluedroid): `esp_bt_controller_enable()`

SOC_IEEE802154_SUPPORTED  
- Thread/Zigbee: `esp_openthread_init`

</div>

## Startup

During startup, the ESP-IDF bootloader temporarily enables the non-RF internal entropy source (SAR ADC using internal reference voltage noise) that provides entropy for any first boot key generation.

not SOC_WIFI_SUPPORTED and not SOC_IEEE802154_SUPPORTED and not SOC_BT_SUPPORTED

For {IDF_TARGET_NAME}, the High Speed ADC is not available. Hence, the non-RF internal entropy source (SAR ADC) is kept enabled by default at the time of application startup.

SOC_WIFI_SUPPORTED or SOC_IEEE802154_SUPPORTED or SOC_BT_SUPPORTED

However, after the application starts executing, then normally only pseudo-random numbers are available until {IDF_TARGET_RF_NAME} {IDF_TARGET_RF_IS} initialized or until the internal entropy source has been enabled again.

To re-enable the entropy source temporarily during application startup, or for an application that does not use {IDF_TARGET_RF_NAME}, call the function `bootloader_random_enable` to re-enable the internal entropy source. The function `bootloader_random_disable` must be called to disable the entropy source again before using any of the following features:

\- ADC :esp32: - I2S :SOC_WIFI_SUPPORTED or SOC_IEEE802154_SUPPORTED or SOC_BT_SUPPORTED: - {IDF_TARGET_RF_NAME}

</div>

> **Note**
>
> > **Note**
>
> <!-- Only for: not esp32 -->
## Secondary Entropy

{IDF_TARGET_NAME} RNG contains a secondary entropy source, based on sampling an asynchronous 8 MHz internal oscillator (see the Technical Reference Manual for details). This entropy source is always enabled in ESP-IDF and is continuously mixed into the RNG state by hardware. In testing, this secondary entropy source was sufficient to pass the [Dieharder](https://webhome.phy.duke.edu/~rgb/General/dieharder.php) random number test suite without the main entropy source enabled (test input was created by concatenating short samples from continuously resetting {IDF_TARGET_NAME}). However, it is currently only guaranteed that true random numbers are produced when the main entropy source is also enabled as described above.

## API Reference

inc/esp_random.inc

inc/bootloader_random.inc

## `getrandom()`

A compatible version of the Linux `getrandom()` function is also provided for ease of porting:

``` c
#include <sys/random.h>

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);
```

This function is implemented by calling `esp_fill_random` internally.

The `flags` argument is ignored. This function is always non-blocking but the strength of any random numbers is dependent on the same conditions described above.

Return value is -1 (with `errno` set to `EFAULT`) if the `buf` argument is NULL, and equal to `buflen` otherwise.

## `getentropy()`

A compatible version of the Linux `getentropy()` function is also provided for easy porting:

``` c
#include <unistd.h>

int getentropy(void *buffer, size_t length);
```

This function is implemented by calling `getrandom` internally.

The strength of any random numbers is dependent on the same conditions described above.

Return value is 0 on success and -1 otherwise with `errno` set to:

> - `EFAULT` if the `buffer` argument is NULL.
> - `EIO` if the `length` is more then 256.
