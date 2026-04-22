<!-- Source: _sources/get-started/start-project.rst.txt (ESP-IDF v6.0 documentation) -->

{IDF_TARGET_FEATURES:default="\[NEEDS TO BE UPDATED\]", esp32="WiFi/BT/BLE, silicon revision 1, 2 MB external flash", esp32s2="WiFi, silicon revision 0, 2 MB external flash", esp32s3="This is esp32s3 chip with 2 CPU core(s), WiFi/BLE, silicon revision 0, 2 MB external flash", esp32c2="WiFi/BLE, silicon revision 0, 2 MB embedded flash", esp32c3="WiFi/BLE, silicon revision 0, 2 MB external flash", esp32c6="WiFi/BLE, 802.15.4 (Zigbee/Thread), silicon revision v0.0, 2 MB external flash", esp32h2="BLE, 802.15.4 (Zigbee/Thread), silicon revision v0.1, 2 MB external flash", esp32p4="silicon revision v0.0, 2 MB external flash", esp32c5="WiFi/BLE, 802.15.4 (Zigbee/Thread), silicon revision v1.0, 2 MB external flash"}

{IDF_TARGET_HEAP_SIZE:default="\[NEEDS TO BE UPDATED\]", esp32="298968", esp32s2="253900", esp32s3="390684", esp32c2="203888", esp32c3="337332", esp32c6="473816", esp32h2="268256", esp32p4="618848", esp32c5="348724"}

# Build the Project

Build the project by running:

``` batch
idf.py build
```

This command compiles the application and all ESP-IDF components, then it generates the bootloader, partition table, and application binaries.

``` none
$ idf.py build
Running cmake in directory /path/to/hello_world/build
Executing "cmake -G Ninja --warn-uninitialized /path/to/hello_world"...
Warn about uninitialized values.
-- Found Git: /usr/bin/git (found version "2.17.0")
-- Building empty aws_iot component due to configuration
-- Component names: ...
-- Component paths: ...

... (more lines of build system output)

[527/527] Generating hello_world.bin
esptool v5.0.2

Project build complete. To flash, run:
  idf.py flash
or
  idf.py -p PORT flash
or
  esptool -p (PORT) -b 921600 write-flash --flash-mode dio --flash-size detect --flash-freq 40m 0x10000 build/hello_world.bin  build 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin
```

If there are no errors, the build finishes by generating the firmware binary .bin files.

# Flash onto the Device

To flash the binaries that you just built for the {IDF_TARGET_NAME} in the previous step, you need to run the following command:

``` bash
idf.py -p PORT flash
```

Replace `PORT` with your {IDF_TARGET_NAME} board's USB port name. If the `PORT` is not defined, the `idf.py` will try to connect automatically using the available USB ports.

For more information on `idf.py` arguments, see `idf.py`.

> **Note**
>
> Encountered Issues While Flashing? See the "Additional Tips" below. You can also refer to `flashing-troubleshooting` page or `establish-serial-connection` for more detailed information.

## Normal Operation

When flashing, you will see the output log similar to the following:

If there are no issues by the end of the flash process, the board will reboot and start up the "hello_world" application.

