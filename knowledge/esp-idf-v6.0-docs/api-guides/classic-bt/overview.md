<!-- Source: _sources/api-guides/classic-bt/overview.rst.txt (ESP-IDF v6.0 documentation) -->

# Overview

This document provides an architecture overview of the Bluetooth Classic stack in ESP-IDF and some quick links to related documents and application examples.

<!-- Only for: esp32 -->
{IDF_TARGET_NAME} supports Dual-Mode Bluetooth 4.2.

The Bluetooth Classic stack in ESP-IDF is a layered architecture that enables Bluetooth functionality on {IDF_TARGET_NAME} chip series. The figure below shows its architecture.

<!-- Only for: esp32 -->
<figure>
<img src="../../../_static/classic-bluetooth-architecture.png" class="align-center" alt="../../../_static/classic-bluetooth-architecture.png" />
<figcaption>{IDF_TARGET_NAME} Bluetooth Classic Stack Architecture</figcaption>
</figure>

The following sections briefly describe each layer and provide quick links to the related documents and application examples.

## ESP Bluetooth Controller

At the bottom layer is ESP Bluetooth Controller, which encompasses various modules such as PHY, Baseband, Link Controller, Link Manager, Device Manager, and HCI. It handles hardware interface management and link management. It provides functions in the form of libraries and is accessible through APIs. This layer directly interacts with the hardware and low-level Bluetooth protocols.

- `API reference <../../api-reference/bluetooth/controller_vhci>`
- `Application examples <bluetooth/hci/controller_hci_uart_esp32>`

## Hosts

There is one host, ESP-Bluedroid, supporting Classic Bluetooth in IDF.

### ESP-Bluedroid

ESP-Bluedroid is a modified version of the native Android Bluetooth stack, Bluedroid. It consists of two layers: the Bluetooth Upper Layer (BTU) and the Bluetooth Transport Controller layer (BTC). The BTU layer is responsible for processing bottom layer Bluetooth protocols such as L2CAP and other profiles. The BTU layer provides an interface prefixed with "bta". The BTC layer is mainly responsible for providing a supported interface, prefixed with "esp", to the application layer and handling miscellaneous tasks. All the APIs are located in the ESP_API layer. Developers should use the Classic Bluetooth APIs prefixed with "esp".

- API references
  - `../../api-reference/bluetooth/bt_common`
  - `../../api-reference/bluetooth/classic_bt`
- `Application examples <bluetooth/bluedroid/classic_bt>`

## Applications

At the uppermost layer are applications. You can build your own applications on top of the ESP-Bluedroid stacks, leveraging the provided APIs and profiles to create Bluetooth Classic applications tailored to specific use cases.
