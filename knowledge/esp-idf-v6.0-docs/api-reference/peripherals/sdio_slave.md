<!-- Source: _sources/api-reference/peripherals/sdio_slave.rst.txt (ESP-IDF v6.0 documentation) -->

# SDIO Card Slave Driver

## Overview

<!-- Only for: esp32 -->
The ESP32 SDIO Card host and slave peripherals share two sets of pins, as shown in the table below. The first set is usually occupied by the SPI0 bus, which is responsible for the SPI flash holding the code to run. This means the SDIO slave driver can only run on the second set of pins while the SDIO host is not using it.

The SDIO slave can run under three modes: SPI, 1-bit SD, and 4-bit SD modes. Based on the signals on the interface, the device can determine the current mode and configure itself to adapt to that mode. Later, the slave driver can communicate with the slave device to properly handle commands and data transfers. According to the SDIO specification, the CMD and DAT0-3 signal lines should be pulled up whether in 1-bit SD, 4-bit SD or SPI mode.

### Connections

<!-- Only for: esp32 -->
| Pin Name | Corresponding Pins in SPI Mode | GPIO Number (Slot 1) | GPIO Number (Slot 2) |
|----------|--------------------------------|----------------------|----------------------|
| CLK      | SCLK                           | 6                    | 14                   |
| CMD      | MOSI                           | 11                   | 15                   |
| DAT0     | MISO                           | 7                    | 2                    |
| DAT1     | Interrupt                      | 8                    | 4                    |
| DAT2     | N.C. (pullup)                  | 9                    | 12                   |
| DAT3     | \#CS                           | 10                   | 13                   |

<!-- Only for: esp32c6 -->
| Pin Name | Corresponding Pins in SPI Mode | GPIO Number |
|----------|--------------------------------|-------------|
| CLK      | SCLK                           | 19          |
| CMD      | MOSI                           | 18          |
| DAT0     | MISO                           | 20          |
| DAT1     | Interrupt                      | 21          |
| DAT2     | N.C. (pullup)                  | 22          |
| DAT3     | \#CS                           | 23          |

- 1-bit SD mode: Connect CLK, CMD, DAT0, DAT1 pins, and the ground.
- 4-bit SD mode: Connect all pins, and the ground.
- SPI mode: Connect SCLK, MOSI, MISO, Interrupt, \#CS pins, and the ground.

> **Note**
>
> <!-- Only for: esp32 -->
> **Note**
>
> Refer to `sd_pullup_requirements` for more technical details of the pullups.

sd_pullup_requirements

The host initializes the slave into SD mode by sending the CMD0 command with the DAT3 pin set to a high level. Alternatively, the host initializes the SPI mode by sending CMD0 with CS pin low, which is the same pin as DAT3.

After the initialization, the host can enable the 4-bit SD mode by writing CCCR register 0x07 by CMD52. All the bus detection processes are handled by the slave peripheral.

The host has to communicate with the slave by an ESP-slave-specific protocol.

The slave driver offers three services over Function 1 access by CMD52 and CMD53:

1)  sending and receiving FIFO
2)  52 R/W registers (8-bit) shared by host and slave
3)  16 interrupt sources (8 from host to slave, and 8 from slave to host)

### Terminology

The SDIO slave driver uses the following terms:

- A transfer is initiated by a command token from the host and may consist of a response and multiple data blocks. The core mechanism of the {IDF_TARGET_NAME} SDIO slave driver involves data exchange and communication through transfers.
- Sending: slave to host transfers.
- Receiving: host to slave transfers.

> **Note**
>
> - FIFO: A designated address within Function 1 that can be accessed using CMD53 commands for reading or writing substantial volumes of data. The address corresponds to the length intended for reading from or writing to the slave in a single transfer: **requested length** = 0x1F800 – address.
- Ownership: When the driver assumes ownership of a buffer, it means that the driver has the capability to perform random read/write operations on the buffer (often via DMA). The application should not read/write the buffer until the ownership is returned to the application. If the application reads from a buffer owned by a receiving driver, the data read can be random; similarly, if the application writes to a buffer owned by a sending driver, the data sent may be corrupted.
- Requested length: The length requested in one transfer determined by the FIFO address.
- Transfer length: The length requested in one transfer determined by the CMD53 byte/block count field.

