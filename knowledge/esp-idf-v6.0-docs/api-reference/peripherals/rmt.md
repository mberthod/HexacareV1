<!-- Source: _sources/api-reference/peripherals/rmt.rst.txt (ESP-IDF v6.0 documentation) -->

# Remote Control Transceiver (RMT)

## Introduction

The RMT (Remote Control Transceiver) peripheral was designed to act as an infrared transceiver. However, due to the flexibility of its data format, RMT can be extended to a versatile and general-purpose transceiver, transmitting or receiving many other types of signals. From the perspective of network layering, the RMT hardware contains both physical and data link layers. The physical layer defines the communication media and bit signal representation. The data link layer defines the format of an RMT frame. The minimal data unit in the frame is called the **RMT symbol**, which is represented by `rmt_symbol_word_t` in the driver.

{IDF_TARGET_NAME} contains multiple channels in the RMT peripheral[^1]. Each channel can be independently configured as either transmitter or receiver.

Typically, the RMT peripheral can be used in the following scenarios:

- Transmit or receive infrared signals, with any IR protocols, e.g., NEC
- General-purpose sequence generator
- Transmit signals in a hardware-controlled loop, with a finite or infinite number of times
- Multi-channel simultaneous transmission
- Modulate the carrier to the output signal or demodulate the carrier from the input signal

### Layout of RMT Symbols

The RMT hardware defines data in its own pattern -- the **RMT symbol**. The diagram below illustrates the bit fields of an RMT symbol. Each symbol consists of two pairs of two values. The first value in the pair is a 15-bit value representing the signal's duration in units of RMT ticks. The second in the pair is a 1-bit value representing the signal's logic level, i.e., high or low.

/../\_static/diagrams/rmt/rmt_symbols.diag

### RMT Transmitter Overview

The data path and control path of an RMT TX channel is illustrated in the figure below:

/../\_static/diagrams/rmt/rmt_tx.diag

The driver encodes the user's data into RMT data format, then the RMT transmitter can generate the waveforms according to the encoding artifacts. It is also possible to modulate a high-frequency carrier signal before being routed to a GPIO pad.

### RMT Receiver Overview

The data path and control path of an RMT RX channel is illustrated in the figure below:

/../\_static/diagrams/rmt/rmt_rx.diag

The RMT receiver can sample incoming signals into RMT data format, and store the data in memory. It is also possible to tell the receiver the basic characteristics of the incoming signal, so that the signal's stop condition can be recognized, and signal glitches and noise can be filtered out. The RMT peripheral also supports demodulating the high-frequency carrier from the base signal.

## Functional Overview

The description of the RMT functionality is divided into the following sections:

- `rmt-resource-allocation` - covers how to allocate and properly configure RMT channels. It also covers how to recycle channels and other resources when they are no longer used.
- `rmt-carrier-modulation-and demodulation` - describes how to modulate and demodulate the carrier signals for TX and RX channels respectively.
- `rmt-register-event-callbacks` - covers how to register user-provided event callbacks to receive RMT channel events.
- `rmt-enable-and-disable-channel` - shows how to enable and disable the RMT channel.
- `rmt-initiate-tx-transaction` - describes the steps to initiate a transaction for a TX channel.
- `rmt-initiate-rx-transaction` - describes the steps to initiate a transaction for an RX channel.
- `rmt-multiple-channels-simultaneous-transmission` - describes how to collect multiple channels into a sync group so that their transmissions can be started simultaneously.
- `rmt-rmt-encoder` - focuses on how to write a customized encoder by combining multiple primitive encoders that are provided by the driver.
- `rmt-power-management` - describes how different clock sources affects power consumption.
- `rmt-cache-safe` - describes how disabling the cache affects the RMT driver, and tips to mitigate it.
- `rmt-thread-safety` - lists which APIs are guaranteed to be thread-safe by the driver.
- `rmt-kconfig-options` - describes the various Kconfig options supported by the RMT driver.

### Resource Allocation

Both RMT TX and RX channels are represented by `rmt_channel_handle_t` in the driver. The driver internally manages which channels are available and hands out a free channel on request.

#### Install RMT TX Channel

To install an RMT TX channel, there is a configuration structure that needs to be given in advance `rmt_tx_channel_config_t`. The following list describes each member of the configuration structure.

- `rmt_tx_channel_config_t::gpio_num` sets the GPIO number used by the transmitter.

- `rmt_tx_channel_config_t::clk_src` selects the source clock for the RMT channel. The available clocks are listed in `rmt_clock_source_t`. Note that, the selected clock is also used by other channels, which means the user should ensure this configuration is the same when allocating other channels, regardless of TX or RX. For the effect on the power consumption of different clock sources, please refer to the `rmt-power-management` section.

- `rmt_tx_channel_config_t::resolution_hz` sets the resolution of the internal tick counter. The timing parameter of the RMT signal is calculated based on this **tick**.

- `rmt_tx_channel_config_t::mem_block_symbols` has a slightly different meaning based on if the DMA backend is enabled or not.

  > - If the DMA is enabled via `rmt_tx_channel_config_t::with_dma`, then this field controls the size of the internal DMA buffer. To achieve a better throughput and smaller CPU overhead, you can set a larger value, e.g., `1024`.
  > - If DMA is not used, this field controls the size of the dedicated memory block owned by the channel, which should be at least {IDF_TARGET_SOC_RMT_MEM_WORDS_PER_CHANNEL}.

- `rmt_tx_channel_config_t::trans_queue_depth` sets the depth of the internal transaction queue, the deeper the queue, the more transactions can be prepared in the backlog.

