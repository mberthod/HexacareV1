<!-- Source: _sources/api-reference/peripherals/sdm.rst.txt (ESP-IDF v6.0 documentation) -->

# Sigma-Delta Modulation (SDM)

## Introduction

{IDF_TARGET_NAME} has a second-order sigma-delta modulator, which can generate independent PDM pulses to multiple channels. Please refer to the TRM to check how many hardware channels are available.[^1]

Delta-sigma modulation converts an analog voltage signal into a pulse frequency, or pulse density, which can be understood as pulse-density modulation (PDM) (refer to Delta-sigma modulation on Wikipedia\_).

Typically, a Sigma-Delta modulated channel can be used in scenarios like:

- LED dimming
- Simple DAC (8-bit), with the help of an active RC low-pass filter
- Class D amplifier, with the help of a half-bridge or full-bridge circuit plus an LC low-pass filter

## Functional Overview

The following sections of this document cover the typical steps to install and operate an SDM channel:

- `sdm-resource-allocation` - covers how to initialize and configure an SDM channel and how to recycle the resources when it finishes working.
- `sdm-enable-and-disable-channel` - covers how to enable and disable the channel.
- `sdm-set-equivalent-duty-cycle` - describes how to set the equivalent duty cycle of the PDM pulses.
- `sdm-power-management` - describes how different source clock selections can affect power consumption.
- `sdm-iram-safe` - lists which functions are supposed to work even when the cache is disabled.
- `sdm-thread-safety` - lists which APIs are guaranteed to be thread-safe by the driver.
- `sdm-kconfig-options` - lists the supported Kconfig options that can be used to make a different effect on driver behavior.

### Resource Allocation

In ESP-IDF, the information and attributes of SDM channels are managed and accessed through specific data structures, where the data structure is called `sdm_channel_handle_t`. Each channel is capable to output the binary, hardware-generated signal with the sigma-delta modulation. The driver manages all available channels in a pool so that there is no need to manually assign a fixed channel to a GPIO.

To install an SDM channel, you should call `sdm_new_channel` to get a channel handle. Channel-specific configurations are passed in the `sdm_config_t` structure:

- `sdm_config_t::gpio_num` sets the GPIO that the PDM pulses output from.
- `sdm_config_t::clk_src` selects the source clock for the SDM module. Note that, all channels should select the same clock source.
- `sdm_config_t::sample_rate_hz` sets the sample rate of the SDM module. A higher sample rate can help to output signals with higher SNR (Signal to Noise Ratio), and easier to restore the original signal after the filter.
- `sdm_config_t::invert_out` sets whether to invert the output signal.

The function `sdm_new_channel` can fail due to various errors such as insufficient memory, invalid arguments, etc. Specifically, when there are no more free channels (i.e., all hardware SDM channels have been used up), `ESP_ERR_NOT_FOUND` will be returned.

If a previously created SDM channel is no longer required, you should recycle it by calling `sdm_del_channel`. It allows the underlying HW channel to be used for other purposes. Before deleting an SDM channel handle, you should disable it by `sdm_channel_disable` in advance or make sure it has not been enabled yet by `sdm_channel_enable`.

#### Creating an SDM Channel with a Sample Rate of 1 MHz

``` c
sdm_channel_handle_t chan = NULL;
sdm_config_t config = {
    .clk_src = SDM_CLK_SRC_DEFAULT,
    .sample_rate_hz = 1 * 1000 * 1000,
    .gpio_num = 0,
};
ESP_ERROR_CHECK(sdm_new_channel(&config, &chan));
```

### Enable and Disable Channel

Before doing further IO control to the SDM channel, you should enable it first, by calling `sdm_channel_enable`. Internally, this function:

- switches the channel state from **init** to **enable**
- acquires a proper power management lock if a specific clock source (e.g., APB clock) is selected. See also `sdm-power-management` for more information.

On the contrary, calling `sdm_channel_disable` does the opposite, that is, put the channel back to the **init** state and releases the power management lock.

### Set Pulse Density

