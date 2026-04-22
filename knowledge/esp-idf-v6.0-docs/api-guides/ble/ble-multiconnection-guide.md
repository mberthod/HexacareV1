<!-- Source: _sources/api-guides/ble/ble-multiconnection-guide.rst.txt (ESP-IDF v6.0 documentation) -->

# Multi-Connection Guide

## Introduction

The following table provides an overview of the maximum number of concurrent connections supported for each ESP Bluetooth LE Host. In multi-connection scenarios, connection parameters must be configured appropriately. In general, as the number of connections increases, the connection interval should be increased accordingly. For detailed parameter configuration recommendations and SDK configuration details, please refer to the corresponding example code in the following table.

In this document, the maximum number of connections refers to the maximum number of simultaneous active connections that the device can maintain, whether operating as a central or peripheral.

### Host SDKconfig

<table style="width:98%;">
<caption>Maximum Concurrent Connections by ESP Bluetooth LE Host</caption>
<colgroup>
<col style="width: 15%" />
<col style="width: 29%" />
<col style="width: 30%" />
<col style="width: 21%" />
</colgroup>
<thead>
<tr class="header">
<th><blockquote>
<p>Host</p>
</blockquote></th>
<th>Max Connections</th>
<th><blockquote>
<p>SDKconfig</p>
</blockquote></th>
<th><blockquote>
<p>Example</p>
</blockquote></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>ESP-Bluedroid</td>
<td></td>
<td><p><code class="interpreted-text" role="ref">BT_MULTI_CONNECTION_ENBALE &lt;CONFIG_BT_MULTI_CONNECTION_ENBALE&gt;</code></p>
<p><code class="interpreted-text" role="ref">BT_ACL_CONNECTIONS &lt;CONFIG_BT_ACL_CONNECTIONS&gt;</code></p></td>
<td><code class="interpreted-text" role="example">multi_conn &lt;bluetooth/bluedroid/ble/ble_multi_conn&gt;</code></td>
</tr>
<tr class="even">
<td>ESP-NimBLE</td>
<td></td>
<td><blockquote>
<p><code class="interpreted-text" role="ref">BT_NIMBLE_MAX_CONNECTIONS &lt;CONFIG_BT_NIMBLE_MAX_CONNECTIONS&gt;</code></p>
</blockquote></td>
<td><blockquote>
<p><code class="interpreted-text" role="example">multi_conn&lt;bluetooth/nimble/ble_multi_conn&gt;</code></p>
</blockquote></td>
</tr>
</tbody>
</table>

Maximum Concurrent Connections by ESP Bluetooth LE Host

### Controller SDKconfig

<!-- Only for: esp32 -->
- `BTDM_CTRL_BLE_MAX_CONN <CONFIG_BTDM_CTRL_BLE_MAX_CONN>`

The configuration option **BTDM_CTRL_BLE_MAX_CONN** specifies the maximum number of Bluetooth LE connections that the controller can support concurrently. This value must match the maximum number of connections configured on the Host side, as defined in the table above.

<!-- Only for: esp32c3 or esp32s3 -->
- `BT_CTRL_BLE_MAX_ACT <CONFIG_BT_CTRL_BLE_MAX_ACT>`

The configuration option **BT_CTRL_BLE_MAX_ACT** defines the maximum number of Bluetooth LE activities that the controller can handle simultaneously. Each Bluetooth LE activity consumes one resource, including:

- Connections
- Advertising
- Scanning
- Periodic sync

Therefore, this parameter should be configured as follows:

**Maximum connections + required advertising, scanning and periodic sync instances**

**Example:** If the Host supports up to 8 connections, and the application requires 1 advertising instance and 1 scanning instance concurrently, set **BT_CTRL_BLE_MAX_ACT** to 10 (8 + 1 + 1).

<!-- Only for: not esp32 and not esp32c3 and not esp32s3 -->
- No controller-related SDK configuration is required.

## Note

1.  The ability to support multiple connections highly depends on the application’s overall memory usage. It is recommended to disable unnecessary features to optimize multi-connection performance.
2.  When the device operates in the peripheral role, connection stability and overall performance will be influenced by the central device and the negotiated connection parameters.

<!-- Only for: not esp32 and not esp32c3 and not esp32s3 and not esp32c2 -->
3.  Due to the relatively higher memory usage of ESP-Bluedroid, it supports fewer concurrent connections compared to ESP-Nimble.
4.  If your application requires more simultaneous connections than the values listed above, please contact our [customer support team](https://www.espressif.com/en/contact-us/sales-questions) for further assistance.

<!-- Only for: esp32 or esp32c3 or esp32s3 -->
esp32c2

<!-- Only for: esp32h2 -->
esp32c6 or esp32c5 or esp32c61