- `rmt_tx_channel_config_t::invert_out` is used to decide whether to invert the RMT signal before sending it to the GPIO pad.

- `rmt_tx_channel_config_t::with_dma` enables the DMA backend for the channel. Using the DMA allows a significant amount of the channel's workload to be offloaded from the CPU. However, the DMA backend is not available on all ESP chips, please refer to \[[TRM](%7BIDF_TARGET_TRM_EN_URL%7D#rmt)\] before you enable this option. Or you might encounter a `ESP_ERR_NOT_SUPPORTED` error.

- `rmt_tx_channel_config_t::intr_priority` Set the priority of the interrupt. If set to `0` , then the driver will use a interrupt with low or medium priority (priority level may be one of 1,2 or 3), otherwise use the priority indicated by `rmt_tx_channel_config_t::intr_priority`. Please use the number form (1,2,3) , not the bitmask form ((1\<\<1),(1\<\<2),(1\<\<3)). Please pay attention that once the interrupt priority is set, it cannot be changed until `rmt_del_channel` is called.

- `rmt_tx_channel_config_t::allow_pd` configures if the driver allows the system to power down the peripheral in light sleep mode. Before entering sleep, the system will backup the RMT register context, which will be restored later when the system exit the sleep mode. Powering down the peripheral can save more power, but at the cost of more memory consumed to save the register context. It's a tradeoff between power consumption and memory consumption. This configuration option relies on specific hardware feature, if you enable it on an unsupported chip, you will see error message like `not able to power down in light sleep`.

Once the `rmt_tx_channel_config_t` structure is populated with mandatory parameters, users can call `rmt_new_tx_channel` to allocate and initialize a TX channel. This function returns an RMT channel handle if it runs correctly. Specifically, when there are no more free channels in the RMT resource pool, this function returns `ESP_ERR_NOT_FOUND` error. If some feature (e.g., DMA backend) is not supported by the hardware, it returns `ESP_ERR_NOT_SUPPORTED` error.

``` c
rmt_channel_handle_t tx_chan = NULL;
rmt_tx_channel_config_t tx_chan_config = {
    .clk_src = RMT_CLK_SRC_DEFAULT,   // select source clock
    .gpio_num = 0,                    // GPIO number
    .mem_block_symbols = 64,          // memory block size, 64 * 4 = 256 Bytes
    .resolution_hz = 1 * 1000 * 1000, // 1 MHz tick resolution, i.e., 1 tick = 1 µs
    .trans_queue_depth = 4,           // set the number of transactions that can pend in the background
    .flags.invert_out = false,        // do not invert output signal
    .flags.with_dma = false,          // do not need DMA backend
};
ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &tx_chan));
```

#### Install RMT RX Channel

To install an RMT RX channel, there is a configuration structure that needs to be given in advance `rmt_rx_channel_config_t`. The following list describes each member of the configuration structure.

- `rmt_rx_channel_config_t::gpio_num` sets the GPIO number used by the receiver.

- `rmt_rx_channel_config_t::clk_src` selects the source clock for the RMT channel. The available clocks are listed in `rmt_clock_source_t`. Note that, the selected clock is also used by other channels, which means the user should ensure this configuration is the same when allocating other channels, regardless of TX or RX. For the effect on the power consumption of different clock sources, please refer to the `rmt-power-management` section.

- `rmt_rx_channel_config_t::resolution_hz` sets the resolution of the internal tick counter. The timing parameter of the RMT signal is calculated based on this **tick**.

- `rmt_rx_channel_config_t::mem_block_symbols` has a slightly different meaning based on whether the DMA backend is enabled.

  > - If the DMA is enabled via `rmt_rx_channel_config_t::with_dma`, this field controls the maximum size of the DMA buffer.
  > - If DMA is not used, this field controls the size of the dedicated memory block owned by the channel, which should be at least {IDF_TARGET_SOC_RMT_MEM_WORDS_PER_CHANNEL}.

- `rmt_rx_channel_config_t::invert_in` is used to invert the input signals before it is passed to the RMT receiver. The inversion is done by the GPIO matrix instead of by the RMT peripheral.

- `rmt_rx_channel_config_t::with_dma` enables the DMA backend for the channel. Using the DMA allows a significant amount of the channel's workload to be offloaded from the CPU. However, the DMA backend is not available on all ESP chips, please refer to \[[TRM](%7BIDF_TARGET_TRM_EN_URL%7D#rmt)\] before you enable this option. Or you might encounter a `ESP_ERR_NOT_SUPPORTED` error.

- `rmt_rx_channel_config_t::intr_priority` Set the priority of the interrupt. If set to `0` , then the driver will use a interrupt with low or medium priority (priority level may be one of 1,2 or 3), otherwise use the priority indicated by `rmt_rx_channel_config_t::intr_priority`. Please use the number form (1,2,3) , not the bitmask form ((1\<\<1),(1\<\<2),(1\<\<3)). Please pay attention that once the interrupt priority is set, it cannot be changed until `rmt_del_channel` is called.

- `rmt_rx_channel_config_t::allow_pd` configures if the driver allows the system to power down the peripheral in light sleep mode. Before entering sleep, the system will backup the RMT register context, which will be restored later when the system exit the sleep mode. Powering down the peripheral can save more power, but at the cost of more memory consumed to save the register context. It's a tradeoff between power consumption and memory consumption. This configuration option relies on specific hardware feature, if you enable it on an unsupported chip, you will see error message like `not able to power down in light sleep`.

