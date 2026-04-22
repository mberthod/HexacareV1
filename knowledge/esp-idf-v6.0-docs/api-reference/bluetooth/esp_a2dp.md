<!-- Source: _sources/api-reference/bluetooth/esp_a2dp.rst.txt (ESP-IDF v6.0 documentation) -->

# Bluetooth® A2DP API

## Overview

A2DP (Advanced Audio Distribution Profile) enables high-quality audio streaming from one device to another over Bluetooth. It is primarily used for streaming audio from source devices such as smartphones, computers, and media players to sink devices such as Bluetooth speakers, headphones, and car audio systems. Users can use the A2DP APIs to transmit or receive audio streams.

## Application Examples

- `bluetooth/bluedroid/classic_bt/a2dp_sink_stream` demonstrates how to implement an audio sink device using the Advanced Audio Distribution Profile (A2DP) to receive audio streams. This example also shows how to use I2S for audio stream output.
- `bluetooth/bluedroid/classic_bt/a2dp_source` demonstrates how to use A2DP APIs to transmit audio streams.
- `bluetooth/bluedroid/coex/a2dp_gatts_coex` demonstrates how to use the ESP-IDF A2DP-GATTS_COEX demo to create a GATT service and A2DP profile.

## API Reference

inc/esp_a2dp_api.inc

