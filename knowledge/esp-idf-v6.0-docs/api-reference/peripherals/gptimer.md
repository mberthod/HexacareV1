<!-- Source: _sources/api-reference/peripherals/gptimer.rst.txt (ESP-IDF v6.0 documentation) -->

# General Purpose Timer (GPTimer)

This document introduces the features of the General Purpose Timer (GPTimer) driver in ESP-IDF. The table of contents is as follows:

## Overview

GPTimer is a dedicated driver for the {IDF_TARGET_NAME} \[[Timer Group peripheral](%7BIDF_TARGET_TRM_EN_URL%7D#timg)\]. This timer can select different clock sources and prescalers to meet the requirements of nanosecond-level resolution. Additionally, it has flexible timeout alarm functions and allows automatic updating of the count value at the alarm moment, achieving very precise timing cycles.

Based on the **high resolution, high count range, and high response** capabilities of the hardware timer, the main application scenarios of this driver include:

- Running freely as a calendar clock to provide timestamp services for other modules
- Generating periodic alarms to complete periodic tasks
- Generating one-shot alarms, which can be used to implement a monotonic software timer list with asynchronous updates of alarm values
- Working with the GPIO module to achieve PWM signal output and input capture
- etc.

## Quick Start

This section provides a concise overview of how to use the GPTimer driver. Through practical examples, it demonstrates how to initialize and start a timer, configure alarm events, and register callback functions. The typical usage flow is as follows:

blockdiag {  
default_fontsize = 14; node_width = 250; node_height = 80; class emphasis \[color = pink, style = dashed\];

create \[label="gptimer_new_timer"\]; config \[label="gptimer_set_alarm_action n gptimer_register_event_callbacks"\]; enable \[label="gptimer_enable"\]; start \[label="gptimer_start"\]; running \[label="Timer Running", class="emphasis"\] stop \[label="gptimer_stop"\]; disable \[label="gptimer_disable"\]; cleanup \[label="gptimer_delete_timer"\];

create -\> config -\> enable -\> start -\> running -\> stop -\> disable -\> cleanup; enable -\> start \[folded\]; stop -\> disable \[folded\];

}

### Creating and Starting a Timer

First, we need to create a timer instance. The following code shows how to create a timer with a resolution of 1 MHz:

``` c
gptimer_handle_t gptimer = NULL;
gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT, // Select the default clock source
    .direction = GPTIMER_COUNT_UP,      // Counting direction is up
    .resolution_hz = 1 * 1000 * 1000,   // Resolution is 1 MHz, i.e., 1 tick equals 1 microsecond
};
// Create a timer instance
ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));
// Enable the timer
ESP_ERROR_CHECK(gptimer_enable(gptimer));
// Start the timer
ESP_ERROR_CHECK(gptimer_start(gptimer));
```

When creating a timer instance, we need to configure parameters such as the clock source, counting direction, and resolution through `gptimer_config_t`. These parameters determine how the timer works. Then, call the `gptimer_new_timer` function to create a new timer instance, which returns a handle pointing to the new instance. The timer handle is essentially a pointer to the timer memory object, of type `gptimer_handle_t`.

Here are the other configuration parameters of the `gptimer_config_t` structure and their explanations:

- `gptimer_config_t::clk_src` selects the clock source for the timer. Available clock sources are listed in `gptimer_clock_source_t`, and only one can be selected. Different clock sources vary in resolution, accuracy, and power consumption.

- `gptimer_config_t::direction` sets the counting direction of the timer. Supported directions are listed in `gptimer_count_direction_t`, and only one can be selected.

- `gptimer_config_t::resolution_hz` sets the resolution of the internal counter. Each tick is equivalent to **1 / resolution_hz** seconds.

- `gptimer_config_t::intr_priority` sets the interrupt priority. If set to `0`, a default priority interrupt will be allocated; otherwise, the specified priority will be used.

- `gptimer_config_t::flags` is used to fine-tune some behaviors of the driver, including the following options:

  > - `gptimer_config_t::flags::allow_pd` configures whether the driver allows the system to power down the peripheral in sleep mode. Before entering sleep, the system will back up the GPTimer register context, which will be restored when the system wakes up. Note that powering down the peripheral can save power but will consume more memory to save the register context. You need to balance power consumption and memory usage. This configuration option depends on specific hardware features. If enabled on an unsupported chip, you will see an error message like `not able to power down in light sleep`.

> **Note**
>
> Before starting the timer, it must be enabled. The enable function `gptimer_enable` can switch the internal state machine of the driver to the active state, which includes some system service requests/registrations, such as applying for a power management lock. The corresponding disable function is `gptimer_disable`, which releases all system services.

> **Note**
>
> The `gptimer_start` function is used to start the timer. After starting, the timer will begin counting and will automatically overflow and restart from 0 when it reaches the maximum or minimum value (depending on the counting direction). The `gptimer_stop` function is used to stop the timer. Note that stopping a timer does not clear the current value of the counter. To clear the counter, use the `gptimer_set_raw_count` function introduced later. The `gptimer_start` and `gptimer_stop` functions follow the idempotent principle. This means that if the timer is already started, calling the `gptimer_start` function again will have no effect. Similarly, if the timer is already stopped, calling the `gptimer_stop` function again will have no effect.

> **Note**
>
> ### Setting and Getting the Count Value

When a timer is newly created, its internal counter value defaults to zero. You can set other count values using the `gptimer_set_raw_count` function. The maximum count value depends on the bit width of the hardware timer (usually no less than `54 bits`).

> **Note**
>
> The `gptimer_get_raw_count` function is used to get the current count value of the timer. This count value is the accumulated count since the timer started (assuming it started from 0). Note that the returned value has not been converted to any unit; it is a pure count value. You need to convert the count value to time units based on the actual resolution of the timer. The timer's resolution can be obtained using the `gptimer_get_resolution` function.

``` c
// Check the timer's resolution
uint32_t resolution_hz;
ESP_ERROR_CHECK(gptimer_get_resolution(gptimer, &resolution_hz));
// Read the current count value
uint64_t count;
ESP_ERROR_CHECK(gptimer_get_raw_count(gptimer, &count));
// (Optional) Convert the count value to time units (seconds)
double time = (double)count / resolution_hz;
```

### Triggering Periodic Alarm Events

In addition to the timestamp function, the general-purpose timer also supports alarm functions. The following code shows how to set a periodic alarm that triggers once per second:

``` c
gptimer_handle_t gptimer = NULL;
gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT, // Select the default clock source
    .direction = GPTIMER_COUNT_UP,      // Counting direction is up
    .resolution_hz = 1 * 1000 * 1000,   // Resolution is 1 MHz, i.e., 1 tick equals 1 microsecond
};
// Create a timer instance
ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

static bool example_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    // General process for handling event callbacks:
    // 1. Retrieve user context data from user_ctx (passed in from gptimer_register_event_callbacks)
    // 2. Get alarm event data from edata, such as edata->count_value
    // 3. Perform user-defined operations
    // 4. Return whether a high-priority task was awakened during the above operations to notify the scheduler to switch tasks
    return false;
}

gptimer_alarm_config_t alarm_config = {
    .reload_count = 0,      // When the alarm event occurs, the timer will automatically reload to 0
    .alarm_count = 1000000, // Set the actual alarm period, since the resolution is 1us, 1000000 represents 1s
    .flags.auto_reload_on_alarm = true, // Enable auto-reload function
};
// Set the timer's alarm action
ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

gptimer_event_callbacks_t cbs = {
    .on_alarm = example_timer_on_alarm_cb, // Call the user callback function when the alarm event occurs
};
// Register timer event callback functions, allowing user context to be carried
ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
// Enable the timer
ESP_ERROR_CHECK(gptimer_enable(gptimer));
// Start the timer
ESP_ERROR_CHECK(gptimer_start(gptimer));
```

The `gptimer_set_alarm_action` function is used to configure the timer's alarm action. When the timer count value reaches the specified alarm value, an alarm event will be triggered. Users can choose to automatically reload the preset count value when the alarm event occurs, thereby achieving periodic alarms.

Here are the necessary members of the `gptimer_alarm_config_t` structure and their functions. By configuring these parameters, users can flexibly control the timer's alarm behavior to meet different application needs.

- `gptimer_alarm_config_t::alarm_count` sets the target count value that triggers the alarm event. When the timer count value reaches this value, an alarm event will be triggered. When setting the alarm value, consider the counting direction of the timer. If the current count value has **exceeded** the alarm value, the alarm event will be triggered immediately.
- `gptimer_alarm_config_t::reload_count` sets the count value to be reloaded when the alarm event occurs. This configuration only takes effect when the `gptimer_alarm_config_t::flags::auto_reload_on_alarm` flag is `true`. The actual alarm period will be determined by `|alarm_count - reload_count|`. From a practical application perspective, it is not recommended to set the alarm period to less than 5us.

> **Note**
>
> The `gptimer_register_event_callbacks` function is used to register the timer event callback functions. When the timer triggers a specific event (such as an alarm event), the user-defined callback function will be called. Users can perform custom operations in the callback function, such as sending signals, to achieve more flexible event handling mechanisms. Since the callback function is executed in the interrupt context, avoid performing complex operations (including any operations that may cause blocking) in the callback function to avoid affecting the system's real-time performance. The `gptimer_register_event_callbacks` function also allows users to pass a context pointer to access user-defined data in the callback function.

The supported event callback functions for GPTimer are as follows:

- `gptimer_alarm_cb_t` alarm event callback function, which has a corresponding data structure `gptimer_alarm_event_data_t` for passing alarm event-related data:
  - `gptimer_alarm_event_data_t::alarm_value` stores the alarm value, which is the target count value that triggers the alarm event.
  - `gptimer_alarm_event_data_t::count_value` stores the count value when entering the interrupt handler after the alarm occurs. This value may differ from the alarm value due to interrupt handler delays, and the count value may have been automatically reloaded when the alarm occurred.

> **Note**
>
> ### Triggering One-Shot Alarm Events

Some application scenarios only require triggering a one-shot alarm interrupt. The following code shows how to set a one-shot alarm that triggers after 1 second:

``` c
gptimer_handle_t gptimer = NULL;
gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT, // Select the default clock source
    .direction = GPTIMER_COUNT_UP,      // Counting direction is up
    .resolution_hz = 1 * 1000 * 1000,   // Resolution is 1 MHz, i.e., 1 tick equals 1 microsecond
};
// Create a timer instance
ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

static bool example_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    // This is just a demonstration of how to stop the timer when the alarm occurs for the first time
    gptimer_stop(timer);
    // General process for handling event callbacks:
    // 1. Retrieve user context data from user_ctx (passed in from gptimer_register_event_callbacks)
    // 2. Get alarm event data from edata, such as edata->count_value
    // 3. Perform user-defined operations
    // 4. Return whether a high-priority task was awakened during the above operations to notify the scheduler to switch tasks
    return false;
}

gptimer_alarm_config_t alarm_config = {
    .alarm_count = 1000000, // Set the actual alarm period, since the resolution is 1us, 1000000 represents 1s
    .flags.auto_reload_on_alarm = false; // Disable auto-reload function
};
// Set the timer's alarm action
ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

gptimer_event_callbacks_t cbs = {
    .on_alarm = example_timer_on_alarm_cb, // Call the user callback function when the alarm event occurs
};
// Register timer event callback functions, allowing user context to be carried
ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
// Enable the timer
ESP_ERROR_CHECK(gptimer_enable(gptimer));
// Start the timer
ESP_ERROR_CHECK(gptimer_start(gptimer));
```

Unlike periodic alarms, the above code disables the auto-reload function when configuring the alarm behavior. This means that after the alarm event occurs, the timer will not automatically reload to the preset count value but will continue counting until it overflows. If you want the timer to stop immediately after the alarm, you can call `gptimer_stop` in the callback function.

### Resource Recycling

When the timer is no longer needed, you should call the `gptimer_delete_timer` function to release software and hardware resources. Before deleting, ensure that the timer is already stopped.

## Advanced Features

After understanding the basic usage, we can further explore more features of the GPTimer driver.

### Dynamic Alarm Value Update

The GPTimer driver supports dynamically updating the alarm value in the interrupt callback function by calling the `gptimer_set_alarm_action` function, thereby implementing a monotonic software timer list. The following code shows how to reset the next alarm trigger time when the alarm event occurs:

``` c
gptimer_handle_t gptimer = NULL;
gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT, // Select the default clock source
    .direction = GPTIMER_COUNT_UP,      // Counting direction is up
    .resolution_hz = 1 * 1000 * 1000,   // Resolution is 1 MHz, i.e., 1 tick equals 1 microsecond
};
// Create a timer instance
ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

static bool example_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = edata->alarm_value + 1000000, // Next alarm in 1s from the current alarm
    };
    // Update the alarm value
    gptimer_set_alarm_action(timer, &alarm_config);
    return false;
}

gptimer_alarm_config_t alarm_config = {
    .alarm_count = 1000000, // Set the actual alarm period, since the resolution is 1us, 1000000 represents 1s
    .flags.auto_reload_on_alarm = false, // Disable auto-reload function
};
// Set the timer's alarm action
ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

gptimer_event_callbacks_t cbs = {
    .on_alarm = example_timer_on_alarm_cb, // Call the user callback function when the alarm event occurs
};
// Register timer event callback functions, allowing user context to be carried
ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
// Enable the timer
ESP_ERROR_CHECK(gptimer_enable(gptimer));
// Start the timer
ESP_ERROR_CHECK(gptimer_start(gptimer));
```

SOC_TIMER_SUPPORT_ETM

### GPTimer's ETM Events and Tasks

GPTimer can generate various events that can be connected to the `ETM </api-reference/peripherals/etm>` module. The event types are listed in `gptimer_etm_event_type_t`. Users can create an `ETM event` handle by calling `gptimer_new_etm_event`. GPTimer also supports some tasks that can be triggered by other events and executed automatically. The task types are listed in `gptimer_etm_task_type_t`. Users can create an `ETM task` handle by calling `gptimer_new_etm_task`.

For how to connect the timer events and tasks to the ETM channel, please refer to the `ETM </api-reference/peripherals/etm>` documentation.

### Power Management

When power management `CONFIG_PM_ENABLE` is enabled, the system may adjust or disable the clock source before entering sleep mode, causing the GPTimer to lose accuracy.

To prevent this, the GPTimer driver creates a power management lock internally. When the `gptimer_enable` function is called, the lock is activated to ensure the system does not enter sleep mode, thus maintaining the timer's accuracy. To reduce power consumption, you can call the `gptimer_disable` function to release the power management lock, allowing the system to enter sleep mode. However, this will stop the timer, so you need to restart the timer after waking up.

SOC_TIMER_SUPPORT_SLEEP_RETENTION

Besides disabling the clock source, the system can also power down the GPTimer before entering sleep mode to further reduce power consumption. To achieve this, set `gptimer_config_t::allow_pd` to `true`. Before the system enters sleep mode, the GPTimer register context will be backed up to memory and restored after the system wakes up. Note that enabling this option reduces power consumption but increases memory usage. Therefore, you need to balance power consumption and memory usage when using this feature.

### Thread Safety

The driver uses critical sections to ensure atomic operations on registers. Key members in the driver handle are also protected by critical sections. The driver's internal state machine uses atomic instructions to ensure thread safety, with state checks preventing certain invalid concurrent operations (e.g., conflicts between <span class="title-ref">start</span> and <span class="title-ref">stop</span>). Therefore, GPTimer driver APIs can be used in a multi-threaded environment without extra locking.

The following functions can also be used in an interrupt context:

- `gptimer_start`
- `gptimer_stop`
- `gptimer_get_raw_count`
- `gptimer_set_raw_count`
- `gptimer_get_captured_count`
- `gptimer_set_alarm_action`

### Cache Safety

When the file system performs Flash read/write operations, the system temporarily disables the Cache function to avoid errors when loading instructions and data from Flash. This causes the GPTimer interrupt handler to be unresponsive during this period, preventing the user callback function from executing in time. If you want the interrupt handler to run normally when the Cache is disabled, you can enable the `CONFIG_GPTIMER_ISR_CACHE_SAFE` option.

> **Note**
>
> ### Performance

To improve the real-time responsiveness of interrupt handling, the GPTimer driver provides the `CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM` option. Once enabled, the interrupt handler is placed in internal RAM, reducing delays caused by potential cache misses when loading instructions from Flash.

> **Note**
>
> As mentioned above, the GPTimer driver allows some functions to be called in an interrupt context. By enabling the `CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM` option, these functions can also be placed in IRAM, which helps avoid performance loss caused by cache misses and allows them to be used when the Cache is disabled.

### Other Kconfig Options

- The `CONFIG_GPTIMER_ENABLE_DEBUG_LOG` option forces the GPTimer driver to enable all debug logs, regardless of the global log level settings. Enabling this option helps developers obtain more detailed log information during debugging, making it easier to locate and solve problems.

### Resource Consumption

Use the `/api-guides/tools/idf-size` tool to check the code and data consumption of the GPTimer driver. The following are the test conditions (using ESP32-C2 as an example):

- Compiler optimization level set to `-Os` to ensure minimal code size.

- Default log level set to `ESP_LOG_INFO` to balance debug information and performance.

- Disable the following driver optimization options:  
  - `CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM` - Do not place the interrupt handler in IRAM.
  - `CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM` - Do not place control functions in IRAM.
  - `CONFIG_GPTIMER_ISR_CACHE_SAFE` - Do not enable Cache safety options.

**Note that the following data are not exact values and are for reference only; they may differ on different chip models.**

<table style="width:93%;">
<colgroup>
<col style="width: 16%" />
<col style="width: 11%" />
<col style="width: 6%" />
<col style="width: 6%" />
<col style="width: 6%" />
<col style="width: 6%" />
<col style="width: 11%" />
<col style="width: 6%" />
<col style="width: 11%" />
<col style="width: 8%" />
</colgroup>
<thead>
<tr class="header">
<th>Component Layer</th>
<th>Total Size</th>
<th>DIRAM</th>
<th>.bss</th>
<th>.data</th>
<th>.text</th>
<th>Flash Code</th>
<th>.text</th>
<th>Flash Data</th>
<th>.rodata</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>soc</td>
<td><blockquote>
<p>8</p>
</blockquote></td>
<td><blockquote>
<p>0</p>
</blockquote></td>
<td><blockquote>
<p>0</p>
</blockquote></td>
<td><blockquote>
<p>0</p>
</blockquote></td>
<td><blockquote>
<p>0</p>
</blockquote></td>
<td><blockquote>
<p>0</p>
</blockquote></td>
<td><blockquote>
<p>0</p>
</blockquote></td>
<td><blockquote>
<p>8</p>
</blockquote></td>
<td><blockquote>
<p>8</p>
</blockquote></td>
</tr>
<tr class="even">
<td>hal</td>
<td><blockquote>
<p>206</p>
</blockquote></td>
<td><blockquote>
<p>0</p>
</blockquote></td>
<td><blockquote>
<p>0</p>
</blockquote></td>
<td><blockquote>
<p>0</p>
</blockquote></td>
<td><blockquote>
<p>0</p>
</blockquote></td>
<td><blockquote>
<p>206</p>
</blockquote></td>
<td><blockquote>
<p>206</p>
</blockquote></td>
<td><blockquote>
<p>0</p>
</blockquote></td>
<td><blockquote>
<p>0</p>
</blockquote></td>
</tr>
<tr class="odd">
<td>driver</td>
<td><blockquote>
<p>4251</p>
</blockquote></td>
<td><blockquote>
<p>12</p>
</blockquote></td>
<td><blockquote>
<p>12</p>
</blockquote></td>
<td><blockquote>
<p>0</p>
</blockquote></td>
<td><blockquote>
<p>0</p>
</blockquote></td>
<td><blockquote>
<p>4046</p>
</blockquote></td>
<td><blockquote>
<p>4046</p>
</blockquote></td>
<td><blockquote>
<p>193</p>
</blockquote></td>
<td><blockquote>
<p>193</p>
</blockquote></td>
</tr>
</tbody>
</table>

Additionally, each GPTimer handle dynamically allocates about `100` bytes of memory from the heap. If the `gptimer_config_t::flags::allow_pd` option is enabled, each timer will also consume approximately `30` extra bytes of memory during sleep to store the register context.

## Application Examples

- `peripherals/timer_group/gptimer` demonstrates how to use the general-purpose timer APIs on ESP SOC chips to generate periodic alarm events and trigger different alarm actions.

\- `peripherals/timer_group/wiegand_interface` uses two timers (one in one-shot alarm mode and the other in periodic alarm mode) to trigger interrupts and change the GPIO output state in the alarm event callback function, simulating the output waveform of the Wiegand protocol. :SOC_TIMER_SUPPORT_ETM: - `peripherals/timer_group/gptimer_capture_hc_sr04` demonstrates how to use the general-purpose timer and Event Task Matrix (ETM) to accurately capture timestamps of ultrasonic sensor events and convert them into distance information.

## API Reference

### GPTimer Driver APIs

inc/gptimer.inc

### GPTimer Driver Types

inc/gptimer_types.inc

### GPTimer HAL Types

inc/timer_types.inc

### GPTimer ETM APIs

SOC_TIMER_SUPPORT_ETM

inc/gptimer_etm.inc

</div>