Once the `rmt_rx_channel_config_t` structure is populated with mandatory parameters, users can call `rmt_new_rx_channel` to allocate and initialize an RX channel. This function returns an RMT channel handle if it runs correctly. Specifically, when there are no more free channels in the RMT resource pool, this function returns `ESP_ERR_NOT_FOUND` error. If some feature (e.g., DMA backend) is not supported by the hardware, it returns `ESP_ERR_NOT_SUPPORTED` error.

``` c
rmt_channel_handle_t rx_chan = NULL;
rmt_rx_channel_config_t rx_chan_config = {
    .clk_src = RMT_CLK_SRC_DEFAULT,   // select source clock
    .resolution_hz = 1 * 1000 * 1000, // 1 MHz tick resolution, i.e., 1 tick = 1 µs
    .mem_block_symbols = 64,          // memory block size, 64 * 4 = 256 Bytes
    .gpio_num = 2,                    // GPIO number
    .flags.invert_in = false,         // do not invert input signal
    .flags.with_dma = false,          // do not need DMA backend
};
ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &rx_chan));
```

> **Note**
>
> #### Uninstall RMT Channel

If a previously installed RMT channel is no longer needed, it is recommended to recycle the resources by calling `rmt_del_channel`, which in return allows the underlying software and hardware resources to be reused for other purposes.

### Carrier Modulation and Demodulation

The RMT transmitter can generate a carrier wave and modulate it onto the message signal. Compared to the message signal, the carrier signal's frequency is significantly higher. In addition, the user can only set the frequency and duty cycle for the carrier signal. The RMT receiver can demodulate the carrier signal from the incoming signal. Note that, carrier modulation and demodulation are not supported on all ESP chips, please refer to \[[TRM](%7BIDF_TARGET_TRM_EN_URL%7D#rmt)\] before configuring the carrier, or you might encounter a `ESP_ERR_NOT_SUPPORTED` error.

Carrier-related configurations lie in `rmt_carrier_config_t`:

- `rmt_carrier_config_t::frequency_hz` sets the carrier frequency, in Hz.
- `rmt_carrier_config_t::duty_cycle` sets the carrier duty cycle.
- `rmt_carrier_config_t::extra_rmt_carrier_config_flags::polarity_active_low` sets the carrier polarity, i.e., on which level the carrier is applied.
- `rmt_carrier_config_t::extra_rmt_carrier_config_flags::always_on` sets whether to output the carrier even when the data transmission has finished. This configuration is only valid for the TX channel.

> **Note**
>
> ``` c
rmt_carrier_config_t tx_carrier_cfg = {
    .duty_cycle = 0.33,                 // duty cycle 33%
    .frequency_hz = 38000,              // 38 KHz
    .flags.polarity_active_low = false, // carrier should be modulated to high level
};
// modulate carrier to TX channel
ESP_ERROR_CHECK(rmt_apply_carrier(tx_chan, &tx_carrier_cfg));

rmt_carrier_config_t rx_carrier_cfg = {
    .duty_cycle = 0.33,                 // duty cycle 33%
    .frequency_hz = 25000,              // 25 KHz carrier, should be smaller than the transmitter's carrier frequency
    .flags.polarity_active_low = false, // the carrier is modulated to high level
};
// demodulate carrier from RX channel
ESP_ERROR_CHECK(rmt_apply_carrier(rx_chan, &rx_carrier_cfg));
```

### Register Event Callbacks

When an event occurs on an RMT channel (e.g., transmission or receiving is completed), the CPU is notified of this event via an interrupt. If you have some function that needs to be called when a particular events occur, you can register a callback for that event to the RMT driver's ISR (Interrupt Service Routine) by calling `rmt_tx_register_event_callbacks` and `rmt_rx_register_event_callbacks` for TX and RX channel respectively. Since the registered callback functions are called in the interrupt context, the user should ensure the callback function does not block, e.g., by making sure that only FreeRTOS APIs with the `FromISR` suffix are called from within the function. The callback function has a boolean return value used to indicate whether a higher priority task has been unblocked by the callback.

The TX channel-supported event callbacks are listed in the `rmt_tx_event_callbacks_t`:

- `rmt_tx_event_callbacks_t::on_trans_done` sets a callback function for the "trans-done" event. The function prototype is declared in `rmt_tx_done_callback_t`.

The RX channel-supported event callbacks are listed in the `rmt_rx_event_callbacks_t`:

- `rmt_rx_event_callbacks_t::on_recv_done` sets a callback function for "receive-done" event. The function prototype is declared in `rmt_rx_done_callback_t`.

> **Note**
>
> Users can save their own context in `rmt_tx_register_event_callbacks` and `rmt_rx_register_event_callbacks` as well, via the parameter `user_data`. The user data is directly passed to each callback function.

In the callback function, users can fetch the event-specific data that is filled by the driver in the `edata`. Note that the `edata` pointer is **only** valid during the callback, please do not try to save this pointer and use that outside of the callback function.

The TX-done event data is defined in `rmt_tx_done_event_data_t`:

- `rmt_tx_done_event_data_t::num_symbols` indicates the number of transmitted RMT symbols. This also reflects the size of the encoding artifacts. Please note, this value accounts for the `EOF` symbol as well, which is appended by the driver to mark the end of one transaction.

The RX-complete event data is defined in `rmt_rx_done_event_data_t`:

