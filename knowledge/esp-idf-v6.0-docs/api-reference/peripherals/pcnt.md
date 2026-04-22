<!-- Source: _sources/api-reference/peripherals/pcnt.rst.txt (ESP-IDF v6.0 documentation) -->

# Pulse Counter (PCNT)

## Introduction

The PCNT (Pulse Counter) module is designed to count the number of rising and/or falling edges of input signals. The {IDF_TARGET_NAME} contains multiple pulse counter units in the module.[^1] Each unit is in effect an independent counter with multiple channels, where each channel can increment/decrement the counter on a rising/falling edge. Furthermore, each channel can be configured separately.

PCNT channels can react to signals of **edge** type and **level** type, however for simple applications, detecting the edge signal is usually sufficient. PCNT channels can be configured react to both pulse edges (i.e., rising and falling edge), and can be configured to increase, decrease or do nothing to the unit's counter on each edge. The level signal is the so-called **control signal**, which is used to control the counting mode of the edge signals that are attached to the same channel. By combining the usage of both edge and level signals, a PCNT unit can act as a **quadrature decoder**.

Besides that, PCNT unit is equipped with a separate glitch filter, which is helpful to remove noise from the signal.

Typically, a PCNT module can be used in scenarios like:

- Calculate periodic signal's frequency by counting the pulse numbers within a time slice
- Decode quadrature signals into speed and direction

## Functional Overview

Description of the PCNT functionality is divided into the following sections:

- `pcnt-resource-allocation` - covers how to allocate PCNT units and channels with properly set of configurations. It also covers how to recycle the resources when they finished working.
- `pcnt-setup-channel-actions` - covers how to configure the PCNT channel to behave on different signal edges and levels.

\- `pcnt-watch-points` - describes how to configure PCNT watch points (i.e., tell PCNT unit to trigger an event when the count reaches a certain value). :SOC_PCNT_SUPPORT_STEP_NOTIFY: - `pcnt-step-notify` - describes how to configure PCNT watch step (i.e., tell PCNT unit to trigger an event when the count increment reaches a certain value). - `pcnt-register-event-callbacks` - describes how to hook your specific code to the watch point event callback function. - `pcnt-set-glitch-filter` - describes how to enable and set the timing parameters for the internal glitch filter. :SOC_PCNT_SUPPORT_CLEAR_SIGNAL: - `pcnt-set-clear-signal` - describes how to set the parameters for the external clear signal. - `pcnt-enable-disable-unit` - describes how to enable and disable the PCNT unit. - `pcnt-unit-io-control` - describes IO control functions of PCNT unit, like enable glitch filter, start and stop unit, get and clear count value. - `pcnt-power-management` - describes what functionality will prevent the chip from going into low power mode. - `pcnt-iram-safe` - describes tips on how to make the PCNT interrupt and IO control functions work better along with a disabled cache. - `pcnt-thread-safe` - lists which APIs are guaranteed to be thread safe by the driver. - `pcnt-kconfig-options` - lists the supported Kconfig options that can be used to make a different effect on driver behavior.

### Resource Allocation

The PCNT unit and channel are represented by `pcnt_unit_handle_t` and `pcnt_channel_handle_t` respectively. All available units and channels are maintained by the driver in a resource pool, so you do not need to know the exact underlying instance ID.

#### Install PCNT Unit

To install a PCNT unit, there is a configuration structure that needs to be given in advance: `pcnt_unit_config_t`:

- `pcnt_unit_config_t::low_limit` and `pcnt_unit_config_t::high_limit` specify the range for the internal hardware counter. The counter will reset to zero automatically when it crosses either the high or low limit.

\- `pcnt_unit_config_t::accum_count` sets whether to create an internal accumulator for the counter. This is helpful when you want to extend the counter's width, which by default is 16 bit at most, defined in the hardware. See also `pcnt-compensate-overflow-loss` for how to use this feature to compensate the overflow loss. :SOC_PCNT_SUPPORT_STEP_NOTIFY: - `pcnt_unit_config_t::en_step_notify_up` Configure whether to enable watch step to count in the positive direction. :SOC_PCNT_SUPPORT_STEP_NOTIFY: - `pcnt_unit_config_t::en_step_notify_down` Configure whether to enable watch step to count in the negative direction. - `pcnt_unit_config_t::intr_priority` sets the priority of the interrupt. If it is set to `0`, the driver will allocate an interrupt with a default priority. Otherwise, the driver will use the given priority.