If you would like to use the Eclipse or VS Code IDE instead of running `idf.py`, check out [Eclipse Plugin](https://github.com/espressif/idf-eclipse-plugin/blob/master/README.md), [VSCode Extension](https://github.com/espressif/vscode-esp-idf-extension/blob/master/docs/tutorial/install.md).

# Monitor the Output

To check if "hello_world" is indeed running, type `idf.py -p PORT monitor` (Do not forget to replace PORT with your serial port name).

This command launches the `IDF Monitor <../api-guides/tools/idf-monitor>` application.

``` bash
$ idf.py -p <PORT> monitor
Running idf_monitor in directory [...]/esp/hello_world/build
Executing "python [...]/esp-idf/tools/idf_monitor.py -b 115200 [...]/esp/hello_world/build/hello_world.elf"...
--- idf_monitor on <PORT> 115200 ---
--- Quit: Ctrl+] | Menu: Ctrl+T | Help: Ctrl+T followed by Ctrl+H ---
ets Jun  8 2016 00:22:57

rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
ets Jun  8 2016 00:22:57
...
```

After startup and diagnostic logs scroll up, you should see "Hello world!" printed out by the application.

``` none
...
Hello world!
Restarting in 10 seconds...
This is {IDF_TARGET_PATH_NAME} chip with {IDF_TARGET_SOC_CPU_CORES_NUM} CPU core(s), {IDF_TARGET_FEATURES}
Minimum free heap size: {IDF_TARGET_HEAP_SIZE} bytes
Restarting in 9 seconds...
Restarting in 8 seconds...
Restarting in 7 seconds...
```

To exit IDF monitor use the shortcut `Ctrl+]`.

<!-- Only for: esp32 or esp32c2 -->
If IDF monitor fails shortly after the upload, or, if instead of the messages above, you see random garbage similar to what is given below, your board is likely using a 26 MHz crystal. Most development board designs use 40 MHz, so ESP-IDF uses this frequency as a default value.

<figure>
<img src="../../_static/get-started-garbled-output.png" class="align-center" alt="../../_static/get-started-garbled-output.png" />
</figure>

If you have such a problem, do the following:

1.  Exit the monitor.
2.  Go back to `menuconfig`.
3.  Go to `Component config` --\> `Hardware Settings` --\> `Main XTAL Config` --\> `Main XTAL frequency`, then change `CONFIG_XTAL_FREQ` to 26 MHz.
4.  After that, `build and flash` the application again.

In the current version of ESP-IDF, main XTAL frequencies supported by {IDF_TARGET_NAME} are as follows:

SOC_XTAL_SUPPORT_26M  
- 26 MHz

SOC_XTAL_SUPPORT_32M  
- 32 MHz

SOC_XTAL_SUPPORT_40M  
- 40 MHz

> **Note**
>
> See also:

- `IDF Monitor <../api-guides/tools/idf-monitor>` for handy shortcuts and more details on using IDF monitor.
- `idf.py` for a full reference of `idf.py` commands and options.

**That is all that you need to get started with {IDF_TARGET_NAME}!**

Now you are ready to try some other `examples`, or go straight to developing your own applications.

> **Important**
>
> # Additional Tips

## Permission Denied Issue

With some Linux distributions, you may get the error message similar to `Could not open port <PORT>: Permission denied: '<PORT>'` when flashing the {IDF_TARGET_NAME}. `This can be solved by adding the current user to the specific group <linux-dialout-group>`, such as `dialout` or `uucp` group.

## Python Compatibility

ESP-IDF supports Python 3.10 or newer. It is recommended to upgrade your operating system to a recent version satisfying this requirement. Other options include the installation of Python from [sources](https://www.python.org/downloads/) or the use of a Python version management system such as [pyenv](https://github.com/pyenv/pyenv).

<!-- Only for: esp32 or esp32s2 or esp32s3 -->
## Start with Board Support Package

To speed up prototyping on some development boards, you can use [Board Support Packages (BSPs)](https://github.com/espressif/esp-bsp), which makes initialization of a particular board as easy as few function calls.

A BSP typically supports all of the hardware components provided on development board. Apart from the pinout definition and initialization functions, a BSP ships with drivers for the external components such as sensors, displays, audio codecs etc.

The BSPs are distributed via `IDF Component Manager </api-guides/tools/idf-component-manager>`, so they can be found in [ESP Component Registry](https://components.espressif.com).

esp32

**Here is an example of how to add ESP-WROVER-KIT BSP to your project:**

``` bash
idf.py add-dependency esp_wrover_kit
```

<!-- Only for: esp32s2 -->
**Here is an example of how to add ESP32-S2-Kaluga-Kit BSP to your project:**

``` bash
idf.py add-dependency esp32_s2_kaluga_kit
```

<!-- Only for: esp32s3 -->
**Here is an example of how to add ESP-BOX BSP to your project:**

``` bash
idf.py add-dependency esp-box
```

More examples of BSP usage can be found in [BSP examples folder](https://github.com/espressif/esp-bsp/tree/master/examples).

## Flash Erase

Erasing the flash is also possible. To erase the entire flash memory you can run the following command:

``` bash
idf.py -p PORT erase-flash
```

For erasing the OTA data, if present, you can run this command:

``` bash
idf.py -p PORT erase-otadata
```

The flash erase command can take a while to be done. Do not disconnect your device while the flash erasing is in progress.

# Related Documents

- `establish-serial-connection`
- `../api-guides/tools/idf-monitor`

establish-serial-connection flashing-troubleshooting