- `rmt_rx_done_event_data_t::received_symbols` points to the received RMT symbols. These symbols are saved in the `buffer` parameter of the `rmt_receive` function. Users should not free this receive buffer before the callback returns. If you also enabled the partial receive feature, then the user buffer will be used as a "second level buffer", where its content can be overwritten by data comes in afterwards. In this case, you should copy the received data to another place if you want to keep it or process it later.
- `rmt_rx_done_event_data_t::num_symbols` indicates the number of received RMT symbols. This value is not larger than the `buffer_size` parameter of `rmt_receive` function. If the `buffer_size` is not sufficient to accommodate all the received RMT symbols, the driver only keeps the maximum number of symbols that the buffer can hold, and excess symbols are discarded or ignored.
- `rmt_rx_done_event_data_t::extra_rmt_rx_done_event_flags::is_last` indicates whether the current received buffer is the last one in the transaction. This is useful when you enable the partial reception feature by `rmt_receive_config_t::extra_rmt_receive_flags::en_partial_rx`.

### Enable and Disable Channel

`rmt_enable` must be called in advance before transmitting or receiving RMT symbols. For TX channels, enabling a channel enables a specific interrupt and prepares the hardware to dispatch transactions. For RX channels, enabling a channel enables an interrupt, but the receiver is not started during this time, as the characteristics of the incoming signal have yet to be specified. The receiver is started in `rmt_receive`.

`rmt_disable` does the opposite by disabling the interrupt and clearing any pending interrupts. The transmitter and receiver are disabled as well.

``` c
ESP_ERROR_CHECK(rmt_enable(tx_chan));
ESP_ERROR_CHECK(rmt_enable(rx_chan));
```

### Initiate TX Transaction

RMT is a special communication peripheral, as it is unable to transmit raw byte streams like SPI and I2C. RMT can only send data in its own format `rmt_symbol_word_t`. However, the hardware does not help to convert the user data into RMT symbols, this can only be done in software by the so-called **RMT Encoder**. The encoder is responsible for encoding user data into RMT symbols and then writing to the RMT memory block or the DMA buffer. For how to create an RMT encoder, please refer to `rmt-rmt-encoder`.

Once you created an encoder, you can initiate a TX transaction by calling `rmt_transmit`. This function takes several positional parameters like channel handle, encoder handle, and payload buffer. Besides, you also need to provide a transmission-specific configuration in `rmt_transmit_config_t`:

- `rmt_transmit_config_t::loop_count` sets the number of transmission loops. After the transmitter has finished one round of transmission, it can restart the same transmission again if this value is not set to zero. As the loop is controlled by hardware, the RMT channel can be used to generate many periodic sequences with minimal CPU intervention.

  > - Setting `rmt_transmit_config_t::loop_count` to <span class="title-ref">-1</span> means an infinite loop transmission. In this case, the channel does not stop until `rmt_disable` is called. The "trans-done" event is not generated as well.
  >
  >   >
  > not esp32
  >
  > - Setting `rmt_transmit_config_t::loop_count` to a positive number means finite number of iterations. In this case, the "trans-done" event is when the specified number of iterations have completed.
  >
  > </div>

- `rmt_transmit_config_t::eot_level` sets the output level when the transmitter finishes working or stops working by calling `rmt_disable`.

- `rmt_transmit_config_t::queue_nonblocking` sets whether to wait for a free slot in the transaction queue when it is full. If this value is set to `true`, then the function will return with an error code `ESP_ERR_INVALID_STATE` when the queue is full. Otherwise, the function will block until a free slot is available in the queue.

> **Note**
>
> Internally, `rmt_transmit` constructs a transaction descriptor and sends it to a job queue, which is dispatched in the ISR. So it is possible that the transaction is not started yet when `rmt_transmit` returns. You cannot recycle or modify the payload buffer until the transaction is finished. You can get the transaction completion event by registering a callback function via `rmt_tx_register_event_callbacks`. To ensure all pending transactions to complete, you can also use `rmt_tx_wait_all_done`.

### Multiple Channels Simultaneous Transmission

In some real-time control applications (e.g., to make two robotic arms move simultaneously), you do not want any time drift between different channels. The RMT driver can help to manage this by creating a so-called **Sync Manager**. The sync manager is represented by `rmt_sync_manager_handle_t` in the driver. The procedure of RMT sync transmission is shown as follows:

<figure>
<img src="/../_static/rmt_tx_sync.png" class="align-center" alt="/../_static/rmt_tx_sync.png" />
<figcaption>RMT TX Sync</figcaption>
</figure>

#### Install RMT Sync Manager

To create a sync manager, the user needs to tell which channels are going to be managed in the `rmt_sync_manager_config_t`:

- `rmt_sync_manager_config_t::tx_channel_array` points to the array of TX channels to be managed.
- `rmt_sync_manager_config_t::array_size` sets the number of channels to be managed.