> **Note**
>
> - Receiving buffer size: The buffer size is pre-defined between the host and the slave before communication starts. The slave application has to set the buffer size during initialization by the `recv_buffer_size` parameter in the `sdio_slave_config_t` structure.
- Interrupts: The {IDF_TARGET_NAME} SDIO slave supports interrupts in two directions: from host to slave (referred to as slave interrupts) and from slave to host (referred to as host interrupts). For more details, refer to `interrupts`.
- Registers: Specific addresses in Function 1 accessed by CMD52 or CMD53.

### Communication with ESP SDIO Slave

The host should initialize the {IDF_TARGET_NAME} SDIO slave according to the standard SDIO initialization process (Sector 3.1.2 of [SDIO Simplified Specification](https://www.sdcard.org/downloads/pls/pdf/?p=PartE1_SDIO_Simplified_Specification_Ver3.00.jpg&f=PartE1_SDIO_Simplified_Specification_Ver3.00.pdf&e=EN_SSE1)), which is described briefly in [ESP SDIO Slave Initialization](https://espressif.github.io/idf-extra-components/latest/esp_serial_slave_link/sdio_slave_protocol.html#esp-sdio-slave-initialization).

Furthermore, there is an {IDF_TARGET_NAME}-specific upper-level communication protocol built upon the foundation of CMD52/CMD53 to Function 1. Within this particular communication protocol, the master and slave engage in data exchange and communication through the utilization of CMD52/CMD53 commands. For more detailed information, please consult the [ESP SDIO Slave Protocol](https://espressif.github.io/idf-extra-components/latest/esp_serial_slave_link/sdio_slave_protocol.html#esp-sdio-slave-protocol) section.

There is also a component [ESSL](https://components.espressif.com/components/espressif/esp_serial_slave_link) designed for {IDF_TARGET_NAME} master to communicate with {IDF_TARGET_NAME} SDIO slave. See example `peripherals/sdio` when programming your host.

### Interrupts

There are interrupts from host to slave, and from slave to host to help communicating conveniently.

#### Slave Interrupts

The host can trigger an interruption in the slave by writing a single bit to the 0x08D register. As soon as any bit within the register is set, an interrupt is generated, prompting the SDIO slave driver to invoke the callback function specified in the `slave_intr_cb` member of the `sdio_slave_config_t` structure.

> **Note**
>
> Similar to the previous information, there's an alternative set of functions available. You can call `sdio_slave_wait_int` to wait for an interrupt within a certain time, or call `sdio_slave_clear_int` to clear interrupts from host. The callback function can work with the wait functions perfectly.

#### Host Interrupts

The slave can interrupt the host by an interrupt line at certain time, which is level-sensitive, i.e., the interrupt signal can be triggered by detecting the level change of the interrupt line. When the host see the interrupt line pulled down, it may read the slave interrupt status register, to see the interrupt source. Host can clear interrupt bits, or choose to disable a interrupt source. The interrupt line holds active until all the sources are cleared or disabled.

There are several dedicated interrupt sources as well as general-purpose sources. see `sdio_slave_hostint_t` for more information.

### Shared Registers

There are 52 R/W shared registers (8-bit) to share information between host and slave. The slave can write or read the registers at any time by `sdio_slave_read_reg` and `sdio_slave_write_reg`. The host can access (R/W) the register by CMD52 or CMD53.

### Receiving FIFO

When the host is going to send the slave some packets, it has to check whether the slave is ready to receive by reading the buffer number of slave.

To allow the host sending data to the slave, the application has to load buffers to the slave driver by the following steps:

1.  Register the buffer by calling `sdio_slave_recv_register_buf`, and get the handle of the registered buffer. The driver allocates memory for the linked-list descriptor needed to link the buffer onto the hardware. The size of these buffers should equal to the Receiving buffer size.

2.  Load buffers onto the driver by passing the buffer handle to `sdio_slave_recv_load_buf`.

3.  Get the received data by calling `sdio_slave_recv` or `sdio_slave_recv_packet`. If a non-blocking call is needed, set `wait=0`.

    The difference between two APIs is that, `sdio_slave_recv_packet` gives more information about packet, which can consist of several buffers.

    When `ESP_ERR_NOT_FINISHED` is returned by this API, you should call this API iteratively until the return value is `ESP_OK`. All the continuous buffers returned with `ESP_ERR_NOT_FINISHED`, together with the last buffer returned with `ESP_OK`, belong to one packet from the host.

    Call `sdio_slave_recv_get_buf` to get the address of the received data, and the actual length received in each buffer. The packet length is the sum of received length of all the buffers in the packet.

    If the host never send data longer than the Receiving buffer size, or you do not care about the packet boundary (e.g., the data is only a byte stream), you can call the simpler version `sdio_slave_recv` instead.

4.  Pass the handle of processed buffer back to the driver by `sdio_recv_load_buf` again.

> **Note**
>
> ### Sending FIFO

Each time the slave has data to send, it raises an interrupt, and the host requests the packet length. There are two sending modes:

- Stream Mode: When a buffer is loaded into the driver, the buffer length is included into the packet length requested by host in the incoming communications. This is irrespective of whether previous packets have been sent or not. In other words, the length of the newly loaded buffer is included into the length of the packet requested by the host, even if there are previously unsent packets. This enables the host to receive data from several buffers in a single transfer.
- Packet Mode: The packet length is updated packet by packet, and only when previous packet is sent. This means that the host can only get data of one buffer in one transfer.

> **Note**
>
> The sending mode can be set in the `sending_mode` member of `sdio_slave_config_t`, and the buffer numbers can be set in the `send_queue_size`. All the buffers are restricted to be no larger than 4092 bytes. Though in the stream mode, several buffers can be sent in one transfer, each buffer is still counted as one in the queue.

The application can call `sdio_slave_transmit` to send packets. In this case, the function returns when the transfer is successfully done, so the queue is not fully used. When higher efficiency is required, the application can use the following functions instead:

1.  Pass buffer information (address, length, as well as an `arg` indicating the buffer) to `sdio_slave_send_queue`.
    - If non-blocking call is needed, set `wait=0`.
    - If the `wait` is not `portMAX_DELAY` (wait until success), application has to check the result to know whether the data is put in to the queue or discard.
2.  Call `sdio_slave_send_get_finished` to get and deal with a finished transfer. A buffer should be kept unmodified until returned from `sdio_slave_send_get_finished`. This means the buffer is actually sent to the host, rather than just staying in the queue.

There are several ways to use the `arg` in the queue parameter:

> 1.  Directly point `arg` to a dynamic-allocated buffer, and use the `arg` to free it when transfer finished.
>
> 2.  Wrap transfer information in a transfer structure, and point `arg` to the structure. You can use the structure to do more things like:
>
>         typedef struct {
>             uint8_t* buffer;
>             size_t   size;
>             int      id;
>         }sdio_transfer_t;
>
>         //and send as:
>         sdio_transfer_t trans = {
>             .buffer = ADDRESS_TO_SEND,
>             .size = 8,
>             .id = 3,  //the 3rd transfer so far
>         };
>         sdio_slave_send_queue(trans.buffer, trans.size, &trans, portMAX_DELAY);
>
>         //... maybe more transfers are sent here
>
>         //and deal with finished transfer as:
>         sdio_transfer_t* arg = NULL;
>         sdio_slave_send_get_finished((void**)&arg, portMAX_DELAY);
>         ESP_LOGI("tag", "(%d) successfully send %d bytes of %p", arg->id, arg->size, arg->buffer);
>         some_post_callback(arg); //do more things
>
> 3.  Work with the receiving part of this driver, and point `arg` to the receive buffer handle of this buffer, so that we can directly use the buffer to receive data when it is sent:
>
>         uint8_t buffer[256]={1,2,3,4,5,6,7,8};
>         sdio_slave_buf_handle_t handle = sdio_slave_recv_register_buf(buffer);
>         sdio_slave_send_queue(buffer, 8, handle, portMAX_DELAY);
>
>         //... maybe more transfers are sent here
>
>         //and load finished buffer to receive as
>         sdio_slave_buf_handle_t handle = NULL;
>         sdio_slave_send_get_finished((void**)&handle, portMAX_DELAY);
>         sdio_slave_recv_load_buf(handle);
>
>     For more about this, see `peripherals/sdio`.

### Reset SDIO

Calling `sdio_slave_reset` can reset PKT_LEN (Packet length accumulator value) and TOKEN1 (Receiving buffers accumulated number) at the SDIO slave driver software level to resynchronize the transmit and receive counts with the host.

If there is a usage scenario where the ESP chip remains powered on but the HOST is powered off. During the power-off period of the HOST, some unknown signals may be generated on the SDIO signal line, causing the SDIO hardware state machine to be abnormal. The HOST restarts and executes the card identification process, and the ESP will not respond normally. In this case, consider calling `sdio_slave_reset_hw` to reset the SDIO hardware.

> **Note**
>
> ## Application Example

- `peripherals/sdio/host` and `peripherals/sdio/slave` demonstrate how to use a host to communicate with an ESP SDIO slave device.

## API Reference

inc/sdio_slave_types.inc

inc/sdio_slave.inc

