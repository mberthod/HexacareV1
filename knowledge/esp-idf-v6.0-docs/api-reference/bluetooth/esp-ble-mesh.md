<!-- Source: _sources/api-reference/bluetooth/esp-ble-mesh.rst.txt (ESP-IDF v6.0 documentation) -->

# ESP-BLE-MESH

> **Note**
>
> With various features of ESP-BLE-MESH, users can create a managed flooding mesh network for several scenarios, such as lighting, sensor and etc.

For an ESP32 to join and work on a ESP-BLE-MESH network, it must be provisioned firstly. By provisioning, the ESP32, as an unprovisioned device, will join the ESP-BLE-MESH network and become a ESP-BLE-MESH node, communicating with other nodes within or beyond the radio range.

Apart from ESP-BLE-MESH nodes, inside ESP-BLE-MESH network, there is also ESP32 that works as ESP-BLE-MESH provisioner, which could provision unprovisioned devices into ESP-BLE-MESH nodes and configure the nodes with various features.

For information how to start using ESP32 and ESP-BLE-MESH, please see the Section `getting-started-with-ble-mesh`. If you are interested in information on ESP-BLE-MESH architecture, including some details of software implementation, please see Section `../../api-guides/esp-ble-mesh/ble-mesh-architecture`.

## Application Examples and Demos

Please refer to Sections `esp-ble-mesh-examples` and `esp-ble-mesh-demo-videos`.

## API Reference

ESP-BLE-MESH APIs are divided into the following parts:

- [ESP-BLE-MESH Definitions](#esp-ble-mesh-definitions)
- [ESP-BLE-MESH Core API Reference](#esp-ble-mesh-core-api-reference)
- [ESP-BLE-MESH Models API Reference](#esp-ble-mesh-models-api-reference)
- [ESP-BLE-MESH (v1.1) Core API Reference](#esp-ble-mesh-v1.1-core-api-reference)

## ESP-BLE-MESH Definitions

This section contains only one header file, which lists the following items of ESP-BLE-MESH.

- ID of all the models and related message opcodes
- Structs of model, element and Composition Data
- Structs of used by ESP-BLE-MESH Node/Provisioner for provisioning
- Structs used to transmit/receive messages
- Event types and related event parameters

inc/esp_ble_mesh_defs.inc

## ESP-BLE-MESH Core API Reference

This section contains ESP-BLE-MESH Core related APIs, which can be used to initialize ESP-BLE-MESH stack, provision, send/publish messages, etc.

This API reference covers six components:

- [ESP-BLE-MESH Stack Initialization](#esp-ble-mesh-stack-initialization)
- [Reading of Local Data Information](#reading-of-local-data-information)
- [Low Power Operation (Updating)](#low-power-operation-updating)
- [Send/Publish Messages, add Local AppKey, etc.](#sendpublish-messages-add-local-appkey-etc.)
- [ESP-BLE-MESH Node/Provisioner Provisioning](#esp-ble-mesh-nodeprovisioner-provisioning)
- [ESP-BLE-MESH GATT Proxy Server](#esp-ble-mesh-gatt-proxy-server)

### ESP-BLE-MESH Stack Initialization

inc/esp_ble_mesh_common_api.inc

### Reading of Local Data Information

inc/esp_ble_mesh_local_data_operation_api.inc

Coexist with BLE

inc/esp_ble_mesh_ble_api.inc

### Low Power Operation (Updating)

inc/esp_ble_mesh_low_power_api.inc

### Send/Publish Messages, Add Local AppKey, Etc.

inc/esp_ble_mesh_networking_api.inc

### ESP-BLE-MESH Node/Provisioner Provisioning

inc/esp_ble_mesh_provisioning_api.inc

### ESP-BLE-MESH GATT Proxy Server

inc/esp_ble_mesh_proxy_api.inc

## ESP-BLE-MESH Models API Reference

This section contains ESP-BLE-MESH Model related APIs, event types, event parameters, etc.

There are six categories of models:

- [Configuration Client/Server Models](#configuration-clientserver-models)
- [Health Client/Server Models](#health-clientserver-models)
- [Generic Client/Server Models](#generic-clientserver-models)
- [Sensor Client/Server Models](#sensor-clientserver-models)
- [Time and Scenes Client/Server Models](#time-and-scenes-clientserver-models)
- [Lighting Client/Server Models](#lighting-clientserver-models)

> **Note**
>
> ### Configuration Client/Server Models

inc/esp_ble_mesh_config_model_api.inc

### Health Client/Server Models

inc/esp_ble_mesh_health_model_api.inc

### Generic Client/Server Models

inc/esp_ble_mesh_generic_model_api.inc

### Sensor Client/Server Models

inc/esp_ble_mesh_sensor_model_api.inc

### Time and Scenes Client/Server Models

inc/esp_ble_mesh_time_scene_model_api.inc

### Lighting Client/Server Models

inc/esp_ble_mesh_lighting_model_api.inc

## ESP-BLE-MESH (v1.1) Core API Reference

> **Note**
>
> This section contains ESP-BLE-MESH v1.1 Core related APIs, event types, event parameters, etc.

This API reference covers 10 components:

- [Remote Provisioning](#remote-provisioning)
- [Directed Forwarding](#directed-forwarding)
- [Subnet Bridge Configuration](#subnet-bridge-configuration)
- [Mesh Private Beacon](#mesh-private-beacon)
- [On-Demand Private Proxy](#on-demand-private-proxy)
- [SAR Configuration](#sar-configuration)
- [Solicitation PDU RPL Configuration](#solicitation-pdu-rpl-configuration)
- [Opcodes Aggregator](#opcodes-aggregator)
- [Large Composition Data](#large-composition-data)
- [Composition and Metadata](#composition-and-metadata)

### Remote Provisioning

inc/esp_ble_mesh_rpr_model_api.inc

### Directed Forwarding

inc/esp_ble_mesh_df_model_api.inc

### Subnet Bridge Configuration

inc/esp_ble_mesh_brc_model_api.inc

### Mesh Private Beacon

inc/esp_ble_mesh_prb_model_api.inc

### On-Demand Private Proxy

inc/esp_ble_mesh_odp_model_api.inc

### SAR Configuration

inc/esp_ble_mesh_sar_model_api.inc

### Solicitation PDU RPL Configuration

inc/esp_ble_mesh_srpl_model_api.inc

### Opcodes Aggregator

inc/esp_ble_mesh_agg_model_api.inc

### Large Composition Data

inc/esp_ble_mesh_lcd_model_api.inc

### Composition and Metadata

inc/esp_ble_mesh_cm_data_api.inc

Device Firmware Update

inc/esp_ble_mesh_dfu_model_api.inc

Device Firmware Slots

inc/esp_ble_mesh_dfu_slot_api.inc