`rmt_new_sync_manager` can return a manager handle on success. This function could also fail due to various errors such as invalid arguments, etc. Especially, when the sync manager has been installed before, and there are no hardware resources to create another manager, this function reports `ESP_ERR_NOT_FOUND` error. In addition, if the sync manager is not supported by the hardware, it reports a `ESP_ERR_NOT_SUPPORTED` error. Please refer to \[[TRM](%7BIDF_TARGET_TRM_EN_URL%7D#rmt)\] before using the sync manager feature.

#### Start Transmission Simultaneously

For any managed TX channel, it does not start the machine until `rmt_transmit` has been called on all channels in `rmt_sync_manager_config_t::tx_channel_array`. Before that, the channel is just put in a waiting state. TX channels will usually complete their transactions at different times due to differing transactions, thus resulting in a loss of sync. So before restarting a simultaneous transmission, the user needs to call `rmt_sync_reset` to synchronize all channels again.

Calling `rmt_del_sync_manager` can recycle the sync manager and enable the channels to initiate transactions independently afterward.

``` c
rmt_channel_handle_t tx_channels[2] = {NULL}; // declare two channels
int tx_gpio_number[2] = {0, 2};
// install channels one by one
for (int i = 0; i < 2; i++) {
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,       // select source clock
        .gpio_num = tx_gpio_number[i],    // GPIO number
        .mem_block_symbols = 64,          // memory block size, 64 * 4 = 256 Bytes
        .resolution_hz = 1 * 1000 * 1000, // 1 MHz resolution
        .trans_queue_depth = 1,           // set the number of transactions that can pend in the background
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &tx_channels[i]));
}
// enable the channels
for (int i = 0; i < 2; i++) {
    ESP_ERROR_CHECK(rmt_enable(tx_channels[i]));
}
// install sync manager
rmt_sync_manager_handle_t synchro = NULL;
rmt_sync_manager_config_t synchro_config = {
    .tx_channel_array = tx_channels,
    .array_size = sizeof(tx_channels) / sizeof(tx_channels[0]),
};
ESP_ERROR_CHECK(rmt_new_sync_manager(&synchro_config, &synchro));

ESP_ERROR_CHECK(rmt_transmit(tx_channels[0], led_strip_encoders[0], led_data, led_num * 3, &transmit_config));
// tx_channels[0] does not start transmission until call of `rmt_transmit()` for tx_channels[1] returns
ESP_ERROR_CHECK(rmt_transmit(tx_channels[1], led_strip_encoders[1], led_data, led_num * 3, &transmit_config));
```

### Initiate RX Transaction

As also discussed in the `rmt-enable-and-disable-channel`, calling `rmt_enable` does not prepare an RX to receive RMT symbols. The user needs to specify the basic characteristics of the incoming signals in `rmt_receive_config_t`:

- `rmt_receive_config_t::signal_range_min_ns` specifies the minimal valid pulse duration in either high or low logic levels. A pulse width that is smaller than this value is treated as a glitch, and ignored by the hardware.
- `rmt_receive_config_t::signal_range_max_ns` specifies the maximum valid pulse duration in either high or low logic levels. A pulse width that is bigger than this value is treated as **Stop Signal**, and the receiver generates receive-complete event immediately.
- If the incoming packet is long, that they cannot be stored in the user buffer at once, you can enable the partial reception feature by setting `rmt_receive_config_t::extra_rmt_receive_flags::en_partial_rx` to `true`. In this case, the driver invokes `rmt_rx_event_callbacks_t::on_recv_done` callback multiple times during one transaction, when the user buffer is **almost full**. You can check the value of `rmt_rx_done_event_data_t::extra_rmt_rx_done_event_flags::is_last` to know if the transaction is about to finish. Please note this features is not supported on all ESP series chips because it relies on hardware abilities like "ping-pong receive" or "DMA receive".

The RMT receiver starts the RX machine after the user calls `rmt_receive` with the provided configuration above. Note that, this configuration is transaction specific, which means, to start a new round of reception, the user needs to set the `rmt_receive_config_t` again. The receiver saves the incoming signals into its internal memory block or DMA buffer, in the format of `rmt_symbol_word_t`.

> **Note**
>
> >
> Note

After calling `rmt_receive` function, the actual reception starts at the first level change of the received signal. If you need to get the start time of the reception, you can register a GPIO interrupt on the GPIO of the RMT RX channel, please refer to `GPIO doc </api-reference/peripherals/gpio>` for details.

SOC_RMT_SUPPORT_RX_PINGPONG

Due to the limited size of the memory block, the RMT receiver notifies the driver to copy away the accumulated symbols in a ping-pong way.

not SOC_RMT_SUPPORT_RX_PINGPONG

Due to the limited size of the memory block, the RMT receiver can only save short frames whose length is not longer than the memory block capacity. Long frames are truncated by the hardware, and the driver reports an error message: `hw buffer too small, received symbols truncated`.

The copy destination should be provided in the `buffer` parameter of `rmt_receive` function. If this buffer overflows due to an insufficient buffer size, the receiver can continue to work, but overflowed symbols are dropped and the following error message is reported: `user buffer too small, received symbols truncated`. Please take care of the lifecycle of the `buffer` parameter, ensuring that the buffer is not recycled before the receiver is finished or stopped.

The receiver is stopped by the driver when it finishes working, i.e., receive a signal whose duration is bigger than `rmt_receive_config_t::signal_range_max_ns`. The user needs to call `rmt_receive` again to restart the receiver, if necessary. The user can get the received data in the `rmt_rx_event_callbacks_t::on_recv_done` callback. See also `rmt-register-event-callbacks` for more information.

``` c
static bool example_rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t receive_queue = (QueueHandle_t)user_data;
    // send the received RMT symbols to the parser task
    xQueueSendFromISR(receive_queue, edata, &high_task_wakeup);
    // return whether any task is woken up
    return high_task_wakeup == pdTRUE;
}

QueueHandle_t receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
rmt_rx_event_callbacks_t cbs = {
    .on_recv_done = example_rmt_rx_done_callback,
};
ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_channel, &cbs, receive_queue));

// the following timing requirement is based on NEC protocol
rmt_receive_config_t receive_config = {
    .signal_range_min_ns = 1250,     // the shortest duration for NEC signal is 560 µs, 1250 ns < 560 µs, valid signal is not treated as noise
    .signal_range_max_ns = 12000000, // the longest duration for NEC signal is 9000 µs, 12000000 ns > 9000 µs, the receive does not stop early
};

rmt_symbol_word_t raw_symbols[64]; // 64 symbols should be sufficient for a standard NEC frame
// ready to receive
ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config));
// wait for the RX-done signal
rmt_rx_done_event_data_t rx_data;
xQueueReceive(receive_queue, &rx_data, portMAX_DELAY);
// parse the received symbols
example_parse_nec_frame(rx_data.received_symbols, rx_data.num_symbols);
```

### RMT Encoder

An RMT encoder is part of the RMT TX transaction, whose responsibility is to generate and write the correct RMT symbols into hardware memory or DMA buffer at a specific time. There are some special restrictions for an encoding function:

- During a single transaction, the encoding function may be called multiple times. This is necessary because the target RMT memory block cannot hold all the artifacts at once. To overcome this limitation, the driver utilizes a **ping-pong** approach, where the encoding session is divided into multiple parts. This means that the encoder needs to **keep track of its state** to continue encoding from where it left off in the previous part.
- The encoding function is running in the ISR context. To speed up the encoding session, it is highly recommended to put the encoding function into IRAM. This can also avoid the cache miss during encoding.

To help get started with the RMT driver faster, some commonly used encoders are provided out-of-the-box. They can either work alone or be chained together into a new encoder. See also [Composite Pattern](https://en.wikipedia.org/wiki/Composite_pattern) for the principle behind it. The driver has defined the encoder interface in `rmt_encoder_t`, it contains the following functions:

- `rmt_encoder_t::encode` is the fundamental function of an encoder. This is where the encoding session happens.

  > - The function might be called multiple times within a single transaction. The encode function should return the state of the current encoding session.
  > - The supported states are listed in the `rmt_encode_state_t`. If the result contains `RMT_ENCODING_COMPLETE`, it means the current encoder has finished work.
  > - If the result contains `RMT_ENCODING_MEM_FULL`, the program needs to yield from the current session, as there is no space to save more encoding artifacts.

- `rmt_encoder_t::reset` should reset the encoder state back to the initial state (the RMT encoder is stateful).

  > - If the RMT transmitter is manually stopped without resetting its corresponding encoder, subsequent encoding session can be erroneous.
  > - This function is also called implicitly in `rmt_disable`.

- `rmt_encoder_t::del` should free the resources allocated by the encoder.

#### Copy Encoder

A copy encoder is created by calling `rmt_new_copy_encoder`. A copy encoder's main functionality is to copy the RMT symbols from user space into the driver layer. It is usually used to encode `const` data, i.e., data does not change at runtime after initialization such as the leading code in the IR protocol.

A configuration structure `rmt_copy_encoder_config_t` should be provided in advance before calling `rmt_new_copy_encoder`. Currently, this configuration is reserved for future expansion, and has no specific use or setting items for now.

#### Bytes Encoder

A bytes encoder is created by calling `rmt_new_bytes_encoder`. The bytes encoder's main functionality is to convert the user space byte stream into RMT symbols dynamically. It is usually used to encode dynamic data, e.g., the address and command fields in the IR protocol.

A configuration structure `rmt_bytes_encoder_config_t` should be provided in advance before calling `rmt_new_bytes_encoder`:

- `rmt_bytes_encoder_config_t::bit0` and `rmt_bytes_encoder_config_t::bit1` are necessary to specify the encoder how to represent bit zero and bit one in the format of `rmt_symbol_word_t`.
- `rmt_bytes_encoder_config_t::msb_first` sets the bit endianness of each byte. If it is set to true, the encoder encodes the **Most Significant Bit** first. Otherwise, it encodes the **Least Significant Bit** first.

Besides the primitive encoders provided by the driver, the user can implement his own encoder by chaining the existing encoders together. A common encoder chain is shown as follows:

/../\_static/diagrams/rmt/rmt_encoder_chain.diag

#### Simple Callback Encoder

A simple callback encoder is created by calling `rmt_new_simple_encoder`. The simple callback encoder allows you to provide a callback that reads data from userspace and writes symbols to the output stream without having to chain other encoders. The callback itself gets a pointer to the data passed to `rmt_transmit`, a counter indicating the amount of symbols already written by the callback in this transmission, and a pointer where to write the encoded RMT symbols as well as the free space there. If the space is not enough for the callback to encode something, it can return 0 and the RMT will wait for previous symbols to be transmitted and call the callback again, now with more space available. If the callback successfully writes RMT symbols, it should return the number of symbols written.

A configuration structure `rmt_simple_encoder_config_t` should be provided in advance before calling `rmt_new_simple_encoder`:

- `rmt_simple_encoder_config_t::callback` and `rmt_simple_encoder_config_t::arg` provide the callback function and an opaque argument that will be passed to that function.
- `rmt_simple_encoder_config_t::min_chunk_size` specifies the minimum amount of free space, in symbols, the encoder will be always be able to write some data to. In other words, when this amount of free space is passed to the encoder, it should never return 0 (except when the encoder is done encoding symbols).

While the functionality of an encoding process using the simple callback encoder can usually also realized by chaining other encoders, the simple callback can be more easy to understand and maintain than an encoder chain.

SOC_BITSCRAMBLER_SUPPORTED and SOC_RMT_SUPPORT_DMA

#### BitScrambler Encoder

When the RMT transmit channel has DMA enabled, we can control the data on the DMA path by writing `BitScrambler </api-reference/peripherals/bitscrambler>` assembly code, which implements simple encoding operations. Compared to CPU-based encoding, the BitScrambler offers higher performance without consuming CPU resources. However, due to the limited instruction memory space of the BitScrambler, it cannot implement complex encoding operations. Additionally, the output data format from the BitScrambler program must conform to the `rmt_symbol_word_t` structure.

A BitScrambler encoder can be created by calling `rmt_new_bitscrambler_encoder`. This function takes a configuration parameter of type `rmt_bs_encoder_config_t`, which includes the following configuration items:  
- `rmt_bs_encoder_config_t::program_bin` points to the binary file of the BitScrambler program. This binary file must comply with the BitScrambler assembly language specification and will be loaded into the BitScrambler's instruction memory at runtime. For information on how to write and compile BitScrambler programs, please refer to the `BitScrambler Programming Guide </api-reference/peripherals/bitscrambler>`.

> **Note**
>
> </div>

#### Customize RMT Encoder for NEC Protocol

This section demonstrates how to write an NEC encoder. The NEC IR protocol uses pulse distance encoding of the message bits. Each pulse burst is `562.5 µs` in length, logical bits are transmitted as follows. It is worth mentioning that the least significant bit of each byte is sent first.

- Logical `0`: a `562.5 µs` pulse burst followed by a `562.5 µs` space, with a total transmit time of `1.125 ms`
- Logical `1`: a `562.5 µs` pulse burst followed by a `1.6875 ms` space, with a total transmit time of `2.25 ms`

When a key is pressed on the remote controller, the transmitted message includes the following elements in the specified order:

<figure>
<img src="/../_static/ir_nec.png" class="align-center" alt="/../_static/ir_nec.png" />
<figcaption>IR NEC Frame</figcaption>
</figure>

- `9 ms` leading pulse burst, also called the "AGC pulse"
- `4.5 ms` space
- 8-bit address for the receiving device
- 8-bit logical inverse of the address
- 8-bit command
- 8-bit logical inverse of the command
- a final `562.5 µs` pulse burst to signify the end of message transmission

Then you can construct the NEC `rmt_encoder_t::encode` function in the same order, for example:

``` c
// IR NEC scan code representation
typedef struct {
    uint16_t address;
    uint16_t command;
} ir_nec_scan_code_t;

// construct an encoder by combining primitive encoders
typedef struct {
    rmt_encoder_t base;           // the base "class" declares the standard encoder interface
    rmt_encoder_t *copy_encoder;  // use the copy_encoder to encode the leading and ending pulse
    rmt_encoder_t *bytes_encoder; // use the bytes_encoder to encode the address and command data
    rmt_symbol_word_t nec_leading_symbol; // NEC leading code with RMT representation
    rmt_symbol_word_t nec_ending_symbol;  // NEC ending code with RMT representation
    int state; // record the current encoding state, i.e., we are in which encoding phase
} rmt_ir_nec_encoder_t;

static size_t rmt_encode_ir_nec(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    rmt_ir_nec_encoder_t *nec_encoder = __containerof(encoder, rmt_ir_nec_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    ir_nec_scan_code_t *scan_code = (ir_nec_scan_code_t *)primary_data;
    rmt_encoder_handle_t copy_encoder = nec_encoder->copy_encoder;
    rmt_encoder_handle_t bytes_encoder = nec_encoder->bytes_encoder;
    switch (nec_encoder->state) {
    case 0: // send leading code
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, &nec_encoder->nec_leading_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            nec_encoder->state = 1; // we can only switch to the next state when the current encoder finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there is no free space to put other encoding artifacts
        }
    // fall-through
    case 1: // send address
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, &scan_code->address, sizeof(uint16_t), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            nec_encoder->state = 2; // we can only switch to the next state when the current encoder finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there is no free space to put other encoding artifacts
        }
    // fall-through
    case 2: // send command
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, &scan_code->command, sizeof(uint16_t), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            nec_encoder->state = 3; // we can only switch to the next state when the current encoder finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there is no free space to put other encoding artifacts
        }
    // fall-through
    case 3: // send ending code
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, &nec_encoder->nec_ending_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            nec_encoder->state = RMT_ENCODING_RESET; // back to the initial encoding session
            state |= RMT_ENCODING_COMPLETE; // telling the caller the NEC encoding has finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there is no free space to put other encoding artifacts
        }
    }
out:
    *ret_state = state;
    return encoded_symbols;
}
```

A full sample code can be found in `peripherals/rmt/ir_nec_transceiver`. In the above snippet, we use a `switch-case` and several `goto` statements to implement a [Finite-state machine](https://en.wikipedia.org/wiki/Finite-state_machine) . With this pattern, users can construct much more complex IR protocols.

### Power Management

When power management is enabled, i.e., `CONFIG_PM_ENABLE` is on, the system may adjust or disable the clock source before going to sleep. As a result, the time base inside the RMT can't work as expected.

The driver can prevent the above issue by creating a power management lock. The lock type is set based on different clock sources. The driver will acquire the lock in `rmt_enable`, and release it in `rmt_disable`. That means, any RMT transactions in between these two functions are guaranteed to work correctly and stable.

SOC_RMT_SUPPORT_SLEEP_RETENTION

Besides the potential changes to the clock source, when the power management is enabled, the system can also power down the RMT hardware before sleep. Set `rmt_tx_channel_config_t::allow_pd` and `rmt_rx_channel_config_t::allow_pd` to `true` to enable the power down feature. RMT registers will be backed up before sleep and restored after wake up. Please note, enabling this option will increase the memory consumption.

### Cache Safe

By default, the RMT interrupt is deferred when the Cache is disabled for reasons like writing or erasing the main Flash. Thus the transaction-done interrupt does not get handled in time, which is not acceptable in a real-time application. What is worse, when the RMT transaction relies on **ping-pong** interrupt to successively encode or copy RMT symbols, a delayed interrupt can lead to an unpredictable result.

There is a Kconfig option `CONFIG_RMT_TX_ISR_CACHE_SAFE` and `CONFIG_RMT_RX_ISR_CACHE_SAFE` that has the following features:

1.  Enable the interrupt being serviced even when the cache is disabled
2.  Place all functions used by the ISR into IRAM[^2]
3.  Place the driver object into DRAM in case it is mapped to PSRAM by accident

This Kconfig option allows the interrupt handler to run while the cache is disabled but comes at the cost of increased IRAM consumption.

Please note, when `CONFIG_RMT_TX_ISR_CACHE_SAFE` is enabled, you must also place the encoder functions (mainly the `rmt_encoder_t::encode` and `rmt_encoder_t::reset`) into IRAM. You can use `RMT_ENCODER_FUNC_ATTR` to decorate your encoder functions.

Another Kconfig option `CONFIG_RMT_RECV_FUNC_IN_IRAM` can place `rmt_receive` into the IRAM as well. So that the receive function can be used even when the flash cache is disabled.

### Thread Safety

The factory function `rmt_new_tx_channel`, `rmt_new_rx_channel` and `rmt_new_sync_manager` are guaranteed to be thread-safe by the driver, which means, user can call them from different RTOS tasks without protection by extra locks. Other functions that take the `rmt_channel_handle_t` and `rmt_sync_manager_handle_t` as the first positional parameter, are not thread-safe. which means the user should avoid calling them from multiple tasks.

The following functions are allowed to use under ISR context as well.

- `rmt_receive`

### Kconfig Options

- `CONFIG_RMT_TX_ISR_CACHE_SAFE` and `CONFIG_RMT_RX_ISR_CACHE_SAFE` control whether the default ISR handler can work when cache is disabled, see also `rmt-cache-safe` for more information.
- `CONFIG_RMT_ENABLE_DEBUG_LOG` is used to enable the debug log at the cost of increased firmware binary size.
- `CONFIG_RMT_RECV_FUNC_IN_IRAM` controls where to place the RMT receive function (IRAM or Flash), see `rmt-cache-safe` for more information.

## Application Examples

- `peripherals/rmt/led_strip` demonstrates how to use the RMT peripheral to drive a WS2812 LED strip, which is able to change the number of LEDs and the chasing effect.
- `peripherals/rmt/led_strip_simple_encoder` demonstrates how to use the RMT peripheral to drive a WS2812 LED strip by implementing a callback that converts RGB pixels into a format recognized by the hardware.
- `peripherals/rmt/ir_nec_transceiver` demonstrates how to use the RMT peripheral to implement the encoding and decoding of remote IR NEC protocol.
- `peripherals/rmt/dshot_esc` demonstrates how to use the RMT TX channel to achieve infinite loop transmission. It constructs an RMT encoder for the DShot digital protocol. This protocol is primarily used for communication between flight controllers and electronic speed controllers, offering greater resistance to electrical noise compared to traditional analog protocols.

\- `peripherals/rmt/onewire` demonstrates how to simulate the 1-wire hardware protocol by using the <span class="title-ref">onewire_bus</span> library, and read data from multiple DS18B20 temperature sensors on the bus. This library is built upon a pair of transmit and receive channels of the RMT peripheral. :SOC_RMT_SUPPORT_TX_LOOP_COUNT: - `peripherals/rmt/musical_buzzer` demonstrates how to use the RMT TX channel to drive a passive buzzer to play simple music. Each musical note is represented by a constant frequency of PWM signal with a fixed duration. :SOC_RMT_SUPPORT_TX_LOOP_AUTO_STOP: - `peripherals/rmt/stepper_motor` demonstrates how to use the RMT peripheral to drive a STEP/DIR interfaced stepper motor controller (such as DRV8825). After programming the RMT encoder, S-curve profile can be implemented for desired acceleration, constant speed, and deceleration phases, thereby smoothly driving the stepper motor.

## FAQ

- Why the RMT transmits more data than expected?

  > The encoding for the RMT transmission is carried out within the ISR context. Should the RMT encoding process be prolonged (for example, through logging or tracing the procedure) or if it is delayed due to interrupt latency and preemptive interrupts, the hardware transmitter might read from the memory before the encoder has written to it. Consequently, the transmitter would end up sending outdated data. Although it's not possible to instruct the transmitter to pause and wait, this issue can be mitigated by employing a combination of the following strategies:
  >
  > > - Increase the `rmt_tx_channel_config_t::mem_block_symbols`, in steps of {IDF_TARGET_SOC_RMT_MEM_WORDS_PER_CHANNEL}.
  > > - Place the encoding function in the IRAM with `IRAM_ATTR` attribute.
  > > - Enable the `rmt_tx_channel_config_t::with_dma` if DMA is available.
  > > - Install the RMT driver on a separate CPU core to avoid competing for the same CPU resources with other interrupt heavy peripherals (e.g. WiFi, Bluetooth).

## API Reference

inc/rmt_tx.inc

inc/rmt_rx.inc

inc/rmt_common.inc

inc/rmt_encoder.inc

inc/components/esp_driver_rmt/include/driver/rmt_types.inc

inc/components/esp_hal_rmt/include/hal/rmt_types.inc

[^1]: Different ESP chip series might have different numbers of RMT channels. Please refer to \[[TRM](%7BIDF_TARGET_TRM_EN_URL%7D#rmt)\] for details. The driver does not forbid you from applying for more RMT channels, but it returns an error when there are no hardware resources available. Please always check the return value when doing [Resource Allocation](#resource-allocation).

[^2]: The callback function, e.g., `rmt_tx_event_callbacks_t::on_trans_done`, and the functions invoked by itself should also reside in IRAM, users need to take care of this by themselves.
