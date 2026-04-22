<!-- Source: _sources/api-guides/jtag-debugging/configure-other-jtag.rst.txt (ESP-IDF v6.0 documentation) -->

# Configure Other JTAG Interfaces

{IDF_TARGET_JTAG_SEL_EFUSE:default="Not Updated!", esp32s3="STRAP_JTAG_SEL", esp32c6="JTAG_SEL_ENABLE", esp32h2="JTAG_SEL_ENABLE", esp32p4="JTAG_SEL_ENABLE", esp32c5="JTAG_SEL_ENABLE", esp32c61="JTAG_SEL_ENABLE", esp32h4="JTAG_SEL_ENABLE"}

For guidance about which JTAG interface to select when using OpenOCD with {IDF_TARGET_NAME}, refer to the section `jtag-debugging-selecting-jtag-adapter`. Then follow the configuration steps below to get it working.

SOC_USB_SERIAL_JTAG_SUPPORTED

## Configure eFuses

By default, {IDF_TARGET_NAME} JTAG interface is connected to the `built-in USB_SERIAL_JTAG peripheral <configure-builtin-jtag>`. To use an external JTAG adapter instead, you need to switch the JTAG interface to the GPIO pins. This can be done by burning eFuses using `idf.py` tool.

<!-- Only for: esp32c3 -->
Burning `DIS_USB_JTAG` eFuse will permanently disable the connection between USB_SERIAL_JTAG and the JTAG port of the {IDF_TARGET_NAME}. JTAG interface can then be connected to . Note that USB CDC functionality of USB_SERIAL_JTAG will still be usable, i.e., flashing and monitoring over USB CDC will still work.

<!-- Only for: not esp32c3 -->
- Burning `DIS_USB_JTAG` eFuse will permanently disable the connection between USB_SERIAL_JTAG and the JTAG port of the {IDF_TARGET_NAME}. JTAG interface can then be connected to . Note that USB CDC functionality of USB_SERIAL_JTAG will still be usable, i.e., flashing and monitoring over USB CDC will still work.
- Burning `{IDF_TARGET_JTAG_SEL_EFUSE}` eFuse will enable selection of JTAG interface by a strapping pin, . If the strapping pin is low when {IDF_TARGET_NAME} is reset, JTAG interface will use . If the strapping pin is high, USB_SERIAL_JTAG will be used as the JTAG interface.

> **Warning**
>
> </div>

## Configure Hardware

1.  Identify all pins/signals on JTAG interface and {IDF_TARGET_NAME} board that should be connected to establish communication.
2.  Verify if {IDF_TARGET_NAME} pins used for JTAG communication are not connected to some other hardware that may disturb JTAG operation.
3.  Connect identified pin/signals of {IDF_TARGET_NAME} and JTAG interface.

## Configure Drivers

You may need to install driver software to make JTAG work with computer. Refer to documentation of your JTAG adapter for related details.

On Linux, adding OpenOCD udev rules is required and is done by copying the [udev rules file](https://github.com/espressif/openocd-esp32/blob/master/contrib/60-openocd.rules) into the `/etc/udev/rules.d` directory.

## Connect

Connect JTAG interface to the computer. Power on {IDF_TARGET_NAME} and JTAG interface boards. Check if the JTAG interface is visible on the computer.

To carry on with debugging environment setup, proceed to section `jtag-debugging-run-openocd`.
