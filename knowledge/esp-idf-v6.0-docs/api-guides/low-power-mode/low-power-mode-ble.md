<!-- Source: _sources/api-guides/low-power-mode/low-power-mode-ble.rst.txt (ESP-IDF v6.0 documentation) -->

# Introduction to Low Power Mode in Bluetooth<sup>®</sup> Low Energy Scenarios

This section introduces clock source selection in low power modes for Bluetooth Low Energy (Bluetooth LE), along with common related issues.

## Clock Source Selection in Low Power Mode

According to the Bluetooth specification, the sleep clock accuracy must be within 500 PPM. Make sure the clock source selected for Bluetooth LE low power mode meets this requirement. Otherwise, Bluetooth LE may not perform normally and can cause a series of problems, such as ACL connection establishment failure or ACL connection timeout.

### Selecting Main XTAL

To select the main XTAL as the Bluetooth LE internal clock source, configure the following option:

<!-- Only for: esp32 -->
`CONFIG_BTDM_CTRL_LOW_POWER_CLOCK` = Main crystal (<span class="title-ref">CONFIG_BTDM_CTRL_LPCLK_SEL_MAIN_XTAL</span>)

<!-- Only for: esp32c3 or esp32s3 -->
`CONFIG_BT_CTRL_LOW_POWER_CLOCK` = Main crystal (<span class="title-ref">CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL</span>)

<!-- Only for: esp32c2 or esp32c6 or esp32h2 or esp32c5 or esp32c61 -->
`CONFIG_BT_LE_LP_CLK_SRC` = Use main XTAL as RTC clock source (<span class="title-ref">CONFIG_BT_LE_LP_CLK_SRC_MAIN_XTAL</span>)

When this is selected, the main XTAL remains powered on during light-sleep, resulting in higher current consumption. Please refer to `Power Save README <bluetooth/nimble/power_save/README.md>` for the typical current consumption in light-sleep using XTAL versus a 32 kHz external crystal.

### Selecting 32 kHz External Crystal

To use a 32 kHz external crystal as the Bluetooth LE internal clock source, configure the following options:

**Configuration Path 1:**

<!-- Only for: esp32 -->
`CONFIG_BTDM_CTRL_LOW_POWER_CLOCK` = External 32 kHz crystal/oscillator (<span class="title-ref">CONFIG_BTDM_CTRL_LPCLK_SEL_EXT_32K_XTAL</span>)

<!-- Only for: esp32c3 or esp32s3 -->
`CONFIG_BT_CTRL_LOW_POWER_CLOCK` = External 32 kHz crystal/oscillator (<span class="title-ref">CONFIG_BT_CTRL_LPCLK_SEL_EXT_32K_XTAL</span>)

<!-- Only for: esp32c2 or esp32c6 or esp32h2 or esp32c5 or esp32c61 -->
`CONFIG_BT_LE_LP_CLK_SRC` = Use system RTC slow clock source (<span class="title-ref">CONFIG_BT_LE_LP_CLK_SRC_DEFAULT</span>)

**Configuration Path 2:**

`CONFIG_RTC_CLK_SRC` = External 32 kHz crystal (<span class="title-ref">CONFIG_RTC_CLK_SRC_EXT_CRYS</span>)

**Note:** Even if 32 kHz is selected in menuconfig, the system will fall back to the main XTAL if the external crystal is not detected during Bluetooth LE initialization. This may lead to unexpected current consumption in light-sleep mode.

### Selecting 136 kHz RC Oscillator

<!-- Only for: esp32c3 or esp32s3 -->
To use a 136 kHz internal RC oscillator as the Bluetooth LE internal clock source, configure the following option:

**Configuration Path 1:**

`CONFIG_BT_CTRL_LOW_POWER_CLOCK` = Internal 136kHz RC oscillator (<span class="title-ref">CONFIG_BT_CTRL_LPCLK_SEL_RTC_SLOW</span>)

Generally, the 136 kHz RC oscillator cannot meet the accuracy requirement of Bluetooth LE. It is only suitable for scenarios with low clock accuracy requirements, such as legacy advertising (ADV) or scanning. It does not support connections in central or peripheral roles.

<!-- Only for: esp32 -->
**Note:** ESP32 does not support using 136 kHz RC oscillator as the Bluetooth LE clock source.

<!-- Only for: esp32c2 or esp32c6 or esp32h2 or esp32c5 or esp32c61 -->
To use a 136 kHz internal RC oscillator as the Bluetooth LE internal clock source, configure the following options:

**Configuration Path 1:**

`CONFIG_BT_LE_LP_CLK_SRC` = Use system RTC slow clock source (<span class="title-ref">CONFIG_BT_LE_LP_CLK_SRC_DEFAULT</span>)

<!-- Only for: not esp32 -->
**Configuration Path 2:**

`CONFIG_RTC_CLK_SRC` = Internal 136 kHz RC oscillator (<span class="title-ref">CONFIG_RTC_CLK_SRC_INT_RC</span>)

<!-- Only for: esp32c2 or esp32c6 or esp32h2 or esp32c5 or esp32c61 -->
If low current consumption is required but there is no access to the External 32 kHz Crystal, this clock source is recommended. However, this clock source has a sleep clock accuracy exceeding 500 PPM, which is only supported when pairing with another ESP chip. For non-ESP peer devices, the following Bluetooth LE features are not supported:

1.  Central role of Connection
2.  Advertiser of Periodic Advertising

If the peer device also uses 136 kHz RC as the clock source, the following configuration should be set:

**Configuration Path:**

> - `CONFIG_BT_LE_LL_PEER_SCA_SET_ENABLE` = y
> - `CONFIG_BT_LE_LL_PEER_SCA` = 3000

**Note:** Using the 136 kHz RC oscillator may occasionally cause issues such as connection establishment failures or connection timeouts.

### How to Check the Current Clock Source Used by Bluetooth LE

You can check the current Bluetooth LE clock source from the initialization logs:

| Log Message                                                        | Clock Source                             |
|--------------------------------------------------------------------|------------------------------------------|
| Using main XTAL as clock source                                    | Main XTAL                                |
| Using 136 kHz RC as clock source                                   | Internal 136 kHz RC oscillator           |
| Using external 32.768 kHz crystal as clock source                  | External 32 kHz crystal                  |
| Using external 32.768 kHz oscillator at 32K_XP pin as clock source | External 32 kHz oscillator at 32K_XP pin |

Bluetooth LE Initialization Logs and Clock Sources

## FAQ

### 1. Bluetooth LE ACL Connection Fails or Disconnects in Low Power Mode

As explained in the clock source selection section above, when ACL connections fail to establish or unexpectedly disconnect in low power mode, first verify whether the current clock source meets Bluetooth LE accuracy requirements.

### 2. Measured light-sleep Current Higher Than Expected

As described in the clock source selection section above, if the main XTAL is used as the clock source, it remains powered on during light-sleep, resulting in higher current consumption than other clock sources. The average current may vary depending on the specific application, Bluetooth LE configuration, and the duration spent in light-sleep. Some applications may have higher average current because Bluetooth LE is active for a larger proportion of the time transmitting and receiving.

### 3. Unable to Enter light-sleep Mode

If Auto light-sleep is enabled but the device fails to enter light-sleep, it's usually due to insufficient IDLE time, which prevents the automatic entry conditions from being met. This can be caused by excessive logging or Bluetooth LE configurations that reduce IDLE time, such as continuous scanning.