> **Note**
>
> Unit allocation and initialization is done by calling a function `pcnt_new_unit` with `pcnt_unit_config_t` as an input parameter. The function will return a PCNT unit handle only when it runs correctly. Specifically, when there are no more free PCNT units in the pool (i.e., unit resources have been used up), then this function will return `ESP_ERR_NOT_FOUND` error.

If a previously created PCNT unit is no longer needed, it is recommended to recycle the resource by calling `pcnt_del_unit`. Which in return allows the underlying unit hardware to be used for other purposes. Before deleting a PCNT unit, one should ensure the following prerequisites:

- The unit is in the init state, in other words, the unit is either disabled by `pcnt_unit_disable` or not enabled yet.
- The attached PCNT channels are all removed by `pcnt_del_channel`.

``` c
#define EXAMPLE_PCNT_HIGH_LIMIT 100
#define EXAMPLE_PCNT_LOW_LIMIT  -100

pcnt_unit_config_t unit_config = {
    .high_limit = EXAMPLE_PCNT_HIGH_LIMIT,
    .low_limit = EXAMPLE_PCNT_LOW_LIMIT,
};
pcnt_unit_handle_t pcnt_unit = NULL;
ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));
```

#### Install PCNT Channel

To install a PCNT channel, you must initialize a `pcnt_chan_config_t` structure in advance, and then call `pcnt_new_channel`. The configuration fields of the `pcnt_chan_config_t` structure are described below:

- `pcnt_chan_config_t::edge_gpio_num` and `pcnt_chan_config_t::level_gpio_num` specify the GPIO numbers used by **edge** type signal and **level** type signal. Please note, either of them can be assigned to `-1` if it is not actually used, and thus it will become a **virtual IO**. For some simple pulse counting applications where one of the level/edge signals is fixed (i.e., never changes), you can reclaim a GPIO by setting the signal as a virtual IO on channel allocation. Setting the level/edge signal as a virtual IO causes that signal to be internally routed to a fixed High/Low logic level, thus allowing you to save a GPIO for other purposes.
- `pcnt_chan_config_t::virt_edge_io_level` and `pcnt_chan_config_t::virt_level_io_level` specify the virtual IO level for **edge** and **level** input signal, to ensure a deterministic state for such control signal. Please note, they are only valid when either `pcnt_chan_config_t::edge_gpio_num` or `pcnt_chan_config_t::level_gpio_num` is assigned to `-1`.
- `pcnt_chan_config_t::invert_edge_input` and `pcnt_chan_config_t::invert_level_input` are used to decide whether to invert the input signals before they going into PCNT hardware. The invert is done by GPIO matrix instead of PCNT hardware.

Channel allocating and initialization is done by calling a function `pcnt_new_channel` with the above `pcnt_chan_config_t` as an input parameter plus a PCNT unit handle returned from `pcnt_new_unit`. This function will return a PCNT channel handle if it runs correctly. Specifically, when there are no more free PCNT channel within the unit (i.e., channel resources have been used up), then this function will return `ESP_ERR_NOT_FOUND` error. Note that, when install a PCNT channel for a specific unit, one should ensure the unit is in the init state, otherwise this function will return `ESP_ERR_INVALID_STATE` error.

If a previously created PCNT channel is no longer needed, it is recommended to recycle the resources by calling `pcnt_del_channel`. Which in return allows the underlying channel hardware to be used for other purposes.

``` c
#define EXAMPLE_CHAN_GPIO_A 0
#define EXAMPLE_CHAN_GPIO_B 2

pcnt_chan_config_t chan_config = {
    .edge_gpio_num = EXAMPLE_CHAN_GPIO_A,
    .level_gpio_num = EXAMPLE_CHAN_GPIO_B,
};
pcnt_channel_handle_t pcnt_chan = NULL;
ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_config, &pcnt_chan));
```

> **Note**
>
> ### Set Up Channel Actions

The PCNT will increase/decrease/hold its internal count value when the input pulse signal toggles. You can set different actions for edge signal and/or level signal.