For the output PDM signals, the pulse density decides the output analog voltage that is restored by a low-pass filter. The restored analog voltage from the channel is calculated by `Vout = VDD_IO / 256 * duty + VDD_IO / 2`. The range of the quantized `density` input parameter of `sdm_channel_set_pulse_density` is from -128 to 127 (8-bit signed integer). Depending on the value of the `density` parameter, the duty cycle of the output signal will be changed accordingly. For example, if a zero value is set, then the output signal's duty will be around 50%.

### Power Management

When power management is enabled (i.e., `CONFIG_PM_ENABLE` is on), the system will adjust the APB frequency before going into Light-sleep, thus potentially changing the sample rate of the sigma-delta modulator.

However, the driver can prevent the system from changing APB frequency by acquiring a power management lock of type `ESP_PM_APB_FREQ_MAX`. Whenever the driver creates an SDM channel instance that has selected `SDM_CLK_SRC_APB` as its clock source, the driver guarantees that the power management lock is acquired when enabling the channel by `sdm_channel_enable`. Likewise, the driver releases the lock when `sdm_channel_disable` is called for that channel.

### IRAM Safe

There is a Kconfig option `CONFIG_SDM_CTRL_FUNC_IN_IRAM` that can put commonly-used IO control functions into IRAM as well. So that these functions can also be executable when the cache is disabled. These IO control functions are listed as follows:

- `sdm_channel_set_pulse_density`

### Thread Safety

The driver uses critical sections to ensure atomic operations on registers. Key members in the driver handle are also protected by critical sections. The driver's internal state machine uses atomic instructions to ensure thread safety, with state checks preventing certain invalid concurrent operations (e.g., conflicts between <span class="title-ref">enable</span> and <span class="title-ref">delete</span>). Therefore, SDM driver APIs can be used in a multi-threaded environment without extra locking.

The following functions can also be used in an interrupt context:

- `sdm_channel_set_pulse_density`

### Kconfig Options

- `CONFIG_SDM_CTRL_FUNC_IN_IRAM` controls where to place the SDM channel control functions (IRAM or Flash), see `sdm-iram-safe` for more information.
- `CONFIG_SDM_ENABLE_DEBUG_LOG` is used to enable the debug log output. Enabling this option increases the firmware binary size.

## Convert to an Analog Signal (Optional)

Typically, if a Sigma-Delta signal is connected to an LED to adjust the brightness, you do not have to add any filter between them, because our eyes have their own low-pass filters for changes in light intensity. However, if you want to check the real voltage or watch the analog waveform, you need to design an analog low-pass filter. Also, it is recommended to use an active filter instead of a passive filter to gain better isolation and not lose too much voltage.

For example, you can take the following [Sallen-Key topology Low Pass Filter](https://en.wikipedia.org/wiki/Sallen%E2%80%93Key_topology) as a reference.

<figure>
<img src="../../../_static/typical_sallenkey_LP_filter.png" class="align-center" alt="../../../_static/typical_sallenkey_LP_filter.png" />
<figcaption>Sallen-Key Low Pass Filter</figcaption>
</figure>

(Refer to `peripherals/sigma_delta/sdm_dac/README.md` to see the waveforms before and after filtering.)

## Application Examples

- `peripherals/sigma_delta/sdm_dac` demonstrates how to use the sigma-delta driver to act as an 8-bit DAC, and output a 100 Hz sine wave.
- `peripherals/sigma_delta/sdm_led` demonstrates how to use the sigma-delta driver to control the brightness of an LED or LCD backlight.

## API Reference

inc/sdm.inc

inc/sdm_types.inc

[^1]: Different ESP chip series might have different numbers of SDM channels. Please refer to Chapter [GPIO and IOMUX](%7BIDF_TARGET_TRM_EN_URL%7D#iomuxgpio) in {IDF_TARGET_NAME} Technical Reference Manual for more details. The driver does not forbid you from applying for more channels, but it will return an error when all available hardware resources are used up. Please always check the return value when doing resource allocation (e.g., `sdm_new_channel`).
