<!-- Source: _sources/api-reference/peripherals/uart.rst.txt (ESP-IDF v6.0 documentation) -->

# Universal Asynchronous Receiver/Transmitter (UART)

{IDF_TARGET_UART_EXAMPLE_PORT:default = "UART_NUM_1", esp32 = "UART_NUM_2", esp32s3 = "UART_NUM_2"}

## Introduction

A Universal Asynchronous Receiver/Transmitter (UART) is a hardware feature that handles communication (i.e., timing requirements and data framing) using widely-adopted asynchronous serial communication interfaces, such as RS232, RS422, and RS485. A UART provides a widely adopted and cheap method to realize full-duplex or half-duplex data exchange among different devices.

The {IDF_TARGET_NAME} chip has {IDF_TARGET_SOC_UART_HP_NUM} UART controllers (also referred to as port), each featuring an identical set of registers to simplify programming and for more flexibility.

Each UART controller is independently configurable with parameters such as baud rate, data bit length, bit ordering, number of stop bits, parity bit, etc. All the regular UART controllers are compatible with UART-enabled devices from various manufacturers and can also support Infrared Data Association (IrDA) protocols.

SOC_UART_HAS_LP_UART

Additionally, the {IDF_TARGET_NAME} chip has one low-power (LP) UART controller. It is the cut-down version of regular UART. Usually, the LP UART controller only support basic UART functionality with a much smaller RAM size, and does not support IrDA or RS485 protocols. For a full list of difference between UART and LP UART, please refer to the **{IDF_TARGET_NAME} Technical Reference Manual** \> **UART Controller (UART)** \> **Features** \[[PDF](%7BIDF_TARGET_TRM_EN_URL%7D#uart)\]).

SOC_UHCI_SUPPORTED

uhci

The {IDF_TARGET_NAME} chip also supports using DMA with UART. For details, see to `uhci`.

## Functional Overview

The overview describes how to establish communication between an {IDF_TARGET_NAME} and other UART devices using the functions and data types of the UART driver. A typical programming workflow is broken down into the sections provided below:

1.  `uart-api-driver-installation` - Allocating {IDF_TARGET_NAME}'s resources for the UART driver
2.  `uart-api-setting-communication-parameters` - Setting baud rate, data bits, stop bits, etc.
3.  `uart-api-setting-communication-pins` - Assigning pins for connection to a device
4.  `uart-api-running-uart-communication` - Sending/receiving data
5.  `uart-api-using-interrupts` - Triggering interrupts on specific communication events
6.  `uart-api-deleting-driver` - Freeing allocated resources if a UART communication is no longer required

Steps 1 to 3 comprise the configuration stage. Step 4 is where the UART starts operating. Steps 5 and 6 are optional.

SOC_UART_HAS_LP_UART

Additionally, when using the LP UART Controller you need to pay attention to `uart-api-lp-uart-driver`.

The UART driver's functions identify each of the UART controllers using `uart_port_t`. This identification is needed for all the following function calls.

### Install Drivers

First of all, install the driver by calling `uart_driver_install` and specify the following parameters:

- UART port number
- Size of RX ring buffer
- Size of TX ring buffer
- Event queue size
- Pointer to store the event queue handle
- Flags to allocate an interrupt

The function allocates the required internal resources for the UART driver.

``` c
// Setup UART buffered IO with event queue
const int uart_buffer_size = (1024 * 2);
QueueHandle_t uart_queue;
// Install UART driver using an event queue here
ESP_ERROR_CHECK(uart_driver_install({IDF_TARGET_UART_EXAMPLE_PORT}, uart_buffer_size, uart_buffer_size, 10, &uart_queue, 0));
```

### Set Communication Parameters

As the next step, UART communication parameters can be configured all in a single step or individually in multiple steps.

#### Single Step

Call the function `uart_param_config` and pass to it a `uart_config_t` structure. The `uart_config_t` structure should contain all the required parameters. See the example below.

``` c
const uart_port_t uart_num = {IDF_TARGET_UART_EXAMPLE_PORT};
uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS,
    .rx_flow_ctrl_thresh = 122,
};
// Configure UART parameters
ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
```

For more information on how to configure the hardware flow control options, please refer to `peripherals/uart/uart_echo`.

SOC_UART_SUPPORT_SLEEP_RETENTION

Additionally, `uart_config_t::allow_pd` can be set to enable the backup of the UART configuration registers before entering sleep and restore these registers after exiting sleep. This allows the UART to continue working properly after waking up even when the UART module power domain is entirely off during sleep. This option implies an balance between power consumption and memory usage. If the power consumption is not a concern, you can disable this option to save memory.

#### Multiple Steps

Configure specific parameters individually by calling a dedicated function from the table given below. These functions are also useful if re-configuring a single parameter.

| Parameter to Configure     | Function                                                        |
|----------------------------|-----------------------------------------------------------------|
| Baud rate                  | `uart_set_baudrate`                                             |
| Number of transmitted bits | `uart_set_word_length` selected out of `uart_word_length_t`     |
| Parity control             | `uart_set_parity` selected out of `uart_parity_t`               |
| Number of stop bits        | `uart_set_stop_bits` selected out of `uart_stop_bits_t`         |
| Hardware flow control mode | `uart_set_hw_flow_ctrl` selected out of `uart_hw_flowcontrol_t` |
| Communication mode         | `uart_set_mode` selected out of `uart_mode_t`                   |

Functions for Configuring specific parameters individually

Each of the above functions has a `_get_` counterpart to check the currently set value. For example, to check the current baud rate value, call `uart_get_baudrate`.

### Set Communication Pins

After setting communication parameters, configure the physical GPIO pins to which the other UART device will be connected. For this, call the function `uart_set_pin` and specify the GPIO pin numbers to which the driver should route the TX, RX, RTS, CTS, DTR, and DSR signals. If you want to keep a currently allocated pin number for a specific signal, pass the macro `UART_PIN_NO_CHANGE`.

The same macro `UART_PIN_NO_CHANGE` should be specified for pins that will not be used.

``` c
// Set UART pins(TX: IO4, RX: IO5, RTS: IO18, CTS: IO19, DTR: UNUSED, DSR: UNUSED)
ESP_ERROR_CHECK(uart_set_pin({IDF_TARGET_UART_EXAMPLE_PORT}, 4, 5, 18, 19, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
```

### Run UART Communication

Serial communication is controlled by each UART controller's finite state machine (FSM).

The process of sending data involves the following steps:

1.  Write data into TX FIFO buffer
2.  FSM serializes the data
3.  FSM sends the data out

The process of receiving data is similar, but the steps are reversed:

1.  FSM processes an incoming serial stream and parallelizes it
2.  FSM writes the data into RX FIFO buffer
3.  Read the data from RX FIFO buffer

Therefore, an application only writes and reads data from a specific buffer using `uart_write_bytes` and `uart_read_bytes` respectively, and the FSM does the rest.

#### Transmit Data

After preparing the data for transmission, call the function `uart_write_bytes` and pass the data buffer's address and data length to it. The function copies the data to the TX ring buffer (either immediately or after enough space is available), and then exit. When there is free space in the TX FIFO buffer, an interrupt service routine (ISR) moves the data from the TX ring buffer to the TX FIFO buffer in the background. The code below demonstrates the use of this function.

``` c
// Write data to UART.
char* test_str = "This is a test string.\n";
uart_write_bytes(uart_num, (const char*)test_str, strlen(test_str));
```

The function `uart_write_bytes_with_break` is similar to `uart_write_bytes` but adds a serial break signal at the end of the transmission. A 'serial break signal' means holding the TX line low for a period longer than one data frame.

``` c
// Write data to UART, end with a break signal.
uart_write_bytes_with_break(uart_num, "test break\n",strlen("test break\n"), 100);
```

Another function for writing data to the TX FIFO buffer is `uart_tx_chars`. Unlike `uart_write_bytes`, this function does not block until space is available. Instead, it writes all data which can immediately fit into the hardware TX FIFO, and then return the number of bytes that were written.

There is a 'companion' function `uart_wait_tx_done` that monitors the status of the TX FIFO buffer and returns once it is empty.

``` c
// Wait for packet to be sent
const uart_port_t uart_num = {IDF_TARGET_UART_EXAMPLE_PORT};
ESP_ERROR_CHECK(uart_wait_tx_done(uart_num, 100)); // wait timeout is 100 RTOS ticks (TickType_t)
```

#### Receive Data

Once the data is received by the UART and saved in the RX FIFO buffer, it needs to be retrieved using the function `uart_read_bytes`. Before reading data, you can check the number of bytes available in the RX FIFO buffer by calling `uart_get_buffered_data_len`. An example of using these functions is given below.

``` c
// Read data from UART.
const uart_port_t uart_num = {IDF_TARGET_UART_EXAMPLE_PORT};
uint8_t data[128];
int length = 0;
ESP_ERROR_CHECK(uart_get_buffered_data_len(uart_num, (size_t*)&length));
length = uart_read_bytes(uart_num, data, length, 100);
```

If the data in the RX FIFO buffer is no longer needed, you can clear the buffer by calling `uart_flush`.

#### Software Flow Control

If the hardware flow control is disabled, you can manually set the RTS and DTR signal levels by using the functions `uart_set_rts` and `uart_set_dtr` respectively.

#### Communication Mode Selection

The UART controller supports a number of communication modes. A mode can be selected using the function `uart_set_mode`. Once a specific mode is selected, the UART driver handles the behavior of a connected UART device accordingly. As an example, it can control the RS485 driver chip using the RTS line to allow half-duplex RS485 communication.

``` bash
// Setup UART in rs485 half duplex mode
ESP_ERROR_CHECK(uart_set_mode(uart_num, UART_MODE_RS485_HALF_DUPLEX));
```

### Use Interrupts

There are many interrupts that can be generated depending on specific UART states or detected errors. The full list of available interrupts is provided in *{IDF_TARGET_NAME} Technical Reference Manual* \> *UART Controller (UART)* \> *UART Interrupts* \[[PDF](%7BIDF_TARGET_TRM_EN_URL%7D#uart)\]. You can enable or disable specific interrupts by calling `uart_enable_intr_mask` or `uart_disable_intr_mask` respectively.

The UART driver provides a convenient way to handle specific interrupts by wrapping them into corresponding events. Events defined in `uart_event_type_t` can be reported to a user application using the FreeRTOS queue functionality.

To receive the events that have happened, call `uart_driver_install` and get the event queue handle returned from the function. Please see the above `code snippet <driver-code-snippet>` as an example.

The processed events include the following:

- **FIFO overflow** (`UART_FIFO_OVF`): The RX FIFO can trigger an interrupt when it receives more data than the FIFO can store.

  > - (Optional) Configure the full threshold of the FIFO space by entering it in the structure `uart_intr_config_t` and call `uart_intr_config` to set the configuration. This can help the data stored in the RX FIFO can be processed timely in the driver to avoid FIFO overflow.
  > - Enable the interrupts using the functions `uart_enable_rx_intr`.
  > - Disable these interrupts using the corresponding functions `uart_disable_rx_intr`.

  ``` c
  const uart_port_t uart_num = {IDF_TARGET_UART_EXAMPLE_PORT};
  // Configure a UART interrupt threshold and timeout
  uart_intr_config_t uart_intr = {
      .intr_enable_mask = UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT,
      .rxfifo_full_thresh = 100,
      .rx_timeout_thresh = 10,
  };
  ESP_ERROR_CHECK(uart_intr_config(uart_num, &uart_intr));

  // Enable UART RX FIFO full threshold and timeout interrupts
  ESP_ERROR_CHECK(uart_enable_rx_intr(uart_num));
  ```

- **Pattern detection** (`UART_PATTERN_DET`): An interrupt triggered on detecting a 'pattern' of the same character being received/sent repeatedly. It can be used, e.g., to detect a command string with a specific number of identical characters (the 'pattern') at the end. The following functions are available:

  > - Configure and enable this interrupt using `uart_enable_pattern_det_baud_intr`
  > - Disable the interrupt using `uart_disable_pattern_det_intr`

  ``` c
  //Set UART pattern detect function
  uart_enable_pattern_det_baud_intr(EX_UART_NUM, '+', PATTERN_CHR_NUM, 9, 0, 0);
  ```

- **Other events**: The UART driver can report other events such as data receiving (`UART_DATA`), ring buffer full (`UART_BUFFER_FULL`), detecting NULL after the stop bit (`UART_BREAK`), parity check error (`UART_PARITY_ERR`), and frame error (`UART_FRAME_ERR`).

The strings inside of brackets indicate corresponding event names. An example of how to handle various UART events can be found in `peripherals/uart/uart_events`.

### Deleting a Driver

If the communication established with `uart_driver_install` is no longer required, the driver can be removed to free allocated resources by calling `uart_driver_delete`.

### Macros

The API also defines several macros. For example, `UART_HW_FIFO_LEN` defines the length of hardware FIFO buffers; `UART_BITRATE_MAX` gives the maximum baud rate supported by the UART controllers, etc.

SOC_UART_HAS_LP_UART

### Use LP UART Controller with HP Core

The UART driver also supports to control the LP UART controller when the chip is in active mode. The configuration steps for the LP UART are the same as the steps for a normal UART controller, except:

- The port number for the LP UART controller is defined by `LP_UART_NUM_0`.
- The available clock sources for the LP UART controller can be found in `lp_uart_sclk_t`.

\- The size of the hardware FIFO for the LP UART controller is much smaller, which is defined in `SOC_LP_UART_FIFO_LEN`. :SOC_LP_GPIO_MATRIX_SUPPORTED: - The GPIO pins for the LP UART controller can only be selected from the LP GPIO pins. :not SOC_LP_GPIO_MATRIX_SUPPORTED: - The GPIO pins for the LP UART controller are unalterable, because there is no LP GPIO matrix on the target. Please see **{IDF_TARGET_NAME} Technical Reference Manual** \> **IO MUX and GPIO Matrix (GPIO, IO MUX)** \> **LP IO MUX Functions List** \[[PDF](%7BIDF_TARGET_TRM_EN_URL%7D#lp-io-mux-func-list)\] for the specific pin numbers.

</div>

## Overview of RS485 Specific Communication 0ptions

> **Note**
>
> - `UART_RS485_CONF_REG.UART_RS485_EN`: setting this bit enables RS485 communication mode support.
- `UART_RS485_CONF_REG.UART_RS485TX_RX_EN`: if this bit is set, the transmitter's output signal loops back to the receiver's input signal.
- `UART_RS485_CONF_REG.UART_RS485RXBY_TX_EN`: if this bit is set, the transmitter will still be sending data if the receiver is busy (remove collisions automatically by hardware).

The {IDF_TARGET_NAME}'s RS485 UART hardware can detect signal collisions during transmission of a datagram and generate the interrupt `UART_RS485_CLASH_INT` if this interrupt is enabled. The term collision means that a transmitted datagram is not equal to the one received on the other end. Data collisions are usually associated with the presence of other active devices on the bus or might occur due to bus errors.

The collision detection feature allows handling collisions when their interrupts are activated and triggered. The interrupts `UART_RS485_FRM_ERR_INT` and `UART_RS485_PARITY_ERR_INT` can be used with the collision detection feature to control frame errors and parity bit errors accordingly in RS485 mode. This functionality is supported in the UART driver and can be used by selecting the `UART_MODE_RS485_APP_CTRL` mode (see the function `uart_set_mode`).

The collision detection feature can work with circuit A and circuit C (see Section [Interface Connection Options](#interface-connection-options)). Use the function `uart_get_collision_flag` to check if the collision detection flag has been raised. In the case of using circuit A or B, either DTR or RTS pin can be connected to the DE/~RE pin of the transceiver module to achieve half-duplex communication.

The RS485 half-duplex communication mode is supported by the UART driver and can be activated by selecting the `UART_MODE_RS485_HALF_DUPLEX` mode calling `uart_set_mode`. The DTR line is automatically controlled by the hardware directly under RS485 half-duplex mode, while the RTS line is software-controlled by the UART driver. Once the host starts writing data to the TX FIFO buffer, the UART driver automatically asserts the RTS pin (logic 1); once the last bit of the data has been transmitted, the driver de-asserts the RTS pin (logic 0). To use this mode, the software would have to disable the hardware flow control function. Since the switching is made in the interrupt handler, comparing to DTR line, some latency is expected on RTS line.

<!-- Only for: esp32 -->
> **Note**
>
> ### Interface Connection Options

This section provides example schematics to demonstrate the basic aspects of {IDF_TARGET_NAME}'s RS485 interface connection.

> **Note**
>
> #### Circuit A: Collision Detection Circuit

``` none
VCC ---------------+
                   |
           +-------x-------+
RXD <------| R             |
           |              B|----------<> B
TXD ------>| D    ADM483   |
ESP                |               |     RS485 bus side
DTR/RTS ------>| DE            |
           |              A|----------<> A
      +----| /RE           |
      |    +-------x-------+
      |            |
     GND          GND
```

This circuit is preferable because it allows for collision detection and is quite simple at the same time. The receiver in the line driver is constantly enabled, which allows the UART to monitor the RS485 bus. Echo suppression is performed by the UART peripheral when the bit `UART_RS485_CONF_REG.UART_RS485TX_RX_EN` is enabled.

#### Circuit B: Manual Switching Transmitter/Receiver Without Collision Detection

``` none
VCC ---------------+
                   |
           +-------x-------+
RXD <------| R             |
           |              B|-----------<> B
TXD ------>| D    ADM483   |
ESP                |               |     RS485 bus side
DTR/RTS --+--->| DE            |
      |    |              A|-----------<> A
      +----| /RE           |
           +-------x-------+
                   |
                  GND
```

This circuit does not allow for collision detection. It suppresses the null bytes that the hardware receives when the bit `UART_RS485_CONF_REG.UART_RS485TX_RX_EN` is set. The bit `UART_RS485_CONF_REG.UART_RS485RXBY_TX_EN` is not applicable in this case.

#### Circuit C: Auto Switching Transmitter/Receiver

``` none
VCC1 <-------------------+-----------+           +-------------------+----> VCC2
              10K ____   |           |           |                   |
             +---|____|--+       +---x-----------x---+    10K ____   |
             |                   |                   |   +---|____|--+
RX <----------+-------------------| RXD               |   |
                  10K ____       |                  A|---+---------------<> A (+)
             +-------|____|------| PV    ADM2483     |   |    ____  120
             |   ____            |                   |   +---|____|---+  RS485 bus side
     VCC1 <--+--|____|--+------->| DE                |                |
             10K        |        |                  B|---+------------+--<> B (-)
                     ---+    +-->| /RE               |   |    ____
        10K          |       |   |                   |   +---|____|---+
       ____       | /-C      +---| TXD               |    10K         |
TX >---|____|--+_B_|/   NPN   |   |                   |                |
                  |\         |   +---x-----------x---+                |
                  | \-E      |       |           |                    |
                     |       |       |           |                    |
                    GND1    GND1    GND1        GND2                 GND2
```

This galvanically isolated circuit does not require RTS pin control by a software application or driver because it controls the transceiver direction automatically. However, it requires suppressing null bytes during transmission by setting `UART_RS485_CONF_REG.UART_RS485RXBY_TX_EN` to 1 and `UART_RS485_CONF_REG.UART_RS485TX_RX_EN` to 0. This setup can work in any RS485 UART mode or even in `UART_MODE_UART`.

## Application Examples

- `peripherals/uart/uart_async_rxtxtasks` demonstrates how to use two asynchronous tasks for communication via the same UART interface, with one task transmitting "Hello world" periodically and the other task receiving and printing data from the UART.
- `peripherals/uart/uart_echo` demonstrates how to use the UART interfaces to echo back any data received on the configured UART.
- `peripherals/uart/uart_echo_rs485` demonstrates how to use the ESP32's UART software driver in RS485 half duplex transmission mode to echo any data it receives on UART port back to the sender in the RS485 network, requiring external connection of bus drivers.
- `peripherals/uart/uart_events` demonstrates how to use the UART driver to handle special UART events, read data from UART0, and echo it back to the monitoring console.
- `peripherals/uart/uart_repl` demonstrates how to use and connect two UARTs, allowing the UART used for stdout to send commands and receive replies from another console UART without human interaction.
- `peripherals/uart/uart_select` demonstrates the use of `select()` for synchronous I/O multiplexing on the UART interface, allowing for non-blocking read and write from/to various sources such as UART and sockets, where a ready resource can be served without being blocked by a busy resource.
- `peripherals/uart/nmea0183_parser` demonstrates how to parse NMEA-0183 data streams from GPS/BDS/GLONASS modules using the ESP UART Event driver and ESP event loop library, and output common information such as UTC time, latitude, longitude, altitude, and speed.

## API Reference

inc/uart.inc

inc/uart_wakeup.inc

inc/uart_types.inc

### GPIO Lookup Macros

Some UART ports have dedicated IO_MUX pins to which they are connected directly. These can be useful if you need very high UART baud rates, which means you will have to use IO_MUX pins only. In other cases, any GPIO pin can be used for UART communication by routing the signals through the GPIO matrix. If the UART port has dedicated IO_MUX pins, `UxTXD_GPIO_NUM` and `UxRXD_GPIO_NUM` can be used to find the corresponding IO_MUX pin numbers.

inc/uart_pins.inc