- `pcnt_channel_set_edge_action` function is to set specific actions for rising and falling edge of the signal attached to the `pcnt_chan_config_t::edge_gpio_num`. Supported actions are listed in `pcnt_channel_edge_action_t`.
- `pcnt_channel_set_level_action` function is to set specific actions for high and low level of the signal attached to the `pcnt_chan_config_t::level_gpio_num`. Supported actions are listed in `pcnt_channel_level_action_t`. This function is not mandatory if the `pcnt_chan_config_t::level_gpio_num` is set to `-1` when allocating PCNT channel by `pcnt_new_channel`.

``` c
// decrease the counter on rising edge, increase the counter on falling edge
ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
// keep the counting mode when the control signal is high level, and reverse the counting mode when the control signal is low level
ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
```

### Watch Points

Each PCNT unit can be configured to watch several different values that you are interested in. The value to be watched is also called **Watch Point**. The watch point itself can not exceed the range set in `pcnt_unit_config_t` by `pcnt_unit_config_t::low_limit` and `pcnt_unit_config_t::high_limit`. When the counter reaches either watch point, a watch event will be triggered and notify you by interrupt if any watch event callback has ever registered in `pcnt_unit_register_event_callbacks`. See `pcnt-register-event-callbacks` for how to register event callbacks.

The watch point can be added and removed by `pcnt_unit_add_watch_point` and `pcnt_unit_remove_watch_point`. The commonly-used watch points are: **zero cross**, **maximum/minimum count** and other threshold values. The number of available watch point is limited, `pcnt_unit_add_watch_point` will return error `ESP_ERR_NOT_FOUND` if it can not find any free hardware resource to save the watch point. You can not add the same watch point for multiple times, otherwise it will return error `ESP_ERR_INVALID_STATE`.

It is recommended to remove the unused watch point by `pcnt_unit_remove_watch_point` to recycle the watch point resources.

``` c
// add zero across watch point
ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, 0));
// add high limit watch point
ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, EXAMPLE_PCNT_HIGH_LIMIT));
```

not SOC_PCNT_SUPPORT_RUNTIME_THRES_UPDATE

> **Note**
>
> </div>

SOC_PCNT_SUPPORT_STEP_NOTIFY

### Watch Step

PCNT unit can be configured to watch a specific value increment (can be positive or negative) that you are interested in. The function of watching value increment is also called **Watch Step**. To install watch step requires enabling `pcnt_unit_config_t::en_step_notify_up` or `pcnt_unit_config_t::en_step_notify_down`. The step interval itself can not exceed the range set in `pcnt_unit_config_t` by `pcnt_unit_config_t::low_limit` and `pcnt_unit_config_t::high_limit`.When the counter increment reaches step interval, a watch event will be triggered and notify you by interrupt if any watch event callback has ever registered in `pcnt_unit_register_event_callbacks`. See `pcnt-register-event-callbacks` for how to register event callbacks.

The watch step can be added and removed by `pcnt_unit_add_watch_step` and `pcnt_unit_remove_watch_step`. The parameter `step_interval` can be positive(step forward, e.g., \[N\]-\>\[N+1\]-\>\[N+2\]-\>...) or negative(step backward, e.g., \[N\]-\>\[N-1\]-\>\[N-2\]-\>...). The same direction can only add one watch step, otherwise it will return error `ESP_ERR_INVALID_STATE`.

> **Note**
>
> It is recommended to remove the unused watch step by `pcnt_unit_remove_watch_step` to recycle the watch step resources.

> **Note**
>
> ``` c
// add positive direction watch step with 100 step intervals
ESP_ERROR_CHECK(pcnt_unit_add_watch_step(pcnt_unit, 100));
```

not SOC_PCNT_SUPPORT_STEP_NOTIFY

### Register Event Callbacks

When PCNT unit reaches any enabled watch point, specific event will be generated and notify the CPU by interrupt. If you have some function that want to get executed when event happens, you should hook your function to the interrupt service routine by calling `pcnt_unit_register_event_callbacks`. All supported event callbacks are listed in the `pcnt_event_callbacks_t`:

- `pcnt_event_callbacks_t::on_reach` sets a callback function for watch point event. As this function is called within the ISR context, you must ensure that the function does not attempt to block (e.g., by making sure that only FreeRTOS APIs with `ISR` suffix are called from within the function). The function prototype is declared in `pcnt_watch_cb_t`.

You can save their own context to `pcnt_unit_register_event_callbacks` as well, via the parameter `user_ctx`. This user data will be directly passed to the callback functions.

In the callback function, the driver will fill in the event data of specific event. For example, the watch point event or watch step event data is declared as `pcnt_watch_event_data_t`:

- `pcnt_watch_event_data_t::watch_point_value` saves the count value when the event triggered.
- `pcnt_watch_event_data_t::zero_cross_mode` saves how the PCNT unit crosses the zero point in the latest time. The possible zero cross modes are listed in the `pcnt_unit_zero_cross_mode_t`. Usually different zero cross mode means different **counting direction** and **counting step size**.

Registering callback function results in lazy installation of interrupt service, thus this function should only be called before the unit is enabled by `pcnt_unit_enable`. Otherwise, it can return `ESP_ERR_INVALID_STATE` error.

``` c
static bool example_pcnt_on_reach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_wakeup;
    QueueHandle_t queue = (QueueHandle_t)user_ctx;
    // send watch point to queue, from this interrupt callback
    xQueueSendFromISR(queue, &(edata->watch_point_value), &high_task_wakeup);
    // return whether a high priority task has been waken up by this function
    return (high_task_wakeup == pdTRUE);
}

pcnt_event_callbacks_t cbs = {
    .on_reach = example_pcnt_on_reach,
};
QueueHandle_t queue = xQueueCreate(10, sizeof(int));
ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, queue));
```

### Set Glitch Filter

The PCNT unit features filters to ignore possible short glitches in the signals. The parameters that can be configured for the glitch filter are listed in `pcnt_glitch_filter_config_t`:

- `pcnt_glitch_filter_config_t::max_glitch_ns` sets the maximum glitch width, in nano seconds. If a signal pulse's width is smaller than this value, then it will be treated as noise and will not increase/decrease the internal counter.

You can enable the glitch filter for PCNT unit by calling `pcnt_unit_set_glitch_filter` with the filter configuration provided above. Particularly, you can disable the glitch filter later by calling `pcnt_unit_set_glitch_filter` with a `NULL` filter configuration.

This function should be called when the unit is in the init state. Otherwise, it will return `ESP_ERR_INVALID_STATE` error.

> **Note**
>
> ``` c
pcnt_glitch_filter_config_t filter_config = {
    .max_glitch_ns = 1000,
};
ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));
```

SOC_PCNT_SUPPORT_CLEAR_SIGNAL

### Use External Clear Signal

The PCNT unit can receive a clear signal from the GPIO. The parameters that can be configured for the clear signal are listed in `pcnt_clear_signal_config_t`:

> - `pcnt_clear_signal_config_t::clear_signal_gpio_num` specify the GPIO numbers used by **clear** signal. The default active level is high, and the input mode is pull-down enabled.
> - `pcnt_clear_signal_config_t::invert_clear_signal` is used to decide whether to invert the input signal before it going into PCNT hardware. The invert is done by GPIO matrix instead of PCNT hardware. The input mode is pull-up enabled when the input signal is inverted.

This signal acts in the same way as calling `pcnt_unit_clear_count`, but is not subject to software latency, and is suitable for use in situations with low latency requirements. Also please note, the flip frequency of this signal can not be too high.

``` c
pcnt_clear_signal_config_t clear_signal_config = {
    .clear_signal_gpio_num = PCNT_CLEAR_SIGNAL_GPIO,
};
ESP_ERROR_CHECK(pcnt_unit_set_clear_signal(pcnt_unit, &clear_signal_config));
```

not SOC_PCNT_SUPPORT_CLEAR_SIGNAL

### Enable and Disable Unit

Before doing IO control to the PCNT unit, you need to enable it first, by calling `pcnt_unit_enable`. Internally, this function:

- switches the PCNT driver state from **init** to **enable**.
- enables the interrupt service if it has been lazy installed in `pcnt_unit_register_event_callbacks`.
- acquires a proper power management lock if it has been installed. See also `pcnt-power-management` for more information.

On the contrary, calling `pcnt_unit_disable` will do the opposite, that is, put the PCNT driver back to the **init** state, disable the interrupts service and release the power management lock.

``` c
ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
```

### Unit IO Control

#### Start/Stop and Clear

Calling `pcnt_unit_start` makes the PCNT unit start to work, increase or decrease counter according to pulse signals. On the contrary, calling `pcnt_unit_stop` will stop the PCNT unit but retain current count value. Instead, clearing counter can only be done by calling `pcnt_unit_clear_count`.

