<!-- Source: _sources/api-reference/network/esp_openthread.rst.txt (ESP-IDF v6.0 documentation) -->

# Thread

## Introduction

[Thread](https://www.threadgroup.org) is an IP-based mesh networking protocol. It is based on the 802.15.4 physical and MAC layer.

## Application Examples

- `openthread/ot_br` demonstrates how to set up a Thread border router on {IDF_TARGET_NAME}, enabling functionalities such as bidirectional IPv6 connectivity, service discovery, etc.
- `openthread/ot_cli` demonstrates how to use the OpenThread Command Line Interface with additional features such as TCP, UDP, and Iperf. This requires a board equipped with an IEEE 802.15.4 module. This example provides instructions on how to set up a network using at least two 802.15.4 boards.
- `openthread/ot_rcp` demonstrates how to work with a Host Processor to perform as a Thread border router and function as a Thread sniffer, using a board with an IEEE 802.15.4 module.
- `openthread/ot_trel` demonstrates Thread Radio Encapsulation Link (TREL) function. This requires a board equipped with a Wi-Fi module.
- `openthread/ot_sleepy_device/deep_sleep` demonstrates Thread Deep-sleep function.
- `openthread/ot_sleepy_device/light_sleep` demonstrates Thread Light-sleep function.

## API Reference

For manipulating the Thread network, the OpenThread API shall be used. The OpenThread API docs can be found at the [OpenThread API docs](https://openthread.io/reference).

ESP-IDF provides extra APIs for launching and managing the OpenThread stack, binding to network interfaces and border routing features.

inc/esp_openthread.inc

inc/esp_openthread_types.inc

inc/esp_openthread_lock.inc

inc/esp_openthread_netif_glue.inc

inc/esp_openthread_border_router.inc

