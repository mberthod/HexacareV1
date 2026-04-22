<!-- Source: _sources/api-guides/ble/host-feature-support-status.rst.txt (ESP-IDF v6.0 documentation) -->

orphan  

# ESP Host Major Feature Support Status

The table below shows the support status of major features on ESP-Bluedroid and ESP-NimBLE Host. If you plan to run the Bluetooth LE Controller and Host on {IDF_TARGET_NAME} together, the functionality of the Host may be limited by the support status of the Controller, please check the `{IDF_TARGET_NAME} Major Feature Support Status Table <ble-feature-support-status>` .

![supported_def](../../../_static/ble/feature_status/supported.svg) **This feature has completed development and internal testing.**[^1]

![experimental_def](../../../_static/ble/feature_status/experimental.svg) **This feature has been developed and is currently undergoing internal testing.** You can explore these features for evaluation and feedback purposes but should be cautious of potential issues.

![developing_def](../../../_static/ble/feature_status/developingYYYYMM.svg) **The feature is currently being actively developed, and expected to be supported by the end of YYYY/MM.** You should anticipate future updates regarding the progress and availability of these features. If you do have an urgent need, please contact our [customer support team](https://www.espressif.com/en/contact-us/sales-questions) for a possible feature trial.

![unsupported_def](../../../_static/ble/feature_status/unsupported.svg) **This feature is not supported on this Host.** If you have related requirements, please prioritize selecting other Espressif Bluetooth Host that support this feature. If none of our chip series meet your needs, please contact [customer support team](https://www.espressif.com/en/contact-us/sales-questions), and our R&D team will conduct an internal feasibility assessment for you.

![NA_def](../../../_static/ble/feature_status/NA.svg) The feature with this label could be the following type:  
- **Controller-only Feature**: The feature exists only below HCI, and cannot be configured/enabled via Host API, such as Advertising Channel Index. It does not require the support from the Host.

<table>
<thead>
<tr class="header">
<th><p>Core Spec</p>
</th>
<th><p>Major Features</p>
</th>
<th><p>ESP-Bluedroid Host</p>
</th>
<th><p>ESP-NimBLE Host</p>
</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><p><a href="https://www.bluetooth.com/specifications/specs/core-specification-4-2/">4.2</a></p>
</td>
<td>LE Data Packet Length Extension</td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
</tr>
<tr class="even">
<td></td>
<td>LE Secure Connections</td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
</tr>
<tr class="odd">
<td></td>
<td>Link Layer Privacy</td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
</tr>
<tr class="even">
<td></td>
<td>Link Layer Extended Filter Policies</td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
</tr>
<tr class="odd">
<td><p><a href="https://www.bluetooth.com/specifications/specs/core-specification-5-0/">5.0</a></p>
</td>
<td>2 Msym/s PHY for LE</td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
</tr>
<tr class="even">
<td></td>
<td>LE Long Range (Coded PHY S=2/S=8)</td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
</tr>
<tr class="odd">
<td></td>
<td>High Duty Cycle Non-Connectable Advertising</td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
</tr>
<tr class="even">
<td></td>
<td>LE Advertising Extensions</td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
</tr>
<tr class="odd">
<td></td>
<td>LE Channel Selection Algorithm #2</td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
</tr>
<tr class="even">
<td><p><a href="https://www.bluetooth.com/specifications/specs/core-specification-5-1/">5.1</a></p>
</td>
<td>Angle of Arrival (AoA)/Angle of Departure (AoD)</td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
</tr>
<tr class="odd">
<td></td>
<td>GATT Caching</td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
</tr>
<tr class="even">
<td></td>
<td>Randomized Advertising Channel Indexing</td>
<td><img src="../../../_static/ble/feature_status/NA.svg" class="align-center" alt="NA" /></td>
<td><img src="../../../_static/ble/feature_status/NA.svg" class="align-center" alt="NA" /></td>
</tr>
<tr class="odd">
<td></td>
<td>Periodic Advertising Sync Transfer</td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
</tr>
<tr class="even">
<td><p><a href="https://www.bluetooth.com/specifications/specs/core-specification-5-2/">5.2</a></p>
</td>
<td>LE Isochronous Channels (BIS/CIS)</td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
<td><img src="../../../_static/ble/feature_status/developing202512.svg" class="align-center" width="120" alt="developing202512" /></td>
</tr>
<tr class="odd">
<td></td>
<td>Enhanced Attribute Protocol</td>
<td><img src="../../../_static/ble/feature_status/unsupported.svg" class="align-center" alt="unsupported" /></td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
</tr>
<tr class="even">
<td></td>
<td>LE Power Control</td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
</tr>
<tr class="odd">
<td><p><a href="https://www.bluetooth.com/specifications/specs/core-specification-5-3/">5.3</a></p>
</td>
<td>AdvDataInfo in Periodic Advertising</td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
</tr>
<tr class="even">
<td></td>
<td>LE Enhanced Connection Update (Connection Subrating)</td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
</tr>
<tr class="odd">
<td></td>
<td>LE Channel Classification</td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
</tr>
<tr class="even">
<td><p><a href="https://www.bluetooth.com/specifications/specs/core-specification-5-4/">5.4</a></p>
</td>
<td>Advertising Coding Selection</td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
<td><img src="../../../_static/ble/feature_status/supported.svg" class="align-center" alt="supported" /></td>
</tr>
<tr class="odd">
<td></td>
<td>Encrypted Advertising Data</td>
<td><img src="../../../_static/ble/feature_status/developing202512.svg" class="align-center" width="120" alt="developing202512" /></td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
</tr>
<tr class="even">
<td></td>
<td>LE GATT Security Levels Characteristic</td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
</tr>
<tr class="odd">
<td></td>
<td>Periodic Advertising with Responses</td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
</tr>
<tr class="even">
<td><p><a href="https://www.bluetooth.com/specifications/specs/core-specification-6-0/">6.0</a></p>
</td>
<td>Channel Sounding</td>
<td><img src="../../../_static/ble/feature_status/experimental.svg" class="align-center" alt="experimental" /></td>
<td><img src="../../../_static/ble/feature_status/unsupported.svg" class="align-center" alt="unsupported" /></td>
</tr>
<tr class="odd">
<td></td>
<td>LL Extended Feature Set</td>
<td><img src="../../../_static/ble/feature_status/developing202606.svg" class="align-center" width="125" alt="developing202606" /></td>
<td><img src="../../../_static/ble/feature_status/developing202606.svg" class="align-center" width="125" alt="developing202606" /></td>
</tr>
<tr class="even">
<td></td>
<td>Decision-Based Advertising Filtering</td>
<td><img src="../../../_static/ble/feature_status/unsupported.svg" class="align-center" alt="unsupported" /></td>
<td><img src="../../../_static/ble/feature_status/unsupported.svg" class="align-center" alt="unsupported" /></td>
</tr>
<tr class="odd">
<td></td>
<td>Enhancements for ISOAL</td>
<td><img src="../../../_static/ble/feature_status/unsupported.svg" class="align-center" alt="unsupported" /></td>
<td><img src="../../../_static/ble/feature_status/unsupported.svg" class="align-center" alt="unsupported" /></td>
</tr>
<tr class="even">
<td></td>
<td>Monitoring Advertisers</td>
<td><img src="../../../_static/ble/feature_status/developing202606.svg" class="align-center" width="125" alt="developing202606" /></td>
<td><img src="../../../_static/ble/feature_status/developing202606.svg" class="align-center" width="125" alt="developing202606" /></td>
</tr>
<tr class="odd">
<td></td>
<td>Frame Space Update</td>
<td><img src="../../../_static/ble/feature_status/unsupported.svg" class="align-center" alt="unsupported" /></td>
<td><img src="../../../_static/ble/feature_status/unsupported.svg" class="align-center" alt="unsupported" /></td>
</tr>
</tbody>
</table>

It is important to clarify that this document is not a binding commitment to our customers. The above feature support status information is for general informational purposes only and is subject to change without notice. You are encouraged to consult with our [customer support team](https://www.espressif.com/en/contact-us/sales-questions) for the most up-to-date information and to verify the suitability of features for your specific needs.

[^1]: If you would like to know the Bluetooth SIG certification information for supported features, please consult [SIG Bluetooth Product Database](https://qualification.bluetooth.com/Listings/Search).