Note, `pcnt_unit_start` and `pcnt_unit_stop` should be called when the unit has been enabled by `pcnt_unit_enable`. Otherwise, it will return `ESP_ERR_INVALID_STATE` error.

``` c
ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
```

#### Get Count Value

You can read current count value at any time by calling `pcnt_unit_get_count`. The returned count value is a **signed** integer, where the sign can be used to reflect the direction.

``` c
int pulse_count = 0;
ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
```

#### Compensate Overflow Loss

The internal hardware counter will be cleared to zero automatically when it reaches high or low limit. If you want to compensate for that count loss and extend the counter's bit-width, you can:

1.  Enable `pcnt_unit_config_t::accum_count` when installing the PCNT unit.
2.  Add the high/low limit as the `pcnt-watch-points`.
3.  Now, the returned count value from the `pcnt_unit_get_count` function not only reflects the hardware's count value, but also accumulates the high/low overflow loss to it.

> **Note**
>
> > **Note**
>
> ### Power Management

When power management is enabled (i.e., `CONFIG_PM_ENABLE` is on), the system adjusts the APB frequency before entering light sleep, which can cause the PCNT glitch filter to misinterpret valid signals as noise.

To prevent this, the driver can acquire a power management lock of type `ESP_PM_APB_FREQ_MAX`, ensuring the APB frequency remains constant. This lock is acquired when the PCNT unit is enabled via `pcnt_unit_enable` and released when the unit is disabled via `pcnt_unit_disable`.

### IRAM Safe

By default, the PCNT interrupt will be deferred when the Cache is disabled for reasons like writing/erasing Flash. Thus the alarm interrupt will not get executed in time, which is not expected in a real-time application.

There is a Kconfig option `CONFIG_PCNT_ISR_IRAM_SAFE` that:

1.  Enables the interrupt being serviced even when cache is disabled
2.  Places all functions that used by the ISR into IRAM[^2]
3.  Places driver object into DRAM (in case it is mapped to PSRAM by accident)

This allows the interrupt to run while the cache is disabled but comes at the cost of increased IRAM consumption.

There is another Kconfig option `CONFIG_PCNT_CTRL_FUNC_IN_IRAM` that can put commonly used IO control functions into IRAM as well. So that these functions can also be executable when the cache is disabled. These IO control functions are as follows:

- `pcnt_unit_start`
- `pcnt_unit_stop`
- `pcnt_unit_clear_count`
- `pcnt_unit_get_count`

### Thread Safety

The factory functions `pcnt_new_unit` and `pcnt_new_channel` are guaranteed to be thread safe by the driver, which means, you can call them from different RTOS tasks without protection by extra locks.

The following functions are allowed to run under ISR context, the driver uses a critical section to prevent them being called concurrently in both task and ISR.

- `pcnt_unit_start`
- `pcnt_unit_stop`
- `pcnt_unit_clear_count`
- `pcnt_unit_get_count`

Other functions that take the `pcnt_unit_handle_t` and `pcnt_channel_handle_t` as the first positional parameter, are not treated as thread safe. This means you should avoid calling them from multiple tasks.

### Kconfig Options

- `CONFIG_PCNT_CTRL_FUNC_IN_IRAM` controls where to place the PCNT control functions (IRAM or Flash), see `pcnt-iram-safe` for more information.
- `CONFIG_PCNT_ISR_IRAM_SAFE` controls whether the default ISR handler can work when cache is disabled, see `pcnt-iram-safe` for more information.
- `CONFIG_PCNT_ENABLE_DEBUG_LOG` is used to enabled the debug log output. Enabling this option increases the firmware binary size.

## Application Examples

- `peripherals/pcnt/rotary_encoder` demonstrates how to use the PCNT peripheral to decode the differential signals generated from a common rotary encoder, EC11, and how to configure the rotary encoder to wake the system from light-sleep.

## API Reference

inc/pulse_cnt.inc

inc/pcnt_types.inc

[^1]: Different ESP chip series might have different number of PCNT units and channels. Please refer to the \[[TRM](%7BIDF_TARGET_TRM_EN_URL%7D#pcnt)\] for details. The driver does not forbid you from applying for more PCNT units and channels, but it returns error when all available hardware resources are used up. Please always check the return value when doing resource allocation (e.g., `pcnt_new_unit`).

[^2]: `pcnt_event_callbacks_t::on_reach` callback and the functions invoked by itself should also be placed in IRAM, you need to take care of them by themselves.
